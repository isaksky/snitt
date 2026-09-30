#pragma once

#include <QObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QUrl>
#include <QPointer>
#include <QList>
#include <QImage>
#include <QRect>
#include <QVariantList>
#include "videorecorder.h"
#include "trimsession.h"
#ifdef Q_OS_MACOS
#include "macrecorder.h"
using Recorder = MacRecorder;
#else
using Recorder = VideoRecorder;
#endif
class RegionSelector;
class AppSettings;

class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool capturing READ capturing NOTIFY capturingChanged)
    Q_PROPERTY(bool recording READ recording NOTIFY recordingChanged)
    Q_PROPERTY(bool startingRecording READ startingRecording NOTIFY recordingChanged)
    Q_PROPERTY(bool finishingRecording READ finishingRecording NOTIFY recordingChanged)
    Q_PROPERTY(int recordingElapsed READ recordingElapsed NOTIFY recordingElapsedChanged)
    Q_PROPERTY(QString recordingPath READ recordingPath NOTIFY recordingChanged)
    Q_PROPERTY(QRect recordingRegion READ recordingRegion NOTIFY recordingChanged)
    Q_PROPERTY(QRect recordingIndicatorGeometry READ recordingIndicatorGeometry NOTIFY recordingChanged)
    Q_PROPERTY(QRect recordingControlsGeometry READ recordingControlsGeometry NOTIFY recordingChanged)
    Q_PROPERTY(bool recordingProtectionPending READ recordingProtectionPending NOTIFY recordingChanged)
    Q_PROPERTY(QObject* trim READ trim CONSTANT)
public:
    explicit Backend(AppSettings *settings, QObject *parent = nullptr);
    ~Backend() override;
    bool capturing() const { return m_capturing; }
    bool recording() const { return m_pendingRecording || m_recorder.active(); }
    bool startingRecording() const { return m_pendingRecording || m_recorder.starting(); }
    bool recordingProtectionPending() const { return m_pendingRecording; }
    bool finishingRecording() const { return m_recorder.finishing(); }
    int recordingElapsed() const { return m_recorder.elapsed(); }
    QString recordingPath() const { return m_recorder.path(); }
    QObject *trim() { return &m_trim; }
    QRect recordingRegion() const { return m_recordingRegion; }
    QRect recordingIndicatorGeometry() const { return m_recordingIndicatorGeometry; }
    QRect recordingControlsGeometry() const { return m_recordingControlsGeometry; }
    static QRect indicatorGeometry(const QRect &region, const QRect &screen, const QRect &available);
    static QRect controlsGeometry(const QRect &available);
    static QRect editorGeometry(const QRect &available, const QSize &size);
    Q_INVOKABLE void positionEditorForCapture(QObject *editor);
    Q_INVOKABLE void capture(bool multiple = false, bool video = false);
    Q_INVOKABLE void finishRecording() { if (!m_pendingRecording) m_recorder.finish(); }
    Q_INVOKABLE void stopRecordingFromHotkey();
    Q_INVOKABLE void cancelRecording();
    Q_INVOKABLE bool beginProtectedRecording(QObject *indicator, QObject *controls, QObject *outline = nullptr);
    Q_INVOKABLE bool protectRecordingControls(QObject *window);
    Q_INVOKABLE bool revealFile(const QString &path) const { return recording::revealSavedFile(path); }
signals:
    void capturingChanged();
    void captured(const QUrl &file);
    void regionsCaptured(const QVariantList &images);
    void captureFinished(bool captured);
    void recordingChanged();
    void recordingProcessStarted();
    void recordingReady();
    void recordingElapsedChanged();
    void recordingSaved(const QString &path);
    void recordingCanceled();
    void error(const QString &message);
private:
    friend class EditorTests;
    bool windowsExclusionSupported() const;
    void finish(bool captured = false);
    void showSelectors();
    void captureNextScreen();
    void updateSelections();
    void removeSelection(int index);
    void startRecording(RegionSelector *selector, const QRectF &area);
    struct ScreenImage { QRect geometry; QImage image; };
    struct Selection { RegionSelector *owner; QRectF area; QImage image; };
    QList<ScreenImage> m_screens;
    AppSettings *m_settings = nullptr;
    QList<Selection> m_selections;
    int m_screenIndex = 0;
    bool m_multiple = false;
    bool m_video = false;
    Recorder m_recorder;
    TrimSession m_trim;
    bool m_pendingRecording = false;
    recording::Source m_pendingSource;
#ifdef Q_OS_WIN
    enum class ExclusionTestMode { Native, ForceFailure, ForceLegacyVersion };
    ExclusionTestMode m_exclusionTestMode = ExclusionTestMode::Native;
#endif
    QRect m_recordingRegion;
    QRect m_captureScreenGeometry;
    QRect m_recordingIndicatorGeometry;
    QRect m_recordingControlsGeometry;
    bool m_stopWhenReady = false;
    QTemporaryDir m_temp;
    QProcess m_process;
    bool m_capturing = false;
    QString m_output;
    QList<QPointer<RegionSelector>> m_selectors;
};
