import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

TextArea {
    id: control
    required property QtObject relay
    textFormat: TextEdit.PlainText
    wrapMode: TextEdit.Wrap
    selectByMouse: true
    // A scroll container supplies the viewport width; height remains content-driven.
    width: parent ? parent.width : implicitWidth
    property int cursorIndex: -1
    property int anchorIndex: -1
    readonly property int cursorCodepoint: fromUtf16(cursorPosition)
    readonly property int selectionStartCodepoint: fromUtf16(selectionStart)
    readonly property int selectionEndCodepoint: fromUtf16(selectionEnd)
    function toUtf16(index) {
        let offset = 0
        for (let n = 0; n < index && offset < text.length; ++n) {
            const c = text.charCodeAt(offset++)
            if (c >= 0xd800 && c <= 0xdbff && offset < text.length
                    && text.charCodeAt(offset) >= 0xdc00 && text.charCodeAt(offset) <= 0xdfff) ++offset
        }
        return offset
    }
    function fromUtf16(offset) {
        let count = 0
        for (let n = 0; n < offset; ++n) {
            const c = text.charCodeAt(n)
            if (c >= 0xd800 && c <= 0xdbff && n + 1 < offset
                    && text.charCodeAt(n + 1) >= 0xdc00 && text.charCodeAt(n + 1) <= 0xdfff) ++n
            ++count
        }
        return count
    }
    function applySelection() {
        if (inputMethodComposing || cursorIndex < 0) return
        select(toUtf16(anchorIndex < 0 ? cursorIndex : anchorIndex), toUtf16(cursorIndex))
    }
    Component.onCompleted: applySelection()
    onCursorRectangleChanged: {
        const viewport = parent ? parent.parent : null
        if (!activeFocus || !viewport || !("contentY" in viewport)) return
        const top = y + cursorRectangle.y
        const bottom = top + cursorRectangle.height
        if (top < viewport.contentY) viewport.contentY = top
        else if (bottom > viewport.contentY + viewport.height)
            viewport.contentY = bottom - viewport.height
    }
    // textChanged also covers paste and IME commit. Native controlled writes
    // suppress this relay, but never suppress Qt's own layout notifications.
    property string lastText: ""
    onTextChanged: {
        if (text === lastText) return
        lastText = text
        if (relay) relay.dispatch(control, "on-change", text)
    }
    Keys.priority: Keys.BeforeItem
    Keys.onPressed: event => {
        if (inputMethodComposing) return
        if (event.key === Qt.Key_Escape) {
            event.accepted = true
            if (!event.isAutoRepeat) relay.dispatch(control, "on-cancel", "")
        } else if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                   && (event.modifiers & Qt.ControlModifier)) {
            event.accepted = true
            if (!event.isAutoRepeat) relay.dispatch(control, "on-commit", "")
        }
    }
    MouseArea {
        anchors.fill: parent
        z: 1000
        acceptedButtons: Qt.NoButton
        scrollGestureEnabled: true
        onWheel: event => {
            event.accepted = control.relay.pointer(control, "on-wheel", {
                "angle-x": event.angleDelta.x, "angle-y": event.angleDelta.y,
                "pixel-x": event.pixelDelta.x, "pixel-y": event.pixelDelta.y,
                modifiers: event.modifiers, inverted: event.inverted})
        }
    }
}
