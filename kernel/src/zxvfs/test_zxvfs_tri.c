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
#include "../zab/zab.h"
#include "../invproof/invproof.h"

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

/* ---- a well-formed triad fixture, built from REAL ZAB programs ----
 * Capabilities are now DERIVED from these instruction streams, so the fixture
 * has to be actual bytecode: plain text would derive the empty set and make
 * every capability case vacuous. */
static uint8_t P_PLUS[128], P_MINUS[128], P_ZERO[128];
static uint32_t L_PLUS, L_MINUS, L_ZERO;

static uint32_t mkprog(uint8_t *out, const uint8_t *ops, uint16_t n) {
    zab_ins_t ins[32]; zab_header_t h;
    for (uint16_t i = 0; i < n; i++) { ins[i].op = ops[i]; ins[i].a = 0; ins[i].b = 0; }
    h.magic = ZAB_MAGIC; h.version = ZAB_VERSION; h.count = n; h.seal = 0;
    uint32_t seal = zab_compute_seal(&h, ins, n), o = 0;
    out[o++] = (uint8_t)(ZAB_MAGIC);       out[o++] = (uint8_t)(ZAB_MAGIC >> 8);
    out[o++] = (uint8_t)(ZAB_MAGIC >> 16); out[o++] = (uint8_t)(ZAB_MAGIC >> 24);
    out[o++] = (uint8_t)(ZAB_VERSION);     out[o++] = (uint8_t)(ZAB_VERSION >> 8);
    out[o++] = (uint8_t)(n);               out[o++] = (uint8_t)(n >> 8);
    out[o++] = (uint8_t)(seal);            out[o++] = (uint8_t)(seal >> 8);
    out[o++] = (uint8_t)(seal >> 16);      out[o++] = (uint8_t)(seal >> 24);
    for (uint16_t i = 0; i < n; i++) {
        out[o++] = ins[i].op; out[o++] = ins[i].a;
        out[o++] = (uint8_t)(ins[i].b); out[o++] = (uint8_t)(ins[i].b >> 8);
    }
    return o;
}

static void build_fixture(void) {
    /* S+ posts to the ledger and mutates state; S- posts the reversing entry
     * (a strict subset); S0 is INERT. trispace.c requires S0's capability set
     * to be exactly zero -- stricter than "no production effect" -- so the
     * unresolved remainder may not even observe until a policy resolves it. */
    const uint8_t plus[]  = { ZAB_OP_READ, ZAB_OP_POST, ZAB_OP_WRITE, ZAB_OP_END };
    const uint8_t minus[] = { ZAB_OP_POST, ZAB_OP_END };
    const uint8_t zero[]  = { ZAB_OP_NOP, ZAB_OP_END };
    L_PLUS  = mkprog(P_PLUS,  plus,  4);
    L_MINUS = mkprog(P_MINUS, minus, 2);
    L_ZERO  = mkprog(P_ZERO,  zero,  2);
}

static void spec_default(zxvfs_tri_spec_t *s) {
    memset(s, 0, sizeof(*s));
    s->data[TRI_POSITIVE] = P_PLUS;  s->len[TRI_POSITIVE] = L_PLUS;
    s->data[TRI_NEGATIVE] = P_MINUS; s->len[TRI_NEGATIVE] = L_MINUS;
    s->data[TRI_NEUTRAL]  = P_ZERO;  s->len[TRI_NEUTRAL]  = L_ZERO;
    /* declared == what the bytecode actually carries */
    s->capability_set[TRI_POSITIVE] = ZAB_CAP_READ_STATE|ZAB_CAP_LEDGER|ZAB_CAP_WRITE_STATE;
    s->capability_set[TRI_NEGATIVE] = ZAB_CAP_LEDGER;
    s->capability_set[TRI_NEUTRAL]  = ZAB_CAP_NONE;
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
    build_fixture();

    printf("Tri-Space native storage (zxvfs_tri)\n");

    /* ---------------- happy path ---------------- */
    printf("bind + read:\n");
    CHECK(fresh_fs(&fs, &dev) == 0, "format + mount");
    spec_default(&spec);
    CHECK(zxvfs_tri_write(&fs, "ledger", &spec) == 0, "well-formed triad stores");
    CHECK(zxvfs_tri_state(&fs, "ledger") == ZXVFS_TRI_BOUND, "state is BOUND");
    CHECK(zxvfs_tri_open(&fs, "ledger", 0) == 0, "opens and fully verifies");
    int n = zxvfs_tri_read_role(&fs, "ledger", TRI_NEGATIVE, buf, sizeof(buf));
    CHECK(n == (int)L_MINUS && memcmp(buf, P_MINUS, L_MINUS) == 0,
          "S- payload reads back byte-exact");

    /* survives a remount */
    zxvfs_t fs2;
    CHECK(zxvfs_mount(&fs2, &dev) == 0 && zxvfs_tri_open(&fs2, "ledger", 0) == 0,
          "triad survives remount");

    /* ---------------- the five requirements ---------------- */
    printf("the five hard requirements (all must be REFUSED):\n");
    /* Cases 2 and 3 are now expressed as BYTECODE, not as declared numbers:
     * the S- actually contains SEND/SPAWN its S+ never had, and the S0 actually
     * contains a ledger POST. Declarations are set to match, so these test the
     * real rules rather than the (now-derived) declaration. Case 6 is the lie. */
    static uint8_t P_GREEDY[128], P_ACTIVE0[128]; static uint32_t L_GREEDY, L_ACTIVE0;
    { const uint8_t greedy[]  = { ZAB_OP_POST, ZAB_OP_SEND, ZAB_OP_SPAWN, ZAB_OP_END };
      const uint8_t active0[] = { ZAB_OP_POST, ZAB_OP_END };
      L_GREEDY  = mkprog(P_GREEDY,  greedy,  4);
      L_ACTIVE0 = mkprog(P_ACTIVE0, active0, 2); }

    struct { const char *what; tri_quarantine_t why; } cases[] = {
        { "1. missing member refused", TRI_Q_MISSING_MEMBER },
        { "2. S- carrying capabilities S+ lacks refused", TRI_Q_NEG_OVER_CAPABLE },
        { "3. S0 containing a production effect refused", TRI_Q_NEUTRAL_HAS_EFFECT },
        { "4. irreversible claiming exact inverse refused", TRI_Q_BAD_INVERSE_CLAIM },
        { "5. generated S- claiming proven inverse refused", TRI_Q_UNPROVEN_INVERSE },
        { "6. S- that LIES about its capabilities refused", TRI_Q_CAP_MISDECLARED },
    };
    for (int c = 0; c < 6; c++) {
        CHECK(fresh_fs(&fs, &dev) == 0, "  (reset)");
        spec_default(&spec);
        switch (c) {
            case 0: spec.data[TRI_NEGATIVE] = 0; break;
            case 1: spec.data[TRI_NEGATIVE] = P_GREEDY; spec.len[TRI_NEGATIVE] = L_GREEDY;
                    spec.capability_set[TRI_NEGATIVE] =
                        ZAB_CAP_LEDGER|ZAB_CAP_NET|ZAB_CAP_SPAWN; break;
            case 2: spec.data[TRI_NEUTRAL] = P_ACTIVE0; spec.len[TRI_NEUTRAL] = L_ACTIVE0;
                    spec.capability_set[TRI_NEUTRAL] = ZAB_CAP_LEDGER; break;
            case 3: spec.effect_is_irreversible = true;
                    spec.inverse_kind = TRI_INV_EXACT; break;
            case 4: spec.generated[TRI_NEGATIVE] = true;
                    spec.claims_proven_inverse[TRI_NEGATIVE] = true; break;
            case 5: /* the artifact posts to the ledger but declares nothing */
                    spec.data[TRI_NEGATIVE] = P_GREEDY; spec.len[TRI_NEGATIVE] = L_GREEDY;
                    spec.capability_set[TRI_NEGATIVE] = ZAB_CAP_NONE; break;
        }
        int rc = zxvfs_tri_write(&fs, "bad", &spec);
        int refused = ZXVFS_TRI_IS_RULE_ERR(rc) && ZXVFS_TRI_REASON(rc) == cases[c].why;
        CHECK(refused, cases[c].what);
        /* refusal must not leave a trail */
        CHECK(zxvfs_tri_state(&fs, "bad") == ZXVFS_TRI_ABSENT,
              "  refusal left the filesystem untouched");
    }

    /* ---------------- tamper detection + durable quarantine ---------------- */
    /* ---------------- proven inverse must carry a real proof ---------------- */
    printf("proven inverse (a claim now costs a proof):\n");
    {   static uint8_t st_before[256], st_after[256], witness[1024];
        for (uint32_t i = 0; i < 256; i++) { st_before[i] = (uint8_t)(i*5+1); st_after[i] = st_before[i]; }
        st_after[9] = 0x77; st_after[200] = 0x33;
        int wl = zxi_build(st_before, st_after, 256, witness, sizeof(witness));

        /* (a) claiming a proven inverse with NO witness at all */
        CHECK(fresh_fs(&fs, &dev) == 0, "  (reset)");
        spec_default(&spec);
        spec.claims_proven_inverse[TRI_NEGATIVE] = true;
        spec.inverse_kind = TRI_INV_EXACT;
        int r1 = zxvfs_tri_write(&fs, "prov", &spec);
        CHECK(ZXVFS_TRI_IS_RULE_ERR(r1) && ZXVFS_TRI_REASON(r1) == TRI_Q_UNPROVEN_INVERSE,
              "claiming a proven inverse with no witness is refused");

        /* (b) a REAL witness as the S- payload: the claim is honoured */
        CHECK(fresh_fs(&fs, &dev) == 0, "  (reset)");
        spec_default(&spec);
        spec.data[TRI_NEGATIVE] = witness; spec.len[TRI_NEGATIVE] = (uint32_t)wl;
        spec.capability_set[TRI_NEGATIVE] = ZAB_CAP_NONE;  /* a witness is data */
        spec.claims_proven_inverse[TRI_NEGATIVE] = true;
        spec.inverse_kind = TRI_INV_EXACT;
        spec.proof_state = st_after; spec.proof_state_len = 256;
        CHECK(zxvfs_tri_write(&fs, "prov", &spec) == 0,
              "a witness that VERIFIES is accepted as a proven inverse");

        /* (c) the undo that does not undo — well-formed, wrong result */
        CHECK(fresh_fs(&fs, &dev) == 0, "  (reset)");
        static uint8_t bad_wit[1024];
        memcpy(bad_wit, witness, (uint32_t)wl);
        bad_wit[ZXI_HDR_BYTES + 6] ^= 0x01;      /* corrupt one delta byte */
        { uint32_t sm = 2166136261u;             /* reseal so it stays well-formed */
          for (int i = 0; i < wl; i++) { if (i >= 84 && i < 88) continue;
              sm ^= bad_wit[i]; sm *= 16777619u; }
          if (!sm) sm = 1u;
          bad_wit[84]=(uint8_t)sm; bad_wit[85]=(uint8_t)(sm>>8);
          bad_wit[86]=(uint8_t)(sm>>16); bad_wit[87]=(uint8_t)(sm>>24); }
        spec_default(&spec);
        spec.data[TRI_NEGATIVE] = bad_wit; spec.len[TRI_NEGATIVE] = (uint32_t)wl;
        spec.capability_set[TRI_NEGATIVE] = ZAB_CAP_NONE;
        spec.claims_proven_inverse[TRI_NEGATIVE] = true;
        spec.inverse_kind = TRI_INV_EXACT;
        spec.proof_state = st_after; spec.proof_state_len = 256;
        int r3 = zxvfs_tri_write(&fs, "prov", &spec);
        CHECK(ZXVFS_TRI_IS_RULE_ERR(r3) && ZXVFS_TRI_REASON(r3) == TRI_Q_UNPROVEN_INVERSE,
              "an undo that does NOT restore the prior state is refused");
    }

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
