/* tvl_bringup.h — the TOL VOVINA UPAAH LOT roll-call.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 *
 * WHY THIS FILE EXISTS. tvl_geom, tvl_raster, tvl_stereo and tvl_rom each ship
 * a *_selfcheck(), and all four were PRESENT in kernel_arm64.elf while NOTHING
 * CALLED ANY OF THEM: two survived on __attribute__((used, retain)) and two on
 * -Wl,--undefined GC roots. Retention is not execution. A self-check nothing
 * runs proves nothing at boot, so this is the single arch-neutral entry point
 * that actually runs them and prints one evidence line per module — the same
 * discipline boot_features.c already applies to the platform layer.
 *
 * It emits through a puts callback the caller supplies (no arch dependency, no
 * libc) and returns the number of modules that self-checked clean, out of 4.
 */
#ifndef ZXV_TVL_BRINGUP_H
#define ZXV_TVL_BRINGUP_H

typedef void (*tvl_puts_t)(const char *);

/* Runs tvl_geom_selfcheck, tvl_raster_selfcheck, tvl_stereo_selfcheck and
 * tvl_rom_selfcheck, in that order (geometry first — raster and stereo are
 * oriented BY it). Prints the failure bitmask when a module reports one, so a
 * failure names itself. Returns how many of the 4 passed. */
unsigned tvl_bringup(tvl_puts_t puts);

#endif /* ZXV_TVL_BRINGUP_H */
