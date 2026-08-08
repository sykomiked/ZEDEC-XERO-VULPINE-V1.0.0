/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* holo.c — self-check for the holographic shading primitive. See holo.h. */
#include "holo.h"

#define R(c) (((c) >> 16) & 0xFF)
#define G(c) (((c) >>  8) & 0xFF)
#define B(c) ( (c)        & 0xFF)

int holo_selfcheck(void){
    int m = 0;
    uint32_t grey = 0x808080u;

    /* [1] NEAR -> WARM: red rises, blue falls (chromostereopsis, +red/-blue). */
    { uint32_t n = holo_shade(grey, 0, +100, 6, 36);
      if (R(n) > 0x80 && B(n) < 0x80) m |= 1; }

    /* [2] FAR -> COOL: red falls, blue rises (-red/+blue). */
    { uint32_t f = holo_shade(grey, 0, -100, 6, 36);
      if (R(f) < 0x80 && B(f) > 0x80) m |= 2; }

    /* [3] GREY-STABLE shimmer: a mid-grey pixel barely moves under strong
     *     valence (the 255-2c term ~ -1 at c=128), so tinting is anchored. */
    { uint32_t s = holo_shade(grey, 200, 0, 6, 0);
      int dr = (int)R(s) - 0x80; if (dr < 0) dr = -dr;
      if (dr <= 6) m |= 4; }

    /* [4] ANTI-COLOUR + CLAMP: saturated red under positive valence swings
     *     toward its complement (cyan) and never overflows. */
    { uint32_t a = holo_shade(0xFF0000u, 200, 0, 6, 0);
      if (R(a) < 0x40 && G(a) > 0xC0 && B(a) > 0xC0) m |= 8; }

    return (m == 15) ? 1 : 0;
}
