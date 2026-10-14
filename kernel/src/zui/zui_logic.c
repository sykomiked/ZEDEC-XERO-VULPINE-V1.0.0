/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_logic.c — hexagram tag -> look. See zui_logic.h. */
#include "zui_logic.h"
#include "zui_canvas.h"
#include "zui_srgb_lut.h"
#include "holo.h"

#define ZUI_LOGIC_DEPTH_PT 24  /* full forward / backward travel */
#define ZUI_LOGIC_TINT     36  /* full tint toward light (of 256) */
#define ZUI_LOGIC_SHADE    112 /* full shade toward the void      */
#define ZUI_LOGIC_SHIMMER  48  /* entropic colour/anti-colour swing */

static int32_t pol_strength(uint8_t pol)
{
    switch (pol) {
    case LPX_P_NEGENTROPIC:
        return 256;
    case LPX_P_NEUTRAL:
        return 171;
    case LPX_P_ENTROPIC:
        return 85;
    default:
        return 0;
    }
}

static int32_t phase_travel(uint8_t ph)
{
    switch (ph) {
    case LPX_PH_POTENTIAL:
        return 64;
    case LPX_PH_EMERGING:
        return 128;
    case LPX_PH_MANIFEST:
        return 256;
    case LPX_PH_DISSOLVING:
        return 128;
    default:
        return 0;
    }
}

zui_look_t zui_look(lpx_t tag)
{
    zui_look_t l = {0, 0, 256, 0, false, false};
    tag &= 0x3Fu;
    if (lpx_forbidden(tag)) {
        l.forbidden = true;
        return l;
    }
    int32_t s = pol_strength(lpx_pol(tag));
    int32_t t = phase_travel(lpx_phase(tag));
    int32_t travel = (ZUI_LOGIC_DEPTH_PT * s * t + (1 << 15)) >> 16;
    l.contrast = 160 + (s * 96) / 256; /* weaker evidence, less contrast */
    if (lpx_pol(tag) == LPX_P_ENTROPIC) l.shimmer = ZUI_LOGIC_SHIMMER;
    switch (lpx_base(tag)) {
    case LPX_B_TRUE:
        l.z_pt = travel;
        l.tone = (ZUI_LOGIC_TINT * s) / 256;
        break;
    case LPX_B_FALSE:
        l.z_pt = -travel;
        l.tone = -(ZUI_LOGIC_SHADE * s) / 256;
        l.contrast -= 48;
        break;
    case LPX_B_BOTH:
        l.split = true; /* both claims stay visible */
        break;
    default: /* NEITHER: held at the glass */
        l.contrast = 256;
        l.shimmer = 0;
        break;
    }
    return l;
}

static uint32_t tone_and_contrast(const zui_look_t *l, uint32_t c, uint32_t bg, int32_t tone,
                                  int32_t contrast)
{
    (void) l;
    if (tone > 0) c = zui_mix(c, 0xFFFFFFu, (uint32_t) (tone > 255 ? 255 : tone));
    if (tone < 0) c = zui_mix(c, bg, (uint32_t) (-tone > 255 ? 255 : -tone));
    if (contrast < 256) c = zui_mix(bg, c, (uint32_t) (contrast < 0 ? 0 : contrast));
    return c & 0xFFFFFFu;
}

uint32_t zui_look_color(const zui_look_t *l, uint32_t rgb, uint32_t bg, uint32_t tick)
{
    if (!l || l->forbidden) return rgb & 0xFFFFFFu;
    uint32_t c = tone_and_contrast(l, rgb, bg, l->tone, l->contrast);
    if (l->shimmer) {
        /* triangle wave over 64 ticks: -A .. +A, toward and away from the
         * anti-colour (holo_shade's grey-stable term) */
        int32_t p = (int32_t) (tick & 63u);
        int32_t tri = p < 32 ? p : 63 - p; /* 0..31..0 */
        int32_t v = ((tri * 2 - 31) * l->shimmer) / 31;
        c = holo_shade(c, v, 0, 8, 0);
    }
    return c;
}

static uint32_t lum(uint32_t c)
{
    return 2126u * zui_srgb_to_lin[(c >> 16) & 0xFF] + 7152u * zui_srgb_to_lin[(c >> 8) & 0xFF] +
           722u * zui_srgb_to_lin[c & 0xFF]; /* 0 .. 40,950,000 */
}

uint32_t zui_contrast_x100(uint32_t a, uint32_t b)
{
    uint64_t la = lum(a), lb = lum(b), off = 2047500u; /* 0.05 of full */
    if (la < lb) {
        uint64_t t = la;
        la = lb;
        lb = t;
    }
    return (uint32_t) (((la + off) * 100u) / (lb + off));
}

uint32_t zui_look_text(const zui_look_t *l, uint32_t rgb, uint32_t bg)
{
    rgb &= 0xFFFFFFu;
    if (!l || l->forbidden) return rgb;
    uint32_t want = zui_contrast_x100(rgb, bg) >= 450u ? 450u : 0u;
    /* back the shading off in steps until the floor holds */
    for (int32_t k = 8; k >= 0; k--) {
        uint32_t c =
            tone_and_contrast(l, rgb, bg, (l->tone * k) / 8, 256 - ((256 - l->contrast) * k) / 8);
        if (zui_contrast_x100(c, bg) >= want) return c;
    }
    return rgb;
}
