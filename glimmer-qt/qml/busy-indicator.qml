import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Controls.Material

BusyIndicator {
    property string accessibleName: ""
    property string tooltip: ""
    Accessible.name: accessibleName
    ToolTip.text: tooltip
}
