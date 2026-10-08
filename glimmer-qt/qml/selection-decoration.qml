import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

Item {
    id: root
    required property QtObject relay
    property var regions: []
    property bool dragging: false
    property rect dragArea: Qt.rect(0, 0, 0, 0)
    property string dimensions: ""
    property string notice: ""
    readonly property real panelWidth: Math.min(960, Math.max(1, width - 24))
    component Artwork: Image {
        property string kind
        property string label: "normal"
        // Quick applies the display scale to provider requests itself.
        source: width > 0 && height > 0 ? "image://snitt-selector-artwork/" + kind + "/" + width + "/" + height + "/" + encodeURIComponent(label) : ""
        sourceSize.width: width; sourceSize.height: height
    }
    component Border: Artwork {
        property rect area
        x: Math.floor(area.x) - 1; y: Math.floor(area.y) - 1
        width: Math.ceil(area.x + area.width + 1) - x
        height: Math.ceil(area.y + area.height + 1) - y
        kind: "border"
        label: [area.x - x, area.y - y, area.width, area.height].join("|")
    }
    Border {
        visible: root.dragging && root.dragArea.width > 0 && root.dragArea.height > 0
        area: root.dragArea
    }
    Repeater {
        model: root.regions
        Item {
            id: region
            required property var modelData
            x: modelData.x; y: modelData.y
            width: modelData.width; height: modelData.height
            Border {
                area: Qt.rect(region.x, region.y, region.width, region.height)
                x: Math.floor(area.x) - 1 - region.x; y: Math.floor(area.y) - 1 - region.y
                width: Math.ceil(area.x + area.width + 1) - Math.floor(area.x) + 1
                height: Math.ceil(area.y + area.height + 1) - Math.floor(area.y) + 1
                label: [area.x - Math.floor(area.x) + 1, area.y - Math.floor(area.y) + 1, area.width, area.height].join("|")
            }
            Artwork {
                // Rasterize fractional origins inside the artwork; shifting an
                // already rasterized number would introduce a second blur.
                readonly property real phaseX: region.x - Math.floor(region.x)
                readonly property real phaseY: region.y - Math.floor(region.y)
                x: -phaseX; y: -phaseY
                width: 28 + (phaseX > 0 ? 1 : 0); height: 28 + (phaseY > 0 ? 1 : 0)
                kind: "badge"; label: [region.modelData.number, phaseX, phaseY].join("|")
            }
            Button {
                id: removeButton
                objectName: "picker-remove-" + region.modelData.token
                x: Math.max(0, Math.min(root.width - 28,
                     Math.round(region.x + (region.width >= 60 ? region.width - 28 : region.width + 4)))) - region.x
                y: Math.max(0, Math.min(root.height - 28, Math.round(region.y))) - region.y
                width: 28; height: 28
                display: AbstractButton.IconOnly
                icon.source: "qrc:/icons/trash.svg"
                icon.width: 20; icon.height: 20; icon.color: "#e9edf3"
                padding: 0; leftPadding: 0; rightPadding: 0; topPadding: 0; bottomPadding: 0; topInset: 0; bottomInset: 0
                focusPolicy: Qt.NoFocus; hoverEnabled: true
                Accessible.name: "Remove region " + region.modelData.number
                NativeToolTip { relay: root.relay; text: removeButton.Accessible.name; active: removeButton.hovered && !removeButton.pressed }
                contentItem: Item {
                    Image {
                        anchors.centerIn: parent; width: 20; height: 20
                        source: "image://snitt-selector-icon/trash/normal"
                        sourceSize.width: 20; sourceSize.height: 20
                    }
                }
                background: Artwork { kind: "remove"; label: parent.hovered ? "#394149" : "#1b1e23" }
                HoverHandler { cursorShape: Qt.PointingHandCursor }
                onClicked: root.relay.dispatch(root, "on-remove", String(region.modelData.token))
            }
        }
    }
    Artwork {
        visible: root.dragging
        width: Math.round(dimensionMetrics.advanceWidth) + 20; height: 30
        x: Math.max(0, Math.min(root.width - width, Math.round(root.dragArea.x + root.dragArea.width) - width))
        y: {
            const bottom = Math.round(root.dragArea.y + root.dragArea.height)
            const desired = bottom + 8
            return Math.max(0, desired + height > root.height ? bottom - height - 8 : desired)
        }
        kind: "dimensions"; label: root.dimensions
        TextMetrics { id: dimensionMetrics; font.pixelSize: 14; text: root.dimensions }
    }
    Artwork {
        visible: root.notice.length > 0
        x: (root.width - width) / 2; y: 91; width: root.panelWidth
        height: noticeLabel.implicitHeight + 20
        kind: "notice"; label: root.notice
        Text {
            id: noticeLabel
            visible: false; width: parent.width - 20
            text: root.notice; wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
            color: "#f1d4a2"
            renderType: Text.NativeRendering
        }
    }
}
