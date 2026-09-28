#pragma once

#include <QString>
#include <QtGlobal>

// Limit the presentation timeline of a temporary, non-fragmented FFmpeg MP4.
// Encoded samples and their decode timestamps remain unchanged.
bool limitMp4Duration(const QString &path, qint64 durationMs);
