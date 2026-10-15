/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* hollow_model.c — Hollow, the desktop (hollow_model.h). */
#include "hollow_model.h"

#include <string.h>

/* ---- the rules ------------------------------------------------------------ */

int hollow_grid(int64_t n, int64_t sw, int64_t sh, hollow_grid_t *g)
{
    if (!g || sw < 1 || sw > HOLLOW_PX_MAX || sh < 1 || sh > HOLLOW_PX_MAX) return ZXM_E_ARG;
    int64_t cols = hollow_tile_cols(n), rows = hollow_tile_rows(n);
    if (cols < 0 || rows < 0) return ZXM_E_ARG;
    hollow_grid_t t = {cols, rows, 0, 0};
    if (n > 0) {
        t.cell_w = hollow_tile_cell(sw, cols);
        t.cell_h = hollow_tile_cell(sh, rows);
        if (t.cell_w < 0 || t.cell_h < 0) return ZXM_E_ARG;
    }
    *g = t;
    return ZXM_OK;
}

bool hollow_window_ok(int64_t x, int64_t y, int64_t width, int64_t height, int64_t sw, int64_t sh)
{
    if (sw < 1 || sw > HOLLOW_PX_MAX || sh < 1 || sh > HOLLOW_PX_MAX) return false;
    int64_t across = hollow_shown(x, width, sw), down = hollow_shown(y, height, sh);
    if (across < 0 || down < 0) return false;
    return hollow_window_fits(width, height, across, down);
}

int32_t hollow_image_kind(const uint8_t *b, uint32_t n)
{
    if (!b) return HOLLOW_IMG_NONE;
    if (n >= 8u && !memcmp(b, "\x89PNG\r\n\x1a\n", 8)) return HOLLOW_IMG_PNG;
    if (n >= 3u && b[0] == 0xff && b[1] == 0xd8 && b[2] == 0xff) return HOLLOW_IMG_JPEG;
    if (n >= 6u && (!memcmp(b, "GIF87a", 6) || !memcmp(b, "GIF89a", 6))) return HOLLOW_IMG_GIF;
    if (n >= 2u && b[0] == 'B' && b[1] == 'M') return HOLLOW_IMG_BMP;
    if (n >= 12u && !memcmp(b, "RIFF", 4) && !memcmp(b + 8, "WEBP", 4)) return HOLLOW_IMG_WEBP;
    return HOLLOW_IMG_NONE;
}

/* ---- the desk as text (HD1-HD5) -------------------------------------------- */

static const char HEAD[] = "hollow 1\n";

typedef struct {
    char wallpaper[HOLLOW_PATH_MAX + 1];
    hollow_pin_t dock[HOLLOW_DOCK_MAX];
    uint32_t ndock;
    hollow_win_t win[HOLLOW_WIN_MAX];
    uint32_t nwin;
} desk_t;

static bool put(char *out, uint32_t cap, uint32_t *at, const char *s)
{
    uint32_t n = (uint32_t) strlen(s);
    if (*at + n + 1u > cap) return false;
    memcpy(out + *at, s, n);
    *at += n;
    out[*at] = 0;
    return true;
}

static bool put_num(char *out, uint32_t cap, uint32_t *at, int64_t v)
{
    char t[24];
    int k = 23;
    bool neg = v < 0;
    uint64_t u = neg ? (uint64_t) 0 - (uint64_t) v : (uint64_t) v;
    t[k] = 0;
    do {
        t[--k] = (char) ('0' + (int) (u % 10u));
        u /= 10u;
    } while (u && k > 1);
    if (neg) t[--k] = '-';
    return put(out, cap, at, t + k);
}

/* " <v>" */
static bool put_arg(char *out, uint32_t cap, uint32_t *at, int64_t v)
{
    return put(out, cap, at, " ") && put_num(out, cap, at, v);
}

/* The whole desk (HD1-HD4); its length, or -1 when it does not fit. */
static int32_t compose(const desk_t *d, char *out, uint32_t cap)
{
    uint32_t at = 0;
    if (!put(out, cap, &at, HEAD)) return -1;
    if (d->wallpaper[0] && !(put(out, cap, &at, "wallpaper ") && put(out, cap, &at, d->wallpaper) &&
                             put(out, cap, &at, "\n")))
        return -1;
    for (uint32_t i = 0; i < d->ndock; i++)
        if (!(put(out, cap, &at, "pin") && put_arg(out, cap, &at, d->dock[i].app) &&
              put_arg(out, cap, &at, d->dock[i].added) && put(out, cap, &at, "\n")))
            return -1;
    for (uint32_t i = 0; i < d->nwin; i++) {
        const hollow_win_t *w = &d->win[i];
        if (!(put(out, cap, &at, "win") && put_arg(out, cap, &at, w->app) &&
              put_arg(out, cap, &at, w->x) && put_arg(out, cap, &at, w->y) &&
              put_arg(out, cap, &at, w->width) && put_arg(out, cap, &at, w->height) &&
              put(out, cap, &at, "\n")))
            return -1;
    }
    return (int32_t) at;
}

/* A plain decimal of 1 to 12 digits, no leading zero, '-' allowed when
 * neg, at s[*at], ended by `end`. */
static bool get_num(const char *s, uint32_t n, uint32_t *at, char end, bool neg, int64_t *v)
{
    uint32_t i = *at, digits = 0;
    bool minus = false;
    int64_t x = 0;
    if (neg && i < n && s[i] == '-') {
        minus = true;
        i++;
    }
    uint32_t first = i;
    while (i < n && s[i] >= '0' && s[i] <= '9' && digits < 13u) {
        x = x * 10 + (s[i] - '0');
        i++;
        digits++;
    }
    if (!digits || digits > 12u || i >= n || s[i] != end) return false;
    if (digits > 1u && s[first] == '0') return false;
    if (minus && !x) return false; /* "-0" is not plain */
    *v = minus ? -x : x;
    *at = i + 1u;
    return true;
}

static bool app_ok(int64_t app)
{
    return app >= 1 && app <= HOLLOW_APP_MAX;
}

static int32_t find_pin(const desk_t *d, int64_t app)
{
    for (uint32_t i = 0; i < d->ndock; i++)
        if (d->dock[i].app == app) return (int32_t) i;
    return -1;
}

static int32_t find_win(const desk_t *d, int64_t app)
{
    for (uint32_t i = 0; i < d->nwin; i++)
        if (d->win[i].app == app) return (int32_t) i;
    return -1;
}

static bool pin_ok(const desk_t *d, uint32_t place, int64_t app, int64_t added)
{
    return place < HOLLOW_DOCK_MAX && app_ok(app) && added >= 0 &&
           hollow_dock_fits((int64_t) place, app, 1, added) && find_pin(d, app) < 0;
}

static bool lead(const char *s, uint32_t n, uint32_t *at, const char *word)
{
    uint32_t k = (uint32_t) strlen(word);
    if (n - *at < k || memcmp(s + *at, word, k)) return false;
    *at += k;
    return true;
}

/* Parse text into d (HD1-HD5): false when it is not a desk. */
static bool parse(desk_t *d, const char *s, uint32_t n)
{
    uint32_t at = (uint32_t) strlen(HEAD);
    int stage = 0; /* 0 wallpaper may come, 1 pins, 2 windows */
    if (n < at || memcmp(s, HEAD, at)) return false;
    memset(d, 0, sizeof *d);
    while (at < n) {
        int64_t a, b, x, y, w, ht;
        if (stage == 0 && lead(s, n, &at, "wallpaper ")) {
            uint32_t k = 0;
            while (at + k < n && s[at + k] != '\n') {
                unsigned char c = (unsigned char) s[at + k];
                if (c < 0x20u || c == 0x7fu || k >= HOLLOW_PATH_MAX) return false;
                k++;
            }
            if (!k || at + k >= n) return false;
            memcpy(d->wallpaper, s + at, k);
            d->wallpaper[k] = 0;
            at += k + 1u;
            stage = 1;
            continue;
        }
        if (stage <= 1 && lead(s, n, &at, "pin ")) {
            if (!get_num(s, n, &at, ' ', false, &a) || !get_num(s, n, &at, '\n', false, &b) ||
                !pin_ok(d, d->ndock, a, b))
                return false;
            d->dock[d->ndock++] = (hollow_pin_t){(int32_t) a, b};
            stage = 1;
            continue;
        }
        if (lead(s, n, &at, "win ")) {
            if (!get_num(s, n, &at, ' ', false, &a) || !get_num(s, n, &at, ' ', true, &x) ||
                !get_num(s, n, &at, ' ', true, &y) || !get_num(s, n, &at, ' ', false, &w) ||
                !get_num(s, n, &at, '\n', false, &ht) || !app_ok(a) || find_win(d, a) >= 0 ||
                d->nwin >= HOLLOW_WIN_MAX ||
                !hollow_window_ok(x, y, w, ht, HOLLOW_PX_MAX, HOLLOW_PX_MAX))
                return false;
            d->win[d->nwin++] =
                (hollow_win_t){(int32_t) a, (int32_t) x, (int32_t) y, (int32_t) w, (int32_t) ht};
            stage = 2;
            continue;
        }
        return false;
    }
    return true;
}

/* ---- zxvd: the desk file ---------------------------------------------------- */

/* One request's text: whole lines of at most this many bytes. No control
 * byte is ever written, so each escapes to at most two (\" or \\), well
 * inside the request line. */
#define PIECE 6000u

static int write_text(zxm_link_t *l, const char *text, uint32_t len)
{
    uint32_t at = 0;
    bool first = true;
    do {
        uint32_t n = len - at;
        if (n > PIECE) {
            n = PIECE;
            while (n && text[at + n - 1u] != '\n') n--;
            if (!n) return ZXM_E_TOOBIG;
        }
        bool last = at + n >= len;
        zxm_begin(l, "files.write");
        zxm_p_str(l, "path", HOLLOW_DESK);
        zxm_p_strn(l, "text", text + at, n);
        if (!(first && last)) zxm_p_str(l, "mode", first ? "begin" : last ? "end" : "add");
        int rc = zxm_call(l);
        if (rc) return rc;
        at += n;
        first = false;
    } while (at < len);
    return ZXM_OK;
}

static void keep(hollow_t *h, const desk_t *d)
{
    memcpy(h->wallpaper, d->wallpaper, sizeof h->wallpaper);
    memcpy(h->dock, d->dock, sizeof h->dock);
    h->ndock = d->ndock;
    memcpy(h->win, d->win, sizeof h->win);
    h->nwin = d->nwin;
}

static desk_t NEXT; /* a change before it is written (HD6) */

static void begin_change(const hollow_t *h)
{
    memcpy(NEXT.wallpaper, h->wallpaper, sizeof NEXT.wallpaper);
    memcpy(NEXT.dock, h->dock, sizeof NEXT.dock);
    NEXT.ndock = h->ndock;
    memcpy(NEXT.win, h->win, sizeof NEXT.win);
    NEXT.nwin = h->nwin;
}

/* Write NEXT whole, then keep it (HD6). */
static int commit(hollow_t *h)
{
    static char text[HOLLOW_TEXT_MAX];
    int32_t len = compose(&NEXT, text, sizeof text);
    if (len < 0) return ZXM_E_TOOBIG;
    int rc = write_text(h->link, text, (uint32_t) len);
    if (rc) return rc;
    keep(h, &NEXT);
    return ZXM_OK;
}

void hollow_init(hollow_t *h, zxm_link_t *link)
{
    memset(h, 0, sizeof *h);
    h->link = link;
    memcpy(h->here.id, "home", 5);
    memcpy(h->here.kind, "home", 5);
    h->here.write = true;
    h->front.left = -1;
}

int hollow_load(hollow_t *h)
{
    static char text[HOLLOW_TEXT_MAX];
    static char chunk[HOLLOW_TEXT_MAX];
    zxm_link_t *l = h->link;
    uint32_t len = 0;
    uint64_t off = 0;
    for (;;) {
        zxm_begin(l, "files.read");
        zxm_p_str(l, "path", HOLLOW_DESK);
        zxm_p_u64(l, "offset", off);
        int rc = zxm_call(l);
        if (rc == ZXM_E_REMOTE && l->err_code == HOLLOW_E_FILES && !off &&
            strstr(l->err, "no such file")) {
            memset(&NEXT, 0, sizeof NEXT); /* not made yet: an empty desk */
            keep(h, &NEXT);
            h->wallpaper_kind = HOLLOW_IMG_NONE;
            h->loaded = true;
            return ZXM_OK;
        }
        if (rc) return rc;
        uint64_t size, next_off;
        bool eof;
        int32_t t = zxm_get(l, l->result, "text");
        if (zxm_get_u64(l, l->result, "size", &size) ||
            zxm_get_u64(l, l->result, "next", &next_off) ||
            zxm_get_bool(l, l->result, "eof", &eof) || t < 0)
            return ZXM_E_PROTOCOL;
        if (size >= HOLLOW_TEXT_MAX) return ZXM_E_TOOBIG;
        int32_t n = w4_json_str(l->resp, &l->tok[t], chunk, sizeof chunk);
        if (n < 0 || len + (uint32_t) n >= HOLLOW_TEXT_MAX) return ZXM_E_TOOBIG;
        memcpy(text + len, chunk, (size_t) n);
        len += (uint32_t) n;
        if (eof) break;
        if (next_off <= off) return ZXM_E_PROTOCOL;
        off = next_off;
    }
    if (!parse(&NEXT, text, len)) {
        h->loaded = false; /* HD5: never overwritten */
        return ZXM_E_PROTOCOL;
    }
    keep(h, &NEXT);
    h->loaded = true;
    return ZXM_OK;
}

int hollow_pin(hollow_t *h, int64_t app, int64_t now)
{
    if (!app_ok(app) || now < 0 || !hollow_digits_fit(now, 12)) return ZXM_E_ARG;
    if (!h->loaded) return ZXM_E_STATE;
    begin_change(h);
    if (find_pin(&NEXT, app) >= 0) return ZXM_E_STATE;
    if (NEXT.ndock >= HOLLOW_DOCK_MAX) return ZXM_E_TOOBIG;
    if (!pin_ok(&NEXT, NEXT.ndock, app, now)) return ZXM_E_ARG;
    NEXT.dock[NEXT.ndock++] = (hollow_pin_t){(int32_t) app, now};
    return commit(h);
}

int hollow_unpin(hollow_t *h, uint32_t place)
{
    if (!h->loaded) return ZXM_E_STATE;
    if (place >= h->ndock) return ZXM_E_ARG;
    begin_change(h);
    memmove(&NEXT.dock[place], &NEXT.dock[place + 1u],
            (size_t) (NEXT.ndock - place - 1u) * sizeof NEXT.dock[0]);
    NEXT.ndock--;
    return commit(h);
}

int hollow_move(hollow_t *h, uint32_t from, uint32_t to)
{
    if (!h->loaded) return ZXM_E_STATE;
    if (from >= h->ndock || to >= h->ndock) return ZXM_E_ARG;
    if (from == to) return ZXM_OK;
    begin_change(h);
    hollow_pin_t p = NEXT.dock[from];
    if (from < to)
        memmove(&NEXT.dock[from], &NEXT.dock[from + 1u], (size_t) (to - from) * sizeof p);
    else
        memmove(&NEXT.dock[to + 1u], &NEXT.dock[to], (size_t) (from - to) * sizeof p);
    NEXT.dock[to] = p;
    for (uint32_t i = 0; i < NEXT.ndock; i++)
        if (!hollow_dock_fits((int64_t) i, NEXT.dock[i].app, 1, NEXT.dock[i].added))
            return ZXM_E_ARG;
    return commit(h);
}

/* Put a window place into NEXT (replacing app's own). */
static int set_place(int64_t app, int64_t x, int64_t y, int64_t w, int64_t ht, int64_t sw,
                     int64_t sh)
{
    if (!app_ok(app) || !hollow_window_ok(x, y, w, ht, sw, sh)) return ZXM_E_ARG;
    int32_t i = find_win(&NEXT, app);
    if (i < 0) {
        if (NEXT.nwin >= HOLLOW_WIN_MAX) return ZXM_E_TOOBIG;
        i = (int32_t) NEXT.nwin++;
    }
    NEXT.win[i] =
        (hollow_win_t){(int32_t) app, (int32_t) x, (int32_t) y, (int32_t) w, (int32_t) ht};
    return ZXM_OK;
}

int hollow_place(hollow_t *h, int64_t app, int64_t x, int64_t y, int64_t width, int64_t height,
                 int64_t sw, int64_t sh)
{
    if (!app_ok(app) || !hollow_window_ok(x, y, width, height, sw, sh)) return ZXM_E_ARG;
    if (!h->loaded) return ZXM_E_STATE;
    begin_change(h);
    int rc = set_place(app, x, y, width, height, sw, sh);
    return rc ? rc : commit(h);
}

int hollow_tile(hollow_t *h, const int32_t *apps, uint32_t n, int64_t sw, int64_t sh)
{
    hollow_grid_t g;
    if (!apps && n) return ZXM_E_ARG;
    if (hollow_grid((int64_t) n, sw, sh, &g)) return ZXM_E_ARG;
    for (uint32_t i = 0; i < n; i++) {
        if (!app_ok(apps[i])) return ZXM_E_ARG;
        for (uint32_t k = 0; k < i; k++)
            if (apps[k] == apps[i]) return ZXM_E_ARG;
    }
    if (n && !hollow_window_ok(0, 0, g.cell_w, g.cell_h, sw, sh)) return ZXM_E_ARG;
    if (!h->loaded) return ZXM_E_STATE;
    begin_change(h);
    for (uint32_t i = 0; i < n; i++) {
        int64_t col = (int64_t) i % g.cols, row = (int64_t) i / g.cols;
        int rc = set_place(apps[i], col * g.cell_w, row * g.cell_h, g.cell_w, g.cell_h, sw, sh);
        if (rc) return rc;
    }
    return commit(h);
}

/* ---- the wallpaper ---------------------------------------------------------- */

/* strict RFC 4648, as zxvd writes it */
static int64_t unb64(const char *s, uint32_t n, uint8_t *out, uint64_t cap)
{
    uint64_t o = 0;
    if (n % 4u) return -1;
    for (uint32_t i = 0; i < n; i += 4) {
        uint32_t v = 0, pad = 0;
        for (uint32_t j = 0; j < 4; j++) {
            char c = s[i + j];
            uint32_t d;
            if (c >= 'A' && c <= 'Z')
                d = (uint32_t) (c - 'A');
            else if (c >= 'a' && c <= 'z')
                d = (uint32_t) (c - 'a') + 26u;
            else if (c >= '0' && c <= '9')
                d = (uint32_t) (c - '0') + 52u;
            else if (c == '+')
                d = 62;
            else if (c == '/')
                d = 63;
            else if (c == '=' && i + 4 == n && j >= 2)
                d = 0, pad++;
            else
                return -1;
            if (pad && c != '=') return -1;
            v = v << 6 | d;
        }
        if (o + 3u - pad > cap) return -1;
        out[o++] = (uint8_t) (v >> 16);
        if (pad < 2) out[o++] = (uint8_t) (v >> 8);
        if (pad < 1) out[o++] = (uint8_t) v;
    }
    return (int64_t) o;
}

/* Read path's bytes from the start, up to cap (want: at most this many). */
static int read_image(zxm_link_t *l, const char *path, uint8_t *buf, uint32_t cap, uint32_t want,
                      uint32_t *len)
{
    uint64_t off = 0;
    *len = 0;
    for (;;) {
        zxm_begin(l, "files.read");
        zxm_p_str(l, "path", path);
        zxm_p_u64(l, "offset", off);
        if (want) zxm_p_u64(l, "max", want);
        zxm_p_str(l, "encoding", "base64");
        int rc = zxm_call(l);
        if (rc) return rc;
        uint64_t next, size;
        bool eof;
        int32_t t = zxm_get(l, l->result, "base64");
        if (t < 0 || l->tok[t].type != W4_J_STR || zxm_get_u64(l, l->result, "next", &next) ||
            zxm_get_u64(l, l->result, "size", &size) || zxm_get_bool(l, l->result, "eof", &eof))
            return ZXM_E_PROTOCOL;
        if (!want && size > cap) return ZXM_E_TOOBIG;
        int64_t n = unb64(l->resp + l->tok[t].start, (uint32_t) (l->tok[t].end - l->tok[t].start),
                          buf + off, cap - off);
        if (n < 0 || off + (uint64_t) n != next) return ZXM_E_PROTOCOL;
        off = next;
        zxm_progress(l, off, size);
        if (eof || want) break;
    }
    *len = (uint32_t) off;
    return ZXM_OK;
}

int hollow_set_wallpaper(hollow_t *h, const char *path)
{
    uint8_t head[12];
    uint32_t got = 0;
    if (!path) return ZXM_E_ARG;
    size_t k = strlen(path);
    if (k > HOLLOW_PATH_MAX) return ZXM_E_ARG;
    for (size_t i = 0; i < k; i++)
        if ((unsigned char) path[i] < 0x20u || path[i] == 0x7f) return ZXM_E_ARG;
    if (!h->loaded) return ZXM_E_STATE;
    int32_t kind = HOLLOW_IMG_NONE;
    if (k) {
        int rc = read_image(h->link, path, head, sizeof head, sizeof head, &got);
        if (rc) return rc;
        kind = hollow_image_kind(head, got);
        if (kind == HOLLOW_IMG_NONE) return ZXM_E_ARG; /* not an image */
    }
    begin_change(h);
    memcpy(NEXT.wallpaper, path, k + 1u);
    int rc = commit(h);
    if (rc) return rc;
    h->wallpaper_kind = kind;
    return ZXM_OK;
}

int hollow_wallpaper(hollow_t *h, uint8_t *buf, uint32_t cap, uint32_t *len)
{
    if (!buf || !len) return ZXM_E_ARG;
    *len = 0;
    if (!h->wallpaper[0]) return ZXM_E_STATE;
    if (cap > HOLLOW_IMAGE_MAX) cap = HOLLOW_IMAGE_MAX;
    uint32_t got = 0;
    int rc = read_image(h->link, h->wallpaper, buf, cap, 0, &got);
    if (rc) return rc;
    int32_t kind = hollow_image_kind(buf, got);
    if (kind == HOLLOW_IMG_NONE) return ZXM_E_PROTOCOL; /* it changed: no longer an image */
    h->wallpaper_kind = kind;
    *len = got;
    return ZXM_OK;
}

/* ---- zxvd: spaces, notices, simple mode, the app in front ------------------- */

static int u32(zxm_link_t *l, int32_t o, const char *key, uint32_t max, uint32_t *v)
{
    uint64_t x;
    if (zxm_get_u64(l, o, key, &x) || x > max) return ZXM_E_PROTOCOL;
    *v = (uint32_t) x;
    return ZXM_OK;
}

static int read_space(zxm_link_t *l, int32_t o, hollow_space_t *s)
{
    memset(s, 0, sizeof *s);
    if (o < 0 || zxm_get_str(l, o, "id", s->id, sizeof s->id) || !s->id[0] ||
        zxm_get_str(l, o, "kind", s->kind, sizeof s->kind) ||
        zxm_get_str(l, o, "name", s->name, sizeof s->name) ||
        zxm_get_bool(l, o, "write", &s->write))
        return ZXM_E_PROTOCOL;
    return ZXM_OK;
}

static hollow_t SEEN; /* what zxvd said, before it is kept */

int hollow_refresh(hollow_t *h)
{
    zxm_link_t *l = h->link;
    hollow_t *t = &SEEN;
    zxm_begin(l, "spaces.list");
    int rc = zxm_call(l);
    if (rc) return rc;
    int32_t arr = zxm_get(l, l->result, "spaces");
    if (arr < 0 || l->tok[arr].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    uint32_t n = zxm_len(l, arr);
    t->nspaces = n < HOLLOW_SPACES ? n : HOLLOW_SPACES;
    t->more_spaces = n > HOLLOW_SPACES;
    for (uint32_t i = 0; i < t->nspaces; i++)
        if (read_space(l, zxm_at(l, arr, i), &t->space[i])) return ZXM_E_PROTOCOL;
    if (!t->nspaces || strcmp(t->space[0].id, "home")) return ZXM_E_PROTOCOL; /* home first */

    zxm_begin(l, "notes.list");
    if ((rc = zxm_call(l)) != ZXM_OK) return rc;
    arr = zxm_get(l, l->result, "notes");
    if (arr < 0 || l->tok[arr].type != W4_J_ARR || zxm_len(l, arr) > HOLLOW_NOTES ||
        u32(l, l->result, "unread", UINT32_MAX, &t->unread))
        return ZXM_E_PROTOCOL;
    t->nnotes = zxm_len(l, arr);
    for (uint32_t i = 0; i < t->nnotes; i++) {
        int32_t o = zxm_at(l, arr, i);
        hollow_note_t *m = &t->note[i];
        memset(m, 0, sizeof *m);
        if (o < 0 || u32(l, o, "seq", UINT32_MAX, &m->seq) || u32(l, o, "pri", 3, &m->pri) ||
            zxm_get_bool(l, o, "read", &m->read) ||
            zxm_get_str(l, o, "title", m->title, sizeof m->title))
            return ZXM_E_PROTOCOL;
    }

    t->simple = false;
    t->simple_known = false;
    zxm_begin(l, "ui.get");
    rc = zxm_call(l);
    if (rc == ZXM_OK) {
        if (zxm_get_bool(l, l->result, "simple_mode", &t->simple)) return ZXM_E_PROTOCOL;
        t->simple_known = true;
    } else if (!(rc == ZXM_E_REMOTE && l->err_code == HOLLOW_E_METHOD)) {
        return rc;
    }

    memcpy(h->space, t->space, (size_t) t->nspaces * sizeof h->space[0]);
    h->nspaces = t->nspaces;
    h->more_spaces = t->more_spaces;
    memcpy(h->note, t->note, (size_t) t->nnotes * sizeof h->note[0]);
    h->nnotes = t->nnotes;
    h->unread = t->unread;
    h->simple = t->simple;
    h->simple_known = t->simple_known;
    return ZXM_OK;
}

int hollow_enter(hollow_t *h, const char *space)
{
    hollow_space_t s;
    if (!space || !space[0] || strlen(space) > ZXM_SPACE_MAX) return ZXM_E_ARG;
    zxm_link_t *l = h->link;
    zxm_begin(l, "spaces.get");
    zxm_p_str(l, "space", space);
    int rc = zxm_call(l);
    if (rc) return rc;
    if (read_space(l, l->result, &s) || strcmp(s.id, space)) return ZXM_E_PROTOCOL;
    h->here = s;
    return ZXM_OK;
}

int hollow_tick(hollow_t *h, const char *app)
{
    hollow_front_t f;
    int64_t left;
    if (!app || !app[0] || strlen(app) > HOLLOW_APP_NAME) return ZXM_E_ARG;
    for (const char *c = app; *c; c++)
        if (*c <= ' ' || *c > '~') return ZXM_E_ARG;
    zxm_link_t *l = h->link;
    zxm_begin(l, "screen.tick");
    zxm_p_str(l, "app", app);
    int rc = zxm_call(l);
    if (rc) return rc;
    memset(&f, 0, sizeof f);
    if (zxm_get_str(l, l->result, "app", f.app, sizeof f.app) || strcmp(f.app, app) ||
        zxm_get_bool(l, l->result, "allowed", &f.allowed) ||
        zxm_get_bool(l, l->result, "counted", &f.counted) ||
        u32(l, l->result, "used", 1440, &f.used) || zxm_get_i64(l, l->result, "left", &left) ||
        left < -1 || left > INT32_MAX || zxm_get_str(l, l->result, "why", f.why, sizeof f.why) ||
        (f.allowed && f.why[0]) || (!f.allowed && !f.why[0]))
        return ZXM_E_PROTOCOL;
    f.left = (int32_t) left;
    h->front = f;
    return ZXM_OK;
}

int hollow_mark_read(hollow_t *h, uint32_t *marked)
{
    uint32_t m;
    zxm_link_t *l = h->link;
    zxm_begin(l, "notes.mark_read");
    int rc = zxm_call(l);
    if (rc) return rc;
    if (u32(l, l->result, "marked", UINT32_MAX, &m)) return ZXM_E_PROTOCOL;
    for (uint32_t i = 0; i < h->nnotes; i++) h->note[i].read = true;
    h->unread = 0;
    if (marked) *marked = m;
    return ZXM_OK;
}
