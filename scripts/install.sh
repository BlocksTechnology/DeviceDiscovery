#!/bin/bash
# One-time machine setup (needs sudo): dependencies, first daemon install,
# systemd unit. After this, updates arrive through the BlocksScreen updater,
# which runs scripts/build.sh and restarts the service - no sudo needed.
#
# Usage: scripts/install.sh [--prebuilt] [--usb-access]
#   --prebuilt     download release binaries from GitHub instead of compiling
#                  on this machine (amd64/arm64 only). Remembered in
#                  .install-mode, so later updates do the same. Run without
#                  it to switch back to building from source.
#   --usb-access   also install udev/70-device-discovery-usb.rules
#                  (read the trade-off in that file first)
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
UNIT="device-discoveryd.service"
EXPECTED_REPO="/home/blocks/DeviceDiscovery"

prebuilt=0
usb_access=0
for arg in "$@"; do
    case "$arg" in
        --prebuilt) prebuilt=1 ;;
        --usb-access) usb_access=1 ;;
        *) echo "[DeviceDiscovery] unknown option: $arg" >&2; exit 2 ;;
    esac
done

if [[ "$REPO" != "$EXPECTED_REPO" ]]; then
    echo "[DeviceDiscovery] $UNIT expects the repo at $EXPECTED_REPO, found $REPO" >&2
    echo "[DeviceDiscovery] clone it there, or edit ExecStart in systemd/$UNIT" >&2
    exit 1
fi

if (( prebuilt )); then
    sudo apt-get install -y --no-install-recommends \
        curl ca-certificates libusb-1.0-0 libudev1
    echo prebuilt > "$REPO/.install-mode"
else
    sudo apt-get install -y --no-install-recommends \
        build-essential cmake pkg-config \
        libusb-1.0-0-dev libudev-dev nlohmann-json3-dev
    rm -f "$REPO/.install-mode"
fi

"$REPO/scripts/build.sh"

# `link` keeps the unit file in the repo, so unit changes arrive with
# git updates and only need a daemon-reload.
sudo systemctl link "$REPO/systemd/$UNIT"
sudo systemctl daemon-reload
sudo systemctl enable "$UNIT"
sudo systemctl restart "$UNIT"

if (( usb_access )); then
    sudo install -m 0644 "$REPO/udev/70-device-discovery-usb.rules" /etc/udev/rules.d/
    sudo udevadm control --reload-rules
    sudo udevadm trigger --subsystem-match=usb
fi

systemctl --no-pager status "$UNIT" | head -5
