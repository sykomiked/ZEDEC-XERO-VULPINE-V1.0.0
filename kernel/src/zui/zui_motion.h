/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_motion.h — reduced motion and the other comfort preferences, kept
 * once for the whole process, and the helpers every animation calls.
 *
 * Some people get dizzy or sick from things sliding and zooming; some cannot
 * read text over a translucent layer, need more contrast, heavier or larger
 * text, a bigger pointer, a flash instead of a sound, or both ears hearing
 * the same audio. These are settings of the person, not of an app, so they
 * live here once and every part of zui reads them.
 *
 *   MO1 ONE SET. The preferences are a single process-wide zui_motion_prefs_t
 *       (no allocation). zui_motion_get reads it; zui_motion_set and the
 *       single-field setters write it, clamping every value into range.
 *   MO2 RANGES. Text scale is 100..310 percent, the pointer 100..400
 *       percent, the reduced-motion crossfade 0..ZUI_MOTION_MS. Out-of-range
 *       values are clamped, never refused, so a bad settings file still
 *       gives a usable screen.
 *   MO3 ANIMATIONS ASK. An animation never reads the flags itself; it asks
 *       zui_motion_duration(ms) how long to run. With motion on that is ms.
 *       With reduce motion it is the crossfade (crossfade_ms, never longer
 *       than ms) or 0 for a cut. zui_motion_style says which: FULL, FADE or
 *       CUT. In FADE and CUT nothing slides, zooms or parts: an element
 *       either fades (zui_motion_fade_a255) or is simply there.
 *   MO4 TRAVEL. zui_motion_travel(dist, t, total) is how far a moving part
 *       has gone at animation time t of total: dist * t / total with motion
 *       on, and 0 until the end (then dist) when motion is reduced, so the
 *       window doors of zui_wm.h WM2 stay shut and then are simply gone.
 *       zui_motion_step(ms, total) turns real milliseconds into animation
 *       time, so a total-ms animation ends after zui_motion_duration(total)
 *       real ms (at once for a cut).
 *   MO5 HOOKS. zui_motion_watch(fn, ctx) calls fn once at once (changed =
 *       ZUI_PREF_ALL) and again on every change with the mask of what
 *       changed. Up to ZUI_MOTION_HOOKS watchers; the same (fn, ctx) is kept
 *       once. zui_motion_sync_flag is a ready watcher that copies
 *       reduce_motion into a bool (zui_ui_t.reduce_motion, a theme's flag).
 *   MO6 TEXT, POINTER, TRANSPARENCY, CONTRAST. zui_motion_text_px scales a
 *       pixel size by the text scale; zui_motion_cursor_px the pointer;
 *       zui_motion_alpha makes any translucent layer opaque when reduce
 *       transparency is on; zui_motion_scheme picks the high-contrast colour
 *       set (zui_palette.h) when increase contrast is on.
 *   MO7 ALERTS AND AUDIO. zui_motion_alert sends an alert to a flash of the
 *       screen instead of the sound when flash_alert is on (one flash, never
 *       a strobe: ZUI_MOTION_FLASH_MS long). zui_motion_mix_mono folds
 *       interleaved stereo to the same signal in both channels when
 *       mono_audio is on.
 *
 * Integer only, no allocation, no libc, deterministic. Not thread safe: the
 * settings are written from the settings app's thread, between frames.
 */
#ifndef ZXV_ZUI_MOTION_H
#define ZXV_ZUI_MOTION_H

#include <stdbool.h>
#include <stdint.h>
#include "zui_palette.h"
#include "zui_tokens.h"

#define ZUI_MOTION_SCALE_MIN  100 /* text scale, percent           */
#define ZUI_MOTION_SCALE_MAX  310
#define ZUI_MOTION_CURSOR_MIN 100 /* pointer size, percent         */
#define ZUI_MOTION_CURSOR_MAX 400
#define ZUI_MOTION_FADE_MS    120 /* default reduced-motion fade   */
#define ZUI_MOTION_FLASH_MS   200 /* MO7: one flash                */
#ifndef ZUI_MOTION_HOOKS
#    define ZUI_MOTION_HOOKS 16u
#endif

typedef struct {
    bool reduce_motion;       /* MO3 */
    bool reduce_transparency; /* MO6 */
    bool increase_contrast;   /* MO6 */
    bool bold_text;           /* the renderer picks the bold face  */
    bool flash_alert;         /* MO7 */
    bool mono_audio;          /* MO7 */
    uint16_t text_scale_pct;  /* MO2: 100..310                     */
    uint16_t cursor_pct;      /* MO2: 100..400                     */
    uint16_t crossfade_ms;    /* MO3: 0 cuts; at most ZUI_MOTION_MS */
} zui_motion_prefs_t;

/* MO5: what changed (a mask). */
enum {
    ZUI_PREF_REDUCE_MOTION = 1u << 0,
    ZUI_PREF_REDUCE_TRANSPARENCY = 1u << 1,
    ZUI_PREF_INCREASE_CONTRAST = 1u << 2,
    ZUI_PREF_BOLD_TEXT = 1u << 3,
    ZUI_PREF_FLASH_ALERT = 1u << 4,
    ZUI_PREF_MONO_AUDIO = 1u << 5,
    ZUI_PREF_TEXT_SCALE = 1u << 6,
    ZUI_PREF_CURSOR = 1u << 7,
    ZUI_PREF_CROSSFADE = 1u << 8,
    ZUI_PREF_ALL = (1u << 9) - 1u
};

typedef enum { ZUI_MOTION_FULL = 0, ZUI_MOTION_FADE, ZUI_MOTION_CUT } zui_motion_style_t;

typedef void (*zui_motion_hook_fn)(const zui_motion_prefs_t *p, uint32_t changed, void *ctx);

/* ---- MO1, MO2: the preferences ------------------------------------------- */

/* Everything off, text 100 %, pointer 100 %, crossfade ZUI_MOTION_FADE_MS. */
void zui_motion_defaults(zui_motion_prefs_t *p);
const zui_motion_prefs_t *zui_motion_get(void);
/* Copy p in (clamped); tell the watchers. Returns the mask of what changed. */
uint32_t zui_motion_set(const zui_motion_prefs_t *p);
/* Back to the defaults (the watchers stay and are told). */
uint32_t zui_motion_reset(void);
/* One field each; each returns the change mask (0: it already had that value). */
uint32_t zui_motion_set_flag(uint32_t which, bool on); /* one ZUI_PREF_ flag bit (bits 0..5) */
uint32_t zui_motion_set_text_scale(uint32_t pct);
uint32_t zui_motion_set_cursor(uint32_t pct);
uint32_t zui_motion_set_crossfade(uint32_t ms);
/* Counts every change, so a renderer can cache what it derived. */
uint32_t zui_motion_generation(void);

/* ---- MO5: watchers ------------------------------------------------------- */

bool zui_motion_watch(zui_motion_hook_fn fn, void *ctx);   /* false: the table is full */
bool zui_motion_unwatch(zui_motion_hook_fn fn, void *ctx); /* false: was not watching  */
/* A ready watcher: ctx is a bool *, kept equal to reduce_motion. */
void zui_motion_sync_flag(const zui_motion_prefs_t *p, uint32_t changed, void *ctx);

/* ---- MO3, MO4: animations ------------------------------------------------ */

zui_motion_style_t zui_motion_style(void);
uint32_t zui_motion_duration(uint32_t ms);
/* Real ms -> animation ms for an animation `total` long (MO4). */
uint32_t zui_motion_step(uint32_t ms, uint32_t total);
/* How far along an animation `total` long is after `elapsed` real ms, 0..256. */
uint32_t zui_motion_progress_q8(uint32_t elapsed, uint32_t total);
/* An ease-out curve on 0..256 (linear when motion is reduced). */
uint32_t zui_motion_ease_q8(uint32_t t);
int32_t zui_motion_travel(int32_t dist, uint32_t t, uint32_t total);
/* A leaving layer's alpha for a crossfade, 255 -> 0 over the duration. */
uint32_t zui_motion_fade_a255(uint32_t elapsed, uint32_t total);

/* ---- MO6 ----------------------------------------------------------------- */

int32_t zui_motion_text_px(int32_t px);
int32_t zui_motion_cursor_px(int32_t px);
uint32_t zui_motion_alpha(uint32_t a255);
zui_scheme_t zui_motion_scheme(zui_scheme_t chosen);

/* ---- MO7 ----------------------------------------------------------------- */

typedef struct {
    void (*sound)(void *ctx);              /* play the alert sound   */
    void (*flash)(void *ctx, uint32_t ms); /* flash the screen once  */
    void *ctx;
} zui_alert_out_t;

/* Returns true when it flashed, false when it sounded (or had nothing to use). */
bool zui_motion_alert(const zui_alert_out_t *o);
/* Interleaved L R L R ... int16 frames; returns true when it mixed. */
bool zui_motion_mix_mono(int16_t *lr, uint32_t frames);

#endif /* ZXV_ZUI_MOTION_H */
