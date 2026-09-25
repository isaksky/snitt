import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    width: 640
    height: 420
    minimumWidth: 400
    minimumHeight: 300
    visible: true
    title: "xshot — Hello World"
    color: "#101318"

    Material.theme: Material.Dark
    Material.accent: "#80cbc4"

    ColumnLayout {
        anchors.centerIn: parent
        spacing: 20

        Label {
            text: backend.greeting
            font.pixelSize: 32
            font.weight: Font.DemiBold
            Layout.alignment: Qt.AlignHCenter
        }

        Label {
            text: "C++17 + Qt Quick"
            color: "#a6adb8"
            font.pixelSize: 16
            Layout.alignment: Qt.AlignHCenter
        }

        Button {
            text: "Say hello"
            highlighted: true
            Layout.alignment: Qt.AlignHCenter
            onClicked: backend.sayHello()
        }
    }
}
