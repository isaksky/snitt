import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Button {
    id: control
    required property QtObject relay
    property string accessibleName: ""
    property string tooltip: ""
    property string iconSource: ""
    icon.source: iconSource
    property bool iconOnly: false
    property string mode: "normal"
    property color modeColor: "transparent"
    property string contentSource: ""
    property string backgroundSource: ""
    readonly property color actionColor: modeColor.a > 0 ? modeColor
        : mode === "good" ? "#a3e6ca" : mode === "bad" ? "#f29a9a" : Material.accent
    readonly property color foreground: !enabled ? "#8f99a8" : highlighted ? "#142820" : checked ? actionColor : Material.foreground
    Accessible.name: accessibleName || text
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    display: iconOnly ? AbstractButton.IconOnly : AbstractButton.TextBesideIcon
    icon.width: 24
    icon.height: 24
    icon.color: foreground
    palette.buttonText: foreground
    leftPadding: 12
    rightPadding: 12
    topPadding: 8
    bottomPadding: 8
    topInset: 0
    bottomInset: 0
    implicitWidth: Math.max(40, (contentSource ? customContent.implicitWidth : implicitContentWidth) + leftPadding + rightPadding)
    implicitHeight: Math.max(40, (contentSource ? customContent.implicitHeight : implicitContentHeight) + topPadding + bottomPadding)
    ToolTip.text: tooltip
    ToolTip.visible: tooltip.length > 0 && (hovered || visualFocus)
    ToolTip.delay: 500
    onClicked: relay.dispatch(control, "on-click", "")

    // A replacement component can bind to parent.action. The owning button
    // continues to provide hit testing, keyboard activation and accessibility.
    Loader {
        id: customContent
        readonly property var action: control
        anchors.fill: parent
        anchors.margins: 8
        source: control.contentSource
    }
    // Material uses IconLabel; explicitly supply contrast for highlighted text.
    Binding { target: control.contentItem; property: "color"; value: control.foreground }
    readonly property Item customContentItem: customContent.item
    readonly property Item customBackgroundItem: customBackground.item
    contentItem.visible: contentSource.length === 0
    background: Item {
        Rectangle {
            anchors.fill: parent
            visible: control.backgroundSource.length === 0
            radius: 7
            color: !control.enabled ? "#30343b" : control.highlighted ? control.actionColor
                : control.down ? "#394149" : control.checked ? "#283b50"
                : control.hovered ? "#2a2e35" : "transparent"
            opacity: control.enabled && control.down && control.highlighted ? 0.75 : 1
            border.width: control.checked ? 1 : 0
            border.color: control.actionColor
        }
        Loader {
            id: customBackground
            property var action: control
            anchors.fill: parent
            source: control.backgroundSource
        }
        // Always drawn above custom backgrounds as well.
        Rectangle {
            anchors.fill: parent
            anchors.margins: 2
            color: "transparent"
            radius: 6
            border.width: 2
            border.color: control.actionColor
            visible: control.visualFocus
        }
    }
}
