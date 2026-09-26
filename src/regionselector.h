#pragma once
#include <QWidget>
#include <QImage>
#include <QList>

class QLabel;
class QPushButton;
class QToolButton;

// One frozen, full-screen overlay per monitor. Coordinates stay screen-local.
class RegionSelector : public QWidget {
    Q_OBJECT
public:
    explicit RegionSelector(QImage image, const QRect &geometry);
    static QRect pixelRect(const QRectF &selection, const QSizeF &viewSize, const QSize &imageSize);
    QImage crop(const QRectF &area) const;
    void setSelections(bool multiple, const QList<QPair<int, QRectF>> &areas, int total);
    void setNotice(const QString &notice);
    void setVideo(bool video);
signals:
    void selected(const QImage &image);
    void canceled();
    void singleRequested();
    void multipleRequested();
    void videoRequested();
    void videoSelected(const QRectF &area);
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
    void resizeEvent(QResizeEvent *) override;
private:
    QRectF selection() const;
    void updateToolbar();
    void layoutControls();
    QWidget *m_toolbar = nullptr;
    QLabel *m_instruction = nullptr;
    QLabel *m_count = nullptr;
    QLabel *m_noticeLabel = nullptr;
    QPushButton *m_singleButton = nullptr;
    QPushButton *m_multipleButton = nullptr;
    QPushButton *m_videoButton = nullptr;
    QPushButton *m_cancelButton = nullptr;
    QPushButton *m_arrangeButton = nullptr;
    QList<QToolButton *> m_removeButtons;
    QImage m_image;
    QPointF m_start, m_end;
    bool m_dragging = false;
    bool m_multiple = false;
    bool m_video = false;
    int m_total = 0;
    QList<QPair<int, QRectF>> m_areas;
    QString m_notice;
};
