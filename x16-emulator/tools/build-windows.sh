#!/usr/bin/env bash
# Cross-compiles x16emu.exe for 64-bit Windows from Linux, using mingw-w64.
# Output: build-windows/x16emu.exe, plus the SDL2.dll it needs alongside it
# (copied into the same directory for convenience).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_ROOT/build-windows"

if ! command -v x86_64-w64-mingw32-gcc >/dev/null 2>&1; then
    echo "mingw-w64 not found. Install it, e.g.:" >&2
    echo "  sudo apt install g++-mingw-w64-x86-64" >&2
    exit 1
fi

"$SCRIPT_DIR/fetch-windows-deps.sh"

cmake -S "$PROJECT_ROOT" -B "$BUILD_DIR" \
    -DCMAKE_TOOLCHAIN_FILE="$PROJECT_ROOT/cmake/mingw-w64-toolchain.cmake" \
    -DCMAKE_BUILD_TYPE=Release \
    -DENABLE_FLUIDSYNTH=OFF \
    -DENABLE_TRACE=OFF

cmake --build "$BUILD_DIR" -j"$(nproc)"

# Runtime DLLs the .exe needs next to it that aren't part of a stock Windows
# install: SDL2 itself, zlib (dynamically linked), and mingw's winpthread
# (needed because esp32wifi.c uses pthread mutexes).
cp "$PROJECT_ROOT/.crossdeps/SDL2-mingw/x86_64-w64-mingw32/bin/SDL2.dll" "$BUILD_DIR/"
cp "$PROJECT_ROOT/.crossdeps/zlib-mingw/usr/x86_64-w64-mingw32/lib/zlib1.dll" "$BUILD_DIR/"
cp "/usr/x86_64-w64-mingw32/lib/libwinpthread-1.dll" "$BUILD_DIR/"

# Generic "double-click to play" launcher stub used by bundle-x16-app.sh.
# GUI subsystem (-mwindows) so no console window flashes on launch;
# -municode so the mingw CRT calls our wWinMain with proper wide-char argv.
echo "Building launcher.exe stub..."
x86_64-w64-mingw32-gcc-posix -O2 -municode -mwindows \
    -o "$BUILD_DIR/launcher.exe" "$SCRIPT_DIR/launcher_win32.c"

echo
echo "Windows build ready: $BUILD_DIR/x16emu.exe (+ SDL2.dll alongside it)"
file "$BUILD_DIR/x16emu.exe" 2>/dev/null || true
