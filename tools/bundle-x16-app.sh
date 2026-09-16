#!/usr/bin/env bash
# bundle-x16-app.sh — turn a built X16 app into a single distributable
# bundle that runs "as native" on Linux and Windows via this repo's
# Wi-Fi-enabled emulator, plus a ready-to-copy package for real X16 hardware
# with a real ESP32 WiFi card.
#
# Usage:
#   tools/bundle-x16-app.sh --name "DESK COMMANDER" --prg DCMAIN.PRG \
#       --sdcard "/path/to/app/dist/sdcard" [--out ~/x16-bundles] [--no-zip] \
#       [--platforms linux,windows]
#
#   --name       Human-readable app name. Used for the bundle folder/zip
#                name and the launcher filenames (e.g. "DESK COMMANDER.exe").
#   --prg        The .PRG file (relative to --sdcard) to boot into with -run.
#                Optional: omit it if the app's sdcard folder has its own
#                AUTOBOOT.X16 and boots itself once the KERNAL sees it.
#   --sdcard     Path to the app's built, ready-to-run files: everything
#                that would normally be copied onto a real X16's SD card
#                (the .PRG, its overlay .BIN files, assets, AUTOBOOT.X16
#                if any).
#   --out        Where to write the bundle. Defaults to ~/x16-bundles.
#   --no-zip     Leave the bundle as a plain folder instead of also zipping it.
#   --platforms  Comma-separated subset of "linux,windows" to actually
#                build (default: both). The real-hardware/ folder is always
#                included either way - it's just a file copy, not a build.
#
# What comes out, under <out>/<slug>/:
#   linux/            x16emu + rom.bin + app files + "<name>.sh"
#   windows/          x16emu.exe + DLLs + rom.bin + app files + "<name>.exe"
#   real-hardware/    just the app files, for copying onto a real X16's SD
#                     card used with a real ESP32 WiFi card — no emulator
#                     involved, this is what actually runs on the physical
#                     machine.
#   README.txt
# ...plus <out>/<slug>.zip, the single file to actually hand someone.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"
EMU_DIR="$REPO_ROOT/x16-emulator"
LINUX_BUILD="$EMU_DIR/build"
WINDOWS_BUILD="$EMU_DIR/build-windows"

NAME=""
PRG=""
SDCARD=""
OUT="$HOME/x16-bundles"
DO_ZIP=1
PLATFORMS="linux,windows"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --name) NAME="$2"; shift 2 ;;
        --prg) PRG="$2"; shift 2 ;;
        --sdcard) SDCARD="$2"; shift 2 ;;
        --out) OUT="$2"; shift 2 ;;
        --no-zip) DO_ZIP=0; shift ;;
        --platforms) PLATFORMS="$2"; shift 2 ;;
        -h|--help) grep '^#' "$0" | sed 's/^# \?//'; exit 0 ;;
        *) echo "Unknown argument: $1" >&2; exit 1 ;;
    esac
done

if [[ -z "$NAME" || -z "$SDCARD" ]]; then
    echo "Usage: $0 --name \"APP NAME\" --sdcard /path/to/dist/sdcard [--prg FILE.PRG] [--out DIR] [--no-zip] [--platforms linux,windows]" >&2
    exit 1
fi

DO_LINUX=0; DO_WINDOWS=0
IFS=',' read -ra _plats <<< "$PLATFORMS"
for _p in "${_plats[@]}"; do
    case "$_p" in
        linux) DO_LINUX=1 ;;
        windows) DO_WINDOWS=1 ;;
        "") ;;
        *) echo "Unknown platform in --platforms: $_p (expected linux and/or windows)" >&2; exit 1 ;;
    esac
done
if [[ "$DO_LINUX" -eq 0 && "$DO_WINDOWS" -eq 0 ]]; then
    echo "--platforms selected nothing to build" >&2
    exit 1
fi
if [[ ! -d "$SDCARD" ]]; then
    echo "--sdcard directory does not exist: $SDCARD" >&2
    exit 1
fi
if [[ -n "$PRG" && ! -f "$SDCARD/$PRG" ]]; then
    echo "--prg file not found inside --sdcard: $SDCARD/$PRG" >&2
    exit 1
fi
if [[ -z "$PRG" && ! -f "$SDCARD/AUTOBOOT.X16" ]]; then
    echo "No --prg given and no AUTOBOOT.X16 in $SDCARD — the app wouldn't know what to run. Pass --prg." >&2
    exit 1
fi

# Filesystem-safe slug for directory/zip names (spaces -> hyphens, upper-cased).
SLUG="$(echo "$NAME" | tr '[:lower:]' '[:upper:]' | tr -s ' ' '-' | tr -cd 'A-Z0-9-')"
if [[ -z "$SLUG" ]]; then
    echo "Could not derive a filesystem-safe name from --name \"$NAME\"" >&2
    exit 1
fi

BUNDLE_DIR="$OUT/$SLUG"

echo "==> Bundling '$NAME' ($SLUG) from $SDCARD"

# --- 1. Make sure the emulator build(s) we need exist ---
if [[ "$DO_LINUX" -eq 1 || "$DO_WINDOWS" -eq 1 ]]; then
    # Windows packaging also borrows rom.bin from the Linux build (the ROM
    # is a platform-independent data file), so it's needed either way.
    if [[ ! -x "$LINUX_BUILD/x16emu" ]]; then
        echo "==> No Linux build found, building it..."
        cmake -S "$EMU_DIR" -B "$LINUX_BUILD" -DCMAKE_BUILD_TYPE=Release
        cmake --build "$LINUX_BUILD" -j"$(nproc)"
    fi
    if [[ ! -f "$LINUX_BUILD/rom.bin" ]]; then
        echo "No ROM at $LINUX_BUILD/rom.bin. Grab one from https://github.com/X16Community/x16-rom/releases and place it there, then re-run." >&2
        exit 1
    fi
fi

if [[ "$DO_WINDOWS" -eq 1 && ( ! -x "$WINDOWS_BUILD/x16emu.exe" || ! -x "$WINDOWS_BUILD/launcher.exe" ) ]]; then
    echo "==> No Windows cross-build found, building it..."
    "$EMU_DIR/tools/build-windows.sh"
fi

# --- 2. Lay out the bundle ---
rm -rf "$BUNDLE_DIR"
mkdir -p "$BUNDLE_DIR/real-hardware"
echo "==> Copying app files..."
cp -a "$SDCARD/." "$BUNDLE_DIR/real-hardware/"

if [[ "$DO_LINUX" -eq 1 ]]; then
    mkdir -p "$BUNDLE_DIR/linux/app"
    cp -a "$SDCARD/." "$BUNDLE_DIR/linux/app/"

    echo "==> Assembling Linux launcher..."
    cp "$LINUX_BUILD/x16emu" "$BUNDLE_DIR/linux/"
    cp "$LINUX_BUILD/rom.bin" "$BUNDLE_DIR/linux/"
    chmod +x "$BUNDLE_DIR/linux/x16emu"

    LINUX_SH="$BUNDLE_DIR/linux/$NAME.sh"
    {
        echo '#!/usr/bin/env bash'
        echo "# $NAME - launches in the Wi-Fi-enabled Commander X16 emulator."
        echo 'set -euo pipefail'
        echo 'SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"'
        echo 'chmod +x "$SCRIPT_DIR/x16emu" 2>/dev/null || true'
        if [[ -n "$PRG" ]]; then
            printf 'exec "$SCRIPT_DIR/x16emu" -rom "$SCRIPT_DIR/rom.bin" -wifi -fsroot "$SCRIPT_DIR/app" -startin "$SCRIPT_DIR/app" -prg "$SCRIPT_DIR/app/%s" -run -rtc -scale 2 "$@"\n' "$PRG"
        else
            echo 'exec "$SCRIPT_DIR/x16emu" -rom "$SCRIPT_DIR/rom.bin" -wifi -fsroot "$SCRIPT_DIR/app" -startin "$SCRIPT_DIR/app" -rtc -scale 2 "$@"'
        fi
    } > "$LINUX_SH"
    chmod +x "$LINUX_SH"
fi

if [[ "$DO_WINDOWS" -eq 1 ]]; then
    mkdir -p "$BUNDLE_DIR/windows/app"
    cp -a "$SDCARD/." "$BUNDLE_DIR/windows/app/"

    echo "==> Assembling Windows launcher..."
    cp "$WINDOWS_BUILD/x16emu.exe" "$BUNDLE_DIR/windows/"
    cp "$WINDOWS_BUILD/SDL2.dll" "$BUNDLE_DIR/windows/"
    cp "$WINDOWS_BUILD/zlib1.dll" "$BUNDLE_DIR/windows/"
    cp "$WINDOWS_BUILD/libwinpthread-1.dll" "$BUNDLE_DIR/windows/"
    cp "$LINUX_BUILD/rom.bin" "$BUNDLE_DIR/windows/rom.bin"
    cp "$WINDOWS_BUILD/launcher.exe" "$BUNDLE_DIR/windows/$NAME.exe"
    if [[ -n "$PRG" ]]; then
        echo "PRG=$PRG" > "$BUNDLE_DIR/windows/launcher.cfg"
    else
        : > "$BUNDLE_DIR/windows/launcher.cfg"
    fi
fi

echo "==> Writing README..."
{
    echo "$NAME — cross-platform bundle"
    printf '=%.0s' $(seq 1 $((${#NAME} + 25))); echo
    echo
    echo "This bundle contains these ways to run $NAME:"
    echo

    if [[ "$DO_LINUX" -eq 1 ]]; then
        cat <<EOF
linux/
    Run "$NAME.sh" (or double-click it, if your file manager runs
    executable .sh files). Self-contained: the Wi-Fi-enabled X16 emulator,
    the system ROM, and the app itself are all bundled alongside it.

EOF
    fi

    if [[ "$DO_WINDOWS" -eq 1 ]]; then
        cat <<EOF
windows/
    Run "$NAME.exe". Same idea: double-click it and the app opens directly,
    no console window, no separate install step. Everything it needs
    (x16emu.exe, its DLLs, the ROM, the app) is in this folder already —
    keep the folder together if you move it.

EOF
    fi

    echo "real-hardware/"
    echo "    For an actual Commander X16 with a real ESP32 WiFi/network expansion"
    echo "    card (not the emulator). Copy everything in this folder onto the root"
    if [[ -f "$SDCARD/AUTOBOOT.X16" ]]; then
        echo "    of the X16's SD card. It boots itself automatically via AUTOBOOT.X16."
    else
        echo "    of the X16's SD card, then on the X16 type:"
        echo "        LOAD\"$PRG\""
        echo "        RUN"
    fi
    echo
    echo "Built from the X16-emulator-wifi-support fork:"
    echo "https://github.com/lionsarmor (this machine's copy: $REPO_ROOT)"
} > "$BUNDLE_DIR/README.txt"

# --- 3. Zip it into the single file people actually get handed ---
if [[ "$DO_ZIP" -eq 1 ]]; then
    echo "==> Zipping..."
    ZIP_PATH="$OUT/$SLUG.zip"
    rm -f "$ZIP_PATH"
    (cd "$OUT" && zip -rq "$SLUG.zip" "$SLUG")
    echo
    echo "Bundle ready: $ZIP_PATH"
else
    echo
    echo "Bundle ready (unzipped): $BUNDLE_DIR"
fi
