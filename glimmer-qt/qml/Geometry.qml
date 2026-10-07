import QtQuick
import QtQuick.Layouts

Rectangle {
    id: root
    property bool positionManaged: true
    property bool sizeManaged: true
    color: "transparent"
    property real offsetX: 0
    property real offsetY: 0
    property real explicitWidth: -1
    property real explicitHeight: -1
    property real relativeX: 0
    property real relativeY: 0
    property real relativeWidth: -1
    property real relativeHeight: -1
    property string anchorMode: "none"
    readonly property real localWidth: parent ? parent.width : 0
    readonly property real localHeight: parent ? parent.height : 0
    Binding {
        target: root
        property: "x"
        when: !root.positionManaged
        value: root.anchorMode === "fill" ? 0 : root.offsetX + root.relativeX * root.localWidth + (root.anchorMode === "center" ? (root.localWidth - root.width) / 2 : 0)
        restoreMode: Binding.RestoreNone
    }
    Binding {
        target: root
        property: "y"
        when: !root.positionManaged
        value: root.anchorMode === "fill" ? 0 : root.offsetY + root.relativeY * root.localHeight + (root.anchorMode === "center" ? (root.localHeight - root.height) / 2 : 0)
        restoreMode: Binding.RestoreNone
    }
    Binding {
        target: root
        property: "width"
        when: !root.sizeManaged
        value: root.anchorMode === "fill" ? root.localWidth : root.relativeWidth >= 0 ? root.relativeWidth * root.localWidth : root.explicitWidth >= 0 ? root.explicitWidth : root.implicitWidth
        restoreMode: Binding.RestoreNone
    }
    Binding {
        target: root
        property: "height"
        when: !root.sizeManaged
        value: root.anchorMode === "fill" ? root.localHeight : root.relativeHeight >= 0 ? root.relativeHeight * root.localHeight : root.explicitHeight >= 0 ? root.explicitHeight : root.implicitHeight
        restoreMode: Binding.RestoreNone
    }
}
