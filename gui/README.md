# GUI for the Rufflabs Timing system


## Overview
This GUI is intended to be interface on the modual device that acts as the hub for the race system.

## Setup and Config
Build and run steps for Mac, Windows, and the board are in the [root README](../README.md).
Short version:

| Where   | Command (from repo root, after installing Qt 6.5+) | Binary |
|---------|-----------------------------------------------------|--------|
| Mac     | `cmake -B build -DCMAKE_PREFIX_PATH="$(brew --prefix qt)" && cmake --build build` | `build/gui/race_gui.app` |
| Windows | Open the root `CMakeLists.txt` in Qt Creator, then press Run | `build\gui\Release\gui.exe` |
| Board   | `./scripts/build.sh`, which `gui.service` runs on boot | `build/gui/gui` |

- QML lives in `qml/` and is compiled in via `resources.qrc`, so **rebuild after QML edits**.
  A new `.qml` file has to be added to `resources.qrc` (and `qml/qmldir` if it's a component).
- The app uses the Qt Quick Controls **Basic** style everywhere (`main.cpp`), so it looks the same
  on a laptop as on the board.
- On the board it draws straight to the display with `QT_QPA_PLATFORM=eglfs` (set in
  `systemd/gui.service`). If the screen stays black, switch it to `linuxfb`.


