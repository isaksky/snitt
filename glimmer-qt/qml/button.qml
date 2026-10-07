import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Button {
    id: control
    required property QtObject relay
    onClicked: relay.dispatch(control, "on-click", "")
}
