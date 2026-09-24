# device_discoveryd socket protocol

This is the contract between `device_discoveryd` and its clients, such as BlocksScreen's `devices/discovery/client.py`. The two live in separate repos, so any incompatible change here must bump `proto`.

For how the daemon and BlocksScreen fit around this protocol, see the architecture diagrams: <https://claude.ai/artifact/8n51G5sNt9RdY5WbqzeBue>.

## Transport

- Unix stream socket. Path, in order of precedence:
  1. `--socket PATH` argument
  2. `$DEVICE_DISCOVERY_SOCKET`
  3. `$XDG_RUNTIME_DIR/blockscreen/device_discovery.sock`
  4. `/tmp/blockscreen/device_discovery.sock`

  Under systemd (`systemd/device-discoveryd.service`), the path is `/run/device-discovery/device_discovery.sock`.
- Server-to-client only. Anything a client sends is read and discarded.
- Newline-delimited JSON: one object per line, UTF-8.
- A client that stops reading and lets 1 MiB queue up is disconnected.

## Messages

Every message has a `type`. On connect, the server sends `hello` and then `snapshot`. After that it sends `added` or `removed` whenever the device set changes.

```json
{"type":"hello","proto":1,"pid":1234}
{"type":"snapshot","devices":[Device, ...]}
{"type":"added","device":Device}
{"type":"removed","device":Device}
```

- **`proto`**: protocol version, currently `1`. Clients should refuse or warn on a version they don't know.
- **`snapshot`**: every device currently connected. The baseline for later `added`/`removed` messages.
- **`added`/`removed`**: sent after udev activity settles (200 ms debounce), so one physical plug or unplug produces one set of changes.

Device identity, used to tell whether a device was added or removed:
- **Serial devices:** `symlink_name`.
- **USB devices:** `usb_bus_number` plus `usb_device_address`. This is unique among devices connected at the same time, but not stable across replugs.

## Device object

| Field | Type | Notes |
|---|---|---|
| `name` | str | Display name |
| `manufacturer`, `product`, `serial_number` | str | Serial devices: parsed from the by-id name. USB devices: string descriptors, or empty if the device couldn't be opened. |
| `symlink_name` | str | Serial only: file name under `/dev/serial/by-id/` |
| `device_path` | str | Serial only: resolved node, e.g. `/dev/ttyACM0` |
| `mcu_type` | str | Serial Klipper/Katapult only |
| `interface` | str | Serial only: USB interface number from `-ifNN` |
| `video_path` | str | USB video only: `/dev/videoN` |
| `can_uuid`, `can_interface` | str | Reserved for CAN, always empty for now |
| `connection` | str | `Serial`, `USB`, `CAN`, `Unknown` |
| `firmware` | str | `Klipper`, `Katapult`, `Unflashed`, `Unknown` |
| `usb_class` | str | `Video`, `Audio`, `HID`, `MassStorage`, `CDCSerial`, `Hub`, `VendorSpecific`, `Unknown` |
| `vendor_id`, `product_id` | int | USB only; `0` for serial entries |
| `vid_hex`, `pid_hex` | str | e.g. `"0x1d50"` |
| `usb_bus_number`, `usb_device_address` | int | USB only |
| `is_klipper`, `is_katapult` | bool | Derived from `firmware` |

A physical USB-serial board currently appears twice, once as `Serial` and once as `USB`, with nothing linking the two entries. See ANALYSIS.md, Problem 4.

## Versioning

- **Adding** a field or a message type: no bump. Clients must ignore what they don't know.
- **Renaming, removing or changing the meaning** of a field or message: bump `kProtocolVersion` in `src/discoveryd_main.cpp` and update the client's supported version.
