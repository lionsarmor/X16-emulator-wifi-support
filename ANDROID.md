# Android

This fork's emulator core (CPU, VERA, memory, and the WiFi/ESP32 network
card) cross-compiles for Android and packages into a real, installable
APK. This is new and rougher than the Linux/Windows path in
[EXPORTING.md](EXPORTING.md) — read the "What's not done yet" section
before expecting a finished app.

## What's actually proven working

- The current, up-to-date emulator source in `x16-emulator/src/` (not an
  old snapshot) compiles cleanly against the Android NDK and links into
  `libmain.so`, alongside a from-source Android build of SDL2.
- `./gradlew assembleDebug` produces a real signed-for-debug APK
  (`com.lionsarmor.x16wifi`, targets arm64-v8a, minSdk 24 / Android 7.0+)
  containing that library plus `libSDL2.so` and `libc++_shared.so`.
- The APK bundles the X16 system ROM as an asset and extracts it to the
  app's private storage on first launch, then boots `x16emu` with
  `-rom <extracted path> -wifi -fsroot <private storage>/sdroot -scale 2`
  — the same flags you'd use on desktop, just supplied through a small
  custom `X16Activity` (`android/app/src/main/java/com/lionsarmor/x16wifi/X16Activity.java`)
  instead of a shell command line.
- `INTERNET` and `ACCESS_NETWORK_STATE` permissions are declared, which
  Android requires before any socket the WiFi card opens will work at all.
- **`AT&G` (the WiFi card's HTTP(S) GET) works on Android now, through a
  real fix, not a stub.** Desktop builds shell out to `curl`; there's no
  such binary inside an app's sandbox, so `zm_http_get` in `esp32wifi.c`
  now branches on `__ANDROID__` to call Android's own `HttpURLConnection`
  instead, through a small JNI bridge
  (`android/app/src/main/java/com/lionsarmor/x16wifi/HttpBridge.java`).
  This also means Android gets its TLS/CA trust store from the OS itself,
  not a vendored copy that would go stale. Verified as far as this machine
  can without a device: the native code and the Java method agree exactly
  on the JNI contract (class path, method name, `(Ljava/lang/String;I)[B`
  signature) confirmed by disassembling the actual compiled `.dex`, not
  just by reading the source. Not yet confirmed making a real network
  request on an actual device.
- **Two real, on-device bugs found and fixed - not just compile-checked.**
  Getting this running on an actual Android Virtual Device (AVD) surfaced
  both immediately:
  - A guaranteed crash on every single launch, before any command-line
    parsing even ran: `main.c` called `strlen()` on `SDL_GetBasePath()`'s
    return value unconditionally, and that function is explicitly
    unsupported on Android (`SDL_sysfilesystem.c` returns `NULL` there
    always) - `strlen(NULL)` is an instant SIGSEGV. Fixed with a null
    check; the fallback path it guards is unused on Android anyway, since
    `X16Activity` always passes an explicit `-rom` path.
  - Touches and mouse clicks landing nowhere near where they were
    tapped, because the app had no fixed orientation and fought the
    device over portrait vs. landscape at startup, leaving the
    touch-to-logical-coordinate math computed against transiently stale
    window dimensions. Fixing this took two changes, not one:
    `android:screenOrientation="landscape"` in the manifest turned out
    not to be sufficient by itself, since SDL2's own Android backend
    independently calls `setRequestedOrientation()` once the window
    exists and, with no orientation hint set, defaults to
    `SCREEN_ORIENTATION_FULL_SENSOR` - silently overriding the manifest.
    The real fix was also setting `SDL_HINT_ORIENTATIONS` in `video.c`
    before window creation. Confirmed via
    `adb shell dumpsys window | grep mCurrentAppOrientation`: before this,
    it read `FULL_SENSOR` (10) despite the manifest; after, it correctly
    reads `SENSOR_LANDSCAPE` (6), and screenshots confirm real landscape
    rendering with taps landing within a few pixels of their target.
  - Also added `-widescreen` to the default Android launch args (an
    existing, already-tested desktop flag, not new code) once landscape
    was locked in, so real content - DESK COMMANDER's chat, the
    screensaver - gets far more of a phone/tablet's actual (much wider
    than 4:3) display instead of a narrow letterboxed strip.
- A full on-screen keyboard is built into the emulator itself (`x16-emulator/src/osk.c`),
  not just a system IME popup. A small keyboard icon is always drawn in the
  top-right corner; tapping it shows the complete X16 key layout - every
  letter, digit, F1-F8, arrow keys, TAB, RUN/STOP, RESTORE, Shift, and
  Ctrl - across the bottom of the screen, and tapping it again hides it.
  Regular keys send a real keydown on finger-down and keyup on finger-up
  (so KERNAL key-repeat works normally); Shift/Ctrl latch on tap instead of
  needing to be held; RESTORE fires the same NMI real hardware wires it to.
  This draws with the emulator's own renderer, so it's identical on Linux,
  Windows, and Android alike - see the main [README.md](README.md) for how
  it's exercised on desktop too. Verified visually end-to-end on Linux
  (toggle, full layout, and Shift's highlight all confirmed by screenshot);
  the Android build compiles and packages the same code, but hasn't been
  confirmed on an actual device yet.

I do not have a device or emulator (AVD) attached to verify it visually
boots to a `READY.` prompt — that's the next thing to actually check. A
debug build is at `~/RODDY TARGETS/android-dev/X16-Emulator-WiFi-debug.apk`;
install it on a phone with `adb install X16-Emulator-WiFi-debug.apk` (or
just copy it over and open it) to find out.

- **Per-app bundling works, matching the Linux/Windows bundler.**
  `android/bundle-app-android.sh --name "DESK COMMANDER" --prg DCMAIN.PRG --sdcard /path/to/dist/sdcard`
  produces a dedicated APK that boots straight into that one app, no
  picker, the same way `tools/bundle-x16-app.sh` does for Linux/Windows.
  Each app gets its own Android package ID (derived from its name, e.g.
  `com.lionsarmor.x16wifi.deskcommander`, or set explicitly with
  `--app-id`), so several bundled apps install side-by-side as distinct
  apps instead of overwriting each other. Verified end-to-end for DESK
  COMMANDER: correct package name, correct app label, its files present
  under `assets/app/` in the built APK. The easiest way to use this is
  through `tools/x16-publisher.py` (see [EXPORTING.md](EXPORTING.md)),
  which wraps this script and the Linux/Windows one behind one GUI.

## Building it yourself

```sh
cd android
./fetch-sdl.sh          # downloads SDL2 source into app/jni/SDL/ (not committed to git)
echo "sdk.dir=/path/to/your/Android/sdk" > local.properties
./gradlew assembleDebug
```

The Android SDK/NDK/JDK aren't installed via this repo — you need your own
(Android Studio's SDK Manager is the easiest way, or the command-line
`sdkmanager`). `local.properties` can be skipped if you export `ANDROID_HOME`
instead. This was built and tested against NDK 25/26 and JDK 17; nothing
about the setup is version-pinned tightly beyond that.

The native side is wired through `android/app/jni/src/CMakeLists.txt`,
which points at `x16-emulator/src/` directly rather than a second copy —
Android always builds from the exact same source as Linux and Windows.

To bundle a specific app instead of the bare emulator, use
`android/bundle-app-android.sh` (see above) rather than calling `gradlew`
directly — it stages the app's files into `assets/app/` and sets the
per-app build properties for you.

## What's not done yet

This is a real, itemized list, not a vague disclaimer:

- **Only arm64-v8a is built.** Covers the overwhelming majority of real
  phones sold in the last several years; add `armeabi-v7a` to
  `abiFilters` in `app/build.gradle` if you need older 32-bit devices too.
- **Debug-signed only.** Fine for installing on your own device or
  testing; a real release build needs its own signing key before it could
  go anywhere like the Play Store.

## Why this instead of the old `x16-emu-android` project

A pre-existing Android port ([svangsgaard/x16-emu-android](https://github.com/svangsgaard/x16-emu-android))
does exist, but it's frozen on X16 ROM revision R36 from January 2020 —
years before the platform's current memory map and VERA registers were
settled, has no WiFi card emulation at all, needs a physical keyboard to
do anything, and is GPL-2.0 licensed against this project's 2-clause BSD.
Its one genuinely reusable idea — that SDL2's official Android build path
works for this emulator family — is what this setup is built on; the
actual emulator core, SDL2 version, and Android API level here are all
current instead.
