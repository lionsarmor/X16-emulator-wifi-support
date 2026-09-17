# X16-emulator-wifi-support

A fork of the official [Commander X16 emulator](https://github.com/X16Community/x16-emulator)
that adds full emulation of the community **921.6Kbps Serial & ESP32 Network Card** —
the real WiFi expansion card for the X16. Software written for that card's network
port runs unmodified here and reaches the real Internet through your host machine's
own connection. No real hardware, no physical ESP32, no changes to your X16 program.

Verified end-to-end against [DESK COMMANDER](https://github.com/lionsarmor/DESK-COMMANDER),
a real X16 desktop application with a WiFi setup panel and a web-backed chat app: card
detection, network scan, join, live TCP connections, and HTTPS API calls all work
against this emulator exactly as they do against the physical card.

## Why

The Commander X16 is an open-source 8-bit computer. The official emulator is
excellent but has no concept of the community WiFi/network expansion card, so any
X16 software built around it — terminal programs, BBS clients, chat apps, anything
that talks to the Internet — can only be tested on real hardware. This fork adds
that card to the emulator, so that software can be developed and tested entirely on
a desktop machine.

## The real card, briefly

The physical card is a TL16C2550 dual-UART board (the same chip the official Serial
MIDI card uses) whose first UART is wired to an onboard ESP32-WROOM module running
[Bo Zimmerman's Zimodem](https://github.com/bozimmerman/Zimodem) firmware: a
Hayes-modem-style AT command set that bridges to TCP/IP and HTTP. Software talks to
the card the same way it would talk to an old-fashioned modem — dial a host, listen
for a connection, fetch a URL — and the firmware handles the network side.

## What this fork adds

New files: [`x16-emulator/src/esp32wifi.c`](x16-emulator/src/esp32wifi.c) and
[`esp32wifi.h`](x16-emulator/src/esp32wifi.h) (start there for full implementation
notes — every design decision is explained inline).

- **Register-accurate UART emulation** for both of the card's 16550-compatible
  ports, in a 16-byte memory-mapped I/O window at the card's factory default
  address: `$9FE0-$9FE7` for the network port, `$9FE8-$9FEF` for the physical
  RS-232 port. Timing is paced to real CPU cycles against the configured baud
  divisor, including FIFO trigger levels and interrupt behavior, so software that
  actually relies on realistic UART timing (not just "peek a byte") works
  correctly.
- **A Zimodem-compatible AT command engine** behind the network port:
  - `ATD"host:port"` / `ATDT"host:port"` — dial out over a real TCP connection.
  - `ATDS"host:port"` — raw TLS on desktop builds with OpenSSL, including
    certificate/hostname verification and SNI. Weather Commander uses this
    for its direct HTTPS requests. Builds without OpenSSL report `ERROR`
    for secure dials; they never silently downgrade them to plain TCP.
  - `ATA<port>` — listen for an incoming TCP connection.
  - `+++` — standard Hayes guard-timed escape back to command mode without
    hanging up; `ATO` resumes the connection.
  - `ATH` — hang up.
  - `ATW<n>` — WiFi scan; reports one synthetic access point (`X16-EMULATOR-NET`)
    representing "you're already online through the host," since there's no real
    radio to scan with.
  - `ATW"ssid,password"` — WiFi join; always succeeds, since the underlying
    connectivity is already real.
  - `ATI2` — reports a real-looking local IP once joined, `0.0.0.0` otherwise, for
    software that checks connection status this way.
  - `AT&G"http(s)://..."` — Zimodem's raw HTTP(S) GET extension. Performs a real
    request (via `curl` on desktop, so TLS is handled properly; via Android's own
    `HttpURLConnection` on Android, where there's no `curl` binary to shell out
    to) and returns the response body over the UART, letting 6502 code talk to a
    web API without implementing HTTP or TLS itself.
  - Plus `ATE`, `ATV`, `ATZ`, `ATI`, `ATB`, `ATQ`/`ATX`/`ATF`/`ATR`, `AT&W`/`AT&`*,
    and `ATSn` for echo/verbosity/reset/info/baud/quiet/extended-results/save/
    S-register compatibility with real startup strings.
- **Basic Telnet IAC negotiation stripping** on connections dialed with the `T`
  modifier, for talking to raw telnet BBSes without garbage in the stream.
- The physical RS-232 port (the second UART) is emulated at the register level
  only — nothing is connected to the other end of it yet.

## Using it

Build normally (see below), then pass the new `-wifi` flag:

```sh
./x16emu -wifi
```

In an application's Wi-Fi settings, select **X16-EMULATOR-NET** and leave its
password empty. This represents the host's internet connection. `ATH` closes
the current socket while preserving the network association. Large raw socket
replies apply backpressure until the guest consumes the queued bytes.

On this development computer, **WEATHERWIFI** builds Weather Commander and runs
this emulator with `-wifi`. Its project now lives under
`Desktop/Roddy Software/WEATHER COMMANDER`.

or at a non-default address (only the low byte matters — the card always lives
somewhere in the `$9Fxx` I/O page):

```sh
./x16emu -wifi e0
```

Run `./x16emu -help` and look for `-wifi` for the full flag description.

## On-screen keyboard

A small keyboard icon is always drawn in the top-right corner of the
emulator window, on every platform (Linux, Windows, and Android alike,
since it's drawn by the emulator's own renderer, not anything OS-specific).
Click or tap it to show a full X16 key layout across the bottom of the
screen — every letter, digit, F1-F8, arrow key, TAB, RUN/STOP, RESTORE,
Shift, and Ctrl — and tap it again to hide it. This exists for touchscreens
that have no physical keyboard at all (a phone, primarily), but works
identically with a mouse on a normal desktop too.

Regular keys behave like a real keypress held for exactly as long as you
hold the on-screen button, including KERNAL key-repeat if you hold one
down. Shift and Ctrl latch on tap instead, since holding two on-screen
buttons down for a combo is awkward with touch — tap once to hold it,
tap again to release. RESTORE triggers the same NMI real X16 hardware
wires it to, not a scancode.

See `x16-emulator/src/osk.c` for the implementation.

## Building

Standard CMake build — see [`x16-emulator/README.md`](x16-emulator/README.md) and
[`x16-emulator/quickstart-linux.md`](x16-emulator/quickstart-linux.md) for complete
platform instructions:

```sh
cd x16-emulator
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Install OpenSSL development files (Ubuntu: `libssl-dev`) before configuring
to enable raw `ATDS` TLS. The desktop CMake build detects and links OpenSSL.
The Android-specific build still provides HTTPS through `AT&G`; raw `ATDS`
requires a separate TLS integration there.

Run `python3 tools/test_wifi_transport.py` for local TCP/TLS regression tests:
trusted/untrusted certificates, a 60 KB binary reply with a slow reader, and
Wi-Fi state across socket close/reset.

You'll also need a `rom.bin` (the X16 system ROM) next to the built `x16emu`
binary — grab the latest release from
[X16Community/x16-rom](https://github.com/X16Community/x16-rom/releases). It isn't
bundled here, matching upstream's own practice.

The WiFi card's HTTP(S) support (`AT&G`) shells out to the `curl` command-line
tool on Linux, Windows, and macOS, which needs to be on your system `PATH` at
runtime. It ships by default on essentially every Linux distribution, macOS,
and Windows 10 (1803+); nothing else is required. Android doesn't need `curl`
at all — it uses Android's own `HttpURLConnection` instead (see
[`ANDROID.md`](ANDROID.md)).

## Publishing an app: Linux, Windows, Android, and real hardware

Point `tools/x16-publisher.py` — a desktop GUI, `python3 tools/x16-publisher.py`,
no extra install needed — at any built X16 app's folder, tick which
platforms to publish to, and click Publish:

- **Linux / Windows**: a double-click launcher with this fork's Wi-Fi
  emulator bundled in, fully self-contained.
- **Android**: a dedicated, installable APK that boots straight into that
  one app, no picker — each app gets its own Android package ID so
  several install side-by-side without overwriting each other.
- **Real hardware**: a ready-to-copy folder for an actual Commander X16
  with a real ESP32 WiFi card — no emulator involved at all.

Every app gets its own icon: three are included in `assets/icons/`
(a plain Roddy "dot" for the bare emulator, and dedicated icons for DESK
COMMANDER and WEATHER COMMANDER, all sampled from this project's actual
brand colors), and the GUI can add more — pick "Upload new icon…" and
point it at any image. Everything lands in one place, `~/RODDY TARGETS`,
instead of scattering across wherever each tool happened to run from.

The GUI is a thin wrapper around two command-line tools you can also call
directly (CI, scripting a release): `tools/bundle-x16-app.sh` for
Linux/Windows, `android/bundle-app-android.sh` for Android. Full
reference for both: [`EXPORTING.md`](EXPORTING.md) and [`ANDROID.md`](ANDROID.md).

## Android

This fork's emulator core cross-compiles into a real installable Android
APK (arm64-v8a, current ROM and SDL2, not a stale port), with the same
full on-screen keyboard as desktop (see above) and the WiFi card's `AT&G`
working there too, through Android's own `HttpURLConnection` via JNI
instead of the `curl` binary desktop builds use.

Getting this actually running on a real device surfaced two real bugs,
both found and fixed by running the app rather than only compiling it: a
guaranteed startup crash (`SDL_GetBasePath()` returns `NULL` on Android;
this called `strlen()` on it unconditionally), and a touch/mouse
coordinate mismatch caused by the app fighting the device over portrait
vs. landscape orientation at startup. Fixing that one took two changes,
not one: locking the manifest to landscape (also just the correct choice
for an inherently 4:3, landscape 8-bit computer) turned out not to be
enough by itself, since SDL2's own Android backend independently resets
the orientation at runtime unless told otherwise (`SDL_HINT_ORIENTATIONS`)
— confirmed fixed via `adb shell dumpsys window` showing the runtime
orientation actually matching the lock, not just the manifest declaring it.

There's also best-effort support for OUYA, the discontinued Android-based
console — an intent-filter and banner image so it shows up in OUYA's own
launcher, if anyone still has one running. Unverified on real hardware
(none available), and honest about the caveats in [`ANDROID.md`](ANDROID.md).

See [`ANDROID.md`](ANDROID.md) for what's proven working, what isn't yet,
and how to build it.

## Known limitations

These are deliberate scope decisions, not oversights — see the comment header in
`esp32wifi.c` for the full reasoning behind each one:

- DNS lookups for `ATD` and requests for `AT&G` are synchronous, so a slow or
  unreachable host briefly pauses the whole emulator (bounded to 15 seconds for
  `AT&G`; unbounded but typically fast for `ATD`).
- WiFi scan/join is a fixed synthetic access point, not a real scan of nearby
  networks — there's no radio to scan with, only the host's own connection.
- Telnet support is a minimal IAC option-decliner, not a full implementation.
- The SSH, PETSCII-translation, and XON/XOFF dial modifiers are accepted
  syntactically but have no effect.
- The card's second UART (the physical RS-232 port) has no host-side connection.

## License

2-clause BSD, same as upstream — see [`LICENSE`](LICENSE).
