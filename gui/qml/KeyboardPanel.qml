import QtQuick
import QtQuick.VirtualKeyboard

// On-screen keyboard: the board's touchscreen has no physical keyboard. Main.qml loads this only
// when main.cpp turned it on (eglfs/linuxfb, or RUFF_ONSCREEN_KEYBOARD=1), so a laptop without
// the Qt Virtual Keyboard module still runs. Slides up when a text field gets focus.
InputPanel {
    width: parent ? parent.width : 0
    y: active ? parent.height - height : parent.height
    Behavior on y { NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }
}
