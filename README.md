# DeviceDiscovery

Finds hardware attached to a Blocks printer's host: serial boards under `/dev/serial/by-id`, USB devices (including cameras), and later CAN nodes. It runs as a background service (`device_discoveryd`) that watches udev for plug and unplug events and reports them to [BlocksScreen](https://github.com/BlocksTechnology/BlocksScreen) over a Unix socket.

**Architecture diagrams** (runtime wiring, hotplug debounce, deployment, migration status): <https://claude.ai/artifact/8n51G5sNt9RdY5WbqzeBue>

Three ways to use it:

| Mode | Target | Used by |
|---|---|---|
| Background service | `device_discoveryd` | BlocksScreen (`BlocksScreen/devices/discovery/client.py`) through [the socket protocol](docs/PROTOCOL.md) |
| Python module | `DeviceDiscovery` (pybind11) | Scripts, tests, one-off scans |
| CLI | `device_discovery` | Debugging: prints one scan |

## Repo layout

```
include/, src/        C++17 sources (scanners, udev monitor, IPC server, daemon)
bindings/             pybind11 module
systemd/              device-discoveryd.service (linked into /etc/systemd/system)
udev/                 optional USB permission rule
scripts/install.sh    one-time machine setup (sudo)
scripts/build.sh      incremental daemon-only build (run on every update)
docs/PROTOCOL.md      socket protocol: the contract with BlocksScreen
ANALYSIS.md           code analysis and known problems
```

## Install on a printer (one time)

```bash
git clone https://github.com/BlocksTechnology/DeviceDiscovery.git ~/DeviceDiscovery
~/DeviceDiscovery/scripts/install.sh            # add --usb-access to also install the udev rule
```

This installs the build dependencies (`cmake`, `libusb-1.0-0-dev`, `libudev-dev`, `nlohmann-json3-dev`), builds the daemon, and links and enables `device-discoveryd.service`. The unit expects the repo at `/home/blocks/DeviceDiscovery` and runs as `blocks:blocksscreen`.

## Updates

The BlocksScreen updater manages this repo as the `DeviceDiscovery` component (see BlocksScreen's `updater/components.yaml`). After each git update, BlocksScreen's `updater/hooks/DeviceDiscovery.sh` runs `scripts/build.sh`, and then the updater restarts `device-discoveryd.service`. No sudo is needed after the first install.

`scripts/build.sh` must finish within the updater's 60 s hook timeout. That's why it builds only the daemon, and why it keeps `build/` between updates so only changed files recompile.

## Development

```bash
# everything, including the Python module
cmake -S . -B build-dev -DCMAKE_BUILD_TYPE=Debug
cmake --build build-dev -j
ln -sf build-dev/compile_commands.json .    # for clangd

# run the daemon without systemd
build-dev/device_discoveryd --socket /tmp/dd.sock

# Python module into the current venv
pip install .
```

Logs under systemd: `journalctl -u device-discoveryd -f`.
