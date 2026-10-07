#include <QApplication>
#include <QImage>
#include <QPointer>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlProperty>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QThread>
#include <QTimer>
#include <QDebug>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#ifdef Q_OS_MACOS
#include <pthread.h>
#endif

using Handle = uint64_t;
using EventHandler = void (*)(Handle, const char *, const char *);
static Handle nextHandle = 0;
static int ownedObjects = 0;
static thread_local QByteArray error;
static int fail(const char *message) { error = message; return 0; }
static Handle freshHandle() {
    // Never recycle identities, even after a session ends or an address is reused.
    if (nextHandle == std::numeric_limits<Handle>::max()) qFatal("Qt handle space exhausted");
    return ++nextHandle;
}

class Relay : public QObject {
    Q_OBJECT
public:
    EventHandler handler = nullptr;
    int callbackDepth = 0;
    QHash<Handle, QPointer<QObject>> objects;
    QHash<QObject *, Handle> identities;

    void notify(Handle id, const char *event, const char *value = "") {
        if (handler) {
            ++callbackDepth;
            handler(id, event, value);
            --callbackDepth;
        }
    }
    Handle track(QObject *object) {
        const Handle id = freshHandle();
        objects.insert(id, object);
        identities.insert(object, id);
        ++ownedObjects;
        connect(object, &QObject::destroyed, this, [this, object, id] {
            --ownedObjects;
            if (objects.remove(id)) {
                identities.remove(object);
                notify(id, "destroyed");
            }
        });
        return id;
    }
    void invalidate(QObject *root) {
        // Includes registered descendants, but never exposes Qt's internal objects.
        QList<Handle> removed;
        for (auto it = objects.cbegin(); it != objects.cend(); ++it) {
            for (QObject *p = it.value(); p; p = p->parent()) {
                if (p == root) { removed.append(it.key()); break; }
            }
        }
        for (Handle id : removed) identities.remove(objects.take(id));
        for (Handle id : removed) notify(id, "destroyed");
    }
    Q_INVOKABLE void dispatch(QObject *widget, const QString &event, const QString &value) {
        const Handle id = identities.value(widget);
        if (!id) return; // An event queued by a removed control is inert.
        const auto eventBytes = event.toUtf8();
        const auto valueBytes = value.toUtf8();
        notify(id, eventBytes.constData(), valueBytes.constData());
    }
};

// One application for the process; sessions/engines may be opened repeatedly.
// argv outlives QApplication, including its process-exit destructor.
struct Application {
    int argc = 1;
    char program[16] = "glimmer-qt";
    char *argv[2] = {program, nullptr};
    QApplication app{argc, argv};
};
// Process-owned: Jolt may terminate from a worker thread, so a C++ static
// destructor must not tear down QApplication there. The OS reclaims this one
// application at process exit; every session/engine/object is closed explicitly.
static Application *application = nullptr;
struct Session {
    Handle id = freshHandle();
    Relay relay;
    QQmlEngine engine;
    QPointer<QQuickWindow> window;
    Handle container = 0;
    bool exitRequested = false;
    bool executing = false;
    int exitCode = 0;

    QObject *create(const QString &tag) {
        QQmlComponent component(&engine, QUrl("qrc:/glimmer-qt/" + tag + ".qml"));
        QVariantMap props;
        if (tag == "button" || tag == "entry") props.insert("relay", QVariant::fromValue(&relay));
        auto *object = component.createWithInitialProperties(props);
        if (!object) { qWarning() << component.errors(); fail("Qt component creation failed"); return nullptr; }
        QQmlEngine::setObjectOwnership(object, QQmlEngine::CppOwnership);
        object->setParent(&engine);
        return object;
    }
};
// Posting is the sole cross-thread operation. The lock fences session teardown.
static std::mutex postMutex;
static Session *active = nullptr;
static bool guiThread() {
    if (!QCoreApplication::instance() || !QThread::isMainThread()) {
        fail("Qt operation requires the application main thread");
        return false;
    }
    return true;
}
static Session *sessionFor(Handle id) {
    if (!guiThread()) return nullptr;
    if (!active || !id || active->id != id) { fail("Invalid or disposed Qt session handle"); return nullptr; }
    return active;
}
static QObject *objectFor(Handle id) {
    if (!guiThread()) return nullptr;
    QObject *object = active ? active->relay.objects.value(id).data() : nullptr;
    if (!object) fail("Invalid or disposed Qt object handle");
    return object;
}
static QQuickItem *itemFor(Handle id) {
    auto *item = qobject_cast<QQuickItem *>(objectFor(id));
    if (!item) fail("Invalid or disposed Qt item handle");
    return item;
}

extern "C" Q_DECL_EXPORT const char *gqt_error() { return error.constData(); }
extern "C" Q_DECL_EXPORT Handle gqt_open(const char *title, int width, int height, const char *appId) {
    // Qt does not know its main thread until the first application is created.
    // The managed entry point uses host/call-on-main-thread on every platform.
#ifdef Q_OS_MACOS
    if (!pthread_main_np()) { fail("Qt session must open on the main thread"); return 0; }
#endif
    if (QCoreApplication::instance() && !QThread::isMainThread()) {
        fail("Qt session must open on the main thread"); return 0;
    }
    if (active) { fail("A Qt session is already running"); return 0; }
    if (!application) {
        if (QCoreApplication::instance()) { fail("An external Qt application already exists"); return 0; }
        QQuickStyle::setStyle("Material");
        application = new Application;
    }
    auto session = std::make_unique<Session>();
    application->app.setApplicationName(QString::fromUtf8(appId));
    session->window = qobject_cast<QQuickWindow *>(session->create("window"));
    if (!session->window) return 0;
    session->relay.track(session->window);
    auto *container = session->window->property("mountPoint").value<QObject *>();
    if (!container) { fail("Qt window has no mount point"); return 0; }
    session->container = session->relay.track(container);
    session->window->setTitle(QString::fromUtf8(title));
    session->window->resize(width, height);
    QObject::connect(session->window, &QObject::destroyed, &session->relay, [] { QCoreApplication::exit(1); });
    std::lock_guard<std::mutex> lock(postMutex);
    active = session.release();
    return active->id;
}
extern "C" Q_DECL_EXPORT Handle gqt_container(Handle id) {
    auto *s = sessionFor(id);
    return s ? s->container : 0;
}
extern "C" Q_DECL_EXPORT int gqt_set_handler(Handle id, EventHandler handler) {
    auto *s = sessionFor(id);
    if (!s) return 0;
    s->relay.handler = handler;
    return 1;
}
extern "C" Q_DECL_EXPORT Handle gqt_create(Handle id, const char *tag) {
    auto *s = sessionFor(id);
    if (!s) return 0;
    const QString type = QString::fromUtf8(tag);
    if (type != "vbox" && type != "hbox" && type != "label" && type != "entry" && type != "button") {
        fail("Unsupported Qt component tag"); return 0;
    }
    auto *object = s->create(type);
    return object ? s->relay.track(object) : 0;
}
static int setProperty(Handle id, const char *property, const QVariant &value) {
    auto *object = objectFor(id);
    if (!object) return 0;
    QQmlProperty prop(object, QString::fromUtf8(property), qmlContext(object));
    // Equal writes preserve cursor/selection and QML bindings.
    if (!prop.isValid() || !(prop.read() == value || prop.write(value))) return fail("Qt rejected a property");
    return 1;
}
extern "C" Q_DECL_EXPORT int gqt_set_string(Handle id, const char *prop, const char *value) {
    return setProperty(id, prop, QString::fromUtf8(value));
}
extern "C" Q_DECL_EXPORT int gqt_set_number(Handle id, const char *prop, double value) {
    return setProperty(id, prop, value);
}
extern "C" Q_DECL_EXPORT int gqt_set_bool(Handle id, const char *prop, int value) {
    return setProperty(id, prop, bool(value));
}
extern "C" Q_DECL_EXPORT int gqt_append(Handle parentId, Handle childId) {
    auto *parent = itemFor(parentId);
    auto *child = itemFor(childId);
    if (!parent || !child) return 0;
    if (child->parentItem()) return fail("Qt child already has a parent");
    for (auto *p = parent; p; p = p->parentItem())
        if (p == child) return fail("Qt ownership cycle");
    child->setParent(parent);
    child->setParentItem(parent);
    return 1;
}
extern "C" Q_DECL_EXPORT int gqt_reorder(Handle parentId, Handle childId, Handle siblingId) {
    auto *parent = itemFor(parentId);
    auto *child = itemFor(childId);
    auto *sibling = siblingId ? itemFor(siblingId) : nullptr;
    if (!parent || !child || (siblingId && !sibling)) return 0;
    if (child->parentItem() != parent || (sibling && (sibling == child || sibling->parentItem() != parent)))
        return fail("Qt reorder requires distinct children of the same parent");
    if (sibling) child->stackAfter(sibling);
    else if (!parent->childItems().isEmpty() && parent->childItems().first() != child)
        child->stackBefore(parent->childItems().first());
    return 1;
}
extern "C" Q_DECL_EXPORT int gqt_remove(Handle id) {
    auto *child = itemFor(id);
    if (!child) return 0;
    if (id == active->container) return fail("Cannot remove the session mount point");
    active->relay.invalidate(child);
    child->setParentItem(nullptr);
    child->setParent(&active->engine);
    // May be inside this control's signal delivery. Invalidate now, delete later.
    child->deleteLater();
    return 1;
}
extern "C" Q_DECL_EXPORT int gqt_post(Handle sessionId, Handle token, int delayMs) {
    std::lock_guard<std::mutex> lock(postMutex);
    if (!active || active->id != sessionId || delayMs < 0) return 0;
    auto *relay = &active->relay;
    return QMetaObject::invokeMethod(relay, [relay, token, delayMs] {
        QTimer::singleShot(delayMs, relay, [relay, token] {
            const auto value = QByteArray::number(qulonglong(token));
            relay->notify(0, "work", value.constData());
        });
    }, Qt::QueuedConnection);
}
extern "C" Q_DECL_EXPORT int gqt_exec(Handle id) {
    auto *s = sessionFor(id);
    if (!s) return -1;
    if (s->executing) { fail("Qt event loop is already running"); return -1; }
    if (s->exitRequested) return s->exitCode;
    if (!s->window) { fail("Qt window was destroyed"); return -1; }
    s->window->show();
    s->executing = true;
    const int code = application->app.exec();
    s->executing = false;
    return code;
}
extern "C" Q_DECL_EXPORT int gqt_exit(Handle id, int code) {
    auto *s = sessionFor(id);
    if (!s) return 0;
    s->exitRequested = true;
    s->exitCode = code;
    QCoreApplication::exit(code);
    return 1;
}
extern "C" Q_DECL_EXPORT int gqt_close(Handle id) {
    auto *s = sessionFor(id);
    if (!s) return 0;
    if (s->executing || s->relay.callbackDepth)
        return fail("Cannot close an executing Qt session; request exit first");
    {
        std::lock_guard<std::mutex> lock(postMutex);
        active = nullptr;
    }
    s->relay.handler = nullptr;
    delete s;
    return 1;
}
// Diagnostics/test operations obey the same checked handle and thread contract.
extern "C" Q_DECL_EXPORT int gqt_alive(Handle id) {
    return guiThread() && active && active->relay.objects.contains(id) && active->relay.objects.value(id);
}
extern "C" Q_DECL_EXPORT int gqt_live_count() {
    return guiThread() && active ? active->relay.objects.size() : 0;
}
extern "C" Q_DECL_EXPORT int gqt_owned_count() {
    return guiThread() ? ownedObjects : -1;
}
extern "C" Q_DECL_EXPORT const char *gqt_read(Handle id, const char *prop) {
    auto *object = objectFor(id);
    if (!object) return nullptr;
    QQmlProperty property(object, QString::fromUtf8(prop), qmlContext(object));
    if (!property.isValid()) { fail("Invalid Qt property"); return nullptr; }
    static thread_local QByteArray result;
    result = property.read().toString().toUtf8();
    return result.constData();
}
extern "C" Q_DECL_EXPORT int gqt_emit(Handle id, const char *signal) {
    auto *object = objectFor(id);
    if (!object) return 0;
    return QMetaObject::invokeMethod(object, signal, Qt::DirectConnection) || fail("Invalid Qt signal");
}
extern "C" Q_DECL_EXPORT int gqt_save_window(Handle id, const char *path) {
    auto *s = sessionFor(id);
    if (!s || !s->window) return 0;
    return s->window->grabWindow().save(QString::fromUtf8(path)) || fail("Could not save Qt window image");
}
extern "C" Q_DECL_EXPORT int gqt_request_close(Handle id) {
    auto *s = sessionFor(id);
    return s && s->window && s->window->close();
}
extern "C" Q_DECL_EXPORT int gqt_destroy_native(Handle id) {
    auto *object = objectFor(id);
    if (!object) return 0;
    object->deleteLater();
    return 1;
}
#include "bridge.moc"
