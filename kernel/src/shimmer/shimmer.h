/* shimmer.h — ZXV desktop shimmer field ("a trick of the light")
 *
 * The desktop ground must read as a LIVING surface, not a static fill:
 * a slow caustic interference that drifts across the midnight-emerald
 * backdrop with occasional gold glints, the way light moves on water
 * or through a gemstone.
 *
 * Two properties matter architecturally:
 *
 *  1. IT IS EVENT-DRIVEN, NOT CLOCK-DRIVEN. shimmer_advance() is called
 *     once per kernel event cycle (the phase tick). The field's whole
 *     state is a single integer phase counter, so the animation is a
 *     pure function of causal event count. Identical event sequence =>
 *     identical frames. Nothing here reads a wall clock, which keeps it
 *     inside the external-clock-bridge rule.
 *
 *  2. IT IS INTEGER-ONLY. No float, no libc, no tables larger than a
 *     256-entry sine LUT. Sampling a pixel is a handful of adds, shifts
 *     and table lookups, so it is affordable per-pixel on a soft
 *     framebuffer with no GPU.
 *
 * The field is a sum of three travelling plane waves at mutually
 * incommensurate angles plus a domain warp. Incommensurate frequencies
 * mean the pattern never visibly repeats — the eye reads it as organic
 * rather than as a loop.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)  (ZXV shimmer slice)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#ifndef ZXV_SHIMMER_H
#define ZXV_SHIMMER_H

#include <stdint.h>
#include <stdbool.h>

/* Fixed-point: Q8.8 for wave accumulation, LUT is 256 entries/turn. */
#define SHM_LUT_BITS   8
#define SHM_LUT_SIZE   (1 << SHM_LUT_BITS)      /* 256 */
#define SHM_LUT_MASK   (SHM_LUT_SIZE - 1)

typedef struct {
    uint32_t width, height;

    /* the entire animation state: one causal counter */
    uint32_t phase;          /* advanced once per event cycle */

    /* per-wave spatial frequencies and drift rates (Q8.8-ish integers) */
    int32_t  ax[3], ay[3];   /* spatial frequency along x / y */
    int32_t  w[3];           /* phase drift per event cycle */

    /* domain warp strength — bends the wavefronts so they look caustic
     * rather than like a plaid of straight lines */
    int32_t  warp;

    /* spatial scale: coordinates are shifted right by `scale` before
     * sampling, so features grow 2^scale wider. A desktop wants broad,
     * slow ribbons (scale 3-4); a small panel wants finer detail. */
    uint8_t  scale;

    /* output shaping */
    uint8_t  amplitude;      /* 0-255 overall shimmer strength */
    uint8_t  glint;          /* 0-255 gold-glint threshold sharpness */

    /* stats */
    uint64_t frames;
} shimmer_t;

/* Signed sine LUT, amplitude +/-127, indexed by an 8-bit turn. */
int32_t shm_sin(uint32_t turn8);

/* Initialize. `seed` decorrelates the wave set so two surfaces on screen
 * (e.g. desktop vs a panel) shimmer independently. */
void shimmer_init(shimmer_t *s, uint32_t width, uint32_t height, uint32_t seed);

/* Advance exactly one event cycle. Call from the kernel event loop —
 * never from a timer poll. */
void shimmer_advance(shimmer_t *s);

/* Sample the field at a pixel. Returns 0..255. Deterministic for a given
 * (phase, x, y): the same event count always yields the same frame. */
uint8_t shimmer_sample(const shimmer_t *s, int32_t x, int32_t y);

/* Composite the field over a base ARGB colour: lifts lightness subtly and
 * adds a gold glint at the field's peaks. Returns the shaded ARGB. */
uint32_t shimmer_shade(const shimmer_t *s, uint32_t base_argb,
                       int32_t x, int32_t y);

/* Tuning */
void shimmer_set_amplitude(shimmer_t *s, uint8_t amplitude);
void shimmer_set_glint(shimmer_t *s, uint8_t glint);
void shimmer_set_scale(shimmer_t *s, uint8_t scale);

#endif /* ZXV_SHIMMER_H */
