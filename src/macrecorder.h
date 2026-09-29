#pragma once

#include <QObject>
#include <QElapsedTimer>
#include <QTimer>
#include <QString>
#include "videorecorder.h"

// ScreenCaptureKit can exclude Snitt's own windows from the recorded image.
// FFmpeg's AVFoundation display input cannot, even with NSWindowSharingNone.
class MacRecorder final : public QObject {
    Q_OBJECT
public:
    explicit MacRecorder(QObject *parent = nullptr);
    ~MacRecorder() override;
    bool active() const { return m_active; }
    bool starting() const { return m_active && !m_ready; }
    bool finishing() const { return m_finishing; }
    int elapsed() const { return m_elapsed; }
    QString path() const { return m_output; }
    void start(const recording::Source &source, const QString &saveRoot);
    void finish();
    void cancel();

    // Called only by the ScreenCaptureKit delegate on the Qt main thread.
    void nativePrepared(quint64 generation, void *content, const QString &error);
    void nativeCaptureStarted(quint64 generation, const QString &error);
    void nativeRecordingStarted(quint64 generation);
    void nativeFrame(quint64 generation);
    void nativeRecordingFinished(quint64 generation);
    void nativeStopped(quint64 generation, const QString &error);
    void nativeFailed(quint64 generation, const QString &error);
    void nativeUserStopped(quint64 generation);
signals:
    void changed();
    void processStarted(); // Native stream start requested (no process on macOS).
    void ready();
    void elapsedChanged();
    void saved(const QString &path);
    void canceled();
    void error(const QString &message);
private:
    friend class RecordingTests;
    void maybeReady();
    void maybeComplete();
    void fail(const QString &message);
    bool discardOutput(const QString &path);
    void reset();
    bool current(quint64 generation) const { return m_active && generation == m_generation; }
    recording::Source m_source;
    void *m_session = nullptr;
    QTimer m_timeout;
    QTimer m_clock;
    QElapsedTimer m_elapsedClock;
    QString m_output;
    quint64 m_generation = 0;
    int m_elapsed = 0;
    bool m_active = false;
    bool m_ready = false;
    bool m_finishing = false;
    bool m_canceling = false;
    bool m_recordingStarted = false;
    bool m_firstFrame = false;
    bool m_recordingFinished = false;
    bool m_stopped = false;
};
