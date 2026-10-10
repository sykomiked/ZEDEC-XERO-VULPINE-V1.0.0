/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* lattice_dimensions.h — drive the 13-space lattice desktop from the dimensional
 * ladder. The pattern is UNIVERSAL: the same 0d..13d ladder that structures the
 * cosmos structures the desktop.
 *
 * The 13 lattice spaces (BASE, VINO, FLEET, STUDIO, CHIGLET, REFINERY, CONCORD,
 * CROWN, WYRMGATE, RAM, INTERSPACE, BADGE, LOGISTICS) are bound to dimensions
 * 0d..12d, and the DESKTOP ITSELF is 13d — the universal container, the gateway.
 * That container is the MegaROM: the bootable UI layer that holds the 13 spaces
 * and, alongside them, the graphics-engine MegaROMs (Cinder, Quill) and the
 * radial field. The Fibonacci-numbered dimensions {0,1,2,3,5,8} are the PRIMARY
 * (anchor) spaces; the desktop (13, Fibonacci) is the primary container. Spaces
 * are laid out by the golden angle (phyllotaxis) — the desktop is the dimensional
 * ladder made navigable, in perfect micro<->macro correspondence with everything
 * else the ladder governs (M5 axes, the economy, Chiglet, the render lineage). */
#ifndef ZXV_LATTICE_DIMENSIONS_H
#define ZXV_LATTICE_DIMENSIONS_H

#include <stdint.h>

#define LD_SPACES 13

typedef struct space_dim {
    int          space;    /* lattice node index 0..12                          */
    const char  *name;     /* the space's name                                  */
    const char  *domain;   /* its domain <-> its dimension's essence            */
    int          dim;      /* the dimension it is bound to (0..12)              */
    uint8_t      primary;  /* 1 iff a Fibonacci (prime/primary) dimension       */
} space_dim_t;

/* The binding table (13 spaces -> dimensions 0..12). */
const space_dim_t *lattice_dimensions(int *count_out);

/* Render the dimensional desktop: the 13 spaces arranged by the golden angle at
 * PHI-scaled radii, primary (Fibonacci) spaces ringed, the 13d container drawn as
 * the field that holds them. Into a w*h ARGB framebuffer. */
void lattice_dim_render(uint32_t *fb, int w, int h);

/* Register the dimensional desktop as the MegaROM (13d = the universal container
 * that holds the 13 spaces and the graphics MegaROMs). */
void lattice_dim_register_megarom(void);

/* On-target self-check: 13 spaces bound to 13 distinct dimensions; the primary
 * flags are exactly the Fibonacci dimensions; the desktop container is 13d; and
 * the desktop renders coherently. Returns 1 on pass; *primaries_out = count of
 * primary (Fibonacci) spaces. */
int  lattice_dim_selfcheck(uint32_t *primaries_out);

#endif /* ZXV_LATTICE_DIMENSIONS_H */
