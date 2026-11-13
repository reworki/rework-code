#include "LspClient.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QStandardPaths>
#include <QUrl>

LspClient::LspClient(QObject *parent) : QObject(parent) {
    connect(&m_process, &QProcess::readyReadStandardOutput, this, &LspClient::readOutput);
    // Drain stderr: verbose clangd logging must not fill the process pipe.
    connect(&m_process, &QProcess::readyReadStandardError, this, [this] {
        m_process.readAllStandardError();
    });
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        if (!m_stopping) setNotReady(tr("clangd: %1").arg(m_process.errorString()));
    });
    connect(&m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int, QProcess::ExitStatus) {
        if (!m_stopping) setNotReady(tr("clangd остановлен"));
    });
}

LspClient::~LspClient() {
    disconnect(&m_process, nullptr, this, nullptr);
    stop();
}

QString LspClient::canonicalPath(const QString &path) {
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

QString LspClient::documentUri(const QString &path) {
    return QUrl::fromLocalFile(canonicalPath(path)).toString(QUrl::FullyEncoded);
}

bool LspClient::start(const QString &rootPath, const QString &executable) {
    stop();
    const QString candidate = executable.isEmpty() ? QStringLiteral("clangd") : executable;
    QString binary = QStandardPaths::findExecutable(candidate);
    if (binary.isEmpty() && QFileInfo(candidate).isAbsolute() && QFileInfo::exists(candidate))
        binary = candidate;
    if (binary.isEmpty()) {
        emit statusChanged(tr("clangd не найден. Укажите путь к clangd или добавьте его в PATH."));
        return false;
    }
    m_root = canonicalPath(rootPath);
    m_buffer.clear();
    m_process.setProgram(binary);
    m_process.setArguments({QStringLiteral("--background-index")});
    m_process.setWorkingDirectory(m_root);
    m_process.start();
    // QProcess queues writes before the process starts; no synchronous wait is required.
    emit statusChanged(tr("Запуск clangd…"));
    const QJsonObject capabilities{
        {QStringLiteral("workspace"), QJsonObject{{QStringLiteral("configuration"), true}}},
        {QStringLiteral("textDocument"), QJsonObject{
            {QStringLiteral("completion"), QJsonObject{
                {QStringLiteral("completionItem"), QJsonObject{{QStringLiteral("snippetSupport"), false}}}}},
            {QStringLiteral("publishDiagnostics"), QJsonObject{}}}}};
    sendRequest(QStringLiteral("initialize"), QJsonObject{
        {QStringLiteral("processId"), static_cast<qint64>(QCoreApplication::applicationPid())},
        {QStringLiteral("rootUri"), documentUri(m_root)},
        {QStringLiteral("capabilities"), capabilities},
        {QStringLiteral("clientInfo"), QJsonObject{{QStringLiteral("name"), QStringLiteral("REWORK Code")}}}});
    return true;
}

void LspClient::stop() {
    m_ready = false;
    for (auto it = m_documents.begin(); it != m_documents.end(); ++it) it->sent = false;
    m_pendingRequests.clear();
    m_buffer.clear();
    if (m_process.state() != QProcess::NotRunning) {
        // A direct shutdown is appropriate here: this server owns no user data.
        m_stopping = true;
        m_process.kill();
        m_process.waitForFinished(1000);
        m_stopping = false;
    }
}

void LspClient::setNotReady(const QString &status) {
    m_ready = false;
    for (auto it = m_documents.begin(); it != m_documents.end(); ++it) it->sent = false;
    const auto pending = m_pendingRequests;
    m_pendingRequests.clear();
    for (auto it = pending.cbegin(); it != pending.cend(); ++it)
        if (it.value() == QLatin1String("textDocument/completion"))
            emit completionReceived(it.key(), QJsonValue(QJsonValue::Null));
    emit statusChanged(status);
}

void LspClient::sendObject(const QJsonObject &message) {
    if (m_process.state() == QProcess::NotRunning) return;
    const QByteArray json = QJsonDocument(message).toJson(QJsonDocument::Compact);
    m_process.write("Content-Length: " + QByteArray::number(json.size()) + "\r\n\r\n" + json);
}

void LspClient::sendNotification(const QString &method, const QJsonObject &params) {
    sendObject(QJsonObject{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                           {QStringLiteral("method"), method}, {QStringLiteral("params"), params}});
}

int LspClient::sendRequest(const QString &method, const QJsonObject &params) {
    const int id = m_nextId++;
    m_pendingRequests.insert(id, method);
    sendObject(QJsonObject{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                           {QStringLiteral("id"), id}, {QStringLiteral("method"), method},
                           {QStringLiteral("params"), params}});
    return id;
}

void LspClient::sendOpen(Document &document) {
    if (!m_ready || document.sent) return;
    sendNotification(QStringLiteral("textDocument/didOpen"), QJsonObject{
        {QStringLiteral("textDocument"), QJsonObject{
            {QStringLiteral("uri"), documentUri(document.path)},
            {QStringLiteral("languageId"), QStringLiteral("cpp")},
            {QStringLiteral("version"), document.version},
            {QStringLiteral("text"), document.text}}}});
    document.sent = true;
}

void LspClient::openDocument(const QString &path, const QString &text) {
    const QString key = canonicalPath(path);
    if (m_documents.contains(key)) {
        changeDocument(key, text);
        return;
    }
    Document document;
    document.path = key;
    document.text = text;
    auto it = m_documents.insert(key, document);
    sendOpen(it.value());
}

void LspClient::changeDocument(const QString &path, const QString &text) {
    const QString key = canonicalPath(path);
    auto it = m_documents.find(key);
    if (it == m_documents.end()) { openDocument(key, text); return; }
    Document &doc = it.value();
    if (doc.text == text) return;
    doc.text = text;
    ++doc.version;
    if (m_ready && doc.sent)
        sendNotification(QStringLiteral("textDocument/didChange"), QJsonObject{
            {QStringLiteral("textDocument"), QJsonObject{{QStringLiteral("uri"), documentUri(key)},
                                                         {QStringLiteral("version"), doc.version}}},
            {QStringLiteral("contentChanges"), QJsonArray{QJsonObject{{QStringLiteral("text"), text}}}}});
}

void LspClient::closeDocument(const QString &path) {
    const QString key = canonicalPath(path);
    auto it = m_documents.find(key);
    if (it == m_documents.end()) return;
    if (m_ready && it->sent)
        sendNotification(QStringLiteral("textDocument/didClose"), QJsonObject{
            {QStringLiteral("textDocument"), QJsonObject{{QStringLiteral("uri"), documentUri(key)}}}});
    m_documents.erase(it);
}

int LspClient::requestCompletion(const QString &path, int line, int character) {
    const QString key = canonicalPath(path);
    const auto it = m_documents.constFind(key);
    if (!m_ready || it == m_documents.cend() || !it->sent || line < 0 || character < 0) return -1;
    return sendRequest(QStringLiteral("textDocument/completion"), QJsonObject{
        {QStringLiteral("textDocument"), QJsonObject{{QStringLiteral("uri"), documentUri(key)}}},
        {QStringLiteral("position"), QJsonObject{{QStringLiteral("line"), line},
                                                 {QStringLiteral("character"), character}}}});
}

void LspClient::readOutput() {
    m_buffer += m_process.readAllStandardOutput();
    for (;;) {
        const qsizetype end = m_buffer.indexOf("\r\n\r\n");
        if (end < 0) {
            if (m_buffer.size() > 8192) { m_buffer.clear(); emit statusChanged(tr("Некорректный ответ clangd")); }
            return;
        }
        qint64 length = -1;
        const QList<QByteArray> headers = m_buffer.left(end).split('\n');
        for (QByteArray header : headers) {
            header = header.trimmed();
            if (header.toLower().startsWith("content-length:")) {
                bool ok = false;
                const qint64 parsed = header.mid(15).trimmed().toLongLong(&ok);
                if (ok) length = parsed;
            }
        }
        if (length < 0 || length > 64 * 1024 * 1024) {
            m_buffer.clear(); emit statusChanged(tr("Некорректный размер ответа clangd")); return;
        }
        if (m_buffer.size() - end - 4 < length) return;
        const QByteArray body = m_buffer.mid(end + 4, length);
        m_buffer.remove(0, end + 4 + length);
        QJsonParseError error;
        const QJsonDocument json = QJsonDocument::fromJson(body, &error);
        if (error.error == QJsonParseError::NoError && json.isObject()) handleMessage(json.object());
    }
}

void LspClient::handleMessage(const QJsonObject &message) {
    const QString method = message.value(QStringLiteral("method")).toString();
    const QJsonValue id = message.value(QStringLiteral("id"));
    if (!method.isEmpty()) {
        if (method == QLatin1String("textDocument/publishDiagnostics")) {
            const QJsonObject params = message.value(QStringLiteral("params")).toObject();
            const QString path = QUrl(params.value(QStringLiteral("uri")).toString()).toLocalFile();
            if (!path.isEmpty()) emit diagnosticsReceived(canonicalPath(path), params.value(QStringLiteral("diagnostics")).toArray());
        }
        if (!id.isUndefined()) {
            QJsonValue result(QJsonValue::Null);
            if (method == QLatin1String("workspace/configuration")) {
                QJsonArray config;
                const auto items = message.value(QStringLiteral("params")).toObject().value(QStringLiteral("items")).toArray();
                for (qsizetype i = 0; i < items.size(); ++i) config.append(QJsonValue(QJsonValue::Null));
                result = config;
            }
            sendObject(QJsonObject{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                                   {QStringLiteral("id"), id}, {QStringLiteral("result"), result}});
        }
        return;
    }
    if (!id.isDouble()) return;
    const int requestId = id.toInt();
    const QString request = m_pendingRequests.take(requestId);
    if (request == QLatin1String("initialize")) {
        if (message.contains(QStringLiteral("error"))) {
            setNotReady(tr("Не удалось инициализировать clangd"));
            return;
        }
        m_ready = true;
        sendNotification(QStringLiteral("initialized"), QJsonObject{});
        for (auto it = m_documents.begin(); it != m_documents.end(); ++it) sendOpen(it.value());
        emit statusChanged(tr("clangd подключён"));
        emit initialized();
    } else if (request == QLatin1String("textDocument/completion")) {
        emit completionReceived(requestId, message.contains(QStringLiteral("error"))
            ? QJsonValue(QJsonValue::Null) : message.value(QStringLiteral("result")));
    }
}
