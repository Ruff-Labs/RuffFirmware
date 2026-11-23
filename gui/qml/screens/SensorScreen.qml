import QtQuick
import QtQuick.Layouts
import BoardGui

// Live sensor signals per lane (from backend): alignment, the FORWARD and BACK beams, and any
// fault the firmware spotted (SensorHealth.hpp). A lane turned off in LANE_PINS shows as not wired.
Item {
    id: root
    property string title: "Sensors"
    property string subtitle: "Live gate status"

    // one signal: name on the left, state on the right with a coloured dot
    component SignalRow: RowLayout {
        property string name
        property string value
        property color  tone
        Layout.fillWidth: true
        spacing: Theme.spacingSm
        Rectangle { Layout.preferredWidth: 12; Layout.preferredHeight: 12; radius: 6; color: tone }
        Text { Layout.fillWidth: true; text: name; color: Theme.textPrimary; font.pixelSize: Theme.fontBody }
        Text { text: value; color: tone; font.pixelSize: Theme.fontBody; font.bold: true }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingLg
        spacing: Theme.spacingMd

        Text {
            visible: !backend.connected
            text: "Firmware not running: no sensor data"
            color: Theme.danger
            font.pixelSize: Theme.fontSmall
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.spacingMd

            Repeater {
                model: 2
                delegate: Card {
                    required property int index
                    readonly property bool inUse: !!backend.laneInUse[index]
                    readonly property string fault: backend.sensorFaults[index] || ""
                    readonly property bool live: backend.connected && inUse

                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    border.color: fault ? Theme.danger : Theme.borderSubtle
                    opacity: inUse ? 1.0 : 0.6

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: Theme.spacingSm

                        SectionLabel { text: backend.laneNames[index] }

                        Text {
                            visible: !inUse
                            Layout.fillWidth: true
                            text: "Not wired (turned off in LANE_PINS): races run without this lane"
                            color: Theme.textMuted
                            font.pixelSize: Theme.fontSmall
                            wrapMode: Text.WordWrap
                        }

                        SignalRow {
                            visible: inUse
                            name: "Alignment"
                            value: !live ? "--" : backend.aligned[index] ? "Aligned" : "Not aligned"
                            tone: !live ? Theme.textMuted : backend.aligned[index] ? Theme.success : Theme.warning
                        }
                        SignalRow {
                            visible: inUse
                            name: "Forward beam"
                            value: !live ? "--" : backend.forward[index] ? "Broken" : "Clear"
                            tone: !live ? Theme.textMuted : backend.forward[index] ? Theme.warning : Theme.success
                        }
                        SignalRow {
                            visible: inUse
                            name: "Back beam"
                            value: !live ? "--" : backend.back[index] ? "Broken" : "Clear"
                            tone: !live ? Theme.textMuted : backend.back[index] ? Theme.warning : Theme.success
                        }

                        Item { Layout.fillHeight: true }

                        Text {
                            visible: fault !== ""
                            Layout.fillWidth: true
                            text: fault
                            color: Theme.danger
                            font.pixelSize: Theme.fontSmall
                            wrapMode: Text.WordWrap
                        }
                    }
                }
            }
        }
    }
}
