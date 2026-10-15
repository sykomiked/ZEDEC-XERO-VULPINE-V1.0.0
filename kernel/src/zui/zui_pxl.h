/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_pxl.h — the paraconsistent pixel filter: depth from two axes per pixel.
 *
 * The owner's rule (2026-10-12): two axioms make a flat screen look deep.
 *
 *   axis A  COLOUR <-> ANTI-COLOUR   a pixel leans into its own hue (more
 *                                    saturated) or toward its complement
 *   axis B  TINT   <-> SHADE         a pixel lifts toward light or sinks
 *                                    toward the void
 *
 * How the two combine in each pixel is the depth effect. It is an optical
 * illusion on a 2D panel (aerial perspective, tint/shade relief and
 * complementary contrast), not a hologram, and it needs no GPU: one small
 * lookup table and a handful of integer operations per pixel, run once per
 * frame at the present step, the same on Metal, Vulkan, D3D or a bare-metal
 * framebuffer.
 *
 * WHERE THE NUMBERS COME FROM
 * Draw calls stamp a PLANE beside the colour surface: for each pixel, the
 * 6-bit paraconsistent tag of the thing drawn there (lpres_hex.h) and its
 * depth z (-127..127, + toward the viewer). The filter then reads, per pixel:
 *
 *   1. the LUT entry for (tag, z): how far along each axis this state sits.
 *      TRUE comes forward: tint + its own colour. FALSE recedes: shade +
 *      anti-colour. NEITHER (held, at the glass) is untouched. BOTH is a glut:
 *      colour and anti-colour at once, interleaved pixel by pixel so the eye
 *      sees a lustre rather than either. Polarity scales strength (entropic
 *      weak, FORBIDDEN flat), phase scales how far it has travelled.
 *   2. the slope of the plane: where depth rises toward the light, the pixel
 *      is tinted and leans to its colour; where it falls away, shaded and
 *      leaning to the anti-colour. Raised things get a lit edge and a shadow
 *      edge, a warm fringe and a cool one, with no geometry at all. The light
 *      direction follows the head, the pointer or the device's tilt.
 *
 * Integer only, freestanding, no allocation; the caller owns every buffer.
 * Pixels whose tag is NEITHER at z 0 on flat ground are skipped, so a mostly
 * flat screen costs little.
 */
#ifndef ZXV_ZUI_PXL_H
#define ZXV_ZUI_PXL_H

#include <stdint.h>
#include <stdbool.h>
#include "zui_canvas.h"
#include "lpres_hex.h"

/* The plane: one tag byte and one depth byte per pixel. */
typedef struct {
    uint8_t *tag; /* lpx_t per pixel                       */
    int8_t *z;    /* depth per pixel, + toward the viewer   */
    int32_t w, h, stride;
} zui_plane_t;

void zui_plane_init(zui_plane_t *p, uint8_t *tag, int8_t *z, int32_t w, int32_t h, int32_t stride);
/* Everything back to the glass: NEITHER / neutral / manifest, z 0. */
void zui_plane_clear(zui_plane_t *p);
/* Stamp a (rounded) rectangle with a tag and a depth (clamped to +-127). */
void zui_plane_mark(zui_plane_t *p, zui_rect_t r, int32_t radius, lpx_t tag, int32_t z);

/* Video, photos, anything drawn without its own depth: stamp the rect with
 * the clip's tag and take z from each pixel's brightness (brighter reads
 * nearer, an old painter's cue), scaled by z_k (0..256, 256 = +-64). Run it
 * per frame after the frame is drawn into s; integer, one pass. */
void zui_plane_mark_luma(zui_plane_t *p, const zui_surface_t *s, zui_rect_t r, lpx_t tag,
                         int32_t z_k);

/* One LUT entry: a = colour (+) / anti-colour (-), b = tint (+) / shade (-),
 * each -256..256; glut = BOTH, interleave the sign of a. */
typedef struct {
    int16_t a, b;
    uint8_t glut;
    uint8_t edge; /* bevel width 1..3 px: 1 angular, 3 round */
    uint8_t tsh;  /* lustre speed: tick >> tsh (brief fast, lasting slow) */
} zui_pxl_cell_t;

#define ZUI_PXL_ZBINS 32 /* z quantised to 32 bins of 8 */
typedef struct {
    zui_pxl_cell_t cell[64][ZUI_PXL_ZBINS];
    int32_t edge_k; /* slope gain, 0..256 */
} zui_pxl_lut_t;

/* Build the table once. strength 0..256 scales everything (the user's depth
 * slider); 0 makes the filter the identity. */
void zui_pxl_build(zui_pxl_lut_t *lut, int32_t strength);

/* The filter. light_x / light_y give where the light comes from, -256..256
 * each (0,0 = straight on; the default top-left is -181,-181). tick drives
 * the glut interleave so BOTH shimmers slowly; pass 0 for a still frame. */
void zui_pxl_apply(zui_surface_t *s, const zui_plane_t *p, const zui_pxl_lut_t *lut,
                   int32_t light_x, int32_t light_y, uint32_t tick);

/* The same over rows [y0, y1) only. Bands never write outside their rows
 * and only read the plane, so a host may run one band per core at once and
 * get exactly the full-frame result. */
void zui_pxl_apply_rows(zui_surface_t *s, const zui_plane_t *p, const zui_pxl_lut_t *lut,
                        int32_t light_x, int32_t light_y, uint32_t tick, int32_t y0, int32_t y1);

/* THE OTHER TWO AXES (owner, 2026-10-12 09:09Z). Colour/anti-colour and
 * tint/shade are two of four. The other two are shape and time:
 *
 *   axis C  ROUND <-> ANGULAR       corner radius and how soft the relief
 *                                   edge is: a rounded bevel ramps over
 *                                   3 px, an angular one is a crisp 1 px lip
 *   axis D  DURATION <-> BRIEFNESS  how long an effect holds and how it eases:
 *                                   lasting things settle slowly (smootherstep),
 *                                   brief ones snap (ease-out) and their
 *                                   lustre moves faster
 *
 * The tag decides both. Polarity drives shape: negentropic (cooperative,
 * settled) is round, entropic is angular, FORBIDDEN is square and still.
 * Phase drives time: manifest lasts, dissolving is brief. Draw calls ask
 * zui_pxl_radius for their corner radius and zui_pxl_ease / zui_pxl_decay
 * for their motion, so the whole engine (widgets, text panels, video frames
 * stamped on the plane) speaks the same four axes. */
typedef struct {
    int32_t round_q8; /* 0 angular .. 256 round                     */
    int32_t edge_px;  /* relief bevel width, 1..3                   */
    int32_t hold_ms;  /* how long an effect lasts; 0 = no motion    */
    bool brief;       /* eases out fast rather than settling slowly */
} zui_pxl_style_t;

zui_pxl_style_t zui_pxl_style(lpx_t tag);
/* Corner radius for a thing with this tag, from the design's base radius. */
int32_t zui_pxl_radius(lpx_t tag, int32_t base);
/* Progress 0..256 of an effect `elapsed_ms` after it began, on this tag's
 * clock and curve. 256 once done (and at once when the tag has no motion). */
int32_t zui_pxl_ease(lpx_t tag, uint32_t elapsed_ms);
/* What is left of a cue 0..256 (256 - ease): scale a stamp's z by it. */
int32_t zui_pxl_decay(lpx_t tag, uint32_t elapsed_ms);

/* One pixel, for tests and shaders: rgb moved a along axis A and b along B. */
uint32_t zui_pxl_px(uint32_t rgb, int32_t a, int32_t b);

#endif /* ZXV_ZUI_PXL_H */
