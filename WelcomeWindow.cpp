#include "WelcomeWindow.h"

#include <QAbstractItemView>
#include <QDir>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QShortcut>
#include <QSettings>
#include <QStyle>
#include <QVBoxLayout>

WelcomeWindow::WelcomeWindow(QWidget *parent) : QDialog(parent) {
    setObjectName("welcomeWindow");
    setWindowTitle(QStringLiteral("REWORK Code — Начало работы"));
    resize(1060, 690);
    setMinimumSize(800, 540);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(32, 28, 32, 24);
    outer->setSpacing(0);

    auto *brand = new QLabel(QStringLiteral("REWORK <span style='color:#a5b5ff'>CODE</span>"));
    brand->setObjectName("brand");
    brand->setTextFormat(Qt::RichText);
    outer->addWidget(brand);
    auto *tag = new QLabel(QStringLiteral("C++ WORKSPACE  /  ТВОРИ СВОЙ КОД"));
    tag->setObjectName("eyebrow");
    outer->addWidget(tag);
    outer->addSpacing(30);

    auto *headline = new QLabel(QStringLiteral("Начать работу"));
    headline->setObjectName("headline");
    outer->addWidget(headline);
    auto *subtitle = new QLabel(QStringLiteral("Продолжи с того места, где остановился, или создай что-то новое."));
    subtitle->setObjectName("subtitle");
    outer->addWidget(subtitle);
    outer->addSpacing(22);

    auto *columns = new QHBoxLayout;
    columns->setSpacing(24);
    auto *recentColumn = new QVBoxLayout;
    recentColumn->setSpacing(10);
    m_search = new QLineEdit;
    m_search->setObjectName("recentSearch");
    m_search->setPlaceholderText(QStringLiteral("Поиск среди недавних проектов"));
    m_search->setClearButtonEnabled(true);
    m_search->setAccessibleName(QStringLiteral("Поиск среди недавних проектов"));
    recentColumn->addWidget(m_search);
    auto *recentTitle = new QLabel(QStringLiteral("НЕДАВНИЕ ПРОЕКТЫ"));
    recentTitle->setObjectName("sectionTitle");
    recentColumn->addWidget(recentTitle);

    auto *recentContainer = new QFrame;
    recentContainer->setObjectName("recentContainer");
    auto *listLayout = new QVBoxLayout(recentContainer);
    listLayout->setContentsMargins(3,3,3,3);
    listLayout->setSpacing(0);
    m_recent = new QListWidget;
    m_recent->setObjectName("recentList");
    m_recent->setFrameShape(QFrame::NoFrame);
    m_recent->setFocusPolicy(Qt::StrongFocus);
    m_recent->setSelectionMode(QAbstractItemView::SingleSelection);
    m_recent->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_recent->setAccessibleName(QStringLiteral("Недавние проекты"));
    listLayout->addWidget(m_recent, 1);
    m_empty = new QLabel(QStringLiteral("Здесь появятся открытые проекты"));
    m_empty->setObjectName("emptyMessage");
    m_empty->setAlignment(Qt::AlignCenter);
    m_empty->setWordWrap(true);
    listLayout->addWidget(m_empty);
    recentColumn->addWidget(recentContainer, 1);
    columns->addLayout(recentColumn, 1);

    auto *actions = new QFrame;
    actions->setObjectName("actionsPanel");
    actions->setMinimumWidth(238);
    actions->setMaximumWidth(270);
    auto *actionsLayout = new QVBoxLayout(actions);
    actionsLayout->setContentsMargins(20, 20, 20, 20);
    actionsLayout->setSpacing(10);
    auto *actionsTitle = new QLabel(QStringLiteral("ПРОСТРАНСТВО"));
    actionsTitle->setObjectName("sectionTitle");
    actionsLayout->addWidget(actionsTitle);
    actionsLayout->addSpacing(7);
    auto *create = new QPushButton(QStringLiteral("＋   Создать проект"));
    create->setObjectName("primaryAction");
    create->setMinimumHeight(48);
    auto *open = new QPushButton(QStringLiteral("▱   Открыть папку"));
    open->setObjectName("secondaryAction");
    open->setMinimumHeight(48);
    actionsLayout->addWidget(create);
    actionsLayout->addWidget(open);
    auto *actionsHint = new QLabel(QStringLiteral("Открой папку проекта, чтобы увидеть файлы и подключить анализ C++."));
    actionsHint->setObjectName("actionsHint");
    actionsHint->setWordWrap(true);
    actionsLayout->addSpacing(8);
    actionsLayout->addWidget(actionsHint);
    actionsLayout->addStretch();
    columns->addWidget(actions);
    outer->addLayout(columns, 1);
    outer->addSpacing(22);

    auto *footer = new QHBoxLayout;
    auto *version = new QLabel(QStringLiteral("REWORK LABS   ·   C++ EDITOR"));
    version->setObjectName("eyebrow");
    footer->addWidget(version);
    footer->addStretch();
    auto *withoutCode = new QPushButton(QStringLiteral("Продолжить без проекта  →"));
    withoutCode->setObjectName("continueAction");
    withoutCode->setMinimumHeight(40);
    footer->addWidget(withoutCode);
    outer->addLayout(footer);

    setStyleSheet(QStringLiteral(R"(
        QDialog#welcomeWindow { background: #101521; color: #e6ecff; font-family: 'Segoe UI'; }
        QLabel { background: transparent; color: #e6ecff; }
        QLabel#brand { font-size: 23px; font-weight: 800; letter-spacing: 2px; }
        QLabel#eyebrow { color: #73839e; font-size: 10px; font-weight: 700; letter-spacing: 1.5px; }
        QLabel#headline { color: #f0f3ff; font-size: 25px; font-weight: 700; }
        QLabel#subtitle { color: #96a5bf; font-size: 13px; margin-top: 5px; }
        QLabel#sectionTitle { color: #8595b1; font-size: 10px; font-weight: 700; letter-spacing: 1.4px; }
        QLabel#emptyMessage { color: #7889a4; font-size: 13px; padding: 28px 10px; }
        QLabel#actionsHint { color: #8696ae; font-size: 12px; line-height: 1.4; }
        QLineEdit#recentSearch { background: #1a2131; border: 1px solid #35435b; border-radius: 8px; padding: 10px 12px; color: #e9efff; selection-background-color: #596ba7; }
        QLineEdit#recentSearch:focus { border: 1px solid #9aaeff; }
        QFrame#recentContainer, QFrame#actionsPanel { background: #171e2c; border: 1px solid #2b364a; border-radius: 10px; }
        QListWidget#recentList { background: transparent; outline: 0; border: 0; padding: 4px; }
        QListWidget#recentList::item { border-radius: 6px; padding: 2px; }
        QListWidget#recentList::item:hover { background: #263146; }
        QListWidget#recentList::item:selected { background: #303d58; }
        QLabel#projectName { color: #e5ebfa; font-size: 13px; font-weight: 600; }
        QLabel#projectPath { color: #899ab4; font-size: 11px; }
        QLabel#projectIcon { color: #a9bbff; font-size: 18px; font-weight: 600; }
        QPushButton { text-align: left; border-radius: 7px; padding: 10px 13px; font-size: 13px; font-weight: 600; }
        QPushButton#primaryAction { background: #667ed4; color: white; border: 1px solid #879aeb; }
        QPushButton#primaryAction:hover { background: #7b91e4; }
        QPushButton#secondaryAction { background: #222c3f; color: #e9efff; border: 1px solid #39475e; }
        QPushButton#secondaryAction:hover { background: #303c54; border-color: #758dc7; }
        QPushButton#continueAction { background: #222c3f; color: #dce6fb; border: 1px solid #39475e; }
        QPushButton#continueAction:hover { background: #303c54; border-color: #758dc7; }
        QScrollBar:vertical { width: 9px; background: transparent; margin: 3px; }
        QScrollBar::handle:vertical { background: #52617d; border-radius: 3px; min-height: 26px; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }
    )"));

    connect(create, &QPushButton::clicked, this, &WelcomeWindow::createProjectRequested);
    connect(open, &QPushButton::clicked, this, &WelcomeWindow::openProjectRequested);
    connect(withoutCode, &QPushButton::clicked, this, &WelcomeWindow::continueRequested);
    connect(m_search, &QLineEdit::textChanged, this, &WelcomeWindow::updateFilter);
    connect(m_recent, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) {
        if (item) emit projectSelected(item->data(Qt::UserRole).toString());
    });
    auto *searchShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+F")), this);
    connect(searchShortcut, &QShortcut::activated, m_search, qOverload<>(&QLineEdit::setFocus));
    refreshRecent();
}

void WelcomeWindow::refreshRecent() {
    QSettings settings(QStringLiteral("REWORK"), QStringLiteral("REWORK Code"));
    setRecentProjects(settings.value(QStringLiteral("recentProjects")).toStringList());
}

void WelcomeWindow::setRecentProjects(const QStringList &paths) {
    const QString filter = m_search->text();
    m_recent->clear();
    QStringList seen;
    for (const QString &rawPath : paths) {
        const QString path = QDir::cleanPath(rawPath.trimmed());
        if (rawPath.trimmed().isEmpty()) continue;
        bool duplicate = false;
        for (const QString &existing : seen)
            if (existing.compare(path, Qt::CaseInsensitive) == 0) { duplicate = true; break; }
        if (duplicate) continue;
        seen.append(path);
        const QString name = QFileInfo(path).fileName().isEmpty() ? QDir::toNativeSeparators(path) : QFileInfo(path).fileName();
        auto *item = new QListWidgetItem(m_recent);
        item->setData(Qt::UserRole, path);
        item->setToolTip(QDir::toNativeSeparators(path));
        item->setSizeHint(QSize(0, 62));
        // Provide text for accessibility even when a custom visual row is used.
        item->setText(name + QStringLiteral(" — ") + QDir::toNativeSeparators(path));
        auto *row = new QWidget;
        row->setAttribute(Qt::WA_TransparentForMouseEvents);
        row->setStyleSheet(QStringLiteral("background: transparent;"));
        auto *layout = new QHBoxLayout(row);
        layout->setContentsMargins(12, 5, 12, 5);
        layout->setSpacing(14);
        auto *icon = new QLabel(QStringLiteral("▱"));
        icon->setObjectName("projectIcon");
        icon->setFixedWidth(25);
        auto *labels = new QVBoxLayout;
        labels->setContentsMargins(0,0,0,0); labels->setSpacing(2);
        auto *title = new QLabel(name); title->setObjectName("projectName");
        auto *where = new QLabel(QDir::toNativeSeparators(path)); where->setObjectName("projectPath");
        title->setTextInteractionFlags(Qt::NoTextInteraction);
        where->setTextInteractionFlags(Qt::NoTextInteraction);
        labels->addWidget(title); labels->addWidget(where);
        layout->addWidget(icon); layout->addLayout(labels, 1);
        m_recent->setItemWidget(item,row);
    }
    updateFilter(filter);
}

void WelcomeWindow::updateFilter(const QString &query) {
    int visible = 0;
    for (int i = 0; i < m_recent->count(); ++i) {
        auto *item = m_recent->item(i);
        const bool match = item->text().contains(query.trimmed(), Qt::CaseInsensitive);
        item->setHidden(!match);
        if (match) ++visible;
    }
    m_empty->setText(query.trimmed().isEmpty() ? QStringLiteral("Здесь появятся открытые проекты") : QStringLiteral("Проекты не найдены"));
    m_empty->setVisible(visible == 0);
    m_recent->setVisible(visible > 0);
}

