/* test_zxvfs_fuzz.c — randomized crash fuzzer for ZXVFS + Tri-Space storage.
 *
 * WHAT THIS ADDS OVER test_zxvfs_tri.c
 * ------------------------------------
 * That suite crashes at EVERY write of ONE fixed triad write — exhaustive, but
 * for a single scenario on a clean disk. It cannot reach the state space that
 * actually worries me: an allocator under churn. Files replaced, grown,
 * unlinked, triads rewritten, space recycled — and a crash somewhere in the
 * middle of all that.
 *
 * THE ORACLE IS THE POINT
 * -----------------------
 * A fuzzer without an oracle only finds crashes. The invariant checked here
 * after every recovery is the filesystem-consistency one (what fsck exists to
 * verify), and it catches SILENT corruption that no amount of read-back testing
 * would notice:
 *
 *   DOUBLE-ALLOCATED  two live files claim the same sector. Writing one
 *                     silently corrupts the other. The worst failure a
 *                     filesystem can have, and completely invisible until the
 *                     day it matters.
 *   UNMARKED          a live file's sector is FREE in the bitmap, so the
 *                     allocator will hand it out again — double-allocation
 *                     that has not happened yet.
 *   LEAKED            a sector is marked used but no file references it. Not
 *                     corruption, only lost space — but under copy-on-write
 *                     the bitmap and the inode commit in the SAME transaction,
 *                     so a leak would mean that atomicity is broken. This
 *                     asserts zero leaks rather than tolerating them.
 *
 * Plus the tri-space invariant: any triad that still reports BOUND after
 * recovery must open completely, with all three roles intact. That check runs
 * AFTER the post-recovery operations, and it is the only DATA-level oracle here
 * (it re-derives digests) — fsck sees metadata only, so data corruption under
 * consistent metadata is outside its view for plain files.
 *
 * HONEST STATUS OF THE LAG DETECTOR: the machinery distinguishes "surfaced at
 * recovery" from "surfaced N operations later" and reports the distance. Both
 * deliberate faults injected to validate the oracle (bitmap not committed; old
 * extents never freed) detonated IMMEDIATELY, so the delayed path is exercised
 * and reported but has not yet been proven against a genuinely lagged bug. It
 * is instrumentation that is ready for one, not evidence that one was caught.
 *
 * GAME MASTER CONVENTIONS
 * -----------------------
 * Follows the harness's methodology (build_system/game_master.py): a seeded,
 * reproducible workload; both data POLARITIES (S+ raw, S- the arithmetic
 * negative, bytes XOR 0xFF) so the stack is exercised in both; and on failure
 * it prints the exact seed to replay. Where Game Master fault-stresses the whole
 * booted system under QEMU, this fault-stresses the storage layer directly —
 * thousands of crash/recover cycles a second instead of one boot at a time.
 *
 * Build/run:
 *   gcc -std=c11 -Wall -Wextra -Werror -Iinclude -Isrc/zxvfs \
 *       src/zxvfs/test_zxvfs_fuzz.c src/zxvfs/zxvfs_tri.c src/zxvfs/zxvfs.c \
 *       src/trispace/trispace.c src/robin_debanks/sha256.c src/zab/zab.c \
 *       src/zab/zab_exec.c src/invproof/invproof.c -o /tmp/fuzz && /tmp/fuzz [iters] [seed]
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "zxvfs_tri.h"
#include "../zab/zab.h"
#include "../fractal/zorder.h"

#define DISK_SECTORS ZXVFS_TOTAL_SECTORS
static uint8_t g_disk[DISK_SECTORS][BLOCKDEV_SECTOR_SIZE];
static uint8_t snapshot[DISK_SECTORS][BLOCKDEV_SECTOR_SIZE];

static long g_crash_after = -1, g_writes = 0;
static int g_crashed = 0;

static int mem_read(block_device_t *dev, uint32_t lba, uint8_t *buf)
{
    (void) dev;
    if (lba >= DISK_SECTORS) return -1;
    memcpy(buf, g_disk[lba], BLOCKDEV_SECTOR_SIZE);
    return 0;
}
static int mem_write(block_device_t *dev, uint32_t lba, const uint8_t *buf)
{
    (void) dev;
    if (lba >= DISK_SECTORS) return -1;
    if (g_crashed) return -1;
    if (g_crash_after >= 0 && g_writes >= g_crash_after) {
        g_crashed = 1;
        return -1;
    }
    g_writes++;
    memcpy(g_disk[lba], buf, BLOCKDEV_SECTOR_SIZE);
    return 0;
}
static void dev_init(block_device_t *d)
{
    memset(d, 0, sizeof(*d));
    d->present = true;
    d->total_sectors = DISK_SECTORS;
    d->read_sector = mem_read;
    d->write_sector = mem_write;
}

/* deterministic PRNG — the whole run must be replayable from its seed */
static uint32_t rng_state;
static uint32_t rnd(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}
static uint32_t rnd_max(uint32_t n)
{
    return n ? rnd() % n : 0;
}

/* ---------------- the oracle: filesystem consistency ---------------- */
static uint8_t shadow[ZXVFS_BITMAP_BYTES];
static int sh_get(uint32_t i)
{
    return (shadow[i >> 3] >> (i & 7)) & 1;
}
static void sh_set(uint32_t i)
{
    shadow[i >> 3] |= (uint8_t) (1u << (i & 7));
}

typedef struct {
    int dbl, unmarked, leaked, bad_inode;
} fsck_t;

static void fsck(block_device_t *dev, fsck_t *r)
{
    memset(r, 0, sizeof(*r));
    memset(shadow, 0, sizeof(shadow));

    static uint8_t bm[ZXVFS_BITMAP_BYTES];
    for (uint32_t s = 0; s < ZXVFS_BITMAP_SECTORS; s++)
        mem_read(dev, ZXVFS_BITMAP_SECTOR + s, bm + s * BLOCKDEV_SECTOR_SIZE);

    uint8_t sec[BLOCKDEV_SECTOR_SIZE];
    for (uint32_t i = 0; i < ZXVFS_MAX_FILES; i++) {
        mem_read(dev, ZXVFS_INODE_SECTOR + i / ZXVFS_INODES_PER_SECTOR, sec);
        zxvfs_inode_t in;
        memcpy(&in, sec + (i % ZXVFS_INODES_PER_SECTOR) * sizeof(zxvfs_inode_t), sizeof(in));
        if (!in.used) continue;
        if (in.nextents > ZXVFS_MAX_EXTENTS) {
            r->bad_inode++;
            continue;
        }
        for (uint32_t e = 0; e < in.nextents; e++) {
            uint32_t st = in.extent[e].start, c = in.extent[e].count;
            if (c == 0 || st >= ZXVFS_DATA_SECTORS || c > ZXVFS_DATA_SECTORS - st) {
                r->bad_inode++;
                break;
            }
            for (uint32_t k = 0; k < c; k++) {
                uint32_t b = st + k;
                if (sh_get(b)) r->dbl++; /* two owners */
                sh_set(b);
                if (!((bm[b >> 3] >> (b & 7)) & 1)) r->unmarked++;
            }
        }
    }
    for (uint32_t b = 0; b < ZXVFS_DATA_SECTORS; b++)
        if (((bm[b >> 3] >> (b & 7)) & 1) && !sh_get(b)) r->leaked++;
}

/* ---------------- workload ---------------- */
static const char *FILES[] = {"alpha", "beta", "gamma", "delta"};
static const char *TRIADS[] = {"tri0", "tri1"};
#define POST_OPS 12u /* operations to keep running AFTER recovery */
static uint8_t payload[9000];

/* Game Master polarity: S+ is the raw dataset, S- its arithmetic negative. */
static void fill_payload(uint32_t len, int negative, uint32_t salt)
{
    for (uint32_t i = 0; i < len; i++) {
        uint8_t v = (uint8_t) ((i * 31u + salt * 7u) & 0xFF);
        payload[i] = negative ? (uint8_t) (v ^ 0xFF) : v;
    }
}

static uint8_t P_S[64], P_M[64], P_Z[64];
static uint32_t L_S, L_M, L_Z;
static uint32_t mkprog(uint8_t *out, const uint8_t *ops, uint16_t n)
{
    zab_ins_t ins[16];
    zab_header_t h;
    for (uint16_t i = 0; i < n; i++) {
        ins[i].op = ops[i];
        ins[i].a = 0;
        ins[i].b = 0;
    }
    h.magic = ZAB_MAGIC;
    h.version = ZAB_VERSION;
    h.count = n;
    h.seal = 0;
    uint32_t seal = zab_compute_seal(&h, ins, n), o = 0;
    out[o++] = (uint8_t) (ZAB_MAGIC);
    out[o++] = (uint8_t) (ZAB_MAGIC >> 8);
    out[o++] = (uint8_t) (ZAB_MAGIC >> 16);
    out[o++] = (uint8_t) (ZAB_MAGIC >> 24);
    out[o++] = (uint8_t) (ZAB_VERSION);
    out[o++] = (uint8_t) (ZAB_VERSION >> 8);
    out[o++] = (uint8_t) (n);
    out[o++] = (uint8_t) (n >> 8);
    out[o++] = (uint8_t) (seal);
    out[o++] = (uint8_t) (seal >> 8);
    out[o++] = (uint8_t) (seal >> 16);
    out[o++] = (uint8_t) (seal >> 24);
    for (uint16_t i = 0; i < n; i++) {
        out[o++] = ins[i].op;
        out[o++] = ins[i].a;
        out[o++] = (uint8_t) (ins[i].b);
        out[o++] = (uint8_t) (ins[i].b >> 8);
    }
    return o;
}
static void build_progs(void)
{
    const uint8_t sp[] = {ZAB_OP_READ, ZAB_OP_POST, ZAB_OP_END};
    const uint8_t sm[] = {ZAB_OP_POST, ZAB_OP_END};
    const uint8_t sz[] = {ZAB_OP_NOP, ZAB_OP_END};
    L_S = mkprog(P_S, sp, 3);
    L_M = mkprog(P_M, sm, 2);
    L_Z = mkprog(P_Z, sz, 2);
}
static void tri_spec(zxvfs_tri_spec_t *s)
{
    memset(s, 0, sizeof(*s));
    s->data[TRI_POSITIVE] = P_S;
    s->len[TRI_POSITIVE] = L_S;
    s->data[TRI_NEGATIVE] = P_M;
    s->len[TRI_NEGATIVE] = L_M;
    s->data[TRI_NEUTRAL] = P_Z;
    s->len[TRI_NEUTRAL] = L_Z;
    s->capability_set[TRI_POSITIVE] = ZAB_CAP_READ_STATE | ZAB_CAP_LEDGER;
    s->capability_set[TRI_NEGATIVE] = ZAB_CAP_LEDGER;
    s->capability_set[TRI_NEUTRAL] = ZAB_CAP_NONE;
    s->inverse_kind = TRI_INV_EXACT;
    for (uint32_t i = 0; i < TRI_ID_LEN; i++) s->triad_id[i] = (uint8_t) (i + 1);
    for (uint32_t i = 0; i < TRI_DIGEST_LEN; i++) s->source_graph_digest[i] = (uint8_t) (0xA0 + i);
}

/* Perform one random operation. Returns 0 (its return value is not the point;
 * the point is what the disk looks like afterwards). */
static void do_op(zxvfs_t *fs, uint32_t op)
{
    zxvfs_tri_spec_t sp;
    switch (op % 6) {
    case 0:
    case 1: { /* whole-file write (CoW) */
        uint32_t len = 1 + rnd_max(6000);
        fill_payload(len, (int) (rnd() & 1), rnd());
        zxvfs_write(fs, FILES[rnd_max(4)], payload, len);
        break;
    }
    case 2: { /* positional write / grow */
        const char *n = FILES[rnd_max(4)];
        int sz = zxvfs_size(fs, n);
        if (sz < 0) {
            fill_payload(512, 0, rnd());
            zxvfs_write(fs, n, payload, 512);
            break;
        }
        uint32_t off = (uint32_t) rnd_max((uint32_t) sz + 1);
        uint32_t len = 1 + rnd_max(1500);
        fill_payload(len, (int) (rnd() & 1), rnd());
        zxvfs_pwrite(fs, n, off, payload, len);
        break;
    }
    case 3:
        zxvfs_unlink(fs, FILES[rnd_max(4)]);
        break;
    case 4:
        tri_spec(&sp);
        zxvfs_tri_write(fs, TRIADS[rnd_max(2)], &sp);
        break;
    case 5:
        zxvfs_tri_unlink(fs, TRIADS[rnd_max(2)]);
        break;
    default:
        break;
    }
}

int main(int argc, char **argv)
{
    uint32_t iters = (argc > 1) ? (uint32_t) strtoul(argv[1], 0, 0) : 400;
    uint32_t seed0 = (argc > 2) ? (uint32_t) strtoul(argv[2], 0, 0) : 0x5A585646u;
    block_device_t dev;
    dev_init(&dev);
    build_progs();

    printf("ZXVFS crash fuzzer (Game Master methodology): %u iterations, seed 0x%08X\n", iters,
           seed0);

    /* ---- SOAK: latent corruption with no crash at all ----
     * A crash is not the only way to corrupt a filesystem, and in a system
     * where effects are deferred, damage introduced by an ORDINARY operation
     * may not manifest for many operations more. So before the crash campaign,
     * run a long clean workload and check consistency after EVERY operation:
     * that pins the fault to the operation that caused it rather than to
     * whichever later one happened to trip over it. */
    {
        rng_state = seed0 ^ 0xA5A5A5A5u;
        memset(g_disk, 0, sizeof(g_disk));
        g_crash_after = -1;
        g_writes = 0;
        g_crashed = 0;
        zxvfs_t sf;
        uint32_t soak_bad = 0, soak_ops = 2000;
        if (zxvfs_format(&dev) == 0 && zxvfs_mount(&sf, &dev) == 0) {
            for (uint32_t k = 0; k < soak_ops; k++) {
                do_op(&sf, rnd());
                fsck_t r;
                fsck(&dev, &r);
                if (r.dbl || r.unmarked || r.leaked || r.bad_inode) {
                    printf("  [VIOLATION] soak: op %u introduced "
                           "double=%d unmarked=%d leaked=%d bad_inode=%d\n",
                           k, r.dbl, r.unmarked, r.leaked, r.bad_inode);
                    soak_bad++;
                    break;
                }
            }
        } else
            soak_bad++;
        if (soak_bad == 0)
            printf("  [PASS] soak: %u operations, consistent after every single one\n", soak_ops);
        else
            return (printf("\nFAILED zxvfs_fuzz: soak\n"), 1);
    }

    /* ---- LOCALITY: measured against a baseline, per zorder.h's own standard.
     * "less data movement for the same result, measured against a baseline
     * placement" -- never "geometry makes it faster". We churn the disk, then
     * measure how far apart one triad's four files end up, with the Z-order
     * hint and without it. If the ratio is 1.0 the benefit is absent and this
     * says so. */
    {
        uint32_t spread_hint = 0, spread_plain = 0;
        for (int mode = 0; mode < 2; mode++) {
            rng_state = seed0 ^ 0x5EED10C0u;
            memset(g_disk, 0, sizeof(g_disk));
            g_crash_after = -1;
            g_writes = 0;
            g_crashed = 0;
            zxvfs_t lf;
            if (zxvfs_format(&dev) != 0 || zxvfs_mount(&lf, &dev) != 0) continue;
            /* churn: fill and free so the free list is fragmented */
            for (int k = 0; k < 40; k++) {
                uint32_t len = 512 + rnd_max(4000);
                fill_payload(len, 0, rnd());
                zxvfs_write(&lf, FILES[rnd_max(4)], payload, len);
                if (rnd() & 1) zxvfs_unlink(&lf, FILES[rnd_max(4)]);
            }
            zxvfs_tri_spec_t sp;
            tri_spec(&sp);
            if (mode == 0) {
                /* baseline: defeat the hint by pinning it to 0 after tri sets it */
                zxvfs_set_alloc_hint(0);
            }
            zxvfs_tri_write(&lf, "loc0", &sp);
            /* measure the span of sectors the triad's files occupy */
            uint32_t lo = 0xFFFFFFFFu, hi = 0;
            const char *parts[4] = {"loc0.zxvc", "loc0.cedez", "loc0.cedec", "loc0.tri"};
            uint8_t sec[BLOCKDEV_SECTOR_SIZE];
            for (uint32_t i = 0; i < ZXVFS_MAX_FILES; i++) {
                mem_read(&dev, ZXVFS_INODE_SECTOR + i / ZXVFS_INODES_PER_SECTOR, sec);
                zxvfs_inode_t in;
                memcpy(&in, sec + (i % ZXVFS_INODES_PER_SECTOR) * sizeof(zxvfs_inode_t),
                       sizeof(in));
                if (!in.used) continue;
                int mine = 0;
                for (int q = 0; q < 4; q++)
                    if (!strcmp(in.name, parts[q])) mine = 1;
                if (!mine) continue;
                for (uint32_t e = 0; e < in.nextents; e++) {
                    uint32_t a = in.extent[e].start, b = a + in.extent[e].count;
                    if (a < lo) lo = a;
                    if (b > hi) hi = b;
                }
            }
            uint32_t span = (hi > lo) ? (hi - lo) : 0;
            if (mode == 0)
                spread_plain = span;
            else
                spread_hint = span;
        }
        printf("  locality: triad span %u sectors WITHOUT the hint, %u WITH\n", spread_plain,
               spread_hint);
        if (spread_plain == 0 || spread_hint == 0)
            printf("  [PASS] locality measured (degenerate span, no claim made)\n");
        else if (spread_hint <= spread_plain)
            printf("  [PASS] Z-order hint did not worsen locality (%u -> %u)\n", spread_plain,
                   spread_hint);
        else
            printf("  [PASS] locality REPORTED honestly: hint made it worse (%u -> %u)\n",
                   spread_plain, spread_hint);
    }

    uint32_t violations = 0, crashes = 0, recovered_triads = 0;
    int lag_seen = -1;
    fsck_t worst;
    memset(&worst, 0, sizeof(worst));
    uint32_t bad_seed = 0;

    for (uint32_t it = 0; it < iters; it++) {
        uint32_t seed = seed0 + it;
        rng_state = seed ? seed : 1;

        memset(g_disk, 0, sizeof(g_disk));
        g_crash_after = -1;
        g_writes = 0;
        g_crashed = 0;
        zxvfs_t fs;
        if (zxvfs_format(&dev) != 0 || zxvfs_mount(&fs, &dev) != 0) {
            violations++;
            continue;
        }

        /* build up arbitrary state, cleanly */
        uint32_t nops = rnd_max(9);
        for (uint32_t k = 0; k < nops; k++) do_op(&fs, rnd());

        /* Then cut power INSIDE the next operation. Picking a crash offset
         * blindly wastes most iterations on operations that finish first, so
         * measure the op on a snapshot, rewind, and replay it with the cut
         * placed somewhere it will actually land. */
        uint32_t op_seed = rnd();
        memcpy(snapshot, g_disk, sizeof(g_disk));
        uint32_t rng_save = rng_state;
        long before = g_writes;
        do_op(&fs, op_seed);
        long op_writes = g_writes - before;

        memcpy(g_disk, snapshot, sizeof(g_disk)); /* rewind the platter */
        rng_state = rng_save;
        g_writes = before;
        g_crashed = 0;
        if (op_writes > 0) {
            g_crash_after = before + (long) rnd_max((uint32_t) op_writes);
            do_op(&fs, op_seed);
            if (g_crashed) crashes++;
        }

        /* reboot */
        g_crashed = 0;
        g_crash_after = -1;
        zxvfs_t rfs;
        if (zxvfs_mount(&rfs, &dev) != 0) {
            printf("  [VIOLATION] seed 0x%08X: mount failed after crash\n", seed);
            violations++;
            continue;
        }

        /* ---- the oracle, at the moment of recovery ---- */
        fsck_t r;
        fsck(&dev, &r);
        if (r.dbl || r.unmarked || r.leaked || r.bad_inode) {
            if (!bad_seed) {
                bad_seed = seed;
                worst = r;
                lag_seen = 0;
            }
            violations++;
            continue;
        }

        /* ---- TEMPORAL LAG: keep operating and keep checking ----
         * Consistent AT recovery is not the same as safe. A crash can leave
         * state that passes fsck now but only becomes corruption after further
         * use -- the fuse is lit, the bomb has not gone off. Checking once at
         * recovery would report that as clean. So continue the workload and
         * re-check after every operation, and record HOW MANY operations later
         * the damage surfaced: that distance is the fuse length, and it is the
         * number you need to debug a deferred fault. */
        for (uint32_t k = 0; k < POST_OPS; k++) {
            do_op(&rfs, rnd());
            fsck_t r2;
            fsck(&dev, &r2);
            if (r2.dbl || r2.unmarked || r2.leaked || r2.bad_inode) {
                if (!bad_seed) {
                    bad_seed = seed;
                    worst = r2;
                    lag_seen = (int) k + 1;
                }
                violations++;
                break;
            }
        }

        /* ---- tri-space invariant: BOUND means complete ---- */
        for (int t = 0; t < 2; t++) {
            if (zxvfs_tri_state(&rfs, TRIADS[t]) != ZXVFS_TRI_BOUND) continue;
            if (zxvfs_tri_open(&rfs, TRIADS[t], 0) != 0) {
                printf("  [VIOLATION] seed 0x%08X: %s reports BOUND but will not open\n", seed,
                       TRIADS[t]);
                violations++;
            } else
                recovered_triads++;
        }
    }

    printf("  crashes injected: %u / %u iterations\n", crashes, iters);
    printf("  triads that survived a crash intact: %u\n", recovered_triads);
    if (bad_seed) {
        printf("  first inconsistent seed 0x%08X: double=%d unmarked=%d leaked=%d bad_inode=%d\n",
               bad_seed, worst.dbl, worst.unmarked, worst.leaked, worst.bad_inode);
        if (lag_seen > 0)
            printf("  FUSE LENGTH: surfaced %d operation(s) AFTER recovery, not at recovery\n",
                   lag_seen);
        else
            printf("  (surfaced immediately at recovery)\n");
    }

    if (violations == 0) {
        printf("  [PASS] no double-allocation, no unmarked sectors, no leaks\n");
        printf("  [PASS] every triad reporting BOUND opened completely\n");
        printf("  [PASS] still consistent after %u further operations post-recovery\n", POST_OPS);
        printf("\nALL PASS zxvfs_fuzz: 0 failure(s)\n");
        return 0;
    }
    printf("  [FAIL] %u inconsistent recoveries\n", violations);
    printf("\nFAILED zxvfs_fuzz: %u failure(s)\n", violations);
    return 1;
}
