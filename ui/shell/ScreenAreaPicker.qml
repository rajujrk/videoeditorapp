import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import TechieVideoEditor.Theme

// Choosing part of a display to record: a window over the whole display, dimmed but for the
// area, which is dragged to move, by its edges and corners to size, or drawn afresh by
// dragging outside it. Quick shapes (the whole display, 16:9, 9:16, square) and its size in
// points. Return (or Use this area) accepts, Esc cancels.
Window {
    id: picker
    required property var display // {x, y, width, height}: the display, global points
    // The area, in this window's points (the display's own).
    property real areaX: display.width * 0.2
    property real areaY: display.height * 0.2
    property real areaWidth: display.width * 0.6
    property real areaHeight: display.height * 0.6
    readonly property real smallest: 64
    signal accepted(var area) // {x, y, width, height}: global points
    signal canceled()

    objectName: "screenAreaPicker"
    title: qsTr("Choose the area to record")
    flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.Tool | Qt.NoDropShadowWindowHint
    color: "transparent"
    x: display.x || 0
    y: display.y || 0
    width: display.width || 1280
    height: display.height || 720
    visible: true

    // Starts from an area already chosen (global points), kept on the display.
    function start(area) {
        if (area && area.width > 0 && area.height > 0)
            setArea(area.x - (display.x || 0), area.y - (display.y || 0), area.width, area.height)
        requestActivate()
    }
    function setArea(x, y, w, h) {
        w = Math.max(smallest, Math.min(width, w))
        h = Math.max(smallest, Math.min(height, h))
        areaX = Math.round(Math.max(0, Math.min(width - w, x)))
        areaY = Math.round(Math.max(0, Math.min(height - h, y)))
        areaWidth = Math.round(w)
        areaHeight = Math.round(h)
    }
    // A shape (width over height; 0: the whole display), as large as fits at 80 %, centred.
    function shape(ratio) {
        if (ratio <= 0) {
            setArea(0, 0, width, height)
            return
        }
        let w = width * 0.8, h = w / ratio
        if (h > height * 0.8) {
            h = height * 0.8
            w = h * ratio
        }
        setArea((width - w) / 2, (height - h) / 2, w, h)
    }
    function accept() {
        accepted({ x: (display.x || 0) + areaX, y: (display.y || 0) + areaY, width: areaWidth, height: areaHeight })
    }

    Shortcut { sequences: ["Return", "Enter"]; onActivated: picker.accept() }
    Shortcut { sequence: "Esc"; onActivated: picker.canceled() }

    // Dimmed round the area.
    Item {
        anchors.fill: parent
        readonly property color dim: Qt.rgba(0, 0, 0, 0.5)
        Rectangle { color: parent.dim; x: 0; y: 0; width: parent.width; height: picker.areaY }
        Rectangle { color: parent.dim; x: 0; y: picker.areaY + picker.areaHeight; width: parent.width; height: parent.height - y }
        Rectangle { color: parent.dim; x: 0; y: picker.areaY; width: picker.areaX; height: picker.areaHeight }
        Rectangle {
            color: parent.dim
            x: picker.areaX + picker.areaWidth
            y: picker.areaY
            width: parent.width - x
            height: picker.areaHeight
        }
    }

    // Dragging outside the area draws a new one.
    MouseArea {
        objectName: "areaCanvas"
        anchors.fill: parent
        cursorShape: Qt.CrossCursor
        property point from
        onPressed: (mouse) => from = Qt.point(mouse.x, mouse.y)
        onPositionChanged: (mouse) => {
            if (!pressed)
                return
            picker.setArea(Math.min(from.x, mouse.x), Math.min(from.y, mouse.y),
                           Math.abs(mouse.x - from.x), Math.abs(mouse.y - from.y))
        }
    }

    // The area: drag to move.
    Rectangle {
        id: area
        objectName: "recordArea"
        x: picker.areaX
        y: picker.areaY
        width: picker.areaWidth
        height: picker.areaHeight
        color: "transparent"
        border.color: Theme.color.accent
        border.width: 2
        MouseArea {
            anchors.fill: parent
            cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
            property point from
            property point start
            onPressed: (mouse) => {
                from = mapToItem(null, mouse.x, mouse.y)
                start = Qt.point(picker.areaX, picker.areaY)
            }
            onPositionChanged: (mouse) => {
                if (!pressed)
                    return
                const p = mapToItem(null, mouse.x, mouse.y)
                picker.setArea(start.x + p.x - from.x, start.y + p.y - from.y, picker.areaWidth, picker.areaHeight)
            }
        }
    }

    // Edges and corners: drag to size (the opposite side stays).
    Repeater {
        // [x share, y share]: 0 left/top, 0.5 middle, 1 right/bottom.
        model: [[0, 0], [0.5, 0], [1, 0], [1, 0.5], [1, 1], [0.5, 1], [0, 1], [0, 0.5]]
        delegate: Rectangle {
            id: handle
            required property var modelData
            required property int index
            objectName: "areaHandle:" + index
            readonly property real u: modelData[0]
            readonly property real v: modelData[1]
            x: picker.areaX + u * picker.areaWidth - width / 2
            y: picker.areaY + v * picker.areaHeight - height / 2
            width: 12
            height: 12
            radius: 2
            color: "#FFFFFF"
            border.color: Theme.color.accent
            border.width: 2
            MouseArea {
                anchors.fill: parent
                anchors.margins: -6
                cursorShape: handle.u === 0.5 ? Qt.SizeVerCursor : handle.v === 0.5 ? Qt.SizeHorCursor
                           : (handle.u === handle.v ? Qt.SizeFDiagCursor : Qt.SizeBDiagCursor)
                property var start: null
                onPressed: start = { l: picker.areaX, t: picker.areaY, r: picker.areaX + picker.areaWidth,
                                     b: picker.areaY + picker.areaHeight }
                onPositionChanged: (mouse) => {
                    if (!pressed || start === null)
                        return
                    const p = mapToItem(null, mouse.x, mouse.y)
                    let l = start.l, t = start.t, r = start.r, b = start.b
                    if (handle.u === 0) l = Math.min(p.x, r - picker.smallest)
                    if (handle.u === 1) r = Math.max(p.x, l + picker.smallest)
                    if (handle.v === 0) t = Math.min(p.y, b - picker.smallest)
                    if (handle.v === 1) b = Math.max(p.y, t + picker.smallest)
                    picker.setArea(l, t, r - l, b - t)
                }
            }
        }
    }

    // Its size, over the area's top left.
    Rectangle {
        objectName: "areaSize"
        x: picker.areaX
        y: picker.areaY >= height + 6 ? picker.areaY - height - 6 : picker.areaY + 6
        width: sizeText.implicitWidth + Theme.space.m * 2
        height: sizeText.implicitHeight + Theme.space.xs * 2
        radius: Theme.radius.s
        color: Qt.rgba(0, 0, 0, 0.7)
        Text {
            id: sizeText
            anchors.centerIn: parent
            text: Math.round(picker.areaWidth) + " × " + Math.round(picker.areaHeight)
            font: Theme.type.mono
            color: "#FFFFFF"
        }
    }

    // The controls: under the area, or inside it at the bottom when there is no room.
    Rectangle {
        id: bar
        objectName: "areaBar"
        x: Math.max(8, Math.min(picker.width - width - 8, picker.areaX + (picker.areaWidth - width) / 2))
        y: picker.areaY + picker.areaHeight + height + 16 <= picker.height ? picker.areaY + picker.areaHeight + 10
                                                                          : picker.areaY + picker.areaHeight - height - 10
        width: row.implicitWidth + Theme.space.m * 2
        height: row.implicitHeight + Theme.space.s * 2
        radius: Theme.radius.l
        color: Theme.color.surfaceRaised
        border.color: Theme.color.borderStrong
        RowLayout {
            id: row
            anchors.centerIn: parent
            spacing: Theme.space.xs
            Repeater {
                model: [{ id: "full", label: qsTr("Whole display"), ratio: 0 }, { id: "16x9", label: "16:9", ratio: 16 / 9 },
                        { id: "9x16", label: "9:16", ratio: 9 / 16 }, { id: "1x1", label: "1:1", ratio: 1 }]
                delegate: TextButton {
                    required property var modelData
                    objectName: "areaShape:" + modelData.id
                    text: modelData.label
                    variant: "ghost"
                    onClicked: picker.shape(modelData.ratio)
                }
            }
            Divider { implicitWidth: 1; implicitHeight: 18 }
            TextButton { objectName: "cancelArea"; text: qsTr("Cancel"); variant: "secondary"; onClicked: picker.canceled() }
            TextButton { objectName: "acceptArea"; text: qsTr("Use this area"); variant: "primary"; onClicked: picker.accept() }
        }
    }
}
