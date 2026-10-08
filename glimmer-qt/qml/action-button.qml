import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Presentation only. Checked state and all actions are controlled by the host.
Button {
    id: control
    property string requestedFontFamily: ""
    Binding { target: control; property: "font.family"; when: control.requestedFontFamily.length > 0; value: control.requestedFontFamily }
    required property QtObject relay
    property string accessibleName: ""
    property string tooltip: ""
    property string iconSource: ""
    property bool iconOnly: false
    property bool preserveIconColors: false
    property bool underlineShortcut: false
    property bool shortcutLabel: false
    property bool naturalHeight: false
    property string appearance: "action"
    property color modeColor: "transparent"
    readonly property color actionColor: modeColor
    readonly property bool primary: appearance === "primary"
    readonly property bool segment: appearance === "segment"
    readonly property bool customText: primary || segment || shortcutLabel
    readonly property color textColor: segment ? (enabled ? segmentTextColor() : "#8f99a8")
        : primary ? (enabled ? "#142820" : "#9ba5b5")
        : !enabled ? "#8f99a8" : highlighted ? "#162a40" : checked ? "#91bff0" : "#dfe5ed"

    function segmentTextColor() {
        function luminance(value) {
            function linear(c) { return c <= 0.04045 ? c / 12.92 : Math.pow((c + 0.055) / 1.055, 2.4) }
            return 0.2126 * linear(value.r) + 0.7152 * linear(value.g) + 0.0722 * linear(value.b)
        }
        const foreground = Qt.lighter(modeColor, 1.6)
        const background = checked ? Qt.darker(modeColor, 1.8) : Qt.color("#22262c")
        const f = luminance(foreground), b = luminance(background)
        if ((Math.max(f, b) + 0.05) / (Math.min(f, b) + 0.05) >= 4.5) return foreground
        return b > 0.18 ? "#000000" : "#ffffff"
    }

    Accessible.name: accessibleName || text
    focusPolicy: Qt.NoFocus
    flat: !primary
    display: iconOnly ? AbstractButton.IconOnly : AbstractButton.TextBesideIcon
    icon.source: iconSource
    icon.width: 26
    icon.height: 26
    icon.color: preserveIconColors ? "transparent" : textColor
    palette.buttonText: textColor
    leftPadding: primary ? 16 : segment ? 6 : iconOnly ? (preserveIconColors ? 7 : 8) : 12
    rightPadding: leftPadding
    topPadding: 0
    bottomPadding: 0
    topInset: 0
    bottomInset: 0
    implicitWidth: iconOnly ? 40 : Math.max(40, (customText ? label.implicitWidth : contentItem.implicitWidth) + leftPadding + rightPadding)
    implicitHeight: primary && naturalHeight ? label.implicitHeight + 20 : 40
    opacity: preserveIconColors && !enabled ? 0.4 : 1
    ToolTip.visible: pressed && tooltip.length > 0
    ToolTip.text: tooltip
    onClicked: relay.dispatch(control, "on-click", "")

    // Keep the style's IconLabel for artwork; custom text supplies the original
    // first-letter underline without rich-text font/layout differences.
    Item {
        anchors.fill: parent
        visible: control.customText && !control.iconOnly
        Text {
            id: label
            anchors.centerIn: parent
            text: control.text
            textFormat: Text.PlainText
            font: control.font
            color: control.textColor
            TextMetrics { id: firstLetter; font: label.font; text: label.text.charAt(0) }
            Rectangle {
                visible: control.underlineShortcut
                y: Math.ceil(label.baselineOffset + 2)
                width: Math.ceil(firstLetter.advanceWidth)
                height: 1
                color: label.color
            }
        }
    }
    contentItem.visible: control.iconOnly || !control.customText
    background: Rectangle {
        radius: control.segment ? 6 : 7
        color: control.segment ? "transparent"
            : control.primary ? (!control.enabled ? "#30343b" : control.down ? "#84ccb0" : control.hovered ? "#b5efd8" : "#a3e6ca")
            : control.highlighted ? (!control.enabled ? "#30343b" : control.down ? "#73a8df" : control.hovered ? "#b4d4f5" : "#91bff0")
            : control.checked ? "#283b50" : control.down ? "#394149" : control.hovered ? "#2a2e35" : "transparent"
        border.width: control.segment ? (control.visualFocus ? 1 : 0)
            : control.primary ? (control.visualFocus ? 2 : 0) : control.checked ? 1 : 0
        border.color: control.segment ? (control.visualFocus ? control.textColor : Qt.darker(control.modeColor, 1.2))
            : control.primary ? "#142820" : "#6994bf"
    }
}
