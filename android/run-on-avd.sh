#!/usr/bin/env bash
# run-on-avd.sh — install a bundled APK onto an Android emulator (AVD) and
# launch it, starting the AVD first if none is already running.
#
# Usage:
#   android/run-on-avd.sh --apk /path/to/App.apk --package com.example.app [--avd x16test]
#
# --apk      Path to the APK to install (from android/bundle-app-android.sh).
# --package  The installed application ID (--app-id you gave the bundler,
#             or the one it derived from --name).
# --avd      Which AVD to boot if none is running. Defaults to "x16test"
#             (see ANDROID.md for how that one was created).
set -euo pipefail

: "${ANDROID_HOME:=/home/legion/android-tools/sdk}"
ADB="$ANDROID_HOME/platform-tools/adb"
EMULATOR="$ANDROID_HOME/emulator/emulator"

APK=""
PACKAGE=""
AVD="x16test"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --apk) APK="$2"; shift 2 ;;
        --package) PACKAGE="$2"; shift 2 ;;
        --avd) AVD="$2"; shift 2 ;;
        -h|--help) grep '^#' "$0" | sed 's/^# \?//'; exit 0 ;;
        *) echo "Unknown argument: $1" >&2; exit 1 ;;
    esac
done

if [[ -z "$APK" || -z "$PACKAGE" ]]; then
    echo "Usage: $0 --apk /path/to/App.apk --package com.example.app [--avd x16test]" >&2
    exit 1
fi
if [[ ! -f "$APK" ]]; then
    echo "APK not found: $APK" >&2
    exit 1
fi

if ! "$ADB" devices | grep -q "device$"; then
    echo "==> No emulator running - booting $AVD (this takes a moment)..."
    nohup "$EMULATOR" -avd "$AVD" -no-audio -no-boot-anim -gpu swiftshader_indirect \
        > /tmp/avd-"$AVD".log 2>&1 &
    "$ADB" wait-for-device
    echo "==> Waiting for boot to finish..."
    for _ in $(seq 1 60); do
        boot="$("$ADB" shell getprop sys.boot_completed 2>/dev/null | tr -d '\r')"
        [[ "$boot" == "1" ]] && break
        sleep 3
    done
fi

echo "==> Installing $APK..."
"$ADB" install -r "$APK"

echo "==> Launching $PACKAGE..."
"$ADB" shell am start -n "$PACKAGE/com.lionsarmor.x16wifi.X16Activity"
