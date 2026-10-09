/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zx_notify_host.c — hosted bridge from the notification bus
 * (kernel/src/social/zx_notify.h) to the operating system's notifications.
 *
 * Compiled only into the hosted app (zxv-host); it uses libc and never
 * links into the kernel.
 *
 *   macOS   osascript -e 'display notification "<body>" with title "<title>"
 *           subtitle "<source>"'
 *   Linux   notify-send -u <urgency> -a ZXV -- <title> <body>
 *   other   one line on stdout
 *
 * INJECTION: the command is started with fork + execvp and an argv array, so
 * no shell ever parses user text: quotes, backticks, $( ) and ; are inert.
 * What remains is the AppleScript string literal itself, so every user
 * string is escaped (\ -> \\, " -> \") and every control byte, including
 * CR, LF and TAB, becomes a space, so text can neither close the literal nor
 * start a new AppleScript statement. notify-send gets "--" before its
 * positional arguments (a title starting with '-' is not an option) and its
 * text has & < > as entities, because some servers parse bodies as markup.
 * Invalid UTF-8 becomes '?'.
 *
 * The child is double-forked so the app never waits on the notifier and
 * never leaves a zombie.
 *
 * HONEST LIMITS: osascript shows the notification as coming from "Script
 * Editor" unless the app is signed and uses UNUserNotificationCenter;
 * clicking it does not open the note's action. On Linux, notify-send must
 * be installed and a session bus present; if not, the child fails quietly.
 * Windows prints (no toast). Long strings are cut to the bus's limits.
 */
#if !defined(_WIN32)
#    define _DEFAULT_SOURCE
#    define _DARWIN_C_SOURCE
#    include <sys/types.h>
#    include <sys/wait.h>
#    include <unistd.h>
#endif
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "zx_notify.h"

#define SCRIPT_MAX 1400u
#define ARG_MAX_N  10u

static zxn_host_mode_t g_mode = ZXN_HOST_AUTO;
static char g_argbuf[ARG_MAX_N][SCRIPT_MAX];
static const char *g_argv[ARG_MAX_N + 1];

void zxn_host_set_mode(zxn_host_mode_t mode)
{
    g_mode = mode;
}

const char *zxn_host_last_argv(uint32_t i)
{
    return i < ARG_MAX_N ? g_argv[i] : NULL;
}

/* Length of a valid UTF-8 sequence at p (1..4), or 0 if invalid. */
static uint32_t utf8_len(const unsigned char *p)
{
    unsigned char c = p[0];
    if (c < 0x80) return 1;
    uint32_t n;
    uint32_t cp;
    if (c >= 0xC2 && c <= 0xDF) {
        n = 2;
        cp = c & 0x1Fu;
    } else if (c >= 0xE0 && c <= 0xEF) {
        n = 3;
        cp = c & 0x0Fu;
    } else if (c >= 0xF0 && c <= 0xF4) {
        n = 4;
        cp = c & 0x07u;
    } else
        return 0;
    for (uint32_t i = 1; i < n; i++) {
        if ((p[i] & 0xC0u) != 0x80u) return 0;
        cp = (cp << 6) | (p[i] & 0x3Fu);
    }
    if ((n == 3 && cp < 0x800) || (n == 4 && (cp < 0x10000 || cp > 0x10FFFF)) ||
        (cp >= 0xD800 && cp <= 0xDFFF))
        return 0;
    return n;
}

typedef struct {
    char *out;
    uint32_t cap, len;
} sink_t;

static bool put(sink_t *s, const char *p, uint32_t n)
{
    if (s->len + n + 1u > s->cap) return false;
    memcpy(s->out + s->len, p, n);
    s->len += n;
    return true;
}

/* Shared walk: markup = notify-send rules, else AppleScript rules. Stops at
 * the last whole character that fits, so output never ends mid-escape. */
static uint32_t clean(const char *in, char *out, uint32_t cap, bool markup)
{
    if (!out || cap == 0) return 0;
    sink_t s = {out, cap, 0};
    const unsigned char *p = (const unsigned char *) (in ? in : "");
    while (*p) {
        uint32_t n = utf8_len(p);
        bool ok;
        if (n == 0) {
            ok = put(&s, "?", 1);
            n = 1;
        } else if (n > 1) {
            ok = put(&s, (const char *) p, n);
        } else if (*p < 0x20 || *p == 0x7F) {
            ok = put(&s, " ", 1);
        } else if (!markup && *p == '\\') {
            ok = put(&s, "\\\\", 2);
        } else if (!markup && *p == '"') {
            ok = put(&s, "\\\"", 2);
        } else if (markup && *p == '&') {
            ok = put(&s, "&amp;", 5);
        } else if (markup && *p == '<') {
            ok = put(&s, "&lt;", 4);
        } else if (markup && *p == '>') {
            ok = put(&s, "&gt;", 4);
        } else {
            ok = put(&s, (const char *) p, 1);
        }
        if (!ok) break;
        p += n;
    }
    out[s.len] = 0;
    return s.len;
}

uint32_t zxn_host_escape_applescript(const char *in, char *out, uint32_t cap)
{
    return clean(in, out, cap, false);
}

uint32_t zxn_host_clean_text(const char *in, char *out, uint32_t cap)
{
    return clean(in, out, cap, true);
}

static void run(const char *const *argv)
{
    uint32_t i = 0;
    for (; argv[i] && i < ARG_MAX_N; i++) {
        snprintf(g_argbuf[i], sizeof g_argbuf[i], "%s", argv[i]);
        g_argv[i] = g_argbuf[i];
    }
    for (; i <= ARG_MAX_N; i++) g_argv[i] = NULL;
    if (g_mode == ZXN_HOST_DRY_RUN_MAC || g_mode == ZXN_HOST_DRY_RUN_LINUX) return;
#if !defined(_WIN32)
    pid_t pid = fork();
    if (pid < 0) return;
    if (pid == 0) {
        pid_t inner = fork();
        if (inner == 0) {
            execvp(argv[0], (char *const *) argv);
            _exit(127);
        }
        _exit(0);
    }
    int st;
    (void) waitpid(pid, &st, 0);
#endif
}

static void print_note(const zxn_note_t *n)
{
    char t[ZXN_TITLE_MAX * 2], b[ZXN_BODY_MAX * 2], s[ZXN_SOURCE_MAX * 2];
    zxn_host_clean_text(n->title, t, sizeof t);
    zxn_host_clean_text(n->body, b, sizeof b);
    zxn_host_clean_text(n->source, s, sizeof s);
    if (n->count > 1)
        printf("[notify %s] %s: %s (x%u)\n", s, t, b, (unsigned) n->count);
    else
        printf("[notify %s] %s: %s\n", s, t, b);
    fflush(stdout);
}

void zxn_host_deliver(void *ctx, const zxn_note_t *n, bool coalesced)
{
    (void) ctx;
    (void) coalesced;
    if (!n) return;
    zxn_host_mode_t mode = g_mode;
#if defined(__APPLE__)
    bool mac = true, lin = false;
#elif defined(__linux__)
    bool mac = false, lin = true;
#else
    bool mac = false, lin = false;
#endif
    if (mode == ZXN_HOST_DRY_RUN_MAC) mac = true, lin = false;
    if (mode == ZXN_HOST_DRY_RUN_LINUX) mac = false, lin = true;
    if (mode == ZXN_HOST_PRINT || (mode == ZXN_HOST_AUTO && !mac && !lin)) {
        print_note(n);
        return;
    }
    char title[ZXN_TITLE_MAX * 2 + 16], body[ZXN_BODY_MAX * 2], src[ZXN_SOURCE_MAX * 2];
    char counted[ZXN_TITLE_MAX + 16];
    if (n->count > 1)
        snprintf(counted, sizeof counted, "%s (%u)", n->title, (unsigned) n->count);
    else
        snprintf(counted, sizeof counted, "%s", n->title);
    if (mac) {
        char script[SCRIPT_MAX];
        zxn_host_escape_applescript(counted, title, sizeof title);
        zxn_host_escape_applescript(n->body, body, sizeof body);
        zxn_host_escape_applescript(n->source, src, sizeof src);
        snprintf(script, sizeof script,
                 "display notification \"%s\" with title \"%s\" subtitle \"%s\"", body, title, src);
        const char *argv[] = {"osascript", "-e", script, NULL};
        run(argv);
    } else {
        zxn_host_clean_text(counted, title, sizeof title);
        zxn_host_clean_text(n->body, body, sizeof body);
        const char *urg = n->pri >= ZXN_PRI_URGENT ? "critical"
                          : n->pri == ZXN_PRI_LOW  ? "low"
                                                   : "normal";
        const char *argv[] = {"notify-send", "-u", urg, "-a", "ZXV", "--", title, body, NULL};
        run(argv);
    }
}
