#include "regionselector.h"
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <cmath>

RegionSelector::RegionSelector(QImage image, const QRect &geometry)
    : QWidget(nullptr, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool),
      m_image(std::move(image)) {
    setObjectName("regionSelector");
    setWindowTitle("xshot — Select a region");
    m_image.setDevicePixelRatio(1);
    setGeometry(geometry);
    setCursor(Qt::CrossCursor);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
}

QRect RegionSelector::pixelRect(const QRectF &selection, const QSizeF &viewSize, const QSize &imageSize) {
    if (viewSize.isEmpty() || imageSize.isEmpty()) return {};
    const QRectF bounded = selection.normalized().intersected(QRectF(QPointF(0, 0), viewSize));
    const qreal sx = imageSize.width() / viewSize.width();
    const qreal sy = imageSize.height() / viewSize.height();
    const int x = qRound(bounded.left() * sx), y = qRound(bounded.top() * sy);
    return QRect(x, y, qRound(bounded.right() * sx) - x, qRound(bounded.bottom() * sy) - y)
        .intersected(QRect(QPoint(0, 0), imageSize));
}

QRectF RegionSelector::selection() const {
    return QRectF(m_start, m_end).normalized().intersected(rect());
}

void RegionSelector::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.drawImage(rect(), m_image);
    p.fillRect(rect(), QColor(0, 0, 0, 115));
    const QRectF area = selection();
    if (m_dragging && !area.isEmpty()) {
        p.save();
        p.setClipRect(area);
        p.drawImage(rect(), m_image);
        p.restore();
        p.setPen(QPen(QColor("#a3e6ca"), 2));
        p.drawRect(area);
    }
    const QRect pixels = pixelRect(area, size(), m_image.size());
    const QString hint = m_dragging ? QStringLiteral("%1 × %2 px · Release to capture · Esc to cancel")
                                         .arg(pixels.width()).arg(pixels.height())
                                   : QStringLiteral("Drag to select a region · Esc to cancel");
    QFont font = p.font(); font.setPixelSize(16); p.setFont(font);
    const int boxWidth = qMin(width() - 24, p.fontMetrics().horizontalAdvance(hint) + 32);
    const QRect box((width() - boxWidth) / 2, 24, boxWidth, 42);
    p.fillRect(box, QColor("#1b1e23"));
    p.setPen(Qt::white);
    p.drawText(box, Qt::AlignCenter, hint);
}

void RegionSelector::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::RightButton) { emit canceled(); return; }
    if (event->button() != Qt::LeftButton) return;
    m_start = m_end = event->position();
    m_dragging = true;
    update();
}

void RegionSelector::mouseMoveEvent(QMouseEvent *event) {
    if (!m_dragging) return;
    m_end = event->position();
    update();
}

void RegionSelector::mouseReleaseEvent(QMouseEvent *event) {
    if (!m_dragging || event->button() != Qt::LeftButton) return;
    m_end = event->position();
    m_dragging = false;
    const QRect region = pixelRect(selection(), size(), m_image.size());
    if (region.width() >= 2 && region.height() >= 2) emit selected(m_image.copy(region));
    else update();
}

void RegionSelector::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape) emit canceled();
    else QWidget::keyPressEvent(event);
}
