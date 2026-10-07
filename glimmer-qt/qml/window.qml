import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    property alias mountPoint: content
    color: "#111317"
    Material.theme: Material.Dark
    Material.accent: "#91bff0"
    ColumnLayout {
        id: content
        anchors.fill: parent
        spacing: 0
    }
}
