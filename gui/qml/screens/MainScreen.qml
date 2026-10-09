import QtQuick
import QtQuick.Layouts
import BoardGui

// Home dashboard. The sidebar now handles navigation, so instead of a 2x2
// grid that repeats the sidebar, Home shows status at a glance: a hero card
// for the next race plus tappable status tiles.
// TODO: the race/sensor values below are placeholders - bind them to the
// C++ backend (RaceConfig, sensor model, last result) once it exists.
Item {
    id: root
    property string title: "Home"
    property string subtitle: "Race overview"
    signal openScreen(string name)

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingLg
        anchors.topMargin: Theme.spacingMd
        spacing: Theme.spacingMd

        // ---- Hero: next race + primary action ----
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 150
            radius: Theme.radiusLg
            border.color: Theme.tint(Theme.accent, 0.3)
            // Accent wash fading into the surface: the one place on Home
            // where colour is used generously, so the main action stands out.
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: Theme.tint(Theme.accent, 0.18) }
                GradientStop { position: 0.7; color: Theme.surface }
            }

            Rectangle {   // 1px top highlight
                anchors { top: parent.top; topMargin: 1; horizontalCenter: parent.horizontalCenter }
                width: parent.width - Theme.radiusLg * 2
                height: 1
                color: Theme.highlight
            }

            RowLayout {
                anchors.fill: parent
                anchors.margins: Theme.spacingLg
                spacing: Theme.spacingLg

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingXs

                    SectionLabel { text: "Next race"; color: Theme.accent }
                    Text {
                        Layout.fillWidth: true
                        text: "Lane 1  vs  Lane 2"
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fontTitle + 4
                        font.weight: Font.DemiBold
                        font.letterSpacing: -0.3
                        elide: Text.ElideRight
                    }
                    Text {
                        text: "3 heats  ·  gates armed"
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontSmall
                    }
                }

                ColumnLayout {
                    spacing: Theme.spacingSm
                    AppButton {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 150
                        text: "Set Up Race"
                        variant: "primary"
                        onClicked: root.openScreen("setup")
                    }
                    AppButton {
                        Layout.fillWidth: true
                        text: "Live Timing"
                        onClicked: root.openScreen("race")
                    }
                }
            }
        }
        // TODO: this is all fake data this need to be live at one point 
        // ---- Status tiles ----
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.spacingMd

            NavTile {
                Layout.fillWidth: true; Layout.fillHeight: true
                value: "3 / 4"
                label: "Sensors"
                subtitle: "Gates ready"
                accentColor: Theme.success
                onClicked: root.openScreen("sensor")
            }
            NavTile {
                Layout.fillWidth: true; Layout.fillHeight: true
                value: "--.---"
                label: "Race"
                subtitle: "Last result"
                accentColor: Theme.warning
                onClicked: root.openScreen("race")
            }
            NavTile {
                Layout.fillWidth: true; Layout.fillHeight: true
                value: "80%"
                label: "Settings"
                subtitle: "Brightness"
                accentColor: Theme.textMuted
                onClicked: root.openScreen("settings")
            }
        }
    }
}
