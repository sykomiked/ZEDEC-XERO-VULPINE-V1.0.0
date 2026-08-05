/* test_prism_break.c — Prism Break Holographic Shader tests
 *
 * Tests initialization, color utilities, wavelength mapping, layer
 * management, touch ripples, pixel rendering, frame rendering, and
 * M5 coverage.
 *
 * Author: Michael Laurence Curzi (c)
 * 36N9 Genetics, LLC — All Rights Reserved
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#include "prism_break.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

static int feq(double a, double b, double eps) {
    double diff = fabs(a - b);
    double scale = fabs(a) > fabs(b) ? fabs(a) : fabs(b);
    if (scale < 1.0) scale = 1.0;
    return diff <= eps * scale;
}

int main(void) {
    /* ===== init ===== */
    {
        static prism_break_t pb;
        pb_init(&pb, 320, 240);
        assert(pb.width == 320);
        assert(pb.height == 240);
        assert(pb.num_layers == PB_MAX_LAYERS);
        assert(pb.num_ripples == 0);
        assert(pb.base_color.a == 255);
    }

    /* ===== color utilities ===== */
    {
        pb_color_t c = pb_make_color(128, 200, 100, 50);
        assert(c.a == 128 && c.r == 200 && c.g == 100 && c.b == 50);

        uint32_t argb = pb_color_to_argb(c);
        assert(argb == PB_ARGB(128, 200, 100, 50));

        pb_color_t c2 = pb_argb_to_color(argb);
        assert(c2.a == 128 && c2.r == 200 && c2.g == 100 && c2.b == 50);
    }

    /* ===== alpha blend ===== */
    {
        pb_color_t dst = pb_make_color(255, 0, 0, 0);   /* black */
        pb_color_t src = pb_make_color(128, 255, 255, 255); /* 50% white */
        pb_color_t result = pb_blend(dst, src);
        assert(result.a == 255);
        assert(result.r == 128); /* 255 * 128 / 255 = 128 */
        assert(result.g == 128);
        assert(result.b == 128);
    }

    /* ===== wavelength to color ===== */
    {
        /* Blue (~440nm) */
        pb_color_t blue = pb_wavelength_to_color(440);
        assert(blue.b > 0);

        /* Green (~510nm) */
        pb_color_t green = pb_wavelength_to_color(510);
        assert(green.g == 255);

        /* Red (~650nm) */
        pb_color_t red = pb_wavelength_to_color(650);
        assert(red.r == 255);

        /* Below visible spectrum */
        pb_color_t dark = pb_wavelength_to_color(300);
        assert(dark.r == 0 && dark.g == 0 && dark.b == 0);
    }

    /* ===== layer management ===== */
    {
        static prism_break_t pb;
        pb_init(&pb, 320, 240);

        /* All layers enabled by default */
        for (uint32_t i = 0; i < pb.num_layers; i++) {
            assert(pb.layers[i].enabled);
        }

        /* Disable scanlines */
        pb_enable_layer(&pb, PB_LAYER_SCANLINES, false);
        pb_layer_t *sl = NULL;
        for (uint32_t i = 0; i < pb.num_layers; i++) {
            if (pb.layers[i].type == PB_LAYER_SCANLINES) { sl = &pb.layers[i]; break; }
        }
        assert(sl != NULL && sl->enabled == false);

        /* Re-enable */
        pb_enable_layer(&pb, PB_LAYER_SCANLINES, true);
        assert(sl->enabled == true);

        /* Set intensity */
        pb_set_layer_intensity(&pb, PB_LAYER_PRISM, 64);
        pb_layer_t *pr = NULL;
        for (uint32_t i = 0; i < pb.num_layers; i++) {
            if (pb.layers[i].type == PB_LAYER_PRISM) { pr = &pb.layers[i]; break; }
        }
        assert(pr != NULL && pr->intensity == 64);

        /* Intensity clamping */
        pb_set_layer_intensity(&pb, PB_LAYER_PRISM, 300);
        assert(pr->intensity == 255);
        pb_set_layer_intensity(&pb, PB_LAYER_PRISM, -10);
        assert(pr->intensity == 0);
    }

    /* ===== touch ripples ===== */
    {
        static prism_break_t pb;
        pb_init(&pb, 320, 240);

        int32_t r1 = pb_touch(&pb, 100, 100);
        assert(r1 >= 0);
        assert(pb.num_ripples == 1);
        assert(pb.touches_processed == 1);
        assert(pb.ripples_spawned == 1);
        assert(pb.ripples[r1].active);
        assert(pb.ripples[r1].x == 100);
        assert(pb.ripples[r1].y == 100);
        assert(pb.ripples[r1].radius == 0);

        /* Add more ripples */
        pb_touch(&pb, 200, 150);
        pb_touch(&pb, 50, 50);
        assert(pb.num_ripples == 3);
    }

    /* ===== ripple update (expansion) ===== */
    {
        static prism_break_t pb;
        pb_init(&pb, 320, 240);

        pb_touch(&pb, 160, 120);
        assert(pb.ripples[0].radius == 0);

        /* Update — should expand */
        uint32_t completed = pb_update_ripples(&pb);
        assert(completed == 0);
        assert(pb.ripples[0].radius == PB_RIPPLE_SPEED);

        /* Keep updating until it completes */
        uint32_t total_completed = 0;
        for (int i = 0; i < 100; i++) {
            total_completed += pb_update_ripples(&pb);
            if (total_completed > 0) break;
        }
        assert(total_completed == 1);
        assert(!pb.ripples[0].active);
        assert(pb.ripples_completed == 1);
    }

    /* ===== ripple capacity limit ===== */
    {
        static prism_break_t pb;
        pb_init(&pb, 320, 240);

        for (uint32_t i = 0; i < PB_MAX_RIPPLES; i++) {
            int32_t r = pb_touch(&pb, (int32_t)(i * 10), (int32_t)(i * 10));
            assert(r >= 0);
        }
        /* Next touch should fail */
        int32_t r = pb_touch(&pb, 999, 999);
        assert(r == -1);
    }

    /* ===== pixel rendering ===== */
    {
        static prism_break_t pb;
        pb_init(&pb, 64, 64);

        /* Render a pixel — should return a valid color */
        pb_color_t pixel = pb_render_pixel(&pb, 32, 32);
        assert(pixel.a == 255);

        /* Out of bounds */
        pb_color_t oob = pb_render_pixel(&pb, -1, -1);
        assert(oob.a == 0 && oob.r == 0 && oob.g == 0 && oob.b == 0);

        oob = pb_render_pixel(&pb, 100, 100);
        assert(oob.a == 0);
    }

    /* ===== scanline effect ===== */
    {
        static prism_break_t pb;
        pb_init(&pb, 64, 64);

        /* Disable all layers except scanlines to isolate effect */
        pb_enable_layer(&pb, PB_LAYER_PRISM, false);
        pb_enable_layer(&pb, PB_LAYER_RIPPLES, false);
        pb_enable_layer(&pb, PB_LAYER_ABERRATION, false);
        pb_enable_layer(&pb, PB_LAYER_VIGNETTE, false);

        /* Pixel on scanline (y=0, which is divisible by PB_SCANLINE_PERIOD) */
        pb_color_t on_scan = pb_render_pixel(&pb, 32, 0);
        /* Should be darker due to scanline overlay */
        pb_color_t off_scan = pb_render_pixel(&pb, 32, 1);
        /* On-scanline should be darker than off-scanline */
        assert(on_scan.r <= off_scan.r);
    }

    /* ===== frame rendering ===== */
    {
        static prism_break_t pb;
        pb_init(&pb, 64, 64);

        pb_render_frame(&pb);
        assert(pb.frames_rendered == 1);

        /* Framebuffer should have non-zero pixels */
        uint32_t *fb = pb_get_framebuffer(&pb);
        assert(fb != NULL);
        assert(fb[0] != 0); /* top-left should have base color */

        /* Render another frame */
        pb_render_frame(&pb);
        assert(pb.frames_rendered == 2);
    }

    /* ===== touch + render integration ===== */
    {
        static prism_break_t pb;
        pb_init(&pb, 128, 128);

        /* Touch and render — ripple should affect pixels near touch point */
        pb_touch(&pb, 64, 64);
        pb_render_frame(&pb);

        /* Update ripple and render again */
        pb_update_ripples(&pb);
        pb_render_frame(&pb);

        assert(pb.frames_rendered == 2);
        assert(pb.touches_processed == 1);
    }

    /* ===== base color change ===== */
    {
        static prism_break_t pb;
        pb_init(&pb, 64, 64);

        pb_set_base_color(&pb, pb_make_color(255, 255, 0, 0)); /* red */
        pb_enable_layer(&pb, PB_LAYER_PRISM, false);
        pb_enable_layer(&pb, PB_LAYER_SCANLINES, false);
        pb_enable_layer(&pb, PB_LAYER_RIPPLES, false);
        pb_enable_layer(&pb, PB_LAYER_ABERRATION, false);
        pb_enable_layer(&pb, PB_LAYER_VIGNETTE, false);

        pb_color_t pixel = pb_render_pixel(&pb, 32, 32);
        assert(pixel.r == 255);
        assert(pixel.g == 0);
        assert(pixel.b == 0);
    }

    /* ===== coverage computation ===== */
    {
        static prism_break_t pb;
        pb_init(&pb, 64, 64);

        /* All layers enabled → r = 1.0 */
        pb_update_coverage(&pb);
        assert(feq(pb.m5.r, 1.0, 1e-9));

        /* No ripples → ell = 0.0 */
        assert(feq(pb.m5.ell, 0.0, 1e-9));

        /* Add a ripple → ell > 0 */
        pb_touch(&pb, 32, 32);
        pb_update_coverage(&pb);
        assert(pb.m5.ell > SR_ZERO);
    }

    printf("All Prism Break shader tests passed\n");
    return 0;
}
