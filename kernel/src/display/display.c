/* display.c — display mode negotiation. See display.h.
 *
 * Freestanding: integer only, no libc, no allocation, no float.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)  (ZXV display slice)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#include "display.h"

/* Standard modes, largest first, so "the best that fits" is a linear scan.
 * Widths are multiples of 16 — a stride that is not keeps ramfb and several
 * scanout engines shearing the image, which is a bug that looks like a driver
 * problem and is really an arithmetic one. */
typedef struct { uint32_t w, h; const char *name; } mode_t;
static const mode_t MODES[] = {
    { 7680, 4320, "7680x4320" },   /* 8K UHD    — 126.6 MB scanout           */
    { 5120, 2880, "5120x2880" },   /* 5K                                     */
    { 3840, 2160, "3840x2160" },   /* 4K UHD    —  31.6 MB scanout           */
    { 3440, 1440, "3440x1440" },   /* ultrawide                              */
    { 2560, 1440, "2560x1440" },   /* QHD       —  14.1 MB, the default cap  */
    { 1920, 1200, "1920x1200" },   /* 16:10                                  */
    { 1920, 1080, "1920x1080" },   /* FHD                                    */
    { 1600,  900, "1600x900"  },
    { 1280, 1024, "1280x1024" },   /* 5:4 — more pixels than 1440x900        */
    { 1440,  900, "1440x900"  },
    { 1360,  768, "1360x768"  },   /* the aligned form of the 1366 laptop
                                    * panel: 1366 is NOT a multiple of 16 and
                                    * shears on scanout engines that assume a
                                    * 16-pixel stride granule                */
    { 1280,  720, "1280x720"  },   /* HD — the previous hardcoded default    */
    { 1024,  768, "1024x768"  },
    {  800,  600, "800x600"   },   /* what UEFI GOP typically hands us       */
    {  640,  480, "640x480"   },
};
#define NMODES (sizeof(MODES) / sizeof(MODES[0]))

/* UI scale from vertical resolution. A taller panel is usually a bigger or
 * denser one, so elements grow with it — otherwise more pixels just means
 * everything is smaller, which is not more detail. */
static uint32_t scale_for(uint32_t h) {
    if (h >= 4320u) return 4000u;   /* 8K: 4x, or every control is a speck   */
    if (h >= 2160u) return 2500u;   /* 4K: 2.5x                              */
    if (h >= 1440u) return 1750u;   /* QHD                                   */
    if (h >= 1080u) return 1500u;   /* 1.5x  */
    if (h >=  900u) return 1250u;
    if (h >=  700u) return 1000u;   /* 1x    */
    return 875u;                    /* small panels: tighten slightly */
}

static bool fits(uint32_t w, uint32_t h) {
    if (w == 0 || h == 0) return false;
    if (w > ZXV_DISPLAY_MAX_W || h > ZXV_DISPLAY_MAX_H) return false;
    return (uint64_t)w * h <= ZXV_DISPLAY_MAX_PIXELS;
}

uint32_t zxv_display_mode_count(void) { return (uint32_t)NMODES; }

bool zxv_display_mode(uint32_t idx, uint32_t *w, uint32_t *h, const char **name) {
    if (idx >= NMODES) return false;
    if (w) *w = MODES[idx].w;
    if (h) *h = MODES[idx].h;
    if (name) *name = MODES[idx].name;
    return true;
}

static const char *name_for(uint32_t w, uint32_t h) {
    for (uint32_t i = 0; i < NMODES; i++)
        if (MODES[i].w == w && MODES[i].h == h) return MODES[i].name;
    return "custom";
}

int zxv_display_negotiate(zxv_display_t *d,
                          uint32_t fw_w, uint32_t fw_h, uint32_t fw_stride,
                          uint32_t req_w, uint32_t req_h) {
    if (!d) return -1;
    for (unsigned i = 0; i < sizeof(*d); i++) ((uint8_t *)d)[i] = 0;

    /* 1. FIRMWARE-OWNED. The firmware has a live scanout and knows the panel;
     * adopt its exact geometry and compose natively into it. Rescaling a
     * different-sized composition into it is what made the desktop look soft. */
    if (fw_w && fw_h) {
        if (!fits(fw_w, fw_h)) return -2;   /* cannot back it with our scanout */
        /* The stride comes from firmware: a row shorter than the width would
         * make rows overlap, and a padded row can push stride * h past the
         * static scanout even when w * h fits. */
        uint32_t stride = fw_stride ? fw_stride : fw_w;
        if (stride < fw_w) return -2;
        if ((uint64_t) stride * fw_h > ZXV_DISPLAY_MAX_PIXELS) return -2;
        d->w = fw_w; d->h = fw_h;
        d->stride = stride;
        d->origin = ZXV_DISP_FIRMWARE;
        d->firmware_owned = true;
        d->scale_permille = scale_for(fw_h);
        d->name = name_for(fw_w, fw_h);
        return 0;
    }

    /* 2. REQUESTED. Honoured only if it fits; REFUSED rather than silently
     * clamped, so a caller never lays out for a screen it did not get. */
    if (req_w && req_h) {
        if (!fits(req_w, req_h)) return -3;
        d->w = req_w; d->h = req_h; d->stride = req_w;
        d->origin = ZXV_DISP_REQUESTED;
        d->scale_permille = scale_for(req_h);
        d->name = name_for(req_w, req_h);
        return 0;
    }

    /* 3. DEFAULT: the largest standard mode within budget. */
    for (uint32_t i = 0; i < NMODES; i++) {
        if (!fits(MODES[i].w, MODES[i].h)) continue;
        d->w = MODES[i].w; d->h = MODES[i].h; d->stride = MODES[i].w;
        d->origin = ZXV_DISP_DEFAULT;
        d->scale_permille = scale_for(MODES[i].h);
        d->name = MODES[i].name;
        return 0;
    }
    return -4;
}

/* ---- DECLARATION -----------------------------------------------------------
 * Pure mode arithmetic over a constant table -- it touches no hardware, so it
 * requires nothing, and the scanout layer requires IT (kernel_main negotiates
 * the geometry before ramfb_init is handed w/h).
 *
 * The bring-up asserts the invariant the table exists to hold: every negotiated
 * width is a multiple of 16. A width that is not shears the image on scanout
 * engines that assume a 16-pixel stride granule -- a bug that presents as a
 * driver fault and is really an arithmetic one. */
#include "zxv_decl.h"

static int display_bringup(void) {
    zxv_display_t d;
    if (zxv_display_negotiate(&d, 0, 0, 0, 0, 0) != 0) return -1;
    if (d.w == 0 || d.h == 0 || d.stride < d.w) return -1;
    if ((d.w & 15u) != 0u) return -1;
    if (d.scale_permille == 0u) return -1;
    return 0;
}

ZXV_DECLARE(display,
    ZXV_PROVIDES(display_mode_ready),
    ZXV_REQUIRES_NONE,
    ZXV_BRINGUP(display_bringup));
