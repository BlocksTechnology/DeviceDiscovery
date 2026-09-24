#!/bin/bash
# Incremental, daemon-only build into build/.
#
# Called by BlocksScreen's updater hook (updater/hooks/DeviceDiscovery.sh)
# after every git update of this repo, and by install.sh. The updater kills
# hooks after 60 s, so this deliberately skips the pybind11 module (slowest
# target) and relies on build/ being kept between updates so only changed
# files recompile.
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO"

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DDD_BUILD_PYTHON=OFF >/dev/null
cmake --build build --target device_discoveryd -j"$(nproc)"

echo "[DeviceDiscovery] built $REPO/build/device_discoveryd"
