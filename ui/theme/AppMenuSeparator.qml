import QtQuick
import QtQuick.Controls

// A thin line between groups of AppMenu entries.
MenuSeparator {
    topPadding: Theme.space.xxs
    bottomPadding: Theme.space.xxs
    contentItem: Rectangle {
        implicitHeight: 1
        color: Theme.color.borderSubtle
    }
}
