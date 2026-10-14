/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_depth.h — depth on a screen, stated honestly.
 *
 * WHAT A FLAT PANEL CAN AND CANNOT DO
 * -----------------------------------
 * A hologram reconstructs a light field: each eye, from each position, gets a
 * different picture of the same object. An ordinary panel emits one picture to
 * both eyes, so no code turns it into a hologram. What it CAN carry are depth
 * cues, and this module implements the ones that actually work:
 *
 *   ZUI_DEPTH_FLAT         no cue. Byte-identical to drawing without depth.
 *   ZUI_DEPTH_CHROMA       chromostereopsis (emu/holo.h holo_shade): near goes
 *                          warm, far goes cool. Glasses-free, but weak, and
 *                          some viewers see it reversed (hence reverse_chroma).
 *   ZUI_DEPTH_PARALLAX     head-tracked motion parallax. The camera reports the
 *                          viewer's head offset; each layer shifts by its depth
 *                          so the screen behaves like a window. The strongest
 *                          glasses-free cue a single flat panel has.
 *   ZUI_DEPTH_ANAGLYPH_*   true stereo through filter glasses. This is the
 *                          colour / anti-colour idea made exact: the scene is
 *                          drawn once per eye, and each eye's picture is
 *                          carried in one colour and its complement. Least-
 *                          squares (Dubois) matrices in linear light keep the
 *                          ghosting low. Amber/blue keeps gold and skin tones
 *                          far better than red/cyan; red/cyan fits the glasses
 *                          most people already own.
 *
 * Real light-field panels, stereo monitors and headsets are where depth is
 * genuine; the present layer hands them per-eye or per-view pictures drawn
 * with the same zui_eye_shift(). Wording in UI and docs: "depth effect",
 * never "hologram", except on such hardware.
 *
 * DEPTH CARRIES MEANING
 * ---------------------
 * z is in pixels, positive toward the viewer. The house rule (tvl_stereo.h):
 * S+ (available, active) comes forward, S- (withdrawn) recedes, S0 (held,
 * undecided, e.g. an AI suggestion nobody accepted yet) sits exactly at the
 * glass: z == 0 shifts by exactly 0 in every mode.
 *
 * Integer only, freestanding, no allocation.
 */
#ifndef ZXV_ZUI_DEPTH_H
#define ZXV_ZUI_DEPTH_H

#include <stdint.h>
#include <stdbool.h>
#include "zui_canvas.h"

typedef enum {
    ZUI_DEPTH_FLAT = 0,
    ZUI_DEPTH_CHROMA,
    ZUI_DEPTH_PARALLAX,
    ZUI_DEPTH_ANAGLYPH_AMBER_BLUE,
    ZUI_DEPTH_ANAGLYPH_RED_CYAN
} zui_depth_mode_t;

typedef enum {
    ZUI_ROLE_HELD = 0,      /* S0: at the glass                 */
    ZUI_ROLE_FORWARD,       /* S+: toward the viewer            */
    ZUI_ROLE_BACK           /* S-: behind the glass             */
} zui_role_t;

typedef struct {
    zui_depth_mode_t mode;
    int32_t view_dist_px;   /* eye-to-screen distance, in screen pixels   */
    int32_t ipd_px;         /* eye separation, in screen pixels           */
    int32_t head_x, head_y; /* tracked head offset from centre, pixels    */
    int32_t chroma_k;       /* chromostereopsis strength, 0 = off         */
    bool    reverse_chroma; /* for viewers who see warm/cool backwards    */
} zui_depth_t;

/* Sensible defaults for a panel of the given pixel density: 60 cm viewing
 * distance, 63 mm eye separation. */
void zui_depth_init(zui_depth_t *d, zui_depth_mode_t mode, uint32_t px_per_cm);

/* z for a role; magnitude in pixels. HELD is 0 for every magnitude. */
int32_t zui_role_z(zui_role_t role, int32_t magnitude_px);

/* Where a point at depth z appears on the glass when the eye sits eye_x
 * pixels off centre: -eye_x * z / (V - z). Exact 0 for z == 0. Points behind
 * the glass move with the eye, points in front move against it. */
int32_t zui_eye_shift(const zui_depth_t *d, int32_t z, int32_t eye_x);

/* Per-layer offset for the current mode and eye (eye: -1 left, +1 right,
 * 0 for a single mono view). Covers parallax (head offset) and stereo. */
void zui_layer_offset(const zui_depth_t *d, int32_t z, int eye,
                      int32_t *dx, int32_t *dy);

/* Chromostereopsis tint for a pixel at depth z (identity at z == 0 or when
 * the mode does not use it). */
uint32_t zui_chroma_px(const zui_depth_t *d, uint32_t rgb, int32_t z);

/* Combine left- and right-eye pictures into one anaglyph frame. All three
 * surfaces must be the same size; out may alias neither input. Returns false
 * if the mode is not an anaglyph mode or the sizes differ. */
bool zui_anaglyph(const zui_depth_t *d, zui_surface_t *out,
                  const zui_surface_t *left, const zui_surface_t *right);

/* One pixel of the same transform, for tests and shaders. */
uint32_t zui_anaglyph_px(zui_depth_mode_t mode, uint32_t l, uint32_t r);

#endif /* ZXV_ZUI_DEPTH_H */
