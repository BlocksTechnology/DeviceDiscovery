# DeviceDiscovery: Analysis Report

_Analysis date: 2026-09-23, written while the tool still lived in BlocksScreen at `BlocksScreen/tools/DeviceDiscovery/` (before the move to this repo). Since then: the Python client moved to BlocksScreen as `BlocksScreen/devices/discovery/client.py`, Problems 1–3 (firmware detection, `is_klipper`/`is_katapult`, CMake `GIT_TAG`) are fixed, and `pyproject.toml` now requires Python `>=3.11`._

## What it is

DeviceDiscovery is a C++17 tool of about 1.6k lines. It finds hardware attached to the printer's host: serial boards under `/dev/serial/by-id`, USB devices, and (planned) CAN nodes. It can be used in three ways:

1. **Python module** (`DeviceDiscovery`, built with pybind11): imported directly from Python.
2. **Command-line tool** (`device_discovery`): prints what it finds.
3. **Background service** (`device_discoveryd`): watches for plug and unplug events through udev and pushes JSON updates to clients over a Unix socket.

Nothing in BlocksScreen imports it yet. The configurator (`tools/configuration_manager/configurator.py`) still uses the separate pure-Python `tools/serial_scanner.py`.

## Layout

```
                ┌──────────────── discovery_core (static lib) ────────────────┐
                │ SerialScanner   /dev/serial/by-id → parse symlink names     │
                │ USBScanner      libusb → VID/PID, class, string descriptors │
                │ device_labels   enum → string (shared by CLI + JSON)        │
                └───────┬──────────────────────┬──────────────────────┬───────┘
                        │                      │                      │
   pybind11 module "DeviceDiscovery"   CLI "device_discovery"   discovery_daemon (static lib)
   (py_bindings.cpp)                   (main.cpp, prints)       DeviceMonitor  udev netlink + epoll + 200ms debounce
                                                                IpcServer      Unix socket, JSON-lines broadcast
                                                                device_json    Device → nlohmann::json
                                                                        │
                                                         "device_discoveryd" (discoveryd_main.cpp)
                                                                        │  $XDG_RUNTIME_DIR/blockscreen/device_discovery.sock
                                                                        ▼
                                                     BlocksScreen: devices/discovery/client.py → Qt signals
```

## Components

| Piece | File(s) | What it does |
|---|---|---|
| `Scanner<T>` | `include/scanner.hpp` | A shared base class (C++ template pattern, CRTP) that gives every scanner the same `scan()` method. |
| `SerialScanner` | `src/serial_scanner.cpp` | Lists `/dev/serial/by-id/*` and resolves each link to its real device path. Splits the link name into manufacturer, product, serial number and interface. Meant to detect firmware from name keywords (`Klipper`, `Katapult`/`CanBoot`, and `1a86`/`FTDI`/`Silicon_Labs`/`Prolific` meaning unflashed); see Problem 1. |
| `USBScanner` | `src/usb_scanner.cpp` | Lists every USB device except hubs through libusb. Records VID/PID, bus and address, and a device class from its interfaces (Video wins, then CDC serial, then Audio, HID, Storage, Vendor). Opens each device to read the manufacturer, product and serial strings. For cameras, looks up the matching `/dev/videoN` in `/sys/class/video4linux`. |
| `CANScanner` | `src/can_scanner.cpp` | A stub. Lists `can*` and `slcan*` network interfaces; `scan()` prints "not yet implemented". Not part of any build target. |
| `device_labels` | `src/device_labels.cpp` | Turns the `FirmwareState`, `ConnectionType` and `USBClass` enums into strings. The CLI and the JSON output both use it. |
| `DeviceMonitor` | `src/device_monitor.cpp` | A background thread that listens for udev `tty` and `usb` events. Events only mark the device list as changed; once events stop for 200 ms it does one full rescan and compares it with the previous one. Serial devices are matched by by-id link name, USB devices by bus and address. The devices present at startup are the baseline and aren't reported as "added". |
| `IpcServer` | `src/ipc_server.cpp` | A Unix socket server on its own thread that only sends data. `broadcast()` can be called from any thread: it queues the message and wakes the server thread. Each client has an outgoing buffer; a client that lets it grow past 1 MiB is dropped. Anything clients send is read and ignored. |
| `device_discoveryd` | `src/discoveryd_main.cpp` | Takes an initial scan and wires the monitor to the server. On each change it rescans to refresh the snapshot given to newly connecting clients. Stops on SIGINT or SIGTERM and removes the socket file. |
| `DeviceDiscoveryClient` | BlocksScreen `devices/discovery/client.py` | A `QObject` with a reader thread that reconnects with backoff (2, 5, 10, 30 s). Emits `snapshot_ready(list)`, `device_added(dict)`, `device_removed(dict)`, `daemon_connected` and `daemon_disconnected`. Follows the same pattern as `lib/updater_worker.py`. |

## Socket protocol

One JSON object per line, sent to each client:

```
{"type":"hello","pid":1234}                         ← on connect
{"type":"snapshot","devices":[{...}, ...]}          ← on connect
{"type":"added","device":{...}}                     ← on hotplug
{"type":"removed","device":{...}}
```

Each device object has these fields: `name, manufacturer, product, serial_number, symlink_name, device_path, mcu_type, interface, video_path, can_uuid, can_interface, connection, firmware, usb_class, vendor_id, product_id, usb_bus_number, usb_device_address, vid_hex, pid_hex, is_klipper, is_katapult`.

## Build

- CMake plus scikit-build-core (own `pyproject.toml`, package name `BS-Dev-Discovery`).
- Needs `libusb-1.0` and `libudev` through pkg-config. pybind11 and nlohmann_json are downloaded if not installed system-wide.
- Targets: `DeviceDiscovery` (Python module), `device_discoveryd` (daemon), `device_discovery` (command-line tool).
- The install step only installs the Python module, not the daemon or the command-line tool.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

## What was checked by running it

It was built from source and run against fake `/dev/serial/by-id` entries in a private namespace:

- **CLI and Python module:** both run. Link names are split into fields correctly. `repr` gives `<Device [Serial/Unknown] Klipper stm32h723xx → …>`.
- **Daemon:** started, a client connected and received `hello` plus a snapshot of 7 devices (4 serial, 3 real USB, including `Chicony HD Webcam [Video]`). SIGTERM gave a clean shutdown with exit code 0 and the socket file removed.
- **Hotplug:** not tested; it needs real udev events.

## Problems found

### Broken

1. _(fixed: `parseSymlink` now calls `detectFirmware`)_ **Serial firmware is never detected.** `SerialScanner::parseSymlink` never calls `detectFirmware()`, so every serial device comes back `Unknown`. Knock-on effects:
   - `mcu_type` is never filled in.
   - `scan_klipper()` and `scan_katapult()` always return `[]`, and `scan_unflashed()` returns everything.

   Seen: fake devices including `usb-Klipper_stm32h723xx_…` and `usb-Katapult_rp2040_…` all came back `[Unknown]`. The pure-Python `tools/serial_scanner.py` does call its detector, so the two versions already disagree.
2. _(fixed: derived from `firmware`)_ **Wrong keyword checks for `is_klipper`/`is_katapult`** in `parseSymlink`. They test `"Klipper "` (with a trailing space, but link names use underscores) and `"Canboot"` (the detector uses `"CanBoot"`). Neither can ever match. The JSON and Python outputs recompute these from `firmware` anyway.
3. ~~**CMake typo:**~~ _(fixed)_ `GIT TAG V3.1.0` in the pybind11 `FetchContent_Declare` should be `GIT_TAG`. This only matters on machines without pybind11 installed system-wide.

### Design gaps to decide on

4. **One board, two entries.** A Klipper board or Beacon shows up as a Serial device *and* a USB CDC device, with nothing linking them: the serial entry has VID/PID `0x0000` and the USB entry has no `/dev/tty` path. Linking them via sysfs (`/sys/class/tty/ttyACM*/device/../idVendor`) would fix this.
5. **Many Python attributes missing.** The Python `Device` doesn't expose `firmware`, `connection`, `usb_class`, `vendor_id` or `product_id`, only derived helpers (`vid_hex`, `is_klipper`, `is_camera`, …).
6. **USB names depend on permissions.** The scanner has to open each device to read its name strings. When it isn't allowed to, the name falls back to `USB 0xVVVV:0xPPPP` (seen for `0x8087:0x0036`).
7. **Every change is scanned twice.** Each change runs one scan for the comparison (`rescanAndDiff`) and a second in the daemon (`monitor.scanNow()`) to refresh the snapshot. The code comment says the snapshot "can never drift" from the comparison, but because they're separate scans it can. A client connecting between the two scans could receive a device in its snapshot and then again as `added`. Each scan also sets up libusb from scratch and opens every device.
8. **Signals are blocked late.** `discoveryd_main.cpp` blocks SIGINT/SIGTERM only *after* starting its threads. In practice it works because Linux delivers the signal to the thread waiting for it, but a signal arriving before that wait starts would kill the process without cleanup. Block the signals before `monitor.start()` / `server.start()`.
9. **Loose socket permissions.** Without `XDG_RUNTIME_DIR` the socket goes in `/tmp/blockscreen/` with default permissions and a predictable name. Socket paths are also limited to 108 bytes (`sun_path`); longer `XDG_RUNTIME_DIR` values fail with "socket path too long".

### Loose ends

- **Python version mismatch:** the committed `build/` module is compiled for Python 3.12. `pyproject.toml` asks for `>=3.11.15`, while the project is pinned to 3.11.2. As built, the repo's Python can't import it.
- **CAN support is only a stub:** `can_scanner.cpp` isn't built, and its bindings are commented out.
- **Dead code:** `main.cpp` declares an unused `serial_scan()`, `serial_scanner.cpp` ends in a commented-out `scanUsb`, and an unused `Manufacturers` enum is left in `device.hpp`.
- **Python client quirks:**
  - It emits `daemon_disconnected` on every failed retry, even if it never connected.
  - ~~It lives in `python/`, which isn't an importable package.~~ _(fixed: now `BlocksScreen/devices/discovery/client.py`)_
  - Nothing in the app uses it yet.
- **No tests** for any part of this tool.

## Where to start

1. **Fix firmware detection** (Problems 1 and 2). It's a one-line fix plus the keywords, and without it the serial scanner is useless for Klipper boards.
2. **Pick one source of truth.** The configurator uses `tools/serial_scanner.py` while this tool duplicates it in C++. Decide whether the configurator should switch to the pybind module or the daemon.
3. **Link serial and USB entries** (Problem 4) and expose the missing fields in Python (Problem 5) before any UI is built on top.
4. **Build for 3.11** and decide how the daemon gets installed and run (for example as a systemd user service) before wiring `DeviceDiscoveryClient` into the app.
