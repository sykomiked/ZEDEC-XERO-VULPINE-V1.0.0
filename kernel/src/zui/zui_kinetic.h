/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_kinetic.h — the kinetic state engine for vector app icons.
 *
 * Every app icon is a vector drawing with exactly three addressable groups
 * (tools/zxvg/lint_icons.py checks this): the chassis (the frame, mostly
 * still), the core-artifact (what the app is) and the kinetic-element (the
 * part that moves). This engine keeps, per icon instance, its interaction
 * state, the app's activity and two springs, and turns the frame clock into
 * one override per group each frame. The renderer applies the overrides to
 * the named groups; nothing here draws.
 *
 *   KI1 FRAME CLOCK. Time is the caller's monotonic millisecond frame clock,
 *       never a CPU cycle counter, so an icon moves at the same speed on every
 *       machine. Differences are taken modulo 2^32 (the clock may wrap); a
 *       clock that steps backwards counts as no time passing.
 *   KI2 ACTIVITY IS TEMPO. zui_kinetic_activity_t says what the app is doing
 *       and sets the tempo of the icon's ambient motion:
 *         NOT_RUNNING  STILL   no ambient motion, no redraws, no badge
 *         OPEN_IDLE    SLOW    period ZUI_KINETIC_SLOW_MS   (4000 ms)
 *         RUNNING      MEDIUM  period ZUI_KINETIC_MEDIUM_MS (1600 ms)
 *         NEEDS_INPUT  FAST    period ZUI_KINETIC_FAST_MS   (750 ms)
 *       FAST is the quickest motion in the system and stays under the
 *       WCAG 2.3.1 limit: every oscillation here (breath, jitter, pulse) runs
 *       at the tempo, at most 1000/750 = 1.33 cycles per second, so at most
 *       1.33 flashes per second (the limit is 3). Changing the activity keeps
 *       the phase fraction, so the motion never jumps.
 *   KI3 TIERS. The tier picks what the ambient motion looks like:
 *       L1 MECHANICAL. Quantized, no organic easing: the breath is sampled and
 *          held four times a period, the kinetic element advances one 15
 *          degree escapement click every half period, the hover lift moves in
 *          four equal steps and the pointer aim in 15 degree clicks.
 *       L2 ORGANIC. Smooth: a breathing scale of +/-1.5 % on the core and the
 *          kinetic element plus a chest rise and fall (the core moves up
 *          ZUI_KINETIC_CHEST units at the top of the breath); springs ease.
 *       L3 CRYSTALLINE. Oscillation is a gradient-stop shift (grad_shift
 *          shimmer) instead of geometry: no breathing scale, no jitter.
 *          Directed motion (hover lift, aim, firing rotation, the BOTH split)
 *          stays geometric in every tier, because it carries information.
 *       Brand colours of the artwork (not drawn here): L1 gold #F2C230 on
 *       green #002911; L2 the original colours on gold with a dashed border;
 *       L3 the original colours on gold.
 *   KI4 STATES. On top of the ambient tempo:
 *       IDLE    only the ambient motion.
 *       HOVER   a critically damped spring lifts the icon to 1.08x; the
 *               kinetic element turns toward the pointer (integer atan2 about
 *               its pivot, 0 = straight up), clamped to +/-30 degrees.
 *       FIRING  the kinetic element turns at a rate proportional to the work
 *               rate (ZUI_KINETIC_FIRE_DD per token per cycle, in tenths of
 *               a degree per second, capped at one turn per second). L1
 *               steps in 15 degree escapement clicks; L2 and L3 are smooth.
 *       STALL   backpressure: the element strains ZUI_KINETIC_STRAIN past
 *               where it stopped and does not advance. A jitter of at most
 *               ZUI_KINETIC_JITTER at the tempo (<= 1.33 Hz) rides on top;
 *               its tint is constant, so luminance never flashes.
 *       BOTH    a Belnap contradiction held in escrow: the icon splits into
 *               two copies ZUI_KINETIC_SPLIT units either side (the frame's
 *               ghost[] is the second draw), both tinted quarantine amber
 *               (ZUI_KINETIC_QUARANTINE, distinct from the gold). The
 *               ghost's opacity pulses by at most ZUI_KINETIC_PULSE / 256 at
 *               the tempo (low contrast, <= 1.33 Hz). While held, every
 *               zui_kinetic_set_state except BOTH is refused; only
 *               zui_kinetic_settle releases it.
 *   KI5 REDRAWS COST. zui_kinetic_frame says whether the output changed since
 *       the last frame it reported (redraw). An invisible icon never asks for
 *       a redraw; a NOT_RUNNING icon at rest stops asking once its spring has
 *       settled, so STILL icons cost nothing. An L1 icon asks only when a
 *       click or a step lands.
 *   KI6 REDUCE MOTION. With zui_motion's reduce_motion on, nothing moves or
 *       oscillates: springs land at once, there is no breath, aim, rotation,
 *       jitter or pulse. State stays readable statically: HOVER keeps the
 *       1.08x lift, STALL the held strain and its tint, BOTH the full split
 *       and amber, FIRING a gold tint on the kinetic element, and every
 *       frame carries the activity badge (pip: none / dim / steady /
 *       bright-with-ring), which the renderer must draw (pip_required).
 *   KI7 OUTPUT. zui_kinetic_group_t has the shape of zui_vec's per-group
 *       override: m is a 16.16 affine [a b c d e f] (x' = a x + c y + e,
 *       y' = b x + d y + f) applied about the pivot (px, py) in 1/16 icon
 *       units of the 1024 grid, e and f being 16.16 icon units (exactly
 *       zui_vec_override_t's fields and units); opacity 0..256; tint
 *       XRGB mixed in by tint_amt / 256; grad_shift is added to every
 *       gradient's parameter (as zui_vec does), so the pattern slides by
 *       grad_shift / 65536 of the gradient's length. Angles are tenths of
 *       a degree, clockwise on screen (y grows down).
 *   KI8 DETERMINISTIC. Integer only, no float, no libc, no allocation; the
 *       springs run in fixed 4 ms steps with the remainder carried, so the
 *       same inputs give the same outputs whatever the frame cadence.
 *
 * Not thread safe: one instance belongs to the thread that draws it.
 */
#ifndef ZXV_ZUI_KINETIC_H
#define ZXV_ZUI_KINETIC_H

#include <stdbool.h>
#include <stdint.h>

#define ZUI_KINETIC_GROUPS     3
#define ZUI_KINETIC_ONE        65536 /* 1.0 in 16.16                     */
#define ZUI_KINETIC_PIVOT      512   /* default pivot, icon units         */
#define ZUI_KINETIC_SLOW_MS    4000u /* KI2 tempo periods                 */
#define ZUI_KINETIC_MEDIUM_MS  1600u
#define ZUI_KINETIC_FAST_MS    750u
#define ZUI_KINETIC_BREATH     983       /* 1.5 % of ONE                      */
#define ZUI_KINETIC_CHEST      6         /* L2 chest rise, icon units         */
#define ZUI_KINETIC_SHIMMER    3932      /* L3 grad_shift amplitude (6 %)     */
#define ZUI_KINETIC_LIFT       70779     /* 1.08 in 16.16                     */
#define ZUI_KINETIC_AIM_MAX    300       /* +/-30 degrees                     */
#define ZUI_KINETIC_CLICK      150       /* 15 degree escapement click        */
#define ZUI_KINETIC_FIRE_DD    60        /* dd per second per token/cycle     */
#define ZUI_KINETIC_FIRE_MAX   3600      /* dd per second: one turn           */
#define ZUI_KINETIC_STRAIN     80        /* 8 degrees                         */
#define ZUI_KINETIC_STRAIN_MS  300u      /* time to take up the strain        */
#define ZUI_KINETIC_JITTER     10        /* 1 degree                          */
#define ZUI_KINETIC_SPLIT      12        /* icon units each side              */
#define ZUI_KINETIC_SPLIT_MS   200u      /* time to part                      */
#define ZUI_KINETIC_GHOST      176       /* ghost opacity                     */
#define ZUI_KINETIC_PULSE      8         /* ghost opacity swing, /256         */
#define ZUI_KINETIC_QUARANTINE 0xFF8A1Fu /* amber; ZUI_GOLD is 0xF2C230      */
#define ZUI_KINETIC_TINT_BOTH  128u
#define ZUI_KINETIC_STALL_TINT 0xFF7A93u /* ZUI_ALERT, held constant        */
#define ZUI_KINETIC_TINT_STALL 40u
#define ZUI_KINETIC_FIRE_TINT  0xFFD24Du /* ZUI_GOLD_HI, reduce motion only */
#define ZUI_KINETIC_TINT_FIRE  48u

enum { ZUI_KINETIC_CHASSIS = 0, ZUI_KINETIC_CORE = 1, ZUI_KINETIC_ELEMENT = 2 };

typedef enum {
    ZUI_KINETIC_IDLE = 0,
    ZUI_KINETIC_HOVER,
    ZUI_KINETIC_FIRING,
    ZUI_KINETIC_STALL,
    ZUI_KINETIC_BOTH
} zui_kinetic_state_t;

typedef enum { ZUI_KINETIC_L1 = 1, ZUI_KINETIC_L2, ZUI_KINETIC_L3 } zui_kinetic_tier_t;

typedef enum {
    ZUI_KINETIC_NOT_RUNNING = 0, /* STILL  */
    ZUI_KINETIC_OPEN_IDLE,       /* SLOW   */
    ZUI_KINETIC_RUNNING,         /* MEDIUM */
    ZUI_KINETIC_NEEDS_INPUT      /* FAST   */
} zui_kinetic_activity_t;

/* KI6: the static activity badge. */
typedef enum {
    ZUI_KINETIC_PIP_NONE = 0, /* NOT_RUNNING */
    ZUI_KINETIC_PIP_DIM,      /* OPEN_IDLE   */
    ZUI_KINETIC_PIP_STEADY,   /* RUNNING     */
    ZUI_KINETIC_PIP_RING      /* NEEDS_INPUT: bright with a ring */
} zui_kinetic_pip_t;

/* KI7: the same shape as zui_vec's per-group override. */
typedef struct {
    const char *group; /* "chassis", "core-artifact", "kinetic-element" */
    int32_t m[6];      /* 16.16 affine [a b c d e f]                     */
    int32_t px, py;    /* pivot, 1/16 icon units                         */
    uint16_t opacity;  /* 0..256                                         */
    uint32_t tint;     /* XRGB8888                                       */
    uint16_t tint_amt; /* 0..256                                         */
    int32_t grad_shift;
} zui_kinetic_group_t;

typedef struct {
    zui_kinetic_group_t g[ZUI_KINETIC_GROUPS];     /* the draw              */
    zui_kinetic_group_t ghost[ZUI_KINETIC_GROUPS]; /* BOTH: the second draw */
    bool ghost_on;
    zui_kinetic_pip_t pip; /* KI6, always filled                  */
    uint32_t pip_color;
    uint16_t pip_opacity;
    bool pip_required; /* reduce motion: the badge must be drawn */
    bool redraw;       /* KI5                                    */
    bool animating;    /* later frames will differ with no new input */
} zui_kinetic_frame_t;

typedef struct {
    int32_t x; /* value                      */
    int32_t v; /* velocity, value/256 a step */
} zui_kinetic_spring_t;

typedef struct {
    zui_kinetic_state_t state;
    zui_kinetic_tier_t tier;
    zui_kinetic_activity_t act;
    bool escrow, visible, dirty, have_last, pointer_in;
    uint32_t now_ms;                /* the clock as last advanced  */
    uint32_t entered_ms;            /* when the state was entered  */
    uint32_t epoch_ms;              /* phase origin of the tempo   */
    uint32_t spring_rem;            /* ms not yet stepped (< 4)    */
    zui_kinetic_spring_t lift, aim; /* lift 16.16; aim dd * 64 */
    int32_t pivot_x, pivot_y;
    int32_t ptr_x, ptr_y;
    uint32_t work;     /* tokens per cycle            */
    uint32_t fire_acc; /* dd * 1000, mod 3600000      */
    int32_t stall_base;
    int32_t clicks;  /* L1 ambient clicks, dd       */
    uint32_t halves; /* half periods seen           */
    zui_kinetic_frame_t last;
} zui_kinetic_t;

/* ---- the instance -------------------------------------------------------- */

/* IDLE, NOT_RUNNING, visible, pivot at the centre, at rest. */
void zui_kinetic_init(zui_kinetic_t *k, zui_kinetic_tier_t tier, uint32_t now_ms);
/* KI4: false when BOTH is held (only zui_kinetic_settle leaves it) or bad. */
bool zui_kinetic_set_state(zui_kinetic_t *k, zui_kinetic_state_t s, uint32_t now_ms);
/* KI4: release BOTH into s (not BOTH). False when nothing was held. */
bool zui_kinetic_settle(zui_kinetic_t *k, zui_kinetic_state_t s, uint32_t now_ms);
/* KI2 */
void zui_kinetic_set_activity(zui_kinetic_t *k, zui_kinetic_activity_t a, uint32_t now_ms);
void zui_kinetic_set_visible(zui_kinetic_t *k, bool visible);
/* The pointer in icon units; inside false drops it (aim returns to rest). */
void zui_kinetic_set_pointer(zui_kinetic_t *k, int32_t x, int32_t y, bool inside);
void zui_kinetic_set_work(zui_kinetic_t *k, uint32_t tokens_per_cycle);
/* The kinetic element's pivot, icon units (the output's px, py are x 16). */
void zui_kinetic_set_pivot(zui_kinetic_t *k, int32_t x, int32_t y);

/* Advance to now_ms and fill out. Returns out->redraw. */
bool zui_kinetic_frame(zui_kinetic_t *k, uint32_t now_ms, zui_kinetic_frame_t *out);

/* ---- helpers (exposed for tests and other animations) -------------------- */

/* sin of an angle in tenths of a degree, 16.16 (-65536..65536). */
int32_t zui_kinetic_sin(int32_t dd);
int32_t zui_kinetic_cos(int32_t dd);
/* Angle of (dx, dy) in tenths of a degree, 0..3599, clockwise from +x on a
 * y-down screen; 0 for (0, 0). */
int32_t zui_kinetic_atan2(int32_t dy, int32_t dx);
/* KI2: the tempo period in ms, 0 for NOT_RUNNING. */
uint32_t zui_kinetic_period_ms(zui_kinetic_activity_t a);
/* KI4: firing rate in tenths of a degree per second. */
uint32_t zui_kinetic_fire_rate(uint32_t tokens_per_cycle);
/* One 4 ms critically damped step toward target. */
void zui_kinetic_spring_step(zui_kinetic_spring_t *s, int32_t target);

#endif /* ZXV_ZUI_KINETIC_H */
