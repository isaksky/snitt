#include "screenshotsave.h"
#include "savepaths.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUuid>

QString screenshots::savePng(const QImage &image, const QString &picturesDirectory, QString *error) {
    return savePng(image, picturesDirectory, error, QDateTime::currentDateTime());
}

QString screenshots::savePng(const QImage &image, const QString &picturesDirectory, QString *error,
                             const QDateTime &saveTime) {
    if (image.isNull() || picturesDirectory.isEmpty()) {
        if (error) *error = QStringLiteral("There is no screenshot or Pictures folder to save to.");
        return {};
    }
    const QDateTime localSaveTime = saveTime.toLocalTime();
    const QString directory = savepaths::datedMediaDirectory(picturesDirectory, localSaveTime);
    if (!QDir().mkpath(directory)) {
        if (error) *error = QStringLiteral("Could not create the screenshots folder: %1")
            .arg(QDir::toNativeSeparators(directory));
        return {};
    }
    for (int attempt = 0; attempt < 5; ++attempt) {
        const QString path = QDir(directory).filePath(QStringLiteral("snitt-%1-%2.png")
            .arg(localSaveTime.toString(QStringLiteral("yyyyMMdd-HHmmss-zzz")),
                 QUuid::createUuid().toString(QUuid::WithoutBraces).left(8)));
        QFile file(path);
        // NewOnly reserves the name atomically; a collision never overwrites a prior image.
        if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
            if (QFileInfo::exists(path)) continue;
            if (error) *error = QStringLiteral("Could not save the screenshot to %1: %2")
                .arg(QDir::toNativeSeparators(directory), file.errorString());
            return {};
        }
        const bool saved = image.save(&file, "PNG") && file.flush();
        const QString failure = file.errorString();
        file.close();
        if (!saved) {
            QFile::remove(path);
            if (error) *error = QStringLiteral("Could not finish saving the screenshot in %1: %2")
                .arg(QDir::toNativeSeparators(directory), failure);
            return {};
        }
        return path;
    }
    if (error) *error = QStringLiteral("Could not find a unique screenshot filename in %1.")
        .arg(QDir::toNativeSeparators(directory));
    return {};
}
