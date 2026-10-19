/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* hwplat_x86.h — the x86_64 bare-metal platform for the class drivers. */
#ifndef ZXV_HWPLAT_X86_H
#define ZXV_HWPLAT_X86_H

#include "hwdrv/hwdrv.h"

/* Calibrate the delay and return the platform. log prints one line (the
 * caller adds nothing; this passes lines without a newline). tsc_per_us, when
 * not 0, receives the calibration result. */
const hw_plat_t *hwplat_x86_init(void (*log)(const char *), uint64_t *tsc_per_us);

/* Map physical MMIO uncached (identity, 2 MB pages; the low 1 GB is already
 * mapped and left as boot set it). Usable before hwplat_x86_init. 0 when the
 * mapping tables are full. */
volatile void *hwplat_x86_map_mmio(uint64_t phys, uint64_t size);

#endif
