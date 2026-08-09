/* display.h — display mode negotiation: one desktop, many screens
 *
 * THE BOTTLENECK THIS REMOVES
 * ---------------------------
 * The desktop was built against a compile-time 1280x720. That constant sized
 * the scanout buffer AND appeared literally at every call site — the
 * compositor, the VBE surface, the lattice renderer, the pointer bounds. A
 * single hardcoded resolution has two costs, and the second is worse:
 *
 *   1. the screen is whatever we guessed, on every machine.
 *   2. when the firmware owns the scanout (UEFI GOP), the kernel composed at
 *      1280x720 and then RESCALED into the firmware's mode. On an 800x600 GOP
 *      that is a downscale of an image drawn for a different screen: soft
 *      edges, unreadable text, wasted pixels. The desktop looked small and
 *      blurry not because 800x600 is small, but because nothing was ever drawn
 *      at 800x600.
 *
 * So the fix is not a bigger constant. It is to make the resolution a RUNTIME
 * FACT that everything reads, and to compose NATIVELY at whatever that fact
 * turns out to be.
 *
 * NEGOTIATION, NOT ASSUMPTION
 * ---------------------------
 * Three cases, in priority order:
 *   FIRMWARE-OWNED  the firmware already has a live scanout (GOP). Adopt its
 *                   exact geometry. It knows what the panel is; we do not, and
 *                   rescaling into it is strictly worse than drawing into it.
 *   REQUESTED       a caller asks for a specific mode (a config, a user).
 *                   Honoured if it fits the scanout budget.
 *   DEFAULT         pick the largest standard mode that fits the budget.
 *
 * In every case the chosen mode must fit ZXV_DISPLAY_MAX_PIXELS, because the
 * scanout is a statically-sized buffer in a freestanding kernel with no
 * allocator. A mode that would not fit is REFUSED rather than clamped silently:
 * a caller that asked for 4K and got 720p without being told would draw a
 * layout for a screen that does not exist.
 *
 * DETAIL, NOT JUST SIZE
 * ---------------------
 * More pixels alone makes everything smaller, which is the opposite of "more
 * detail" — it is the same detail, harder to see. Each mode therefore carries a
 * UI SCALE in permille, so a 4K desktop draws bigger elements rather than
 * identical elements at a quarter of the apparent size. Callers multiply their
 * layout metrics by it. Scale is chosen from vertical resolution, which tracks
 * how far away a panel of that size is usually viewed from.
 *
 * Freestanding: integer only, no libc, no allocation, no float.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)  (ZXV display slice)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#ifndef ZXV_DISPLAY_H
#define ZXV_DISPLAY_H

#include <stdint.h>
#include <stdbool.h>

/* ---- the scanout budget, and why it is a build-time choice ----------------
 * The scanout is a STATIC buffer: this is a freestanding kernel with no
 * allocator, so the ceiling is fixed at compile time and every negotiation
 * respects it. The cost is not small and it is not linear in "how modern the
 * screen sounds" — it is 4 bytes per pixel, twice over once you count the back
 * buffer the compositor draws into:
 *
 *     mode                 scanout     + back buffer
 *     1920x1080  FHD          7.9 MB        15.8 MB
 *     2560x1440  QHD         14.1 MB        28.1 MB
 *     3440x1440  ultrawide   18.9 MB        37.8 MB
 *     3840x2160  4K UHD      31.6 MB        63.3 MB
 *     7680x4320  8K UHD     126.6 MB       253.1 MB
 *
 * 8K therefore needs a quarter of a gigabyte of RAM before the kernel has done
 * anything else, which is why it is NOT the default: a machine booted with
 * -m 512M would spend half its memory on a framebuffer. The modes are all in
 * the table and all selectable; what changes is whether the scanout can back
 * them. Raise the budget deliberately, knowing the cost:
 *
 *     -DZXV_DISPLAY_MAX_W=3840 -DZXV_DISPLAY_MAX_H=2160   (4K, needs ~64 MB)
 *     -DZXV_DISPLAY_MAX_W=7680 -DZXV_DISPLAY_MAX_H=4320   (8K, needs ~253 MB)
 *
 * A mode beyond the budget is REFUSED with a reason, never silently clamped —
 * so "I asked for 4K and got 4K" and "I asked for 4K and got 1440p" are
 * distinguishable facts rather than a mystery about why the layout looks wrong.
 * Default is QHD: a real step up from the old hardcoded 720p, and still sane on
 * a 512 MB machine. */
#ifndef ZXV_DISPLAY_MAX_W
#define ZXV_DISPLAY_MAX_W       2560u
#endif
#ifndef ZXV_DISPLAY_MAX_H
#define ZXV_DISPLAY_MAX_H       1440u
#endif
#define ZXV_DISPLAY_MAX_PIXELS  ((uint64_t)ZXV_DISPLAY_MAX_W * ZXV_DISPLAY_MAX_H)

/* Bytes of scanout a mode needs (one buffer, 32bpp). The compositor needs this
 * twice: once to draw into and once to present. */
static inline uint64_t zxv_display_bytes(uint32_t w, uint32_t h) {
    return (uint64_t)w * h * 4u;
}

typedef enum {
    ZXV_DISP_NONE = 0,      /* nothing negotiated yet          */
    ZXV_DISP_FIRMWARE,      /* firmware owns the scanout (GOP) */
    ZXV_DISP_REQUESTED,     /* an explicit mode was honoured   */
    ZXV_DISP_DEFAULT        /* best standard mode that fits    */
} zxv_disp_origin_t;

typedef struct {
    uint32_t w, h;
    uint32_t stride;            /* pixels per row (>= w; firmware may pad) */
    uint32_t scale_permille;    /* UI scale: 1000 = 1x, 1500 = 1.5x, 2000 = 2x */
    zxv_disp_origin_t origin;
    bool     firmware_owned;    /* true => do NOT touch ramfb               */
    const char *name;           /* "1280x720" etc, for the boot log         */
} zxv_display_t;

/* How many standard modes are known. */
uint32_t zxv_display_mode_count(void);

/* Enumerate the standard modes, largest first. Returns false if idx is out of
 * range. Useful for a settings UI and for tests. */
bool zxv_display_mode(uint32_t idx, uint32_t *w, uint32_t *h, const char **name);

/* Negotiate. Pass firmware geometry when the firmware already has a scanout
 * (fw_w/fw_h/fw_stride non-zero); otherwise pass zeros. Pass req_w/req_h to
 * ask for a specific mode, or zeros to take the best that fits.
 * Returns 0 on success, <0 if nothing usable could be chosen. */
int zxv_display_negotiate(zxv_display_t *d,
                          uint32_t fw_w, uint32_t fw_h, uint32_t fw_stride,
                          uint32_t req_w, uint32_t req_h);

/* Scale a layout metric into the negotiated mode. Rounds to nearest, so a
 * 1.5x scale turns a 3px border into 5px rather than 4. */
static inline uint32_t zxv_display_scaled(const zxv_display_t *d, uint32_t v) {
    if (!d || d->scale_permille == 0) return v;
    return (v * d->scale_permille + 500u) / 1000u;
}

#endif /* ZXV_DISPLAY_H */
