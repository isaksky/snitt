import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Label {
    id: control
    property real explicitHeight: -1
    Binding { target: control; property: "height"; when: control.explicitHeight >= 0; value: control.explicitHeight }
    property string requestedFontFamily: ""
    Binding { target: control; property: "font.family"; when: control.requestedFontFamily.length > 0; value: control.requestedFontFamily }
    property string accessibleName: ""
    property string tooltip: ""
    property bool tooltipOnPress: false
    Accessible.name: accessibleName || text
    textFormat: Text.PlainText
    wrapMode: Text.Wrap
    verticalAlignment: Text.AlignVCenter
    HoverHandler { id: hover }
    TapHandler { id: tap; enabled: control.tooltipOnPress }
    ToolTip.text: tooltip
    ToolTip.visible: tooltip.length > 0 && (tooltipOnPress ? tap.pressed : hover.hovered)
    ToolTip.delay: tooltipOnPress ? 0 : 500
}
