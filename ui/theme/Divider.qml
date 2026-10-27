import QtQuick

// Hairline separator. Horizontal by default; set vertical: true for a column divider.
Rectangle {
    property bool vertical: false
    implicitWidth: vertical ? 1 : 1
    implicitHeight: 1
    color: Theme.color.borderSubtle
}
