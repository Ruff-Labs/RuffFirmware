#!/usr/bin/env bash
# Pull latest code, rebuild, reinstall service files, restart.
set -euo pipefail
cd "$(dirname "$0")/.."
git pull --ff-only
./scripts/build.sh

# Pick up any changes to the unit files
sudo cp systemd/*.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl restart firmware gui
