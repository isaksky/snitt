#pragma once

#include <QImage>
#include <QString>

namespace screenshots {
// picturesDirectory is the OS-resolved Pictures folder, or a test directory.
// Returns the unique PNG path on success, or an empty string with an error.
QString savePng(const QImage &image, const QString &picturesDirectory, QString *error);
}
