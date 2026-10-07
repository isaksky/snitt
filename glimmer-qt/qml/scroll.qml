import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Geometry {
    id: control
    required property QtObject relay
    property alias content: viewport.contentItem
    property alias contentItem: viewport
    readonly property real contentWidth: viewport.contentWidth
    readonly property real contentHeight: viewport.contentHeight
    property real explicitContentWidth: -1
    property real padding: 0
    property string horizontalPolicy: "as-needed"
    property string verticalPolicy: "as-needed"
    clip: true
    function policy(value) {
        return value === "always" ? ScrollBar.AlwaysOn
             : value === "never" ? ScrollBar.AlwaysOff : ScrollBar.AsNeeded
    }
    Flickable {
        id: viewport
        anchors.fill: parent
        anchors.margins: control.padding
        clip: true
        contentWidth: control.explicitContentWidth < 0 ? width : control.explicitContentWidth
        contentHeight: contentItem.childrenRect.height
        onContentHeightChanged: contentY = Math.max(0, Math.min(contentY, contentHeight - height))
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.horizontal: ScrollBar { policy: control.policy(control.horizontalPolicy) }
        ScrollBar.vertical: ScrollBar { policy: control.policy(control.verticalPolicy) }
    }
    MouseArea {
        anchors.fill: viewport
        acceptedButtons: Qt.NoButton
        scrollGestureEnabled: true
        onWheel: event => {
            event.accepted = control.relay.pointer(control, "on-wheel", {
                "angle-x": event.angleDelta.x, "angle-y": event.angleDelta.y,
                "pixel-x": event.pixelDelta.x, "pixel-y": event.pixelDelta.y,
                modifiers: event.modifiers, inverted: event.inverted})
        }
    }
}
