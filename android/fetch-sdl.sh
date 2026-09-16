#!/usr/bin/env bash
# Fetches SDL2's source (including its official Android build support) into
# app/jni/SDL/, where the Android project's CMake build expects to find it.
# Not committed to git — same reasoning as x16-emulator/tools/fetch-windows-deps.sh:
# keep the repo itself small, fetch third-party source on demand.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SDL_VERSION="2.30.0"
DEST="$SCRIPT_DIR/app/jni/SDL"

if [[ -f "$DEST/CMakeLists.txt" ]]; then
    echo "SDL2 source already present at $DEST, skipping download."
    exit 0
fi

echo "Downloading SDL2 $SDL_VERSION source..."
tmp_tar="$(mktemp)"
curl -fsSL -o "$tmp_tar" \
    "https://github.com/libsdl-org/SDL/releases/download/release-${SDL_VERSION}/SDL2-${SDL_VERSION}.tar.gz"

mkdir -p "$DEST"
tar xzf "$tmp_tar" -C "$DEST" --strip-components=1 "SDL2-${SDL_VERSION}"
rm -f "$tmp_tar"

# Not needed for the Android build; trims a meaningful chunk of size.
rm -rf "$DEST/android-project" "$DEST/test" "$DEST/VisualC" "$DEST/VisualC-GDK" "$DEST/Xcode" "$DEST/Xcode-iOS"

echo "SDL2 source ready at $DEST"
