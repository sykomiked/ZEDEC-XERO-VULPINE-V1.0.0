/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* gesso_rules.h — Gesso's rules in C, the reference its KRON, C++ (as
 * WebAssembly), Trebago, Eve, Sutra and MYRA blocks (blocks/gesso) are
 * checked against, beside the C of the entries it uses from Hollow and
 * Witness (their own rules headers). Pure integer functions, no state: the
 * model (gesso_model.c) and the language test (tests/gesso_zxapp.inc) both
 * include this, so the languages are tested against the very C the model
 * runs. A channel value (a pixel of one channel) is 0..255.
 *
 *   GR1 BRIGHTNESS (pixel_bright): px + delta, held inside 0..255. px
 *       0..255 and delta -255..255, anything else refused (GESSO_REFUSED).
 *   GR2 CONTRAST (pixel_contrast): about the middle grey 128, by a gain in
 *       hundredths of a percent (10000 leaves the pixel as it is, 0 makes
 *       it 128): 128 + floor((px - 128) * gain / 10000), the floor toward
 *       minus infinity, held inside 0..255. px 0..255 and gain
 *       0..GESSO_GAIN_MAX (4 times), anything else refused.
 *   GR3 BLEND (pixel_blend): top over base at alpha (0 is all base, 255
 *       all top): floor((base * (255 - alpha) + top * alpha) / 255), which
 *       is inside 0..255 whenever its inputs are. Each 0..255, anything
 *       else refused.
 *   GR4 PIXELS IN A RANGE (pixel_range_count): how many of a row of pixels
 *       lie in lo..hi, both ends counted (one lo and one hi for the whole
 *       row); none when lo > hi. A histogram's bin is the range of its
 *       levels; one level's count is witness.kind_count.
 *   GR5 BRUSH LIMITS (brush_limits_ok): a brush 1..GESSO_BRUSH_MAX (512)
 *       pixels across, an opacity 0..255 and a hardness 0..100 percent.
 *       Each field against its own range.
 *   GR6 CANVAS LIMITS (canvas_limits_ok): a canvas 1..GESSO_SIDE_MAX
 *       (2048) pixels wide and high. Each side against its own range.
 *   GR7 A LAYER RECORD of ^ZXGESSO holds integers of at most the digits its
 *       pieces declare, sign aside (blocks/gesso/layer.myra): layer 1,
 *       kind 1, amount 5, opacity 3, visible 1. What a layer may hold
 *       (gesso_layer_ok) is the model's, inside those digits.
 *
 * The brush clips to the canvas with Hollow's shown (the pixels of a span
 * pos..pos+size-1 on a side 0..span-1), and the pixels a change held at 0
 * or 255 are counted with Witness's kind_count. */
#ifndef GESSO_RULES_H
#define GESSO_RULES_H

#include <stdbool.h>
#include <stdint.h>

#include "hollow_rules.h"
#include "witness_rules.h"

#define GESSO_REFUSED    (-1)
#define GESSO_PX_MAX     255   /* a channel's top value */
#define GESSO_MID        128   /* GR2: the grey contrast turns about */
#define GESSO_UNITY      10000 /* GR2: a gain of 100.00 % */
#define GESSO_GAIN_MAX   40000 /* GR2: 4 times */
#define GESSO_BRUSH_MAX  512   /* GR5: pixels across */
#define GESSO_HARD_MAX   100   /* GR5: percent */
#define GESSO_SIDE_MAX   2048  /* GR6: pixels a side */
#define GESSO_AMOUNT_MAX 99999 /* GR7: amount fits 5 digits */

/* GR7: what a layer does */
enum {
    GESSO_LAYER_BRIGHT = 1,   /* amount: GR1's delta */
    GESSO_LAYER_CONTRAST = 2, /* amount: GR2's gain */
    GESSO_LAYER_WASH = 3,     /* amount: a grey level laid over every pixel */
};

/* GR1 */
static inline int64_t gesso_bright_px(int64_t px, int64_t delta)
{
    if (px < 0 || px > GESSO_PX_MAX || delta < -GESSO_PX_MAX || delta > GESSO_PX_MAX)
        return GESSO_REFUSED;
    int64_t v = px + delta;
    return v < 0 ? 0 : v > GESSO_PX_MAX ? GESSO_PX_MAX : v;
}

/* GR2 */
static inline int64_t gesso_contrast_px(int64_t px, int64_t gain)
{
    if (px < 0 || px > GESSO_PX_MAX || gain < 0 || gain > GESSO_GAIN_MAX) return GESSO_REFUSED;
    int64_t p = (px - GESSO_MID) * gain;
    int64_t q = p / GESSO_UNITY;
    if (p % GESSO_UNITY != 0 && p < 0) q -= 1;
    int64_t v = GESSO_MID + q;
    return v < 0 ? 0 : v > GESSO_PX_MAX ? GESSO_PX_MAX : v;
}

/* GR3 */
static inline int64_t gesso_blend_px(int64_t base, int64_t top, int64_t alpha)
{
    if (base < 0 || base > GESSO_PX_MAX || top < 0 || top > GESSO_PX_MAX || alpha < 0 ||
        alpha > GESSO_PX_MAX)
        return GESSO_REFUSED;
    return (base * (GESSO_PX_MAX - alpha) + top * alpha) / GESSO_PX_MAX;
}

/* GR4 */
static inline int64_t gesso_range_count(const int64_t *px, uint32_t n, int64_t lo, int64_t hi)
{
    int64_t c = 0;
    if (!px) return 0;
    for (uint32_t i = 0; i < n; i++) c += px[i] >= lo && px[i] <= hi;
    return c;
}

/* GR5 */
static inline bool gesso_brush_ok(int64_t size, int64_t opacity, int64_t hardness)
{
    return size >= 1 && size <= GESSO_BRUSH_MAX && opacity >= 0 && opacity <= GESSO_PX_MAX &&
           hardness >= 0 && hardness <= GESSO_HARD_MAX;
}

/* GR6 */
static inline bool gesso_canvas_ok(int64_t width, int64_t height)
{
    return width >= 1 && width <= GESSO_SIDE_MAX && height >= 1 && height <= GESSO_SIDE_MAX;
}

/* GR7 */
static inline bool gesso_record_fits(int64_t layer, int64_t kind, int64_t amount, int64_t opacity,
                                     int64_t visible)
{
    return witness_digits_fit(layer, 1) && witness_digits_fit(kind, 1) &&
           witness_digits_fit(amount, 5) && witness_digits_fit(opacity, 3) &&
           witness_digits_fit(visible, 1);
}

/* What a layer may hold, inside GR7's digits: a kind, the amount that kind
 * takes (GR1's delta, GR2's gain, a grey level), an opacity 0..255 and
 * visible 0 or 1. */
static inline bool gesso_layer_ok(int64_t kind, int64_t amount, int64_t opacity, int64_t visible)
{
    bool amount_ok =
        (kind == GESSO_LAYER_BRIGHT && amount >= -GESSO_PX_MAX && amount <= GESSO_PX_MAX) ||
        (kind == GESSO_LAYER_CONTRAST && amount >= 0 && amount <= GESSO_GAIN_MAX) ||
        (kind == GESSO_LAYER_WASH && amount >= 0 && amount <= GESSO_PX_MAX);
    return amount_ok && opacity >= 0 && opacity <= GESSO_PX_MAX && (visible == 0 || visible == 1);
}

#endif
