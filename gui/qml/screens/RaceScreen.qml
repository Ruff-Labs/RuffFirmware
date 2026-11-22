import QtQuick
import QtQuick.Layouts
import BoardGui

// Live timing from the firmware (backend, see gui/backend/backend.hpp).
// The buttons send judge remote commands, same as the Remote screen.
Item {
    id: root
    property string title: "Race"
    property string subtitle: "Live timing"   // shown under the title in PageHeader

    readonly property bool running: backend.connected && backend.raceState === "Running"

    // ms -> "12.345"; negative (countdown to green) -> "-2.345"
    function formatTime(ms) {
        const t = Math.round(ms)
        const a = Math.abs(t)
        return (t < 0 ? "-" : "") + Math.floor(a / 1000) + "." + ("00" + (a % 1000)).slice(-3)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingLg
        spacing: Theme.spacingMd

        // Big clock
        Text {
            Layout.alignment: Qt.AlignHCenter
            text: backend.connected ? root.formatTime(backend.clockMs) : "--.---"
            color: root.running ? Theme.textPrimary : Theme.textMuted
            font.pixelSize: Theme.fontDisplay
            font.bold: true
            font.family: "monospace"
        }
        Text {
            Layout.alignment: Qt.AlignHCenter
            text: backend.connected ? backend.raceState : "Firmware not running"
            color: backend.connected ? Theme.textMuted : Theme.danger
            font.pixelSize: Theme.fontSmall
        }

        // Lane results
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.spacingMd

            Repeater {
                model: [
                    { lane: 0, color: Theme.accent },
                    { lane: 1, color: Theme.warning }
                ]
                delegate: Card {
                    required property var modelData
                    readonly property bool won: backend.winner === modelData.lane
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    border.color: won ? Theme.success : Theme.borderSubtle

                    ColumnLayout {
                        anchors.fill: parent
                        SectionLabel {
                            text: backend.laneNames[modelData.lane] + (won ? "  ·  winner" : "")
                            color: won ? Theme.success : modelData.color
                        }
                        Item { Layout.fillHeight: true }
                        Text {
                            text: backend.raceTimes[modelData.lane]   // "--.---", "12.345" or "DNF"
                            color: Theme.textPrimary
                            font.pixelSize: Theme.fontTitle + 10
                            font.family: "monospace"
                        }
                        Text {
                            visible: backend.connected
                            text: "splits " + backend.splits[modelData.lane]
                                  + "  ·  false starts " + backend.falseStarts[modelData.lane]
                            color: backend.falseStarts[modelData.lane] > 0 ? Theme.danger : Theme.textMuted
                            font.pixelSize: Theme.fontSmall
                        }
                    }
                }
            }
        }

        // Controls: judge remote commands
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingMd
            AppButton {
                Layout.fillWidth: true
                text: "Start"
                variant: "success"
                enabled: backend.connected && !root.running
                onClicked: virtualRemote.press("start")
            }
            AppButton {
                Layout.fillWidth: true
                text: "Stop"
                variant: "danger"
                enabled: root.running
                onClicked: virtualRemote.press("stop")
            }
            AppButton {
                Layout.fillWidth: true
                text: "Reset"
                enabled: backend.connected && !root.running
                onClicked: virtualRemote.press("reset")
            }
            // head table: new race, also clears false starts + red lights (works mid-heat, on purpose)
            AppButton {
                Layout.fillWidth: true
                text: "Reset Race"
                variant: "danger"
                enabled: backend.connected
                onClicked: virtualRemote.press("resetRace")
            }
        }
    }
}
