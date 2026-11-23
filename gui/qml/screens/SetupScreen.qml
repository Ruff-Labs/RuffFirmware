import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import BoardGui

// Pre-race configuration. "Start Race" sends the lane names + heats to the firmware
// (virtualRemote.setupRace), which starts a new race with them and echoes them back,
// so the Race and Home screens show them via backend.laneNames / backend.heats.
Item {
    id: root
    property string title: "Setup"
    property string subtitle: "Configure lanes and heats"
    signal startRace()

    property alias lane1Name: lane1Field.text
    property alias lane2Name: lane2Field.text
    property alias heats: heatsSpin.value

    RowLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingLg
        spacing: Theme.spacingLg

        // ---- Left: form ----
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.spacingSm

            SectionLabel { text: "Lane 1" }
            TextField {
                id: lane1Field
                Layout.fillWidth: true
                implicitHeight: Theme.touchTarget
                placeholderText: "Lane 1 name"
                color: Theme.textPrimary
                placeholderTextColor: Theme.textMuted
                background: Rectangle {
                    radius: Theme.radius - 4; color: Theme.surface
                    border.color: lane1Field.activeFocus ? Theme.accent : Theme.border
                }
            }

            SectionLabel { text: "Lane 2"; Layout.topMargin: Theme.spacingSm }
            TextField {
                id: lane2Field
                Layout.fillWidth: true
                implicitHeight: Theme.touchTarget
                placeholderText: "Lane 2 name"
                color: Theme.textPrimary
                placeholderTextColor: Theme.textMuted
                background: Rectangle {
                    radius: Theme.radius - 4; color: Theme.surface
                    border.color: lane2Field.activeFocus ? Theme.accent : Theme.border
                }
            }

            SectionLabel { text: "Heats"; Layout.topMargin: Theme.spacingSm }
            SpinBox {
                id: heatsSpin
                from: 1; to: 10; value: 3
                implicitHeight: Theme.touchTarget
                editable: false
            }

            Item { Layout.fillHeight: true }
        }

        // ---- Right: summary + start ----
        ColumnLayout {
            // fillWidth: false stops the full-width children below from making
            // this column grow; with the sidebar the content area is narrower,
            // so the form on the left needs the extra room.
            Layout.fillWidth: false
            Layout.preferredWidth: 240
            Layout.fillHeight: true
            spacing: Theme.spacingMd

            Card {
                Layout.fillWidth: true
                Layout.fillHeight: true
                ColumnLayout {
                    width: parent.width
                    spacing: Theme.spacingSm
                    SectionLabel { text: "Summary" }
                    Text {
                        text: (root.lane1Name || "Lane 1") + "  vs  " + (root.lane2Name || "Lane 2")
                        color: Theme.textPrimary; font.pixelSize: Theme.fontBody
                        Layout.fillWidth: true; wrapMode: Text.WordWrap
                    }
                    Text {
                        text: root.heats + (root.heats === 1 ? " heat" : " heats")
                        color: Theme.textMuted; font.pixelSize: Theme.fontSmall
                    }
                }
            }

            AppButton {
                Layout.fillWidth: true
                text: "Start Race"
                variant: "primary"
                // the firmware ignores a setup mid-heat anyway; this just makes that visible
                enabled: backend.connected && !backend.raceActive
                onClicked: {
                    virtualRemote.setupRace(root.lane1Name, root.lane2Name, root.heats)
                    root.startRace()
                }
            }
            Text {
                Layout.fillWidth: true
                visible: !backend.connected || backend.raceActive
                text: !backend.connected ? "Firmware not running" : "Finish or abort the current heat first"
                color: Theme.textMuted
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.WordWrap
            }
        }
    }
}
