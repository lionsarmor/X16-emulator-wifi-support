#!/usr/bin/env bash
# bundle-app-android.sh — package a built X16 app into a dedicated,
# installable Android APK that boots straight into it, no picker, no
# generic bare-BASIC boot. Mirrors tools/bundle-x16-app.sh's Linux/Windows
# behavior for Android.
#
# Usage:
#   android/bundle-app-android.sh --name "DESK COMMANDER" --prg DCMAIN.PRG \
#       --sdcard "/path/to/app/dist/sdcard" [--out ~/RODDY TARGETS] \
#       [--app-id com.example.deskcommander] [--icon desk-commander]
#
# Each app should get its own --app-id (defaults to a slug derived from
# --name under com.lionsarmor.x16wifi) so it installs as a distinct app
# alongside others rather than overwriting a previous install.
#
# --icon accepts either the name of an icon already in assets/icons/
# (e.g. "desk-commander" for assets/icons/desk-commander.png - see that
# folder for what's there) or a path to any other square-ish image file.
# Omit it to keep whatever icon android/app/src/main/res/mipmap-*/ already
# has (the plain Roddy dot, by default).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"
ICON_LIBRARY_DIR="$REPO_ROOT/assets/icons"

NAME=""
PRG=""
SDCARD=""
OUT="$HOME/RODDY TARGETS"
APP_ID=""
ICON=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --name) NAME="$2"; shift 2 ;;
        --prg) PRG="$2"; shift 2 ;;
        --sdcard) SDCARD="$2"; shift 2 ;;
        --out) OUT="$2"; shift 2 ;;
        --app-id) APP_ID="$2"; shift 2 ;;
        --icon) ICON="$2"; shift 2 ;;
        -h|--help) grep '^#' "$0" | sed 's/^# \?//'; exit 0 ;;
        *) echo "Unknown argument: $1" >&2; exit 1 ;;
    esac
done

if [[ -z "$NAME" || -z "$SDCARD" ]]; then
    echo "Usage: $0 --name \"APP NAME\" --sdcard /path/to/dist/sdcard [--prg FILE.PRG] [--out DIR] [--app-id com.example.app]" >&2
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

SLUG="$(echo "$NAME" | tr '[:upper:]' '[:lower:]' | tr -cs 'a-z0-9' '-' | sed 's/^-*//;s/-*$//')"
if [[ -z "$SLUG" ]]; then
    echo "Could not derive an app id from --name \"$NAME\"" >&2
    exit 1
fi
if [[ -z "$APP_ID" ]]; then
    APP_ID="com.lionsarmor.x16wifi.$(echo "$SLUG" | tr -d '-')"
fi

UPPER_SLUG="$(echo "$SLUG" | tr '[:lower:]' '[:upper:]')"

: "${JAVA_HOME:=/home/legion/android-tools/jdk}"
: "${ANDROID_HOME:=/home/legion/android-tools/sdk}"
export JAVA_HOME ANDROID_HOME

if [[ ! -x "$JAVA_HOME/bin/java" ]]; then
    echo "No JDK found at JAVA_HOME=$JAVA_HOME. Set JAVA_HOME to a valid JDK 17+." >&2
    exit 1
fi
if [[ ! -d "$ANDROID_HOME" ]]; then
    echo "No Android SDK found at ANDROID_HOME=$ANDROID_HOME. Set ANDROID_HOME to your SDK path." >&2
    exit 1
fi

echo "==> Bundling '$NAME' ($APP_ID) for Android from $SDCARD"

if [[ ! -f "$SCRIPT_DIR/app/jni/SDL/CMakeLists.txt" ]]; then
    echo "==> Fetching SDL2 source (first Android build only)..."
    "$SCRIPT_DIR/fetch-sdl.sh"
fi

ASSETS_DIR="$SCRIPT_DIR/app/src/main/assets"
APP_ASSETS_DIR="$ASSETS_DIR/app"

echo "==> Staging app files into Android assets..."
rm -rf "$APP_ASSETS_DIR"
mkdir -p "$APP_ASSETS_DIR"
cp -a "$SDCARD/." "$APP_ASSETS_DIR/"

if [[ -n "$PRG" ]]; then
    echo "PRG=$PRG" > "$ASSETS_DIR/launcher.cfg"
else
    : > "$ASSETS_DIR/launcher.cfg"
fi

ICON_CHANGED=0
if [[ -n "$ICON" ]]; then
    if [[ -f "$ICON" ]]; then
        ICON_PATH="$ICON"
    elif [[ -f "$ICON_LIBRARY_DIR/$ICON.png" ]]; then
        ICON_PATH="$ICON_LIBRARY_DIR/$ICON.png"
    else
        echo "--icon '$ICON' not found as a file, and no $ICON_LIBRARY_DIR/$ICON.png in the icon library." >&2
        echo "Icons available in the library: $(ls "$ICON_LIBRARY_DIR" 2>/dev/null | sed 's/\.png$//' | tr '\n' ' ')" >&2
        exit 1
    fi
    echo "==> Applying icon: $ICON_PATH"
    python3 "$REPO_ROOT/tools/gen-app-icon.py" "$ICON_PATH" "$SCRIPT_DIR/app/src/main/res"
    ICON_CHANGED=1
fi

# The mipmap files just (maybe) overwritten are checked into git as the
# default Roddy-dot icon - restore them once the build's done (success or
# not) so a per-app icon never lingers as an uncommitted change in the
# working tree. Only if this is actually a git checkout with no other
# unrelated mipmap edits already pending.
restore_default_icon() {
    if [[ "$ICON_CHANGED" -eq 1 ]] && git -C "$REPO_ROOT" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
        git -C "$REPO_ROOT" checkout -- android/app/src/main/res/mipmap-*/ic_launcher.png 2>/dev/null || true
    fi
}
trap restore_default_icon EXIT

echo "==> Building APK (this can take a while on the first run)..."
(cd "$SCRIPT_DIR" && ./gradlew assembleDebug -PxAppId="$APP_ID" -PxAppLabel="$NAME")

mkdir -p "$OUT"
APK_OUT="$OUT/$UPPER_SLUG.apk"
cp "$SCRIPT_DIR/app/build/outputs/apk/debug/app-debug.apk" "$APK_OUT"

echo
echo "Android bundle ready: $APK_OUT"
echo "Install with: adb install -r \"$APK_OUT\""
