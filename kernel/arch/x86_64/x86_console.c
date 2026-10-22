/* x86_console.c — the x86-64 console after [BOOT_OK].
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 *
 * Two inputs, one line: the PS/2 keyboard (the i8042 port a PC, QEMU, KVM
 * and a VPS's VNC console all have) and COM1. Both are polled; this kernel
 * masks the 8259s (ring3.c) and takes no IRQs. What is typed is echoed to
 * uart_puts, which the UEFI text console mirrors on the screen.
 *
 * The commands are the arm64 shell's login set, on the same modules:
 *   login <seed text>    pp_console_login; the seed is echoed as '*'
 *   create               a first-time passport from RDRAND; [NEW_SEED] once
 *   confirm <seed text>  type it back; echoed as '*'
 *   whoami, logout
 *   ask <question>       the boot model, for whoever is logged in
 *   help
 * The line is wiped after it runs, so no seed stays in memory here.
 *
 * Order: a byte is checked (control bytes other than backspace and Enter are
 * dropped), then stored if the line has room (a longer line is cut, never
 * overrun), then echoed; Enter runs the line, then wipes it. */
#include "x86_console.h"

#include "../../src/passport/pp_console.h"
#include "../../src/swarm/swarm_bootmodel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifndef TEST_HOST
#    include "x86_64_arch.h"
#endif

#define LINE_MAX 256u /* bytes of a line, the terminating 0 included */

static void (*g_out)(const char *);
static char g_line[LINE_MAX];
static uint32_t g_len;
static bool g_after_cr;

static void out(const char *s)
{
    if (g_out) g_out(s);
}

static uint32_t slen(const char *s)
{
    uint32_t n = 0;
    while (s[n]) n++;
    return n;
}

static bool same(const char *a, const char *b)
{
    while (*a && *a == *b) a++, b++;
    return *a == *b;
}

/* 1 when line begins with p. */
static bool prefix(const char *line, const char *p)
{
    while (*p)
        if (*line++ != *p++) return false;
    return true;
}

/* ---------- random bytes: RDRAND ---------- */

static bool rng_fill(uint8_t *p, uint32_t n)
{
#if defined(__x86_64__)
    uint32_t a, b, c, d;
    __asm__ __volatile__("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(1u), "c"(0u));
    if (!(c & (1u << 30))) return false; /* no RDRAND */
    for (uint32_t i = 0; i < n; i += 8) {
        uint64_t v = 0;
        uint8_t ok = 0;
        /* Intel's guidance: a DRNG underflow clears CF; ten tries, then give up */
        for (int t = 0; t < 10 && !ok; t++)
            __asm__ __volatile__("rdrand %0; setc %1" : "=r"(v), "=qm"(ok) : : "cc");
        if (!ok) return false;
        for (uint32_t k = 0; k < 8 && i + k < n; k++) p[i + k] = (uint8_t) (v >> (8 * k));
    }
    return true;
#else
    (void) p;
    (void) n;
    return false;
#endif
}

/* ---------- the commands ---------- */

static void help(void)
{
    out("  login <seed text>    log in with your passport's seed\r\n");
    out("  create               make a new passport (shows its seed once)\r\n");
    out("  confirm <seed text>  type the new seed back to finish\r\n");
    out("  whoami, logout       who is logged in; log out\r\n");
    out("  ask <question>       ask the boot model (logged in)\r\n");
}

static void run(const char *line)
{
    if (!line[0]) return;
    if (prefix(line, "login ")) {
        pp_console_login(line + 6, slen(line + 6), out);
    } else if (same(line, "login")) {
        out("login: login <seed text>\r\n");
    } else if (same(line, "create")) {
        uint8_t rnd[32];
        if (!rng_fill(rnd, sizeof rnd))
            out("[CREATE_FAIL] this CPU gives no hardware random numbers (RDRAND); make the seed "
                "on "
                "another device and log in with it\r\n");
        else
            pp_console_create(rnd, out);
        for (uint32_t i = 0; i < sizeof rnd; i++) ((volatile uint8_t *) rnd)[i] = 0;
    } else if (prefix(line, "confirm ")) {
        pp_console_confirm(line + 8, slen(line + 8), out);
    } else if (same(line, "whoami")) {
        pp_console_whoami(out);
    } else if (same(line, "logout")) {
        pp_console_logout(out);
    } else if (prefix(line, "ask ") || same(line, "ask")) {
        if (!pp_console_logged_in()) {
            out("[AI_REFUSED] log in first: login <seed text>\r\n");
            return;
        }
        const char *q = line[3] ? line + 4 : "";
        swarm_bootmodel_ask(q, slen(q), 0, out);
    } else if (same(line, "help")) {
        help();
    } else {
        out("unknown command; type help\r\n");
    }
}

/* ---------- the line editor ---------- */

/* The seed after "login " or "confirm " is echoed as '*'. */
static bool secret(void)
{
    g_line[g_len] = 0;
    return prefix(g_line, "login ") || prefix(g_line, "confirm ");
}

void x86_console_feed(uint8_t c)
{
    if (c == '\n' && g_after_cr) { /* the LF of a CR LF */
        g_after_cr = false;
        return;
    }
    g_after_cr = c == '\r';
    if (c == '\r' || c == '\n') {
        g_line[g_len] = 0;
        out("\r\n");
        run(g_line);
        for (uint32_t i = 0; i < LINE_MAX; i++) ((volatile char *) g_line)[i] = 0;
        g_len = 0;
        out("> ");
        return;
    }
    if (c == 0x08 || c == 0x7f) {
        if (g_len) {
            g_line[--g_len] = 0;
            out("\b \b");
        }
        return;
    }
    if (c < 0x20 || c > 0x7e) return;  /* control bytes and non-ASCII type nothing */
    if (g_len + 1 >= LINE_MAX) return; /* full: the rest of the line is cut */
    bool hide = secret();
    g_line[g_len++] = (char) c;
    char e[2] = {hide ? '*' : (char) c, 0};
    out(e);
}

/* ---------- PS/2 scancodes (set 1, US layout) ---------- */

static const char SC_PLAIN[0x3a] = {0,   27,  '1', '2', '3',  '4', '5', '6',  '7', '8', '9', '0',
                                    '-', '=', 8,   9,   'q',  'w', 'e', 'r',  't', 'y', 'u', 'i',
                                    'o', 'p', '[', ']', '\r', 0,   'a', 's',  'd', 'f', 'g', 'h',
                                    'j', 'k', 'l', ';', '\'', '`', 0,   '\\', 'z', 'x', 'c', 'v',
                                    'b', 'n', 'm', ',', '.',  '/', 0,   '*',  0,   ' '};
static const char SC_SHIFT[0x3a] = {0,   27,  '!', '@', '#',  '$', '%', '^', '&', '*', '(', ')',
                                    '_', '+', 8,   9,   'Q',  'W', 'E', 'R', 'T', 'Y', 'U', 'I',
                                    'O', 'P', '{', '}', '\r', 0,   'A', 'S', 'D', 'F', 'G', 'H',
                                    'J', 'K', 'L', ':', '"',  '~', 0,   '|', 'Z', 'X', 'C', 'V',
                                    'B', 'N', 'M', '<', '>',  '?', 0,   '*', 0,   ' '};

static bool g_lshift, g_rshift, g_caps, g_ext;

uint8_t x86_console_scancode(uint8_t sc)
{
    if (sc == 0xe0) { /* the next code is an extended key */
        g_ext = true;
        return 0;
    }
    bool ext = g_ext;
    g_ext = false;
    bool up = sc & 0x80;
    uint8_t k = sc & 0x7f;
    if (k == 0x2a) g_lshift = !up;
    if (k == 0x36 && !ext) g_rshift = !up;
    if (up) return 0;
    if (k == 0x3a) {
        g_caps = !g_caps;
        return 0;
    }
    if (ext) return k == 0x1c ? '\r' : 0; /* keypad Enter; arrows and the rest type nothing */
    if (k >= sizeof SC_PLAIN) return 0;
    bool shift = g_lshift || g_rshift;
    char c = shift ? SC_SHIFT[k] : SC_PLAIN[k];
    if (g_caps && ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) c ^= 0x20;
    if (c == 27 || c == 9) return 0; /* Esc and Tab type nothing here */
    return (uint8_t) c;
}

/* ---------- the hardware ---------- */

#ifndef TEST_HOST
#    define COM1     0x3F8
#    define KBD_DATA 0x60
#    define KBD_STAT 0x64

static bool g_kbd;

static bool kbd_wait_in(void) /* room to write */
{
    for (uint32_t i = 0; i < 100000; i++)
        if (!(inb(KBD_STAT) & 2)) return true;
    return false;
}

static bool kbd_wait_out(void) /* a byte to read */
{
    for (uint32_t i = 0; i < 100000; i++)
        if (inb(KBD_STAT) & 1) return true;
    return false;
}

static bool kbd_cmd(uint8_t cmd)
{
    if (!kbd_wait_in()) return false;
    outb(KBD_STAT, cmd);
    return true;
}

/* The i8042 as the firmware left it, made ours: both ports off, the output
 * buffer drained, IRQs off (this kernel polls), translation to set 1 on, the
 * first port back on. A board without the controller reads 0xFF. */
static bool kbd_init(void)
{
    if (inb(KBD_STAT) == 0xff) return false;
    if (!kbd_cmd(0xad) || !kbd_cmd(0xa7)) return false;
    for (uint32_t i = 0; i < 64 && (inb(KBD_STAT) & 1); i++) (void) inb(KBD_DATA);
    if (!kbd_cmd(0x20) || !kbd_wait_out()) return false;
    uint8_t cfg = inb(KBD_DATA);
    cfg = (uint8_t) ((cfg & ~0x03u) | 0x40u); /* no IRQ1/IRQ12; translate */
    cfg = (uint8_t) (cfg & ~0x10u);           /* first port's clock on */
    if (!kbd_cmd(0x60) || !kbd_wait_in()) return false;
    outb(KBD_DATA, cfg);
    if (!kbd_cmd(0xae)) return false;
    return true;
}

void x86_console_init(void (*o)(const char *))
{
    g_out = o;
    g_kbd = kbd_init();
    out(g_kbd ? "  [DRIVER ONLINE] PS/2 keyboard (i8042, polled)\r\n"
              : "  [DRIVER ABSENT] no PS/2 keyboard controller; type on COM1\r\n");
    out("[CONSOLE] type help; login <seed text>, or create for a new passport\r\n");
    out("> ");
}

void x86_console_poll(void)
{
    for (uint32_t n = 0; n < 64 && (inb(COM1 + 5) & 1); n++) x86_console_feed(inb(COM1));
    if (!g_kbd) return;
    for (uint32_t n = 0; n < 64; n++) {
        uint8_t st = inb(KBD_STAT);
        if (!(st & 1)) break;
        uint8_t sc = inb(KBD_DATA);
        if (st & 0x20) continue; /* the mouse port's byte */
        uint8_t c = x86_console_scancode(sc);
        if (c) x86_console_feed(c);
    }
}
#else
void x86_console_init(void (*o)(const char *))
{
    g_out = o;
}

void x86_console_poll(void) {}
#endif
