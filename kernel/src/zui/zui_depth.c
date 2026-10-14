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
    d->chroma_k = 24;
    d->reverse_chroma = false;
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
        if (d->mode == ZUI_DEPTH_PARALLAX || d->mode == ZUI_DEPTH_ANAGLYPH_AMBER_BLUE ||
            d->mode == ZUI_DEPTH_ANAGLYPH_RED_CYAN) {
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
    if (!d || d->mode != ZUI_DEPTH_CHROMA || z == 0 || d->chroma_k == 0) return rgb & 0xFFFFFFu;
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
