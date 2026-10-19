#!/usr/bin/env bash
# One-time board setup: install deps, build, install + start the services.
# Needs Debian 13 (trixie) or newer: the code uses the libgpiod v2 C++ API and Qt >= 6.5.
set -euo pipefail
cd "$(dirname "$0")/.."

sudo apt update
sudo apt install -y \
    build-essential cmake git pkg-config \
    libgpiod-dev gpiod \
    libcurl4-openssl-dev nlohmann-json3-dev \
    qt6-base-dev qt6-declarative-dev qt6-qpa-plugins \
    qml6-module-qtquick qml6-module-qtquick-controls qml6-module-qtquick-layouts \
    qml6-module-qtquick-templates qml6-module-qtqml-workerscript

# Supabase settings (optional; without this file the firmware saves to CSV only)
if [ ! -f /etc/ruff/firmware.env ]; then
    sudo mkdir -p /etc/ruff
    sudo tee /etc/ruff/firmware.env >/dev/null <<'ENV'
# Fill these in, then: sudo systemctl restart firmware
SUPABASE_URL=
SUPABASE_KEY=
BOARD_ID=
ENV
    sudo chmod 600 /etc/ruff/firmware.env
    echo "Edit /etc/ruff/firmware.env to enable Supabase upload"
fi

# Build before enabling the services, otherwise they start with no binaries
./scripts/build.sh

sudo cp systemd/*.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now firmware gui
