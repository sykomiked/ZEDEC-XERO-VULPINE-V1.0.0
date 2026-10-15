/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_boot.c — the OS boot screen, live, at the pace of the real load (see
 * zui_boot.h). */
#include "zui_boot.h"
#include "zui_tokens.h"

#include "brand/fox_zxvg.inc"

#define FOX       ZUI_BOOT_LOGOS /* ref index of the mark */
#define GOLD_ARGB (0xFF000000u | ZUI_GOLD)
#define UNITS     4096u /* the real load, rescaled for zxl_launch_blocks */

/* B3: every waiting audible cue starts sounding (silent ones are dropped) */
static void drain(zui_boot_t *b)
{
    zxl_event_t ev;
    while (zxl_launch_next(&b->launch, &ev))
        if (ev.audible && ev.kind != ZXL_EV_LIGHT)
            zxl_player_play(&b->player, zxl_boot_theme(), &ev);
}

static void screen_p(zui_boot_t *b)
{
    uint32_t p = zxl_launch_screen_p(&b->launch);
    if (b->phase == ZUI_BOOT_LOADING && p > 255u) p = 255u; /* only ready reaches the end */
    if (p > b->p) b->p = p;
}

/* B1: a logo on its tile, faint until its part lights; an empty tile when
 * the host has not handed it over */
static void draw_logo(zui_surface_t *s, zui_rect_t box, uint32_t lit, void *ctx)
{
    zui_boot_ref_t *r = ctx;
    zui_boot_t *b = r->owner;
    uint32_t i = r->part;
    if (b->logo[i])
        zui_vec_draw_simple(s, &b->work, b->logo[i], b->logo_n[i], box, 56u + lit * 200u / 256u,
                            0); /* the gold logos keep their own golds (the crest's five) */
    /* B3: the coin sounds as the tile's gold comes up */
    if (b->phase == ZUI_BOOT_LOADING && b->total && lit >= 128u &&
        zxl_launch_lit(&b->launch, i) == 0) {
        drain(b);
        screen_p(b);
    }
}

static void draw_fox(zui_surface_t *s, zui_rect_t box, uint32_t lit, void *ctx)
{
    zui_boot_ref_t *r = ctx;
    zui_boot_t *b = r->owner;
    zui_vec_draw_simple(s, &b->work, zxv_fox, sizeof zxv_fox, box, 64u + lit * 192u / 256u,
                        GOLD_ARGB);
}

void zui_boot_init(zui_boot_t *b, const zxl_prefs_t *prefs, bool reduce_motion)
{
    if (!b) return;
    uint8_t *z = (uint8_t *) b;
    for (uint32_t i = 0; i < sizeof *b; i++) z[i] = 0;
    b->prefs = prefs ? *prefs : zxl_prefs_default();
    b->reduce_motion = reduce_motion;
    for (uint32_t i = 0; i <= ZUI_BOOT_LOGOS; i++) {
        b->ref[i].owner = b;
        b->ref[i].part = i;
    }
    zxl_launch_init(&b->launch, zxl_boot_theme(), ZUI_BOOT_LOGOS, &b->prefs, 0);
    zxl_player_init(&b->player);
    zxl_player_prepare(&b->player, zxl_boot_theme()); /* the gong is ready when the system is */
}

int zui_boot_logo(zui_boot_t *b, uint32_t which, const uint8_t *zxvg, uint32_t n)
{
    if (!b || which >= ZUI_BOOT_LOGOS || !zxvg || zui_zxvg_check(zxvg, n)) return -1;
    b->logo[which] = zxvg;
    b->logo_n[which] = n;
    return 0;
}

int zui_boot_load(zui_boot_t *b, uint64_t done, uint64_t total)
{
    if (!b || b->phase != ZUI_BOOT_LOADING) return -1;
    if (done < b->done || (b->total && total != b->total) || (total && done > total)) return -1;
    if (!total) { /* not known yet: the heads breathe */
        if (done) return -1;
        return 0;
    }
    b->done = done;
    b->total = total;
    uint32_t units = (uint32_t) (done * UNITS / total);
    if (zxl_launch_blocks(&b->launch, units, UNITS)) return -1;
    drain(b);
    screen_p(b);
    return 0;
}

void zui_boot_ready(zui_boot_t *b)
{
    if (!b || b->phase != ZUI_BOOT_LOADING) return;
    if (b->total) zxl_launch_blocks(&b->launch, UNITS, UNITS);
    zxl_launch_finish(&b->launch); /* B4: the gong */
    drain(b);
    b->phase = ZUI_BOOT_OPENING;
    b->p = 256u;
    if (b->reduce_motion) {
        b->open_ms = ZUI_BOOT_OPEN_MS;
        b->phase = ZUI_BOOT_DONE;
    }
}

void zui_boot_fail(zui_boot_t *b)
{
    if (!b || b->phase != ZUI_BOOT_LOADING) return;
    zxl_launch_fail(&b->launch);
    b->phase = ZUI_BOOT_FAILED;
}

void zui_boot_tick(zui_boot_t *b, uint32_t ms)
{
    if (!b) return;
    b->tick += ms;
    if (b->phase != ZUI_BOOT_OPENING) return;
    b->open_ms += ms;
    if (b->open_ms >= ZUI_BOOT_OPEN_MS) {
        b->open_ms = ZUI_BOOT_OPEN_MS;
        b->phase = ZUI_BOOT_DONE;
    }
}

/* the three phases from the screen's progress, as every launch screen */
static zui_launch_state_t state_of(const zui_boot_t *b)
{
    uint32_t p = b->p;
    zui_launch_state_t st;
    st.gather = p >= 128u ? 256u : p * 2u;
    st.light = p <= 128u ? 0u : (p - 128u) * 4u > 256u ? 256u : (p - 128u) * 4u;
    st.release = p <= 192u ? 0u : (p - 192u) * 4u > 256u ? 256u : (p - 192u) * 4u;
    if (b->phase == ZUI_BOOT_LOADING && !b->total) st.gather = ZUI_BAR_UNKNOWN;
    if (b->phase == ZUI_BOOT_LOADING && b->launch.fed && b->launch.rest == ZXL_UNKNOWN &&
        st.light >= 256u)
        st.release = ZUI_BAR_UNKNOWN; /* every tile lit, the rest still starting */
    st.open = b->open_ms * 256u / ZUI_BOOT_OPEN_MS;
    return st;
}

void zui_boot_draw(zui_boot_t *b, zui_surface_t *s, zui_surface_t *face,
                   const zui_surface_t *behind)
{
    if (!b || !s) return;
    if (!b->started) { /* B3: the chime as the screen first shows */
        b->started = true;
        drain(b);
    }
    zui_launch_t l;
    uint8_t *z = (uint8_t *) &l;
    for (uint32_t i = 0; i < sizeof l; i++) z[i] = 0;
    l.mark = draw_fox;
    l.ctx[0] = &b->ref[FOX];
    for (uint32_t i = 0; i < ZUI_BOOT_LOGOS; i++) {
        l.parts[i] = draw_logo;
        l.ctx[1 + i] = &b->ref[i];
    }
    l.n = ZUI_BOOT_LOGOS;
    zui_rect_t area = {0, 0, s->w, s->h};
    int32_t sx = s->w * 256 / 1280, sy = s->h * 256 / 800, sc = sx < sy ? sx : sy;
    if (sc < 128) sc = 128;
    zui_launch_state_t st = state_of(b);
    if (b->phase == ZUI_BOOT_LOADING || b->phase == ZUI_BOOT_FAILED || !face || !behind ||
        face->w < s->w || face->h < s->h) {
        zui_launch_draw(s, area, sc, &l, st, b->tick);
        return;
    }
    if (b->phase == ZUI_BOOT_DONE) { /* the doors are open: only what is behind */
        zui_vault_draw(s, area, sc, face, behind, 256u);
        return;
    }
    zui_launch_draw(face, area, sc, &l, st, b->tick);
    zui_vault_draw(s, area, sc, face, behind, st.open);
}

uint32_t zui_boot_audio(zui_boot_t *b, int16_t *lr, uint32_t frames)
{
    if (!b || !lr) return 0;
    zxl_player_render(&b->player, lr, frames);
    return frames;
}
