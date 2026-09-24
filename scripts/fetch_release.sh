#!/bin/bash
# Installs a prebuilt device_discoveryd from GitHub Releases into bin/,
# the alternative to compiling it on the device with scripts/build.sh.
#
# Called by build.sh when the machine was set up with `install.sh --prebuilt`,
# so the BlocksScreen updater hook works unchanged in either mode.
#
# Usage: scripts/fetch_release.sh [TAG]
#   TAG  release to install, e.g. v0.1.0. Default: the tag at HEAD if HEAD is
#        exactly on one (binary matches the checked-out source), otherwise
#        the latest release.
#
# Environment: DD_GH_REPO (default BlocksTechnology/DeviceDiscovery),
#              DD_RELEASE_BASE_URL (download from here instead of GitHub).
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
GH_REPO="${DD_GH_REPO:-BlocksTechnology/DeviceDiscovery}"

arch="$(dpkg --print-architecture)"
case "$arch" in
    amd64|arm64) ;;
    *)
        echo "[DeviceDiscovery] no prebuilt daemon for $arch, use scripts/build.sh" >&2
        exit 1
        ;;
esac

tag="${1:-$(git -C "$REPO" describe --tags --exact-match 2>/dev/null || true)}"
if [[ -n "$tag" ]]; then
    if grep -qx "release $tag .*" "$REPO/bin/VERSION" 2>/dev/null; then
        echo "[DeviceDiscovery] release $tag already installed"
        exit 0
    fi
    base="https://github.com/$GH_REPO/releases/download/$tag"
else
    base="https://github.com/$GH_REPO/releases/latest/download"
fi
# Override for mirrors and testing: a directory URL holding the assets.
base="${DD_RELEASE_BASE_URL:-$base}"

name="device_discoveryd-bookworm-$arch"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

# The updater kills hooks after 60 s; keep the downloads well inside that.
curl -fsSL --retry 2 --max-time 20 -o "$tmp/$name.tar.gz" "$base/$name.tar.gz"
curl -fsSL --retry 2 --max-time 10 -o "$tmp/$name.tar.gz.sha256" "$base/$name.tar.gz.sha256"
(cd "$tmp" && sha256sum --check --quiet "$name.tar.gz.sha256")
tar xzf "$tmp/$name.tar.gz" -C "$tmp"

# Replace via rename so a running daemon keeps its (old) inode and a crash
# mid-copy never leaves a half-written binary for the service to start.
mkdir -p "$REPO/bin"
install -m 0755 "$tmp/$name/device_discoveryd" "$REPO/bin/device_discoveryd.new"
mv -f "$REPO/bin/device_discoveryd.new" "$REPO/bin/device_discoveryd"
echo "release $(cat "$tmp/$name/VERSION")" > "$REPO/bin/VERSION"

echo "[DeviceDiscovery] installed $(cat "$REPO/bin/VERSION") into $REPO/bin/device_discoveryd"
