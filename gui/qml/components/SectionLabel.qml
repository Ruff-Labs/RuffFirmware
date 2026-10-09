import QtQuick
import BoardGui

// Small uppercase heading used to label groups on a screen.
Text {
    color: Theme.textMuted
    font.pixelSize: Theme.fontSmall
    font.weight: Font.DemiBold          // was bold; lighter weight + wider tracking looks more refined
    font.capitalization: Font.AllUppercase
    font.letterSpacing: 1.4
}
