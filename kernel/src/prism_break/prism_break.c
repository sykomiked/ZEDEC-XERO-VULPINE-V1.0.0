/* prism_break.c — Prism Break Holographic Touchscreen Shader
 *
 * See prism_break.h for design rationale. All rendering is done with
 * integer math and fixed-point operations — no floating point, no GPU.
 * The framebuffer is ARGB8888 (32-bit per pixel), matching the ARM64
 * VBE-compatible display.
 *
 * Author: Michael Laurence Curzi (c)
 * 36N9 Genetics, LLC — All Rights Reserved
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#include "prism_break.h"
#include <string.h>

/* ===== Color Utilities ===== */

pb_color_t pb_make_color(uint8_t a, uint8_t r, uint8_t g, uint8_t b) {
    pb_color_t c = { a, r, g, b };
    return c;
}

uint32_t pb_color_to_argb(pb_color_t c) {
    return PB_ARGB(c.a, c.r, c.g, c.b);
}

pb_color_t pb_argb_to_color(uint32_t argb) {
    pb_color_t c;
    c.a = (uint8_t)((argb >> 24) & 0xFF);
    c.r = (uint8_t)((argb >> 16) & 0xFF);
    c.g = (uint8_t)((argb >> 8) & 0xFF);
    c.b = (uint8_t)(argb & 0xFF);
    return c;
}

pb_color_t pb_blend(pb_color_t dst, pb_color_t src) {
    /* Alpha blend src over dst */
    uint32_t alpha = src.a;
    uint32_t inv_alpha = 255 - alpha;
    pb_color_t out;
    out.a = 255;
    out.r = (uint8_t)((src.r * alpha + dst.r * inv_alpha) / 255);
    out.g = (uint8_t)((src.g * alpha + dst.g * inv_alpha) / 255);
    out.b = (uint8_t)((src.b * alpha + dst.b * inv_alpha) / 255);
    return out;
}

pb_color_t pb_wavelength_to_color(int32_t wavelength) {
    /* Map wavelength (380-750nm) to approximate RGB color.
     * Simplified CIE-like mapping using integer math. */
    pb_color_t c = { 255, 0, 0, 0 };

    if (wavelength < 380) {
        c.r = 0; c.g = 0; c.b = 0;
    } else if (wavelength < 440) {
        c.r = 0;
        c.g = 0;
        c.b = (uint8_t)(255 * (440 - wavelength) / (440 - 380));
    } else if (wavelength < 490) {
        c.r = 0;
        c.g = (uint8_t)(255 * (wavelength - 440) / (490 - 440));
        c.b = 255;
    } else if (wavelength < 510) {
        c.r = 0;
        c.g = 255;
        c.b = (uint8_t)(255 * (510 - wavelength) / (510 - 490));
    } else if (wavelength < 580) {
        c.r = (uint8_t)(255 * (wavelength - 510) / (580 - 510));
        c.g = 255;
        c.b = 0;
    } else if (wavelength < 645) {
        c.r = 255;
        c.g = (uint8_t)(255 * (645 - wavelength) / (645 - 580));
        c.b = 0;
    } else if (wavelength <= 750) {
        c.r = 255;
        c.g = 0;
        c.b = 0;
    } else {
        c.r = 0; c.g = 0; c.b = 0;
    }

    return c;
}

/* ===== Init ===== */

void pb_init(prism_break_t *pb, uint32_t width, uint32_t height) {
    if (!pb) return;
    memset(pb, 0, sizeof(*pb));

    if (width > PB_MAX_WIDTH) width = PB_MAX_WIDTH;
    if (height > PB_MAX_HEIGHT) height = PB_MAX_HEIGHT;

    pb->width = width;
    pb->height = height;
    pb->num_layers = 0;
    pb->num_ripples = 0;
    pb->frames_rendered = 0;
    pb->touches_processed = 0;
    pb->ripples_spawned = 0;
    pb->ripples_completed = 0;

    /* Default base color: deep space black with slight blue tint */
    pb->base_color = pb_make_color(255, 8, 12, 24);

    /* Enable all layers by default with default intensities */
    pb->layers[0] = (pb_layer_t){ PB_LAYER_BASE, true, 255 };
    pb->layers[1] = (pb_layer_t){ PB_LAYER_PRISM, true, PB_PRISM_INTENSITY };
    pb->layers[2] = (pb_layer_t){ PB_LAYER_SCANLINES, true, PB_SCANLINE_INTENSITY };
    pb->layers[3] = (pb_layer_t){ PB_LAYER_RIPPLES, true, 128 };
    pb->layers[4] = (pb_layer_t){ PB_LAYER_ABERRATION, true, PB_ABERRATION_OFFSET };
    pb->layers[5] = (pb_layer_t){ PB_LAYER_VIGNETTE, true, 64 };
    pb->num_layers = PB_MAX_LAYERS;

    pb->m5.omega = 0;
    pb->m5.chi = 0;
    pb->m5.phi = SR_ZERO;

    pb_update_coverage(pb);
}

void pb_set_base_color(prism_break_t *pb, pb_color_t color) {
    if (!pb) return;
    pb->base_color = color;
}

/* ===== Layer Management ===== */

static pb_layer_t *pb_find_layer(prism_break_t *pb, pb_layer_type_t type) {
    for (uint32_t i = 0; i < pb->num_layers; i++) {
        if (pb->layers[i].type == type) return &pb->layers[i];
    }
    return NULL;
}

void pb_enable_layer(prism_break_t *pb, pb_layer_type_t type, bool enabled) {
    if (!pb) return;
    pb_layer_t *l = pb_find_layer(pb, type);
    if (l) l->enabled = enabled;
}

void pb_set_layer_intensity(prism_break_t *pb, pb_layer_type_t type, int32_t intensity) {
    if (!pb) return;
    if (intensity < 0) intensity = 0;
    if (intensity > 255) intensity = 255;
    pb_layer_t *l = pb_find_layer(pb, type);
    if (l) l->intensity = intensity;
}

/* ===== Touch Ripples ===== */

int32_t pb_touch(prism_break_t *pb, int32_t x, int32_t y) {
    if (!pb) return -1;

    uint32_t slot = PB_MAX_RIPPLES;
    for (uint32_t i = 0; i < PB_MAX_RIPPLES; i++) {
        if (!pb->ripples[i].active) {
            slot = i;
            break;
        }
    }
    if (slot >= PB_MAX_RIPPLES) return -1;

    pb->ripples[slot].x = x;
    pb->ripples[slot].y = y;
    pb->ripples[slot].radius = 0;
    pb->ripples[slot].max_radius = PB_RIPPLE_MAX_RADIUS;
    pb->ripples[slot].active = true;

    pb->num_ripples++;
    pb->touches_processed++;
    pb->ripples_spawned++;

    return (int32_t)slot;
}

uint32_t pb_update_ripples(prism_break_t *pb) {
    if (!pb) return 0;
    uint32_t completed = 0;

    for (uint32_t i = 0; i < PB_MAX_RIPPLES; i++) {
        if (!pb->ripples[i].active) continue;
        pb->ripples[i].radius += PB_RIPPLE_SPEED;
        if (pb->ripples[i].radius >= pb->ripples[i].max_radius) {
            pb->ripples[i].active = false;
            if (pb->num_ripples > 0) pb->num_ripples--;
            pb->ripples_completed++;
            completed++;
        }
    }

    if (completed > 0) pb_update_coverage(pb);
    return completed;
}

/* ===== Pixel Rendering ===== */

/* Layer 1: Base color */
static pb_color_t pb_layer_base(prism_break_t *pb, int32_t x, int32_t y) {
    (void)x; (void)y;
    return pb->base_color;
}

/* Layer 2: Prism refraction — shifts hue based on position.
 * Creates a rainbow prism effect across the screen. */
static pb_color_t pb_layer_prism(prism_break_t *pb, int32_t x, int32_t y,
                                  int32_t intensity) {
    if (pb->width == 0) return pb_make_color(0, 0, 0, 0);

    /* Map x position to wavelength (380-750nm) */
    int32_t wavelength = 380 + (x * (750 - 380)) / (int32_t)pb->width;
    pb_color_t prism_color = pb_wavelength_to_color(wavelength);

    /* Modulate by y to create vertical variation */
    int32_t y_factor = 128 + (y * 64) / (int32_t)(pb->height > 0 ? pb->height : 1);
    if (y_factor > 255) y_factor = 255;

    prism_color.a = (uint8_t)(intensity / 4);
    prism_color.r = (uint8_t)((prism_color.r * y_factor) / 255);
    prism_color.g = (uint8_t)((prism_color.g * y_factor) / 255);
    prism_color.b = (uint8_t)((prism_color.b * y_factor) / 255);

    return prism_color;
}

/* Layer 3: Holographic scanlines — interference pattern */
static pb_color_t pb_layer_scanlines(prism_break_t *pb, int32_t x, int32_t y,
                                      int32_t intensity) {
    (void)pb; (void)x;
    pb_color_t c = { 0, 0, 0, 0 };
    if ((y % PB_SCANLINE_PERIOD) == 0) {
        c.a = (uint8_t)intensity;
        c.r = 0; c.g = 0; c.b = 0;
    }
    return c;
}

/* Layer 4: Touch ripples — expanding wavefronts */
static pb_color_t pb_layer_ripples(prism_break_t *pb, int32_t x, int32_t y,
                                    int32_t intensity) {
    pb_color_t c = { 0, 0, 0, 0 };

    for (uint32_t i = 0; i < PB_MAX_RIPPLES; i++) {
        if (!pb->ripples[i].active) continue;

        int32_t dx = x - pb->ripples[i].x;
        int32_t dy = y - pb->ripples[i].y;
        /* Approximate distance (no sqrt — Manhattan distance scaled) */
        int32_t dist = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
        int32_t radius = pb->ripples[i].radius;

        /* Ring of width ~4 pixels at the ripple's current radius */
        int32_t diff = dist - radius;
        if (diff < 0) diff = -diff;
        if (diff <= 2) {
            /* Fade based on how far the ripple has expanded */
            int32_t fade = 255 - (radius * 255) / pb->ripples[i].max_radius;
            if (fade < 0) fade = 0;
            int32_t alpha = (intensity * fade) / 255;
            c.a = (uint8_t)alpha;
            c.r = (uint8_t)(alpha);  /* white-blue ripple */
            c.g = (uint8_t)(alpha * 3 / 4);
            c.b = (uint8_t)alpha;
            return c;
        }
    }
    return c;
}

/* Layer 5: Chromatic aberration — RGB channel offset at screen edges */
static pb_color_t pb_layer_aberration(prism_break_t *pb, int32_t x, int32_t y,
                                       int32_t offset) {
    pb_color_t c = { 0, 0, 0, 0 };
    if (offset <= 0) return c;

    /* Only apply aberration near screen edges (last 10% of each side) */
    int32_t edge_x = (int32_t)pb->width - x;
    int32_t edge_y = (int32_t)pb->height - y;
    if (edge_x < 0) edge_x = -edge_x;
    if (edge_y < 0) edge_y = -edge_y;

    int32_t margin_x = (int32_t)(pb->width / 10);
    int32_t margin_y = (int32_t)(pb->height / 10);

    if (x < margin_x || edge_x < margin_x || y < margin_y || edge_y < margin_y) {
        /* Sample neighboring pixels for channel offset */
        int32_t rx = x + offset;
        int32_t bx = x - offset;
        if (rx >= (int32_t)pb->width) rx = (int32_t)pb->width - 1;
        if (bx < 0) bx = 0;

        uint32_t r_pixel = pb->backbuffer[y * pb->width + rx];
        uint32_t b_pixel = pb->backbuffer[y * pb->width + bx];

        c.a = 128;
        c.r = (uint8_t)((r_pixel >> 16) & 0xFF);
        c.g = 0;
        c.b = (uint8_t)(b_pixel & 0xFF);
    }
    return c;
}

/* Layer 6: Vignette — darkening at screen edges */
static pb_color_t pb_layer_vignette(prism_break_t *pb, int32_t x, int32_t y,
                                     int32_t intensity) {
    (void)intensity;
    pb_color_t c = { 0, 0, 0, 0 };

    /* Distance from center (Manhattan) */
    int32_t cx = (int32_t)(pb->width / 2);
    int32_t cy = (int32_t)(pb->height / 2);
    int32_t dx = x - cx;
    int32_t dy = y - cy;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    int32_t dist = dx + dy;
    int32_t max_dist = cx + cy;

    /* Vignette factor: 0 at center, 1 at edges */
    int32_t factor = (dist * 256) / (max_dist > 0 ? max_dist : 1);
    /* Apply only beyond PB_VIGNETTE_RADIUS */
    int32_t threshold = (int32_t)(256 * PB_VIGNETTE_RADIUS);
    if (factor > threshold) {
        int32_t excess = factor - threshold;
        int32_t max_excess = 256 - threshold;
        int32_t darken = (excess * intensity) / max_excess;
        if (darken > 255) darken = 255;
        c.a = (uint8_t)darken;
        c.r = 0; c.g = 0; c.b = 0;
    }
    return c;
}

pb_color_t pb_render_pixel(prism_break_t *pb, int32_t x, int32_t y) {
    if (!pb || x < 0 || y < 0 || x >= (int32_t)pb->width || y >= (int32_t)pb->height) {
        return pb_make_color(0, 0, 0, 0);
    }

    /* Start with base layer */
    pb_color_t pixel = pb_layer_base(pb, x, y);

    /* Composite each enabled layer in order */
    for (uint32_t i = 0; i < pb->num_layers; i++) {
        if (!pb->layers[i].enabled) continue;

        pb_color_t layer_color;
        switch (pb->layers[i].type) {
            case PB_LAYER_BASE:
                continue; /* already applied */
            case PB_LAYER_PRISM:
                layer_color = pb_layer_prism(pb, x, y, pb->layers[i].intensity);
                break;
            case PB_LAYER_SCANLINES:
                layer_color = pb_layer_scanlines(pb, x, y, pb->layers[i].intensity);
                break;
            case PB_LAYER_RIPPLES:
                layer_color = pb_layer_ripples(pb, x, y, pb->layers[i].intensity);
                break;
            case PB_LAYER_ABERRATION:
                layer_color = pb_layer_aberration(pb, x, y, pb->layers[i].intensity);
                break;
            case PB_LAYER_VIGNETTE:
                layer_color = pb_layer_vignette(pb, x, y, pb->layers[i].intensity);
                break;
            default:
                continue;
        }
        pixel = pb_blend(pixel, layer_color);
    }

    return pixel;
}

void pb_render_frame(prism_break_t *pb) {
    if (!pb) return;

    /* Render to backbuffer first (for aberration sampling) */
    for (uint32_t y = 0; y < pb->height; y++) {
        for (uint32_t x = 0; x < pb->width; x++) {
            pb_color_t pixel = pb_render_pixel(pb, (int32_t)x, (int32_t)y);
            pb->backbuffer[y * pb->width + x] = pb_color_to_argb(pixel);
        }
    }

    /* Copy backbuffer to framebuffer */
    memcpy(pb->framebuffer, pb->backbuffer, (size_t) pb->width * pb->height * sizeof(uint32_t));

    pb->frames_rendered++;
    pb_update_coverage(pb);
}

uint32_t *pb_get_framebuffer(prism_break_t *pb) {
    if (!pb) return NULL;
    return pb->framebuffer;
}

surplus_real_t pb_update_coverage(prism_break_t *pb) {
    if (!pb) return SR_ZERO;

    /* r: enabled layer ratio */
    uint32_t enabled = 0;
    uint32_t total = 0;
    for (uint32_t i = 0; i < pb->num_layers; i++) {
        total++;
        if (pb->layers[i].enabled) enabled++;
    }
    pb->m5.r = (total == 0) ? SR_ONE
        : SR_DIV(SR_FROM_INT((int64_t)enabled), SR_FROM_INT((int64_t)total));

    /* ell: active ripple ratio (ripples active / max ripples) */
    uint32_t active = 0;
    for (uint32_t i = 0; i < PB_MAX_RIPPLES; i++) {
        if (pb->ripples[i].active) active++;
    }
    pb->m5.ell = SR_DIV(SR_FROM_INT((int64_t)active), SR_FROM_INT((int64_t)PB_MAX_RIPPLES));

    surplus_real_t product = SR_MUL(pb->m5.r, pb->m5.ell);
    surplus_real_t floor = SR_DIV(SR_FROM_INT(COVERAGE_FLOOR_NUM), SR_FROM_INT(COVERAGE_FLOOR_DEN));
    pb->coverage_ratio = SR_DIV(product, floor);

    return pb->coverage_ratio;
}
