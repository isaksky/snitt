#pragma once

#include <QObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QUrl>
#include <QStringList>
#include <memory>

#ifdef Q_OS_MACOS
class MacClipExport;
#endif

// A finalized recording remains untouched until a reviewed export is validated.
class TrimSession : public QObject {
    Q_OBJECT
    Q_PROPERTY(QUrl source READ source NOTIFY changed)
    Q_PROPERTY(QString path READ path NOTIFY changed)
    Q_PROPERTY(qint64 duration READ duration NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(double progress READ progress NOTIFY changed)
    Q_PROPERTY(QString problem READ problem NOTIFY changed)
    Q_PROPERTY(QStringList thumbnails READ thumbnails NOTIFY changed)
public:
    explicit TrimSession(QObject *parent = nullptr);
    ~TrimSession() override;
    QUrl source() const { return QUrl::fromLocalFile(m_path); }
    QString path() const { return m_path; }
    qint64 duration() const { return m_duration; }
    bool busy() const { return m_busy; }
    double progress() const { return m_progress; }
    QString problem() const { return m_problem; }
    QStringList thumbnails() const { return m_thumbnails; }
    void open(const QString &path);
    Q_INVOKABLE void setDuration(qint64 milliseconds);
    Q_INVOKABLE void setThumbnailWindow(qint64 startMs, qint64 endMs);
    Q_INVOKABLE void keepOriginal();
    Q_INVOKABLE void exportRange(qint64 startMs, qint64 endMs);
    Q_INVOKABLE void cancelExport();
signals:
    void changed();
    void finalized(const QString &path);
protected:
    virtual bool replaceFile(const QString &temporary, const QString &original, QString *problem);
private:
    void generateThumbnails();
    void nextWindowsThumbnail();
    bool stopThumbnails();
    void resumeThumbnails();
    void exportFinished(bool success, const QString &detail);
    void validateOutput();
    void replaceOutput();
    void fail(const QString &message);
    void removeTemporaryOutput();
    void clear();
    QString m_path;
    QString m_tempOutput;
    QString m_problem;
    QStringList m_thumbnails;
    std::unique_ptr<QTemporaryDir> m_thumbDir;
    QProcess m_thumbProcess;
    QProcess m_exportProcess;
    QProcess m_probeProcess;
    QByteArray m_progressBuffer;
    qint64 m_duration = 0;
    qint64 m_startMs = 0;
    qint64 m_endMs = 0;
    double m_progress = 0; // -1 while native export has no measured fraction.
    bool m_busy = false;
    bool m_canceling = false;
    int m_nextThumb = 0;
    qint64 m_thumbnailStartMs = 0;
    qint64 m_thumbnailEndMs = 0;
    quint64 m_generation = 0;
#ifdef Q_OS_MACOS
    std::unique_ptr<MacClipExport> m_macExport;
#endif
};
