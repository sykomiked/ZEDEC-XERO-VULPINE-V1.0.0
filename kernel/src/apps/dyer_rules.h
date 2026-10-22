/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* dyer_rules.h — Dyer's rules in C, the reference its KRON, Trebago, Eve
 * and MYRA blocks (blocks/dyer) are checked against. Pure integer
 * functions, no state: the model (dyer_model.c) and the language test
 * (tests/dyer_zxapp.inc) both include this, so the languages are tested
 * against the very C the model runs. A channel is 0..255, a hue 0..359
 * degrees, a saturation or lightness 0..1000 thousandths. Every rounding
 * is to the nearest, a half up, unless it says floor.
 *
 *   DY1 RGB TO HSL (rgb_to_hsl, part 1 hue, 2 saturation, 3 lightness).
 *       With mx and mn the largest and smallest channel and d = mx - mn:
 *       lightness = (200 (mx + mn) + 51) / 102 rounded down, which is
 *       1000 (mx + mn) / 510 to the nearest; a grey (d = 0) has hue 0 and
 *       saturation 0; otherwise saturation = (2000 d + den) / (2 den),
 *       den = 255 - |mx + mn - 255|, and hue = (2 y + d) / (2 d) mod 360,
 *       y = 360 d + 60 (g - b) when red is largest, 120 d + 60 (b - r) when
 *       green is (red not), else 240 d + 60 (r - g). A channel outside
 *       0..255 or a part outside 1..3 is refused (DYER_REFUSED).
 *   DY2 HSL TO RGB (hsl_to_rgb, part 1 red, 2 green, 3 blue), the CSS
 *       Color 4 form in thirtieths of a degree: a = s min(l, 1000 - l),
 *       k = (n + h) mod 360 with n 0 for red, 240 for green and 120 for
 *       blue, t = max(-30, min(k - 90, 270 - k, 30)), q = 30000 l - a t
 *       (the channel's level times 30000000), and the channel is
 *       (17 q + 1000000) / 2000000 rounded down, which is 255 q / 30000000
 *       to the nearest. A hue outside 0..359, a saturation or lightness
 *       outside 0..1000 or a part outside 1..3 is refused.
 *   DY3 RELATIVE LUMINANCE (rel_luminance), in millionths:
 *       floor((2126 R + 7152 G + 722 B) / 10000), R, G and B the channels'
 *       linear light in millionths from DYER_SRGB (dyer_srgb_linear). A
 *       channel outside 0..255 is refused.
 *   DY4 CONTRAST RATIO x100 (contrast_ratio_x100) of two luminances in
 *       millionths, either one the lighter: floor(100 (lighter + 50000) /
 *       (darker + 50000)), 100 for one colour on itself up to 2100 for
 *       white on black. A luminance outside 0..1000000 is refused.
 *   DY5 CONTRAST GRADE (contrast_grade) of a ratio x100: 2 at
 *       DYER_PASS_TEXT (450) or more, enough for body text; 1 at
 *       DYER_PASS_LARGE (300) to 449, enough for large text and the parts
 *       of a screen only; 0 below 300. A ratio outside 100..2100, which no
 *       two colours make, is refused.
 *   DY6 A SWATCH (swatch_ok) of a palette: its place 0..63 and its colour
 *       0..16777215 (0xRRGGBB). Each field against its own range (Eve);
 *       the ^ZXDYER record (MYRA) holds place in 2 digits and colour in 8,
 *       sign aside, so it fits wider than the ranges (dyer_record_fits).
 *
 * DYER_SRGB is the sRGB transfer function of IEC 61966-2-1 at the 256
 * channel values, in millionths, rounded to the nearest once, when the
 * table was made: c / 255 / (1292 / 100) for c / 255 at most 4045 / 100000,
 * else ((c / 255 + 55 / 1000) / (1055 / 1000)) to the power 12 / 5. The
 * same 256 numbers are blocks/dyer/lum.kron's t and lum.tb's t. */
#ifndef DYER_RULES_H
#define DYER_RULES_H

#include <stdbool.h>
#include <stdint.h>

#define DYER_REFUSED     (-1)
#define DYER_CHANNEL_MAX 255
#define DYER_HUE_MAX     359
#define DYER_UNIT        1000     /* DY1, DY2: saturation and lightness */
#define DYER_LUM_MAX     1000000  /* DY3: white */
#define DYER_FLARE       50000    /* DY4: the 5 % added to each side */
#define DYER_RATIO_MIN   100      /* DY4: one colour on itself */
#define DYER_RATIO_MAX   2100     /* DY4: white on black */
#define DYER_PASS_TEXT   450      /* DY5: body text */
#define DYER_PASS_LARGE  300      /* DY5: large text, parts of the screen */
#define DYER_PLACES      64       /* DY6: places 0..63 */
#define DYER_COLOR_MAX   16777215 /* DY6: 0xFFFFFF */

enum { DYER_HUE = 1, DYER_SATURATION = 2, DYER_LIGHTNESS = 3 };
enum { DYER_RED = 1, DYER_GREEN = 2, DYER_BLUE = 3 };

static inline bool dyer_channel_ok(int64_t c)
{
    return c >= 0 && c <= DYER_CHANNEL_MAX;
}

/* DY3: a channel's linear light in millionths; DYER_REFUSED outside 0..255. */
static inline int64_t dyer_srgb_linear(int64_t c)
{
    static const int32_t DYER_SRGB[DYER_CHANNEL_MAX + 1] = {
        0,      304,    607,     911,    1214,   1518,   1821,   2125,   2428,   2732,   3035,
        3347,   3677,   4025,    4391,   4777,   5182,   5605,   6049,   6512,   6995,   7499,
        8023,   8568,   9134,    9721,   10330,  10960,  11612,  12286,  12983,  13702,  14444,
        15209,  15996,  16807,   17642,  18500,  19382,  20289,  21219,  22174,  23153,  24158,
        25187,  26241,  27321,   28426,  29557,  30713,  31896,  33105,  34340,  35601,  36889,
        38204,  39546,  40915,   42311,  43735,  45186,  46665,  48172,  49707,  51269,  52861,
        54480,  56128,  57805,   59511,  61246,  63010,  64803,  66626,  68478,  70360,  72272,
        74214,  76185,  78187,   80220,  82283,  84376,  86500,  88656,  90842,  93059,  95307,
        97587,  99899,  102242,  104616, 107023, 109462, 111932, 114435, 116971, 119538, 122139,
        124772, 127438, 130136,  132868, 135633, 138432, 141263, 144128, 147027, 149960, 152926,
        155926, 158961, 162029,  165132, 168269, 171441, 174647, 177888, 181164, 184475, 187821,
        191202, 194618, 198069,  201556, 205079, 208637, 212231, 215861, 219526, 223228, 226966,
        230740, 234551, 238398,  242281, 246201, 250158, 254152, 258183, 262251, 266356, 270498,
        274677, 278894, 283149,  287441, 291771, 296138, 300544, 304987, 309469, 313989, 318547,
        323143, 327778, 332452,  337164, 341914, 346704, 351533, 356400, 361307, 366253, 371238,
        376262, 381326, 386429,  391572, 396755, 401978, 407240, 412543, 417885, 423268, 428690,
        434154, 439657, 445201,  450786, 456411, 462077, 467784, 473531, 479320, 485150, 491021,
        496933, 502886, 508881,  514918, 520996, 527115, 533276, 539479, 545724, 552011, 558340,
        564712, 571125, 577580,  584078, 590619, 597202, 603827, 610496, 617207, 623960, 630757,
        637597, 644480, 651406,  658375, 665387, 672443, 679542, 686685, 693872, 701102, 708376,
        715694, 723055, 730461,  737910, 745404, 752942, 760525, 768151, 775822, 783538, 791298,
        799103, 806952, 814847,  822786, 830770, 838799, 846873, 854993, 863157, 871367, 879622,
        887923, 896269, 904661,  913099, 921582, 930111, 938686, 947307, 955973, 964686, 973445,
        982251, 991102, 1000000,
    };
    return dyer_channel_ok(c) ? DYER_SRGB[c] : DYER_REFUSED;
}

static inline int64_t dyer_max3(int64_t a, int64_t b, int64_t c)
{
    int64_t m = a > b ? a : b;
    return m > c ? m : c;
}

static inline int64_t dyer_min3(int64_t a, int64_t b, int64_t c)
{
    int64_t m = a < b ? a : b;
    return m < c ? m : c;
}

/* DY1 */
static inline int64_t dyer_hsl_part(int64_t r, int64_t g, int64_t b, int64_t part)
{
    if (!dyer_channel_ok(r) || !dyer_channel_ok(g) || !dyer_channel_ok(b) || part < DYER_HUE ||
        part > DYER_LIGHTNESS)
        return DYER_REFUSED;
    int64_t mx = dyer_max3(r, g, b), mn = dyer_min3(r, g, b), d = mx - mn;
    if (part == DYER_LIGHTNESS) return (200 * (mx + mn) + 51) / 102;
    if (!d) return 0;
    if (part == DYER_SATURATION) {
        int64_t far = mx + mn - 255, den = 255 - (far < 0 ? -far : far);
        return (2000 * d + den) / (2 * den);
    }
    int64_t y = r == mx   ? 360 * d + 60 * (g - b)
                : g == mx ? 120 * d + 60 * (b - r)
                          : 240 * d + 60 * (r - g);
    return (2 * y + d) / (2 * d) % 360;
}

/* DY2 */
static inline int64_t dyer_rgb_part(int64_t h, int64_t s, int64_t l, int64_t part)
{
    if (h < 0 || h > DYER_HUE_MAX || s < 0 || s > DYER_UNIT || l < 0 || l > DYER_UNIT ||
        part < DYER_RED || part > DYER_BLUE)
        return DYER_REFUSED;
    int64_t n = part == DYER_RED ? 0 : part == DYER_GREEN ? 240 : 120;
    int64_t k = (n + h) % 360, t = k - 90;
    if (270 - k < t) t = 270 - k;
    if (t > 30) t = 30;
    if (t < -30) t = -30;
    int64_t q = 30000 * l - s * (l < DYER_UNIT - l ? l : DYER_UNIT - l) * t;
    return (17 * q + 1000000) / 2000000;
}

/* DY3 */
static inline int64_t dyer_luminance(int64_t r, int64_t g, int64_t b)
{
    if (!dyer_channel_ok(r) || !dyer_channel_ok(g) || !dyer_channel_ok(b)) return DYER_REFUSED;
    return (2126 * dyer_srgb_linear(r) + 7152 * dyer_srgb_linear(g) + 722 * dyer_srgb_linear(b)) /
           10000;
}

/* DY4 */
static inline int64_t dyer_contrast_x100(int64_t fore, int64_t back)
{
    if (fore < 0 || fore > DYER_LUM_MAX || back < 0 || back > DYER_LUM_MAX) return DYER_REFUSED;
    int64_t hi = fore > back ? fore : back, lo = fore > back ? back : fore;
    return 100 * (hi + DYER_FLARE) / (lo + DYER_FLARE);
}

/* DY5 */
static inline int64_t dyer_contrast_grade(int64_t ratio)
{
    if (ratio < DYER_RATIO_MIN || ratio > DYER_RATIO_MAX) return DYER_REFUSED;
    return (ratio >= DYER_PASS_TEXT) + (ratio >= DYER_PASS_LARGE);
}

/* DY6: the ranges (Eve) */
static inline bool dyer_swatch_ok(int64_t place, int64_t color)
{
    return place >= 0 && place < DYER_PLACES && color >= 0 && color <= DYER_COLOR_MAX;
}

/* DY6: the ^ZXDYER record's digits (MYRA), sign aside */
static inline bool dyer_record_fits(int64_t place, int64_t color)
{
    return place > -100 && place < 100 && color > -100000000 && color < 100000000;
}

#endif
