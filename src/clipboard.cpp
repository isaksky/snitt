#include "clipboard.h"
#include <QClipboard>
#include <QGuiApplication>
#ifdef Q_OS_WIN
#include <QBuffer>
#include <QElapsedTimer>
#include <QImage>
#include <QPointer>
#include <QScopeGuard>
#include <qt_windows.h>
#include <cstring>
#include <limits>

namespace {
static_assert(sizeof(BITMAPV5HEADER) == 124);

struct ClipboardOwner : QObject {
    explicit ClipboardOwner(QObject *parent) : QObject(parent), window(CreateWindowExW(
        0, L"STATIC", L"Snitt clipboard", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, nullptr, nullptr)) {}
    ~ClipboardOwner() override { if (window) DestroyWindow(window); }
    HWND window;
};

HGLOBAL clipboardMemory(const QByteArray &bytes) {
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, SIZE_T(bytes.size()));
    if (!memory) return nullptr;
    void *destination = GlobalLock(memory);
    if (!destination) { GlobalFree(memory); return nullptr; }
    std::memcpy(destination, bytes.constData(), size_t(bytes.size()));
    GlobalUnlock(memory);
    return memory;
}

QByteArray imageDib(const QImage &source) {
    const QImage image = source.convertToFormat(QImage::Format_ARGB32);
    const quint64 pixelBytes = quint64(image.width()) * image.height() * 4;
    if (image.isNull() || pixelBytes > std::numeric_limits<DWORD>::max()) return {};
    BITMAPV5HEADER header{};
    header.bV5Size = sizeof(header);
    header.bV5Width = image.width();
    header.bV5Height = -image.height(); // QImage rows run from top to bottom.
    header.bV5Planes = 1;
    header.bV5BitCount = 32;
    header.bV5Compression = BI_BITFIELDS;
    header.bV5SizeImage = DWORD(pixelBytes);
    header.bV5RedMask = 0x00ff0000;
    header.bV5GreenMask = 0x0000ff00;
    header.bV5BlueMask = 0x000000ff;
    header.bV5AlphaMask = 0xff000000;
    header.bV5CSType = LCS_sRGB;
    header.bV5Intent = LCS_GM_IMAGES;
    QByteArray bytes(qsizetype(sizeof(header) + pixelBytes), Qt::Uninitialized);
    std::memcpy(bytes.data(), &header, sizeof(header));
    std::memcpy(bytes.data() + sizeof(header), image.constBits(), size_t(pixelBytes));
    return bytes;
}

bool publishWindows(const QMimeData &data) {
    struct Payload { UINT format; HGLOBAL memory; };
    QList<Payload> payloads;
    const auto freeUnpublished = qScopeGuard([&] {
        for (const auto &payload : payloads) if (payload.memory) GlobalFree(payload.memory);
    });
    const auto append = [&](UINT format, const QByteArray &bytes) {
        if (!format || bytes.isEmpty()) return false;
        HGLOBAL memory = clipboardMemory(bytes);
        if (!memory) return false;
        payloads.append({format, memory});
        return true;
    };
    // Encode and allocate everything before changing the clipboard. Both image
    // formats preserve alpha; DIBV5 also lets Windows synthesize legacy bitmaps.
    if (data.hasImage()) {
        const QImage image = qvariant_cast<QImage>(data.imageData());
        QByteArray png;
        QBuffer buffer(&png);
        if (image.isNull() || !buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG")) return false;
        if (!append(RegisterClipboardFormatW(L"PNG"), png) || !append(CF_DIBV5, imageDib(image))) return false;
    }
    if (data.hasText()) {
        QString text = data.text();
        text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
        text.replace(u'\n', QStringLiteral("\r\n"));
        const QByteArray unicode(reinterpret_cast<const char *>(text.utf16()),
                                 (text.size() + 1) * qsizetype(sizeof(char16_t)));
        if (!append(CF_UNICODETEXT, unicode)) return false;
    }
    if (payloads.isEmpty()) return false;

    static QPointer<ClipboardOwner> owner;
    if (!owner) owner = new ClipboardOwner(qApp);
    if (!owner->window) return false;
    QElapsedTimer timeout;
    timeout.start();
    while (!OpenClipboard(owner->window)) {
        if (timeout.elapsed() >= 1000) return false;
        // Let existing clipboard readers complete sent-message requests without
        // dispatching queued input or Qt timers during a QML copy handler.
        MsgWaitForMultipleObjectsEx(0, nullptr, 10, QS_SENDMESSAGE, MWMO_INPUTAVAILABLE);
        MSG message;
        PeekMessage(&message, nullptr, 0, 0, PM_NOREMOVE | PM_QS_SENDMESSAGE);
    }
    const auto close = qScopeGuard([] { CloseClipboard(); });
    if (!EmptyClipboard()) return false;
    for (auto &payload : payloads) {
        if (!SetClipboardData(payload.format, payload.memory)) return false;
        payload.memory = nullptr; // Ownership transfers only on success.
    }
    return true;
}
}
#endif

bool snittClipboard::publish(std::unique_ptr<QMimeData> data) {
    if (!data) return false;
    for (const QString &format : data->formats()) {
        if (format != QStringLiteral("application/x-qt-image") && format != QStringLiteral("text/plain"))
            return false;
    }
    if (!data->hasImage() && !data->hasText()) return false;
    auto *clipboard = QGuiApplication::clipboard();
#ifdef Q_OS_WIN
    if (QGuiApplication::platformName() == QStringLiteral("windows")) return publishWindows(*data);
#endif
    // Qt takes ownership and may delete data on failure. Compare identity only;
    // do not dereference this pointer after transferring it.
    QMimeData *published = data.release();
    clipboard->setMimeData(published);
    return clipboard->mimeData() == published;
}
