import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Label {
    id: control
    property string accessibleName: ""
    property string tooltip: ""
    Accessible.name: accessibleName || text
    textFormat: Text.PlainText
    wrapMode: Text.Wrap
    verticalAlignment: Text.AlignVCenter
    HoverHandler { id: hover }
    ToolTip.text: tooltip
    ToolTip.visible: tooltip.length > 0 && hover.hovered
    ToolTip.delay: 500
}
