#pragma once
#include <QCursor>
#include <QPainter>
#include <QPixmap>
#include <QtMath>

namespace captureCursor {
inline QCursor crosshair(qreal devicePixelRatio) {
#ifdef Q_OS_WIN
    const qreal ratio = qMax(qreal(1), devicePixelRatio);
    QPixmap pixmap(qCeil(32 * ratio), qCeil(32 * ratio));
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    // A solid outline stays visible on gray as well as light/dark screenshots.
    // The black center pixel at (15, 15) is also the cursor hotspot.
    painter.fillRect(QRect(14, 3, 3, 25), Qt::white);
    painter.fillRect(QRect(3, 14, 25, 3), Qt::white);
    painter.fillRect(QRect(15, 4, 1, 23), Qt::black);
    painter.fillRect(QRect(4, 15, 23, 1), Qt::black);
    painter.end();
    return QCursor(pixmap, 15, 15);
#else
    Q_UNUSED(devicePixelRatio);
    return QCursor(Qt::CrossCursor);
#endif
}
}
