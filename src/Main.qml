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
    minimumHeight: 560
    visible: false
    title: "xshot"
    color: "#111317"
    Material.theme: Material.Dark
    Material.accent: "#91bff0"
    property bool editingText: false
    property real textX: 0
    property real textY: 0
    property real textWidth: 0
    property string notice: ""
    property bool restoreAfterCapture: false
    readonly property string commandKey: Qt.platform.os === "osx" ? "⌘" : "Ctrl+"
    readonly property string copyKey: "C"
    readonly property string redoKey: Qt.platform.os === "osx" ? "⇧⌘Z" : "Ctrl+Y"
    readonly property bool shortcutsOn: !editingText && !backend.capturing && !backend.recording && !openDialog.visible
        && !rearrangeDialog.visible && !captureErrorDialog.visible
    readonly property bool annotationShortcuts: shortcutsOn && !canvas.arranging
    readonly property string hint: editingText ? "Text · " + canvas.textSize + " px · Wheel outside note to resize · " + commandKey + "Enter to place · Esc to cancel"
        : canvas.tool === "cut" ? "Drag sideways to remove a column · drag up or down to remove a row"
        : canvas.tool === "rect" ? "Rectangle · " + canvas.strokeWidth + " px · Wheel to resize · Drag to draw"
        : canvas.tool === "highlight" ? "Drag to highlight an area"
        : canvas.tool === "arrow" ? "Arrow · " + canvas.strokeWidth + " px · Wheel to resize · Drag from tail to tip"
        : canvas.tool === "blur" ? "Pixelate · " + canvas.pixelBlockSize + " px blocks · Wheel to resize · Drag over an area"
        : canvas.tool === "erase" ? "Drag to fill an area with the color where you started"
        : "Text · " + canvas.textSize + " px · Wheel to resize · Click to place"

    function commitText() {
        if (!editingText) return
        canvas.addText(textX, textY, textWidth, canvas.imageHeight - textY, textInput.text, canvas.textSize)
        editingText = false
        textInput.text = ""
        canvas.forceActiveFocus()
    }
    function chooseTool(tool) { commitText(); canvas.tool = tool }
    function pasteImage() {
        if (!canvas.paste()) return
        // Pasting replaces the whole document. Discard an unfinished draft only
        // after a successful paste so an empty clipboard leaves it intact.
        editingText = false
        textInput.text = ""
        textX = 0
        textY = 0
        textWidth = 0
        canvas.forceActiveFocus()
    }
    function capture() { startCapture(false, false) }
    function hotkeyCapture() {
        if (backend.recording) { backend.stopRecordingFromHotkey(); return }
        capture()
    }
    function captureMultiple() { startCapture(true, false) }
    function captureVideo() { startCapture(false, true) }
    function startCapture(multiple, video) {
        if (backend.recording) return
        if (backend.capturing) return
        captureErrorDialog.close()
        commitText()
        notice = ""
        restoreAfterCapture = win.visible
        win.hide()
        backend.capture(multiple, video)
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
    function saveAndClose() {
        commitText()
        canvas.cancel()
        const path = canvas.save()
        if (path === "") return
        win.hide()
        canvas.clear()
        if (!backend.revealFile(path)) {
            captureErrorDialog.message = "Screenshot saved at " + path + ", but could not reveal it in the file manager."
            win.showEditor()
            captureErrorDialog.open()
        }
    }
    function dismissEditor() {
        win.editingText = false
        textInput.text = ""
        win.restoreAfterCapture = false
        captureErrorDialog.close()
        rearrangeDialog.close()
        win.hide()
        canvas.clear()
    }
    function showEditor() { win.show(); win.raise(); win.requestActivate() }
    function openImage(file) { commitText(); canvas.load(file); showEditor() }
    onClosing: close => { close.accepted = false; win.dismissEditor() }
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
            if (!backend.recording && (captured || win.restoreAfterCapture || win.notice !== "")) win.showEditor()
        }
        function onRecordingSaved(path) { win.hide(); canvas.clear() }
        function onRecordingCanceled() { if (win.restoreAfterCapture) win.showEditor() }
        function onRecordingChanged() {
            if (!backend.recording) {
                startupIndicator.completing = false
            }
        }
        function onRecordingReady() {
            startupIndicator.complete()
        }
    }

    Window {
        id: startupIndicator
        objectName: "recordingStartupIndicator"
        property bool completing: false
        property int diameter: 0
        function complete() { completing = true; completionTimer.restart() }
        title: "xshot — Preparing recording"
        transientParent: null
        width: diameter; height: diameter
        flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.WindowTransparentForInput
        color: "transparent"
        visible: backend.startingRecording || completing
        opacity: backend.recordingProtectionPending ? 0 : 1
        onVisibleChanged: if (visible) {
            const area = backend.recordingRegion
            let selectedScreen = screen
            for (const candidate of Qt.application.screens) {
                if (area.x >= candidate.virtualX && area.x < candidate.virtualX + candidate.width
                        && area.y >= candidate.virtualY && area.y < candidate.virtualY + candidate.height) {
                    selectedScreen = candidate
                    break
                }
            }
            screen = selectedScreen
            const placement = backend.recordingIndicatorGeometry
            diameter = placement.width
            x = placement.x
            y = placement.y
            raise()
            if (Qt.platform.os === "windows")
                Qt.callLater(() => backend.beginProtectedRecording(startupIndicator, recordingWindow))
        }
        Timer {
            id: completionTimer
            interval: 180
            onTriggered: startupIndicator.completing = false
        }
        Rectangle {
            anchors.fill: parent
            radius: width / 2
            color: "#d91b1e23"
            border.color: "#6687ac9b"
            border.width: 2
        }
        Canvas {
            id: circularWipe
            anchors.centerIn: parent
            width: parent.width * 0.72
            height: width
            onPaint: {
                const context = getContext("2d")
                context.reset()
                context.lineWidth = Math.max(7, width * 0.06)
                context.lineCap = "round"
                context.strokeStyle = "#91bff0"
                context.beginPath()
                context.arc(width / 2, height / 2, width * 0.42,
                            -Math.PI / 2, startupIndicator.completing ? 3 * Math.PI / 2 : Math.PI)
                context.stroke()
            }
            RotationAnimator on rotation {
                from: 0; to: 360; duration: 1050
                loops: Animation.Infinite
                running: startupIndicator.visible && !startupIndicator.completing
            }
            Connections {
                target: startupIndicator
                function onCompletingChanged() { circularWipe.requestPaint() }
            }
        }
        Text {
            anchors.centerIn: parent
            text: startupIndicator.completing ? "Ready" : "Preparing"
            color: "#f0f3f7"
            font.pixelSize: Math.max(14, startupIndicator.diameter * 0.075)
            font.weight: Font.DemiBold
        }
    }

    Window {
        id: recordingWindow
        objectName: "recordingWindow"
        title: "xshot — Recording"
        transientParent: null
        width: backend.recordingControlsGeometry.width
        height: backend.recordingControlsGeometry.height
        x: backend.recordingControlsGeometry.x
        y: backend.recordingControlsGeometry.y
        flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
        color: "transparent"
        Material.theme: Material.Dark
        visible: backend.recording
        opacity: backend.recordingProtectionPending ? 0 : 1
        onVisibleChanged: if (visible) {
            const area = backend.recordingRegion
            for (const candidate of Qt.application.screens) {
                if (area.x >= candidate.virtualX && area.x < candidate.virtualX + candidate.width
                        && area.y >= candidate.virtualY && area.y < candidate.virtualY + candidate.height) {
                    screen = candidate
                    break
                }
            }
            const placement = backend.recordingControlsGeometry
            x = placement.x
            y = placement.y
            raise()
        }
        onClosing: close => { close.accepted = false; if (!backend.finishingRecording) backend.cancelRecording() }
        Rectangle {
            anchors.fill: parent
            radius: 12
            color: "#ee1b1e23"
            border.color: "#6994bf"
            border.width: 1
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12; anchors.rightMargin: 8
                spacing: 6
                Label {
                    Layout.fillWidth: true
                    text: backend.finishingRecording ? "Saving…"
                        : backend.startingRecording ? "Starting…"
                        : "Recording · " + Math.floor(backend.recordingElapsed / 60) + ":"
                            + ("0" + (backend.recordingElapsed % 60)).slice(-2)
                    color: "#f0f3f7"; font.pixelSize: 13; font.weight: Font.DemiBold
                }
                ActionButton {
                    objectName: "recordingCancelButton"
                    text: "Cancel"
                    Accessible.name: "Cancel recording"
                    enabled: !backend.finishingRecording
                    onClicked: backend.cancelRecording()
                    ToolTip.visible: hovered
                    ToolTip.text: "Cancel and discard recording (Esc)"
                }
                PrimaryButton {
                    objectName: "recordingStopButton"
                    text: "Stop"
                    Accessible.name: "Stop and save recording"
                    enabled: !backend.startingRecording && !backend.finishingRecording
                    onClicked: backend.finishRecording()
                    ToolTip.visible: hovered
                    ToolTip.text: "Stop and save (Ctrl+Print Screen or " + win.commandKey + "C)"
                }
            }
        }
        Shortcut { sequences: [StandardKey.Copy]; enabled: recordingWindow.visible && !backend.startingRecording && !backend.finishingRecording; onActivated: backend.finishRecording() }
        Shortcut { sequence: "Escape"; enabled: recordingWindow.visible && !backend.finishingRecording; onActivated: backend.cancelRecording() }
    }

    Dialog {
        id: captureErrorDialog
        objectName: "captureErrorDialog"
        property string message: ""
        title: message.startsWith("Screenshot saved at ") ? "Could not reveal screenshot" : "Capture unavailable"
        anchors.centerIn: parent
        width: Math.min(560, win.width - 48)
        modal: true
        closePolicy: Popup.CloseOnEscape
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
                    visible: Qt.platform.os === "osx" && captureErrorDialog.message.indexOf("System Settings") >= 0
                    onClicked: Qt.openUrlExternally("x-apple.systempreferences:com.apple.preference.security?Privacy_ScreenCapture")
                }
            }
        }
    }

    Dialog {
        id: rearrangeDialog
        objectName: "rearrangeDialog"
        title: "Back to Arrange?"
        anchors.centerIn: parent
        width: Math.min(560, win.width - 48)
        modal: true
        background: Rectangle { color: "#242930"; radius: 12; border.color: "#454d59" }
        Overlay.modal: Rectangle { color: "#99000000" }
        header: Label {
            text: rearrangeDialog.title
            color: "#f0f3f7"
            font.pixelSize: 20
            font.weight: Font.DemiBold
            leftPadding: 24; rightPadding: 24; topPadding: 24; bottomPadding: 8
        }
        contentItem: ColumnLayout {
            spacing: 20
            Label {
                text: "Returning to Arrange will discard your annotations and cuts. Your captured regions will be kept."
                color: "#dfe5ed"
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                ActionButton {
                    objectName: "keepEditingButton"
                    text: "Keep editing"
                    onClicked: rearrangeDialog.reject()
                }
                PrimaryButton {
                    objectName: "discardEditsButton"
                    text: "Discard edits"
                    onClicked: rearrangeDialog.accept()
                }
            }
        }
        onAccepted: { canvas.arrange(); canvas.forceActiveFocus() }
        onRejected: canvas.forceActiveFocus()
    }
    FileDialog {
        id: openDialog
        title: "Open a screenshot"
        nameFilters: ["Images (*.png *.jpg *.jpeg *.webp *.bmp *.tif *.tiff)"]
        onAccepted: { win.commitText(); canvas.load(selectedFile) }
    }

    Shortcut { sequence: "X"; enabled: win.annotationShortcuts; onActivated: win.chooseTool("cut") }
    Shortcut { sequence: "R"; enabled: win.annotationShortcuts; onActivated: win.chooseTool("rect") }
    Shortcut { sequence: "H"; enabled: win.annotationShortcuts; onActivated: win.chooseTool("highlight") }
    Shortcut { sequence: "T"; enabled: win.annotationShortcuts; onActivated: win.chooseTool("text") }
    Shortcut { sequence: "A"; enabled: win.annotationShortcuts; onActivated: win.chooseTool("arrow") }
    Shortcut { sequence: "B"; enabled: win.annotationShortcuts; onActivated: win.chooseTool("blur") }
    Shortcut { sequence: "E"; enabled: win.annotationShortcuts; onActivated: win.chooseTool("erase") }
    Shortcut { sequence: "G"; enabled: win.annotationShortcuts; onActivated: canvas.ink = "#22c55e" }
    Shortcut { sequence: "D"; enabled: win.annotationShortcuts; onActivated: canvas.ink = "#ef4444" }
    Shortcut { sequence: "V"; enabled: win.shortcutsOn; onActivated: win.captureVideo() }
    Shortcut { sequence: "M"; enabled: win.shortcutsOn; onActivated: win.captureMultiple() }
    Shortcut { sequences: ["Return", "Enter"]; enabled: win.shortcutsOn && canvas.arranging; onActivated: canvas.annotate() }
    Shortcut { sequences: ["+", "="]; enabled: win.shortcutsOn && canvas.arranging; onActivated: canvas.columns++ }
    Shortcut { sequence: "-"; enabled: win.shortcutsOn && canvas.arranging; onActivated: canvas.columns-- }
    Shortcut { sequences: [StandardKey.Undo]; enabled: win.annotationShortcuts; onActivated: canvas.undo() }
    Shortcut { sequences: [StandardKey.Redo]; enabled: win.annotationShortcuts; onActivated: canvas.redo() }
    Shortcut { sequences: ["Backspace", "Delete"]; enabled: win.shortcutsOn && canvas.arranging; onActivated: canvas.removeRegion(canvas.selectedRegion) }
    Shortcut { sequence: "Left"; enabled: win.shortcutsOn && canvas.arranging; onActivated: canvas.moveRegion(canvas.selectedRegion, canvas.selectedRegion - 1) }
    Shortcut { sequence: "Right"; enabled: win.shortcutsOn && canvas.arranging; onActivated: canvas.moveRegion(canvas.selectedRegion, canvas.selectedRegion + 1) }
    Shortcut { sequences: [StandardKey.Open]; enabled: win.shortcutsOn; onActivated: openDialog.open() }
    Shortcut { sequences: [StandardKey.New]; enabled: win.shortcutsOn; onActivated: win.capture() }
    Shortcut { sequences: [StandardKey.Paste]; enabled: win.shortcutsOn; onActivated: win.pasteImage() }
    Shortcut {
        sequence: "C"; enabled: win.shortcutsOn && canvas.hasImage
        onActivated: win.finish()
    }
    Shortcut {
        sequence: "S"; enabled: win.shortcutsOn && canvas.hasImage
        onActivated: win.saveAndClose()
    }
    Shortcut {
        sequence: "Escape"; enabled: win.shortcutsOn
        onActivated: if (!canvas.cancel()) win.dismissEditor()
    }

    component ActionButton: Button {
        id: actionControl
        property color textColor: checked ? "#91bff0" : "#dfe5ed"
        flat: true
        font.pixelSize: 13
        leftPadding: text === "" ? 8 : 12
        rightPadding: text === "" ? 8 : 12
        topPadding: 0
        bottomPadding: 0
        topInset: 0
        bottomInset: 0
        implicitHeight: 40
        implicitWidth: text === "" ? 40 : Math.max(40, contentItem.implicitWidth + leftPadding + rightPadding)
        icon.width: 26
        icon.height: 26
        icon.color: enabled ? textColor : "#8f99a8"
        focusPolicy: Qt.NoFocus
        palette.buttonText: enabled ? textColor : "#8f99a8"
        background: Rectangle {
            radius: 7
            color: parent.checked ? "#283b50" : parent.down ? "#394149"
                : parent.hovered ? "#2a2e35" : "transparent"
            border.width: parent.checked ? 1 : 0
            border.color: "#6994bf"
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
        height: editorHeader.implicitHeight + 24
        color: "#1b1e23"
        ColumnLayout {
            id: editorHeader
            anchors.fill: parent
            anchors.margins: 12
            anchors.leftMargin: 16; anchors.rightMargin: 16
            spacing: 12
            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                Label { text: "xshot"; font.pixelSize: 20; font.weight: Font.DemiBold }
                Item { Layout.fillWidth: true }
                ActionButton {
                    objectName: "backToArrangeButton"
                    text: "← Arrange"
                    visible: canvas.regionCount > 0 && !canvas.arranging
                    onClicked: win.arrangeRegions()
                }
                ActionButton {
                    objectName: "saveArrangementButton"
                    text: "Save and close"
                    visible: canvas.arranging
                    enabled: canvas.hasImage
                    onClicked: win.saveAndClose()
                    ToolTip.visible: hovered
                    ToolTip.text: "Save a PNG to Pictures/xshot and close (S)"
                }
                ActionButton {
                    objectName: "copyArrangementButton"
                    text: "Copy and close"
                    visible: canvas.arranging
                    enabled: canvas.hasImage
                    onClicked: win.finish()
                    ToolTip.visible: hovered
                    ToolTip.text: "Copy the combined image and close (C)"
                }
                PrimaryButton {
                    objectName: "continueToAnnotateButton"
                    text: "Annotate →"
                    visible: canvas.arranging
                    focusPolicy: Qt.NoFocus
                    onClicked: { canvas.annotate(); canvas.forceActiveFocus() }
                    ToolTip.visible: hovered
                    ToolTip.text: "Continue to annotate (Enter)"
                }
                ActionButton {
                    objectName: "saveButton"
                    text: "Save and close"
                    visible: !canvas.arranging
                    enabled: canvas.hasImage
                    onClicked: win.saveAndClose()
                    ToolTip.visible: hovered
                    ToolTip.text: "Save a PNG to Pictures/xshot and close (S)"
                }
                PrimaryButton {
                    id: doneButton
                    objectName: "copyButton"
                    text: "Copy and close"
                    visible: !canvas.arranging
                    enabled: canvas.hasImage
                    focusPolicy: Qt.NoFocus
                    onClicked: win.finish()
                    ToolTip.visible: hovered
                    ToolTip.text: "Copy image to clipboard and close (C)"
                }
            }
            ColumnLayout {
                visible: canvas.regionCount > 0
                Layout.fillWidth: true
                spacing: 6
                RowLayout {
                    spacing: 12
                    Label {
                        text: "Arrange"
                        color: canvas.arranging ? "#91bff0" : "#9ba5b5"
                        font.pixelSize: 18
                        font.weight: canvas.arranging ? Font.Bold : Font.Normal
                        Accessible.name: canvas.arranging ? "Arrange, current phase" : "Arrange, completed phase"
                    }
                    Label { text: "→"; color: "#9ba5b5" }
                    Label {
                        text: "Annotate (optional)"
                        color: !canvas.arranging ? "#91bff0" : "#9ba5b5"
                        font.pixelSize: 18
                        font.weight: !canvas.arranging ? Font.Bold : Font.Normal
                        Accessible.name: !canvas.arranging ? "Annotate, current phase, optional" : "Annotate, optional next phase"
                    }
                }
                Label {
                    text: canvas.arranging
                        ? "Put your captures in order and choose a layout."
                        : "Add marks or remove content from the combined image."
                    color: "#b8c1ce"
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }
            }
            Flow {
                objectName: "arrangementToolbar"
                visible: canvas.arranging
                Layout.fillWidth: true
                spacing: 4
                Label { text: canvas.regionCount + " regions"; height: 40; verticalAlignment: Text.AlignVCenter; rightPadding: 12; color: "#dfe5ed"; font.weight: Font.DemiBold }
                Label { text: "Columns"; height: 40; verticalAlignment: Text.AlignVCenter; color: "#9ba5b5" }
                ActionButton { objectName: "fewerColumnsButton"; text: "−"; Accessible.name: "Fewer columns"; ToolTip.visible: hovered; ToolTip.text: "Fewer columns (−)"; enabled: canvas.columns > 1; onClicked: canvas.columns-- }
                Label { text: canvas.columns; height: 40; verticalAlignment: Text.AlignVCenter; color: "#dfe5ed" }
                ActionButton { objectName: "moreColumnsButton"; text: "+"; Accessible.name: "More columns"; ToolTip.visible: hovered; ToolTip.text: "More columns (+)"; enabled: canvas.columns < Math.min(6, canvas.regionCount); onClicked: canvas.columns++ }
                ActionButton { text: ""; icon.source: "qrc:/icons/arrow-left.svg"; Accessible.name: "Move region left"; ToolTip.visible: hovered; ToolTip.text: "Move region left (←)"; enabled: canvas.selectedRegion > 0; onClicked: canvas.moveRegion(canvas.selectedRegion, canvas.selectedRegion - 1) }
                ActionButton { text: ""; icon.source: "qrc:/icons/arrow-right.svg"; Accessible.name: "Move region right"; ToolTip.visible: hovered; ToolTip.text: "Move region right (→)"; enabled: canvas.selectedRegion >= 0 && canvas.selectedRegion < canvas.regionCount - 1; onClicked: canvas.moveRegion(canvas.selectedRegion, canvas.selectedRegion + 1) }
                ActionButton { text: ""; icon.source: "qrc:/icons/trash.svg"; Accessible.name: "Remove region"; ToolTip.visible: hovered; ToolTip.text: "Remove region (Delete)"; enabled: canvas.selectedRegion >= 0; onClicked: canvas.removeRegion(canvas.selectedRegion) }
            }
            Flow {
                objectName: "annotationToolbar"
                visible: !canvas.arranging
                Layout.fillWidth: true
                spacing: 4
                Repeater {
                    model: [ {label: "Cut", key: "X", tool: "cut", glyph: "scissors"},
                             {label: "Rectangle", key: "R", tool: "rect", glyph: "square"},
                             {label: "Highlight", key: "H", tool: "highlight", glyph: "highlighter"},
                             {label: "Text", key: "T", tool: "text", glyph: "type"},
                             {label: "Arrow", key: "A", tool: "arrow", glyph: "move-up-right"},
                             {label: "Pixelate", key: "B", tool: "blur", glyph: "grid-3x3"},
                             {label: "Smart erase", key: "E", tool: "erase", glyph: "eraser"} ]
                    ActionButton {
                        required property var modelData
                        objectName: "tool_" + modelData.tool
                        text: modelData.tool === "cut" ? "Cut" : ""
                        icon.source: "qrc:/icons/" + modelData.glyph + ".svg"
                        Accessible.name: modelData.label
                        ToolTip.visible: hovered
                        ToolTip.text: modelData.tool === "blur" ? "Pixelate image colors (B) · Wheel adjusts blocks"
                            : modelData.tool === "cut" ? "Cut: remove an image strip (X)"
                            : modelData.label + " (" + modelData.key + ")"
                        checked: canvas.tool === modelData.tool
                        enabled: canvas.hasImage && !canvas.arranging
                        onClicked: win.chooseTool(modelData.tool)
                    }
                }
                Repeater {
                    model: [ {label: "Good", ink: "#22c55e", labelColor: "#57dc8b", key: "G", glyph: "check"},
                             {label: "Bad", ink: "#ef4444", labelColor: "#ff9999", key: "D", glyph: "x"} ]
                    ActionButton {
                        required property var modelData
                        objectName: "ink_" + modelData.key
                        text: modelData.label
                        icon.source: "qrc:/icons/" + modelData.glyph + ".svg"
                        Accessible.name: modelData.label + " mode"
                        ToolTip.visible: hovered
                        ToolTip.text: modelData.label + " mode (" + modelData.key + ")"
                        textColor: modelData.labelColor
                        checked: canvas.ink.toString() === modelData.ink
                        enabled: canvas.hasImage
                        onClicked: { win.commitText(); canvas.ink = modelData.ink }
                    }
                }
            }
        }
    }

    EditorCanvas {
        id: canvas
        objectName: "canvas"
        anchors.fill: parent
        focus: true
        onError: message => win.showError(message)
        onTextRequested: (x, y) => {
            win.textWidth = Math.min(360, canvas.imageWidth)
            win.textX = Math.min(x, canvas.imageWidth - win.textWidth)
            win.textY = Math.max(0, Math.min(y, canvas.imageHeight - Math.max(32, canvas.textSize * 1.5)))
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
            onWheel: wheel => {
                // TextArea handles its own scrolling, including at scroll limits.
                if (win.editingText && wheel.x >= textFrame.x && wheel.x < textFrame.x + textFrame.width
                        && wheel.y >= textFrame.y && wheel.y < textFrame.y + textFrame.height) {
                    wheel.accepted = true
                    return
                }
                if (canvas.arranging || (canvas.tool !== "rect" && canvas.tool !== "arrow" && canvas.tool !== "text" && canvas.tool !== "blur")) {
                    wheel.accepted = false
                    return
                }
                canvas.adjustToolSize(wheel.angleDelta.y || wheel.pixelDelta.y * 8)
                wheel.accepted = true
            }
        }
        ScrollView {
            id: textFrame
            objectName: "annotationTextFrame"
            visible: win.editingText
            x: canvas.imageRect.x + win.textX * canvas.imageScale
            y: canvas.imageRect.y + win.textY * canvas.imageScale
            width: win.textWidth * canvas.imageScale
            height: Math.min(Math.max(canvas.textSize * 1.5 * canvas.imageScale,
                                      textInput.contentHeight + textInput.topPadding + textInput.bottomPadding),
                             (canvas.imageHeight - win.textY) * canvas.imageScale)
            clip: true
            background: Rectangle { color: "#dd181b21"; border.color: "#9ba5b5"; radius: 2 }
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            TextArea {
                id: textInput
                objectName: "annotationText"
                width: textFrame.availableWidth
                padding: 0
                topPadding: 0
                bottomPadding: 0
                leftPadding: 0
                rightPadding: 0
                topInset: 0
                bottomInset: 0
                leftInset: 0
                rightInset: 0
                font.family: Qt.platform.os === "windows" ? "Segoe UI" : "Helvetica"
                font.pixelSize: Math.max(1, Math.round(canvas.textSize * canvas.imageScale))
                font.weight: Font.DemiBold
                color: canvas.ink
                selectionColor: "#32547b"
                wrapMode: TextEdit.WordWrap
                selectByMouse: true
                Accessible.name: "Annotation text"
                background: null
                Keys.onPressed: event => {
                    if (event.key === Qt.Key_Escape) {
                        win.editingText = false
                        textInput.text = ""
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
    }

    ColumnLayout {
        anchors.centerIn: parent
        visible: !canvas.hasImage
        spacing: 16
        Label { text: "No screenshot open"; font.pixelSize: 26; font.weight: Font.DemiBold; Layout.alignment: Qt.AlignHCenter }
        Label {
            text: Qt.platform.os === "osx"
                ? "Press Ctrl+Print Screen (F13) to select a screen region."
                : "Press Ctrl+Print Screen to select a screen region, or open or paste an image."
            color: "#9ba5b5"
            font.pixelSize: 14
            Layout.alignment: Qt.AlignHCenter
        }
        PrimaryButton { text: "Capture region (" + win.commandKey + "N)"; Layout.alignment: Qt.AlignHCenter; onClicked: win.capture(); ToolTip.visible: hovered; ToolTip.text: "Capture a screen region (" + win.commandKey + "N)" }
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            ActionButton { text: "Open image (" + win.commandKey + "O)"; onClicked: openDialog.open(); ToolTip.visible: hovered; ToolTip.text: "Open image (" + win.commandKey + "O)" }
            Label { text: "or"; color: "#9ba5b5" }
            ActionButton { text: "Paste image (" + win.commandKey + "V)"; onClicked: win.pasteImage(); ToolTip.visible: hovered; ToolTip.text: "Paste image (" + win.commandKey + "V)" }
        }
        ActionButton { text: "Multiple regions (M)"; Layout.alignment: Qt.AlignHCenter; onClicked: win.captureMultiple(); ToolTip.visible: hovered; ToolTip.text: "Capture multiple regions (M)" }
        ActionButton { text: "Record region (V)"; Layout.alignment: Qt.AlignHCenter; onClicked: win.captureVideo(); ToolTip.visible: hovered; ToolTip.text: "Record a region (V)" }
    }

    footer: Rectangle {
        height: footerActions.implicitHeight + 44
        color: "#1b1e23"
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 0
            Flow {
                id: footerActions
                Layout.fillWidth: true
                spacing: 4
                ActionButton { text: ""; icon.source: "qrc:/icons/square-dashed.svg"; Accessible.name: "Capture region"; ToolTip.visible: hovered; ToolTip.text: "Capture region (" + win.commandKey + "N)"; onClicked: win.capture() }
                ActionButton { text: ""; icon.source: "qrc:/icons/copy.svg"; Accessible.name: "Multiple regions"; ToolTip.visible: hovered; ToolTip.text: "Multiple regions (M)"; onClicked: win.captureMultiple() }
                ActionButton { text: ""; icon.source: "qrc:/icons/video.svg"; Accessible.name: "Record region"; ToolTip.visible: hovered; ToolTip.text: "Record region (V)"; onClicked: win.captureVideo() }
                ActionButton { text: ""; icon.source: "qrc:/icons/folder-open.svg"; Accessible.name: "Open image"; ToolTip.visible: hovered; ToolTip.text: "Open image (" + win.commandKey + "O)"; onClicked: openDialog.open() }
                ActionButton { objectName: "pasteImageButton"; text: ""; icon.source: "qrc:/icons/clipboard.svg"; Accessible.name: "Paste image"; ToolTip.visible: hovered; ToolTip.text: "Paste image (" + win.commandKey + "V)"; onClicked: win.pasteImage() }
                ActionButton { text: ""; icon.source: "qrc:/icons/undo-2.svg"; Accessible.name: "Undo"; ToolTip.visible: hovered; ToolTip.text: "Undo (" + win.commandKey + "Z)"; visible: !canvas.arranging; enabled: canvas.canUndo && !win.editingText; onClicked: canvas.undo() }
                ActionButton { text: ""; icon.source: "qrc:/icons/redo-2.svg"; Accessible.name: "Redo"; ToolTip.visible: hovered; ToolTip.text: "Redo (" + win.redoKey + ")"; visible: !canvas.arranging; enabled: canvas.canRedo && !win.editingText; onClicked: canvas.redo() }
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 14
                Label {
                    text: win.notice !== "" ? win.notice
                        : canvas.arranging ? "Drag to reorder · +/− changes columns · Enter to annotate · C to copy or S to save"
                        : canvas.hasImage ? win.hint : "Select a region · M for multiple regions · V for recording · Esc closes"
                    color: win.notice !== "" ? "#91bff0" : "#8f99a8"
                    font.pixelSize: 12
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                Label {
                    visible: canvas.hasImage
                    text: "Preview " + Math.round(canvas.imageScale * 100) + "% · Image "
                        + canvas.imageWidth + " × " + canvas.imageHeight + " px"
                    color: "#8f99a8"
                    font.pixelSize: 12
                    ToolTip.visible: hoverHandler.hovered
                    ToolTip.text: "Preview zoom only; unannotated exports keep the image's natural pixel size"
                    HoverHandler { id: hoverHandler }
                }
            }
        }
    }
}
