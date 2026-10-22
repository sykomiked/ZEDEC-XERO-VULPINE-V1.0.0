/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Gesso's model against zxvd's real dispatcher (LINK): its rules, then an
 * image made, edited, brushed and layered, saved to the Hoard as a new file
 * (never over one that is there), opened again, and written over its
 * original only on the owner's word. */
#include "gesso_model.h"

static gesso_t GE, GE2;

/* Write text to path in the Hoard (a hand-made file). */
static int ge_text(const char *path, const char *text)
{
    zxm_begin(&LINK, "files.write");
    zxm_p_str(&LINK, "path", path);
    zxm_p_str(&LINK, "text", text);
    return zxm_call(&LINK);
}

APPS_TEST(gesso)
{
    int64_t bins[GESSO_BINS_MAX], count = 0;
    uint32_t nb = 0;
    const uint8_t red[3] = {255, 0, 0};

    /* the rules, as the blocks run them */
    CHECK(gesso_bright_px(100, 50) == 150 && gesso_bright_px(250, 10) == 255 &&
              gesso_bright_px(5, -10) == 0 && gesso_bright_px(256, 0) == GESSO_REFUSED,
          "gesso: brightness holds to 0..255 and refuses outside its domain (GR1)");
    CHECK(gesso_contrast_px(100, 20000) == 72 && gesso_contrast_px(127, 5000) == 127 &&
              gesso_contrast_px(0, 0) == 128 && gesso_contrast_px(0, 40001) == GESSO_REFUSED,
          "gesso: contrast turns about 128 and floors toward minus infinity (GR2)");
    CHECK(gesso_blend_px(100, 200, 128) == 150 && gesso_blend_px(0, 255, 255) == 255 &&
              gesso_blend_px(0, 0, 256) == GESSO_REFUSED,
          "gesso: blend of top over base (GR3)");
    CHECK(gesso_brush_ok(1, 0, 0) && gesso_brush_ok(512, 255, 100) && !gesso_brush_ok(0, 0, 0) &&
              !gesso_brush_ok(513, 0, 0) && !gesso_brush_ok(1, 256, 0) &&
              !gesso_brush_ok(1, 0, 101),
          "gesso: the brush limits (GR5)");
    CHECK(gesso_canvas_ok(1, 1) && gesso_canvas_ok(2048, 2048) && !gesso_canvas_ok(0, 1) &&
              !gesso_canvas_ok(1, 2049),
          "gesso: the canvas limits (GR6)");
    CHECK(gesso_layer_ok(GESSO_LAYER_BRIGHT, -255, 255, 1) &&
              gesso_layer_ok(GESSO_LAYER_CONTRAST, 40000, 0, 0) &&
              !gesso_layer_ok(GESSO_LAYER_WASH, 256, 10, 1) && !gesso_layer_ok(4, 0, 10, 1) &&
              !gesso_layer_ok(GESSO_LAYER_BRIGHT, 0, 10, 2),
          "gesso: what a layer may hold (GR7)");

    /* nothing until there is an image; a canvas outside the limits is refused */
    gesso_init(&GE, &LINK);
    CHECK(gesso_brightness(&GE, 1) == ZXM_E_STATE && gesso_flatten(&GE) == ZXM_E_STATE &&
              gesso_save_as(&GE, "gesso-none.ppm") == ZXM_E_STATE &&
              gesso_save_over(&GE, true) == ZXM_E_STATE,
          "gesso: no image yet");
    CHECK(gesso_new(&GE, 0, 10, 3, 0) == ZXM_E_ARG && gesso_new(&GE, 10, 2049, 3, 0) == ZXM_E_ARG &&
              gesso_new(&GE, 10, 10, 2, 0) == ZXM_E_ARG &&
              gesso_new(&GE, 10, 10, 3, 256) == ZXM_E_ARG && GE.width == 0,
          "gesso: a canvas outside its limits is refused");

    /* edits over the whole image */
    CHECK(gesso_new(&GE, 300, 200, 3, 100) == ZXM_OK && GE.dirty && GE.held_dark == 0 &&
              GE.held_light == 0,
          "gesso: a 300 by 200 colour canvas");
    CHECK(gesso_brightness(&GE, 256) == ZXM_E_ARG && gesso_contrast(&GE, -1) == ZXM_E_ARG &&
              gesso_wash(&GE, 0, 256) == ZXM_E_ARG && GE.px[0] == 100,
          "gesso: an amount outside its rule is refused, nothing changed");
    CHECK(gesso_brightness(&GE, 200) == ZXM_OK && GE.px[0] == 255 &&
              GE.held_light == 300 * 200 * 3 && GE.held_dark == 0,
          "gesso: brightened past white, every value held at 255 (witness.kind_count)");
    CHECK(gesso_contrast(&GE, 5000) == ZXM_OK && GE.px[0] == gesso_contrast_px(255, 5000) &&
              GE.held_light == 0,
          "gesso: half the contrast");
    CHECK(gesso_wash(&GE, 0, 255) == ZXM_OK && GE.px[7] == 0 && GE.held_dark == 300 * 200 * 3,
          "gesso: a full black wash");

    /* a brush dab, partly off the canvas, clipped with hollow.shown */
    CHECK(gesso_dab(&GE, 0, 0, 0, red, 255, 100) == ZXM_E_ARG &&
              gesso_dab(&GE, 0, 0, 9, red, 256, 100) == ZXM_E_ARG &&
              gesso_dab(&GE, 0, 0, 9, NULL, 255, 100) == ZXM_E_ARG &&
              gesso_dab(&GE, 200000, 0, 9, red, 255, 100) == ZXM_E_ARG,
          "gesso: a brush outside its limits or far past the canvas is refused");
    CHECK(gesso_dab(&GE, 1, 1, 9, red, 255, 100) == ZXM_OK && GE.px[0] == 255 && GE.px[1] == 0 &&
              GE.px[(1u * 300u + 1u) * 3u] == 255 && GE.px[(10u * 300u + 10u) * 3u] == 0,
          "gesso: a hard dab at the corner paints only inside the canvas and its circle");
    CHECK(gesso_dab(&GE, 150, 100, 20, red, 200, 0) == ZXM_OK &&
              GE.px[(99u * 300u + 149u) * 3u] > 150 && GE.px[(99u * 300u + 149u) * 3u] <= 200 &&
              GE.px[(90u * 300u + 140u) * 3u] == 0,
          "gesso: a soft dab is strongest at its centre and lays nothing past its rim");
    CHECK(gesso_dab(&GE, 299, 199, 512, red, 0, 50) == ZXM_OK, "gesso: an empty dab is fine");

    /* counts: a histogram in bins and the range count of each bin agree */
    CHECK(gesso_histogram(&GE, 0, 0, bins, &nb) == ZXM_E_ARG &&
              gesso_histogram(&GE, 3, 1, bins, &nb) == ZXM_E_ARG &&
              gesso_range(&GE, -1, 0, 255, &count) == ZXM_E_ARG,
          "gesso: a bin width of 0 or a channel past the image is refused");
    for (int64_t w = 1; w <= 256; w = w * 2 + 1) {
        int64_t sum = 0;
        bool same =
            gesso_histogram(&GE, 0, w, bins, &nb) == ZXM_OK && nb == (uint32_t) ((256 + w - 1) / w);
        for (uint32_t b = 0; same && b < nb; b++) {
            same =
                gesso_range(&GE, 0, (int64_t) b * w, (int64_t) b * w + w - 1, &count) == ZXM_OK &&
                count == bins[b];
            sum += bins[b];
        }
        CHECK(same && sum == 300 * 200, "gesso: %u bins of width %lld are the range counts", nb,
              (long long) w);
    }
    CHECK(gesso_range(&GE, 1, 1, 255, &count) == ZXM_OK && count == 0,
          "gesso: red only: no green above 0");

    /* layers wait above the image until they are laid down */
    CHECK(gesso_new(&GE, 4, 2, 1, 100) == ZXM_OK, "gesso: a small grey canvas");
    CHECK(gesso_layer_add(&GE, GESSO_LAYER_BRIGHT, 50, 255) == ZXM_OK &&
              gesso_layer_add(&GE, GESSO_LAYER_CONTRAST, 20000, 255) == ZXM_OK &&
              gesso_layer_add(&GE, GESSO_LAYER_WASH, 0, 128) == ZXM_OK && GE.nlayers == 3 &&
              GE.px[0] == 100,
          "gesso: three layers, the pixels untouched");
    CHECK(gesso_layer_add(&GE, 4, 0, 10) == ZXM_E_ARG &&
              gesso_layer_add(&GE, GESSO_LAYER_BRIGHT, 300, 10) == ZXM_E_ARG &&
              gesso_layer_add(&GE, GESSO_LAYER_WASH, 10, 256) == ZXM_E_ARG &&
              gesso_layer_show(&GE, 3, false) == ZXM_E_ARG && gesso_layer_drop(&GE, 9) == ZXM_E_ARG,
          "gesso: a layer outside its limits, or a place past the stack, is refused");
    CHECK(gesso_layer_show(&GE, 1, false) == ZXM_OK && gesso_layer_drop(&GE, 2) == ZXM_OK &&
              GE.nlayers == 2,
          "gesso: contrast hidden, the wash dropped");
    CHECK(gesso_flatten(&GE) == ZXM_OK && GE.px[0] == 150 && GE.px[7] == 150 && GE.nlayers == 0,
          "gesso: laid down: only the brightness shows");
    for (uint32_t i = 0; i < GESSO_LAYERS_MAX; i++)
        (void) gesso_layer_add(&GE, GESSO_LAYER_WASH, (int64_t) i, 1);
    CHECK(GE.nlayers == GESSO_LAYERS_MAX &&
              gesso_layer_add(&GE, GESSO_LAYER_WASH, 0, 1) == ZXM_E_STATE,
          "gesso: at most %u layers", GESSO_LAYERS_MAX);

    /* GM5: saved as a new file, never over one that is there */
    CHECK(gesso_new(&GE, 300, 200, 3, 60) == ZXM_OK &&
              gesso_dab(&GE, 100, 80, 64, red, 255, 60) == ZXM_OK,
          "gesso: a canvas with a dab");
    CHECK(gesso_save_as(&GE, "") == ZXM_E_ARG, "gesso: no path");
    CHECK(gesso_save_as(&GE, "gesso-original.ppm") == ZXM_OK && !GE.dirty &&
              !strcmp(GE.saved, "gesso-original.ppm"),
          "gesso: saved as new (several pieces): %s", LINK.err);
    CHECK(gesso_save_as(&GE, "gesso-original.ppm") == ZXM_E_REMOTE,
          "gesso: never over a file that is there: %s", LINK.err);
    gesso_init(&GE2, &LINK);
    CHECK(gesso_open(&GE2, "", "gesso-original.ppm") == ZXM_OK && GE2.width == 300 &&
              GE2.height == 200 && GE2.channels == 3 && !memcmp(GE2.px, GE.px, 300u * 200u * 3u) &&
              !strcmp(GE2.path, "gesso-original.ppm") && !GE2.dirty,
          "gesso: read back value for value: %s", LINK.err);

    /* an edit of the original goes to a new file; the original stays */
    CHECK(gesso_brightness(&GE2, 40) == ZXM_OK && GE2.dirty &&
              gesso_save_as(&GE2, "gesso-brighter.ppm") == ZXM_OK,
          "gesso: the edit saved as new: %s", LINK.err);
    CHECK(gesso_open(&GE, "home", "gesso-original.ppm") == ZXM_OK && GE.px[0] == 60 &&
              GE.space[0] == 0,
          "gesso: the original is as it was");
    CHECK(gesso_open(&GE, "", "gesso-brighter.ppm") == ZXM_OK && GE.px[0] == 100,
          "gesso: the new file holds the edit");

    /* GM6: over the original only on the owner's word */
    CHECK(gesso_open(&GE2, "", "gesso-original.ppm") == ZXM_OK &&
              gesso_contrast(&GE2, 0) == ZXM_OK && gesso_save_over(&GE2, false) == ZXM_E_STATE &&
              GE2.dirty && gesso_open(&GE, "", "gesso-original.ppm") == ZXM_OK && GE.px[0] == 60,
          "gesso: without the owner's word nothing is written over the original");
    CHECK(gesso_contrast(&GE2, 0) == ZXM_OK && gesso_save_over(&GE2, true) == ZXM_OK &&
              !GE2.dirty && gesso_open(&GE, "", "gesso-original.ppm") == ZXM_OK &&
              GE.px[0] == 128 && GE.px[300u * 200u * 3u - 1u] == 128,
          "gesso: confirmed, the original is written over: %s", LINK.err);

    /* GM1: a grey PGM made by hand, comments and all, and files that are no image */
    CHECK(ge_text("gesso-hand.pgm", "P5\n# made by hand\n2 2\n255\nABCD") == ZXM_OK,
          "gesso: a hand-made PGM written: %s", LINK.err);
    CHECK(gesso_open(&GE, "", "gesso-hand.pgm") == ZXM_OK && GE.channels == 1 && GE.width == 2 &&
              GE.height == 2 && GE.px[0] == 'A' && GE.px[3] == 'D',
          "gesso: a PGM with a comment read: %s", LINK.err);
    CHECK(ge_text("gesso-deep.pgm", "P5\n2 2\n65535\nABCDEFGH") == ZXM_OK &&
              ge_text("gesso-short.pgm", "P5\n2 2\n255\nABC") == ZXM_OK &&
              ge_text("gesso-wide.pgm", "P5\n2049 1\n255\nA") == ZXM_OK &&
              ge_text("gesso-text.ppm", "not an image") == ZXM_OK,
          "gesso: files that are no image written: %s", LINK.err);
    CHECK(gesso_open(&GE, "", "gesso-deep.pgm") == ZXM_E_ARG &&
              gesso_open(&GE, "", "gesso-short.pgm") == ZXM_E_ARG &&
              gesso_open(&GE, "", "gesso-wide.pgm") == ZXM_E_ARG &&
              gesso_open(&GE, "", "gesso-text.ppm") == ZXM_E_ARG && GE.width == 2 &&
              !strcmp(GE.path, "gesso-hand.pgm"),
          "gesso: a 16-bit, short, too wide or text file leaves the image as it was");
    CHECK(gesso_open(&GE, "", "gesso-none.pgm") == ZXM_E_REMOTE && GE.width == 2,
          "gesso: a missing image is zxvd's error: %s", LINK.err);
    CHECK(gesso_brightness(&GE, -1) == ZXM_OK && gesso_save_over(&GE, true) == ZXM_OK &&
              gesso_open(&GE2, "", "gesso-hand.pgm") == ZXM_OK && GE2.px[0] == '@' &&
              GE2.channels == 1,
          "gesso: a grey image written over on the owner's word: %s", LINK.err);
}
