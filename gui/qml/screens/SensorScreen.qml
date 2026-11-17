import QtQuick
import QtQuick.Layouts
import BoardGui

// Sensor/gate status. The ListModel is placeholder data — replace it with a
// C++ model (QAbstractListModel) that updates from the board's hardware.

//NOTE: right now forward and backward are pushed to beam status in general.
Item {
    id: root
    property string title: "Sensors"
    property string subtitle: "Live gate status"   // shown under the title in PageHeader

    // ListModel {
    //     id: sensorModel
    //     ListElement { name: "Forward";  lane: "Lane 1"; status: "ok";      reading: "Clear" }
    //     ListElement { name: "Back"; lane: "Lane 1"; status: "ok";      reading: "Clear" }
    //     ListElement { name: "Forward";  lane: "Lane 2"; status: "warning"; reading: "Weak signal" }
    //     ListElement { name: "Back"; lane: "Lane 2"; status: "error";   reading: "Not detected" }
    // }


    // ListElement values must be constants (no bindings), so the model only says WHICH
    // sensor each card is; the delegate below reads its live value from `backend`.
    ListModel {
        id: sensorModel
        ListElement { name: "Alignment"; lane: "Lane 1"; kind: "align"; laneIndex: 0 }
        ListElement { name: "Beam";      lane: "Lane 1"; kind: "beam";  laneIndex: 0 }
        ListElement { name: "Alignment"; lane: "Lane 2"; kind: "align"; laneIndex: 1 }
        ListElement { name: "Beam";      lane: "Lane 2"; kind: "beam";  laneIndex: 1 }
    }

    function statusColor(s) {
        return s === "ok" ? Theme.success : s === "warning" ? Theme.warning : Theme.danger
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingLg
        spacing: Theme.spacingMd

        // TODO: actually check and see if this is needed later on. (low key might just be a reset or refresh)
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
                required property string kind
                required property int laneIndex

                readonly property bool active: kind === "align" ? !!backend.aligned[laneIndex]
                                                                : !!backend.beamBroken[laneIndex]
                readonly property string status: active ? "ok" : "error"
                readonly property string reading: kind === "align" ? (active ? "Aligned" : "Not aligned")
                                                                   : (active ? "Broken" : "Clear")

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
