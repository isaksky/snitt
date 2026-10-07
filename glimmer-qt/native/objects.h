#pragma once
// Included after values.h. No private Qt API or generated application bindings.
#include <QMetaMethod>
#include <atomic>
#include <array>

namespace Objects {
static bool supported(QMetaType type) {
    if (type.flags().testFlag(QMetaType::PointerToQObject)) return true;
    if (type.flags().testFlag(QMetaType::IsEnumeration)) return true;
    switch (type.id()) {
    case QMetaType::Void: case QMetaType::Bool: case QMetaType::Int:
    case QMetaType::UInt: case QMetaType::LongLong: case QMetaType::Double:
    case QMetaType::QString: case QMetaType::QStringList: case QMetaType::QUrl:
    case QMetaType::QColor: case QMetaType::QPoint: case QMetaType::QPointF:
    case QMetaType::QSize: case QMetaType::QSizeF: case QMetaType::QRect:
    case QMetaType::QRectF: case QMetaType::QVariant: case QMetaType::QVariantList:
    case QMetaType::QVariantMap: return true;
    default: return false;
    }
}
static QMetaMethod method(QObject *object, const char *signature, bool signal) {
    const auto normalized = QMetaObject::normalizedSignature(signature);
    if (!normalized.contains('(')) Values::reject("Qt method requires an explicit signature");
    int index = signal ? object->metaObject()->indexOfSignal(normalized.constData())
                       : object->metaObject()->indexOfMethod(normalized.constData());
    if (index < 0) Values::reject("Missing Qt method/signature");
    auto method = object->metaObject()->method(index);
    if (!signal && (method.access() != QMetaMethod::Public || method.methodType() == QMetaMethod::Signal))
        Values::reject("Qt invocation requires a public method or slot");
    if (method.parameterCount() > 10) Values::reject("Qt methods support at most 10 arguments");
    if (!supported(method.returnMetaType())) Values::reject("Unsupported Qt method return type");
    for (int i = 0; i < method.parameterCount(); ++i) {
        auto name = method.parameterTypes().at(i);
        if (!supported(method.parameterMetaType(i)) || method.parameterMetaType(i).id() == QMetaType::Void || name.endsWith('&'))
            Values::reject("Unsupported Qt method argument type");
    }
    return method;
}

class SignalTap;
struct Subscription {
    Handle id, owner, target;
    QString property;
    SignalTap *tap;
    std::atomic<bool> live{true};
};
static QHash<Handle, std::shared_ptr<Subscription>> subscriptions;
static QHash<QObject *, QHash<int, SignalTap *>> taps;

// One capture object per sender/signature, retained until the sender dies.
// Disconnecting the last subscriber severs its Qt connection. Retaining this
// tiny receiver makes concurrent direct signal captures safe during disconnect;
// it never holds a managed closure. Services must stop/join producers before
// destroying their sender, as required for any QObject used by a worker.
class SignalTap : public QObject {
public:
    Relay *relay;
    Handle target;
    QMetaMethod signal;
    QMetaObject::Connection connection;
    std::mutex mutex;
    QList<std::shared_ptr<Subscription>> listeners;
    SignalTap(QObject *sender, Relay *relay, Handle target, QMetaMethod signal)
        : QObject(sender), relay(relay), target(target), signal(signal) {}
    int qt_metacall(QMetaObject::Call call, int index, void **args) override {
        index = QObject::qt_metacall(call, index, args);
        if (index < 0) return index;
        if (call != QMetaObject::InvokeMetaMethod || index != 0) return index;
        // Called synchronously on the emitter's thread. Only immutable metadata,
        // copied values, atomics and locked identity/listener maps are used here.
        if (controlledWrites.contains(target)) return -1;
        QList<std::shared_ptr<Subscription>> current;
        { std::lock_guard<std::mutex> lock(mutex); current = listeners; }
        if (current.isEmpty()) return -1;
        QVariantList values;
        for (int i = 0; i < signal.parameterCount(); ++i) {
            auto type = signal.parameterMetaType(i);
            values.append(type.id() == QMetaType::QVariant ? *static_cast<QVariant *>(args[i + 1])
                                                          : QVariant(type, args[i + 1]));
        }
        Values::References refs;
        { std::lock_guard<std::mutex> lock(postMutex); refs = referenceIds; }
        QMetaObject::invokeMethod(relay, [relay = relay, current, values, refs] {
            for (const auto &sub : current) {
                if (!sub->live) continue; // cancellation also fences already queued events
                try {
                    QJsonArray result;
                    if (sub->property.isEmpty()) {
                        // Snapshot refs at emission, not delivery: address reuse
                        // cannot turn a queued reference into a different object.
                        result = Values::encode(values, 0, refs, true);
                    } else {
                        auto *target = objectFor(sub->target);
                        if (!target) continue;
                        const auto name = sub->property.toUtf8();
                        const char *wire = gqt_read_value(sub->target, name.constData());
                        if (!wire) Values::reject(error.constData());
                        result = Values::node("list", QJsonArray{Values::parse(wire)});
                    }
                    const auto bytes = QJsonDocument(result).toJson(QJsonDocument::Compact);
                    relay->notify(sub->id, "signal", bytes.constData());
                } catch (const std::exception &e) {
                    relay->notify(sub->id, "signal-error", e.what());
                }
            }
        }, Qt::QueuedConnection);
        return -1;
    }
};
static void disconnect(Handle token) {
    auto sub = subscriptions.take(token);
    if (!sub) return;
    sub->live = false;
    auto *tap = sub->tap;
    {
        std::lock_guard<std::mutex> lock(tap->mutex);
        tap->listeners.removeAll(sub);
        if (tap->listeners.isEmpty()) QObject::disconnect(tap->connection);
    }
    tap->relay->notify(token, "subscription-ended");
}
}
static void cancelSubscriptions(Handle owner) {
    const auto all = Objects::subscriptions;
    for (const auto &sub : all)
        if (sub->owner == owner || sub->target == owner) Objects::disconnect(sub->id);
}
static void stopSubscriptions() {
    for (Handle token : Objects::subscriptions.keys()) Objects::disconnect(token);
}

extern "C" Q_DECL_EXPORT const char *gqt_invoke(Handle id, const char *signature, const char *wire) {
    try {
        auto *object = objectFor(id);
        if (!object) return nullptr;
        auto method = Objects::method(object, signature, false);
        if (method.methodSignature() == "deleteLater()" && !active->relay.disposable.contains(id))
            Values::reject("Cannot destroy a borrowed Qt object");
        auto node = Values::parse(wire);
        if (node[0] != "list") Values::reject("Qt method arguments must be a vector");
        auto args = Values::array(node[1]);
        if (args.size() != method.parameterCount()) Values::reject("Wrong Qt method argument count");
        std::array<QVariant, 10> values;
        std::array<QGenericArgument, 10> argv;
        auto names = method.parameterTypes();
        for (int i = 0; i < args.size(); ++i) {
            values[i] = Values::converted(Values::decode(args[i]), method.parameterMetaType(i));
            argv[i] = QGenericArgument(names[i].constData(), method.parameterMetaType(i).id() == QMetaType::QVariant
                                       ? &values[i] : values[i].constData());
        }
        const auto returnType = method.returnMetaType();
        QVariant result(returnType.id() == QMetaType::Void ? QMetaType() : returnType);
        QGenericReturnArgument returned;
        if (returnType.id() != QMetaType::Void)
            returned = QGenericReturnArgument(method.typeName(), returnType.id() == QMetaType::QVariant ? &result : result.data());
        if (!method.invoke(object, Qt::DirectConnection, returned, argv[0], argv[1], argv[2], argv[3], argv[4],
                           argv[5], argv[6], argv[7], argv[8], argv[9])) Values::reject("Qt method invocation failed");
        return Values::result(Values::encode(result));
    } catch (const std::exception &e) { fail(e.what()); return nullptr; }
    catch (...) { fail("Qt method threw an unknown native exception"); return nullptr; }
}
extern "C" Q_DECL_EXPORT Handle gqt_subscribe(Handle owner, Handle target, const char *name, int property) {
    try {
        if (!objectFor(owner)) return 0;
        auto *object = objectFor(target);
        if (!object) return 0;
        if (!active->accepting) { fail("Qt session is shutting down"); return 0; }
        QMetaMethod signal;
        if (property) {
            // Deliberately direct properties only: grouped paths may have a
            // different QObject sender and require a separate exposed handle.
            int index = object->metaObject()->indexOfProperty(name);
            if (index < 0) Values::reject("Missing Qt property");
            auto prop = object->metaObject()->property(index);
            if (!prop.isReadable() || !prop.hasNotifySignal()) Values::reject("Qt property requires readable NOTIFY metadata");
            signal = prop.notifySignal();
            if (!gqt_read_value(target, name)) Values::reject(error.constData());
        } else signal = Objects::method(object, name, true);
        // Validate notify signal arguments too, before admitting the connection.
        signal = Objects::method(object, signal.methodSignature().constData(), true);
        auto *tap = Objects::taps.value(object).value(signal.methodIndex());
        if (!tap) {
            tap = new Objects::SignalTap(object, &active->relay, target, signal);
            Objects::taps[object].insert(signal.methodIndex(), tap);
            QObject::connect(object, &QObject::destroyed, &active->relay, [object] { Objects::taps.remove(object); });
        }
        auto sub = std::make_shared<Objects::Subscription>();
        sub->id = freshHandle(); sub->owner = owner; sub->target = target; sub->tap = tap;
        if (property) sub->property = QString::fromUtf8(name);
        {
            std::lock_guard<std::mutex> lock(tap->mutex);
            if (tap->listeners.isEmpty()) {
                tap->connection = QMetaObject::connect(object, signal.methodIndex(), tap,
                    QObject::staticMetaObject.methodCount(), Qt::DirectConnection, nullptr);
                if (!tap->connection) Values::reject("Qt signal connection failed");
            }
            tap->listeners.append(sub);
        }
        Objects::subscriptions.insert(sub->id, sub);
        return sub->id;
    } catch (const std::exception &e) { fail(e.what()); return 0; }
}
extern "C" Q_DECL_EXPORT int gqt_disconnect(Handle token) {
    if (!guiThread()) return 0;
    Objects::disconnect(token);
    return 1;
}
extern "C" Q_DECL_EXPORT int gqt_subscription_count() {
    return guiThread() ? Objects::subscriptions.size() : -1;
}
extern "C" Q_DECL_EXPORT int gqt_write_controlled(Handle id, const char *name, const char *wire) {
    if (!guiThread()) return 0;
    const bool already = controlledWrites.contains(id);
    controlledWrites.insert(id);
    int result = gqt_write_value(id, name, wire);
    if (!already) controlledWrites.remove(id);
    return result;
}
// Explicitly expose a native-owned descendant, without adopting or reparenting.
// The extension vouches for the live pointer. Returned handles become stale on
// native deletion or invalidation of any ancestor. Raw pointers never cross Jolt.
extern "C" GQT_API uint64_t gqt_expose_object(QObject *object) {
    if (!guiThread() || !active || !object) return 0;
    if (object->thread() != QCoreApplication::instance()->thread()) { fail("Qt objects require GUI affinity"); return 0; }
    auto known = active->relay.identities.value(object);
    if (known) return known;
    for (auto *p = object->parent(); p; p = p->parent()) {
        if (active->relay.identities.contains(p)) {
            QQmlEngine::setObjectOwnership(object, QQmlEngine::CppOwnership);
            return active->relay.track(object, false);
        }
    }
    fail("Exposed Qt object must descend from a live registered object");
    return 0;
}
