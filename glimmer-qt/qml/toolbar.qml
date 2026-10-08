import QtQuick
import QtQuick.Layouts

// Four measured groups: leading tools, options, flexible hint, trailing actions.
// Only layout lives here; controls and application state remain host-owned.
Item {
    id: root
    readonly property Item leading: children.length > 0 ? children[0] : null
    readonly property Item options: children.length > 1 ? children[1] : null
    readonly property Item hint: children.length > 2 ? children[2] : null
    readonly property Item trailing: children.length > 3 ? children[3] : null
    property real hintMinimumWidth: 180
    readonly property bool singleLine: !!leading && !!options && !!trailing
        && width >= leading.implicitWidth + options.implicitWidth + trailing.implicitWidth + hintMinimumWidth + 48
    implicitHeight: !leading || !options || !hint || !trailing ? 0
        : singleLine ? Math.max(leading.implicitHeight, options.implicitHeight, hint.height, trailing.implicitHeight) + 8
        : leading.implicitHeight + 8 + Math.max(options.implicitHeight, hint.height, trailing.implicitHeight) + 4

    Binding { target: root.leading; property: "x"; value: 0; when: !!root.leading }
    Binding { target: root.leading; property: "y"; value: 0; when: !!root.leading }
    Binding { target: root.leading; property: "width"; value: root.leading ? root.leading.implicitWidth : 0; when: !!root.leading }
    Binding { target: root.leading; property: "height"; value: root.leading ? root.leading.implicitHeight : 0; when: !!root.leading }
    Binding { target: root.options; property: "x"; value: root.singleLine ? root.leading.width + 16 : 0; when: !!root.options }
    Binding { target: root.options; property: "y"; value: root.singleLine ? 0 : root.leading ? root.leading.height + 8 : 0; when: !!root.options }
    Binding { target: root.options; property: "width"; value: root.options ? root.options.implicitWidth : 0; when: !!root.options }
    Binding { target: root.options; property: "height"; value: root.options ? root.options.implicitHeight : 0; when: !!root.options }
    Binding { target: root.trailing; property: "x"; value: root.trailing ? root.width - root.trailing.width : 0; when: !!root.trailing }
    Binding { target: root.trailing; property: "y"; value: root.options ? root.options.y : 0; when: !!root.trailing }
    Binding { target: root.trailing; property: "width"; value: root.trailing ? root.trailing.implicitWidth : 0; when: !!root.trailing }
    Binding { target: root.trailing; property: "height"; value: root.trailing ? root.trailing.implicitHeight : 0; when: !!root.trailing }
    Binding { target: root.hint; property: "x"; value: root.options ? root.options.x + root.options.width + 16 : 0; when: !!root.hint }
    Binding { target: root.hint; property: "y"; value: root.options ? root.options.y : 0; when: !!root.hint }
    Binding { target: root.hint; property: "width"; value: root.trailing && root.hint ? Math.max(0, root.trailing.x - root.hint.x - 16) : 0; when: !!root.hint }
    Binding { target: root.hint; property: "height"; value: root.hint ? Math.max(40, root.hint.implicitHeight) : 40; when: !!root.hint }
}
