import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Dialogs
import XShot 1.0

ApplicationWindow {
    id: win
    width: 1100
    height: 760
    minimumWidth: 860
    minimumHeight: 480
    visible: false
    title: "xshot"
    color: "#111317"
    Material.theme: Material.Dark
    Material.accent: "#a3e6ca"
    property bool editingText: false
    property real textX: 0
    property real textY: 0
    property real textWidth: 0
    property string notice: ""
    property bool restoreAfterCapture: false
    readonly property string commandKey: Qt.platform.os === "osx" ? "⌘" : "Ctrl+"
    readonly property bool shortcutsOn: !editingText && !backend.capturing && !openDialog.visible
        && !rearrangeDialog.visible && !captureErrorDialog.visible
    readonly property bool annotationShortcuts: shortcutsOn && !canvas.arranging
    readonly property string hint: editingText ? "Type your note · " + commandKey + "Enter to place · Esc to cancel"
        : canvas.tool === "cut" ? "Drag sideways to remove a column · drag up or down to remove a row"
        : canvas.tool === "rect" ? "Drag to draw a rectangle"
        : canvas.tool === "arrow" ? "Drag from the tail to the arrow tip"
        : "Click on the image to place text"

    function commitText() {
        if (!editingText) return
        canvas.addText(textX, textY, textWidth, canvas.imageHeight - textY, textInput.text)
        editingText = false
        textInput.text = ""
        canvas.forceActiveFocus()
    }
    function chooseTool(tool) { commitText(); canvas.tool = tool }
    function capture() { startCapture(false) }
    function captureMultiple() { startCapture(true) }
    function startCapture(multiple) {
        if (backend.capturing) return
        captureErrorDialog.close()
        commitText()
        notice = ""
        restoreAfterCapture = win.visible
        win.hide()
        backend.capture(multiple)
    }
    function arrangeRegions() {
        commitText()
        if (canvas.canUndo || canvas.canRedo) rearrangeDialog.open()
        else canvas.arrange()
    }
    function finish() {
        commitText()
        canvas.cancel()
        if (canvas.copy()) { win.hide(); canvas.clear() }
    }
    function showEditor() { win.show(); win.raise(); win.requestActivate() }
    function openImage(file) { commitText(); canvas.load(file); showEditor() }
    onClosing: close => { close.accepted = false; win.hide() }
    function showError(message) { notice = message; noticeTimer.restart() }

    Timer { id: noticeTimer; interval: 9000; onTriggered: win.notice = "" }
    Timer {
        interval: 200; running: true
        onTriggered: {
            if (initialImage.toString() !== "") win.openImage(initialImage)
            else if (showOnStart) win.showEditor()
            else if (!startInBackground) win.capture()
        }
    }
    Connections {
        target: backend
        function onCaptured(file) { canvas.load(file) }
        function onRegionsCaptured(images) { canvas.loadRegions(images) }
        function onError(message) {
            captureErrorDialog.message = message
            win.showEditor()
            captureErrorDialog.open()
        }
        function onCaptureFinished(captured) {
            if (captured || win.restoreAfterCapture || win.notice !== "") win.showEditor()
        }
    }

    Dialog {
        id: captureErrorDialog
        objectName: "captureErrorDialog"
        property string message: ""
        title: "Screen capture unavailable"
        anchors.centerIn: parent
        width: Math.min(560, win.width - 48)
        modal: true
        closePolicy: Popup.NoAutoClose
        contentItem: ColumnLayout {
            spacing: 20
            Label {
                text: captureErrorDialog.message
                color: "#dfe5ed"
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                ActionButton { text: "Close"; onClicked: captureErrorDialog.close() }
                PrimaryButton {
                    text: "Open System Settings"
                    visible: Qt.platform.os === "osx"
                    onClicked: Qt.openUrlExternally("x-apple.systempreferences:com.apple.preference.security?Privacy_ScreenCapture")
                }
            }
        }
    }

    Dialog {
        id: rearrangeDialog
        title: "Rearrange regions?"
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.Cancel | Dialog.Ok
        Label { text: "This resets annotations and cuts. Your original regions are kept."; color: "#dfe5ed" }
        onAccepted: canvas.arrange()
    }
    FileDialog {
        id: openDialog
        title: "Open a screenshot"
        nameFilters: ["Images (*.png *.jpg *.jpeg *.webp *.bmp *.tif *.tiff)"]
        onAccepted: { win.commitText(); canvas.load(selectedFile) }
    }

    Shortcut { sequence: "X"; enabled: win.annotationShortcuts; onActivated: win.chooseTool("cut") }
    Shortcut { sequence: "R"; enabled: win.annotationShortcuts; onActivated: win.chooseTool("rect") }
    Shortcut { sequence: "T"; enabled: win.annotationShortcuts; onActivated: win.chooseTool("text") }
    Shortcut { sequence: "A"; enabled: win.annotationShortcuts; onActivated: win.chooseTool("arrow") }
    Shortcut { sequence: "G"; enabled: win.annotationShortcuts; onActivated: canvas.ink = "#22c55e" }
    Shortcut { sequence: "B"; enabled: win.annotationShortcuts; onActivated: canvas.ink = "#ef4444" }
    Shortcut { sequence: "Return"; enabled: win.shortcutsOn && canvas.hasImage; onActivated: canvas.arranging ? canvas.annotate() : win.finish() }
    Shortcut { sequence: "Enter"; enabled: win.shortcutsOn && canvas.hasImage; onActivated: canvas.arranging ? canvas.annotate() : win.finish() }
    Shortcut { sequences: [StandardKey.Undo]; enabled: win.annotationShortcuts; onActivated: canvas.undo() }
    Shortcut { sequences: [StandardKey.Redo]; enabled: win.annotationShortcuts; onActivated: canvas.redo() }
    Shortcut { sequences: ["Backspace", "Delete"]; enabled: win.shortcutsOn && canvas.arranging; onActivated: canvas.removeRegion(canvas.selectedRegion) }
    Shortcut { sequence: "Left"; enabled: win.shortcutsOn && canvas.arranging; onActivated: canvas.moveRegion(canvas.selectedRegion, canvas.selectedRegion - 1) }
    Shortcut { sequence: "Right"; enabled: win.shortcutsOn && canvas.arranging; onActivated: canvas.moveRegion(canvas.selectedRegion, canvas.selectedRegion + 1) }
    Shortcut { sequences: [StandardKey.Open]; enabled: win.shortcutsOn; onActivated: openDialog.open() }
    Shortcut { sequences: [StandardKey.New]; enabled: win.shortcutsOn; onActivated: win.capture() }
    Shortcut { sequences: [StandardKey.Paste]; enabled: win.shortcutsOn; onActivated: canvas.paste() }
    Shortcut {
        sequences: [StandardKey.Copy]; enabled: win.shortcutsOn && canvas.hasImage
        onActivated: { canvas.copy(); win.notice = "Copied to clipboard"; noticeTimer.restart() }
    }
    Shortcut {
        sequence: "Escape"; enabled: win.shortcutsOn
        onActivated: canvas.cancel()
    }

    component ActionButton: Button {
        id: actionControl
        property color textColor: checked ? "#a3e6ca" : "#dfe5ed"
        flat: true
        font.pixelSize: 13
        leftPadding: 14
        rightPadding: 14
        implicitHeight: 40
        focusPolicy: Qt.NoFocus
        contentItem: Text {
            text: actionControl.text
            font: actionControl.font
            color: actionControl.enabled ? actionControl.textColor : "#8f99a8"
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: 7
            color: parent.checked ? "#303b3a" : parent.hovered ? "#2a2e35" : "transparent"
            border.width: parent.checked ? 1 : 0
            border.color: "#82988f"
        }
    }

    component PrimaryButton: Button {
        id: primaryControl
        highlighted: true
        font.weight: Font.DemiBold
        leftPadding: 16
        rightPadding: 16
        topPadding: 10
        bottomPadding: 10
        topInset: 0
        bottomInset: 0
        // Explicit colors avoid Material's light highlighted label on our mint accent.
        contentItem: Text {
            text: primaryControl.text
            font: primaryControl.font
            color: primaryControl.enabled ? "#142820" : "#9ba5b5"
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: 7
            color: !primaryControl.enabled ? "#30343b"
                : primaryControl.down ? "#84ccb0"
                : primaryControl.hovered ? "#b5efd8" : "#a3e6ca"
            border.width: primaryControl.visualFocus ? 2 : 0
            border.color: "#142820"
        }
    }

    header: Rectangle {
        height: 70
        color: "#1b1e23"
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 20
            anchors.rightMargin: 20
            spacing: 6
            Label { text: "xshot"; font.pixelSize: 20; font.weight: Font.DemiBold; Layout.rightMargin: 16 }
            Repeater {
                model: [ {label: "Cut", key: "X", tool: "cut"},
                         {label: "Rect", key: "R", tool: "rect"},
                         {label: "Text", key: "T", tool: "text"},
                         {label: "Arrow", key: "A", tool: "arrow"} ]
                ActionButton {
                    required property var modelData
                    text: modelData.label + "   " + modelData.key
                    checked: canvas.tool === modelData.tool
                    enabled: canvas.hasImage && !canvas.arranging
                    onClicked: win.chooseTool(modelData.tool)
                }
            }
            Rectangle { width: 1; height: 24; color: "#373c44"; Layout.leftMargin: 12; Layout.rightMargin: 12 }
            Repeater {
                model: [ {label: "Good", ink: "#22c55e", labelColor: "#57dc8b", key: "G"},
                         {label: "Bad", ink: "#ef4444", labelColor: "#ff9999", key: "B"} ]
                ActionButton {
                    required property var modelData
                    text: "●  " + modelData.label
                    textColor: modelData.labelColor
                    checked: canvas.ink.toString() === modelData.ink
                    onClicked: { win.commitText(); canvas.ink = modelData.ink }
                    ToolTip.visible: hovered
                    ToolTip.text: modelData.key
                }
            }
            Item { Layout.fillWidth: true }
            PrimaryButton {
                id: doneButton
                text: canvas.arranging ? "Copy" : "Done   ↵"
                enabled: canvas.hasImage
                focusPolicy: Qt.NoFocus
                onClicked: win.finish()
                ToolTip.visible: hovered
                ToolTip.text: "Copy image to clipboard and finish"
            }
        }
    }

    Rectangle {
        id: arrangementBar
        anchors.top: parent.top
        width: parent.width
        height: visible ? 64 : 0
        visible: canvas.regionCount > 0
        color: "#1b1e23"
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 20; anchors.rightMargin: 20
            spacing: 8
            Label { text: canvas.regionCount + " regions"; color: "#dfe5ed"; font.weight: Font.DemiBold }
            Label { text: "Columns"; color: "#9ba5b5"; visible: canvas.arranging; Layout.leftMargin: 12 }
            ActionButton { text: "−"; Accessible.name: "Fewer columns"; visible: canvas.arranging; enabled: canvas.columns > 1; onClicked: canvas.columns-- }
            Label { text: canvas.columns; color: "#dfe5ed"; visible: canvas.arranging }
            ActionButton { text: "+"; Accessible.name: "More columns"; visible: canvas.arranging; enabled: canvas.columns < Math.min(6, canvas.regionCount); onClicked: canvas.columns++ }
            Item { Layout.fillWidth: true }
            ActionButton { text: "Move left"; visible: canvas.arranging; enabled: canvas.selectedRegion > 0; onClicked: canvas.moveRegion(canvas.selectedRegion, canvas.selectedRegion - 1) }
            ActionButton { text: "Move right"; visible: canvas.arranging; enabled: canvas.selectedRegion >= 0 && canvas.selectedRegion < canvas.regionCount - 1; onClicked: canvas.moveRegion(canvas.selectedRegion, canvas.selectedRegion + 1) }
            ActionButton { text: "Remove"; visible: canvas.arranging; enabled: canvas.selectedRegion >= 0; onClicked: canvas.removeRegion(canvas.selectedRegion) }
            PrimaryButton { text: "Annotate   ↵"; visible: canvas.arranging; onClicked: { canvas.annotate(); canvas.forceActiveFocus() } }
            ActionButton { text: "Arrange regions"; visible: !canvas.arranging; onClicked: win.arrangeRegions() }
        }
    }

    EditorCanvas {
        id: canvas
        objectName: "canvas"
        anchors.fill: parent
        anchors.topMargin: arrangementBar.height
        focus: true
        onError: message => win.showError(message)
        onTextRequested: (x, y) => {
            win.textWidth = Math.min(360, canvas.imageWidth)
            win.textX = Math.min(x, canvas.imageWidth - win.textWidth)
            win.textY = Math.max(0, Math.min(y, canvas.imageHeight - 32))
            win.editingText = true
            textInput.text = ""
            textInput.forceActiveFocus()
        }
        MouseArea {
            anchors.fill: parent
            enabled: canvas.hasImage
            cursorShape: canvas.arranging ? Qt.OpenHandCursor : canvas.tool === "text" ? Qt.IBeamCursor : Qt.CrossCursor
            onPressed: mouse => {
                if (win.editingText) { win.commitText(); return }
                canvas.forceActiveFocus()
                canvas.begin(mouse.x, mouse.y)
            }
            onPositionChanged: mouse => { if (pressed) canvas.move(mouse.x, mouse.y) }
            onReleased: mouse => canvas.end(mouse.x, mouse.y)
            onCanceled: canvas.cancel()
        }
        TextArea {
            id: textInput
            objectName: "annotationText"
            visible: win.editingText
            x: canvas.imageRect.x + win.textX * canvas.imageScale
            y: canvas.imageRect.y + win.textY * canvas.imageScale
            width: win.textWidth * canvas.imageScale
            height: Math.min(Math.max(36 * canvas.imageScale, implicitHeight),
                             (canvas.imageHeight - win.textY) * canvas.imageScale)
            padding: 0
            topInset: 0
            bottomInset: 0
            leftInset: 0
            rightInset: 0
            font.family: Qt.platform.os === "windows" ? "Segoe UI" : "Helvetica"
            font.pixelSize: Math.max(1, 24 * canvas.imageScale)
            font.weight: Font.DemiBold
            color: canvas.ink
            selectionColor: "#42655b"
            wrapMode: TextEdit.WordWrap
            selectByMouse: true
            Accessible.name: "Annotation text"
            background: Rectangle { color: "#dd181b21"; border.color: "#9ba5b5"; radius: 2 }
            Keys.onPressed: event => {
                if (event.key === Qt.Key_Escape) {
                    win.editingText = false
                    canvas.forceActiveFocus()
                    event.accepted = true
                } else if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                           && (event.modifiers & Qt.ControlModifier)) {
                    win.commitText()
                    event.accepted = true
                }
            }
        }
    }

    ColumnLayout {
        anchors.centerIn: parent
        visible: !canvas.hasImage
        spacing: 16
        Label { text: "No screenshot open"; font.pixelSize: 26; font.weight: Font.DemiBold; Layout.alignment: Qt.AlignHCenter }
        Label {
            text: Qt.platform.os === "osx"
                ? "Press Ctrl+Print Screen (F13), or Ctrl+Shift+X, to select a screen region."
                : "Press Ctrl+Print Screen to select a screen region, or open or paste an image."
            color: "#9ba5b5"
            font.pixelSize: 14
            Layout.alignment: Qt.AlignHCenter
        }
        PrimaryButton { text: "Select a screen region"; Layout.alignment: Qt.AlignHCenter; onClicked: win.capture() }
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            ActionButton { text: "Open image"; onClicked: openDialog.open() }
            Label { text: "or"; color: "#9ba5b5" }
            ActionButton { text: "Paste image"; onClicked: canvas.paste() }
        }
        ActionButton { text: "Select multiple regions"; Layout.alignment: Qt.AlignHCenter; onClicked: win.captureMultiple() }
    }

    footer: Rectangle {
        height: 88
        color: "#1b1e23"
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 0
            RowLayout {
                spacing: 4
                ActionButton { text: "New capture"; onClicked: win.capture() }
                ActionButton { text: "Multiple regions"; onClicked: win.captureMultiple() }
                ActionButton { text: "Open"; onClicked: openDialog.open() }
                ActionButton { text: "Undo"; enabled: canvas.canUndo && !win.editingText && !canvas.arranging; onClicked: canvas.undo() }
                ActionButton { text: "Redo"; enabled: canvas.canRedo && !win.editingText && !canvas.arranging; onClicked: canvas.redo() }
                Item { Layout.fillWidth: true }
                Label {
                    text: canvas.hasImage ? canvas.imageWidth + " × " + canvas.imageHeight + " px" : win.commandKey + "V to paste an image"
                    color: "#8f99a8"; font.pixelSize: 12; Layout.rightMargin: 8
                }
            }
            Label {
                text: win.notice !== "" ? win.notice
                    : canvas.arranging ? "Drag regions to reorder · Click to select · Delete to remove · Enter to annotate"
                    : canvas.hasImage ? win.hint : "Select a region to start · M selects multiple regions · Esc cancels"
                color: win.notice !== "" ? "#a3e6ca" : "#8f99a8"
                font.pixelSize: 12
                elide: Text.ElideRight
                Layout.fillWidth: true
                Layout.leftMargin: 14
            }
        }
    }
}
