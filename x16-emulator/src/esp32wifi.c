// Commander X16 Emulator
// WiFi / ESP32 Network Card emulation
// All rights reserved. License: 2-clause BSD
//
// See esp32wifi.h for an overview. This file contains two layers:
//
//  1. A TL16C2550-style dual 16550 UART register model (uregs[0] and
//     uregs[1]), timing-accurate to the CPU cycle in the same way as
//     the official Serial MIDI card (see midi.c) -- because it is,
//     per the community hardware docs, the very same UART chip on the
//     very same card design, just wired to different peripherals.
//
//  2. A small Hayes/"Zimodem"-compatible AT-command modem ("zm_"
//     functions) sitting behind UART channel 0 (the network port),
//     which is what the real card's on-board ESP32 speaks. Instead of
//     emulating the ESP32's WiFi radio and TCP/IP stack in software,
//     we hand outgoing bytes to the host OS's real TCP sockets, so
//     software written for the real card also works here, using the
//     host machine's actual network connection.
//
// UART channel 1 (the card's physical RS-232 DE9 port) is emulated at
// the register level only; nothing is connected to the other end of
// that virtual wire in this version.

// Must be defined before any system header is pulled in (including
// transitively via glue.h -> SDL.h): the project builds in strict C11
// mode with GNU extensions off, which otherwise hides POSIX sockets
// declarations (getaddrinfo, select, fd_set, ...) from glibc's
// headers. Matches the same guard main.c uses.
#if !defined(_WIN32) && !defined(__APPLE__)
#define _XOPEN_SOURCE 600
#define _POSIX_C_SOURCE 1
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "glue.h"
#include "esp32wifi.h"
#include "endian.h"

#if defined(__EMSCRIPTEN__)
  // Real TCP sockets aren't available in the browser sandbox this
  // build targets, so the network engine below compiles out to a
  // stub that always reports "no connection". The UART register
  // model still works, so code that probes for the card's presence
  // won't get stuck, it just can't actually get online.
  #define WIFI_HAVE_SOCKETS 0
#else
  #define WIFI_HAVE_SOCKETS 1
#endif

#if WIFI_HAVE_SOCKETS
  #ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    typedef SOCKET sock_t;
    #define SOCK_INVALID INVALID_SOCKET
    #define CLOSESOCK(s) closesocket(s)
    #define SOCK_ERRNO WSAGetLastError()
    #define SOCK_EWOULDBLOCK WSAEWOULDBLOCK
    #define SOCK_EAGAIN WSAEWOULDBLOCK
  #else
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <sys/select.h>
    #include <sys/time.h>
    #include <netinet/in.h>
    #include <netinet/tcp.h>
    #include <arpa/inet.h>
    #include <netdb.h>
    #include <fcntl.h>
    #include <errno.h>
    #include <unistd.h>
    typedef int sock_t;
    #define SOCK_INVALID (-1)
    #define CLOSESOCK(s) close(s)
    #define SOCK_ERRNO errno
    #define SOCK_EWOULDBLOCK EWOULDBLOCK
    #define SOCK_EAGAIN EAGAIN
  #endif
#endif

// ---------------------------------------------------------------------
// Small ring buffer used to stage bytes destined for a UART's receive
// FIFO (ififo). Bytes sit here until the UART's own baud-rate timing
// (below) lets them into the CPU-visible FIFO one at a time, so that
// software polling the UART sees realistic, paced arrival of data
// instead of an entire TCP packet appearing all at once.
// ---------------------------------------------------------------------
#define DEV_RX_SIZE 4096

typedef struct {
    uint8_t buf[DEV_RX_SIZE];
    int head, tail, count;
} ring_t;

static ring_t dev_rx[2];

static bool ring_push(ring_t *r, uint8_t b)
{
    if (r->count >= DEV_RX_SIZE) {
        return false;
    }
    r->buf[r->tail] = b;
    r->tail = (r->tail + 1) % DEV_RX_SIZE;
    r->count++;
    return true;
}

static bool ring_pop(ring_t *r, uint8_t *b)
{
    if (r->count == 0) {
        return false;
    }
    *b = r->buf[r->head];
    r->head = (r->head + 1) % DEV_RX_SIZE;
    r->count--;
    return true;
}

// ---------------------------------------------------------------------
// UART register model (per channel). Field semantics match the real
// TL16C2550 / 16550A and mirror src/midi.c's struct midi_serial_regs.
// ---------------------------------------------------------------------
typedef struct {
    LOW_HIGH_UNION(dl, dll, dlm);

    bool ier_erbi;
    bool ier_etbei;
    bool ier_elsi;
    bool ier_edssi;

    uint8_t iir;

    bool fcr_fifo_enable;
    uint8_t fcr_ififo_trigger_level_bytes;

    uint8_t lcr_word_length_bits;
    bool lcr_stb;
    bool lcr_pen;
    bool lcr_eps;
    bool lcr_stick;
    bool lcr_break;
    bool lcr_dlab;

    bool mcr_dtr;
    bool mcr_rts;
    bool mcr_out1;
    bool mcr_out2;
    bool mcr_loop;
    bool mcr_afe;

    bool lsr_oe;
    bool lsr_pe;
    bool lsr_fe;
    bool lsr_bi;
    bool lsr_eif;

    bool msr_dcts;
    bool msr_ddsr;
    bool msr_teri;
    bool msr_ddcd;
    bool msr_cts;
    bool msr_dsr;
    bool msr_ri;
    bool msr_dcd;

    uint8_t obyte_bits_remain;
    uint8_t ibyte_bits_remain;
    uint8_t rx_timeout;
    bool rx_timeout_enabled;

    bool thre_intr;
    uint8_t thre_bits_remain;

    uint8_t scratch;
    uint8_t ififo[16];
    uint8_t ifsz;
    uint8_t ofifo[16];
    uint8_t ofsz;

    int64_t clock;   // 40.24 fixed point
    int32_t clockdec; // 8.24 fixed point
} wifi_uart_t;

static wifi_uart_t uregs[2];

static void wifi_iir_check(uint8_t sel);
static void wifi_byte_out(uint8_t sel, uint8_t b);
static void zm_feed_out_byte(uint8_t b);
static void zm_poll(void);
static void zm_reset_engine(void);

// ---------------------------------------------------------------------
// UART core (channel-agnostic). This part is deliberately parallel to
// midi_serial_{init,step,read,write,irq} so the two cards' behavior
// stays consistent, since they really are the same UART hardware.
// ---------------------------------------------------------------------

void wifi_serial_init(void)
{
    for (uint8_t sel = 0; sel < 2; sel++) {
        uregs[sel].ier_erbi = false;
        uregs[sel].ier_etbei = false;
        uregs[sel].ier_elsi = false;
        uregs[sel].ier_edssi = false;

        uregs[sel].iir = 0x01;

        uregs[sel].fcr_fifo_enable = false;
        uregs[sel].fcr_ififo_trigger_level_bytes = 1;

        uregs[sel].lcr_word_length_bits = 5;
        uregs[sel].lcr_stb = false;
        uregs[sel].lcr_pen = false;
        uregs[sel].lcr_eps = false;
        uregs[sel].lcr_stick = false;
        uregs[sel].lcr_break = false;
        uregs[sel].lcr_dlab = false;

        uregs[sel].mcr_dtr = false;
        uregs[sel].mcr_rts = false;
        uregs[sel].mcr_out1 = false;
        uregs[sel].mcr_out2 = false;
        uregs[sel].mcr_loop = false;
        uregs[sel].mcr_afe = false;

        uregs[sel].lsr_oe = false;
        uregs[sel].lsr_pe = false;
        uregs[sel].lsr_fe = false;
        uregs[sel].lsr_bi = false;
        uregs[sel].lsr_eif = false;

        // The card's ESP32/UART side is always powered whenever the
        // card is installed, so CTS/DSR read as asserted by default.
        // DCD/RI reflect the actual network connection state, which
        // machine_reset() intentionally does not disturb.
        uregs[sel].msr_dcts = false;
        uregs[sel].msr_ddsr = false;
        uregs[sel].msr_teri = false;
        uregs[sel].msr_ddcd = false;
        uregs[sel].msr_cts = true;
        uregs[sel].msr_dsr = true;
        uregs[sel].msr_ri = false;
        // Leave msr_dcd as-is: it tracks the live network connection.

        uregs[sel].obyte_bits_remain = 0;
        uregs[sel].rx_timeout = 0;
        uregs[sel].rx_timeout_enabled = false;
        uregs[sel].thre_intr = false;
        uregs[sel].thre_bits_remain = 0;

        uregs[sel].ibyte_bits_remain = 7;

        uregs[sel].ifsz = 0;
        uregs[sel].ofsz = 0;
        uregs[sel].clock = 0;
        uregs[sel].clockdec = 0;
        uregs[sel].scratch = 0;
    }
}

static void wifi_iir_check(uint8_t sel)
{
    uint8_t fifoen = (uint8_t)uregs[sel].fcr_fifo_enable << 6 | (uint8_t)uregs[sel].fcr_fifo_enable << 7;
    if (uregs[sel].ier_elsi && (uregs[sel].lsr_oe || uregs[sel].lsr_pe || uregs[sel].lsr_fe || uregs[sel].lsr_bi)) {
        uregs[sel].iir = (0x06 | fifoen);
    } else if (uregs[sel].ier_erbi && !uregs[sel].fcr_fifo_enable && uregs[sel].ifsz > 0) {
        uregs[sel].iir = (0x04 | fifoen);
    } else if (uregs[sel].ier_erbi && uregs[sel].fcr_fifo_enable && uregs[sel].ifsz >= uregs[sel].fcr_ififo_trigger_level_bytes) {
        uregs[sel].iir = (0x04 | fifoen);
    } else if (uregs[sel].ier_erbi && uregs[sel].fcr_fifo_enable && uregs[sel].ifsz > 0 && uregs[sel].rx_timeout_enabled && uregs[sel].rx_timeout == 0) {
        uregs[sel].iir = (0x0c | fifoen);
    } else if (uregs[sel].ier_etbei && uregs[sel].thre_intr) {
        uregs[sel].iir = (0x02 | fifoen);
    } else if (uregs[sel].ier_edssi && ((!uregs[sel].mcr_afe && uregs[sel].msr_dcts) || uregs[sel].msr_ddcd || uregs[sel].msr_ddsr || uregs[sel].msr_teri)) {
        uregs[sel].iir = (0x00 | fifoen);
    } else {
        uregs[sel].iir = (0x01 | fifoen);
    }
}

static void wifi_enqueue_obyte(uint8_t sel, uint8_t val)
{
    if (uregs[sel].ofsz < (uregs[sel].fcr_fifo_enable ? 16 : 1)) {
        uregs[sel].ofifo[uregs[sel].ofsz] = val;
        uregs[sel].thre_intr = false;
        uregs[sel].ofsz++;
        if (uregs[sel].ofsz == 1) {
            if (uregs[sel].fcr_fifo_enable) {
                uregs[sel].thre_bits_remain = 1 + uregs[sel].lcr_word_length_bits + uregs[sel].lcr_stb + uregs[sel].lcr_pen;
            }
        } else {
            uregs[sel].thre_bits_remain = 0;
        }
    } else {
        fprintf(stderr, "WiFi card: Warning: UART %d TX overflow\n", sel);
    }
    wifi_iir_check(sel);
}

static uint8_t wifi_dequeue_ibyte(uint8_t sel)
{
    uint8_t ret = uregs[sel].ififo[0];
    if (uregs[sel].ifsz > 0) {
        uregs[sel].ifsz--;
        if (uregs[sel].ifsz == 0) {
            uregs[sel].rx_timeout_enabled = false;
        } else {
            memmove(uregs[sel].ififo, uregs[sel].ififo + 1, uregs[sel].ifsz);
            uregs[sel].rx_timeout_enabled = true;
            uregs[sel].rx_timeout = 4 * (2 + uregs[sel].lcr_word_length_bits + uregs[sel].lcr_stb + uregs[sel].lcr_pen);
        }
        wifi_iir_check(sel);
    }
    return ret;
}

static void wifi_calculate_clk(uint8_t sel)
{
    double uart_clks_per_cpu = (WIFI_UART_OSC_RATE_MHZ / WIFI_UART_PRIMARY_DIVIDER) / MHZ;
    if (uregs[sel].dl > 0) {
        uart_clks_per_cpu /= uregs[sel].dl;
        uregs[sel].clockdec = (int32_t)(uart_clks_per_cpu * 0x1000000L);
    } else {
        uregs[sel].clockdec = 0;
    }
}

uint8_t wifi_serial_read(uint8_t reg, bool debugOn)
{
    uint8_t sel = (reg & 8) >> 3;
    switch (reg & 7) {
        case 0x0:
            if (uregs[sel].lcr_dlab) {
                return uregs[sel].dll;
            } else if (debugOn) {
                return uregs[sel].ififo[0];
            } else {
                return wifi_dequeue_ibyte(sel);
            }
        case 0x1:
            if (uregs[sel].lcr_dlab) {
                return uregs[sel].dlm;
            }
            return (((uint8_t)uregs[sel].ier_edssi << 3) |
                    ((uint8_t)uregs[sel].ier_elsi << 2) |
                    ((uint8_t)uregs[sel].ier_etbei << 1) |
                    ((uint8_t)uregs[sel].ier_erbi));
        case 0x2: {
            uint8_t ret = uregs[sel].iir;
            if (!debugOn) {
                uregs[sel].thre_intr = false;
                wifi_iir_check(sel);
            }
            return ret;
        }
        case 0x3:
            return (((uint8_t)uregs[sel].lcr_dlab << 7) |
                    ((uint8_t)uregs[sel].lcr_break << 6) |
                    ((uint8_t)uregs[sel].lcr_stick << 5) |
                    ((uint8_t)uregs[sel].lcr_eps << 4) |
                    ((uint8_t)uregs[sel].lcr_pen << 3) |
                    ((uint8_t)uregs[sel].lcr_stb << 2) |
                    ((uregs[sel].lcr_word_length_bits - 5) & 0x3));
        case 0x4:
            return (((uint8_t)uregs[sel].mcr_afe << 5) |
                    ((uint8_t)uregs[sel].mcr_loop << 4) |
                    ((uint8_t)uregs[sel].mcr_out2 << 3) |
                    ((uint8_t)uregs[sel].mcr_out1 << 2) |
                    ((uint8_t)uregs[sel].mcr_rts << 1) |
                    ((uint8_t)uregs[sel].mcr_dtr));
        case 0x5: {
            uint8_t ret = (((uint8_t)uregs[sel].lsr_eif << 7) |
                    ((uint8_t)(uregs[sel].obyte_bits_remain == 0 && uregs[sel].ofsz == 0) << 6) |
                    ((uint8_t)(uregs[sel].ofsz == 0) << 5) |
                    ((uint8_t)uregs[sel].lsr_bi << 4) |
                    ((uint8_t)uregs[sel].lsr_fe << 3) |
                    ((uint8_t)uregs[sel].lsr_pe << 2) |
                    ((uint8_t)uregs[sel].lsr_oe << 1) |
                    ((uint8_t)(uregs[sel].ifsz > 0)));
            if (!debugOn) {
                uregs[sel].lsr_oe = false;
                uregs[sel].lsr_pe = false;
                uregs[sel].lsr_fe = false;
                uregs[sel].lsr_bi = false;
            }
            return ret;
        }
        case 0x6:
            return (((uint8_t)uregs[sel].msr_dcd << 7) |
                    ((uint8_t)uregs[sel].msr_ri << 6) |
                    ((uint8_t)uregs[sel].msr_dsr << 5) |
                    ((uint8_t)uregs[sel].msr_cts << 4) |
                    ((uint8_t)uregs[sel].msr_ddcd << 3) |
                    ((uint8_t)uregs[sel].msr_teri << 2) |
                    ((uint8_t)uregs[sel].msr_ddsr << 1) |
                    ((uint8_t)uregs[sel].msr_dcts));
        case 0x7:
            return uregs[sel].scratch;
    }
    return 0x00;
}

void wifi_serial_write(uint8_t reg, uint8_t val)
{
    uint8_t sel = (reg & 8) >> 3;
    switch (reg & 7) {
        case 0x0:
            if (uregs[sel].lcr_dlab) {
                uregs[sel].dll = val;
                wifi_calculate_clk(sel);
            } else {
                // Unlike the MIDI card, this card doesn't require a
                // fixed baud rate/format, so every byte is accepted
                // regardless of the current LCR setting.
                wifi_enqueue_obyte(sel, val);
            }
            break;
        case 0x1:
            if (uregs[sel].lcr_dlab) {
                uregs[sel].dlm = val;
                wifi_calculate_clk(sel);
            } else {
                uregs[sel].ier_erbi = !!(val & 1);
                uregs[sel].ier_etbei = !!(val & 2);
                uregs[sel].ier_elsi = !!(val & 4);
                uregs[sel].ier_edssi = !!(val & 8);
            }
            break;
        case 0x2:
            if (val & 1) {
                uregs[sel].fcr_fifo_enable = true;
                switch ((val & 0xc0) >> 6) {
                    case 0: uregs[sel].fcr_ififo_trigger_level_bytes = 1; break;
                    case 1: uregs[sel].fcr_ififo_trigger_level_bytes = 4; break;
                    case 2: uregs[sel].fcr_ififo_trigger_level_bytes = 8; break;
                    case 3: uregs[sel].fcr_ififo_trigger_level_bytes = 14; break;
                }
            } else {
                uregs[sel].fcr_fifo_enable = false;
                uregs[sel].fcr_ififo_trigger_level_bytes = 1;
            }
            if (val & 2) {
                uregs[sel].ifsz = 0;
            }
            if (val & 4) {
                uregs[sel].ofsz = 0;
            }
            if (uregs[sel].thre_bits_remain == 0) {
                uregs[sel].thre_intr = true;
            }
            wifi_iir_check(sel);
            break;
        case 0x3:
            uregs[sel].lcr_word_length_bits = (val & 0x03) + 5;
            uregs[sel].lcr_stb = !!(val & 0x04);
            uregs[sel].lcr_pen = !!(val & 0x08);
            uregs[sel].lcr_eps = !!(val & 0x10);
            uregs[sel].lcr_stick = !!(val & 0x20);
            uregs[sel].lcr_break = !!(val & 0x40);
            uregs[sel].lcr_dlab = !!(val & 0x80);
            if (dev_rx[sel].count == 0) {
                uregs[sel].ibyte_bits_remain = 2 + uregs[sel].lcr_word_length_bits + uregs[sel].lcr_stb + uregs[sel].lcr_pen;
            }
            break;
        case 0x4:
            uregs[sel].mcr_dtr = !!(val & 0x01);
            uregs[sel].mcr_rts = !!(val & 0x02);
            uregs[sel].mcr_out1 = !!(val & 0x04);
            uregs[sel].mcr_out2 = !!(val & 0x08);
            uregs[sel].mcr_loop = !!(val & 0x10);
            uregs[sel].mcr_afe = !!(val & 0x20);
            if (uregs[sel].mcr_loop) {
                uregs[sel].msr_dcts |= uregs[sel].msr_cts ^ uregs[sel].mcr_dtr;
                uregs[sel].msr_cts = uregs[sel].mcr_dtr;
                uregs[sel].msr_ddsr |= uregs[sel].msr_dsr ^ uregs[sel].mcr_rts;
                uregs[sel].msr_dsr = uregs[sel].mcr_rts;
                if (uregs[sel].msr_ri) {
                    uregs[sel].msr_teri |= uregs[sel].msr_ri ^ uregs[sel].mcr_out1;
                }
                uregs[sel].msr_ri = uregs[sel].mcr_out1;
                uregs[sel].msr_ddcd |= uregs[sel].msr_dcd ^ uregs[sel].mcr_out2;
                uregs[sel].msr_dcd = uregs[sel].mcr_out2;
            } else {
                // Not in loopback: CTS/DSR are always asserted (the
                // ESP32 side is always ready); DCD/RI are driven by
                // the network engine instead of by MCR.
                uregs[sel].msr_cts = true;
                uregs[sel].msr_dsr = true;
            }
            break;
        case 0x7:
            uregs[sel].scratch = val;
            break;
    }
}

bool wifi_serial_irq(void)
{
    bool uart0int = (uregs[0].iir & 1) == 0 && uregs[0].mcr_out2;
    bool uart1int = (uregs[1].iir & 1) == 0 && uregs[1].mcr_out2;
    return uart0int || uart1int;
}

// Global emulated-cycle clock, used only for pacing the network engine
// and the +++ escape guard-time / dial timeout. Never reset by
// machine_reset(), which keeps escape/dial timing sane across a CPU
// reset that happens to land mid-sequence.
static int64_t g_cycle_total = 0;
static int64_t g_poll_accum = 0;
#define WIFI_POLL_INTERVAL_US 2000 // poll host sockets ~500x/sec of emulated time

void wifi_serial_step(int clocks)
{
    if (!has_wifi_card) {
        return;
    }

    g_cycle_total += clocks;

    for (uint8_t sel = 0; sel < 2; sel++) {
        uregs[sel].clock -= (int64_t)uregs[sel].clockdec * clocks;
        while (uregs[sel].clock < 0) {
            if (uregs[sel].obyte_bits_remain > 0) {
                uregs[sel].obyte_bits_remain--;
            }
            if (uregs[sel].ofsz > 0) {
                if (uregs[sel].obyte_bits_remain == 0) {
                    wifi_byte_out(sel, uregs[sel].ofifo[0]);
                    uregs[sel].ofsz--;
                    if (uregs[sel].ofsz > 0) {
                        memmove(uregs[sel].ofifo, uregs[sel].ofifo + 1, uregs[sel].ofsz);
                    } else if (uregs[sel].thre_bits_remain == 0) {
                        uregs[sel].thre_intr = true;
                    }
                    uregs[sel].obyte_bits_remain = 2 + uregs[sel].lcr_word_length_bits + uregs[sel].lcr_stb + uregs[sel].lcr_pen;
                }
            } else if (uregs[sel].thre_bits_remain > 0) {
                uregs[sel].thre_bits_remain--;
                if (uregs[sel].thre_bits_remain == 0) {
                    uregs[sel].thre_intr = true;
                }
            }

            if (uregs[sel].rx_timeout > 0) {
                uregs[sel].rx_timeout--;
            }

            if (uregs[sel].ibyte_bits_remain > 0 && dev_rx[sel].count > 0) {
                uregs[sel].ibyte_bits_remain--;
            }
            if (uregs[sel].ibyte_bits_remain == 0 && dev_rx[sel].count > 0) {
                uint8_t b;
                if (uregs[sel].ifsz < (uregs[sel].fcr_fifo_enable ? 16 : 1)) {
                    ring_pop(&dev_rx[sel], &b);
                    uregs[sel].ififo[uregs[sel].ifsz++] = b;
                    uregs[sel].rx_timeout_enabled = true;
                    uregs[sel].rx_timeout = 4 * (2 + uregs[sel].lcr_word_length_bits + uregs[sel].lcr_stb + uregs[sel].lcr_pen);
                } else {
                    uregs[sel].lsr_oe = true;
                    if (ring_pop(&dev_rx[sel], &b) && !uregs[sel].fcr_fifo_enable && uregs[sel].ifsz > 0) {
                        uregs[sel].ififo[uregs[sel].ifsz - 1] = b;
                    }
                }
                uregs[sel].ibyte_bits_remain = 2 + uregs[sel].lcr_word_length_bits + uregs[sel].lcr_stb + uregs[sel].lcr_pen;
            }

            uregs[sel].clock += 0x1000000LL;
            wifi_iir_check(sel);
        }
    }

    g_poll_accum += clocks;
    int64_t threshold = (int64_t)WIFI_POLL_INTERVAL_US * MHZ;
    if (threshold <= 0) {
        threshold = 1;
    }
    if (g_poll_accum >= threshold) {
        g_poll_accum = 0;
        zm_poll();
    }
}

static void wifi_byte_out(uint8_t sel, uint8_t b)
{
    if (sel == 0) {
        zm_feed_out_byte(b);
    }
    // sel == 1: the physical RS-232 port has nothing attached; bytes
    // written to it are simply discarded.
}

// =======================================================================
// Zimodem-compatible AT command engine (UART channel 0 only)
// =======================================================================

typedef enum {
    ZM_COMMAND,
    ZM_DIALING,
    ZM_CONNECTED,
} zm_state_t;

static zm_state_t zm_state = ZM_COMMAND;

#if WIFI_HAVE_SOCKETS
static sock_t zm_sock = SOCK_INVALID;
static sock_t zm_listen_sock = SOCK_INVALID;
#endif

static char zm_cmdbuf[256];
static int zm_cmdlen = 0;

static bool zm_echo = true;
static bool zm_verbose = true;
static bool zm_telnet_mode = false;

#if WIFI_HAVE_SOCKETS
static int64_t zm_dial_start_cycle = 0;
#endif
#define ZM_DIAL_TIMEOUT_SECONDS 15

// +++ escape guard-time state machine
static int zm_esc_count = 0;
static uint8_t zm_esc_pending[3];
static int64_t zm_esc_last_cycle = 0;
static int64_t zm_last_data_byte_cycle = -1000000000LL;

// small outbound coalescing buffer for the live TCP connection
#define ZM_TXBUF_SIZE 4096
#if WIFI_HAVE_SOCKETS
static uint8_t zm_txbuf[ZM_TXBUF_SIZE];
#endif
static int zm_txlen = 0;

// telnet IAC filter state (incoming direction only)
static int zm_telnet_st = 0;
#if WIFI_HAVE_SOCKETS
static uint8_t zm_telnet_cmd = 0;
#endif

static void zm_emit(const char *s)
{
    while (*s) {
        if (!ring_push(&dev_rx[0], (uint8_t)*s)) {
            uregs[0].lsr_oe = true;
        }
        s++;
    }
}

static void zm_result(const char *text, int code)
{
    if (zm_verbose) {
        zm_emit("\r\n");
        zm_emit(text);
        zm_emit("\r\n");
    } else {
        char buf[8];
        snprintf(buf, sizeof(buf), "%d\r", code);
        zm_emit(buf);
    }
}

static int64_t guard_cycles(void)
{
    // ~1 real second's worth of emulated CPU cycles, computed from the
    // current clock speed so it stays correct if -mhz changes it.
    return (int64_t)1000000LL * (MHZ ? MHZ : 8);
}

#if WIFI_HAVE_SOCKETS
static void sock_set_nonblocking(sock_t s)
{
#ifdef _WIN32
    u_long mode = 1;
    ioctlsocket(s, FIONBIO, &mode);
#else
    int flags = fcntl(s, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(s, F_SETFL, flags | O_NONBLOCK);
    }
#endif
}

static void zm_set_dcd(bool up)
{
    if (uregs[0].msr_dcd != up) {
        uregs[0].msr_ddcd = true;
    }
    uregs[0].msr_dcd = up;
}

static void zm_close_data_sock(void)
{
    if (zm_sock != SOCK_INVALID) {
        CLOSESOCK(zm_sock);
        zm_sock = SOCK_INVALID;
    }
    zm_txlen = 0;
    zm_telnet_st = 0;
}

static void zm_connection_lost(void)
{
    bool was_up = (zm_state == ZM_CONNECTED);
    zm_close_data_sock();
    zm_state = ZM_COMMAND;
    zm_set_dcd(false);
    if (was_up) {
        zm_result("NO CARRIER", 3);
    }
}
#endif // WIFI_HAVE_SOCKETS

static void zm_do_reset(void)
{
#if WIFI_HAVE_SOCKETS
    zm_close_data_sock();
    if (zm_listen_sock != SOCK_INVALID) {
        CLOSESOCK(zm_listen_sock);
        zm_listen_sock = SOCK_INVALID;
    }
    zm_set_dcd(false);
#endif
    zm_state = ZM_COMMAND;
    zm_echo = true;
    zm_verbose = true;
    zm_telnet_mode = false;
    zm_esc_count = 0;
}

static void zm_do_hangup(void)
{
#if WIFI_HAVE_SOCKETS
    zm_close_data_sock();
    if (zm_listen_sock != SOCK_INVALID) {
        CLOSESOCK(zm_listen_sock);
        zm_listen_sock = SOCK_INVALID;
    }
    zm_set_dcd(false);
#endif
    zm_state = ZM_COMMAND;
    zm_esc_count = 0;
}

// Parses the "PTEXS" modifier letters Zimodem allows between the
// command letter and the quoted argument. We recognize 'T' (enable
// basic Telnet IAC filtering); the others are accepted for command
// compatibility but have no effect in this emulation.
#if WIFI_HAVE_SOCKETS
static const char *zm_skip_modifiers(const char *p, bool *telnet)
{
    while (*p && *p != '"' && strchr("PTEXSpteXs", *p)) {
        if (*p == 'T' || *p == 't') {
            *telnet = true;
        }
        p++;
    }
    return p;
}
#endif

static void zm_do_dial(const char *args)
{
#if WIFI_HAVE_SOCKETS
    bool telnet = false;
    args = zm_skip_modifiers(args, &telnet);

    bool quoted = false;
    if (*args == '"') {
        quoted = true;
        args++;
    }

    char raw[300];
    int n = 0;
    while (*args && *args != '\r' && *args != '\n' && !(quoted && *args == '"') && n < (int)sizeof(raw) - 1) {
        raw[n++] = *args++;
    }
    raw[n] = 0;

    char *colon = strrchr(raw, ':');
    if (!colon || colon == raw || !colon[1]) {
        zm_result("ERROR", 4);
        return;
    }
    *colon = 0;
    const char *host = raw;
    const char *portstr = colon + 1;

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo *res = NULL;
    if (getaddrinfo(host, portstr, &hints, &res) != 0 || !res) {
        zm_result("NO CARRIER", 3);
        return;
    }

    sock_t s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (s == SOCK_INVALID) {
        freeaddrinfo(res);
        zm_result("NO CARRIER", 3);
        return;
    }
    sock_set_nonblocking(s);
    connect(s, res->ai_addr, (int)res->ai_addrlen); // expected to return EINPROGRESS/WOULDBLOCK
    freeaddrinfo(res);

    zm_sock = s;
    zm_telnet_mode = telnet;
    zm_telnet_st = 0;
    zm_state = ZM_DIALING;
    zm_dial_start_cycle = g_cycle_total;
#else
    (void)args;
    zm_result("NO CARRIER", 3);
#endif
}

static void zm_do_answer(const char *args)
{
#if WIFI_HAVE_SOCKETS
    bool telnet = false;
    args = zm_skip_modifiers(args, &telnet);

    char *end = NULL;
    long port = strtol(args, &end, 10);
    if (end == args || port <= 0 || port > 65535) {
        zm_result("ERROR", 4);
        return;
    }

    sock_t ls = socket(AF_INET, SOCK_STREAM, 0);
    if (ls == SOCK_INVALID) {
        zm_result("ERROR", 4);
        return;
    }
    int yes = 1;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof(yes));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((uint16_t)port);
    if (bind(ls, (struct sockaddr *)&addr, sizeof(addr)) != 0 || listen(ls, 1) != 0) {
        CLOSESOCK(ls);
        zm_result("ERROR", 4);
        return;
    }
    sock_set_nonblocking(ls);

    if (zm_listen_sock != SOCK_INVALID) {
        CLOSESOCK(zm_listen_sock);
    }
    zm_listen_sock = ls;
    zm_telnet_mode = telnet;
    zm_result("OK", 0);
#else
    (void)args;
    zm_result("ERROR", 4);
#endif
}

static void zm_do_online(void)
{
#if WIFI_HAVE_SOCKETS
    if (zm_sock != SOCK_INVALID) {
        zm_state = ZM_CONNECTED;
        zm_result("CONNECT", 1);
        return;
    }
#endif
    zm_result("NO CARRIER", 3);
}

static void zm_do_wifi(const char *args)
{
    if (*args == 0) {
        // "List networks": we're always bridged through the host's
        // real network connection, so report a single synthetic AP.
        zm_emit("\r\n+WIFI: \"HOST-NETWORK\",-40\r\n");
    }
    // Whether listing or joining a specific SSID, we're already
    // "online" via the host, so just confirm success.
    zm_result("OK", 0);
}

static bool starts_with_at(const char *s)
{
    return (s[0] == 'A' || s[0] == 'a') && (s[1] == 'T' || s[1] == 't');
}

static void zm_process_command(const char *line)
{
    while (*line == ' ') {
        line++;
    }
    if (*line == 0) {
        return; // blank line: real modems ignore it silently
    }
    if (!starts_with_at(line)) {
        zm_result("ERROR", 4);
        return;
    }

    const char *p = line + 2;
    if (*p == 0) {
        zm_result("OK", 0);
        return;
    }

    while (*p) {
        char c = (char)toupper((unsigned char)*p);
        switch (c) {
            case 'Z':
                p++;
                zm_do_reset();
                break;
            case 'E':
                p++;
                if (*p == '0') { zm_echo = false; p++; }
                else if (*p == '1') { zm_echo = true; p++; }
                else { zm_echo = true; }
                break;
            case 'V':
                p++;
                if (*p == '0') { zm_verbose = false; p++; }
                else if (*p == '1') { zm_verbose = true; p++; }
                else { zm_verbose = true; }
                break;
            case 'H':
                p++;
                if (*p == '0' || *p == '1') { p++; }
                zm_do_hangup();
                break;
            case 'O':
                p++;
                zm_do_online();
                return;
            case 'I':
                p++;
                while (isdigit((unsigned char)*p)) { p++; }
                zm_emit("\r\nCommander X16 Emulated ESP32 WiFi Network Card\r\n");
                break;
            case 'B':
                p++;
                while (isdigit((unsigned char)*p)) { p++; } // baud value accepted, no functional effect
                break;
            case '&':
                p++;
                if (*p) { p++; } // consume one option letter (e.g. &W, &F): accepted as a no-op
                while (isdigit((unsigned char)*p)) { p++; }
                break;
            case 'S':
                p++;
                while (isdigit((unsigned char)*p)) { p++; }
                if (*p == '=') {
                    p++;
                    while (isdigit((unsigned char)*p)) { p++; }
                } else if (*p == '?') {
                    p++;
                    zm_emit("\r\n0\r\n");
                }
                break;
            case 'D':
                p++;
                zm_do_dial(p);
                return;
            case 'A':
                p++;
                zm_do_answer(p);
                return;
            case 'W':
                p++;
                zm_do_wifi(p);
                return;
            default:
                zm_result("ERROR", 4);
                return;
        }
    }
    zm_result("OK", 0);
}

#if WIFI_HAVE_SOCKETS
static void zm_tx_flush(void)
{
    if (zm_sock == SOCK_INVALID || zm_txlen == 0) {
        return;
    }
    int sent_total = 0;
    while (sent_total < zm_txlen) {
        int n = send(zm_sock, (const char *)zm_txbuf + sent_total, zm_txlen - sent_total, 0);
        if (n <= 0) {
            int err = SOCK_ERRNO;
            if (n < 0 && (err == SOCK_EWOULDBLOCK || err == SOCK_EAGAIN)) {
                break;
            }
            zm_connection_lost();
            return;
        }
        sent_total += n;
    }
    if (sent_total > 0) {
        memmove(zm_txbuf, zm_txbuf + sent_total, zm_txlen - sent_total);
        zm_txlen -= sent_total;
    }
}

static void zm_tx_byte(uint8_t b)
{
    if (zm_txlen < ZM_TXBUF_SIZE) {
        zm_txbuf[zm_txlen++] = b;
    }
    if (zm_txlen >= ZM_TXBUF_SIZE) {
        zm_tx_flush();
    }
}
#endif

static void zm_flush_pending_escape(void)
{
#if WIFI_HAVE_SOCKETS
    for (int i = 0; i < zm_esc_count; i++) {
        zm_tx_byte(zm_esc_pending[i]);
    }
#endif
    zm_esc_count = 0;
}

// Handles one outgoing (CPU -> remote) byte while a call is active,
// including standard Hayes "+++" guarded escape-to-command-mode
// detection.
static void zm_handle_data_byte(uint8_t b)
{
    int64_t now = g_cycle_total;
    int64_t gc = guard_cycles();

    if (b == '+') {
        if (zm_esc_count == 0 && (now - zm_last_data_byte_cycle) >= gc) {
            zm_esc_pending[zm_esc_count++] = b;
            zm_esc_last_cycle = now;
            zm_last_data_byte_cycle = now;
            return;
        } else if (zm_esc_count > 0 && zm_esc_count < 3 && (now - zm_esc_last_cycle) < gc) {
            zm_esc_pending[zm_esc_count++] = b;
            zm_esc_last_cycle = now;
            zm_last_data_byte_cycle = now;
            return;
        }
    }

    zm_flush_pending_escape();
#if WIFI_HAVE_SOCKETS
    zm_tx_byte(b);
#endif
    zm_last_data_byte_cycle = now;
}

static void zm_feed_out_byte(uint8_t b)
{
    if (zm_state == ZM_CONNECTED) {
        zm_handle_data_byte(b);
        return;
    }

    // Command mode (also while ZM_DIALING: a stray keypress here just
    // gets queued for the next command line, matching a real modem's
    // behavior of ignoring the keyboard mid-dial).
    if (zm_echo) {
        ring_push(&dev_rx[0], b);
    }
    if (b == '\r' || b == '\n') {
        zm_cmdbuf[zm_cmdlen] = 0;
        zm_process_command(zm_cmdbuf);
        zm_cmdlen = 0;
    } else if (b == 0x08 || b == 0x7f) {
        if (zm_cmdlen > 0) {
            zm_cmdlen--;
        }
    } else if (zm_cmdlen < (int)sizeof(zm_cmdbuf) - 1) {
        zm_cmdbuf[zm_cmdlen++] = (char)b;
    }
}

#if WIFI_HAVE_SOCKETS
#define TELNET_IAC 255
#define TELNET_SB  250
#define TELNET_SE  240
#define TELNET_WILL 251
#define TELNET_WONT 252
#define TELNET_DO   253
#define TELNET_DONT 254

static void zm_feed_in_byte(uint8_t b)
{
    if (zm_telnet_mode) {
        switch (zm_telnet_st) {
            case 0:
                if (b == TELNET_IAC) {
                    zm_telnet_st = 1;
                    return;
                }
                break;
            case 1:
                if (b == TELNET_IAC) {
                    zm_telnet_st = 0;
                    break; // escaped 0xFF: fall through and deliver it literally
                } else if (b == TELNET_WILL || b == TELNET_WONT || b == TELNET_DO || b == TELNET_DONT) {
                    zm_telnet_cmd = b;
                    zm_telnet_st = 2;
                    return;
                } else if (b == TELNET_SB) {
                    zm_telnet_st = 3;
                    return;
                } else {
                    zm_telnet_st = 0; // other 2-byte IAC commands (NOP, GA, ...): swallow
                    return;
                }
            case 2: {
                uint8_t reply[3];
                reply[0] = TELNET_IAC;
                reply[1] = (zm_telnet_cmd == TELNET_WILL || zm_telnet_cmd == TELNET_WONT) ? TELNET_DONT : TELNET_WONT;
                reply[2] = b;
                if (zm_sock != SOCK_INVALID) {
                    send(zm_sock, (const char *)reply, 3, 0);
                }
                zm_telnet_st = 0;
                return;
            }
            case 3:
                if (b == TELNET_IAC) {
                    zm_telnet_st = 4;
                }
                return;
            case 4:
                zm_telnet_st = (b == TELNET_SE) ? 0 : 3;
                return;
        }
    }
    if (!ring_push(&dev_rx[0], b)) {
        uregs[0].lsr_oe = true;
    }
}

static void zm_poll(void)
{
    // Escape sequence completion: 3 pluses followed by guard-time silence.
    if (zm_esc_count == 3 && (g_cycle_total - zm_esc_last_cycle) >= guard_cycles()) {
        zm_esc_count = 0;
        zm_state = ZM_COMMAND;
        zm_result("OK", 0);
    } else if (zm_esc_count > 0 && zm_esc_count < 3 && (g_cycle_total - zm_esc_last_cycle) >= 2 * guard_cycles()) {
        // Incomplete escape attempt that was never completed or
        // aborted by further typing: treat the held '+'s as data.
        zm_flush_pending_escape();
    }

    if (zm_state == ZM_DIALING) {
        fd_set wfds, efds;
        FD_ZERO(&wfds);
        FD_ZERO(&efds);
        FD_SET(zm_sock, &wfds);
        FD_SET(zm_sock, &efds);
        struct timeval tv = {0, 0};
        int n = select((int)zm_sock + 1, NULL, &wfds, &efds, &tv);
        if (n > 0) {
            int err = 0;
            socklen_t len = sizeof(err);
            getsockopt(zm_sock, SOL_SOCKET, SO_ERROR, (char *)&err, &len);
            if (err == 0 && FD_ISSET(zm_sock, &wfds) && !FD_ISSET(zm_sock, &efds)) {
                zm_state = ZM_CONNECTED;
                zm_set_dcd(true);
                zm_result("CONNECT", 1);
            } else {
                zm_close_data_sock();
                zm_state = ZM_COMMAND;
                zm_result("NO CARRIER", 3);
            }
        } else if ((g_cycle_total - zm_dial_start_cycle) > (int64_t)ZM_DIAL_TIMEOUT_SECONDS * guard_cycles()) {
            zm_close_data_sock();
            zm_state = ZM_COMMAND;
            zm_result("NO CARRIER", 3);
        }
    } else if (zm_listen_sock != SOCK_INVALID && zm_state == ZM_COMMAND) {
        sock_t accepted = accept(zm_listen_sock, NULL, NULL);
        if (accepted != SOCK_INVALID) {
            CLOSESOCK(zm_listen_sock);
            zm_listen_sock = SOCK_INVALID;
            sock_set_nonblocking(accepted);
            zm_sock = accepted;
            zm_state = ZM_CONNECTED;
            zm_set_dcd(true);
            zm_result("RING", 2);
            zm_result("CONNECT", 1);
        }
    }

    if (zm_state == ZM_CONNECTED && zm_sock != SOCK_INVALID) {
        uint8_t buf[1024];
        int n = recv(zm_sock, (char *)buf, sizeof(buf), 0);
        if (n > 0) {
            for (int i = 0; i < n; i++) {
                zm_feed_in_byte(buf[i]);
            }
        } else if (n == 0) {
            zm_connection_lost();
        } else {
            int err = SOCK_ERRNO;
            if (err != SOCK_EWOULDBLOCK && err != SOCK_EAGAIN) {
                zm_connection_lost();
            }
        }
        zm_tx_flush();
    }
}
#else
static void zm_poll(void)
{
    if (zm_esc_count == 3 && (g_cycle_total - zm_esc_last_cycle) >= guard_cycles()) {
        zm_esc_count = 0;
        zm_state = ZM_COMMAND;
        zm_result("OK", 0);
    } else if (zm_esc_count > 0 && zm_esc_count < 3 && (g_cycle_total - zm_esc_last_cycle) >= 2 * guard_cycles()) {
        zm_flush_pending_escape();
    }
}
#endif

static void zm_reset_engine(void)
{
    zm_state = ZM_COMMAND;
    zm_cmdlen = 0;
    zm_echo = true;
    zm_verbose = true;
    zm_telnet_mode = false;
    zm_esc_count = 0;
    zm_txlen = 0;
    zm_telnet_st = 0;
    zm_last_data_byte_cycle = -1000000000LL;
#if WIFI_HAVE_SOCKETS
    zm_sock = SOCK_INVALID;
    zm_listen_sock = SOCK_INVALID;
#endif
}

// ---------------------------------------------------------------------
// Public lifecycle API
// ---------------------------------------------------------------------

void wifi_card_init(void)
{
    memset(&dev_rx[0], 0, sizeof(dev_rx[0]));
    memset(&dev_rx[1], 0, sizeof(dev_rx[1]));
    zm_reset_engine();
#if WIFI_HAVE_SOCKETS && defined(_WIN32)
    static bool wsa_started = false;
    if (!wsa_started) {
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
        wsa_started = true;
    }
#endif
    fprintf(stderr, "WiFi/ESP32 network card enabled at $%04X.\n", wifi_card_addr);
}

void wifi_card_shutdown(void)
{
#if WIFI_HAVE_SOCKETS
    if (zm_sock != SOCK_INVALID) {
        CLOSESOCK(zm_sock);
        zm_sock = SOCK_INVALID;
    }
    if (zm_listen_sock != SOCK_INVALID) {
        CLOSESOCK(zm_listen_sock);
        zm_listen_sock = SOCK_INVALID;
    }
#ifdef _WIN32
    WSACleanup();
#endif
#endif
}
