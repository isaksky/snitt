#pragma once

#include <QColor>
#include <QImage>
#include <QPointF>
#include <QRectF>
#include <QVector>

class QPainter;

// All edits use source-image pixels; the preview and clipboard share a renderer.
class ImageDocument {
public:
    const QImage &image() const;
    void reset(QImage image);
    bool canUndo() const { return m_index > 0; }
    bool canRedo() const { return m_index + 1 < m_history.size(); }
    void undo();
    void redo();
    bool cut(bool vertical, int start, int end);
    bool blur(QRectF area);
    bool erase(QRectF area, QPointF samplePosition);
    bool annotate(const QString &tool, QPointF start, QPointF end, QColor color, qreal strokeWidth = 4);
    bool text(QRectF box, const QString &text, QColor color, int fontSize = 24);
    static void drawAnnotation(QPainter &painter, const QString &tool,
                               QPointF start, QPointF end, QColor color, qreal strokeWidth = 4);
    static void drawPrivacyMask(QPainter &painter, const QRect &area);
private:
    void commit(QImage image);
    QVector<QImage> m_history;
    int m_index = -1;
};
