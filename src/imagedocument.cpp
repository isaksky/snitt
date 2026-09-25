#include "imagedocument.h"
#include <QFont>
#include <QPainter>
#include <cmath>

const QImage &ImageDocument::image() const {
    static const QImage empty;
    return m_index < 0 ? empty : m_history[m_index];
}

void ImageDocument::reset(QImage image) {
    m_history.clear();
    m_index = -1;
    if (!image.isNull()) {
        image.setDevicePixelRatio(1);
        commit(image.convertToFormat(QImage::Format_ARGB32_Premultiplied));
    }
}

void ImageDocument::commit(QImage image) {
    m_history.resize(m_index + 1);
    m_history.append(std::move(image));
    ++m_index;
    qint64 bytes = 0;
    for (const auto &frame : m_history) bytes += frame.sizeInBytes();
    while (m_history.size() > 1 && (bytes > 256LL * 1024 * 1024 || m_history.size() > 50)) {
        bytes -= m_history.front().sizeInBytes();
        m_history.removeFirst();
        --m_index;
    }
}

void ImageDocument::undo() { if (canUndo()) --m_index; }
void ImageDocument::redo() { if (canRedo()) ++m_index; }

bool ImageDocument::cut(bool vertical, int start, int end) {
    if (image().isNull()) return false;
    const int length = vertical ? image().width() : image().height();
    if (start > end) std::swap(start, end);
    start = qBound(0, start, length);
    end = qBound(0, end, length);
    const int removed = end - start;
    if (removed <= 0 || removed >= length) return false;
    QImage result(image().width() - (vertical ? removed : 0),
                  image().height() - (vertical ? 0 : removed), image().format());
    result.fill(Qt::transparent);
    QPainter p(&result);
    if (vertical) {
        p.drawImage(QRect(0, 0, start, result.height()), image(), QRect(0, 0, start, image().height()));
        p.drawImage(QRect(start, 0, length - end, result.height()), image(), QRect(end, 0, length - end, image().height()));
    } else {
        p.drawImage(QRect(0, 0, result.width(), start), image(), QRect(0, 0, image().width(), start));
        p.drawImage(QRect(0, start, result.width(), length - end), image(), QRect(0, end, image().width(), length - end));
    }
    p.end();
    commit(std::move(result));
    return true;
}

void ImageDocument::drawAnnotation(QPainter &p, const QString &tool,
                                   QPointF start, QPointF end, QColor color) {
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(color, 4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    if (tool == "rect") {
        p.drawRect(QRectF(start, end).normalized());
    } else if (tool == "arrow") {
        const QLineF line(start, end);
        if (line.length() < 1) return;
        const QPointF unit = (end - start) / line.length();
        const QPointF side(-unit.y(), unit.x());
        const qreal head = qMin(18.0, line.length() * 0.45);
        const QPointF base = end - unit * head;
        p.drawLine(start, base);
        p.setBrush(color);
        p.setPen(Qt::NoPen);
        p.drawPolygon(QPolygonF{end, base + side * head * 0.48, base - side * head * 0.48});
    }
}

bool ImageDocument::annotate(const QString &tool, QPointF start, QPointF end, QColor color) {
    if (image().isNull() || (tool != "rect" && tool != "arrow") || QLineF(start, end).length() < 3)
        return false;
    if (tool == "rect" && (qAbs(start.x() - end.x()) < 2 || qAbs(start.y() - end.y()) < 2))
        return false;
    QImage result = image().copy();
    QPainter p(&result);
    drawAnnotation(p, tool, start, end, color);
    p.end();
    commit(std::move(result));
    return true;
}

bool ImageDocument::text(QRectF box, const QString &text, QColor color) {
    if (image().isNull() || text.trimmed().isEmpty() || box.width() < 1 || box.height() < 1)
        return false;
    QImage result = image().copy();
    QPainter p(&result);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    QFont font(QStringLiteral("Helvetica"));
    font.setPixelSize(24);
    font.setWeight(QFont::DemiBold);
    p.setFont(font);
    p.setPen(color);
    p.drawText(box, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, text);
    p.end();
    commit(std::move(result));
    return true;
}
