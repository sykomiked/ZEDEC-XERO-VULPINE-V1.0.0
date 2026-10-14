/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_magnifier.c — full-screen and lens zoom, focus / caret / pointer
 * following, and colour filters (zui_magnifier.h). */
#include "zui_magnifier.h"
#include "zui_srgb_lut.h"
#include "zui_tokens.h"

/* ---- small helpers ------------------------------------------------------ */

static int64_t clamp64(int64_t v, int64_t lo, int64_t hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

static int32_t clamp32(int32_t v, int32_t lo, int32_t hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

/* Division rounding toward minus infinity (d > 0). */
static int64_t fdiv(int64_t n, int64_t d)
{
    int64_t q = n / d;
    return (n % d != 0 && n < 0) ? q - 1 : q;
}

static int32_t zoom_of(const zui_magnifier_t *m)
{
    return clamp32(m->zoom_q8, ZUI_MAG_MIN_Q8, ZUI_MAG_MAX_Q8);
}

/* The view's size in source pixels, Q8. */
static int64_t view_w_q8(const zui_magnifier_t *m)
{
    return (int64_t) m->screen_w * 65536 / zoom_of(m);
}

static int64_t view_h_q8(const zui_magnifier_t *m)
{
    return (int64_t) m->screen_h * 65536 / zoom_of(m);
}

/* A view origin on a whole source pixel, so nearest-neighbour blocks are all
 * the same size; only the far screen edge may leave a fraction. */
static int64_t snap(int64_t v_q8)
{
    return fdiv(v_q8, 256) * 256;
}

static void clamp_view(zui_magnifier_t *m)
{
    int64_t mx = (int64_t) m->screen_w * 256 - view_w_q8(m);
    int64_t my = (int64_t) m->screen_h * 256 - view_h_q8(m);
    m->vx_q8 = clamp64(m->vx_q8, 0, mx > 0 ? mx : 0);
    m->vy_q8 = clamp64(m->vy_q8, 0, my > 0 ? my : 0);
}

static void clamp_lens(zui_magnifier_t *m)
{
    m->lens_x = clamp32(m->lens_x, 0, m->screen_w > 0 ? m->screen_w - 1 : 0);
    m->lens_y = clamp32(m->lens_y, 0, m->screen_h > 0 ? m->screen_h - 1 : 0);
}

/* The lens on screen: centred on its point, kept inside the screen. */
static zui_rect_t lens_rect(const zui_magnifier_t *m)
{
    int32_t w = clamp32(m->lens_w, 1, m->screen_w > 0 ? m->screen_w : 1);
    int32_t h = clamp32(m->lens_h, 1, m->screen_h > 0 ? m->screen_h : 1);
    int32_t x = clamp32(m->lens_x - w / 2, 0, m->screen_w - w > 0 ? m->screen_w - w : 0);
    int32_t y = clamp32(m->lens_y - h / 2, 0, m->screen_h - h > 0 ? m->screen_h - h : 0);
    zui_rect_t r = {x, y, w, h};
    return r;
}

static bool in_rect(zui_rect_t r, int32_t x, int32_t y)
{
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

/* ---- setup and zoom ------------------------------------------------------ */

void zui_mag_init(zui_magnifier_t *m, int32_t screen_w, int32_t screen_h)
{
    m->mode = ZUI_MAG_FULL;
    m->sample = ZUI_MAG_NEAREST;
    m->filter = ZUI_FILTER_NONE;
    m->follow = ZUI_MAG_FOLLOW_FOCUS | ZUI_MAG_FOLLOW_CARET | ZUI_MAG_FOLLOW_POINTER;
    m->zoom_q8 = 512;
    m->vx_q8 = m->vy_q8 = 0;
    m->ring_px = 2;
    m->lens_x = m->lens_y = 0;
    zui_mag_resize(m, screen_w, screen_h);
}

void zui_mag_resize(zui_magnifier_t *m, int32_t screen_w, int32_t screen_h)
{
    m->screen_w = screen_w > 0 ? screen_w : 0;
    m->screen_h = screen_h > 0 ? screen_h : 0;
    m->lens_w = m->screen_w / 3 > 0 ? m->screen_w / 3 : 1;
    m->lens_h = m->screen_h / 3 > 0 ? m->screen_h / 3 : 1;
    clamp_view(m);
    clamp_lens(m);
}

int32_t zui_mag_set_zoom(zui_magnifier_t *m, int32_t zoom_q8)
{
    /* M2: snap to the nearest quarter, then clamp */
    int32_t z =
        zoom_q8 < 0 ? 0 : (zoom_q8 + ZUI_MAG_STEP_Q8 / 2) / ZUI_MAG_STEP_Q8 * ZUI_MAG_STEP_Q8;
    z = clamp32(z, ZUI_MAG_MIN_Q8, ZUI_MAG_MAX_Q8);
    int64_t cx = m->vx_q8 + view_w_q8(m) / 2;
    int64_t cy = m->vy_q8 + view_h_q8(m) / 2;
    m->zoom_q8 = z;
    m->vx_q8 = snap(cx - view_w_q8(m) / 2);
    m->vy_q8 = snap(cy - view_h_q8(m) / 2);
    clamp_view(m);
    return z;
}

int32_t zui_mag_zoom_step(zui_magnifier_t *m, int32_t steps)
{
    int64_t z = (int64_t) zoom_of(m) + (int64_t) steps * ZUI_MAG_STEP_Q8;
    return zui_mag_set_zoom(m, (int32_t) clamp64(z, 0, ZUI_MAG_MAX_Q8 * 2));
}

zui_rect_t zui_mag_view(const zui_magnifier_t *m)
{
    int64_t x0 = m->vx_q8 >> 8, y0 = m->vy_q8 >> 8;
    int64_t x1 = (m->vx_q8 + view_w_q8(m) + 255) >> 8;
    int64_t y1 = (m->vy_q8 + view_h_q8(m) + 255) >> 8;
    zui_rect_t r = {(int32_t) x0, (int32_t) y0, (int32_t) (x1 - x0), (int32_t) (y1 - y0)};
    return r;
}

void zui_mag_center(zui_magnifier_t *m, int32_t x, int32_t y)
{
    if (m->mode == ZUI_MAG_LENS) {
        m->lens_x = x;
        m->lens_y = y;
        clamp_lens(m);
        return;
    }
    m->vx_q8 = snap((int64_t) x * 256 + 128 - view_w_q8(m) / 2);
    m->vy_q8 = snap((int64_t) y * 256 + 128 - view_h_q8(m) / 2);
    clamp_view(m);
}

/* ---- M3: following ----------------------------------------------------------- */

/* One axis: bring [a, a + n) (pixels) into the view [v, v + vw) (Q8). */
static int64_t bring(int64_t v, int64_t vw, int32_t a, int32_t n)
{
    int64_t a8 = (int64_t) a * 256, n8 = (int64_t) (n > 0 ? n : 1) * 256;
    if (n8 > vw) return a8; /* larger than the view: its start */
    if (a8 >= v && a8 + n8 <= v + vw) return v;
    return snap(a8 + n8 / 2 - vw / 2); /* out of view: centre it */
}

static bool follow_rect(zui_magnifier_t *m, zui_rect_t r)
{
    if (m->mode == ZUI_MAG_LENS) {
        int32_t ox = m->lens_x, oy = m->lens_y;
        m->lens_x = r.x + (r.w > 0 ? r.w : 1) / 2;
        m->lens_y = r.y + (r.h > 0 ? r.h : 1) / 2;
        clamp_lens(m);
        return ox != m->lens_x || oy != m->lens_y;
    }
    int64_t ox = m->vx_q8, oy = m->vy_q8;
    m->vx_q8 = bring(m->vx_q8, view_w_q8(m), r.x, r.w);
    m->vy_q8 = bring(m->vy_q8, view_h_q8(m), r.y, r.h);
    clamp_view(m);
    return ox != m->vx_q8 || oy != m->vy_q8;
}

bool zui_mag_follow_focus(zui_magnifier_t *m, zui_rect_t r)
{
    if (!(m->follow & ZUI_MAG_FOLLOW_FOCUS)) return false;
    return follow_rect(m, r);
}

bool zui_mag_follow_caret(zui_magnifier_t *m, zui_rect_t r)
{
    if (!(m->follow & ZUI_MAG_FOLLOW_CARET)) return false;
    return follow_rect(m, r);
}

bool zui_mag_follow_pointer(zui_magnifier_t *m, int32_t x, int32_t y)
{
    if (!(m->follow & ZUI_MAG_FOLLOW_POINTER)) return false;
    if (m->mode == ZUI_MAG_LENS) {
        int32_t ox = m->lens_x, oy = m->lens_y;
        m->lens_x = x;
        m->lens_y = y;
        clamp_lens(m);
        return ox != m->lens_x || oy != m->lens_y;
    }
    /* push at the edges only */
    int64_t ox = m->vx_q8, oy = m->vy_q8;
    int64_t vw = view_w_q8(m), vh = view_h_q8(m);
    int64_t px = (int64_t) x * 256, py = (int64_t) y * 256;
    if (px < m->vx_q8) m->vx_q8 = px;
    if (px + 256 > m->vx_q8 + vw) m->vx_q8 = -fdiv(-(px + 256 - vw), 256) * 256; /* ceil */
    if (py < m->vy_q8) m->vy_q8 = py;
    if (py + 256 > m->vy_q8 + vh) m->vy_q8 = -fdiv(-(py + 256 - vh), 256) * 256;
    clamp_view(m);
    return ox != m->vx_q8 || oy != m->vy_q8;
}

/* ---- M6: mapping ----------------------------------------------------------- */

/* Source position (Q8) of the centre of screen pixel (sx, sy). */
static void src_q8(const zui_magnifier_t *m, zui_rect_t lens, int32_t sx, int32_t sy, int64_t *x,
                   int64_t *y)
{
    int64_t z = zoom_of(m);
    if (m->mode == ZUI_MAG_LENS) {
        int32_t cx = lens.x + lens.w / 2, cy = lens.y + lens.h / 2;
        *x = (int64_t) m->lens_x * 256 + 128 + fdiv((int64_t) (sx - cx) * 65536, z);
        *y = (int64_t) m->lens_y * 256 + 128 + fdiv((int64_t) (sy - cy) * 65536, z);
        return;
    }
    *x = m->vx_q8 + ((int64_t) sx * 2 + 1) * 32768 / z;
    *y = m->vy_q8 + ((int64_t) sy * 2 + 1) * 32768 / z;
}

bool zui_mag_to_source(const zui_magnifier_t *m, int32_t sx, int32_t sy, int32_t *x, int32_t *y)
{
    if (sx < 0 || sy < 0 || sx >= m->screen_w || sy >= m->screen_h) return false;
    zui_rect_t lens = lens_rect(m);
    if (m->mode == ZUI_MAG_LENS && !in_rect(lens, sx, sy)) {
        *x = sx;
        *y = sy;
        return true;
    }
    int64_t qx, qy;
    src_q8(m, lens, sx, sy, &qx, &qy);
    *x = (int32_t) clamp64(fdiv(qx, 256), 0, m->screen_w - 1);
    *y = (int32_t) clamp64(fdiv(qy, 256), 0, m->screen_h - 1);
    return true;
}

bool zui_mag_to_screen(const zui_magnifier_t *m, int32_t x, int32_t y, int32_t *sx, int32_t *sy)
{
    int64_t z = zoom_of(m);
    int64_t px = (int64_t) x * 256 + 128, py = (int64_t) y * 256 + 128;
    if (m->mode == ZUI_MAG_LENS) {
        zui_rect_t lens = lens_rect(m);
        int32_t cx = lens.x + lens.w / 2, cy = lens.y + lens.h / 2;
        int64_t ox = fdiv((px - ((int64_t) m->lens_x * 256 + 128)) * z, 65536);
        int64_t oy = fdiv((py - ((int64_t) m->lens_y * 256 + 128)) * z, 65536);
        int64_t lx = cx + ox, ly = cy + oy;
        if (lx >= lens.x && ly >= lens.y && lx < lens.x + lens.w && ly < lens.y + lens.h) {
            *sx = (int32_t) lx;
            *sy = (int32_t) ly;
            return true;
        }
        /* shown at 1x, unless the lens covers it */
        *sx = x;
        *sy = y;
        return x >= 0 && y >= 0 && x < m->screen_w && y < m->screen_h && !in_rect(lens, x, y);
    }
    int64_t vx = fdiv((px - m->vx_q8) * z, 65536);
    int64_t vy = fdiv((py - m->vy_q8) * z, 65536);
    *sx = (int32_t) clamp64(vx, -0x40000000LL, 0x40000000LL);
    *sy = (int32_t) clamp64(vy, -0x40000000LL, 0x40000000LL);
    return vx >= 0 && vy >= 0 && vx < m->screen_w && vy < m->screen_h;
}

/* ---- M5: filters ------------------------------------------------------------ */

/* Simulation matrices for full-strength protan, deutan and tritan vision,
 * from the physiologically based model of colour-vision deficiency published
 * by Machado, Oliveira and Fernandes (2009), in Q12, for linear RGB. Each row
 * is rounded so it sums to exactly 4096 (1.0). */
static const int32_t CVD[3][3][3] = {
    {{624, 4311, -839}, {469, 3221, 406}, {-16, -197, 4309}},   /* protan */
    {{1505, 3525, -934}, {1147, 2755, 194}, {-48, 176, 3968}},  /* deutan */
    {{5142, -314, -732}, {-321, 3813, 604}, {19, 2832, 1245}}}; /* tritan */

static uint32_t cvd(const int32_t mtx[3][3], uint32_t rgb)
{
    int32_t in[3] = {zui_srgb_to_lin[(rgb >> 16) & 255u], zui_srgb_to_lin[(rgb >> 8) & 255u],
                     zui_srgb_to_lin[rgb & 255u]};
    uint32_t out = 0;
    for (int i = 0; i < 3; i++) {
        int32_t v = mtx[i][0] * in[0] + mtx[i][1] * in[1] + mtx[i][2] * in[2];
        v = (v + 2048) >> 12;
        v = clamp32(v, 0, 4095);
        out = (out << 8) | zui_lin_to_srgb[v];
    }
    return out;
}

uint32_t zui_filter_px(zui_filter_t f, uint32_t rgb)
{
    rgb &= 0xFFFFFFu;
    switch (f) {
    case ZUI_FILTER_INVERT:
        return rgb ^ 0xFFFFFFu;
    case ZUI_FILTER_GRAYSCALE: {
        /* Rec. 709 weights in Q8 (54 + 183 + 19 = 256) */
        uint32_t y =
            (54u * ((rgb >> 16) & 255u) + 183u * ((rgb >> 8) & 255u) + 19u * (rgb & 255u) + 128u) >>
            8;
        return y * 0x010101u;
    }
    case ZUI_FILTER_PROTAN:
        return cvd(CVD[0], rgb);
    case ZUI_FILTER_DEUTAN:
        return cvd(CVD[1], rgb);
    case ZUI_FILTER_TRITAN:
        return cvd(CVD[2], rgb);
    default:
        return rgb;
    }
}

static zui_rect_t clip_to(const zui_surface_t *s, zui_rect_t r)
{
    int32_t x0 = r.x > s->clip.x ? r.x : s->clip.x;
    int32_t y0 = r.y > s->clip.y ? r.y : s->clip.y;
    int32_t x1 = r.x + r.w < s->clip.x + s->clip.w ? r.x + r.w : s->clip.x + s->clip.w;
    int32_t y1 = r.y + r.h < s->clip.y + s->clip.h ? r.y + r.h : s->clip.y + s->clip.h;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > s->w) x1 = s->w;
    if (y1 > s->h) y1 = s->h;
    zui_rect_t o = {x0, y0, x1 > x0 ? x1 - x0 : 0, y1 > y0 ? y1 - y0 : 0};
    return o;
}

void zui_filter_rect(zui_surface_t *s, zui_rect_t r, zui_filter_t f)
{
    if (!s || !s->px || f == ZUI_FILTER_NONE) return;
    zui_rect_t c = clip_to(s, r);
    for (int32_t y = c.y; y < c.y + c.h; y++)
        for (int32_t x = c.x; x < c.x + c.w; x++) {
            uint32_t *p = &s->px[(int64_t) y * s->stride + x];
            *p = zui_filter_px(f, *p);
        }
}

const char *zui_filter_name(zui_filter_t f)
{
    switch (f) {
    case ZUI_FILTER_NONE:
        return "none";
    case ZUI_FILTER_INVERT:
        return "invert";
    case ZUI_FILTER_GRAYSCALE:
        return "grayscale";
    case ZUI_FILTER_PROTAN:
        return "protan";
    case ZUI_FILTER_DEUTAN:
        return "deutan";
    case ZUI_FILTER_TRITAN:
        return "tritan";
    default:
        return "?";
    }
}

/* ---- rendering ------------------------------------------------------------------ */

static uint32_t at(const zui_surface_t *s, int64_t x, int64_t y)
{
    x = clamp64(x, 0, s->w - 1);
    y = clamp64(y, 0, s->h - 1);
    return s->px[y * s->stride + x];
}

/* M4: sample src at a Q8 position (a pixel's centre is at +128). */
static uint32_t sample(const zui_surface_t *s, zui_mag_sample_t how, int64_t x, int64_t y)
{
    if (how != ZUI_MAG_BILINEAR) return at(s, fdiv(x, 256), fdiv(y, 256)) & 0xFFFFFFu;
    int64_t px = x - 128, py = y - 128;
    int64_t ix = fdiv(px, 256), iy = fdiv(py, 256);
    uint32_t fx = (uint32_t) (px - ix * 256), fy = (uint32_t) (py - iy * 256);
    uint32_t a = at(s, ix, iy), b = at(s, ix + 1, iy);
    uint32_t c = at(s, ix, iy + 1), d = at(s, ix + 1, iy + 1);
    uint32_t out = 0;
    for (int sh = 16; sh >= 0; sh -= 8) {
        uint32_t ca = (a >> sh) & 255u, cb = (b >> sh) & 255u;
        uint32_t cc = (c >> sh) & 255u, cd = (d >> sh) & 255u;
        uint32_t top = ca * (256u - fx) + cb * fx;
        uint32_t bot = cc * (256u - fx) + cd * fx;
        uint32_t v = (top * (256u - fy) + bot * fy + 32768u) >> 16;
        out = (out << 8) | (v > 255u ? 255u : v);
    }
    return out;
}

static bool overlaps(const zui_surface_t *a, const zui_surface_t *b)
{
    const uint32_t *a0 = a->px, *a1 = a->px + (int64_t) a->stride * (a->h - 1) + a->w;
    const uint32_t *b0 = b->px, *b1 = b->px + (int64_t) b->stride * (b->h - 1) + b->w;
    /* compare as addresses: both may point anywhere */
    uintptr_t pa0 = (uintptr_t) a0, pa1 = (uintptr_t) a1;
    uintptr_t pb0 = (uintptr_t) b0, pb1 = (uintptr_t) b1;
    return pa0 < pb1 && pb0 < pa1;
}

bool zui_mag_render(const zui_magnifier_t *m, const zui_surface_t *src, zui_surface_t *dst)
{
    if (!m || !src || !dst || !src->px || !dst->px) return false;
    if (src->w <= 0 || src->h <= 0 || dst->w <= 0 || dst->h <= 0) return false;
    if (overlaps(src, dst)) return false;
    zui_rect_t all = {0, 0, dst->w, dst->h};
    zui_rect_t c = clip_to(dst, all);
    zui_rect_t lens = lens_rect(m);
    bool is_lens = m->mode == ZUI_MAG_LENS;
    for (int32_t y = c.y; y < c.y + c.h; y++) {
        uint32_t *row = &dst->px[(int64_t) y * dst->stride];
        for (int32_t x = c.x; x < c.x + c.w; x++) {
            if (is_lens && !in_rect(lens, x, y)) {
                row[x] = at(src, x, y) & 0xFFFFFFu; /* the 1x screen around the lens */
                continue;
            }
            int64_t qx, qy;
            src_q8(m, lens, x, y, &qx, &qy);
            row[x] = zui_filter_px(m->filter, sample(src, m->sample, qx, qy));
        }
    }
    if (is_lens && m->ring_px > 0) {
        /* the gold ring, on the inside edge of the lens */
        int32_t t = m->ring_px;
        for (int32_t y = lens.y; y < lens.y + lens.h; y++)
            for (int32_t x = lens.x; x < lens.x + lens.w; x++) {
                bool edge = x < lens.x + t || y < lens.y + t || x >= lens.x + lens.w - t ||
                            y >= lens.y + lens.h - t;
                if (edge && in_rect(c, x, y)) dst->px[(int64_t) y * dst->stride + x] = ZUI_GOLD;
            }
    }
    return true;
}

/* ---- M7: keys ----------------------------------------------------------------- */

bool zui_mag_event(zui_magnifier_t *m, const zui_event_t *e)
{
    if (!m || !e || (e->type != ZUI_EV_KEY_DOWN && e->type != ZUI_EV_KEY_UP)) return false;
    uint8_t want = ZUI_MOD_META | ZUI_MOD_ALT;
    if ((e->mods & want) != want || (e->mods & ZUI_MOD_CTRL)) return false;
    uint16_t k = e->key;
    bool ours = k == '=' || k == '+' || k == '-' || k == '0' || k == 'm' || k == 'i' || k == 'f';
    if (!ours) return false;
    if (e->type == ZUI_EV_KEY_UP) return true; /* the pair's other half */
    switch (k) {
    case '=':
    case '+':
        zui_mag_zoom_step(m, 1);
        break;
    case '-':
        zui_mag_zoom_step(m, -1);
        break;
    case '0':
        zui_mag_set_zoom(m, ZUI_MAG_MIN_Q8);
        break;
    case 'm':
        m->mode = m->mode == ZUI_MAG_FULL ? ZUI_MAG_LENS : ZUI_MAG_FULL;
        break;
    case 'i':
        m->filter = m->filter == ZUI_FILTER_INVERT ? ZUI_FILTER_NONE : ZUI_FILTER_INVERT;
        break;
    default: /* 'f' */
        m->filter = (zui_filter_t) (((uint32_t) m->filter + 1u) % (uint32_t) ZUI_FILTER_COUNT);
        break;
    }
    return true;
}
