#pragma once

#include <QDateTime>
#include <QDir>

namespace savepaths {
inline QString datedMediaDirectory(const QString &saveRoot, const QDateTime &allocatedAt) {
    const QDate date = allocatedAt.toLocalTime().date();
    const QString year = QString::number(date.year()).rightJustified(4, QLatin1Char('0'));
    return QDir(saveRoot).filePath(QStringLiteral("%1/%2").arg(year).arg(date.month()));
}
}
