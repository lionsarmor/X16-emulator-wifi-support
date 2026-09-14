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
    request (via `curl`, so TLS is handled properly) and returns the response body
    over the UART, letting 6502 code talk to a web API without implementing HTTP
    or TLS itself.
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

or at a non-default address (only the low byte matters — the card always lives
somewhere in the `$9Fxx` I/O page):

```sh
./x16emu -wifi e0
```

Run `./x16emu -help` and look for `-wifi` for the full flag description.

## Building

Standard CMake build — see [`x16-emulator/README.md`](x16-emulator/README.md) and
[`x16-emulator/quickstart-linux.md`](x16-emulator/quickstart-linux.md) for complete
platform instructions:

```sh
cd x16-emulator
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

You'll also need a `rom.bin` (the X16 system ROM) next to the built `x16emu`
binary — grab the latest release from
[X16Community/x16-rom](https://github.com/X16Community/x16-rom/releases). It isn't
bundled here, matching upstream's own practice.

The WiFi card's HTTP(S) support (`AT&G`) shells out to the `curl` command-line
tool, which needs to be on your system `PATH` at runtime. It ships by default on
essentially every Linux distribution, macOS, and Windows 10 (1803+); nothing else
is required.

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
