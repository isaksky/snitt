#include "../../glimmer-qt/native/extensions.h"
#include "../../src/appsettings.h"
#include "../../src/backend.h"
#include "../../src/editorcanvas.h"
#include "../../src/globalhotkey.h"

#include <QApplication>
#include <QDir>
#include <QPointer>
#include <QKeySequence>
#include <QStandardPaths>

namespace {
QPointer<GlobalHotkey> applicationHotkey;

AppSettings::Paths applicationPaths() {
    // This development entry point must never read production settings or save
    // into the installed application's default picture/video directories.
    QString root = qEnvironmentVariable("SNITT_JOLT_DATA_DIR");
    if (root.isEmpty())
        root = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    const QDir directory(root);
    return {directory.filePath("settings.ini"), directory.filePath("settings-format.md"),
            directory.filePath("pictures"), directory.filePath("videos")};
}

class Services final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QObject *settings READ settings CONSTANT)
    Q_PROPERTY(QObject *backend READ backend CONSTANT)
    Q_PROPERTY(QObject *trim READ trim CONSTANT)
    Q_PROPERTY(bool macOS READ macOS CONSTANT)
public:
    Services() {
        m_settings = new AppSettings(applicationPaths(), true, this);
        m_backend = new Backend(m_settings, this);
        // Backend owns a member TrimSession; parent it for the bridge's
        // descendant check, then expose only a borrowed handle to Jolt.
        m_backend->trim()->setParent(m_backend);
        // Backend removes its temporary capture file immediately after emitting
        // captured. Copy it into this session's document on the native stack;
        // Jolt's queued signal delivery is intentionally too late for the file.
        connect(m_backend, &Backend::captured, this, [this](const QUrl &url) {
            if (m_canvas) m_canvas->load(url);
        });
        connect(m_backend, &Backend::regionsCaptured, this, [this](const QVariantList &images) {
            if (m_canvas) m_canvas->loadRegions(images);
        });
    }

    QObject *settings() const { return m_settings; }
    QObject *backend() const { return m_backend; }
    QObject *trim() const { return m_backend->trim(); }
    bool macOS() const {
#ifdef Q_OS_MACOS
        return true;
#else
        return false;
#endif
    }

    Q_INVOKABLE bool bindCanvas(QObject *canvas) {
        auto *editor = qobject_cast<EditorCanvas *>(canvas);
        if (!editor || m_canvas) return false;
        m_canvas = editor;
        connect(editor, &QObject::destroyed, this, [this] { m_canvas = nullptr; });
        return true;
    }

    Q_INVOKABLE bool expose() {
        return gqt_expose_object(m_settings) && gqt_expose_object(m_backend)
            && gqt_expose_object(m_backend->trim());
    }
    Q_INVOKABLE bool activateGlobalShortcut(const QString &configured) {
        if (!applicationHotkey) applicationHotkey = new GlobalHotkey(qApp);
        if (!m_hotkeyConnected) {
            connect(applicationHotkey, &GlobalHotkey::activated, this, &Services::globalCaptureActivated);
            m_hotkeyConnected = true;
        }
        m_hotkeyError.clear();
        const QString portable = m_settings->shortcutInfo(configured).value(QStringLiteral("portable")).toString();
        return applicationHotkey->setShortcut(
            QKeySequence::fromString(portable, QKeySequence::PortableText), &m_hotkeyError);
    }
    Q_INVOKABLE QString hotkeyError() const { return m_hotkeyError; }
signals:
    void globalCaptureActivated();
private:
    AppSettings *m_settings = nullptr;
    Backend *m_backend = nullptr;
    QPointer<EditorCanvas> m_canvas;
    QString m_hotkeyError;
    bool m_hotkeyConnected = false;
};

QObject *canvasFactory() { return new EditorCanvas; }
QObject *servicesFactory() { return new Services; }
}

static void initializeSnittResources() { Q_INIT_RESOURCE(resources); }

extern "C" Q_DECL_EXPORT int snitt_jolt_register() {
    if (!qobject_cast<QApplication *>(qApp)) return 0;
    initializeSnittResources();
    return gqt_register_factory("snitt-jolt.canvas", canvasFactory, nullptr)
        && gqt_register_factory("snitt-jolt.services", servicesFactory, nullptr);
}

#include "snitt_jolt.moc"
