#!/usr/bin/env bash
# Fetches the Windows (mingw-w64) development files x16emu needs to be
# cross-compiled from Linux: SDL2 and zlib. Downloaded once into
# x16-emulator/.crossdeps/ (gitignored) and reused on every future build.
#
# SDL2 comes straight from the upstream project's official mingw devel
# release. zlib comes from Ubuntu's own libz-mingw-w64-dev package, pulled
# with `apt-get download` so nothing gets installed system-wide and no root
# is required.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
CROSSDEPS_DIR="$PROJECT_ROOT/.crossdeps"
SDL2_VERSION="2.30.0"

mkdir -p "$CROSSDEPS_DIR"

# --- SDL2 ---
if [[ -f "$CROSSDEPS_DIR/SDL2-mingw/x86_64-w64-mingw32/lib/cmake/SDL2/sdl2-config.cmake" ]]; then
    echo "SDL2 mingw devel already present, skipping download."
else
    echo "Downloading SDL2 $SDL2_VERSION mingw devel package..."
    tmp_tar="$(mktemp)"
    curl -fsSL -o "$tmp_tar" \
        "https://github.com/libsdl-org/SDL/releases/download/release-${SDL2_VERSION}/SDL2-devel-${SDL2_VERSION}-mingw.tar.gz"
    rm -rf "$CROSSDEPS_DIR/SDL2-mingw"
    mkdir -p "$CROSSDEPS_DIR/SDL2-mingw"
    tar xzf "$tmp_tar" -C "$CROSSDEPS_DIR/SDL2-mingw" --strip-components=1 "SDL2-${SDL2_VERSION}"
    rm -f "$tmp_tar"
    echo "SDL2 mingw devel ready at $CROSSDEPS_DIR/SDL2-mingw"
fi

# --- zlib ---
if [[ -f "$CROSSDEPS_DIR/zlib-mingw/usr/x86_64-w64-mingw32/lib/libz.a" ]]; then
    echo "zlib mingw devel already present, skipping download."
else
    echo "Downloading zlib mingw devel package (via apt-get download, no root)..."
    tmp_dir="$(mktemp -d)"
    (cd "$tmp_dir" && apt-get download libz-mingw-w64 libz-mingw-w64-dev)
    rm -rf "$CROSSDEPS_DIR/zlib-mingw"
    mkdir -p "$CROSSDEPS_DIR/zlib-mingw"
    for deb in "$tmp_dir"/*.deb; do
        dpkg -x "$deb" "$CROSSDEPS_DIR/zlib-mingw"
    done
    rm -rf "$tmp_dir"
    echo "zlib mingw devel ready at $CROSSDEPS_DIR/zlib-mingw"
fi

echo "All Windows cross-compile dependencies are ready."
