#pragma once

#include <QImage>
#include <QDateTime>
#include <QString>

namespace screenshots {
struct PngResult {
    enum Status { Saved, Collision, Error } status;
    QString error;
};
// Atomically reserve a single candidate and encode it. The caller chooses its path.
PngResult writePngExclusive(const QImage &image, const QString &path);
// picturesDirectory is the OS-resolved Pictures folder, or a test directory.
// Returns the unique PNG path on success, or an empty string with an error.
QString savePng(const QImage &image, const QString &picturesDirectory, QString *error);
QString savePng(const QImage &image, const QString &picturesDirectory, QString *error,
                const QDateTime &saveTime);
}
