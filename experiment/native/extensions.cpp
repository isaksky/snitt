#include "../../glimmer-qt/native/extensions.h"
#include "../../src/clipboard.h"
#include <QQuickItem>
#include <QDir>
#include <QColor>
#include <QFile>
#include <atomic>
class Marker : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(double angle READ rotation WRITE setRotation)
    Q_PROPERTY(double completedAngle MEMBER completedAngle)
public:
    double completedAngle = -1;
};
static QObject *marker() { return new Marker; }
static void complete(QObject *object) {
    auto *item = static_cast<Marker *>(object);
    item->completedAngle = item->rotation();
}
static QObject *service() { return new QObject; }
static void initializeResources() { Q_INIT_RESOURCE(hello_resources); }
static std::atomic<int> providerRequests{0};
static std::atomic<int> providerReleases{0};
static std::atomic<int> bufferReleases{0};
static void freePixels(void *pointer) {
    delete[] static_cast<uchar *>(pointer);
    ++bufferReleases;
}
static QImage providerImage(void *, const QString &, QSize *actual, const QSize &requested) {
    ++providerRequests;
    const QSize bounded = requested.isValid() ? QSize(240, 120).scaled(requested, Qt::KeepAspectRatio)
                                              : QSize(240, 120);
    const int width = bounded.width(), height = bounded.height();
    auto *pixels = new uchar[width * height * 4];
    QImage image(pixels, width, height, width * 4,
                 QImage::Format_ARGB32_Premultiplied, freePixels, pixels);
    image.fill(QColor("#24ab6d"));
    if (actual) *actual = image.size();
    return image;
}
extern "C" Q_DECL_EXPORT int hello_register_image_provider() {
    return gqt_register_image_provider("hello", providerImage, nullptr,
                                       [](void *) { ++providerReleases; });
}
extern "C" Q_DECL_EXPORT int hello_provider_requests() { return providerRequests.load(); }
extern "C" Q_DECL_EXPORT int hello_provider_releases() { return providerReleases.load(); }
extern "C" Q_DECL_EXPORT int hello_buffer_releases() { return bufferReleases.load(); }
extern "C" Q_DECL_EXPORT int hello_sample_image_pixel(const char *path, int x, int y) {
    const QImage image(QString::fromUtf8(path));
    if (image.isNull() || x < 0 || y < 0 || x >= image.width() || y >= image.height()) return -1;
    return int(image.pixelColor(x, y).rgb() & 0xffffff);
}
extern "C" Q_DECL_EXPORT int hello_write_test_images(const char *directory) {
    QImage image(240, 120, QImage::Format_ARGB32_Premultiplied);
    image.fill(QColor("#c92550"));
    const QString path = QString::fromUtf8(directory);
    QFile invalid(QDir(path).filePath(QStringLiteral("invalid.png")));
    if (!invalid.open(QIODevice::WriteOnly)) return 0;
    invalid.write("not a PNG image", 15);
    invalid.close();
    QImage retina(48, 48, QImage::Format_ARGB32_Premultiplied);
    retina.fill(QColor("#4080f0"));
    return image.save(QDir(path).filePath(QStringLiteral("a # ü.png")), "PNG") &&
           image.save(QDir(path).filePath(QStringLiteral("b & ü.jpg")), "JPEG") &&
           retina.save(QDir(path).filePath(QStringLiteral("retina@2x.png")), "PNG");
}
extern "C" Q_DECL_EXPORT int hello_register_extensions() {
    initializeResources();
    return gqt_register_factory("hello.marker", marker, complete) &&
           gqt_register_factory("hello.service", service, nullptr);
}
// The example application can opt into Snitt's verified native clipboard
// publisher. In particular this retains the Windows lock/retry and PNG/DIBV5
// publication behavior rather than treating Qt setMimeData as proof of copy.
extern "C" Q_DECL_EXPORT int hello_register_snitt_clipboard(uint64_t session) {
    return gqt_register_clipboard_publisher(session, snittClipboard::publish);
}
#include "extensions.moc"
