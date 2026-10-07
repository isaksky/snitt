#pragma once
#include <QObject>
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
