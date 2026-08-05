/* prism_break.h — Prism Break Holographic Touchscreen Shader
 *
 * Native ZXV framebuffer compositor that produces a prism/refraction
 * holographic visual effect for the touchscreen UI. Operates directly
 * on the ARM64 framebuffer (VBE-compatible 32-bit ARGB8888).
 *
 * Key design principles (code as hardware, hardware as code):
 *   - Fixed-function pipeline: no GPU required, all math is integer
 *     or fixed-point (Surplus Real)
 *   - Static allocation: all buffers are compile-time sized
 *   - Deterministic: same input always produces same output
 *   - Prism effect: light refraction simulation via wavelength-to-color
 *     mapping with chromatic aberration at edges
 *   - Touch ripple: touch events generate expanding wavefronts
 *   - Holographic scanlines: subtle interference pattern overlay
 *   - Color depth: 32-bit ARGB8888 (matches ARM64 VBE framebuffer)
 *
 * The shader pipeline:
 *   1. Base layer (solid color or gradient)
 *   2. Prism refraction (wavelength shift at brightness gradients)
 *   3. Holographic scanlines (interference pattern)
 *   4. Touch ripples (expanding wavefronts from touch points)
 *   5. Chromatic aberration (RGB channel offset at edges)
 *   6. Vignette (subtle darkening at screen edges)
 *
 * Author: Michael Laurence Curzi (c)
 * 36N9 Genetics, LLC — All Rights Reserved
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#ifndef PRISM_BREAK_H
#define PRISM_BREAK_H

#include <stdint.h>
#include <stdbool.h>
#include "m5_types.h"
#include "surplus.h"
#include "edp_risk.h"

/* ===== Constants ===== */

#define PB_MAX_WIDTH        1920
#define PB_MAX_HEIGHT       1080
#define PB_MAX_RIPPLES      8       /* max concurrent touch ripples */
#define PB_RIPPLE_MAX_RADIUS  600   /* max ripple expansion in pixels */
#define PB_RIPPLE_SPEED      8     /* pixels per frame */
#define PB_SCANLINE_PERIOD   4     /* scanline every N pixels */
#define PB_SCANLINE_INTENSITY 12   /* 0-255, how dark scanlines are */
#define PB_PRISM_INTENSITY   32    /* 0-255, prism refraction strength */
#define PB_ABERRATION_OFFSET 2    /* chromatic aberration pixel offset */
#define PB_VIGNETTE_RADIUS   0.85  /* vignette starts at 85% from center */
#define PB_MAX_LAYERS        6     /* max composited layers */

/* ===== Color ===== */

typedef struct pb_color {
    uint8_t a;  /* alpha */
    uint8_t r;  /* red */
    uint8_t g;  /* green */
    uint8_t b;  /* blue */
} pb_color_t;

#define PB_ARGB(a, r, g, b) (((uint32_t)(a) << 24) | ((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))

/* ===== Touch Ripple ===== */

typedef struct pb_ripple {
    int32_t x;
    int32_t y;
    int32_t radius;        /* current radius */
    int32_t max_radius;
    bool active;
} pb_ripple_t;

/* ===== Shader Layer ===== */

typedef enum {
    PB_LAYER_UNUSED      = 0,
    PB_LAYER_BASE        = 1,  /* solid color or gradient */
    PB_LAYER_PRISM       = 2,  /* prism refraction */
    PB_LAYER_SCANLINES   = 3,  /* holographic scanlines */
    PB_LAYER_RIPPLES     = 4,  /* touch ripples */
    PB_LAYER_ABERRATION  = 5,  /* chromatic aberration */
    PB_LAYER_VIGNETTE    = 6   /* edge darkening */
} pb_layer_type_t;

typedef struct pb_layer {
    pb_layer_type_t type;
    bool enabled;
    int32_t intensity;     /* 0-255 */
} pb_layer_t;

/* ===== Prism Break Engine ===== */

typedef struct prism_break {
    uint32_t width;
    uint32_t height;

    /* Framebuffer (ARGB8888) */
    uint32_t framebuffer[PB_MAX_WIDTH * PB_MAX_HEIGHT];
    uint32_t backbuffer[PB_MAX_WIDTH * PB_MAX_HEIGHT];

    /* Layers */
    pb_layer_t layers[PB_MAX_LAYERS];
    uint32_t num_layers;

    /* Touch ripples */
    pb_ripple_t ripples[PB_MAX_RIPPLES];
    uint32_t num_ripples;

    /* Base color */
    pb_color_t base_color;

    /* Stats */
    uint32_t frames_rendered;
    uint32_t touches_processed;
    uint32_t ripples_spawned;
    uint32_t ripples_completed;

    /* M5 coordinates */
    m5_coords_t m5;
    surplus_real_t coverage_ratio;
} prism_break_t;

/* ===== API ===== */

void pb_init(prism_break_t *pb, uint32_t width, uint32_t height);

/* Set base color (background). */
void pb_set_base_color(prism_break_t *pb, pb_color_t color);

/* Enable/disable a layer. */
void pb_enable_layer(prism_break_t *pb, pb_layer_type_t type, bool enabled);
void pb_set_layer_intensity(prism_break_t *pb, pb_layer_type_t type, int32_t intensity);

/* Process a touch event (spawn ripple). Returns ripple index or -1 if full. */
int32_t pb_touch(prism_break_t *pb, int32_t x, int32_t y);

/* Update ripples (expand and expire). Returns count of completed ripples. */
uint32_t pb_update_ripples(prism_break_t *pb);

/* Render a single pixel (all enabled layers composited). */
pb_color_t pb_render_pixel(prism_break_t *pb, int32_t x, int32_t y);

/* Render the full frame to the backbuffer, then swap to framebuffer. */
void pb_render_frame(prism_break_t *pb);

/* Get framebuffer pointer. */
uint32_t *pb_get_framebuffer(prism_break_t *pb);

/* Update M5 coverage. */
surplus_real_t pb_update_coverage(prism_break_t *pb);

/* ===== Color Utilities ===== */

pb_color_t pb_make_color(uint8_t a, uint8_t r, uint8_t g, uint8_t b);
uint32_t pb_color_to_argb(pb_color_t c);
pb_color_t pb_argb_to_color(uint32_t argb);
pb_color_t pb_blend(pb_color_t dst, pb_color_t src);
pb_color_t pb_wavelength_to_color(int32_t wavelength);  /* 380-750nm */

#endif /* PRISM_BREAK_H */
