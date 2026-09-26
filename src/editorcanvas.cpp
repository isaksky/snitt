#include "editorcanvas.h"
#include <QClipboard>
#include <QGuiApplication>
#include <QImageReader>
#include <QStandardPaths>
#include "screenshotsave.h"
#include <QPainter>
#include <cmath>

EditorCanvas::EditorCanvas(QQuickItem *parent) : QQuickPaintedItem(parent) {
    setAntialiasing(true);
    setClip(true);
}

qreal EditorCanvas::imageScale() const {
    if (!hasImage()) return 1;
    return qMax(0.001, qMin((width() - 32) / imageWidth(), (height() - 32) / imageHeight()));
}

QRectF EditorCanvas::imageRect() const {
    if (!hasImage()) return {};
    const QSizeF size(imageWidth() * imageScale(), imageHeight() * imageScale());
    return QRectF(QPointF((width() - size.width()) / 2, (height() - size.height()) / 2), size);
}

void EditorCanvas::geometryChange(const QRectF &next, const QRectF &previous) {
    QQuickPaintedItem::geometryChange(next, previous);
    emit imageRectChanged();
    update();
}

void EditorCanvas::setTool(const QString &tool) {
    if (tool == m_tool) return;
    cancel();
    m_tool = tool;
    m_wheelRemainder = 0;
    emit toolChanged();
}

void EditorCanvas::setInk(QColor ink) {
    if (ink == m_ink) return;
    m_ink = ink;
    emit inkChanged();
    update();
}

int EditorCanvas::maxStrokeWidth() const {
    // Stroke: 1..min(64, shortest image edge / 8), with a 1 px floor.
    return hasImage() ? qMax(1, qMin(64, qMin(imageWidth(), imageHeight()) / 8)) : 64;
}

int EditorCanvas::maxTextSize() const {
    // Text: 8..min(144, shortest image edge / 3), with an 8 px floor.
    return hasImage() ? qMax(8, qMin(144, qMin(imageWidth(), imageHeight()) / 3)) : 144;
}

void EditorCanvas::adjustToolSize(qreal wheelDelta) {
    if (!hasImage() || m_arranging || (m_tool != "rect" && m_tool != "arrow" && m_tool != "text")) return;
    m_wheelRemainder += wheelDelta;
    const int steps = int(m_wheelRemainder / 120);
    if (!steps) return;
    m_wheelRemainder -= steps * 120;
    if (m_tool == "text") {
        m_textSize = qBound(8, m_textSize + steps, maxTextSize());
    } else {
        m_strokeWidth = qBound(1, m_strokeWidth + steps, maxStrokeWidth());
    }
    emit sizeChanged();
    update();
}

void EditorCanvas::paint(QPainter *p) {
    if (!hasImage()) return;
    p->setRenderHint(QPainter::SmoothPixmapTransform);
    p->translate(imageRect().topLeft());
    p->scale(imageScale(), imageScale());
    p->setClipRect(QRectF(0, 0, imageWidth(), imageHeight()));
    p->drawImage(QPointF(0, 0), m_document.image());
    if (m_arranging) {
        for (int i = 0; i < m_regionRects.size(); ++i) {
            const QRectF area = m_regionRects[i];
            const bool selected = i == m_selectedRegion;
            const bool target = m_dragging && i == m_dropRegion;
            p->setPen(QPen(QColor(target ? "#00633f" : selected ? "#1261a0" : "#596575"),
                          (selected || target ? 3 : 1) / imageScale(), target ? Qt::DashLine : Qt::SolidLine));
            p->setBrush(Qt::NoBrush);
            p->drawRect(area);
            const qreal badgeSize = 24 / imageScale();
            const QRectF badge(area.topLeft(), QSizeF(badgeSize, badgeSize));
            p->fillRect(badge, QColor(selected ? "#1261a0" : "#1b1e23"));
            p->setPen(Qt::white);
            QFont font = p->font(); font.setPixelSize(qMax(1, qRound(14 / imageScale()))); font.setBold(true); p->setFont(font);
            p->drawText(badge, Qt::AlignCenter, QString::number(i + 1));
        }
        return;
    }
    if (!m_dragging) return;
    if (m_tool == "cut") {
        const bool vertical = qAbs(m_end.x() - m_start.x()) >= qAbs(m_end.y() - m_start.y());
        const QRectF strip = vertical
            ? QRectF(qMin(m_start.x(), m_end.x()), 0, qAbs(m_end.x() - m_start.x()), imageHeight())
            : QRectF(0, qMin(m_start.y(), m_end.y()), imageWidth(), qAbs(m_end.y() - m_start.y()));
        p->fillRect(strip, QColor(239, 68, 68, 85));
        p->setPen(QPen(QColor("#ef4444"), 1.5 / imageScale(), Qt::DashLine));
        p->drawRect(strip);
    } else if (m_tool == "blur" || m_tool == "erase") {
        const QRect area = QRectF(m_start, m_end).normalized().toAlignedRect()
            .intersected(m_document.image().rect());
        if (m_tool == "blur") {
            ImageDocument::drawPrivacyMask(*p, area);
        } else {
            const int x = qBound(0, int(std::floor(m_start.x())), imageWidth() - 1);
            const int y = qBound(0, int(std::floor(m_start.y())), imageHeight() - 1);
            p->save();
            p->setCompositionMode(QPainter::CompositionMode_Source);
            p->fillRect(area, m_document.image().pixelColor(x, y));
            p->restore();
        }
        p->setPen(QPen(QColor("#172331"), 1.5 / imageScale(), Qt::DashLine));
        p->setBrush(Qt::NoBrush);
        p->drawRect(area);
    } else {
        ImageDocument::drawAnnotation(*p, m_tool, m_start, m_end, m_ink, m_strokeWidth);
    }
}

void EditorCanvas::changed() {
    cancel();
    m_strokeWidth = qBound(1, m_strokeWidth, maxStrokeWidth());
    m_textSize = qBound(8, m_textSize, maxTextSize());
    emit sizeChanged();
    emit imageChanged();
    emit imageRectChanged();
    update();
}

bool EditorCanvas::load(const QUrl &url) {
    QImageReader reader(url.toLocalFile());
    reader.setAutoTransform(true);
    const QImage image = reader.read();
    if (image.isNull()) {
        emit error(QStringLiteral("Could not open this image: ") + reader.errorString());
        return false;
    }
    m_document.reset(image);
    clearRegions();
    changed();
    return true;
}

bool EditorCanvas::paste() {
    const QImage image = QGuiApplication::clipboard()->image();
    if (image.isNull()) {
        emit error(QStringLiteral("The clipboard doesn't contain an image."));
        return false;
    }
    m_document.reset(image);
    clearRegions();
    changed();
    return true;
}

bool EditorCanvas::copy() {
    if (!hasImage()) return false;
    QGuiApplication::clipboard()->setImage(exportImage());
    return true;
}

QImage EditorCanvas::exportImage() const {
    return m_document.image();
}

QString EditorCanvas::save() {
    if (!hasImage()) return {};
    const QString pictures = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    QString message;
    const QString path = screenshots::savePng(exportImage(), pictures, &message);
    if (path.isEmpty()) emit error(message);
    return path;
}

void EditorCanvas::undo() { m_document.undo(); changed(); }
void EditorCanvas::clear() { clearRegions(); m_document.reset({}); changed(); }
void EditorCanvas::redo() { m_document.redo(); changed(); }

QPointF EditorCanvas::imagePoint(qreal x, qreal y) const {
    const QPointF point = (QPointF(x, y) - imageRect().topLeft()) / imageScale();
    return {qBound(0.0, point.x(), double(imageWidth())), qBound(0.0, point.y(), double(imageHeight()))};
}

void EditorCanvas::begin(qreal x, qreal y) {
    if (!hasImage() || !imageRect().contains(x, y)) return;
    m_start = m_end = imagePoint(x, y);
    if (m_arranging) {
        m_selectedRegion = m_dropRegion = regionAt(m_start);
        m_dragging = m_selectedRegion >= 0;
        emit arrangementChanged(); update(); return;
    }
    if (m_tool == "text") {
        emit textRequested(m_start.x(), m_start.y());
        return;
    }
    m_dragging = true;
    update();
}

void EditorCanvas::move(qreal x, qreal y) {
    if (!m_dragging) return;
    m_end = imagePoint(x, y);
    if (m_arranging) m_dropRegion = regionAt(m_end);
    update();
}

void EditorCanvas::end(qreal x, qreal y) {
    if (!m_dragging) return;
    move(x, y);
    m_dragging = false;
    if (m_arranging) {
        if ((m_end - m_start).manhattanLength() * imageScale() >= 4)
            moveRegion(m_selectedRegion, m_dropRegion);
        update(); return;
    }
    if (m_tool == "cut") {
        const bool vertical = qAbs(m_end.x() - m_start.x()) >= qAbs(m_end.y() - m_start.y());
        m_document.cut(vertical, qRound(vertical ? m_start.x() : m_start.y()),
                       qRound(vertical ? m_end.x() : m_end.y()));
    } else if (m_tool == "blur") {
        m_document.blur(QRectF(m_start, m_end));
    } else if (m_tool == "erase") {
        m_document.erase(QRectF(m_start, m_end), m_start);
    } else {
        m_document.annotate(m_tool, m_start, m_end, m_ink, m_strokeWidth);
    }
    changed();
}

bool EditorCanvas::cancel() {
    const bool wasDragging = m_dragging;
    m_dragging = false;
    update();
    return wasDragging;
}

void EditorCanvas::addText(qreal x, qreal y, qreal width, qreal height, const QString &text, int fontSize) {
    if (m_document.text(QRectF(x, y, width, height), text, m_ink, fontSize)) changed();
}

void EditorCanvas::clearRegions() {
    m_regions.clear(); m_regionRects.clear();
    m_arranging = false; m_selectedRegion = -1; m_dropRegion = -1; m_columns = 2;
    emit arrangementChanged();
}

int EditorCanvas::regionAt(const QPointF &point) const {
    for (int i = 0; i < m_regionRects.size(); ++i)
        if (m_regionRects[i].contains(point.toPoint())) return i;
    return -1;
}

bool EditorCanvas::composeRegions() {
    if (m_regions.isEmpty()) return false;
    const int cols = qMin(m_columns, int(m_regions.size()));
    const int rows = (m_regions.size() + cols - 1) / cols;
    QList<int> widths(cols, 0), heights(rows, 0);
    for (int i = 0; i < m_regions.size(); ++i) {
        widths[i % cols] = qMax(widths[i % cols], m_regions[i].width());
        heights[i / cols] = qMax(heights[i / cols], m_regions[i].height());
    }
    constexpr int gap = 24;
    qint64 width = gap, height = gap;
    for (int w : widths) width += w + gap;
    for (int h : heights) height += h + gap;
    if (width > 32768 || height > 32768 || width * height > 64 * 1024 * 1024) {
        emit error(QStringLiteral("This layout is too large. Use fewer regions or a different number of columns."));
        return false;
    }
    QImage image(int(width), int(height), QImage::Format_ARGB32_Premultiplied);
    if (image.isNull()) { emit error(QStringLiteral("Not enough memory for this layout.")); return false; }
    image.fill(QColor("#f5f7fa"));
    QList<QRect> areas;
    QPainter painter(&image);
    int y = gap;
    for (int row = 0; row < rows; ++row) {
        int x = gap;
        for (int col = 0; col < cols; ++col) {
            const int i = row * cols + col;
            if (i >= m_regions.size()) break;
            areas.append(QRect(QPoint(x, y), m_regions[i].size()));
            painter.drawImage(x, y, m_regions[i]);
            x += widths[col] + gap;
        }
        y += heights[row] + gap;
    }
    painter.end();
    m_regionRects = areas;
    m_document.reset(image);
    changed(); emit arrangementChanged();
    return true;
}

bool EditorCanvas::loadRegions(const QVariantList &images) {
    if (images.isEmpty() || images.size() > 24) return false;
    QList<QImage> regions;
    qint64 bytes = 0;
    for (const auto &value : images) {
        QImage image = qvariant_cast<QImage>(value);
        if (image.isNull()) return false;
        image.setDevicePixelRatio(1);
        bytes += image.sizeInBytes();
        if (bytes > 256 * 1024 * 1024) { emit error(QStringLiteral("Too many captured pixels. Select smaller regions.")); return false; }
        regions.append(image);
    }
    const auto previous = m_regions;
    const int previousColumns = m_columns;
    m_regions = regions; m_columns = 2;
    if (!composeRegions()) { m_regions = previous; m_columns = previousColumns; return false; }
    m_arranging = true; m_selectedRegion = 0;
    emit arrangementChanged(); update();
    return true;
}

void EditorCanvas::setColumns(int columns) {
    columns = qBound(1, columns, qMax(1, qMin(6, int(m_regions.size()))));
    if (!m_arranging || columns == m_columns) return;
    const int previous = m_columns;
    m_columns = columns;
    if (!composeRegions()) m_columns = previous;
}

void EditorCanvas::moveRegion(int from, int to) {
    if (!m_arranging || from < 0 || to < 0 || from >= m_regions.size() || to >= m_regions.size() || from == to) return;
    m_regions.move(from, to);
    if (!composeRegions()) { m_regions.move(to, from); return; }
    m_selectedRegion = to; emit arrangementChanged(); update();
}

void EditorCanvas::removeRegion(int index) {
    if (!m_arranging || index < 0 || index >= m_regions.size()) return;
    const auto previous = m_regions;
    m_regions.removeAt(index);
    if (m_regions.isEmpty()) { clear(); return; }
    if (!composeRegions()) { m_regions = previous; return; }
    m_selectedRegion = qMin(index, int(m_regions.size()) - 1);
    emit arrangementChanged(); update();
}

void EditorCanvas::arrange() {
    if (m_regions.isEmpty() || !composeRegions()) return;
    m_arranging = true; emit arrangementChanged(); update();
}

void EditorCanvas::annotate() {
    cancel(); m_arranging = false; emit arrangementChanged(); update();
}
