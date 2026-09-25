#include "editorcanvas.h"
#include <QClipboard>
#include <QGuiApplication>
#include <QImageReader>
#include <QPainter>

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
    emit toolChanged();
}

void EditorCanvas::setInk(QColor ink) {
    if (ink == m_ink) return;
    m_ink = ink;
    emit inkChanged();
    update();
}

void EditorCanvas::paint(QPainter *p) {
    if (!hasImage()) return;
    p->setRenderHint(QPainter::SmoothPixmapTransform);
    p->translate(imageRect().topLeft());
    p->scale(imageScale(), imageScale());
    p->setClipRect(QRectF(0, 0, imageWidth(), imageHeight()));
    p->drawImage(QPointF(0, 0), m_document.image());
    if (!m_dragging) return;
    if (m_tool == "cut") {
        const bool vertical = qAbs(m_end.x() - m_start.x()) >= qAbs(m_end.y() - m_start.y());
        const QRectF strip = vertical
            ? QRectF(qMin(m_start.x(), m_end.x()), 0, qAbs(m_end.x() - m_start.x()), imageHeight())
            : QRectF(0, qMin(m_start.y(), m_end.y()), imageWidth(), qAbs(m_end.y() - m_start.y()));
        p->fillRect(strip, QColor(239, 68, 68, 85));
        p->setPen(QPen(QColor("#ef4444"), 1.5 / imageScale(), Qt::DashLine));
        p->drawRect(strip);
    } else {
        ImageDocument::drawAnnotation(*p, m_tool, m_start, m_end, m_ink);
    }
}

void EditorCanvas::changed() {
    cancel();
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
    changed();
    return true;
}

bool EditorCanvas::copy() {
    if (!hasImage()) return false;
    QGuiApplication::clipboard()->setImage(m_document.image());
    return true;
}

void EditorCanvas::undo() { m_document.undo(); changed(); }
void EditorCanvas::redo() { m_document.redo(); changed(); }

QPointF EditorCanvas::imagePoint(qreal x, qreal y) const {
    const QPointF point = (QPointF(x, y) - imageRect().topLeft()) / imageScale();
    return {qBound(0.0, point.x(), double(imageWidth())), qBound(0.0, point.y(), double(imageHeight()))};
}

void EditorCanvas::begin(qreal x, qreal y) {
    if (!hasImage() || !imageRect().contains(x, y)) return;
    m_start = m_end = imagePoint(x, y);
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
    update();
}

void EditorCanvas::end(qreal x, qreal y) {
    if (!m_dragging) return;
    move(x, y);
    m_dragging = false;
    if (m_tool == "cut") {
        const bool vertical = qAbs(m_end.x() - m_start.x()) >= qAbs(m_end.y() - m_start.y());
        m_document.cut(vertical, qRound(vertical ? m_start.x() : m_start.y()),
                       qRound(vertical ? m_end.x() : m_end.y()));
    } else {
        m_document.annotate(m_tool, m_start, m_end, m_ink);
    }
    changed();
}

void EditorCanvas::cancel() { m_dragging = false; update(); }

void EditorCanvas::addText(qreal x, qreal y, qreal width, qreal height, const QString &text) {
    if (m_document.text(QRectF(x, y, width, height), text, m_ink)) changed();
}
