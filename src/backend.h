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
class RegionSelector;

class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool capturing READ capturing NOTIFY capturingChanged)
    Q_PROPERTY(bool recording READ recording NOTIFY recordingChanged)
    Q_PROPERTY(bool startingRecording READ startingRecording NOTIFY recordingChanged)
    Q_PROPERTY(bool finishingRecording READ finishingRecording NOTIFY recordingChanged)
    Q_PROPERTY(int recordingElapsed READ recordingElapsed NOTIFY recordingElapsedChanged)
    Q_PROPERTY(QString recordingPath READ recordingPath NOTIFY recordingChanged)
    Q_PROPERTY(QRect recordingRegion READ recordingRegion NOTIFY recordingChanged)
public:
    explicit Backend(QObject *parent = nullptr);
    ~Backend() override;
    bool capturing() const { return m_capturing; }
    bool recording() const { return m_recorder.active(); }
    bool startingRecording() const { return m_recorder.starting(); }
    bool finishingRecording() const { return m_recorder.finishing(); }
    int recordingElapsed() const { return m_recorder.elapsed(); }
    QString recordingPath() const { return m_recorder.path(); }
    QRect recordingRegion() const { return m_recordingRegion; }
    Q_INVOKABLE void capture(bool multiple = false, bool video = false);
    Q_INVOKABLE void finishRecording() { m_recorder.finish(); }
    Q_INVOKABLE void cancelRecording() { m_recorder.cancel(); }
    Q_INVOKABLE bool protectRecordingControls(QObject *window);
signals:
    void capturingChanged();
    void captured(const QUrl &file);
    void regionsCaptured(const QVariantList &images);
    void captureFinished(bool captured);
    void recordingChanged();
    void recordingElapsedChanged();
    void recordingSaved(const QString &path);
    void recordingCanceled();
    void error(const QString &message);
private:
    void finish(bool captured = false);
    void showSelectors();
    void captureNextScreen();
    void updateSelections();
    void removeSelection(int index);
    void startRecording(RegionSelector *selector, const QRectF &area);
    struct ScreenImage { QRect geometry; QImage image; };
    struct Selection { RegionSelector *owner; QRectF area; QImage image; };
    QList<ScreenImage> m_screens;
    QList<Selection> m_selections;
    int m_screenIndex = 0;
    bool m_multiple = false;
    bool m_video = false;
    VideoRecorder m_recorder;
    QRect m_recordingRegion;
    QTemporaryDir m_temp;
    QProcess m_process;
    bool m_capturing = false;
    QString m_output;
    QList<QPointer<RegionSelector>> m_selectors;
};
