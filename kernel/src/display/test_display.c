/* test_display.c — display mode negotiation tests.
 *
 * The headline test is the one that fixes the actual complaint: when the
 * firmware owns the scanout, we adopt ITS geometry and compose natively,
 * instead of drawing 1280x720 and rescaling into 800x600.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include "display.h"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("  [FAIL] %s\n", msg); failures++; } \
    else         { printf("  [PASS] %s\n", msg); } } while (0)

int main(void) {
    zxv_display_t d;
    printf("Display mode negotiation\n");

    printf("firmware-owned scanout is adopted NATIVELY:\n");
    CHECK(zxv_display_negotiate(&d, 800, 600, 800, 0, 0) == 0, "GOP 800x600 negotiates");
    CHECK(d.w == 800 && d.h == 600,
          "we compose at 800x600 — NOT 1280x720 rescaled into it");
    CHECK(d.firmware_owned, "flagged firmware-owned (do not touch ramfb)");
    CHECK(d.origin == ZXV_DISP_FIRMWARE, "origin recorded");

    CHECK(zxv_display_negotiate(&d, 1920, 1080, 2048, 0, 0) == 0, "GOP FHD negotiates");
    CHECK(d.stride == 2048, "a padded firmware stride is preserved, not assumed == width");

    printf("a mode that cannot be backed is REFUSED, not clamped:\n");
    /* These assert the REFUSAL path, so they only apply when the mode really is
     * over budget. Built with the budget raised they would (correctly) succeed,
     * which is a different fact and is checked below instead. */
#if ZXV_DISPLAY_MAX_H < 4320u
    CHECK(zxv_display_negotiate(&d, 7680, 4320, 7680, 0, 0) < 0,
          "8K firmware mode is refused at the default budget (126 MB scanout)");
#endif
#if ZXV_DISPLAY_MAX_H < 2160u
    CHECK(zxv_display_negotiate(&d, 0, 0, 0, 3840, 2160) < 0,
          "an over-budget REQUEST is refused, so no one lays out for a screen they did not get");
#endif
    printf("       (4K needs %llu MB of scanout, 8K needs %llu MB — per buffer)\n",
           (unsigned long long)(zxv_display_bytes(3840,2160) >> 20),
           (unsigned long long)(zxv_display_bytes(7680,4320) >> 20));

    printf("explicit request, and the default:\n");
    CHECK(zxv_display_negotiate(&d, 0, 0, 0, 1024, 768) == 0 &&
          d.w == 1024 && d.h == 768 && d.origin == ZXV_DISP_REQUESTED,
          "an in-budget request is honoured exactly");
    CHECK(zxv_display_negotiate(&d, 0, 0, 0, 0, 0) == 0, "default negotiates");
    CHECK(d.w == ZXV_DISPLAY_MAX_W && d.h == ZXV_DISPLAY_MAX_H,
          "default is the LARGEST mode that fits the budget, not the old 1280x720");
    printf("       (default negotiated: %s, UI scale %u permille)\n",
           d.name ? d.name : "?", d.scale_permille);

    printf("detail, not just pixels:\n");
    zxv_display_negotiate(&d, 0, 0, 0, 1920, 1080);
    uint32_t big = zxv_display_scaled(&d, 100);
    zxv_display_negotiate(&d, 0, 0, 0, 1280, 720);
    uint32_t base = zxv_display_scaled(&d, 100);
    zxv_display_negotiate(&d, 0, 0, 0, 640, 480);
    uint32_t small = zxv_display_scaled(&d, 100);
    CHECK(big > base && base > small,
          "UI scale RISES with resolution, so more pixels means bigger elements not smaller");
    CHECK(base == 100, "1280x720 stays 1x (the layout it was designed against)");
    printf("       (640x480 %u | 1280x720 %u | 1920x1080 %u for a 100px metric)\n",
           small, base, big);
    zxv_display_negotiate(&d, 0, 0, 0, 1920, 1080);
    CHECK(zxv_display_scaled(&d, 3) == 5, "scaling rounds to nearest (3px border -> 5px, not 4)");

    printf("4K and 8K are known modes, admitted when the budget allows:\n");
    {   int have4k = 0, have8k = 0; uint32_t w=0,h=0; const char *nm=0;
        for (uint32_t i = 0; i < zxv_display_mode_count(); i++) {
            zxv_display_mode(i, &w, &h, &nm);
            if (w == 3840 && h == 2160) have4k = 1;
            if (w == 7680 && h == 4320) have8k = 1;
        }
        CHECK(have4k, "3840x2160 (4K UHD) is in the mode table");
        CHECK(have8k, "7680x4320 (8K UHD) is in the mode table");
#if ZXV_DISPLAY_MAX_H >= 4320u
        CHECK(zxv_display_negotiate(&d, 0, 0, 0, 7680, 4320) == 0 && d.h == 4320,
              "with the budget raised, 8K IS honoured (the knob is real)");
        CHECK(d.scale_permille >= 3000,
              "8K scales the UI up hard, or every control is a speck");
        CHECK(zxv_display_negotiate(&d, 0, 0, 0, 3840, 2160) == 0 &&
              d.scale_permille >= 2000, "4K scales up too");
#endif
    }

    printf("the mode table:\n");
    CHECK(zxv_display_mode_count() >= 8, "several standard modes are offered");
    {   uint32_t w = 0, h = 0; const char *nm = 0; int ok = 1, aligned = 1;
        uint64_t prev_area = (uint64_t)-1;      /* start at "infinity", not 0 */
        for (uint32_t i = 0; i < zxv_display_mode_count(); i++) {
            zxv_display_mode(i, &w, &h, &nm);
            uint64_t area = (uint64_t)w * h;
            if (area > prev_area) ok = 0;       /* largest first */
            if (w & 15u) aligned = 0;
            prev_area = area;
        }
        CHECK(ok, "modes are ordered largest first");
        CHECK(aligned, "every width is a multiple of 16 (a bad stride shears the image)");
        CHECK(!zxv_display_mode(999, &w, &h, &nm), "out-of-range mode index is refused"); }

    printf("\n%s display: %d failure(s)\n", failures ? "FAILED" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
