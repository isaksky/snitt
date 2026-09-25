#pragma once
#include <QWidget>
#include <QImage>
#include <QList>

// One frozen, full-screen overlay per monitor. Coordinates stay screen-local.
class RegionSelector : public QWidget {
    Q_OBJECT
public:
    explicit RegionSelector(QImage image, const QRect &geometry);
    static QRect pixelRect(const QRectF &selection, const QSizeF &viewSize, const QSize &imageSize);
    QImage crop(const QRectF &area) const;
    void setSelections(bool multiple, const QList<QPair<int, QRectF>> &areas, int total);
    void setNotice(const QString &notice);
signals:
    void selected(const QImage &image);
    void canceled();
    void multipleRequested();
    void regionAdded(const QRectF &area);
    void regionRemoved(int index);
    void removeLastRequested();
    void accepted();
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
    bool m_multiple = false;
    int m_total = 0;
    QList<QPair<int, QRectF>> m_areas;
    QString m_notice;
};
