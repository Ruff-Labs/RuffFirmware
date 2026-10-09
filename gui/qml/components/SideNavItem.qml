import QtQuick
import QtQuick.Layouts
import BoardGui

// One row in the SideBar. The sliding accent bar lives in SideBar (shared by
// all items) so it can animate between rows; this item only handles its own
// tinted background, icon and label.
Item {
    id: root
    property string label: ""
    property url iconSource: ""        // Figma-exported SVG; falls back to a letter chip
    property color accentColor: Theme.accent
    property bool active: false
    signal clicked()

    implicitHeight: Theme.navItemHeight

    // Slight press-in instead of a hard color flash
    scale: mouse.pressed ? 0.98 : 1.0
    Behavior on scale { NumberAnimation { duration: Theme.animFast; easing.type: Theme.easing } }

    Rectangle {   // background: accent-tinted when active, never a solid block
        anchors.fill: parent
        radius: Theme.radiusSm
        color: root.active ? Theme.tint(root.accentColor, 0.12)
             : mouse.pressed ? Theme.surfaceHover
             : "transparent"
        Behavior on color { ColorAnimation { duration: Theme.animNormal } }
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.spacingMd
        anchors.rightMargin: Theme.spacingSm
        spacing: Theme.spacingMd - 4

        // Icon slot: image if provided, otherwise a tinted chip with the initial
        Item {
            Layout.preferredWidth: 24
            Layout.preferredHeight: 24
            Image {
                anchors.fill: parent
                source: root.iconSource
                visible: root.iconSource != ""
                fillMode: Image.PreserveAspectFit
                sourceSize: Qt.size(48, 48)
                opacity: root.active ? 1.0 : 0.6
            }
            Rectangle {
                anchors.fill: parent
                visible: root.iconSource == ""
                radius: 7
                color: Theme.tint(root.active ? root.accentColor : Theme.textMuted,
                                  root.active ? 0.22 : 0.14)
                Behavior on color { ColorAnimation { duration: Theme.animNormal } }
                Text {
                    anchors.centerIn: parent
                    text: root.label.charAt(0)
                    color: root.active ? root.accentColor : Theme.textMuted
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                }
            }
        }

        Text {
            Layout.fillWidth: true
            text: root.label
            color: root.active ? Theme.textPrimary : Theme.textMuted
            font.pixelSize: Theme.fontBody
            font.weight: root.active ? Font.DemiBold : Font.Medium
            elide: Text.ElideRight
            Behavior on color { ColorAnimation { duration: Theme.animNormal } }
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        onClicked: root.clicked()
    }
}
