import QtQuick
import QtQuick.Layouts

Rectangle {
    id: root
    color: "transparent"
    property alias content: content
    property alias spacing: content.spacing
    property real padding: 0
    property real leftPadding: -1
    property real rightPadding: -1
    property real topPadding: -1
    property real bottomPadding: -1
    readonly property real insetLeft: leftPadding < 0 ? padding : leftPadding
    readonly property real insetRight: rightPadding < 0 ? padding : rightPadding
    readonly property real insetTop: topPadding < 0 ? padding : topPadding
    readonly property real insetBottom: bottomPadding < 0 ? padding : bottomPadding
    implicitWidth: content.implicitWidth + insetLeft + insetRight
    implicitHeight: content.implicitHeight + insetTop + insetBottom
    ColumnLayout {
        id: content
        x: root ? root.insetLeft : 0
        y: root ? root.insetTop : 0
        width: root ? Math.max(0, root.width - root.insetLeft - root.insetRight) : 0
        height: root ? Math.max(0, root.height - root.insetTop - root.insetBottom) : 0
    }
}
