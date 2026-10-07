import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

TextField {
    id: control
    required property QtObject relay
    property string hint: ""
    // Match Glimmer's entry placeholder semantics: show the hint only when empty.
    placeholderText: text.length === 0 ? hint : ""
    Accessible.name: hint
    // textEdited is user input only; applying a :text prop must not fire :on-change.
    onTextEdited: relay.dispatch(control, "on-change", text)
    onAccepted: relay.dispatch(control, "on-activate", "")
}
