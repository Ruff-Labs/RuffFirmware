import QtQuick
import QtQuick.Layouts
import BoardGui

// Sensor/gate status. The ListModel is placeholder data — replace it with a
// C++ model (QAbstractListModel) that updates from the board's hardware.
Item {
    id: root
    property string title: "Sensors"
    property string subtitle: "Live gate status"   // shown under the title in PageHeader

    ListModel {
        id: sensorModel
        ListElement { name: "Start Gate";  lane: "Lane 1"; status: "ok";      reading: "Clear" }
        ListElement { name: "Finish Gate"; lane: "Lane 1"; status: "ok";      reading: "Clear" }
        ListElement { name: "Start Gate";  lane: "Lane 2"; status: "warning"; reading: "Weak signal" }
        ListElement { name: "Finish Gate"; lane: "Lane 2"; status: "error";   reading: "Not detected" }
    }

    function statusColor(s) {
        return s === "ok" ? Theme.success : s === "warning" ? Theme.warning : Theme.danger
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingLg
        spacing: Theme.spacingMd

        RowLayout {
            Layout.fillWidth: true
            SectionLabel { text: "Gate status"; Layout.fillWidth: true }
            AppButton {
                text: "Test All"
                onClicked: console.log("TODO: call backend sensor self-test")
            }
        }

        GridView {
            id: grid
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: sensorModel
            cellWidth: width / 2
            cellHeight: 110
            interactive: contentHeight > height

            delegate: Item {
                required property string name
                required property string lane
                required property string status
                required property string reading

                width: grid.cellWidth
                height: grid.cellHeight

                Card {
                    anchors.fill: parent
                    anchors.margins: Theme.spacingXs

                    RowLayout {
                        anchors.fill: parent
                        spacing: Theme.spacingMd

                        Rectangle {   // status LED
                            Layout.preferredWidth: 18
                            Layout.preferredHeight: 18
                            radius: 9
                            color: root.statusColor(status)
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Text { text: name; color: Theme.textPrimary; font.pixelSize: Theme.fontBody; font.bold: true }
                            Text { text: lane; color: Theme.textMuted; font.pixelSize: Theme.fontSmall }
                            Text { text: reading; color: root.statusColor(status); font.pixelSize: Theme.fontSmall }
                        }
                    }
                }
            }
        }
    }
}
