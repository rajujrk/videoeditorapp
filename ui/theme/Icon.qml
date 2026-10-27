import QtQuick
import QtQuick.Effects

// A monochrome icon from icons/<name>.svg, tinted to `color`.
Item {
    id: icon
    property string name
    property color color: Theme.color.textSecondary
    property int size: Theme.size.icon

    implicitWidth: size
    implicitHeight: size

    Image {
        id: image
        anchors.fill: parent
        source: icon.name ? Qt.resolvedUrl("icons/" + icon.name + ".svg") : ""
        sourceSize: Qt.size(icon.size * 2, icon.size * 2) // crisp on Retina
        smooth: true
        visible: false
    }

    MultiEffect {
        anchors.fill: parent
        source: image
        colorization: 1.0
        colorizationColor: icon.color
    }
}
