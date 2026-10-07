#pragma once
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUuid>
#include <cerrno>
#include <cstdio>
#include <cstring>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

// Atomic replacement and platform recovery shared by both application entries.
inline bool replaceRecordingFile(const QString &temporary, const QString &original, QString *problem) {
#ifdef Q_OS_WIN
    const QString backup = QFileInfo(original).dir().filePath(
        QStringLiteral(".snitt-original-%1.mp4").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    const bool replaced = ReplaceFileW(reinterpret_cast<LPCWSTR>(original.utf16()),
                                       reinterpret_cast<LPCWSTR>(temporary.utf16()),
                                       reinterpret_cast<LPCWSTR>(backup.utf16()), 0, nullptr, nullptr) != FALSE;
    if (replaced) { QFile::remove(backup); return true; }
    if (QFileInfo(backup).isFile()) {
        // ReplaceFileW may move the original to backup before reporting failure.
        const bool restored = MoveFileExW(reinterpret_cast<LPCWSTR>(backup.utf16()),
                                          reinterpret_cast<LPCWSTR>(original.utf16()),
                                          MOVEFILE_REPLACE_EXISTING) != FALSE;
        if (!restored && CopyFileW(reinterpret_cast<LPCWSTR>(backup.utf16()),
                                   reinterpret_cast<LPCWSTR>(original.utf16()), FALSE)) QFile::remove(backup);
        if (!QFileInfo(original).isFile() && problem)
            *problem = QStringLiteral("The full recording is safe in backup file %1, but Windows could not restore its original path.")
                           .arg(QDir::toNativeSeparators(backup));
    }
    return false;
#else
    const QByteArray source = QFile::encodeName(temporary);
    const QByteArray destination = QFile::encodeName(original);
    if (std::rename(source.constData(), destination.constData()) == 0) return true;
    if (problem) *problem = QStringLiteral("The full recording remains unchanged (%1).")
                            .arg(QString::fromLocal8Bit(std::strerror(errno)));
    return false;
#endif
}
