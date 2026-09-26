#pragma once

#include <QString>
#include <functional>

class QObject;
class QTimer;

// Native AVFoundation helpers keep FFmpeg out of the macOS app runtime.
bool macProbeClip(const QString &path, qint64 *durationMs, int *width, int *height);
class MacClipExport {
public:
    MacClipExport();
    ~MacClipExport();
    void start(const QString &input, const QString &output, qint64 startMs, qint64 endMs,
               QObject *receiver, std::function<void(double)> progress,
               std::function<void(bool, const QString &)> done);
    void cancel();
private:
    void *m_session = nullptr;
    QTimer *m_poll = nullptr;
};
