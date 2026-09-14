# X16-emulator-wifi-support

A fork of the official [Commander X16 emulator](https://github.com/X16Community/x16-emulator)
(vendored in [`x16-emulator/`](x16-emulator/)) that adds emulation of the community
**921.6Kbps Serial & ESP32 Network Card**, so X16 software written for that real
hardware's WiFi/network port also runs against the real Internet inside the emulator,
with no changes to the software.

## What's added

The real card is a TL16C2550 dual-UART expansion card (the same chip already emulated
by the official Serial MIDI card) whose first UART is wired internally to an
ESP32-WROOM module running Bo Zimmerman's "Zimodem" firmware: a Hayes-style AT-command
modem that bridges to TCP/IP. This fork adds [`src/esp32wifi.c`](x16-emulator/src/esp32wifi.c)
/ [`esp32wifi.h`](x16-emulator/src/esp32wifi.h), which:

- Emulates the card's two 16550-compatible UART ports as a 16-byte memory-mapped I/O
  window, matching the real card's default factory address ($9FE0-$9FE7 for the
  network port, $9FE8-$9FEF for the physical serial port).
- Implements a Zimodem-compatible AT command engine behind the network UART:
  `ATD"host:port"` / `ATDT"host:port"` to dial out, `ATA<port>` to listen for an
  incoming connection, `+++` (with standard Hayes guard-time detection) to escape
  back to command mode without hanging up, `ATO` to resume, `ATH` to hang up, plus
  `ATE`, `ATV`, `ATZ`, `ATI`, `ATB`, `AT&W`, and `ATW` for echo/verbosity/reset/info/
  baud/save/WiFi-join compatibility.
- Bridges an active call to a **real TCP socket** on the host machine using the host's
  actual network connection (no ESP32 WiFi stack is simulated; the host already has a
  real network connection, so we use that directly), including basic Telnet IAC
  negotiation stripping for connections to raw telnet servers/BBSes.
- The physical RS-232 UART (second port) is emulated at the register level only;
  nothing is connected to the other end of it yet.

Enable it with the new `-wifi [<address>]` emulator flag (defaults to $9FE0, the
card's factory "IO7-Low" jumper setting):

```sh
./x16emu -wifi
```

See `./x16emu -help` (search for `-wifi`) for the full flag description, and see the
comment header at the top of `x16-emulator/src/esp32wifi.c` for implementation notes,
including the deliberate simplifications (DNS lookups are synchronous/blocking,
`ATW` join always "succeeds" since the host's own network connection is used
directly, and SSH/PETSCII/XON-XOFF dial modifiers are accepted but not implemented).

## Building

Standard CMake build, see [`x16-emulator/README.md`](x16-emulator/README.md) and
[`x16-emulator/quickstart-linux.md`](x16-emulator/quickstart-linux.md) for full
instructions:

```sh
cd x16-emulator
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

You'll also need a `rom.bin` (the X16 system ROM) next to the built `x16emu`
binary; grab the latest release from
[X16Community/x16-rom](https://github.com/X16Community/x16-rom/releases) — it isn't
bundled here, matching upstream's own practice.
