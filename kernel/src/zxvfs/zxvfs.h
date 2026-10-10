/* zxvfs.h — ZXV persistent filesystem: redo journal + extent allocator
 *
 * A deliberately small, crash-consistent filesystem for the ZXV MVP. It sits
 * directly on a block_device_t (virtio-blk in production, a memory-backed
 * device in host tests).
 *
 * FORMAT v2 (extents). v1 was a fixed slot per file: 64 files, one 7680-byte
 * extent each, whole-file I/O only. v2 adds a free-space bitmap, multi-extent
 * files, and positional read/write. v1 images are NOT readable — the version
 * check rejects them rather than misinterpreting their geometry.
 *
 * ATOMICITY, AND WHY DATA IS NOT JOURNALLED
 * -----------------------------------------
 * Writes are COPY-ON-WRITE. A write never overwrites the sectors a file
 * currently occupies: it allocates NEW extents, writes the data there, and only
 * then commits the inode and bitmap in a single journalled transaction. Until
 * that commit nothing references the new sectors, so they are invisible; after
 * it, the old sectors are free. Therefore:
 *
 *     crash before the metadata commit  ->  the OLD file, intact
 *     crash during it                   ->  the redo journal makes it all-or-nothing
 *     crash after                        ->  the NEW file, intact
 *
 * That is full old-or-new atomicity at ANY size while journalling only three
 * metadata sectors, and it is what `zxvfs_tri` relies on when it commits a
 * triad descriptor. The cost is honest: CoW needs free space for the new copy,
 * so a write can fail with -ENOSPC even when it is "only" replacing a file.
 *
 * The journal may therefore only ever target METADATA. `journal_target_ok()`
 * enforces that, which also means a crafted journal cannot be aimed at the data
 * region at all.
 *
 * WHERE ATOMICITY STOPS (stated plainly)
 * --------------------------------------
 * `zxvfs_pwrite` writing INSIDE the current size updates those sectors in
 * place, so a crash mid-write can leave a torn range — the same guarantee POSIX
 * gives. Growing a file IS atomic (the new sectors are unreferenced until the
 * metadata commit). If you need an all-or-nothing update of a whole object, use
 * `zxvfs_write`.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)  (ZXV persistence slice)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#ifndef ZXVFS_H
#define ZXVFS_H

#include <stdint.h>
#include <stdbool.h>
#include "../../include/blockdev.h"

#define ZXVFS_MAGIC        0x5A585646u   /* 'ZXVF' */
#define ZXVFS_JRNL_MAGIC   0x4A524E4Cu   /* 'JRNL' */
#define ZXVFS_VERSION      2

#define ZXVFS_NAME_LEN     32
#define ZXVFS_MAX_FILES    256
#define ZXVFS_MAX_EXTENTS  10            /* per file; inode is exactly 128 B  */
#define ZXVFS_INODE_SIZE   128
#define ZXVFS_INODES_PER_SECTOR (BLOCKDEV_SECTOR_SIZE / ZXVFS_INODE_SIZE)  /* 4 */
#define ZXVFS_INODE_SECTORS (ZXVFS_MAX_FILES / ZXVFS_INODES_PER_SECTOR)    /* 64 */

#define ZXVFS_DATA_SECTORS 8192u         /* 4 MB data region                  */
#define ZXVFS_BITMAP_BYTES (ZXVFS_DATA_SECTORS / 8u)                 /* 1024 */
#define ZXVFS_BITMAP_SECTORS (ZXVFS_BITMAP_BYTES / BLOCKDEV_SECTOR_SIZE) /* 2 */

#define ZXVFS_JOURNAL_MAX  16            /* max staged sectors per txn        */

/* Cap for the WHOLE-FILE convenience API only (it buffers in the caller).
 * Files themselves may be far larger — reach those bytes with pread/pwrite,
 * which stream and never require a buffer of the whole object. */
#define ZXVFS_FILE_MAX_SECTORS 128
#define ZXVFS_FILE_MAX_BYTES (ZXVFS_FILE_MAX_SECTORS * BLOCKDEV_SECTOR_SIZE)

/* On-disk sector layout (all little-endian, native):
 *   0                          superblock
 *   1                          journal header
 *   2 .. 17                    journal staging (16)
 *   18 .. 19                   allocation bitmap (2)
 *   20 .. 83                   inode table (64)
 *   84 ..                      data region (8192) */
#define ZXVFS_SB_SECTOR        0
#define ZXVFS_JHDR_SECTOR      1
#define ZXVFS_JSTAGE_SECTOR    2
#define ZXVFS_BITMAP_SECTOR    (ZXVFS_JSTAGE_SECTOR + ZXVFS_JOURNAL_MAX)      /* 18 */
#define ZXVFS_INODE_SECTOR     (ZXVFS_BITMAP_SECTOR + ZXVFS_BITMAP_SECTORS)   /* 20 */
#define ZXVFS_DATA_SECTOR      (ZXVFS_INODE_SECTOR + ZXVFS_INODE_SECTORS)     /* 84 */
#define ZXVFS_TOTAL_SECTORS    (ZXVFS_DATA_SECTOR + ZXVFS_DATA_SECTORS)

typedef struct {
    uint32_t start;              /* data-region-relative first sector */
    uint32_t count;              /* sectors in this extent            */
} zxvfs_extent_t;

typedef struct {
    char     name[ZXVFS_NAME_LEN]; /* 32 */
    uint32_t size;               /* bytes; used=0 => free slot           36 */
    uint32_t used;               /* 1 = live inode                       40 */
    uint32_t nextents;           /*                                      44 */
    uint32_t _rsvd;              /*                                      48 */
    zxvfs_extent_t extent[ZXVFS_MAX_EXTENTS];   /* 80                   128 */
} zxvfs_inode_t;                 /* sizeof == ZXVFS_INODE_SIZE */

typedef struct {
    uint32_t magic;              /* ZXVFS_MAGIC */
    uint32_t version;
    uint32_t total_sectors;
    uint32_t inode_sector;
    uint32_t inode_count;
    uint32_t data_sector;
    uint32_t generation;         /* bumped on every committed txn */
    uint32_t bitmap_sector;
    uint32_t data_sectors;
    uint32_t _pad[119];
} zxvfs_superblock_t;

typedef struct {
    uint32_t magic;              /* ZXVFS_JRNL_MAGIC */
    uint32_t committed;          /* 1 = transaction committed, replay on mount */
    uint32_t txn_id;
    uint32_t count;              /* number of staged sectors */
    uint32_t target_lba[ZXVFS_JOURNAL_MAX];
    uint32_t checksum;           /* over target_lba[0..count) + count + txn_id */
    uint32_t _pad[512/4 - 4 - ZXVFS_JOURNAL_MAX - 1];
} zxvfs_journal_hdr_t;

typedef struct {
    block_device_t *dev;
    zxvfs_superblock_t sb;
    bool mounted;
    uint32_t next_txn;
    uint32_t journal_replays;
} zxvfs_t;

/* Format a block device with an empty ZXVFS (destroys existing data). */
int zxvfs_format(block_device_t *dev);

/* Mount; replays a committed journal if present. Returns 0 on success,
 * <0 if the device has no valid ZXVFS superblock. */
int zxvfs_mount(zxvfs_t *fs, block_device_t *dev);

/* Whole-file write (create or replace), COPY-ON-WRITE and therefore atomic:
 * after a crash the file is either entirely the old content or entirely the
 * new one. Returns 0, or <0 (including -5 when there is not enough free space
 * to hold the new copy alongside the old). */
int zxvfs_write(zxvfs_t *fs, const char *name, const uint8_t *data, uint32_t len);

/* Whole-file read into buf (up to max). Returns bytes read, or <0. */
int zxvfs_read(zxvfs_t *fs, const char *name, uint8_t *buf, uint32_t max);

/* Positional read. Returns bytes read (0 at/after EOF), or <0. */
int zxvfs_pread(zxvfs_t *fs, const char *name, uint32_t offset,
                uint8_t *buf, uint32_t len);

/* Positional write, growing the file if needed. Bytes written INSIDE the
 * current size land in place and are NOT crash-atomic; growth is. Returns
 * bytes written, or <0. */
int zxvfs_pwrite(zxvfs_t *fs, const char *name, uint32_t offset,
                 const uint8_t *data, uint32_t len);

/* Size in bytes, or <0 if absent. */
int zxvfs_size(zxvfs_t *fs, const char *name);

/* Remove a file (frees its extents). Atomic. Returns 0, or <0 if not found. */
int zxvfs_unlink(zxvfs_t *fs, const char *name);

/* List files: fills names[]/sizes[] up to max, returns count. */
int zxvfs_list(zxvfs_t *fs, char names[][ZXVFS_NAME_LEN], uint32_t *sizes,
               uint32_t max);

/* Number of live files. */
int zxvfs_count(zxvfs_t *fs);

/* Free data sectors remaining (for tests and for ENOSPC diagnostics). */
int zxvfs_free_sectors(zxvfs_t *fs);

/* ---- allocation locality -------------------------------------------------
 * First-fit from sector 0 packs the disk tightly, which is fine until churn:
 * once files are deleted and rewritten, the members of one logical object end
 * up scattered across the region and reading it walks the whole platter.
 *
 * A HINT biases where the search starts, so related files can be asked to
 * cluster. It is only a hint: if there is no room near it the allocator falls
 * back to a full scan rather than failing, because correctness must not depend
 * on locality. Set 0 to restore plain first-fit.
 *
 * zxvfs_tri derives its hint from the triad's Z-order (Morton) code, so a
 * triad's four files cluster and DIFFERENT triads are spread apart instead of
 * competing for the same sectors. The benefit is measured, not assumed — see
 * test_zxvfs_locality in the fuzzer suite. */
void zxvfs_set_alloc_hint(uint32_t data_sector_hint);

#endif /* ZXVFS_H */
