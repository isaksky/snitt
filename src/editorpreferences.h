#pragma once
#include <QVariantMap>
#include <QStringList>

namespace editorPreferences {
inline QVariantMap defaults() {
    return {{"tool", "rect"}, {"colorMode", "bad"}, {"strokeWidth", 4},
            {"textSize", 24}, {"pixelBlockSize", 12}};
}

// Validate both disk input and updates from the editor before remembering them.
inline QVariant validated(const QString &key, const QVariant &value) {
    if (key == "tool") {
        const QStringList tools{"rect", "arrow", "highlight", "text", "blur", "erase", "cut"};
        return tools.contains(value.toString()) ? QVariant(value.toString()) : QVariant();
    }
    if (key == "colorMode") {
        const QString mode = value.toString();
        return mode == "good" || mode == "bad" ? QVariant(mode) : QVariant();
    }
    int minimum = 0, maximum = 0;
    if (key == "strokeWidth") { minimum = 1; maximum = 64; }
    else if (key == "textSize") { minimum = 8; maximum = 144; }
    else if (key == "pixelBlockSize") { minimum = 4; maximum = 64; }
    else return {};
    bool ok = false;
    const int size = value.toString().toInt(&ok);
    return ok && size >= minimum && size <= maximum ? QVariant(size) : QVariant();
}
}
