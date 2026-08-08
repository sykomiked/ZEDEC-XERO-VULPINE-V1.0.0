/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* radial_map.h — graphics from the dimensional principle: a RADIAL mapping of
 * PRIME NUMBERS cross-referenced with a RADIAL mapping of FIBONACCI NUMBERS, the
 * pattern revealed across zoom levels.
 *
 * The lattice is a phyllotaxis (golden-angle, 360/PHI^2 ~ 137.5 deg) spiral —
 * which is exactly optimal disc/sphere packing. Each seed index is tested for
 * primality and for Fibonacci membership, so the same radial field shows where
 * primes and Fibonacci resonate. FRACTAL SPHERE PACKING: at each Fibonacci seed a
 * SMALLER golden spiral is nested — Fibonacci sublattices within Fibonacci — so
 * zooming reveals the same pattern at every scale (the one ratio, PHI, repeating
 * micro<->macro). Depth is shaded through `holo`. Fixed-point, no libm. */
#ifndef ZXV_RADIAL_MAP_H
#define ZXV_RADIAL_MAP_H

#include <stdint.h>

/* Render the radial prime x Fibonacci field into a w*h ARGB framebuffer. zoom_num
 * / zoom_den scales the lattice (reveal the pattern at different zoom levels);
 * sub_depth is how many levels of nested Fibonacci sublattices to draw (0..2). */
void radial_map_render(uint32_t *fb, int w, int h, int zoom_num, int zoom_den, int sub_depth);

/* Register the radial map as a bootable graphics MegaROM. */
void radial_map_register_megarom(void);

/* On-target self-check: render the field and verify it is coherent — a radial
 * lattice (denser toward the centre), prime and Fibonacci seeds distinctly
 * marked, and nested sublattices present. Returns 1 on pass. */
int  radial_map_selfcheck(void);

#endif /* ZXV_RADIAL_MAP_H */
