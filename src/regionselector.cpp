#include "regionselector.h"
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QKeySequence>
#include <cmath>

RegionSelector::RegionSelector(QImage image, const QRect &geometry)
    : QWidget(nullptr, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool),
      m_image(std::move(image)) {
    setObjectName("regionSelector");
    setWindowTitle("xshot — Select a region");
    // paintEvent covers the entire window with the frozen desktop.
    setAttribute(Qt::WA_OpaquePaintEvent);
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

QImage RegionSelector::crop(const QRectF &area) const {
    return m_image.copy(pixelRect(area, size(), m_image.size()));
}

void RegionSelector::setSelections(bool multiple, const QList<QPair<int, QRectF>> &areas, int total) {
    m_multiple = multiple;
    m_areas = areas;
    m_total = total;
    m_notice.clear();
    update();
}

void RegionSelector::setNotice(const QString &notice) { m_notice = notice; update(); }

void RegionSelector::setVideo(bool video) {
    m_video = video;
    setWindowTitle(video ? "xshot — Select a recording region" : "xshot — Select a region");
    update();
}

void RegionSelector::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.drawImage(rect(), m_image);
    p.fillRect(rect(), QColor(0, 0, 0, 115));
    for (const auto &entry : m_areas) {
        p.save();
        p.setClipRect(entry.second);
        p.drawImage(rect(), m_image);
        p.restore();
        p.setPen(QPen(QColor("#a3e6ca"), 2));
        p.drawRect(entry.second);
        const QRectF badge(entry.second.topLeft(), QSizeF(28, 28));
        p.fillRect(badge, QColor("#a3e6ca"));
        p.setPen(QColor("#142820"));
        QFont numberFont = p.font(); numberFont.setPixelSize(16); numberFont.setBold(true); p.setFont(numberFont);
        p.drawText(badge, Qt::AlignCenter, QString::number(entry.first + 1));
    }
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
    const QString copyKey = QKeySequence(QKeySequence::Copy).toString(QKeySequence::NativeText);
    const QString hint = !m_notice.isEmpty() ? m_notice
        : m_dragging ? QStringLiteral("%1 × %2 px · Release to %3 · Esc to cancel")
                                         .arg(pixels.width()).arg(pixels.height())
                                         .arg(m_video ? "start recording" : m_multiple ? "add region" : "capture")
        : m_video ? QStringLiteral("Drag to record one region · Screenshot (V) · Cancel (Esc)")
        : m_multiple ? QStringLiteral("%1 selected · Drag to add · Click to remove · Arrange (%2) · Video (V) · Cancel (Esc)").arg(m_total).arg(copyKey)
        : QStringLiteral("Drag to select a region · Multiple (M) · Video (V) · Cancel (Esc)");
    QFont font = p.font(); font.setPixelSize(16); font.setBold(false); p.setFont(font);
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
    if (m_multiple && (m_end - m_start).manhattanLength() < 4) {
        for (auto it = m_areas.crbegin(); it != m_areas.crend(); ++it) {
            if (it->second.contains(m_end)) { emit regionRemoved(it->first); return; }
        }
    }
    const QRect region = pixelRect(selection(), size(), m_image.size());
    if (region.width() >= 2 && region.height() >= 2) {
        if (m_video) emit videoSelected(selection());
        else if (m_multiple) emit regionAdded(selection());
        else emit selected(m_image.copy(region));
    }
    else update();
}

void RegionSelector::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape) emit canceled();
    else if (event->key() == Qt::Key_V) emit videoRequested();
    else if (event->key() == Qt::Key_M && !m_video) emit multipleRequested();
    else if (m_multiple && event->matches(QKeySequence::Copy)) emit accepted();
    else if (m_multiple && (event->key() == Qt::Key_Backspace || event->key() == Qt::Key_Delete)) emit removeLastRequested();
    else QWidget::keyPressEvent(event);
}
