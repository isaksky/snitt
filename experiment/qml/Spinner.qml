import QtQuick
import QtQuick.Layouts
Item {
    id: control
    required property real angle
    required property QtObject relay
    property real completedAngle: -1
    property alias content: content
    implicitWidth: 160
    implicitHeight: 100
    signal activated()
    onActivated: relay.dispatch(control, "on-activate", "spin")
    Component.onCompleted: completedAngle = angle
    Rectangle {
        width: 30; height: 6; radius: 3
        color: "#91bff0"
        x: 50; y: 20
        rotation: control.angle
    }
    ColumnLayout { id: content; y: 50 }
}
