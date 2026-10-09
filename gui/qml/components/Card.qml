import QtQuick
import BoardGui

// Rounded panel. Children go inside with their own layout.
// Premium pass: faint top-to-bottom gradient, softer border and a 1px top
// highlight, matching NavTile so every surface reads as one system.
Rectangle {
    id: card
    default property alias content: inner.data
    property int padding: Theme.spacingMd

    radius: Theme.radius
    border.color: Theme.borderSubtle
    border.width: 1
    gradient: Gradient {
        GradientStop { position: 0.0; color: Theme.surface }
        GradientStop { position: 1.0; color: Theme.surfaceLow }
    }
    // Give the Card a size from outside (Layout.*, anchors, width/height)
    implicitWidth: 200
    implicitHeight: 100

    Rectangle {   // 1px top highlight
        anchors { top: parent.top; topMargin: 1; horizontalCenter: parent.horizontalCenter }
        width: parent.width - card.radius * 2
        height: 1
        color: Theme.highlight
    }

    Item {
        id: inner
        anchors.fill: parent
        anchors.margins: parent.padding
    }
}
