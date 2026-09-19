/* tvl_stereo.h — TOL VOVINA UPAAH LOT: tri-space stereo depth
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 *
 * WHAT THIS IS
 * ------------
 * Depth that MEANS something. Everywhere else in this tree, tri_role_t
 * (trispace.h) says what an artifact IS: S+ what it does, S- its inverse /
 * withdrawal, S0 the unresolved remainder. This module makes that triad
 * VISIBLE as parallax, so a user reads an object's tri-space role off the
 * screen without a label:
 *
 *     TRI_POSITIVE  ->  disparity > 0   protrudes toward the viewer  (available)
 *     TRI_NEGATIVE  ->  disparity < 0   recedes behind the glass     (withdrawable)
 *     TRI_NEUTRAL   ->  disparity == 0  EXACTLY the screen plane     (held)
 *
 * The S0 case is the load-bearing one. A held, undecided thing must sit
 * exactly AT the glass — not "nearly" flat, not "one pixel" of drift.
 * tvl_disparity(TRI_NEUTRAL, m) returns 0 for EVERY magnitude m, and the
 * whole compositor is built so that disparity 0 reproduces the non-stereo
 * pixel BYTE FOR BYTE. An unresolved thing that shimmered a pixel forward
 * would be asserting a decision nobody made.
 *
 * THE TECHNIQUE (and what it is NOT)
 * ----------------------------------
 * We do COMPLEMENTARY-CHANNEL stereo on one ordinary framebuffer. Each pixel
 * of the source is split into two colour halves along a themed separation
 * axis (a warm anchor half and its exact complement); the two halves are
 * sampled from positions offset in opposite directions by half the disparity,
 * then recombined. One buffer, no headset, no second render target, and it
 * degrades to a flat image by construction.
 *
 * ref: the Virtual Boy's emissive red-on-black look is the flavour we are
 * after. It is NOT the technique we use, and the two must not be conflated in
 * anything written about this file. That machine did TRUE PER-EYE stereo:
 * dual red LED arrays scanned by oscillating mirrors, one independent image
 * per eye, inside a headset. We have one framebuffer and no optics. What we
 * borrow is the LOOK (high-contrast emissive palette, depth used as a
 * first-class signal), not the display.
 *
 * WHY SPLIT/MERGE IS THE FLATNESS PROOF
 * -------------------------------------
 * tvl_split() divides a colour into anchor + complement so that, per channel,
 * anchor[c] + complement[c] == source[c] EXACTLY (the complement is defined as
 * the remainder, not as an independently rounded weighting). So tvl_merge() of
 * a split is the identity for every one of the 2^24 colours. At zero disparity
 * both halves are sampled from the same pixel, so the composite collapses
 * through that identity back to the original colour. Flatness is therefore an
 * algebraic property of the decomposition, not a tolerance we test for.
 *
 * WHAT IT REUSES RATHER THAN REIMPLEMENTS
 * ---------------------------------------
 *   shimmer.h  shimmer_shade() — the living-surface colour modulator. It owns
 *              lightness lift and the gold glint; this module never touches
 *              those. Shimmer is evaluated at the SAMPLE position of each eye
 *              half (it is a property of the surface, not of the output
 *              pixel), which is why the row walker calls it rather than the
 *              caller. Shimmer carries no depth term, so there is no overlap.
 *   emu/holo.h holo_shade() — chromostereopsis (warm advances, cool recedes).
 *              Already integer-only, already inlinable, already self-checked.
 *              We drive it with v=0 (shimmer owns the valence channel) and
 *              depth_z = disparity, and it is an exact identity at depth 0.
 *   theme.h    every colour comes from a theme slot. There is not one magic
 *              RGB literal in tvl_stereo.c: the separation axis is DERIVED
 *              from THEME_EMBER vs THEME_AZURE, the emissive bloom target from
 *              THEME_ACCENT and the crush floor from THEME_VOID. Re-theme the
 *              desktop and the stereo axis re-derives with it.
 *
 * Freestanding: integer only, no float, no libc, no allocation, no static
 * tables beyond what theme/shimmer already own. Deterministic — the same
 * inputs and the same shimmer phase always produce the same bytes, which is
 * what the AI replay path and dimfold losslessness require.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)  (TOL VOVINA tri-space stereo slice)
 */
#ifndef ZXV_TVL_STEREO_H
#define ZXV_TVL_STEREO_H

#include <stdint.h>
#include <stdbool.h>

#include "trispace.h"        /* tri_role_t — the semantic source of truth */
#include "theme.h"           /* the palette, as override-able tokens      */
#include "shimmer.h"         /* the per-pixel colour modulator we call    */

/* Disparity is in PIXELS and is clamped here. Beyond about this much the two
 * halves stop fusing and read as two pictures rather than one deep one; the
 * clamp is a legibility rail, not an arithmetic one. */
#define TVL_DISPARITY_MAX   64

/* Graceful degradation, in the same spirit as hc_display_mode_t: the depth
 * language must survive on a surface that cannot carry it. Every rung below
 * FULL removes a cue; FLAT is defined to be bit-identical to no stereo at all. */
typedef enum {
    TVL_STEREO_FULL = 0,    /* channel separation + chromostereopsis      */
    TVL_STEREO_CHROMA,      /* warm/cool depth tint only, no separation   */
    TVL_STEREO_FLAT         /* no depth cue; identical to the flat path   */
} tvl_stereo_mode_t;

typedef struct {
    /* THE SEPARATION AXIS. anchor_w[c] is how much of channel c belongs to the
     * anchor (near-eye) half, 0..255; the complement half takes the exact
     * remainder. Derived from theme slots by tvl_stereo_set_theme(), never
     * written as a literal. */
    uint8_t  anchor_w[3];   /* [0]=R [1]=G [2]=B */

    uint8_t  separation;    /* 0..255 global strength; 0 == flat, 255 == full */
    uint8_t  chroma_k;      /* chromostereopsis strength for holo_shade; 0=off */
    uint8_t  emissive;      /* 0..255 emissive/high-contrast shaping; 0=off    */

    uint32_t glow_rgb;      /* themed bloom target (THEME_ACCENT by default)   */
    uint32_t void_rgb;      /* themed crush floor  (THEME_VOID  by default)    */

    tvl_stereo_mode_t mode;

    /* Optional living-surface modulator. NULL means "no shimmer"; the module
     * works fully without one. Borrowed, never owned, never advanced here —
     * shimmer_advance() belongs to the event loop, not to a compositor. */
    const shimmer_t *field;

    uint64_t pixels;        /* stat: pixels composited */
} tvl_stereo_t;

/* ---- setup ---------------------------------------------------------------
 * theme may be NULL, in which case the default house palette is used. */
void tvl_stereo_init(tvl_stereo_t *s, const theme_t *theme);
void tvl_stereo_set_theme(tvl_stereo_t *s, const theme_t *theme);
void tvl_stereo_set_field(tvl_stereo_t *s, const shimmer_t *field);
void tvl_stereo_set_mode(tvl_stereo_t *s, tvl_stereo_mode_t mode);
void tvl_stereo_set_separation(tvl_stereo_t *s, uint8_t separation);
void tvl_stereo_set_emissive(tvl_stereo_t *s, uint8_t emissive);

/* max(anchor_w) - min(anchor_w). A theme whose EMBER and AZURE slots are the
 * same hue yields a spread near 0: the two halves then differ only in
 * brightness, and the separation reads as ghosting rather than as depth. That
 * is a theming error, not a code error, so it is REPORTED rather than
 * silently corrected — nothing here overrides the user's palette. */
uint8_t tvl_stereo_axis_spread(const tvl_stereo_t *s);

/* ---- THE SEMANTIC MAPPING ------------------------------------------------
 * Turn a tri-space role plus a magnitude into a signed disparity.
 *
 * TRI_NEUTRAL YIELDS EXACTLY 0 FOR EVERY MAGNITUDE. That is not a rounding
 * outcome and not a special case bolted on afterwards — it is the contract.
 * S0 is the unresolved remainder; a held thing sits flat at the glass because
 * nothing has yet decided whether it comes forward or withdraws. Any nonzero
 * value here would render an assertion the triad never made. An unrecognised
 * role is treated as held for the same reason: silence is not a claim.
 *
 * TRI_POSITIVE and TRI_NEGATIVE are exact negations of one another at equal
 * magnitude, so "available" and "withdrawable" are mirror depths, not two
 * independently tuned effects. */
int32_t tvl_disparity(tri_role_t role, uint32_t magnitude);

/* The depth_z fed to chromostereopsis for a role. Same sign discipline; kept
 * separate so a caller can tint depth without displacing channels. */
int32_t tvl_depth_z(tri_role_t role, uint32_t magnitude);

/* ---- channel separation --------------------------------------------------
 * anchor + complement == argb, per channel, exactly, for every input. The
 * anchor half carries the source alpha; the complement half carries alpha 0,
 * so the saturating merge restores it untouched. */
void     tvl_split(const tvl_stereo_t *s, uint32_t argb,
                   uint32_t *anchor_out, uint32_t *complement_out);
uint32_t tvl_merge(uint32_t anchor, uint32_t complement);

/* ---- compositing ---------------------------------------------------------
 * center_argb is what the pixel would be with no stereo at all; anchor_argb
 * and complement_argb are the two eye samples. Pass all three equal (and any
 * disparity, or 0) to get the flat pixel back byte-for-byte.
 *
 * The caller supplies the samples because only the caller knows the source
 * geometry; tvl_stereo_row() below does the sampling for the common case. */
uint32_t tvl_stereo_compose(const tvl_stereo_t *s,
                            uint32_t center_argb,
                            uint32_t anchor_argb,
                            uint32_t complement_argb,
                            int32_t  disparity);

/* The non-stereo reference path: shimmer + emissive shaping, no depth cue.
 * This is the thing TRI_NEUTRAL must match exactly, and the selfcheck says so. */
uint32_t tvl_stereo_flat_px(const tvl_stereo_t *s, uint32_t base_argb,
                            int32_t x, int32_t y);

/* One scanline. `disp` may be NULL for a uniform `disparity` across the row,
 * or a per-pixel map (in which case `disparity` is ignored). Sampling clamps
 * at the row edges. src and dst must not alias. Returns pixels written. */
uint32_t tvl_stereo_row(tvl_stereo_t *s,
                        const uint32_t *src, const int16_t *disp,
                        uint32_t *dst, uint32_t width,
                        int32_t y, int32_t disparity);

/* A whole surface at ONE tri-space role — the common UI case: a window, a
 * place-portal or a card whose availability is a single tri_role_t. Width and
 * height come from the caller; nothing here assumes a resolution. */
uint32_t tvl_stereo_blit(tvl_stereo_t *s,
                         const uint32_t *src, uint32_t *dst,
                         uint32_t w, uint32_t h,
                         tri_role_t role, uint32_t magnitude);

/* The same surface down the flat path, for comparison and for TVL_STEREO_FLAT
 * consumers. */
uint32_t tvl_stereo_blit_flat(tvl_stereo_t *s,
                              const uint32_t *src, uint32_t *dst,
                              uint32_t w, uint32_t h);

/* ---- selfcheck -----------------------------------------------------------
 * Returns 1 on pass, 0 on failure (the tree's *_selfcheck convention). If
 * fail_mask is non-NULL it receives a bitmask of TVL_CHK_* for exactly which
 * properties broke, so a failure names itself instead of just saying "0". */
#define TVL_CHK_NEUTRAL_ZERO    (1u << 0)  /* S0 disparity was not exactly 0    */
#define TVL_CHK_MIRROR          (1u << 1)  /* S+ / S- not equal-and-opposite    */
#define TVL_CHK_ROUNDTRIP       (1u << 2)  /* split/merge lost a colour         */
#define TVL_CHK_FLAT_IDENTICAL  (1u << 3)  /* S0 pixel != non-stereo pixel      */
#define TVL_CHK_SEPARATES       (1u << 4)  /* nonzero disparity did nothing     */
#define TVL_CHK_ANTISYMMETRY    (1u << 5)  /* +d and -d not mirror composites   */

int tvl_stereo_selfcheck_detail(uint32_t *fail_mask);
int tvl_stereo_selfcheck(void);

#endif /* ZXV_TVL_STEREO_H */
