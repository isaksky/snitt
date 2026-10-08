import QtQuick

// QWidget-compatible tooltip presentation for capture controls. It shares the
// platform style, delay, cursor placement and lifecycle with QToolTip.
Item {
    id: root
    required property QtObject relay
    property bool active: false
    property string text: ""
    Timer {
        interval: root.relay.nativeToolTipDelay()
        running: root.active && root.text.length > 0
        onTriggered: root.relay.nativeToolTip(root, true, root.text)
    }
    onActiveChanged: if (!active) relay.nativeToolTip(root, false, "")
    Component.onDestruction: if (relay) relay.nativeToolTip(root, false, "")
}
