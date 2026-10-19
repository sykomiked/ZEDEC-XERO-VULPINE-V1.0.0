/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_canvas.c — surface, shapes and text. See zui_canvas.h. */
#include "zui_canvas.h"

static int32_t imin(int32_t a, int32_t b)
{
    return a < b ? a : b;
}
static int32_t imax(int32_t a, int32_t b)
{
    return a > b ? a : b;
}

void zui_surface_init(zui_surface_t *s, uint32_t *px, int32_t w, int32_t h, int32_t stride)
{
    s->px = px;
    s->w = w;
    s->h = h;
    s->stride = stride;
    zui_unclip(s);
}

void zui_unclip(zui_surface_t *s)
{
    s->clip.x = 0;
    s->clip.y = 0;
    s->clip.w = s->w;
    s->clip.h = s->h;
}

static zui_rect_t isect(zui_rect_t a, zui_rect_t b)
{
    int32_t x0 = imax(a.x, b.x), y0 = imax(a.y, b.y);
    int32_t x1 = imin(a.x + a.w, b.x + b.w), y1 = imin(a.y + a.h, b.y + b.h);
    zui_rect_t o = {x0, y0, imax(0, x1 - x0), imax(0, y1 - y0)};
    return o;
}

void zui_clip(zui_surface_t *s, zui_rect_t r)
{
    zui_rect_t full = {0, 0, s->w, s->h};
    s->clip = isect(full, r);
}

uint32_t zui_mix(uint32_t a, uint32_t b, uint32_t t)
{
    if (t > 255u) t = 255u;
    uint32_t u = 255u - t;
    uint32_t r = (((a >> 16) & 0xFF) * u + ((b >> 16) & 0xFF) * t + 127u) / 255u;
    uint32_t g = (((a >> 8) & 0xFF) * u + ((b >> 8) & 0xFF) * t + 127u) / 255u;
    uint32_t c = ((a & 0xFF) * u + (b & 0xFF) * t + 127u) / 255u;
    return (r << 16) | (g << 8) | c;
}

void zui_blend_px(zui_surface_t *s, int32_t x, int32_t y, uint32_t rgb, uint32_t a)
{
    if (a == 0) return;
    if (x < s->clip.x || y < s->clip.y || x >= s->clip.x + s->clip.w || y >= s->clip.y + s->clip.h)
        return;
    uint32_t *p = &s->px[(int64_t) y * s->stride + x];
    *p = a >= 255u ? (rgb & 0xFFFFFFu) : zui_mix(*p, rgb, a);
}

void zui_fill_a(zui_surface_t *s, zui_rect_t r, uint32_t rgb, uint32_t a)
{
    zui_rect_t c = isect(r, s->clip);
    for (int32_t y = c.y; y < c.y + c.h; y++) {
        uint32_t *row = &s->px[(int64_t) y * s->stride];
        for (int32_t x = c.x; x < c.x + c.w; x++)
            row[x] = a >= 255u ? (rgb & 0xFFFFFFu) : zui_mix(row[x], rgb, a);
    }
}

void zui_fill(zui_surface_t *s, zui_rect_t r, uint32_t rgb)
{
    zui_fill_a(s, r, rgb, 255u);
}

/* Coverage of pixel (px,py) by a rounded rect, 0..16 (4x4 samples). Samples
 * live at 1/8-pixel positions so every test is exact integer arithmetic. */
static uint32_t rr_cov(int32_t px, int32_t py, zui_rect_t r, int32_t rad)
{
    if (px < r.x || py < r.y || px >= r.x + r.w || py >= r.y + r.h) return 0;
    if (rad <= 0) return 16;
    int32_t cx, cy;
    if (px < r.x + rad)
        cx = r.x + rad;
    else if (px >= r.x + r.w - rad)
        cx = r.x + r.w - rad;
    else
        return 16;
    if (py < r.y + rad)
        cy = r.y + rad;
    else if (py >= r.y + r.h - rad)
        cy = r.y + r.h - rad;
    else
        return 16;
    int64_t R = (int64_t) rad * 8, R2 = R * R;
    uint32_t n = 0;
    for (int32_t j = 0; j < 4; j++) {
        int64_t dy = (int64_t) py * 8 + 2 * j + 1 - (int64_t) cy * 8;
        for (int32_t i = 0; i < 4; i++) {
            int64_t dx = (int64_t) px * 8 + 2 * i + 1 - (int64_t) cx * 8;
            if (dx * dx + dy * dy <= R2) n++;
        }
    }
    return n;
}

static int32_t clamp_rad(zui_rect_t r, int32_t rad)
{
    if (rad < 0) rad = 0;
    if (rad * 2 > r.w) rad = r.w / 2;
    if (rad * 2 > r.h) rad = r.h / 2;
    return rad;
}

void zui_fill_rrect(zui_surface_t *s, zui_rect_t r, int32_t rad, uint32_t rgb, uint32_t a)
{
    rad = clamp_rad(r, rad);
    zui_rect_t c = isect(r, s->clip);
    for (int32_t y = c.y; y < c.y + c.h; y++)
        for (int32_t x = c.x; x < c.x + c.w; x++) {
            uint32_t k = rr_cov(x, y, r, rad);
            if (k) zui_blend_px(s, x, y, rgb, (a * k) / 16u);
        }
}

void zui_stroke_rrect(zui_surface_t *s, zui_rect_t r, int32_t rad, int32_t wd, uint32_t rgb,
                      uint32_t a)
{
    if (wd <= 0) return;
    rad = clamp_rad(r, rad);
    zui_rect_t in = {r.x + wd, r.y + wd, r.w - 2 * wd, r.h - 2 * wd};
    int32_t irad = imax(0, rad - wd);
    zui_rect_t c = isect(r, s->clip);
    for (int32_t y = c.y; y < c.y + c.h; y++)
        for (int32_t x = c.x; x < c.x + c.w; x++) {
            int32_t k = (int32_t) rr_cov(x, y, r, rad);
            if (in.w > 0 && in.h > 0) k -= (int32_t) rr_cov(x, y, in, irad);
            if (k > 0) zui_blend_px(s, x, y, rgb, (a * (uint32_t) k) / 16u);
        }
}

void zui_dash_rrect(zui_surface_t *s, zui_rect_t r, int32_t rad, int32_t wd, int32_t dash,
                    uint32_t rgb)
{
    rad = clamp_rad(r, rad);
    if (dash <= 0) dash = 1;
    zui_rect_t keep = s->clip;
    /* corners solid, so the outline reads as one shape */
    zui_rect_t corners[4] = {{r.x, r.y, rad + wd, rad + wd},
                             {r.x + r.w - rad - wd, r.y, rad + wd, rad + wd},
                             {r.x, r.y + r.h - rad - wd, rad + wd, rad + wd},
                             {r.x + r.w - rad - wd, r.y + r.h - rad - wd, rad + wd, rad + wd}};
    for (int i = 0; i < 4; i++) {
        s->clip = isect(keep, corners[i]);
        zui_stroke_rrect(s, r, rad, wd, rgb, 255u);
    }
    s->clip = keep;
    for (int32_t x = r.x + rad + wd; x < r.x + r.w - rad - wd; x += 2 * dash) {
        int32_t len = imin(dash, r.x + r.w - rad - wd - x);
        zui_hline(s, x, r.y, len, wd, rgb);
        zui_hline(s, x, r.y + r.h - wd, len, wd, rgb);
    }
    for (int32_t y = r.y + rad + wd; y < r.y + r.h - rad - wd; y += 2 * dash) {
        int32_t len = imin(dash, r.y + r.h - rad - wd - y);
        zui_vline(s, r.x, y, len, wd, rgb);
        zui_vline(s, r.x + r.w - wd, y, len, wd, rgb);
    }
}

void zui_engrave(zui_surface_t *s, zui_rect_t r, int32_t rad, uint32_t lo, uint32_t hi,
                 uint32_t a_hi)
{
    zui_stroke_rrect(s, r, rad, 1, lo, 255u);
    zui_rect_t lip = {r.x + 1, r.y + 1, r.w, r.h};
    zui_rect_t keep = s->clip;
    /* the lip shows only on the lower and right sides */
    zui_rect_t full = {0, 0, s->w, s->h};
    zui_rect_t c1 =
        isect(isect(keep, full), (zui_rect_t){r.x, r.y + r.h - rad - 2, r.w + 2, rad + 3});
    s->clip = c1;
    zui_stroke_rrect(s, lip, rad, 1, hi, a_hi);
    s->clip = isect(isect(keep, full), (zui_rect_t){r.x + r.w - rad - 2, r.y, rad + 3, r.h + 2});
    zui_stroke_rrect(s, lip, rad, 1, hi, a_hi);
    s->clip = keep;
}

void zui_hline(zui_surface_t *s, int32_t x, int32_t y, int32_t w, int32_t t, uint32_t rgb)
{
    zui_rect_t r = {x, y, w, t};
    zui_fill(s, r, rgb);
}

void zui_vline(zui_surface_t *s, int32_t x, int32_t y, int32_t h, int32_t t, uint32_t rgb)
{
    zui_rect_t r = {x, y, t, h};
    zui_fill(s, r, rgb);
}

/* ---- text -------------------------------------------------------------- */

static void cache_reset(zui_glyph_cache_t *c)
{
    for (uint32_t i = 0; i < ZUI_GLYPH_SLOTS; i++) c->slots[i].used = false;
    c->used = 0;
    c->misses = 0;
    c->hits = 0;
}

void zui_glyph_cache_init(zui_glyph_cache_t *c)
{
    cache_reset(c);
    c->n_fallback = 0;
    c->sym = 0;
    c->sym_ctx = 0;
}

void zui_text_fallback(zui_glyph_cache_t *c, const ttf_font_t *const *fonts, uint32_t n)
{
    if (!c) return;
    c->n_fallback = 0;
    for (uint32_t i = 0; fonts && i < n && c->n_fallback < ZUI_FALLBACK_MAX; i++)
        if (fonts[i]) c->fallback[c->n_fallback++] = fonts[i];
}

void zui_text_painter(zui_glyph_cache_t *c, zui_sym_fn fn, void *ctx)
{
    if (!c) return;
    c->sym = fn;
    c->sym_ctx = ctx;
}

static uint64_t isqrt64(uint64_t v)
{
    uint64_t r = 0, b = (uint64_t) 1 << 62;
    while (b > v) b >>= 2;
    while (b) {
        if (v >= r + b) {
            v -= r + b;
            r = (r >> 1) + b;
        } else
            r >>= 1;
        b >>= 2;
    }
    return r;
}

void zui_line_q8(zui_surface_t *s, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t width,
                 uint32_t rgb, uint32_t a)
{
    if (!s || width <= 0) return;
    int32_t hw = width / 2;
    int32_t bx0 = (imin(x0, x1) - hw - 256) >> 8, bx1 = (imax(x0, x1) + hw + 256) >> 8;
    int32_t by0 = (imin(y0, y1) - hw - 256) >> 8, by1 = (imax(y0, y1) + hw + 256) >> 8;
    zui_rect_t c = isect((zui_rect_t){bx0, by0, bx1 - bx0 + 1, by1 - by0 + 1}, s->clip);
    int64_t dx = x1 - x0, dy = y1 - y0, len2 = dx * dx + dy * dy;
    for (int32_t y = c.y; y < c.y + c.h; y++)
        for (int32_t x = c.x; x < c.x + c.w; x++) {
            int64_t px = (int64_t) x * 256 + 128 - x0, py = (int64_t) y * 256 + 128 - y0;
            int64_t t = len2 ? (px * dx + py * dy) : 0; /* projection, scaled by len2 */
            int64_t qx, qy;
            if (t <= 0 || !len2) {
                qx = px;
                qy = py;
            } else if (t >= len2) {
                qx = px - dx;
                qy = py - dy;
            } else {
                qx = px - dx * t / len2;
                qy = py - dy * t / len2;
            }
            int64_t d = (int64_t) isqrt64((uint64_t) (qx * qx + qy * qy));
            int64_t cov = (int64_t) hw + 128 - d; /* one pixel of soft edge */
            if (cov <= 0) continue;
            if (cov > 256) cov = 256;
            zui_blend_px(s, x, y, rgb, (uint32_t) ((int64_t) a * cov / 256));
        }
}

/* zui's drawn symbols: strokes on a 1000-unit em, y up from the baseline. */
typedef struct {
    int16_t x0, y0, x1, y1;
} sym_seg_t;

static const sym_seg_t SYM_SHIFT[] = {
    {450, 720, 820, 330}, {820, 330, 620, 330}, {620, 330, 620, 40}, {620, 40, 280, 40},
    {280, 40, 280, 330},  {280, 330, 80, 330},  {80, 330, 450, 720}};
static const sym_seg_t SYM_CTRL[] = {{180, 380, 450, 680}, {450, 680, 720, 380}};
static const sym_seg_t SYM_OPT[] = {
    {80, 600, 330, 600}, {330, 600, 600, 100}, {600, 100, 820, 100}, {560, 600, 820, 600}};
static const sym_seg_t SYM_CMD[] = {
    {300, 500, 600, 500}, {600, 500, 600, 200}, {600, 200, 300, 200}, {300, 200, 300, 500}};
static const sym_seg_t SYM_RET[] = {
    {780, 640, 780, 300}, {780, 300, 150, 300}, {330, 470, 150, 300}, {150, 300, 330, 130}};
static const sym_seg_t SYM_ERASE[] = {
    {300, 620, 840, 620}, {840, 620, 840, 80},  {840, 80, 300, 80},  {300, 80, 60, 350},
    {60, 350, 300, 620},  {470, 480, 690, 220}, {470, 220, 690, 480}};
static const sym_seg_t SYM_ESC[] = {
    {150, 660, 420, 390}, {150, 660, 330, 660}, {150, 660, 150, 480}};
static const sym_seg_t SYM_TAB[] = {
    {80, 350, 700, 350}, {520, 530, 700, 350}, {700, 350, 520, 170}, {800, 560, 800, 140}};
static const sym_seg_t SYM_CHECK[] = {{120, 360, 360, 110}, {360, 110, 820, 640}};
static const sym_seg_t SYM_CROSS[] = {{170, 620, 730, 80}, {170, 80, 730, 620}};

typedef struct {
    uint32_t cp;
    const sym_seg_t *seg;
    uint8_t n;
    uint8_t rings; /* 1: command loops at the square's corners; 2: escape's ring */
} sym_def_t;

#define SYM(cp, a, r) {cp, a, (uint8_t) (sizeof a / sizeof a[0]), r}
static const sym_def_t SYMS[] = {
    SYM(0x2318u, SYM_CMD, 1),   SYM(0x2325u, SYM_OPT, 0),  SYM(0x21E7u, SYM_SHIFT, 0),
    SYM(0x2303u, SYM_CTRL, 0),  SYM(0x23CEu, SYM_RET, 0),  SYM(0x21B5u, SYM_RET, 0),
    SYM(0x232Bu, SYM_ERASE, 0), SYM(0x238Bu, SYM_ESC, 2),  SYM(0x21E5u, SYM_TAB, 0),
    SYM(0x2713u, SYM_CHECK, 0), SYM(0x2715u, SYM_CROSS, 0)};
#undef SYM

static const sym_def_t *sym_find(uint32_t cp)
{
    for (uint32_t i = 0; i < sizeof SYMS / sizeof SYMS[0]; i++)
        if (SYMS[i].cp == cp) return &SYMS[i];
    return 0;
}

bool zui_symbol_has(uint32_t cp)
{
    return sym_find(cp) != 0;
}

static void sym_ring(zui_surface_t *s, int32_t ox, int32_t oy, int32_t k, int32_t cx, int32_t cy,
                     int32_t r, int32_t wd, uint32_t rgb, uint32_t a)
{
    /* 16 chords; cos/sin of k*22.5 deg in Q8 */
    static const int16_t C[17] = {256,  237,  181, 98, 0,  -98, -181, -237, -256,
                                  -237, -181, -98, 0,  98, 181, 237,  256};
    for (int32_t i = 0; i < 16; i++) {
        int32_t ax = cx + r * C[i] / 256, ay = cy + r * C[(i + 12) % 16] / 256;
        int32_t bx = cx + r * C[i + 1] / 256, by = cy + r * C[(i + 13) % 16] / 256;
        zui_line_q8(s, ox + ax * k / 1000, oy - ay * k / 1000, ox + bx * k / 1000,
                    oy - by * k / 1000, wd, rgb, a);
    }
}

int32_t zui_symbol(zui_surface_t *s, uint32_t cp, int32_t x, int32_t base, uint32_t size_px,
                   uint32_t rgb, uint32_t a)
{
    const sym_def_t *d = sym_find(cp);
    if (!d) return -1;
    int32_t adv = (int32_t) (size_px * 900u + 999u) / 1000;
    if (!s) return adv;
    int32_t k = (int32_t) size_px * 256; /* em units -> 1/256 px: u * k / 1000 */
    int32_t ox = x * 256, oy = base * 256;
    int32_t wd = (int32_t) size_px * 256 * 80 / 1000; /* strokes 8% of the em */
    if (wd < 256) wd = 256;
    for (uint32_t i = 0; i < d->n; i++) {
        const sym_seg_t *g = &d->seg[i];
        zui_line_q8(s, ox + g->x0 * k / 1000, oy - g->y0 * k / 1000, ox + g->x1 * k / 1000,
                    oy - g->y1 * k / 1000, wd, rgb, a);
    }
    if (d->rings == 1) { /* the command loops */
        static const int16_t L[4][2] = {{300, 500}, {600, 500}, {300, 200}, {600, 200}};
        for (int i = 0; i < 4; i++)
            sym_ring(s, ox, oy, k, L[i][0] + (i & 1 ? 95 : -95), L[i][1] + (i < 2 ? 95 : -95), 105,
                     wd, rgb, a);
    } else if (d->rings == 2) {
        sym_ring(s, ox, oy, k, 520, 320, 280, wd, rgb, a);
    }
    return adv;
}

/* Decode one UTF-8 scalar; malformed bytes become U+FFFD and advance by 1. */
static uint32_t utf8_next(const char **pp)
{
    const uint8_t *p = (const uint8_t *) *pp;
    uint32_t c = p[0], n = 0, cp;
    if (c < 0x80u) {
        *pp += 1;
        return c;
    }
    if ((c & 0xE0u) == 0xC0u) {
        cp = c & 0x1Fu;
        n = 1;
    } else if ((c & 0xF0u) == 0xE0u) {
        cp = c & 0x0Fu;
        n = 2;
    } else if ((c & 0xF8u) == 0xF0u) {
        cp = c & 0x07u;
        n = 3;
    } else {
        *pp += 1;
        return 0xFFFDu;
    }
    for (uint32_t i = 1; i <= n; i++) {
        if ((p[i] & 0xC0u) != 0x80u) {
            *pp += 1;
            return 0xFFFDu;
        }
        cp = (cp << 6) | (p[i] & 0x3Fu);
    }
    *pp += 1 + n;
    return cp;
}

static int32_t font_px_q8(const ttf_font_t *f, uint32_t size_px, int32_t units)
{
    uint32_t E = f->units_per_em ? f->units_per_em : 1000u;
    return (int32_t) (((int64_t) units * (int64_t) size_px * 256) / (int64_t) E);
}

static int32_t to_px_q8(const zui_text_t *t, int32_t units)
{
    return font_px_q8(t->font, t->size_px, units);
}

/* Where one character comes from. */
typedef struct {
    const ttf_font_t *font; /* NULL: a drawn symbol or the painter */
    uint32_t gid;
    int8_t kind; /* 0 font glyph, 1 zui symbol, 2 painter */
} zui_src_t;

static zui_src_t resolve(const zui_text_t *t, uint32_t cp)
{
    zui_src_t r = {t->font, ttf_glyph_index(t->font, cp), 0};
    if (r.gid || cp < 0x20u) return r;
    const zui_glyph_cache_t *c = t->cache;
    if (c) {
        for (uint32_t i = 0; i < c->n_fallback && i < ZUI_FALLBACK_MAX; i++) {
            uint32_t g = ttf_glyph_index(c->fallback[i], cp);
            if (g) return (zui_src_t){c->fallback[i], g, 0};
        }
    }
    if (zui_symbol_has(cp)) return (zui_src_t){0, 0, 1};
    if (c && c->sym && c->sym(0, cp, 0, 0, t->size_px, 0, 0, c->sym_ctx) >= 0)
        return (zui_src_t){0, 0, 2};
    return r; /* the font's own empty box */
}

static int32_t adv_q8(const zui_text_t *t, uint32_t cp, zui_src_t src)
{
    if (src.kind == 1) return zui_symbol(0, cp, 0, 0, t->size_px, 0, 0) * 256;
    if (src.kind == 2) return t->cache->sym(0, cp, 0, 0, t->size_px, 0, 0, t->cache->sym_ctx) * 256;
    uint32_t gid = src.gid;
    if (t->tabular && cp >= '0' && cp <= '9' && src.font == t->font)
        gid = ttf_glyph_index(t->font, '0');
    return font_px_q8(src.font, t->size_px, (int32_t) ttf_advance(src.font, gid));
}

int32_t zui_text_ascent(const zui_text_t *t)
{
    return (to_px_q8(t, t->font->ascender) + 255) >> 8;
}
int32_t zui_text_descent(const zui_text_t *t)
{
    return to_px_q8(t, t->font->descender) >> 8;
}

int32_t zui_text_width_q8(const zui_text_t *t, const char *s)
{
    int32_t w = 0;
    if (!t || !t->font || !s) return 0;
    while (*s) {
        uint32_t cp = utf8_next(&s);
        w += adv_q8(t, cp, resolve(t, cp));
    }
    return w;
}

static zui_glyph_t *glyph_get(const zui_text_t *t, const ttf_font_t *f, uint32_t gid, uint32_t sub)
{
    zui_glyph_cache_t *c = t->cache;
    uint32_t h = (gid * 2654435761u) ^ (t->size_px * 40503u) ^ (sub * 97u) ^
                 (uint32_t) (((uintptr_t) f >> 4) * 2246822519u);
    uint32_t idx = h % ZUI_GLYPH_SLOTS;
    for (uint32_t probe = 0; probe < 8u; probe++) {
        zui_glyph_t *g = &c->slots[(idx + probe) % ZUI_GLYPH_SLOTS];
        if (g->used && g->font == f && g->gid == gid && g->size == t->size_px && g->sub == sub) {
            c->hits++;
            return g;
        }
        if (!g->used) {
            idx = (idx + probe) % ZUI_GLYPH_SLOTS;
            break;
        }
    }
    c->misses++;
    zui_glyph_t *g = &c->slots[idx];
    int32_t asc = font_px_q8(f, t->size_px, f->ascender) >> 8;
    int32_t dsc = -(font_px_q8(f, t->size_px, f->descender) >> 8);
    int32_t pad = 2 + (int32_t) t->size_px / 8;
    uint32_t w =
        (uint32_t) (((font_px_q8(f, t->size_px, (int32_t) ttf_advance(f, gid)) + 255) >> 8) +
                    2 * pad);
    uint32_t hh = (uint32_t) (asc + dsc + 2 * pad);
    if (w * hh > ZUI_GLYPH_POOL) return 0;
    if (c->used + w * hh > ZUI_GLYPH_POOL) { /* full: start over, keeping the chain */
        cache_reset(c);
    }
    g->font = f;
    g->gid = gid;
    g->size = t->size_px;
    g->sub = sub;
    g->w = w;
    g->h = hh;
    g->at = c->used;
    g->ox = pad;
    g->oy = pad + asc;
    g->ink = ttf_render_at(f, gid, t->size_px, &c->pool[g->at], w, hh,
                           pad * 256 + (int32_t) sub * 64, g->oy);
    g->used = true;
    c->used += w * hh;
    return g;
}

/* Light text on a dark ground thins out under plain linear coverage; lift the
 * mid-coverage a little so strokes keep their weight. Monotone, 0->0, 255->255. */
static uint32_t lift(uint32_t c)
{
    uint32_t inv = 255u - c;
    return (c * 3u + (255u - (inv * inv) / 255u)) / 4u;
}

static int32_t text_run(zui_surface_t *s, const zui_text_t *t, int32_t x_q8, int32_t base,
                        uint32_t rgb, const char *str, int32_t stop_q8, uint32_t alpha)
{
    while (*str) {
        uint32_t cp = utf8_next(&str);
        zui_src_t src = resolve(t, cp);
        int32_t a = adv_q8(t, cp, src);
        if (stop_q8 >= 0 && x_q8 + a > stop_q8) break;
        if (src.kind == 1) {
            zui_symbol(s, cp, (x_q8 + 128) >> 8, base, t->size_px, rgb, alpha);
            x_q8 += a;
            continue;
        }
        if (src.kind == 2) {
            t->cache->sym(s, cp, (x_q8 + 128) >> 8, base, t->size_px, rgb, alpha,
                          t->cache->sym_ctx);
            x_q8 += a;
            continue;
        }
        uint32_t gid = src.gid;
        int32_t pen = x_q8;
        if (t->tabular && cp >= '0' && cp <= '9' && src.font == t->font) /* centre in the cell */
            pen += (a - to_px_q8(t, (int32_t) ttf_advance(t->font, gid))) / 2;
        int32_t px = pen >> 8;
        uint32_t sub = (uint32_t) ((pen & 255) >> 6);
        zui_glyph_t *g = glyph_get(t, src.font, gid, sub);
        if (g && g->ink) {
            const uint8_t *bm = &t->cache->pool[g->at];
            for (uint32_t yy = 0; yy < g->h; yy++)
                for (uint32_t xx = 0; xx < g->w; xx++) {
                    uint32_t cov = bm[yy * g->w + xx];
                    if (cov)
                        zui_blend_px(s, px - g->ox + (int32_t) xx, base - g->oy + (int32_t) yy, rgb,
                                     alpha >= 255u ? lift(cov) : (lift(cov) * alpha) / 255u);
                }
        }
        x_q8 += a;
    }
    return x_q8;
}

int32_t zui_text(zui_surface_t *s, const zui_text_t *t, int32_t x, int32_t base, uint32_t rgb,
                 const char *str)
{
    if (!s || !t || !t->font || !t->cache || !str) return x;
    return (text_run(s, t, x * 256, base, rgb, str, -1, 255u) + 255) >> 8;
}

int32_t zui_text_fit(zui_surface_t *s, const zui_text_t *t, int32_t x, int32_t base, int32_t max_w,
                     uint32_t rgb, const char *str)
{
    if (!s || !t || !t->font || !t->cache || !str) return x;
    if (zui_text_width_q8(t, str) <= max_w * 256) return zui_text(s, t, x, base, rgb, str);
    const char *ell = ttf_glyph_index(t->font, 0x2026u) ? "\xE2\x80\xA6" : "...";
    int32_t ew = zui_text_width_q8(t, ell);
    int32_t end = text_run(s, t, x * 256, base, rgb, str, x * 256 + max_w * 256 - ew, 255u);
    return (text_run(s, t, end, base, rgb, ell, -1, 255u) + 255) >> 8;
}

int32_t zui_text_a(zui_surface_t *s, const zui_text_t *t, int32_t x, int32_t base, uint32_t rgb,
                   uint32_t a, const char *str)
{
    if (!s || !t || !t->font || !t->cache || !str) return x;
    return (text_run(s, t, x * 256, base, rgb, str, -1, a > 255u ? 255u : a) + 255) >> 8;
}
