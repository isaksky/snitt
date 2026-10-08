import QtQuick
import QtQuick.Layouts

Rectangle {
    id: root
    color: "transparent"
    property int selectedSegment: -1
    property int segmentCount: 2
    property color segmentColor: "transparent"
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
    Rectangle {
        visible: !!root && root.selectedSegment >= 0
        x: root ? root.insetLeft + root.selectedSegment * width : 0
        y: root ? root.insetTop : 0
        width: root ? (root.width - root.insetLeft - root.insetRight) / Math.max(1, root.segmentCount) : 0
        height: root ? root.height - root.insetTop - root.insetBottom : 0
        radius: root ? Math.max(0, root.radius - 1) : 0
        color: root ? Qt.darker(root.segmentColor, 1.8) : "transparent"
        border.width: 1
        border.color: root ? Qt.darker(root.segmentColor, 1.2) : "transparent"
    }
    RowLayout {
        id: content
        x: root ? root.insetLeft : 0
        y: root ? root.insetTop : 0
        width: root ? Math.max(0, root.width - root.insetLeft - root.insetRight) : 0
        height: root ? Math.max(0, root.height - root.insetTop - root.insetBottom) : 0
    }
}
