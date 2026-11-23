# RuffLabs Source Code

Board firmware and touchscreen GUI. Part of the [Ruff-Labs](https://github.com/Ruff-Labs) GitHub
org; the website that shows race results is a separate repo there.

| Folder      | What it is                                              | Builds on            |
|-------------|---------------------------------------------------------|----------------------|
| `firmware/` | Sensors, light tree, remote, timer, race DB (`firmware`, `tree_test`) | BeaglePlay only (needs libgpiod v2) |
| `gui/`      | Qt 6 / QML touchscreen app                              | Mac, Windows, board  |
| `common/`   | Shared headers (`protocol.h`)                           | everywhere           |
| `nano/`     | Arduino Nano Every sketches: sensor node, base station, radio range test | PlatformIO |
| `scripts/`  | `setup.sh`, `build.sh`, `update.sh` for the board        | board                |
| `systemd/`  | Services that start firmware + GUI on boot              | board                |
| `tests/`    | Unit tests for the firmware logic (fake hardware)       | Mac, Linux           |

Everything builds from the **top-level** `CMakeLists.txt`. On Mac/Windows CMake prints
`libgpiodcxx not found, skipping firmware and tree_test`. That's expected: the GUI and
`radio_monitor` still get built. The Nano sketches build separately with PlatformIO (below).

---


BEAGLE BOARD CRED CHANGE LATER FOR NOW ITS
user_name = debian
user_pass = rufflab

## Running the GUI on a Mac

Requires Qt **6.5 or newer** and CMake.

```bash
# one time
xcode-select --install          # compiler, if you don't have it
brew install qt cmake

# build (from the repo root)
cmake -B build -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build build --parallel

# run
open build/gui/race_gui.app
# or, to see console output / QML errors:
./build/gui/race_gui.app/Contents/MacOS/race_gui
```

After changing code or QML, just run `cmake --build build --parallel` again (QML is compiled into
the app through `resources.qrc`, so you need to rebuild after QML changes too).

## Running the unit tests (Mac / Linux)

`tests/` checks the Timer, the race state machine and the CSV/Supabase code on a laptop: the
light tree, sensors and remote are faked, and races run on a simulated clock (the whole suite
takes well under a second). Same build as above, then:

```bash
cmake --build build --target unit_tests --parallel
ctest --test-dir build --output-on-failure     # or ./build/tests/unit_tests [name filter]
```

Run them before pushing firmware changes. They're skipped on Windows (the database code is
POSIX-only) and not built on the board (`scripts/build.sh` turns them off).

## Running the GUI on Windows

Requires Qt **6.5 or newer** with the MSVC compiler.

1. Install **Visual Studio 2022** (Community is fine) with the *Desktop development with C++*
   workload.
2. Install Qt with the [Qt Online Installer](https://www.qt.io/download-qt-installer). Under
   *Qt → 6.x.x*, select **MSVC 2022 64-bit**. Also select *Developer and Designer Tools → CMake*
   and *Qt Creator*.

**Easiest: Qt Creator.** *File → Open File or Project* → choose the **repo-root**
`CMakeLists.txt`. Don't use `gui/CMakeLists.txt`, because it depends on `common/` from the root.
Pick the *Desktop Qt 6.x.x MSVC2022 64bit* kit, then press the green ▶ Run button.

**Command line**: open *x64 Native Tools Command Prompt for VS 2022* and run these from the repo root
(change `6.8.0` to whatever version you installed):

```bat
cmake -B build -DCMAKE_PREFIX_PATH=C:\Qt\6.8.0\msvc2022_64
cmake --build build --config Release

:: Qt's DLLs have to be findable when the exe runs
set PATH=C:\Qt\6.8.0\msvc2022_64\bin;%PATH%
build\gui\Release\gui.exe
```

If the window never appears, run it from the prompt and read the QML error it prints.

---

## Running on the board (BeaglePlay)

Requires **Debian 13 (trixie) or newer** on the board. The code uses the libgpiod v2 C++ API and
Qt ≥ 6.5, and older Debian images don't have them.

> Clone the repo anywhere (e.g. `~/RuffFirmware`). `setup.sh` and `update.sh` point the systemd
> services at wherever it is. If you move the clone, re-run `./scripts/update.sh` from the new spot.

### First-time setup

```bash
ssh debian@<board-ip>
git clone https://github.com/Ruff-Labs/<this-repo>.git ~/RuffFirmware
cd ~/RuffFirmware
./scripts/setup.sh
```

`setup.sh` does everything once:
1. `apt install`s the compiler, CMake, libgpiod, libcurl, and Qt/QML packages
2. creates `/etc/ruff/firmware.env` (Supabase and sensor radio settings, see below) and the udev
   rule that names the radio base station `/dev/ruff-radio`
3. builds into `build/` (`scripts/build.sh`)
4. installs `systemd/*.service` and starts **firmware** and **gui**, which also start on every boot
   (the GUI comes up right away; it doesn't wait for WiFi or the firmware)

### Wi-Fi from the touchscreen

Settings → Wi-Fi: type the network name + password (an on-screen keyboard comes up) and tap
Connect. The firmware joins it with `nmcli`, so the board image needs **NetworkManager**: check
once with `nmcli -v`. If it's missing, the Settings screen says so. A static IP / gateway / DNS
is optional (blank = automatic). NetworkManager remembers the network for the next boot.

Settings → Restart restarts the firmware and GUI services (a few seconds), for when something's stuck.

### Sensor radio (Nano Every + nRF24)

The sensors aren't wired to the BeaglePlay: they connect to a **sensor node** (a Nano Every with an
nRF24L01+ PA/LNA radio, on battery). It sends every sensor change to a **base station** (a second
Nano Every + radio) on USB to the BeaglePlay. The light tree is still on the BeaglePlay's GPIO.

```
sensor node --2.4 GHz--> base station --USB--> BeaglePlay  (/dev/ruff-radio)
```

**Wiring, the same on both Nanos:** radio on SPI (D11 MOSI, D12 MISO, D13 SCK), CE on D9 and
CSN on D10. On the sensor node, IRQ also goes to D8. Power the PA/LNA module from the socket adapter
board's own 3.3 V regulator, not the Nano's 3V3 pin: it draws over 100 mA when sending, and a
module that resets or loses packets is almost always under-powered.

**Sensor node inputs** (0-5 V only; a 12 V sensor output needs an optocoupler or a divider):

| Pin | Lane 1  | Pin | Lane 2  |
|-----|---------|-----|---------|
| D2  | BACK    | D5  | BACK    |
| D3  | FORWARD | D6  | FORWARD |
| D4  | ALIGN   | D7  | ALIGN   |

Active-high by default; change `ACTIVE_HIGH` / `INPUT_MODE` in `nano/src/sensor_node/main.cpp` to
match the sensors. Lane 2 is off in `LANE_ENABLED` (`firmware/src/sensors/SensorEvents.hpp`) until
it's wired.

**Flashing** ([PlatformIO](https://platformio.org/install/cli), on a laptop, one Nano on USB at a time):

```bash
cd nano
pio run -e sensor_node  -t upload    # the Nano in the sensor unit
pio run -e base_station -t upload    # the Nano that plugs into the BeaglePlay
pio device monitor                   # sensor node only: prints every edge, for bench testing
```

**Range test first.** Flash `pio run -e radio_test -t upload` on both Nanos. Jumper D2 to GND on
the far one and run it on battery. Watch the near one with `pio device monitor` while you walk the
far one round the ring. Losses of a few % are fine (the node resends, so nothing is lost, only
late), but if losses are steady or there are more than ~3 retries per packet, move the base station
or raise the antennas.

**Checking the link on the board:**

```bash
sudo systemctl stop firmware
./build/firmware/radio_monitor       # every sensor change and pass, + a status line every 2 s
sudo systemctl start firmware
```

`ONLINE` needs the base station plugged in, the sensor node on, and about 3 s for the clocks to
sync. When the link is down, the GUI shows **Sensors offline** and a heat can't start. If it drops
during a heat, the heat goes on: the node keeps every edge and resends it with its original time,
so the passes appear late but timed correctly.

**Accuracy.** The node timestamps each edge itself. The BeaglePlay tracks the node's and the base
station's clocks (offset and drift: the Nano Every has no crystal), so a sensor time is accurate to
well under 1 ms in the simulations, and splits within one lane are exact. There is a small fixed
delay on the USB link (about 0.3-1 ms). To measure it, wire one push button to both a sensor node
input and a spare BeaglePlay GPIO, press it ~100 times, and compare the times. Put the average
difference (BeaglePlay minus radio, in microseconds) in `RUFF_RADIO_OFFSET_US` in
`/etc/ruff/firmware.env`.

`RUFF_SENSORS=gpio` in `/etc/ruff/firmware.env` switches back to sensors wired straight to the
BeaglePlay (pins in `firmware/src/sensors/GpioSensors.cpp`).

### Supabase upload (optional)

Without this, races are only saved to `data/UFLI_races.csv`. To turn on uploading:

```bash
sudo nano /etc/ruff/firmware.env      # fill in SUPABASE_URL, SUPABASE_KEY (anon key), BOARD_ID
sudo systemctl restart firmware
```

See [docs/supabase-schema.md](docs/supabase-schema.md).

### Watching / driving the board from a laptop GUI

The firmware and GUI talk over UDP (`common/include/common/protocol.h`): status to the GUI on
port 5555, commands to the firmware on 5556. On the board both sides use 127.0.0.1. To use a
GUI on a laptop instead:

```bash
# on the board: also send status to the laptop
echo "GUI_HOST=<laptop-ip>" | sudo tee -a /etc/ruff/firmware.env && sudo systemctl restart firmware
# on the laptop: send commands to the board
RUFF_BOARD_HOST=<board-ip> ./build/gui/race_gui.app/Contents/MacOS/race_gui
```

Build the GUI and firmware from the same commit: the GUI ignores status packets from a
different protocol version (and logs it once).

### Updating the board after pushing changes

```bash
cd ~/RuffFirmware
./scripts/update.sh      # git pull, rebuild, reinstall service files, restart both
```

`update.sh` doesn't install packages. If a pull adds one to `setup.sh` (like the on-screen
keyboard, `qml6-module-qtquick-virtualkeyboard qt6-virtualkeyboard-plugin`), install it by hand
or re-run `./scripts/setup.sh`. The same goes for the `/dev/ruff-radio` udev rule: re-running
`setup.sh` adds it (it leaves an existing `/etc/ruff/firmware.env` alone; without `RUFF_SENSORS`
in it the firmware uses the radio).

To rebuild local edits without pulling, run `./scripts/build.sh && sudo systemctl restart firmware gui`.

### Checking on it

```bash
systemctl status firmware gui         # running? crashed?
journalctl -u firmware -f             # live firmware log (Ctrl+C to quit)
journalctl -u gui -f                  # live GUI log
sudo systemctl restart firmware       # or: stop / start
```

Both services restart on their own if they crash.

### Running things by hand (debugging)

Stop the service first, otherwise two copies fight over the GPIO pins and the display:

```bash
sudo systemctl stop firmware gui

# firmware: needs sudo for /dev/gpiochip*; load the Supabase env if you want uploads
sudo bash -c 'set -a; source /etc/ruff/firmware.env; set +a; ./build/firmware/firmware'

# light tree wiring check: each light on for 0.6 s, its name printed
sudo ./build/firmware/tree_test

# GUI straight on the screen (no desktop)
QT_QPA_PLATFORM=eglfs ./build/gui/gui
#   blank/black screen? try:  QT_QPA_PLATFORM=linuxfb ./build/gui/gui

sudo systemctl start firmware gui     # put things back when done
```

---

## Race data
Races are saved to `data/UFLI_races.csv` on the board (backup) and uploaded to Supabase.
See [docs/supabase-schema.md](docs/supabase-schema.md) for the table and board config.
