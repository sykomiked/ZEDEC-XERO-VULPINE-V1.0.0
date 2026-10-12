/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_capture.c — screenshots and screen recording (zui_capture.h). */
#include "zui_capture.h"
#include "zui_tokens.h"

static int32_t imin(int32_t a, int32_t b)
{
    return a < b ? a : b;
}

static int32_t imax(int32_t a, int32_t b)
{
    return a > b ? a : b;
}

static bool inside(zui_rect_t r, int32_t x, int32_t y)
{
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

static zui_rect_t meet(zui_rect_t a, zui_rect_t b)
{
    int32_t x0 = imax(a.x, b.x), y0 = imax(a.y, b.y);
    int32_t x1 = imin(a.x + a.w, b.x + b.w), y1 = imin(a.y + a.h, b.y + b.h);
    zui_rect_t r = {x0, y0, imax(0, x1 - x0), imax(0, y1 - y0)};
    return r;
}

static bool overlaps(zui_rect_t a, zui_rect_t b)
{
    zui_rect_t m = meet(a, b);
    return m.w > 0 && m.h > 0;
}

static bool same_rect(zui_rect_t a, zui_rect_t b)
{
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

/* ---- checksums --------------------------------------------------------- */

/* CRC-32 (the reflected 0xEDB88320 polynomial) a nibble at a time: a
 * sixteen-entry constant table, no start-up step. */
static const uint32_t CRC_NIB[16] = {0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu,
                                     0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
                                     0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu,
                                     0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu};

uint32_t zui_crc32(uint32_t crc, const uint8_t *p, uint32_t n)
{
    crc = ~crc;
    for (uint32_t i = 0; i < n; i++) {
        crc ^= p[i];
        crc = CRC_NIB[crc & 15u] ^ (crc >> 4);
        crc = CRC_NIB[crc & 15u] ^ (crc >> 4);
    }
    return ~crc;
}

#define ADLER_MOD  65521u
#define ADLER_NMAX 5552u /* bytes before the sums could overflow 32 bits */

uint32_t zui_adler32(uint32_t adler, const uint8_t *p, uint32_t n)
{
    uint32_t a = adler & 0xFFFFu, b = adler >> 16;
    while (n) {
        uint32_t k = n < ADLER_NMAX ? n : ADLER_NMAX;
        n -= k;
        while (k--) {
            a += *p++;
            b += a;
        }
        a %= ADLER_MOD;
        b %= ADLER_MOD;
    }
    return (b << 16) | a;
}

/* ---- the capture context ------------------------------------------------ */

void zui_capture_init(zui_capture_t *c, zui_wm_t *wm)
{
    uint8_t *z = (uint8_t *) c;
    for (uint32_t i = 0; i < sizeof *c; i++) z[i] = 0;
    c->wm = wm;
    c->mode = ZUI_PNG_FIXED;
}

bool zui_capture_is_private(const zui_capture_t *c, uint32_t win)
{
    for (uint32_t i = 0; win && i < c->npriv; i++)
        if (c->priv[i] == win) return true;
    return false;
}

bool zui_capture_set_private(zui_capture_t *c, uint32_t win, bool on)
{
    if (!win) return false;
    bool had = zui_capture_is_private(c, win);
    if (on) {
        if (had) return true;
        if (c->npriv >= ZUI_WM_MAX) return false;
        c->priv[c->npriv++] = win;
        return true;
    }
    uint32_t j = 0;
    for (uint32_t i = 0; i < c->npriv; i++)
        if (c->priv[i] != win) c->priv[j++] = c->priv[i];
    c->npriv = j;
    return true;
}

int32_t zui_capture_private_rect(zui_capture_t *c, uint32_t win, zui_rect_t r)
{
    for (uint32_t i = 0; i < ZUI_CAP_PRIVATE_RECTS; i++)
        if (!c->rect[i].used) {
            c->rect[i].used = true;
            c->rect[i].win = win;
            c->rect[i].r = r;
            return (int32_t) i;
        }
    return -1;
}

void zui_capture_private_clear(zui_capture_t *c, int32_t handle)
{
    if (handle >= 0 && (uint32_t) handle < ZUI_CAP_PRIVATE_RECTS) c->rect[handle].used = false;
}

static const zui_wm_window_t *win_of(const zui_capture_t *c, uint32_t id)
{
    if (!c->wm || !id) return 0;
    return zui_wm_get(c->wm, id);
}

/* The index in the stacking order of window id, or -1. */
static int32_t z_of(const zui_wm_t *wm, uint32_t id)
{
    for (uint32_t i = 0; i < wm->nz; i++)
        if (wm->win[wm->z[i]].id == id) return (int32_t) i;
    return -1;
}

static void add_region(zui_capture_t *c, zui_rect_t r, int32_t z_above)
{
    if (r.w <= 0 || r.h <= 0 || c->nreg >= ZUI_CAP_REGIONS) return;
    zui_cap_region_t *g = &c->reg[c->nreg++];
    g->r = r;
    g->ncover = 0;
    if (z_above < 0 || !c->wm) return;
    const zui_wm_t *wm = c->wm;
    for (uint32_t i = (uint32_t) z_above + 1u; i < wm->nz; i++) {
        const zui_wm_window_t *w = &wm->win[wm->z[i]];
        if (!w->minimised && g->ncover < ZUI_WM_MAX) g->cover[g->ncover++] = w->r;
    }
}

/* CP2: the private parts of the screen, as regions in screen coordinates
 * with the windows stacked above each (which stay visible). */
static void screen_regions(zui_capture_t *c)
{
    c->nreg = 0;
    if (c->wm) {
        const zui_wm_t *wm = c->wm;
        for (uint32_t i = 0; i < wm->nz; i++) {
            const zui_wm_window_t *w = &wm->win[wm->z[i]];
            if (!w->minimised && zui_capture_is_private(c, w->id)) add_region(c, w->r, (int32_t) i);
        }
    }
    for (uint32_t i = 0; i < ZUI_CAP_PRIVATE_RECTS; i++) {
        const zui_cap_private_t *p = &c->rect[i];
        if (!p->used) continue;
        if (!p->win) {
            add_region(c, p->r, -1);
            continue;
        }
        const zui_wm_window_t *w = win_of(c, p->win);
        if (!w || w->minimised) continue;
        zui_rect_t ct = zui_wm_content(c->wm, w);
        zui_rect_t r = {ct.x + p->r.x, ct.y + p->r.y, p->r.w, p->r.h};
        add_region(c, meet(r, ct), z_of(c->wm, w->id));
    }
}

/* The private parts of one window painted off screen, in its content's
 * coordinates: all of it when the window is private, else its own private
 * rectangles. Nothing covers them there. */
static void window_regions(zui_capture_t *c, const zui_wm_window_t *w, zui_rect_t ct)
{
    c->nreg = 0;
    zui_rect_t all = {0, 0, ct.w, ct.h};
    if (zui_capture_is_private(c, w->id)) {
        add_region(c, all, -1);
        return;
    }
    for (uint32_t i = 0; i < ZUI_CAP_PRIVATE_RECTS; i++) {
        const zui_cap_private_t *p = &c->rect[i];
        if (p->used && p->win == w->id) add_region(c, meet(p->r, all), -1);
    }
}

static bool black(const zui_capture_t *c, const uint8_t *act, uint32_t nact, int32_t x, int32_t y)
{
    for (uint32_t k = 0; k < nact; k++) {
        const zui_cap_region_t *g = &c->reg[act[k]];
        if (!inside(g->r, x, y)) continue;
        bool covered = false;
        for (uint32_t j = 0; j < g->ncover && !covered; j++) covered = inside(g->cover[j], x, y);
        if (!covered) return true;
    }
    return false;
}

/* The regions crossing row y (screen coordinates). */
static uint32_t active(const zui_capture_t *c, int32_t y, uint8_t *act)
{
    uint32_t n = 0;
    for (uint32_t k = 0; k < c->nreg; k++)
        if (y >= c->reg[k].r.y && y < c->reg[k].r.y + c->reg[k].r.h) act[n++] = (uint8_t) k;
    return n;
}

/* One pixel as captured: black outside the surface and where private. */
static uint32_t px_at(const zui_capture_t *c, const zui_surface_t *s, const uint8_t *act,
                      uint32_t nact, int32_t x, int32_t y)
{
    if (x < 0 || y < 0 || x >= s->w || y >= s->h) return 0;
    if (nact && black(c, act, nact, x, y)) return 0;
    return s->px[(int64_t) y * s->stride + x] & 0xFFFFFFu;
}

/* A PNG row: the filter byte (0) then R G B per pixel. */
static void fetch_row(const zui_capture_t *c, const zui_surface_t *s, zui_rect_t r, int32_t y,
                      uint8_t *out)
{
    uint8_t act[ZUI_CAP_REGIONS];
    int32_t sy = r.y + y;
    uint32_t nact = active(c, sy, act);
    out[0] = 0;
    for (int32_t x = 0; x < r.w; x++) {
        uint32_t p = px_at(c, s, act, nact, r.x + x, sy);
        out[1 + 3 * x] = (uint8_t) (p >> 16);
        out[2 + 3 * x] = (uint8_t) (p >> 8);
        out[3 + 3 * x] = (uint8_t) p;
    }
}

/* ---- the PNG encoder (CP1) ----------------------------------------------- */

static void out(zui_png_t *e, const uint8_t *p, uint32_t n)
{
    if (e->failed || !n) return;
    e->bytes += n;
    if (e->write && !e->write(e->ctx, p, n)) e->failed = true;
}

static void be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t) (v >> 24);
    p[1] = (uint8_t) (v >> 16);
    p[2] = (uint8_t) (v >> 8);
    p[3] = (uint8_t) v;
}

static void chunk(zui_png_t *e, const char *type, const uint8_t *d, uint32_t n)
{
    uint8_t h[8];
    be32(h, n);
    for (int i = 0; i < 4; i++) h[4 + i] = (uint8_t) type[i];
    out(e, h, 8);
    out(e, d, n);
    uint32_t crc = zui_crc32(0, h + 4, 4);
    crc = zui_crc32(crc, d, n);
    uint8_t t[4];
    be32(t, crc);
    out(e, t, 4);
}

static void flush_idat(zui_png_t *e)
{
    if (e->nidat) chunk(e, "IDAT", e->idat, e->nidat);
    e->nidat = 0;
}

static void zbyte(zui_png_t *e, uint8_t b)
{
    e->idat[e->nidat++] = b;
    if (e->nidat == ZUI_CAP_IDAT) flush_idat(e);
}

static void put_bits(zui_png_t *e, uint32_t v, uint32_t n)
{
    e->bits |= v << e->nbits;
    e->nbits += n;
    while (e->nbits >= 8u) {
        zbyte(e, (uint8_t) e->bits);
        e->bits >>= 8;
        e->nbits -= 8u;
    }
}

/* Huffman codes go most significant bit first. */
static void put_code(zui_png_t *e, uint32_t code, uint32_t len)
{
    uint32_t r = 0;
    for (uint32_t i = 0; i < len; i++) r |= ((code >> i) & 1u) << (len - 1u - i);
    put_bits(e, r, len);
}

/* RFC 1951 3.2.6: the fixed literal / length code. */
static void put_sym(zui_png_t *e, uint32_t v)
{
    if (v <= 143u)
        put_code(e, 0x30u + v, 8);
    else if (v <= 255u)
        put_code(e, 0x190u + (v - 144u), 9);
    else if (v <= 279u)
        put_code(e, v - 256u, 7);
    else
        put_code(e, 0xC0u + (v - 280u), 8);
}

static const uint16_t LEN_BASE[29] = {3,  4,  5,  6,   7,   8,   9,   10,  11, 13,
                                      15, 17, 19, 23,  27,  31,  35,  43,  51, 59,
                                      67, 83, 99, 115, 131, 163, 195, 227, 258};
static const uint8_t LEN_EXTRA[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                      2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
static const uint16_t DIST_BASE[30] = {
    1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
    193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
static const uint8_t DIST_EXTRA[30] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                       6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

static void put_match(zui_png_t *e, uint32_t len, uint32_t dist)
{
    uint32_t i = 28;
    while (LEN_BASE[i] > len) i--;
    put_sym(e, 257u + i);
    put_bits(e, len - LEN_BASE[i], LEN_EXTRA[i]);
    uint32_t j = 29;
    while (DIST_BASE[j] > dist) j--;
    put_code(e, j, 5);
    put_bits(e, dist - DIST_BASE[j], DIST_EXTRA[j]);
}

static uint32_t run(const uint8_t *a, const uint8_t *b, uint32_t lim)
{
    uint32_t l = 0;
    while (l < lim && a[l] == b[l]) l++;
    return l;
}

/* One row as fixed-Huffman symbols: a match repeats the pixel to the left
 * (distance 3) or the one above (distance one row); matches stay in the row. */
static void deflate_row(zui_png_t *e, const uint8_t *cur, const uint8_t *prev, uint32_t rowlen)
{
    uint32_t i = 0;
    while (i < rowlen) {
        uint32_t lim = rowlen - i < 258u ? rowlen - i : 258u, best = 0, dist = 0;
        if (i >= 3u) {
            best = run(cur + i, cur + i - 3, lim);
            dist = 3;
        }
        if (prev && rowlen <= 32768u) {
            uint32_t l = run(cur + i, prev + i, lim);
            if (l > best) {
                best = l;
                dist = rowlen;
            }
        }
        if (best >= 3u) {
            put_match(e, best, dist);
            i += best;
        } else {
            put_sym(e, cur[i]);
            i++;
        }
    }
}

static int encode(zui_capture_t *c, const zui_surface_t *s, zui_rect_t r, zui_cap_write_fn write,
                  void *ctx)
{
    if (r.w < 1 || r.h < 1) return ZUI_CAP_E_ARG;
    if (r.w > ZUI_CAP_MAX_W) return ZUI_CAP_E_SIZE;
    zui_png_t *e = &c->png;
    e->write = write;
    e->ctx = ctx;
    e->bytes = 0;
    e->failed = false;
    e->nidat = 0;
    e->bits = e->nbits = 0;
    static const uint8_t SIG[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    out(e, SIG, 8);
    uint8_t ihdr[13];
    be32(ihdr, (uint32_t) r.w);
    be32(ihdr + 4, (uint32_t) r.h);
    ihdr[8] = 8;  /* bits per sample */
    ihdr[9] = 2;  /* RGB */
    ihdr[10] = 0; /* deflate */
    ihdr[11] = 0; /* filter method 0 */
    ihdr[12] = 0; /* no interlace */
    chunk(e, "IHDR", ihdr, 13);
    zbyte(e, 0x78); /* zlib: deflate, 32K window; (0x78 << 8 | 0x01) % 31 == 0 */
    zbyte(e, 0x01);
    uint32_t rowlen = 1u + 3u * (uint32_t) r.w, adler = 1;
    bool fixed = c->mode == ZUI_PNG_FIXED;
    if (fixed) {
        put_bits(e, 1, 1); /* the last block */
        put_bits(e, 1, 2); /* fixed Huffman */
    }
    for (int32_t y = 0; y < r.h && !e->failed; y++) {
        uint8_t *cur = e->row[y & 1], *prev = y ? e->row[(y & 1) ^ 1] : 0;
        fetch_row(c, s, r, y, cur);
        adler = zui_adler32(adler, cur, rowlen);
        if (fixed) {
            deflate_row(e, cur, prev, rowlen);
            continue;
        }
        zbyte(e, y == r.h - 1 ? 1 : 0); /* a stored block per row: BFINAL on the last */
        zbyte(e, (uint8_t) rowlen);
        zbyte(e, (uint8_t) (rowlen >> 8));
        zbyte(e, (uint8_t) ~rowlen);
        zbyte(e, (uint8_t) (~rowlen >> 8));
        for (uint32_t i = 0; i < rowlen; i++) zbyte(e, cur[i]);
    }
    if (fixed) {
        put_sym(e, 256); /* end of block */
        if (e->nbits) put_bits(e, 0, 8u - e->nbits);
    }
    zbyte(e, (uint8_t) (adler >> 24));
    zbyte(e, (uint8_t) (adler >> 16));
    zbyte(e, (uint8_t) (adler >> 8));
    zbyte(e, (uint8_t) adler);
    flush_idat(e);
    chunk(e, "IEND", 0, 0);
    return e->failed ? ZUI_CAP_E_WRITE : ZUI_CAP_OK;
}

int zui_png_write(zui_capture_t *c, const zui_surface_t *s, zui_rect_t r, zui_cap_write_fn write,
                  void *ctx)
{
    screen_regions(c);
    return encode(c, s, r, write, ctx);
}

/* ---- screenshots ------------------------------------------------------- */

static void use(zui_capture_t *c, zui_cap_use_t u, bool on)
{
    if (c->in_use) c->in_use(c->in_use_ctx, u, on);
}

static int shot(zui_capture_t *c, const zui_surface_t *s, zui_rect_t r, zui_cap_write_fn write,
                void *ctx)
{
    if (r.w < 1 || r.h < 1) return ZUI_CAP_E_ARG;
    if (r.w > ZUI_CAP_MAX_W) return ZUI_CAP_E_SIZE;
    use(c, ZUI_CAP_SCREENSHOT, true); /* CP3 */
    int rc = encode(c, s, r, write, ctx);
    use(c, ZUI_CAP_SCREENSHOT, false);
    if (rc == ZUI_CAP_OK) c->shots++;
    return rc;
}

int zui_capture_surface(zui_capture_t *c, const zui_surface_t *s, zui_cap_write_fn write, void *ctx)
{
    screen_regions(c);
    return shot(c, s, (zui_rect_t){0, 0, s->w, s->h}, write, ctx);
}

int zui_capture_rect(zui_capture_t *c, const zui_surface_t *s, zui_rect_t r, zui_cap_write_fn write,
                     void *ctx)
{
    screen_regions(c);
    return shot(c, s, r, write, ctx);
}

zui_rect_t zui_capture_window_rect(const zui_capture_t *c, uint32_t win, bool content_only)
{
    zui_rect_t none = {0, 0, 0, 0};
    const zui_wm_window_t *w = win_of(c, win);
    if (!w || w->minimised) return none;
    return content_only ? zui_wm_content(c->wm, w) : w->r;
}

int zui_capture_window(zui_capture_t *c, const zui_surface_t *s, uint32_t win, bool content_only,
                       zui_surface_t *scratch, uint32_t tick, zui_cap_write_fn write, void *ctx)
{
    const zui_wm_window_t *w = win_of(c, win);
    if (!w) return ZUI_CAP_E_GONE;
    zui_rect_t ct = zui_wm_content(c->wm, w);
    if (content_only && scratch && w->client.paint && w->phase == ZUI_WIN_OPEN) {
        if (ct.w < 1 || ct.h < 1 || scratch->w < ct.w || scratch->h < ct.h) return ZUI_CAP_E_ARG;
        zui_rect_t at = {0, 0, ct.w, ct.h};
        zui_unclip(scratch);
        zui_fill(scratch, at, ZUI_VOID);
        zui_clip(scratch, at);
        w->client.paint(w->client.ctx, scratch, at, tick);
        zui_unclip(scratch);
        window_regions(c, w, ct);
        return shot(c, scratch, at, write, ctx);
    }
    if (w->minimised) return ZUI_CAP_E_GONE;
    screen_regions(c);
    return shot(c, s, content_only ? ct : w->r, write, ctx);
}

/* ---- recording ----------------------------------------------------------- */

static void le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t) v;
    p[1] = (uint8_t) (v >> 8);
    p[2] = (uint8_t) (v >> 16);
    p[3] = (uint8_t) (v >> 24);
}

static bool rec_out(zui_rec_t *r, const uint8_t *p, uint32_t n)
{
    if (r->err) return false;
    if (r->cfg.write && !r->cfg.write(r->cfg.ctx, p, n)) {
        r->err = ZUI_CAP_E_WRITE;
        return false;
    }
    return true;
}

static bool record(zui_rec_t *r, uint8_t kind, uint32_t index, uint32_t t_ms, uint32_t len)
{
    uint8_t h[ZUI_REC_RECORD] = {kind, 0, 0, 0};
    le32(h + 4, index);
    le32(h + 8, t_ms);
    le32(h + 12, len);
    return rec_out(r, h, sizeof h);
}

/* Where the frame comes from now (CP4): a window target keeps the size it
 * started with and follows the window; a window that went away gives a
 * rectangle off the surface (black). */
static zui_rect_t target(const zui_rec_t *r, const zui_surface_t *s)
{
    switch (r->cfg.target) {
    case ZUI_REC_SURFACE:
        return (zui_rect_t){0, 0, s->w, s->h};
    case ZUI_REC_RECT:
        return r->cfg.rect;
    default: {
        zui_rect_t w = zui_capture_window_rect(r->cap, r->cfg.win, r->cfg.content_only);
        if (w.w <= 0) return (zui_rect_t){-r->w, -r->h, r->w, r->h};
        return (zui_rect_t){w.x, w.y, r->w, r->h};
    }
    }
}

static void use_rec(zui_rec_t *r, bool on)
{
    r->cap->recording = on;
    use(r->cap, ZUI_CAP_RECORDING, on);
}

int zui_rec_start(zui_rec_t *r, zui_capture_t *c, const zui_surface_t *s,
                  const zui_rec_config_t *cfg, uint32_t now_ms)
{
    uint8_t *z = (uint8_t *) r;
    for (uint32_t i = 0; i < sizeof *r; i++) z[i] = 0;
    if (!c || !s || !cfg) return ZUI_CAP_E_ARG;
    if (c->recording) return ZUI_CAP_E_STATE;
    if (cfg->fps < 1u || cfg->fps > ZUI_REC_FPS_MAX) return ZUI_CAP_E_ARG;
    r->cap = c;
    r->cfg = *cfg;
    zui_rect_t t;
    if (cfg->target == ZUI_REC_WINDOW) {
        t = zui_capture_window_rect(c, cfg->win, cfg->content_only);
        if (t.w <= 0) return ZUI_CAP_E_GONE;
    } else {
        t = target(r, s);
    }
    if (t.w < 1 || t.h < 1) return ZUI_CAP_E_ARG;
    if (t.w > ZUI_CAP_MAX_W) return ZUI_CAP_E_SIZE;
    r->w = t.w;
    r->h = t.h;
    uint64_t px = (uint64_t) t.w * (uint64_t) t.h;
    if (cfg->encoding == ZUI_REC_DELTA && (!cfg->prev || cfg->prev_cap < px)) return ZUI_CAP_E_ARG;
    if (cfg->prev && cfg->prev_cap < px) r->cfg.prev = 0; /* too small: use the CRC */
    uint8_t h[ZUI_REC_HEADER] = {'Z', 'X', 'V', 'R', 1, 0, ZUI_REC_HEADER, 0};
    le32(h + 8, (uint32_t) t.w);
    le32(h + 12, (uint32_t) t.h);
    le32(h + 16, cfg->fps);
    le32(h + 20, (uint32_t) cfg->encoding);
    le32(h + 24, cfg->key_every);
    le32(h + 28, 0);
    if (!rec_out(r, h, sizeof h)) return r->err;
    r->start_ms = r->last_ms = now_ms;
    r->on = true;
    use_rec(r, true); /* CP3 */
    return ZUI_CAP_OK;
}

static uint32_t frame_crc(zui_rec_t *r, const zui_surface_t *s, zui_rect_t t)
{
    uint8_t act[ZUI_CAP_REGIONS];
    uint32_t crc = 0;
    for (int32_t y = 0; y < t.h; y++) {
        uint32_t nact = active(r->cap, t.y + y, act);
        for (int32_t x = 0; x < t.w; x++) {
            uint8_t b[4];
            le32(b, px_at(r->cap, s, act, nact, t.x + x, t.y + y));
            crc = zui_crc32(crc, b, 4);
        }
    }
    return crc;
}

static bool same_as_prev(zui_rec_t *r, const zui_surface_t *s, zui_rect_t t)
{
    uint8_t act[ZUI_CAP_REGIONS];
    for (int32_t y = 0; y < t.h; y++) {
        uint32_t nact = active(r->cap, t.y + y, act);
        const uint32_t *p = r->cfg.prev + (int64_t) y * t.w;
        for (int32_t x = 0; x < t.w; x++)
            if (px_at(r->cap, s, act, nact, t.x + x, t.y + y) != p[x]) return false;
    }
    return true;
}

static void keep_prev(zui_rec_t *r, const zui_surface_t *s, zui_rect_t t)
{
    uint8_t act[ZUI_CAP_REGIONS];
    for (int32_t y = 0; y < t.h; y++) {
        uint32_t nact = active(r->cap, t.y + y, act);
        uint32_t *p = r->cfg.prev + (int64_t) y * t.w;
        for (int32_t x = 0; x < t.w; x++) p[x] = px_at(r->cap, s, act, nact, t.x + x, t.y + y);
    }
}

/* Pixel k of the frame (row-major), keeping the private regions of the row
 * it is on. */
typedef struct {
    int32_t y;
    uint32_t nact;
    uint8_t act[ZUI_CAP_REGIONS];
} rowc_t;

static uint32_t pix(const zui_rec_t *r, const zui_surface_t *s, zui_rect_t t, uint64_t k,
                    rowc_t *rc)
{
    int32_t y = t.y + (int32_t) (k / (uint64_t) t.w), x = t.x + (int32_t) (k % (uint64_t) t.w);
    if (y != rc->y) {
        rc->y = y;
        rc->nact = active(r->cap, y, rc->act);
    }
    return px_at(r->cap, s, rc->act, rc->nact, x, y);
}

/* CP5: the delta against the previous frame; with emit false it only
 * measures. Returns the payload length. */
static uint32_t delta(zui_rec_t *r, const zui_surface_t *s, zui_rect_t t, bool emit)
{
    uint64_t n = (uint64_t) t.w * (uint64_t) t.h, i = 0;
    uint32_t len = 0;
    const uint32_t *prev = r->cfg.prev;
    rowc_t rc;
    rc.y = INT32_MIN;
    rc.nact = 0;
    while (i < n) {
        uint64_t skip = 0, cnt = 0;
        while (i + skip < n && pix(r, s, t, i + skip, &rc) == prev[i + skip]) skip++;
        while (i + skip + cnt < n && pix(r, s, t, i + skip + cnt, &rc) != prev[i + skip + cnt])
            cnt++;
        len += 8u + 3u * (uint32_t) cnt;
        if (emit) {
            uint8_t h[8];
            le32(h, (uint32_t) skip);
            le32(h + 4, (uint32_t) cnt);
            if (!rec_out(r, h, 8)) return len;
            uint8_t buf[255];
            uint32_t nb = 0;
            for (uint64_t k = i + skip; k < i + skip + cnt; k++) {
                uint32_t p = pix(r, s, t, k, &rc);
                buf[nb++] = (uint8_t) (p >> 16);
                buf[nb++] = (uint8_t) (p >> 8);
                buf[nb++] = (uint8_t) p;
                if (nb == sizeof buf) {
                    if (!rec_out(r, buf, nb)) return len;
                    nb = 0;
                }
            }
            if (nb && !rec_out(r, buf, nb)) return len;
        }
        i += skip + cnt;
    }
    return len;
}

/* The recording's own writer, so a PNG frame goes through rec_out. */
static bool png_to_rec(void *ctx, const uint8_t *p, uint32_t n)
{
    return rec_out((zui_rec_t *) ctx, p, n);
}

static int fail(zui_rec_t *r, int rc)
{
    if (!r->err) r->err = rc;
    r->on = false;
    use_rec(r, false); /* the indicator never outlives the recording */
    return rc;
}

int zui_rec_present(zui_rec_t *r, const zui_surface_t *s, const zui_damage_t *damage,
                    uint32_t now_ms)
{
    if (!r->on) return ZUI_CAP_E_STATE;
    uint32_t el = now_ms - r->start_ms;
    uint32_t slot = (uint32_t) ((uint64_t) el * r->cfg.fps / 1000u);
    if (slot < r->next_slot) return 0; /* CP4: not due */
    r->next_slot = slot + 1u;
    zui_rect_t t = target(r, s);
    screen_regions(r->cap);
    if (damage && r->have_last) { /* nothing drawn where we look: unchanged */
        bool hit = false;
        for (uint32_t i = 0; i < damage->n && i < ZUI_DAMAGE_MAX && !hit; i++)
            hit = overlaps(damage->r[i], t);
        if (!hit && same_rect(t, r->last_rect)) {
            r->skipped++;
            return 0;
        }
    }
    uint32_t crc = 0;
    if (r->cfg.prev) {
        if (r->have_last && same_as_prev(r, s, t)) {
            r->skipped++;
            return 0;
        }
    } else {
        crc = frame_crc(r, s, t);
        if (r->have_last && crc == r->last_crc) {
            r->skipped++;
            return 0;
        }
    }
    bool key = r->cfg.encoding == ZUI_REC_PNG || !r->have_last ||
               (r->cfg.key_every && r->frames % r->cfg.key_every == 0);
    uint32_t len;
    if (key) {
        int rc = encode(r->cap, s, t, 0, 0); /* measure */
        if (rc) return fail(r, rc);
        len = (uint32_t) r->cap->png.bytes;
    } else {
        len = delta(r, s, t, false);
    }
    if (!record(r, key ? ZUI_REC_KIND_KEY : ZUI_REC_KIND_DELTA, r->frames, el, len))
        return fail(r, r->err);
    if (key) {
        int rc = encode(r->cap, s, t, png_to_rec, r);
        if (rc) return fail(r, r->err ? r->err : rc);
    } else {
        delta(r, s, t, true);
        if (r->err) return fail(r, r->err);
    }
    if (r->cfg.prev) keep_prev(r, s, t);
    r->last_crc = crc;
    r->last_rect = t;
    r->have_last = true;
    r->frames++;
    r->last_ms = now_ms;
    return 1;
}

int zui_rec_stop(zui_rec_t *r, uint32_t now_ms)
{
    if (!r->on) return r->err ? r->err : ZUI_CAP_E_STATE;
    r->on = false;
    record(r, ZUI_REC_KIND_END, r->frames, now_ms - r->start_ms, 0);
    use_rec(r, false); /* CP3 */
    return r->err;
}
