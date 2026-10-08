#pragma once
#include <QAccessible>

// Exercise the platform accessibility action, not a QML signal or mouse event.
extern "C" Q_DECL_EXPORT int gqt_test_accessible_mode(Handle id, int activate) {
    QAccessible::setActive(true);
    auto *item = itemFor(id);
    auto *iface = item ? QAccessible::queryAccessibleInterface(item) : nullptr;
    if (!iface) return -1;
    if (activate) {
        auto *actions = iface->actionInterface();
        if (!actions || !actions->actionNames().contains(QAccessibleActionInterface::toggleAction()))
            return -2;
        actions->doAction(QAccessibleActionInterface::toggleAction());
    }
    const auto state = iface->state();
    return (iface->role() == QAccessible::CheckBox ? 1 : 0) |
           (state.checkable ? 2 : 0) | (state.checked ? 4 : 0);
}

extern "C" Q_DECL_EXPORT int gqt_test_tab_focus(Handle id) {
    auto *item = itemFor(id);
    if (!item || !item->window() || !item->isEnabled() || !item->isVisible()) return 0;
    item->forceActiveFocus(Qt::TabFocusReason);
    return item->hasActiveFocus();
}
// Send through QQuickWindow delivery/hit testing, never invoke QML signals.
extern "C" Q_DECL_EXPORT int gqt_test_pointer(Handle id, int action, double x, double y) {
    auto *item = itemFor(id);
    if (!item || !item->window()) return 0;
    auto *window = item->window();
    const QPoint p = item->mapToScene(QPointF(x, y)).toPoint();
    switch (action) {
    case 0: QTest::mousePress(window, Qt::LeftButton, Qt::ShiftModifier, p); break;
    case 1: QTest::mouseMove(window, p); break;
    case 2: QTest::mouseRelease(window, Qt::LeftButton, Qt::ShiftModifier, p); break;
    case 3: if (auto *grabber = window->mouseGrabberItem()) grabber->ungrabMouse(); break;
    case 4: case 5: {
        QWheelEvent event(p, window->mapToGlobal(p), QPoint(0, action == 4 ? 7 : 0),
                          QPoint(0, action == 4 ? 0 : 120), Qt::NoButton,
                          action == 4 ? Qt::ControlModifier : Qt::NoModifier,
                          action == 4 ? Qt::ScrollUpdate : Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(window, &event);
        break;
    }
    case 6: window->setPosition(-600, -400); break;
    default: return 0;
    }
    return 1;
}
