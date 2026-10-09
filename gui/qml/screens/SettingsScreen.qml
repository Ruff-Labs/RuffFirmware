import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import BoardGui

// Device settings. Persist these with QtCore's Settings type or a C++ backend.
Item {
    id: root
    property string title: "Settings"
    property string subtitle: "Network and system "   // shown under the title in PageHeader

    //property alias brightness: brightnessSlider.value
    property alias networkName: networkNameField.text
    property alias networkPassword: networkPasswordField.text
    //property alias soundEnabled: soundSwitch.checked

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

            SectionLabel { text: "Network" }
            Card {
                id: networkCard
                Layout.fillWidth: true
                padding: Theme.spacingMd
                implicitHeight: networkForm.implicitHeight + padding * 2

                ColumnLayout {
                    id: networkForm
                    width: parent.width
                    spacing: Theme.spacingSm

                    Text {
                        text: "Wi-Fi"
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fontBody
                        font.bold: true
                    }
                    Text {
                        text: "Network name and password"
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontSmall
                        Layout.bottomMargin: Theme.spacingXs
                    }

                    Text { text: "Network name (SSID)"; color: Theme.textMuted; font.pixelSize: Theme.fontSmall }
                    TextField {
                        id: networkNameField
                        Layout.fillWidth: true
                        implicitHeight: Theme.touchTarget
                        placeholderText: "Enter Wi-Fi network name"
                        color: Theme.textPrimary
                        placeholderTextColor: Theme.textMuted
                        background: Rectangle {
                            radius: Theme.radius - 4
                            color: Theme.surface
                            border.color: networkNameField.activeFocus ? Theme.accent : Theme.border
                        }
                    }

                    Text { text: "Password"; color: Theme.textMuted; font.pixelSize: Theme.fontSmall }
                    TextField {
                        id: networkPasswordField
                        Layout.fillWidth: true
                        implicitHeight: Theme.touchTarget
                        placeholderText: "Enter Wi-Fi password"
                        echoMode: TextInput.Password
                        color: Theme.textPrimary
                        placeholderTextColor: Theme.textMuted
                        background: Rectangle {
                            radius: Theme.radius - 4
                            color: Theme.surface
                            border.color: networkPasswordField.activeFocus ? Theme.accent : Theme.border
                        }
                    }

                    Text {
                        text: "Optional network details"
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fontBody
                        font.bold: true
                        Layout.topMargin: Theme.spacingSm
                    }
                    Text { text: "IP address"; color: Theme.textMuted; font.pixelSize: Theme.fontSmall }
                    TextField {
                        Layout.fillWidth: true
                        implicitHeight: Theme.touchTarget
                        placeholderText: "Automatic (DHCP)"
                        color: Theme.textPrimary
                        placeholderTextColor: Theme.textMuted
                        background: Rectangle {
                            radius: Theme.radius - 4
                            color: Theme.surface
                            border.color: parent.activeFocus ? Theme.accent : Theme.border
                        }
                    }
                    Text { text: "Gateway"; color: Theme.textMuted; font.pixelSize: Theme.fontSmall }
                    TextField {
                        Layout.fillWidth: true
                        implicitHeight: Theme.touchTarget
                        placeholderText: "Optional"
                        color: Theme.textPrimary
                        placeholderTextColor: Theme.textMuted
                        background: Rectangle {
                            radius: Theme.radius - 4
                            color: Theme.surface
                            border.color: parent.activeFocus ? Theme.accent : Theme.border
                        }
                    }
                    Text { text: "DNS server"; color: Theme.textMuted; font.pixelSize: Theme.fontSmall }
                    TextField {
                        Layout.fillWidth: true
                        implicitHeight: Theme.touchTarget
                        placeholderText: "Optional"
                        color: Theme.textPrimary
                        placeholderTextColor: Theme.textMuted
                        background: Rectangle {
                            radius: Theme.radius - 4
                            color: Theme.surface
                            border.color: parent.activeFocus ? Theme.accent : Theme.border
                        }
                    }
                }
            }

            // ---- Display ----
            // SectionLabel { text: "Display" }
            // Card {
            //     Layout.fillWidth: true
            //     implicitHeight: 72
            //     RowLayout {
            //         anchors.fill: parent
            //         Text { text: "Brightness"; color: Theme.textPrimary; font.pixelSize: Theme.fontBody }
            //         Slider {
            //             id: brightnessSlider
            //             Layout.fillWidth: true
            //             from: 10; to: 100; value: 80; stepSize: 5
            //         }
            //         Text {
            //             text: Math.round(brightnessSlider.value) + "%"
            //             color: Theme.textMuted; font.pixelSize: Theme.fontSmall
            //             Layout.preferredWidth: 44
            //             horizontalAlignment: Text.AlignRight
            //         }
            //     }
            // }

            // }
            // ---- Sound ----
            // SectionLabel { text: "Sound"; Layout.topMargin: Theme.spacingMd }
            // Card {
            //     Layout.fillWidth: true
            //     implicitHeight: 72
            //     RowLayout {
            //         anchors.fill: parent
            //         Text {
            //             Layout.fillWidth: true
            //             text: "Start beeps"; color: Theme.textPrimary; font.pixelSize: Theme.fontBody
            //         }
            //         Switch { id: soundSwitch; checked: true }
            //     }
            // }

            //TODO: make sure that this is linked to the git pull scrip that updates against the repo
            // ---- System ----
            SectionLabel { text: "System"; Layout.topMargin: Theme.spacingMd }
            Card {
                Layout.fillWidth: true
                padding: Theme.spacingSm + 4                       // 12px — tweak to match Figma
                implicitHeight: row.implicitHeight + padding * 2   // grows to fit, like height:auto
                RowLayout {
                    id: row
                    anchors.fill: parent
                    Text {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignVCenter
                        text: "Version 0.1.0"; color: Theme.textMuted; font.pixelSize: Theme.fontSmall
                    }
                    AppButton {
                        Layout.alignment: Qt.AlignVCenter
                        text: "Restart"
                        variant: "danger"
                        onClicked: console.log("TODO: backend restart")
                    }
                }
            }
        }
    }
}
