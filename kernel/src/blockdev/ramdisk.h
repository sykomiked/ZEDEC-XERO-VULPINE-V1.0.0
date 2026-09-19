/* ramdisk.h — shared, arch-neutral RAM-backed block device.
 *
 * The declaration and bring-up live in ramdisk.c; this header is the API the
 * filesystem layer (vfs/zxvfs/fat32) calls. It is DISTINCT from the ARM64-only
 * kernel/arch/arm64/ramdisk.h: that one is arm64's own provider of
 * blockdev_ready, this one is the shared kernel/src provider the four non-arm64
 * arches wire in. Two providers of blockdev_ready at the same contract is
 * ALTERNATIVE PROVISION and is legal (modbind.h:146-153); the symbols here are
 * prefixed zxv_ramdisk_ precisely so the two can never collide.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#ifndef ZXV_SRC_RAMDISK_H
#define ZXV_SRC_RAMDISK_H

#include <stdint.h>
#include <stdbool.h>
#include "blockdev.h"

/* Idempotent; safe to call more than once. */
void      zxv_ramdisk_init(void);

/* Is the backing store present? Reads real state (see ramdisk.c). */
bool      zxv_ramdisk_is_present(void);

/* Total addressable sectors of BLOCKDEV_SECTOR_SIZE bytes each. */
uint32_t  zxv_ramdisk_total_sectors(void);

/* Sector I/O over the real RAM buffer. Return 0 on success, -1 if lba is out
 * of range. Arch-neutral: uint32_t offset arithmetic only, no 64-bit divide. */
int       zxv_ramdisk_read_sector(uint32_t lba, uint8_t *buffer);
int       zxv_ramdisk_write_sector(uint32_t lba, const uint8_t *buffer);

/* Direct pointer to the backing store (for BPB parsing / zero-copy mount). */
uint8_t  *zxv_ramdisk_get_buffer(void);

/* Lay down a minimal, mountable FAT32 image (same on-disk layout the ARM64
 * ramdisk writes). Optional: the block device works without it. */
void      zxv_ramdisk_format_fat32(void);

/* Fill a generic block_device_t whose callbacks route to the functions above. */
void      zxv_ramdisk_create_blockdev(block_device_t *dev);

/* FALSIFIABLE self-check: verifies the buffer exists, that a write-then-read
 * round-trips on a live sector (non-destructively), and that out-of-range LBAs
 * are rejected. Returns 0 only if every assertion held; non-zero otherwise.
 * Callable standalone, with no modbind framework present. */
int       zxv_ramdisk_selfcheck(void);

#endif /* ZXV_SRC_RAMDISK_H */
