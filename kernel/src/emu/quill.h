/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* quill.h — Quill: the second engine MegaROM, an id Tech 2-class polygonal
 * renderer (ref: id Tech 2 / Quake — development reference; Quill is our own
 * GPL-clean reimplementation of the technique, per the render lineage).
 *
 * The leap that defined the generation: from raycast walls (Cinder) to arbitrary
 * TRUE 3D polygons. A full 3D camera (yaw + pitch), triangle meshes transformed
 * through view + perspective projection, rasterized with a per-pixel Z-BUFFER
 * (so nearer geometry correctly occludes farther), PERSPECTIVE-CORRECT texture
 * mapping, and LIGHTMAPS — baked per-vertex lighting multiplied into the texture,
 * the Quake signature. Depth still flows through `holo` (near warm / far cool).
 * Fully integer 16.16 fixed-point with a parabolic sine — no libm, freestanding.
 *
 * Second rung of the graphics-engine lineage, bootable as a graphics MegaROM. */
#ifndef ZXV_QUILL_H
#define ZXV_QUILL_H

#include <stdint.h>

/* Render one frame of the built-in polygonal world into a 32-bit ARGB frame
 * buffer of w*h pixels, from a camera at (cx,cy,cz) 16.16 world units looking
 * with yaw/pitch (16-bit turns). */
void quill_render(uint32_t *fb, int w, int h, int32_t cx, int32_t cy, int32_t cz,
                  uint16_t yaw, uint16_t pitch);

/* Register Quill as a bootable graphics MegaROM (id Tech 2-class renderer). */
void quill_register_megarom(void);

/* On-target self-check: render a frame and verify it is a coherent polygonal
 * scene — many depths in the Z-buffer (real 3D + occlusion), textured surfaces,
 * a lightmap brightness gradient, and the holo depth cue. Returns 1 on pass. */
int  quill_selfcheck(void);

#endif /* ZXV_QUILL_H */
