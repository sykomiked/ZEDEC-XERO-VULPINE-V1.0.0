/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* holo.h — the nonlinear HOLOGRAPHIC shading primitive, extracted from
 * zxv_present so every rendering layer (games, the MegaROM, UI-as-MegaROMs) can
 * apply the same depth-on-a-flat-screen effect. Two real, composable cues:
 *
 *   1. COLOUR / ANTI-COLOUR SHIMMER — each channel is pushed toward its
 *      complement (255-c) by a signed valence v (the phase-tick interference
 *      field, S- .. S+). The term (255 - 2c) is grey-stable: a mid-grey pixel
 *      (128) barely moves, saturated pixels swing toward their anti-colour.
 *      This is the nonlinear tint/shade.
 *   2. CHROMOSTEREOPSIS — near pixels go WARM (+red / -blue), far pixels go COOL
 *      (-red / +blue). The eye focuses long (red) and short (blue) wavelengths
 *      at slightly different depths, so warm advances and cool recedes: genuine
 *      holographic depth on a 2D display, no glasses.
 *
 * Integer-only, branch-light, inlinable (called per pixel in the hot path).
 * valence v in ~[-200,200]; depth_z >0 = near, <0 = far; the two _k are the
 * shimmer strength (right shift) and the chromostereopsis strength. */
#ifndef ZXV_HOLO_H
#define ZXV_HOLO_H

#include <stdint.h>

static inline uint32_t holo_shade(uint32_t rgb, int32_t v, int32_t depth_z,
                                  int32_t shimmer_k, int32_t depth_k){
    int32_t r = (int32_t)((rgb >> 16) & 0xFF);
    int32_t g = (int32_t)((rgb >> 8)  & 0xFF);
    int32_t b = (int32_t)( rgb        & 0xFF);
    r += (v * (255 - 2 * r)) >> shimmer_k;      /* toward anti-colour, grey-stable */
    g += (v * (255 - 2 * g)) >> shimmer_k;
    b += (v * (255 - 2 * b)) >> shimmer_k;
    r += (depth_z * depth_k) >> 8;              /* near warm / far cool */
    b -= (depth_z * depth_k) >> 8;
    if (r < 0) r = 0; else if (r > 255) r = 255;
    if (g < 0) g = 0; else if (g > 255) g = 255;
    if (b < 0) b = 0; else if (b > 255) b = 255;
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

/* Boot/host self-check: verifies the optics — near→warm, far→cool, grey-stable
 * shimmer, and no overflow. Returns 1 on pass. */
int holo_selfcheck(void);

#endif /* ZXV_HOLO_H */
