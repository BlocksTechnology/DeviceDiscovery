#!/bin/bash
# One-time machine setup (needs sudo): build dependencies, first build,
# systemd unit. After this, updates arrive through the BlocksScreen updater,
# which runs scripts/build.sh and restarts the service - no sudo needed.
#
# Usage: scripts/install.sh [--usb-access]
#   --usb-access   also install udev/70-device-discovery-usb.rules
#                  (read the trade-off in that file first)
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
UNIT="device-discoveryd.service"
EXPECTED_REPO="/home/blocks/DeviceDiscovery"

if [[ "$REPO" != "$EXPECTED_REPO" ]]; then
    echo "[DeviceDiscovery] $UNIT expects the repo at $EXPECTED_REPO, found $REPO" >&2
    echo "[DeviceDiscovery] clone it there, or edit ExecStart in systemd/$UNIT" >&2
    exit 1
fi

sudo apt-get install -y --no-install-recommends \
    build-essential cmake pkg-config \
    libusb-1.0-0-dev libudev-dev nlohmann-json3-dev

"$REPO/scripts/build.sh"

# `link` keeps the unit file in the repo, so unit changes arrive with
# git updates and only need a daemon-reload.
sudo systemctl link "$REPO/systemd/$UNIT"
sudo systemctl enable --now "$UNIT"

if [[ "${1:-}" == "--usb-access" ]]; then
    sudo install -m 0644 "$REPO/udev/70-device-discovery-usb.rules" /etc/udev/rules.d/
    sudo udevadm control --reload-rules
    sudo udevadm trigger --subsystem-match=usb
fi

systemctl --no-pager status "$UNIT" | head -5
