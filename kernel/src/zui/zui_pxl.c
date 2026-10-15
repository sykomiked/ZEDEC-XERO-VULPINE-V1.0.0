/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_pxl.c — the paraconsistent pixel filter. See zui_pxl.h. */
#include "zui_pxl.h"
#include "zui_logic.h"

static int32_t clampi(int32_t v, int32_t lo, int32_t hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

void zui_plane_init(zui_plane_t *p, uint8_t *tag, int8_t *z, int32_t w, int32_t h, int32_t stride)
{
    if (!p) return;
    p->tag = tag;
    p->z = z;
    p->w = w;
    p->h = h;
    p->stride = stride;
}

void zui_plane_clear(zui_plane_t *p)
{
    if (!p || !p->tag || !p->z) return;
    uint8_t t = lpx_make(LPX_B_NEITHER, LPX_P_NEUTRAL, LPX_PH_MANIFEST);
    for (int32_t y = 0; y < p->h; y++)
        for (int32_t x = 0; x < p->w; x++) {
            p->tag[y * p->stride + x] = t;
            p->z[y * p->stride + x] = 0;
        }
}

static bool in_round(zui_rect_t r, int32_t rad, int32_t x, int32_t y)
{
    if (rad <= 0) return true;
    int32_t cx = x < r.x + rad ? r.x + rad : x >= r.x + r.w - rad ? r.x + r.w - rad - 1 : x;
    int32_t cy = y < r.y + rad ? r.y + rad : y >= r.y + r.h - rad ? r.y + r.h - rad - 1 : y;
    int32_t dx = x - cx, dy = y - cy;
    return dx * dx + dy * dy <= rad * rad;
}

void zui_plane_mark(zui_plane_t *p, zui_rect_t r, int32_t radius, lpx_t tag, int32_t z)
{
    if (!p || !p->tag || !p->z) return;
    int8_t zz = (int8_t) clampi(z, -127, 127);
    int32_t x0 = r.x < 0 ? 0 : r.x, y0 = r.y < 0 ? 0 : r.y;
    int32_t x1 = r.x + r.w > p->w ? p->w : r.x + r.w, y1 = r.y + r.h > p->h ? p->h : r.y + r.h;
    for (int32_t y = y0; y < y1; y++)
        for (int32_t x = x0; x < x1; x++) {
            if (!in_round(r, radius, x, y)) continue;
            p->tag[y * p->stride + x] = tag;
            p->z[y * p->stride + x] = zz;
        }
}

void zui_plane_mark_luma(zui_plane_t *p, const zui_surface_t *s, zui_rect_t r, lpx_t tag,
                         int32_t z_k)
{
    if (!p || !p->tag || !p->z || !s || !s->px) return;
    z_k = clampi(z_k, 0, 256);
    int32_t x0 = r.x < 0 ? 0 : r.x, y0 = r.y < 0 ? 0 : r.y;
    int32_t x1 = r.x + r.w, y1 = r.y + r.h;
    if (x1 > p->w) x1 = p->w;
    if (x1 > s->w) x1 = s->w;
    if (y1 > p->h) y1 = p->h;
    if (y1 > s->h) y1 = s->h;
    for (int32_t y = y0; y < y1; y++) {
        const uint32_t *row = s->px + (int64_t) y * s->stride;
        for (int32_t x = x0; x < x1; x++) {
            uint32_t c = row[x];
            int32_t lum =
                (int32_t) ((((c >> 16) & 0xFF) * 77 + ((c >> 8) & 0xFF) * 150 + (c & 0xFF) * 29) >>
                           8);
            p->tag[y * p->stride + x] = tag;
            p->z[y * p->stride + x] = (int8_t) (((lum - 128) * z_k) >> 9); /* +-64 at z_k 256 */
        }
    }
}

zui_pxl_style_t zui_pxl_style(lpx_t tag)
{
    zui_pxl_style_t st;
    static const int32_t pol_round[4] = {96, 192, 0,
                                         256};       /* entropic, neutral, forbidden, negentropic */
    static const int32_t ph_round[4] = {2, 3, 2, 4}; /* potential, emerging, dissolving, manifest */
    static const int32_t ph_hold[4] = {300, 480, 180, 900}; /* ms */
    uint8_t pol = lpx_pol(tag) & 3, ph = lpx_phase(tag) & 3;
    st.round_q8 = pol_round[pol] * ph_round[ph] / 4;
    st.edge_px = 1 + st.round_q8 * 2 / 256;
    int32_t hold = ph_hold[ph];
    if (pol == LPX_P_ENTROPIC) hold /= 2;
    if (pol == LPX_P_NEGENTROPIC) hold += hold / 3;
    if (pol == LPX_P_FORBIDDEN) hold = 0; /* flat and still */
    st.hold_ms = hold;
    st.brief = hold < 400;
    return st;
}

int32_t zui_pxl_radius(lpx_t tag, int32_t base)
{
    if (base <= 0) return 0;
    return base * (zui_pxl_style(tag).round_q8 + 64) / 320;
}

int32_t zui_pxl_ease(lpx_t tag, uint32_t elapsed_ms)
{
    zui_pxl_style_t st = zui_pxl_style(tag);
    if (st.hold_ms <= 0 || elapsed_ms >= (uint32_t) st.hold_ms) return 256;
    int64_t p = (int64_t) elapsed_ms * 256 / st.hold_ms; /* 0..255 */
    if (st.brief) {                                      /* ease-out cubic: snaps, then rests */
        int64_t q = 256 - p;
        return (int32_t) (256 - q * q * q / 65536);
    }
    /* smootherstep: 6p^5 - 15p^4 + 10p^3, settles slowly at both ends */
    int64_t p2 = p * p, p3 = p2 * p;
    int64_t v = (p3 * (p * (6 * p - 15 * 256) + 10 * 65536)) >> 32;
    return (int32_t) clampi((int32_t) v, 0, 256);
}

int32_t zui_pxl_decay(lpx_t tag, uint32_t elapsed_ms)
{
    return 256 - zui_pxl_ease(tag, elapsed_ms);
}

void zui_pxl_build(zui_pxl_lut_t *lut, int32_t strength)
{
    if (!lut) return;
    strength = clampi(strength, 0, 256);
    lut->edge_k = strength;
    for (int32_t t = 0; t < 64; t++) {
        zui_look_t lk = zui_look((lpx_t) t);
        for (int32_t zb = 0; zb < ZUI_PXL_ZBINS; zb++) {
            int32_t z = (zb - ZUI_PXL_ZBINS / 2) * 8 + 4; /* bin centre, -124..124 */
            int32_t a = 0, b = 0;
            uint8_t glut = 0;
            if (!lk.forbidden) {
                /* depth: near leans to its colour and lifts, far to its anti-colour and sinks */
                a = z * 3 / 4;
                b = z / 5;
                /* the state's own tone (TRUE tinted, FALSE shaded), as zui_logic decides */
                b += lk.tone / 8;
                if (lk.split ||
                    lk.shimmer >
                        0) { /* BOTH, or a weakening thing: colour and anti-colour at once */
                    glut = 1;
                    a = lk.split ? 64 : clampi(lk.shimmer * 4, 16, 40);
                }
                if (lpx_base((lpx_t) t) == LPX_B_NEITHER && !lk.shimmer) {
                    a = 0;
                    b = 0;
                } /* held: at the glass */
            }
            /* gentle by design: text keeps its contrast (zui_look_text's floor) */
            lut->cell[t][zb].a = (int16_t) clampi(a * strength / 256, -96, 96);
            lut->cell[t][zb].b = (int16_t) clampi(b * strength / 256, -48, 48);
            lut->cell[t][zb].glut = glut;
            zui_pxl_style_t st = zui_pxl_style((lpx_t) t);
            lut->cell[t][zb].edge = (uint8_t) st.edge_px;
            lut->cell[t][zb].tsh = (uint8_t) (st.brief ? 2 : st.hold_ms >= 800 ? 5 : 3);
        }
    }
}

static inline int32_t sat8(int32_t v)
{
    return v < 0 ? 0 : v > 255 ? 255 : v;
}

uint32_t zui_pxl_px(uint32_t rgb, int32_t a, int32_t b)
{
    int32_t c[3] = {(int32_t) ((rgb >> 16) & 0xFF), (int32_t) ((rgb >> 8) & 0xFF),
                    (int32_t) (rgb & 0xFF)};
    int32_t lum = (c[0] * 77 + c[1] * 150 + c[2] * 29) >> 8;
    a = clampi(a, -256, 256);
    b = clampi(b, -256, 256);
    for (int k = 0; k < 3; k++) {
        int32_t v = c[k];
        if (a > 0)
            v += (a * (v - lum)) >> 8; /* into its own colour  */
        else if (a < 0)
            v += (-a * (255 - 2 * v)) >> 9; /* toward its complement */
        v = sat8(v);
        if (b > 0)
            v += (b * (255 - v)) >> 8; /* tint                  */
        else if (b < 0)
            v += (b * v) >> 8; /* shade                 */
        c[k] = sat8(v);
    }
    return ((uint32_t) c[0] << 16) | ((uint32_t) c[1] << 8) | (uint32_t) c[2];
}

void zui_pxl_apply(zui_surface_t *s, const zui_plane_t *p, const zui_pxl_lut_t *lut,
                   int32_t light_x, int32_t light_y, uint32_t tick)
{
    if (!s || !p) return;
    zui_pxl_apply_rows(s, p, lut, light_x, light_y, tick, 0, s->h < p->h ? s->h : p->h);
}

void zui_pxl_apply_rows(zui_surface_t *s, const zui_plane_t *p, const zui_pxl_lut_t *lut,
                        int32_t light_x, int32_t light_y, uint32_t tick, int32_t y0, int32_t y1)
{
    if (!s || !p || !lut || !p->tag || !p->z) return;
    int32_t w = s->w < p->w ? s->w : p->w, h = s->h < p->h ? s->h : p->h;
    if (y0 < 0) y0 = 0;
    if (y1 > h) y1 = h;
    if (w <= 0 || y0 >= y1) return;
    /* one step toward the light; straight-on light gives no relief edge */
    int32_t dx = light_x < -64  ? -1
                 : light_x > 64 ? 1
                                : 0,
            dy = light_y < -64  ? -1
                 : light_y > 64 ? 1
                                : 0;
    int32_t ek = lut->edge_k;
    for (int32_t y = y0; y < y1; y++) {
        const uint8_t *tr = p->tag + y * p->stride;
        const int8_t *zr = p->z + y * p->stride;
        uint32_t *row = s->px + (int64_t) y * s->stride;
        /* rows 1..3 steps toward (t) and away from (a) the light, for the bevel */
        const int8_t *zt[4], *za[4];
        for (int32_t e = 1; e <= 3; e++) {
            zt[e] = p->z + clampi(y + dy * e, 0, h - 1) * p->stride;
            za[e] = p->z + clampi(y - dy * e, 0, h - 1) * p->stride;
        }
        /* UI pixels come in runs: same colour, same cell, same slope. Remember
         * the last answer and reuse it; that is most of the frame. Two slots,
         * by column parity, so the BOTH interleave keeps hitting too. */
        uint32_t last_in[2] = {0, 0}, last_out[2] = {0, 0};
        int32_t last_a[2] = {0x7FFF, 0x7FFF}, last_b[2] = {0x7FFF, 0x7FFF};
        for (int32_t x = 0; x < w; x++) {
            const zui_pxl_cell_t *cl = &lut->cell[tr[x] & 63][((int32_t) zr[x] + 128) >> 3];
            /* the relief: slope summed over the bevel width, so an angular
             * thing gets one crisp lip and a round one a ramp over 3 px */
            int32_t E = cl->edge, g = 0, z0 = zr[x];
            int32_t xaE = clampi(x - dx * E, 0, w - 1), xtE = clampi(x + dx * E, 0, w - 1);
            /* inside a flat run both far samples match: no slope anywhere
             * between (marks are convex), so skip the bevel sum */
            if (za[E][xaE] != z0 || zt[E][xtE] != z0) {
                for (int32_t e = 1; e <= E; e++) {
                    int32_t xa = clampi(x - dx * e, 0, w - 1), xt = clampi(x + dx * e, 0, w - 1);
                    g += (int32_t) za[e][xa] - (int32_t) zt[e][xt];
                }
                if (E > 1) g /= E;
            }
            if (!cl->a && !cl->b && !g) continue; /* flat and held: untouched */
            int32_t a = cl->a, b = cl->b;
            /* colour and anti-colour interleaved; brief things shimmer faster */
            if (cl->glut && ((x + y + (int32_t) (tick >> cl->tsh)) & 1)) a = -a;
            if (g) { /* a lit lip and a shadow lip */
                int32_t gg = g < -48 ? -48 : g > 48 ? 48 : g;
                b += gg * ek / 128;
                a += gg * ek / 256;
            }
            uint32_t in = row[x];
            int32_t k = x & 1;
            if (in != last_in[k] || a != last_a[k] || b != last_b[k]) {
                last_in[k] = in;
                last_a[k] = a;
                last_b[k] = b;
                last_out[k] = zui_pxl_px(in, a, b);
            }
            row[x] = last_out[k];
        }
    }
}
