#!/usr/bin/env bash
# Pull latest code, rebuild, reinstall service files, restart.
set -euo pipefail
cd "$(dirname "$0")/.."
git pull --ff-only
./scripts/build.sh

# Reinstall the unit files (picks up changes), pointed at wherever this repo is cloned
# (they say /home/debian/project as a placeholder)
REPO="$(pwd)"
for f in systemd/*.service; do
    sed "s|/home/debian/project|$REPO|g" "$f" | sudo tee "/etc/systemd/system/$(basename "$f")" >/dev/null
done
sudo systemctl daemon-reload
sudo systemctl restart firmware gui
