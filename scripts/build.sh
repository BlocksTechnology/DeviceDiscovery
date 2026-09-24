#!/bin/bash
# Incremental, daemon-only build into build/, installed into bin/ (where
# the systemd unit runs it from).
#
# Called by BlocksScreen's updater hook (updater/hooks/DeviceDiscovery.sh)
# after every git update of this repo, and by install.sh. The updater kills
# hooks after 60 s, so this deliberately skips the pybind11 module (slowest
# target) and relies on build/ being kept between updates so only changed
# files recompile.
#
# On machines set up with `install.sh --prebuilt` it downloads the release
# binary instead (scripts/fetch_release.sh), so the hook works in both modes.
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO"

if [[ "$(cat .install-mode 2>/dev/null)" == "prebuilt" ]]; then
    exec scripts/fetch_release.sh
fi

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DDD_BUILD_PYTHON=OFF >/dev/null
cmake --build build --target device_discoveryd -j"$(nproc)"

# Replace via rename: a running daemon keeps its old inode.
mkdir -p bin
install -m 0755 build/device_discoveryd bin/device_discoveryd.new
mv -f bin/device_discoveryd.new bin/device_discoveryd
echo "source $(git rev-parse --short HEAD 2>/dev/null || echo unknown)" > bin/VERSION

echo "[DeviceDiscovery] built $REPO/bin/device_discoveryd ($(cat bin/VERSION))"
