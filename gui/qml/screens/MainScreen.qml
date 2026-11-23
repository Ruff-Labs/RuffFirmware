import QtQuick
import QtQuick.Layouts
import BoardGui

// Home: the race that's set up, plus live status tiles that open their screen.
Item {
    id: root
    property string title: "Home"
    property string subtitle: "Race overview"
    signal openScreen(string name)

    // lanes wired up (one-lane mode leaves one out), and how many of those are aligned
    readonly property int lanesInUse: (backend.laneInUse[0] ? 1 : 0) + (backend.laneInUse[1] ? 1 : 0)
    readonly property int lanesAligned: (backend.laneInUse[0] && backend.aligned[0] ? 1 : 0)
                                      + (backend.laneInUse[1] && backend.aligned[1] ? 1 : 0)

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
                        text: backend.laneNames[0] + "  vs  " + backend.laneNames[1]
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fontTitle + 4
                        font.weight: Font.DemiBold
                        font.letterSpacing: -0.3
                        elide: Text.ElideRight
                    }
                    Text {
                        text: backend.heats > 0 ? backend.heats + (backend.heats === 1 ? " heat" : " heats")
                                                : "Not set up yet"
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
        // ---- Status tiles ----
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.spacingMd

            NavTile {
                Layout.fillWidth: true; Layout.fillHeight: true
                value: backend.connected ? root.lanesAligned + " / " + root.lanesInUse : "--"
                label: "Sensors"
                subtitle: !backend.connected      ? "Firmware not running"
                        : backend.anySensorFault ? "Sensor fault, tap to see"
                        : "Lanes aligned"
                accentColor: !backend.connected      ? Theme.textMuted
                           : backend.anySensorFault ? Theme.danger
                           : root.lanesAligned === root.lanesInUse ? Theme.success : Theme.warning
                onClicked: root.openScreen("sensor")
            }

            NavTile {
                Layout.fillWidth: true; Layout.fillHeight: true
                value: !backend.connected  ? "--"
                     : backend.winner >= 0 ? backend.laneNames[backend.winner]
                     : backend.raceState
                label: "Race"
                subtitle: backend.winner < 0       ? "Race state"
                        : backend.showingLastResult ? "Won the last heat"
                        : "Winner"
                accentColor: Theme.warning
                onClicked: root.openScreen("race")
            }

            NavTile {
                Layout.fillWidth: true; Layout.fillHeight: true
                value: backend.ipAddress !== "" ? backend.ipAddress : "Offline"
                label: "Settings"
                subtitle: "Network"
                accentColor: Theme.textMuted
                onClicked: root.openScreen("settings")
            }
        }
    }
}
