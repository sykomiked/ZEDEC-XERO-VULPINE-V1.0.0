/* zxvfs.h — ZXV persistent filesystem with redo journal
 *
 * A deliberately small, crash-consistent filesystem for the ZXV MVP.
 * It sits directly on a block_device_t (virtio-blk in production, a
 * memory-backed device in host tests) and provides atomic multi-sector
 * transactions on top of single-sector-atomic hardware via a redo
 * (write-ahead) journal:
 *
 *   1. stage every changed sector into the journal region
 *   2. write the journal header with committed=1  <-- atomic commit
 *   3. checkpoint: copy staged sectors to their final homes
 *   4. clear the journal header
 *
 * On mount, a committed-but-not-cleared journal is replayed (step 3 is
 * idempotent), so a crash at any point leaves the FS either fully
 * before or fully after the transaction — never half-applied.
 *
 * Scope (MVP): single flat directory, fixed per-file extent, whole-file
 * read/write. Enough to load an application and its config from disk,
 * survive reboots, and recover from power loss.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)  (ZXV persistence slice)
 * License: SEL-3.3
 */
#ifndef ZXVFS_H
#define ZXVFS_H

#include <stdint.h>
#include <stdbool.h>
#include "../../include/blockdev.h"

#define ZXVFS_MAGIC        0x5A585646u   /* 'ZXVF' */
#define ZXVFS_JRNL_MAGIC   0x4A524E4Cu   /* 'JRNL' */
#define ZXVFS_VERSION      1

#define ZXVFS_NAME_LEN     32
#define ZXVFS_MAX_FILES    64
#define ZXVFS_INODES_PER_SECTOR 8        /* 512 / 64 */
#define ZXVFS_INODE_SECTORS (ZXVFS_MAX_FILES / ZXVFS_INODES_PER_SECTOR)  /* 8 */
#define ZXVFS_FILE_MAX_SECTORS 15        /* per-file extent = 7680 bytes */
#define ZXVFS_FILE_MAX_BYTES (ZXVFS_FILE_MAX_SECTORS * BLOCKDEV_SECTOR_SIZE)
#define ZXVFS_JOURNAL_MAX  16            /* max staged sectors per txn */

/* On-disk sector layout (all little-endian, native):
 *   0                         superblock
 *   1                         journal header
 *   2 .. 2+JOURNAL_MAX-1       journal staging sectors (16)
 *   18 .. 18+INODE_SECTORS-1   inode table (8)
 *   26 ..                      data region (MAX_FILES * FILE_MAX_SECTORS) */
#define ZXVFS_SB_SECTOR        0
#define ZXVFS_JHDR_SECTOR      1
#define ZXVFS_JSTAGE_SECTOR    2
#define ZXVFS_INODE_SECTOR     (ZXVFS_JSTAGE_SECTOR + ZXVFS_JOURNAL_MAX)   /* 18 */
#define ZXVFS_DATA_SECTOR      (ZXVFS_INODE_SECTOR + ZXVFS_INODE_SECTORS)  /* 26 */
#define ZXVFS_TOTAL_SECTORS    (ZXVFS_DATA_SECTOR + ZXVFS_MAX_FILES * ZXVFS_FILE_MAX_SECTORS)

typedef struct {
    char     name[ZXVFS_NAME_LEN]; /* 32 */
    uint32_t size;               /* bytes; 0 & used=0 => free slot */
    uint32_t first_sector;       /* absolute LBA of file's data extent */
    uint32_t used;               /* 1 = live inode */
    uint32_t _pad[5];            /* pad to exactly 64 bytes (32+4*3+20) */
} zxvfs_inode_t;                 /* sizeof == 64 == 512/ZXVFS_INODES_PER_SECTOR */

typedef struct {
    uint32_t magic;              /* ZXVFS_MAGIC */
    uint32_t version;
    uint32_t total_sectors;
    uint32_t inode_sector;
    uint32_t inode_count;
    uint32_t data_sector;
    uint32_t generation;         /* bumped on every committed txn */
    uint32_t _pad[121];
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
    /* stats */
    uint32_t journal_replays;
} zxvfs_t;

/* Format a block device with an empty ZXVFS (destroys existing data). */
int zxvfs_format(block_device_t *dev);

/* Mount; replays a committed journal if present. Returns 0 on success,
 * <0 if the device has no valid ZXVFS superblock. */
int zxvfs_mount(zxvfs_t *fs, block_device_t *dev);

/* Whole-file write (create or replace). Atomic: after a crash the file
 * is either the old content or the new content. Returns 0 on success. */
int zxvfs_write(zxvfs_t *fs, const char *name, const uint8_t *data, uint32_t len);

/* Whole-file read into buf (up to max). Returns bytes read, or <0. */
int zxvfs_read(zxvfs_t *fs, const char *name, uint8_t *buf, uint32_t max);

/* Remove a file. Atomic. Returns 0 on success, <0 if not found. */
int zxvfs_unlink(zxvfs_t *fs, const char *name);

/* List files: fills names[]/sizes[] up to max, returns count. */
int zxvfs_list(zxvfs_t *fs, char names[][ZXVFS_NAME_LEN], uint32_t *sizes,
               uint32_t max);

/* Number of live files. */
int zxvfs_count(zxvfs_t *fs);

#endif /* ZXVFS_H */
