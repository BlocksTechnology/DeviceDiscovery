# CI/CD workflow

This page covers what GitHub Actions checks on every change, how releases are published, and how both relate to the way printers actually get updates.

Workflows live in `.github/workflows/`:

| Workflow | File | Runs on | Purpose |
|---|---|---|---|
| CI | `ci.yml` | push to `main`, every pull request, manual run | Build every target and check that the daemon works |
| Release | `release.yml` | push of a `v*` tag | Publish prebuilt daemons as GitHub Release assets, then test-install them |

## How printers get updates

On every update, the BlocksScreen updater does a `git pull` of this repo, runs `scripts/build.sh` on the printer, and restarts `device-discoveryd.service` (see the README). What `build.sh` does depends on how the printer was installed:

- **Source mode (default):** it compiles the daemon from the pulled code. So **whatever is on `main` reaches these printers on their next update**. CI is the gate that keeps a broken `main` from reaching them, which is why every pull request should be green before merging.
- **Prebuilt mode (`install.sh --prebuilt`):** it runs `scripts/fetch_release.sh`, which downloads the daemon from GitHub Releases. These printers only get new daemon code when you **publish a release**. If HEAD is exactly on a release tag, that release is installed; otherwise the latest release is.

```
pull request ──► CI ──► merge to main ──► updater on each printer: git pull → scripts/build.sh
                                               ├─ source mode:   compile         ─┐
tag vX.Y.Z ──► Release ──► GitHub Release ─────┴─ prebuilt mode: fetch_release.sh ─┴─► bin/device_discoveryd
                                                                                      → restart service
```

Either way, the daemon ends up in `bin/device_discoveryd`, which is where the systemd unit runs it from. `bin/VERSION` records its origin: `source <commit>` or `release <tag> <commit>`.

## CI (`ci.yml`)

Three jobs run in parallel. A new push to the same branch or PR cancels the previous run.

### `daemon (bookworm amd64 / arm64)`

This job reproduces the on-printer build as closely as possible:

- It runs in a `debian:bookworm` container, the same Debian release as the printers, on both x86-64 and ARM64 runners.
- It installs the same packages as `scripts/install.sh`.
- It runs `scripts/build.sh`, the exact script the updater runs: a Release build of the daemon only.
- It prints the build time. The updater kills `build.sh` after 60 s on a printer. GitHub runners are faster than a Pi, so this time doesn't enforce that limit, but a jump in it is a warning sign.
- It runs `scripts/smoke_test.py` against the installed `bin/device_discoveryd` (see [Smoke test](#smoke-test)).

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
scripts/smoke_test.py bin/device_discoveryd

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
  scripts/build.sh && scripts/smoke_test.py bin/device_discoveryd'
```

## Releases (`release.yml`)

To publish a release:

```bash
git tag v0.1.0
git push origin v0.1.0
```

The workflow has three stages:

1. **build:** runs `scripts/build.sh` in bookworm on amd64 and arm64, smoke-tests the result, and packages it.
2. **publish:** creates the GitHub Release with auto-generated notes.
3. **verify:** on a clean bookworm container per architecture, with runtime libraries only and no compiler, installs the published release with `scripts/fetch_release.sh <tag>`, exactly as a prebuilt printer would, and smoke-tests it.

Each architecture gets two assets:

- `device_discoveryd-bookworm-<arch>.tar.gz`: the stripped daemon, a `VERSION` file (`<tag> <commit>`), `systemd/`, `udev/`, `PROTOCOL.md` and `LICENSE`
- a matching `.sha256` checksum, which `fetch_release.sh` checks before installing

The asset names don't include the version on purpose. That way `https://github.com/BlocksTechnology/DeviceDiscovery/releases/latest/download/<asset>` always points at the newest release.

Keep the tag in step with the version in `pyproject.toml` and `CMakeLists.txt`. The workflows don't check this.

### Installing a release by hand

```bash
scripts/fetch_release.sh            # the tag at HEAD, or the latest release
scripts/fetch_release.sh v0.1.0     # a specific release
sudo systemctl restart device-discoveryd
```

`DD_GH_REPO` points it at a fork. `DD_RELEASE_BASE_URL` downloads from any directory URL holding the assets, for a mirror or for testing.

## Notes

- **ARM64 runners** (`ubuntu-24.04-arm`) are free for public repositories. If the repo is private and the organisation's plan doesn't include them, the arm64 jobs will stay queued. Remove those matrix entries, or switch to a self-hosted runner.
- **Private repo:** `fetch_release.sh` downloads without authentication, so prebuilt mode only works while the repository (and its releases) are public.
- **32-bit ARM** (`armhf`) has no prebuilt binary. On such a printer, `fetch_release.sh` refuses and you need source mode.
- **Branch protection:** to make CI a real gate, require the `CI` checks on `main` under *Settings → Branches*.
