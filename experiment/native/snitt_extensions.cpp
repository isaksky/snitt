#include "../../glimmer-qt/native/extensions.h"
#include "../../src/appsettings.h"
#include "../../src/backend.h"
#include "../../src/editorcanvas.h"
#include "../../src/globalhotkey.h"
#include "../../src/regionselector.h"
#include "../../src/trimsession.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QLocalServer>
#include <QLocalSocket>
#include <QUuid>

namespace {
QString fixtureRoot;

AppSettings::Paths fixturePaths() {
    const QDir dir(fixtureRoot);
    return {dir.filePath("settings.ini"), dir.filePath("settings-format.md"),
            dir.filePath("pictures"), dir.filePath("videos")};
}

class FakeBackend final : public Backend {
    Q_OBJECT
public:
    explicit FakeBackend(AppSettings *settings, QObject *parent)
        : Backend(settings, parent) {}

    // The inherited capture method is invokable. This override exercises its
    // production signal contract without permission prompts or screen access.
    void capture(bool multiple = false, bool video = false) override {
        Q_UNUSED(multiple);
        Q_UNUSED(video);
        emit capturingChanged();
        emit error(QStringLiteral("fixture capture error"));
        emit captureFinished(false);
    }
    Q_INVOKABLE void fakeRecording() {
        emit recordingChanged();
        emit recordingReady();
    }
};

class SnittServices final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QObject *settings READ settings CONSTANT)
    Q_PROPERTY(QObject *backend READ backend CONSTANT)
    Q_PROPERTY(QObject *trim READ trim CONSTANT)
    Q_PROPERTY(QString ipcName READ ipcName CONSTANT)
public:
    SnittServices() {
        m_settings = new AppSettings(fixturePaths(), this);
        m_backend = new FakeBackend(m_settings, this);
        m_backend->trim()->setParent(m_backend);
        m_hotkey = new GlobalHotkey(this);
        // The same QApplication installed by glimmer-qt owns this server and
        // the hotkey filter. Neither callback enters Jolt from a native thread.
        connect(m_hotkey, &GlobalHotkey::activated, this, &SnittServices::hotkeyActivated);
        m_server = new QLocalServer(this);
        const QString name = QStringLiteral("snitt-glimmer-%1")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
        if (m_server->listen(name)) {
            connect(m_server, &QLocalServer::newConnection, this, [this] {
                while (auto *socket = m_server->nextPendingConnection()) {
                    socket->setParent(this);
                    connect(socket, &QLocalSocket::readyRead, this, [this, socket] {
                        if (!socket->canReadLine()) return;
                        emit ipcCommand(QString::fromUtf8(socket->readLine().trimmed()));
                        socket->disconnectFromServer();
                    });
                    connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
                }
            });
        }
    }
    QObject *settings() const { return m_settings; }
    QObject *backend() const { return m_backend; }
    QObject *trim() const { return m_backend->trim(); }
    QString ipcName() const { return m_server->serverName(); }

    Q_INVOKABLE bool expose() {
        // Only descendants of this bridge-owned root may be borrowed by Jolt.
        return gqt_expose_object(m_settings) && gqt_expose_object(m_backend)
            && gqt_expose_object(m_backend->trim());
    }
    Q_INVOKABLE bool reloadSettings() { return m_settings->reloadNow(); }
    Q_INVOKABLE void openTrim(const QString &path) {
        static_cast<TrimSession *>(m_backend->trim())->open(path);
    }
    Q_INVOKABLE void fakeHotkey() { emit m_hotkey->activated(); }
    Q_INVOKABLE bool sendIpc(const QString &command) {
        if (!m_server->isListening()) return false;
        QLocalSocket socket;
        socket.connectToServer(m_server->serverName());
        if (!socket.waitForConnected(1000)) return false;
        const QByteArray payload = command.toUtf8() + '\n';
        return socket.write(payload) == payload.size() && socket.flush();
    }
signals:
    void hotkeyActivated();
    void ipcCommand(const QString &command);
private:
    AppSettings *m_settings = nullptr;
    FakeBackend *m_backend = nullptr;
    GlobalHotkey *m_hotkey = nullptr;
    QLocalServer *m_server = nullptr;
};

class SelectorHost final : public QWidget {
    Q_OBJECT
    Q_PROPERTY(QObject *selector READ selector CONSTANT)
    Q_PROPERTY(bool sharedApplication READ sharedApplication CONSTANT)
    Q_PROPERTY(bool iconsReady READ iconsReady CONSTANT)
public:
    SelectorHost() {
        m_settings = new AppSettings(fixturePaths(), this);
        QImage image(32, 24, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::green);
        m_selector = new RegionSelector(image, QRect(0, 0, 32, 24), m_settings);
        m_selector->setParent(this);
        m_selector->hide();
    }
    QObject *selector() const { return m_selector; }
    bool sharedApplication() const { return qobject_cast<QApplication *>(qApp) != nullptr; }
    bool iconsReady() const { return QFile::exists(QStringLiteral(":/icons/copy.svg")); }
    Q_INVOKABLE bool expose() { return gqt_expose_object(m_selector) != 0; }
private:
    AppSettings *m_settings = nullptr;
    RegionSelector *m_selector = nullptr;
};

QObject *canvasFactory() { return new EditorCanvas; }
QObject *servicesFactory() { return new SnittServices; }
QObject *selectorFactory() { return new SelectorHost; }
}

static void initializeSnittResources() { Q_INIT_RESOURCE(resources); }

extern "C" Q_DECL_EXPORT int snitt_fixture_root(const char *path) {
    if (!qobject_cast<QApplication *>(qApp) || !path || !*path || fixtureRoot.size()) return 0;
    const QFileInfo info(QString::fromUtf8(path));
    if (!info.isDir()) return 0;
    fixtureRoot = info.absoluteFilePath();
    return 1;
}

extern "C" Q_DECL_EXPORT int snitt_register_experiment() {
    if (!qobject_cast<QApplication *>(qApp) || fixtureRoot.isEmpty()) return 0;
    initializeSnittResources();
    return gqt_register_factory("snitt.canvas", canvasFactory, nullptr)
        && gqt_register_factory("snitt.services", servicesFactory, nullptr)
        && gqt_register_factory("snitt.selector", selectorFactory, nullptr);
}

#include "snitt_extensions.moc"
