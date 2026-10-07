import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Slider {
    id: control
    required property QtObject relay
    property string accessibleName: ""
    property string tooltip: ""
    property color trackColor: "#454d59"
    property color fillColor: "#91bff0"
    property color handleColor: "#ffffff"
    property color pressedColor: "#91bff0"
    property real handleSize: 18
    property real trackHeight: 3
    implicitHeight: Math.max(24, handleSize)
    implicitWidth: 160
    padding: 0
    focusPolicy: Qt.StrongFocus
    hoverEnabled: true
    snapMode: stepSize > 0 ? Slider.SnapAlways : Slider.NoSnap
    Accessible.name: accessibleName
    ToolTip.text: tooltip
    ToolTip.visible: tooltip.length > 0 && (hovered || visualFocus)

    // moved() is emitted only for user input. Programmatic value/range writes
    // never become application seek requests.
    onMoved: {
        if (from < to)
            relay.dispatch(control, "on-change", String(value))
    }
    onPressedChanged: relay.dispatch(control, "on-interacting-change", String(pressed))

    background: Item {
        Rectangle {
            x: control.handleSize / 2
            y: (parent.height - height) / 2
            width: Math.max(0, parent.width - control.handleSize)
            height: control.trackHeight
            radius: height / 2
            color: control.trackColor
        }
        Rectangle {
            x: control.handleSize / 2
            y: (parent.height - height) / 2
            width: Math.max(0, (parent.width - control.handleSize) * control.visualPosition)
            height: control.trackHeight
            radius: height / 2
            color: control.fillColor
        }
    }
    handle: Rectangle {
        objectName: "sliderHandle"
        x: control.visualPosition * (control.availableWidth - width)
        y: (control.height - height) / 2
        width: control.handleSize
        height: width
        radius: width / 2
        color: control.pressed ? control.pressedColor : control.handleColor
        border.width: 2
        border.color: "#111317"
    }
}
