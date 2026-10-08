import QtQuick
import QtQuick.Controls
import QtQuick.Controls.impl
import QtQuick.Layouts
import QtQuick.Window

// Stateless presentation of the selector's responsive row. The host owns mode,
// selections and shortcuts; only label fitting and pointer feedback live here.
Item {
    id: root
    required property QtObject relay
    property real screenWidth: 960
    property string mode: "single"
    property int selectionCount: 0
    property var hints: ({})
    property var shortcuts: ({})
    property alias singleButton: single
    property alias multipleButton: multiple
    property alias videoButton: video
    property alias arrangeButton: arrange
    property alias cancelButton: cancel
    readonly property bool compact: screenWidth < 560
    readonly property bool hasSelections: mode === "multiple" && selectionCount > 0
    readonly property string instruction: mode === "video" ? "Drag to record." : mode === "multiple" ? "Drag to add regions." : "Drag to capture."
    implicitHeight: 60
    implicitWidth: Math.min(960, Math.max(1, screenWidth - 24))
    property bool instructionVisible: true
    property bool fitting: false
    function hint(key) { return hints[key] || "" }
    function withHint(label, hint) { return hint.length ? label + " (" + hint + ")" : label }
    function fit() {
        if (fitting) return
        fitting = true
        const buttons = [single, multiple, video, arrange, cancel]
        for (let b of buttons) { b.shorten = false; b.iconOnly = false }
        function required() {
            let result = 24, count = 0
            for (let b of buttons) if (b.visible) { result += b.implicitWidth; ++count }
            if (root.hasSelections) { result += countLabel.implicitWidth; ++count }
            return result + count * 8
        }
        for (let b of [arrange, cancel]) {
            if (required() <= width) break
            if (b.shortWidth < b.fullWidth) b.shorten = true
        }
        for (let b of [multiple, video, single]) {
            if (required() <= width) break
            b.iconOnly = true
        }
        instructionVisible = required() + instructionLabel.implicitWidth + 8 <= width
        let left = 12
        for (let b of [single, multiple, video]) { b.x = left; b.width = b.implicitWidth; left += b.width + 8 }
        instructionLabel.x = left
        let right = width - 12
        for (let b of [cancel, arrange]) {
            b.width = b.implicitWidth
            if (b.visible) { right -= b.width; b.x = right; right -= 8 }
        }
        countLabel.x = right - countLabel.implicitWidth
        fitting = false
    }
    onWidthChanged: Qt.callLater(fit)
    onScreenWidthChanged: Qt.callLater(fit)
    onModeChanged: Qt.callLater(fit)
    onSelectionCountChanged: Qt.callLater(fit)
    onHintsChanged: Qt.callLater(fit)
    onShortcutsChanged: Qt.callLater(fit)
    Component.onCompleted: Qt.callLater(fit)

    Image {
        anchors.fill: parent
        source: "image://snitt-selector-artwork/panel/" + width + "/" + height + "/normal"
        sourceSize.width: width; sourceSize.height: height
    }
    MouseArea {
        anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.ArrowCursor
        NativeToolTip { relay: root.relay; text: root.instruction; active: parent.containsMouse && !root.instructionVisible }
    }
    component SelectorButton: Button {
        id: button
        property string fullLabel: ""
        property string shortLabel: fullLabel
        property string commandKey: ""
        property string tooltip: ""
        property string iconName: ""
        property bool primary: false
        property bool shorten: false
        property bool iconOnly: false
        property bool mnemonic: false
        readonly property real fullWidth: Math.ceil(metrics.advanceWidth) + (iconName ? 26 : 0) + 22
        readonly property real shortWidth: Math.ceil(shortMetrics.advanceWidth) + (iconName ? 26 : 0) + 22
        y: 12; height: 36
        implicitWidth: iconOnly ? 48 : shorten ? shortWidth : fullWidth
        implicitHeight: 36
        text: iconOnly ? "" : shorten ? shortLabel : fullLabel
        font.pixelSize: root.compact ? 12 : 14
        font.weight: Font.Normal
        focusPolicy: Qt.NoFocus
        hoverEnabled: true
        spacing: 4
        padding: 0
        leftPadding: 0; rightPadding: 0; topPadding: 0; bottomPadding: 0
        topInset: 0; bottomInset: 0
        display: iconOnly ? AbstractButton.IconOnly : AbstractButton.TextBesideIcon
        icon.source: iconName ? "qrc:/icons/" + iconName + ".svg" : ""
        icon.width: 22; icon.height: 22
        icon.color: !enabled ? "#727a86" : checked ? "#15283d" : "#e9edf3"
        palette.buttonText: !enabled ? "#727a86" : primary ? "#142820" : checked ? "#15283d" : "#e9edf3"
        contentItem: Image {
            source: width > 0 && height > 0 ? "image://snitt-selector-artwork/" +
                (button.iconName ? "mode/" : "label/") + width + "/" + height + "/" +
                encodeURIComponent(button.iconName
                    ? [button.font.pixelSize, button.enabled, button.checked, button.mnemonic, button.iconName, button.text].join("|")
                    : [button.font.pixelSize, button.palette.buttonText, "center", button.text].join("|")) : ""
            sourceSize.width: width; sourceSize.height: height
        }
        Accessible.name: tooltip
        // Use AbstractButton's native accessibility actions and checked value.
        // Exclusive modes cannot temporarily uncheck on a repeated activation.
        checkable: iconName.length > 0
        autoExclusive: checkable
        Accessible.onPressAction: button.click()
        Accessible.onToggleAction: button.click()
        NativeToolTip { relay: root.relay; text: button.tooltip; active: button.hovered && !button.pressed }
        TextMetrics { id: metrics; font: button.font; text: button.fullLabel }
        TextMetrics { id: shortMetrics; font: button.font; text: button.shortLabel }
        background: Image {
            readonly property string fill: !button.enabled ? (button.primary ? "#2b3238" : "transparent")
                : button.primary ? "#a3e6ca" : button.checked ? "#91bff0" : button.hovered ? "#323842" : "transparent"
            source: width > 0 && height > 0 ? "image://snitt-selector-artwork/button/" + width + "/" + height + "/" + encodeURIComponent(fill) : ""
            sourceSize.width: width; sourceSize.height: height
        }
        HoverHandler { cursorShape: Qt.PointingHandCursor }
        onClicked: root.relay.dispatch(root, "on-action", commandKey)
    }
    SelectorButton {
        id: single; objectName: "picker-single"
        fullLabel: "Region"; commandKey: "single"; iconName: "square-dashed"
        tooltip: root.withHint("Select a region", root.hint("regionSingle"))
        checked: root.mode === "single"
        mnemonic: (root.shortcuts.regionSingle || []).indexOf("R") >= 0
    }
    SelectorButton {
        id: multiple; objectName: "picker-multiple"
        fullLabel: "Multi"; commandKey: "multiple"; iconName: "copy"
        tooltip: root.withHint("Select multiple regions", root.hint("regionMultiple"))
        checked: root.mode === "multiple"; enabled: root.mode !== "video"
        mnemonic: (root.shortcuts.regionMultiple || []).indexOf("M") >= 0
    }
    SelectorButton {
        id: video; objectName: "picker-video"
        fullLabel: "Video"; commandKey: "video"; iconName: "video"
        tooltip: root.withHint("Record a region", root.hint("regionVideo"))
        checked: root.mode === "video"
        mnemonic: (root.shortcuts.regionVideo || []).indexOf("V") >= 0
    }
    component SelectorLabel: Item {
        id: caption
        property string text
        property string alignment: "left"
        y: 12; height: 36; width: implicitWidth
        implicitWidth: Math.ceil(labelMetrics.advanceWidth)
        implicitHeight: 36
        Accessible.role: Accessible.StaticText
        Accessible.name: text
        Image {
            anchors.fill: parent
            source: width > 0 ? "image://snitt-selector-artwork/label/" + width + "/36/" +
                encodeURIComponent([root.compact ? 12 : 14, "#b9c1cd", caption.alignment, caption.text].join("|")) : ""
            sourceSize.width: width; sourceSize.height: height
        }
        TextMetrics { id: labelMetrics; font.pixelSize: root.compact ? 12 : 14; text: caption.text }
    }
    SelectorLabel { id: instructionLabel; visible: root.instructionVisible; text: root.instruction }
    SelectorLabel {
        id: countLabel; alignment: "right"; visible: root.hasSelections
        text: root.selectionCount + (root.selectionCount === 1 ? " region" : " regions")
    }
    SelectorButton {
        id: arrange; objectName: "picker-arrange"
        visible: root.hasSelections; primary: true; commandKey: "accept"
        fullLabel: root.withHint("Arrange", root.hint("regionArrange").split(" / ")[0])
        shortLabel: "Arrange"
        tooltip: root.withHint("Continue to Arrange", root.hint("regionArrange"))
    }
    SelectorButton {
        id: cancel; objectName: "picker-cancel"
        commandKey: "cancel"
        fullLabel: root.compact && root.hint("regionCancel") ? root.hint("regionCancel").split(" / ")[0]
            : root.withHint("Cancel", root.hint("regionCancel").split(" / ")[0])
        shortLabel: "Cancel"
        tooltip: root.withHint("Cancel selection", root.hint("regionCancel"))
    }
}
