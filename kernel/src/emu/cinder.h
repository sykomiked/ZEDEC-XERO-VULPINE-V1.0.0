/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* cinder.h — Cinder: the first real engine MegaROM, an id Tech 1-class software
 * renderer (ref: id Tech 1 / Doom / Wolfenstein — development reference; Cinder
 * is our own GPL-clean reimplementation of the technique, per the render lineage).
 *
 * The defining graphics technique of that era, done from scratch: a column-based
 * textured raycaster. For each screen column a ray is cast into a grid world; the
 * nearest wall's projected height gives a vertical textured strip, with ceiling
 * above and floor below. Depth is fed through the system's `holo` primitive so
 * near walls read warm and far walls cool (chromostereopsis) — the engine renders
 * in the system's own holographic idiom. Fully integer/fixed-point (16.16) with a
 * parabolic sine, procedural textures — no libm, no assets, freestanding.
 *
 * This turns the graphics-engine lineage from a catalog into a RUNNING renderer,
 * bootable as a graphics MegaROM on the holo/ramfb/zxv_shell stack. */
#ifndef ZXV_CINDER_H
#define ZXV_CINDER_H

#include <stdint.h>

/* Render one frame of the built-in world from camera (px,py,angle) into a 32-bit
 * ARGB framebuffer of w*h pixels. px/py are 16.16 world units (1 cell = 65536);
 * angle is a 16-bit turn (0..65535 = full circle). */
void cinder_render(uint32_t *fb, int w, int h, int32_t px, int32_t py, uint16_t angle);

/* Register Cinder as a bootable graphics MegaROM (id Tech 1-class renderer). */
void cinder_register_megarom(void);

/* On-target self-check: render a frame of the test world and verify it is a
 * coherent scene — walls drawn, ceiling/floor present, and the holo depth cue
 * makes near walls warmer than far. Returns 1 on pass. */
int  cinder_selfcheck(void);

#endif /* ZXV_CINDER_H */
