/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* fuzz_fat32_mount.c — FAT32 driver mounting a hostile disk image.
 *
 * The input is the disk (sector n = bytes [512n, 512n+512); a short last
 * sector is zero-padded; reads past the image fail). The harness mounts it,
 * lists the root, reads every file into a buffer sized from the directory
 * entry (capped), and descends into subdirectories (bounded depth).
 * Properties: num_files stays within FAT32_MAX_FILES, names are terminated. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "fat32.h"
#include "fuzz_in.h"

#define IMG_MAX (256u * 1024u)

static const uint8_t *g_img;
static size_t g_len;

static int rd(block_device_t *d, uint32_t lba, uint8_t *buf)
{
    (void) d;
    uint64_t off = (uint64_t) lba * 512u;
    if (off >= g_len) return -1;
    size_t n = g_len - (size_t) off < 512u ? g_len - (size_t) off : 512u;
    memcpy(buf, g_img + off, n);
    if (n < 512u) memset(buf + n, 0, 512u - n);
    return 0;
}

static fat32_state_t g_fs;

static void walk(int depth)
{
    if (g_fs.num_files > FAT32_MAX_FILES) abort();
    uint32_t n = g_fs.num_files;
    char dirs[8][13];
    uint32_t nd = 0;
    for (uint32_t i = 0; i < n; i++) {
        fat32_file_t *f = &g_fs.files[i];
        if (memchr(f->name, 0, sizeof f->name) == NULL) abort();
        if (fat32_find_file(&g_fs, f->name) == NULL) abort();
        if (f->is_dir) {
            if (nd < 8 && f->name[0] != '.') memcpy(dirs[nd++], f->name, 13);
            continue;
        }
        uint32_t cap = f->size < 8192u ? f->size : 8192u; /* inputs are <= 4 KiB by default */
        uint8_t *out = (uint8_t *) malloc(cap ? cap : 1u);
        if (!out) abort();
        (void) fat32_read_file(&g_fs, f, out, cap);
        free(out);
    }
    if (depth >= 3) return;
    for (uint32_t i = 0; i < nd; i++) {
        if (fat32_change_dir(&g_fs, dirs[i]) == 0) walk(depth + 1);
    }
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size > IMG_MAX) return 0;
    uint8_t *img = fz_dup(data, size);
    g_img = img;
    g_len = size;
    block_device_t dev;
    memset(&dev, 0, sizeof dev);
    dev.present = true;
    dev.read_sector = rd;
    dev.total_sectors = (uint32_t) ((size + 511u) / 512u);
    memset(&g_fs, 0, sizeof g_fs);
    if (fat32_mount(&g_fs, &dev) == 0) {
        if (fat32_read_dir(&g_fs, g_fs.root_cluster) == 0) walk(0);
        uint32_t c = g_fs.root_cluster;
        for (int i = 0; i < 1024 && c >= 2u && c < 0x0FFFFFF8u; i++)
            c = fat32_next_cluster(&g_fs, c);
        uint8_t *clu = (uint8_t *) malloc(FAT32_CLUSTER_MAX);
        if (!clu) abort();
        (void) fat32_read_cluster(&g_fs, g_fs.root_cluster, clu);
        (void) fat32_read_cluster(&g_fs, 0xFFFFFFFFu, clu);
        free(clu);
    }
    free(img);
    return 0;
}
