#import <AppKit/AppKit.h>
#include <QGuiApplication>
#include <QWindow>

void gqtActivateMacApplication() {
    if (QGuiApplication::platformName() == QStringLiteral("cocoa")) {
        // Qt's requestActivate() makes the window key without activating its
        // application. Its raise() activates only an already-visible native
        // window; a new Quick window may still be waiting for its first frame.
        // Explicit activation must also work from an inactive menu-bar app.
        [NSApp activateIgnoringOtherApps:YES];
    }
}

#ifdef GQT_PROPERTY_TESTS
extern "C" Q_DECL_EXPORT int gqt_test_macos_background() {
    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
    [NSApp hide:nil];
    return 1;
}
extern "C" Q_DECL_EXPORT int gqt_test_macos_application_active() {
    return NSApp.active ? 1 : 0;
}
extern "C" Q_DECL_EXPORT int gqt_test_macos_window_visible(void *pointer) {
    auto *window = static_cast<QWindow *>(pointer);
    NSView *view = reinterpret_cast<NSView *>(window->winId());
    return view.window.visible ? 1 : 0;
}
#endif
