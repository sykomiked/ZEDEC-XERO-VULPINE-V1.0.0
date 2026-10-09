/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* rom_dimensions.h — the ROM library revisited through the dimensional ladder.
 *
 * The insight: the ROM-era consoles (NES, SMS, Game Boy, PC Engine, SNES,
 * Genesis, GBA) all live at 2d — the PLANE. Their whole art is sprites and tiles
 * on a flat surface. The graphics-engine lineage is the leap up the ladder: 3d
 * (space — Cinder's raycast, Quill's polygons) and 4d (event space — dynamic,
 * shader-era). 2 and 3 are Fibonacci (prime/anchor) dimensions: the plane and
 * space are the two great anchors of play.
 *
 * And the transition from the 2D ROMs to the 3D holographic MegaROM is EXACTLY
 * Helion's spiral: an old 2D game is lifted into 3D not by extrusion (pop it out,
 * fill the plank) but by SPIN — the plane spins about an axis perpendicular to
 * its setting, carrying the 2D screen into depth as a helix. That is how the ROMs
 * gain beyond-PS6 holographic depth without inventing content that was never
 * there. `rom_lift` performs it; `rom_dimensions_render` shows a 2D game frame
 * flat (the ROM) beside itself spiral-lifted (the MegaROM). */
#ifndef ZXV_ROM_DIMENSIONS_H
#define ZXV_ROM_DIMENSIONS_H

#include <stdint.h>

typedef struct play_dim {
    const char *name;      /* the system or engine                             */
    int         dim;       /* the dimension its native graphics occupy         */
    uint8_t     is_engine; /* 0 = ROM-era console, 1 = graphics engine         */
    uint8_t     threed;    /* 0 = flat plane, 1 = real 3D                       */
} play_dim_t;

/* The play-dimension lineage: ROM consoles at 2d, engines climbing 3d..4d. */
const play_dim_t *rom_play_dimensions(int *count_out);

/* Lift a normalized 2D game-screen point (sx,sy in 0..65536 = 0..1) at spin step
 * `i` into a 3D holographic point, by Helion's spiral (not extrusion). Outputs
 * screen x/y (into a w*h view) and a depth cue. */
void rom_lift(int32_t sx16, int32_t sy16, int i, int w, int h,
              int *out_x, int *out_y, int32_t *out_depth);

/* Render a 2D game frame flat (left, the ROM) beside itself spiral-lifted into 3D
 * (right, the MegaROM's holographic form) into a w*h ARGB framebuffer. */
void rom_dimensions_render(uint32_t *fb, int w, int h);

/* On-target self-check: the ROM consoles sit at 2d and the engines climb to 3d+
 * (a monotone leap), 2 and 3 are the Fibonacci anchor dimensions, and the 2D->3D
 * lift is a genuine SPIRAL (Helion: perpendicular + 3-axis, not extrusion).
 * Returns 1 on pass; *rom2d_out = count of ROM systems at 2d. */
int  rom_dimensions_selfcheck(uint32_t *rom2d_out);

#endif /* ZXV_ROM_DIMENSIONS_H */
