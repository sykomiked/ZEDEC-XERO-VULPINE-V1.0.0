/* test_fab.c — behaviour tests for file fabrication (fab.h).
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 *
 * Every rule in fab.h has a test that passes the rule at its edge and one
 * that breaks it by one, and each failure checks the exact error, so a
 * mutated comparison or a skipped check shows up as a failed line.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fab.h"
#include "fab_int.h"
#include "../robin_debanks/sha256.h"
#include "../pqsec/pq_matrix_algs.h"

static int fails, checks;

/* pinned SHA-256 of outputs for fixed inputs (test_golden) */
#define GOLD_RECIPE     "52ad42a93103421deaf9b93acf04ea804872b39615f547188d47ba3575e78768"
#define GOLD_STREAM_KEY "f0a1ad1b3353158470c329238b4b01716ccca6d3cf9a5fbb2d8772e3521cbe82"
#define GOLD_FRAME      "79ef2568e6914af360e9db31d975634bb93290bdb17d90cd0c4063d70009f480"
#define GOLD_CP         "3a9fe1df599434416df16ac01900596d2529c1e494ce7ccdeb58f0a38ccaef0e"
#define CHECK(c)                                                                                   \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            fails++;                                                                               \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                                    \
        }                                                                                          \
    } while (0)
#define EQ(a, b)                                                                                   \
    do {                                                                                           \
        long long _a = (long long) (a), _b = (long long) (b);                                      \
        checks++;                                                                                  \
        if (_a != _b) {                                                                            \
            fails++;                                                                               \
            printf("FAIL %s:%d: %s = %lld, want %lld\n", __FILE__, __LINE__, #a, _a, _b);          \
        }                                                                                          \
    } while (0)

/* ---- deterministic bytes -------------------------------------------------- */

static uint64_t rng = 0x9E3779B97F4A7C15ull;
static uint8_t rb(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return (uint8_t) (rng >> 24);
}
static void fill(uint8_t *p, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) p[i] = rb();
}
static void seed32(uint8_t s[32], uint8_t v)
{
    for (int i = 0; i < 32; i++) s[i] = (uint8_t) (v + i);
}

/* ---- keys ----------------------------------------------------------------- */

static uint8_t spk[FAB_SIG_PK], ssk[FAB_SIG_SK]; /* sender */
static uint8_t opk[FAB_SIG_PK], osk[FAB_SIG_SK]; /* someone else */
static uint8_t rek[FAB_KEM_EK], rdk[FAB_KEM_DK]; /* recipient */
static uint8_t xek[FAB_KEM_EK], xdk[FAB_KEM_DK]; /* another recipient */

static void keys(void)
{
    uint8_t s[32], z[32];
    seed32(s, 1);
    fab_sig_keypair(s, spk, ssk);
    seed32(s, 2);
    fab_sig_keypair(s, opk, osk);
    seed32(s, 3);
    seed32(z, 4);
    fab_kem_keypair(s, z, rek, rdk);
    seed32(s, 5);
    seed32(z, 6);
    fab_kem_keypair(s, z, xek, xdk);
}

/* ---- a block store the tests can corrupt ----------------------------------- */

#define NB 4
#define BL 70000u
static uint8_t blk[NB][BL];
static uint32_t blen[NB] = {BL, 4096, 300, 65536};
static ipfsn_cid_t bcid[NB];
static fab_src_t srcs[NB + 1];

typedef struct {
    int corrupt; /* index of a block to flip one byte of, or -1 */
    int shorten; /* index of a block to return one byte short, or -1 */
    int missing; /* index of a block the store does not have, or -1 */
    int fetches;
} store_t;
static store_t st;

static int get(void *ctx, const ipfsn_cid_t *cid, uint8_t *buf, uint32_t cap, uint32_t *len)
{
    store_t *s = (store_t *) ctx;
    s->fetches++;
    for (int i = 0; i < NB; i++) {
        if (!ipfsn_cid_equal(cid, &bcid[i])) continue;
        if (i == s->missing) return -1;
        uint32_t n = blen[i] - (i == s->shorten ? 1u : 0u);
        if (n > cap) return -1;
        memcpy(buf, blk[i], n);
        if (i == s->corrupt) buf[n / 2] ^= 1;
        *len = n;
        return 0;
    }
    return -1;
}

static void store_reset(void)
{
    st.corrupt = st.shorten = st.missing = -1;
    st.fetches = 0;
}

static void blocks(void)
{
    for (int i = 0; i < NB; i++) {
        fill(blk[i], blen[i]);
        CHECK(ipfsn_cid_sha256(IPFSN_MC_RAW, blk[i], blen[i], &bcid[i]) == IPFSN_OK);
        srcs[i].data = blk[i];
        srcs[i].len = blen[i];
        srcs[i].cid = bcid[i];
        srcs[i].prev = false;
    }
}

/* ---- seal state ------------------------------------------------------------ */

static lpres_state_t seal_state;
static int seal_calls;
static lpres_state_t seal(void *ctx)
{
    (void) ctx;
    seal_calls++;
    return seal_state;
}

static fab_work_t W;
#define OUTCAP (1u << 20)
static uint8_t out[OUTCAP], diffb[OUTCAP], planb[1u << 20], rec[1u << 21], rec2[1u << 21];
static uint8_t opsb[FAB_OP_REC * 4096], target[OUTCAP], tdiff[OUTCAP];
static fab_ient_t table[1u << 14];

static fab_io_t io(void)
{
    fab_io_t x = {get, &st, 0, 0, 0, 0, diffb, OUTCAP, out, OUTCAP, &W};
    return x;
}

/* Target: slices of blocks 0 and 1 with glue, then a run of block 3. */
static uint32_t make_target(void)
{
    uint32_t n = 0;
    memcpy(target + n, blk[0] + 1000, 20000);
    n += 20000;
    memcpy(target + n, "glue-glue!", 10);
    n += 10;
    memcpy(target + n, blk[1] + 7, 3000);
    n += 3000;
    memcpy(target + n, blk[3], 65536);
    n += 65536;
    memcpy(target + n, blk[0] + 50000, 20000);
    n += 20000;
    return n;
}

/* Build a plan + diff for target[0..tlen) over sources 0..nsrc-1. */
static uint32_t plan_for(uint32_t tlen, uint32_t nsrc, uint32_t slot_us, uint32_t *dlen)
{
    uint32_t no = 0, pl = 0;
    ipfsn_cid_t tc;
    EQ(fab_build(srcs, nsrc, target, tlen, 32, table, 14, opsb, 4096, &no, tdiff, OUTCAP, dlen),
       FAB_OK);
    EQ(fab_target_cid(target, tlen, &W, &tc), FAB_OK);
    EQ(fab_plan_write(&tc, tlen, slot_us, srcs, nsrc, opsb, no, planb, sizeof planb, &pl), FAB_OK);
    return pl;
}

static uint32_t coin_seed = 77; /* another value gives another stream id */
static fab_seal_args_t args(uint8_t kind, uint8_t mode)
{
    static uint8_t coins[32], rnd[32];
    seed32(coins, coin_seed);
    seed32(rnd, 99);
    fab_seal_args_t a = {kind, mode, spk, ssk, mode ? rek : 0, mode ? coins : 0, rnd};
    return a;
}

/* Re-sign a recipe after the test edits it, so the edit reaches the later
 * checks instead of stopping at the signature. */
static void resign(uint8_t *buf, uint32_t len)
{
    uint32_t o = len - FAB_SIG - FAB_SIG_PK;
    uint8_t h[32];
    sha256(buf, o, h);
    pqm_mldsa87_sign(ssk, h, 32, (const uint8_t *) FAB_RECIPE_CTX, sizeof FAB_RECIPE_CTX - 1u, 0,
                     buf + o + FAB_SIG_PK);
}

/* Parse, check, open, hydrate, assemble. Returns the first error. */
static int run(const uint8_t *buf, uint32_t len, const uint8_t *dk, fab_io_t *x, uint32_t *olen,
               fab_stream_t *s)
{
    fab_recipe_t r;
    fab_plan_t p;
    int rc = fab_recipe_parse(buf, len, &r);
    if (rc) return rc;
    if ((rc = fab_recipe_check(&r, 0, 0))) return rc;
    if ((rc = fab_open_plan(&r, dk, planb, sizeof planb, &p))) return rc;
    if ((rc = fab_hydrate(&p, x, 0))) return rc;
    return fab_assemble(&r, &p, dk, x, s, olen);
}

static bool all_zero(const uint8_t *p, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
        if (p[i]) return false;
    return true;
}

/* ---- tests ----------------------------------------------------------------- */

static void test_target_cid(void)
{
    ipfsn_cid_t c;
    char s[IPFSN_CID_STR_MAX];
    EQ(fab_target_cid((const uint8_t *) "", 0, &W, &c), FAB_OK);
    CHECK(ipfsn_cid_to_string(&c, s, sizeof s) > 0);
    /* `ipfs add --cid-version 1` of the empty file */
    CHECK(strcmp(s, "bafkreihdwdcefgh4dqkjv67uzcmw7ojee6xedzdetojuzjevtenxquvyku") == 0);
    /* one chunk: a raw block; one byte more: dag-pb root */
    EQ(fab_target_cid(blk[0], IPFSN_UFS_CHUNK, &W, &c), FAB_OK);
    EQ(c.codec, IPFSN_MC_RAW);
    memcpy(target, blk[0], BL);
    memcpy(target + BL, blk[0], BL);
    memcpy(target + 2 * BL, blk[0], BL);
    memcpy(target + 3 * BL, blk[0], BL);
    EQ(fab_target_cid(target, IPFSN_UFS_CHUNK + 1u, &W, &c), FAB_OK);
    EQ(c.codec, IPFSN_MC_DAG_PB);
    EQ(fab_target_cid(0, 1, &W, &c), FAB_ERR_ARG);
    EQ(fab_target_cid(target, 1, 0, &c), FAB_ERR_ARG);
}

static void test_build(void)
{
    uint32_t no = 0, dl = 0;
    /* whole block -> one op, empty diff */
    memcpy(target, blk[3], 65536);
    EQ(fab_build(srcs, NB, target, 65536, 32, table, 14, opsb, 4096, &no, tdiff, OUTCAP, &dl),
       FAB_OK);
    EQ(no, 1);
    EQ(dl, 0);
    EQ(opsb[0], FAB_OP_SLICE);
    EQ(opsb[3], 3);
    EQ((opsb[8] << 24) | (opsb[9] << 16) | (opsb[10] << 8) | opsb[11], 65536);

    uint32_t tl = make_target();
    EQ(fab_build(srcs, NB, target, tl, 32, table, 14, opsb, 4096, &no, tdiff, OUTCAP, &dl), FAB_OK);
    CHECK(dl >= 8 && dl <= 10); /* the glue; a glue byte may happen to extend a match */
    CHECK(no >= 5 && no <= 7);
    /* the ops rebuild the target exactly */
    uint32_t pos = 0, dpos = 0;
    for (uint32_t i = 0; i < no; i++) {
        const uint8_t *op = opsb + i * FAB_OP_REC;
        uint32_t b = (uint32_t) ((op[2] << 8) | op[3]);
        uint32_t s = (uint32_t) ((op[4] << 24) | (op[5] << 16) | (op[6] << 8) | op[7]);
        uint32_t l = (uint32_t) ((op[8] << 24) | (op[9] << 16) | (op[10] << 8) | op[11]);
        if (op[0] == FAB_OP_SLICE) {
            CHECK(memcmp(target + pos, blk[b] + s, l) == 0);
        } else {
            CHECK(op[0] == FAB_OP_LIT);
            CHECK(memcmp(target + pos, tdiff + dpos, l) == 0);
            dpos += l;
        }
        pos += l;
    }
    EQ(pos, tl);
    EQ(dpos, dl);

    /* nothing to match: all diff, one op */
    fill(target, 5000);
    EQ(fab_build(srcs, NB, target, 5000, 32, table, 14, opsb, 4096, &no, tdiff, OUTCAP, &dl),
       FAB_OK);
    EQ(no, 1);
    EQ(dl, 5000);
    /* shorter than a window */
    EQ(fab_build(srcs, NB, target, 31, 32, table, 14, opsb, 4096, &no, tdiff, OUTCAP, &dl), FAB_OK);
    EQ(dl, 31);
    EQ(fab_build(srcs, NB, target, 0, 32, table, 14, opsb, 4096, &no, tdiff, OUTCAP, &dl), FAB_OK);
    EQ(no, 0);

    /* argument and capacity rules */
    tl = make_target();
    EQ(fab_build(srcs, NB, target, tl, 7, table, 14, opsb, 4096, &no, tdiff, OUTCAP, &dl),
       FAB_ERR_ARG);
    EQ(fab_build(srcs, NB, target, tl, 8, table, 14, opsb, 4096, &no, tdiff, OUTCAP, &dl), FAB_OK);
    EQ(fab_build(srcs, NB, target, tl, 32, table, 3, opsb, 4096, &no, tdiff, OUTCAP, &dl),
       FAB_ERR_ARG);
    EQ(fab_build(srcs, NB, target, tl, 32, table, 25, opsb, 4096, &no, tdiff, OUTCAP, &dl),
       FAB_ERR_ARG);
    EQ(fab_build(srcs, NB, target, tl, 32, table, 14, opsb, 2, &no, tdiff, OUTCAP, &dl),
       FAB_ERR_SPACE);
    EQ(fab_build(srcs, NB, target, tl, 32, table, 14, opsb, 4096, &no, tdiff, 5, &dl),
       FAB_ERR_SPACE);
    fab_src_t two[2] = {srcs[0], srcs[1]};
    two[0].prev = true;
    EQ(fab_build(two, 2, target, tl, 32, table, 14, opsb, 4096, &no, tdiff, OUTCAP, &dl),
       FAB_ERR_ARG);
    two[0].prev = false;
    two[1].len = 0;
    EQ(fab_build(two, 2, target, tl, 32, table, 14, opsb, 4096, &no, tdiff, OUTCAP, &dl),
       FAB_ERR_ARG);
    two[1].len = FAB_BLOCK_MAX + 1u;
    EQ(fab_build(two, 2, target, tl, 32, table, 14, opsb, 4096, &no, tdiff, OUTCAP, &dl),
       FAB_ERR_ARG);
}

/* A plan built by hand: target = slice of block 0 + 4 literal bytes. */
static uint8_t hp[FAB_PLAN_HDR + 3 * FAB_BLOCK_REC + 3 * FAB_OP_REC];
static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t) (v >> 24);
    p[1] = (uint8_t) (v >> 16);
    p[2] = (uint8_t) (v >> 8);
    p[3] = (uint8_t) v;
}
static void op(uint8_t *o, uint8_t k, uint16_t b, uint32_t s, uint32_t l)
{
    o[0] = k;
    o[1] = 0;
    o[2] = (uint8_t) (b >> 8);
    o[3] = (uint8_t) b;
    put32(o + 4, s);
    put32(o + 8, l);
}
/* 2 blocks, 2 ops: SLICE(b1, 296, 4) LIT(4)  -> target 8 bytes, diff 4 */
static uint32_t hand_plan(void)
{
    memset(hp, 0, sizeof hp);
    ipfsn_cid_t c;
    memcpy(target, blk[2] + 296, 4);
    memcpy(target + 4, "abcd", 4);
    fab_target_cid(target, 8, &W, &c);
    ipfsn_cid_encode(&c, hp, 36);
    hp[43] = 8;
    put32(hp + 44, 2);
    put32(hp + 48, 2);
    ipfsn_cid_encode(&bcid[0], hp + 64, 36);
    put32(hp + 100, blen[0]);
    ipfsn_cid_encode(&bcid[2], hp + 104, 36);
    put32(hp + 140, blen[2]);
    op(hp + 144, FAB_OP_SLICE, 1, 296, 4);
    op(hp + 156, FAB_OP_LIT, 0, 0, 4);
    return 168;
}

static void test_plan_rules(void)
{
    fab_plan_t p;
    uint32_t n = hand_plan();
    EQ(fab_plan_parse(hp, n, FAB_PLAN_FILE, 4, &p), FAB_OK);
    EQ(p.n_blocks, 2);
    EQ(p.n_ops, 2);
    EQ(p.target_len, 8);
    EQ(p.lit_total, 4);
    EQ(p.prev_need, 0);
    ipfsn_cid_t c;
    uint32_t l;
    EQ(fab_plan_block(&p, 1, &c, &l), FAB_OK);
    CHECK(ipfsn_cid_equal(&c, &bcid[2]));
    EQ(l, 300);
    EQ(fab_plan_block(&p, 2, &c, &l), FAB_ERR_ARG);

    EQ(fab_plan_parse(hp, n - 1, FAB_PLAN_FILE, 4, &p), FAB_ERR_PLAN);
    EQ(fab_plan_parse(hp, 63, FAB_PLAN_FILE, 4, &p), FAB_ERR_PLAN);
    EQ(fab_plan_parse(hp, n, FAB_PLAN_FILE, 3, &p), FAB_ERR_PLAN);
    EQ(fab_plan_parse(hp, n, FAB_PLAN_FILE, 5, &p), FAB_ERR_PLAN);
    EQ(fab_plan_parse(hp, n, FAB_PLAN_OPEN, 4, &p), FAB_ERR_PLAN); /* open needs a period */
    EQ(fab_plan_parse(hp, n, FAB_PLAN_FRAME, 4, &p), FAB_OK);
    EQ(fab_plan_parse(hp, n, 3, 4, &p), FAB_ERR_ARG);

#define MUT(stmt, want)                                                                            \
    do {                                                                                           \
        hand_plan();                                                                               \
        stmt;                                                                                      \
        EQ(fab_plan_parse(hp, n, FAB_PLAN_FILE, 4, &p), want);                                     \
    } while (0)
    MUT(hp[43] = 9, FAB_ERR_PLAN); /* target length != sum */
    MUT(hp[43] = 7, FAB_ERR_PLAN);
    MUT(hp[52 + 3] = 1, FAB_ERR_PLAN); /* period in a file */
    MUT(hp[56] = 1, FAB_ERR_PLAN);     /* reserved */
    MUT(hp[63] = 1, FAB_ERR_PLAN);
    MUT(hp[0] = 0, FAB_ERR_PLAN);          /* target CID not v1 */
    MUT(hp[3] = 0, FAB_ERR_PLAN);          /* multihash not sha2-256 */
    MUT(hp[1] = 0x71, FAB_ERR_PLAN);       /* codec dag-cbor */
    MUT(hp[1] = 0x70, FAB_OK);             /* dag-pb is allowed */
    MUT(put32(hp + 140, 0), FAB_ERR_PLAN); /* block length 0 */
    MUT(put32(hp + 140, FAB_BLOCK_MAX + 1u), FAB_ERR_PLAN);
    MUT(put32(hp + 140, FAB_BLOCK_MAX), FAB_OK);
    MUT(put32(hp + 140, 299), FAB_ERR_PLAN); /* slice now past the end */
    MUT(op(hp + 144, FAB_OP_SLICE, 1, 297, 4), FAB_ERR_PLAN);
    MUT(op(hp + 144, FAB_OP_SLICE, 1, 295, 4), FAB_OK);     /* range rules only; bytes differ */
    MUT(op(hp + 144, FAB_OP_SLICE, 2, 0, 4), FAB_ERR_PLAN); /* no block 2 */
    MUT(op(hp + 144, FAB_OP_SLICE, 0, 0, 4), FAB_OK);
    MUT(op(hp + 144, FAB_OP_SLICE, 1, 0xFFFFFFFFu, 4), FAB_ERR_PLAN); /* overflow */
    MUT(hp[145] = 1, FAB_ERR_PLAN);                                   /* op zero byte */
    MUT(hp[144] = 0, FAB_ERR_PLAN);                                   /* unknown kind */
    MUT(hp[144] = 4, FAB_ERR_PLAN);
    MUT(op(hp + 144, FAB_OP_PREV, 0, 0, 4), FAB_ERR_PLAN); /* PREV in a file */
    MUT(op(hp + 156, FAB_OP_LIT, 1, 0, 4), FAB_ERR_PLAN);  /* LIT names a block */
    MUT(op(hp + 156, FAB_OP_LIT, 0, 1, 4), FAB_ERR_PLAN);
    MUT(op(hp + 156, FAB_OP_LIT, 0, 0, 0), FAB_ERR_PLAN); /* zero length */
    MUT(memcpy(hp + 104, hp + 64, 40), FAB_ERR_PLAN);     /* same block twice */
    MUT(put32(hp + 44, FAB_MAX_BLOCKS + 1u), FAB_ERR_PLAN);
    MUT(put32(hp + 48, FAB_MAX_OPS + 1u), FAB_ERR_PLAN);
    MUT(put32(hp + 48, 3), FAB_ERR_PLAN);
#undef MUT

    /* PREV in a frame: prev_need is the highest byte read */
    hand_plan();
    op(hp + 144, FAB_OP_PREV, 0, 10, 4);
    EQ(fab_plan_parse(hp, n, FAB_PLAN_FRAME, 4, &p), FAB_OK);
    EQ(p.prev_need, 14);
    op(hp + 144, FAB_OP_PREV, 0, 0xFFFFFFFFu, 4);
    EQ(fab_plan_parse(hp, n, FAB_PLAN_FRAME, 4, &p), FAB_OK);
    EQ(p.prev_need, 0x100000003ull);

    /* period: an open needs one, a frame must not have one */
    hand_plan();
    put32(hp + 52, 16667);
    EQ(fab_plan_parse(hp, n, FAB_PLAN_OPEN, 4, &p), FAB_OK);
    EQ(p.slot_us, 16667);
    EQ(fab_plan_parse(hp, n, FAB_PLAN_FRAME, 4, &p), FAB_ERR_PLAN);
    EQ(fab_plan_parse(0, n, FAB_PLAN_FILE, 4, &p), FAB_ERR_ARG);
}

static void test_public_file(void)
{
    uint32_t tl = make_target(), dl = 0, len = 0, olen = 0;
    uint32_t pl = plan_for(tl, NB, 0, &dl);
    fab_seal_args_t a = args(FAB_KIND_FILE, FAB_MODE_PUBLIC);
    EQ(fab_recipe_seal(&a, planb, pl, tdiff, dl, 0, rec, sizeof rec, &len), FAB_OK);
    EQ(len, FAB_HDR + pl + dl + FAB_SIG_PK + FAB_SIG);
    fab_io_t x = io();
    store_reset();
    EQ(run(rec, len, 0, &x, &olen, 0), FAB_OK);
    EQ(olen, tl);
    CHECK(memcmp(out, target, tl) == 0);
    EQ(st.fetches > 0, 1);

    fab_recipe_t r;
    EQ(fab_recipe_parse(rec, len, &r), FAB_OK);
    EQ(r.mode, FAB_MODE_PUBLIC);
    CHECK(r.kem_ct == 0 && r.plan_tag == 0 && r.diff_tag == 0);
    CHECK(all_zero(r.recipient_id, FAB_ID));

    /* not checked -> no opening */
    fab_plan_t p;
    EQ(fab_open_plan(&r, 0, planb, sizeof planb, &p), FAB_ERR_SIGN);
    /* trust */
    uint8_t ids[2][FAB_ID];
    fab_key_id(opk, FAB_SIG_PK, ids[0]);
    EQ(fab_recipe_check(&r, (const uint8_t(*)[FAB_ID]) ids, 1), FAB_ERR_TRUST);
    CHECK(!r.checked);
    fab_key_id(spk, FAB_SIG_PK, ids[1]);
    EQ(fab_recipe_check(&r, (const uint8_t(*)[FAB_ID]) ids, 2), FAB_OK);
    CHECK(r.checked);
    EQ(fab_recipe_check(&r, 0, 1), FAB_ERR_ARG);
    EQ(fab_open_plan(&r, 0, planb, pl - 1, &p), FAB_ERR_SPACE);
    EQ(fab_open_plan(&r, 0, planb, pl, &p), FAB_OK);

    /* space */
    EQ(fab_hydrate(&p, &x, 0), FAB_OK);
    x.out_cap = tl - 1;
    EQ(fab_assemble(&r, &p, 0, &x, 0, &olen), FAB_ERR_SPACE);
    x.out_cap = tl;
    x.diff_cap = dl - 1;
    EQ(fab_assemble(&r, &p, 0, &x, 0, &olen), FAB_ERR_SPACE);
    x.diff_cap = dl;
    EQ(fab_assemble(&r, &p, 0, &x, 0, &olen), FAB_OK);
    fab_stream_t s;
    EQ(fab_assemble(&r, &p, 0, &x, &s, &olen), FAB_ERR_ARG); /* a file has no stream */
}

static void test_recipe_form(void)
{
    uint32_t n = hand_plan(), len = 0;
    fab_seal_args_t a = args(FAB_KIND_FILE, FAB_MODE_PUBLIC);
    EQ(fab_recipe_seal(&a, hp, n, (const uint8_t *) "abcd", 4, 0, rec, sizeof rec, &len), FAB_OK);
    fab_recipe_t r;
    EQ(fab_recipe_parse(rec, len, &r), FAB_OK);
    EQ(fab_recipe_parse(rec, len - 1, &r), FAB_ERR_FORM);
    EQ(fab_recipe_parse(rec, FAB_HDR - 1, &r), FAB_ERR_FORM);
    EQ(fab_recipe_parse(0, len, &r), FAB_ERR_ARG);
    memcpy(rec2, rec, len);
    rec2[len] = 0;
    EQ(fab_recipe_parse(rec2, len + 1, &r), FAB_ERR_FORM);
#define FORM(i, v, want)                                                                           \
    do {                                                                                           \
        memcpy(rec2, rec, len);                                                                    \
        rec2[i] = (uint8_t) (v);                                                                   \
        EQ(fab_recipe_parse(rec2, len, &r), want);                                                 \
    } while (0)
    FORM(0, 'X', FAB_ERR_FORM);
    FORM(3, 'X', FAB_ERR_FORM);
    FORM(4, 2, FAB_ERR_FORM);
    FORM(5, 2, FAB_ERR_FORM);
    FORM(5, FAB_KIND_STREAM, FAB_ERR_FORM); /* a public stream */
    FORM(6, 2, FAB_ERR_FORM);
    FORM(7, 1, FAB_ERR_FORM);
    FORM(40, 1, FAB_ERR_FORM); /* public with a recipient */
    FORM(71, 1, FAB_ERR_FORM);
    FORM(75, n + 1, FAB_ERR_FORM);
    FORM(79, 5, FAB_ERR_FORM);
    FORM(8, rec[8] ^ 1, FAB_OK); /* parses; check catches it */
    EQ(fab_recipe_check(&r, 0, 0), FAB_ERR_SIGN);
#undef FORM
    /* plan shorter than its header */
    memcpy(rec2, rec, len);
    put32(rec2 + 72, 63);
    put32(rec2 + 76, 4 + n - 63);
    EQ(fab_recipe_parse(rec2, len, &r), FAB_ERR_FORM);
    /* every signed byte matters */
    for (uint32_t i = 0; i < len; i += 97) {
        memcpy(rec2, rec, len);
        rec2[i] ^= 0x10;
        if (fab_recipe_parse(rec2, len, &r) != FAB_OK) continue;
        EQ(fab_recipe_check(&r, 0, 0), FAB_ERR_SIGN);
    }
    memcpy(rec2, rec, len);
    rec2[len - 1] ^= 1;
    EQ(fab_recipe_parse(rec2, len, &r), FAB_OK);
    EQ(fab_recipe_check(&r, 0, 0), FAB_ERR_SIGN);
    /* a key swapped in with its matching id is still not the signer */
    memcpy(rec2, rec, len);
    memcpy(rec2 + len - FAB_SIG - FAB_SIG_PK, opk, FAB_SIG_PK);
    fab_key_id(opk, FAB_SIG_PK, rec2 + 8);
    EQ(fab_recipe_parse(rec2, len, &r), FAB_OK);
    EQ(fab_recipe_check(&r, 0, 0), FAB_ERR_SIGN);

    /* seal refuses what parse would refuse */
    EQ(fab_recipe_seal(&a, hp, n, (const uint8_t *) "abc", 3, 0, rec2, sizeof rec2, &len),
       FAB_ERR_PLAN);
    EQ(fab_recipe_seal(&a, hp, n, (const uint8_t *) "abcd", 4, 0, rec2, 100, &len), FAB_ERR_SPACE);
    fab_stream_t s;
    EQ(fab_recipe_seal(&a, hp, n, (const uint8_t *) "abcd", 4, &s, rec2, sizeof rec2, &len),
       FAB_ERR_ARG);
    fab_seal_args_t b = args(FAB_KIND_STREAM, FAB_MODE_PUBLIC);
    EQ(fab_recipe_seal(&b, hp, n, (const uint8_t *) "abcd", 4, &s, rec2, sizeof rec2, &len),
       FAB_ERR_ARG);
    b = args(FAB_KIND_FILE, FAB_MODE_SEALED);
    uint8_t bad[FAB_KEM_EK];
    memset(bad, 0xFF, sizeof bad);
    b.recipient_ek = bad;
    EQ(fab_recipe_seal(&b, hp, n, (const uint8_t *) "abcd", 4, 0, rec2, sizeof rec2, &len),
       FAB_ERR_KEY);
    b.recipient_ek = 0;
    EQ(fab_recipe_seal(&b, hp, n, (const uint8_t *) "abcd", 4, 0, rec2, sizeof rec2, &len),
       FAB_ERR_ARG);
}

static void test_sealed_file(void)
{
    uint32_t tl = make_target(), dl = 0, len = 0, olen = 0;
    uint32_t pl = plan_for(tl, NB, 0, &dl);
    fab_seal_args_t a = args(FAB_KIND_FILE, FAB_MODE_SEALED);
    EQ(fab_recipe_seal(&a, planb, pl, tdiff, dl, 0, rec, sizeof rec, &len), FAB_OK);
    EQ(len, FAB_HDR + FAB_KEM_CT + pl + dl + 2 * FAB_TAG + FAB_SIG_PK + FAB_SIG);
    /* the plan and diff are not in clear */
    CHECK(memcmp(rec + FAB_HDR + FAB_KEM_CT, planb, 64) != 0);
    fab_io_t x = io();
    store_reset();
    EQ(run(rec, len, rdk, &x, &olen, 0), FAB_OK);
    EQ(olen, tl);
    CHECK(memcmp(out, target, tl) == 0);
    /* another recipient's key */
    EQ(run(rec, len, xdk, &x, &olen, 0), FAB_ERR_KEY);
    EQ(run(rec, len, 0, &x, &olen, 0), FAB_ERR_ARG);
    /* an invalid key */
    static uint8_t baddk[FAB_KEM_DK];
    memcpy(baddk, rdk, FAB_KEM_DK);
    baddk[FAB_KEM_DK - 40] ^= 1; /* H(ek) */
    EQ(run(rec, len, baddk, &x, &olen, 0), FAB_ERR_KEY);
    /* same id, rewritten ciphertext: implicit rejection -> the plan fails AEAD */
    memcpy(rec2, rec, len);
    rec2[FAB_HDR + 5] ^= 1;
    resign(rec2, len);
    EQ(run(rec2, len, rdk, &x, &olen, 0), FAB_ERR_AUTH);
    /* plan ciphertext and tag */
    memcpy(rec2, rec, len);
    rec2[FAB_HDR + FAB_KEM_CT + 3] ^= 1;
    resign(rec2, len);
    EQ(run(rec2, len, rdk, &x, &olen, 0), FAB_ERR_AUTH);
    memcpy(rec2, rec, len);
    rec2[FAB_HDR + FAB_KEM_CT + pl] ^= 1;
    resign(rec2, len);
    EQ(run(rec2, len, rdk, &x, &olen, 0), FAB_ERR_AUTH);
    /* diff: plan opens, diff fails, out untouched and diff wiped */
    memset(out, 0, tl);
    memcpy(rec2, rec, len);
    rec2[FAB_HDR + FAB_KEM_CT + pl + FAB_TAG + 1] ^= 1;
    resign(rec2, len);
    memset(diffb, 0x55, dl);
    EQ(run(rec2, len, rdk, &x, &olen, 0), FAB_ERR_AUTH);
    CHECK(all_zero(out, tl));
    CHECK(all_zero(diffb, dl));
    memcpy(rec2, rec, len);
    rec2[FAB_HDR + FAB_KEM_CT + pl + FAB_TAG + dl] ^= 1; /* diff tag */
    resign(rec2, len);
    EQ(run(rec2, len, rdk, &x, &olen, 0), FAB_ERR_AUTH);
    /* a rewritten recipient id is refused before any key is used */
    memcpy(rec2, rec, len);
    rec2[41] ^= 1;
    resign(rec2, len);
    EQ(run(rec2, len, rdk, &x, &olen, 0), FAB_ERR_KEY);
}

static void test_blocks_and_target(void)
{
    uint32_t tl = make_target(), dl = 0, len = 0, olen = 0, got = 0;
    uint32_t pl = plan_for(tl, NB, 0, &dl); /* block 2 unused: a decoy */
    fab_seal_args_t a = args(FAB_KIND_FILE, FAB_MODE_PUBLIC);
    EQ(fab_recipe_seal(&a, planb, pl, tdiff, dl, 0, rec, sizeof rec, &len), FAB_OK);
    fab_recipe_t r;
    fab_plan_t p;
    fab_io_t x = io();
    EQ(fab_recipe_parse(rec, len, &r), FAB_OK);
    EQ(fab_recipe_check(&r, 0, 0), FAB_OK);
    EQ(fab_open_plan(&r, 0, planb, sizeof planb, &p), FAB_OK);
    EQ(p.n_blocks, NB);

    store_reset();
    EQ(fab_hydrate(&p, &x, &got), FAB_OK);
    EQ(got, NB);
    EQ(st.fetches, NB);
    /* the decoy must be fetched by hydrate, never by assembly */
    store_reset();
    st.missing = 2;
    EQ(fab_hydrate(&p, &x, &got), FAB_ERR_FETCH);
    EQ(got, 2);
    EQ(fab_assemble(&r, &p, 0, &x, 0, &olen), FAB_OK);
    CHECK(memcmp(out, target, tl) == 0);

    store_reset();
    st.corrupt = 3;
    EQ(fab_hydrate(&p, &x, &got), FAB_ERR_BLOCK);
    EQ(got, 3);
    memset(out, 0x77, tl);
    EQ(fab_assemble(&r, &p, 0, &x, 0, &olen), FAB_ERR_BLOCK);
    CHECK(all_zero(out, tl));
    EQ(olen, 0);
    store_reset();
    st.shorten = 1;
    EQ(fab_assemble(&r, &p, 0, &x, 0, &olen), FAB_ERR_BLOCK);
    store_reset();
    st.missing = 0;
    EQ(fab_assemble(&r, &p, 0, &x, 0, &olen), FAB_ERR_FETCH);
    store_reset();
    EQ(fab_hydrate(0, &x, &got), FAB_ERR_ARG);

    /* a plan whose target CID names other bytes: assembled, then refused */
    uint32_t n = hand_plan();
    ipfsn_cid_t other;
    fab_target_cid((const uint8_t *) "12345678", 8, &W, &other);
    ipfsn_cid_encode(&other, hp, 36);
    EQ(fab_recipe_seal(&a, hp, n, (const uint8_t *) "abcd", 4, 0, rec, sizeof rec, &len), FAB_OK);
    memset(out, 0, sizeof out);
    store_reset();
    EQ(run(rec, len, 0, &x, &olen, 0), FAB_ERR_TARGET);
    CHECK(all_zero(out, 8));
    CHECK(all_zero(diffb, 4));
    EQ(olen, 0);
    /* and with the right CID it passes */
    n = hand_plan();
    EQ(fab_recipe_seal(&a, hp, n, (const uint8_t *) "abcd", 4, 0, rec, sizeof rec, &len), FAB_OK);
    EQ(run(rec, len, 0, &x, &olen, 0), FAB_OK);
    EQ(olen, 8);
    CHECK(memcmp(out, target, 8) == 0);
    /* wrong diff bytes (signed by the sender, so they pass every other check) */
    EQ(fab_recipe_seal(&a, hp, n, (const uint8_t *) "abce", 4, 0, rec, sizeof rec, &len), FAB_OK);
    EQ(run(rec, len, 0, &x, &olen, 0), FAB_ERR_TARGET);
}

static int views;
static int view(void *ctx, const ipfsn_cid_t *cid, const uint8_t **data, uint32_t *len)
{
    store_t *s = (store_t *) ctx;
    views++;
    for (int i = 0; i < NB; i++) {
        if (!ipfsn_cid_equal(cid, &bcid[i])) continue;
        if (i == s->missing) return -1;
        if (i == s->corrupt) blk[i][blen[i] / 2] ^= 1;
        *data = blk[i];
        *len = blen[i] - (i == s->shorten ? 1u : 0u);
        return 0;
    }
    return -1;
}

static void test_view(void)
{
    uint32_t tl = make_target(), dl = 0, len = 0, olen = 0;
    uint32_t pl = plan_for(tl, NB, 0, &dl);
    fab_seal_args_t a = args(FAB_KIND_FILE, FAB_MODE_SEALED);
    EQ(fab_recipe_seal(&a, planb, pl, tdiff, dl, 0, rec, sizeof rec, &len), FAB_OK);
    fab_io_t x = io();
    x.get = 0;
    x.view = view;
    x.view_ctx = &st;
    store_reset();
    views = 0;
    EQ(run(rec, len, rdk, &x, &olen, 0), FAB_OK);
    EQ(olen, tl);
    CHECK(memcmp(out, target, tl) == 0);
    /* hydrate views all 4 once; assembly views the 3 it copies from once each,
     * although block 0 is used by two ops apart */
    EQ(views, NB + 3);
    store_reset();
    st.corrupt = 0;
    EQ(run(rec, len, rdk, &x, &olen, 0), FAB_ERR_BLOCK);
    blk[0][blen[0] / 2] ^= 1; /* undo */
    store_reset();
    st.shorten = 3;
    EQ(run(rec, len, rdk, &x, &olen, 0), FAB_ERR_BLOCK);
    store_reset();
    st.missing = 1;
    EQ(run(rec, len, rdk, &x, &olen, 0), FAB_ERR_FETCH);
    store_reset();
    EQ(run(rec, len, rdk, &x, &olen, 0), FAB_OK);
    /* neither source */
    x.view = 0;
    EQ(run(rec, len, rdk, &x, &olen, 0), FAB_ERR_ARG);
}

static void test_seal(void)
{
    uint32_t n = hand_plan(), len = 0, olen = 0;
    fab_seal_args_t a = args(FAB_KIND_FILE, FAB_MODE_SEALED);
    EQ(fab_recipe_seal(&a, hp, n, (const uint8_t *) "abcd", 4, 0, rec, sizeof rec, &len), FAB_OK);
    fab_io_t x = io();
    x.seal = seal;
    lpres_state_t refuse[3] = {LPRES_STATE_FALSE, LPRES_STATE_BOTH, LPRES_STATE_NEITHER};
    for (int i = 0; i < 3; i++) {
        seal_state = refuse[i];
        seal_calls = 0;
        memset(out, 0x33, 8);
        store_reset();
        EQ(run(rec, len, rdk, &x, &olen, 0), FAB_ERR_SEAL);
        EQ(seal_calls, 1);
        EQ(out[0], 0x33);  /* nothing written */
        EQ(st.fetches, 2); /* hydrate ran before the seal was asked */
    }
    seal_state = LPRES_STATE_TRUE;
    EQ(run(rec, len, rdk, &x, &olen, 0), FAB_OK);
    EQ(olen, 8);
}

/* ---- streams ---------------------------------------------------------------- */

#define FR 4096u
static uint8_t f0[FR], f1[FR], fbuf[FR], frame[FR * 2], frame2[FR * 2], cp[FAB_CP_LEN];
static uint8_t fplan[1u << 16];

/* Frame plan for `next` built on `prev` plus the public blocks. */
static uint32_t frame_plan(const uint8_t *prevb, const uint8_t *next, uint32_t *dl, bool use_prev)
{
    fab_src_t fs[NB + 1];
    memcpy(fs, srcs, sizeof srcs[0] * NB);
    uint32_t ns = NB;
    if (use_prev) {
        fs[NB].data = prevb;
        fs[NB].len = FR;
        fs[NB].prev = true;
        ns++;
    }
    uint32_t no = 0, pl = 0;
    ipfsn_cid_t tc;
    EQ(fab_build(fs, ns, next, FR, 16, table, 14, opsb, 4096, &no, tdiff, OUTCAP, dl), FAB_OK);
    EQ(fab_target_cid(next, FR, &W, &tc), FAB_OK);
    EQ(fab_plan_write(&tc, FR, 0, fs, ns, opsb, no, fplan, sizeof fplan, &pl), FAB_OK);
    return pl;
}

static void next_frame(const uint8_t *from, uint8_t *to, uint32_t k)
{
    memcpy(to, from, FR);
    for (uint32_t i = 0; i < 24; i++) to[(k * 131 + i * 7) % FR] = rb(); /* small motion */
}

static void test_stream(void)
{
    /* frame 0: a background from block 0 */
    memcpy(target, blk[0], FR);
    memcpy(f0, blk[0], FR);
    uint32_t dl = 0, len = 0, olen = 0, fl = 0;
    uint32_t pl = plan_for(FR, NB, 16667, &dl);
    fab_seal_args_t a = args(FAB_KIND_STREAM, FAB_MODE_SEALED);
    static fab_stream_t tx, rx, evil;
    EQ(fab_recipe_seal(&a, planb, pl, tdiff, dl, &tx, rec, sizeof rec, &len), FAB_OK);
    CHECK(tx.live);
    EQ(tx.slot_us, 16667);
    fab_io_t x = io();
    store_reset();
    EQ(run(rec, len, rdk, &x, &olen, 0), FAB_ERR_ARG); /* a stream needs state */
    EQ(run(rec, len, rdk, &x, &olen, &rx), FAB_OK);
    EQ(olen, FR);
    CHECK(memcmp(out, f0, FR) == 0);
    CHECK(rx.live);
    CHECK(memcmp(rx.key, tx.key, FAB_KEY) == 0);
    CHECK(memcmp(rx.id, tx.id, FAB_SID) == 0);
    EQ(rx.slot_us, 16667);
    EQ(rx.seq, 0);
    memcpy(fbuf, out, FR); /* receiver's prev */

    /* frames 1..5: small motion, built on the previous frame */
    uint8_t *cur = f0, *nxt = f1;
    for (uint32_t k = 1; k <= 5; k++) {
        next_frame(cur, nxt, k);
        uint32_t fpl = frame_plan(cur, nxt, &dl, true);
        EQ(fab_frame_seal(&tx, k - 1, k, fplan, fpl, tdiff, dl, frame, sizeof frame, &fl), FAB_OK);
        CHECK(fl < 1200); /* fits one Ethernet MTU with room */
        EQ(fab_frame_apply(&rx, frame, fl, fbuf, FR, planb, sizeof planb, &x, &olen), FAB_OK);
        EQ(olen, FR);
        CHECK(memcmp(out, nxt, FR) == 0);
        memcpy(fbuf, out, FR);
        EQ(rx.seq, k);
        if (k == 5) {
            EQ(fab_frame_apply(&rx, frame, fl, fbuf, FR, planb, sizeof planb, &x, &olen),
               FAB_ERR_REPLAY);
        }
        uint8_t *t = cur;
        cur = nxt;
        nxt = t;
    }
    /* checkpoint over 0..5 */
    EQ(fab_checkpoint_sign(&tx, 0, 5, ssk, 0, cp), FAB_OK);
    EQ(fab_checkpoint_verify(&rx, cp, FAB_CP_LEN), FAB_OK);
    EQ(rx.checked_seq, 5);
    EQ(fab_checkpoint_verify(&rx, cp, FAB_CP_LEN - 1), FAB_ERR_FORM);
    cp[FAB_CP_LEN - 1] ^= 1;
    EQ(fab_checkpoint_verify(&rx, cp, FAB_CP_LEN), FAB_ERR_SIGN);
    EQ(fab_checkpoint_sign(&tx, 0, 5, osk, 0, cp), FAB_OK); /* someone else's key */
    EQ(fab_checkpoint_verify(&rx, cp, FAB_CP_LEN), FAB_ERR_SIGN);
    EQ(fab_checkpoint_sign(&tx, 5, 4, ssk, 0, cp), FAB_ERR_ARG);
    EQ(fab_checkpoint_sign(&tx, 0, FAB_CHAIN_RING, ssk, 0, cp), FAB_ERR_ARG);

    /* tampering with a frame */
    next_frame(cur, nxt, 6);
    uint32_t fpl = frame_plan(cur, nxt, &dl, true);
    EQ(fab_frame_seal(&tx, 5, 6, fplan, fpl, tdiff, dl, frame, sizeof frame, &fl), FAB_OK);
    for (uint32_t i = 0; i < fl; i += 13) {
        memcpy(frame2, frame, fl);
        frame2[i] ^= 4;
        int rc = fab_frame_apply(&rx, frame2, fl, fbuf, FR, planb, sizeof planb, &x, &olen);
        if (i < 4 || (i >= 4 && i < 8) || (i >= 48 && i < 56))
            CHECK(rc == FAB_ERR_FORM || rc == FAB_ERR_AUTH);
        else if (i >= 8 && i < 24)
            EQ(rc, FAB_ERR_STREAM);
        else if (i >= 24 && i < 32)
            CHECK(rc == FAB_ERR_AUTH || rc == FAB_ERR_REPLAY);
        else
            EQ(rc, FAB_ERR_AUTH);
    }
    EQ(rx.seq, 5); /* nothing above was accepted */
    EQ(fab_frame_apply(&rx, frame, fl - 1, fbuf, FR, planb, sizeof planb, &x, &olen), FAB_ERR_FORM);
    EQ(fab_frame_apply(&rx, frame, fl, fbuf, FR, planb, 10, &x, &olen), FAB_ERR_SPACE);
    EQ(fab_frame_apply(&rx, frame, fl, out, FR, planb, sizeof planb, &x, &olen), FAB_ERR_ARG);
    fab_io_t y = x;
    y.out_cap = FR - 1;
    EQ(fab_frame_apply(&rx, frame, fl, fbuf, FR, planb, sizeof planb, &y, &olen), FAB_ERR_SPACE);
    /* the wrong prev (base right, bytes wrong) fails the target check */
    memcpy(frame2, fbuf, FR);
    fbuf[100] ^= 1;
    EQ(fab_frame_apply(&rx, frame, fl, fbuf, FR, planb, sizeof planb, &x, &olen), FAB_ERR_TARGET);
    memcpy(fbuf, frame2, FR);
    EQ(fab_frame_apply(&rx, frame, fl, fbuf, 16, planb, sizeof planb, &x, &olen), FAB_ERR_RESYNC);
    EQ(fab_frame_apply(&rx, frame, fl, 0, 0, planb, sizeof planb, &x, &olen), FAB_ERR_RESYNC);
    y = x;
    y.seal = seal;
    seal_state = LPRES_STATE_BOTH;
    EQ(fab_frame_apply(&rx, frame, fl, fbuf, FR, planb, sizeof planb, &y, &olen), FAB_ERR_SEAL);
    EQ(fab_frame_apply(&rx, frame, fl, fbuf, FR, planb, sizeof planb, &x, &olen), FAB_OK);
    memcpy(fbuf, out, FR);
    {
        uint8_t *t = cur;
        cur = nxt;
        nxt = t;
    }

    /* loss: frame 7 never arrives; frame 8 built on 7 cannot apply */
    next_frame(cur, nxt, 7);
    fpl = frame_plan(cur, nxt, &dl, true);
    EQ(fab_frame_seal(&tx, 6, 7, fplan, fpl, tdiff, dl, frame, sizeof frame, &fl), FAB_OK);
    memcpy(f0 == cur ? f1 : f0, nxt, 0);
    static uint8_t lost[FR];
    memcpy(lost, nxt, FR);
    next_frame(lost, cur, 8); /* cur now holds frame 8 */
    fpl = frame_plan(lost, cur, &dl, true);
    EQ(fab_frame_seal(&tx, 7, 8, fplan, fpl, tdiff, dl, frame, sizeof frame, &fl), FAB_OK);
    EQ(fab_frame_apply(&rx, frame, fl, fbuf, FR, planb, sizeof planb, &x, &olen), FAB_ERR_RESYNC);
    EQ(rx.seq, 6);
    /* frame 9: independent (no PREV) recovers */
    memcpy(nxt, blk[3], FR);
    memcpy(nxt + 100, "resync!", 7);
    fpl = frame_plan(0, nxt, &dl, false);
    EQ(fab_frame_seal(&tx, 0, 9, fplan, fpl, tdiff, dl, frame, sizeof frame, &fl), FAB_OK);
    EQ(fab_frame_apply(&rx, frame, fl, fbuf, FR, planb, sizeof planb, &x, &olen), FAB_OK);
    CHECK(memcmp(out, nxt, FR) == 0);
    EQ(rx.seq, 9);
    EQ(rx.prev_seq, 9);
    /* a checkpoint over the gap proves nothing either way */
    EQ(fab_checkpoint_sign(&tx, 5, 9, ssk, 0, cp), FAB_OK);
    EQ(fab_checkpoint_verify(&rx, cp, FAB_CP_LEN), FAB_ERR_RESYNC);
    EQ(fab_checkpoint_sign(&tx, 9, 9, ssk, 0, cp), FAB_OK);
    EQ(fab_checkpoint_verify(&rx, cp, FAB_CP_LEN), FAB_OK);
    EQ(rx.checked_seq, 9);

    /* a forger holding the stream key: frame 10 accepted, checkpoint exposes it */
    memcpy(fbuf, out, FR);
    memcpy(&evil, &tx, sizeof tx);
    next_frame(nxt, cur, 10);
    fpl = frame_plan(nxt, cur, &dl, true);
    EQ(fab_frame_seal(&evil, 9, 10, fplan, fpl, tdiff, dl, frame, sizeof frame, &fl), FAB_OK);
    EQ(fab_frame_apply(&rx, frame, fl, fbuf, FR, planb, sizeof planb, &x, &olen), FAB_OK);
    next_frame(nxt, cur, 11); /* the real frame 10 */
    fpl = frame_plan(nxt, cur, &dl, true);
    EQ(fab_frame_seal(&tx, 9, 10, fplan, fpl, tdiff, dl, frame, sizeof frame, &fl), FAB_OK);
    EQ(fab_checkpoint_sign(&tx, 9, 10, ssk, 0, cp), FAB_OK);
    EQ(fab_checkpoint_verify(&rx, cp, FAB_CP_LEN), FAB_ERR_STREAM);
    EQ(rx.checked_seq, 9);

    /* a frame of another stream */
    memcpy(frame2, frame, fl);
    frame2[8] ^= 1;
    EQ(fab_frame_apply(&rx, frame2, fl, fbuf, FR, planb, sizeof planb, &x, &olen), FAB_ERR_STREAM);
    /* sender rules */
    EQ(fab_frame_seal(&tx, tx.seq + 1, 0, fplan, fpl, tdiff, dl, frame, sizeof frame, &fl),
       FAB_ERR_ARG);
    EQ(fab_frame_seal(&tx, 0, 0, fplan, fpl, tdiff, dl, frame, 10, &fl), FAB_ERR_SPACE);
    EQ(fab_frame_seal(&tx, 0, 0, fplan, fpl, tdiff, dl + 1, frame, sizeof frame, &fl),
       FAB_ERR_PLAN);
    fab_stream_t dead;
    memset(&dead, 0, sizeof dead);
    EQ(fab_frame_seal(&dead, 0, 0, fplan, fpl, tdiff, dl, frame, sizeof frame, &fl),
       FAB_ERR_STREAM);
    EQ(fab_frame_apply(&dead, frame, fl, fbuf, FR, planb, sizeof planb, &x, &olen), FAB_ERR_STREAM);
    EQ(fab_checkpoint_verify(&dead, cp, FAB_CP_LEN), FAB_ERR_STREAM);
    /* the checkpoint window rule on the receiving side */
    EQ(fab_checkpoint_sign(&tx, 9, 10, ssk, 0, cp), FAB_OK);
    cp[39] = 0; /* to < from */
    EQ(fab_checkpoint_verify(&rx, cp, FAB_CP_LEN), FAB_ERR_FORM);
}

/* ---- byte helpers, against hand-written expectations ------------------------- */

static void test_helpers(void)
{
    uint8_t b[12];
    fab__put32(b, 0x01020304u);
    EQ(b[0], 1);
    EQ(b[1], 2);
    EQ(b[2], 3);
    EQ(b[3], 4);
    EQ(fab__get32((const uint8_t *) "\x80\x40\x20\x10"), 0x80402010u);
    fab__put64(b, 0x0102030405060708ull);
    for (int i = 0; i < 8; i++) EQ(b[i], i + 1);
    EQ(fab__get64((const uint8_t *) "\xF1\x02\x03\x04\x05\x06\x07\x08"), 0xF102030405060708ull);
    memset(b, 0xAA, sizeof b);
    fab__frame_nonce(2, 0x1122334455667788ull, b);
    static const uint8_t want[12] = {2, 0, 0, 0, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
    CHECK(memcmp(b, want, 12) == 0);
    CHECK(fab__eq("abc", "abc", 3));
    CHECK(!fab__eq("abc", "abd", 3));
    CHECK(!fab__eq("xbc", "abc", 3));
    CHECK(fab__eq("a", "b", 0));
    CHECK(fab__zero((const uint8_t *) "\0\0\0", 3));
    CHECK(!fab__zero((const uint8_t *) "\0\0\1", 3));
    uint8_t c[4] = {9, 9, 9, 9};
    fab__cpy(c, "xyz", 3);
    CHECK(memcmp(c, "xyz\x09", 4) == 0);
    fab__wipe(c, 3);
    CHECK(memcmp(c, "\0\0\0\x09", 4) == 0);
}

/* ---- the UnixFS target CID against independent computations ----------------- */

static int count_sink(void *ctx, const ipfsn_cid_t *cid, const uint8_t *block, uint32_t len)
{
    (void) ctx;
    (void) cid;
    (void) block;
    (void) len;
    return 0;
}

static void test_target_kat(void)
{
    ipfsn_cid_t a, b;
    /* up to one chunk the root is the raw leaf itself */
    EQ(fab_target_cid(blk[0], BL, &W, &a), FAB_OK);
    EQ(ipfsn_cid_sha256(IPFSN_MC_RAW, blk[0], BL, &b), IPFSN_OK);
    CHECK(ipfsn_cid_equal(&a, &b));
    /* several chunks: the explicit Kubo defaults give the same root */
    uint32_t n = 3u * IPFSN_UFS_CHUNK + 1000u;
    for (uint32_t i = 0; i < n; i++) target[i % OUTCAP] = (uint8_t) (i * 7u + (i >> 9));
    static ipfsn_ufs_t u;
    static uint8_t chunk[IPFSN_UFS_CHUNK];
    static uint8_t big[3u * IPFSN_UFS_CHUNK + 1000u];
    for (uint32_t i = 0; i < n; i++) big[i] = (uint8_t) (i * 7u + (i >> 9));
    uint64_t size = 0;
    EQ(ipfsn_ufs_init(&u, chunk, IPFSN_UFS_CHUNK, IPFSN_UFS_LINKS, count_sink, 0), IPFSN_OK);
    EQ(ipfsn_ufs_update(&u, big, n), IPFSN_OK);
    EQ(ipfsn_ufs_final(&u, &b, &size), IPFSN_OK);
    EQ(fab_target_cid(big, n, &W, &a), FAB_OK);
    CHECK(ipfsn_cid_equal(&a, &b));
    EQ(a.codec, IPFSN_MC_DAG_PB);
    /* the empty file needs no data pointer */
    EQ(fab_target_cid(0, 0, &W, &a), FAB_OK);
    EQ(fab_target_cid(blk[0], 1, &W, 0), FAB_ERR_ARG);
}

/* ---- the wire format, byte for byte, and pinned outputs ----------------------- */

static void hash_hex(const uint8_t *p, uint32_t n, char out[65])
{
    uint8_t h[32];
    sha256(p, n, h);
    for (int i = 0; i < 32; i++) sprintf(out + 2 * i, "%02x", h[i]);
}

static void test_golden(void)
{
    uint32_t n = hand_plan(), len = 0;
    fab_seal_args_t a = args(FAB_KIND_FILE, FAB_MODE_SEALED);
    memset(rec, 0xFF, 200);
    EQ(fab_recipe_seal(&a, hp, n, (const uint8_t *) "abcd", 4, 0, rec, sizeof rec, &len), FAB_OK);
    CHECK(memcmp(rec, "FABR\x01\x00\x01\x00", 8) == 0);
    uint8_t id[32];
    fab_key_id(spk, FAB_SIG_PK, id);
    CHECK(memcmp(rec + 8, id, 32) == 0);
    sha256(spk, FAB_SIG_PK, id);
    CHECK(memcmp(rec + 8, id, 32) == 0);
    sha256(rek, FAB_KEM_EK, id);
    CHECK(memcmp(rec + 40, id, 32) == 0);
    EQ(fab__get32(rec + 72), n);
    EQ(rec[72], 0);
    EQ(rec[75], n & 0xFF);
    EQ(rec[79], 4);
    CHECK(memcmp(rec + len - FAB_SIG - FAB_SIG_PK, spk, FAB_SIG_PK) == 0);
    char hx[65];
    hash_hex(rec, len, hx);
    /* the whole recipe for fixed keys, coins and signing randomness: any
     * change to the key schedule, labels, nonces or layout changes it */
    CHECK(strcmp(hx, GOLD_RECIPE) == 0);
    if (strcmp(hx, GOLD_RECIPE)) printf("  recipe %s\n", hx);

    /* public: the plan in clear right after the header */
    a = args(FAB_KIND_FILE, FAB_MODE_PUBLIC);
    memset(rec2, 0xFF, 200);
    EQ(fab_recipe_seal(&a, hp, n, (const uint8_t *) "abcd", 4, 0, rec2, sizeof rec2, &len), FAB_OK);
    CHECK(memcmp(rec2, "FABR\x01\x00\x00\x00", 8) == 0);
    CHECK(all_zero(rec2 + 40, 32));
    CHECK(memcmp(rec2 + FAB_HDR, hp, n) == 0);
    CHECK(memcmp(rec2 + FAB_HDR + n, "abcd", 4) == 0);

    /* a stream, its first frame and a checkpoint */
    memcpy(target, blk[0], FR);
    uint32_t dl = 0, pl = plan_for(FR, NB, 16667, &dl);
    a = args(FAB_KIND_STREAM, FAB_MODE_SEALED);
    static fab_stream_t tx;
    EQ(fab_recipe_seal(&a, planb, pl, tdiff, dl, &tx, rec, sizeof rec, &len), FAB_OK);
    hash_hex(tx.key, FAB_KEY, hx);
    CHECK(strcmp(hx, GOLD_STREAM_KEY) == 0);
    if (strcmp(hx, GOLD_STREAM_KEY)) printf("  stream key %s\n", hx);
    uint32_t fpl = hand_plan(), fl = 0;
    memset(frame, 0xFF, 64);
    EQ(fab_frame_seal(&tx, 0, 0x0A0B0C0D0E0F1011ull, hp, fpl, (const uint8_t *) "abcd", 4, frame,
                      sizeof frame, &fl),
       FAB_OK);
    CHECK(memcmp(frame, "FABF\x01\x00\x00\x00", 8) == 0);
    CHECK(memcmp(frame + 8, tx.id, FAB_SID) == 0);
    EQ(fab__get64(frame + 24), 1);
    EQ(fab__get64(frame + 32), 0);
    static const uint8_t slot[8] = {0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11};
    CHECK(memcmp(frame + 40, slot, 8) == 0);
    EQ(fab__get32(frame + 48), fpl);
    EQ(fab__get32(frame + 52), 4);
    EQ(fl, FAB_FHDR + fpl + 4 + 2 * FAB_TAG);
    hash_hex(frame, fl, hx);
    CHECK(strcmp(hx, GOLD_FRAME) == 0);
    if (strcmp(hx, GOLD_FRAME)) printf("  frame %s\n", hx);
    EQ(fab_checkpoint_sign(&tx, 0, 1, ssk, 0, cp), FAB_OK);
    CHECK(memcmp(cp, "FABC\x01\x00\x00\x00", 8) == 0);
    CHECK(memcmp(cp + 8, tx.id, FAB_SID) == 0);
    EQ(fab__get64(cp + 24), 0);
    EQ(fab__get64(cp + 32), 1);
    hash_hex(cp, FAB_CP_LEN, hx);
    CHECK(strcmp(hx, GOLD_CP) == 0);
    if (strcmp(hx, GOLD_CP)) printf("  checkpoint %s\n", hx);
}

/* ---- argument rules ------------------------------------------------------------- */

static void test_args(void)
{
    uint32_t n = hand_plan(), len = 0, olen = 7;
    fab_seal_args_t a = args(FAB_KIND_FILE, FAB_MODE_PUBLIC);
    EQ(fab_recipe_seal(&a, hp, n, (const uint8_t *) "abcd", 4, 0, rec, sizeof rec, &len), FAB_OK);
    fab_recipe_t r, z;
    fab_plan_t p;
    memset(&z, 0, sizeof z);
    EQ(fab_recipe_parse(rec, len, 0), FAB_ERR_ARG);
    EQ(fab_recipe_parse(rec, len, &r), FAB_OK);
    EQ(fab_recipe_check(0, 0, 0), FAB_ERR_ARG);
    EQ(fab_recipe_check(&z, 0, 0), FAB_ERR_ARG);
    /* not checked yet */
    fab_io_t x = io();
    EQ(fab_open_plan(&r, 0, planb, sizeof planb, &p), FAB_ERR_SIGN);
    EQ(fab_recipe_check(&r, 0, 0), FAB_OK);
    EQ(fab_open_plan(0, 0, planb, sizeof planb, &p), FAB_ERR_ARG);
    EQ(fab_open_plan(&z, 0, planb, sizeof planb, &p), FAB_ERR_ARG);
    EQ(fab_open_plan(&r, 0, 0, sizeof planb, &p), FAB_ERR_ARG);
    EQ(fab_open_plan(&r, 0, planb, sizeof planb, 0), FAB_ERR_ARG);
    EQ(fab_open_plan(&r, 0, planb, sizeof planb, &p), FAB_OK);
    EQ(fab_plan_parse(hp, n, FAB_PLAN_FILE, 4, 0), FAB_ERR_ARG);
    ipfsn_cid_t c;
    uint32_t l;
    EQ(fab_plan_block(0, 0, &c, &l), FAB_ERR_ARG);
    EQ(fab_plan_block(&p, 0, 0, &l), FAB_ERR_ARG);
    EQ(fab_plan_block(&p, 0, &c, 0), FAB_ERR_ARG);
    EQ(fab_hydrate(&p, 0, 0), FAB_ERR_ARG);
    fab_io_t y = x;
    y.get = 0;
    EQ(fab_hydrate(&p, &y, 0), FAB_ERR_ARG);
    y = x;
    y.work = 0;
    EQ(fab_hydrate(&p, &y, 0), FAB_ERR_ARG);
    store_reset();
    EQ(fab_hydrate(&p, &x, 0), FAB_OK);

    /* assemble */
    EQ(fab_assemble(0, &p, 0, &x, 0, &olen), FAB_ERR_ARG);
    EQ(fab_assemble(&z, &p, 0, &x, 0, &olen), FAB_ERR_ARG);
    EQ(fab_assemble(&r, 0, 0, &x, 0, &olen), FAB_ERR_ARG);
    EQ(fab_assemble(&r, &p, 0, &x, 0, 0), FAB_ERR_ARG);
    EQ(fab_assemble(&r, &p, 0, 0, 0, &olen), FAB_ERR_ARG);
    y = x;
    y.get = 0;
    EQ(fab_assemble(&r, &p, 0, &y, 0, &olen), FAB_ERR_ARG);
    y = x;
    y.work = 0;
    EQ(fab_assemble(&r, &p, 0, &y, 0, &olen), FAB_ERR_ARG);
    y = x;
    y.out = 0;
    EQ(fab_assemble(&r, &p, 0, &y, 0, &olen), FAB_ERR_ARG);
    y = x;
    y.diff_buf = 0;
    EQ(fab_assemble(&r, &p, 0, &y, 0, &olen), FAB_ERR_ARG);
    y.diff_cap = 0; /* no diff buffer is fine for no diff, but this plan has 4 bytes */
    EQ(fab_assemble(&r, &p, 0, &y, 0, &olen), FAB_ERR_SPACE);
    fab_plan_t q = p;
    q.which = FAB_PLAN_OPEN;
    EQ(fab_assemble(&r, &q, 0, &x, 0, &olen), FAB_ERR_ARG);
    q = p;
    q.lit_total = 3;
    EQ(fab_assemble(&r, &q, 0, &x, 0, &olen), FAB_ERR_ARG);
    fab_recipe_t u = r;
    u.checked = false;
    EQ(fab_assemble(&u, &p, 0, &x, 0, &olen), FAB_ERR_SIGN);
    EQ(olen, 7); /* untouched by refused arguments */
    EQ(fab_assemble(&r, &p, 0, &x, 0, &olen), FAB_OK);
    EQ(olen, 8);
    /* exactly enough room */
    y = x;
    y.out_cap = 8;
    y.diff_cap = 4;
    EQ(fab_assemble(&r, &p, 0, &y, 0, &olen), FAB_OK);

    /* streams and checkpoints */
    static fab_stream_t s;
    memset(&s, 0, sizeof s);
    s.live = true;
    EQ(fab_frame_apply(0, frame, 100, 0, 0, planb, sizeof planb, &x, &olen), FAB_ERR_ARG);
    EQ(fab_frame_apply(&s, 0, 100, 0, 0, planb, sizeof planb, &x, &olen), FAB_ERR_ARG);
    EQ(fab_frame_apply(&s, frame, 100, 0, 0, 0, sizeof planb, &x, &olen), FAB_ERR_ARG);
    EQ(fab_frame_apply(&s, frame, 100, 0, 0, planb, sizeof planb, &x, 0), FAB_ERR_ARG);
    EQ(fab_frame_apply(&s, frame, 100, 0, 0, planb, sizeof planb, 0, &olen), FAB_ERR_ARG);
    y = x;
    y.out = 0;
    EQ(fab_frame_apply(&s, frame, 100, 0, 0, planb, sizeof planb, &y, &olen), FAB_ERR_ARG);
    EQ(fab_checkpoint_verify(0, cp, FAB_CP_LEN), FAB_ERR_ARG);
    EQ(fab_checkpoint_verify(&s, 0, FAB_CP_LEN), FAB_ERR_ARG);
    EQ(fab_checkpoint_sign(0, 0, 0, ssk, 0, cp), FAB_ERR_ARG);
    EQ(fab_checkpoint_sign(&s, 0, 0, 0, 0, cp), FAB_ERR_ARG);
    EQ(fab_checkpoint_sign(&s, 0, 0, ssk, 0, 0), FAB_ERR_ARG);

    /* sending side */
    EQ(fab_recipe_seal(0, hp, n, (const uint8_t *) "abcd", 4, 0, rec2, sizeof rec2, &len),
       FAB_ERR_ARG);
    EQ(fab_recipe_seal(&a, 0, n, (const uint8_t *) "abcd", 4, 0, rec2, sizeof rec2, &len),
       FAB_ERR_ARG);
    EQ(fab_recipe_seal(&a, hp, n, 0, 4, 0, rec2, sizeof rec2, &len), FAB_ERR_ARG);
    EQ(fab_recipe_seal(&a, hp, n, (const uint8_t *) "abcd", 4, 0, 0, sizeof rec2, &len),
       FAB_ERR_ARG);
    EQ(fab_recipe_seal(&a, hp, n, (const uint8_t *) "abcd", 4, 0, rec2, sizeof rec2, 0),
       FAB_ERR_ARG);
    fab_seal_args_t b = a;
    b.sender_pk = 0;
    EQ(fab_recipe_seal(&b, hp, n, (const uint8_t *) "abcd", 4, 0, rec2, sizeof rec2, &len),
       FAB_ERR_ARG);
    b = a;
    b.sender_sk = 0;
    EQ(fab_recipe_seal(&b, hp, n, (const uint8_t *) "abcd", 4, 0, rec2, sizeof rec2, &len),
       FAB_ERR_ARG);
    b = a;
    b.kind = 2;
    EQ(fab_recipe_seal(&b, hp, n, (const uint8_t *) "abcd", 4, 0, rec2, sizeof rec2, &len),
       FAB_ERR_ARG);
    b = a;
    b.mode = 2;
    EQ(fab_recipe_seal(&b, hp, n, (const uint8_t *) "abcd", 4, 0, rec2, sizeof rec2, &len),
       FAB_ERR_ARG);
    b = args(FAB_KIND_FILE, FAB_MODE_SEALED);
    b.kem_coins = 0;
    EQ(fab_recipe_seal(&b, hp, n, (const uint8_t *) "abcd", 4, 0, rec2, sizeof rec2, &len),
       FAB_ERR_ARG);
    /* exactly enough room, and one byte less */
    uint32_t want = FAB_HDR + n + 4 + FAB_SIG_PK + FAB_SIG;
    EQ(fab_recipe_seal(&a, hp, n, (const uint8_t *) "abcd", 4, 0, rec2, want, &len), FAB_OK);
    EQ(fab_recipe_seal(&a, hp, n, (const uint8_t *) "abcd", 4, 0, rec2, want - 1, &len),
       FAB_ERR_SPACE);
    /* an empty diff needs no pointer */
    uint32_t pl8 = hand_plan();
    op(hp + 156, FAB_OP_SLICE, 1, 0, 4);
    memcpy(target + 4, blk[2], 4);
    {
        ipfsn_cid_t tc;
        fab_target_cid(target, 8, &W, &tc);
        ipfsn_cid_encode(&tc, hp, 36);
    }
    EQ(fab_recipe_seal(&a, hp, pl8, 0, 0, 0, rec2, sizeof rec2, &len), FAB_OK);
    EQ(fab_recipe_parse(rec2, len, &r), FAB_OK);
    EQ(fab_recipe_check(&r, 0, 0), FAB_OK);
    EQ(fab_open_plan(&r, 0, planb, sizeof planb, &p), FAB_OK);
    y = x;
    y.diff_buf = 0;
    y.diff_cap = 0;
    store_reset();
    EQ(fab_assemble(&r, &p, 0, &y, 0, &olen), FAB_OK);
    CHECK(memcmp(out, target, 8) == 0);

    /* plan writer */
    ipfsn_cid_t tc;
    fab_target_cid(target, 8, &W, &tc);
    EQ(fab_plan_write(0, 8, 0, srcs, NB, opsb, 1, planb, sizeof planb, &l), FAB_ERR_ARG);
    EQ(fab_plan_write(&tc, 8, 0, 0, NB, opsb, 1, planb, sizeof planb, &l), FAB_ERR_ARG);
    EQ(fab_plan_write(&tc, 8, 0, srcs, NB, 0, 1, planb, sizeof planb, &l), FAB_ERR_ARG);
    EQ(fab_plan_write(&tc, 8, 0, srcs, NB, opsb, 1, 0, sizeof planb, &l), FAB_ERR_ARG);
    EQ(fab_plan_write(&tc, 8, 0, srcs, NB, opsb, 1, planb, sizeof planb, 0), FAB_ERR_ARG);
    EQ(fab_plan_write(&tc, 8, 0, 0, 0, 0, 0, planb, sizeof planb, &l), FAB_OK);
    EQ(l, FAB_PLAN_HDR);
    uint32_t pw = FAB_PLAN_HDR + NB * FAB_BLOCK_REC + 2 * FAB_OP_REC;
    EQ(fab_plan_write(&tc, 8, 0, srcs, NB, opsb, 2, planb, pw, &l), FAB_OK);
    EQ(l, pw);
    EQ(fab_plan_write(&tc, 8, 0, srcs, NB, opsb, 2, planb, pw - 1, &l), FAB_ERR_SPACE);
    EQ(fab_plan_write(&tc, 8, 0, srcs, NB, opsb, FAB_MAX_OPS + 1u, planb, sizeof planb, &l),
       FAB_ERR_ARG);
    /* the writer's bytes */
    memset(planb, 0xEE, pw);
    EQ(fab_plan_write(&tc, 0x0102030405ull, 0x0A0B0C0Du, srcs, NB, opsb, 2, planb, pw, &l), FAB_OK);
    uint8_t cb[36];
    ipfsn_cid_encode(&tc, cb, 36);
    CHECK(memcmp(planb, cb, 36) == 0);
    CHECK(memcmp(planb + 36, "\0\0\0\x01\x02\x03\x04\x05", 8) == 0);
    EQ(fab__get32(planb + 44), NB);
    EQ(fab__get32(planb + 48), 2);
    CHECK(memcmp(planb + 52, "\x0A\x0B\x0C\x0D", 4) == 0);
    CHECK(all_zero(planb + 56, 8));
    for (int i = 0; i < NB; i++) {
        ipfsn_cid_encode(&bcid[i], cb, 36);
        CHECK(memcmp(planb + 64 + 40 * i, cb, 36) == 0);
        EQ(fab__get32(planb + 64 + 40 * i + 36), blen[i]);
    }
    CHECK(memcmp(planb + 64 + 40 * NB, opsb, 24) == 0);
}

/* ---- plan edges ----------------------------------------------------------------- */

static uint8_t bigplan[FAB_PLAN_HDR + (FAB_MAX_OPS + 1u) * FAB_OP_REC];

static uint32_t many_blocks(uint32_t nb)
{
    ipfsn_cid_t c;
    memset(bigplan, 0, FAB_PLAN_HDR);
    fab_target_cid(0, 0, &W, &c);
    ipfsn_cid_encode(&c, bigplan, 36);
    put32(bigplan + 44, nb);
    for (uint32_t i = 0; i < nb; i++) {
        uint8_t v[4];
        put32(v, i);
        ipfsn_cid_sha256(IPFSN_MC_RAW, v, 4, &c);
        ipfsn_cid_encode(&c, bigplan + 64 + i * 40, 36);
        put32(bigplan + 64 + i * 40 + 36, 4);
    }
    return FAB_PLAN_HDR + nb * FAB_BLOCK_REC;
}

static uint32_t many_ops(uint32_t no)
{
    ipfsn_cid_t c;
    memset(bigplan, 0, FAB_PLAN_HDR);
    fab_target_cid(0, 0, &W, &c);
    ipfsn_cid_encode(&c, bigplan, 36);
    bigplan[36 + 5] = (uint8_t) (no >> 16);
    bigplan[36 + 6] = (uint8_t) (no >> 8);
    bigplan[36 + 7] = (uint8_t) no;
    put32(bigplan + 48, no);
    for (uint32_t i = 0; i < no; i++) op(bigplan + 64 + i * 12, FAB_OP_LIT, 0, 0, 1);
    return FAB_PLAN_HDR + no * FAB_OP_REC;
}

/* blocks b0 (len 300) and b1 (len 300), ops as given: a scratch plan */
static uint8_t sp[FAB_PLAN_HDR + 3 * FAB_BLOCK_REC + 5 * FAB_OP_REC];
static uint32_t scratch(uint32_t nb, uint32_t no, uint64_t tlen)
{
    memset(sp, 0, sizeof sp);
    ipfsn_cid_t c;
    fab_target_cid((const uint8_t *) "x", 1, &W, &c);
    ipfsn_cid_encode(&c, sp, 36);
    sp[42] = (uint8_t) (tlen >> 8);
    sp[43] = (uint8_t) tlen;
    put32(sp + 44, nb);
    put32(sp + 48, no);
    for (uint32_t i = 0; i < nb; i++) {
        ipfsn_cid_encode(&bcid[i == 0 ? 2 : i == 1 ? 1 : 3], sp + 64 + 40 * i, 36);
        put32(sp + 64 + 40 * i + 36, 300);
    }
    return FAB_PLAN_HDR + nb * FAB_BLOCK_REC + no * FAB_OP_REC;
}
#define SOP(i, nb) (sp + 64 + 40 * (nb) + 12 * (i))

static void test_plan_edges(void)
{
    fab_plan_t p;
    uint32_t n = many_blocks(FAB_MAX_BLOCKS);
    EQ(fab_plan_parse(bigplan, n, FAB_PLAN_FILE, 0, &p), FAB_OK);
    EQ(p.n_blocks, FAB_MAX_BLOCKS);
    n = many_blocks(FAB_MAX_BLOCKS + 1u);
    EQ(fab_plan_parse(bigplan, n, FAB_PLAN_FILE, 0, &p), FAB_ERR_PLAN);
    n = many_ops(FAB_MAX_OPS);
    EQ(fab_plan_parse(bigplan, n, FAB_PLAN_FILE, FAB_MAX_OPS, &p), FAB_OK);
    EQ(p.target_len, FAB_MAX_OPS);
    n = many_ops(FAB_MAX_OPS + 1u);
    EQ(fab_plan_parse(bigplan, n, FAB_PLAN_FILE, FAB_MAX_OPS + 1u, &p), FAB_ERR_PLAN);

    /* a plan of exactly its header: the empty file */
    n = many_blocks(0);
    EQ(fab_plan_parse(bigplan, n, FAB_PLAN_FILE, 0, &p), FAB_OK);
    EQ(p.n_ops, 0);
    /* shorter than the header, from an exact-size buffer */
    static uint8_t short63[63];
    memcpy(short63, bigplan, 63);
    EQ(fab_plan_parse(short63, 63, FAB_PLAN_FILE, 0, &p), FAB_ERR_PLAN);

    /* the first block is checked like the others */
    n = scratch(2, 1, 4);
    op(SOP(0, 2), FAB_OP_SLICE, 1, 0, 4);
    EQ(fab_plan_parse(sp, n, FAB_PLAN_FILE, 0, &p), FAB_OK);
    put32(sp + 64 + 36, 0);
    EQ(fab_plan_parse(sp, n, FAB_PLAN_FILE, 0, &p), FAB_ERR_PLAN);
    n = scratch(2, 1, 4);
    op(SOP(0, 2), FAB_OP_SLICE, 1, 0, 4);
    sp[64] = 0; /* block 0's CID */
    EQ(fab_plan_parse(sp, n, FAB_PLAN_FILE, 0, &p), FAB_ERR_PLAN);
    /* duplicates found at any distance */
    n = scratch(3, 1, 4);
    op(SOP(0, 3), FAB_OP_SLICE, 0, 0, 4);
    EQ(fab_plan_parse(sp, n, FAB_PLAN_FILE, 0, &p), FAB_OK);
    memcpy(sp + 64 + 80, sp + 64 + 40, 40); /* block 2 = block 1 */
    EQ(fab_plan_parse(sp, n, FAB_PLAN_FILE, 0, &p), FAB_ERR_PLAN);
    /* ops of length 1 are fine, a zero-length op is not even when sums agree */
    n = scratch(1, 3, 3);
    op(SOP(0, 1), FAB_OP_SLICE, 0, 0, 1);
    op(SOP(1, 1), FAB_OP_LIT, 0, 0, 1);
    op(SOP(2, 1), FAB_OP_SLICE, 0, 299, 1);
    EQ(fab_plan_parse(sp, n, FAB_PLAN_FILE, 1, &p), FAB_OK);
    op(SOP(1, 1), FAB_OP_LIT, 0, 0, 0);
    sp[43] = 2;
    EQ(fab_plan_parse(sp, n, FAB_PLAN_FILE, 0, &p), FAB_ERR_PLAN);
    /* a SLICE naming one block past the list reads no record from the ops */
    n = scratch(2, 4, 12);
    op(SOP(0, 2), FAB_OP_SLICE, 2, 0, 4);
    op(SOP(1, 2), FAB_OP_LIT, 0, 0, 4);
    op(SOP(2, 2), FAB_OP_SLICE, 0, 0, 2);
    op(SOP(3, 2), FAB_OP_LIT, 0, 0, 2);
    EQ(fab_plan_parse(sp, n, FAB_PLAN_FILE, 6, &p), FAB_ERR_PLAN);
    op(SOP(0, 2), FAB_OP_SLICE, 1, 0, 4);
    EQ(fab_plan_parse(sp, n, FAB_PLAN_FILE, 6, &p), FAB_OK);
    EQ(p.lit_total, 6);
    /* prev_need is the furthest byte, not the last op's */
    n = scratch(0, 3, 12);
    op(SOP(0, 0), FAB_OP_PREV, 0, 10, 4);
    op(SOP(1, 0), FAB_OP_PREV, 0, 0, 4);
    op(SOP(2, 0), FAB_OP_PREV, 0, 3, 4);
    EQ(fab_plan_parse(sp, n, FAB_PLAN_FRAME, 0, &p), FAB_OK);
    EQ(p.prev_need, 14);
    EQ(p.lit_total, 0);
    op(SOP(0, 0), FAB_OP_PREV, 0, 0, 4);
    op(SOP(1, 0), FAB_OP_PREV, 0, 20, 4);
    EQ(fab_plan_parse(sp, n, FAB_PLAN_FRAME, 0, &p), FAB_OK);
    EQ(p.prev_need, 24);
}

/* ---- recipe edges ------------------------------------------------------------------- */

static void test_recipe_edges(void)
{
    uint32_t n = hand_plan(), len = 0, olen = 0;
    fab_seal_args_t a = args(FAB_KIND_FILE, FAB_MODE_SEALED);
    EQ(fab_recipe_seal(&a, hp, n, (const uint8_t *) "abcd", 4, 0, rec, sizeof rec, &len), FAB_OK);
    fab_recipe_t r;
    /* sealed needs a recipient id */
    memcpy(rec2, rec, len);
    memset(rec2 + 40, 0, 32);
    EQ(fab_recipe_parse(rec2, len, &r), FAB_ERR_FORM);
    rec2[71] = 1;
    EQ(fab_recipe_parse(rec2, len, &r), FAB_OK);
    /* shorter than a header, from exact-size buffers */
    static uint8_t h79[79], h80[80];
    memcpy(h79, rec, 79);
    EQ(fab_recipe_parse(h79, 79, &r), FAB_ERR_FORM);
    memcpy(h80, rec, 80);
    EQ(fab_recipe_parse(h80, 80, &r), FAB_ERR_FORM);
    /* a plan of exactly its header parses */
    uint32_t pl = many_blocks(0);
    fab_seal_args_t b = args(FAB_KIND_FILE, FAB_MODE_PUBLIC);
    EQ(fab_recipe_seal(&b, bigplan, pl, 0, 0, 0, rec2, sizeof rec2, &len), FAB_OK);
    EQ(fab_recipe_parse(rec2, len, &r), FAB_OK);
    EQ(r.plan_len, FAB_PLAN_HDR);
    EQ(fab_recipe_check(&r, 0, 0), FAB_OK);
    fab_plan_t p;
    EQ(fab_open_plan(&r, 0, planb, sizeof planb, &p), FAB_OK);
    fab_io_t x = io();
    EQ(fab_assemble(&r, &p, 0, &x, 0, &olen), FAB_OK);
    EQ(olen, 0);

    /* a valid signature by someone else over the sender's id is not the sender's */
    EQ(fab_recipe_seal(&b, hp, n, (const uint8_t *) "abcd", 4, 0, rec, sizeof rec, &len), FAB_OK);
    memcpy(rec2, rec, len);
    uint32_t o = len - FAB_SIG - FAB_SIG_PK;
    memcpy(rec2 + o, opk, FAB_SIG_PK);
    uint8_t h[32];
    sha256(rec2, o, h);
    pqm_mldsa87_sign(osk, h, 32, (const uint8_t *) FAB_RECIPE_CTX, sizeof FAB_RECIPE_CTX - 1u, 0,
                     rec2 + o + FAB_SIG_PK);
    EQ(fab_recipe_parse(rec2, len, &r), FAB_OK);
    EQ(fab_recipe_check(&r, 0, 0), FAB_ERR_SIGN);
    CHECK(!r.checked);
    /* the trust list: the only entry, the first of two, the second of two */
    uint8_t ids[2][FAB_ID];
    fab_key_id(spk, FAB_SIG_PK, ids[0]);
    fab_key_id(opk, FAB_SIG_PK, ids[1]);
    EQ(fab_recipe_parse(rec, len, &r), FAB_OK);
    EQ(fab_recipe_check(&r, (const uint8_t(*)[FAB_ID]) ids, 1), FAB_OK);
    EQ(fab_recipe_check(&r, (const uint8_t(*)[FAB_ID]) ids, 2), FAB_OK);
    EQ(fab_recipe_check(&r, (const uint8_t(*)[FAB_ID]) ids + 1, 1), FAB_ERR_TRUST);
    EQ(fab_recipe_check(&r, (const uint8_t(*)[FAB_ID]) ids, 0), FAB_ERR_TRUST);

    /* a plan that fails its rules is wiped from the caller's buffer */
    memcpy(rec2, rec, len);
    rec2[FAB_HDR + 56] = 1; /* plan reserved byte */
    resign(rec2, len);
    EQ(fab_recipe_parse(rec2, len, &r), FAB_OK);
    EQ(fab_recipe_check(&r, 0, 0), FAB_OK);
    memset(planb, 0x5A, n);
    EQ(fab_open_plan(&r, 0, planb, sizeof planb, &p), FAB_ERR_PLAN);
    CHECK(all_zero(planb, n));
    /* and a sealed one too, after decryption */
    a = args(FAB_KIND_FILE, FAB_MODE_SEALED);
    EQ(fab_recipe_seal(&a, hp, n, (const uint8_t *) "abcd", 4, 0, rec, sizeof rec, &len), FAB_OK);
    EQ(fab_recipe_parse(rec, len, &r), FAB_OK);
    EQ(fab_recipe_check(&r, 0, 0), FAB_OK);
    memset(planb, 0x5A, n);
    EQ(fab_open_plan(&r, rdk, planb, n, &p), FAB_OK); /* exactly enough room */
    CHECK(memcmp(planb, hp, n) == 0);
    /* the right key to open, the wrong one to assemble */
    store_reset();
    EQ(fab_hydrate(&p, &x, 0), FAB_OK);
    EQ(fab_assemble(&r, &p, xdk, &x, 0, &olen), FAB_ERR_KEY);
    EQ(fab_assemble(&r, &p, 0, &x, 0, &olen), FAB_ERR_ARG);
    EQ(fab_assemble(&r, &p, rdk, &x, 0, &olen), FAB_OK);
}

/* ---- block cache and literals ------------------------------------------------------- */

static void test_cache(void)
{
    /* slice, literal, slice of the same block: one fetch during assembly */
    uint32_t tl = 0, dl = 0, len = 0, olen = 0;
    memcpy(target, blk[0], 1000);
    tl = 1000;
    memcpy(target + tl, "~~LITERAL ONE~~", 15);
    tl += 15;
    memcpy(target + tl, blk[0] + 30000, 2000);
    tl += 2000;
    memcpy(target + tl, "~~LITERAL TWO, LONGER~~", 23);
    tl += 23;
    memcpy(target + tl, blk[0] + 60000, 900);
    tl += 900;
    uint32_t pl = plan_for(tl, NB, 0, &dl);
    EQ(dl, 38);
    fab_seal_args_t a = args(FAB_KIND_FILE, FAB_MODE_PUBLIC);
    EQ(fab_recipe_seal(&a, planb, pl, tdiff, dl, 0, rec, sizeof rec, &len), FAB_OK);
    fab_recipe_t r;
    fab_plan_t p;
    fab_io_t x = io();
    EQ(fab_recipe_parse(rec, len, &r), FAB_OK);
    EQ(fab_recipe_check(&r, 0, 0), FAB_OK);
    EQ(fab_open_plan(&r, 0, planb, sizeof planb, &p), FAB_OK);
    EQ(p.n_ops, 5);
    store_reset();
    EQ(fab_hydrate(&p, &x, 0), FAB_OK);
    st.fetches = 0;
    EQ(fab_assemble(&r, &p, 0, &x, 0, &olen), FAB_OK);
    EQ(st.fetches, 1);
    EQ(olen, tl);
    CHECK(memcmp(out, target, tl) == 0); /* two literals, each from its own place */

    /* a new call forgets the cached block: block 2 after block 3 */
    memcpy(target, blk[3], 5000);
    pl = plan_for(5000, NB, 0, &dl);
    EQ(fab_recipe_seal(&a, planb, pl, tdiff, dl, 0, rec, sizeof rec, &len), FAB_OK);
    store_reset();
    EQ(run(rec, len, 0, &x, &olen, 0), FAB_OK);
    EQ(W.loaded, 3);
    memcpy(target, blk[2], 300);
    pl = plan_for(300, NB, 0, &dl);
    EQ(dl, 0);
    EQ(fab_recipe_seal(&a, planb, pl, tdiff, dl, 0, rec, sizeof rec, &len), FAB_OK);
    store_reset();
    EQ(run(rec, len, 0, &x, &olen, 0), FAB_OK);
    CHECK(memcmp(out, blk[2], 300) == 0);
    W.loaded = 2; /* as if block 2 were cached, but the buffer holds block 3 */
    fab_recipe_parse(rec, len, &r);
    fab_recipe_check(&r, 0, 0);
    fab_open_plan(&r, 0, planb, sizeof planb, &p);
    memcpy(W.block, blk[3], 300);
    EQ(fab_assemble(&r, &p, 0, &x, 0, &olen), FAB_OK); /* assembly does not trust it */
    CHECK(memcmp(out, blk[2], 300) == 0);
}

/* ---- the builder at its edges ---------------------------------------------------------- */

static uint32_t hash32(const uint8_t *p, uint32_t w)
{
    uint32_t h = 0;
    for (uint32_t i = 0; i < w; i++) h = h * 0x01000193u + p[i];
    return h;
}

/* window i of a fixed xorshift sequence */
static void rwin(uint8_t w[32], uint32_t i)
{
    uint32_t x = i * 2654435761u + 0x9e3779b9u;
    for (uint32_t k = 0; k < 32; k++) {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        w[k] = (uint8_t) (x >> 24);
    }
}

typedef struct {
    uint32_t h, i;
} hp_t;
static int cmp_hp(const void *a, const void *b)
{
    const hp_t *x = a, *y = b;
    return x->h < y->h ? -1 : x->h > y->h ? 1 : (x->i < y->i ? -1 : x->i > y->i);
}

static uint8_t w32[8][32], tw[32], bigsrc[4096], collide_a[32], collide_b[32];
static uint8_t ffops[FAB_OP_REC * 64];

static void test_build_edges(void)
{
    uint32_t no = 0, dl = 0;
    /* a block of exactly one window; the target is that block */
    fill(w32[0], 32);
    fab_src_t one = {w32[0], 32, bcid[0], false};
    memcpy(tw, w32[0], 32);
    EQ(fab_build(&one, 1, tw, 32, 32, table, 14, opsb, 64, &no, tdiff, OUTCAP, &dl), FAB_OK);
    EQ(no, 1);
    EQ(dl, 0);
    EQ(opsb[0], FAB_OP_SLICE);
    EQ(fab__get32(opsb + 8), 32);
    /* eight one-window blocks, in another order: eight slices and no diff */
    fab_src_t eight[8];
    static uint8_t t8[256];
    for (int i = 0; i < 8; i++) {
        fill(w32[i], 32);
        eight[i].data = w32[i];
        eight[i].len = 32;
        eight[i].cid = bcid[0];
        eight[i].prev = false;
    }
    static const int order[8] = {5, 2, 7, 0, 3, 6, 1, 4};
    for (int i = 0; i < 8; i++) memcpy(t8 + 32 * i, w32[order[i]], 32);
    EQ(fab_build(eight, 8, t8, 256, 32, table, 4, opsb, 64, &no, tdiff, OUTCAP, &dl), FAB_OK);
    EQ(no, 8);
    EQ(dl, 0);
    for (int i = 0; i < 8; i++) {
        EQ(opsb[12 * i], FAB_OP_SLICE);
        EQ(opsb[12 * i + 3], order[i]);
        EQ(fab__get32(opsb + 12 * i + 4), 0);
    }
    /* the first window of a long block survives later windows in a small table */
    fill(bigsrc, sizeof bigsrc);
    fab_src_t big = {bigsrc, sizeof bigsrc, bcid[0], false};
    memcpy(tw, bigsrc, 32);
    EQ(fab_build(&big, 1, tw, 32, 32, table, 4, opsb, 64, &no, tdiff, OUTCAP, &dl), FAB_OK);
    EQ(no, 1);
    EQ(dl, 0);
    /* a table full of garbage from a bigger build, then one source */
    for (uint32_t i = 0; i < (1u << 14); i++) {
        table[i].hash = (uint32_t) i * 2654435761u;
        table[i].off = 1u << 30;
        table[i].src = 0xFFFF;
        table[i].used = 1;
    }
    EQ(fab_build(&one, 1, tw, 32, 32, table, 14, opsb, 64, &no, tdiff, OUTCAP, &dl), FAB_OK);
    EQ(dl, 32); /* tw is now bigsrc's window: not in `one` */
    memcpy(tw, w32[0], 32);
    EQ(fab_build(&one, 1, tw, 32, 32, table, 14, opsb, 64, &no, tdiff, OUTCAP, &dl), FAB_OK);
    EQ(dl, 0);
    EQ(fab_build(srcs, NB, blk[1], 4096, 32, table, 14, opsb, 64, &no, tdiff, OUTCAP, &dl), FAB_OK);
    EQ(fab_build(&one, 1, blk[3], 4096, 32, table, 14, opsb, 64, &no, tdiff, OUTCAP, &dl), FAB_OK);
    EQ(dl, 4096); /* stale entries for source 3 are not looked at */

    /* a hash collision: equal hashes, different bytes, no match */
    {
        static hp_t v[300000];
        uint32_t found = 0;
        /* random whole windows: about ten equal pairs are expected in 300000 */
        for (uint32_t i = 0; i < 300000; i++) {
            uint8_t w[32];
            rwin(w, i);
            v[i].h = hash32(w, 32);
            v[i].i = i;
        }
        qsort(v, 300000, sizeof v[0], cmp_hp);
        for (uint32_t i = 1; i < 300000 && !found; i++)
            if (v[i].h == v[i - 1].h) {
                rwin(collide_a, v[i - 1].i);
                rwin(collide_b, v[i].i);
                found = 1;
            }
        CHECK(found);
        EQ(hash32(collide_a, 32), hash32(collide_b, 32));
        CHECK(memcmp(collide_a, collide_b, 32) != 0);
        fab_src_t ca = {collide_a, 32, bcid[0], false};
        EQ(fab_build(&ca, 1, collide_b, 32, 32, table, 14, opsb, 64, &no, tdiff, OUTCAP, &dl),
           FAB_OK);
        EQ(no, 1);
        EQ(dl, 32);
        EQ(opsb[0], FAB_OP_LIT);
    }

    /* a match stops at the end of the target, even when the next byte agrees */
    static uint8_t t65[65];
    memcpy(t65, bigsrc, 65);
    EQ(fab_build(&big, 1, t65, 64, 32, table, 14, opsb, 64, &no, tdiff, OUTCAP, &dl), FAB_OK);
    EQ(no, 1);
    EQ(fab__get32(opsb + 8), 64);
    /* and at the end of the source (exact-size arrays) */
    static uint8_t s64[64], t96[96];
    memcpy(s64, bigsrc, 64);
    memcpy(t96, bigsrc, 96);
    fab_src_t sb = {s64, 64, bcid[0], false};
    EQ(fab_build(&sb, 1, t96, 96, 32, table, 14, opsb, 64, &no, tdiff, OUTCAP, &dl), FAB_OK);
    EQ(no, 2);
    EQ(fab__get32(opsb + 8), 64);
    EQ(dl, 32);
    /* backward extension stops at the start of the source */
    memcpy(t96, bigsrc + 8, 88);
    memcpy(t96 + 88, "12345678", 8);
    EQ(fab_build(&sb, 1, t96, 96, 32, table, 14, opsb, 64, &no, tdiff, OUTCAP, &dl), FAB_OK);
    EQ(fab__get32(opsb + 4), 8);
    EQ(fab__get32(opsb + 8), 56);
    /* random target in an exact-size array: rolling never reads past it */
    static uint8_t t100[100];
    fill(t100, 100);
    EQ(fab_build(&big, 1, t100, 100, 32, table, 14, opsb, 64, &no, tdiff, OUTCAP, &dl), FAB_OK);
    EQ(dl, 100);

    /* the longest candidate wins; on a tie, the earlier source */
    static uint8_t sa[96], sb2[96], tt[80];
    fill(sa, 96);
    memcpy(sb2, sa, 32);
    fill(sb2 + 32, 64);
    memcpy(tt, sa, 80);
    fab_src_t two[2] = {{sa, 96, bcid[0], false}, {sb2, 96, bcid[1], false}};
    EQ(fab_build(two, 2, tt, 80, 32, table, 14, opsb, 64, &no, tdiff, OUTCAP, &dl), FAB_OK);
    EQ(no, 1);
    EQ(opsb[3], 0);
    EQ(fab__get32(opsb + 8), 80);
    two[0].data = sb2; /* now the earlier source is the shorter one */
    two[1].data = sa;
    EQ(fab_build(two, 2, tt, 80, 32, table, 14, opsb, 64, &no, tdiff, OUTCAP, &dl), FAB_OK);
    EQ(no, 1);
    EQ(opsb[3], 1);
    two[0].data = sa;
    EQ(fab_build(two, 2, tt, 80, 32, table, 14, opsb, 64, &no, tdiff, OUTCAP, &dl), FAB_OK);
    EQ(opsb[3], 0); /* identical sources: the first */
    /* a longer match through backward extension beats a longer forward one */
    static uint8_t sc[128], sd[128], tc2[100];
    fill(sc, 128);
    memcpy(sd, sc + 32, 32); /* sd: window X then other bytes */
    fill(sd + 32, 96);
    memcpy(sd + 32, sc + 64, 8); /* sd extends 8 bytes forward */
    memcpy(tc2, sc + 8, 100);    /* the target: sc from 8 */
    fab_src_t cd[2] = {{sd, 128, bcid[0], false}, {sc, 128, bcid[1], false}};
    EQ(fab_build(cd, 2, tc2, 100, 32, table, 14, opsb, 64, &no, tdiff, OUTCAP, &dl), FAB_OK);
    EQ(no, 1);
    EQ(opsb[3], 1);
    EQ(fab__get32(opsb + 4), 8);
    EQ(fab__get32(opsb + 8), 100);

    /* every op byte is written: a dirty ops buffer still gives a valid plan */
    memset(ffops, 0xFF, sizeof ffops);
    uint32_t tl = make_target();
    EQ(fab_build(srcs, NB, target, tl, 32, table, 14, ffops, 64, &no, tdiff, OUTCAP, &dl), FAB_OK);
    for (uint32_t i = 0; i < no; i++) EQ(ffops[12 * i + 1], 0);
    for (uint32_t i = 0; i < no; i++)
        if (ffops[12 * i] == FAB_OP_LIT)
            CHECK(ffops[12 * i + 2] == 0 && ffops[12 * i + 3] == 0 &&
                  fab__get32(ffops + 12 * i + 4) == 0);
    /* exactly enough ops and diff, and one less of each */
    uint32_t need = no, dneed = dl;
    EQ(fab_build(srcs, NB, target, tl, 32, table, 14, opsb, need, &no, tdiff, dneed, &dl), FAB_OK);
    EQ(fab_build(srcs, NB, target, tl, 32, table, 14, opsb, need - 1, &no, tdiff, OUTCAP, &dl),
       FAB_ERR_SPACE);
    EQ(fab_build(srcs, NB, target, tl, 32, table, 14, opsb, need, &no, tdiff, dneed - 1, &dl),
       FAB_ERR_SPACE);
    /* two literal runs, the second overflowing by one */
    memcpy(target, "AAAA", 4);
    memcpy(target + 4, blk[0], 500);
    memcpy(target + 504, "BBBBBB", 6);
    EQ(fab_build(srcs, NB, target, 510, 32, table, 14, opsb, 64, &no, tdiff, 10, &dl), FAB_OK);
    EQ(dl, 10);
    CHECK(memcmp(tdiff, "AAAABBBBBB", 10) == 0);
    EQ(fab_build(srcs, NB, target, 510, 32, table, 14, opsb, 64, &no, tdiff, 9, &dl),
       FAB_ERR_SPACE);
    EQ(fab_build(srcs, NB, target, 510, 32, table, 14, opsb, 64, &no, tdiff, 3, &dl),
       FAB_ERR_SPACE);

    /* block numbers above 255 */
    static fab_src_t many[300];
    static uint8_t mb[300][32];
    for (int i = 0; i < 300; i++) {
        fill(mb[i], 32);
        many[i].data = mb[i];
        many[i].len = 32;
        many[i].cid = bcid[0];
        many[i].prev = false;
    }
    memcpy(tw, mb[298], 32);
    EQ(fab_build(many, 300, tw, 32, 32, table, 14, opsb, 64, &no, tdiff, OUTCAP, &dl), FAB_OK);
    EQ(no, 1);
    EQ(opsb[2], 1);
    EQ(opsb[3], 298 - 256);

    /* source rules */
    static fab_src_t lots[FAB_MAX_BLOCKS + 1];
    for (uint32_t i = 0; i <= FAB_MAX_BLOCKS; i++) {
        lots[i].data = w32[i % 8];
        lots[i].len = 32;
        lots[i].cid = bcid[0];
        lots[i].prev = false;
    }
    memcpy(tw, w32[3], 32);
    EQ(fab_build(lots, FAB_MAX_BLOCKS, tw, 32, 32, table, 14, opsb, 64, &no, tdiff, OUTCAP, &dl),
       FAB_OK);
    EQ(fab_build(lots, FAB_MAX_BLOCKS + 1, tw, 32, 32, table, 14, opsb, 64, &no, tdiff, OUTCAP,
                 &dl),
       FAB_ERR_ARG);
    lots[FAB_MAX_BLOCKS].prev = true; /* 4096 blocks plus the previous frame */
    EQ(fab_build(lots, FAB_MAX_BLOCKS + 1, tw, 32, 32, table, 14, opsb, 64, &no, tdiff, OUTCAP,
                 &dl),
       FAB_OK);
    fab_src_t nul = {0, 32, bcid[0], false};
    EQ(fab_build(&nul, 1, tw, 32, 32, table, 14, opsb, 64, &no, tdiff, OUTCAP, &dl), FAB_ERR_ARG);
    static uint8_t maxb[FAB_BLOCK_MAX];
    fab_src_t mx = {maxb, FAB_BLOCK_MAX, bcid[0], false};
    EQ(fab_build(&mx, 1, tw, 32, 32, table, 14, opsb, 64, &no, tdiff, OUTCAP, &dl), FAB_OK);
    EQ(fab_build(&one, 1, tw, 32, 32, table, 4, opsb, 64, &no, tdiff, OUTCAP, &dl), FAB_OK);
    EQ(fab_build(0, 1, tw, 32, 32, table, 14, opsb, 64, &no, tdiff, OUTCAP, &dl), FAB_ERR_ARG);
    EQ(fab_build(&one, 1, 0, 32, 32, table, 14, opsb, 64, &no, tdiff, OUTCAP, &dl), FAB_ERR_ARG);
    EQ(fab_build(&one, 1, tw, 32, 32, 0, 14, opsb, 64, &no, tdiff, OUTCAP, &dl), FAB_ERR_ARG);
    EQ(fab_build(&one, 1, tw, 32, 32, table, 14, 0, 64, &no, tdiff, OUTCAP, &dl), FAB_ERR_ARG);
    EQ(fab_build(&one, 1, tw, 32, 32, table, 14, opsb, 64, 0, tdiff, OUTCAP, &dl), FAB_ERR_ARG);
    EQ(fab_build(&one, 1, tw, 32, 32, table, 14, opsb, 64, &no, 0, OUTCAP, &dl), FAB_ERR_ARG);
    EQ(fab_build(&one, 1, tw, 32, 32, table, 14, opsb, 64, &no, tdiff, OUTCAP, 0), FAB_ERR_ARG);
    EQ(fab_build(0, 0, 0, 0, 32, table, 14, opsb, 64, &no, 0, 0, &dl), FAB_OK);
    EQ(no, 0);
}

/* ---- frames at their edges ---------------------------------------------------------------- */

#define FS 2048u
static uint8_t g0[FS], g1[FS], gbig[3 * FS + 64], gframe[FS * 3], gframe2[FS * 3];

/* Frame plan over the previous frame only (no blocks). */
static uint32_t prev_plan(const uint8_t *prevb, uint32_t prevl, const uint8_t *next, uint32_t nl,
                          uint32_t *dl)
{
    fab_src_t fs[1] = {{prevb, prevl, bcid[0], true}};
    uint32_t no = 0, pl = 0;
    ipfsn_cid_t tc;
    EQ(fab_build(fs, 1, next, nl, 16, table, 14, opsb, 4096, &no, tdiff, OUTCAP, dl), FAB_OK);
    EQ(fab_target_cid(next, nl, &W, &tc), FAB_OK);
    EQ(fab_plan_write(&tc, nl, 0, fs, 1, opsb, no, fplan, sizeof fplan, &pl), FAB_OK);
    return pl;
}

static void open_stream(fab_stream_t *tx, fab_stream_t *rx, const uint8_t *f, uint32_t fl)
{
    uint32_t dl = 0, len = 0, olen = 0;
    memcpy(target, f, fl);
    uint32_t pl = plan_for(fl, NB, 1000, &dl);
    fab_seal_args_t a = args(FAB_KIND_STREAM, FAB_MODE_SEALED);
    EQ(fab_recipe_seal(&a, planb, pl, tdiff, dl, tx, rec, sizeof rec, &len), FAB_OK);
    fab_io_t x = io();
    store_reset();
    EQ(run(rec, len, rdk, &x, &olen, rx), FAB_OK);
}

static void test_frame_edges(void)
{
    static fab_stream_t tx, rx, tmp;
    fill(g0, FS);
    open_stream(&tx, &rx, g0, FS);
    /* frame 1: frame 0 shifted by 100, with a new head: PREV ops from inside */
    memcpy(g1, "NEW HEAD", 8);
    memcpy(g1 + 8, g0 + 100, FS - 100);
    memcpy(g1 + 8 + FS - 100, g0, 92);
    uint32_t dl = 0, fl = 0, olen = 0;
    uint32_t pl = prev_plan(g0, FS, g1, FS, &dl);
    fab_plan_t p;
    EQ(fab_plan_parse(fplan, pl, FAB_PLAN_FRAME, dl, &p), FAB_OK);
    EQ(p.prev_need, FS); /* the tail reads to the end of frame 0 */
    EQ(fab_frame_seal(&tx, 0, 1, fplan, pl, tdiff, dl, gframe, sizeof gframe, &fl), FAB_OK);
    fab_io_t x = io();
    /* exactly the room the frame needs */
    fab_io_t y = x;
    y.out_cap = FS;
    y.diff_cap = dl;
    memcpy(&tmp, &rx, sizeof rx);
    EQ(fab_frame_apply(&tmp, gframe, fl, g0, (uint32_t) p.prev_need, planb, pl, &y, &olen), FAB_OK);
    EQ(olen, FS);
    CHECK(memcmp(out, g1, FS) == 0);
    /* one byte less of the previous frame than the plan reads */
    memcpy(&tmp, &rx, sizeof rx);
    olen = 5;
    EQ(fab_frame_apply(&tmp, gframe, fl, g0, (uint32_t) p.prev_need - 1u, planb, pl, &y, &olen),
       FAB_ERR_RESYNC);
    EQ(olen, 0);
    EQ(tmp.seq, 0);
    /* a stream that is not live, with the right id and key */
    memcpy(&tmp, &rx, sizeof rx);
    tmp.live = false;
    EQ(fab_frame_apply(&tmp, gframe, fl, g0, FS, planb, sizeof planb, &x, &olen), FAB_ERR_STREAM);
    EQ(fab_checkpoint_verify(&tmp, cp, FAB_CP_LEN), FAB_ERR_STREAM);
    fab_stream_t dtx;
    memcpy(&dtx, &tx, sizeof tx);
    dtx.live = false;
    EQ(fab_checkpoint_sign(&dtx, 0, 1, ssk, 0, cp), FAB_ERR_STREAM);
    EQ(fab_frame_seal(&dtx, 0, 2, fplan, pl, tdiff, dl, gframe2, sizeof gframe2, &fl),
       FAB_ERR_STREAM);
    /* the previous frame may sit right before or right after the output */
    memcpy(gbig, g0, FS);
    y = x;
    y.out = gbig + FS;
    y.out_cap = FS;
    memcpy(&tmp, &rx, sizeof rx);
    EQ(fab_frame_apply(&tmp, gframe, fl, gbig, FS, planb, sizeof planb, &y, &olen), FAB_OK);
    CHECK(memcmp(gbig + FS, g1, FS) == 0);
    memcpy(gbig + 2 * FS, g0, FS);
    y.out = gbig + FS;
    memcpy(&tmp, &rx, sizeof rx);
    EQ(fab_frame_apply(&tmp, gframe, fl, gbig + 2 * FS, FS, planb, sizeof planb, &y, &olen),
       FAB_OK);
    /* one byte of overlap either way is refused */
    memcpy(&tmp, &rx, sizeof rx);
    EQ(fab_frame_apply(&tmp, gframe, fl, gbig + 2 * FS - 1, FS, planb, sizeof planb, &y, &olen),
       FAB_ERR_ARG);
    EQ(fab_frame_apply(&tmp, gframe, fl, gbig + 1, FS, planb, sizeof planb, &y, &olen),
       FAB_ERR_ARG);
    /* an empty previous frame inside the output is no overlap */
    EQ(fab_frame_apply(&tmp, gframe, fl, out + 5, 0, planb, sizeof planb, &x, &olen),
       FAB_ERR_RESYNC);

    /* frame form */
    static uint8_t f55[55];
    memcpy(f55, gframe, 55);
    EQ(fab_frame_apply(&tmp, f55, 55, g0, FS, planb, sizeof planb, &x, &olen), FAB_ERR_FORM);
#define FFORM(i, v)                                                                                \
    do {                                                                                           \
        memcpy(gframe2, gframe, fl);                                                               \
        gframe2[i] = (uint8_t) (v);                                                                \
        memcpy(&tmp, &rx, sizeof rx);                                                              \
        EQ(fab_frame_apply(&tmp, gframe2, fl, g0, FS, planb, sizeof planb, &x, &olen),             \
           FAB_ERR_FORM);                                                                          \
    } while (0)
    FFORM(0, 'X');
    FFORM(3, 'R');
    FFORM(4, 2);
    FFORM(5, 1);
    FFORM(6, 1);
    FFORM(7, 1);
#undef FFORM
    /* a plan length below the plan header, totals still agreeing */
    memcpy(gframe2, gframe, fl);
    uint32_t fpl = fab__get32(gframe + 48), fdl = fab__get32(gframe + 52);
    fab__put32(gframe2 + 48, 63);
    fab__put32(gframe2 + 52, fdl + fpl - 63);
    memcpy(&tmp, &rx, sizeof rx);
    EQ(fab_frame_apply(&tmp, gframe2, fl, g0, FS, planb, sizeof planb, &x, &olen), FAB_ERR_FORM);
    fab__put32(gframe2 + 48, 64);
    fab__put32(gframe2 + 52, fdl + fpl - 64);
    EQ(fab_frame_apply(&tmp, gframe2, fl, g0, FS, planb, sizeof planb, &x, &olen), FAB_ERR_AUTH);
    /* the output is wiped when the rebuilt frame is not the target */
    memcpy(gbig, g0, FS);
    gbig[700] ^= 1;
    memset(out, 0x44, FS);
    memcpy(&tmp, &rx, sizeof rx);
    EQ(fab_frame_apply(&tmp, gframe, fl, gbig, FS, planb, sizeof planb, &x, &olen), FAB_ERR_TARGET);
    CHECK(all_zero(out, FS));
    EQ(tmp.seq, 0);
    /* accepted for real */
    EQ(fab_frame_apply(&rx, gframe, fl, g0, FS, planb, sizeof planb, &x, &olen), FAB_OK);
    EQ(rx.seq, 1);
    EQ(rx.prev_seq, 1);
    /* the sender: exactly enough room, and a frame on a later base is refused */
    pl = prev_plan(g1, FS, g0, FS, &dl);
    uint32_t want = FAB_FHDR + pl + dl + 2 * FAB_TAG;
    memcpy(&tmp, &tx, sizeof tx);
    EQ(fab_frame_seal(&tmp, 1, 2, fplan, pl, tdiff, dl, gframe2, want - 1, &fl), FAB_ERR_SPACE);
    EQ(fab_frame_seal(&tmp, 2, 2, fplan, pl, tdiff, dl, gframe2, want, &fl), FAB_ERR_ARG);
    EQ(fab_frame_seal(&tmp, 1, 2, 0, pl, tdiff, dl, gframe2, want, &fl), FAB_ERR_ARG);
    EQ(fab_frame_seal(&tmp, 1, 2, fplan, pl, 0, dl, gframe2, want, &fl), FAB_ERR_ARG);
    EQ(fab_frame_seal(&tmp, 1, 2, fplan, pl, tdiff, dl, 0, want, &fl), FAB_ERR_ARG);
    EQ(fab_frame_seal(&tmp, 1, 2, fplan, pl, tdiff, dl, gframe2, want, 0), FAB_ERR_ARG);
    EQ(fab_frame_seal(0, 1, 2, fplan, pl, tdiff, dl, gframe2, want, &fl), FAB_ERR_ARG);
    memset(gframe2, 0xFF, 64);
    EQ(fab_frame_seal(&tmp, 1, 2, fplan, pl, tdiff, dl, gframe2, want, &fl), FAB_OK);
    EQ(fl, want);
    EQ(fab_frame_apply(&rx, gframe2, fl, g1, FS, planb, pl, &x, &olen), FAB_OK);
    CHECK(memcmp(out, g0, FS) == 0);
}

/* ---- the checkpoint ring ------------------------------------------------------------------- */

static void test_ring(void)
{
    static fab_stream_t tx, rx, tx2, rx2;
    static uint8_t cur[256], nxt[256], fr[2048];
    memcpy(cur, blk[1], 256);
    open_stream(&tx, &rx, cur, 256);
    fab_io_t x = io();
    uint32_t dl = 0, fl = 0, olen = 0;
    for (uint32_t k = 1; k <= 130; k++) {
        memcpy(nxt, cur, 256);
        nxt[k % 256] ^= (uint8_t) (k | 1);
        uint32_t pl = prev_plan(cur, 256, nxt, 256, &dl);
        EQ(fab_frame_seal(&tx, k - 1, k, fplan, pl, tdiff, dl, fr, sizeof fr, &fl), FAB_OK);
        EQ(fab_frame_apply(&rx, fr, fl, cur, 256, planb, sizeof planb, &x, &olen), FAB_OK);
        memcpy(cur, nxt, 256);
    }
    EQ(rx.seq, 130);
    /* the last 128 frames are held: a window of 128 is the most */
    EQ(fab_checkpoint_sign(&tx, 3, 130, ssk, 0, cp), FAB_OK);
    EQ(fab_checkpoint_verify(&rx, cp, FAB_CP_LEN), FAB_OK);
    EQ(rx.checked_seq, 130);
    EQ(fab_checkpoint_sign(&tx, 2, 129, ssk, 0, cp), FAB_ERR_RESYNC); /* 2 has left the ring */
    EQ(fab_checkpoint_sign(&tx, 2, 130, ssk, 0, cp), FAB_ERR_ARG);
    EQ(fab_checkpoint_sign(&tx, 60, 70, ssk, 0, cp), FAB_OK);
    EQ(fab_checkpoint_verify(&rx, cp, FAB_CP_LEN), FAB_OK);
    EQ(rx.checked_seq, 130); /* an older window does not lower it */
    EQ(fab__get64(cp + 24), 60);
    EQ(fab__get64(cp + 32), 70);
    /* a window of 128 sent by hand is refused before its signature */
    fab__put64(cp + 24, 2);
    fab__put64(cp + 32, 130);
    EQ(fab_checkpoint_verify(&rx, cp, FAB_CP_LEN), FAB_ERR_FORM);
    EQ(fab_checkpoint_sign(&tx, 70, 70, ssk, 0, cp), FAB_OK);
    EQ(fab_checkpoint_verify(&rx, cp, FAB_CP_LEN), FAB_OK);
    /* form */
#define CFORM(i, v)                                                                                \
    do {                                                                                           \
        EQ(fab_checkpoint_sign(&tx, 70, 71, ssk, 0, cp), FAB_OK);                                  \
        cp[i] = (uint8_t) (v);                                                                     \
        EQ(fab_checkpoint_verify(&rx, cp, FAB_CP_LEN), FAB_ERR_FORM);                              \
    } while (0)
    CFORM(0, 'X');
    CFORM(3, 'F');
    CFORM(4, 2);
    CFORM(5, 1);
    CFORM(6, 1);
    CFORM(7, 1);
#undef CFORM
    EQ(fab_checkpoint_sign(&tx, 70, 71, ssk, 0, cp), FAB_OK);
    EQ(fab_checkpoint_verify(&rx, cp, FAB_CP_LEN + 1), FAB_ERR_FORM);
    /* a checkpoint the sender signed for another of its streams */
    memcpy(nxt, blk[2], 256);
    coin_seed = 78;
    open_stream(&tx2, &rx2, nxt, 256);
    coin_seed = 77;
    EQ(fab_checkpoint_sign(&tx2, 0, 0, ssk, 0, cp), FAB_OK);
    EQ(fab_checkpoint_verify(&rx2, cp, FAB_CP_LEN), FAB_OK);
    EQ(fab_checkpoint_verify(&rx, cp, FAB_CP_LEN), FAB_ERR_STREAM);
}

int main(void)
{
    setvbuf(stdout, 0, _IONBF, 0);
    keys();
    blocks();
    test_target_cid();
    test_build();
    test_plan_rules();
    test_public_file();
    test_recipe_form();
    test_sealed_file();
    test_blocks_and_target();
    test_view();
    test_seal();
    test_stream();
    test_helpers();
    test_target_kat();
    test_golden();
    test_args();
    test_plan_edges();
    test_recipe_edges();
    test_cache();
    test_build_edges();
    test_frame_edges();
    test_ring();
    printf("%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
