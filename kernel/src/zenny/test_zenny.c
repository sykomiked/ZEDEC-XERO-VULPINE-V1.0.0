/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zenny.c — Zenny chips (zenny.h Z1-Z6): worked mints, every refusal,
 * rebalances, quotes against a brute-force search, decompression receipts
 * and the 0.08889% fee, the WORM chain and tamper detection. The signature
 * hook is a stand-in (SHA-256 keyed by the signer's id); production passes
 * ML-DSA verification. */
#include <stdio.h>
#include <string.h>
#include "zenny.h"
#include "../robin_debanks/sha256.h"

static int pass, fail;
#define CHECK(c, msg)                                                                              \
    do {                                                                                           \
        if (c)                                                                                     \
            pass++;                                                                                \
        else {                                                                                     \
            fail++;                                                                                \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, msg);                                   \
        }                                                                                          \
    } while (0)

enum { MINTER = 1, CB = 2, CUST = 77, OTHER = 9 };
static zenny_t Z;

/* stand-in signature: SHA-256(secret(signer) || msg) */
static void sign(uint32_t who, const uint8_t *m, uint32_t n, uint8_t sig[32])
{
    uint8_t b[64];
    memset(b, 0, sizeof b);
    b[0] = (uint8_t) who;
    b[1] = 0x5A;
    b[2] = (uint8_t) (who >> 8);
    memcpy(b + 8, m, n);
    sha256(b, 8 + n, sig);
}
static uint32_t nverify;
static bool verify(void *ctx, uint32_t who, const uint8_t *m, uint32_t n, const uint8_t *sig,
                   uint32_t sl)
{
    (void) ctx;
    uint8_t want[32];
    nverify++;
    if (sl != 32 || n != ZENNY_SIG_MSG) return false;
    sign(who, m, n, want);
    return !memcmp(want, sig, 32);
}

/* a request with storage for its signatures */
typedef struct {
    zenny_req_t r;
    uint8_t sm[32], sc[32], at[ZENNY_MAX_LEGS][32];
} kit_t;

static zenny_leg_t leg(uint8_t kind, uint64_t asset, uint32_t cust, uint64_t qty, uint64_t w)
{
    zenny_leg_t l = {kind, cust, asset, qty, w};
    return l;
}
static zenny_price_t px(uint64_t num, uint64_t den)
{
    zenny_price_t p = {num, den};
    return p;
}

/* worked example: kappa 1,000,000 reference units per chip
 *   VBILL      6,000,000 @ 1/1     = 6,000,000  weight 6
 *   FIAT      30,000,000 @ 1/10    = 3,000,000  weight 3
 *   COMMODITY         25 @ 40000/1 = 1,000,000  weight 1 (custodian 77)
 *   T = 10,000,000, chips = 10, W = 10 */
static void base(kit_t *k, uint32_t rules, uint64_t nonce)
{
    memset(k, 0, sizeof *k);
    k->r.minter = MINTER;
    k->r.co_minter = (rules & ZENNY_RULE_BILATERAL) ? CB : 0;
    k->r.kappa = 1000000;
    k->r.rules = rules;
    k->r.nonce = nonce;
    k->r.n_legs = 3;
    k->r.leg[0] = leg(ZENNY_VBILL, 1, 0, 6000000, 6);
    k->r.leg[1] = leg(ZENNY_FIAT, 840, 0, 30000000, 3);
    k->r.leg[2] = leg(ZENNY_COMMODITY, 79, CUST, 25, 1);
    k->r.price[0] = px(1, 1);
    k->r.price[1] = px(1, 10);
    k->r.price[2] = px(40000, 1);
    k->r.tick = 100;
}

/* sign for a mint (slot < 0) or a rebalance of `slot` */
static void seal(kit_t *k, int slot)
{
    uint8_t d[32], id[32], head[32] = {0}, m[ZENNY_SIG_MSG];
    zenny_purpose_t p;
    if (slot < 0) {
        zenny_mint_digest(&k->r, d);
        memcpy(id, d, 32);
        p = ZENNY_SIG_MINT;
    } else {
        zenny_rebalance_digest(&Z, (uint32_t) slot, &k->r, d);
        memcpy(id, Z.chip[slot].id, 32);
        memcpy(head, zenny_current(&Z, (uint32_t) slot)->hash, 32);
        p = ZENNY_SIG_REBALANCE;
    }
    zenny_sign_msg(p, d, m);
    sign(k->r.minter, m, sizeof m, k->sm);
    sign(k->r.co_minter, m, sizeof m, k->sc);
    k->r.sig_minter = k->sm;
    k->r.sig_minter_len = 32;
    k->r.sig_co = k->sc;
    k->r.sig_co_len = 32;
    for (uint32_t i = 0; i < k->r.n_legs && i < ZENNY_MAX_LEGS; i++) {
        uint8_t a[32];
        zenny_att_digest(id, head, &k->r.leg[i], a);
        zenny_sign_msg(ZENNY_SIG_ATTEST, a, m);
        sign(k->r.leg[i].custodian, m, sizeof m, k->at[i]);
        k->r.att[i] = k->at[i];
        k->r.att_len[i] = 32;
    }
}

static zenny_status_t mint_kit(kit_t *k, uint32_t *slot)
{
    seal(k, -1);
    return zenny_mint(&Z, &k->r, slot);
}

static uint32_t used(void)
{
    uint32_t n = 0;
    for (uint32_t s = 0; s < ZENNY_MAX_CHIPS; s++) n += Z.chip[s].used;
    return n;
}

/* ---- independent byte builders for the documented hashes ---- */
typedef struct {
    uint8_t b[1024];
    uint32_t n;
} buf_t;
static void put(buf_t *b, uint64_t v)
{
    for (int i = 0; i < 8; i++) b->b[b->n++] = (uint8_t) (v >> (8 * i));
}
static void putb(buf_t *b, const void *p, uint32_t n)
{
    memcpy(b->b + b->n, p, n);
    b->n += n;
}
static void putlegs(buf_t *b, uint32_t n, const zenny_leg_t *l, const zenny_price_t *p)
{
    put(b, n);
    for (uint32_t i = 0; i < n && i < ZENNY_MAX_LEGS; i++) {
        put(b, l[i].kind);
        put(b, l[i].custodian);
        put(b, l[i].asset);
        put(b, l[i].qty);
        put(b, l[i].weight);
        put(b, p[i].num);
        put(b, p[i].den);
    }
}
static void ref_id(const zenny_req_t *r, uint8_t out[32])
{
    buf_t b = {{0}, 0};
    putb(&b, "zxv/zenny/id/v1.", 16);
    put(&b, r->minter);
    put(&b, r->co_minter);
    put(&b, r->kappa);
    put(&b, r->rules);
    putlegs(&b, r->n_legs, r->leg, r->price);
    put(&b, r->nonce);
    sha256(b.b, b.n, out);
}
static void ref_ver(const zenny_chip_t *ch, const zenny_version_t *v, uint8_t out[32])
{
    buf_t b = {{0}, 0};
    putb(&b, v->prev, 32);
    putb(&b, "zxv/zenny/ver/1.", 16);
    putb(&b, ch->id, 32);
    put(&b, ch->minter);
    put(&b, ch->co_minter);
    put(&b, ch->kappa);
    put(&b, ch->rules);
    put(&b, ch->nonce);
    put(&b, v->op);
    put(&b, v->chips);
    put(&b, v->tick);
    put(&b, v->recipient);
    putlegs(&b, v->n_legs, v->leg, v->price);
    put(&b, v->version);
    sha256(b.b, b.n, out);
}

static void test_mint(void)
{
    zenny_init(&Z, verify, NULL);
    kit_t k;
    uint32_t s = 99, s2;
    base(&k, ZENNY_RULE_ALLOW_REBALANCE, 1);
    CHECK(mint_kit(&k, &s) == ZENNY_OK && s == 0, "the worked basket mints into slot 0");
    const zenny_version_t *v = zenny_current(&Z, s);
    CHECK(v && v->chips == 10 && v->version == 0 && v->op == ZENNY_OP_MINT && v->tick == 100 &&
              v->n_legs == 3 && v->leg[2].qty == 25 && v->price[1].den == 10,
          "10,000,000 reference units at kappa 1,000,000 = 10 chips");
    uint8_t id[32], ref[32], zero[32] = {0};
    zenny_mint_digest(&k.r, id);
    ref_id(&k.r, ref);
    CHECK(!memcmp(id, ref, 32) && !memcmp(Z.chip[0].id, id, 32),
          "the chip id is SHA-256 of exactly the documented mint bytes");
    CHECK(!memcmp(v->prev, zero, 32), "version 0 chains from 32 zero bytes");
    ref_ver(&Z.chip[0], v, ref);
    CHECK(!memcmp(v->hash, ref, 32), "the version hash is exactly the documented bytes");
    CHECK(zenny_find(&Z, id, &s2) && s2 == 0 && zenny_find(&Z, id, NULL), "found by id");
    CHECK(zenny_verify_chain(&Z, 0) == ZENNY_OK && zenny_check(&Z, 0) == ZENNY_OK,
          "the chain verifies and claim == value");
    CHECK(Z.chip[0].kappa == 1000000 && Z.chip[0].minter == MINTER &&
              Z.chip[0].rules == ZENNY_RULE_ALLOW_REBALANCE && Z.chip[0].nonce == 1,
          "the manifest fields are recorded");

    /* the same content again is the same id */
    CHECK(zenny_mint(&Z, &k.r, &s2) == ZENNY_ERR_DUP && used() == 1, "same id refused");
    base(&k, ZENNY_RULE_ALLOW_REBALANCE, 2);
    CHECK(mint_kit(&k, &s2) == ZENNY_OK && s2 == 1, "another nonce is another chip");
    CHECK(zenny_mint(&Z, &k.r, &s2) == ZENNY_ERR_DUP, "a duplicate of the second chip refused");
    /* a bilateral chip co-minted with a central bank */
    base(&k, ZENNY_RULE_BILATERAL, 3);
    CHECK(mint_kit(&k, &s2) == ZENNY_OK && s2 == 2 && Z.chip[2].co_minter == CB,
          "bilateral chip minted with both signatures");
    /* scaled weights describe the same basket */
    base(&k, 0, 4);
    k.r.leg[0].weight = 12, k.r.leg[1].weight = 6, k.r.leg[2].weight = 2;
    CHECK(mint_kit(&k, &s2) == ZENNY_OK, "weights 12:6:2 equal 6:3:1");
    /* the edge kinds and a full leg table */
    base(&k, 0, 5);
    k.r.n_legs = ZENNY_MAX_LEGS;
    for (uint32_t i = 0; i < ZENNY_MAX_LEGS; i++) {
        k.r.leg[i] = leg(i & 1 ? ZENNY_COMPUTE : ZENNY_VBILL, i, 0, 1000000, 1);
        k.r.price[i] = px(1, 1);
    }
    CHECK(mint_kit(&k, &s2) == ZENNY_OK && zenny_current(&Z, s2)->chips == 8,
          "eight legs (the cap) of VBILL and COMPUTE mint");

    /* the signed messages */
    uint8_t m[ZENNY_SIG_MSG], d[32];
    for (int i = 0; i < 32; i++) d[i] = (uint8_t) (i + 1);
    zenny_sign_msg(ZENNY_SIG_MINT, d, m);
    CHECK(!memcmp(m, "zxv/zenny/mint/1", 16) && !memcmp(m + 16, d, 32), "mint message bytes");
    zenny_sign_msg(ZENNY_SIG_REBALANCE, d, m);
    CHECK(!memcmp(m, "zxv/zenny/rebl/1", 16), "rebalance message tag");
    zenny_sign_msg(ZENNY_SIG_ATTEST, d, m);
    CHECK(!memcmp(m, "zxv/zenny/attn/1", 16) && !memcmp(m + 16, d, 32), "attestation tag");
    zenny_sign_msg((zenny_purpose_t) 3, d, m);
    uint8_t z48[ZENNY_SIG_MSG] = {0};
    CHECK(!memcmp(m, z48, sizeof m), "an unknown purpose gives an all-zero message");
    /* the attestation digest */
    zenny_leg_t g = leg(ZENNY_COMMODITY, 0x0102030405060708ull, 0xA0B0C0D0u, 0x1122334455ull, 3);
    buf_t b = {{0}, 0};
    uint8_t hd[32];
    for (int i = 0; i < 32; i++) hd[i] = (uint8_t) (0xC0 + i);
    putb(&b, "zxv/zenny/att/2.", 16);
    putb(&b, d, 32);
    putb(&b, hd, 32);
    put(&b, g.kind);
    put(&b, g.custodian);
    put(&b, g.asset);
    put(&b, g.qty);
    sha256(b.b, b.n, ref);
    zenny_att_digest(d, hd, &g, id);
    CHECK(!memcmp(id, ref, 32), "the attestation digest is exactly the documented bytes");
    hd[31] ^= 1;
    zenny_att_digest(d, hd, &g, ref);
    CHECK(memcmp(id, ref, 32), "the attestation digest binds the version head");
    /* the digest of an oversized request reads only the leg table */
    base(&k, 0, 6);
    k.r.n_legs = ZENNY_MAX_LEGS + 1;
    for (uint32_t i = 0; i < ZENNY_MAX_LEGS; i++) {
        k.r.leg[i] = leg(ZENNY_VBILL, i + 100, 0, i, 1);
        k.r.price[i] = px(1, 1);
    }
    zenny_mint_digest(&k.r, id);
    ref_id(&k.r, ref);
    CHECK(!memcmp(id, ref, 32), "the mint digest hashes n but reads at most the leg table");
}

static void expect(kit_t *k, zenny_status_t want, const char *msg)
{
    uint32_t s = 77, n = used();
    seal(k, -1);
    zenny_status_t st = zenny_mint(&Z, &k->r, &s);
    if (st != want) printf("  got %d want %d\n", st, want);
    CHECK(st == want && used() == n && s == 77, msg);
}

static void test_refusals(void)
{
    zenny_init(&Z, verify, NULL);
    kit_t k;
    uint32_t s;
    base(&k, 0, 1);
    seal(&k, -1);
    CHECK(zenny_mint(NULL, &k.r, &s) == ZENNY_ERR_ARG &&
              zenny_mint(&Z, NULL, &s) == ZENNY_ERR_ARG &&
              zenny_mint(&Z, &k.r, NULL) == ZENNY_ERR_ARG,
          "null arguments refused");
    Z.verify = NULL;
    CHECK(zenny_mint(&Z, &k.r, &s) == ZENNY_ERR_ARG, "no verify hook: refused");
    Z.verify = verify;
    zenny_init(NULL, verify, NULL); /* no crash */

    base(&k, 0, 1), k.r.n_legs = 0;
    expect(&k, ZENNY_ERR_LEGS, "no legs refused");
    base(&k, 0, 1), k.r.n_legs = ZENNY_MAX_LEGS + 1;
    expect(&k, ZENNY_ERR_LEGS, "too many legs refused");
    base(&k, 0, 1), k.r.kappa = 0;
    expect(&k, ZENNY_ERR_KAPPA, "kappa 0 refused");
    base(&k, 0x20, 1);
    expect(&k, ZENNY_ERR_ARG, "an unknown rule bit refused");
    base(&k, ZENNY_RULE_MARGIN, 1);
    expect(&k, ZENNY_ERR_LEVERAGE, "margin / leverage refused");
    base(&k, ZENNY_RULE_MARGIN | ZENNY_RULE_ALLOW_REBALANCE, 1);
    expect(&k, ZENNY_ERR_LEVERAGE, "margin refused alongside other rules");
    base(&k, ZENNY_RULE_CREDIT, 1);
    expect(&k, ZENNY_ERR_CREDIT, "a claim above the deposit (credit) refused");
    base(&k, ZENNY_RULE_BILATERAL, 1), k.r.co_minter = MINTER;
    expect(&k, ZENNY_ERR_ARG, "bilateral with oneself refused");
    base(&k, 0, 1), k.r.leg[1].kind = 0;
    expect(&k, ZENNY_ERR_KIND, "kind 0 refused");
    base(&k, 0, 1), k.r.leg[1].kind = 5;
    expect(&k, ZENNY_ERR_KIND, "kind 5 refused");
    base(&k, 0, 1), k.r.price[1].den = 0;
    expect(&k, ZENNY_ERR_PRICE, "den 0 refused");
    base(&k, 0, 1), k.r.leg[1].qty = 30000005;
    expect(&k, ZENNY_ERR_INEXACT, "a value that does not divide exactly refused");
    base(&k, 0, 1), k.r.kappa = 3000000;
    expect(&k, ZENNY_ERR_INEXACT, "T / kappa not exact refused");
    base(&k, 0, 1), k.r.kappa = 10000000;
    CHECK(mint_kit(&k, &s) == ZENNY_OK && zenny_current(&Z, s)->chips == 1,
          "kappa equal to T gives one chip");
    base(&k, 0, 1), k.r.leg[0].weight = 7;
    expect(&k, ZENNY_ERR_WEIGHT, "weights that do not match the values refused");
    base(&k, 0, 1), k.r.leg[2].weight = 2;
    expect(&k, ZENNY_ERR_WEIGHT, "a wrong weight on the last leg refused");
    base(&k, 0, 1), k.r.leg[1].weight = 1, k.r.leg[2].weight = 3;
    expect(&k, ZENNY_ERR_WEIGHT, "weights swapped between two legs refused");
    base(&k, 0, 1), k.r.leg[0].kind = ZENNY_FIAT;
    expect(&k, ZENNY_ERR_ANCHOR, "no V-Bill leg refused");
    base(&k, 0, 1), k.r.leg[0].weight = 0, k.r.leg[0].qty = 0, k.r.leg[1].qty = 90000000;
    k.r.leg[1].weight = 9;
    expect(&k, ZENNY_ERR_ANCHOR, "a V-Bill leg with weight 0 is no anchor");
    base(&k, 0, 1), k.r.n_legs = 1, k.r.leg[0].qty = 0;
    expect(&k, ZENNY_ERR_ARG, "nothing deposited refused");
    base(&k, 0, 1), k.r.leg[0].qty = UINT64_MAX, k.r.price[0] = px(2, 1);
    expect(&k, ZENNY_ERR_OVERFLOW, "a leg value above 64 bits refused");
    base(&k, 0, 1), k.r.n_legs = 2, k.r.leg[0].qty = 1ull << 63, k.r.leg[1].qty = 1ull << 63;
    k.r.price[1] = px(1, 1);
    expect(&k, ZENNY_ERR_OVERFLOW, "a total above 64 bits refused");
    base(&k, 0, 1), k.r.n_legs = 2, k.r.leg[0].weight = UINT64_MAX, k.r.leg[1].weight = 1;
    expect(&k, ZENNY_ERR_OVERFLOW, "a weight sum above 64 bits refused");

    /* signatures */
    base(&k, 0, 1);
    seal(&k, -1);
    k.r.sig_minter = NULL;
    CHECK(zenny_mint(&Z, &k.r, &s) == ZENNY_ERR_SIG, "missing minter signature refused");
    seal(&k, -1);
    k.sm[5] ^= 1;
    CHECK(zenny_mint(&Z, &k.r, &s) == ZENNY_ERR_SIG, "a flipped bit refused");
    seal(&k, -1);
    k.r.sig_minter_len = 31;
    CHECK(zenny_mint(&Z, &k.r, &s) == ZENNY_ERR_SIG, "a short signature refused");
    seal(&k, -1);
    k.r.minter = OTHER;
    CHECK(zenny_mint(&Z, &k.r, &s) == ZENNY_ERR_SIG, "a signature by someone else refused");
    base(&k, ZENNY_RULE_BILATERAL, 1);
    seal(&k, -1);
    k.r.sig_co = NULL;
    CHECK(zenny_mint(&Z, &k.r, &s) == ZENNY_ERR_SIG, "bilateral without the co-minter refused");
    seal(&k, -1);
    k.sc[0] ^= 1;
    CHECK(zenny_mint(&Z, &k.r, &s) == ZENNY_ERR_SIG, "bilateral with a bad co-signature refused");
    seal(&k, -1);
    k.r.sig_co_len = 0;
    CHECK(zenny_mint(&Z, &k.r, &s) == ZENNY_ERR_SIG, "co-signature length checked");
    base(&k, 0, 1);
    seal(&k, -1);
    k.r.sig_co = NULL;
    CHECK(zenny_mint(&Z, &k.r, &s) == ZENNY_OK, "a non-bilateral chip needs no co-signature");
    /* attestations */
    base(&k, 0, 2);
    seal(&k, -1);
    k.r.att[2] = NULL;
    CHECK(zenny_mint(&Z, &k.r, &s) == ZENNY_ERR_ATTEST, "commodity without attestation refused");
    seal(&k, -1);
    k.at[2][9] ^= 1;
    CHECK(zenny_mint(&Z, &k.r, &s) == ZENNY_ERR_ATTEST, "a bad attestation refused");
    seal(&k, -1);
    k.r.att_len[2] = 64;
    CHECK(zenny_mint(&Z, &k.r, &s) == ZENNY_ERR_ATTEST, "attestation length checked");
    seal(&k, -1);
    k.r.leg[2].custodian = OTHER; /* the id changes: re-sign the mint, keep the old att */
    {
        uint8_t keep[32];
        memcpy(keep, k.at[2], 32);
        seal(&k, -1);
        memcpy(k.at[2], keep, 32);
    }
    CHECK(zenny_mint(&Z, &k.r, &s) == ZENNY_ERR_ATTEST, "another custodian's attestation refused");
    /* the commodity leg first */
    base(&k, 0, 3);
    k.r.leg[0] = k.r.leg[2], k.r.price[0] = k.r.price[2];
    k.r.leg[2] = leg(ZENNY_VBILL, 1, 0, 6000000, 6), k.r.price[2] = px(1, 1);
    seal(&k, -1);
    k.r.att[0] = NULL;
    CHECK(zenny_mint(&Z, &k.r, &s) == ZENNY_ERR_ATTEST, "a first-leg commodity is attested too");
    /* non-commodity legs need none */
    base(&k, 0, 2);
    seal(&k, -1);
    k.r.att[0] = k.r.att[1] = NULL;
    uint32_t nv = nverify;
    CHECK(zenny_mint(&Z, &k.r, &s) == ZENNY_OK && nverify == nv + 2,
          "only the commodity leg is attested (minter + one custodian)");
    /* a full chip table */
    for (uint64_t n = 10; used() < ZENNY_MAX_CHIPS; n++) {
        base(&k, 0, n);
        mint_kit(&k, &s);
    }
    base(&k, 0, 999);
    seal(&k, -1);
    CHECK(zenny_mint(&Z, &k.r, &s) == ZENNY_ERR_FULL, "no free slot refused");
}

/* the rebalanced manifest: 5,000,000 VBILL, 40,000,000 FIAT @1/10, the same
 * commodity at a new price 50000 with qty 20 (attested again): T = 10,000,000 */
static void rebased(kit_t *k, uint32_t rules)
{
    base(k, rules, 0);
    k->r.leg[0].qty = 5000000, k->r.leg[0].weight = 5;
    k->r.leg[1].qty = 40000000, k->r.leg[1].weight = 4;
    k->r.leg[2].qty = 20, k->r.price[2] = px(50000, 1);
    k->r.tick = 200;
}

static zenny_status_t rebal(kit_t *k, uint32_t s)
{
    seal(k, (int) s);
    return zenny_rebalance(&Z, s, &k->r);
}

static void test_rebalance(void)
{
    zenny_init(&Z, verify, NULL);
    kit_t k, m;
    uint32_t s, fixed, bil;
    base(&m, ZENNY_RULE_ALLOW_REBALANCE, 1);
    mint_kit(&m, &s);
    base(&m, 0, 2);
    mint_kit(&m, &fixed);
    base(&m, ZENNY_RULE_ALLOW_REBALANCE | ZENNY_RULE_BILATERAL, 3);
    mint_kit(&m, &bil);

    rebased(&k, ZENNY_RULE_ALLOW_REBALANCE);
    uint8_t d1[32], d2[32];
    CHECK(zenny_rebalance_digest(&Z, s, &k.r, d1) == ZENNY_OK, "rebalance digest");
    {
        buf_t b = {{0}, 0};
        putb(&b, "zxv/zenny/rbd/1.", 16);
        putb(&b, Z.chip[s].id, 32);
        putb(&b, Z.chip[s].ver[0].hash, 32);
        put(&b, k.r.minter);
        put(&b, k.r.co_minter);
        put(&b, k.r.kappa);
        put(&b, k.r.rules);
        putlegs(&b, k.r.n_legs, k.r.leg, k.r.price);
        sha256(b.b, b.n, d2);
        CHECK(!memcmp(d1, d2, 32), "the rebalance digest is exactly the documented bytes");
    }
    CHECK(zenny_rebalance_digest(&Z, 7, &k.r, d1) == ZENNY_ERR_ARG &&
              zenny_rebalance_digest(&Z, s, NULL, d1) == ZENNY_ERR_ARG &&
              zenny_rebalance_digest(&Z, s, &k.r, NULL) == ZENNY_ERR_ARG &&
              zenny_rebalance_digest(NULL, s, &k.r, d1) == ZENNY_ERR_ARG,
          "rebalance digest arguments checked");

    k.r.leg[3] = leg(ZENNY_COMPUTE, 5, 5, 5, 5); /* past n_legs: never read */
    CHECK(rebal(&k, s) == ZENNY_OK, "rebalance to 5:4:1 at a new commodity price");
    const zenny_version_t *v = zenny_current(&Z, s);
    CHECK(v->version == 1 && v->op == ZENNY_OP_REBALANCE && v->chips == 10 && v->tick == 200 &&
              v->leg[0].qty == 5000000 && v->leg[2].qty == 20 && v->price[2].num == 50000 &&
              !memcmp(v->prev, Z.chip[s].ver[0].hash, 32),
          "version 1 chains from version 0, claim unchanged");
    CHECK(zenny_verify_chain(&Z, s) == ZENNY_OK && zenny_check(&Z, s) == ZENNY_OK,
          "chain and claim == value after the rebalance");
    CHECK(zenny_rebalance(&Z, s, &k.r) == ZENNY_ERR_SIG && Z.chip[s].n_ver == 2,
          "the same signed rebalance cannot be replayed");
    {
        /* X -> Y -> X: the custodian's attestation of X (25 units, signed for
         * the mint) must not back the return to X, nor Y's for a second Y */
        uint8_t attY[32], attX[32], z32[32] = {0}, a[32], msg[ZENNY_SIG_MSG];
        memcpy(attY, k.at[2], 32);
        kit_t x;
        base(&x, ZENNY_RULE_ALLOW_REBALANCE, 0);
        zenny_att_digest(Z.chip[s].id, z32, &x.r.leg[2], a);
        zenny_sign_msg(ZENNY_SIG_ATTEST, a, msg);
        sign(CUST, msg, sizeof msg, attX);
        seal(&x, (int) s);
        CHECK(memcmp(x.at[2], attX, 32) != 0, "a fresh attestation differs from the mint's");
        memcpy(x.at[2], attX, 32);
        CHECK(zenny_rebalance(&Z, s, &x.r) == ZENNY_ERR_ATTEST && Z.chip[s].n_ver == 2,
              "X->Y->X: the mint's attestation for the same quantity is refused");
        CHECK(rebal(&x, s) == ZENNY_OK && Z.chip[s].n_ver == 3 &&
                  zenny_current(&Z, s)->leg[2].qty == 25 && zenny_verify_chain(&Z, s) == ZENNY_OK,
              "X->Y->X with an attestation bound to the current version");
        kit_t y;
        rebased(&y, ZENNY_RULE_ALLOW_REBALANCE);
        seal(&y, (int) s);
        memcpy(y.at[2], attY, 32);
        CHECK(zenny_rebalance(&Z, s, &y.r) == ZENNY_ERR_ATTEST && Z.chip[s].n_ver == 3,
              "X->Y->X->Y: Y's earlier attestation is refused too");
        CHECK(rebal(&y, s) == ZENNY_OK && Z.chip[s].n_ver == 4 && zenny_check(&Z, s) == ZENNY_OK,
              "and a fresh one passes");
    }

    uint32_t nv0 = Z.chip[s].n_ver;
#define REFUSE(edit, want, msg)                                                                    \
    do {                                                                                           \
        rebased(&k, ZENNY_RULE_ALLOW_REBALANCE);                                                   \
        edit;                                                                                      \
        CHECK(rebal(&k, s) == (want) && Z.chip[s].n_ver == nv0, msg);                              \
    } while (0)
    REFUSE(k.r.kappa = 500000, ZENNY_ERR_IMMUTABLE, "changing kappa refused");
    REFUSE(k.r.rules = 0, ZENNY_ERR_IMMUTABLE, "changing the rules refused");
    REFUSE(k.r.minter = OTHER, ZENNY_ERR_IMMUTABLE, "changing the minter refused");
    REFUSE(k.r.co_minter = CB, ZENNY_ERR_IMMUTABLE, "changing the co-minter refused");
    REFUSE(k.r.n_legs = 2, ZENNY_ERR_IMMUTABLE, "dropping a leg refused");
    REFUSE(k.r.leg[2].kind = ZENNY_COMPUTE, ZENNY_ERR_IMMUTABLE, "changing a leg kind refused");
    REFUSE(k.r.leg[0].asset = 2, ZENNY_ERR_IMMUTABLE, "changing the first leg's asset refused");
    REFUSE(k.r.leg[1].asset = 978, ZENNY_ERR_IMMUTABLE, "changing a leg asset refused");
    REFUSE(k.r.leg[2].custodian = OTHER, ZENNY_ERR_IMMUTABLE, "changing a custodian refused");
    REFUSE((k.r.leg[0].qty = 7000000, k.r.leg[0].weight = 7), ZENNY_ERR_CLAIM,
           "raising the claim (11 chips) refused");
    REFUSE((k.r.leg[0].qty = 4000000, k.r.leg[0].weight = 4), ZENNY_ERR_CLAIM,
           "lowering the claim (9 chips) refused");
    REFUSE(k.r.leg[0].qty = 5500000, ZENNY_ERR_INEXACT, "a claim that is not whole chips refused");
    REFUSE(k.r.leg[0].weight = 6, ZENNY_ERR_WEIGHT, "weights that do not match refused");
    REFUSE(k.r.price[1].den = 0, ZENNY_ERR_PRICE, "den 0 refused");
    {
        rebased(&k, ZENNY_RULE_ALLOW_REBALANCE);
        seal(&k, (int) s);
        k.r.att[2] = NULL;
        CHECK(zenny_rebalance(&Z, s, &k.r) == ZENNY_ERR_ATTEST, "fresh attestation required");
        seal(&k, (int) s);
        k.sm[1] ^= 1;
        CHECK(zenny_rebalance(&Z, s, &k.r) == ZENNY_ERR_SIG, "minter signature required");
    }
    rebased(&k, 0);
    CHECK(rebal(&k, fixed) == ZENNY_ERR_NOT_ALLOWED && Z.chip[fixed].n_ver == 1,
          "a chip minted without ALLOW_REBALANCE cannot be rebalanced");
    rebased(&k, ZENNY_RULE_ALLOW_REBALANCE | ZENNY_RULE_BILATERAL);
    seal(&k, (int) bil);
    k.r.sig_co = NULL;
    CHECK(zenny_rebalance(&Z, bil, &k.r) == ZENNY_ERR_SIG, "bilateral rebalance needs both");
    CHECK(rebal(&k, bil) == ZENNY_OK, "bilateral rebalance with both signatures");
    CHECK(zenny_rebalance(&Z, 7, &k.r) == ZENNY_ERR_ARG &&
              zenny_rebalance(&Z, ZENNY_MAX_CHIPS, &k.r) == ZENNY_ERR_ARG &&
              zenny_rebalance(&Z, s, NULL) == ZENNY_ERR_ARG &&
              zenny_rebalance(NULL, s, &k.r) == ZENNY_ERR_ARG,
          "rebalance arguments checked");
    Z.verify = NULL;
    CHECK(zenny_rebalance(&Z, s, &k.r) == ZENNY_ERR_ARG, "no verify hook: refused");
    Z.verify = verify;

    /* the version log fills and then refuses (never overwrites) */
    uint32_t n = 0;
    while (Z.chip[s].n_ver < ZENNY_MAX_VERSIONS) {
        if (Z.chip[s].n_ver & 1)
            base(&k, ZENNY_RULE_ALLOW_REBALANCE, 0);
        else
            rebased(&k, ZENNY_RULE_ALLOW_REBALANCE);
        n += rebal(&k, s) == ZENNY_OK;
    }
    CHECK(n == ZENNY_MAX_VERSIONS - 4, "rebalances up to the version cap");
    base(&k, ZENNY_RULE_ALLOW_REBALANCE, 0);
    CHECK(rebal(&k, s) == ZENNY_ERR_FULL && zenny_verify_chain(&Z, s) == ZENNY_OK,
          "a full version log refuses and the chain still verifies");
    zenny_price_t bp[3] = {{1, 1}, {1, 10}, {40000, 1}};
    CHECK(zenny_decompress(&Z, s, 2, bp, 1, NULL) == ZENNY_ERR_FULL,
          "decompression refused too when the log is full");

    /* a fully decompressed chip cannot be refilled by a rebalance */
    base(&k, ZENNY_RULE_ALLOW_REBALANCE, 50);
    uint32_t e;
    mint_kit(&k, &e);
    CHECK(zenny_decompress(&Z, e, 10, bp, 1, NULL) == ZENNY_OK &&
              zenny_current(&Z, e)->chips == 0 && zenny_check(&Z, e) == ZENNY_OK,
          "fully decompressed: 0 chips, all legs 0, still consistent");
    base(&k, ZENNY_RULE_ALLOW_REBALANCE, 0);
    CHECK(rebal(&k, e) == ZENNY_ERR_CLAIM, "rebalancing value into an empty chip refused");
    CHECK(zenny_quote(&Z, e, 5, NULL) == 0, "an empty chip quotes 0");
}

/* brute force: the largest c <= want that pays out exactly */
static uint64_t brute(const zenny_version_t *v, uint64_t want, const zenny_price_t *bp)
{
    if (want > v->chips) want = v->chips;
    for (uint64_t c = want; c > 0; c--) {
        bool ok = true;
        for (uint32_t i = 0; i < v->n_legs && ok; i++) {
            if ((v->leg[i].qty * c) % v->chips) ok = false;
            if (ok && bp) {
                uint64_t o = v->leg[i].qty * c / v->chips;
                if (bp[i].den == 0 || (o * bp[i].num) % bp[i].den) ok = false;
            }
        }
        if (ok) return c;
    }
    return 0;
}

static zenny_t Q;

static void test_quote(void)
{
    zenny_init(&Z, verify, NULL);
    kit_t k;
    uint32_t s;
    base(&k, 0, 1);
    mint_kit(&k, &s);
    /* commodity pays out 25c/10 = 5c/2: c must be even */
    CHECK(zenny_quote(&Z, s, 7, NULL) == 6 && zenny_quote(&Z, s, 6, NULL) == 6 &&
              zenny_quote(&Z, s, 1, NULL) == 0 && zenny_quote(&Z, s, 0, NULL) == 0,
          "quote rounds down to an even count");
    CHECK(zenny_quote(&Z, s, 100, NULL) == 10 && zenny_quote(&Z, s, 10, NULL) == 10 &&
              zenny_quote(&Z, s, 11, NULL) == 10,
          "quote never exceeds the chips outstanding");
    zenny_price_t bp[3] = {{1, 1}, {1, 10}, {3, 4}};
    /* at 3/4 the commodity out 5k pays 15k/4: c a multiple of 8 */
    CHECK(zenny_quote(&Z, s, 9, bp) == 8 && zenny_quote(&Z, s, 7, bp) == 0 &&
              zenny_quote(&Z, s, 10, bp) == 8,
          "with batch prices the step is 8");
    zenny_price_t b0[3] = {{1, 1}, {1, 0}, {3, 4}};
    CHECK(zenny_quote(&Z, s, 10, b0) == 0, "a batch den 0 quotes 0");
    zenny_price_t bo[3] = {{UINT64_MAX, 1}, {1, 10}, {1, 1}};
    CHECK(zenny_quote(&Z, s, 10, bo) == 0, "a disposal value above 64 bits quotes 0");
    zenny_price_t bk[3] = {{1, 1}, {1, 1}, {1, 0xFFFFFFFFFFFFFFC5ull}};
    CHECK(zenny_quote(&Z, s, 10, bk) == 0, "a step above 64 bits quotes 0");
    zenny_price_t bl[3] = {{1, 1}, {1, 205891132094649ull}, {1, 1ull << 40}}; /* 3^30, 2^40 */
    CHECK(zenny_quote(&Z, s, 10, bl) == 0, "a price lcm above 64 bits quotes 0");
    CHECK(zenny_quote(&Z, 5, 10, NULL) == 0 && zenny_quote(NULL, s, 10, NULL) == 0 &&
              zenny_quote(&Z, ZENNY_MAX_CHIPS, 10, NULL) == 0,
          "no chip quotes 0");
    zenny_receipt_t rc;
    CHECK(zenny_decompress(&Z, s, 8, bp, 5, &rc) == ZENNY_OK && rc.out[2] == 20 &&
              rc.value[2] == 15 && rc.total == 4800000 + 2400000 + 15,
          "the quoted 8 decompresses at the batch prices");

    /* against brute force over small baskets (state set directly) */
    static const uint64_t qs[] = {0, 1, 2, 3, 4, 6, 9, 10, 12, 35};
    static const zenny_price_t ps[] = {{1, 1}, {1, 2}, {3, 4}, {2, 3}, {5, 6}, {0, 7}};
    uint32_t bad = 0, cases = 0;
    for (uint64_t n = 1; n <= 12; n++)
        for (uint32_t a = 0; a < 10; a++)
            for (uint32_t b = 0; b < 10; b++)
                for (uint32_t p = 0; p < 7; p++) {
                    zenny_init(&Q, verify, NULL);
                    Q.chip[0].used = true;
                    Q.chip[0].n_ver = 1;
                    zenny_version_t *v = &Q.chip[0].ver[0];
                    v->chips = n;
                    v->n_legs = 2;
                    v->leg[0].qty = qs[a];
                    v->leg[1].qty = qs[b];
                    v->price[0] = v->price[1] = (zenny_price_t){1, 1}; /* stored: always exact */
                    zenny_price_t bp2[2] = {ps[p % 6], ps[(p + 2) % 6]};
                    const zenny_price_t *use = p == 6 ? NULL : bp2;
                    for (uint64_t w = 0; w <= n + 1; w++) {
                        cases++;
                        if (zenny_quote(&Q, 0, w, use) != brute(v, w, use)) {
                            if (bad++ < 5)
                                printf("  quote n=%llu q=%llu,%llu p=%u w=%llu\n",
                                       (unsigned long long) n, (unsigned long long) qs[a],
                                       (unsigned long long) qs[b], p, (unsigned long long) w);
                        }
                    }
                }
    CHECK(bad == 0 && cases > 50000, "quote matches the brute-force search");
}

static void test_decompress(void)
{
    zenny_init(&Z, verify, NULL);
    kit_t k;
    uint32_t s;
    base(&k, 0, 1);
    mint_kit(&k, &s);
    zenny_price_t bp[3] = {{1, 1}, {1, 10}, {40000, 1}};
    zenny_receipt_t rc;
    memset(&rc, 0xEE, sizeof rc);
    CHECK(zenny_decompress(&Z, s, 6, bp, 300, &rc) == ZENNY_OK, "tap-to-pay 6 chips");
    CHECK(rc.out[0] == 3600000 && rc.out[1] == 18000000 && rc.out[2] == 15 && rc.n_legs == 3,
          "each leg pays qty * 6 / 10");
    CHECK(rc.value[0] == 3600000 && rc.value[1] == 1800000 && rc.value[2] == 600000 &&
              rc.total == 6000000 && rc.fee == 5333,
          "valued at the batch price: 6,000,000, fee floor(0.08889%) = 5333");
    CHECK(rc.seq == 0 && rc.version == 1 && rc.chips == 6 && rc.tick == 300 &&
              !memcmp(rc.chip_id, Z.chip[s].id, 32) && rc.out[3] == 0 && rc.value[7] == 0,
          "the receipt names the chip, version, count and tick");
    const zenny_version_t *v = zenny_current(&Z, s);
    CHECK(v->chips == 4 && v->leg[0].qty == 2400000 && v->leg[1].qty == 12000000 &&
              v->leg[2].qty == 10 && v->op == ZENNY_OP_DECOMPRESS && v->tick == 300 &&
              v->price[2].num == 40000 && v->leg[0].weight == 6,
          "the remainder: 4 chips, legs reduced, valuation prices kept");
    CHECK(zenny_check(&Z, s) == ZENNY_OK && zenny_verify_chain(&Z, s) == ZENNY_OK,
          "claim == value for the remainder; the chain verifies");
    zenny_receipt_t g;
    CHECK(zenny_receipt_get(&Z, 0, &g) == ZENNY_OK && !memcmp(&g, &rc, sizeof g),
          "the receipt is in the log");

    uint32_t nv = Z.chip[s].n_ver;
    CHECK(zenny_decompress(&Z, s, 3, bp, 1, &rc) == ZENNY_ERR_INEXACT && Z.chip[s].n_ver == nv,
          "3 of 4 chips would pay 7.5 commodity units: refused");
    CHECK(zenny_decompress(&Z, s, 5, bp, 1, &rc) == ZENNY_ERR_FUNDS && Z.chip[s].n_ver == nv,
          "5 of 4 chips: overdraft fails closed");
    CHECK(zenny_decompress(&Z, s, 0, bp, 1, &rc) == ZENNY_ERR_ARG &&
              zenny_decompress(&Z, s, 2, NULL, 1, &rc) == ZENNY_ERR_ARG &&
              zenny_decompress(&Z, 6, 2, bp, 1, &rc) == ZENNY_ERR_ARG &&
              zenny_decompress(NULL, s, 2, bp, 1, &rc) == ZENNY_ERR_ARG,
          "decompress arguments checked");
    zenny_price_t b0[3] = {{1, 1}, {1, 0}, {1, 1}};
    CHECK(zenny_decompress(&Z, s, 2, b0, 1, &rc) == ZENNY_ERR_PRICE, "batch den 0 refused");
    zenny_price_t bi[3] = {{1, 1}, {1, 10}, {3, 4}};
    CHECK(zenny_decompress(&Z, s, 2, bi, 1, &rc) == ZENNY_ERR_INEXACT,
          "a batch value that does not divide exactly refused");
    zenny_price_t bo[3] = {{1, 1}, {UINT64_MAX, 1}, {1, 1}};
    CHECK(zenny_decompress(&Z, s, 2, bo, 1, &rc) == ZENNY_ERR_OVERFLOW,
          "a batch value above 64 bits refused");
    zenny_price_t bs[3] = {{1ull << 63, 1200000}, {1ull << 63, 6000000}, {1, 1}};
    CHECK(zenny_decompress(&Z, s, 2, bs, 1, &rc) == ZENNY_ERR_OVERFLOW,
          "a batch total above 64 bits refused");
    CHECK(Z.chip[s].n_ver == nv && Z.r_next == 1, "refusals change nothing");
    CHECK(zenny_decompress(&Z, s, 4, bp, 2, NULL) == ZENNY_OK && zenny_current(&Z, s)->chips == 0,
          "the last 4 chips (no receipt copy wanted)");
    CHECK(zenny_decompress(&Z, s, 1, bp, 2, NULL) == ZENNY_ERR_FUNDS, "nothing left: FUNDS");

    /* the fee boundary: 1124 pays 0, 1125 pays 1 */
    base(&k, 0, 2);
    k.r.n_legs = 1, k.r.leg[0].qty = 1125, k.r.kappa = 1125;
    mint_kit(&k, &s);
    zenny_price_t one[1] = {{1, 1}}, almost[1] = {{1124, 1125}};
    CHECK(zenny_decompress(&Z, s, 1, almost, 1, &rc) == ZENNY_OK && rc.total == 1124 && rc.fee == 0,
          "1124 reference units: fee 0");
    base(&k, 0, 3);
    k.r.n_legs = 1, k.r.leg[0].qty = 1125, k.r.kappa = 1125;
    mint_kit(&k, &s);
    CHECK(zenny_decompress(&Z, s, 1, one, 1, &rc) == ZENNY_OK && rc.total == 1125 && rc.fee == 1,
          "1125 reference units: fee 1");
}

static void test_receipts(void)
{
    zenny_init(&Z, verify, NULL);
    kit_t k;
    uint32_t s[2];
    for (int i = 0; i < 2; i++) {
        base(&k, 0, (uint64_t) i + 1);
        k.r.leg[2].qty = 50, k.r.price[2] = px(20000, 1); /* every c pays exactly */
        mint_kit(&k, &s[i]);
    }
    zenny_price_t bp[3] = {{1, 1}, {1, 10}, {20000, 1}};
    zenny_receipt_t rc, g;
    uint32_t ok = 0;
    for (uint32_t i = 0; i < ZENNY_MAX_RECEIPTS; i++)
        ok += zenny_decompress(&Z, s[i & 1], 1, bp, i, &rc) == ZENNY_OK;
    CHECK(ok == ZENNY_MAX_RECEIPTS && rc.seq == ZENNY_MAX_RECEIPTS - 1, "16 receipts");
    uint32_t nv = Z.chip[s[0]].n_ver;
    CHECK(zenny_decompress(&Z, s[0], 1, bp, 99, &rc) == ZENNY_ERR_LOG && Z.chip[s[0]].n_ver == nv,
          "a full receipt ring refuses (tax records are never overwritten)");
    CHECK(zenny_receipt_get(&Z, 3, &g) == ZENNY_OK && g.seq == 3 && g.tick == 3 &&
              !memcmp(g.chip_id, Z.chip[s[1]].id, 32),
          "receipt 3 is chip 2's");
    CHECK(zenny_receipt_get(&Z, 16, &g) == ZENNY_ERR_ARG &&
              zenny_receipt_get(&Z, 0, NULL) == ZENNY_ERR_ARG &&
              zenny_receipt_get(NULL, 0, &g) == ZENNY_ERR_ARG,
          "a receipt not yet written is refused");
    CHECK(zenny_receipt_ack(&Z, 17) == ZENNY_ERR_ARG && zenny_receipt_ack(NULL, 1) == ZENNY_ERR_ARG,
          "cannot drain receipts not yet written");
    CHECK(zenny_receipt_ack(&Z, 5) == ZENNY_OK && Z.r_acked == 5, "drain 0..4");
    CHECK(zenny_receipt_get(&Z, 4, &g) == ZENNY_ERR_ARG && zenny_receipt_get(&Z, 5, &g) == ZENNY_OK,
          "drained receipts are gone, the rest remain");
    CHECK(zenny_receipt_ack(&Z, 4) == ZENNY_ERR_ARG && zenny_receipt_ack(&Z, 5) == ZENNY_OK,
          "draining never goes backwards; repeating is fine");
    CHECK(zenny_decompress(&Z, s[0], 1, bp, 40, &rc) == ZENNY_OK && rc.seq == 16 &&
              zenny_receipt_get(&Z, 16, &g) == ZENNY_OK && g.tick == 40 &&
              zenny_receipt_get(&Z, 5, &g) == ZENNY_OK && g.seq == 5,
          "after draining the ring wraps without losing an undrained receipt");
    CHECK(zenny_receipt_ack(&Z, 17) == ZENNY_OK && zenny_receipt_get(&Z, 16, &g) == ZENNY_ERR_ARG,
          "drain everything");
}

static void test_chain(void)
{
    zenny_init(&Z, verify, NULL);
    kit_t k;
    uint32_t s;
    base(&k, ZENNY_RULE_ALLOW_REBALANCE | ZENNY_RULE_BILATERAL, 1);
    mint_kit(&k, &s);
    rebased(&k, ZENNY_RULE_ALLOW_REBALANCE | ZENNY_RULE_BILATERAL);
    rebal(&k, s);
    zenny_price_t bp[3] = {{1, 1}, {1, 10}, {50000, 1}};
    zenny_decompress(&Z, s, 5, bp, 9, NULL);
    zenny_chip_t *ch = &Z.chip[s];
    uint8_t ref[32];
    for (uint32_t i = 0; i < ch->n_ver; i++) {
        ref_ver(ch, &ch->ver[i], ref);
        CHECK(!memcmp(ref, ch->ver[i].hash, 32) &&
                  (i == 0 || !memcmp(ch->ver[i].prev, ch->ver[i - 1].hash, 32)),
              "every version hash is the documented bytes and links to the last");
    }
    CHECK(ch->n_ver == 3 && zenny_verify_chain(&Z, s) == ZENNY_OK, "mint, rebalance, decompress");
    static zenny_chip_t save;
    save = *ch;
#define TAMPER(edit, msg)                                                                          \
    do {                                                                                           \
        *ch = save;                                                                                \
        edit;                                                                                      \
        CHECK(zenny_verify_chain(&Z, s) == ZENNY_ERR_CHAIN, msg);                                  \
    } while (0)
    TAMPER(ch->ver[1].leg[0].qty++, "a changed quantity is detected");
    TAMPER(ch->ver[2].tick++, "a changed tick is detected");
    TAMPER(ch->ver[2].chips++, "a changed chip count is detected");
    TAMPER(ch->ver[2].op = ZENNY_OP_REBALANCE, "a changed op is detected");
    TAMPER(ch->ver[1].version = 5, "a changed version number is detected");
    TAMPER(ch->ver[1].price[2].den++, "a changed price is detected");
    TAMPER(ch->ver[1].n_legs = 2, "a changed leg count is detected");
    TAMPER(ch->ver[0].hash[0] ^= 1, "a changed version-0 hash is detected");
    TAMPER(ch->ver[2].hash[31] ^= 1, "a changed last hash is detected");
    TAMPER(ch->ver[0].prev[0] ^= 1, "a nonzero genesis link is detected");
    TAMPER(ch->ver[0].leg[1].qty++, "a changed mint quantity is detected (id)");
    TAMPER(ch->ver[0].n_legs = 9, "an impossible leg count is detected");
    TAMPER(ch->kappa = 2000000, "a changed kappa is detected");
    TAMPER(ch->rules = ZENNY_RULE_ALLOW_REBALANCE, "changed rules are detected");
    TAMPER(ch->minter = OTHER, "a changed minter is detected");
    TAMPER(ch->co_minter = OTHER, "a changed co-minter is detected");
    TAMPER(ch->nonce = 7, "a changed nonce is detected");
    TAMPER(ch->id[31] ^= 1, "a changed id is detected");
    TAMPER(ch->ver[0].prev[31] ^= 1, "a changed last genesis byte is detected");
    /* a forged link, rehashed consistently: only the link check sees it */
    *ch = save;
    ch->ver[1].prev[31] ^= 1;
    ref_ver(ch, &ch->ver[1], ch->ver[1].hash);
    pay_memcpy(ch->ver[2].prev, ch->ver[1].hash, 32);
    ref_ver(ch, &ch->ver[2], ch->ver[2].hash);
    CHECK(zenny_verify_chain(&Z, s) == ZENNY_ERR_CHAIN, "a rehashed broken link is detected");
    TAMPER(ch->n_ver = 0, "an empty log is detected");
    TAMPER(ch->n_ver = ZENNY_MAX_VERSIONS + 1, "an impossible log length is detected");
    /* a version 0 that is not a mint, rehashed consistently */
    *ch = save;
    ch->ver[0].op = ZENNY_OP_REBALANCE;
    ref_ver(ch, &ch->ver[0], ch->ver[0].hash);
    pay_memcpy(ch->ver[1].prev, ch->ver[0].hash, 32);
    ref_ver(ch, &ch->ver[1], ch->ver[1].hash);
    pay_memcpy(ch->ver[2].prev, ch->ver[1].hash, 32);
    ref_ver(ch, &ch->ver[2], ch->ver[2].hash);
    CHECK(zenny_verify_chain(&Z, s) == ZENNY_ERR_CHAIN, "version 0 must be the mint");
    ch->ver[0].op = ZENNY_OP_MINT;
    ref_ver(ch, &ch->ver[0], ch->ver[0].hash);
    pay_memcpy(ch->ver[1].prev, ch->ver[0].hash, 32);
    ref_ver(ch, &ch->ver[1], ch->ver[1].hash);
    pay_memcpy(ch->ver[2].prev, ch->ver[1].hash, 32);
    ref_ver(ch, &ch->ver[2], ch->ver[2].hash);
    CHECK(zenny_verify_chain(&Z, s) == ZENNY_OK, "rebuilt with the reference hash it verifies");
    *ch = save;
    CHECK(zenny_verify_chain(&Z, s) == ZENNY_OK, "restored, it verifies");
    CHECK(zenny_verify_chain(&Z, 5) == ZENNY_ERR_ARG &&
              zenny_verify_chain(NULL, s) == ZENNY_ERR_ARG,
          "no chip: ARG");

    /* zenny_check on a tampered current version */
    zenny_version_t *v = &ch->ver[ch->n_ver - 1];
    v->chips++;
    CHECK(zenny_check(&Z, s) == ZENNY_ERR_CLAIM, "a chip count above the value: CLAIM");
    *ch = save;
    v->leg[0].qty += 1000000;
    CHECK(zenny_check(&Z, s) == ZENNY_ERR_WEIGHT, "a leg off its weight: WEIGHT");
    *ch = save;
    ch->kappa = 0;
    CHECK(zenny_check(&Z, s) == ZENNY_ERR_CHAIN, "kappa 0: CHAIN");
    *ch = save;
    v->n_legs = ZENNY_MAX_LEGS + 1;
    CHECK(zenny_check(&Z, s) == ZENNY_ERR_CHAIN, "an impossible leg count: CHAIN");
    *ch = save;
    v->n_legs = ZENNY_MAX_LEGS;
    CHECK(zenny_check(&Z, s) == ZENNY_ERR_KIND, "eight legs are read (the rest are kind 0)");
    *ch = save;
    CHECK(zenny_check(&Z, s) == ZENNY_OK && zenny_check(&Z, 5) == ZENNY_ERR_ARG &&
              zenny_check(NULL, s) == ZENNY_ERR_ARG,
          "restored check; no chip: ARG");
    CHECK(zenny_current(&Z, 5) == NULL && zenny_current(NULL, 0) == NULL, "no current version");
    uint8_t id[32] = {0};
    CHECK(!zenny_find(&Z, id, NULL) && !zenny_find(&Z, NULL, NULL) && !zenny_find(NULL, id, NULL),
          "find misses");
    /* a freed slot is not found and is reused */
    Z.chip[s].used = false;
    CHECK(!zenny_find(&Z, save.id, NULL), "an unused slot is not searched");
}

/* ---- Z7 top-up ---- */
static void cap_sign(uint32_t s, uint32_t i, uint64_t cap, uint64_t exp, uint8_t sig[32])
{
    uint8_t d[32], m[ZENNY_SIG_MSG];
    const zenny_leg_t *l = &zenny_current(&Z, s)->leg[i];
    zenny_cap_digest(Z.chip[s].id, i, l, cap, exp, d);
    zenny_sign_msg(ZENNY_SIG_ATTEST, d, m);
    sign(l->custodian, m, sizeof m, sig);
}
static zenny_status_t attest(uint32_t s, uint32_t i, uint64_t cap, uint64_t exp)
{
    uint8_t sig[32];
    cap_sign(s, i, cap, exp, sig);
    return zenny_attest(&Z, s, i, cap, exp, sig, 32);
}

/* small basket, kappa 1: VBILL 6 @1/1 w6, FIAT 1 @4/1 w4 -> 10 chips */
static uint32_t small(uint32_t rules, uint64_t nonce)
{
    kit_t k;
    uint32_t s = 99;
    base(&k, rules, nonce);
    k.r.kappa = 1;
    k.r.n_legs = 2;
    k.r.leg[0] = leg(ZENNY_VBILL, 1, 0, 6, 6);
    k.r.leg[1] = leg(ZENNY_FIAT, 840, 0, 1, 4);
    k.r.price[1] = px(4, 1);
    CHECK(mint_kit(&k, &s) == ZENNY_OK, "small basket minted");
    return s;
}

static zenny_t TQ;
static uint64_t brute_topup(uint32_t s, uint64_t want, const zenny_price_t *pr)
{
    for (uint64_t a = want; a > 0; a--) {
        TQ = Z;
        if (zenny_topup(&TQ, s, a, pr, 5, 1, NULL) == ZENNY_OK) return a;
    }
    return 0;
}

static void test_topup(void)
{
    zenny_init(&Z, verify, NULL);
    kit_t k;
    uint32_t s, s2;
    base(&k, ZENNY_RULE_ALLOW_TOPUP, 1);
    CHECK(mint_kit(&k, &s) == ZENNY_OK, "a top-up chip mints");
    zenny_price_t mp[3] = {{1, 1}, {1, 10}, {40000, 1}};
    uint64_t added = 77;
    CHECK(zenny_topup(&Z, s, 2000000, mp, 42, 10, &added) == ZENNY_ERR_UNBACKED && added == 77 &&
              Z.chip[s].n_ver == 1,
          "no standing attestation: the commodity leg is unbacked");
    CHECK(zenny_topup_quote(&Z, s, 5000000, mp, 10) == 0, "and the quote is 0");

    /* standing attestations */
    uint8_t sig[32], d[32], ref[32];
    cap_sign(s, 2, 30, 1000, sig);
    CHECK(zenny_attest(&Z, s, 2, 30, 1000, sig, 31) == ZENNY_ERR_ATTEST, "short signature refused");
    sig[0] ^= 1;
    CHECK(zenny_attest(&Z, s, 2, 30, 1000, sig, 32) == ZENNY_ERR_ATTEST, "bad signature refused");
    CHECK(zenny_attest(&Z, s, 2, 31, 1000, NULL, 32) == ZENNY_ERR_ATTEST, "no signature refused");
    CHECK(attest(s, 2, 30, 1000) == ZENNY_OK && Z.chip[s].att_cap[2] == 30 &&
              Z.chip[s].att_exp[2] == 1000,
          "custodian attests 30 units until tick 1000");
    CHECK(attest(s, 2, 99, 1000) == ZENNY_ERR_STALE && attest(s, 2, 99, 999) == ZENNY_ERR_STALE &&
              Z.chip[s].att_cap[2] == 30,
          "an attestation that does not expire later is refused");
    CHECK(attest(s, 0, 30, 2000) == ZENNY_ERR_ARG && attest(s, 1, 30, 2000) == ZENNY_ERR_ARG,
          "only commodity legs take attestations");
    CHECK(zenny_attest(&Z, s, 3, 1, 2000, sig, 32) == ZENNY_ERR_ARG &&
              zenny_attest(&Z, 6, 2, 1, 2000, sig, 32) == ZENNY_ERR_ARG &&
              zenny_attest(NULL, s, 2, 1, 2000, sig, 32) == ZENNY_ERR_ARG,
          "attest arguments checked");
    Z.verify = NULL;
    CHECK(zenny_attest(&Z, s, 2, 1, 2000, sig, 32) == ZENNY_ERR_ARG, "no verify hook");
    Z.verify = verify;
    {
        buf_t b = {{0}, 0};
        const zenny_leg_t *l = &zenny_current(&Z, s)->leg[2];
        putb(&b, "zxv/zenny/cap/1.", 16);
        putb(&b, Z.chip[s].id, 32);
        put(&b, 2);
        put(&b, l->kind);
        put(&b, l->custodian);
        put(&b, l->asset);
        put(&b, 0x123456789ull);
        put(&b, 0xABCDEF01ull);
        sha256(b.b, b.n, ref);
        zenny_cap_digest(Z.chip[s].id, 2, l, 0x123456789ull, 0xABCDEF01ull, d);
        CHECK(!memcmp(d, ref, 32), "the cap digest is exactly the documented bytes");
    }

    /* the worked top-up: 2,000,000 buys 1,200,000 VBILL, 6,000,000 FIAT, 5 commodity */
    CHECK(zenny_topup_quote(&Z, s, 3500000, mp, 10) == 2000000 &&
              zenny_topup_quote(&Z, s, 1999999, mp, 10) == 0,
          "top-up steps of 2,000,000 (the commodity takes 5 units per 2 chips)");
    CHECK(zenny_topup(&Z, s, 2000000, mp, 42, 10, &added) == ZENNY_OK && added == 2,
          "top-up of 2,000,000 mints 2 chips");
    const zenny_version_t *v = zenny_current(&Z, s);
    CHECK(v->op == ZENNY_OP_TOPUP && v->chips == 12 && v->recipient == 42 && v->tick == 10 &&
              v->leg[0].qty == 7200000 && v->leg[1].qty == 36000000 && v->leg[2].qty == 30 &&
              v->leg[0].weight == 6 && v->version == 1,
          "legs grew in exactly the weights; recipient recorded");
    CHECK(zenny_check(&Z, s) == ZENNY_OK && zenny_verify_chain(&Z, s) == ZENNY_OK,
          "claim == value after the top-up; the chain verifies");
    ref_ver(&Z.chip[s], v, ref);
    CHECK(!memcmp(ref, v->hash, 32), "the top-up version hash includes the recipient");
    CHECK(zenny_topup(&Z, s, 2000000, mp, 42, 11, NULL) == ZENNY_ERR_UNBACKED &&
              Z.chip[s].n_ver == 2,
          "35 commodity units would exceed the cap of 30");
    CHECK(attest(s, 2, 35, 1100) == ZENNY_OK, "a fresh attestation for 35");
    CHECK(zenny_topup(&Z, s, 2000000, mp, 42, 1100, NULL) == ZENNY_ERR_UNBACKED,
          "at the expiry tick the attestation has lapsed");
    CHECK(zenny_topup_quote(&Z, s, 2000000, mp, 1100) == 0, "a lapsed attestation quotes 0");
    CHECK(zenny_topup(&Z, s, 2000000, mp, 43, 1099, NULL) == ZENNY_OK &&
              zenny_current(&Z, s)->leg[2].qty == 35,
          "one tick earlier, up to exactly the cap, it is accepted");
    CHECK(zenny_topup(&Z, s, 1500000, mp, 42, 20, NULL) == ZENNY_ERR_INEXACT,
          "1,500,000 is not whole chips");
    CHECK(zenny_topup(&Z, s, 1000000, mp, 42, 20, NULL) == ZENNY_ERR_INEXACT,
          "one chip would buy 2.5 commodity units");
    zenny_price_t p0[3] = {{1, 1}, {1, 0}, {40000, 1}}, pn[3] = {{1, 1}, {0, 10}, {40000, 1}};
    CHECK(zenny_topup(&Z, s, 2000000, p0, 42, 20, NULL) == ZENNY_ERR_PRICE &&
              zenny_topup(&Z, s, 2000000, pn, 42, 20, NULL) == ZENNY_ERR_PRICE,
          "den 0 or num 0 cannot buy");
    zenny_price_t po[3] = {{1, UINT64_MAX}, {1, 10}, {40000, 1}};
    CHECK(zenny_topup(&Z, s, 2000000, po, 42, 20, NULL) == ZENNY_ERR_OVERFLOW,
          "a quantity above 64 bits refused");
    CHECK(zenny_topup(&Z, s, 0, mp, 42, 20, NULL) == ZENNY_ERR_ARG &&
              zenny_topup(&Z, s, 2000000, NULL, 42, 20, NULL) == ZENNY_ERR_ARG &&
              zenny_topup(&Z, 7, 2000000, mp, 42, 20, NULL) == ZENNY_ERR_ARG &&
              zenny_topup(NULL, s, 2000000, mp, 42, 20, NULL) == ZENNY_ERR_ARG,
          "top-up arguments checked");
    CHECK(zenny_topup_quote(&Z, s, 2000000, NULL, 20) == 0 &&
              zenny_topup_quote(&Z, 7, 2000000, mp, 20) == 0 &&
              zenny_topup_quote(&Z, s, 2000000, pn, 20) == 0,
          "quote: no chip, no prices or a num 0 quotes 0");
    zenny_price_t pk[3] = {{0xFFFFFFFFFFFFFFC5ull, 1}, {1, 10}, {40000, 1}};
    CHECK(zenny_topup_quote(&Z, s, UINT64_MAX, pk, 20) == 0, "a step above 64 bits quotes 0");
    zenny_price_t pl[3] = {{1ull << 40, 1}, {205891132094649ull, 10}, {40000, 1}};
    CHECK(zenny_topup_quote(&Z, s, UINT64_MAX, pl, 20) == 0, "a price lcm above 64 bits quotes 0");

    /* moved prices: the old legs are no longer worth the claim */
    uint32_t sm = small(ZENNY_RULE_ALLOW_TOPUP, 2);
    zenny_price_t sp[2] = {{1, 1}, {4, 1}}, dbl[2] = {{2, 1}, {8, 1}}, skew[2] = {{1, 1}, {8, 1}};
    CHECK(zenny_topup(&Z, sm, 20, dbl, 1, 1, NULL) == ZENNY_ERR_CLAIM,
          "both prices doubled: the basket is worth 2x its claim, refused");
    CHECK(zenny_topup(&Z, sm, 20, skew, 1, 1, NULL) == ZENNY_ERR_WEIGHT,
          "one price moved: the weights no longer hold, refused");
    CHECK(zenny_topup_quote(&Z, sm, 20, dbl, 1) == 0, "moved prices quote 0");
    CHECK(zenny_topup(&Z, sm, 1, sp, 1, 1, NULL) == ZENNY_ERR_INEXACT,
          "1 cannot be split 6:4 exactly");
    CHECK(zenny_topup(&Z, sm, 5, sp, 1, 1, NULL) == ZENNY_ERR_INEXACT,
          "5 buys 2 reference units of FIAT at 4: half a unit, refused");
    CHECK(zenny_topup_quote(&Z, sm, 19, sp, 1) == 10 && zenny_topup_quote(&Z, sm, 9, sp, 1) == 0,
          "small basket: steps of 10");
    CHECK(zenny_topup_unit(&Z, sm, sp) == 10 && zenny_topup_unit(&Z, sm, dbl) == 20 &&
              zenny_topup_unit(&Z, s, mp) == 2000000,
          "the unit: 10 at 1:4, 20 at 2:8 (it changes with prices), 2,000,000 for the worked chip");
    CHECK(zenny_topup_unit(&Z, s, pn) == 0 && zenny_topup_unit(&Z, s, NULL) == 0 &&
              zenny_topup_unit(&Z, 7, mp) == 0 && zenny_topup_unit(&Z, s, pk) == 0,
          "no unit: num 0, no prices, no chip, above 64 bits");
    uint32_t bad = 0, ubad = 0;
    for (uint64_t w = 0; w <= 1000; w++) {
        uint64_t u = zenny_topup_unit(&Z, sm, sp);
        if (zenny_topup_quote(&Z, sm, w, sp, 1) != w - w % u) ubad++;
    }
    CHECK(ubad == 0, "quote(want) == want - want % unit for every want up to 1000");
    for (uint64_t w = 0; w <= 35; w++)
        if (zenny_topup_quote(&Z, sm, w, sp, 1) != brute_topup(sm, w, sp)) bad++;
    CHECK(bad == 0, "the top-up quote matches a brute-force search");

    /* rules and backing by kind */
    uint32_t nt = small(0, 3);
    CHECK(zenny_topup(&Z, nt, 10, sp, 1, 1, NULL) == ZENNY_ERR_NOT_ALLOWED,
          "without ALLOW_TOPUP refused");
    base(&k, ZENNY_RULE_ALLOW_TOPUP, 4);
    k.r.kappa = 1, k.r.n_legs = 2;
    k.r.leg[0] = leg(ZENNY_VBILL, 1, 0, 5, 1);
    k.r.leg[1] = leg(ZENNY_COMPUTE, 9, 0, 5, 1);
    k.r.price[1] = px(1, 1);
    mint_kit(&k, &s2);
    zenny_price_t one[2] = {{1, 1}, {1, 1}};
    CHECK(zenny_topup(&Z, s2, 2, one, 1, 1, NULL) == ZENNY_ERR_UNBACKED,
          "a weighted COMPUTE leg is unbacked");
    base(&k, ZENNY_RULE_ALLOW_TOPUP, 5);
    k.r.kappa = 1, k.r.n_legs = 3;
    k.r.leg[0] = leg(ZENNY_VBILL, 1, 0, 5, 1);
    k.r.leg[1] = leg(ZENNY_COMPUTE, 9, 0, 0, 0);
    k.r.leg[2] = leg(ZENNY_COMMODITY, 79, CUST, 0, 0);
    k.r.price[1] = px(1, 1), k.r.price[2] = px(1, 1);
    mint_kit(&k, &s2);
    zenny_price_t one3[3] = {{1, 1}, {1, 1}, {1, 1}};
    CHECK(zenny_topup(&Z, s2, 3, one3, 7, 1, &added) == ZENNY_OK && added == 3 &&
              zenny_current(&Z, s2)->leg[1].qty == 0 && zenny_check(&Z, s2) == ZENNY_OK,
          "weight-0 COMPUTE and unattested COMMODITY legs take nothing and do not block");
    CHECK(zenny_topup_quote(&Z, s2, 7, one3, 1) == 7, "and quote every whole chip");
    zenny_price_t z3[3] = {{1, 1}, {0, 1}, {0, 1}};
    CHECK(zenny_topup_unit(&Z, s2, z3) == 1, "a weight-0 leg's price does not matter");
    zenny_price_t pk3[3] = {{0xFFFFFFFFFFFFFFC5ull, 1}, {1, 10}, {20000, 1}};
    CHECK(zenny_topup_unit(&Z, s, pk3) == 0, "a unit above 64 bits is 0");
    base(&k, 0, 8);
    k.r.kappa = 1;
    k.r.leg[2] = leg(ZENNY_FIAT, 978, 0, 1, 1);
    k.r.leg[0].qty = 6, k.r.leg[1].qty = 30, k.r.price[2] = px(1, 1);
    CHECK(mint_kit(&k, &s2) == ZENNY_OK, "weights 6:3:1 at kappa 1");
    zenny_price_t p631[3] = {{1, 1}, {1, 10}, {1, 1}};
    CHECK(zenny_topup_unit(&Z, s2, p631) == 10, "6:3:1 needs multiples of 10 (not just 5)");

    /* overflow of the chip count */
    base(&k, ZENNY_RULE_ALLOW_TOPUP, 6);
    k.r.kappa = 1, k.r.n_legs = 1;
    k.r.leg[0] = leg(ZENNY_VBILL, 1, 0, 1ull << 62, 1);
    k.r.price[0] = px(2, 1);
    mint_kit(&k, &s2);
    zenny_price_t two[1] = {{2, 1}};
    CHECK(zenny_topup(&Z, s2, 1ull << 63, two, 1, 1, NULL) == ZENNY_ERR_OVERFLOW,
          "a chip count above 64 bits refused");
    zenny_price_t half[1] = {{1, 2}};
    CHECK(zenny_topup(&Z, s2, 1ull << 63, half, 1, 1, NULL) == ZENNY_ERR_OVERFLOW,
          "a leg quantity above 64 bits refused");

    /* a full version log */
    while (Z.chip[sm].n_ver < ZENNY_MAX_VERSIONS) zenny_topup(&Z, sm, 10, sp, 1, 1, NULL);
    CHECK(zenny_topup(&Z, sm, 10, sp, 1, 1, NULL) == ZENNY_ERR_FULL &&
              zenny_verify_chain(&Z, sm) == ZENNY_OK && zenny_current(&Z, sm)->chips == 160,
          "15 top-ups then the log is full; still exact (160 chips)");

    /* the raffle adapter */
    uint32_t r = small(ZENNY_RULE_ALLOW_TOPUP, 7);
    zenny_raffle_ctx_t rc = {&Z, r, sp, 55, 9, ZENNY_ERR_ARG};
    int who = 0;
    CHECK(zenny_raffle_credit(&rc, ZENNY_RAFFLE_DEST_NAV, &who, 20) && rc.last == ZENNY_OK &&
              zenny_current(&Z, r)->chips == 30 && zenny_current(&Z, r)->recipient == 55 &&
              zenny_current(&Z, r)->tick == 9,
          "a raffle NAV credit of 20 mints 20 chips to the recipient");
    uint32_t nv = Z.chip[r].n_ver;
    rc.last = ZENNY_OK;
    CHECK(!zenny_raffle_credit(&rc, ZENNY_RAFFLE_DEST_NAV, &who, 5) &&
              rc.last == ZENNY_ERR_INEXACT && Z.chip[r].n_ver == nv,
          "an inexact credit returns false and changes nothing");
    rc.last = ZENNY_ERR_ARG;
    CHECK(!zenny_raffle_credit(&rc, 2, &who, 20) && !zenny_raffle_credit(&rc, 4, &who, 20) &&
              rc.last == ZENNY_ERR_ARG && Z.chip[r].n_ver == nv,
          "another destination returns false and changes nothing");
    CHECK(!zenny_raffle_credit(NULL, ZENNY_RAFFLE_DEST_NAV, &who, 20), "no context: false");
}

int main(void)
{
    test_mint();
    test_refusals();
    test_rebalance();
    test_quote();
    test_decompress();
    test_receipts();
    test_chain();
    test_topup();
    printf("PASS %d FAIL %d\n", pass, fail);
    return fail ? 1 : 0;
}
