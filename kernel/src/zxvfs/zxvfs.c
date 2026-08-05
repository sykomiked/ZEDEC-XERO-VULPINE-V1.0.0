/* zxvfs.c — ZXV persistent filesystem with redo journal
 *
 * See zxvfs.h for the on-disk layout and crash-consistency model.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)  (ZXV persistence slice)
 * License: SEL-3.3
 */
#include "zxvfs.h"

/* On-disk layout invariants: an inode must be exactly 1/8 of a sector,
 * and the header structs must be exactly one sector. A mismatch would
 * corrupt the inode-offset math and the single-sector-atomic commit. */
typedef char zxvfs_assert_inode[(sizeof(zxvfs_inode_t) == 64) ? 1 : -1];
typedef char zxvfs_assert_sb[(sizeof(zxvfs_superblock_t) == 512) ? 1 : -1];
typedef char zxvfs_assert_jhdr[(sizeof(zxvfs_journal_hdr_t) == 512) ? 1 : -1];

/* This module uses its own tiny zmemset/zmemcpy so it never depends on
 * the freestanding -Dmemset=fs_memset remapping. Host test builds
 * (ZXVFS_HOST) alias them to libc for speed. */
#ifdef ZXVFS_HOST
#include <string.h>
#define zmemset memset
#define zmemcpy memcpy
#else
static void *zmemset(void *d, int c, unsigned long n) {
    unsigned char *p = d; while (n--) *p++ = (unsigned char)c; return d;
}
static void *zmemcpy(void *d, const void *s, unsigned long n) {
    unsigned char *pd = d; const unsigned char *ps = s;
    while (n--) *pd++ = *ps++; return d;
}
#endif

static int zstreq(const char *a, const char *b) {
    uint32_t i = 0;
    for (; i < ZXVFS_NAME_LEN; i++) {
        if (a[i] != b[i]) return 0;
        if (a[i] == '\0') return 1;
    }
    return 1;
}

static uint32_t zstrlen(const char *s) {
    uint32_t n = 0;
    while (n < ZXVFS_NAME_LEN - 1 && s[n]) n++;
    return n;
}

static uint32_t jchecksum(const zxvfs_journal_hdr_t *h) {
    uint32_t c = 0x9E3779B9u ^ h->txn_id ^ (h->count * 2654435761u);
    for (uint32_t i = 0; i < h->count && i < ZXVFS_JOURNAL_MAX; i++)
        c = (c * 16777619u) ^ h->target_lba[i];
    return c;
}

/* ---- sector I/O helpers ---- */
static int rd(zxvfs_t *fs, uint32_t lba, uint8_t *buf) {
    return blockdev_read(fs->dev, lba, buf);
}
static int wr(zxvfs_t *fs, uint32_t lba, const uint8_t *buf) {
    return blockdev_write(fs->dev, lba, buf);
}

/* ---- journal ---- */

/* Apply (checkpoint) all staged sectors to their final homes. Idempotent. */
static int journal_checkpoint(zxvfs_t *fs, const zxvfs_journal_hdr_t *h) {
    uint8_t sec[BLOCKDEV_SECTOR_SIZE];
    for (uint32_t i = 0; i < h->count && i < ZXVFS_JOURNAL_MAX; i++) {
        if (rd(fs, ZXVFS_JSTAGE_SECTOR + i, sec) != 0) return -1;
        if (wr(fs, h->target_lba[i], sec) != 0) return -1;
    }
    return 0;
}

static int journal_clear(zxvfs_t *fs) {
    zxvfs_journal_hdr_t h;
    zmemset(&h, 0, sizeof(h));
    h.magic = ZXVFS_JRNL_MAGIC;
    h.committed = 0;
    return wr(fs, ZXVFS_JHDR_SECTOR, (const uint8_t *)&h);
}

/* Run a full transaction: stage `count` sectors, commit, checkpoint,
 * clear. `target[]` = final LBAs, `data[]` = pointer to each sector's
 * new 512-byte contents. */
static int journal_txn(zxvfs_t *fs, const uint32_t *target,
                       const uint8_t *const *data, uint32_t count) {
    if (count == 0) return 0;
    if (count > ZXVFS_JOURNAL_MAX) return -1;

    /* 1. stage */
    for (uint32_t i = 0; i < count; i++) {
        if (wr(fs, ZXVFS_JSTAGE_SECTOR + i, data[i]) != 0) return -1;
    }

    /* 2. commit header (single-sector atomic write) */
    zxvfs_journal_hdr_t h;
    zmemset(&h, 0, sizeof(h));
    h.magic = ZXVFS_JRNL_MAGIC;
    h.committed = 1;
    h.txn_id = fs->next_txn++;
    h.count = count;
    for (uint32_t i = 0; i < count; i++) h.target_lba[i] = target[i];
    h.checksum = jchecksum(&h);
    if (wr(fs, ZXVFS_JHDR_SECTOR, (const uint8_t *)&h) != 0) return -1;

    /* 3. checkpoint */
    if (journal_checkpoint(fs, &h) != 0) return -1;

    /* 4. clear */
    return journal_clear(fs);
}

/* Replay a committed journal on mount, if present. */
static int journal_recover(zxvfs_t *fs) {
    uint8_t sec[BLOCKDEV_SECTOR_SIZE];
    if (rd(fs, ZXVFS_JHDR_SECTOR, sec) != 0) return -1;
    zxvfs_journal_hdr_t *h = (zxvfs_journal_hdr_t *)sec;
    if (h->magic != ZXVFS_JRNL_MAGIC) return 0;      /* uninitialized */
    if (!h->committed) return 0;                     /* nothing to redo */
    if (h->count > ZXVFS_JOURNAL_MAX) return 0;      /* corrupt: ignore */
    if (h->checksum != jchecksum(h)) return 0;       /* torn commit: ignore */

    if (journal_checkpoint(fs, h) != 0) return -1;
    fs->journal_replays++;
    return journal_clear(fs);
}

/* ---- inode helpers ---- */

/* Load the inode-table sector that holds inode index `idx`. */
static uint32_t inode_sector_for(uint32_t idx) {
    return ZXVFS_INODE_SECTOR + idx / ZXVFS_INODES_PER_SECTOR;
}
static uint32_t inode_offset_in_sector(uint32_t idx) {
    return (idx % ZXVFS_INODES_PER_SECTOR) * sizeof(zxvfs_inode_t);
}

static int load_inode(zxvfs_t *fs, uint32_t idx, zxvfs_inode_t *out) {
    uint8_t sec[BLOCKDEV_SECTOR_SIZE];
    if (rd(fs, inode_sector_for(idx), sec) != 0) return -1;
    zmemcpy(out, sec + inode_offset_in_sector(idx), sizeof(*out));
    return 0;
}

/* Find inode index by name, or -1. */
static int find_inode(zxvfs_t *fs, const char *name) {
    for (uint32_t i = 0; i < ZXVFS_MAX_FILES; i++) {
        zxvfs_inode_t in;
        if (load_inode(fs, i, &in) != 0) return -1;
        if (in.used && zstreq(in.name, name)) return (int)i;
    }
    return -1;
}

static int find_free_inode(zxvfs_t *fs) {
    for (uint32_t i = 0; i < ZXVFS_MAX_FILES; i++) {
        zxvfs_inode_t in;
        if (load_inode(fs, i, &in) != 0) return -1;
        if (!in.used) return (int)i;
    }
    return -1;
}

/* ---- public API ---- */

int zxvfs_format(block_device_t *dev) {
    if (!dev || !dev->present) return -1;
    uint8_t sec[BLOCKDEV_SECTOR_SIZE];

    /* superblock */
    zmemset(sec, 0, sizeof(sec));
    zxvfs_superblock_t *sb = (zxvfs_superblock_t *)sec;
    sb->magic = ZXVFS_MAGIC;
    sb->version = ZXVFS_VERSION;
    sb->total_sectors = ZXVFS_TOTAL_SECTORS;
    sb->inode_sector = ZXVFS_INODE_SECTOR;
    sb->inode_count = ZXVFS_MAX_FILES;
    sb->data_sector = ZXVFS_DATA_SECTOR;
    sb->generation = 1;
    if (blockdev_write(dev, ZXVFS_SB_SECTOR, sec) != 0) return -1;

    /* clear journal header */
    zmemset(sec, 0, sizeof(sec));
    ((zxvfs_journal_hdr_t *)sec)->magic = ZXVFS_JRNL_MAGIC;
    if (blockdev_write(dev, ZXVFS_JHDR_SECTOR, sec) != 0) return -1;

    /* zero the inode table */
    zmemset(sec, 0, sizeof(sec));
    for (uint32_t s = 0; s < ZXVFS_INODE_SECTORS; s++) {
        if (blockdev_write(dev, ZXVFS_INODE_SECTOR + s, sec) != 0) return -1;
    }
    return 0;
}

int zxvfs_mount(zxvfs_t *fs, block_device_t *dev) {
    if (!fs || !dev || !dev->present) return -1;
    zmemset(fs, 0, sizeof(*fs));
    fs->dev = dev;

    uint8_t sec[BLOCKDEV_SECTOR_SIZE];
    if (rd(fs, ZXVFS_SB_SECTOR, sec) != 0) return -1;
    zxvfs_superblock_t *sb = (zxvfs_superblock_t *)sec;
    if (sb->magic != ZXVFS_MAGIC || sb->version != ZXVFS_VERSION) return -2;
    zmemcpy(&fs->sb, sb, sizeof(fs->sb));

    /* crash recovery before any read/write */
    if (journal_recover(fs) != 0) return -3;

    fs->next_txn = fs->sb.generation + 1;
    fs->mounted = true;
    return 0;
}

int zxvfs_write(zxvfs_t *fs, const char *name, const uint8_t *data,
                uint32_t len) {
    if (!fs || !fs->mounted || !name || name[0] == '\0') return -1;
    if (len > ZXVFS_FILE_MAX_BYTES) return -2;
    if (zstrlen(name) >= ZXVFS_NAME_LEN - 1) return -3;

    int idx = find_inode(fs, name);
    if (idx < 0) idx = find_free_inode(fs);
    if (idx < 0) return -4;   /* directory full */

    uint32_t data_lba = fs->sb.data_sector +
                        (uint32_t)idx * ZXVFS_FILE_MAX_SECTORS;
    uint32_t nsectors = (len + BLOCKDEV_SECTOR_SIZE - 1) / BLOCKDEV_SECTOR_SIZE;

    /* Build the transaction: N data sectors + 1 inode-table sector. */
    static uint8_t staged[ZXVFS_JOURNAL_MAX][BLOCKDEV_SECTOR_SIZE];
    const uint8_t *dptr[ZXVFS_JOURNAL_MAX];
    uint32_t target[ZXVFS_JOURNAL_MAX];
    uint32_t n = 0;

    for (uint32_t s = 0; s < nsectors; s++) {
        zmemset(staged[n], 0, BLOCKDEV_SECTOR_SIZE);
        uint32_t off = s * BLOCKDEV_SECTOR_SIZE;
        uint32_t chunk = len - off;
        if (chunk > BLOCKDEV_SECTOR_SIZE) chunk = BLOCKDEV_SECTOR_SIZE;
        zmemcpy(staged[n], data + off, chunk);
        target[n] = data_lba + s;
        dptr[n] = staged[n];
        n++;
    }

    /* inode-table sector: read-modify-write the slot */
    uint32_t isec = inode_sector_for((uint32_t)idx);
    if (rd(fs, isec, staged[n]) != 0) return -5;
    zxvfs_inode_t *in =
        (zxvfs_inode_t *)(staged[n] + inode_offset_in_sector((uint32_t)idx));
    zmemset(in, 0, sizeof(*in));
    uint32_t nl = zstrlen(name);
    zmemcpy(in->name, name, nl);
    in->name[nl] = '\0';
    in->size = len;
    in->first_sector = data_lba;
    in->used = 1;
    target[n] = isec;
    dptr[n] = staged[n];
    n++;

    return journal_txn(fs, target, dptr, n);
}

int zxvfs_read(zxvfs_t *fs, const char *name, uint8_t *buf, uint32_t max) {
    if (!fs || !fs->mounted || !name) return -1;
    int idx = find_inode(fs, name);
    if (idx < 0) return -2;

    zxvfs_inode_t in;
    if (load_inode(fs, (uint32_t)idx, &in) != 0) return -3;

    uint32_t len = in.size;
    if (len > max) len = max;
    uint32_t got = 0;
    uint8_t sec[BLOCKDEV_SECTOR_SIZE];
    uint32_t s = 0;
    while (got < len) {
        if (rd(fs, in.first_sector + s, sec) != 0) return -4;
        uint32_t chunk = len - got;
        if (chunk > BLOCKDEV_SECTOR_SIZE) chunk = BLOCKDEV_SECTOR_SIZE;
        zmemcpy(buf + got, sec, chunk);
        got += chunk;
        s++;
    }
    return (int)got;
}

int zxvfs_unlink(zxvfs_t *fs, const char *name) {
    if (!fs || !fs->mounted || !name) return -1;
    int idx = find_inode(fs, name);
    if (idx < 0) return -2;

    static uint8_t staged[BLOCKDEV_SECTOR_SIZE];
    uint32_t isec = inode_sector_for((uint32_t)idx);
    if (rd(fs, isec, staged) != 0) return -3;
    zxvfs_inode_t *in =
        (zxvfs_inode_t *)(staged + inode_offset_in_sector((uint32_t)idx));
    zmemset(in, 0, sizeof(*in));   /* used = 0 */

    const uint8_t *dptr = staged;
    uint32_t target = isec;
    return journal_txn(fs, &target, &dptr, 1);
}

int zxvfs_list(zxvfs_t *fs, char names[][ZXVFS_NAME_LEN], uint32_t *sizes,
               uint32_t max) {
    if (!fs || !fs->mounted) return -1;
    uint32_t out = 0;
    for (uint32_t i = 0; i < ZXVFS_MAX_FILES && out < max; i++) {
        zxvfs_inode_t in;
        if (load_inode(fs, i, &in) != 0) return -2;
        if (!in.used) continue;
        zmemcpy(names[out], in.name, ZXVFS_NAME_LEN);
        if (sizes) sizes[out] = in.size;
        out++;
    }
    return (int)out;
}

int zxvfs_count(zxvfs_t *fs) {
    if (!fs || !fs->mounted) return -1;
    int c = 0;
    for (uint32_t i = 0; i < ZXVFS_MAX_FILES; i++) {
        zxvfs_inode_t in;
        if (load_inode(fs, i, &in) != 0) return -1;
        if (in.used) c++;
    }
    return c;
}
