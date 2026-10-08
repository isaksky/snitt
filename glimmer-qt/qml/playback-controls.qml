import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    required property QtObject relay
    property alias playButton: playButton
    property string elapsed: ""
    property string total: ""
    property string label: "Play"
    property string tooltip: ""
    property string iconSource: ""
    property bool canPlay: true
    implicitHeight: playbackTime.implicitHeight + playButton.height + 4
    Item {
        id: playbackTime
        objectName: "review-time"
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.alignWhenCentered: false
        implicitWidth: timeMetrics.advanceWidth * 2 + timeSeparator.implicitWidth + 16
        implicitHeight: elapsedTime.implicitHeight
        width: implicitWidth; height: implicitHeight
        TextMetrics { id: timeMetrics; font: elapsedTime.font; text: root.total }
        Row {
            spacing: 8
            Label {
                id: elapsedTime
                width: timeMetrics.advanceWidth
                horizontalAlignment: Text.AlignRight
                font.features: { "tnum": 1 }
                text: root.elapsed
                color: "#dfe5ed"
            }
            Label { id: timeSeparator; text: "/"; color: "#dfe5ed" }
            Label { width: timeMetrics.advanceWidth; font: elapsedTime.font; text: root.total; color: "#dfe5ed" }
        }
    }
    ToolButton {
        id: playButton
        objectName: "review-play"
        anchors.top: playbackTime.bottom
        anchors.topMargin: 4
        anchors.horizontalCenter: parent.horizontalCenter
        width: 40; height: 40
        display: AbstractButton.IconOnly
        text: root.label
        icon.source: root.iconSource
        icon.width: 24; icon.height: 24
        Accessible.name: text
        ToolTip.visible: pressed
        ToolTip.text: root.tooltip
        enabled: root.canPlay
        onClicked: relay.dispatch(root, "on-play", "")
    }
}
