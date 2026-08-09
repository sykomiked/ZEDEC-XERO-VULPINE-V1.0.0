/* test_zxvfs_tri.c — host tests for Tri-Space native storage.
 *
 * The headline test is EXHAUSTIVE CRASH INJECTION: for every possible write
 * index during a triad write, cut power there, remount, and assert the
 * invariant that justifies this whole layer —
 *
 *     you never observe a BOUND triad whose S- (or S0) did not land.
 *
 * Everything else is a negative test: each of the five hard requirements must
 * be REFUSED, and refusal must leave the filesystem untouched.
 *
 * Build/run:
 *   gcc -std=c11 -Wall -Wextra -Iinclude -Isrc/zxvfs \
 *       src/zxvfs/test_zxvfs_tri.c src/zxvfs/zxvfs_tri.c src/zxvfs/zxvfs.c \
 *       src/trispace/trispace.c src/robin_debanks/sha256.c \
 *       -o /tmp/test_zxvfs_tri && /tmp/test_zxvfs_tri
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 */
#include <stdio.h>
#include <string.h>
#include "zxvfs_tri.h"

#define DISK_SECTORS ZXVFS_TOTAL_SECTORS
static uint8_t g_disk[DISK_SECTORS][BLOCKDEV_SECTOR_SIZE];

static long g_crash_after = -1;
static long g_writes = 0;
static int  g_crashed = 0;

static int mem_read(block_device_t *dev, uint32_t lba, uint8_t *buf) {
    (void)dev;
    if (lba >= DISK_SECTORS) return -1;
    memcpy(buf, g_disk[lba], BLOCKDEV_SECTOR_SIZE);
    return 0;
}
static int mem_write(block_device_t *dev, uint32_t lba, const uint8_t *buf) {
    (void)dev;
    if (lba >= DISK_SECTORS) return -1;
    if (g_crashed) return -1;
    if (g_crash_after >= 0 && g_writes >= g_crash_after) { g_crashed = 1; return -1; }
    g_writes++;
    memcpy(g_disk[lba], buf, BLOCKDEV_SECTOR_SIZE);
    return 0;
}
static void dev_init(block_device_t *dev) {
    memset(dev, 0, sizeof(*dev));
    dev->present = true;
    dev->total_sectors = DISK_SECTORS;
    dev->read_sector = mem_read;
    dev->write_sector = mem_write;
}

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("  [FAIL] %s\n", msg); failures++; } \
    else         { printf("  [PASS] %s\n", msg); } } while (0)

/* ---- a well-formed triad fixture ---- */
static const uint8_t S_PLUS[]  = "S+ : transfer(from,to,amount) -- what it DOES";
static const uint8_t S_MINUS[] = "S- : reverse(to,from,amount) -- the undo path";
static const uint8_t S_ZERO[]  = "S0 : unresolved -- rounding policy undecided";

static void spec_default(zxvfs_tri_spec_t *s) {
    memset(s, 0, sizeof(*s));
    s->data[TRI_POSITIVE] = S_PLUS;  s->len[TRI_POSITIVE] = (uint32_t)sizeof(S_PLUS);
    s->data[TRI_NEGATIVE] = S_MINUS; s->len[TRI_NEGATIVE] = (uint32_t)sizeof(S_MINUS);
    s->data[TRI_NEUTRAL]  = S_ZERO;  s->len[TRI_NEUTRAL]  = (uint32_t)sizeof(S_ZERO);
    s->capability_set[TRI_POSITIVE] = 0x7;   /* S+ may do 3 things            */
    s->capability_set[TRI_NEGATIVE] = 0x3;   /* S- a subset of them           */
    s->capability_set[TRI_NEUTRAL]  = 0x0;   /* S0 may do nothing             */
    s->inverse_kind = TRI_INV_EXACT;
    s->effect_is_irreversible = false;
    for (uint32_t i = 0; i < TRI_ID_LEN; i++) s->triad_id[i] = (uint8_t)(i + 1);
    for (uint32_t i = 0; i < TRI_DIGEST_LEN; i++) s->source_graph_digest[i] = (uint8_t)(0xA0 + i);
}

static int fresh_fs(zxvfs_t *fs, block_device_t *dev) {
    memset(g_disk, 0, sizeof(g_disk));
    g_crash_after = -1; g_writes = 0; g_crashed = 0;
    if (zxvfs_format(dev) != 0) return -1;
    return zxvfs_mount(fs, dev);
}

int main(void) {
    block_device_t dev; zxvfs_t fs; zxvfs_tri_spec_t spec;
    static uint8_t buf[ZXVFS_FILE_MAX_BYTES];
    dev_init(&dev);

    printf("Tri-Space native storage (zxvfs_tri)\n");

    /* ---------------- happy path ---------------- */
    printf("bind + read:\n");
    CHECK(fresh_fs(&fs, &dev) == 0, "format + mount");
    spec_default(&spec);
    CHECK(zxvfs_tri_write(&fs, "ledger", &spec) == 0, "well-formed triad stores");
    CHECK(zxvfs_tri_state(&fs, "ledger") == ZXVFS_TRI_BOUND, "state is BOUND");
    CHECK(zxvfs_tri_open(&fs, "ledger", 0) == 0, "opens and fully verifies");
    int n = zxvfs_tri_read_role(&fs, "ledger", TRI_NEGATIVE, buf, sizeof(buf));
    CHECK(n == (int)sizeof(S_MINUS) && memcmp(buf, S_MINUS, sizeof(S_MINUS)) == 0,
          "S- payload reads back byte-exact");

    /* survives a remount */
    zxvfs_t fs2;
    CHECK(zxvfs_mount(&fs2, &dev) == 0 && zxvfs_tri_open(&fs2, "ledger", 0) == 0,
          "triad survives remount");

    /* ---------------- the five requirements ---------------- */
    printf("the five hard requirements (all must be REFUSED):\n");
    struct { const char *what; tri_quarantine_t why; void (*mut)(zxvfs_tri_spec_t *); } cases[] = {
        { "1. missing member refused", TRI_Q_MISSING_MEMBER, 0 },
        { "2. S- over-capable refused", TRI_Q_NEG_OVER_CAPABLE, 0 },
        { "3. S0 with production capability refused", TRI_Q_NEUTRAL_HAS_EFFECT, 0 },
        { "4. irreversible claiming exact inverse refused", TRI_Q_BAD_INVERSE_CLAIM, 0 },
        { "5. generated S- claiming proven inverse refused", TRI_Q_UNPROVEN_INVERSE, 0 },
    };
    for (int c = 0; c < 5; c++) {
        CHECK(fresh_fs(&fs, &dev) == 0, "  (reset)");
        spec_default(&spec);
        switch (c) {
            case 0: spec.data[TRI_NEGATIVE] = 0; break;
            case 1: spec.capability_set[TRI_NEGATIVE] = 0xF; break;   /* > S+ 0x7 */
            case 2: spec.capability_set[TRI_NEUTRAL]  = 0x1; break;
            case 3: spec.effect_is_irreversible = true;
                    spec.inverse_kind = TRI_INV_EXACT; break;
            case 4: spec.generated[TRI_NEGATIVE] = true;
                    spec.claims_proven_inverse[TRI_NEGATIVE] = true; break;
        }
        int rc = zxvfs_tri_write(&fs, "bad", &spec);
        int refused = ZXVFS_TRI_IS_RULE_ERR(rc) && ZXVFS_TRI_REASON(rc) == cases[c].why;
        CHECK(refused, cases[c].what);
        /* refusal must not leave a trail */
        CHECK(zxvfs_tri_state(&fs, "bad") == ZXVFS_TRI_ABSENT,
              "  refusal left the filesystem untouched");
    }

    /* ---------------- tamper detection + durable quarantine ---------------- */
    printf("tamper detection:\n");
    CHECK(fresh_fs(&fs, &dev) == 0, "  (reset)");
    spec_default(&spec);
    CHECK(zxvfs_tri_write(&fs, "ledger", &spec) == 0, "  triad stored");
    /* edit the S- payload underneath the descriptor, as an attacker would */
    static uint8_t evil[] = "S- : reverse(to,from,amount) -- BACKDOORED!!!!";
    CHECK(zxvfs_write(&fs, "ledger.cedez", evil, (uint32_t)sizeof(evil)) == 0,
          "  S- payload altered beneath the triad");
    int rc = zxvfs_tri_open(&fs, "ledger", 0);
    CHECK(ZXVFS_TRI_IS_RULE_ERR(rc) && ZXVFS_TRI_REASON(rc) == TRI_Q_SEAL_MISMATCH,
          "altered S- is detected on open");
    CHECK(zxvfs_tri_state(&fs, "ledger") == ZXVFS_TRI_QUARANTINED,
          "triad is QUARANTINED on disk");
    CHECK(zxvfs_mount(&fs2, &dev) == 0 &&
          zxvfs_tri_state(&fs2, "ledger") == ZXVFS_TRI_QUARANTINED,
          "quarantine survives reboot (verdict is durable)");
    CHECK(zxvfs_tri_read_role(&fs2, "ledger", TRI_POSITIVE, buf, sizeof(buf)) < 0,
          "quarantined triad yields NO payload (S+ is gated too)");

    /* ---------------- exhaustive crash injection ---------------- */
    printf("exhaustive crash injection (the core invariant):\n");
    /* how many device writes does a full triad write take? */
    CHECK(fresh_fs(&fs, &dev) == 0, "  (reset)");
    spec_default(&spec);
    long base = g_writes;
    (void)zxvfs_tri_write(&fs, "ledger", &spec);
    long total = g_writes - base;
    printf("  (a triad write performs %ld device writes; testing a crash at each)\n", total);

    int violations = 0, bound_after = 0, absent_after = 0;
    for (long cut = 0; cut <= total; cut++) {
        /* rebuild a clean FS, then crash at write index `cut` */
        memset(g_disk, 0, sizeof(g_disk));
        g_crash_after = -1; g_writes = 0; g_crashed = 0;
        if (zxvfs_format(&dev) != 0 || zxvfs_mount(&fs, &dev) != 0) { violations++; break; }
        long start = g_writes;
        g_crash_after = start + cut;
        spec_default(&spec);
        (void)zxvfs_tri_write(&fs, "ledger", &spec);   /* may be cut short */

        /* "reboot": stop crashing, remount over the same platter */
        g_crashed = 0; g_crash_after = -1;
        zxvfs_t rfs;
        if (zxvfs_mount(&rfs, &dev) != 0) { violations++; continue; }

        zxvfs_tri_state_t st = zxvfs_tri_state(&rfs, "ledger");
        if (st == ZXVFS_TRI_BOUND) {
            /* claims bound => must verify completely, including every role */
            if (zxvfs_tri_open(&rfs, "ledger", 0) != 0) { violations++; continue; }
            int a = zxvfs_tri_read_role(&rfs, "ledger", TRI_POSITIVE, buf, sizeof(buf));
            int b = zxvfs_tri_read_role(&rfs, "ledger", TRI_NEGATIVE, buf, sizeof(buf));
            int c = zxvfs_tri_read_role(&rfs, "ledger", TRI_NEUTRAL,  buf, sizeof(buf));
            if (a <= 0 || b <= 0 || c <= 0) { violations++; continue; }
            bound_after++;
        } else {
            /* not bound => nothing may be readable through the triad API */
            if (zxvfs_tri_read_role(&rfs, "ledger", TRI_POSITIVE, buf, sizeof(buf)) > 0)
                { violations++; continue; }
            absent_after++;
        }
    }
    printf("  (%d cut points left no triad, %d left a complete one)\n",
           absent_after, bound_after);
    CHECK(violations == 0,
          "NO crash point ever exposes a bound S+ without its bound S-/S0");
    CHECK(bound_after > 0 && absent_after > 0,
          "both outcomes actually occur (the test is not vacuous)");

    printf("\n%s zxvfs_tri: %d failure(s)\n",
           failures ? "FAILED" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
