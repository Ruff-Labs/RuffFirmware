import QtQuick
import QtQuick.Controls.Basic
import BoardGui

// Styled button. variant: "primary" | "secondary" | "danger" | "success"
Button {
    id: control
    property string variant: "secondary"

    readonly property color baseColor: {
        switch (variant) {
        case "primary": return Theme.accent
        case "danger":  return Theme.danger
        case "success": return Theme.success
        default:        return Theme.surfaceHover
        }
    }

    implicitHeight: Theme.touchTarget
    implicitWidth: Math.max(96, contentItem.implicitWidth + leftPadding + rightPadding)
    leftPadding: Theme.spacingMd
    rightPadding: Theme.spacingMd
    font.pixelSize: Theme.fontBody
    font.bold: variant !== "secondary"

    contentItem: Text {
        text: control.text
        font: control.font
        color: !control.enabled ? Theme.textMuted
             : (control.flat ? Theme.accent
             : (control.variant === "secondary" ? Theme.textPrimary : "#FFFFFF"))
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        radius: Theme.radius - 4
        visible: !control.flat || control.down
        color: control.flat ? Theme.surfaceHover
             : (control.down ? Qt.darker(control.baseColor, 1.25) : control.baseColor)
        opacity: control.enabled ? 1.0 : 0.4
        border.color: control.variant === "secondary" && !control.flat ? Theme.border : "transparent"
    }
}
