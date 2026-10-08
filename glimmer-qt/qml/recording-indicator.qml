import QtQuick
import QtQuick.Layouts

Rectangle {
    id: root
    property bool completing: false
    radius: width / 2
    color: "#d91b1e23"
    border.color: "#6687ac9b"
    border.width: 2
    onCompletingChanged: circularWipe.requestPaint()
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
                        -Math.PI / 2, root.completing ? 3 * Math.PI / 2 : Math.PI)
            context.stroke()
        }
        RotationAnimator on rotation {
            from: 0; to: 360; duration: 1050
            loops: Animation.Infinite
            running: root.visible && !root.completing
        }
    }
    Text {
        anchors.centerIn: parent
        text: root.completing ? "Ready" : "Preparing"
        color: "#f0f3f7"
        font.pixelSize: Math.max(14, root.width * 0.075)
        font.weight: Font.DemiBold
    }
}
