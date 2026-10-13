#include "../../glimmer-qt/native/extensions.h"
#include "../../src/appsettings.h"
#include "../../src/backend.h"
#include "../../src/editorcanvas.h"
#include "../../src/globalhotkey.h"
#include "media_jobs.h"
#include "document_bridge.h"

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
#include <QMutex>
#include <QMutexLocker>
#include <QPointer>
#include <QPainter>
#include <QSvgRenderer>
#include <QTextLayout>
#include <QStyleOptionButton>
#include <QPushButton>
#include <QLabel>
#include <QSystemTrayIcon>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QWindow>
#include <QWidget>
#include <QScreen>
#include <QUuid>
#include <memory>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace {
QPointer<GlobalHotkey> applicationHotkey;
QMutex pickerImagesMutex;
QHash<QString, QImage> pickerImages;

QImage pickerImageRequest(void *, const QString &id, QSize *, const QSize &) {
    QMutexLocker lock(&pickerImagesMutex);
    return pickerImages.value(id);
}

// Match the selector's QIcon rasterization at the destination display scale.
// This is artwork only; selection, modes and all commands remain Jolt-owned.
QImage selectorIconRequest(void *, const QString &id, QSize *size, const QSize &requested) {
    const auto parts = id.split('/');
    if (parts.size() != 2 || !QStringList{"square-dashed", "copy", "video", "trash"}.contains(parts[0]))
        return {};
    QSvgRenderer svg(QStringLiteral(":/icons/") + parts[0] + QStringLiteral(".svg"));
    QPixmap artwork(96, 96);
    artwork.fill(Qt::transparent);
    {
        QPainter painter(&artwork);
        svg.render(&painter);
        painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        painter.fillRect(artwork.rect(), QColor(parts[1] == "checked" ? "#15283d" : "#e9edf3"));
    }
    artwork.setDevicePixelRatio(4);
    QIcon icon(artwork);
    const int extent = parts[0] == "trash" ? 20 : 22;
    const QSize pixels = !requested.isEmpty() ? requested : QSize(extent, extent);
    QImage result(pixels, QImage::Format_ARGB32_Premultiplied);
    result.setDotsPerMeterX(qRound(QGuiApplication::primaryScreen()->logicalDotsPerInchX() / 0.0254));
    result.setDotsPerMeterY(qRound(QGuiApplication::primaryScreen()->logicalDotsPerInchY() / 0.0254));
    result.setDevicePixelRatio(qreal(pixels.width()) / extent);
    result.fill(Qt::transparent);
    QPainter painter(&result);
    icon.paint(&painter, QRect(0, 0, extent, extent), Qt::AlignCenter,
               parts[1] == "disabled" ? QIcon::Disabled : QIcon::Normal);
    if (size) *size = pixels;
    return result;
}

// Raster primitives keep QWidget's edge and text metrics in the Quick selector.
// The provider is stateless: Jolt supplies all labels, dimensions and modes.
QImage selectorArtworkRequest(void *, const QString &id, QSize *size, const QSize &requested) {
    const auto parts = id.split('/');
    if (parts.size() < 4) return {};
    const QString kind = parts[0];
    const QSizeF logicalSize(parts[1].toDouble(), parts[2].toDouble());
    const QSize logical = logicalSize.toSize();
    if (logical.isEmpty() || logical.width() > 16384 || logical.height() > 16384) return {};
    const QSize pixels = !requested.isEmpty() ? requested : logical;
    QImage result(pixels, QImage::Format_ARGB32_Premultiplied);
    result.setDotsPerMeterX(qRound(QGuiApplication::primaryScreen()->logicalDotsPerInchX() / 0.0254));
    result.setDotsPerMeterY(qRound(QGuiApplication::primaryScreen()->logicalDotsPerInchY() / 0.0254));
    result.setDevicePixelRatio(qreal(pixels.width()) / logicalSize.width());
    result.fill(Qt::transparent);
    QPainter painter(&result);
    const QRect bounds(QPoint(), logical);
    const QString label = QUrl::fromPercentEncoding(parts.mid(3).join('/').toUtf8());
    if (kind == "label") {
        const auto fields = label.split('|');
        if (fields.size() < 4) return {};
        QFont font = QApplication::font(); font.setPixelSize(fields[0].toInt());
        painter.setFont(font); painter.setPen(QColor(fields[1]));
        painter.drawText(bounds, Qt::AlignVCenter | (fields[2] == "left" ? Qt::AlignLeft : fields[2] == "right" ? Qt::AlignRight : Qt::AlignHCenter), fields.mid(3).join('|'));
    } else if (kind == "mode") {
        const auto fields = label.split('|');
        if (fields.size() < 6) return {};
        QFont font = QApplication::font(); font.setPixelSize(fields[0].toInt());
        QPushButton measure;
        measure.resize(logical); measure.setFont(font);
        measure.setStyleSheet("border: 1px solid transparent; border-radius: 6px; padding: 6px 10px;");
        measure.ensurePolished();
        QStyleOptionButton option; option.initFrom(&measure); option.rect = bounds;
        const QRectF contents = measure.style()->subElementRect(QStyle::SE_PushButtonContents, &option, &measure);
        QTextLayout layout(fields.mid(5).join('|'), font);
        if (fields[3] == "true") {
            QTextCharFormat format; format.setUnderlineStyle(QTextCharFormat::SingleUnderline);
            layout.setFormats({{0, 1, format}});
        }
        layout.beginLayout(); auto line = layout.createLine(); line.setLineWidth(contents.width()); layout.endLayout();
        const qreal x = contents.center().x() - (26 + line.naturalTextWidth()) / 2;
        const auto state = fields[1] == "false" ? "disabled" : fields[2] == "true" ? "checked" : "normal";
        const QImage icon = selectorIconRequest(nullptr, fields[4] + "/" + state, nullptr,
                                                 QSize(qRound(22 * result.devicePixelRatio()), qRound(22 * result.devicePixelRatio())));
        painter.drawImage(QPointF(qRound(x), qRound(contents.center().y() - 11)), icon);
        painter.setPen(fields[1] == "false" ? QColor("#727a86") : fields[2] == "true" ? QColor("#15283d") : QColor("#e9edf3"));
        layout.draw(&painter, QPointF(x + 26, contents.center().y() - line.height() / 2));
    } else if (kind == "border") {
        const auto fields = label.split('|');
        if (fields.size() != 4) return {};
        painter.setPen(QPen(QColor("#91bff0"), 2));
        painter.drawRect(QRectF(fields[0].toDouble(), fields[1].toDouble(),
                               fields[2].toDouble(), fields[3].toDouble()));
    } else if (kind == "badge" || kind == "dimensions") {
        QFont font = QApplication::font();
        font.setPixelSize(kind == "badge" ? 16 : 14);
        font.setBold(kind == "badge");
        painter.setFont(font);
        const auto fields = label.split('|');
        const QRectF textBounds = kind == "badge"
            ? QRectF(fields.value(1).toDouble(), fields.value(2).toDouble(), 28, 28)
            : QRectF(bounds);
        if (kind == "badge") painter.fillRect(textBounds, QColor("#91bff0"));
        else {
            painter.setPen(Qt::NoPen); painter.setBrush(QColor("#1b1e23"));
            painter.drawRoundedRect(bounds, 5, 5);
        }
        painter.setPen(kind == "badge" ? QColor("#15283d") : Qt::white);
        painter.drawText(textBounds, Qt::AlignCenter, kind == "badge" ? fields[0] : label);
    } else {
        QWidget surface;
        surface.setAttribute(Qt::WA_TranslucentBackground);
        surface.resize(logical);
        // A child retains the same stylesheet paint path as the live reference.
        std::unique_ptr<QWidget> widget(kind == "notice"
            ? static_cast<QWidget *>(new QLabel(label, &surface)) : new QWidget(&surface));
        QWidget &control = *widget;
        control.setAttribute(Qt::WA_StyledBackground);
        control.setGeometry(bounds);
        QString style;
        if (kind == "panel") style = "background: rgba(27,30,35,190); border: 1px solid #414751; border-radius: 12px;";
        else if (kind == "button") style = QString("background: %1; border: 1px solid transparent; border-radius: 6px;").arg(label);
        else if (kind == "remove") style = QString("background: %1; border: 1px solid #91bff0; border-radius: 5px;").arg(label);
        else if (kind == "notice") {
            auto *notice = static_cast<QLabel *>(&control);
            notice->setWordWrap(true);
            notice->setAlignment(Qt::AlignCenter);
            style = "color: #f1d4a2; background: #1b1e23; border-radius: 6px; padding: 10px;";
        }
        else return {};
        control.setStyleSheet(style);
        control.ensurePolished();
        surface.render(&painter, QPoint(), QRegion(), QWidget::DrawChildren);
    }
    if (size) *size = pixels;
    return result;
}

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
    QString root = qEnvironmentVariable("SNITT_JOLT_DATA_DIR");
    if (root.isEmpty() && qEnvironmentVariableIsSet("SNITT_JOLT_PRODUCTION"))
        return AppSettings::standardPaths();
    if (root.isEmpty())
        root = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    const QDir directory(root);
    return {directory.filePath("settings.ini"), directory.filePath("settings-format.md"),
            directory.filePath("pictures"), directory.filePath("videos")};
}

class Services final : public QObject {
    Q_OBJECT
    MediaJobs *m_media = nullptr;
    Q_PROPERTY(QObject *settings READ settings CONSTANT)
    Q_PROPERTY(QObject *backend READ backend CONSTANT)
    Q_PROPERTY(QObject *trim READ trim CONSTANT)
    Q_PROPERTY(bool macOS READ macOS CONSTANT)
public:
    Services() {
        m_settings = new AppSettings(applicationPaths(), true, this);
        m_backend = new Backend(m_settings, this);
        m_media = new MediaJobs(this);
        m_backend->setExternalCaptureRouting(true);
        m_backend->setExternalRegionPicker(true);
        m_backend->setExternalRecording(true);
        connect(m_backend, &Backend::recordingSourceReady, this, [this](const recording::Source &source) {
            m_recordingSource = source;
            m_sourceAvailable = true;
            emit recordingSourceReady();
        });
        connect(m_backend, &Backend::pickerReady, this, [this] {
            clearPickerImages();
            m_pickerScreens = m_backend->pickerScreens();
            const QString captureId = QUuid::createUuid().toString(QUuid::WithoutBraces);
            QMutexLocker lock(&pickerImagesMutex);
            for (QVariant &entry : m_pickerScreens) {
                QVariantMap screen = entry.toMap();
                const QString key = captureId + QStringLiteral("/") + QString::number(screen.value("index").toInt());
                pickerImages.insert(key, m_backend->pickerImage(screen.value("index").toInt()));
                m_pickerKeys.append(key);
                screen.insert(QStringLiteral("source"), QStringLiteral("image://snitt-capture/") + key);
                entry = screen;
            }
            lock.unlock();
            emit capturePickerReady();
        });
        connect(m_backend, &Backend::captureFinished, this, [this] { clearPickerImages(); });
        connect(m_backend, &Backend::pickerNotice, this, &Services::capturePickerNotice);
        // Backend removes its temporary file when the signal returns. Retain
        // pixels here until Jolt accepts or discards this session's result.
        connect(m_backend, &Backend::captured, this, [this](const QUrl &url) {
            QImageReader reader(url.toLocalFile());
            reader.setAutoTransform(true);
            m_captureImage = reader.read();
            m_captureRegions.clear();
            m_captureKind = m_captureImage.isNull() ? None : Image;
            if (m_captureKind == None)
                emit captureDataError(QStringLiteral("Could not open this image: ") + reader.errorString());
            else
                emit captureReady();
        });
        connect(m_backend, &Backend::regionsCaptured, this, [this](const QVariantList &images) {
            m_captureImage = QImage();
            m_captureRegions = images;
            m_captureKind = Regions;
            emit captureReady();
        });
    }

    ~Services() override { clearPickerImages(); }

    QObject *settings() const { return m_settings; }
    QObject *backend() const { return m_backend; }
    QObject *trim() const { return m_media; }
    bool macOS() const {
#ifdef Q_OS_MACOS
        return true;
#else
        return false;
#endif
    }

    // Recorder instances never cross generations. Queued bridge events carry the
    // Jolt generation, while platform callbacks retain their own native fence.
    Q_INVOKABLE bool prepareRecorder(int generation, QObject *indicator, QObject *controls, QObject *outline) {
        if (generation <= m_recordingGeneration || m_recording || !m_sourceAvailable) return false;
        m_recordingGeneration = generation;
        m_protected = false;
        for (QObject *object : {indicator, controls, outline}) {
            auto *window = qobject_cast<QWindow *>(object);
            if (!window) return false;
            window->winId(); // Create each native window before protection/start.
#ifdef Q_OS_WIN
            // A fake recorder captures nothing, and offscreen test windows have no HWND.
            if (!fakeRecorder() && !m_backend->protectRecordingControls(window)) return false;
#endif
        }
        if (qEnvironmentVariable("SNITT_JOLT_FAKE_RECORDER") == QStringLiteral("protection-failure")) return false;
        // macOS ScreenCaptureKit excludes the entire current application before
        // starting its stream, and reports an error if that exclusion fails.
        m_protected = true;
        return true;
    }

    Q_INVOKABLE bool startRecorder(int generation) {
        if (!m_protected || !m_sourceAvailable || generation != m_recordingGeneration || m_recording) return false;
        m_protected = false;
        m_sourceAvailable = false;
        if (fakeRecorder()) { ++m_recordingStarts; return true; }
        auto *recorder = new Recorder(this);
        m_recording = recorder;
        connect(recorder, &Recorder::ready, this, [this, generation] { emit recorderEvent(generation, "ready", ""); });
        connect(recorder, &Recorder::changed, this, [this, recorder, generation] {
            if (recorder->finishing()) emit recorderEvent(generation, "finishing", "");
        });
        connect(recorder, &Recorder::elapsedChanged, this, [this, recorder, generation] {
            emit recorderEvent(generation, "elapsed", QString::number(recorder->elapsed()));
        });
        connect(recorder, &Recorder::saved, this, [this, generation](const QString &path) {
            emit recorderEvent(generation, "saved", path);
        });
        connect(recorder, &Recorder::canceled, this, [this, generation] { emit recorderEvent(generation, "canceled", ""); });
        connect(recorder, &Recorder::error, this, [this, generation](const QString &error) { emit recorderEvent(generation, "failed", error); });
        recorder->start(m_recordingSource, m_settings->videosRoot());
        return true; // Synchronous startup failures are delivered as events too.
    }
    Q_INVOKABLE void finishRecorder(int generation) {
        if (generation != m_recordingGeneration) return;
        if (fakeRecorder()) ++m_recordingStops;
        else if (m_recording) m_recording->finish();
    }
    Q_INVOKABLE void cancelRecorder(int generation) {
        if (generation != m_recordingGeneration) return;
        m_protected = m_sourceAvailable = false;
        if (fakeRecorder() || !m_recording) emit recorderEvent(generation, "canceled", "");
        else m_recording->cancel();
    }
    Q_INVOKABLE void retireRecorder(int generation) {
        if (generation != m_recordingGeneration) return;
        m_protected = m_sourceAvailable = false;
        if (m_recording) {
            m_recording->disconnect(this);
            m_recording->cancel();
            m_recording->deleteLater();
            m_recording = nullptr;
        }
    }
    Q_INVOKABLE void recordingSourceForTest() {
        if (fakeRecorder()) { m_sourceAvailable = true; emit recordingSourceReady(); }
    }
    Q_INVOKABLE void recorderEventForTest(int generation, const QString &event, const QString &data) {
        if (fakeRecorder()) emit recorderEvent(generation, event, data);
    }
    Q_INVOKABLE QVariantList recorderCountsForTest() const {
        return {m_recordingStarts, m_recordingStops};
    }

    Q_INVOKABLE bool bindCanvas(QObject *canvas) {
        auto *editor = qobject_cast<EditorCanvas *>(canvas);
        if (!editor || m_canvas) return false;
        m_canvas = editor;
        connect(editor, &QObject::destroyed, this, [this] { m_canvas = nullptr; });
        return true;
    }

    Q_INVOKABLE bool consumeCapture() {
        if (!m_canvas) return false;
        const CaptureKind kind = m_captureKind;
        m_captureKind = None;
        bool loaded = false;
        if (kind == Image) loaded = m_canvas->loadCapturedImage(m_captureImage);
        if (kind == Regions) loaded = m_canvas->loadExternalRegions(m_captureRegions);
        m_captureImage = QImage();
        m_captureRegions.clear();
        return loaded;
    }

    Q_INVOKABLE void discardCapture() {
        m_captureKind = None;
        m_captureImage = QImage();
        m_captureRegions.clear();
    }

    Q_INVOKABLE QVariantList capturePickerScreens() const { return m_pickerScreens; }

    Q_INVOKABLE bool queueCaptureFileForTest(const QString &path) {
        if (qEnvironmentVariable("SNITT_JOLT_FAKE_CAPTURE") != QStringLiteral("1")) return false;
        QImageReader reader(path);
        reader.setAutoTransform(true);
        m_captureImage = reader.read();
        if (m_captureImage.isNull()) return false;
        m_captureRegions.clear();
        m_captureKind = Image;
        emit captureReady();
        return true;
    }

    Q_INVOKABLE bool queueCaptureRegionsForTest(const QStringList &paths) {
        if (qEnvironmentVariable("SNITT_JOLT_FAKE_CAPTURE") != QStringLiteral("1") || paths.isEmpty()) return false;
        QVariantList images;
        for (const QString &path : paths) {
            QImageReader reader(path);
            const QImage image = reader.read();
            if (image.isNull()) return false;
            images.append(image);
        }
        m_captureImage = QImage();
        m_captureRegions = images;
        m_captureKind = Regions;
        emit captureReady();
        return true;
    }

    Q_INVOKABLE void finishCaptureForTest(bool captured) {
        if (qEnvironmentVariable("SNITT_JOLT_FAKE_CAPTURE") == QStringLiteral("1"))
            emit m_backend->captureFinished(captured);
    }

    Q_INVOKABLE void failCaptureForTest(const QString &message) {
        if (qEnvironmentVariable("SNITT_JOLT_FAKE_CAPTURE") == QStringLiteral("1")) {
            emit m_backend->error(message);
            emit m_backend->captureFinished(false);
        }
    }

    Q_INVOKABLE QVariantList captureDisplaysForTest() const {
        QVariantList result;
        if (qEnvironmentVariable("SNITT_JOLT_FAKE_CAPTURE") != QStringLiteral("1")) return result;
        for (QScreen *screen : QGuiApplication::screens()) {
            result.append(QVariantMap{{QStringLiteral("geometry"), screen->geometry()},
                                      {QStringLiteral("scale"), screen->devicePixelRatio()}});
        }
        return result;
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
            && gqt_expose_object(m_media);
    }
    // Desktop artwork and platform key spelling only; Jolt owns the menu/actions.
    Q_INVOKABLE bool applyTrayArtwork(QObject *object) {
        auto *tray = qobject_cast<QSystemTrayIcon *>(object);
        if (!tray) return false;
        QPixmap pixmap(32, 32); pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap); painter.setRenderHint(QPainter::Antialiasing);
#ifdef Q_OS_MACOS
        painter.setPen(QPen(Qt::black, 3));
#else
        painter.setPen(QPen(QColor("#91bff0"), 3));
#endif
        painter.drawRoundedRect(QRectF(3, 7, 26, 20), 4, 4);
        painter.drawEllipse(QPointF(16, 17), 5, 5);
        painter.drawLine(QPointF(10, 4), QPointF(21, 4)); painter.end();
        QIcon icon(pixmap);
#ifdef Q_OS_MACOS
        icon.setIsMask(true);
#endif
        tray->setIcon(icon);
        return true;
    }
    Q_INVOKABLE QString globalShortcutDescription(const QString &configured) const {
        if (applicationHotkey) return applicationHotkey->description();
        const auto sequence = QKeySequence::fromString(configured, QKeySequence::PortableText);
        QString description = sequence.toString(QKeySequence::NativeText);
#ifdef Q_OS_MACOS
        if (sequence.count() == 1 && sequence[0].key() == Qt::Key_Print)
            description.replace(QStringLiteral("Print"), QStringLiteral("F13 / Print Screen"));
#endif
        return description.isEmpty() ? QStringLiteral("Capture shortcut") : description;
    }
    Q_INVOKABLE bool activateGlobalShortcut(const QString &configured) {
        if (qEnvironmentVariable("SNITT_JOLT_FAKE_HOTKEY") == QStringLiteral("1")) {
            m_hotkeyError.clear();
            const QString portable = m_settings->shortcutInfo(configured)
                .value(QStringLiteral("portable")).toString();
            // Apply the native key policy without registering a system shortcut.
            return GlobalHotkey::supports(QKeySequence::fromString(portable, QKeySequence::PortableText),
                                          &m_hotkeyError);
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
    void recordingSourceReady();
    void recorderEvent(int generation, const QString &event, const QString &data);
    void globalCaptureActivated();
    void captureReady();
    void captureDataError(const QString &message);
    void capturePickerReady();
    void capturePickerNotice(const QString &message);
private:
    bool fakeRecorder() const {
        const QString mode = qEnvironmentVariable("SNITT_JOLT_FAKE_RECORDER");
        return mode == QStringLiteral("manual") || mode == QStringLiteral("protection-failure");
    }
    recording::Source m_recordingSource;
    QPointer<Recorder> m_recording;
    int m_recordingGeneration = 0;
    int m_recordingStarts = 0;
    int m_recordingStops = 0;
    bool m_sourceAvailable = false;
    bool m_protected = false;
    void clearPickerImages() {
        QMutexLocker lock(&pickerImagesMutex);
        for (const QString &key : m_pickerKeys) pickerImages.remove(key);
        m_pickerKeys.clear();
        m_pickerScreens.clear();
    }
    enum CaptureKind { None, Image, Regions };
    CaptureKind m_captureKind = None;
    QImage m_captureImage;
    QVariantList m_captureRegions;
    QVariantList m_pickerScreens;
    QStringList m_pickerKeys;
    AppSettings *m_settings = nullptr;
    Backend *m_backend = nullptr;
    QPointer<EditorCanvas> m_canvas;
    QString m_hotkeyError;
    bool m_hotkeyConnected = false;
};

QObject *canvasFactory() { return new DocumentCanvas; }
QObject *servicesFactory() { return new Services; }
QObject *lifecycleFactory() { return new Lifecycle; }
}

static void initializeSnittResources() { Q_INIT_RESOURCE(resources); }

extern "C" Q_DECL_EXPORT int snitt_jolt_register() {
    if (!qobject_cast<QApplication *>(qApp)) return 0;
    if (qEnvironmentVariableIsSet("SNITT_JOLT_PRODUCTION")) {
        qApp->setOrganizationName(QStringLiteral("snitt"));
        qApp->setApplicationDisplayName(QStringLiteral("Snitt"));
    }
    initializeSnittResources();
    return gqt_register_image_provider("snitt-capture", pickerImageRequest, nullptr, nullptr)
        && gqt_register_image_provider("snitt-selector-icon", selectorIconRequest, nullptr, nullptr)
        && gqt_register_image_provider("snitt-selector-artwork", selectorArtworkRequest, nullptr, nullptr)
        && gqt_register_factory("snitt-jolt.canvas", canvasFactory, nullptr)
        && gqt_register_factory("snitt-jolt.services", servicesFactory, nullptr)
        && gqt_register_factory("snitt-jolt.lifecycle", lifecycleFactory, nullptr);
}

#include "snitt_jolt.moc"
