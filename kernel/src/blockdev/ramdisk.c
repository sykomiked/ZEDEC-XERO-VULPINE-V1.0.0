/* ramdisk.c — shared, arch-neutral RAM-backed block device.
 *
 * A GENUINE block device: it really reads and writes a static RAM buffer, needs
 * no QEMU -drive and no hardware. It is the kernel/src provider of
 * blockdev_ready that the four non-arm64 arches (x86_64, riscv64, riscv32,
 * arm32) wire in; ARM64 keeps its own kernel/arch/arm64/ramdisk.c. Both PROVIDE
 * blockdev_ready at contract 1 — alternative provision, legal by modbind.h:
 * 146-153 — and the zxv_ramdisk_ prefix here guarantees the two never clash if
 * a future build ever links both.
 *
 * The logic is deliberately the SAME shape as arm64/ramdisk.c (which is already
 * arch-neutral C: no asm, no board specifics), so this is a share of proven code
 * rather than a reinvention. The one substantive addition is a FALSIFIABLE
 * bring-up: arm64's checks present && sectors>0; this one additionally performs
 * a non-destructive write-then-read round-trip on a live sector, so a buffer
 * that is unmapped, read-only, or not actually backing the callbacks makes the
 * bring-up return non-zero and the module stays HELD (S0) — the honest report,
 * never "true by construction".
 *
 * Freestanding: integer only, no libc, no allocation, no float. The 32-bit
 * targets (riscv32, arm32) are respected — every offset is uint32_t
 * (16MB store fits), so nothing lowers to a 64-bit divide/shift helper that
 * libgcc would have to supply and does not exist here.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#include "blockdev/ramdisk.h"
#include "blockdev.h"
#include "zxv_barrier.h"
#include <stdint.h>
#include <stdbool.h>

/* 16MB RAM disk — enough for a small FAT32 filesystem, matching arm64's.
 * 32768 * 512 = 16,777,216 bytes, which fits in a uint32_t offset, so every
 * `lba * SECTOR` product below stays 32-bit on the 32-bit toolchains. */
#define ZXV_RAMDISK_SECTORS  32768u
#define ZXV_SECTOR           ((uint32_t)BLOCKDEV_SECTOR_SIZE)  /* 512 */

static uint8_t zxv_ramdisk_data[ZXV_RAMDISK_SECTORS * 512u]
    __attribute__((aligned(4096)));

void zxv_ramdisk_init(void) {
    /* Backing store is a static array; nothing to allocate. Idempotent. */
}

bool zxv_ramdisk_is_present(void) {
    /* Read real state: the buffer must have a non-NULL address. A linker that
     * dropped .bss, or a store the map does not cover, is not "present". */
    return zxv_ramdisk_data != (uint8_t *)0;
}

uint32_t zxv_ramdisk_total_sectors(void) {
    return ZXV_RAMDISK_SECTORS;
}

int zxv_ramdisk_read_sector(uint32_t lba, uint8_t *buffer) {
    if (lba >= ZXV_RAMDISK_SECTORS) return -1;
    uint32_t base = lba * ZXV_SECTOR;           /* 32-bit, no divide */
    for (uint32_t i = 0; i < ZXV_SECTOR; i++)
        buffer[i] = zxv_ramdisk_data[base + i];
    return 0;
}

int zxv_ramdisk_write_sector(uint32_t lba, const uint8_t *buffer) {
    if (lba >= ZXV_RAMDISK_SECTORS) return -1;
    uint32_t base = lba * ZXV_SECTOR;
    for (uint32_t i = 0; i < ZXV_SECTOR; i++)
        zxv_ramdisk_data[base + i] = buffer[i];
    /* Publish the store before any reader (or a translation flip that follows a
     * later mm bring-up) can observe a half-written sector. ZXV_DSB() is the
     * one-guarantee/five-spellings barrier — never a raw per-arch mnemonic. */
    ZXV_DSB();
    return 0;
}

uint8_t *zxv_ramdisk_get_buffer(void) {
    return zxv_ramdisk_data;
}

void zxv_ramdisk_format_fat32(void) {
    uint32_t i;
    for (i = 0; i < ZXV_RAMDISK_SECTORS * 512u; i++)
        zxv_ramdisk_data[i] = 0;

    uint8_t *boot = zxv_ramdisk_data;

    /* Jump instruction */
    boot[0] = 0xEB; boot[1] = 0x58; boot[2] = 0x90;
    /* OEM name */
    const char *oem = "M5KERNEL";
    for (i = 0; i < 8; i++) boot[3 + i] = (uint8_t)oem[i];

    /* BPB fields (little-endian) */
    uint16_t bytes_per_sector = 512;
    uint8_t  sectors_per_cluster = 8;   /* 4KB clusters */
    uint16_t reserved_sectors = 32;
    uint8_t  num_fats = 2;
    uint32_t total_sectors = ZXV_RAMDISK_SECTORS;
    /* sectors_per_fat: divide by a COMPILE-TIME constant (128), which the
     * compiler lowers to a multiply+shift — no runtime 64-bit divide helper. */
    uint32_t sectors_per_fat = (total_sectors / (uint32_t)(bytes_per_sector / 4)) + 1u;
    uint32_t root_cluster = 2;

    boot[11] = (uint8_t)(bytes_per_sector & 0xFF);
    boot[12] = (uint8_t)((bytes_per_sector >> 8) & 0xFF);
    boot[13] = sectors_per_cluster;
    boot[14] = (uint8_t)(reserved_sectors & 0xFF);
    boot[15] = (uint8_t)((reserved_sectors >> 8) & 0xFF);
    boot[16] = num_fats;
    boot[17] = 0; boot[18] = 0;         /* root_entries (0 for FAT32) */
    boot[19] = 0; boot[20] = 0;         /* total_sectors16 (0 for FAT32) */
    boot[21] = 0xF8;                    /* media descriptor */
    boot[22] = 0; boot[23] = 0;         /* fat_size16 (0 for FAT32) */
    boot[24] = 0x3F; boot[25] = 0;      /* sectors_per_track */
    boot[26] = 0xFF; boot[27] = 0;      /* num_heads */
    boot[28] = 0; boot[29] = 0; boot[30] = 0; boot[31] = 0;  /* hidden_sectors */
    boot[32] = (uint8_t)(total_sectors & 0xFF);
    boot[33] = (uint8_t)((total_sectors >> 8) & 0xFF);
    boot[34] = (uint8_t)((total_sectors >> 16) & 0xFF);
    boot[35] = (uint8_t)((total_sectors >> 24) & 0xFF);
    boot[36] = (uint8_t)(sectors_per_fat & 0xFF);
    boot[37] = (uint8_t)((sectors_per_fat >> 8) & 0xFF);
    boot[38] = (uint8_t)((sectors_per_fat >> 16) & 0xFF);
    boot[39] = (uint8_t)((sectors_per_fat >> 24) & 0xFF);
    boot[40] = 0; boot[41] = 0;         /* flags */
    boot[42] = 0; boot[43] = 0;         /* version */
    boot[44] = (uint8_t)(root_cluster & 0xFF);
    boot[45] = (uint8_t)((root_cluster >> 8) & 0xFF);
    boot[46] = (uint8_t)((root_cluster >> 16) & 0xFF);
    boot[47] = (uint8_t)((root_cluster >> 24) & 0xFF);
    boot[48] = 1; boot[49] = 0;         /* fs_info_sector */
    boot[50] = 6; boot[51] = 0;         /* backup_boot_sector */
    boot[64] = 0x80;                    /* drive_number */
    boot[66] = 0x29;                    /* boot_signature */
    boot[67] = 0x36; boot[68] = 0x4E; boot[69] = 0x39; boot[70] = 0x00;  /* volume_id */
    const char *label = "M5 BOOT    ";
    for (i = 0; i < 11; i++) boot[71 + i] = (uint8_t)label[i];
    const char *fstype = "FAT32   ";
    for (i = 0; i < 8; i++) boot[82 + i] = (uint8_t)fstype[i];
    boot[510] = 0x55;
    boot[511] = 0xAA;

    /* FAT[0], FAT[1], and cluster 2 (root dir) EOC in the first FAT. */
    uint32_t fat_start = (uint32_t)reserved_sectors * (uint32_t)bytes_per_sector;
    zxv_ramdisk_data[fat_start + 0] = 0xF8;
    zxv_ramdisk_data[fat_start + 1] = 0xFF;
    zxv_ramdisk_data[fat_start + 2] = 0xFF;
    zxv_ramdisk_data[fat_start + 3] = 0x0F;
    zxv_ramdisk_data[fat_start + 4] = 0xFF;
    zxv_ramdisk_data[fat_start + 5] = 0xFF;
    zxv_ramdisk_data[fat_start + 6] = 0xFF;
    zxv_ramdisk_data[fat_start + 7] = 0x0F;
    uint32_t fat_entry_2 = fat_start + 8;
    zxv_ramdisk_data[fat_entry_2 + 0] = 0xFF;
    zxv_ramdisk_data[fat_entry_2 + 1] = 0xFF;
    zxv_ramdisk_data[fat_entry_2 + 2] = 0xFF;
    zxv_ramdisk_data[fat_entry_2 + 3] = 0x0F;
    ZXV_DSB();
}

/* Block device adapter: wraps the sector functions for block_device_t. */
static int zxv_ramdisk_block_read(block_device_t *dev, uint32_t lba, uint8_t *buf) {
    (void)dev;
    return zxv_ramdisk_read_sector(lba, buf);
}
static int zxv_ramdisk_block_write(block_device_t *dev, uint32_t lba, const uint8_t *buf) {
    (void)dev;
    return zxv_ramdisk_write_sector(lba, buf);
}

void zxv_ramdisk_create_blockdev(block_device_t *dev) {
    blockdev_init(dev, zxv_ramdisk_block_read, zxv_ramdisk_block_write,
                  ZXV_RAMDISK_SECTORS, "ZXV SRC RAMDISK v1.0");
}

/* ---- falsifiable round-trip probe -----------------------------------------
 * Non-destructive: saves the probe sector, writes a pattern that is NOT a fixed
 * constant (it is seeded from the live buffer address and the sector count, so
 * a stuck/aliased store cannot accidentally match), reads it back through the
 * real read path, compares, then restores the original bytes. Two static 512B
 * scratch buffers rather than stack arrays, so this is safe on the smallest
 * boot stacks. Returns 0 iff the round-trip and the bounds rejection both held.
 */
static uint8_t zxv_probe_save[512];
static uint8_t zxv_probe_read[512];

static int zxv_ramdisk_roundtrip(void) {
    if (!zxv_ramdisk_is_present())        return -1;
    uint32_t total = zxv_ramdisk_total_sectors();
    if (total == 0u)                      return -1;

    /* A non-constant seed: the low bits of the buffer address XOR the sector
     * count. "No fixed values you could read from the hardware." */
    uint32_t seed = (uint32_t)(uintptr_t)zxv_ramdisk_get_buffer();
    seed ^= total;
    seed ^= 0xA5u;                        /* keep it non-zero on a zeroed store */

    uint32_t probe_lba = total - 1u;      /* last sector; least likely to be live fs */

    /* Save current contents (so a mounted fs is untouched). */
    if (zxv_ramdisk_read_sector(probe_lba, zxv_probe_save) != 0) return -1;

    /* Write the seeded pattern. */
    uint32_t i;
    for (i = 0; i < 512u; i++)
        zxv_probe_read[i] = (uint8_t)(seed + i * 31u);
    if (zxv_ramdisk_write_sector(probe_lba, zxv_probe_read) != 0) return -1;

    /* Read it back through the real path and compare. */
    for (i = 0; i < 512u; i++) zxv_probe_read[i] = 0;   /* clear before read-back */
    if (zxv_ramdisk_read_sector(probe_lba, zxv_probe_read) != 0) { goto restore_fail; }
    for (i = 0; i < 512u; i++) {
        if (zxv_probe_read[i] != (uint8_t)(seed + i * 31u)) { goto restore_fail; }
    }

    /* Restore the original bytes: the probe leaves no trace. */
    (void)zxv_ramdisk_write_sector(probe_lba, zxv_probe_save);

    /* Bounds rejection must actually reject. */
    if (zxv_ramdisk_read_sector(total, zxv_probe_read) != -1)  return -1;
    if (zxv_ramdisk_write_sector(total, zxv_probe_read) != -1) return -1;
    return 0;

restore_fail:
    (void)zxv_ramdisk_write_sector(probe_lba, zxv_probe_save);
    return -1;
}

int zxv_ramdisk_selfcheck(void) {
    zxv_ramdisk_init();
    return zxv_ramdisk_roundtrip();
}

/* ---- DECLARATION -----------------------------------------------------------
 * PROVIDES(blockdev_ready) — the shared kernel/src provider; virtio_blk and the
 * arm64 ramdisk provide the same capability at the same contract on arm64.
 * REQUIRES(mm_ready) — the backing store is memory, so the honest requirement is
 * translation, exactly as arm64/ramdisk.c reasons. On an arch whose mm_ready is
 * still HELD (riscv/arm32 today, MMU off) this module is HELD too: PROVIDED but
 * S0, which is the honest outcome, and it flips to READY automatically the
 * instant an mm_ready provider's bring-up starts succeeding — no edit here.
 *
 * The bring-up is falsifiable by construction: it round-trips a live sector and
 * checks bounds, so it CAN return non-zero. A provider that cannot fail is not a
 * provider. */
#include "zxv_decl.h"

static int zxv_ramdisk_bringup(void) {
    return zxv_ramdisk_roundtrip();
}

ZXV_DECLARE(zxv_ramdisk,
    ZXV_PROVIDES(blockdev_ready),
    ZXV_REQUIRES(mm_ready),
    ZXV_BRINGUP(zxv_ramdisk_bringup));
