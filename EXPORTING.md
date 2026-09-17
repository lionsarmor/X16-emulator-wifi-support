# Exporting an X16 app to Linux, Windows, and real hardware

`tools/bundle-x16-app.sh` turns a built X16 program into a single zip that
runs "as native" on Linux and Windows via this fork's Wi-Fi-enabled
emulator, plus a ready-to-copy package for a real Commander X16 with a real
ESP32 WiFi card. One command in, one zip out.

This covers how to use it, what it produces, and how to troubleshoot it.
For how the Wi-Fi card itself works, see the main [README.md](README.md).
For Android specifically (a separate script, `android/bundle-app-android.sh`,
with its own considerations), see [ANDROID.md](ANDROID.md).

## Prefer a GUI?

`tools/x16-publisher.py` is a desktop app wrapping all of this — point it
at an app folder, tick which platforms you want (Linux, Windows, Android),
and click Publish. It calls the exact same scripts this document describes,
streams their output live, and adds a couple of one-click extras: a
"Test on this machine" button that launches a freshly built Linux bundle
immediately, and an "Install to connected device" button that runs
`adb install` when a phone is plugged in. No extra install needed — it's
plain Python 3 with the Tkinter GUI toolkit already built in:

```sh
python3 tools/x16-publisher.py
```

The rest of this document explains what that GUI is doing under the hood,
and is the reference for using the scripts directly (CI, automation,
scripting your own release process, etc).

## Before you start

You need an X16 app that's already built — that is, you (or its own build
script) have already produced the folder of files that would normally go on
the X16's SD card: the `.PRG`, any overlay `.BIN` files it loads at runtime,
data files, and optionally an `AUTOBOOT.X16` so the KERNAL boots it
automatically. Most X16 projects call this folder `dist/sdcard` or similar.
The bundler doesn't compile your app — point it at that finished output.

The first time you bundle anything, the script also builds both emulators
it needs:

- **Linux**: a normal CMake build of `x16-emulator/`, same as the main
  README describes.
- **Windows**: cross-compiled with mingw-w64. If `x86_64-w64-mingw32-gcc`
  isn't installed, install it first:
  ```sh
  sudo apt install g++-mingw-w64-x86-64
  ```
  Everything else Windows needs (SDL2, zlib, for the mingw target) is
  fetched automatically into `x16-emulator/.crossdeps/` on first run — no
  root required for that part.

Both builds are cached (`x16-emulator/build/` and `build-windows/`), so
this only happens once; later bundles reuse them instantly. Delete those
folders if you ever need to force a rebuild (e.g. after pulling emulator
changes).

## Bundling an app

```sh
tools/bundle-x16-app.sh \
    --name "DESK COMMANDER" \
    --prg DCMAIN.PRG \
    --sdcard "/home/legion/Desktop/DESK-COMMANDER/dist/sdcard"
```

Or, if you've sourced `~/.bash_aliases`, the shorter alias set up for this:

```sh
x16bundle --name "WEATHER COMMANDER" --prg WEATHER.PRG \
    --sdcard "/home/legion/Desktop/WEATHER COMMANDER/dist/sdcard"
```

| Flag | Required | Meaning |
|---|---|---|
| `--name` | yes | Human-readable app name. Becomes the bundle/zip name and the launcher filenames, e.g. `DESK COMMANDER.exe`. |
| `--sdcard` | yes | Path to the app's finished, ready-to-run files. |
| `--prg` | only if the app has no `AUTOBOOT.X16` | Which `.PRG` (relative to `--sdcard`) to load and `RUN`. If the app already autoboots itself, you can omit this. |
| `--out` | no | Output directory. Defaults to `~/RODDY TARGETS`. |
| `--no-zip` | no | Leave the bundle as a plain folder instead of also zipping it. |
| `--platforms` | no | Comma-separated subset of `linux,windows` to actually build. Defaults to both. `real-hardware/` is always included either way — it's just a file copy, not a build. |

Run `tools/bundle-x16-app.sh --help` any time for the same reference.

## What you get

```
~/RODDY TARGETS/DESK-COMMANDER.zip        <- the one file to hand someone
~/RODDY TARGETS/DESK-COMMANDER/           <- same thing, unzipped
    linux/
        x16emu, rom.bin                 (the Wi-Fi-enabled emulator)
        app/                            (your app's files, copied in)
        DESK COMMANDER.sh                <- run this
    windows/
        x16emu.exe, SDL2.dll, zlib1.dll, libwinpthread-1.dll, rom.bin
        app/                            (your app's files, copied in)
        launcher.cfg                    (tells the launcher which PRG to run)
        DESK COMMANDER.exe               <- run this
    real-hardware/
        (just your app's files, nothing else)
    README.txt                          (generated, explains the above)
```

Every platform folder is self-contained — the emulator binary, the system
ROM, and the app all travel together. Move the whole bundle anywhere (a USB
stick, another machine, wherever) and it keeps working, since both
launchers find their own files by locating themselves first rather than
hardcoding a path.

## Running it, per platform

- **Linux**: extract the zip, run `linux/DESK COMMANDER.sh` from a
  terminal (or double-click it if your file manager runs executable
  `.sh` files). It boots straight into the app with the Wi-Fi card enabled.
- **Windows**: extract the zip, double-click `windows/DESK COMMANDER.exe`.
  No console window, no install step — it just opens the emulator running
  the app. Keep the `windows/` folder's contents together; the launcher
  looks for `x16emu.exe`, the DLLs, and `app/` next to itself.
- **Real hardware**: copy everything inside `real-hardware/` onto the root
  of an actual X16's SD card. With a real ESP32 WiFi card installed, it
  runs exactly like the emulator does — this folder has no emulator in it
  at all, it's just the app. If it has an `AUTOBOOT.X16`, it boots itself;
  otherwise, on the X16, type `LOAD"<your .PRG>"` then `RUN`.

## Troubleshooting

- **"No ROM at .../build/rom.bin"** — grab `rom.bin` from
  [X16Community/x16-rom releases](https://github.com/X16Community/x16-rom/releases)
  and place it at `x16-emulator/build/rom.bin`, same as for a normal build.
- **"mingw-w64 not found"** — install it: `sudo apt install g++-mingw-w64-x86-64`.
- **"No --prg given and no AUTOBOOT.X16..."** — the bundler has no way to
  know what to launch. Either pass `--prg`, or make sure the app's
  `--sdcard` folder has an `AUTOBOOT.X16`.
- **Windows build fails to link with a `pthread_*` undefined reference** —
  this means the system's default `x86_64-w64-mingw32-gcc` resolved to the
  "win32" threading alternative instead of "posix". The toolchain file
  (`x16-emulator/cmake/mingw-w64-toolchain.cmake`) already pins the
  `-posix` compiler explicitly to avoid this; if you see it anyway, check
  that `x86_64-w64-mingw32-gcc-posix` is actually installed
  (`update-alternatives --list x86_64-w64-mingw32-gcc`).

## What's not covered yet

- **macOS**: not built. The `.app` wrapper itself would be simple (just a
  folder structure and a shell script — no compiler needed), but producing
  an actual macOS `x16emu` binary needs either `osxcross` set up on a Linux
  build machine, or building directly on a Mac. Neither is wired up here.
- **Android**: a much bigger lift, not a packaging problem. It needs a real
  SDL-for-Android port of the emulator (none exists for this codebase yet)
  and a rewrite of the Wi-Fi card's `AT&G` HTTP fetch, which currently
  shells out to the `curl` command-line tool — something a sandboxed APK
  can't do. Treat this as a separate project if it's ever wanted.

Both would slot into the same bundle layout as additional platform folders
without changing how `linux/`, `windows/`, or `real-hardware/` work.
