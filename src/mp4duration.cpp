#include "mp4duration.h"

#include <QFile>
#include <QList>
#include <QtEndian>
#include <limits>

namespace {
struct Box { QByteArray type; qint64 begin = 0; qint64 end = 0; };
struct Patch { qint64 offset; int bytes; quint64 value; };

bool readNumber(QFile &file, qint64 offset, int bytes, quint64 &value) {
    if (!file.seek(offset)) return false;
    const QByteArray data = file.read(bytes);
    if (data.size() != bytes) return false;
    value = 0;
    for (unsigned char byte : data) value = (value << 8) | byte;
    return true;
}

bool children(QFile &file, qint64 begin, qint64 end, QList<Box> &boxes) {
    while (begin < end) {
        if (end - begin < 8 || !file.seek(begin)) return false;
        const QByteArray header = file.read(8);
        if (header.size() != 8) return false;
        quint64 size = qFromBigEndian<quint32>(header.constData());
        qint64 headerSize = 8;
        if (size == 1) {
            if (end - begin < 16 || !readNumber(file, begin + 8, 8, size)) return false;
            headerSize = 16;
        } else if (size == 0) {
            size = quint64(end - begin);
        }
        if (size < quint64(headerSize) || size > quint64(end - begin)) return false;
        boxes.append({header.mid(4, 4), begin + headerSize, begin + qint64(size)});
        begin += qint64(size);
    }
    return true;
}

Box boxOfType(const QList<Box> &boxes, const char *type) {
    for (const Box &box : boxes) if (box.type == type) return box;
    return {};
}

bool version(QFile &file, const Box &box, quint64 &value) {
    return box.end - box.begin >= 4 && readNumber(file, box.begin, 1, value) && value <= 1;
}

bool capHeader(QFile &file, const Box &box, bool track, quint64 duration, QList<Patch> &patches) {
    quint64 v, previous;
    if (!version(file, box, v)) return false;
    const int bytes = v == 1 ? 8 : 4;
    const qint64 offset = box.begin + (v == 1 ? 24 : 16) + (track ? 4 : 0);
    if (box.end - offset < bytes || !readNumber(file, offset, bytes, previous)) return false;
    patches.append({offset, bytes, qMin(previous, duration)});
    return true;
}

bool capEdits(QFile &file, const Box &box, quint64 duration, QList<Patch> &patches) {
    quint64 v, count;
    if (!version(file, box, v) || box.end - box.begin < 8
        || !readNumber(file, box.begin + 4, 4, count)) return false;
    // FFmpeg writes one media edit, optionally preceded by an empty edit for
    // delayed streams. Refuse other layouts instead of guessing at their timing.
    const int bytes = v == 1 ? 8 : 4;
    const int entrySize = bytes * 2 + 4;
    if ((count != 1 && count != 2) || box.end - box.begin - 8 != qint64(count) * entrySize)
        return false;
    for (quint64 i = 0; i < count; ++i) {
        const qint64 offset = box.begin + 8 + qint64(i) * entrySize;
        quint64 length, mediaTime, rate;
        if (!readNumber(file, offset, bytes, length)
            || !readNumber(file, offset + bytes, bytes, mediaTime)
            || !readNumber(file, offset + bytes * 2, 4, rate) || rate != 0x00010000)
            return false;
        const quint64 emptyTime = bytes == 8 ? std::numeric_limits<quint64>::max()
                                             : std::numeric_limits<quint32>::max();
        if (i + 1 < count) {
            if (mediaTime != emptyTime || length >= duration) return false;
            duration -= length;
        } else {
            if (mediaTime >= (quint64(1) << (bytes * 8 - 1))) return false;
            patches.append({offset, bytes, qMin(length, duration)});
        }
    }
    return true;
}
}

bool limitMp4Duration(const QString &path, qint64 durationMs) {
    if (durationMs <= 0) return false;
    QFile file(path);
    if (!file.open(QIODevice::ReadWrite)) return false;
    QList<Box> roots, movie;
    if (!children(file, 0, file.size(), roots)) return false;
    const Box moov = boxOfType(roots, "moov");
    if (moov.type.isEmpty() || !children(file, moov.begin, moov.end, movie)) return false;
    const Box mvhd = boxOfType(movie, "mvhd");
    quint64 v, timescale;
    if (!version(file, mvhd, v)) return false;
    const qint64 scaleOffset = mvhd.begin + (v == 1 ? 20 : 12);
    if (mvhd.end - scaleOffset < 4 || !readNumber(file, scaleOffset, 4, timescale) || timescale == 0)
        return false;
    const quint64 seconds = quint64(durationMs / 1000);
    const quint64 fraction = (quint64(durationMs % 1000) * timescale + 500) / 1000;
    if (seconds > (std::numeric_limits<quint64>::max() - fraction) / timescale) return false;
    const quint64 duration = seconds * timescale + fraction;
    if (duration == 0) return false;

    // MP4 movie/track headers and edit durations use the movie timescale. Leave
    // mdhd/stts/ctts alone: extra reference frames must remain decodable even
    // though the edit list prevents their presentation beyond the chosen end.
    QList<Patch> patches;
    if (!capHeader(file, mvhd, false, duration, patches)) return false;
    int tracks = 0;
    for (const Box &track : movie) {
        if (track.type != "trak") continue;
        QList<Box> fields, edits;
        if (!children(file, track.begin, track.end, fields)) return false;
        const Box edts = boxOfType(fields, "edts");
        if (edts.type.isEmpty() || !children(file, edts.begin, edts.end, edits)
            || !capEdits(file, boxOfType(edits, "elst"), duration, patches)
            || !capHeader(file, boxOfType(fields, "tkhd"), true, duration, patches)) return false;
        ++tracks;
    }
    if (tracks == 0) return false;
    // Validate every field before writing; this is only ever called on the
    // temporary export, which is probed again before replacing the original.
    for (const Patch &patch : patches) {
        QByteArray data(patch.bytes, '\0');
        quint64 value = patch.value;
        for (int i = patch.bytes - 1; i >= 0; --i) { data[i] = char(value & 0xff); value >>= 8; }
        if (!file.seek(patch.offset) || file.write(data) != data.size()) return false;
    }
    return file.flush();
}
