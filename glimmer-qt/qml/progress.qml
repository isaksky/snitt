import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ProgressBar {
    id: control
    property string accessibleName: ""
    property string tooltip: ""
    property color trackColor: "#454d59"
    property color fillColor: "#91bff0"
    property real trackHeight: 4
    implicitWidth: 160
    implicitHeight: Math.max(8, trackHeight)
    Accessible.name: accessibleName
    ToolTip.text: tooltip

    background: Rectangle {
        y: (control.height - height) / 2
        width: control.width
        height: control.trackHeight
        radius: height / 2
        color: control.trackColor
    }
    contentItem: Item {
        property real sweep: 0
        clip: true
        SequentialAnimation on sweep {
            running: control.indeterminate && control.visible
            loops: Animation.Infinite
            NumberAnimation { from: 0; to: 1; duration: 1250; easing.type: Easing.InOutCubic }
        }
        Rectangle {
            id: fill
            y: (parent.height - height) / 2
            width: control.indeterminate ? Math.max(12, parent.width * 0.28)
                                         : parent.width * control.position
            height: control.trackHeight
            radius: height / 2
            color: control.fillColor
            visible: control.indeterminate || control.position > 0
            x: control.indeterminate ? (parent.width + width) * parent.sweep - width : 0
        }
    }
}
