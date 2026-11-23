import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import BoardGui

// Wi-Fi setup and system info. "Connect" asks the firmware to join the network with nmcli
// (virtualRemote.setWifi); progress comes back in backend.wifiState / wifiMessage. The board's
// NetworkManager remembers the network, so there's nothing to save here.
Item {
    id: root
    property string title: "Settings"
    property string subtitle: "Network and system"

    property string inputError: ""   // what setWifi() rejected, shown instead of the firmware's message
    readonly property bool working: backend.wifiState === "working"

    // one labelled text field, styled like the rest of the app
    component Field: ColumnLayout {
        property alias label: caption.text
        property alias text: input.text
        property alias placeholder: input.placeholderText
        property alias echoMode: input.echoMode
        Layout.fillWidth: true
        spacing: Theme.spacingXs
        Text { id: caption; color: Theme.textMuted; font.pixelSize: Theme.fontSmall }
        TextField {
            id: input
            Layout.fillWidth: true
            implicitHeight: Theme.touchTarget
            color: Theme.textPrimary
            placeholderTextColor: Theme.textMuted
            inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
            background: Rectangle {
                radius: Theme.radius - 4
                color: Theme.surface
                border.color: input.activeFocus ? Theme.accent : Theme.border
            }
        }
    }

    Flickable {
        anchors.fill: parent
        contentHeight: column.implicitHeight + Theme.spacingLg * 2
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        ColumnLayout {
            id: column
            x: Theme.spacingLg
            y: Theme.spacingLg
            width: parent.width - Theme.spacingLg * 2
            spacing: Theme.spacingSm

            // ---- Wi-Fi ----
            SectionLabel { text: "Wi-Fi" }
            Card {
                Layout.fillWidth: true
                padding: Theme.spacingMd
                implicitHeight: wifiForm.implicitHeight + padding * 2

                ColumnLayout {
                    id: wifiForm
                    width: parent.width
                    spacing: Theme.spacingSm

                    Text {
                        Layout.fillWidth: true
                        text: !backend.connected  ? "Firmware not running: can't change Wi-Fi"
                            : backend.wifiSsid    ? "On \"" + backend.wifiSsid + "\"" + (backend.ipAddress ? "  ·  " + backend.ipAddress : "")
                            : "Not on Wi-Fi" + (backend.ipAddress ? "  ·  wired, " + backend.ipAddress : "")
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fontBody
                        font.bold: true
                        elide: Text.ElideRight
                    }

                    Field { id: ssid; label: "Network name (SSID)"; placeholder: "TrackWiFi" }
                    Field { id: password; label: "Password"; placeholder: "Blank for an open network"; echoMode: TextInput.Password }

                    Text {
                        text: "Static IP (optional: leave blank for automatic)"
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fontSmall
                        font.bold: true
                        Layout.topMargin: Theme.spacingSm
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingSm
                        Field { id: address; label: "IP address"; placeholder: "192.168.1.50/24" }
                        Field { id: gateway; label: "Gateway"; placeholder: "192.168.1.1" }
                        Field { id: dns; label: "DNS server"; placeholder: "1.1.1.1" }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.topMargin: Theme.spacingSm
                        spacing: Theme.spacingMd
                        Text {
                            Layout.fillWidth: true
                            text: root.inputError || backend.wifiMessage
                            color: root.inputError || backend.wifiState === "failed" ? Theme.danger
                                 : backend.wifiState === "done" ? Theme.success
                                 : Theme.textMuted
                            font.pixelSize: Theme.fontSmall
                            wrapMode: Text.WordWrap
                        }
                        AppButton {
                            text: root.working ? "Connecting..." : "Connect"
                            variant: "primary"
                            enabled: backend.connected && !root.working
                            onClicked: root.inputError = virtualRemote.setWifi(ssid.text, password.text,
                                                                                address.text, gateway.text, dns.text)
                        }
                    }
                }
            }

            // ---- System ----
            SectionLabel { text: "System"; Layout.topMargin: Theme.spacingMd }
            Card {
                Layout.fillWidth: true
                padding: Theme.spacingSm + 4
                implicitHeight: systemRow.implicitHeight + padding * 2
                RowLayout {
                    id: systemRow
                    anchors.fill: parent
                    Text {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignVCenter
                        text: "GUI " + appVersion + "  ·  Firmware " + (backend.firmwareVersion || "--")
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontSmall
                        elide: Text.ElideRight
                    }
                    AppButton {
                        Layout.alignment: Qt.AlignVCenter
                        text: "Restart"
                        variant: "danger"
                        enabled: backend.connected
                        onClicked: confirmRestart.open()
                    }
                }
            }
        }
    }

    Dialog {
        id: confirmRestart
        anchors.centerIn: parent
        modal: true
        title: "Restart the race software?"
        standardButtons: Dialog.Yes | Dialog.Cancel
        onAccepted: virtualRemote.restartSoftware()
        Label {
            width: 360
            wrapMode: Text.WordWrap
            color: Theme.textPrimary
            text: backend.raceActive
                  ? "A heat is running and will be lost. The screen goes blank for a few seconds."
                  : "The screen goes blank for a few seconds while the firmware and this app restart."
        }
    }
}
