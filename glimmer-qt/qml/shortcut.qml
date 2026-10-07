import QtQuick

Item {
    id: control
    required property QtObject relay
    property var sequences: []
    property string scope: "window"
    property bool repeatKeys: false
    Shortcut {
        sequences: control.sequences
        context: control.scope === "application" ? Qt.ApplicationShortcut : Qt.WindowShortcut
        enabled: control.enabled && control.visible
        autoRepeat: control.repeatKeys
        onActivated: control.relay.dispatch(control, "on-activate", "")
        onActivatedAmbiguously: control.relay.dispatch(control, "on-ambiguous", "")
    }
}
