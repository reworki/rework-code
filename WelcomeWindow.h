#pragma once

#include <QDialog>
#include <QStringList>

class QLabel;
class QLineEdit;
class QListWidget;

// The caller owns navigation, validation, and persistence of recent paths.
class WelcomeWindow : public QDialog {
    Q_OBJECT
public:
    explicit WelcomeWindow(QWidget *parent = nullptr);
    void setRecentProjects(const QStringList &paths);
    void refreshRecent();

signals:
    void createProjectRequested();
    void openProjectRequested();
    void continueRequested();
    void projectSelected(const QString &path);

private:
    void updateFilter(const QString &query);
    QLineEdit *m_search = nullptr;
    QListWidget *m_recent = nullptr;
    QLabel *m_empty = nullptr;
};

