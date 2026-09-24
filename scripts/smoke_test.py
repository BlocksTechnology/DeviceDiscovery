#!/usr/bin/env python3
"""Start device_discoveryd, check the handshake from docs/PROTOCOL.md, stop it.

There are no unit tests, so CI uses this to catch a daemon that crashes on
startup, breaks the hello/snapshot handshake, or doesn't shut down cleanly.
It needs no real hardware: an empty snapshot is fine.

Usage: scripts/smoke_test.py [path/to/device_discoveryd]
"""
import json
import os
import signal
import socket
import subprocess
import sys
import tempfile
import time

EXPECTED_PROTO = 1
DEVICE_FIELDS = {
    "name", "manufacturer", "product", "serial_number", "symlink_name",
    "device_path", "mcu_type", "interface", "video_path", "can_uuid",
    "can_interface", "connection", "firmware", "usb_class", "vendor_id",
    "product_id", "vid_hex", "pid_hex", "usb_bus_number",
    "usb_device_address", "is_klipper", "is_katapult",
}


def fail(msg):
    print(f"FAIL: {msg}", file=sys.stderr)
    sys.exit(1)


def connect(path, timeout=5.0):
    deadline = time.monotonic() + timeout
    while True:
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        try:
            s.connect(path)
            return s
        except (FileNotFoundError, ConnectionRefusedError):
            s.close()
            if time.monotonic() > deadline:
                fail(f"daemon never opened {path}")
            time.sleep(0.05)


def read_lines(sock, count, timeout=5.0):
    sock.settimeout(timeout)
    buf = b""
    while buf.count(b"\n") < count:
        chunk = sock.recv(65536)
        if not chunk:
            got = buf.count(b"\n")
            fail(f"socket closed after {got} of {count} lines")
        buf += chunk
    return [json.loads(line) for line in buf.split(b"\n")[:count]]


def main():
    daemon = sys.argv[1] if len(sys.argv) > 1 else "build/device_discoveryd"
    with tempfile.TemporaryDirectory() as tmp:
        sock_path = os.path.join(tmp, "dd.sock")
        proc = subprocess.Popen([daemon, "--socket", sock_path])
        try:
            with connect(sock_path) as s:
                hello, snapshot = read_lines(s, 2)
        finally:
            if proc.poll() is None:
                proc.send_signal(signal.SIGTERM)
            code = proc.wait(timeout=5)

        if hello.get("type") != "hello":
            fail(f"first message is not hello: {hello}")
        if hello.get("proto") != EXPECTED_PROTO:
            fail(f"proto {hello.get('proto')}, expected {EXPECTED_PROTO} "
                 "(bumped kProtocolVersion? update this test and PROTOCOL.md)")
        if snapshot.get("type") != "snapshot" or not isinstance(snapshot.get("devices"), list):
            fail(f"second message is not a snapshot: {snapshot}")
        for d in snapshot["devices"]:
            missing = DEVICE_FIELDS - d.keys()
            if missing:
                fail(f"device {d.get('name')!r} missing fields {sorted(missing)}")
        if code != 0:
            fail(f"daemon exited with {code} on SIGTERM")
        if os.path.exists(sock_path):
            fail("socket file left behind after shutdown")

    print(f"OK: proto {hello['proto']}, {len(snapshot['devices'])} device(s) in snapshot")


if __name__ == "__main__":
    main()
