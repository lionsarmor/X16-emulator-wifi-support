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
- The system's on-screen keyboard now appears and can type into the
  emulator. The fix was a single `SDL_StartTextInput()` call added to
  `x16-emulator/src/video.c` (Android-only, guarded by `#ifdef __ANDROID__`):
  SDL2's Android backend already translates typed characters into real
  `SDL_KEYDOWN`/`SDL_KEYUP` events (see `SDLInputConnection.commitText()`
  in `SDLActivity.java`, which calls `nativeGenerateScancodeForUnichar()`
  per character, plus real key events for Enter and backspace) — desktop
  builds simply never call the one function that tells Android to show it.

I do not have a device or emulator (AVD) attached to verify it visually
boots to a `READY.` prompt — that's the next thing to actually check. A
debug build is at `~/x16-bundles/android-dev/X16-Emulator-WiFi-debug.apk`;
install it on a phone with `adb install X16-Emulator-WiFi-debug.apk` (or
just copy it over and open it) to find out.

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

## What's not done yet

This is a real, itemized list, not a vague disclaimer:

- **On-screen keyboard covers typing, not the whole keyboard.** Letters,
  digits, space, backspace, and enter now come through the system IME
  (see above) — likely enough for plain BASIC use. But arrow keys,
  function keys, ESC, and modifier combos (Ctrl/Shift shortcuts, RUN/STOP,
  RESTORE) have no equivalent on a glass keyboard; no IME sends them. X16
  software that leans on those, including this fork's own DESK COMMANDER
  and WEATHER COMMANDER, will still need either a Bluetooth/USB keyboard
  or a future custom on-screen overlay with dedicated buttons for those
  keys mapped to SDL scancodes.
- **`AT&G` (the WiFi card's HTTP fetch) won't work yet.** It currently
  shells out to the `curl` command-line tool, which doesn't exist inside
  Android's app sandbox. The real fix is linking libcurl (or using
  Android's own networking APIs via JNI) directly into `libmain.so`
  instead of spawning a subprocess — a change to `esp32wifi.c`, not just
  packaging.
- **No app picker.** This APK boots straight to bare Commander BASIC with
  an empty SD-card folder — it doesn't yet bundle or let you choose a
  specific app like DESK COMMANDER the way `tools/bundle-x16-app.sh` does
  for Linux/Windows. Extending that bundler to also produce an Android
  variant (dropping the chosen app's files into `assets/` instead of
  `rom.bin` alone) is the natural next step once the two items above are
  sorted.
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
