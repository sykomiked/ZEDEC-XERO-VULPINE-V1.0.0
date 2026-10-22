/* test_x86_console.c — the x86-64 console's line editor and scancode map on
 * the host: what reaches the commands, what is echoed, what is wiped.
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 *
 * The passport and the boot model are stood in for here, recording what the
 * console hands them; their own tests (test_pp_console, test_swarm_bootmodel)
 * cover what they do with it. */
#include "x86_console.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---- stand-ins for pp_console and swarm_bootmodel ---- */
static char g_call[512];
static bool g_in;

int32_t pp_console_login(const char *t, uint32_t n, void (*o)(const char *))
{
    (void) o;
    snprintf(g_call, sizeof g_call, "login[%.*s]", (int) n, t);
    g_in = true;
    return 0;
}
void pp_console_logout(void (*o)(const char *))
{
    (void) o;
    snprintf(g_call, sizeof g_call, "logout");
    g_in = false;
}
void pp_console_whoami(void (*o)(const char *))
{
    (void) o;
    snprintf(g_call, sizeof g_call, "whoami");
}
int32_t pp_console_create(const uint8_t r[32], void (*o)(const char *))
{
    (void) o;
    uint32_t z = 0;
    for (int i = 0; i < 32; i++) z |= r[i];
    snprintf(g_call, sizeof g_call, "create[%s]", z ? "random" : "zero");
    return 0;
}
int32_t pp_console_confirm(const char *t, uint32_t n, void (*o)(const char *))
{
    (void) o;
    snprintf(g_call, sizeof g_call, "confirm[%.*s]", (int) n, t);
    return 0;
}
bool pp_console_logged_in(void)
{
    return g_in;
}
typedef struct swarm_bootmodel_out swarm_bootmodel_out_t;
int32_t swarm_bootmodel_ask(const char *q, uint32_t n, swarm_bootmodel_out_t *r,
                            void (*o)(const char *))
{
    (void) r;
    (void) o;
    snprintf(g_call, sizeof g_call, "ask[%.*s]", (int) n, q);
    return 0;
}

/* ---- the echo ---- */
static char g_echo[4096];
static void out(const char *s)
{
    size_t n = strlen(g_echo);
    snprintf(g_echo + n, sizeof g_echo - n, "%s", s);
}

static int checks, fails;
static void check(bool ok, const char *what)
{
    checks++;
    if (!ok) fails++;
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
}

static void typed(const char *s)
{
    g_call[0] = 0;
    g_echo[0] = 0;
    while (*s) x86_console_feed((uint8_t) *s++);
}

/* press and release one set-1 key; returns what it typed (0 if nothing) */
static uint8_t key(uint8_t sc)
{
    uint8_t c = x86_console_scancode(sc);
    uint8_t r = x86_console_scancode((uint8_t) (sc | 0x80));
    return c ? c : r;
}

int main(void)
{
    x86_console_init(out);

    typed("login ABCDE-FGHIJ\r");
    check(!strcmp(g_call, "login[ABCDE-FGHIJ]"), "a login line reaches pp_console_login whole");
    check(strstr(g_echo, "login ***********") && !strstr(g_echo, "ABCDE"),
          "the seed is echoed as '*'");
    check(strstr(g_echo, "> ") != NULL, "the prompt comes back after the line");

    typed("confirm XYZ\n");
    check(!strcmp(g_call, "confirm[XYZ]") && !strstr(g_echo, "XYZ"),
          "confirm's seed is masked too");

    typed("ask what\r\n");
    check(!strcmp(g_call, "ask[what]"),
          "ask reaches the model when logged in; CR LF runs one line");
    typed("whoami\r");
    check(!strcmp(g_call, "whoami"), "whoami");
    typed("logout\r");
    check(!strcmp(g_call, "logout"), "logout");
    typed("ask what\r");
    check(g_call[0] == 0 && strstr(g_echo, "[AI_REFUSED]"),
          "ask is refused when nobody is logged in");

    typed("loginx\b\b\b\b\b\bwhoami\r");
    check(!strcmp(g_call, "whoami"), "backspace erases, down to an empty line");
    typed("\b\b\r");
    check(g_call[0] == 0, "backspace on an empty line does nothing");

    typed("who\x01\x1b[am\x7fi\r");
    check(!strcmp(g_call, "unknown") || strstr(g_echo, "unknown command"),
          "control bytes type nothing");
    typed("whoo\x01\x7f\x02\x1b"
          "ami\r");
    check(!strcmp(g_call, "whoami"), "a control byte between letters is dropped, DEL erases");

    char lng[600] = "login ";
    memset(lng + 6, 'A', 500);
    lng[506] = '\r';
    typed(lng);
    check(!strncmp(g_call, "login[", 6) && strlen(g_call) == 6 + 249 + 1,
          "an overlong line is cut at 255 bytes");

    typed("create\r");
#if defined(__x86_64__)
    check(!strcmp(g_call, "create[random]") || strstr(g_echo, "[CREATE_FAIL]"),
          "create hands over 32 random bytes (or says RDRAND is missing)");
#endif
    typed("\r");
    check(g_call[0] == 0, "an empty line runs nothing");

    /* scancodes, set 1 */
    check(key(0x1e) == 'a' && key(0x02) == '1' && key(0x39) == ' ', "a, 1 and space");
    x86_console_scancode(0x2a); /* left shift down */
    check(key(0x1e) == 'A' && key(0x02) == '!' && key(0x35) == '?',
          "shift gives capitals and symbols");
    x86_console_scancode(0xaa); /* left shift up */
    check(key(0x1e) == 'a', "shift released");
    key(0x3a); /* caps lock */
    check(key(0x1e) == 'A' && key(0x02) == '1', "caps lock capitalises letters only");
    key(0x3a);
    check(key(0x1c) == '\r' && key(0x0e) == 8, "Enter and backspace");
    x86_console_scancode(0xe0);
    check(x86_console_scancode(0x1c) == '\r', "keypad Enter (E0 1C)");
    x86_console_scancode(0xe0);
    check(x86_console_scancode(0x48) == 0, "an arrow key types nothing");
    x86_console_scancode(0xe0);
    x86_console_scancode(0x36); /* E0 36: a fake shift some boards send */
    check(key(0x1e) == 'a', "an extended 0x36 is not a right shift");
    check(key(0x01) == 0 && key(0x0f) == 0, "Esc and Tab type nothing");
    check(x86_console_scancode(0x9e) == 0, "a release types nothing");

    printf("test_x86_console: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
