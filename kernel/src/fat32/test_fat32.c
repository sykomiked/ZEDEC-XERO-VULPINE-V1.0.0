/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* test_fat32.c — FAT32 driver on an in-memory disk, including hostile
 * boot sectors, looping FAT chains and oversized directory entries. */
#include <stdio.h>
#include <string.h>
#include "fat32.h"

#define SECTORS 64u
static uint8_t disk[SECTORS * 512u];
static int failures = 0;

#define CHECK(c, msg)                                                                              \
    do {                                                                                           \
        if (c) {                                                                                   \
            printf("  [PASS] %s\n", msg);                                                          \
        } else {                                                                                   \
            printf("  [FAIL] %s\n", msg);                                                          \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static int rd(block_device_t *d, uint32_t lba, uint8_t *buf)
{
    (void) d;
    if (lba >= SECTORS) return -1;
    memcpy(buf, disk + lba * 512u, 512u);
    return 0;
}

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t) v;
    p[1] = (uint8_t) (v >> 8);
}

static void put32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++) p[i] = (uint8_t) (v >> (8 * i));
}

/* Layout: 1 reserved sector, 1 FAT of 1 sector, 1 sector per cluster.
 * Data starts at sector 2; cluster n is sector n (2 + n - 2). */
static void build(uint8_t spc, uint16_t bps)
{
    memset(disk, 0, sizeof disk);
    uint8_t *b = disk;
    put16(b + 11, bps);
    b[13] = spc;
    put16(b + 14, 1); /* reserved sectors */
    b[16] = 1;        /* FATs */
    put32(b + 36, 1); /* fat_size32 */
    put32(b + 44, 2); /* root cluster */
    uint8_t *fat = disk + 512u;
    put32(fat + 0, 0x0FFFFFF8u);
    put32(fat + 4, 0x0FFFFFFFu);
    put32(fat + 8, 0x0FFFFFFFu);  /* root: one cluster */
    put32(fat + 12, 0x0FFFFFFFu); /* HELLO.TXT: one cluster */
    uint8_t *root = disk + 2u * 512u;
    memcpy(root, "HELLO   TXT", 11);
    root[11] = 0x20;
    put16(root + 26, 3);
    put32(root + 28, 5);
    memcpy(root + 32, "\xE5OLD    TXT", 11); /* deleted entry */
    memcpy(disk + 3u * 512u, "hello", 5);
}

int main(void)
{
    static fat32_state_t fs;
    block_device_t dev;
    memset(&dev, 0, sizeof dev);
    dev.present = true;
    dev.read_sector = rd;
    dev.total_sectors = SECTORS;
    uint8_t buf[16];

    printf("FAT32 driver\n");
    build(1, 512);
    CHECK(fat32_mount(&fs, &dev) == 0, "a well-formed volume mounts");
    CHECK(fat32_read_dir(&fs, fs.root_cluster) == 0, "root directory reads");
    CHECK(fs.num_files == 1, "the deleted entry is skipped, one live file");
    fat32_file_t *f = fat32_find_file(&fs, "HELLO.TXT");
    CHECK(f != NULL, "HELLO.TXT is found by its 8.3 name");
    CHECK(f && fat32_read_file(&fs, f, buf, sizeof buf) == 0 && memcmp(buf, "hello", 5) == 0,
          "file contents read back");
    CHECK(f && fat32_read_file(&fs, f, buf, 4) == -1,
          "a file larger than the caller's buffer is refused");

    build(128, 512);
    CHECK(fat32_mount(&fs, &dev) == -1,
          "128 sectors per cluster (64 KiB) is refused, not read into a 4 KiB stack buffer");
    build(1, 4096);
    CHECK(fat32_mount(&fs, &dev) == -1, "a sector size other than the device's 512 is refused");
    build(3, 512);
    CHECK(fat32_mount(&fs, &dev) == -1, "a non-power-of-two cluster size is refused");

    build(1, 512);
    put32(disk + 512u + 8, 2); /* root chain loops onto itself */
    memset(disk + 2u * 512u, 0, 64);
    memcpy(disk + 2u * 512u, "\xE5", 1); /* only a deleted entry, so the loop never fills */
    CHECK(fat32_mount(&fs, &dev) == 0, "looping volume mounts");
    CHECK(fat32_read_dir(&fs, fs.root_cluster) == -1,
          "a FAT chain that loops ends with an error instead of spinning forever");

    build(1, 512);
    put16(disk + 2u * 512u + 26, 1); /* file points at reserved cluster 1 */
    fat32_mount(&fs, &dev);
    fat32_read_dir(&fs, fs.root_cluster);
    f = fat32_find_file(&fs, "HELLO.TXT");
    CHECK(f && fat32_read_file(&fs, f, buf, sizeof buf) == -1,
          "a directory entry pointing at reserved cluster 1 is refused");
    CHECK(fat32_read_cluster(&fs, 0, buf) == -1, "cluster 0 cannot be read as data");

    build(1, 512);
    put32(disk + 2u * 512u + 28, 1000); /* size bigger than the 1-cluster chain */
    fat32_mount(&fs, &dev);
    fat32_read_dir(&fs, fs.root_cluster);
    f = fat32_find_file(&fs, "HELLO.TXT");
    static uint8_t big[2048];
    CHECK(f && fat32_read_file(&fs, f, big, sizeof big) == -1,
          "a chain shorter than the recorded size is reported, not padded");

    printf("%s (%d failures)\n", failures ? "FAIL" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
