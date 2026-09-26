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
    Material.accent: "#a3e6ca"
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
    readonly property string hint: editingText ? "Type your note · " + commandKey + "Enter to place · Esc to cancel"
        : canvas.tool === "cut" ? "Drag sideways to remove a column · drag up or down to remove a row"
        : canvas.tool === "rect" ? "Drag to draw a rectangle"
        : canvas.tool === "arrow" ? "Drag from the tail to the arrow tip"
        : canvas.tool === "blur" ? "Drag to hide an area with an opaque pixelated blur"
        : canvas.tool === "erase" ? "Drag to fill an area with the color where you started"
        : "Click on the image to place text"

    function commitText() {
        if (!editingText) return
        canvas.addText(textX, textY, textWidth, canvas.imageHeight - textY, textInput.text)
        editingText = false
        textInput.text = ""
        canvas.forceActiveFocus()
    }
    function chooseTool(tool) { commitText(); canvas.tool = tool }
    function capture() { startCapture(false, false) }
    function captureMultiple() { startCapture(true, false) }
    function captureVideo() { startCapture(false, true) }
    function startCapture(multiple, video) {
        if (backend.recording) { recordingWindow.controlsHidden = false; recordingWindow.raise(); recordingWindow.requestActivate(); return }
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
            if (!backend.recording && (captured || win.restoreAfterCapture || win.notice !== "")) win.showEditor()
        }
        function onRecordingSaved(path) { win.hide(); canvas.clear() }
        function onRecordingCanceled() { if (win.restoreAfterCapture) win.showEditor() }
        function onRecordingChanged() { if (!backend.recording) recordingWindow.controlsHidden = false }
    }

    Window {
        id: recordingWindow
        objectName: "recordingWindow"
        property bool controlsHidden: false
        title: "xshot — Recording"
        transientParent: null
        width: 440; height: 130
        minimumWidth: 440; maximumWidth: 440
        minimumHeight: 130; maximumHeight: 130
        flags: Qt.Tool | Qt.WindowStaysOnTopHint
        color: "#1b1e23"
        visible: backend.recording && !controlsHidden
        onVisibleChanged: if (visible) {
            const area = backend.recordingRegion
            for (const candidate of Qt.application.screens) {
                if (area.x >= candidate.virtualX && area.x < candidate.virtualX + candidate.width
                        && area.y >= candidate.virtualY && area.y < candidate.virtualY + candidate.height) {
                    screen = candidate
                    break
                }
            }
            const desktop = Qt.rect(screen.virtualX, screen.virtualY, screen.width, screen.height)
            const bottom = desktop.y + desktop.height - 40
            const right = desktop.x + desktop.width - 8
            x = Math.max(desktop.x + 8, Math.min(area.x, right - width))
            y = Math.max(desktop.y + 32, Math.min(area.y, bottom - height))
            if (area.y + area.height + height + 8 <= bottom) y = area.y + area.height + 8
            else if (area.y - height - 8 >= desktop.y + 32) y = area.y - height - 8
            else if (area.x + area.width + width + 8 <= right) x = area.x + area.width + 8
            else if (area.x - width - 8 >= desktop.x + 8) x = area.x - width - 8
            raise()
            requestActivate()
            Qt.callLater(() => backend.protectRecordingControls(recordingWindow))
        }
        onClosing: close => { close.accepted = false; if (!backend.finishingRecording) backend.cancelRecording() }
        ColumnLayout {
            anchors.fill: parent; anchors.margins: 16; spacing: 12
            RowLayout {
                Layout.fillWidth: true
                Label {
                    text: backend.finishingRecording ? "Saving recording…"
                        : backend.startingRecording ? "Starting recording…"
                        : "Recording · " + Math.floor(backend.recordingElapsed / 60) + ":"
                            + ("0" + (backend.recordingElapsed % 60)).slice(-2)
                    color: "#f0f3f7"; font.pixelSize: 18; font.weight: Font.DemiBold
                }
                Item { Layout.fillWidth: true }
                ActionButton {
                    text: "Hide"
                    enabled: !backend.finishingRecording
                    onClicked: recordingWindow.controlsHidden = true
                    ToolTip.visible: hovered
                    ToolTip.text: "Ctrl+Print Screen brings recording controls back"
                }
            }
            RowLayout {
                Item { Layout.fillWidth: true }
                ActionButton { text: "Cancel (Esc)"; enabled: !backend.finishingRecording; onClicked: backend.cancelRecording() }
                PrimaryButton {
                    text: "Stop and copy path (" + win.commandKey + "C)"
                    enabled: !backend.startingRecording && !backend.finishingRecording
                    onClicked: backend.finishRecording()
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
        title: "Capture unavailable"
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
                    text: "Discard edits & arrange"
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
    Shortcut { sequences: [StandardKey.Paste]; enabled: win.shortcutsOn; onActivated: canvas.paste() }
    Shortcut {
        sequence: "C"; enabled: win.shortcutsOn && canvas.hasImage
        onActivated: win.finish()
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
                    text: "← Back to arrange"
                    visible: canvas.regionCount > 0 && !canvas.arranging
                    onClicked: win.arrangeRegions()
                }
                ActionButton {
                    objectName: "copyArrangementButton"
                    text: "Copy & finish (" + win.copyKey + ")"
                    visible: canvas.arranging
                    onClicked: win.finish()
                    ToolTip.visible: hovered
                    ToolTip.text: "Copy the combined image without annotations and finish"
                }
                PrimaryButton {
                    objectName: "continueToAnnotateButton"
                    text: "Continue to annotate → (Enter)"
                    visible: canvas.arranging
                    focusPolicy: Qt.NoFocus
                    onClicked: { canvas.annotate(); canvas.forceActiveFocus() }
                }
                PrimaryButton {
                    id: doneButton
                    objectName: "copyButton"
                    text: "Copy & finish (" + win.copyKey + ")"
                    visible: !canvas.arranging
                    enabled: canvas.hasImage
                    focusPolicy: Qt.NoFocus
                    onClicked: win.finish()
                    ToolTip.visible: hovered
                    ToolTip.text: "Copy image to clipboard and finish"
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
                        color: canvas.arranging ? "#a3e6ca" : "#9ba5b5"
                        font.pixelSize: 18
                        font.weight: canvas.arranging ? Font.Bold : Font.Normal
                        Accessible.name: canvas.arranging ? "Arrange, current phase" : "Arrange, completed phase"
                    }
                    Label { text: "→"; color: "#9ba5b5" }
                    Label {
                        text: "Annotate (optional)"
                        color: !canvas.arranging ? "#a3e6ca" : "#9ba5b5"
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
                ActionButton { objectName: "fewerColumnsButton"; text: "Fewer (−)"; Accessible.name: "Fewer columns (−)"; enabled: canvas.columns > 1; onClicked: canvas.columns-- }
                Label { text: canvas.columns; height: 40; verticalAlignment: Text.AlignVCenter; color: "#dfe5ed" }
                ActionButton { objectName: "moreColumnsButton"; text: "More (+)"; Accessible.name: "More columns (+)"; enabled: canvas.columns < Math.min(6, canvas.regionCount); onClicked: canvas.columns++ }
                ActionButton { text: "Move left (←)"; enabled: canvas.selectedRegion > 0; onClicked: canvas.moveRegion(canvas.selectedRegion, canvas.selectedRegion - 1) }
                ActionButton { text: "Move right (→)"; enabled: canvas.selectedRegion >= 0 && canvas.selectedRegion < canvas.regionCount - 1; onClicked: canvas.moveRegion(canvas.selectedRegion, canvas.selectedRegion + 1) }
                ActionButton { text: "Remove (Delete)"; enabled: canvas.selectedRegion >= 0; onClicked: canvas.removeRegion(canvas.selectedRegion) }
            }
            Flow {
                objectName: "annotationToolbar"
                visible: !canvas.arranging
                Layout.fillWidth: true
                spacing: 4
                Repeater {
                    model: [ {label: "Cut", key: "X", tool: "cut"},
                             {label: "Rectangle", key: "R", tool: "rect"},
                             {label: "Text", key: "T", tool: "text"},
                             {label: "Arrow", key: "A", tool: "arrow"},
                             {label: "Blur", key: "B", tool: "blur"},
                             {label: "Smart erase", key: "E", tool: "erase"} ]
                    ActionButton {
                        required property var modelData
                        objectName: "tool_" + modelData.tool
                        text: modelData.label + " (" + modelData.key + ")"
                        checked: canvas.tool === modelData.tool
                        enabled: canvas.hasImage && !canvas.arranging
                        onClicked: win.chooseTool(modelData.tool)
                    }
                }
                Repeater {
                    model: [ {label: "Good", ink: "#22c55e", labelColor: "#57dc8b", key: "G"},
                             {label: "Bad", ink: "#ef4444", labelColor: "#ff9999", key: "D"} ]
                    ActionButton {
                        required property var modelData
                        objectName: "ink_" + modelData.key
                        text: "●  " + modelData.label + " (" + modelData.key + ")"
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
                ? "Press Ctrl+Print Screen (F13) to select a screen region."
                : "Press Ctrl+Print Screen to select a screen region, or open or paste an image."
            color: "#9ba5b5"
            font.pixelSize: 14
            Layout.alignment: Qt.AlignHCenter
        }
        PrimaryButton { text: "Select a screen region (" + win.commandKey + "N)"; Layout.alignment: Qt.AlignHCenter; onClicked: win.capture() }
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            ActionButton { text: "Open image (" + win.commandKey + "O)"; onClicked: openDialog.open() }
            Label { text: "or"; color: "#9ba5b5" }
            ActionButton { text: "Paste image (" + win.commandKey + "V)"; onClicked: canvas.paste() }
        }
        ActionButton { text: "Select multiple regions (M)"; Layout.alignment: Qt.AlignHCenter; onClicked: win.captureMultiple() }
        ActionButton { text: "Record a region (V)"; Layout.alignment: Qt.AlignHCenter; onClicked: win.captureVideo() }
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
                ActionButton { text: "New (" + win.commandKey + "N)"; onClicked: win.capture() }
                ActionButton { text: "Multiple (M)"; onClicked: win.captureMultiple() }
                ActionButton { text: "Record (V)"; onClicked: win.captureVideo() }
                ActionButton { text: "Open (" + win.commandKey + "O)"; onClicked: openDialog.open() }
                ActionButton { text: "Undo (" + win.commandKey + "Z)"; visible: !canvas.arranging; enabled: canvas.canUndo && !win.editingText; onClicked: canvas.undo() }
                ActionButton { text: "Redo (" + win.redoKey + ")"; visible: !canvas.arranging; enabled: canvas.canRedo && !win.editingText; onClicked: canvas.redo() }
            }
            Label {
                text: win.notice !== "" ? win.notice
                    : canvas.arranging ? "Drag to reorder · +/− changes columns · Enter to annotate · " + win.copyKey + " to copy and finish"
                    : canvas.hasImage ? win.hint : "Select a region · M for multiple regions · V for recording · Esc cancels"
                color: win.notice !== "" ? "#a3e6ca" : "#8f99a8"
                font.pixelSize: 12
                elide: Text.ElideRight
                Layout.fillWidth: true
                Layout.leftMargin: 14
            }
        }
    }
}
