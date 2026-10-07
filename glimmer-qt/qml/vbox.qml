import QtQuick
import QtQuick.Layouts

Item {
    id: root
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
        x: root.insetLeft
        y: root.insetTop
        width: Math.max(0, root.width - root.insetLeft - root.insetRight)
        height: Math.max(0, root.height - root.insetTop - root.insetBottom)
    }
}
