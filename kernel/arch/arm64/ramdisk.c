/* ramdisk.c — RAM-based block device for ARM64
 *
 * Provides a simple in-memory block device that implements the
 * same sector read/write interface as the ATA driver. Used with
 * FAT32/VFS on ARM64 where no physical ATA controller exists.
 *
 * The RAM disk is pre-formatted with a minimal FAT32 filesystem
 * containing a few test files for kernel verification.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#include "ramdisk.h"
#include "../include/blockdev.h"
#include <stdint.h>
#include <stdbool.h>

/* 16MB RAM disk — enough for a small FAT32 filesystem */
#define RAMDISK_SIZE_SECTORS 32768  /* 16MB / 512 bytes */
static uint8_t ramdisk_data[RAMDISK_SIZE_SECTORS * 512]
    __attribute__((aligned(4096)));

static bool ramdisk_present = true;

void ramdisk_init(void) {
    /* RAM disk is always present */
    ramdisk_present = true;
}

bool ramdisk_is_present(void) {
    return ramdisk_present;
}

uint32_t ramdisk_total_sectors(void) {
    return RAMDISK_SIZE_SECTORS;
}

int ramdisk_read_sector(uint32_t lba, uint8_t *buffer) {
    if (lba >= RAMDISK_SIZE_SECTORS) return -1;
    for (uint32_t i = 0; i < 512; i++)
        buffer[i] = ramdisk_data[lba * 512 + i];
    return 0;
}

int ramdisk_write_sector(uint32_t lba, const uint8_t *buffer) {
    if (lba >= RAMDISK_SIZE_SECTORS) return -1;
    for (uint32_t i = 0; i < 512; i++)
        ramdisk_data[lba * 512 + i] = buffer[i];
    return 0;
}

/* Get a direct pointer to the RAM disk data (for FAT32 BPB parsing) */
uint8_t *ramdisk_get_buffer(void) {
    return ramdisk_data;
}

/* Format the RAM disk with a minimal FAT32 filesystem.
 * Writes a valid BPB and an empty root directory so fat32_mount succeeds. */
void ramdisk_format_fat32(void) {
    /* Zero the entire disk first */
    for (uint32_t i = 0; i < RAMDISK_SIZE_SECTORS * 512; i++)
        ramdisk_data[i] = 0;

    /* Build a minimal FAT32 BPB at sector 0 */
    uint8_t *boot = ramdisk_data;

    /* Jump instruction */
    boot[0] = 0xEB; boot[1] = 0x58; boot[2] = 0x90;
    /* OEM name */
    const char *oem = "M5KERNEL";
    for (int i = 0; i < 8; i++) boot[3 + i] = oem[i];

    /* BPB fields (little-endian) */
    uint16_t bytes_per_sector = 512;
    uint8_t sectors_per_cluster = 8;  /* 4KB clusters */
    uint16_t reserved_sectors = 32;
    uint8_t num_fats = 2;
    uint32_t total_sectors = RAMDISK_SIZE_SECTORS;
    uint32_t sectors_per_fat = (total_sectors / (bytes_per_sector / 4)) + 1;
    uint32_t root_cluster = 2;

    boot[11] = (uint8_t)(bytes_per_sector & 0xFF);
    boot[12] = (uint8_t)((bytes_per_sector >> 8) & 0xFF);
    boot[13] = sectors_per_cluster;
    boot[14] = (uint8_t)(reserved_sectors & 0xFF);
    boot[15] = (uint8_t)((reserved_sectors >> 8) & 0xFF);
    boot[16] = num_fats;
    /* root_entries (16-bit, 0 for FAT32) */
    boot[17] = 0; boot[18] = 0;
    /* total_sectors16 (0 for FAT32) */
    boot[19] = 0; boot[20] = 0;
    /* media descriptor */
    boot[21] = 0xF8;
    /* fat_size16 (0 for FAT32) */
    boot[22] = 0; boot[23] = 0;
    /* sectors_per_track, num_heads */
    boot[24] = 0x3F; boot[25] = 0;
    boot[26] = 0xFF; boot[27] = 0;
    /* hidden_sectors */
    boot[28] = 0; boot[29] = 0; boot[30] = 0; boot[31] = 0;
    /* total_sectors32 */
    boot[32] = (uint8_t)(total_sectors & 0xFF);
    boot[33] = (uint8_t)((total_sectors >> 8) & 0xFF);
    boot[34] = (uint8_t)((total_sectors >> 16) & 0xFF);
    boot[35] = (uint8_t)((total_sectors >> 24) & 0xFF);
    /* fat_size32 */
    boot[36] = (uint8_t)(sectors_per_fat & 0xFF);
    boot[37] = (uint8_t)((sectors_per_fat >> 8) & 0xFF);
    boot[38] = (uint8_t)((sectors_per_fat >> 16) & 0xFF);
    boot[39] = (uint8_t)((sectors_per_fat >> 24) & 0xFF);
    /* flags, version */
    boot[40] = 0; boot[41] = 0;
    boot[42] = 0; boot[43] = 0;
    /* root_cluster */
    boot[44] = (uint8_t)(root_cluster & 0xFF);
    boot[45] = (uint8_t)((root_cluster >> 8) & 0xFF);
    boot[46] = (uint8_t)((root_cluster >> 16) & 0xFF);
    boot[47] = (uint8_t)((root_cluster >> 24) & 0xFF);
    /* fs_info_sector */
    boot[48] = 1; boot[49] = 0;
    /* backup_boot_sector */
    boot[50] = 6; boot[51] = 0;
    /* drive_number */
    boot[64] = 0x80;
    /* boot_signature */
    boot[66] = 0x29;
    /* volume_id (random-ish) */
    boot[67] = 0x36; boot[68] = 0x4E; boot[69] = 0x39; boot[70] = 0x00;
    /* volume_label */
    const char *label = "M5 BOOT    ";
    for (int i = 0; i < 11; i++) boot[71 + i] = label[i];
    /* fs_type */
    const char *fstype = "FAT32   ";
    for (int i = 0; i < 8; i++) boot[82 + i] = fstype[i];
    /* Boot signature */
    boot[510] = 0x55;
    boot[511] = 0xAA;

    /* Initialize FAT[0] and FAT[1] in the first FAT */
    uint32_t fat_start = reserved_sectors * bytes_per_sector;
    ramdisk_data[fat_start + 0] = 0xF8;  /* media type */
    ramdisk_data[fat_start + 1] = 0xFF;
    ramdisk_data[fat_start + 2] = 0xFF;
    ramdisk_data[fat_start + 3] = 0x0F;  /* EOC marker */
    ramdisk_data[fat_start + 4] = 0xFF;  /* FAT[1] = EOC */
    ramdisk_data[fat_start + 5] = 0xFF;
    ramdisk_data[fat_start + 6] = 0xFF;
    ramdisk_data[fat_start + 7] = 0x0F;

    /* Mark cluster 2 (root directory) as end-of-chain */
    uint32_t fat_entry_2 = fat_start + 8;
    ramdisk_data[fat_entry_2 + 0] = 0xFF;
    ramdisk_data[fat_entry_2 + 1] = 0xFF;
    ramdisk_data[fat_entry_2 + 2] = 0xFF;
    ramdisk_data[fat_entry_2 + 3] = 0x0F;
}

/* Block device adapter: wraps ramdisk_read_sector for block_device_t */
static int ramdisk_block_read(block_device_t *dev, uint32_t lba, uint8_t *buf) {
    (void)dev;
    return ramdisk_read_sector(lba, buf);
}

static int ramdisk_block_write(block_device_t *dev, uint32_t lba, const uint8_t *buf) {
    (void)dev;
    return ramdisk_write_sector(lba, buf);
}

/* Create a block device backed by the RAM disk */
void ramdisk_create_blockdev(block_device_t *dev) {
    blockdev_init(dev, ramdisk_block_read, ramdisk_block_write,
                  RAMDISK_SIZE_SECTORS, "M5 RAMDISK v1.0");
}

/* ---- DECLARATION -----------------------------------------------------------
 * ONE OF TWO PROVIDERS OF blockdev_ready, and the reason that is not a defect.
 * virtio_blk provides the same capability at the same contract; a machine with
 * no virtio disk still resolves blockdev_ready through this one and still
 * boots. Alternative provision is legal (modbind.h:146-153); what is NOT legal
 * is the two providers disagreeing about the contract, and modbind_verify_graph
 * reports that as MB_ERR_CONTRACT rather than letting registration order decide
 * at runtime. Both sides are contract 1 -- change one and the gate says so.
 *
 * The backing store is a static array, so the only requirement is translation. */
#include "zxv_decl.h"

static int ramdisk_bringup(void) {
    if (!ramdisk_is_present()) return -1;
    if (ramdisk_total_sectors() == 0u) return -1;
    return 0;
}

ZXV_DECLARE(ramdisk,
    ZXV_PROVIDES(blockdev_ready),
    ZXV_REQUIRES(mm_ready),
    ZXV_BRINGUP(ramdisk_bringup));
