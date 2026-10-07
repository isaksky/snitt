import QtQuick
Text {
    text: parent.action.text + (parent.action.checked ? " ✓" : "")
    font: parent.action.font
    color: parent.action.foreground
    horizontalAlignment: Text.AlignHCenter
    verticalAlignment: Text.AlignVCenter
}
