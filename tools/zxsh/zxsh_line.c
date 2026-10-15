/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxsh_line.c — line editing (zxsh_line.h L1-L3). */
#define _DEFAULT_SOURCE
#define _DARWIN_C_SOURCE
#include "zxsh_line.h"
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#define HIST_MAX    1000
#define MAX_CHOICES 256

static char *hist[HIST_MAX];
static int nhist;
static char yank[4096];

void zline_history_add(const char *line)
{
    if (!line[strspn(line, " \t")]) return;
    if (nhist && !strcmp(hist[nhist - 1], line)) return;
    char *c = strdup(line);
    if (!c) return;
    if (nhist == HIST_MAX) {
        free(hist[0]);
        memmove(hist, hist + 1, (HIST_MAX - 1) * sizeof *hist);
        nhist--;
    }
    hist[nhist++] = c;
}

int zline_history_count(void)
{
    return nhist;
}

/* --------------------------------------------------------------- output */

/* Through stdio, flushed at once: this is the user's own terminal, and
 * history lines come from a file under $HOME. */
static void put(const char *s, size_t n)
{
    while (n) {
        size_t w = fwrite(s, 1, n, stdout);
        if (w == 0) {
            if (ferror(stdout) && errno == EINTR) {
                clearerr(stdout);
                continue;
            }
            break;
        }
        s += w;
        n -= w;
    }
    fflush(stdout);
}

static void puts_(const char *s)
{
    put(s, strlen(s));
}

static int cols(void)
{
    struct winsize ws;
    if (ioctl(1, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) return ws.ws_col;
    return 80;
}

/* visible width of s: escapes skipped, UTF-8 continuation bytes skipped */
static size_t vis(const char *s, size_t n)
{
    size_t w = 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char) s[i];
        if (c == 0x1b) {
            i++;
            if (i < n && s[i] == '[')
                while (i + 1 < n && !((s[i + 1] >= '@' && s[i + 1] <= '~'))) i++;
            i++;
            continue;
        }
        if ((c & 0xC0) != 0x80) w++;
    }
    return w;
}

typedef struct {
    char *b;
    size_t max, len, pos;
    const char *prompt;
} ed_t;

static void refresh(ed_t *e)
{
    size_t pw = vis(e->prompt, strlen(e->prompt)), w = (size_t) cols();
    size_t room = w > pw + 2 ? w - pw - 1 : 1;
    /* L2: scroll so the cursor stays visible */
    size_t start = 0, cur = vis(e->b, e->pos);
    while (cur - vis(e->b, start) >= room && start < e->pos) {
        start++;
        while (start < e->pos && ((unsigned char) e->b[start] & 0xC0) == 0x80) start++;
    }
    size_t end = start, shown = 0;
    while (end < e->len && shown < room) {
        end++;
        while (end < e->len && ((unsigned char) e->b[end] & 0xC0) == 0x80) end++;
        shown++;
    }
    char tail[32];
    puts_("\r");
    puts_(e->prompt);
    put(e->b + start, end - start);
    puts_("\033[K");
    size_t back = vis(e->b + e->pos, end > e->pos ? end - e->pos : 0);
    if (back) {
        snprintf(tail, sizeof tail, "\033[%zuD", back);
        puts_(tail);
    }
}

static void insert(ed_t *e, const char *s, size_t n)
{
    if (e->len + n + 1 > e->max) return;
    memmove(e->b + e->pos + n, e->b + e->pos, e->len - e->pos);
    memcpy(e->b + e->pos, s, n);
    e->len += n;
    e->pos += n;
    e->b[e->len] = 0;
}

static void erase(ed_t *e, size_t from, size_t to, bool keep)
{
    if (to <= from) return;
    if (keep && to - from < sizeof yank) {
        memcpy(yank, e->b + from, to - from);
        yank[to - from] = 0;
    }
    memmove(e->b + from, e->b + to, e->len - to);
    e->len -= to - from;
    e->b[e->len] = 0;
    if (e->pos > to)
        e->pos -= to - from;
    else if (e->pos > from)
        e->pos = from;
}

static size_t prev_char(const ed_t *e, size_t p)
{
    if (!p) return 0;
    p--;
    while (p && ((unsigned char) e->b[p] & 0xC0) == 0x80) p--;
    return p;
}

static size_t next_char(const ed_t *e, size_t p)
{
    if (p >= e->len) return e->len;
    p++;
    while (p < e->len && ((unsigned char) e->b[p] & 0xC0) == 0x80) p++;
    return p;
}

static bool wordch(char c)
{
    return c != ' ' && c != '\t' && c != '/' && c != ';' && c != '|' && c != '&';
}

static size_t word_left(const ed_t *e, size_t p)
{
    while (p && !wordch(e->b[p - 1])) p--;
    while (p && wordch(e->b[p - 1])) p--;
    return p;
}

static size_t word_right(const ed_t *e, size_t p)
{
    while (p < e->len && !wordch(e->b[p])) p++;
    while (p < e->len && wordch(e->b[p])) p++;
    return p;
}

static void set_line(ed_t *e, const char *s)
{
    size_t n = strlen(s);
    if (n + 1 > e->max) n = e->max - 1;
    memcpy(e->b, s, n);
    e->b[n] = 0;
    e->len = e->pos = n;
}

/* ----------------------------------------------------------- completion */

static int cmp_str(const void *a, const void *b)
{
    return strcmp(*(char *const *) a, *(char *const *) b);
}

static void complete(ed_t *e, zline_complete_fn comp, void *ctx, bool list)
{
    char *ch[MAX_CHOICES];
    size_t ws = e->pos;
    int n = comp(ctx, e->b, e->pos, ch, MAX_CHOICES, &ws);
    if (n <= 0) {
        puts_("\a");
        return;
    }
    if (ws > e->pos) ws = e->pos;
    qsort(ch, (size_t) n, sizeof *ch, cmp_str);
    /* the longest common prefix of every choice */
    size_t common = strlen(ch[0]);
    for (int i = 1; i < n; i++) {
        size_t k = 0;
        while (k < common && ch[i][k] == ch[0][k]) k++;
        common = k;
    }
    size_t typed = e->pos - ws;
    if (common > typed || n == 1) {
        if (common >= typed) {
            insert(e, ch[0] + typed, common - typed);
            size_t L = strlen(ch[0]);
            if (n == 1 && L && ch[0][L - 1] != '/') insert(e, " ", 1);
        }
    } else if (list) {
        int w = cols(), colw = 1;
        for (int i = 0; i < n; i++)
            if ((int) strlen(ch[i]) + 2 > colw) colw = (int) strlen(ch[i]) + 2;
        int per = w / colw > 0 ? w / colw : 1;
        puts_("\r\n");
        for (int i = 0; i < n; i++) {
            char cell[512];
            snprintf(cell, sizeof cell, "%-*s", colw, ch[i]);
            puts_(cell);
            if ((i + 1) % per == 0 || i + 1 == n) puts_("\r\n");
        }
    } else {
        puts_("\a");
    }
    for (int i = 0; i < n; i++) free(ch[i]);
}

/* --------------------------------------------------------------- search */

static int search(ed_t *e, const char *prompt)
{
    char q[256] = "";
    size_t ql = 0;
    int at = nhist;
    char p[400];
    for (;;) {
        int hit = -1;
        for (int i = at - 1; ql && i >= 0; i--)
            if (strstr(hist[i], q)) {
                hit = i;
                break;
            }
        if (hit < 0 && !ql) hit = -1;
        snprintf(p, sizeof p, "(search)'%s': ", q);
        ed_t view = {e->b, e->max, 0, 0, p};
        if (hit >= 0) set_line(e, hist[hit]);
        view.len = e->len;
        view.pos = e->len;
        refresh(&view);
        char c;
        if (read(0, &c, 1) != 1) return -1;
        if (c == 18) { /* Ctrl-R again: older match */
            if (hit >= 0) at = hit;
        } else if (c == 127 || c == 8) {
            if (ql) q[--ql] = 0;
            at = nhist;
        } else if (c == 3 || c == 7) {
            set_line(e, "");
            e->prompt = prompt;
            return 0;
        } else if (c == '\r' || c == '\n') {
            e->prompt = prompt;
            return 1; /* accept */
        } else if ((unsigned char) c >= 32 && ql + 1 < sizeof q) {
            q[ql++] = c;
            q[ql] = 0;
        } else {
            e->prompt = prompt;
            e->pos = e->len;
            return 0; /* any other key: edit the match */
        }
    }
}

/* ------------------------------------------------------------------ read */

static int read_plain(char *buf, size_t max)
{
    if (!fgets(buf, (int) max, stdin)) return ZLINE_EOF;
    size_t n = strlen(buf);
    if (n && buf[n - 1] == '\n') buf[--n] = 0;
    return (int) n;
}

int zline_read(const char *prompt, char *buf, size_t max, zline_complete_fn comp, void *ctx)
{
    const char *term = getenv("TERM");
    struct termios old, raw;
    if (max < 2) return ZLINE_EOF;
    if (!isatty(0) || !isatty(1) || (term && !strcmp(term, "dumb")) || tcgetattr(0, &old) != 0) {
        puts_(prompt);
        return read_plain(buf, max);
    }
    raw = old;
    raw.c_iflag &= (tcflag_t) ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_lflag &= (tcflag_t) ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(0, TCSAFLUSH, &raw) != 0) {
        puts_(prompt);
        return read_plain(buf, max);
    }
    ed_t e = {buf, max, 0, 0, prompt};
    buf[0] = 0;
    int hi = nhist, result = 0;
    char saved[4096] = "";
    bool last_tab = false;
    refresh(&e);
    for (;;) {
        char c;
        ssize_t r = read(0, &c, 1);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) {
            result = ZLINE_EOF;
            break;
        }
        bool tab = false;
        switch (c) {
        case '\r':
        case '\n':
            result = (int) e.len;
            goto done;
        case 3: /* Ctrl-C */
            puts_("^C");
            result = ZLINE_INTR;
            goto done;
        case 4: /* Ctrl-D */
            if (!e.len) {
                result = ZLINE_EOF;
                goto done;
            }
            erase(&e, e.pos, next_char(&e, e.pos), false);
            break;
        case 1:
            e.pos = 0;
            break;
        case 5:
            e.pos = e.len;
            break;
        case 2:
            e.pos = prev_char(&e, e.pos);
            break;
        case 6:
            e.pos = next_char(&e, e.pos);
            break;
        case 11:
            erase(&e, e.pos, e.len, true);
            break;
        case 21:
            erase(&e, 0, e.pos, true);
            break;
        case 23:
            erase(&e, word_left(&e, e.pos), e.pos, true);
            break;
        case 25:
            insert(&e, yank, strlen(yank));
            break;
        case 20: /* Ctrl-T */
            if (e.pos > 0 && e.len > 1) {
                if (e.pos == e.len) e.pos--;
                char t = e.b[e.pos - 1];
                e.b[e.pos - 1] = e.b[e.pos];
                e.b[e.pos] = t;
                e.pos++;
            }
            break;
        case 12:
            puts_("\033[H\033[2J");
            break;
        case 127:
        case 8:
            erase(&e, prev_char(&e, e.pos), e.pos, false);
            break;
        case 9:
            tab = true;
            if (comp) complete(&e, comp, ctx, last_tab);
            break;
        case 18: {
            int s = search(&e, prompt);
            if (s < 0) {
                result = ZLINE_EOF;
                goto done;
            }
            if (s == 1) {
                refresh(&e);
                result = (int) e.len;
                goto done;
            }
            break;
        }
        case 27: {
            char s[3] = {0, 0, 0};
            if (read(0, s, 1) != 1) break;
            if (s[0] == 'b')
                e.pos = word_left(&e, e.pos);
            else if (s[0] == 'f')
                e.pos = word_right(&e, e.pos);
            else if (s[0] == 127)
                erase(&e, word_left(&e, e.pos), e.pos, true);
            else if (s[0] == '[' || s[0] == 'O') {
                if (read(0, s + 1, 1) != 1) break;
                char k = s[1];
                if (k >= '0' && k <= '9') {
                    char t;
                    if (read(0, &t, 1) != 1) break;
                    while (t != '~' && t >= '0' && t <= ';')
                        if (read(0, &t, 1) != 1) break;
                    if (k == '1' || k == '7')
                        e.pos = 0;
                    else if (k == '4' || k == '8')
                        e.pos = e.len;
                    else if (k == '3')
                        erase(&e, e.pos, next_char(&e, e.pos), false);
                    break;
                }
                if (k == 'A' || k == 'B') {
                    if (hi == nhist) snprintf(saved, sizeof saved, "%s", e.b);
                    if (k == 'A' && hi > 0)
                        hi--;
                    else if (k == 'B' && hi < nhist)
                        hi++;
                    else
                        break;
                    set_line(&e, hi == nhist ? saved : hist[hi]);
                } else if (k == 'C')
                    e.pos = next_char(&e, e.pos);
                else if (k == 'D')
                    e.pos = prev_char(&e, e.pos);
                else if (k == 'H')
                    e.pos = 0;
                else if (k == 'F')
                    e.pos = e.len;
            }
            break;
        }
        default:
            if ((unsigned char) c >= 32) insert(&e, &c, 1);
            break;
        }
        last_tab = tab;
        refresh(&e);
    }
done:
    tcsetattr(0, TCSAFLUSH, &old);
    puts_("\r\n");
    if (result == ZLINE_INTR) buf[0] = 0;
    return result;
}
