/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* display.h (views) — the Display settings panel (zv.h): which screens are
 * attached and, for the chosen one, its resolution, scaling, refresh rate,
 * orientation, depth effect strength and night light, plus the theme for
 * every screen.
 *
 * The settings model lives here with its view (dsp_*); the screens and
 * their modes come from the kernel's display negotiation (display/display.h,
 * dsp_add_kernel_screen) or from a list the caller hands in.
 *
 *   D1 PROPOSE, THEN APPLY. Every control changes the proposal only. The
 *      screen changes when the person presses Apply, through the caller's
 *      apply callback, never silently. Discard drops the proposal.
 *   D2 KEEP OR REVERT. After Apply a confirmation asks to keep the new
 *      settings and counts down DSP_CONFIRM_MS (15 s, integer ms). Keep
 *      ends it; Revert, or the time running out, puts back exactly what was
 *      there before through the same callback. No new proposal is taken
 *      while it asks.
 *   D3 RANGES. Scaling 100%..300% in steps of 25; depth effect strength
 *      0..256; night light warmth 0..256 with a schedule (from and to,
 *      minutes of the day, wrapping past midnight). A mode change keeps the
 *      refresh rate when the new mode has it, else takes the mode's highest.
 *      A mode the scanout budget cannot back is not listed (display.h:
 *      refused, never clamped).
 *   D4 THEMES are named here (Dark, Light, High contrast); their palettes
 *      come from the theme module.
 *   D5 One gold element: Apply, or Keep while the confirmation asks.
 *      Integer only, no allocation.
 */
#ifndef ZXV_VIEW_DISPLAY_H
#define ZXV_VIEW_DISPLAY_H

#include <stdbool.h>
#include <stdint.h>

#include "zv.h"

#define DSP_SCREENS      4u
#define DSP_MODES        16u
#define DSP_RATES        6u
#define DSP_NAME_MAX     32u
#define DSP_CONFIRM_MS   15000u
#define DSP_SCALE_MIN    100u
#define DSP_SCALE_MAX    300u
#define DSP_SCALE_STEP   25u
#define DSP_LEVEL_MAX    256u /* depth effect strength and warmth */
#define DSP_LEVEL_STEP   16u  /* what one press of - or + moves them */
#define DSP_MINUTES_STEP 30u  /* the night light schedule's step */

enum { DSP_OK = 0, DSP_E_ARG = -1, DSP_E_STATE = -2, DSP_E_APPLY = -3 };

typedef enum { DSP_ROT_0 = 0, DSP_ROT_90, DSP_ROT_180, DSP_ROT_270 } dsp_orient_t;
typedef enum { DSP_THEME_DARK = 0, DSP_THEME_LIGHT, DSP_THEME_HIGH_CONTRAST } dsp_theme_t;
typedef enum { DSP_NIGHT_OFF = 0, DSP_NIGHT_ON, DSP_NIGHT_SCHEDULE } dsp_night_t;
#define DSP_THEMES 3u

typedef struct {
    uint32_t w, h;
    uint32_t rate[DSP_RATES]; /* refresh rates in mHz (59940 = 59.94 Hz), highest first */
    uint32_t nrate;
} dsp_mode_t;

typedef struct {
    char name[DSP_NAME_MAX];
    dsp_mode_t mode[DSP_MODES]; /* largest first */
    uint32_t nmode;
} dsp_screen_t;

typedef struct {
    uint32_t mode, rate;           /* indexes into the screen's lists */
    uint32_t scale;                /* percent */
    uint8_t orient;                /* dsp_orient_t */
    uint32_t depth;                /* 0..256 */
    uint8_t night;                 /* dsp_night_t */
    uint32_t warmth;               /* 0..256 */
    uint16_t night_from, night_to; /* minutes of the day */
} dsp_settings_t;

typedef struct {
    dsp_settings_t s[DSP_SCREENS];
    uint8_t theme; /* dsp_theme_t, every screen */
} dsp_config_t;

/* D1: put cfg on the screens; return DSP_OK, or anything else to refuse
 * (the proposal then stays, unapplied). */
typedef int (*dsp_apply_fn)(void *ctx, const dsp_config_t *cfg);

typedef struct {
    dsp_screen_t screen[DSP_SCREENS];
    uint32_t nscreen, sel;
    dsp_config_t applied, proposed, before;
    bool confirming;
    uint32_t deadline, now;
    dsp_apply_fn apply;
    void *apply_ctx;
    uint32_t applies, reverts, keeps;
} dsp_model_t;

void dsp_init(dsp_model_t *m, uint32_t now_ms);
/* A screen and its modes (copied, at most DSP_MODES); returns its index or
 * -1. Its settings start at mode 0, its first rate, the scale its height
 * suggests, no rotation, depth 128, night light off at warmth 128 from
 * 21:00 to 07:00. */
int32_t dsp_add_screen(dsp_model_t *m, const char *name, const dsp_mode_t *modes, uint32_t n);
/* A screen with the kernel's standard modes (display.h) that fit the
 * scanout budget, each at 60 Hz. */
int32_t dsp_add_kernel_screen(dsp_model_t *m, const char *name);
int dsp_select(dsp_model_t *m, uint32_t screen);

/* D1, D3: proposals for the selected screen (theme: every screen). */
int dsp_set_mode(dsp_model_t *m, uint32_t mode);
int dsp_set_rate(dsp_model_t *m, uint32_t rate);
int dsp_set_scale(dsp_model_t *m, uint32_t pct); /* rounds to the step */
int dsp_set_orient(dsp_model_t *m, uint32_t o);
int dsp_set_depth(dsp_model_t *m, uint32_t level);
int dsp_set_night(dsp_model_t *m, uint32_t mode);
int dsp_set_warmth(dsp_model_t *m, uint32_t level);
int dsp_set_schedule(dsp_model_t *m, uint32_t from_min, uint32_t to_min);
int dsp_set_theme(dsp_model_t *m, uint32_t theme);

bool dsp_pending(const dsp_model_t *m); /* the proposal differs from what is applied */
void dsp_discard(dsp_model_t *m);
int dsp_apply(dsp_model_t *m, uint32_t now_ms); /* D2: starts the countdown */
int dsp_keep(dsp_model_t *m);
int dsp_revert(dsp_model_t *m);
/* D2: time passes; true when the countdown ran out and it reverted. */
bool dsp_tick(dsp_model_t *m, uint32_t now_ms);
uint32_t dsp_confirm_left_ms(const dsp_model_t *m);

const dsp_settings_t *dsp_proposed(const dsp_model_t *m); /* the selected screen's */
const dsp_settings_t *dsp_applied(const dsp_model_t *m);
bool dsp_settings_equal(const dsp_settings_t *a, const dsp_settings_t *b);
uint32_t dsp_scale_for(uint32_t h); /* the suggested scale for a height */
/* D3: is the night light on at this minute of the day? */
bool dsp_night_active(const dsp_settings_t *s, uint32_t minute_of_day);
/* The night light's channel gains (x/256) for a warmth: red stays, green and
 * blue come down, blue most. */
void dsp_night_gain(uint32_t warmth, uint32_t *r, uint32_t *g, uint32_t *b);

const char *dsp_theme_name(uint32_t theme);
const char *dsp_orient_name(uint32_t o);
const char *dsp_night_name(uint32_t n);
uint32_t dsp_mode_text(char *out, uint32_t cap, const dsp_mode_t *md); /* "2560 × 1440" */
uint32_t dsp_rate_text(char *out, uint32_t cap, uint32_t mhz);         /* "59.94 Hz" */
uint32_t dsp_minutes_text(char *out, uint32_t cap, uint32_t minutes);  /* "21:00" */

/* ---- the view ------------------------------------------------------------ */

typedef struct {
    zv_shell_t sh;
    dsp_model_t *m;
    zui_id_t screens;
    zui_id_t res, rate, scale_dn, scale_val, scale_up, orient;
    zui_id_t depth_dn, depth_track, depth_val, depth_up;
    zui_id_t night, warmth_row, warmth_dn, warmth_track, warmth_val, warmth_up;
    zui_id_t sched_row, from_dn, from_val, from_up, to_dn, to_val, to_up;
    zui_id_t theme, pending, discard, apply;
    zui_id_t confirm, confirm_text, revert, keep;
    uint32_t shown_left_s; /* the countdown last shown */
} display_view_t;

void display_view_build(display_view_t *v, zui_ui_t *ui, dsp_model_t *m);
void display_view_sync(display_view_t *v);
bool display_view_action(display_view_t *v, zui_id_t w, zui_action_t a, int32_t arg);
/* D2: time passes (resyncs when the countdown's second changes or it ran
 * out). Returns true when it reverted. */
bool display_view_tick(display_view_t *v, uint32_t now_ms);

#endif /* ZXV_VIEW_DISPLAY_H */
