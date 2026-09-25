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
    visible: true
    title: "xshot"
    color: "#111317"
    Material.theme: Material.Dark
    Material.accent: "#a3e6ca"
    property bool editingText: false
    property real textX: 0
    property real textY: 0
    property real textWidth: 0
    property string notice: ""
    readonly property bool shortcutsOn: !editingText && !backend.capturing && !openDialog.visible
    readonly property string hint: editingText ? "Type your note · ⌘Enter to place · Esc to cancel"
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
    function capture() {
        commitText()
        notice = ""
        win.hide()
        backend.capture()
    }
    function finish() {
        commitText()
        canvas.cancel()
        if (canvas.copy()) win.close()
    }
    function showError(message) { notice = message; noticeTimer.restart() }

    Timer { id: noticeTimer; interval: 9000; onTriggered: win.notice = "" }
    Timer {
        interval: 200; running: true
        onTriggered: {
            if (initialImage.toString() !== "") canvas.load(initialImage)
            else win.capture()
        }
    }
    Connections {
        target: backend
        function onCaptured(file) { canvas.load(file) }
        function onError(message) { win.showError(message) }
        function onCaptureFinished() { win.show(); win.raise(); win.requestActivate() }
    }
    FileDialog {
        id: openDialog
        title: "Open a screenshot"
        nameFilters: ["Images (*.png *.jpg *.jpeg *.webp *.bmp *.tif *.tiff)"]
        onAccepted: { win.commitText(); canvas.load(selectedFile) }
    }

    Shortcut { sequence: "X"; enabled: win.shortcutsOn; onActivated: win.chooseTool("cut") }
    Shortcut { sequence: "R"; enabled: win.shortcutsOn; onActivated: win.chooseTool("rect") }
    Shortcut { sequence: "T"; enabled: win.shortcutsOn; onActivated: win.chooseTool("text") }
    Shortcut { sequence: "A"; enabled: win.shortcutsOn; onActivated: win.chooseTool("arrow") }
    Shortcut { sequence: "G"; enabled: win.shortcutsOn; onActivated: canvas.ink = "#22c55e" }
    Shortcut { sequence: "B"; enabled: win.shortcutsOn; onActivated: canvas.ink = "#ef4444" }
    Shortcut { sequence: "Return"; enabled: win.shortcutsOn && canvas.hasImage; onActivated: win.finish() }
    Shortcut { sequence: "Enter"; enabled: win.shortcutsOn && canvas.hasImage; onActivated: win.finish() }
    Shortcut { sequences: [StandardKey.Undo]; enabled: win.shortcutsOn; onActivated: canvas.undo() }
    Shortcut { sequences: [StandardKey.Redo]; enabled: win.shortcutsOn; onActivated: canvas.redo() }
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
            color: actionControl.enabled ? actionControl.textColor : "#626a77"
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: 7
            color: parent.checked ? "#303b3a" : parent.hovered ? "#2a2e35" : "transparent"
            border.width: parent.checked ? 1 : 0
            border.color: "#51645f"
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
                    enabled: canvas.hasImage
                    onClicked: win.chooseTool(modelData.tool)
                }
            }
            Rectangle { width: 1; height: 24; color: "#373c44"; Layout.leftMargin: 12; Layout.rightMargin: 12 }
            Repeater {
                model: [ {label: "Good", ink: "#22c55e", key: "G"}, {label: "Bad", ink: "#ef4444", key: "B"} ]
                ActionButton {
                    required property var modelData
                    text: "●  " + modelData.label
                    textColor: modelData.ink
                    checked: canvas.ink.toString() === modelData.ink
                    onClicked: { win.commitText(); canvas.ink = modelData.ink }
                    ToolTip.visible: hovered
                    ToolTip.text: modelData.key
                }
            }
            Item { Layout.fillWidth: true }
            Button {
                id: doneButton
                text: "Done   ↵"
                highlighted: true
                Material.foreground: "#142820"
                enabled: canvas.hasImage
                focusPolicy: Qt.NoFocus
                font.weight: Font.DemiBold
                contentItem: Text {
                    text: doneButton.text
                    font: doneButton.font
                    color: doneButton.enabled ? "#142820" : "#626a77"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                onClicked: win.finish()
                ToolTip.visible: hovered
                ToolTip.text: "Copy image to clipboard and close"
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
            cursorShape: canvas.tool === "text" ? Qt.IBeamCursor : Qt.CrossCursor
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
            font.family: "Helvetica"
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
        Label { text: "Capture. Mark. Copy."; font.pixelSize: 32; font.weight: Font.DemiBold; Layout.alignment: Qt.AlignHCenter }
        Label { text: "Just enough tools to make your point."; color: "#9ba5b5"; font.pixelSize: 16; Layout.alignment: Qt.AlignHCenter }
        Button { text: "Select a screen region"; highlighted: true; Layout.alignment: Qt.AlignHCenter; onClicked: win.capture() }
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            ActionButton { text: "Open image"; onClicked: openDialog.open() }
            Label { text: "or"; color: "#6b7380" }
            ActionButton { text: "Paste image"; onClicked: canvas.paste() }
        }
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
                ActionButton { text: "Open"; onClicked: openDialog.open() }
                ActionButton { text: "Undo"; enabled: canvas.canUndo && !win.editingText; onClicked: canvas.undo() }
                ActionButton { text: "Redo"; enabled: canvas.canRedo && !win.editingText; onClicked: canvas.redo() }
                Item { Layout.fillWidth: true }
                Label {
                    text: canvas.hasImage ? canvas.imageWidth + " × " + canvas.imageHeight + " px" : "⌘V to paste an image"
                    color: "#8f99a8"; font.pixelSize: 12; Layout.rightMargin: 8
                }
            }
            Label {
                text: win.notice !== "" ? win.notice : canvas.hasImage ? win.hint : "Select a region to start · Esc cancels the selection"
                color: win.notice !== "" ? "#a3e6ca" : "#8f99a8"
                font.pixelSize: 12
                elide: Text.ElideRight
                Layout.fillWidth: true
                Layout.leftMargin: 14
            }
        }
    }
}
