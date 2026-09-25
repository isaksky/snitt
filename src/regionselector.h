#pragma once
#include <QWidget>
#include <QImage>

// One frozen, full-screen overlay per monitor. Coordinates stay screen-local.
class RegionSelector : public QWidget {
    Q_OBJECT
public:
    explicit RegionSelector(QImage image, const QRect &geometry);
    static QRect pixelRect(const QRectF &selection, const QSizeF &viewSize, const QSize &imageSize);
signals:
    void selected(const QImage &image);
    void canceled();
protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
private:
    QRectF selection() const;
    QImage m_image;
    QPointF m_start, m_end;
    bool m_dragging = false;
};
