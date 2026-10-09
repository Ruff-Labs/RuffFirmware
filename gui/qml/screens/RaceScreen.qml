import QtQuick
import QtQuick.Layouts
import BoardGui

// Live timing. The Timer here is a UI-only stopwatch so you can see it work;
// real timing should come from the board (hardware timestamps) via C++.
Item {
    id: root
    property string title: "Race"
    property string subtitle: "Live timing"   // shown under the title in PageHeader

    property bool running: false
    property int elapsedMs: 0
    property double startedAt: 0
    property int lane1Ms: -1
    property int lane2Ms: -1

    function formatTime(ms) {
        if (ms < 0) return "--.---"
        const s = Math.floor(ms / 1000)
        const rem = ms % 1000
        return s + "." + ("00" + rem).slice(-3)
    }
    function start() {
        lane1Ms = -1; lane2Ms = -1
        startedAt = Date.now() - elapsedMs
        running = true
    }
    function stop()  { running = false }
    function reset() { running = false; elapsedMs = 0; lane1Ms = -1; lane2Ms = -1 }

    Timer {
        interval: 16; repeat: true; running: root.running
        onTriggered: root.elapsedMs = Date.now() - root.startedAt
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingLg
        spacing: Theme.spacingMd

        // Big clock
        Text {
            Layout.alignment: Qt.AlignHCenter
            text: root.formatTime(root.elapsedMs)
            color: root.running ? Theme.textPrimary : Theme.textMuted
            font.pixelSize: Theme.fontDisplay
            font.bold: true
            font.family: "monospace"
        }

        // Lane results
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.spacingMd

            Repeater {
                model: [
                    { name: "Lane 1", ms: root.lane1Ms, color: Theme.accent },
                    { name: "Lane 2", ms: root.lane2Ms, color: Theme.warning }
                ]
                delegate: Card {
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    ColumnLayout {
                        anchors.fill: parent
                        SectionLabel { text: modelData.name; color: modelData.color }
                        Item { Layout.fillHeight: true }
                        Text {
                            text: root.formatTime(modelData.ms)
                            color: Theme.textPrimary
                            font.pixelSize: Theme.fontTitle + 10
                            font.family: "monospace"
                        }
                    }
                }
            }
        }

        // Controls
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingMd
            AppButton {
                Layout.fillWidth: true
                text: root.running ? "Stop" : "Start"
                variant: root.running ? "danger" : "success"
                onClicked: root.running ? root.stop() : root.start()
            }
            AppButton {
                Layout.fillWidth: true
                text: "Reset"
                enabled: !root.running
                onClicked: root.reset()
            }
        }
    }
}
