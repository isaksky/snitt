#include "../../glimmer-qt/native/extensions.h"
#include "../../src/appsettings.h"
#include "../../src/backend.h"
#include "../../src/editorcanvas.h"
#include "../../src/globalhotkey.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QImageReader>
#include <QKeySequence>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QPointer>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <memory>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace {
QPointer<GlobalHotkey> applicationHotkey;

class Lifecycle final : public QObject {
    Q_OBJECT
public:
    explicit Lifecycle(QObject *parent = nullptr) : QObject(parent) {
        const QString user = QString::fromLatin1(QCryptographicHash::hash(
            QDir::homePath().toUtf8(), QCryptographicHash::Sha256).toHex().left(16));
        const QString identity = qEnvironmentVariable("SNITT_JOLT_IPC_ID", "development");
        const QString suffix = QString::fromLatin1(QCryptographicHash::hash(
            identity.toUtf8(), QCryptographicHash::Sha256).toHex().left(12));
        // Keep the Unix socket path under macOS's short sun_path limit even
        // when the test identity is long and TempLocation is deeply nested.
        m_name = QStringLiteral("snittj-") + user + QStringLiteral("-") + suffix;
#ifdef Q_OS_WIN
        DWORD session = 0;
        ProcessIdToSessionId(GetCurrentProcessId(), &session);
        m_name += QStringLiteral("-%1").arg(session);
#endif
        m_lock.reset(new QLockFile(QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                                       .filePath(m_name + QStringLiteral(".lock"))));
        connect(&m_server, &QLocalServer::newConnection, this, [this] {
            while (auto *socket = m_server.nextPendingConnection()) {
                socket->setParent(this);
                socket->setReadBufferSize(64 * 1024);
                connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
                const auto read = [this, socket] {
                    if (!socket->canReadLine()) {
                        if (socket->bytesAvailable() >= 64 * 1024) socket->disconnectFromServer();
                        return;
                    }
                    QJsonParseError error{};
                    const QJsonDocument document = QJsonDocument::fromJson(socket->readLine(64 * 1024), &error);
                    if (error.error != QJsonParseError::NoError || !document.isArray()) {
                        socket->disconnectFromServer();
                        return;
                    }
                    QStringList args;
                    for (const auto &value : document.array()) {
                        if (!value.isString()) { socket->disconnectFromServer(); return; }
                        args.append(value.toString());
                    }
                    emit commandReceived(args);
                    socket->write("OK\n");
                    socket->flush();
                    socket->disconnectFromServer();
                };
                connect(socket, &QLocalSocket::readyRead, this, read);
                QTimer::singleShot(2000, socket, [socket] { socket->disconnectFromServer(); });
                read();
            }
        });
    }

    Q_INVOKABLE QString absolutePath(const QString &path) const {
        return QFileInfo(path).absoluteFilePath();
    }
    Q_INVOKABLE QString fileUrl(const QString &path) const {
        return QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded);
    }
    Q_INVOKABLE QString start(const QStringList &arguments) {
        if (m_server.isListening()) return QStringLiteral("owner");
        Probe probe = forward(arguments);
        if (probe == Delivered) return QStringLiteral("forwarded");
        if (probe == Absent && arguments.contains(QStringLiteral("--quit")))
            return QStringLiteral("quit");
        if (!m_lock->tryLock(0)) {
            for (int attempt = 0; attempt < 10; ++attempt) {
                QThread::msleep(100);
                if (forward(arguments) == Delivered) return QStringLiteral("forwarded");
            }
            return QStringLiteral("error");
        }
        // A refused or unresponsive socket may belong to a live process whose
        // accept backlog is full. The lock alone cannot prove it is abandoned.
        probe = forward(arguments);
        if (probe == Delivered) return QStringLiteral("forwarded");
        if (probe == Unresponsive) {
            qWarning() << "Snitt Jolt endpoint is unresponsive:" << m_name;
            return QStringLiteral("error");
        }
        m_server.setSocketOptions(QLocalServer::UserAccessOption);
        if (!m_server.listen(m_name)) {
            qWarning() << "Snitt Jolt could not listen:" << m_server.errorString();
            return QStringLiteral("error");
        }
        return QStringLiteral("owner");
    }
signals:
    void commandReceived(const QStringList &arguments);
private:
    enum Probe { Delivered, Absent, Unresponsive };
    Probe forward(const QStringList &arguments) const {
        QLocalSocket socket;
        socket.connectToServer(m_name);
        if (!socket.waitForConnected(500)) {
            return socket.error() == QLocalSocket::ServerNotFoundError
                ? Absent : Unresponsive;
        }
        socket.write(QJsonDocument(QJsonArray::fromStringList(arguments))
                         .toJson(QJsonDocument::Compact) + '\n');
        if (!socket.waitForBytesWritten(1000) && socket.bytesToWrite()) return Unresponsive;
        if (!socket.waitForReadyRead(1000) && !socket.canReadLine()) return Unresponsive;
        return socket.readLine() == QByteArray("OK\n") ? Delivered : Unresponsive;
    }
    QString m_name;
    std::unique_ptr<QLockFile> m_lock;
    QLocalServer m_server;
};

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
            if (m_canvas) m_canvas->loadExternalRegions(images);
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

    Q_INVOKABLE bool loadRegionFilesForTest(const QStringList &paths) {
        QVariantList images;
        for (const QString &path : paths) {
            QImageReader reader(path);
            const QImage image = reader.read();
            if (image.isNull()) return false;
            images.append(image);
        }
        return m_canvas && m_canvas->loadExternalRegions(images);
    }

    Q_INVOKABLE bool expose() {
        return gqt_expose_object(m_settings) && gqt_expose_object(m_backend)
            && gqt_expose_object(m_backend->trim());
    }
    Q_INVOKABLE bool activateGlobalShortcut(const QString &configured) {
        if (qEnvironmentVariable("SNITT_JOLT_FAKE_HOTKEY") == QStringLiteral("1")) {
            m_hotkeyError.clear();
            const QString portable = m_settings->shortcutInfo(configured)
                .value(QStringLiteral("portable")).toString();
            const QKeySequence sequence = QKeySequence::fromString(portable, QKeySequence::PortableText);
            if (sequence.count() != 1 || sequence[0].key() == Qt::Key_unknown
#ifdef Q_OS_MACOS
                || (sequence[0].key() >= Qt::Key_F21 && sequence[0].key() <= Qt::Key_F35)
#endif
                ) {
                m_hotkeyError = QStringLiteral("The test hotkey adapter rejects this native key.");
                return false;
            }
            return true;
        }
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
    Q_INVOKABLE void emitGlobalCaptureForTest() {
        if (qEnvironmentVariable("SNITT_JOLT_FAKE_HOTKEY") == QStringLiteral("1"))
            emit globalCaptureActivated();
    }
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
QObject *lifecycleFactory() { return new Lifecycle; }
}

static void initializeSnittResources() { Q_INIT_RESOURCE(resources); }

extern "C" Q_DECL_EXPORT int snitt_jolt_register() {
    if (!qobject_cast<QApplication *>(qApp)) return 0;
    initializeSnittResources();
    return gqt_register_factory("snitt-jolt.canvas", canvasFactory, nullptr)
        && gqt_register_factory("snitt-jolt.services", servicesFactory, nullptr)
        && gqt_register_factory("snitt-jolt.lifecycle", lifecycleFactory, nullptr);
}

#include "snitt_jolt.moc"
