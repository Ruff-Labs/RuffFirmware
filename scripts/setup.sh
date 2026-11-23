#!/usr/bin/env bash
# One-time board setup: install deps, build, install + start the services.
# Needs Debian 13 (trixie) or newer: the code uses the libgpiod v2 C++ API and Qt >= 6.5.
set -euo pipefail
cd "$(dirname "$0")/.."

sudo apt update
sudo apt install -y \
    build-essential cmake git pkg-config \
    libgpiod-dev gpiod \
    libcurl4-openssl-dev \
    qt6-base-dev qt6-declarative-dev qt6-qpa-plugins \
    qml6-module-qtquick qml6-module-qtquick-controls qml6-module-qtquick-layouts \
    qml6-module-qtquick-templates qml6-module-qtqml-workerscript \
    qml6-module-qtquick-virtualkeyboard qt6-virtualkeyboard-plugin

# Supabase settings (optional; without this file the firmware saves to CSV only)
if [ ! -f /etc/ruff/firmware.env ]; then
    sudo mkdir -p /etc/ruff
    sudo tee /etc/ruff/firmware.env >/dev/null <<'ENV'
# Fill these in, then: sudo systemctl restart firmware
SUPABASE_URL=
SUPABASE_KEY=
BOARD_ID=
# Optional: also send race status to a GUI running on a laptop (its IPv4 address)
GUI_HOST=
# Sensors: radio (sensor node via the base station on USB) or gpio (wired to the BeaglePlay)
RUFF_SENSORS=radio
RUFF_RADIO_DEV=/dev/ruff-radio
# Microseconds added to every radio sensor time, from the accuracy test in the README
RUFF_RADIO_OFFSET_US=0
ENV
    sudo chmod 600 /etc/ruff/firmware.env
    echo "Edit /etc/ruff/firmware.env to enable Supabase upload"
fi

# The base station (an Arduino Nano Every on USB) always appears as /dev/ruff-radio,
# whatever ttyACM number it gets. Plug in only the base station: the sensor node runs on battery.
sudo tee /etc/udev/rules.d/99-ruff-radio.rules >/dev/null <<'UDEV'
SUBSYSTEM=="tty", ATTRS{idVendor}=="2341", ATTRS{idProduct}=="0058", SYMLINK+="ruff-radio"
UDEV
sudo udevadm control --reload-rules
sudo udevadm trigger --subsystem-match=tty

# Build before enabling the services, otherwise they start with no binaries
./scripts/build.sh

# Install the unit files pointed at wherever this repo is cloned
# (they say /home/debian/project as a placeholder)
REPO="$(pwd)"
for f in systemd/*.service; do
    sed "s|/home/debian/project|$REPO|g" "$f" | sudo tee "/etc/systemd/system/$(basename "$f")" >/dev/null
done
sudo systemctl daemon-reload
sudo systemctl enable --now firmware gui
