#include "../../glimmer-qt/native/extensions.h"
#include <QQuickItem>
class Marker : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(double angle READ rotation WRITE setRotation)
    Q_PROPERTY(double completedAngle MEMBER completedAngle)
public:
    double completedAngle = -1;
};
static QObject *marker() { return new Marker; }
static void complete(QObject *object) {
    auto *item = static_cast<Marker *>(object);
    item->completedAngle = item->rotation();
}
static QObject *service() { return new QObject; }
extern "C" Q_DECL_EXPORT int hello_register_extensions() {
    return gqt_register_factory("hello.marker", marker, complete) &&
           gqt_register_factory("hello.service", service, nullptr);
}
#include "extensions.moc"
