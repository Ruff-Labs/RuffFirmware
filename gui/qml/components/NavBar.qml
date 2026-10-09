import QtQuick
import QtQuick.Layouts
import BoardGui

// Top bar: back button, screen title, home button.
// NOTE: no longer used - Main.qml now uses SideBar + PageHeader. Kept for
// reference; safe to delete along with its qmldir/resources.qrc entries.
Rectangle {
    id: root
    property string title: ""
    property bool canGoBack: false
    signal backClicked()
    signal homeClicked()

    implicitHeight: Theme.navBarHeight
    color: Theme.surface

    Rectangle {   // bottom divider
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: 1
        color: Theme.border
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.spacingSm
        anchors.rightMargin: Theme.spacingSm
        spacing: Theme.spacingSm

        AppButton {
            text: "‹  Back"          // swap for an Image with your Figma back icon
            flat: true
            visible: root.canGoBack
            onClicked: root.backClicked()
        }

        Text {
            Layout.fillWidth: true
            text: root.title
            color: Theme.textPrimary
            font.pixelSize: Theme.fontTitle
            font.bold: true
            elide: Text.ElideRight
            horizontalAlignment: root.canGoBack ? Text.AlignHCenter : Text.AlignLeft
            leftPadding: root.canGoBack ? 0 : Theme.spacingSm
        }

        AppButton {
            text: "Home"
            flat: true
            visible: root.canGoBack
            onClicked: root.homeClicked()
        }
    }
}
