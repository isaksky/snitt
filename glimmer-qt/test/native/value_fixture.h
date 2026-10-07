#pragma once
#include <QObject>
#include <QPointer>
#include <QColor>
#include <QUrl>
#include <QRectF>
#include <QVariant>

class ValueFixture : public QObject {
    Q_OBJECT
public:
    enum Mode { Idle = 0, Busy = 1 };
    Q_ENUM(Mode)
    enum Option { None = 0, First = 1, Second = 2 };
    Q_DECLARE_FLAGS(Options, Option)
    Q_FLAG(Options)
    Q_PROPERTY(qint64 duration MEMBER duration)
    Q_PROPERTY(int count MEMBER count RESET resetCount)
    Q_PROPERTY(double progress MEMBER progress)
    Q_PROPERTY(bool busy MEMBER busy)
    Q_PROPERTY(QString text MEMBER text)
    Q_PROPERTY(QColor color MEMBER color)
    Q_PROPERTY(QUrl source MEMBER source)
    Q_PROPERTY(QPoint point MEMBER point)
    Q_PROPERTY(QSize size MEMBER size)
    Q_PROPERTY(QRect rect MEMBER rect)
    Q_PROPERTY(QPointF pointF MEMBER pointF)
    Q_PROPERTY(QSizeF sizeF MEMBER sizeF)
    Q_PROPERTY(QRectF rectF MEMBER rectF)
    Q_PROPERTY(QVariantMap settings READ getSettings WRITE setSettings)
    Q_PROPERTY(QVariantList list MEMBER list)
    Q_PROPERTY(QStringList strings MEMBER strings)
    Q_PROPERTY(QObject* object READ object WRITE setObject)
    Q_PROPERTY(Mode mode MEMBER mode)
    Q_PROPERTY(Options options READ getOptions WRITE setOptions)
    Q_PROPERTY(QString readOnly READ readOnly CONSTANT)
    Q_PROPERTY(QByteArray unsupported READ unsupported CONSTANT)
    explicit ValueFixture(QObject *parent) : QObject(parent) {}
    qint64 duration = 0;
    int count = 7;
    double progress = 0;
    bool busy = false;
    QString text;
    QColor color = Qt::red;
    QUrl source;
    QPoint point;
    QSize size;
    QRect rect;
    QPointF pointF;
    QSizeF sizeF;
    QRectF rectF;
    QVariantMap settings;
    // MEMBER-generated moc setters use QVariant's coercive equality. A real
    // typed setter must accept changes in nested types even for equal numbers.
    QVariantMap getSettings() const { return settings; }
    void setSettings(const QVariantMap &value) { settings = value; }
    QVariantList list;
    QStringList strings;
    QPointer<QObject> reference;
    QObject *object() const { return reference; }
    void setObject(QObject *o) { reference = o; }
    Mode mode = Idle;
    Options options;
    Options getOptions() const { return options; }
    void setOptions(Options value) { options = value; }
    QString readOnly() const { return "fixed"; }
    QByteArray unsupported() const { return "bytes"; }
    void resetCount() { count = 7; }
};
Q_DECLARE_OPERATORS_FOR_FLAGS(ValueFixture::Options)
