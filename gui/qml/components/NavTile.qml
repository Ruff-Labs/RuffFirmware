import QtQuick
import QtQuick.Layouts
import BoardGui

// Tappable status tile used on the MainScreen dashboard.
// Restyled for the sidebar layout: tinted icon chip (instead of a solid
// block), an optional big `value` so tiles show live info, a faint gradient
// with a 1px top highlight for depth, and a press-in scale.
Rectangle {
    id: root
    property string label: ""
    property string subtitle: ""
    property string value: ""          // optional headline number, e.g. "3 / 4"
    property url iconSource: ""        // set to a Figma-exported SVG/PNG
    property color accentColor: Theme.accent
    signal clicked()

    radius: Theme.radius
    border.color: mouse.pressed ? Theme.tint(accentColor, 0.6) : Theme.borderSubtle
    border.width: 1
    gradient: Gradient {
        GradientStop { position: 0.0; color: mouse.pressed ? Theme.surfaceHover : Theme.surface }
        GradientStop { position: 1.0; color: mouse.pressed ? Theme.surface : Theme.surfaceLow }
    }

    scale: mouse.pressed ? 0.97 : 1.0
    Behavior on scale { NumberAnimation { duration: Theme.animFast; easing.type: Theme.easing } }
    Behavior on border.color { ColorAnimation { duration: Theme.animFast } }

    Rectangle {   // 1px top highlight: the "glass edge"
        anchors { top: parent.top; topMargin: 1; horizontalCenter: parent.horizontalCenter }
        width: parent.width - root.radius * 2
        height: 1
        color: Theme.highlight
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingMd
        spacing: Theme.spacingXs

        RowLayout {
            Layout.fillWidth: true

            // Icon slot: image if provided, otherwise a tinted chip
            Rectangle {
                Layout.preferredWidth: 36
                Layout.preferredHeight: 36
                radius: 10
                color: Theme.tint(root.accentColor, 0.15)
                Image {
                    anchors.centerIn: parent
                    width: 20; height: 20
                    source: root.iconSource
                    visible: root.iconSource != ""
                    fillMode: Image.PreserveAspectFit
                    sourceSize: Qt.size(40, 40)
                }
                Text {
                    anchors.centerIn: parent
                    visible: root.iconSource == ""
                    text: root.label.charAt(0)
                    color: root.accentColor
                    font.pixelSize: 15
                    font.weight: Font.DemiBold
                }
            }

            Item { Layout.fillWidth: true }

            Text {   // chevron hints the tile is tappable
                text: "›"
                color: Theme.textMuted
                font.pixelSize: Theme.fontTitle
            }
        }

        Item { Layout.fillHeight: true }

        Text {
            Layout.fillWidth: true
            visible: text.length > 0
            text: root.value
            color: Theme.textPrimary
            font.pixelSize: Theme.fontTitle + 4
            font.weight: Font.DemiBold
            font.letterSpacing: -0.3
            elide: Text.ElideRight
        }
        Text {
            Layout.fillWidth: true
            text: root.label
            color: root.value.length > 0 ? Theme.textMuted : Theme.textPrimary
            font.pixelSize: root.value.length > 0 ? Theme.fontSmall : Theme.fontTitle
            font.weight: root.value.length > 0 ? Font.Medium : Font.DemiBold
            elide: Text.ElideRight
        }
        Text {
            Layout.fillWidth: true
            text: root.subtitle
            color: Theme.textMuted
            font.pixelSize: Theme.fontSmall - 1
            elide: Text.ElideRight
            visible: text.length > 0
            opacity: 0.8
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        onClicked: root.clicked()
    }
}
