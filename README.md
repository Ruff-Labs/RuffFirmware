# RuffLabs Source Code

Board firmware and touchscreen GUI. Part of the [Ruff-Labs](https://github.com/Ruff-Labs) GitHub
org; the website that shows race results is a separate repo there.

| Folder      | What it is                                              | Builds on            |
|-------------|---------------------------------------------------------|----------------------|
| `firmware/` | Sensors, light tree, remote, timer, race DB (`firmware`, `tree_test`) | BeaglePlay only (needs libgpiod v2) |
| `gui/`      | Qt 6 / QML touchscreen app                              | Mac, Windows, board  |
| `common/`   | Shared headers (`protocol.h`)                           | everywhere           |
| `scripts/`  | `setup.sh`, `build.sh`, `update.sh` for the board        | board                |
| `systemd/`  | Services that start firmware + GUI on boot              | board                |

Everything builds from the **top-level** `CMakeLists.txt`. On Mac/Windows CMake prints
`libgpiodcxx not found, skipping firmware and tree_test`. That's expected; only the GUI gets built.

---

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

> The systemd services expect the repo at **`/home/debian/project`**. Clone it there or the
> services won't find the binaries.

### First-time setup

```bash
ssh debian@<board-ip>
git clone https://github.com/Ruff-Labs/<this-repo>.git /home/debian/project
cd /home/debian/project
./scripts/setup.sh
```

`setup.sh` does everything once:
1. `apt install`s the compiler, CMake, libgpiod, libcurl, and Qt/QML packages
2. creates an empty `/etc/ruff/firmware.env` (Supabase settings, see below)
3. builds into `build/` (`scripts/build.sh`)
4. installs `systemd/*.service` and starts **firmware** and **gui**, which also start on every boot

### Supabase upload (optional)

Without this, races are only saved to `data/UFLI_races.csv`. To turn on uploading:

```bash
sudo nano /etc/ruff/firmware.env      # fill in SUPABASE_URL, SUPABASE_KEY (anon key), BOARD_ID
sudo systemctl restart firmware
```

See [docs/supabase-schema.md](docs/supabase-schema.md).

### Updating the board after pushing changes

```bash
cd /home/debian/project
./scripts/update.sh      # git pull, rebuild, reinstall service files, restart both
```

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

# light tree test program
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
