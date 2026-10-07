import QtQuick
import QtQuick.Controls

Item {
    id: control
    required property QtObject relay
    property string title: ""
    property bool modal: true
    property real dialogWidth: 400
    property alias content: popup.contentItem
    readonly property alias dialogVisible: popup.visible
    property var previousFocus: null
    property bool pending: false

    function open() {
        if (pending || !control.Window.window) return
        previousFocus = control.Window.window.activeFocusItem
        pending = true
        popup.open()
    }
    function finish(accepted) {
        if (!pending) return
        pending = false
        popup.close()
        if (previousFocus) previousFocus.forceActiveFocus(Qt.OtherFocusReason)
        previousFocus = null
        relay.dispatch(control, accepted ? "on-accepted" : "on-rejected", "")
    }
    function accept() { finish(true) }
    function reject() { finish(false) }
    function close() { finish(false) }

    Dialog {
        id: popup
        parent: control.Overlay.overlay
        anchors.centerIn: parent
        width: control.dialogWidth
        title: control.title
        modal: control.modal
        focus: true
        closePolicy: Popup.CloseOnEscape
        // No transitions: visibility and terminal outcome change atomically.
        enter: Transition {}
        exit: Transition {}
        contentItem: Column { spacing: 12 }
        Overlay.modal: Rectangle { color: "#99000000" }
        onClosed: { if (control.pending) control.finish(false) }
        onVisibleChanged: control.relay.dispatch(control, "on-visible-change", "")
    }
}
