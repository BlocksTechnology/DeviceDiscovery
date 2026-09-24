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
scripts/fetch_release.sh  download the prebuilt daemon from GitHub Releases
docs/PROTOCOL.md      socket protocol: the contract with BlocksScreen
docs/WORKFLOW.md      CI/CD: what GitHub Actions checks, how releases work
scripts/smoke_test.py daemon handshake check (used by CI)
.github/workflows/    CI and release workflows
ANALYSIS.md           code analysis and known problems
```

## Install on a printer (one time)

```bash
git clone https://github.com/BlocksTechnology/DeviceDiscovery.git ~/DeviceDiscovery
~/DeviceDiscovery/scripts/install.sh              # build from source on the printer
~/DeviceDiscovery/scripts/install.sh --prebuilt   # or: download the release binary (amd64/arm64)
```

Add `--usb-access` to either command to also install the udev rule.

There are two install modes:

| Mode | Installs | How the daemon gets onto the printer |
|---|---|---|
| **Source** (default) | build deps (`cmake`, `libusb-1.0-0-dev`, `libudev-dev`, `nlohmann-json3-dev`) | `scripts/build.sh` compiles it |
| **Prebuilt** (`--prebuilt`) | runtime libs and `curl` only, no compiler | `scripts/fetch_release.sh` downloads it from [GitHub Releases](https://github.com/BlocksTechnology/DeviceDiscovery/releases) and checks its sha256 |

Both modes put the daemon in `bin/device_discoveryd` and record where it came from in `bin/VERSION` (`source <commit>` or `release <tag> <commit>`). The installer then links and enables `device-discoveryd.service`. The unit expects the repo at `/home/blocks/DeviceDiscovery` and runs as `blocks:blocksscreen`.

The chosen mode is saved in `.install-mode`. To switch, run `install.sh` again with or without `--prebuilt`.

## Updates

The BlocksScreen updater manages this repo as the `DeviceDiscovery` component (see BlocksScreen's `updater/components.yaml`). After each git update, BlocksScreen's `updater/hooks/DeviceDiscovery.sh` runs `scripts/build.sh`, and then the updater restarts `device-discoveryd.service`. No sudo is needed after the first install.

`scripts/build.sh` follows the install mode:

- **Source:** compiles the daemon. It must finish within the updater's 60 s hook timeout. That's why it builds only the daemon, and why it keeps `build/` between updates so only changed files recompile.
- **Prebuilt:** runs `scripts/fetch_release.sh`. If HEAD is exactly on a release tag, it installs that release, and skips the download if it's already installed. Otherwise it installs the latest release, which can be older than the checked-out source.

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

CI runs on every pull request; see [docs/WORKFLOW.md](docs/WORKFLOW.md) for what it checks and how to run the same checks locally.
