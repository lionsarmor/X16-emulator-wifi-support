// Commander X16 Emulator
// WiFi / ESP32 Network Card emulation
// All rights reserved. License: 2-clause BSD
//
// Emulates the community "921.6Kbps Serial & ESP32 Network Card":
//   - A TL16C2550-compatible dual UART (same register model as the
//     official Serial MIDI card, see midi.c) mapped as two 8-register
//     16550-style ports in a 16-byte I/O window (default $9FE0-$9FEF,
//     matching the card's factory "IO7-Low" jumper setting).
//   - Port 1 ($9FE0-$9FE7 by default) is wired internally to the
//     on-board ESP32-WROOM, which ships preloaded with Bo Zimmerman's
//     "Zimodem" firmware: a Hayes-style AT-command modem that bridges
//     to TCP/IP. We emulate the ESP32 side of that link directly using
//     the host's real network stack, so X16 software that already
//     speaks Zimodem's AT dialect (ATD"host:port", ATA<port>, +++, etc)
//     works unmodified against the real Internet.
//   - Port 2 ($9FE8-$9FEF) is the card's physical RS-232 DE9 port. It
//     is not connected to anything in this emulation (no host serial
//     passthrough yet); it behaves like an idle UART with no device
//     attached on the other end.

#pragma once

#include <stdint.h>
#include <stdbool.h>

// UART reference clock, matches the real card's 14.7456MHz crystal
// (chosen by the card's designer to hit standard baud rates exactly,
// e.g. 14745600 / 16 / 1 = 921600).
#define WIFI_UART_OSC_RATE_MHZ 14.7456f
#define WIFI_UART_PRIMARY_DIVIDER 16

// One-time setup at emulator startup. Safe to call even if the card
// is not installed. Does not touch the network.
void wifi_card_init(void);

// Called on every machine reset (see machine_reset() in main.c). Resets
// the UART register file only; an established network connection (if
// any) is left alone, matching how a real modem doesn't hang up just
// because the host computer was reset.
void wifi_serial_init(void);

// Advance the emulated UART and (throttled) network engine by `clocks`
// CPU cycles. Safe to call unconditionally; it is a cheap no-op unless
// -wifi is enabled.
void wifi_serial_step(int clocks);

uint8_t wifi_serial_read(uint8_t reg, bool debugOn);
void wifi_serial_write(uint8_t reg, uint8_t val);
bool wifi_serial_irq(void);

// Closes any open sockets and releases OS networking resources. Call
// once at emulator shutdown.
void wifi_card_shutdown(void);
