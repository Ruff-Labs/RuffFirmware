import QtQuick
import QtQuick.Layouts
import BoardGui

// Left navigation rail (replaces the old top NavBar).
// Top: brand block. Middle: nav items with one shared sliding indicator.
// Bottom: compact system status footer.
Rectangle {
    id: root
    property string currentRoute: "home"
    signal navigate(string route)

    // Same tone as the window background; the 1px edge on the right is what
    // separates it from content. Keeps the sidebar quiet so content leads.
    color: Theme.background
    implicitWidth: Theme.sidebarWidth

    // Each entry keeps the accent its screen used on the old tile grid
    readonly property var items: [
        { route: "home",     label: "Home",     accent: Theme.accent },
        { route: "setup",    label: "Setup",    accent: Theme.accent },
        { route: "sensor",   label: "Sensors",  accent: Theme.success },
        { route: "race",     label: "Race",     accent: Theme.warning },
        { route: "settings", label: "Settings", accent: Theme.textMuted }
    ]
    readonly property int activeIndex: {
        for (let i = 0; i < items.length; ++i)
            if (items[i].route === currentRoute) return i
        return -1
    }

    Rectangle {   // right edge divider
        anchors { top: parent.top; bottom: parent.bottom; right: parent.right }
        width: 1
        color: Theme.borderSubtle
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingMd
        anchors.rightMargin: Theme.spacingMd + 1   // don't sit on the divider
        spacing: 0

        // ---- Brand block ----
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingSm
            Layout.topMargin: Theme.spacingSm
            Layout.bottomMargin: Theme.spacingLg
            spacing: Theme.spacingSm + 2

            Rectangle {   // logo mark: swap for your Figma logo Image
                Layout.preferredWidth: 32
                Layout.preferredHeight: 32
                radius: 9
                gradient: Gradient {
                    GradientStop { position: 0.0; color: Qt.lighter(Theme.accent, 1.15) }
                    GradientStop { position: 1.0; color: Qt.darker(Theme.accent, 1.3) }
                }
                Text {
                    anchors.centerIn: parent
                    text: "R"
                    color: "#FFFFFF"
                    font.pixelSize: 16
                    font.weight: Font.Bold
                }
            }
            Text {
                Layout.fillWidth: true
                text: "Ruff"
                color: Theme.textPrimary
                font.pixelSize: Theme.fontTitle - 2
                font.weight: Font.DemiBold
                font.letterSpacing: 0.3
            }
        }

        // ---- Nav items ----
        Item {
            Layout.fillWidth: true
            implicitHeight: navColumn.implicitHeight

            Column {
                id: navColumn
                width: parent.width
                spacing: Theme.spacingXs

                Repeater {
                    model: root.items
                    delegate: SideNavItem {
                        required property var modelData
                        width: navColumn.width
                        label: modelData.label
                        accentColor: modelData.accent
                        active: modelData.route === root.currentRoute
                        onClicked: root.navigate(modelData.route)
                    }
                }
            }

            // One indicator for the whole list that slides to the active row.
            // This motion is the single biggest "premium" cue in the sidebar.
            Rectangle {
                id: indicator
                readonly property int barHeight: 20
                visible: root.activeIndex >= 0
                x: 0
                y: root.activeIndex * (Theme.navItemHeight + navColumn.spacing)
                   + (Theme.navItemHeight - barHeight) / 2
                width: 3
                height: barHeight
                radius: 2
                color: root.activeIndex >= 0 ? root.items[root.activeIndex].accent : Theme.accent
                Behavior on y { NumberAnimation { duration: Theme.animNormal + 60; easing.type: Theme.easing } }
                Behavior on color { ColorAnimation { duration: Theme.animNormal } }
            }
        }

        Item { Layout.fillHeight: true }

        // ---- Footer: system status ----
        // TODO: bind these to the C++ backend (sensor health, firmware version)
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: footer.implicitHeight + Theme.spacingMd
            radius: Theme.radiusSm
            color: Theme.surface
            border.color: Theme.borderSubtle

            ColumnLayout {
                id: footer
                anchors.centerIn: parent
                width: parent.width - Theme.spacingMd * 2
                spacing: 2

                RowLayout {
                    spacing: Theme.spacingSm
                    Rectangle {   // status LED with a soft halo
                        Layout.preferredWidth: 8
                        Layout.preferredHeight: 8
                        radius: 4
                        color: Theme.success
                        Rectangle {
                            anchors.centerIn: parent
                            width: 16; height: 16; radius: 8
                            color: Theme.tint(Theme.success, 0.18)
                            z: -1
                        }
                    }
                    Text {
                        text: "System ready"
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fontSmall - 1
                        font.weight: Font.Medium
                    }
                }
                Text {
                    text: "Firmware v0.1.0"
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSmall - 2
                }
            }
        }
    }
}
