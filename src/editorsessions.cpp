#include "editorsessions.h"
#include "appsettings.h"
#include "backend.h"
#include <QFileInfo>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QTimer>

struct EditorSessions::Session : QObject {
    explicit Session(QObject *parent) : QObject(parent) {}
    ~Session() override {
        // QML bindings need their context and backend during window teardown.
        delete window;
        delete context;
    }
    QQmlContext *context = nullptr;
    Backend *backend = nullptr;
    QQuickWindow *window = nullptr;
};

EditorSessions::EditorSessions(QQmlEngine *engine, AppSettings *settings, QObject *parent)
    : QObject(parent), m_engine(engine), m_settings(settings),
      m_component(engine, QUrl(QStringLiteral("qrc:/Main.qml"))) {}

EditorSessions::~EditorSessions() {
    // Also includes sessions queued for deletion after a QML close handler.
    const auto owned = children();
    for (QObject *child : owned) delete child;
}

bool EditorSessions::isReady() const { return m_component.isReady(); }

QList<QQuickWindow *> EditorSessions::windows() const {
    QList<QQuickWindow *> result;
    for (auto *session : m_sessions) result.append(session->window);
    return result;
}

Backend *EditorSessions::createBackend(QObject *owner) { return new Backend(m_settings, owner); }

EditorSessions::Session *EditorSessions::createSession(const QString &label) {
    if (!isReady()) {
        emit error(m_component.errorString());
        return nullptr;
    }
    auto *session = new Session(this);
    session->backend = createBackend(session);
    session->context = new QQmlContext(m_engine->rootContext(), session);
    session->context->setContextProperty(QStringLiteral("backend"), session->backend);
    session->context->setContextProperty(QStringLiteral("appSettings"), m_settings);
    session->context->setContextProperty(QStringLiteral("initialImage"), QUrl());
    session->context->setContextProperty(QStringLiteral("startInBackground"), true);
    session->context->setContextProperty(QStringLiteral("showOnStart"), false);
    QObject *root = m_component.createWithInitialProperties({
        {QStringLiteral("sessionManager"), QVariant::fromValue(this)},
        {QStringLiteral("sessionLabel"), label.isEmpty() ? tr("Capture %1").arg(m_nextNumber++) : label}
    }, session->context);
    session->window = qobject_cast<QQuickWindow *>(root);
    if (!session->window) {
        emit error(m_component.errorString());
        delete root;
        delete session;
        return nullptr;
    }
    root->setParent(session);
    QQmlEngine::setObjectOwnership(root, QQmlEngine::CppOwnership);
    m_sessions.append(session);
    connect(session->window, &QWindow::activeChanged, session, [this, session] {
        if (session->window->isActive()) m_lastActive = session;
    });
    // Main.qml consumes each temporary capture file synchronously before Backend
    // removes it. Only this session's QML context receives its capture signals.
    connect(session->backend, &Backend::captureFinished, session, [this, session] {
        if (!session->backend->recording()) completeCapture(session);
    });
    connect(session->backend, &Backend::recordingSaved, session, [this, session] { completeCapture(session); });
    connect(session->backend, &Backend::recordingCanceled, session, [this, session] { completeCapture(session); });
    connect(session->backend, &Backend::error, session, [this, session] {
        if (!session->backend->capturing() && !session->backend->recording()) completeCapture(session);
    });
    auto *trim = qobject_cast<TrimSession *>(session->backend->trim());
    connect(trim, &TrimSession::finalized, session, [this, session] {
        // Finish the QML review's close handler before destroying its objects.
        QTimer::singleShot(0, session, [this, session] {
            if (!session->window->isVisible()) removeSession(session);
        });
    });
    return session;
}

void EditorSessions::activate(Session *session) {
    if (!session) return;
    auto *trim = qobject_cast<TrimSession *>(session->backend->trim());
    if (!trim->path().isEmpty()) {
        auto *review = session->window->findChild<QQuickWindow *>(QStringLiteral("recordingReviewWindow"));
        if (review) {
            if (review->visibility() == QWindow::Minimized) review->showNormal();
            review->show(); review->raise(); review->requestActivate();
            return;
        }
    }
    QMetaObject::invokeMethod(session->window, "showEditor");
}

void EditorSessions::showEditor() {
    if (m_capture) return;
    Session *session = m_lastActive;
    if (!session && !m_sessions.isEmpty()) session = m_sessions.last();
    if (!session) session = createSession();
    activate(session);
}

void EditorSessions::openImage(const QUrl &file) {
    auto *session = createSession(QFileInfo(file.toLocalFile()).fileName());
    if (session) QMetaObject::invokeMethod(session->window, "loadImage", Q_ARG(QVariant, file));
}

void EditorSessions::hotkeyCapture() {
    if (m_capture && m_capture->backend->recording()) {
        m_capture->backend->stopRecordingFromHotkey();
        return;
    }
    capture();
}

void EditorSessions::capture(bool multiple, bool video) {
    if (m_capture) return;
    for (auto *session : m_sessions) {
        if (!qobject_cast<TrimSession *>(session->backend->trim())->path().isEmpty()) {
            activate(session);
            return;
        }
    }
    auto *session = createSession();
    if (!session) return;
    m_capture = session;
    m_restoreFocus = nullptr;
    for (auto *existing : m_sessions) {
        auto *window = existing->window;
        if (window->isActive()) m_restoreFocus = window;
        if (window->isVisible()) m_hiddenEditors.append({window, window->visibility()});
    }
    for (const auto &hidden : m_hiddenEditors) hidden.window->hide();
    QMetaObject::invokeMethod(session->window, "startCapture", Q_ARG(QVariant, multiple), Q_ARG(QVariant, video));
}

void EditorSessions::completeCapture(Session *session) {
    if (m_capture != session) return;
    m_capture = nullptr;
    for (const auto &hidden : m_hiddenEditors)
        if (hidden.window) hidden.window->setVisibility(hidden.visibility);
    m_hiddenEditors.clear();
    if (session->window->isVisible()
            || !qobject_cast<TrimSession *>(session->backend->trim())->path().isEmpty()) {
        activate(session);
    } else {
        if (m_restoreFocus) {
            m_restoreFocus->raise();
            m_restoreFocus->requestActivate();
        }
        removeSession(session);
    }
    m_restoreFocus = nullptr;
}

void EditorSessions::closeEditor(QObject *window) {
    for (auto *session : m_sessions) {
        if (session->window == window) {
            removeSession(session);
            return;
        }
    }
}

void EditorSessions::removeSession(Session *session) {
    if (!m_sessions.removeOne(session)) return;
    if (m_lastActive == session) m_lastActive = nullptr;
    session->deleteLater();
}
