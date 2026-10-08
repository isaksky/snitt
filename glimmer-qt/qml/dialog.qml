import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Item {
    id: control
    required property QtObject relay
    property string title: ""
    property bool modal: true
    property real dialogWidth: 400
    property Item centerTarget: null
    property bool fitWindow: false
    property bool animated: false
    property string appearance: "default"
    property real contentSpacing: 12
    property Item styledBackground: Rectangle { color: "#242930"; radius: 12; border.color: "#454d59" }
    property Item styledHeader: Label {
        text: control.title; color: "#f0f3f7"; font.pixelSize: 20; font.weight: Font.DemiBold
        leftPadding: 24; rightPadding: 24; topPadding: 24; bottomPadding: 8
    }
    Binding { target: popup; property: "background"; when: control.appearance === "confirm"; value: control.styledBackground }
    Binding { target: popup; property: "header"; when: control.appearance === "confirm"; value: control.styledHeader }
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
        // Reparenting to a content-area anchor must retain the window theme.
        Material.theme: control.Window.window ? control.Window.window.Material.theme : Material.Dark
        parent: control.centerTarget || control.Overlay.overlay
        anchors.centerIn: parent
        width: control.fitWindow && control.Window.window ? Math.min(control.dialogWidth, control.Window.window.width - 48) : control.dialogWidth
        title: control.title
        modal: control.modal
        focus: true
        closePolicy: Popup.CloseOnEscape
        enter: Transition {
            NumberAnimation { property: "scale"; from: control.animated ? 0.9 : 1; to: 1; duration: control.animated ? 220 : 0; easing.type: Easing.OutQuint }
            NumberAnimation { property: "opacity"; from: control.animated ? 0 : 1; to: 1; duration: control.animated ? 150 : 0; easing.type: Easing.OutCubic }
        }
        exit: Transition {
            NumberAnimation { property: "scale"; from: 1; to: control.animated ? 0.9 : 1; duration: control.animated ? 220 : 0; easing.type: Easing.OutQuint }
            NumberAnimation { property: "opacity"; from: 1; to: control.animated ? 0 : 1; duration: control.animated ? 150 : 0; easing.type: Easing.OutCubic }
        }
        contentItem: ColumnLayout { spacing: control.contentSpacing }
        Overlay.modal: Rectangle {
            color: control.appearance === "reference" ? popup.Material.backgroundDimColor : "#99000000"
            Behavior on opacity { enabled: control.appearance === "reference" && control.animated; NumberAnimation { duration: 150 } }
        }
        onClosed: { if (control.pending) control.finish(false) }
        onVisibleChanged: control.relay.dispatch(control, "on-visible-change", "")
    }
}
