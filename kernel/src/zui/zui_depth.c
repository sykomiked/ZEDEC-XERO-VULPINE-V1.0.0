/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_depth.c — depth cues. See zui_depth.h for what is and is not claimed. */
#include "zui_depth.h"
#include "zui_srgb_lut.h"
#include "holo.h"

void zui_depth_init(zui_depth_t *d, zui_depth_mode_t mode, uint32_t px_per_cm)
{
    if (!d) return;
    if (px_per_cm == 0) px_per_cm = 40; /* ~100 dpi */
    d->mode = mode;
    d->view_dist_px = (int32_t) (60u * px_per_cm);
    d->ipd_px = (int32_t) ((63u * px_per_cm + 5u) / 10u);
    d->head_x = 0;
    d->head_y = 0;
    d->chroma_k = mode == ZUI_DEPTH_FOIL ? 40 : 24;
    d->reverse_chroma = false;
    d->foil_k = mode == ZUI_DEPTH_FOIL ? 256 : 0;
    d->sheen = 0;
}

int32_t zui_role_z(zui_role_t role, int32_t m)
{
    if (m < 0) m = -m;
    switch (role) {
    case ZUI_ROLE_FORWARD:
        return m;
    case ZUI_ROLE_BACK:
        return -m;
    case ZUI_ROLE_HELD:
        return 0;
    }
    return 0; /* unknown role: held */
}

int32_t zui_eye_shift(const zui_depth_t *d, int32_t z, int32_t eye_x)
{
    if (!d || z == 0 || eye_x == 0) return 0;
    int64_t V = d->view_dist_px > 0 ? d->view_dist_px : 2400;
    if (z >= V) z = (int32_t) (V - 1); /* never at or behind the eye */
    int64_t num = -(int64_t) eye_x * z, den = V - z;
    /* round to nearest, symmetric about zero, so S+ and S- mirror exactly */
    int64_t q = (num >= 0 ? num + den / 2 : num - den / 2) / den;
    return (int32_t) q;
}

void zui_layer_offset(const zui_depth_t *d, int32_t z, int eye, int32_t *dx, int32_t *dy)
{
    int32_t x = 0, y = 0;
    if (d) {
        int32_t hx = 0, hy = 0;
        if (d->mode == ZUI_DEPTH_PARALLAX || d->mode == ZUI_DEPTH_FOIL ||
            d->mode == ZUI_DEPTH_ANAGLYPH_AMBER_BLUE || d->mode == ZUI_DEPTH_ANAGLYPH_RED_CYAN) {
            hx = d->head_x;
            hy = d->head_y;
        }
        if (eye != 0 &&
            (d->mode == ZUI_DEPTH_ANAGLYPH_AMBER_BLUE || d->mode == ZUI_DEPTH_ANAGLYPH_RED_CYAN))
            hx += eye < 0 ? -d->ipd_px / 2 : d->ipd_px / 2;
        x = zui_eye_shift(d, z, hx);
        y = zui_eye_shift(d, z, hy);
    }
    if (dx) *dx = x;
    if (dy) *dy = y;
}

uint32_t zui_chroma_px(const zui_depth_t *d, uint32_t rgb, int32_t z)
{
    if (!d || (d->mode != ZUI_DEPTH_CHROMA && d->mode != ZUI_DEPTH_FOIL) || z == 0 ||
        d->chroma_k == 0)
        return rgb & 0xFFFFFFu;
    if (d->reverse_chroma) z = -z;
    return holo_shade(rgb & 0xFFFFFFu, 0, z, 8, d->chroma_k);
}

/* Least-squares anaglyph matrices (E. Dubois, "A projection method to generate
 * anaglyph stereo images", ICASSP 2001), as published for red/cyan and
 * amber/blue glasses, in Q12 (4096 = 1.0). Each row of L + R sums to 1.0, so
 * a picture both eyes agree on comes out as itself: flat stays flat. */
static const int32_t RC_L[9] = {1868, 2048, 721, -164, -156, -66, -61, -86, -20};
static const int32_t RC_R[9] = {-176, -360, -8, 1548, 3006, -74, -295, -463, 5022};
static const int32_t AB_L[9] = {4350, -840, 1225, -106, 3719, 279, -156, -709, 90};
static const int32_t AB_R[9] = {-66, -504, -70, 25, 254, -70, 385, 758, 3731};

static int32_t lin(uint32_t c)
{
    return zui_srgb_to_lin[c & 0xFFu];
}

uint32_t zui_anaglyph_px(zui_depth_mode_t mode, uint32_t l, uint32_t r)
{
    const int32_t *ML, *MR;
    if (mode == ZUI_DEPTH_ANAGLYPH_AMBER_BLUE) {
        ML = AB_L;
        MR = AB_R;
    } else if (mode == ZUI_DEPTH_ANAGLYPH_RED_CYAN) {
        ML = RC_L;
        MR = RC_R;
    } else
        return l & 0xFFFFFFu;
    int32_t li[3] = {lin(l >> 16), lin(l >> 8), lin(l)};
    int32_t ri[3] = {lin(r >> 16), lin(r >> 8), lin(r)};
    uint32_t out = 0;
    for (int k = 0; k < 3; k++) {
        int64_t v = 0;
        for (int j = 0; j < 3; j++)
            v += (int64_t) ML[k * 3 + j] * li[j] + (int64_t) MR[k * 3 + j] * ri[j];
        v = (v + 2048) >> 12;
        if (v < 0) v = 0;
        if (v > 4095) v = 4095;
        out = (out << 8) | zui_lin_to_srgb[v];
    }
    return out;
}

bool zui_anaglyph(const zui_depth_t *d, zui_surface_t *out, const zui_surface_t *left,
                  const zui_surface_t *right)
{
    if (!d || !out || !left || !right) return false;
    if (d->mode != ZUI_DEPTH_ANAGLYPH_AMBER_BLUE && d->mode != ZUI_DEPTH_ANAGLYPH_RED_CYAN)
        return false;
    if (left->w != right->w || left->h != right->h || out->w != left->w || out->h != left->h)
        return false;
    for (int32_t y = 0; y < out->h; y++)
        for (int32_t x = 0; x < out->w; x++)
            out->px[(int64_t) y * out->stride + x] =
                zui_anaglyph_px(d->mode, left->px[(int64_t) y * left->stride + x],
                                right->px[(int64_t) y * right->stride + x]);
    return true;
}

/* ---- FOIL: sheen and fringe -------------------------------------------- */
uint32_t zui_foil_px(uint32_t rgb, uint32_t t, int32_t k)
{
    int32_t r = (int32_t) ((rgb >> 16) & 0xFF), g = (int32_t) ((rgb >> 8) & 0xFF),
            b = (int32_t) (rgb & 0xFF);
    int32_t mx = r > g ? (r > b ? r : b) : (g > b ? g : b),
            mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
    int32_t sat = mx - mn; /* 0..255: how coloured */
    if (mx > 24 && sat < 56)
        sat = 56; /* tinted panels catch a little foil; black and grey stay put */
    if (mx - mn < 8) sat = 0;
    t &= 255u;
    int32_t tri = t < 128u ? (int32_t) t : 255 - (int32_t) t; /* 0..127..0 */
    int32_t wave = tri * 2 - 127;                             /* -127..127 */
    if (k < 0)
        k = 0;
    else if (k > 256)
        k = 256;
    int32_t v = wave * sat / 255 * k / 256; /* toward (+) or away from (-) the anti-colour */
    if (v > 0) v = v * 3 / 4;               /* never all the way to the anti-colour */
    return holo_shade(rgb & 0xFFFFFFu, v, 0, 8, 0);
}

static bool in_round(zui_rect_t r, int32_t rad, int32_t x, int32_t y)
{
    if (rad <= 0) return true;
    int32_t cx = x < r.x + rad ? r.x + rad : x >= r.x + r.w - rad ? r.x + r.w - rad - 1 : x;
    int32_t cy = y < r.y + rad ? r.y + rad : y >= r.y + r.h - rad ? r.y + r.h - rad - 1 : y;
    int32_t dx = x - cx, dy = y - cy;
    return dx * dx + dy * dy <= rad * rad;
}

void zui_foil_rect(zui_surface_t *s, const zui_depth_t *d, zui_rect_t r, int32_t radius, int32_t z)
{
    if (!s || !d || d->mode != ZUI_DEPTH_FOIL || z == 0 || d->foil_k <= 0 || r.w <= 0 || r.h <= 0)
        return;
    int32_t az = z < 0 ? -z : z, k = d->foil_k * (az > 24 ? 24 : az) / 24;
    int32_t diag = r.w + r.h;
    /* the sheen sweeps along the diagonal; tilting the view (head / pointer /
     * gyro) slides it, and the host's idle drift moves it slowly */
    int32_t shift = (d->head_x + d->head_y) * 256 / 320 + d->sheen;
    if (z < 0) shift += 128; /* far things shimmer in counter-phase */
    for (int32_t y = r.y; y < r.y + r.h; y++) {
        if (y < s->clip.y || y >= s->clip.y + s->clip.h) continue;
        uint32_t *row = s->px + (int64_t) y * s->stride;
        for (int32_t x = r.x; x < r.x + r.w; x++) {
            if (x < s->clip.x || x >= s->clip.x + s->clip.w || !in_round(r, radius, x, y)) continue;
            int32_t t = ((x - r.x) + (y - r.y)) * 256 / diag + shift;
            row[x] = zui_foil_px(row[x], (uint32_t) (t * 2) & 255u, k);
        }
    }
    /* the fringe: colour on the lit edge, anti-colour on the far edge, split
     * further the nearer the thing is (or the other way round behind the glass) */
    int32_t o = 1 + (az > 24 ? 24 : az) / 12;
    uint32_t warm = 0xFFB25Cu, cool = 0x4DA6FFu, a = (uint32_t) (60 + 140 * k / 256);
    uint32_t first = z > 0 ? warm : cool, second = z > 0 ? cool : warm;
    int32_t rr = radius < r.w / 2 && radius < r.h / 2 ? radius : (r.w < r.h ? r.w : r.h) / 2;
    for (int32_t x = r.x + rr; x < r.x + r.w - rr; x++) { /* top and bottom edges */
        zui_blend_px(s, x, r.y - o, first, a);
        zui_blend_px(s, x, r.y + r.h - 1 + o, second, a);
    }
    for (int32_t y = r.y + rr; y < r.y + r.h - rr; y++) { /* left and right edges */
        zui_blend_px(s, r.x - o, y, first, a);
        zui_blend_px(s, r.x + r.w - 1 + o, y, second, a);
    }
}
