/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_motion.c — the process-wide comfort preferences and the animation
 * helpers (zui_motion.h). */
#include "zui_motion.h"

static zui_motion_prefs_t prefs = {
    false, false, false, false, false, false, 100, 100, ZUI_MOTION_FADE_MS};
static uint32_t generation;
static struct {
    zui_motion_hook_fn fn;
    void *ctx;
} hooks[ZUI_MOTION_HOOKS];

static uint32_t clampu(uint32_t v, uint32_t lo, uint32_t hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

void zui_motion_defaults(zui_motion_prefs_t *p)
{
    if (!p) return;
    p->reduce_motion = false;
    p->reduce_transparency = false;
    p->increase_contrast = false;
    p->bold_text = false;
    p->flash_alert = false;
    p->mono_audio = false;
    p->text_scale_pct = ZUI_MOTION_SCALE_MIN;
    p->cursor_pct = ZUI_MOTION_CURSOR_MIN;
    p->crossfade_ms = ZUI_MOTION_FADE_MS;
}

const zui_motion_prefs_t *zui_motion_get(void)
{
    return &prefs;
}

uint32_t zui_motion_generation(void)
{
    return generation;
}

static void notify(uint32_t changed)
{
    if (!changed) return;
    generation++;
    for (uint32_t i = 0; i < ZUI_MOTION_HOOKS; i++)
        if (hooks[i].fn) hooks[i].fn(&prefs, changed, hooks[i].ctx);
}

uint32_t zui_motion_set(const zui_motion_prefs_t *in)
{
    if (!in) return 0;
    zui_motion_prefs_t n = *in;
    n.text_scale_pct =
        (uint16_t) clampu(in->text_scale_pct, ZUI_MOTION_SCALE_MIN, ZUI_MOTION_SCALE_MAX);
    n.cursor_pct = (uint16_t) clampu(in->cursor_pct, ZUI_MOTION_CURSOR_MIN, ZUI_MOTION_CURSOR_MAX);
    n.crossfade_ms = (uint16_t) clampu(in->crossfade_ms, 0, ZUI_MOTION_MS);
    uint32_t c = 0;
    if (n.reduce_motion != prefs.reduce_motion) c |= ZUI_PREF_REDUCE_MOTION;
    if (n.reduce_transparency != prefs.reduce_transparency) c |= ZUI_PREF_REDUCE_TRANSPARENCY;
    if (n.increase_contrast != prefs.increase_contrast) c |= ZUI_PREF_INCREASE_CONTRAST;
    if (n.bold_text != prefs.bold_text) c |= ZUI_PREF_BOLD_TEXT;
    if (n.flash_alert != prefs.flash_alert) c |= ZUI_PREF_FLASH_ALERT;
    if (n.mono_audio != prefs.mono_audio) c |= ZUI_PREF_MONO_AUDIO;
    if (n.text_scale_pct != prefs.text_scale_pct) c |= ZUI_PREF_TEXT_SCALE;
    if (n.cursor_pct != prefs.cursor_pct) c |= ZUI_PREF_CURSOR;
    if (n.crossfade_ms != prefs.crossfade_ms) c |= ZUI_PREF_CROSSFADE;
    prefs = n;
    notify(c);
    return c;
}

uint32_t zui_motion_reset(void)
{
    zui_motion_prefs_t d;
    zui_motion_defaults(&d);
    return zui_motion_set(&d);
}

uint32_t zui_motion_set_flag(uint32_t which, bool on)
{
    zui_motion_prefs_t n = prefs;
    switch (which) {
    case ZUI_PREF_REDUCE_MOTION:
        n.reduce_motion = on;
        break;
    case ZUI_PREF_REDUCE_TRANSPARENCY:
        n.reduce_transparency = on;
        break;
    case ZUI_PREF_INCREASE_CONTRAST:
        n.increase_contrast = on;
        break;
    case ZUI_PREF_BOLD_TEXT:
        n.bold_text = on;
        break;
    case ZUI_PREF_FLASH_ALERT:
        n.flash_alert = on;
        break;
    case ZUI_PREF_MONO_AUDIO:
        n.mono_audio = on;
        break;
    default:
        return 0;
    }
    return zui_motion_set(&n);
}

uint32_t zui_motion_set_text_scale(uint32_t pct)
{
    zui_motion_prefs_t n = prefs;
    n.text_scale_pct = (uint16_t) clampu(pct, ZUI_MOTION_SCALE_MIN, ZUI_MOTION_SCALE_MAX);
    return zui_motion_set(&n);
}

uint32_t zui_motion_set_cursor(uint32_t pct)
{
    zui_motion_prefs_t n = prefs;
    n.cursor_pct = (uint16_t) clampu(pct, ZUI_MOTION_CURSOR_MIN, ZUI_MOTION_CURSOR_MAX);
    return zui_motion_set(&n);
}

uint32_t zui_motion_set_crossfade(uint32_t ms)
{
    zui_motion_prefs_t n = prefs;
    n.crossfade_ms = (uint16_t) clampu(ms, 0, ZUI_MOTION_MS);
    return zui_motion_set(&n);
}

/* ---- MO5 ------------------------------------------------------------------- */

bool zui_motion_watch(zui_motion_hook_fn fn, void *ctx)
{
    if (!fn) return false;
    int32_t free_slot = -1;
    for (uint32_t i = 0; i < ZUI_MOTION_HOOKS; i++) {
        if (hooks[i].fn == fn && hooks[i].ctx == ctx) {
            fn(&prefs, ZUI_PREF_ALL, ctx);
            return true;
        }
        if (!hooks[i].fn && free_slot < 0) free_slot = (int32_t) i;
    }
    if (free_slot < 0) return false;
    hooks[free_slot].fn = fn;
    hooks[free_slot].ctx = ctx;
    fn(&prefs, ZUI_PREF_ALL, ctx);
    return true;
}

bool zui_motion_unwatch(zui_motion_hook_fn fn, void *ctx)
{
    for (uint32_t i = 0; i < ZUI_MOTION_HOOKS; i++)
        if (fn && hooks[i].fn == fn && hooks[i].ctx == ctx) {
            hooks[i].fn = 0;
            hooks[i].ctx = 0;
            return true;
        }
    return false;
}

void zui_motion_sync_flag(const zui_motion_prefs_t *p, uint32_t changed, void *ctx)
{
    if (p && ctx && (changed & ZUI_PREF_REDUCE_MOTION)) *(bool *) ctx = p->reduce_motion;
}

/* ---- MO3, MO4 ------------------------------------------------------------------ */

zui_motion_style_t zui_motion_style(void)
{
    if (!prefs.reduce_motion) return ZUI_MOTION_FULL;
    return prefs.crossfade_ms ? ZUI_MOTION_FADE : ZUI_MOTION_CUT;
}

uint32_t zui_motion_duration(uint32_t ms)
{
    if (!prefs.reduce_motion) return ms;
    return prefs.crossfade_ms < ms ? prefs.crossfade_ms : ms;
}

uint32_t zui_motion_step(uint32_t ms, uint32_t total)
{
    uint32_t d = zui_motion_duration(total);
    if (d >= total) return ms;
    if (d == 0) return total;
    uint64_t v = ((uint64_t) ms * total + d - 1u) / d;
    return v > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t) v;
}

uint32_t zui_motion_progress_q8(uint32_t elapsed, uint32_t total)
{
    uint32_t d = zui_motion_duration(total);
    if (d == 0 || elapsed >= d) return 256;
    return (uint32_t) ((uint64_t) elapsed * 256u / d);
}

uint32_t zui_motion_ease_q8(uint32_t t)
{
    if (t >= 256) return 256;
    if (prefs.reduce_motion) return t;
    uint32_t u = 256u - t;
    return 256u - (u * u * u) / 65536u;
}

int32_t zui_motion_travel(int32_t dist, uint32_t t, uint32_t total)
{
    if (total == 0 || t >= total) return dist;
    if (prefs.reduce_motion) return 0;
    return (int32_t) ((int64_t) dist * t / total);
}

uint32_t zui_motion_fade_a255(uint32_t elapsed, uint32_t total)
{
    uint32_t p = zui_motion_progress_q8(elapsed, total);
    return 255u - (p * 255u + 128u) / 256u;
}

/* ---- MO6 ------------------------------------------------------------------------ */

int32_t zui_motion_text_px(int32_t px)
{
    return (int32_t) (((int64_t) px * prefs.text_scale_pct + 50) / 100);
}

int32_t zui_motion_cursor_px(int32_t px)
{
    return (int32_t) (((int64_t) px * prefs.cursor_pct + 50) / 100);
}

uint32_t zui_motion_alpha(uint32_t a255)
{
    if (a255 > 255u) a255 = 255u;
    return prefs.reduce_transparency && a255 ? 255u : a255;
}

zui_scheme_t zui_motion_scheme(zui_scheme_t chosen)
{
    return prefs.increase_contrast ? ZUI_SCHEME_HIGH_CONTRAST : chosen;
}

/* ---- MO7 -------------------------------------------------------------------------- */

bool zui_motion_alert(const zui_alert_out_t *o)
{
    if (!o) return false;
    if (prefs.flash_alert && o->flash) {
        o->flash(o->ctx, ZUI_MOTION_FLASH_MS);
        return true;
    }
    if (o->sound) o->sound(o->ctx);
    return false;
}

bool zui_motion_mix_mono(int16_t *lr, uint32_t frames)
{
    if (!prefs.mono_audio || !lr) return false;
    for (uint32_t i = 0; i < frames; i++) {
        int32_t s = ((int32_t) lr[2 * i] + (int32_t) lr[2 * i + 1]);
        /* halve, rounding toward minus infinity so the result is symmetric */
        int16_t m = (int16_t) (s >= 0 ? s / 2 : -((-s + 1) / 2));
        lr[2 * i] = m;
        lr[2 * i + 1] = m;
    }
    return true;
}
