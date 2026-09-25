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
    bool annotate(const QString &tool, QPointF start, QPointF end, QColor color);
    bool text(QRectF box, const QString &text, QColor color);
    static void drawAnnotation(QPainter &painter, const QString &tool,
                               QPointF start, QPointF end, QColor color);
private:
    void commit(QImage image);
    QVector<QImage> m_history;
    int m_index = -1;
};
