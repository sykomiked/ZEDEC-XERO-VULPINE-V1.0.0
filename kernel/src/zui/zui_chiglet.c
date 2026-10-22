/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_chiglet.c — the companion's form from its genes. See zui_chiglet.h. */
#include <stdbool.h>
#include "zui_chiglet.h"
#include "zui_tokens.h"
#include "holo.h"

enum { G_EMOTION, G_TEMPER, G_CURIOUS, G_CAUTION, G_WARMTH, G_PACE, G_VERBOSE, G_SPECIALTY };

static const uint32_t ACCENTS[6] = {ZUI_GOLD,      ZUI_OK,    ZUI_INFO,
                                    ZUI_GOLD_GLOW, 0x9FE3C0u, 0xF2A65Au};

uint32_t zui_chiglet_accent(const zui_chiglet_t *c)
{
    return c ? ACCENTS[c->genes[G_EMOTION] % 6u] : ZUI_GOLD;
}

static void disc(zui_surface_t *s, int32_t cx, int32_t cy, int32_t r, uint32_t rgb)
{
    if (r < 1) r = 1;
    zui_rect_t b = {cx - r, cy - r, 2 * r, 2 * r};
    zui_fill_rrect(s, b, r, rgb, 255u);
}

/* integer sine-ish table: 16 steps around a circle, scaled by 1024 */
static const int16_t SIN16[16] = {0, 392,  724,  946,  1024,  946,  724,  392,
                                  0, -392, -724, -946, -1024, -946, -724, -392};

uint32_t zui_chiglet_rings(uint32_t together)
{
    static const uint32_t AT[4] = {13u, 89u, 610u, 4181u};
    uint32_t n = 0;
    while (n < 4u && together >= AT[n]) n++;
    return n;
}

void zui_chiglet_draw_aura(zui_surface_t *s, const zui_chiglet_t *c, int32_t cx, int32_t cy,
                           int32_t r, uint32_t tick)
{
    if (!s || !c || r < 8) return;
    const uint8_t *g = c->genes;
    uint32_t accent = zui_chiglet_accent(c);

    /* halo: the rings its time with its person has earned */
    uint32_t rings = zui_chiglet_rings(c->together);
    for (uint32_t i = 0; i < rings; i++) {
        int32_t rr = r + r / 4 + (int32_t) i * (r / 6 + 2);
        zui_rect_t b = {cx - rr, cy - rr, 2 * rr, 2 * rr};
        zui_stroke_rrect(s, b, rr, 1, accent, 70u + 30u * (rings - i));
    }

    /* motes orbit at the pace gene's count and speed */
    uint32_t motes = 1u + g[G_PACE] % 4u;
    for (uint32_t i = 0; i < motes; i++) {
        uint32_t ph = (tick * (1u + g[G_PACE] % 3u) + i * 16u / motes) & 15u;
        int32_t orx = r + r / 2;
        int32_t mx = cx + (orx * SIN16[ph]) / 1024;
        int32_t my = cy + (orx * SIN16[(ph + 4u) & 15u]) / 2048;
        disc(s, mx, my, r / 10 + 1, accent);
    }
}

void zui_chiglet_draw(zui_surface_t *s, const zui_chiglet_t *c, int32_t cx, int32_t cy, int32_t r,
                      uint32_t tick)
{
    if (!s || !c || r < 8) return;
    const uint8_t *g = c->genes;
    uint32_t accent = zui_chiglet_accent(c);

    zui_chiglet_draw_aura(s, c, cx, cy, r, tick);

    /* antennae: curiosity */
    uint32_t ant = g[G_CURIOUS] % 4u;
    for (uint32_t i = 0; i < ant; i++) {
        int32_t ax = cx + ((int32_t) i - (int32_t) (ant - 1) / 2) * (r / 2) - (ant % 2 ? 0 : r / 4);
        zui_rect_t stem = {ax - 1, cy - r - r / 3, 2, r / 3 + 2};
        zui_fill(s, stem, ZUI_TEXT_2);
        disc(s, ax, cy - r - r / 3, r / 8 + 1, accent);
    }

    /* body: roundness from temperament, tint from warmth (chromostereopsis) */
    int32_t corner = r / 3 + (int32_t) ((uint32_t) r * 2u / 3u * (g[G_TEMPER] % 8u) / 7u);
    int32_t warm = (int32_t) (g[G_WARMTH] % 9u) - 4; /* -4 .. +4 */
    uint32_t body = holo_shade(0x1D5C3Bu, 0, warm * 20, 8, 40);
    zui_rect_t bb = {cx - r, cy - r, 2 * r, 2 * r};
    zui_fill_rrect(s, bb, corner, body, 255u);
    /* shield: caution */
    int32_t sw = 1 + (int32_t) (g[G_CAUTION] % 3u);
    zui_stroke_rrect(s, bb, corner, sw, ZUI_TEXT_2, 200u);
    zui_engrave(s,
                (zui_rect_t){cx - r + sw + 1, cy - r + sw + 1, 2 * (r - sw - 1), 2 * (r - sw - 1)},
                corner > sw + 1 ? corner - sw - 1 : 0, ZUI_ENGRAVE_LO, ZUI_ENGRAVE_HI, 60u);

    /* face: eyes by mood */
    int32_t ey = cy - r / 5, ex = r / 3, er = r / 7 + 1;
    disc(s, cx - ex, ey, er, ZUI_TEXT);
    if (c->mood == ZUI_CHG_UNCERTAIN) {
        zui_rect_t lid = {cx + ex - er, ey - er / 3, 2 * er, (er * 2) / 3 + 1};
        zui_fill_rrect(s, lid, er / 2, ZUI_TEXT, 255u); /* one eye narrowed */
    } else {
        disc(s, cx + ex, ey, er, ZUI_TEXT);
    }
    disc(s, cx - ex, ey, er / 2, ZUI_VOID);
    if (c->mood != ZUI_CHG_UNCERTAIN) disc(s, cx + ex, ey, er / 2, ZUI_VOID);

    /* mouth: verbosity sets width; speaking opens it */
    int32_t mw = r / 4 + (int32_t) ((uint32_t) r / 2u * (g[G_VERBOSE] % 5u) / 4u);
    int32_t mh = c->mood == ZUI_CHG_SPEAKING ? r / 5 + 1 : 2;
    bool open = c->mood == ZUI_CHG_SPEAKING;
    if (c->mouth) { /* the speech level: 2 px closed .. r / 4 + 2 wide open */
        mh = 2 + (int32_t) (((uint32_t) r / 4u) * c->mouth / 255u);
        open = mh > 3;
    }
    zui_rect_t mouth = {cx - mw / 2, cy + r / 4, mw, mh};
    zui_fill_rrect(s, mouth, mh / 2, open ? ZUI_VOID : ZUI_TEXT_2, 255u);

    /* chest emblem: specialty, one of four marks in the accent colour */
    int32_t ey2 = cy + r / 2 + r / 8, k = r / 8 + 1;
    switch (g[G_SPECIALTY] % 4u) {
    case 0:
        disc(s, cx, ey2, k, accent);
        break;
    case 1:
        zui_fill(s, (zui_rect_t){cx - k, ey2 - k, 2 * k, 2 * k}, accent);
        break;
    case 2:
        zui_fill(s, (zui_rect_t){cx - 2 * k, ey2 - k / 2, 4 * k, k}, accent);
        break;
    default:
        zui_fill(s, (zui_rect_t){cx - k / 2, ey2 - 2 * k, k, 4 * k}, accent);
        zui_fill(s, (zui_rect_t){cx - 2 * k, ey2 - k / 2, 4 * k, k}, accent);
        break;
    }
}
