import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Dialogs
import QtMultimedia
import "Format.js" as Format
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
    readonly property var keys: appSettings.shortcutHints
    readonly property var shortcuts: appSettings.shortcuts
    readonly property bool shortcutsOn: !editingText && !backend.capturing && !backend.recording && !reviewWindow.visible && !openDialog.visible
        && !rearrangeDialog.visible && !captureErrorDialog.visible
    readonly property bool annotationShortcuts: shortcutsOn && !canvas.arranging
    readonly property bool imageInputShortcuts: shortcutsOn && (!canvas.hasImage || canvas.arranging)
    readonly property string hint: editingText ? canvas.textSize + " px · " + commandKey + "Enter to place"
        : canvas.tool === "cut" ? "↔ removes columns · ↕ removes rows"
        : canvas.tool === "rect" || canvas.tool === "arrow"
            ? "Line width " + canvas.strokeWidth + " px · Scroll to adjust"
        : canvas.tool === "highlight" ? "Overlapping highlights build up color"
        : canvas.tool === "blur" ? "Block size " + canvas.pixelBlockSize + " px · Scroll to adjust"
        : canvas.tool === "erase" ? "Fills with the color where you start"
        : "Text size " + canvas.textSize + " px · Scroll to adjust"

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
        if (backend.trim.path !== "") return
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
    Connections {
        target: appSettings
        function onReloadFailed(message) { win.showError("Settings not applied: " + message) }
    }

    Window {
        id: recordingOutline
        objectName: "recordingOutline"
        title: "xshot — Recording region"
        transientParent: null
        readonly property int outlineWidth: 3
        flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
            | Qt.WindowTransparentForInput | Qt.WindowDoesNotAcceptFocus
        color: "transparent"
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
            x = area.x - outlineWidth
            y = area.y - outlineWidth
            width = area.width + 2 * outlineWidth
            height = area.height + 2 * outlineWidth
            raise()
        }
        Rectangle {
            anchors.fill: parent
            color: "transparent"
            border.color: "#ef4444"
            border.width: recordingOutline.outlineWidth
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
                Qt.callLater(() => backend.beginProtectedRecording(startupIndicator, recordingWindow, recordingOutline))
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
                }
                PrimaryButton {
                    objectName: "recordingStopButton"
                    text: "Stop"
                    Accessible.name: "Stop and save recording"
                    enabled: !backend.startingRecording && !backend.finishingRecording
                    onClicked: backend.finishRecording()
                }
            }
        }
    }

    Window {
        id: reviewWindow
        objectName: "recordingReviewWindow"
        title: "xshot — Review recording"
        transientParent: null
        width: 960; height: 690
        minimumWidth: 680; minimumHeight: 520
        color: "#111317"
        visible: backend.trim.path !== ""
        Material.theme: Material.Dark
        Material.accent: "#91bff0"
        property url playbackSource: ""
        property bool preparingExport: false
        property bool dismissAfterCancel: false
        property bool resumeAfterScrub: false
        readonly property bool canEdit: backend.trim.duration > 0 && !backend.trim.busy && !preparingExport
        readonly property bool hasTrim: trimBar.startSec > 0.001
            || trimBar.endSec < backend.trim.duration / 1000 - 0.001

        function togglePlay() {
            if (!canEdit) return
            if (reviewPlayer.priming) reviewPlayer.finishPriming()
            if (reviewPlayer.playbackState === MediaPlayer.PlayingState) {
                reviewPlayer.pauseAtPlayhead()
                return
            }
            if (trimBar.playheadSec < trimBar.startSec || trimBar.playheadSec >= trimBar.endSec - 0.01)
                trimBar.playheadSec = trimBar.startSec
            reviewPlayer.position = Math.round(trimBar.playheadSec * 1000)
            reviewPlayer.play()
        }
        function seek(seconds) {
            if (!canEdit) return
            if (reviewPlayer.priming) reviewPlayer.finishPriming()
            trimBar.playheadSec = Math.max(trimBar.startSec, Math.min(trimBar.endSec,
                trimBar.playheadSec + seconds))
            reviewPlayer.position = Math.round(trimBar.playheadSec * 1000)
        }
        function prepareExport() {
            if (!canEdit || !hasTrim) return
            preparingExport = true
            reviewPlayer.stop()
            playbackSource = ""
            exportReleaseTimer.restart()
        }
        function cancelExport() {
            const wasPreparing = preparingExport
            exportReleaseTimer.stop()
            preparingExport = false
            if (backend.trim.busy) backend.trim.cancelExport()
            else if (wasPreparing && backend.trim.path !== "")
                playbackSource = backend.trim.source
        }
        onVisibleChanged: if (visible) {
            playbackSource = backend.trim.source
            trimBar.startSec = 0
            trimBar.endSec = backend.trim.duration / 1000
            trimBar.playheadSec = 0
            raise(); requestActivate()
        } else {
            reviewPlayer.stop()
            playbackSource = ""
            preparingExport = false
            dismissAfterCancel = false
            resumeAfterScrub = false
        }
        onClosing: close => {
            close.accepted = false
            if (backend.trim.busy) {
                dismissAfterCancel = true
                backend.trim.cancelExport()
            } else if (preparingExport) {
                exportReleaseTimer.stop()
                preparingExport = false
                backend.trim.keepOriginal()
            } else backend.trim.keepOriginal()
        }
        Connections {
            target: backend.trim
            function onChanged() {
                if (reviewWindow.dismissAfterCancel && !backend.trim.busy) {
                    reviewWindow.dismissAfterCancel = false
                    backend.trim.keepOriginal()
                    return
                }
                if (!backend.trim.busy && !reviewWindow.preparingExport
                        && backend.trim.path !== "" && reviewWindow.playbackSource.toString() === "")
                    reviewWindow.playbackSource = backend.trim.source
            }
        }
        Timer {
            id: exportReleaseTimer
            interval: 180
            onTriggered: {
                reviewWindow.preparingExport = false
                backend.trim.exportRange(Math.round(trimBar.startSec * 1000),
                                         Math.round(trimBar.endSec * 1000))
            }
        }
        MediaPlayer {
            id: reviewPlayer
            objectName: "recordingReviewPlayer"
            source: reviewWindow.playbackSource
            videoOutput: reviewVideo
            audioOutput: AudioOutput { muted: reviewPlayer.priming }
            property bool primed: false
            property bool priming: false
            property bool finishingPrime: false

            function syncPlayhead() {
                if (priming || trimBar.interacting) return
                trimBar.playheadSec = position / 1000
            }
            function pauseAtPlayhead() {
                // The backend reports the last rendered frame, which can be
                // seconds behind the clock in a static screen recording.
                const seconds = trimBar.playheadSec
                pause()
                position = Math.round(seconds * 1000)
                trimBar.playheadSec = seconds
            }
            function advancePlayhead(elapsedSeconds) {
                trimBar.playheadSec = Math.min(trimBar.endSec,
                    trimBar.playheadSec + elapsedSeconds * playbackRate)
                if (trimBar.playheadSec >= trimBar.endSec && trimBar.endSec > 0)
                    pauseAtPlayhead()
            }
            function startPriming() {
                if (primed || priming || !reviewWindow.visible || reviewWindow.preparingExport
                        || source.toString() === "") return
                primed = true
                priming = true
                position = Math.round(trimBar.startSec * 1000)
                play()
                primeFallback.restart()
            }
            function finishPriming() {
                if (!priming || finishingPrime) return
                finishingPrime = true
                primeFallback.stop()
                pause()
                position = Math.round(trimBar.startSec * 1000)
                trimBar.playheadSec = trimBar.startSec
                priming = false
                finishingPrime = false
            }
            onSourceChanged: {
                primeFallback.stop()
                priming = false
                primed = false
                finishingPrime = false
            }
            onMediaStatusChanged: {
                if (mediaStatus === MediaPlayer.LoadedMedia || mediaStatus === MediaPlayer.BufferedMedia)
                    startPriming()
            }
            onDurationChanged: duration => {
                if (duration > 0) {
                    backend.trim.setDuration(duration)
                    if (trimBar.endSec <= 0) trimBar.endSec = backend.trim.duration / 1000
                }
            }
            onPositionChanged: if (playbackState !== MediaPlayer.PlayingState) syncPlayhead()
            onPlaybackStateChanged: if (playbackState !== MediaPlayer.PlayingState) syncPlayhead()
            onErrorOccurred: (error, errorString) => {
                if (reviewWindow.visible && !backend.trim.busy)
                    playbackError.text = "Playback unavailable: " + errorString
            }
        }
        PlaybackClock {
            objectName: "recordingPlaybackClock"
            // Advance by elapsed time, not by the timestamps of decoded frames.
            // Screen recordings may legitimately hold one frame for seconds.
            running: reviewWindow.visible && !reviewPlayer.priming && !trimBar.interacting
                && reviewPlayer.playbackState === MediaPlayer.PlayingState
                && reviewPlayer.mediaStatus !== MediaPlayer.LoadingMedia
                && reviewPlayer.mediaStatus !== MediaPlayer.StalledMedia
            onAdvanced: seconds => reviewPlayer.advancePlayhead(seconds)
        }
        Timer {
            id: primeFallback
            interval: 500
            onTriggered: reviewPlayer.finishPriming()
        }
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 24
            spacing: 14
            Label {
                Layout.fillWidth: true
                text: "Review recording"
                color: "#f0f3f7"
                font.pixelSize: 24
                font.weight: Font.DemiBold
            }
            Label {
                Layout.fillWidth: true
                text: "Drag the white handle to seek, or the blue handles to trim."
                color: "#aab3c0"; wrapMode: Text.WordWrap
            }
            Rectangle {
                Layout.fillWidth: true; Layout.fillHeight: true
                color: "#07090b"; radius: 10
                VideoOutput {
                    id: reviewVideo
                    anchors.fill: parent
                    anchors.margins: 4
                    fillMode: VideoOutput.PreserveAspectFit
                }
                Connections {
                    target: reviewVideo.videoSink
                    function onVideoFrameChanged(frame) {
                        reviewPlayer.finishPriming()
                    }
                }
                Label {
                    id: playbackError
                    anchors.centerIn: parent
                    visible: text !== ""
                    color: "#f0b5b5"
                    text: ""
                }
            }
            Item {
                objectName: "recordingPlaybackControls"
                Layout.fillWidth: true
                Layout.preferredHeight: playbackTime.implicitHeight + reviewPlayButton.height + 4
                Item {
                    id: playbackTime
                    objectName: "recordingPlaybackTime"
                    anchors.top: parent.top
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.alignWhenCentered: false
                    implicitWidth: timeMetrics.advanceWidth * 2 + timeSeparator.implicitWidth + 16
                    implicitHeight: elapsedTime.implicitHeight
                    width: implicitWidth; height: implicitHeight
                    TextMetrics {
                        id: timeMetrics
                        font: elapsedTime.font
                        text: Format.fmt(backend.trim.duration / 1000)
                    }
                    Row {
                        spacing: 8
                        Label {
                            id: elapsedTime
                            objectName: "recordingElapsedTime"
                            width: timeMetrics.advanceWidth
                            horizontalAlignment: Text.AlignRight
                            font.features: { "tnum": 1 }
                            text: Format.fmt(trimBar.playheadSec)
                            color: "#dfe5ed"
                        }
                        Label {
                            id: timeSeparator
                            objectName: "recordingTimeSeparator"
                            text: "/"
                            color: "#dfe5ed"
                        }
                        Label {
                            objectName: "recordingTotalTime"
                            width: timeMetrics.advanceWidth
                            font: elapsedTime.font
                            text: timeMetrics.text
                            color: "#dfe5ed"
                        }
                    }
                }
                ToolButton {
                    id: reviewPlayButton
                    objectName: "recordingReviewPlayButton"
                    anchors.top: playbackTime.bottom
                    anchors.topMargin: 4
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 40; height: 40
                    display: AbstractButton.IconOnly
                    text: reviewPlayer.mediaStatus === MediaPlayer.LoadingMedia || reviewPlayer.priming ? "Loading…"
                        : reviewPlayer.playbackState === MediaPlayer.PlayingState ? "Pause" : "Play"
                    icon.source: reviewPlayer.playbackState === MediaPlayer.PlayingState && !reviewPlayer.priming
                        ? "qrc:/icons/pause.svg" : "qrc:/icons/play.svg"
                    icon.width: 24; icon.height: 24
                    Accessible.name: text
                    ToolTip.visible: hovered
                    ToolTip.text: text + (win.keys.reviewPlayPause ? " (" + win.keys.reviewPlayPause + ")" : "")
                    enabled: reviewWindow.canEdit && reviewPlayer.duration > 0 && !reviewPlayer.priming
                    onClicked: reviewWindow.togglePlay()
                }
            }
            TrimBar {
                id: trimBar
                objectName: "recordingTrimBar"
                Layout.fillWidth: true
                Layout.preferredHeight: 108
                enabled: reviewWindow.canEdit
                durationSec: backend.trim.duration / 1000
                thumbCount: 12
                thumbnails: backend.trim.thumbnails
                onScrub: seconds => {
                    if (reviewPlayer.priming) reviewPlayer.finishPriming()
                    reviewPlayer.position = Math.round(seconds * 1000)
                }
                onInteractingChanged: {
                    if (interacting) {
                        reviewWindow.resumeAfterScrub = !reviewPlayer.priming
                            && reviewPlayer.playbackState === MediaPlayer.PlayingState
                        if (reviewPlayer.priming) reviewPlayer.finishPriming()
                        reviewPlayer.pauseAtPlayhead()
                    } else if (reviewWindow.resumeAfterScrub) {
                        reviewWindow.resumeAfterScrub = false
                        if (reviewWindow.canEdit && playheadSec < endSec) reviewPlayer.play()
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Label { text: "From " + Format.fmt(trimBar.startSec); color: "#91bff0" }
                Item { Layout.fillWidth: true }
                Label { text: "To " + Format.fmt(trimBar.endSec); color: "#91bff0" }
            }
            Label {
                Layout.fillWidth: true
                text: backend.trim.problem !== "" ? backend.trim.problem
                    : backend.trim.busy ? "Exporting trimmed recording…"
                        + (backend.trim.progress < 0 ? "" : " " + Math.round(backend.trim.progress * 100) + "%")
                    : ""
                visible: text !== ""
                color: backend.trim.problem !== "" ? "#f0b5b5" : "#aab3c0"
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                ActionButton {
                    objectName: "recordingKeepOriginalButton"
                    text: "Keep original"
                    visible: reviewWindow.hasTrim
                    enabled: !backend.trim.busy && !reviewWindow.preparingExport
                    onClicked: backend.trim.keepOriginal()
                }
                Item { Layout.fillWidth: true }
                ActionButton {
                    objectName: "recordingCancelExportButton"
                    text: "Cancel export"
                    visible: backend.trim.busy || reviewWindow.preparingExport
                    onClicked: reviewWindow.cancelExport()
                }
                PrimaryButton {
                    objectName: "recordingSaveTrimButton"
                    text: reviewWindow.hasTrim ? "Save trim" : "Keep original"
                    enabled: reviewWindow.hasTrim ? reviewWindow.canEdit
                        : !backend.trim.busy && !reviewWindow.preparingExport
                    onClicked: {
                        if (reviewWindow.hasTrim) reviewWindow.prepareExport()
                        else backend.trim.keepOriginal()
                    }
                }
            }
        }
        Shortcut { sequences: win.shortcuts.reviewPlayPause; enabled: reviewWindow.visible && reviewWindow.canEdit; onActivated: reviewWindow.togglePlay() }
        Shortcut { sequences: win.shortcuts.reviewSeekBackward; enabled: reviewWindow.visible && reviewWindow.canEdit; onActivated: reviewWindow.seek(-1) }
        Shortcut { sequences: win.shortcuts.reviewSeekForward; enabled: reviewWindow.visible && reviewWindow.canEdit; onActivated: reviewWindow.seek(1) }
        Shortcut { sequences: win.shortcuts.reviewSeekBackwardFar; enabled: reviewWindow.visible && reviewWindow.canEdit; onActivated: reviewWindow.seek(-5) }
        Shortcut { sequences: win.shortcuts.reviewSeekForwardFar; enabled: reviewWindow.visible && reviewWindow.canEdit; onActivated: reviewWindow.seek(5) }
        Shortcut { sequences: win.shortcuts.reviewMarkStart; enabled: reviewWindow.visible && reviewWindow.canEdit; onActivated: {
            trimBar.startSec = Math.min(trimBar.playheadSec, trimBar.endSec - 0.1) } }
        Shortcut { sequences: win.shortcuts.reviewMarkEnd; enabled: reviewWindow.visible && reviewWindow.canEdit; onActivated: {
            trimBar.endSec = Math.max(trimBar.playheadSec, trimBar.startSec + 0.1) } }
        Shortcut { objectName: "recordingReviewEscapeShortcut"; sequences: win.shortcuts.reviewCancel; enabled: reviewWindow.visible; onActivated: {
            if (backend.trim.busy || reviewWindow.preparingExport) {
                reviewWindow.cancelExport()
            } else backend.trim.keepOriginal()
        } }
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

    Shortcut { sequences: win.shortcuts.toolCut; enabled: win.annotationShortcuts; onActivated: win.chooseTool("cut") }
    Shortcut { sequences: win.shortcuts.toolRectangle; enabled: win.annotationShortcuts; onActivated: win.chooseTool("rect") }
    Shortcut { sequences: win.shortcuts.toolHighlight; enabled: win.annotationShortcuts; onActivated: win.chooseTool("highlight") }
    Shortcut { sequences: win.shortcuts.toolText; enabled: win.annotationShortcuts; onActivated: win.chooseTool("text") }
    Shortcut { sequences: win.shortcuts.toolArrow; enabled: win.annotationShortcuts; onActivated: win.chooseTool("arrow") }
    Shortcut { sequences: win.shortcuts.toolPixelate; enabled: win.annotationShortcuts; onActivated: win.chooseTool("blur") }
    Shortcut { sequences: win.shortcuts.toolErase; enabled: win.annotationShortcuts; onActivated: win.chooseTool("erase") }
    Shortcut { sequences: win.shortcuts.goodMode; enabled: win.annotationShortcuts; onActivated: canvas.setInkMode("good") }
    Shortcut { sequences: win.shortcuts.badMode; enabled: win.annotationShortcuts; onActivated: canvas.setInkMode("bad") }
    Shortcut { objectName: "captureVideoShortcut"; sequences: win.shortcuts.captureVideo; enabled: win.imageInputShortcuts; onActivated: win.captureVideo() }
    Shortcut { objectName: "captureMultipleShortcut"; sequences: win.shortcuts.captureMultiple; enabled: win.imageInputShortcuts; onActivated: win.captureMultiple() }
    Shortcut { sequences: win.shortcuts.arrange; enabled: win.shortcutsOn && canvas.arranging; onActivated: canvas.annotate() }
    Shortcut { sequences: win.shortcuts.columnsMore; enabled: win.shortcutsOn && canvas.arranging; onActivated: canvas.columns++ }
    Shortcut { sequences: win.shortcuts.columnsLess; enabled: win.shortcutsOn && canvas.arranging; onActivated: canvas.columns-- }
    Shortcut { sequences: win.shortcuts.undo; enabled: win.annotationShortcuts; onActivated: canvas.undo() }
    Shortcut { sequences: win.shortcuts.redo; enabled: win.annotationShortcuts; onActivated: canvas.redo() }
    Shortcut { sequences: win.shortcuts.removeRegion; enabled: win.shortcutsOn && canvas.arranging; onActivated: canvas.removeRegion(canvas.selectedRegion) }
    Shortcut { sequences: win.shortcuts.moveRegionLeft; enabled: win.shortcutsOn && canvas.arranging; onActivated: canvas.moveRegion(canvas.selectedRegion, canvas.selectedRegion - 1) }
    Shortcut { sequences: win.shortcuts.moveRegionRight; enabled: win.shortcutsOn && canvas.arranging; onActivated: canvas.moveRegion(canvas.selectedRegion, canvas.selectedRegion + 1) }
    Shortcut { objectName: "openImageShortcut"; sequences: win.shortcuts.openImage; enabled: win.imageInputShortcuts; onActivated: openDialog.open() }
    Shortcut { objectName: "captureShortcut"; sequences: win.shortcuts.capture; enabled: win.imageInputShortcuts; onActivated: win.capture() }
    Shortcut { objectName: "pasteImageShortcut"; sequences: win.shortcuts.pasteImage; enabled: win.imageInputShortcuts; onActivated: win.pasteImage() }
    Shortcut {
        sequences: win.shortcuts.copyClose; enabled: win.shortcutsOn && canvas.hasImage
        onActivated: win.finish()
    }
    Shortcut {
        sequences: win.shortcuts.saveClose; enabled: win.shortcutsOn && canvas.hasImage
        onActivated: win.saveAndClose()
    }
    Shortcut {
        sequences: win.shortcuts.dismiss; enabled: win.shortcutsOn
        onActivated: if (!canvas.cancel()) win.dismissEditor()
    }

    component ActionButton: Button {
        id: actionControl
        property color textColor: highlighted ? "#162a40" : checked ? "#91bff0" : "#dfe5ed"
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
            color: actionControl.highlighted ? (!actionControl.enabled ? "#30343b"
                : actionControl.down ? "#73a8df" : actionControl.hovered ? "#b4d4f5" : "#91bff0")
                : actionControl.checked ? "#283b50" : actionControl.down ? "#394149"
                : actionControl.hovered ? "#2a2e35" : "transparent"
            border.width: actionControl.checked ? 1 : 0
            border.color: "#6994bf"
        }
    }

    component ShortcutLabel: Item {
        id: shortcutLabel
        property alias text: label.text
        property alias font: label.font
        property alias color: label.color
        property bool underlineShortcut: false
        implicitWidth: label.implicitWidth
        implicitHeight: label.implicitHeight
        Text {
            id: label
            anchors.centerIn: parent
            textFormat: Text.PlainText
            TextMetrics {
                id: shortcutLetter
                font: label.font
                text: label.text.charAt(0)
            }
            Rectangle {
                visible: shortcutLabel.underlineShortcut
                y: Math.ceil(label.baselineOffset + 2)
                width: Math.ceil(shortcutLetter.advanceWidth)
                height: 1
                color: label.color
            }
        }
    }

    component ShortcutButton: ActionButton {
        id: shortcutControl
        property bool underlineShortcut: false
        contentItem: ShortcutLabel {
            text: shortcutControl.text
            font: shortcutControl.font
            color: shortcutControl.enabled ? shortcutControl.textColor : "#8f99a8"
            underlineShortcut: shortcutControl.underlineShortcut
        }
    }

    component PrimaryButton: Button {
        id: primaryControl
        property bool underlineShortcut: false
        highlighted: true
        font.weight: Font.DemiBold
        leftPadding: 16
        rightPadding: 16
        topPadding: 10
        bottomPadding: 10
        topInset: 0
        bottomInset: 0
        // Explicit colors avoid Material's light highlighted label on our mint accent.
        contentItem: ShortcutLabel {
            text: primaryControl.text
            font: primaryControl.font
            color: primaryControl.enabled ? "#142820" : "#9ba5b5"
            underlineShortcut: primaryControl.underlineShortcut
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
                visible: canvas.arranging
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
                ShortcutButton {
                    objectName: "saveArrangementButton"
                    text: "Save and close"
                    underlineShortcut: win.shortcuts.saveClose.indexOf("S") >= 0
                    visible: canvas.arranging
                    enabled: canvas.hasImage
                    onClicked: win.saveAndClose()
                    ToolTip.visible: hovered
                    ToolTip.text: "Save under " + appSettings.picturesRoot + "/YEAR/MONTH and close (" + win.keys.saveClose + ")"
                }
                ShortcutButton {
                    objectName: "copyArrangementButton"
                    text: "Copy and close"
                    underlineShortcut: win.shortcuts.copyClose.indexOf("C") >= 0
                    visible: canvas.arranging
                    enabled: canvas.hasImage
                    onClicked: win.finish()
                    ToolTip.visible: hovered
                    ToolTip.text: "Copy the combined image and close (" + win.keys.copyClose + ")"
                }
                PrimaryButton {
                    objectName: "continueToAnnotateButton"
                    text: "Annotate →"
                    visible: canvas.arranging
                    focusPolicy: Qt.NoFocus
                    onClicked: { canvas.annotate(); canvas.forceActiveFocus() }
                    ToolTip.visible: hovered
                    ToolTip.text: "Continue to annotate (" + win.keys.arrange + ")"
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
                ActionButton { objectName: "fewerColumnsButton"; text: "−"; Accessible.name: "Fewer columns"; ToolTip.visible: hovered; ToolTip.text: "Fewer columns (" + win.keys.columnsLess + ")"; enabled: canvas.columns > 1; onClicked: canvas.columns-- }
                Label { text: canvas.columns; height: 40; verticalAlignment: Text.AlignVCenter; color: "#dfe5ed" }
                ActionButton { objectName: "moreColumnsButton"; text: "+"; Accessible.name: "More columns"; ToolTip.visible: hovered; ToolTip.text: "More columns (" + win.keys.columnsMore + ")"; enabled: canvas.columns < Math.min(6, canvas.regionCount); onClicked: canvas.columns++ }
                ActionButton { text: ""; icon.source: "qrc:/icons/arrow-left.svg"; Accessible.name: "Move region left"; ToolTip.visible: hovered; ToolTip.text: "Move region left (" + win.keys.moveRegionLeft + ")"; enabled: canvas.selectedRegion > 0; onClicked: canvas.moveRegion(canvas.selectedRegion, canvas.selectedRegion - 1) }
                ActionButton { text: ""; icon.source: "qrc:/icons/arrow-right.svg"; Accessible.name: "Move region right"; ToolTip.visible: hovered; ToolTip.text: "Move region right (" + win.keys.moveRegionRight + ")"; enabled: canvas.selectedRegion >= 0 && canvas.selectedRegion < canvas.regionCount - 1; onClicked: canvas.moveRegion(canvas.selectedRegion, canvas.selectedRegion + 1) }
                ActionButton { text: ""; icon.source: "qrc:/icons/trash.svg"; Accessible.name: "Remove region"; ToolTip.visible: hovered; ToolTip.text: "Remove region (" + win.keys.removeRegion + ")"; enabled: canvas.selectedRegion >= 0; onClicked: canvas.removeRegion(canvas.selectedRegion) }
            }
            Item {
                id: annotationToolbar
                objectName: "annotationToolbar"
                visible: !canvas.arranging
                Layout.fillWidth: true
                Layout.preferredHeight: implicitHeight
                // Keep whole groups together; only use the second row when their
                // measured widths and breathing room no longer fit.
                property bool singleLine: width >= annotationToolCluster.implicitWidth
                    + modeGroup.implicitWidth + finishGroup.implicitWidth + 180 + 48
                implicitHeight: singleLine
                    ? Math.max(annotationToolCluster.implicitHeight, modeGroup.implicitHeight,
                               annotationHint.height, finishGroup.implicitHeight) + 8
                    : annotationToolCluster.implicitHeight + 8
                        + Math.max(modeGroup.implicitHeight, annotationHint.height,
                                   finishGroup.implicitHeight) + 4

                RowLayout {
                    id: annotationToolCluster
                    objectName: "annotationToolCluster"
                    x: 0
                    y: 0
                    height: implicitHeight
                    spacing: 6
                    RowLayout {
                        id: annotationToolButtons
                        objectName: "annotationToolButtons"
                        spacing: 2
                        Repeater {
                            model: [ {label: "Cut", key: win.keys.toolCut, tool: "cut", glyph: "cut"},
                                     {label: "Rectangle", key: win.keys.toolRectangle, tool: "rect", glyph: "rectangle"},
                                     {label: "Highlight", key: win.keys.toolHighlight, tool: "highlight", glyph: "highlight"},
                                     {label: "Text", key: win.keys.toolText, tool: "text", glyph: "text"},
                                     {label: "Arrow", key: win.keys.toolArrow, tool: "arrow", glyph: "arrow"},
                                     {label: "Pixelate", key: win.keys.toolPixelate, tool: "blur", glyph: "pixelate"},
                                     {label: "Smart erase", key: win.keys.toolErase, tool: "erase", glyph: "smart-erase"} ]
                            ColumnLayout {
                                required property var modelData
                                spacing: 4
                                Layout.preferredWidth: 40
                                Layout.maximumWidth: 40
                                ActionButton {
                                    objectName: "tool_" + modelData.tool
                                    text: ""
                                    icon.source: "qrc:/icons/annotation/" + modelData.glyph + ".svg"
                                    icon.width: 26
                                    icon.height: 26
                                    // Preserve the artwork's colors independently of Good/Bad ink.
                                    icon.color: "transparent"
                                    leftPadding: 7
                                    rightPadding: 7
                                    opacity: enabled ? 1 : 0.4
                                    Accessible.name: modelData.label
                                    ToolTip.visible: hovered
                                    ToolTip.text: modelData.tool === "blur" ? "Pixelate (" + modelData.key + ") · Scroll to adjust block size"
                                        : modelData.tool === "cut" ? "Cut: remove an image strip (" + modelData.key + ")"
                                        : modelData.label + " (" + modelData.key + ")"
                                    checked: canvas.tool === modelData.tool
                                    enabled: canvas.hasImage && !canvas.arranging
                                    focusPolicy: Qt.TabFocus
                                    Layout.preferredWidth: 40
                                    Layout.preferredHeight: 40
                                    onClicked: win.chooseTool(modelData.tool)
                                }
                                Label {
                                    objectName: "shortcut_" + modelData.tool
                                    text: modelData.key
                                    textFormat: Text.PlainText
                                    color: "#9ba5b5"
                                    font.pixelSize: 12
                                    horizontalAlignment: Text.AlignHCenter
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                            }
                        }
                    }
                    RowLayout {
                        objectName: "historyButtons"
                        spacing: 2
                        Layout.leftMargin: 10
                        Layout.alignment: Qt.AlignTop
                        ActionButton {
                            objectName: "undoButton"
                            text: ""
                            icon.source: "qrc:/icons/undo-2.svg"
                            Accessible.name: "Undo"
                            ToolTip.visible: hovered
                            ToolTip.text: "Undo (" + win.keys.undo + ")"
                            enabled: canvas.canUndo && !win.editingText
                            focusPolicy: Qt.TabFocus
                            onClicked: canvas.undo()
                        }
                        ActionButton {
                            objectName: "redoButton"
                            text: ""
                            icon.source: "qrc:/icons/redo-2.svg"
                            Accessible.name: "Redo"
                            ToolTip.visible: hovered
                            ToolTip.text: "Redo (" + win.keys.redo + ")"
                            enabled: canvas.canRedo && !win.editingText
                            focusPolicy: Qt.TabFocus
                            onClicked: canvas.redo()
                        }
                    }
                }

                ColumnLayout {
                    id: modeGroup
                    objectName: "modeGroup"
                    x: annotationToolbar.singleLine ? annotationToolCluster.width + 16 : 0
                    y: annotationToolbar.singleLine ? 0
                        : annotationToolCluster.height + 8
                    spacing: 4
                    Rectangle {
                        id: modeSegmentedControl
                        objectName: "modeSegmentedControl"
                        color: "#22262c"
                        radius: 7
                        border.width: 1
                        border.color: "#424952"
                        implicitWidth: 168
                        implicitHeight: 40
                        Layout.preferredWidth: implicitWidth
                        Layout.preferredHeight: implicitHeight
                        Layout.alignment: Qt.AlignHCenter

                        function labelColor(ink, selected) {
                            function luminance(value) {
                                function linear(channel) {
                                    return channel <= 0.04045 ? channel / 12.92
                                        : Math.pow((channel + 0.055) / 1.055, 2.4)
                                }
                                return 0.2126 * linear(value.r) + 0.7152 * linear(value.g) + 0.0722 * linear(value.b)
                            }
                            const foreground = Qt.lighter(ink, 1.6)
                            const background = selected ? Qt.darker(ink, 1.8) : color
                            const foregroundLuminance = luminance(foreground)
                            const backgroundLuminance = luminance(background)
                            const contrast = (Math.max(foregroundLuminance, backgroundLuminance) + 0.05)
                                / (Math.min(foregroundLuminance, backgroundLuminance) + 0.05)
                            if (contrast >= 4.5) return foreground
                            // Keep arbitrary configured inks readable against either segment.
                            return backgroundLuminance > 0.18 ? "#000000" : "#ffffff"
                        }

                        Rectangle {
                            id: selectedModeSegment
                            x: canvas.colorMode === "good" ? 1 : modeSegmentedControl.width / 2
                            y: 1
                            width: (modeSegmentedControl.width - 2) / 2
                            height: modeSegmentedControl.height - 2
                            radius: 6
                            color: Qt.darker(canvas.colorMode === "good" ? canvas.goodColor : canvas.badColor, 1.8)
                            border.width: 1
                            border.color: Qt.darker(canvas.colorMode === "good" ? canvas.goodColor : canvas.badColor, 1.2)
                        }
                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 1
                            spacing: 0
                            ShortcutButton {
                                id: goodModeButton
                                objectName: "ink_good"
                                text: "Good"
                                underlineShortcut: win.shortcuts.goodMode.indexOf("G") >= 0
                                font.pixelSize: 12
                                leftPadding: 6
                                rightPadding: 6
                                background: Rectangle {
                                    color: "transparent"
                                    radius: 6
                                    border.width: goodModeButton.visualFocus ? 1 : 0
                                    border.color: goodModeButton.textColor
                                }
                                Accessible.name: "Good mode"
                                ToolTip.visible: hovered
                                ToolTip.text: "Good mode (" + win.keys.goodMode + ")"
                                textColor: modeSegmentedControl.labelColor(canvas.goodColor, checked)
                                Material.foreground: textColor
                                Material.accent: textColor
                                checked: canvas.colorMode === "good"
                                enabled: canvas.hasImage
                                focusPolicy: Qt.TabFocus
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                onClicked: { win.commitText(); canvas.setInkMode("good") }
                            }
                            ShortcutButton {
                                id: badModeButton
                                objectName: "ink_bad"
                                text: "Bad"
                                underlineShortcut: win.shortcuts.badMode.indexOf("B") >= 0
                                font.pixelSize: 12
                                leftPadding: 6
                                rightPadding: 6
                                background: Rectangle {
                                    color: "transparent"
                                    radius: 6
                                    border.width: badModeButton.visualFocus ? 1 : 0
                                    border.color: badModeButton.textColor
                                }
                                Accessible.name: "Bad mode"
                                ToolTip.visible: hovered
                                ToolTip.text: "Bad mode (" + win.keys.badMode + ")"
                                textColor: modeSegmentedControl.labelColor(canvas.badColor, checked)
                                Material.foreground: textColor
                                Material.accent: textColor
                                checked: canvas.colorMode === "bad"
                                enabled: canvas.hasImage
                                focusPolicy: Qt.TabFocus
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                onClicked: { win.commitText(); canvas.setInkMode("bad") }
                            }
                        }
                    }
                    Label {
                        objectName: "modeHeading"
                        text: "Color Mode"
                        color: "#9ba5b5"
                        font.pixelSize: 12
                        horizontalAlignment: Text.AlignHCenter
                        Layout.fillWidth: true
                    }
                }

                Label {
                    id: annotationHint
                    objectName: "annotationHint"
                    x: modeGroup.x + modeGroup.width + 16
                    y: modeGroup.y
                    width: Math.max(0, finishGroup.x - x - 16)
                    height: Math.max(40, implicitHeight)
                    text: win.notice !== "" ? win.notice
                        : canvas.hasImage ? win.hint : "Capture or open an image"
                    textFormat: Text.PlainText
                    color: win.notice !== "" ? "#91bff0" : "#8f99a8"
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                    verticalAlignment: Text.AlignVCenter
                }

                ColumnLayout {
                    id: finishGroup
                    objectName: "finishGroup"
                    x: annotationToolbar.width - width
                    y: annotationToolbar.singleLine ? 0
                        : annotationToolCluster.height + 8
                    spacing: 4
                    RowLayout {
                        id: finishButtons
                        objectName: "finishButtons"
                        spacing: 4
                        Layout.alignment: Qt.AlignHCenter
                        ShortcutButton {
                            objectName: "saveButton"
                            text: "Save"
                            underlineShortcut: win.shortcuts.saveClose.indexOf("S") >= 0
                            Accessible.name: "Save image and close"
                            enabled: canvas.hasImage
                            focusPolicy: Qt.TabFocus
                            ToolTip.visible: hovered
                            ToolTip.text: "Save under " + appSettings.picturesRoot + "/YEAR/MONTH and close (" + win.keys.saveClose + ")"
                            onClicked: win.saveAndClose()
                        }
                        PrimaryButton {
                            objectName: "copyButton"
                            text: "Copy"
                            underlineShortcut: win.shortcuts.copyClose.indexOf("C") >= 0
                            Accessible.name: "Copy image and close"
                            enabled: canvas.hasImage
                            focusPolicy: Qt.TabFocus
                            implicitHeight: 40
                            font.pixelSize: 13
                            topPadding: 0
                            bottomPadding: 0
                            ToolTip.visible: hovered
                            ToolTip.text: "Copy image to clipboard and close (" + win.keys.copyClose + ")"
                            onClicked: win.finish()
                        }
                    }
                    Label {
                        objectName: "finishHeading"
                        text: "Finish (will close)"
                        color: "#9ba5b5"
                        font.pixelSize: 12
                        horizontalAlignment: Text.AlignHCenter
                        Layout.fillWidth: true
                    }
                }
            }
        }
    }

    EditorCanvas {
        id: canvas
        objectName: "canvas"
        goodColor: appSettings.goodColor
        badColor: appSettings.badColor
        saveRoot: appSettings.picturesRoot
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
            WheelHandler {
                target: null
                acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                onWheel: event => {
                    canvas.adjustToolSize(event.angleDelta.y || event.pixelDelta.y * 8)
                    event.accepted = true
                }
            }
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
                font.family: canvas.annotationFontFamily
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
        MouseArea {
            id: textResizeHandle
            objectName: "annotationTextResizeHandle"
            visible: win.editingText
            x: textFrame.x + textFrame.width - 4
            y: textFrame.y
            width: 16
            height: Math.max(24, textFrame.height)
            hoverEnabled: true
            preventStealing: true
            cursorShape: Qt.SizeHorCursor
            property real pressX: 0
            property real initialWidth: 0
            ToolTip.visible: containsMouse && !pressed
            ToolTip.text: "Resize text width"
            onPressed: mouse => {
                pressX = mapToItem(canvas, mouse.x, mouse.y).x
                initialWidth = win.textWidth
            }
            onPositionChanged: mouse => {
                if (!pressed) return
                const delta = (mapToItem(canvas, mouse.x, mouse.y).x - pressX) / canvas.imageScale
                const maximum = canvas.imageWidth - win.textX
                const minimum = Math.min(maximum, Math.max(24, canvas.textSize * 2))
                win.textWidth = Math.max(minimum, Math.min(maximum, initialWidth + delta))
            }
            onReleased: textInput.forceActiveFocus()
            onCanceled: if (win.editingText) textInput.forceActiveFocus()
            Rectangle {
                anchors.centerIn: parent
                width: 6
                height: 24
                radius: 3
                color: textResizeHandle.pressed || textResizeHandle.containsMouse ? "#91bff0" : "#dfe5ed"
                border.color: "#111317"
                border.width: 1
            }
        }
    }

    ColumnLayout {
        anchors.centerIn: parent
        visible: !canvas.hasImage
        spacing: 16
        Label { text: "No screenshot open"; font.pixelSize: 26; font.weight: Font.DemiBold; Layout.alignment: Qt.AlignHCenter }
        Label {
            text: "Press " + win.keys.globalCapture + " to select a screen region or stop recording."
            color: "#9ba5b5"
            font.pixelSize: 14
            Layout.alignment: Qt.AlignHCenter
        }
        PrimaryButton { text: "Capture region (" + win.keys.capture + ")"; Layout.alignment: Qt.AlignHCenter; onClicked: win.capture(); ToolTip.visible: hovered; ToolTip.text: "Capture a screen region (" + win.keys.capture + ")" }
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            ActionButton { text: "Open image (" + win.keys.openImage + ")"; onClicked: openDialog.open(); ToolTip.visible: hovered; ToolTip.text: "Open image (" + win.keys.openImage + ")" }
            Label { text: "or"; color: "#9ba5b5" }
            ActionButton { text: "Paste image (" + win.keys.pasteImage + ")"; onClicked: win.pasteImage(); ToolTip.visible: hovered; ToolTip.text: "Paste image (" + win.keys.pasteImage + ")" }
        }
        ActionButton { text: "Multiple regions (" + win.keys.captureMultiple + ")"; Layout.alignment: Qt.AlignHCenter; onClicked: win.captureMultiple(); ToolTip.visible: hovered; ToolTip.text: "Capture multiple regions (" + win.keys.captureMultiple + ")" }
        ActionButton { text: "Record region (" + win.keys.captureVideo + ")"; Layout.alignment: Qt.AlignHCenter; onClicked: win.captureVideo(); ToolTip.visible: hovered; ToolTip.text: "Record a region (" + win.keys.captureVideo + ")" }
    }

    footer: Rectangle {
        height: canvas.hasImage ? 36 : 0
        visible: canvas.hasImage
        color: "#1b1e23"
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            spacing: 12
            Label {
                visible: canvas.arranging
                text: win.notice !== "" ? win.notice
                    : "Drag to reorder · " + win.keys.columnsMore + "/" + win.keys.columnsLess + " columns · " + win.keys.arrange + " to annotate"
                color: win.notice !== "" ? "#91bff0" : "#8f99a8"
                font.pixelSize: 12
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            Item {
                visible: !canvas.arranging
                Layout.fillWidth: true
            }
            Label {
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
