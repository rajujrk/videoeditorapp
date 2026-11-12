import QtQuick
import QtQuick.Controls
import TechieVideoEditor.Theme
import TechieVideoEditor.Player

// A library tile's corner: ☆ to keep it as a favourite (★ when it is one), and # to give a
// favourite tags. Shown while the tile is hovered, and always once it is a favourite.
Row {
    id: badge
    required property string kind
    required property string itemId
    property bool hovered: false
    signal tagsRequested()

    readonly property bool favourite: Favourites.items[kind + ":" + itemId] !== undefined
    readonly property var tags: Favourites.items[kind + ":" + itemId] || []
    visible: hovered || favourite || star.hovered || tagButton.hovered
    spacing: 2

    component Corner: AbstractButton {
        id: corner
        property string glyph
        property color tint: Theme.color.textSecondary
        implicitWidth: 22
        implicitHeight: 22
        hoverEnabled: true
        ToolTip.visible: hovered
        ToolTip.delay: 500
        background: Rectangle {
            radius: width / 2
            color: corner.hovered ? Theme.color.surfaceHover : Theme.color.surfaceRaised
            border.color: Theme.color.borderSubtle
        }
        contentItem: Text {
            text: corner.glyph
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            font.pixelSize: 13
            color: corner.tint
        }
    }

    Corner {
        id: tagButton
        objectName: "favouriteTags:" + badge.kind + ":" + badge.itemId
        visible: badge.favourite
        glyph: "#"
        tint: badge.tags.length > 0 ? Theme.color.accentText : Theme.color.textSecondary
        ToolTip.text: badge.tags.length > 0 ? qsTr("Tags: %1").arg(badge.tags.join(", ")) : qsTr("Add tags")
        onClicked: badge.tagsRequested()
    }
    Corner {
        id: star
        objectName: "favourite:" + badge.kind + ":" + badge.itemId
        glyph: badge.favourite ? "★" : "☆"
        tint: badge.favourite ? Theme.color.warning : Theme.color.textSecondary
        ToolTip.text: badge.favourite ? qsTr("Remove from favourites") : qsTr("Add to favourites")
        onClicked: Favourites.toggle(badge.kind, badge.itemId)
    }
}
