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
#include "../robin_debanks/sha256.h"
#include "../pqsec/pq_matrix_algs.h"

static int fails, checks;
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

static fab_seal_args_t args(uint8_t kind, uint8_t mode)
{
    static uint8_t coins[32], rnd[32];
    seed32(coins, 77);
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
    printf("%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
