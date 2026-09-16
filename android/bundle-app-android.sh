#!/usr/bin/env bash
# bundle-app-android.sh — package a built X16 app into a dedicated,
# installable Android APK that boots straight into it, no picker, no
# generic bare-BASIC boot. Mirrors tools/bundle-x16-app.sh's Linux/Windows
# behavior for Android.
#
# Usage:
#   android/bundle-app-android.sh --name "DESK COMMANDER" --prg DCMAIN.PRG \
#       --sdcard "/path/to/app/dist/sdcard" [--out ~/x16-bundles] [--app-id com.example.deskcommander]
#
# Each app should get its own --app-id (defaults to a slug derived from
# --name under com.lionsarmor.x16wifi) so it installs as a distinct app
# alongside others rather than overwriting a previous install.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

NAME=""
PRG=""
SDCARD=""
OUT="$HOME/x16-bundles"
APP_ID=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --name) NAME="$2"; shift 2 ;;
        --prg) PRG="$2"; shift 2 ;;
        --sdcard) SDCARD="$2"; shift 2 ;;
        --out) OUT="$2"; shift 2 ;;
        --app-id) APP_ID="$2"; shift 2 ;;
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

echo "==> Building APK (this can take a while on the first run)..."
(cd "$SCRIPT_DIR" && ./gradlew assembleDebug -PxAppId="$APP_ID" -PxAppLabel="$NAME")

mkdir -p "$OUT"
APK_OUT="$OUT/$UPPER_SLUG.apk"
cp "$SCRIPT_DIR/app/build/outputs/apk/debug/app-debug.apk" "$APK_OUT"

echo
echo "Android bundle ready: $APK_OUT"
echo "Install with: adb install -r \"$APK_OUT\""
