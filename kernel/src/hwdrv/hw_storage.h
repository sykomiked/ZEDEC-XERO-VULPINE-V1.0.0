/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* hw_storage.h — GPT partitions and FAT32 on class-driver disks. */
#ifndef ZXV_HW_STORAGE_H
#define ZXV_HW_STORAGE_H

#include "hwdrv.h"
#include "../../include/blockdev.h"
#include "../fat32/fat32.h"

/* hw_gpt_find_esp results */
#define HW_GPT_OK     0
#define HW_GPT_NONE   -1 /* no "EFI PART" header: not a GPT disk */
#define HW_GPT_IO     -2 /* the disk refused a read */
#define HW_GPT_BAD    -3 /* header or partition array failed CRC32 or bounds */
#define HW_GPT_NO_ESP -4 /* valid GPT without an EFI system partition (or the type asked for) */

typedef struct hw_part {
    uint32_t index; /* 1-based partition number */
    uint64_t first, last;
    char name[37]; /* GPT partition name, ASCII ('?' for anything else) */
} hw_part_t;

/* CRC-32/IEEE (reflected 0xEDB88320), chainable: crc = hw_crc32(crc, ...). */
uint32_t hw_crc32(uint32_t crc, const uint8_t *p, uint32_t n);

/* Find the first EFI system partition. scratch holds one block (<= 4096). */
int hw_gpt_find_esp(hw_blk_t *b, hw_part_t *out, uint8_t *scratch);

/* Find the first partition of a type GUID (on-disk byte order); same checks
 * and results as hw_gpt_find_esp, HW_GPT_NO_ESP when there is none. */
int hw_gpt_find_type(hw_blk_t *b, const uint8_t type[16], hw_part_t *out, uint8_t *scratch);

/* The type of a swarm disc's model partition (mk_golden_disc.sh). */
extern const uint8_t HW_GPT_ZXV_SWARM[16];

/* A block_device_t over blocks [base, base+count) of a 512-byte-sector disk. */
typedef struct hw_bdev {
    block_device_t bd;
    hw_blk_t *b;
    uint64_t base, count;
} hw_bdev_t;
int hw_bdev_init(hw_bdev_t *d, hw_blk_t *b, uint64_t base, uint64_t count);

/* For each registered disk: find the ESP, mount it as FAT32, read the root
 * file `path` (8.3 name, e.g. "MEGAROM.TVL"). One log line per disk:
 * [STORAGE OK|NONE|FAILED]. Returns how many disks mounted. */
uint32_t hw_storage_selftest(const hw_plat_t *p, hw_registry_t *r, const char *path);

#endif
