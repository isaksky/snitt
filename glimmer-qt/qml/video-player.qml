import QtQuick
import QtQuick.Layouts
import QtMultimedia

Geometry {
    id: root
    property alias player: player
    property alias audioOutput: audio
    property alias videoOutput: video
    property alias videoSink: video.videoSink
    property alias source: player.source
    property alias position: player.position
    property alias playbackRate: player.playbackRate
    property alias muted: audio.muted
    property string fillMode: "fit"

    VideoOutput {
        id: video
        anchors.fill: parent
        fillMode: root.fillMode === "crop" ? VideoOutput.PreserveAspectCrop
                  : root.fillMode === "stretch" ? VideoOutput.Stretch
                  : VideoOutput.PreserveAspectFit
    }
    AudioOutput { id: audio }
    MediaPlayer {
        id: player
        videoOutput: video
        audioOutput: audio
    }
}
