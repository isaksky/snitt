#pragma once
#include <QObject>
#include <QImage>
#include <QSize>
#include <QString>
#include <cstdint>
// Application libraries link against glimmer_qt and register on the GUI thread
// during an active session. Factories return a fresh, unparented object. The
// bridge owns it, writes checked initial properties, then calls complete.
// Callbacks must not throw. The library must remain loaded for the process.
using GqtFactory = QObject *(*)();
using GqtComplete = void (*)(QObject *);
#ifdef GQT_BUILD
#define GQT_API Q_DECL_EXPORT
#else
#define GQT_API Q_DECL_IMPORT
#endif
extern "C" GQT_API int gqt_register_factory(const char *name, GqtFactory factory,
                                                 GqtComplete complete);

// Expose an existing live GUI-thread descendant of a registered object.
// Ownership stays with its native parent; never pass a dangling pointer.
extern "C" GQT_API uint64_t gqt_expose_object(QObject *object);

// Register a session-owned image:// provider on the GUI thread before its first
// URL is used. The callback may run on a Qt loader thread when Image.asynchronous
// is true: it must be thread-safe and must not access GUI objects or Jolt.
// Callbacks must not throw; the bridge catches exceptions defensively and
// reports a failed image request instead of unwinding through Qt.
// Return a QImage owning (or retaining via QImage cleanup) its pixel storage;
// never return a view of a temporary buffer. Qt retains implicit copies until
// rendering finishes. release(userData) runs when the session engine is torn down.
using GqtImageRequest = QImage (*)(void *userData, const QString &id,
                                   QSize *actualSize, const QSize &requestedSize);
using GqtImageRelease = void (*)(void *userData);
extern "C" GQT_API int gqt_register_image_provider(const char *name,
                                                     GqtImageRequest request,
                                                     void *userData,
                                                     GqtImageRelease release);
