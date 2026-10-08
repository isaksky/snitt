import QtQuick
import QtQuick.Layouts

Item {
    id: root
    anchors.fill: parent
    readonly property Item content: children.length ? children[0] : null
    Binding { target: root.content; property: "width"; value: root.content ? root.content.implicitWidth : 0; when: !!root.content }
    Binding { target: root.content; property: "height"; value: root.content ? root.content.implicitHeight : 0; when: !!root.content }
    Binding { target: root.content; property: "anchors.centerIn"; value: root; when: !!root.content }
}
