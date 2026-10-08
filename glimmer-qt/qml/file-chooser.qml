import QtQuick
import QtQuick.Dialogs

Item {
    id: control
    required property QtObject relay
    property string title: "Open image"
    property var nameFilters: ["Images (*.png *.jpg *.jpeg *.webp *.bmp *.tif *.tiff)"]
    property url currentFolder: ""
    property url selectedFile: ""
    readonly property alias dialogVisible: chooser.visible
    property bool pending: false
    property var previousFocus: null
    function open() {
        if (pending || !control.Window.window) return
        previousFocus = control.Window.window.activeFocusItem
        if (currentFolder.toString().length > 0) chooser.currentFolder = currentFolder
        if (selectedFile.toString().length > 0) chooser.selectedFile = selectedFile
        pending = true
        chooser.open()
    }
    function finish(accepted) {
        if (!pending) return
        pending = false
        if (accepted) selectedFile = chooser.selectedFile
        chooser.close()
        if (previousFocus) previousFocus.forceActiveFocus(Qt.OtherFocusReason)
        previousFocus = null
        relay.dispatch(control, accepted ? "on-accepted" : "on-rejected",
                       accepted ? relay.encodedLocalUrl(selectedFile) : "")
    }
    function close() { finish(false) }
    function reject() { finish(false) }
    FileDialog {
        id: chooser
        objectName: "glimmerFileDialog"
        parentWindow: control.Window.window
        title: control.title
        nameFilters: control.nameFilters
        fileMode: FileDialog.OpenFile
        onAccepted: control.finish(true)
        onRejected: control.finish(false)
        onVisibleChanged: control.relay.dispatch(control, "on-visible-change", "")
    }
}
