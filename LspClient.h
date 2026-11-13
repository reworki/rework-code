#pragma once

#include <QObject>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QProcess>
#include <QString>

// Small asynchronous LSP client for one clangd process and workspace.
class LspClient : public QObject {
    Q_OBJECT
public:
    explicit LspClient(QObject *parent = nullptr);
    ~LspClient() override;

    // executable may be an absolute path or a command on PATH. Empty selects clangd.
    bool start(const QString &rootPath, const QString &executable = QString());
    void stop();
    bool isReady() const { return m_ready; }

    void openDocument(const QString &path, const QString &text);
    void changeDocument(const QString &path, const QString &text);
    void closeDocument(const QString &path);
    // Returns -1 if the server or document is not ready. Coordinates are zero-based UTF-16.
    int requestCompletion(const QString &path, int line, int character);

signals:
    void statusChanged(const QString &status);
    void initialized();
    void diagnosticsReceived(const QString &path, const QJsonArray &diagnostics);
    // result is the LSP CompletionList object, array of CompletionItem, or null on error.
    void completionReceived(int requestId, const QJsonValue &result);

private:
    struct Document { QString path; QString text; int version = 1; bool sent = false; };
    static QString canonicalPath(const QString &path);
    static QString documentUri(const QString &path);
    void sendObject(const QJsonObject &message);
    void sendNotification(const QString &method, const QJsonObject &params);
    int sendRequest(const QString &method, const QJsonObject &params);
    void sendOpen(Document &document);
    void readOutput();
    void handleMessage(const QJsonObject &message);
    void setNotReady(const QString &status);

    QProcess m_process;
    QByteArray m_buffer;
    QHash<QString, Document> m_documents;
    QHash<int, QString> m_pendingRequests;
    QString m_root;
    bool m_ready = false;
    bool m_stopping = false;
    int m_nextId = 1;
};
