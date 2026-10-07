#include "../../glimmer-qt/native/extensions.h"
#include "../../src/appsettings.h"
#include "../../src/backend.h"
#include "../../src/editorcanvas.h"

#include <QApplication>
#include <QDir>
#include <QStandardPaths>

namespace {
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
public:
    Services() {
        m_settings = new AppSettings(applicationPaths(), this);
        m_backend = new Backend(m_settings, this);
        // Backend owns a member TrimSession; parent it for the bridge's
        // descendant check, then expose only a borrowed handle to Jolt.
        m_backend->trim()->setParent(m_backend);
    }

    QObject *settings() const { return m_settings; }
    QObject *backend() const { return m_backend; }
    QObject *trim() const { return m_backend->trim(); }

    Q_INVOKABLE bool expose() {
        return gqt_expose_object(m_settings) && gqt_expose_object(m_backend)
            && gqt_expose_object(m_backend->trim());
    }
private:
    AppSettings *m_settings = nullptr;
    Backend *m_backend = nullptr;
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
