import QtQuick
import QtQuick.Layouts
import BoardGui

// Title area at the top of the content column. Took over the title job from
// the old NavBar; reads `title` / `subtitle` off whichever screen is showing.
Item {
    id: root
    property string title: ""
    property string subtitle: ""

    implicitHeight: column.implicitHeight + Theme.spacingLg

    ColumnLayout {
        id: column
        anchors {
            left: parent.left; right: parent.right; bottom: parent.bottom
            leftMargin: Theme.spacingLg; rightMargin: Theme.spacingLg
        }
        spacing: 2

        Text {
            Layout.fillWidth: true
            text: root.title
            color: Theme.textPrimary
            font.pixelSize: Theme.fontTitle + 2
            font.weight: Font.DemiBold
            font.letterSpacing: -0.3   // tighter tracking on large text looks crisper
            elide: Text.ElideRight
        }
        Text {
            Layout.fillWidth: true
            text: root.subtitle
            visible: text.length > 0
            color: Theme.textMuted
            font.pixelSize: Theme.fontSmall
            elide: Text.ElideRight
        }
    }
}
