import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

Geometry {
    id: root
    required property QtObject relay
    property alias acceptedButtons: input.acceptedButtons
    property alias hoverEnabled: input.hoverEnabled
    property alias preventStealing: input.preventStealing
    property alias cursorShape: input.cursorShape
    property string tooltip: ""
    property bool resizeGrip: false
    ToolTip.visible: input.pressed && tooltip.length > 0
    ToolTip.text: tooltip
    Rectangle {
        visible: root.resizeGrip
        anchors.centerIn: parent
        width: 6
        height: 24
        radius: 3
        color: input.pressed || input.containsMouse ? "#91bff0" : "#dfe5ed"
        border.color: "#111317"
        border.width: 1
    }
    // MouseArea owns the exclusive grab after an accepted press. Children can
    // be composed above it; unaccepted presses/wheels follow Qt propagation.
    MouseArea {
        id: input
        anchors.fill: parent
        scrollGestureEnabled: true
        property var lastSample: ({x: 0, y: 0, "global-x": 0, "global-y": 0,
                                   button: 0, buttons: 0, modifiers: 0})
        function sample(event) {
            const global = root.mapToGlobal(event.x, event.y)
            return {x: event.x, y: event.y, "global-x": global.x, "global-y": global.y,
                    button: event.button || 0, buttons: event.buttons || 0,
                    modifiers: event.modifiers || 0}
        }
        function send(name, event) {
            lastSample = sample(event)
            return root.relay.pointer(root, name, lastSample)
        }
        onPressed: mouse => { mouse.accepted = send("on-pointer-press", mouse) }
        onPositionChanged: mouse => { send("on-pointer-move", mouse) }
        onReleased: mouse => { send("on-pointer-release", mouse) }
        onCanceled: { root.relay.pointer(root, "on-pointer-cancel", lastSample) }
        onEntered: { send("on-hover-enter", {x: mouseX, y: mouseY}) }
        onExited: { send("on-hover-leave", {x: mouseX, y: mouseY}) }
        onWheel: wheel => {
            const payload = sample(wheel)
            payload["angle-x"] = wheel.angleDelta.x
            payload["angle-y"] = wheel.angleDelta.y
            payload["pixel-x"] = wheel.pixelDelta.x
            payload["pixel-y"] = wheel.pixelDelta.y
            payload.inverted = wheel.inverted
            wheel.accepted = root.relay.pointer(root, "on-wheel", payload)
        }
    }
}
