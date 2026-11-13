#pragma once
#include <QObject>
#include <QImage>
#include <QString>
#include <memory>
class QWidget;
class QEvent;

// Live sampled glass based on REWORK Music PRO: all captured pixels stay local.
// While live, Windows excludes the window from screenshots and screen sharing.
class MusicGlass final : public QObject {
    Q_OBJECT
public:
    explicit MusicGlass(QWidget *window);
    ~MusicGlass() override;
    bool start();
    void stop();
    bool isActive() const;
    bool isReady() const;
    bool isFrozen() const;
    QString errorString() const;
    // Pauses sampling, freezes the most recent frame, and allows the window to
    // appear in screenshots. Returns false if no frame is available.
    bool freezeForScreenshot(bool frozen);
    // Nested popup/dialog suppression; resume is delayed until the popup hides.
    void suspendCapture(bool suspend);
    // Returns a copy-on-write frame only when it matches the current geometry.
    QImage latestFrame() const;
    static QImage filterFrame(const QImage &input);
signals:
    void frameAvailable();
    void readyChanged(bool ready);
    void errorOccurred(const QString &message);
protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
private:
    void updateRequest();
    void reportError(const QString &error);
    struct Impl;
    std::unique_ptr<Impl> d;
};
