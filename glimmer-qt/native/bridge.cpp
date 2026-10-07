#include <QApplication>
#include <QClipboard>
#include <QMimeData>
#include "extensions.h"
#include <QElapsedTimer>
#include <QCloseEvent>
#include <QKeyEvent>
#include <QInputMethodEvent>
#include <QSet>
#include <QImage>
#include <QFontMetricsF>
#include <QPointer>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlProperty>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QThread>
#include <QTimer>
#include <QWidget>
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
static int ownedTimers = 0;
static thread_local QByteArray error;
static int fail(const char *message) { error = message; return 0; }
static Handle freshHandle() {
    // Never recycle identities, even after a session ends or an address is reused.
    if (nextHandle == std::numeric_limits<Handle>::max()) qFatal("Qt handle space exhausted");
    return ++nextHandle;
}

// This mutex protects admission, native job identities and owner liveness. It is
// never held across a managed callback. Qt objects are accessed only on the GUI
// thread; queued commands use the relay as context so teardown drops them.
static std::mutex postMutex;
static QSet<Handle> liveOwners;
static void cancelOwner(Handle owner);
static void cancelSubscriptions(Handle owner);
static void stopSubscriptions();
// Worker signal capture consults identities only under postMutex.
static QHash<QObject *, Handle> referenceIds;
static thread_local QSet<Handle> controlledWrites;

class Relay : public QObject {
    Q_OBJECT
public:
    EventHandler handler = nullptr;
    QHash<Handle, bool> pointerReplies;
    Q_INVOKABLE bool pointer(QObject *widget, const QString &event, QVariantMap payload);
    int callbackDepth = 0;
    QHash<Handle, QPointer<QObject>> objects;
    QHash<QObject *, Handle> identities;
    QSet<Handle> disposable;
    // Preserve identities of references written through the managed boundary,
    // even if a raw QObject* in a QVariant survives deletion/address reuse.
    QHash<Handle, QHash<QString, QHash<QObject *, Handle>>> valueReferences;

    void notify(Handle id, const char *event, const char *value = "") {
        if (handler) {
            ++callbackDepth;
            handler(id, event, value);
            --callbackDepth;
        }
    }
    Handle track(QObject *object, bool canDispose = true) {
        const Handle id = freshHandle();
        objects.insert(id, object);
        identities.insert(object, id);
        if (canDispose) disposable.insert(id);
        ++ownedObjects;
        { std::lock_guard<std::mutex> lock(postMutex); liveOwners.insert(id); referenceIds.insert(object, id); }
        connect(object, &QObject::destroyed, this, [this, object, id] {
            { std::lock_guard<std::mutex> lock(postMutex); referenceIds.remove(object); }
            --ownedObjects;
            cancelOwner(id);
            if (objects.remove(id)) {
                identities.remove(object);
                valueReferences.remove(id);
                disposable.remove(id);
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
        for (Handle id : removed) {
            cancelOwner(id);
            { std::lock_guard<std::mutex> lock(postMutex); referenceIds.remove(objects.value(id)); }
            identities.remove(objects.take(id));
            valueReferences.remove(id);
            disposable.remove(id);
        }
        for (Handle id : removed) notify(id, "destroyed");
    }
    Q_INVOKABLE void dispatch(QObject *widget, const QString &event, const QString &value) {
        const Handle id = identities.value(widget);
        if (!id || controlledWrites.contains(id)) return;
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
struct Job {
    Handle token, owner;
    uint64_t generation = 0;
    int delay;
    bool repeat;
    QTimer *timer = nullptr; // GUI thread only
    QElapsedTimer elapsed;
};
struct WidgetServices : QList<QPointer<QWidget>> {
    ~WidgetServices() {
        while (!isEmpty()) delete takeLast().data();
    }
};
struct Session {
    Handle id = freshHandle();
    bool accepting = true; // under postMutex; outlives QObject destruction callbacks
    QHash<Handle, std::shared_ptr<Job>> jobs; // under postMutex
    Relay relay;
    // QWidget::parentWidget() assumes every QObject parent is a QWidget. Retain
    // standalone services explicitly, never parent them to the QML engine.
    // Declared before engine so services outlive its windows during teardown.
    WidgetServices widgetServices;
    QQmlEngine engine;
    QPointer<QQuickWindow> window;
    Handle container = 0;
    QHash<Handle, Handle> windows; // window -> mount point
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
// Posting, cancellation and restart are safe from any thread.
static Session *active = nullptr;
static void retireJob(Relay *relay, const std::shared_ptr<Job> &job) {
    // Called under postMutex, including by workers. Do not inspect timer here.
    QMetaObject::invokeMethod(relay, [job] {
        if (job->timer) { job->timer->stop(); job->timer->deleteLater(); job->timer = nullptr; }
    }, Qt::QueuedConnection);
}
static void cancelOwner(Handle owner) {
    QList<Handle> canceled;
    Relay *relay = nullptr;
    {
        std::lock_guard<std::mutex> lock(postMutex);
        liveOwners.remove(owner);
        if (!active) return;
        relay = &active->relay;
        for (auto it = active->jobs.begin(); it != active->jobs.end();) {
            if (it.value()->owner == owner) {
                canceled.append(it.key());
                retireJob(relay, it.value());
                it = active->jobs.erase(it);
            } else ++it;
        }
    }
    cancelSubscriptions(owner);
    for (Handle token : canceled) relay->notify(token, "work-canceled");
}
static void stopJobs(Session *s) {
    {
        std::lock_guard<std::mutex> lock(postMutex);
        if (!s->accepting) return;
        s->accepting = false;
        for (const auto &job : s->jobs) retireJob(&s->relay, job);
        s->jobs.clear();
    }
    stopSubscriptions();
    s->relay.notify(0, "shutdown");
}
static int enqueueJob(Session *s, const std::shared_ptr<Job> &job) {
    // Caller holds postMutex. The identity check fences cancel/restart before
    // installation, as well as timer events already queued when canceled.
    return QMetaObject::invokeMethod(&s->relay, [s, job] {
        std::lock_guard<std::mutex> lock(postMutex);
        if (s->jobs.value(job->token) != job) return;
        auto *timer = new QTimer(&s->relay);
        ++ownedTimers;
        QObject::connect(timer, &QObject::destroyed, [] { --ownedTimers; });
        job->timer = timer;
        timer->setTimerType(Qt::PreciseTimer);
        timer->setSingleShot(!job->repeat);
        job->elapsed.start();
        QObject::connect(timer, &QTimer::timeout, &s->relay, [s, job] {
            double seconds;
            {
                std::lock_guard<std::mutex> lock(postMutex);
                if (s->jobs.value(job->token) != job) return;
                seconds = job->elapsed.nsecsElapsed() / 1000000000.0;
                if (!job->repeat) {
                    s->jobs.remove(job->token);
                    job->timer->deleteLater();
                    job->timer = nullptr;
                }
            }
            const auto value = QByteArray::number(qulonglong(job->generation)) + "," + QByteArray::number(seconds, 'g', 17);
            s->relay.notify(job->token, "work", value.constData());
        });
        timer->start(job->delay);
    }, Qt::QueuedConnection);
}
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
    else if (object->thread() != QCoreApplication::instance()->thread()) {
        fail("Qt objects require GUI affinity"); return nullptr;
    }
    return object;
}
static QQuickItem *itemFor(Handle id) {
    auto *item = qobject_cast<QQuickItem *>(objectFor(id));
    if (!item) fail("Invalid or disposed Qt item handle");
    return item;
}

extern "C" Q_DECL_EXPORT const char *gqt_error() { return error.constData(); }
// Consume the native close event first. Managed policy runs on a later turn,
// never on QWindow's close-event stack, and duplicate pending requests coalesce.
class CloseFilter : public QObject {
public:
    Session *session;
    Handle window;
    bool pending = false;
    CloseFilter(Session *s, Handle id, QObject *parent)
        : QObject(parent), session(s), window(id) {}
    bool eventFilter(QObject *, QEvent *event) override {
        if (event->type() != QEvent::Close) return false;
        static_cast<QCloseEvent *>(event)->ignore();
        if (!pending) {
            pending = true;
            QTimer::singleShot(0, this, [this] {
                pending = false;
                if (session->accepting && session->windows.contains(window))
                    session->relay.notify(window, "close-request");
            });
        }
        return true;
    }
};
extern "C" Q_DECL_EXPORT Handle gqt_open_application(const char *appId) {
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
    application->app.setQuitOnLastWindowClosed(false);
    application->app.setApplicationName(QString::fromUtf8(appId));
    auto session = std::make_unique<Session>();
    auto *s = session.get();
    QObject::connect(&application->app, &QCoreApplication::aboutToQuit, &session->relay, [s] { stopJobs(s); });
    std::lock_guard<std::mutex> lock(postMutex);
    active = session.release();
    return active->id;
}
extern "C" Q_DECL_EXPORT Handle gqt_window_create(Handle id, const char *title, int width, int height) {
    auto *s = sessionFor(id);
    if (!s) return 0;
    if (!s->accepting) { fail("Qt application is shutting down"); return 0; }
    auto *window = qobject_cast<QQuickWindow *>(s->create("window"));
    if (!window) return 0;
    auto *container = window->property("mountPoint").value<QObject *>();
    if (!container) { delete window; fail("Qt window has no mount point"); return 0; }
    Handle handle = s->relay.track(window, false);
    Handle mount = s->relay.track(container);
    s->windows.insert(handle, mount);
    if (!s->container) { s->window = window; s->container = mount; }
    window->installEventFilter(new CloseFilter(s, handle, window));
    window->setTitle(QString::fromUtf8(title));
    window->resize(width, height);
    return handle;
}
extern "C" Q_DECL_EXPORT Handle gqt_window_container(Handle id) {
    if (!objectFor(id) || !active->windows.contains(id)) { fail("Invalid Qt window handle"); return 0; }
    return active->windows.value(id);
}
static QQuickWindow *windowFor(Handle id) {
    auto *window = qobject_cast<QQuickWindow *>(objectFor(id));
    if (!window || !active->windows.contains(id)) { fail("Invalid or disposed Qt window handle"); return nullptr; }
    return window;
}
extern "C" Q_DECL_EXPORT int gqt_window_action(Handle id, int action) {
    auto *window = windowFor(id);
    if (!window) return 0;
    switch (action) {
    case 0: window->show(); break;
    case 1: window->hide(); break;
    case 2: window->showMinimized(); break;
    case 3: window->showNormal(); break;
    case 4:
        if (window->windowState() == Qt::WindowMinimized) window->showNormal();
        window->show(); window->raise(); window->requestActivate(); break;
    case 5: window->raise(); break;
    case 6: window->requestActivate(); break;
    case 7: {
        // sendEvent also handles hidden windows; QWindow::close may skip those.
        QCloseEvent event;
        QCoreApplication::sendEvent(window, &event);
        break;
    }
    default: return fail("Invalid Qt window action");
    }
    return 1;
}
extern "C" Q_DECL_EXPORT int gqt_window_destroy(Handle id) {
    auto *window = windowFor(id);
    if (!window) return 0;
    active->windows.remove(id);
    active->relay.invalidate(window);
    window->hide();
    window->deleteLater();
    return 1;
}
// Preserve the original low-level single-window entry point.
extern "C" Q_DECL_EXPORT Handle gqt_open(const char *title, int width, int height, const char *appId) {
    Handle id = gqt_open_application(appId);
    if (!id) return 0;
    Handle window = gqt_window_create(id, title, width, height);
    if (!window) {
        auto *s = active;
        { std::lock_guard<std::mutex> lock(postMutex); active = nullptr; }
        delete s;
        return 0;
    }
    active->window = windowFor(window);
    active->container = active->windows.value(window);
    active->window->show();
    return id;
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
#include "values.h"

bool Relay::pointer(QObject *widget, const QString &event, QVariantMap payload) {
    const Handle id = identities.value(widget);
    auto *item = qobject_cast<QQuickItem *>(widget);
    if (!id || !item || (!item->isEnabled() && event != "on-pointer-cancel")) return false;
    const Handle token = freshHandle();
    payload.insert("token", QVariant::fromValue(qulonglong(token)));
    pointerReplies.insert(token, false);
    const QByteArray data = QJsonDocument(Values::encode(payload)).toJson(QJsonDocument::Compact);
    notify(id, event.toUtf8().constData(), data.constData());
    return pointerReplies.take(token);
}
extern "C" Q_DECL_EXPORT int gqt_pointer_accept(Handle token, int accepted) {
    if (!guiThread() || !active || !active->relay.pointerReplies.contains(token))
        return fail("Pointer event is no longer active");
    active->relay.pointerReplies[token] = accepted != 0;
    return 1;
}
// Owned by the watched item. Only key events are intercepted; IME preedit and
// commit events continue untouched to Qt's text control.
class KeyFilter : public QObject {
public:
    bool composing = false;
    explicit KeyFilter(QQuickItem *item) : QObject(item) { item->installEventFilter(this); }
    bool eventFilter(QObject *object, QEvent *event) override {
        if (event->type() == QEvent::InputMethod)
            composing = !static_cast<QInputMethodEvent *>(event)->preeditString().isEmpty();
        if (event->type() == QEvent::FocusOut) composing = false;
        if (event->type() != QEvent::KeyPress && event->type() != QEvent::KeyRelease) return false;
        auto *item = static_cast<QQuickItem *>(object);
        if (!item->isEnabled() || composing || item->property("inputMethodComposing").toBool()) return false;
        const char *name = event->type() == QEvent::KeyPress ? "on-key-press" : "on-key-release";
        if (!item->property(name).toBool()) return false;
        auto *key = static_cast<QKeyEvent *>(event);
        // Unknown/dead keys belong to the input method, not command handlers.
        if (!key->key() || key->key() == Qt::Key_unknown ||
            (key->key() >= Qt::Key_Dead_Grave && key->key() <= Qt::Key_Dead_Longsolidusoverlay)) return false;
        QVariantMap payload{{"key", key->key()}, {"text", key->text()},
                            {"modifiers", int(key->modifiers())}, {"auto-repeat", key->isAutoRepeat()},
                            {"count", key->count()}};
        QPointer<QQuickItem> guard(item);
        const bool accepted = active->relay.pointer(item, QString::fromLatin1(name), payload);
        if (accepted) key->accept();
        return accepted || !guard;
    }
};
extern "C" Q_DECL_EXPORT int gqt_keys(Handle id, int press, int release) {
    auto *item = itemFor(id); if (!item) return 0;
    if (!item->property("gqtKeyFilter").toBool()) {
        new KeyFilter(item);
        item->setProperty("gqtKeyFilter", true);
    }
    item->setProperty("on-key-press", press != 0);
    item->setProperty("on-key-release", release != 0);
    return 1;
}
extern "C" Q_DECL_EXPORT int gqt_focus(Handle id, int direction) {
    auto *item = itemFor(id); if (!item) return 0;
    if (!item->window()) return fail("Focus requires an attached item");
    if (direction < -1 || direction > 1) return fail("Invalid focus direction");
    auto *target = direction ? item->nextItemInFocusChain(direction > 0) : item;
    if (!target || !target->isVisible() || !target->isEnabled()) return fail("Focus target is unavailable");
    target->forceActiveFocus(direction > 0 ? Qt::TabFocusReason : direction < 0 ? Qt::BacktabFocusReason : Qt::OtherFocusReason);
    return 1;
}
extern "C" Q_DECL_EXPORT const char *gqt_map_point(Handle from, Handle to, double x, double y) {
    auto *source = itemFor(from);
    if (!source) return nullptr;
    auto *target = to ? itemFor(to) : nullptr;
    if (to && !target) return nullptr;
    if (!source->window() || (target && !target->window()) || !std::isfinite(x) || !std::isfinite(y)) {
        fail("Coordinate mapping requires attached items and finite coordinates"); return nullptr;
    }
    const QPointF global = source->mapToGlobal(QPointF(x, y));
    const QPointF result = target ? target->mapFromGlobal(global) : global;
    return Values::result(Values::encode(QVariantList{result.x(), result.y()}));
}

extern "C" Q_DECL_EXPORT const char *gqt_measure_text(Handle id, const char *text) {
    try {
        auto *object = objectFor(id);
        if (!object) return nullptr;
        auto font = Values::property(object, "font").read();
        if (font.metaType() != QMetaType::fromType<QFont>())
            Values::reject("Text measurement requires a font-bearing control");
        const QFontMetricsF metrics(font.value<QFont>());
        const auto str = QString::fromUtf8(text);
        const auto bounds = metrics.boundingRect(str);
        return Values::result(Values::encode(QVariantMap{
            {"advance", metrics.horizontalAdvance(str)}, {"height", metrics.height()},
            {"ascent", metrics.ascent()}, {"descent", metrics.descent()},
            {"underline-position", metrics.underlinePos()}, {"line-width", metrics.lineWidth()},
            {"x", bounds.x()}, {"y", bounds.y()}, {"width", bounds.width()}}));
    } catch (const std::exception &e) { fail(e.what()); return nullptr; }
}

#include "objects.h"
struct Factory { GqtFactory create; GqtComplete complete; };
static QHash<QString, Factory> factories;
extern "C" Q_DECL_EXPORT int gqt_register_factory(const char *name, GqtFactory factory, GqtComplete complete) {
    if (!guiThread()) return 0;
    QString key = QString::fromUtf8(name);
    if (key.isEmpty() || !factory || factories.contains(key)) return fail("Invalid or duplicate Qt factory");
    factories.insert(key, {factory, complete});
    return 1;
}
extern "C" Q_DECL_EXPORT Handle gqt_create_registered(Handle id, const char *source,
                                                        const char *wire, const char *relayProperty) {
    auto *s = sessionFor(id);
    if (!s) return 0;
    QQmlComponent component(&s->engine);
    std::unique_ptr<QObject> object;
    bool begun = false;
    try {
        QString uri = QString::fromUtf8(source);
        Factory factory{};
        if (uri.startsWith("native:")) {
            factory = factories.value(uri.mid(7));
            if (!factory.create) Values::reject("Unregistered Qt native factory");
            object.reset(factory.create());
        } else {
            component.loadUrl(QUrl(uri), QQmlComponent::PreferSynchronous);
            if (component.isError()) throw std::runtime_error(component.errorString().toStdString());
            object.reset(component.beginCreate(s->engine.rootContext()));
            begun = bool(object);
        }
        if (!object) Values::reject("Qt component/factory returned no object");
        if (object->parent()) Values::reject("Qt factory must return an unparented object");
        if (object->thread() != QCoreApplication::instance()->thread()) Values::reject("Qt factory requires GUI affinity");
        auto map = Values::parse(wire);
        if (map[0].toString() != "map") Values::reject("Initial properties must be a map");
        QVariantMap props;
        QHash<QString, Values::References> refs;
        for (const auto &entry : Values::array(map[1])) {
            auto pair = Values::array(entry);
            if (pair.size() != 2) Values::reject("Malformed initial property entry");
            auto name = Values::text(pair[0]);
            if (props.contains(name)) Values::reject("Duplicate initial property");
            auto prop = Values::property(object.get(), name.toUtf8().constData());
            props.insert(name, Values::checked(prop, Values::array(pair[1]), refs[name]));
        }
        if (*relayProperty) {
            auto prop = Values::property(object.get(), relayProperty);
            auto type = QMetaType(prop.propertyType());
            if (!prop.isWritable() || !type.flags().testFlag(QMetaType::PointerToQObject) ||
                !s->relay.metaObject()->inherits(type.metaObject()))
                Values::reject("Relay property must accept a QObject");
            props.insert(QString::fromUtf8(relayProperty), QVariant::fromValue(&s->relay));
        }
        if (begun) {
            // Qt's initial-property API rejects dotted/grouped properties. These
            // cannot be QML required declarations; write them before completion.
            for (auto it = props.begin(); it != props.end();) {
                if (it.key().contains('.')) {
                    if (!QQmlProperty::write(object.get(), it.key(), it.value(), qmlContext(object.get())))
                        Values::reject("Qt rejected grouped initial property");
                    it = props.erase(it);
                } else ++it;
            }
            component.setInitialProperties(object.get(), props);
            component.completeCreate();
            begun = false;
            if (component.isError()) throw std::runtime_error(component.errorString().toStdString());
        } else {
            for (auto it = props.cbegin(); it != props.cend(); ++it)
                if (!QQmlProperty::write(object.get(), it.key(), it.value())) Values::reject("Qt factory property write failed");
            if (factory.complete) factory.complete(object.get());
        }
        QQmlEngine::setObjectOwnership(object.get(), QQmlEngine::CppOwnership);
        if (auto *widget = qobject_cast<QWidget *>(object.get())) {
            s->widgetServices.append(widget);
            QObject::connect(widget, &QObject::destroyed, &s->relay, [s] {
                s->widgetServices.removeIf([](const auto &entry) { return entry.isNull(); });
            });
        } else
            object->setParent(&s->engine);
        Handle handle = s->relay.track(object.release());
        s->relay.valueReferences.insert(handle, refs);
        return handle;
    } catch (const std::exception &e) {
        // Balance beginCreate even when schema validation rejects an initial value.
        if (begun) component.completeCreate();
        error = QByteArray(source) + ": " + e.what();
        if (component.isError()) error += "\n" + component.errorString().toUtf8();
        return 0;
    }
}
// Composition targets are owned descendants, never arbitrary external objects.
extern "C" Q_DECL_EXPORT Handle gqt_child_target(Handle id, const char *property) {
    auto *object = objectFor(id);
    if (!object) return 0;
    if (!*property) return id;
    auto *target = QQmlProperty::read(object, QString::fromUtf8(property)).value<QObject *>();
    bool owned = false;
    for (auto *p = target; p; p = p->parent()) if (p == object) owned = true;
    if (!owned) { fail("Child target must be an owned QObject"); return 0; }
    auto handle = active->relay.identities.value(target);
    return handle ? handle : active->relay.track(target, false);
}
extern "C" Q_DECL_EXPORT int gqt_append(Handle parentId, Handle childId) {
    auto *parent = objectFor(parentId);
    auto *child = objectFor(childId);
    if (!parent || !child) return 0;
    if (parent->isWidgetType() || child->isWidgetType())
        return fail("QWidget services cannot be composed into a Qt Quick tree");
    if (!active->relay.disposable.contains(childId)) return fail("Cannot reparent a borrowed Qt object");
    if (!qobject_cast<QQuickItem *>(child)) {
        for (auto *p = parent; p; p = p->parent()) if (p == child) return fail("Qt ownership cycle");
        if (child->parent() != &active->engine) return fail("Qt child already has a parent");
        child->setParent(parent); return 1;
    }
    auto *parentItem = qobject_cast<QQuickItem *>(parent);
    auto *childItem = qobject_cast<QQuickItem *>(child);
    if (!parentItem) return fail("Visual child requires a visual parent");
    if (childItem->parentItem()) return fail("Qt child already has a parent");
    for (auto *p = parentItem; p; p = p->parentItem())
        if (p == child) return fail("Qt ownership cycle");
    child->setParent(parent);
    childItem->setParentItem(parentItem);
    return 1;
}
extern "C" Q_DECL_EXPORT int gqt_is_item(Handle id) {
    return qobject_cast<QQuickItem *>(objectFor(id)) != nullptr;
}
extern "C" Q_DECL_EXPORT int gqt_reorder(Handle parentId, Handle childId, Handle siblingId) {
    auto *obj = objectFor(childId);
    auto *owner = objectFor(parentId);
    if (obj && owner && !qobject_cast<QQuickItem *>(obj))
        return obj->parent() == owner || fail("Qt reorder requires a child of this parent");
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
    auto *child = objectFor(id);
    if (!child) return 0;
    if (id == active->container) return fail("Cannot remove the session mount point");
    if (!active->relay.disposable.contains(id)) return fail("Cannot remove a borrowed Qt object");
    active->relay.invalidate(child);
    if (auto *item = qobject_cast<QQuickItem *>(child)) item->setParentItem(nullptr);
    if (!child->isWidgetType()) child->setParent(&active->engine);
    // May be inside this control's signal delivery. Invalidate now, delete later.
    child->deleteLater();
    return 1;
}
extern "C" Q_DECL_EXPORT Handle gqt_owner(Handle sessionId) {
    auto *s = sessionFor(sessionId);
    return s ? s->relay.track(new QObject(&s->engine)) : 0;
}
extern "C" Q_DECL_EXPORT int gqt_destroy_owner(Handle owner) {
    auto *object = objectFor(owner);
    if (!object) return 0;
    if (!active->relay.disposable.contains(owner)) return fail("Cannot destroy a borrowed Qt object");
    if (object->metaObject() != &QObject::staticMetaObject)
        return fail("Use widget disposal for a widget owner");
    active->relay.invalidate(object);
    object->deleteLater();
    return 1;
}
extern "C" Q_DECL_EXPORT int gqt_schedule(Handle sessionId, Handle owner, Handle token, int delayMs, int repeat) {
    std::lock_guard<std::mutex> lock(postMutex);
    if (!active || active->id != sessionId || !active->accepting || delayMs < 0 ||
        (repeat && !delayMs) || (owner && !liveOwners.contains(owner)) || active->jobs.contains(token)) return 0;
    auto job = std::make_shared<Job>();
    job->token = token; job->owner = owner; job->delay = delayMs; job->repeat = repeat;
    active->jobs.insert(token, job);
    if (enqueueJob(active, job)) return 1;
    active->jobs.remove(token);
    return 0;
}
extern "C" Q_DECL_EXPORT int gqt_post(Handle sessionId, Handle token, int delayMs) {
    return gqt_schedule(sessionId, 0, token, delayMs, 0);
}
extern "C" Q_DECL_EXPORT int gqt_cancel(Handle sessionId, Handle token) {
    std::lock_guard<std::mutex> lock(postMutex);
    if (!active || active->id != sessionId) return 0;
    auto job = active->jobs.take(token);
    if (!job) return 0;
    retireJob(&active->relay, job);
    return 1;
}
extern "C" Q_DECL_EXPORT int gqt_restart(Handle sessionId, Handle token, int delayMs) {
    std::lock_guard<std::mutex> lock(postMutex);
    if (!active || active->id != sessionId || !active->accepting || delayMs < 0) return 0;
    auto previous = active->jobs.value(token);
    if (!previous || (previous->repeat && !delayMs)) return 0;
    auto job = std::make_shared<Job>();
    job->generation = previous->generation + 1;
    job->token = token; job->owner = previous->owner; job->delay = delayMs; job->repeat = previous->repeat;
    retireJob(&active->relay, previous);
    active->jobs.insert(token, job);
    return enqueueJob(active, job);
}
extern "C" Q_DECL_EXPORT int gqt_pending_count() {
    std::lock_guard<std::mutex> lock(postMutex);
    return active ? active->jobs.size() : 0;
}
extern "C" Q_DECL_EXPORT int gqt_timer_count() { return QThread::isMainThread() ? ownedTimers : -1; }
extern "C" Q_DECL_EXPORT int gqt_gui_thread() { return QThread::isMainThread(); }
extern "C" Q_DECL_EXPORT int gqt_exec(Handle id) {
    auto *s = sessionFor(id);
    if (!s) return -1;
    if (s->executing) { fail("Qt event loop is already running"); return -1; }
    if (s->exitRequested) return s->exitCode;
    s->executing = true;
    const int code = application->app.exec();
    s->executing = false;
    stopJobs(s);
    return code;
}
extern "C" Q_DECL_EXPORT int gqt_exit(Handle id, int code) {
    auto *s = sessionFor(id);
    if (!s) return 0;
    stopJobs(s);
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
    stopJobs(s);
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
    return s && s->window ? gqt_window_action(s->relay.identities.value(s->window), 7) : 0;
}
extern "C" Q_DECL_EXPORT int gqt_destroy_native(Handle id) {
    auto *object = objectFor(id);
    if (!object) return 0;
    if (!active->relay.disposable.contains(id)) return fail("Cannot destroy a borrowed Qt object");
    object->deleteLater();
    return 1;
}
#include "bridge.moc"
#ifdef GQT_PROPERTY_TESTS
#include <QTest>
#include "../test/native/pointer_fixture.h"
#include "../test/native/value_fixture.h"
#include "../test/native/object_fixture.h"
extern "C" Q_DECL_EXPORT int gqt_register_object_fixture() {
    return gqt_register_factory("test.object", []() -> QObject * { return new ObjectFixture; }, nullptr);
}
extern "C" Q_DECL_EXPORT int gqt_register_window_fixture() {
    return gqt_register_factory("test.widget", []() -> QObject * {
        if (!qobject_cast<QApplication *>(QCoreApplication::instance())) return nullptr;
        return new QWidget;
    }, nullptr);
}
extern "C" Q_DECL_EXPORT int gqt_test_click(Handle id) {
    auto *item = itemFor(id);
    if (!item || !item->window()) return 0;
    QTest::mouseClick(item->window(), Qt::LeftButton, Qt::NoModifier,
                     item->mapToScene(QPointF(item->width()/2, item->height()/2)).toPoint());
    return 1;
}
extern "C" Q_DECL_EXPORT int gqt_test_action_input(Handle id, int action) {
    auto *item = itemFor(id);
    if (!item || !item->window()) return 0;
    auto *window = item->window();
    const auto point = item->mapToScene(QPointF(item->width()/2, item->height()/2)).toPoint();
    switch (action) {
    case 0: QTest::mouseMove(window, point); break;
    case 1: QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, point); break;
    case 2: QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, point); break;
    case 3: QTest::keyClick(window, Qt::Key_Tab); break;
    default: return fail("Unknown test input");
    }
    return 1;
}
extern "C" Q_DECL_EXPORT int gqt_test_key(Handle id, int key, int modifiers) {
    auto *window = qobject_cast<QQuickWindow *>(objectFor(id)); if (!window) return 0;
    // QtTest forwards its delay argument as the synthetic key count.
    QTest::keyClick(window, Qt::Key(key), Qt::KeyboardModifiers(modifiers), 1);
    return 1;
}
extern "C" Q_DECL_EXPORT int gqt_test_modal(Handle id, int show) {
    auto *window = qobject_cast<QQuickWindow *>(objectFor(id)); if (!window) return 0;
    static QPointer<QWindow> dialog;
    if (show) {
        if (dialog) return 0;
        dialog = new QWindow(window);
        dialog->setModality(Qt::WindowModal);
        dialog->setTransientParent(window);
        dialog->resize(200, 100);
        dialog->show();
        dialog->requestActivate();
    } else { delete dialog.data(); dialog = nullptr; }
    return 1;
}
extern "C" Q_DECL_EXPORT int gqt_test_paste(Handle id, const char *text) {
    auto *window = qobject_cast<QQuickWindow *>(objectFor(id)); if (!window) return 0;
    auto *previous = new QMimeData;
    if (const auto *mime = QApplication::clipboard()->mimeData())
        for (const auto &format : mime->formats()) previous->setData(format, mime->data(format));
    QApplication::clipboard()->setText(QString::fromUtf8(text));
    QTest::keyClick(window, Qt::Key_V, Qt::ControlModifier);
    QApplication::clipboard()->setMimeData(previous);
    return 1;
}
extern "C" Q_DECL_EXPORT int gqt_test_ime(Handle id, int phase) {
    auto *item = itemFor(id); if (!item) return 0;
    QInputMethodEvent event(phase == 0 ? QString::fromUtf8("に") : QString(), {});
    if (phase == 1) event.setCommitString(QString::fromUtf8("日本"));
    QCoreApplication::sendEvent(item, &event);
    return 1;
}
extern "C" Q_DECL_EXPORT int gqt_test_space(Handle id) {
    auto *window = windowFor(id);
    if (!window) return 0;
    QTest::keyClick(window, Qt::Key_Space);
    return 1;
}
extern "C" Q_DECL_EXPORT int gqt_test_awaiting_gc() { return ObjectFixture::awaitingGc; }
extern "C" Q_DECL_EXPORT void gqt_test_gc_completed() { ObjectFixture::collected = true; }
extern "C" Q_DECL_EXPORT Handle gqt_value_fixture(Handle id) {
    auto *s = sessionFor(id);
    return s ? s->relay.track(new ValueFixture(&s->engine)) : 0;
}
// Deterministic address reuse exercises raw QVariant QObject* lifetime fencing.
extern "C" Q_DECL_EXPORT Handle gqt_reuse_test_object(Handle id) {
    auto *object = objectFor(id);
    if (!object || object->metaObject() != &QObject::staticMetaObject) return 0;
    object->~QObject();
    return active->relay.track(new (object) QObject(&active->engine));
}
#endif
