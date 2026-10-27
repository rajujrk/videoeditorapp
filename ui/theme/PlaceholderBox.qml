import QtQuick

// Wireframe placeholder for content that a later phase fills in. Dashed outline + label.
Rectangle {
    id: box
    property string label
    property string phase // e.g. "Phase 1"

    color: "transparent"
    radius: Theme.radius.m

    Canvas {
        anchors.fill: parent
        property color strokeColor: Theme.color.borderStrong
        onStrokeColorChanged: requestPaint()
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            ctx.strokeStyle = strokeColor
            ctx.lineWidth = 1
            ctx.setLineDash([4, 4])
            ctx.beginPath()
            ctx.roundedRect(0.5, 0.5, width - 1, height - 1, box.radius, box.radius)
            ctx.stroke()
        }
    }

    Column {
        anchors.centerIn: parent
        spacing: Theme.space.xxs
        width: parent.width - Theme.space.l * 2

        Text {
            width: parent.width
            text: box.label
            font: Theme.type.label
            color: Theme.color.textSecondary
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }
        Text {
            width: parent.width
            visible: box.phase !== ""
            text: box.phase
            font: Theme.type.caption
            color: Theme.color.textDisabled
            horizontalAlignment: Text.AlignHCenter
        }
    }
}
