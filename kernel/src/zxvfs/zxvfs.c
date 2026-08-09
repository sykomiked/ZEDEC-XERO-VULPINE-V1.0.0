/* zxvfs.c — ZXV persistent filesystem: redo journal + extent allocator
 *
 * See zxvfs.h for the on-disk layout and the crash-consistency model.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)  (ZXV persistence slice)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#include "zxvfs.h"

/* On-disk layout invariants. A mismatch would corrupt the inode-offset math
 * or the single-sector-atomic commit. */
typedef char zxvfs_assert_inode[(sizeof(zxvfs_inode_t) == ZXVFS_INODE_SIZE) ? 1 : -1];
typedef char zxvfs_assert_sb[(sizeof(zxvfs_superblock_t) == 512) ? 1 : -1];
typedef char zxvfs_assert_jhdr[(sizeof(zxvfs_journal_hdr_t) == 512) ? 1 : -1];

#ifdef ZXVFS_HOST
#include <string.h>
#define zmemset memset
#define zmemcpy memcpy
#else
static void *zmemset(void *d, int c, unsigned long n) {
    unsigned char *p = d;
    while (n--) *p++ = (unsigned char)c;
    return d;
}
static void *zmemcpy(void *d, const void *s, unsigned long n) {
    unsigned char *a = d; const unsigned char *b = s;
    while (n--) *a++ = *b++;
    return d;
}
#endif

static int zstreq(const char *a, const char *b) {
    while (*a && *b) { if (*a != *b) return 0; a++; b++; }
    return *a == *b;
}
static uint32_t zstrlen(const char *s) { uint32_t n = 0; while (s[n]) n++; return n; }

static uint32_t jchecksum(const zxvfs_journal_hdr_t *h) {
    uint32_t c = 0x9E3779B9u ^ h->txn_id ^ (h->count * 2654435761u);
    for (uint32_t i = 0; i < h->count && i < ZXVFS_JOURNAL_MAX; i++)
        c = (c * 16777619u) ^ h->target_lba[i];
    return c;
}

static int rd(zxvfs_t *fs, uint32_t lba, uint8_t *buf) {
    return blockdev_read(fs->dev, lba, buf);
}
static int wr(zxvfs_t *fs, uint32_t lba, const uint8_t *buf) {
    return blockdev_write(fs->dev, lba, buf);
}

/* ---- journal ---- */

/* Under copy-on-write, DATA is never journalled — it is written to sectors
 * nothing references yet. So a legitimate transaction only ever targets
 * METADATA, and we can refuse anything else outright. On replay the header is
 * untrusted (a torn or crafted journal), and an unchecked target_lba is a
 * write-what-where primitive (red-team); this bound is now tight enough that a
 * crafted journal cannot even be aimed into the data region. */
static int journal_target_ok(uint32_t lba) {
    return lba >= ZXVFS_BITMAP_SECTOR && lba < ZXVFS_DATA_SECTOR;
}

static int journal_checkpoint(zxvfs_t *fs, const zxvfs_journal_hdr_t *h) {
    uint8_t sec[BLOCKDEV_SECTOR_SIZE];
    if (h->count > ZXVFS_JOURNAL_MAX) return -1;
    for (uint32_t i = 0; i < h->count; i++)
        if (!journal_target_ok(h->target_lba[i])) return -1;
    for (uint32_t i = 0; i < h->count; i++) {
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

static int journal_txn(zxvfs_t *fs, const uint32_t *target,
                       const uint8_t *const *data, uint32_t count) {
    if (count == 0) return 0;
    if (count > ZXVFS_JOURNAL_MAX) return -1;
    for (uint32_t i = 0; i < count; i++)
        if (!journal_target_ok(target[i])) return -1;

    for (uint32_t i = 0; i < count; i++)
        if (wr(fs, ZXVFS_JSTAGE_SECTOR + i, data[i]) != 0) return -1;

    zxvfs_journal_hdr_t h;
    zmemset(&h, 0, sizeof(h));
    h.magic = ZXVFS_JRNL_MAGIC;
    h.committed = 1;
    h.txn_id = fs->next_txn++;
    h.count = count;
    for (uint32_t i = 0; i < count; i++) h.target_lba[i] = target[i];
    h.checksum = jchecksum(&h);
    if (wr(fs, ZXVFS_JHDR_SECTOR, (const uint8_t *)&h) != 0) return -1;

    if (journal_checkpoint(fs, &h) != 0) return -1;
    return journal_clear(fs);
}

static int journal_recover(zxvfs_t *fs) {
    uint8_t sec[BLOCKDEV_SECTOR_SIZE];
    if (rd(fs, ZXVFS_JHDR_SECTOR, sec) != 0) return -1;
    zxvfs_journal_hdr_t *h = (zxvfs_journal_hdr_t *)sec;
    if (h->magic != ZXVFS_JRNL_MAGIC) return 0;
    if (!h->committed) return 0;
    if (h->count == 0 || h->count > ZXVFS_JOURNAL_MAX) return 0;
    if (h->checksum != jchecksum(h)) return 0;      /* torn commit: ignore */
    for (uint32_t i = 0; i < h->count; i++)
        if (!journal_target_ok(h->target_lba[i])) return journal_clear(fs);
    if (journal_checkpoint(fs, h) != 0) return -1;
    fs->journal_replays++;
    return journal_clear(fs);
}

/* ---- allocation bitmap ----
 * One bit per data sector. Held in memory for the duration of an operation and
 * committed as part of the same transaction as the inode, so allocation state
 * and file state can never disagree after a crash. */
static uint8_t g_bitmap[ZXVFS_BITMAP_BYTES];

static int bitmap_load(zxvfs_t *fs) {
    for (uint32_t s = 0; s < ZXVFS_BITMAP_SECTORS; s++)
        if (rd(fs, ZXVFS_BITMAP_SECTOR + s,
               g_bitmap + s * BLOCKDEV_SECTOR_SIZE) != 0) return -1;
    return 0;
}
static int bit_get(uint32_t i) { return (g_bitmap[i >> 3] >> (i & 7)) & 1; }
static void bit_set(uint32_t i) { g_bitmap[i >> 3] |= (uint8_t)(1u << (i & 7)); }
static void bit_clr(uint32_t i) { g_bitmap[i >> 3] &= (uint8_t)~(1u << (i & 7)); }

static uint32_t bitmap_free_count(void) {
    uint32_t n = 0;
    for (uint32_t i = 0; i < ZXVFS_DATA_SECTORS; i++) if (!bit_get(i)) n++;
    return n;
}

/* First-fit: find the longest contiguous free run starting at or after `from`,
 * capped at `want`. Returns run length (0 if none) and sets *start. */
static uint32_t find_run(uint32_t from, uint32_t want, uint32_t *start) {
    uint32_t i = from;
    while (i < ZXVFS_DATA_SECTORS) {
        if (bit_get(i)) { i++; continue; }
        uint32_t s = i, n = 0;
        while (i < ZXVFS_DATA_SECTORS && !bit_get(i) && n < want) { i++; n++; }
        *start = s;
        return n;
    }
    return 0;
}

/* Allocate `want` sectors into up to ZXVFS_MAX_EXTENTS extents, marking them
 * used in the in-memory bitmap. Returns extent count, or -1 if the space could
 * not be assembled (too fragmented, or not enough free). */
/* Where the next allocation prefers to start. Advisory only. */
static uint32_t g_alloc_hint = 0;
void zxvfs_set_alloc_hint(uint32_t h) {
    g_alloc_hint = (h < ZXVFS_DATA_SECTORS) ? h : 0;
}

static int alloc_extents(uint32_t want, zxvfs_extent_t *out) {
    uint32_t got = 0, ne = 0, cursor = g_alloc_hint;
    /* Try from the hint first; if that tail cannot satisfy the request, fall
     * back to a full scan from 0. Locality is a preference, never a
     * precondition — a filesystem that fails to allocate because it could not
     * be tidy would be worse than one that is untidy. */
    if (cursor != 0) {
        uint32_t probe_start = 0;
        if (find_run(cursor, want, &probe_start) < want) cursor = 0;
    }
    while (got < want) {
        if (ne >= ZXVFS_MAX_EXTENTS) return -1;
        uint32_t start = 0;
        uint32_t n = find_run(cursor, want - got, &start);
        if (n == 0) return -1;
        out[ne].start = start; out[ne].count = n;
        for (uint32_t k = 0; k < n; k++) bit_set(start + k);
        got += n; cursor = start + n; ne++;
    }
    return (int)ne;
}

static void free_extents(const zxvfs_inode_t *in) {
    for (uint32_t e = 0; e < in->nextents && e < ZXVFS_MAX_EXTENTS; e++)
        for (uint32_t k = 0; k < in->extent[e].count; k++)
            bit_clr(in->extent[e].start + k);
}

/* ---- inode helpers ---- */
static uint32_t inode_sector_for(uint32_t idx) {
    return ZXVFS_INODE_SECTOR + idx / ZXVFS_INODES_PER_SECTOR;
}
static uint32_t inode_offset_in_sector(uint32_t idx) {
    return (idx % ZXVFS_INODES_PER_SECTOR) * sizeof(zxvfs_inode_t);
}

/* v1 defended against a crafted inode by recomputing the extent base from the
 * inode index. With real extents that is impossible — the extents ARE the
 * location — so the equivalent guard is to validate them: every extent must lie
 * inside the data region (with no integer wrap), and the file's size must not
 * exceed the space its extents actually cover. An inode failing this is treated
 * as absent rather than followed off the end of the region (red-team). */
static int inode_valid(const zxvfs_inode_t *in) {
    if (!in->used) return 1;                       /* free slots are fine */
    if (in->nextents > ZXVFS_MAX_EXTENTS) return 0;
    uint32_t cap = 0;
    for (uint32_t e = 0; e < in->nextents; e++) {
        uint32_t s = in->extent[e].start, c = in->extent[e].count;
        if (c == 0) return 0;
        if (s >= ZXVFS_DATA_SECTORS) return 0;
        if (c > ZXVFS_DATA_SECTORS - s) return 0;  /* wrap / past the end */
        cap += c;
    }
    if (cap > ZXVFS_DATA_SECTORS) return 0;
    if (in->size > cap * BLOCKDEV_SECTOR_SIZE) return 0;
    return 1;
}

static int load_inode(zxvfs_t *fs, uint32_t idx, zxvfs_inode_t *out) {
    uint8_t sec[BLOCKDEV_SECTOR_SIZE];
    if (rd(fs, inode_sector_for(idx), sec) != 0) return -1;
    zmemcpy(out, sec + inode_offset_in_sector(idx), sizeof(*out));
    if (!inode_valid(out)) { zmemset(out, 0, sizeof(*out)); return -2; }
    return 0;
}

static int find_inode(zxvfs_t *fs, const char *name) {
    for (uint32_t i = 0; i < ZXVFS_MAX_FILES; i++) {
        zxvfs_inode_t in;
        if (load_inode(fs, i, &in) != 0) continue;   /* skip invalid slots */
        if (in.used && zstreq(in.name, name)) return (int)i;
    }
    return -1;
}

static int find_free_inode(zxvfs_t *fs) {
    for (uint32_t i = 0; i < ZXVFS_MAX_FILES; i++) {
        zxvfs_inode_t in;
        if (load_inode(fs, i, &in) != 0) continue;
        if (!in.used) return (int)i;
    }
    return -1;
}

/* Absolute LBA of the file's `i`-th sector, or 0 if beyond its extents. */
static uint32_t file_lba(zxvfs_t *fs, const zxvfs_inode_t *in, uint32_t i) {
    for (uint32_t e = 0; e < in->nextents && e < ZXVFS_MAX_EXTENTS; e++) {
        if (i < in->extent[e].count)
            return fs->sb.data_sector + in->extent[e].start + i;
        i -= in->extent[e].count;
    }
    return 0;
}

/* Commit a new inode + the current bitmap in ONE transaction. */
static int commit_meta(zxvfs_t *fs, uint32_t idx, const zxvfs_inode_t *in) {
    static uint8_t staged[ZXVFS_BITMAP_SECTORS + 1][BLOCKDEV_SECTOR_SIZE];
    const uint8_t *dptr[ZXVFS_BITMAP_SECTORS + 1];
    uint32_t target[ZXVFS_BITMAP_SECTORS + 1];
    uint32_t n = 0;

    for (uint32_t s = 0; s < ZXVFS_BITMAP_SECTORS; s++) {
        zmemcpy(staged[n], g_bitmap + s * BLOCKDEV_SECTOR_SIZE, BLOCKDEV_SECTOR_SIZE);
        target[n] = ZXVFS_BITMAP_SECTOR + s;
        dptr[n] = staged[n];
        n++;
    }
    uint32_t isec = inode_sector_for(idx);
    if (rd(fs, isec, staged[n]) != 0) return -1;
    zmemcpy(staged[n] + inode_offset_in_sector(idx), in, sizeof(*in));
    target[n] = isec; dptr[n] = staged[n]; n++;

    return journal_txn(fs, target, dptr, n);
}

/* ---- public API ---- */

int zxvfs_format(block_device_t *dev) {
    if (!dev || !dev->present) return -1;
    uint8_t sec[BLOCKDEV_SECTOR_SIZE];

    zmemset(sec, 0, sizeof(sec));
    zxvfs_superblock_t *sb = (zxvfs_superblock_t *)sec;
    sb->magic = ZXVFS_MAGIC;
    sb->version = ZXVFS_VERSION;
    sb->total_sectors = ZXVFS_TOTAL_SECTORS;
    sb->inode_sector = ZXVFS_INODE_SECTOR;
    sb->inode_count = ZXVFS_MAX_FILES;
    sb->data_sector = ZXVFS_DATA_SECTOR;
    sb->bitmap_sector = ZXVFS_BITMAP_SECTOR;
    sb->data_sectors = ZXVFS_DATA_SECTORS;
    sb->generation = 1;
    if (blockdev_write(dev, ZXVFS_SB_SECTOR, sec) != 0) return -1;

    zmemset(sec, 0, sizeof(sec));
    ((zxvfs_journal_hdr_t *)sec)->magic = ZXVFS_JRNL_MAGIC;
    if (blockdev_write(dev, ZXVFS_JHDR_SECTOR, sec) != 0) return -1;

    zmemset(sec, 0, sizeof(sec));
    for (uint32_t s = 0; s < ZXVFS_BITMAP_SECTORS; s++)
        if (blockdev_write(dev, ZXVFS_BITMAP_SECTOR + s, sec) != 0) return -1;
    for (uint32_t s = 0; s < ZXVFS_INODE_SECTORS; s++)
        if (blockdev_write(dev, ZXVFS_INODE_SECTOR + s, sec) != 0) return -1;
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
    /* FIXED-LAYOUT filesystem: the on-disk geometry MUST match the
     * compile-time constants. Trusting a crafted superblock's data_sector
     * would redirect file writes onto metadata (red-team). */
    if (sb->total_sectors != ZXVFS_TOTAL_SECTORS ||
        sb->inode_sector  != ZXVFS_INODE_SECTOR  ||
        sb->data_sector   != ZXVFS_DATA_SECTOR   ||
        sb->bitmap_sector != ZXVFS_BITMAP_SECTOR ||
        sb->data_sectors  != ZXVFS_DATA_SECTORS  ||
        sb->inode_count   != ZXVFS_MAX_FILES) return -2;
    zmemcpy(&fs->sb, sb, sizeof(fs->sb));

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
    int existed = (idx >= 0);
    if (idx < 0) idx = find_free_inode(fs);
    if (idx < 0) return -4;                       /* directory full */

    zxvfs_inode_t old;
    zmemset(&old, 0, sizeof(old));
    if (existed && load_inode(fs, (uint32_t)idx, &old) != 0) return -6;

    if (bitmap_load(fs) != 0) return -6;

    /* COPY-ON-WRITE: allocate the new home BEFORE releasing the old one, so a
     * crash at any point leaves the old file wholly intact. The price is that a
     * replace needs room for both copies at once. */
    uint32_t nsectors = (len + BLOCKDEV_SECTOR_SIZE - 1) / BLOCKDEV_SECTOR_SIZE;
    zxvfs_inode_t nw;
    zmemset(&nw, 0, sizeof(nw));
    int ne = 0;
    if (nsectors > 0) {
        ne = alloc_extents(nsectors, nw.extent);
        if (ne < 0) return -5;                    /* no space / too fragmented */
    }
    nw.nextents = (uint32_t)ne;
    nw.size = len;
    nw.used = 1;
    uint32_t nl = zstrlen(name);
    zmemcpy(nw.name, name, nl);
    nw.name[nl] = '\0';

    /* Data goes to sectors nothing references yet — invisible until the commit. */
    uint8_t sec[BLOCKDEV_SECTOR_SIZE];
    for (uint32_t s = 0; s < nsectors; s++) {
        uint32_t off = s * BLOCKDEV_SECTOR_SIZE;
        uint32_t chunk = len - off;
        if (chunk > BLOCKDEV_SECTOR_SIZE) chunk = BLOCKDEV_SECTOR_SIZE;
        zmemset(sec, 0, sizeof(sec));
        zmemcpy(sec, data + off, chunk);
        uint32_t lba = file_lba(fs, &nw, s);
        if (lba == 0 || wr(fs, lba, sec) != 0) return -7;
    }

    /* Only now release the old extents, in the same bitmap the commit carries. */
    if (existed) free_extents(&old);

    return commit_meta(fs, (uint32_t)idx, &nw) == 0 ? 0 : -8;
}

int zxvfs_read(zxvfs_t *fs, const char *name, uint8_t *buf, uint32_t max) {
    if (!fs || !fs->mounted || !name || !buf) return -1;
    int idx = find_inode(fs, name);
    if (idx < 0) return -2;
    zxvfs_inode_t in;
    if (load_inode(fs, (uint32_t)idx, &in) != 0) return -3;

    uint32_t len = in.size;
    if (len > max) len = max;
    uint32_t got = 0, s = 0;
    uint8_t sec[BLOCKDEV_SECTOR_SIZE];
    while (got < len) {
        uint32_t lba = file_lba(fs, &in, s);
        if (lba == 0) break;                       /* extents exhausted */
        if (rd(fs, lba, sec) != 0) return -4;
        uint32_t chunk = len - got;
        if (chunk > BLOCKDEV_SECTOR_SIZE) chunk = BLOCKDEV_SECTOR_SIZE;
        zmemcpy(buf + got, sec, chunk);
        got += chunk; s++;
    }
    return (int)got;
}

int zxvfs_pread(zxvfs_t *fs, const char *name, uint32_t offset,
                uint8_t *buf, uint32_t len) {
    if (!fs || !fs->mounted || !name || !buf) return -1;
    int idx = find_inode(fs, name);
    if (idx < 0) return -2;
    zxvfs_inode_t in;
    if (load_inode(fs, (uint32_t)idx, &in) != 0) return -3;
    if (offset >= in.size) return 0;               /* at or past EOF */
    if (len > in.size - offset) len = in.size - offset;

    uint32_t got = 0;
    uint8_t sec[BLOCKDEV_SECTOR_SIZE];
    while (got < len) {
        uint32_t pos = offset + got;
        uint32_t s = pos / BLOCKDEV_SECTOR_SIZE;
        uint32_t in_sec = pos % BLOCKDEV_SECTOR_SIZE;
        uint32_t lba = file_lba(fs, &in, s);
        if (lba == 0) break;
        if (rd(fs, lba, sec) != 0) return -4;
        uint32_t chunk = BLOCKDEV_SECTOR_SIZE - in_sec;
        if (chunk > len - got) chunk = len - got;
        zmemcpy(buf + got, sec + in_sec, chunk);
        got += chunk;
    }
    return (int)got;
}

int zxvfs_pwrite(zxvfs_t *fs, const char *name, uint32_t offset,
                 const uint8_t *data, uint32_t len) {
    if (!fs || !fs->mounted || !name || !data) return -1;
    int idx = find_inode(fs, name);
    if (idx < 0) return -2;
    zxvfs_inode_t in;
    if (load_inode(fs, (uint32_t)idx, &in) != 0) return -3;
    if (len == 0) return 0;
    if (offset > in.size) return -9;               /* no sparse holes */

    uint32_t end = offset + len;
    if (end < offset) return -9;                   /* overflow */

    /* Growing needs new sectors. They are appended as fresh extents and remain
     * unreferenced until the metadata commit, so GROWTH is crash-atomic even
     * though the in-place portion below is not. */
    uint32_t have_sectors = 0;
    for (uint32_t e = 0; e < in.nextents; e++) have_sectors += in.extent[e].count;
    uint32_t need_sectors = (end + BLOCKDEV_SECTOR_SIZE - 1) / BLOCKDEV_SECTOR_SIZE;

    if (need_sectors > have_sectors) {
        if (bitmap_load(fs) != 0) return -6;
        uint32_t want = need_sectors - have_sectors;
        zxvfs_extent_t add[ZXVFS_MAX_EXTENTS];
        int ne = alloc_extents(want, add);
        if (ne < 0) return -5;
        if (in.nextents + (uint32_t)ne > ZXVFS_MAX_EXTENTS) return -5;
        for (int e = 0; e < ne; e++) in.extent[in.nextents + (uint32_t)e] = add[e];
        in.nextents += (uint32_t)ne;
    }

    uint32_t done = 0;
    uint8_t sec[BLOCKDEV_SECTOR_SIZE];
    while (done < len) {
        uint32_t pos = offset + done;
        uint32_t s = pos / BLOCKDEV_SECTOR_SIZE;
        uint32_t in_sec = pos % BLOCKDEV_SECTOR_SIZE;
        uint32_t lba = file_lba(fs, &in, s);
        if (lba == 0) break;
        uint32_t chunk = BLOCKDEV_SECTOR_SIZE - in_sec;
        if (chunk > len - done) chunk = len - done;
        if (in_sec != 0 || chunk != BLOCKDEV_SECTOR_SIZE) {
            if (rd(fs, lba, sec) != 0) return -4;   /* read-modify-write */
        } else {
            zmemset(sec, 0, sizeof(sec));
        }
        zmemcpy(sec + in_sec, data + done, chunk);
        if (wr(fs, lba, sec) != 0) return -7;
        done += chunk;
    }

    if (end > in.size) {
        in.size = end;
        if (need_sectors > have_sectors) {
            if (commit_meta(fs, (uint32_t)idx, &in) != 0) return -8;
        } else {
            /* size-only change: the bitmap is unchanged, but commit_meta
             * carries it anyway, so load it to avoid writing stale bits. */
            if (bitmap_load(fs) != 0) return -6;
            if (commit_meta(fs, (uint32_t)idx, &in) != 0) return -8;
        }
    }
    return (int)done;
}

int zxvfs_size(zxvfs_t *fs, const char *name) {
    if (!fs || !fs->mounted || !name) return -1;
    int idx = find_inode(fs, name);
    if (idx < 0) return -2;
    zxvfs_inode_t in;
    if (load_inode(fs, (uint32_t)idx, &in) != 0) return -3;
    return (int)in.size;
}

int zxvfs_unlink(zxvfs_t *fs, const char *name) {
    if (!fs || !fs->mounted || !name) return -1;
    int idx = find_inode(fs, name);
    if (idx < 0) return -2;
    zxvfs_inode_t in;
    if (load_inode(fs, (uint32_t)idx, &in) != 0) return -3;

    if (bitmap_load(fs) != 0) return -6;
    free_extents(&in);
    zxvfs_inode_t empty;
    zmemset(&empty, 0, sizeof(empty));
    return commit_meta(fs, (uint32_t)idx, &empty) == 0 ? 0 : -8;
}

int zxvfs_list(zxvfs_t *fs, char names[][ZXVFS_NAME_LEN], uint32_t *sizes,
               uint32_t max) {
    if (!fs || !fs->mounted) return -1;
    uint32_t n = 0;
    for (uint32_t i = 0; i < ZXVFS_MAX_FILES && n < max; i++) {
        zxvfs_inode_t in;
        if (load_inode(fs, i, &in) != 0) continue;
        if (!in.used) continue;
        if (names) zmemcpy(names[n], in.name, ZXVFS_NAME_LEN);
        if (sizes) sizes[n] = in.size;
        n++;
    }
    return (int)n;
}

int zxvfs_count(zxvfs_t *fs) {
    if (!fs || !fs->mounted) return -1;
    uint32_t n = 0;
    for (uint32_t i = 0; i < ZXVFS_MAX_FILES; i++) {
        zxvfs_inode_t in;
        if (load_inode(fs, i, &in) != 0) continue;
        if (in.used) n++;
    }
    return (int)n;
}

int zxvfs_free_sectors(zxvfs_t *fs) {
    if (!fs || !fs->mounted) return -1;
    if (bitmap_load(fs) != 0) return -1;
    return (int)bitmap_free_count();
}
