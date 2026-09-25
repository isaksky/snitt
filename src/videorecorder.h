#pragma once

#include <QObject>
#include <QProcess>
#include <QRect>
#include <QTimer>

namespace recording {
enum class Platform { Mac, Windows };
struct Source {
    Platform platform = Platform::Mac;
    // Windows uses physical desktop pixels; Mac crops relative to its display.
    QRect pixelRegion;
    QRectF relativeRegion;
    int screenIndex = 0;
};
QString toolPath(const QString &tool);
QStringList arguments(const Source &source, const QString &output);
}

class VideoRecorder : public QObject {
    Q_OBJECT
public:
    explicit VideoRecorder(QObject *parent = nullptr);
    ~VideoRecorder() override;
    bool active() const { return m_active; }
    bool starting() const { return m_active && !m_firstFrame; }
    bool finishing() const { return m_finishing; }
    int elapsed() const { return m_elapsed; }
    QString path() const { return m_output; }
    void start(const recording::Source &source);
    void finish();
    void cancel();
signals:
    void changed();
    void elapsedChanged();
    void saved(const QString &path);
    void canceled();
    void error(const QString &message);
private:
    void consumeProgress();
    void complete(int code, QProcess::ExitStatus status);
    void fail(const QString &message);
    void reset();
    QProcess m_process;
    QTimer m_timeout;
    QString m_output;
    QByteArray m_stderr;
    QByteArray m_progress;
    bool m_active = false;
    bool m_firstFrame = false;
    bool m_finishing = false;
    bool m_canceling = false;
    int m_elapsed = 0;
};
