import QtQuick
import QtQuick.Layouts
import BoardGui

// Virtual judge remote: the same 5 buttons as the RF remote, sent to the firmware
// (backend/virtual_remote.cpp). For testing until the receiver is wired up.
// The firmware treats these exactly like real remote presses.
Item {
    id: root
    property string title: "Remote"
    property string subtitle: "Virtual judge remote (testing)"   // shown under the title in PageHeader

    // one big button per remote command
    component RemoteButton: AppButton {
        required property string command
        Layout.fillWidth: true
        Layout.fillHeight: true
        font.pixelSize: Theme.fontTitle
        onClicked: virtualRemote.press(command)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingLg
        spacing: Theme.spacingMd

        GridLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            columns: 3
            rowSpacing: Theme.spacingMd
            columnSpacing: Theme.spacingMd

            RemoteButton { text: "Ready"; command: "ready"; variant: "primary" }
            RemoteButton { text: "Start"; command: "start"; variant: "success" }
            RemoteButton { text: "Stop";  command: "stop";  variant: "danger" }
            RemoteButton { text: "Reset"; command: "reset"; variant: "secondary" }
            RemoteButton { text: "Abort"; command: "abort"; variant: "danger"; Layout.columnSpan: 2 }
        }

        Card {
            Layout.fillWidth: true
            implicitHeight: 64

            ColumnLayout {
                anchors.fill: parent
                spacing: 2
                Text {
                    text: "Last sent: " + (virtualRemote.lastPress || "nothing yet")
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontBody
                }
                Text {
                    text: "READY toggles on/off  ·  firmware at " + virtualRemote.host + ":5556"
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSmall
                }
            }
        }
    }
}
