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
    int32_t ek = lut->edge_k, ph = (int32_t) (tick >> 3);
    for (int32_t y = y0; y < y1; y++) {
        const uint8_t *tr = p->tag + y * p->stride;
        const int8_t *zr = p->z + y * p->stride;
        uint32_t *row = s->px + (int64_t) y * s->stride;
        int32_t yt = clampi(y + dy, 0, h - 1), ya = clampi(y - dy, 0, h - 1);
        const int8_t *zt = p->z + yt * p->stride, *za = p->z + ya * p->stride;
        /* UI pixels come in runs: same colour, same cell, same slope. Remember
         * the last answer and reuse it; that is most of the frame. Two slots,
         * by column parity, so the BOTH interleave keeps hitting too. */
        uint32_t last_in[2] = {0, 0}, last_out[2] = {0, 0};
        int32_t last_a[2] = {0x7FFF, 0x7FFF}, last_b[2] = {0x7FFF, 0x7FFF};
        for (int32_t x = 0; x < w; x++) {
            int32_t xa = x - dx, xt = x + dx;
            if (xa < 0)
                xa = 0;
            else if (xa >= w)
                xa = w - 1;
            if (xt < 0)
                xt = 0;
            else if (xt >= w)
                xt = w - 1;
            int32_t g = (int32_t) za[xa] - (int32_t) zt[xt];
            const zui_pxl_cell_t *cl = &lut->cell[tr[x] & 63][((int32_t) zr[x] + 128) >> 3];
            if (!cl->a && !cl->b && !g) continue; /* flat and held: untouched */
            int32_t a = cl->a, b = cl->b;
            if (cl->glut && ((x + y + ph) & 1)) a = -a; /* colour and anti-colour interleaved */
            if (g) { /* the relief edge: a lit lip and a shadow lip, one pixel each */
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
