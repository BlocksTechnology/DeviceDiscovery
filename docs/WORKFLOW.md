# CI/CD workflow

This page covers what GitHub Actions checks on every change, how releases are published, and how both relate to the way printers actually get updates.

Workflows live in `.github/workflows/`:

| Workflow | File | Runs on | Purpose |
|---|---|---|---|
| CI | `ci.yml` | push to `main`, every pull request, manual run | Build every target and check that the daemon works |
| Release | `release.yml` | push of a `v*` tag | Publish prebuilt daemons as GitHub Release assets |

## How printers get updates (not through Actions)

Printers don't download anything from GitHub Actions. The BlocksScreen updater does a `git pull` of this repo, runs `scripts/build.sh` on the printer, and restarts `device-discoveryd.service` (see the README). So **whatever is on `main` reaches printers on their next update**. CI is the gate that keeps a broken `main` from reaching them, which is why every pull request should be green before merging.

```
pull request ──► CI ──► merge to main ──► BlocksScreen updater on each printer
                                          git pull → scripts/build.sh → restart service
tag vX.Y.Z ──► Release ──► GitHub Release with prebuilt daemons (optional)
```

## CI (`ci.yml`)

Three jobs run in parallel. A new push to the same branch or PR cancels the previous run.

### `daemon (bookworm amd64 / arm64)`

This job reproduces the on-printer build as closely as possible:

- It runs in a `debian:bookworm` container, the same Debian release as the printers, on both x86-64 and ARM64 runners.
- It installs the same packages as `scripts/install.sh`.
- It runs `scripts/build.sh`, the exact script the updater runs: a Release build of the daemon only.
- It prints the build time. The updater kills `build.sh` after 60 s on a printer. GitHub runners are faster than a Pi, so this time doesn't enforce that limit, but a jump in it is a warning sign.
- It runs `scripts/smoke_test.py` against the built daemon (see [Smoke test](#smoke-test)).

### `full-build`

This job builds every target in Debug on Ubuntu 24.04: the daemon, the `device_discovery` CLI and the pybind11 module. It then runs the CLI once and the smoke test against the Debug daemon. It catches breakage in targets that `build.sh` skips.

### `python 3.11 / 3.12`

This job runs `pip install .` (scikit-build-core) and imports `DeviceDiscovery`, calling `scan_serial()` and `scan_all_usb()`. Python 3.11 is the minimum in `pyproject.toml`.

### Smoke test

There are no unit tests yet, so `scripts/smoke_test.py` is the main behavioural check. It needs no hardware. It:

1. starts `device_discoveryd --socket <temp path>`
2. connects and reads the first two lines
3. checks that they are `hello` with `proto` 1, then a `snapshot` whose devices have every field listed in [PROTOCOL.md](PROTOCOL.md)
4. sends SIGTERM and checks that the daemon exits with 0 and removes its socket file

CI runners have no serial boards and few or no USB devices, so the snapshot is usually empty. Hotplug (`added`/`removed`) is **not** tested, because that needs real udev events.

If you bump `kProtocolVersion` or rename a device field, update `EXPECTED_PROTO` / `DEVICE_FIELDS` in the smoke test along with `docs/PROTOCOL.md`. The test failing there is intended: it's a reminder that BlocksScreen's client has to change too.

## Running the checks locally

```bash
# daemon job
scripts/build.sh
scripts/smoke_test.py build/device_discoveryd

# full-build job
cmake -S . -B build-dev -DCMAKE_BUILD_TYPE=Debug
cmake --build build-dev -j
build-dev/device_discovery
scripts/smoke_test.py build-dev/device_discoveryd

# python job (inside a venv)
pip install .
cd /tmp && python -c "import DeviceDiscovery as dd; print(dd.scan_serial(), dd.scan_all_usb())"
```

To reproduce the bookworm environment exactly:

```bash
git ls-files -co --exclude-standard | tar cf - -T - | docker run --rm -i debian:bookworm bash -c '
  mkdir /src && cd /src && tar xf - &&
  apt-get update && apt-get install -y --no-install-recommends build-essential cmake pkg-config git \
    ca-certificates python3 libusb-1.0-0-dev libudev-dev nlohmann-json3-dev &&
  scripts/build.sh && scripts/smoke_test.py build/device_discoveryd'
```

## Releases (`release.yml`)

To publish a release:

```bash
git tag v0.1.0
git push origin v0.1.0
```

The workflow builds the daemon with `scripts/build.sh` in bookworm on amd64 and arm64, runs the smoke test, and creates a GitHub Release with auto-generated notes. Each architecture gets two assets:

- `device_discoveryd-<tag>-bookworm-<arch>.tar.gz`: the stripped daemon, `systemd/`, `udev/`, `PROTOCOL.md` and `LICENSE`
- a matching `.sha256` checksum file

Releases are optional and nothing installs them automatically. They exist for machines that shouldn't build from source. Note that the systemd unit in the tarball expects the binary at `/home/blocks/DeviceDiscovery/build/device_discoveryd`, so adjust `ExecStart` if you install it elsewhere.

Keep the tag in step with the version in `pyproject.toml` and `CMakeLists.txt`. The workflows don't check this.

## Notes

- **ARM64 runners** (`ubuntu-24.04-arm`) are free for public repositories. If the repo is private and the organisation's plan doesn't include them, the arm64 jobs will stay queued. Remove those matrix entries, or switch to a self-hosted runner.
- **Branch protection:** to make CI a real gate, require the `CI` checks on `main` under *Settings → Branches*.
