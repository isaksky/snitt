#pragma once
#include "../../src/mp4duration.h"
#include "../../src/recordingfile.h"
#include "../../src/videorecorder.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QVariantMap>
#include <memory>

// Mechanical Qt/file boundary. No trim ranges, scheduling, validation policy,
// retries or operation state live here; those belong to snitt.trim.
class MediaJobs final : public QObject {
    Q_OBJECT
public:
    explicit MediaJobs(QObject *parent) : QObject(parent) {}
    ~MediaJobs() override {
        for (auto *process : m_jobs) { process->disconnect(this); process->kill(); process->waitForFinished(-1); }
        for (const QString &path : m_temporary) QFile::remove(path);
    }
    Q_INVOKABLE QString tool(const QString &name) const {
        const QString bundled = QCoreApplication::applicationDirPath() + "/" + name + "-media";
        return QFileInfo(bundled).isExecutable() ? bundled : recording::toolPath(name);
    }
    Q_INVOKABLE bool start(const QString &channel, int generation, const QString &program, const QStringList &args) {
        if (m_jobs.contains(channel)) return false;
        auto *process = new QProcess(this);
        m_jobs.insert(channel, process);
        connect(process, &QProcess::readyReadStandardOutput, this, [this, process, channel, generation] {
            const QString chunk = QString::fromUtf8(process->readAllStandardOutput());
            process->setProperty("output", process->property("output").toString() + chunk);
            emit output(channel, generation, chunk);
        });
        connect(process, &QProcess::finished, this, [this, process, channel, generation](int code, QProcess::ExitStatus status) {
            const QString out = process->property("output").toString() + QString::fromUtf8(process->readAllStandardOutput());
            const QString error = QString::fromUtf8(process->readAllStandardError());
            m_jobs.remove(channel);
            process->deleteLater();
            emit finished(channel, generation, status == QProcess::NormalExit && code == 0, out, error);
        });
        connect(process, &QProcess::errorOccurred, this, [this, process, channel, generation](QProcess::ProcessError error) {
            if (error != QProcess::FailedToStart) return;
            m_jobs.remove(channel);
            const QString detail = process->errorString();
            process->deleteLater();
            emit finished(channel, generation, false, "", detail);
        });
        process->start(program, args);
        return true;
    }
    Q_INVOKABLE bool stop(const QString &channel) {
        auto *process = m_jobs.value(channel);
        if (!process) return true;
        process->disconnect(this);
        process->kill();
        // Do not release custody or remove output while a writer still exists.
        if (!process->waitForFinished(5000) && process->state() != QProcess::NotRunning) return false;
        m_jobs.remove(channel);
        delete process;
        return true;
    }
    Q_INVOKABLE int activeJobs() const { return m_jobs.size(); }
    Q_INVOKABLE qint64 size(const QString &path) const { return QFileInfo(path).isFile() ? QFileInfo(path).size() : -1; }
    Q_INVOKABLE bool remove(const QString &path) { const bool ok = !QFileInfo::exists(path) || QFile::remove(path); if (ok) m_temporary.removeAll(path); return ok; }
    Q_INVOKABLE QString temporary(const QString &original) {
        const QString path = QFileInfo(original).dir().filePath(".snitt-trim-" + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".mp4");
        m_temporary.append(path);
        return path;
    }
    Q_INVOKABLE QString thumbnailDirectory() { m_thumbnails.reset(new QTemporaryDir); return m_thumbnails->isValid() ? m_thumbnails->path() : QString(); }
    Q_INVOKABLE void clearThumbnails() { m_thumbnails.reset(); }
    Q_INVOKABLE QString localUrl(const QString &path) const { return QUrl::fromLocalFile(path).toString(); }
    Q_INVOKABLE QVariantMap probe(const QString &json) const {
        const auto root = QJsonDocument::fromJson(json.toUtf8()).object();
        const auto streams = root.value("streams").toArray();
        const auto stream = streams.isEmpty() ? QJsonObject() : streams.first().toObject();
        QString duration = stream.value("duration").toString();
        if (duration.isEmpty()) duration = root.value("format").toObject().value("duration").toString();
        return {{"duration", qRound64(duration.toDouble() * 1000)}, {"width", stream.value("width").toInt()}, {"height", stream.value("height").toInt()}};
    }
    Q_INVOKABLE bool limit(const QString &path, qint64 ms) { return limitMp4Duration(path, ms); }
    Q_INVOKABLE QString replace(const QString &temporary, const QString &original) {
        QString problem;
        if (replaceRecordingFile(temporary, original, &problem)) { m_temporary.removeAll(temporary); return {}; }
        return problem.isEmpty() ? QStringLiteral("The full recording remains unchanged.") : problem;
    }
    Q_INVOKABLE void notify() { emit changed(); }
    Q_INVOKABLE void finalize(const QString &path) { emit finalized(path); }
signals:
    void output(const QString &channel, int generation, const QString &chunk);
    void finished(const QString &channel, int generation, bool success, const QString &output, const QString &error);
    void changed();
    void finalized(const QString &path);
private:
    QHash<QString, QProcess *> m_jobs;
    QStringList m_temporary;
    std::unique_ptr<QTemporaryDir> m_thumbnails;
};
