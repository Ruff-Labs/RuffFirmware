import QtQuick
import QtQuick.Layouts
import BoardGui

// Live timing from the firmware. After a heat, its result stays up ("Last heat") until the next
// heat's lights start. The buttons send the same commands as the judge remote.
Item {
    id: root
    property string title: "Race"
    property string subtitle: "Live timing"

    readonly property bool running: backend.connected && backend.raceState === "Running"

    function formatTime(ms) {   // 12345 -> "12.345", -2345 -> "-2.345"
        const t = Math.round(ms)
        const a = Math.abs(t)
        return (t < 0 ? "-" : "") + Math.floor(a / 1000) + "." + ("00" + (a % 1000)).slice(-3)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingLg
        spacing: Theme.spacingMd

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
            text: !backend.connected ? "Firmware not running"
                  : backend.showingLastResult ? "Last heat  ·  now " + backend.raceState
                  : backend.raceState
            color: backend.connected ? Theme.textMuted : Theme.danger
            font.pixelSize: Theme.fontSmall
        }

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
                                  + (backend.laneInUse[modelData.lane] ? "" : "  ·  not wired")
                            color: won ? Theme.success : modelData.color
                        }
                        Item { Layout.fillHeight: true }
                        Text {
                            text: backend.raceTimes[modelData.lane]
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
            AppButton {   // new race: also clears false starts + red lights; works mid-heat on purpose
                Layout.fillWidth: true
                text: "Reset Race"
                variant: "danger"
                enabled: backend.connected
                onClicked: virtualRemote.press("resetRace")
            }
        }
    }
}
