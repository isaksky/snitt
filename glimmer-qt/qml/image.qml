import QtQuick
import QtQuick.Layouts

Geometry {
    id: root
    required property QtObject relay
    property url imageSource: ""
    property int decodeWidth: -1
    property int decodeHeight: -1
    property string imageFill: "fit"
    property bool imageAsync: true
    property bool imageCache: true
    function stateName() {
        return picture.status === Image.Ready ? "ready"
             : picture.status === Image.Loading ? "loading"
             : picture.status === Image.Error ? "error" : "null"
    }
    readonly property string loadState: stateName()
    readonly property string loadError: loadState === "error"
                                        ? "Could not load image: " + imageSource.toString() : ""
    readonly property int paintedWidth: picture.paintedWidth
    readonly property int paintedHeight: picture.paintedHeight
    readonly property bool hasPixels: picture.visible
    implicitWidth: picture.implicitWidth
    implicitHeight: picture.implicitHeight

    function report() {
        if (!relay) return
        const state = stateName()
        relay.dispatch(root, "on-status", state)
        if (state === "ready") relay.dispatch(root, "on-loaded", "")
        else if (state === "error") relay.dispatch(root, "on-error", "Could not load image: " + imageSource.toString())
    }

    Image {
        id: picture
        anchors.fill: parent
        source: root.imageSource
        sourceSize.width: root.decodeWidth
        sourceSize.height: root.decodeHeight
        asynchronous: root.imageAsync
        cache: root.imageCache
        fillMode: root.imageFill === "crop" ? Image.PreserveAspectCrop
                  : root.imageFill === "stretch" ? Image.Stretch
                  : root.imageFill === "pad" ? Image.Pad : Image.PreserveAspectFit
        visible: status === Image.Ready && source === root.imageSource
        onStatusChanged: root.report()
    }
    // Initial synchronous loads can finish during component creation, before
    // the bridge publishes a handle. This owned timer reports their final state.
    Timer { interval: 0; running: true; onTriggered: root.report() }
}
