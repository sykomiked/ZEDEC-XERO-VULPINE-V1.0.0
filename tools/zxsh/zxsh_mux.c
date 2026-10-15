/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxsh_mux.c — sub-terminals built into zxsh (zxsh_mux.h).
 *
 *   M1 PANES. Each pane is a pseudo-terminal (posix_openpt) running the
 *      shell, or `self -c CMD` for a command pane, in its own session with
 *      ZXSH_MUX=1, ZXSH_MUX_SOCK=<socket> and TERM=xterm-256color. A shell
 *      pane closes when its shell exits; a finished command pane stays open
 *      ("done - Ctrl-B x closes") so its output and verdict can be read.
 *   M2 VERDICTS. Every pane shows its verdict letter in its title bar: the
 *      last OSC 7770 report (zxsh prints ESC ] 7770 ; N BEL after each
 *      command), and for a finished command the rule C tag of zxsh_mux.h.
 *      A tab's letter on the status line is zmux_combine of its panes.
 *   M3 CONTROL SOCKET. <home>/mux-<pid>.sock, mode 0600, removed on exit.
 *      One request line per client: kind TAB dir TAB cwd {TAB cmd}, with
 *      \t \n \\ escaped. Replies: "ok", "err TEXT", or "tag N" (together,
 *      sent once every pane of the request has finished).
 *   M4 SCREEN. Each pane has a VT emulator (zvt_*). The whole screen is
 *      composed into a cell grid, diffed against the last frame, and only
 *      changed cells are written, at most ~60 frames a second, on the
 *      terminal's alternate screen in raw mode. Leaving (all panes closed,
 *      Ctrl-B q, SIGTERM, SIGHUP) restores termios, cursor and main screen.
 *   M5 KEYS. Ctrl-B then: % split right, " split below, arrows or o move
 *      focus, c new tab, n p next/previous tab, 1-9 go to tab, x close pane
 *      (SIGHUP to its process group, SIGKILL after 1 s), q leave, ? help,
 *      Ctrl-B a literal Ctrl-B. Everything else goes to the focused pane.
 *   M6 BOUNDS. Every write into a cell array is clamped to the array; any
 *      byte sequence is consumed safely (the tests fuzz it).
 *   M7 LIMITS. Every character is one column wide; no mouse, no scrollback,
 *      no copy mode, no origin mode or insert mode; keys typed while a pane's
 *      pty is full are dropped. A pane's new cwd is the focused pane's cwd
 *      where /proc shows it (Linux), else the multiplexer's.
 */
#define _DEFAULT_SOURCE
#define _DARWIN_C_SOURCE
#define _XOPEN_SOURCE 600
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include "zxsh_core.h"
#include "zxsh_mux.h"

static int fail(char *why, uint32_t max, const char *s) /* sets *why, returns -1 */
{
    if (why && max) snprintf(why, max, "%s", s);
    return -1;
}

static lpx_t nei(char *why, uint32_t max, const char *s) /* sets *why, returns NEITHER */
{
    fail(why, max, s);
    return zxsh_tag(LPX_B_NEITHER, LPX_P_NEUTRAL);
}

/* ------------------------------------------------------------ rule C */

lpx_t zmux_tag_from_status(int st)
{
    static const uint8_t base[6] = {LPX_B_TRUE,  LPX_B_FALSE, LPX_B_NEITHER,
                                    LPX_B_FALSE, LPX_B_BOTH,  LPX_B_NEITHER};
    if (WIFSIGNALED(st)) {
        int s = WTERMSIG(st);
        if (s == SIGSEGV || s == SIGBUS || s == SIGILL || s == SIGFPE || s == SIGABRT ||
            s == SIGSYS || s == SIGTRAP)
            return zxsh_tag(LPX_B_FALSE, LPX_P_FORBIDDEN);
        return zxsh_tag(LPX_B_NEITHER, LPX_P_ENTROPIC);
    }
    if (!WIFEXITED(st)) return zxsh_tag(LPX_B_NEITHER, LPX_P_ENTROPIC);
    int c = WEXITSTATUS(st);
    if (c > 5) return zxsh_tag(LPX_B_FALSE, LPX_P_NEUTRAL);
    return zxsh_tag(base[c], c == 3 ? LPX_P_FORBIDDEN : LPX_P_NEUTRAL);
}

lpx_t zmux_combine(const lpx_t *t, int n)
{
    if (n <= 0) return zxsh_tag(LPX_B_NEITHER, LPX_P_NEUTRAL);
    bool same = true;
    for (int i = 0; i < n; i++) {
        if (lpx_forbidden(t[i])) return zxsh_tag(LPX_B_FALSE, LPX_P_FORBIDDEN);
        if (lpx_base(t[i]) != lpx_base(t[0])) same = false;
    }
    if (same) return zxsh_tag(lpx_base(t[0]), n >= 2 ? LPX_P_NEGENTROPIC : LPX_P_NEUTRAL);
    return zxsh_tag(LPX_B_BOTH, LPX_P_ENTROPIC);
}

/* A pane's final tag: a crash wins, else its report, else its exit. */
static lpx_t final_tag(int vt_tag, int st, bool known)
{
    lpx_t s = known ? zmux_tag_from_status(st) : zxsh_tag(LPX_B_NEITHER, LPX_P_ENTROPIC);
    if (lpx_forbidden(s) || vt_tag < 0) return s;
    return (lpx_t) (vt_tag & 63);
}

/* ------------------------------------------------------- VT emulator */

enum { S_GND, S_ESC, S_CSI, S_OSC, S_OSCESC, S_SKIP1, S_STR, S_STRESC };

#define CELL(v, y, x) ((v)->buf[(v)->scr][(size_t) (y) * (size_t) (v)->cols + (size_t) (x)])

static zvt_cell_t blank(const zvt_t *v)
{
    zvt_cell_t c;
    memset(&c, 0, sizeof c);
    c.ch[0] = ' ';
    c.len = 1;
    if (v) c.bg = v->pen.bg;
    return c;
}

static void clear_cells(zvt_t *v, int y, int x0, int x1) /* [x0, x1) of row y */
{
    if (y < 0 || y >= v->rows) return;
    if (x0 < 0) x0 = 0;
    if (x1 > v->cols) x1 = v->cols;
    zvt_cell_t b = blank(v);
    for (int x = x0; x < x1; x++) CELL(v, y, x) = b;
}

static void clear_rows(zvt_t *v, int y0, int y1) /* [y0, y1) */
{
    for (int y = y0; y < y1; y++) clear_cells(v, y, 0, v->cols);
}

/* Scroll rows top..bot (inclusive) up by n (n > 0) or down by -n. */
static void scroll(zvt_t *v, int top, int bot, int n)
{
    top = top < 0 ? 0 : top;
    bot = bot >= v->rows ? v->rows - 1 : bot;
    int h = bot - top + 1, k = n < 0 ? -n : n;
    k = k > h ? h : k;
    if (h <= 0 || k == 0) return;
    zvt_cell_t *t = v->buf[v->scr] + (size_t) top * (size_t) v->cols;
    size_t W = (size_t) v->cols, keep = (size_t) (h - k) * W * sizeof *t;
    memmove(n > 0 ? t : t + k * W, n > 0 ? t + k * W : t, keep);
    clear_rows(v, n > 0 ? bot - k + 1 : top, n > 0 ? bot + 1 : top + k);
}

static void moveto(zvt_t *v, int y, int x)
{
    v->cy = y < 0 ? 0 : y >= v->rows ? v->rows - 1 : y;
    v->cx = x < 0 ? 0 : x >= v->cols ? v->cols - 1 : x;
    v->wrapnext = false;
}

static void lf(zvt_t *v)
{
    if (v->cy == v->bot) scroll(v, v->top, v->bot, 1);
    if (v->cy != v->bot && v->cy < v->rows - 1) v->cy++;
    v->wrapnext = false;
}

static void ri(zvt_t *v)
{
    if (v->cy == v->top) scroll(v, v->top, v->bot, -1);
    if (v->cy != v->top && v->cy > 0) v->cy--;
    v->wrapnext = false;
}

static void save_cur(zvt_t *v)
{
    v->sx = v->cx;
    v->sy = v->cy;
    v->spen = v->pen;
}

static void restore_cur(zvt_t *v)
{
    v->pen = v->spen;
    moveto(v, v->sy, v->sx);
}

static void put(zvt_t *v, const uint8_t *s, int n)
{
    zvt_cell_t c = v->pen;
    if (v->wrapnext && v->autowrap) v->cx = 0, lf(v);
    v->wrapnext = false;
    memset(c.ch, 0, sizeof c.ch);
    memcpy(c.ch, s, (size_t) n);
    c.len = (uint8_t) n;
    if (v->cy >= 0 && v->cy < v->rows && v->cx >= 0 && v->cx < v->cols) CELL(v, v->cy, v->cx) = c;
    v->wrapnext = v->cx >= v->cols - 1 && v->autowrap;
    v->cx = v->cx >= v->cols - 1 ? v->cols - 1 : v->cx + 1;
}

static void reply(zvt_t *v, const char *s)
{
    size_t n = strlen(s);
    if (v->replen + n > sizeof v->reply) return;
    memcpy(v->reply + v->replen, s, n);
    v->replen += (uint32_t) n;
}

static void reset(zvt_t *v)
{
    memset(&v->pen, 0, sizeof v->pen);
    v->scr = 1;
    clear_rows(v, 0, v->rows);
    v->scr = 0;
    clear_rows(v, 0, v->rows);
    v->cx = v->cy = v->sx = v->sy = 0;
    v->spen = v->pen;
    v->top = 0;
    v->bot = v->rows - 1;
    v->autowrap = v->cursor_on = true;
    v->wrapnext = false;
}

int zvt_init(zvt_t *v, int rows, int cols)
{
    memset(v, 0, sizeof *v);
    v->tag = -1;
    return zvt_resize(v, rows, cols);
}

void zvt_free(zvt_t *v)
{
    free(v->buf[0]);
    free(v->buf[1]);
    v->buf[0] = v->buf[1] = NULL;
}

int zvt_resize(zvt_t *v, int rows, int cols)
{
    rows = rows < 1 ? 1 : rows > 4096 ? 4096 : rows;
    cols = cols < 1 ? 1 : cols > 4096 ? 4096 : cols;
    size_t N = (size_t) rows * (size_t) cols;
    zvt_cell_t *nb[2] = {malloc(N * sizeof(zvt_cell_t)), malloc(N * sizeof(zvt_cell_t))};
    zvt_cell_t b = blank(NULL);
    int off = v->buf[0] && v->cy >= rows ? v->cy - rows + 1 : 0; /* keep the cursor's row */
    bool fresh = !v->buf[0];
    if (!nb[0] || !nb[1]) return free(nb[0]), free(nb[1]), -1;
    for (int s = 0; s < 2; s++) {
        int o = s == v->scr ? off : 0;
        for (size_t i = 0; i < N; i++) nb[s][i] = b;
        for (int y = 0; v->buf[s] && y < rows && y + o < v->rows; y++)
            for (int x = 0; x < cols && x < v->cols; x++)
                nb[s][(size_t) y * (size_t) cols + (size_t) x] =
                    v->buf[s][(size_t) (y + o) * (size_t) v->cols + (size_t) x];
        free(v->buf[s]);
        v->buf[s] = nb[s];
    }
    v->rows = rows;
    v->cols = cols;
    if (fresh) reset(v);
    v->top = 0;
    v->bot = rows - 1;
    moveto(v, v->cy - off, v->cx);
    v->sy = v->sy >= rows ? rows - 1 : v->sy;
    v->sx = v->sx >= cols ? cols - 1 : v->sx;
    return 0;
}

const zvt_cell_t *zvt_cell(const zvt_t *v, int row, int col)
{
    if (!v->buf[0] || row < 0 || row >= v->rows || col < 0 || col >= v->cols) return NULL;
    return &v->buf[v->scr][(size_t) row * (size_t) v->cols + (size_t) col];
}

static void ctl(zvt_t *v, uint8_t b) /* BEL and other controls are ignored */
{
    if (b == 8) moveto(v, v->cy, v->cx - 1);
    if (b == 9) moveto(v, v->cy, (v->cx / 8 + 1) * 8);
    if (b >= 10 && b <= 12) lf(v);
    if (b == 13) moveto(v, v->cy, 0);
}

static int P(const zvt_t *v, int i, int d)
{
    return i < v->npar && v->par[i] > 0 ? v->par[i] : d;
}

/* SGR 38/48 at par[i]: sets *c, returns how many parameters it used. */
static int ext_color(const zvt_t *v, int i, uint32_t *c)
{
    const int *p = v->par + i;
    if (i + 2 < v->npar && p[1] == 5) *c = ZVT_IDX | (uint32_t) (p[2] & 255);
    if (i + 2 < v->npar && p[1] == 5) return 2;
    if (i + 4 < v->npar && p[1] == 2)
        *c = ZVT_RGB | (uint32_t) (p[2] & 255) << 16 | (uint32_t) (p[3] & 255) << 8 |
             (uint32_t) (p[4] & 255);
    return i + 4 < v->npar && p[1] == 2 ? 4 : v->npar; /* malformed: ignore the rest */
}

static void sgr(zvt_t *v)
{
    static const uint8_t on[8] = {0, ZVT_BOLD, ZVT_DIM,    ZVT_ITALIC, ZVT_UNDERLINE,
                                  0, 0,        ZVT_REVERSE};
    static const uint8_t off[6] = {ZVT_BOLD | ZVT_DIM, ZVT_ITALIC, ZVT_UNDERLINE, 0, 0,
                                   ZVT_REVERSE};
    if (!v->npar) memset(&v->pen, 0, sizeof v->pen);
    for (int i = 0; i < v->npar; i++) {
        int p = v->par[i];
        uint32_t c = 0;
        if (p == 0) memset(&v->pen, 0, sizeof v->pen);
        if (p > 0 && p < 8) v->pen.attr |= on[p];
        if (p >= 22 && p <= 27) v->pen.attr &= (uint8_t) ~off[p - 22];
        if ((p >= 30 && p <= 37) || (p >= 90 && p <= 97))
            v->pen.fg = ZVT_IDX | (uint32_t) (p < 90 ? p - 30 : p - 82);
        if ((p >= 40 && p <= 47) || (p >= 100 && p <= 107))
            v->pen.bg = ZVT_IDX | (uint32_t) (p < 100 ? p - 40 : p - 92);
        if (p == 39) v->pen.fg = 0;
        if (p == 49) v->pen.bg = 0;
        if (p != 38 && p != 48) continue;
        int used = ext_color(v, i, &c);
        if (used < v->npar) *(p == 38 ? &v->pen.fg : &v->pen.bg) = c;
        i += used;
    }
}

static void modes(zvt_t *v, uint8_t f) /* CSI ? ... h / l; others ignored */
{
    for (int i = 0; i < v->npar && (f == 'h' || f == 'l'); i++) {
        int p = v->par[i];
        bool on = f == 'h', cur = p == 1049;
        if (p == 25) v->cursor_on = on;
        if (p == 7) v->autowrap = on;
        if (p != 1049 && p != 47 && p != 1047) continue;
        if (on && v->scr == 0 && cur) save_cur(v);
        if (on && v->scr == 0) v->scr = 1, clear_rows(v, 0, v->rows);
        if (!on && v->scr == 1 && cur) restore_cur(v);
        if (!on) v->scr = 0;
    }
}

static void chars(zvt_t *v, uint8_t f, int n) /* DCH and ICH */
{
    int k = n > v->cols - v->cx ? v->cols - v->cx : n;
    zvt_cell_t *row = &CELL(v, v->cy, 0);
    size_t m = (size_t) (v->cols - v->cx - k) * sizeof *row;
    if (f == 'P') memmove(row + v->cx, row + v->cx + k, m);
    if (f == 'P') clear_cells(v, v->cy, v->cols - k, v->cols);
    if (f == '@') memmove(row + v->cx + k, row + v->cx, m);
    if (f == '@') clear_cells(v, v->cy, v->cx, v->cx + k);
    v->wrapnext = false;
}

static void csi(zvt_t *v, uint8_t f)
{
    int n = P(v, 0, 1), m = v->npar ? v->par[0] : 0, t = n - 1, b = P(v, 1, v->rows) - 1;
    char r[32];
    v->st = S_GND;
    if (v->inter || (v->priv && v->priv != '?')) return;
    if (v->priv) modes(v, f);
    if (v->priv) return;
    if (f == 'A') moveto(v, v->cy - n, v->cx);
    if (f == 'B' || f == 'e') moveto(v, v->cy + n, v->cx);
    if (f == 'C' || f == 'a') moveto(v, v->cy, v->cx + n);
    if (f == 'D') moveto(v, v->cy, v->cx - n);
    if (f == 'E' || f == 'F') moveto(v, v->cy + (f == 'E' ? n : -n), 0);
    if (f == 'G' || f == '`') moveto(v, v->cy, n - 1);
    if (f == 'H' || f == 'f') moveto(v, n - 1, P(v, 1, 1) - 1);
    if (f == 'd') moveto(v, n - 1, v->cx);
    if (f == 'J' && m != 1) clear_rows(v, m == 0 ? v->cy + 1 : 0, v->rows);
    if (f == 'J' && m == 1) clear_rows(v, 0, v->cy);
    if ((f == 'J' && m < 2) || f == 'K')
        clear_cells(v, v->cy, m == 0 ? v->cx : 0, m == 1 ? v->cx + 1 : v->cols);
    if ((f == 'L' || f == 'M') && v->cy >= v->top && v->cy <= v->bot)
        scroll(v, v->cy, v->bot, f == 'L' ? -n : n), moveto(v, v->cy, 0);
    if (f == 'P' || f == '@') chars(v, f, n);
    if (f == 'X') clear_cells(v, v->cy, v->cx, v->cx + n);
    if (f == 'S' || f == 'T') scroll(v, v->top, v->bot, f == 'S' ? n : -n);
    if (b >= v->rows) b = v->rows - 1;
    if (f == 'r' && t < b) v->top = t, v->bot = b, moveto(v, 0, 0);
    if (f == 's') save_cur(v);
    if (f == 'u') restore_cur(v);
    if (f == 'm') sgr(v);
    if (f == 'n' && m == 6) snprintf(r, sizeof r, "\x1b[%d;%dR", v->cy + 1, v->cx + 1), reply(v, r);
    if (f == 'c' && m == 0) reply(v, "\x1b[?1;2c");
}

static void osc(zvt_t *v)
{
    v->st = S_GND;
    v->osc[v->oscn] = 0;
    int k = 0, i = 0, t = 0;
    while (v->osc[i] >= '0' && v->osc[i] <= '9' && k < 100000) k = k * 10 + (v->osc[i++] - '0');
    if (v->osc[i] != ';') return;
    const char *s = v->osc + i + 1;
    if (k == 0 || k == 2) snprintf(v->title, sizeof v->title, "%s", s);
    if (k != 7770 || !*s) return;
    while (*s >= '0' && *s <= '9' && t < 64) t = t * 10 + (*s++ - '0');
    if (!*s && t < 64) v->tag = t;
}

/* One state handler per parser state (S_*). */
static void esc(zvt_t *v, uint8_t b)
{
    v->st = S_GND;
    if (b == '[') v->st = S_CSI, v->npar = 0, v->priv = v->inter = 0;
    if (b == '[') memset(v->par, 0, sizeof v->par);
    if (b == ']') v->st = S_OSC, v->oscn = 0;
    if (b && strchr("PX^_", b)) v->st = S_STR;
    if (b && strchr("()*+-./#% ", b)) v->st = S_SKIP1; /* charsets etc.: skip one */
    if (b == '7') save_cur(v);
    if (b == '8') restore_cur(v);
    if (b == 'E') v->cx = 0;
    if (b == 'D' || b == 'E') lf(v);
    if (b == 'M') ri(v);
    if (b == 'c') reset(v);
    if (b == 0x1B) v->st = S_ESC; /* ESC = and ESC > need nothing */
}

static void ground(zvt_t *v, uint8_t b)
{
    if (b == 0x1B) v->st = S_ESC;
    if (b < 0x20 && b != 0x1B) ctl(v, b);
    if (b >= 0x20 && b < 0x7F) put(v, &b, 1);
    if (b >= 0xC2 && b <= 0xF4)
        v->u8[0] = b, v->u8n = 1, v->u8need = b >= 0xF0 ? 4 : b >= 0xE0 ? 3 : 2;
    if (b >= 0x80 && (b < 0xC2 || b > 0xF4)) put(v, (const uint8_t *) "?", 1);
}

static void csi_byte(zvt_t *v, uint8_t b)
{
    bool digit = b >= '0' && b <= '9', sep = b == ';' || b == ':';
    if ((digit || sep) && !v->npar) v->npar = 1;
    int *p = &v->par[v->npar ? v->npar - 1 : 0];
    if (digit) *p = *p > 6552 ? 65535 : *p * 10 + (b - '0');
    if (sep && v->npar < ZVT_NPAR) v->npar++;
    if (b >= 0x3C && b <= 0x3F) v->priv = b;
    if (b >= 0x20 && b <= 0x2F) v->inter = b;
    if (b >= 0x40 && b <= 0x7E) csi(v, b);
    if (b == 0x1B) v->st = S_ESC;
    if (b < 0x20 && b != 0x1B) ctl(v, b);
}

static void osc_byte(zvt_t *v, uint8_t b)
{
    if (b == 7) osc(v);
    if (b == 0x1B) v->st = S_OSCESC;
    if (b >= 0x20 && v->oscn < sizeof v->osc - 1) v->osc[v->oscn++] = (char) b;
}

static void osc_esc(zvt_t *v, uint8_t b) /* ESC \ ends it; ESC x ends it and starts ESC x */
{
    osc(v);
    esc(v, b);
}

static void str_byte(zvt_t *v, uint8_t b) /* DCS, SOS, PM, APC: consumed */
{
    v->st = b == 7 ? S_GND : b == 0x1B ? S_STRESC : S_STR;
}

static void skip1(zvt_t *v, uint8_t b)
{
    v->st = b == 0x1B ? S_ESC : S_GND;
}

static void byte(zvt_t *v, uint8_t b)
{
    static void (*const H[])(zvt_t *, uint8_t) = {ground,  esc,   csi_byte, osc_byte,
                                                  osc_esc, skip1, str_byte, esc};
    if (v->st == S_GND && v->u8need) {
        bool cont = (b & 0xC0) == 0x80;
        if (cont) v->u8[v->u8n++] = b;
        if (cont && v->u8n < v->u8need) return;
        put(v, cont ? v->u8 : (const uint8_t *) "?", cont ? v->u8n : 1);
        v->u8need = v->u8n = 0;
        if (cont) return;
    }
    if (b == 0x18 || b == 0x1A)
        v->st = S_GND; /* CAN, SUB cancel a sequence */
    else
        H[v->st < 8 ? v->st : 0](v, b);
}

void zvt_feed(zvt_t *v, const char *s, size_t n)
{
    for (size_t i = 0; i < n; i++) byte(v, (uint8_t) s[i]);
}

/* ------------------------------------------------------------ layout */

void zmux_tree_init(zmux_tree_t *t)
{
    memset(t, 0, sizeof *t);
    t->root = -1;
}

static int node_alloc(zmux_tree_t *t, int skip)
{
    for (int i = 0; i < ZMUX_NODES; i++)
        if (!t->n[i].used && i != skip) return i;
    return -1;
}

static void leaf(zmux_tree_t *t, int i, int parent, int pane)
{
    t->n[i] = (zmux_node_t){true, 0, parent, -1, -1, 1, 1, pane};
}

static int find_leaf(const zmux_tree_t *t, int pane)
{
    for (int i = 0; i < ZMUX_NODES; i++)
        if (t->n[i].used && !t->n[i].dir && t->n[i].pane == pane) return i;
    return -1;
}

/* Turn leaf `i` into a split: a = its old pane, b = `pane`. Returns b. */
static int split_node(zmux_tree_t *t, int i, char dir, int pane, int wa, int wb)
{
    int a = node_alloc(t, -1), b = node_alloc(t, a);
    if (i < 0 || a < 0 || b < 0) return -1;
    leaf(t, a, i, t->n[i].pane);
    leaf(t, b, i, pane);
    t->n[i] = (zmux_node_t){true, dir == 'v' ? 'v' : 'h', t->n[i].parent, a, b, wa, wb, -1};
    return b;
}

int zmux_tree_add(zmux_tree_t *t, int at, char dir, int pane)
{
    if (t->root < 0) {
        t->root = 0;
        leaf(t, 0, -1, pane);
        return 0;
    }
    return split_node(t, find_leaf(t, at), dir, pane, 1, 1) < 0 ? -1 : 0;
}

static void tile(zmux_tree_t *t, int node, const int *p, int n, int depth)
{
    if (n <= 1 || node < 0) return;
    int k = n / 2;
    int b = split_node(t, node, depth % 2 ? 'v' : 'h', p[k], k, n - k);
    if (b < 0) return;
    tile(t, t->n[node].a, p, k, depth + 1);
    tile(t, b, p + k, n - k, depth + 1);
}

int zmux_tree_tile(zmux_tree_t *t, const int *panes, int n)
{
    zmux_tree_init(t);
    if (n < 1 || n > ZMUX_MAXP) return -1;
    zmux_tree_add(t, -1, 'h', panes[0]);
    tile(t, 0, panes, n, 0);
    return 0;
}

int zmux_tree_remove(zmux_tree_t *t, int pane)
{
    int l = find_leaf(t, pane);
    if (l < 0) return -1;
    int p = t->n[l].parent;
    t->n[l].used = false;
    if (p < 0) {
        t->root = -1;
        return 0;
    }
    int s = t->n[p].a == l ? t->n[p].b : t->n[p].a, pp = t->n[p].parent;
    t->n[p] = t->n[s];
    t->n[p].parent = pp;
    t->n[s].used = false;
    if (t->n[p].dir) t->n[t->n[p].a].parent = t->n[t->n[p].b].parent = p;
    int c = 0;
    for (int i = 0; i < ZMUX_NODES; i++) c += t->n[i].used && !t->n[i].dir;
    return c;
}

typedef struct {
    int *panes, max, n, nb;
    zmux_rect_t *rects, *borders;
} walk_t;

static void walk(const zmux_tree_t *t, int i, zmux_rect_t r, walk_t *w)
{
    const zmux_node_t *nd = &t->n[i];
    if (!nd->dir && w->n < w->max) w->panes[w->n] = nd->pane, w->rects[w->n] = r;
    w->n += !nd->dir;
    if (!nd->dir) return;
    /* split along x ('h') or y ('v'): a, a one-cell border, then b */
    int *pos = nd->dir == 'h' ? &r.x : &r.y, *len = nd->dir == 'h' ? &r.w : &r.h, p0 = *pos;
    int bw = *len > 0, avail = *len - bw, a = avail * nd->wa / (nd->wa + nd->wb);
    zmux_rect_t ra, bd, rb;
    *len = a, ra = r;
    *pos = p0 + a, *len = bw, bd = r;
    *pos = p0 + a + bw, *len = avail - a, rb = r;
    if (w->borders && w->nb < w->max) w->borders[w->nb++] = bd;
    walk(t, nd->a, ra, w);
    walk(t, nd->b, rb, w);
}

int zmux_tree_rects(const zmux_tree_t *t, zmux_rect_t area, int *panes, zmux_rect_t *rects, int max,
                    zmux_rect_t *borders, int *nborders)
{
    walk_t w = {panes, max, 0, 0, rects, borders};
    if (t->root >= 0) walk(t, t->root, area, &w);
    if (nborders) *nborders = w.nb;
    return w.n;
}

/* --------------------------------------------------------- server state */

#define MAXP   32
#define MAXT   9
#define MAXC   8
#define MAXREQ 8

typedef struct {
    bool used, cmd, exited, closing;
    int fd, tab, req, ri; /* req/ri: the together request and index */
    pid_t pid;
    lpx_t tag; /* final, once exited */
    int64_t killat;
    zvt_t vt;
    char name[96];
    zmux_rect_t r;
} pane_t;

typedef struct {
    bool used;
    zmux_tree_t tree;
    int focus;
    char name[16];
} tab_t;

typedef struct {
    bool used;
    int fd, req;
    size_t n;
    char buf[8192];
} client_t;

typedef struct {
    bool used;
    int client; /* client slot, -1 the server's own request, -2 gone */
    int n;
    lpx_t tag[ZMUX_MAX_CMDS];
    bool done[ZMUX_MAX_CMDS];
} req_t;

static struct {
    const char *self;
    pane_t p[MAXP];
    tab_t t[MAXT];
    client_t c[MAXC];
    req_t q[MAXREQ];
    int cur, rows, cols, lfd, sp[2], pfx;
    bool help, quit, dirty, down, own_done;
    lpx_t own_tag;
    char sock[256], msg[96];
    int64_t msgat, lastdraw;
    zvt_cell_t *scr, *prev;
    char *ob;
    size_t obn, obcap;
} S;

static volatile sig_atomic_t g_term, g_winch;

static void on_sig(int s)
{
    int e = errno;
    g_term |= s == SIGTERM || s == SIGHUP;
    g_winch |= s == SIGWINCH;
    ssize_t r = write(S.sp[1], "", 1); /* wake poll() */
    (void) r;
    errno = e;
}

static int64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t) ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void wall(int fd, const char *s, size_t n)
{
    while (n) {
        ssize_t w = write(fd, s, n);
        if (w < 0 && errno != EINTR) return; /* EAGAIN on a full pane: dropped (M7) */
        w = w < 0 ? 0 : w;
        s += w;
        n -= (size_t) w;
    }
}

static void fd_flags(int fd)
{
    fcntl(fd, F_SETFD, FD_CLOEXEC);
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
}

static void note(const char *s)
{
    snprintf(S.msg, sizeof S.msg, "%s", s);
    S.msgat = now_ms();
    S.dirty = true;
}

static void sig(const pane_t *p, int s) /* the pane's process group and the pane */
{
    kill(-p->pid, s);
    kill(p->pid, s);
}

static int leaves(const zmux_tree_t *t, int *ids, zmux_rect_t *r, int rows, int cols)
{
    zmux_rect_t area = {0, 0, cols, rows}, tmp[ZMUX_MAXP];
    return zmux_tree_rects(t, area, ids, r ? r : tmp, ZMUX_MAXP, NULL, NULL);
}

static void relayout(void)
{
    zmux_rect_t rr[ZMUX_MAXP];
    int ids[ZMUX_MAXP];
    for (int ti = 0; ti < MAXT; ti++) {
        int n = S.t[ti].used ? leaves(&S.t[ti].tree, ids, rr, S.rows - 1, S.cols) : 0;
        for (int i = 0; i < n; i++) {
            pane_t *p = &S.p[ids[i]];
            p->r = rr[i];
            int vr = rr[i].h - 1 < 1 ? 1 : rr[i].h - 1, vc = rr[i].w < 1 ? 1 : rr[i].w;
            if (vr == p->vt.rows && vc == p->vt.cols) continue;
            zvt_resize(&p->vt, vr, vc);
            struct winsize ws = {(unsigned short) p->vt.rows, (unsigned short) p->vt.cols, 0, 0};
            if (p->fd >= 0) ioctl(p->fd, TIOCSWINSZ, &ws);
        }
    }
    S.dirty = true;
}

/* ------------------------------------------------------------- panes */

static int spawn(pane_t *p, const char *cwd, const char *cmd, char *why, uint32_t wm)
{
    int m = posix_openpt(O_RDWR | O_NOCTTY);
    if (m < 0 || grantpt(m) || unlockpt(m) || !ptsname(m))
        return close(m), fail(why, wm, "no pseudo-terminal available");
    char sn[128];
    snprintf(sn, sizeof sn, "%s", ptsname(m));
    fd_flags(m);
    struct winsize ws = {(unsigned short) p->vt.rows, (unsigned short) p->vt.cols, 0, 0};
    pid_t pid = fork();
    if (pid < 0) return close(m), fail(why, wm, "fork failed");
    if (pid == 0) {
        int sigs[] = {SIGCHLD, SIGWINCH, SIGTERM, SIGHUP, SIGPIPE, SIGINT, SIGQUIT};
        for (size_t i = 0; i < sizeof sigs / sizeof *sigs; i++) signal(sigs[i], SIG_DFL);
        setsid();
        int s = open(sn, O_RDWR);
        if (s < 0) _exit(127);
        ioctl(s, TIOCSCTTY, 0);
        ioctl(s, TIOCSWINSZ, &ws);
        for (int d = 0; d < 3; d++) dup2(s, d);
        if (s > 2) close(s);
        setenv("ZXSH_MUX", "1", 1);
        setenv("ZXSH_MUX_SOCK", S.sock, 1);
        setenv("TERM", "xterm-256color", 1);
        unsetenv("LINES");
        unsetenv("COLUMNS");
        if (cwd && *cwd) (void) !chdir(cwd);
        execl(S.self, S.self, cmd ? "-c" : (char *) 0, cmd, (char *) 0);
        _exit(127);
    }
    p->fd = m;
    p->pid = pid;
    return 0;
}

static int slot_new(int tab, const char *cmd, int req, int ri)
{
    for (int i = 0; i < MAXP; i++) {
        pane_t *p = &S.p[i];
        if (p->used) continue;
        *p = (pane_t){.used = true, .cmd = cmd != NULL, .fd = -1, .tab = tab, .req = req, .ri = ri};
        snprintf(p->name, sizeof p->name, "%s", cmd ? cmd : "shell");
        if (!zvt_init(&p->vt, 1, 1)) return i;
        p->used = false;
        return -1;
    }
    return -1;
}

static void slot_free(int i)
{
    zvt_free(&S.p[i].vt);
    S.p[i].used = false;
}

static int tab_new(const char *name)
{
    for (int i = 0; i < MAXT; i++) {
        if (S.t[i].used) continue;
        S.t[i] = (tab_t){.used = true, .focus = -1};
        zmux_tree_init(&S.t[i].tree);
        snprintf(S.t[i].name, sizeof S.t[i].name, "%s", name);
        return i;
    }
    return -1;
}

/* Drop empty tabs; keep S.cur on a live tab when one exists. */
static void tabs_gc(void)
{
    int keep = -1;
    for (int i = 0; i < MAXT; i++) {
        if (S.t[i].used && S.t[i].tree.root < 0) S.t[i].used = false;
        if (S.t[i].used && (keep < 0 || i <= S.cur)) keep = i;
    }
    if (keep >= 0 && !S.t[S.cur].used) S.cur = keep;
}

static int pane_new(int tab, int at, char dir, const char *cwd, const char *cmd, char *why,
                    uint32_t wm)
{
    int i = slot_new(tab, cmd, -1, 0);
    if (i < 0) return fail(why, wm, "too many panes");
    if (zmux_tree_add(&S.t[tab].tree, at, dir, i))
        return slot_free(i), fail(why, wm, "no room for another pane in this tab");
    relayout();
    if (!spawn(&S.p[i], cwd, cmd, why, wm)) return S.t[tab].focus = i;
    zmux_tree_remove(&S.t[tab].tree, i);
    slot_free(i);
    relayout();
    return -1;
}

static void client_drop(int ci)
{
    client_t *c = &S.c[ci];
    if (c->req >= 0 && S.q[c->req].used) S.q[c->req].client = -2;
    close(c->fd);
    c->used = false;
}

/* M3: when every pane of a together request is done, answer it. */
static void req_check(int r)
{
    req_t *q = &S.q[r];
    for (int i = 0; i < q->n; i++)
        if (!q->done[i]) return;
    if (!q->used) return;
    lpx_t c = zmux_combine(q->tag, q->n);
    char line[32];
    snprintf(line, sizeof line, "tag %u\n", (unsigned) c);
    if (q->client >= 0) wall(S.c[q->client].fd, line, strlen(line));
    if (q->client >= 0) S.c[q->client].req = -1, client_drop(q->client);
    if (q->client == -1) S.own_tag = c, S.own_done = true;
    q->used = false;
}

static void pane_close(int i);

static void pane_exited(int i, int st, bool known)
{
    pane_t *p = &S.p[i];
    req_t *q = p->req >= 0 ? &S.q[p->req] : NULL;
    p->exited = true;
    p->tag = final_tag(p->vt.tag, st, known);
    if (q && q->used) q->tag[p->ri] = p->tag, q->done[p->ri] = true, req_check(p->req);
    S.dirty = true;
    if (!S.down && p->closing) slot_free(i);
    if (!S.down && !p->closing && !p->cmd) pane_close(i); /* a shell pane closes */
}

static void pane_read(int i)
{
    pane_t *p = &S.p[i];
    char b[4096];
    for (int k = 0; k < 16 && p->fd >= 0; k++) {
        ssize_t r = read(p->fd, b, sizeof b);
        if (r < 0 && (errno == EINTR || errno == EAGAIN)) return;
        if (r <= 0) close(p->fd), p->fd = -1; /* EOF or EIO: its terminal closed */
        if (r <= 0) return;
        zvt_feed(&p->vt, b, (size_t) r);
        wall(p->fd, p->vt.reply, p->vt.replen);
        p->vt.replen = 0;
        S.dirty = true;
    }
}

static void reap(void)
{
    for (int i = 0; i < MAXP; i++) {
        pane_t *p = &S.p[i];
        int st = 0;
        if (!p->used || p->exited || p->pid <= 0) continue;
        pid_t r = waitpid(p->pid, &st, WNOHANG);
        if (r != p->pid && !(r < 0 && errno == ECHILD)) continue;
        if (p->fd >= 0) pane_read(i); /* a last OSC report may be waiting */
        pane_exited(i, st, r == p->pid);
    }
}

static void pane_close(int i)
{
    pane_t *p = &S.p[i];
    tab_t *t = &S.t[p->tab];
    int ids[ZMUX_MAXP];
    zmux_rect_t rr[ZMUX_MAXP];
    zmux_tree_remove(&t->tree, i);
    if (t->focus == i) t->focus = leaves(&t->tree, ids, rr, 1, 1) ? ids[0] : -1;
    if (p->fd >= 0) close(p->fd);
    p->fd = -1;
    p->closing = !p->exited && p->pid > 0;
    p->killat = now_ms() + 1000;
    if (p->closing) sig(p, SIGHUP);
    if (!p->closing) slot_free(i);
    tabs_gc();
    relayout();
}

/* Open panes for cmds tiled in tab ti; a pane that cannot start shows why
 * and finishes NEITHER at once. */
static int open_tiled(int ti, const char *cwd, const char *const *cmds, int n, int req, char *why,
                      uint32_t wm)
{
    int ids[ZMUX_MAXP], free_slots = 0;
    for (int i = 0; i < MAXP; i++) free_slots += !S.p[i].used;
    if (n < 1 || n > ZMUX_MAXP || n > free_slots) return fail(why, wm, "too many panes");
    for (int k = 0; k < n; k++) {
        if ((ids[k] = slot_new(ti, cmds[k], req, k)) >= 0) continue;
        while (k--) slot_free(ids[k]);
        return fail(why, wm, "out of memory");
    }
    zmux_tree_tile(&S.t[ti].tree, ids, n);
    relayout();
    for (int k = 0; k < n; k++) {
        char w[128] = "", line[160];
        if (!spawn(&S.p[ids[k]], cwd, cmds[k], w, sizeof w)) continue;
        snprintf(line, sizeof line, "could not start: %s\r\n", w);
        zvt_feed(&S.p[ids[k]].vt, line, strlen(line));
        pane_exited(ids[k], 127 << 8, true);
    }
    return S.t[ti].focus = ids[0];
}

static void pane_cwd(char *o, size_t n)
{
    char l[64];
    int f = S.t[S.cur].used ? S.t[S.cur].focus : -1;
    ssize_t r = -1;
    snprintf(l, sizeof l, "/proc/%d/cwd", f >= 0 ? (int) S.p[f].pid : 0);
    if (f >= 0 && (r = readlink(l, o, n - 1)) > 0) o[r] = 0;
    if (r <= 0 && !getcwd(o, n)) o[0] = 0;
}

/* One request (M3). client >= 0 is a client slot, -1 the server itself.
 * Returns 0 when done, 1 when together is pending, -1 on failure. */
static int request(const char *kind, char dir, const char *cwd, const char *const *cmds, int n,
                   int client, char *why, uint32_t wm)
{
    bool tog = !strcmp(kind, "together"), split = !strcmp(kind, "split");
    int r = tog ? 0 : -1, ti, rc;
    if (split && !S.t[S.cur].used) return fail(why, wm, "no pane to split");
    for (int k = 0; split && k < (n ? n : 1); k++)
        if (pane_new(S.cur, S.t[S.cur].focus, dir == 'v' ? 'v' : 'h', cwd, n ? cmds[k] : NULL, why,
                     wm) < 0)
            return -1;
    if (split) return 0;
    if (!tog && strcmp(kind, "tab")) return fail(why, wm, "unknown request");
    if (tog && n == 0) return fail(why, wm, "together needs commands");
    while (r >= 0 && r < MAXREQ && S.q[r].used) r++;
    if (r == MAXREQ) return fail(why, wm, "too many requests");
    if ((ti = tab_new(tog ? "together" : n ? "tab" : "shell")) < 0)
        return fail(why, wm, "too many tabs");
    if (r >= 0) S.q[r] = (req_t){.used = true, .client = client, .n = n};
    if (r >= 0 && client >= 0) S.c[client].req = r;
    rc = n ? open_tiled(ti, cwd, cmds, n, r, why, wm) : pane_new(ti, -1, 'h', cwd, NULL, why, wm);
    if (rc < 0 && r >= 0) S.q[r].used = false;
    if (rc < 0 && client >= 0) S.c[client].req = -1;
    if (rc < 0) return S.t[ti].used = false, -1;
    S.cur = ti;
    if (r >= 0) req_check(r);
    return tog ? 1 : 0;
}

static void unescape(char *s)
{
    char *o = s;
    for (; *s; s++, o++)
        *o = *s == '\\' && s[1] ? (s++, *s == 't' ? '\t' : *s == 'n' ? '\n' : *s) : *s;
    *o = 0;
}

static void client_read(int ci)
{
    client_t *c = &S.c[ci];
    ssize_t r = read(c->fd, c->buf + c->n, sizeof c->buf - 1 - c->n);
    if (r < 0 && (errno == EINTR || errno == EAGAIN)) return;
    if (r <= 0) client_drop(ci);
    if (r <= 0 || c->req >= 0) return; /* gone, or serving: extra bytes ignored */
    c->n += (size_t) r;
    c->buf[c->n] = 0;
    char *nl = strchr(c->buf, '\n'), why[128] = "", *f[3 + ZMUX_MAX_CMDS], line[160];
    int nf = 1, rc = -1;
    if (!nl && c->n < sizeof c->buf - 1) return;
    *(nl ? nl : c->buf) = 0; /* an over-long request is a bad request */
    f[0] = c->buf;
    for (char *p = c->buf; *p; p++)
        if (*p == '\t' && nf < 3 + ZMUX_MAX_CMDS) *p = 0, f[nf++] = p + 1;
    for (int i = 0; i < nf; i++) unescape(f[i]);
    if (nf < 3)
        fail(why, sizeof why, "bad request");
    else
        rc = request(f[0], f[1][0], f[2], (const char *const *) f + 3, nf - 3, ci, why, sizeof why);
    if (rc == 1) return; /* together: req_check answers */
    for (char *p = why; *p; p++)
        if (*p == '\n' || *p == '\t') *p = ' ';
    snprintf(line, sizeof line, rc == 0 ? "ok\n" : "err %s\n", why);
    wall(c->fd, line, strlen(line));
    c->req = -1;
    client_drop(ci);
    S.dirty = true;
}

/* ------------------------------------------------------------- keys */

static void send_focus(const char *s, size_t n)
{
    int f = S.t[S.cur].used ? S.t[S.cur].focus : -1;
    if (f >= 0 && S.p[f].fd >= 0) wall(S.p[f].fd, s, n);
}

static void move_focus(uint8_t d) /* to the nearest pane in an arrow's direction */
{
    tab_t *t = &S.t[S.cur];
    int ids[ZMUX_MAXP], best = -1, n = t->used ? leaves(&t->tree, ids, NULL, 0, 0) : 0;
    long bestd = 0;
    zmux_rect_t f = t->focus >= 0 ? S.p[t->focus].r : (zmux_rect_t){0, 0, 0, 0};
    for (int i = 0; i < n && t->focus >= 0; i++) {
        zmux_rect_t r = S.p[ids[i]].r;
        bool ox = r.x < f.x + f.w && f.x < r.x + r.w, oy = r.y < f.y + f.h && f.y < r.y + r.h;
        long dist = d == 'A' && ox && r.y + r.h <= f.y   ? f.y - r.y - r.h
                    : d == 'B' && ox && r.y >= f.y + f.h ? r.y - f.y - f.h
                    : d == 'C' && oy && r.x >= f.x + f.w ? r.x - f.x - f.w
                    : d == 'D' && oy && r.x + r.w <= f.x ? f.x - r.x - r.w
                                                         : -1;
        long side = d <= 'B' ? labs((long) (r.x * 2 + r.w) - (f.x * 2 + f.w))
                             : labs((long) (r.y * 2 + r.h) - (f.y * 2 + f.h));
        if (dist >= 0 && (best < 0 || dist * 100000 + side < bestd))
            best = ids[i], bestd = dist * 100000 + side;
    }
    if (best >= 0) t->focus = best;
}

static int tab_at(int pos) /* pos-th used tab (0-based), -1 if none */
{
    for (int i = 0; i < MAXT; i++)
        if (S.t[i].used && pos-- == 0) return i;
    return -1;
}

static void command(uint8_t k) /* M5: the key after Ctrl-B */
{
    char why[128] = "", cwd[1024];
    tab_t *t = &S.t[S.cur];
    int ids[ZMUX_MAXP], n = t->used ? leaves(&t->tree, ids, NULL, 0, 0) : 0, at = 0;
    int step = k == 'n' ? 1 : k == 'p' ? MAXT - 1 : 0, j = S.cur;
    bool open = k == '%' || k == '"' || k == 'c';
    if (open) pane_cwd(cwd, sizeof cwd);
    if (open && request(k == 'c' ? "tab" : "split", k == '"' ? 'v' : 'h', cwd, NULL, 0, -1, why,
                        sizeof why) < 0)
        note(why);
    for (int i = 0; i < n; i++) at = ids[i] == t->focus ? i : at;
    if (k == 'o' && n) t->focus = ids[(at + 1) % n];
    do j = (j + step) % MAXT;
    while (step && !S.t[j].used);
    if (step) S.cur = j;
    if (k == 'x' && t->used && t->focus >= 0) pane_close(t->focus);
    if (k == 'q') S.quit = true;
    if (k == '?') S.help = true;
    if (k == 2) send_focus("\x02", 1);
    if (k >= '1' && k <= '9' && tab_at(k - '1') >= 0) S.cur = tab_at(k - '1');
    S.dirty = true;
}

static void keys(const char *b, size_t n)
{
    if (S.help) { /* any key dismisses the help; the rest of it is dropped */
        S.help = false;
        S.dirty = true;
        return;
    }
    for (size_t i = 0; i < n; i++) {
        uint8_t k = (uint8_t) b[i];
        int p = S.pfx;
        if (p == 0) {
            size_t j = i;
            while (j < n && b[j] != 2) j++;
            send_focus(b + i, j - i);
            S.pfx = j < n;
            i = j;
            continue;
        }
        /* 1: after Ctrl-B; 2: after Ctrl-B ESC; 3: inside an arrow key */
        S.pfx = p == 1   ? (k == 0x1B ? 2 : (command(k), 0))
                : p == 2 ? (k == '[' || k == 'O' ? 3 : 0)
                         : ((k >= '0' && k <= '9') || k == ';' ? 3 : 0);
        if (p == 3 && k >= 'A' && k <= 'D') move_focus(k), S.dirty = true;
    }
}

/* ------------------------------------------------------------ screen */

static void ob(const char *s, size_t n)
{
    size_t cap = (S.obn + n) * 2 + 4096;
    char *p = S.obn + n > S.obcap ? realloc(S.ob, cap) : S.ob;
    if (!p) return;
    if (p != S.ob || S.obn + n > S.obcap) S.ob = p, S.obcap = cap;
    memcpy(S.ob + S.obn, s, n);
    S.obn += n;
}

static void obf(const char *fmt, int a, int b, int c, int d)
{
    char t[48];
    int n = snprintf(t, sizeof t, fmt, a, b, c, d);
    if (n > 0) ob(t, (size_t) n < sizeof t ? (size_t) n : sizeof t - 1);
}

static bool cell_eq(const zvt_cell_t *a, const zvt_cell_t *b)
{
    return a->len == b->len && a->attr == b->attr && a->fg == b->fg && a->bg == b->bg &&
           !memcmp(a->ch, b->ch, a->len < 4 ? a->len : 4);
}

/* Write UTF-8 text into screen row y from x up to xe; returns the new x. */
static int gput(int y, int x, int xe, const char *s, uint8_t attr, uint32_t fg, uint32_t bg)
{
    for (const uint8_t *u = (const uint8_t *) s; *u && x < xe; x++) {
        zvt_cell_t c = blank(NULL);
        int n = *u < 0x80 ? 1 : *u >= 0xF0 ? 4 : *u >= 0xE0 ? 3 : *u >= 0xC0 ? 2 : 0, ok = n;
        for (int i = 1; i < n && ok; i++) ok = (u[i] & 0xC0) == 0x80;
        n = ok && *u >= 0x20 ? n : 1;
        if (ok && *u >= 0x20) memcpy(c.ch, u, (size_t) n);
        c.len = (uint8_t) n;
        c.attr = attr;
        c.fg = fg;
        c.bg = bg;
        if (x >= 0 && x < S.cols && y >= 0 && y < S.rows)
            S.scr[(size_t) y * (size_t) S.cols + (size_t) x] = c;
        u += n;
    }
    return x;
}

static uint32_t letter_color(char l) /* T green, F red, B gold, ! magenta, N grey */
{
    static const uint32_t C[5] = {ZVT_IDX | 40, ZVT_IDX | 196, ZVT_RGB | 0xF2C230, ZVT_IDX | 201,
                                  ZVT_IDX | 245};
    const char *p = l ? strchr("TFB!", l) : NULL;
    return C[p ? p - "TFB!" : 4];
}

/* The letter a pane shows: its final tag, its last report, or '-'. */
static char pane_letter(const pane_t *p, lpx_t *tag)
{
    if (!p->exited && p->vt.tag < 0) return '-';
    *tag = p->exited ? p->tag : (lpx_t) p->vt.tag;
    return zxsh_letter(*tag);
}

static char tab_letter(int ti)
{
    lpx_t tags[MAXP];
    int n = 0;
    for (int i = 0; i < MAXP; i++)
        n += S.p[i].used && !S.p[i].closing && S.p[i].tab == ti &&
             pane_letter(&S.p[i], &tags[n]) != '-';
    return n ? zxsh_letter(zmux_combine(tags, n)) : '-';
}

static void title_bar(const pane_t *p, int k, bool foc)
{
    zmux_rect_t r = p->r;
    uint32_t bg = ZVT_IDX | (foc ? 25 : 236), fg = ZVT_IDX | (foc ? 231 : 250);
    uint8_t at = foc ? ZVT_BOLD : 0;
    lpx_t tag;
    char head[160], l[2] = {pane_letter(p, &tag), 0};
    snprintf(head, sizeof head, " %d %s  [", k + 1, p->vt.title[0] ? p->vt.title : p->name);
    for (int x = r.x; x < r.x + r.w; x++) gput(r.y, x, x + 1, " ", at, fg, bg);
    int x = gput(r.y, r.x, r.x + r.w, head, at, fg, bg);
    x = gput(r.y, x, r.x + r.w, l, ZVT_BOLD, letter_color(l[0]), bg);
    x = gput(r.y, x, r.x + r.w, "] ", at, fg, bg);
    gput(r.y, x, r.x + r.w,
         !p->exited ? "running"
         : p->cmd   ? "done - Ctrl-B x closes"
                    : "done",
         at, fg, bg);
}

static void compose(void)
{
    static const char *const H[] = {
        " Keys: press Ctrl-B, then          ",   "   %        split right             ",
        "   \"        split below             ", "   arrows o move focus              ",
        "   c        new tab                 ",  "   n  p     next / previous tab     ",
        "   1-9      go to tab               ",  "   x        close the focused pane  ",
        "   q        leave (close all)       ",  "   ?        this help               ",
        "   Ctrl-B   send Ctrl-B             ",  " Press any key to close this help. "};
    zvt_cell_t b = blank(NULL);
    for (size_t i = 0; i < (size_t) S.rows * (size_t) S.cols; i++) S.scr[i] = b;
    tab_t *t = &S.t[S.cur];
    zmux_rect_t rr[ZMUX_MAXP], br[ZMUX_MAXP], area = {0, 0, S.cols, S.rows - 1};
    int ids[ZMUX_MAXP],
        nb = 0, n = t->used ? zmux_tree_rects(&t->tree, area, ids, rr, ZMUX_MAXP, br, &nb) : 0;
    for (int i = 0; i < nb; i++)
        for (int y = br[i].y; y < br[i].y + br[i].h; y++)
            for (int x = br[i].x; x < br[i].x + br[i].w; x++)
                gput(y, x, x + 1, br[i].w == 1 && br[i].h > 1 ? "\xe2\x94\x82" : "\xe2\x94\x80", 0,
                     ZVT_IDX | 240, 0);
    for (int k = 0; k < n; k++) {
        pane_t *p = &S.p[ids[k]];
        zmux_rect_t r = rr[k];
        if (r.w > 0 && r.h > 0) title_bar(p, k, ids[k] == t->focus);
        for (int y = 0; y < r.h - 1 && y < p->vt.rows; y++)
            for (int x = 0; x < r.w && x < p->vt.cols; x++)
                S.scr[(size_t) (r.y + 1 + y) * (size_t) S.cols + (size_t) (r.x + x)] =
                    *zvt_cell(&p->vt, y, x);
    }
    int y = S.rows - 1, x = 0, h = (int) (sizeof H / sizeof *H);
    uint32_t sb = ZVT_IDX | 236, sf = ZVT_IDX | 250;
    for (int i = 0; i < S.cols; i++) gput(y, i, i + 1, " ", 0, sf, sb);
    for (int pos = 0, ti; (ti = tab_at(pos)) >= 0; pos++) {
        char s[40], l[2] = {tab_letter(ti), 0};
        snprintf(s, sizeof s, " %d:%s%s ", pos + 1, S.t[ti].name, ti == S.cur ? "*" : "");
        x = gput(y, x, S.cols, s, ti == S.cur ? ZVT_BOLD : 0, sf, sb);
        x = gput(y, x, S.cols, l, ZVT_BOLD, letter_color(l[0]), sb);
        x = gput(y, x, S.cols, " ", 0, sf, sb);
    }
    if (S.msg[0] && now_ms() - S.msgat >= 4000) S.msg[0] = 0;
    if (S.msg[0]) x = gput(y, x + 1, S.cols, S.msg, 0, ZVT_IDX | 214, sb);
    if (S.cols - 14 > x) gput(y, S.cols - 14, S.cols, "Ctrl-B ? keys ", 0, sf, sb);
    for (int i = 0, y0 = (S.rows - 1 - h) / 2, x0 = (S.cols - 35) / 2; S.help && i < h; i++)
        if (y0 + i >= 0 && y0 + i < S.rows - 1)
            gput(y0 + i, x0 < 0 ? 0 : x0, S.cols, H[i], i ? 0 : ZVT_BOLD, ZVT_IDX | 231,
                 ZVT_IDX | 24);
}

static void sgr_color(uint32_t c, int base)
{
    int i = (int) (c & 255);
    if (c & ZVT_RGB)
        obf(";%d;2;%d;%d;%d", base + 8, (int) (c >> 16 & 255), (int) (c >> 8 & 255), i);
    if ((c & ZVT_IDX) && i < 16) obf(";%d", base + (i < 8 ? i : 60 + i - 8), 0, 0, 0);
    if ((c & ZVT_IDX) && i >= 16) obf(";%d;5;%d", base + 8, i, 0, 0);
}

static void draw(void) /* M4: write only the cells that changed */
{
    compose();
    S.obn = 0;
    ob("\x1b[?25l", 6);
    int ly = -1, lx = -1;
    zvt_cell_t pen = blank(NULL);
    bool have = false;
    for (int y = 0; y < S.rows; y++)
        for (int x = 0; x < S.cols; x++) {
            size_t i = (size_t) y * (size_t) S.cols + (size_t) x;
            zvt_cell_t *c = &S.scr[i];
            if (cell_eq(c, &S.prev[i])) continue;
            if (y != ly || x != lx) obf("\x1b[%d;%dH", y + 1, x + 1, 0, 0);
            if (!have || pen.attr != c->attr || pen.fg != c->fg || pen.bg != c->bg) {
                ob("\x1b[0", 3);
                for (int a = 0; a < 5; a++)
                    if (c->attr & (1u << a)) obf(";%d", "\1\2\3\4\7"[a], 0, 0, 0);
                sgr_color(c->fg, 30);
                sgr_color(c->bg, 40);
                ob("m", 1);
                pen = *c;
                have = true;
            }
            ob((const char *) c->ch, c->len <= 4 ? c->len : 1);
            S.prev[i] = *c;
            ly = y;
            lx = x + 1;
        }
    ob("\x1b[0m", 4);
    tab_t *t = &S.t[S.cur];
    pane_t *p = t->used && t->focus >= 0 ? &S.p[t->focus] : NULL;
    if (p && !S.help && p->vt.cursor_on && p->vt.cy < p->r.h - 1 && p->vt.cx < p->r.w)
        obf("\x1b[%d;%dH\x1b[?25h", p->r.y + 2 + p->vt.cy, p->r.x + 1 + p->vt.cx, 0, 0);
    wall(1, S.ob, S.obn);
}

static int grid_alloc(void)
{
    struct winsize ws;
    bool ok = ioctl(1, TIOCGWINSZ, &ws) == 0 && ws.ws_row && ws.ws_col;
    S.rows = !ok ? 24 : ws.ws_row < 2 ? 2 : ws.ws_row;
    S.cols = ok ? ws.ws_col : 80;
    free(S.scr);
    free(S.prev);
    S.scr = calloc((size_t) S.rows * (size_t) S.cols, sizeof *S.scr);
    S.prev = calloc((size_t) S.rows * (size_t) S.cols, sizeof *S.prev); /* len 0: all drawn */
    return S.scr && S.prev ? 0 : -1;
}

/* ------------------------------------------------------------ server */

static void shutdown_all(void)
{
    S.down = true;
    for (int i = 0; i < MAXP; i++) {
        pane_t *p = &S.p[i];
        if (p->used && p->fd >= 0) close(p->fd);
        if (p->used && !p->exited && p->pid > 0) sig(p, SIGHUP);
        p->fd = -1;
    }
    for (int k = 0, live = 1; k < 50 && live; k++) {
        reap();
        live = 0;
        for (int i = 0; i < MAXP; i++) live |= S.p[i].used && !S.p[i].exited && S.p[i].pid > 0;
        if (live) poll(NULL, 0, 10);
    }
    for (int i = 0; i < MAXP; i++) {
        pane_t *p = &S.p[i];
        int st = 0;
        if (p->used && !p->exited && p->pid > 0) sig(p, SIGKILL);
        if (p->used && !p->exited)
            pane_exited(i, st, p->pid > 0 && waitpid(p->pid, &st, 0) == p->pid);
        if (p->used) slot_free(i);
    }
    for (int i = 0; i < MAXC; i++)
        if (S.c[i].used) client_drop(i);
}

static void accept_client(void)
{
    int fd = accept(S.lfd, NULL, NULL), ci = 0;
    while (ci < MAXC && S.c[ci].used) ci++;
    if (fd >= 0 && ci == MAXC) close(fd);
    if (fd < 0 || ci == MAXC) return;
    fd_flags(fd);
    S.c[ci].used = true, S.c[ci].fd = fd, S.c[ci].req = -1, S.c[ci].n = 0;
}

static void loop(void)
{
    struct pollfd pf[3 + MAXP + MAXC];
    int who[3 + MAXP + MAXC], fds[3] = {S.sp[0], 0, S.lfd};
    char b[4096];
    for (;;) {
        if (g_winch && grid_alloc()) break;
        if (g_winch) g_winch = 0, relayout();
        reap();
        int64_t now = now_ms();
        int n = 0, any = 0;
        for (int i = 0; i < MAXT; i++) any |= S.t[i].used;
        if (S.quit || g_term || !any) break;
        for (int i = 0; i < MAXP; i++)
            if (S.p[i].used && S.p[i].closing && !S.p[i].exited && now >= S.p[i].killat)
                sig(&S.p[i], SIGKILL), S.p[i].killat = INT64_MAX;
        if (S.msg[0] && now - S.msgat >= 4000) S.dirty = true;
        if (S.dirty && now - S.lastdraw >= 16) draw(), S.lastdraw = now, S.dirty = false;
        for (int i = 0; i < 3; i++) pf[n] = (struct pollfd){fds[i], POLLIN, 0}, who[n++] = -1 - i;
        for (int i = 0; i < MAXP; i++)
            if (S.p[i].used && S.p[i].fd >= 0)
                pf[n] = (struct pollfd){S.p[i].fd, POLLIN, 0}, who[n++] = i;
        for (int i = 0; i < MAXC; i++)
            if (S.c[i].used) pf[n] = (struct pollfd){S.c[i].fd, POLLIN, 0}, who[n++] = 100 + i;
        if (poll(pf, (nfds_t) n, S.dirty ? (int) (16 - (now - S.lastdraw)) : 250) <= 0) continue;
        for (int k = 0; k < n; k++) {
            int w = who[k];
            ssize_t r = 0;
            if (!pf[k].revents) continue;
            while (w == -1 && read(S.sp[0], b, sizeof b) > 0) continue;
            if (w == -2 && (r = read(0, b, sizeof b)) > 0) keys(b, (size_t) r);
            if (w == -2 && (r == 0 || (r < 0 && errno != EINTR && errno != EAGAIN))) S.quit = true;
            if (w == -3) accept_client();
            if (w >= 0 && w < 100 && S.p[w].used && S.p[w].fd == pf[k].fd) pane_read(w);
            if (w >= 100 && S.c[w - 100].used && S.c[w - 100].fd == pf[k].fd) client_read(w - 100);
        }
    }
}

static lpx_t server(const zmux_cfg_t *cfg, const char *kind, char dir, const char *const *cmds,
                    int n, char *why, uint32_t wm)
{
    const char *home = cfg->home && *cfg->home ? cfg->home : "/tmp";
    struct sockaddr_un a = {0};
    struct sigaction sa = {0}, old[5];
    int sigs[5] = {SIGCHLD, SIGWINCH, SIGTERM, SIGHUP, SIGPIPE};
    struct termios tio = {0}, raw;
    memset(&S, 0, sizeof S);
    S.self = cfg->self;
    g_term = g_winch = 0;
    a.sun_family = AF_UNIX;
    int len = snprintf(S.sock, sizeof S.sock, "%s/mux-%d.sock", home, (int) getpid());
    if (len < 0 || (size_t) len >= sizeof a.sun_path)
        return nei(why, wm, "the control socket path is too long");
    memcpy(a.sun_path, S.sock, (size_t) len + 1);
    mkdir(home, 0700);
    unlink(S.sock);
    S.lfd = socket(AF_UNIX, SOCK_STREAM, 0);
    mode_t um = umask(077);
    int bad = S.lfd < 0 || bind(S.lfd, (struct sockaddr *) &a, sizeof a) || listen(S.lfd, 8);
    umask(um);
    if (bad || pipe(S.sp))
        return close(S.lfd), unlink(S.sock), nei(why, wm, "could not open the control socket");
    chmod(S.sock, 0600);
    for (int i = 0; i < 3; i++) fd_flags(i ? S.sp[i - 1] : S.lfd);
    sigemptyset(&sa.sa_mask);
    for (int i = 0; i < 5; i++) {
        sa.sa_handler = sigs[i] == SIGPIPE ? SIG_IGN : on_sig;
        sigaction(sigs[i], &sa, &old[i]);
    }
    bool tty = tcgetattr(0, &tio) == 0, tog = !strcmp(kind, "together"), ok = !grid_alloc();
    raw = tio;
    cfmakeraw(&raw);
    if (tty) tcsetattr(0, TCSAFLUSH, &raw);
    wall(1, "\x1b[?1049h\x1b[H\x1b[2J", 14);
    char cwd[1024], w2[128] = "";
    if (!getcwd(cwd, sizeof cwd)) cwd[0] = 0;
    if (!ok) fail(w2, sizeof w2, "out of memory");
    /* split/tab: tab 1 holds a main shell pane, then the request runs */
    if (ok && !tog && pane_new(S.cur = tab_new("main"), -1, 'h', cwd, NULL, w2, sizeof w2) >= 0)
        request(kind, dir, cwd, cmds, n, -1, w2, sizeof w2);
    if (ok && tog && request(kind, dir, cwd, cmds, n, -1, w2, sizeof w2) < 0) S.quit = true;
    if (w2[0]) note(w2);
    if (ok && !S.quit) loop();

    shutdown_all();
    wall(1, "\x1b[0m\x1b[?25h\x1b[?1049l", 19);
    if (tty) tcsetattr(0, TCSAFLUSH, &tio);
    for (int i = 0; i < 5; i++) sigaction(sigs[i], &old[i], NULL);
    close(S.lfd), close(S.sp[0]), close(S.sp[1]), unlink(S.sock);
    free(S.scr), free(S.prev), free(S.ob);
    S.scr = S.prev = NULL, S.ob = NULL;
    if (w2[0]) fail(why, wm, w2);
    if (tog) return S.own_done ? S.own_tag : zxsh_tag(LPX_B_NEITHER, LPX_P_NEUTRAL);
    return zxsh_tag(w2[0] ? LPX_B_NEITHER : LPX_B_TRUE, LPX_P_NEUTRAL);
}

/* ------------------------------------------------------------ client */

static size_t esc_into(char *o, const char *s)
{
    size_t n = 0;
    for (; *s; s++) {
        bool e = *s == '\t' || *s == '\n' || *s == '\\';
        if (e) o[n++] = '\\';
        o[n++] = !e ? *s : *s == '\t' ? 't' : *s == '\n' ? 'n' : '\\';
    }
    return n;
}

static lpx_t client(const char *sock, const char *kind, char dir, const char *const *cmds, int n,
                    char *why, uint32_t wm)
{
    struct sockaddr_un a = {0};
    struct sigaction ign = {0}, old;
    char cwd[1024], d[2] = {dir ? dir : 'h', 0}, r[256];
    const char *f[3 + ZMUX_MAX_CMDS] = {kind, d, cwd};
    size_t cap = 2, L = 0, got = 0;
    a.sun_family = AF_UNIX;
    if (strlen(sock) >= sizeof a.sun_path) return nei(why, wm, "ZXSH_MUX_SOCK is too long");
    memcpy(a.sun_path, sock, strlen(sock) + 1);
    if (!getcwd(cwd, sizeof cwd)) cwd[0] = 0;
    for (int i = 0; i < n; i++) f[3 + i] = cmds[i];
    for (int i = 0; i < n + 3; i++) cap += 2 * strlen(f[i]) + 1;
    char *line = malloc(cap);
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (!line || fd < 0 || connect(fd, (struct sockaddr *) &a, sizeof a))
        return free(line), close(fd), nei(why, wm, "the multiplexer is not reachable");
    for (int i = 0; i < n + 3; i++) {
        if (i) line[L++] = '\t';
        L += esc_into(line + L, f[i]);
    }
    line[L++] = '\n';
    ign.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &ign, &old);
    wall(fd, line, L);
    free(line);
    while (got < sizeof r - 1 && !memchr(r, '\n', got)) { /* together waits here */
        ssize_t k = read(fd, r + got, sizeof r - 1 - got);
        if (k <= 0 && !(k < 0 && errno == EINTR)) break;
        got += k > 0 ? (size_t) k : 0;
    }
    sigaction(SIGPIPE, &old, NULL);
    close(fd);
    r[got] = 0;
    r[strcspn(r, "\n")] = 0;
    if (!strcmp(r, "ok")) return zxsh_tag(LPX_B_TRUE, LPX_P_NEUTRAL);
    if (!strncmp(r, "tag ", 4)) return (lpx_t) (atoi(r + 4) & 63);
    return nei(why, wm, !strncmp(r, "err ", 4) ? r + 4 : "no reply from the multiplexer");
}

/* ------------------------------------------------- no terminal: together */

typedef struct {
    int fd;
    pid_t pid;
    size_t n;
    char buf[4096];
} job_t;

static void job_line(int k, const char *pre, const char *s, size_t n) /* "[k] PRE S\n" */
{
    size_t h = 24 + strlen(pre);
    char *o = malloc(n + h);
    int m = o ? snprintf(o, h, "[%d] %s", k + 1, pre) : 0;
    if (o) memcpy(o + m, s, n), o[(size_t) m + n] = '\n', wall(1, o, (size_t) m + n + 1);
    free(o);
}

static void job_read(job_t *q, int k)
{
    ssize_t r = read(q->fd, q->buf + q->n, sizeof q->buf - q->n);
    char *s = q->buf, *nl;
    if (r < 0 && errno == EINTR) return;
    if (r <= 0 && q->n) job_line(k, "", q->buf, q->n); /* an unterminated last line */
    if (r <= 0) close(q->fd), q->fd = -1, q->n = 0;
    if (r <= 0) return;
    q->n += (size_t) r;
    while ((nl = memchr(s, '\n', q->n - (size_t) (s - q->buf))))
        job_line(k, "", s, (size_t) (nl - s)), s = nl + 1;
    q->n -= (size_t) (s - q->buf);
    memmove(q->buf, s, q->n);
    if (q->n == sizeof q->buf) job_line(k, "", q->buf, q->n), q->n = 0; /* over-long: pieces */
}

static lpx_t batch(const char *self, const char *const *cmds, int n, char *why, uint32_t wm)
{
    job_t *j = n > 0 ? calloc((size_t) n, sizeof *j) : NULL;
    lpx_t tags[ZMUX_MAX_CMDS];
    struct pollfd pf[ZMUX_MAX_CMDS];
    int idx[ZMUX_MAX_CMDS], m = 1;
    if (!j) return nei(why, wm, n > 0 ? "out of memory" : "together needs commands");
    for (int k = 0; k < n; k++) {
        int pp[2];
        j[k].fd = j[k].pid = -1;
        if (pipe(pp)) continue;
        fcntl(pp[0], F_SETFD, FD_CLOEXEC);
        if ((j[k].pid = fork()) == 0) {
            int z = open("/dev/null", O_RDONLY);
            dup2(z, 0);
            dup2(pp[1], 1);
            dup2(pp[1], 2);
            close(pp[1]);
            execl(self, self, "-c", cmds[k], (char *) 0);
            _exit(127);
        }
        close(pp[1]);
        j[k].fd = j[k].pid < 0 ? (close(pp[0]), -1) : pp[0];
    }
    while (m) {
        m = 0;
        for (int k = 0; k < n; k++)
            if (j[k].fd >= 0) pf[m] = (struct pollfd){j[k].fd, POLLIN, 0}, idx[m++] = k;
        if (m && poll(pf, (nfds_t) m, -1) < 0 && errno != EINTR) break;
        for (int i = 0; i < m; i++)
            if (pf[i].revents) job_read(&j[idx[i]], idx[i]);
    }
    for (int k = 0; k < n; k++) {
        int st = 0;
        pid_t r = -1;
        while (j[k].pid > 0 && (r = waitpid(j[k].pid, &st, 0)) < 0 && errno == EINTR) continue;
        tags[k] = r == j[k].pid ? zmux_tag_from_status(st) : zxsh_tag(LPX_B_NEITHER, LPX_P_NEUTRAL);
    }
    for (int k = 0; k < n; k++) {
        char h[4] = {zxsh_letter(tags[k]), ' ', ' ', 0};
        job_line(k, h, cmds[k], strlen(cmds[k]));
    }
    free(j);
    return zmux_combine(tags, n);
}

/* ------------------------------------------------------------- entry */

lpx_t zmux_request(const zmux_cfg_t *cfg, const char *kind, char dir, const char *const *cmds,
                   int n, char *why, uint32_t whymax)
{
    if (why && whymax) why[0] = 0;
    if (!cfg || !cfg->self || !kind || n < 0 || (n && !cmds))
        return nei(why, whymax, "bad request");
    bool tog = !strcmp(kind, "together");
    if (!tog && strcmp(kind, "split") && strcmp(kind, "tab"))
        return nei(why, whymax, "unknown sub-terminal request (split, tab, together)");
    if (n > ZMUX_MAX_CMDS) return nei(why, whymax, "too many commands (at most 16)");
    const char *sock = getenv("ZXSH_MUX_SOCK");
    if (sock && *sock) return client(sock, kind, dir, cmds, n, why, whymax);
    if (isatty(0) && isatty(1)) return server(cfg, kind, dir, cmds, n, why, whymax);
    if (!tog) return nei(why, whymax, "sub-terminals need a terminal");
    return batch(cfg->self, cmds, n, why, whymax);
}
