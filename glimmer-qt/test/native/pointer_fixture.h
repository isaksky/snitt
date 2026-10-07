#pragma once
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
                          Qt::ScrollUpdate, false);
        QCoreApplication::sendEvent(window, &event);
        break;
    }
    case 6: window->setPosition(-600, -400); break;
    default: return 0;
    }
    return 1;
}
