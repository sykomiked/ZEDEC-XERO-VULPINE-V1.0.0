/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_mint.c — minting against attested capacity (mint.h M1-M11) on a real
 * pay_ledger, with REAL ML-DSA-65 signatures (pq_mldsa65_*) as the verify
 * hook: tampered messages, wrong keys and truncated signatures are refused.
 * The Zeckendorf split is cross-checked against vino_zeckendorf
 * (kernel/src/vino_stores) and an independent reference. A deterministic
 * randomized loop checks every invariant against a simple model. Integer
 * only: no floating point anywhere. */
#include <stdio.h>
#include <string.h>
#include "mint.h"
#include "pq_security.h"

/* vino_stores.h pulls in the triple ledger; only this one function is used */
int32_t vino_zeckendorf(uint64_t amount, uint32_t *rungs, uint32_t cap, uint32_t *n);

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

/* ---- ML-DSA-65 attestors ---- */
#define NKEYS 3
static uint8_t pk[NKEYS][PQ_MLDSA65_PK_BYTES], sk[NKEYS][PQ_MLDSA65_SK_BYTES];
static const uint32_t att_id[NKEYS] = {101, 102, 103};
static int verify_calls;

static bool mldsa_verify(void *ctx, uint32_t signer, const uint8_t *msg, uint32_t len,
                         const uint8_t *sig, uint32_t sig_len)
{
    (void) ctx;
    verify_calls++;
    if (sig_len != PQ_MLDSA65_SIG_BYTES) return false;
    for (int i = 0; i < NKEYS; i++)
        if (att_id[i] == signer) return pq_mldsa65_verify(pk[i], msg, len, NULL, 0, sig);
    return false;
}

static void sign_with(int key, const mint_capacity_t *c, uint8_t sig[PQ_MLDSA65_SIG_BYTES])
{
    uint8_t m[MINT_CLAIM_MSG];
    mint_claim_msg(c, m);
    pq_mldsa65_sign(sk[key], m, sizeof m, NULL, 0, NULL, sig);
}

/* ---- a cheap keyed verify for the property loop ---- */
static bool fake_verify(void *ctx, uint32_t signer, const uint8_t *msg, uint32_t len,
                        const uint8_t *sig, uint32_t sig_len)
{
    (void) ctx;
    uint8_t d[32];
    if (sig_len != 32) return false;
    pay_sha3_256(msg, len, d);
    d[0] ^= (uint8_t) signer;
    return memcmp(d, sig, 32) == 0;
}
static void fake_sign(const mint_capacity_t *c, uint8_t sig[32])
{
    uint8_t m[MINT_CLAIM_MSG];
    mint_claim_msg(c, m);
    pay_sha3_256(m, sizeof m, sig);
    sig[0] ^= (uint8_t) c->attestor;
}

static pay_ledger_t L;
static mint_t M;
static uint16_t usd, vbl;
static uint32_t viss, biss, hv1, hv2, hb1, hb2, husd, other_iss;
static const uint8_t SEED[32] = {7, 7, 7, [31] = 0x77};
#define J_A 0x415a41u /* operator jurisdiction codes */
#define J_B 0x425a42u
#define J_C 0x435a43u

static uint32_t open_acct(uint16_t as, uint32_t owner, uint8_t flags)
{
    uint32_t a = 0;
    pay_ledger_open(&L, owner, as, PAY_CAP_FINANCIAL, flags, &a);
    return a;
}
static uint64_t deb(uint32_t a)
{
    return pay_ledger_account(&L, a)->debit;
}
static uint64_t cred(uint32_t a)
{
    return pay_ledger_account(&L, a)->credit;
}

static void setup(void)
{
    pay_ledger_init(&L, NULL);
    pay_ledger_add_fiat(&L, "USD", 840, 2, &usd);
    pay_ledger_add_asset(&L, "VBL", 0, PAY_ASSET_UNIT, &vbl);
    viss = open_acct(L.vfv_asset, 1, PAY_ACCT_ISSUER);
    biss = open_acct(vbl, 2, PAY_ACCT_ISSUER);
    hv1 = open_acct(L.vfv_asset, 10, 0);
    hv2 = open_acct(L.vfv_asset, 11, 0);
    hb1 = open_acct(vbl, 10, 0);
    hb2 = open_acct(vbl, 11, 0);
    husd = open_acct(usd, 10, 0);
    other_iss = open_acct(usd, 3, PAY_ACCT_ISSUER);
    CHECK(mint_init(&M, &L, viss, biss, 100, true, mldsa_verify, NULL, SEED) == MINT_OK,
          "mint set up");
}

/* the standard policy: J_A 10% haircut cap 1e6, attestor 101 5%, units */
static void policy(void)
{
    mint_juris_set(&M, J_A, true, 1000, 1000000);
    mint_attestor_set(&M, att_id[0], true, 500);
    mint_attestor_set(&M, att_id[1], true, 0);
    mint_unit_set(&M, MINT_KIND_EWHR, 250, 1);  /* 2.50 USD per receipt unit */
    mint_unit_set(&M, MINT_KIND_COMPUTE, 3, 2); /* 1.5 minor units per compute unit */
    mint_unit_set(&M, MINT_KIND_KWH, 12, 1);    /* 0.12 USD per kWh */
}

static mint_capacity_t cap_of(uint32_t att, uint8_t idb, uint8_t kind, uint32_t juris, uint64_t qty,
                              uint64_t expiry)
{
    mint_capacity_t c;
    memset(&c, 0, sizeof c);
    c.attestor = att;
    c.id[0] = idb;
    c.id[31] = 0x5a;
    c.kind = kind;
    c.juris = juris;
    c.qty = qty;
    c.expiry = expiry;
    return c;
}

static uint8_t SIG[PQ_MLDSA65_SIG_BYTES];

static void test_init(void)
{
    setup();
    CHECK(M.ready && M.issuer[MINT_TARGET_VINO] == viss && M.issuer[MINT_TARGET_VBILL] == biss &&
              M.vbill_backing == 100 && M.exclusive && M.epoch == 0 && M.last_ledger == PAY_OK,
          "init records issuers, backing, exclusive");
    CHECK(memcmp(M.seed, SEED, 32) == 0, "seed copied");
    CHECK(mint_check(&M), "fresh mint audits clean");
    mint_t m2;
    CHECK(mint_init(NULL, &L, viss, biss, 1, true, mldsa_verify, 0, SEED) == MINT_ERR_ARG &&
              mint_init(&m2, NULL, viss, biss, 1, true, mldsa_verify, 0, SEED) == MINT_ERR_ARG &&
              mint_init(&m2, &L, viss, biss, 0, true, mldsa_verify, 0, SEED) == MINT_ERR_ARG &&
              mint_init(&m2, &L, viss, biss, 1, true, NULL, 0, SEED) == MINT_ERR_ARG,
          "NULL / zero backing / no verify refused");
    CHECK(mint_init(&m2, &L, viss, viss, 1, true, mldsa_verify, 0, SEED) == MINT_ERR_STATE,
          "same issuer twice refused");
    CHECK(mint_init(&m2, &L, hv1, biss, 1, true, mldsa_verify, 0, SEED) == MINT_ERR_STATE &&
              mint_init(&m2, &L, viss, hb1, 1, true, mldsa_verify, 0, SEED) == MINT_ERR_STATE,
          "non-issuer accounts refused");
    CHECK(!m2.ready && !mint_check(&m2), "refused init leaves no usable mint");
    CHECK(mint_init(&m2, &L, L.n_accounts, biss, 1, true, mldsa_verify, 0, SEED) ==
                  MINT_ERR_STATE &&
              mint_init(&m2, &L, viss, L.n_accounts + 5, 1, true, mldsa_verify, 0, SEED) ==
                  MINT_ERR_STATE,
          "missing accounts refused");
    uint32_t viss2 = open_acct(L.vfv_asset, 4, PAY_ACCT_ISSUER);
    CHECK(mint_init(&m2, &L, viss, viss2, 1, true, mldsa_verify, 0, SEED) == MINT_ERR_STATE,
          "two issuers of one asset refused");
    CHECK(mint_init(&m2, &L, viss, other_iss, 7, false, mldsa_verify, 0, NULL) == MINT_OK &&
              !m2.exclusive && m2.seed[0] == 0,
          "NULL seed is zeros; another asset pair accepted");
    CHECK(mint_set_backing(&m2, 0) == MINT_ERR_ARG && mint_set_backing(NULL, 5) == MINT_ERR_ARG &&
              mint_set_backing(&m2, 5) == MINT_OK && m2.vbill_backing == 5,
          "set_backing");
    m2.ready = false;
    CHECK(mint_set_backing(&m2, 6) == MINT_ERR_ARG && m2.vbill_backing == 5,
          "set_backing on a mint not set up refused");
}

static void test_config(void)
{
    setup();
    CHECK(mint_juris(&M, J_A) == NULL && mint_juris(NULL, J_A) == NULL, "table starts empty");
    CHECK(mint_juris_set(&M, J_A, true, 10001, 5) == MINT_ERR_ARG, "haircut above 10000 refused");
    CHECK(mint_juris_set(&M, J_A, true, 0, PAY_BAL_MAX) == MINT_ERR_ARG, "cap at 2^62 refused");
    CHECK(mint_juris_set(NULL, J_A, true, 0, 5) == MINT_ERR_ARG, "NULL refused");
    CHECK(mint_juris_set(&M, J_A, true, 10000, PAY_BAL_MAX - 1) == MINT_OK, "boundaries accepted");
    const mint_juris_t *j = mint_juris(&M, J_A);
    CHECK(j && j->enabled && j->haircut_bps == 10000 && j->cap == PAY_BAL_MAX - 1 &&
              j->code == J_A && j->used_epoch == 0,
          "entry recorded");
    CHECK(mint_juris_set(&M, J_A, false, 3, 7) == MINT_OK && j->haircut_bps == 3 && !j->enabled &&
              j->cap == 7,
          "update in place");
    bool ok = true;
    for (uint32_t i = 1; i < MINT_MAX_JURIS; i++)
        ok = ok && mint_juris_set(&M, 1000 + i, 0, 0, 0) == MINT_OK;
    CHECK(ok && mint_juris_set(&M, 99, true, 0, 1) == MINT_ERR_FULL, "juris table full");
    CHECK(mint_juris_set(&M, 1001, true, 0, 1) == MINT_OK, "a full table still updates");

    CHECK(mint_attestor_set(&M, 1, true, 10001) == MINT_ERR_ARG &&
              mint_attestor_set(NULL, 1, true, 0) == MINT_ERR_ARG,
          "attestor haircut above 10000 / NULL refused");
    CHECK(mint_attestor_set(&M, 1, true, 10000) == MINT_OK && M.att[0].haircut_bps == 10000 &&
              M.att[0].enabled && M.att[0].id == 1,
          "attestor recorded");
    CHECK(mint_attestor_set(&M, 1, false, 20) == MINT_OK && !M.att[0].enabled &&
              M.att[0].haircut_bps == 20 && !M.att[1].used,
          "attestor updated in place");
    ok = true;
    for (uint32_t i = 1; i < MINT_MAX_ATTESTORS; i++)
        ok = ok && mint_attestor_set(&M, 50 + i, 1, 0) == MINT_OK;
    CHECK(ok && mint_attestor_set(&M, 99, true, 0) == MINT_ERR_FULL, "attestor table full");

    CHECK(mint_unit_set(&M, MINT_KINDS, 1, 1) == MINT_ERR_ARG &&
              mint_unit_set(&M, MINT_KIND_KWH, 1, 0) == MINT_ERR_ARG &&
              mint_unit_set(NULL, 0, 1, 1) == MINT_ERR_ARG,
          "unit: bad kind / den 0 / NULL refused");
    CHECK(mint_unit_set(&M, MINT_KIND_KWH, 0, 3) == MINT_OK && M.unit[2].num == 0 &&
              M.unit[2].den == 3,
          "num 0 accepted (= unset)");
    CHECK(mint_unit_set(&M, MINT_KIND_KWH, 9, 4) == MINT_OK && M.unit[2].num == 9 &&
              M.unit[2].den == 4,
          "unit recorded");
    mint_t blank;
    memset(&blank, 0, sizeof blank);
    CHECK(mint_juris_set(&blank, 1, 1, 0, 0) == MINT_ERR_ARG &&
              mint_attestor_set(&blank, 1, 1, 0) == MINT_ERR_ARG &&
              mint_unit_set(&blank, 0, 1, 1) == MINT_ERR_ARG &&
              mint_epoch_advance(&blank) == MINT_ERR_ARG &&
              mint_epoch_advance(NULL) == MINT_ERR_ARG,
          "a mint not set up refuses configuration");
}

static void test_pure(void)
{
    CHECK(mint_haircut(10000, 0) == 10000 && mint_haircut(10000, 10000) == 0 &&
              mint_haircut(10000, 1) == 9999 && mint_haircut(9999, 1) == 9998 &&
              mint_haircut(1, 1) == 0 && mint_haircut(0, 5000) == 0,
          "haircut floors");
    CHECK(mint_haircut(12345, 10001) == 0 && mint_haircut(UINT64_MAX, 0) == UINT64_MAX &&
              mint_haircut(UINT64_MAX, 5000) == UINT64_MAX / 2,
          "haircut bps > 10000 -> 0; 128-bit middle");
    uint64_t g = 7, n = 7;
    CHECK(mint_value(10, 3, 2, 0, 0, &g, &n) && g == 15 && n == 15, "10 * 3/2 = 15");
    CHECK(mint_value(11, 3, 2, 0, 0, &g, &n) && g == 16 && n == 16, "11 * 3/2 = 16 (floor)");
    CHECK(mint_value(1000, 1, 1, 500, 1000, &g, &n) && g == 1000 && n == 855, "1000 -> 950 -> 855");
    /* order matters with floors: h(h(19, 5000), 3333) vs the other way */
    CHECK(mint_value(19, 1, 1, 5000, 3333, &g, &n) && n == 6 &&
              mint_haircut(mint_haircut(19, 3333), 5000) == 6 &&
              mint_value(7, 1, 1, 3333, 5000, &g, &n) && n == 2 &&
              mint_haircut(mint_haircut(7, 5000), 3333) == 2,
          "documented order: attestor first, then jurisdiction");
    CHECK(mint_value(3, 1, 1, 3000, 5000, &g, &n) && n == 1 &&
              mint_haircut(mint_haircut(3, 5000), 3000) == 0,
          "order is attestor then jurisdiction (the other order gives 0)");
    g = n = 9;
    CHECK(!mint_value(1, 1, 0, 0, 0, &g, &n) && g == 9, "den 0 refused");
    CHECK(!mint_value(UINT64_MAX, 3, 2, 0, 0, &g, &n) && g == 9, "gross over 64 bits refused");
    CHECK(mint_value(UINT64_MAX, 2, 2, 0, 0, &g, &n) && g == UINT64_MAX, "exact 64-bit gross");
    CHECK(!mint_value(1, 1, 1, 0, 0, NULL, &n) && !mint_value(1, 1, 1, 0, 0, &g, NULL), "NULL");
    CHECK(mint_coin_face(0) == 0 && mint_coin_face(1) == 1 && mint_coin_face(2) == 2 &&
              mint_coin_face(5) == 8 && mint_coin_face(45) == 1836311903ull &&
              mint_coin_face(46) == 0,
          "ladder faces");
    bool fib = true;
    for (uint32_t r = 3; r <= 45; r++)
        fib = fib && mint_coin_face(r) == mint_coin_face(r - 1) + mint_coin_face(r - 2);
    CHECK(fib, "every face is the sum of the two below");

    mint_coins_t c;
    CHECK(!mint_zeckendorf(5, NULL), "NULL coins");
    CHECK(mint_zeckendorf(0, &c) && c.top == 0 && c.n == 0, "0: no coins");
    CHECK(mint_zeckendorf(100, &c) && c.top == 0 && c.n == 3 && c.rung[0] == 10 && c.rung[1] == 5 &&
              c.rung[2] == 3,
          "100 = 89 + 8 + 3");
    CHECK(mint_zeckendorf(1836311903ull * 2 + 4, &c) && c.top == 2 && c.n == 2 && c.rung[0] == 3 &&
              c.rung[1] == 1,
          "past the top face: top coins repeat, rest split");
    CHECK(mint_zeckendorf(1836311902ull, &c) && c.top == 0 && c.n > 0 && c.rung[0] == 44,
          "just below the top");
    CHECK(mint_zeckendorf(1836311903ull, &c) && c.top == 1 && c.n == 0, "exactly the top");
    /* cross-check against vino_zeckendorf and the defining properties */
    uint64_t x = 0x9e3779b97f4a7c15ull;
    bool same = true, props = true;
    for (int it = 0; it < 3000; it++) {
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        uint64_t amt = it < 200 ? (uint64_t) it : (it < 2000 ? x % 5000000000ull : x % 100000);
        if (it == 2999) amt = 1836311903ull * 3 - 1;
        mint_zeckendorf(amt, &c);
        uint32_t vr[64], vn = 0;
        int32_t rc = vino_zeckendorf(amt, vr, 64, &vn);
        uint32_t k = 0;
        same = same && rc == 0 && vn == c.top + c.n;
        for (uint64_t t = 0; same && t < c.top; t++) same = vr[k++] == MINT_LADDER_RUNGS;
        for (uint32_t i = 0; same && i < c.n; i++) same = vr[k++] == c.rung[i];
        uint64_t sum = c.top * 1836311903ull;
        for (uint32_t i = 0; i < c.n; i++) {
            sum += mint_coin_face(c.rung[i]);
            props = props && c.rung[i] >= 1 && c.rung[i] < MINT_LADDER_RUNGS;
            if (i) props = props && c.rung[i] + 1 < c.rung[i - 1]; /* non-adjacent */
        }
        props = props && sum == amt && c.n <= MINT_ZECK_MAX;
    }
    CHECK(same, "mint_zeckendorf == vino_zeckendorf on 3000 amounts");
    CHECK(props, "every split sums exactly, rungs strictly non-adjacent");
}

static void test_message(void)
{
    mint_capacity_t c = cap_of(0x04030201u, 0xAA, 2, 0x0d0c0b0au, 0x1122334455667788ull, 9);
    uint8_t m[MINT_CLAIM_MSG];
    mint_claim_msg(&c, m);
    CHECK(MINT_CLAIM_MSG == 74 && memcmp(m, "zxv/mint-claim/v1", 17) == 0, "domain tag");
    CHECK(m[17] == 1 && m[20] == 4 && m[21] == 0xAA && m[52] == 0x5a && m[53] == 2 &&
              m[54] == 0x0a && m[57] == 0x0d && m[58] == 0x88 && m[65] == 0x11 && m[66] == 9 &&
              m[73] == 0,
          "little-endian layout attestor || id || kind || juris || qty || expiry");
    uint8_t m2[MINT_CLAIM_MSG];
    const uint8_t *f[6];
    mint_capacity_t d = c;
    d.attestor++;
    mint_claim_msg(&d, m2);
    f[0] = m2;
    CHECK(memcmp(m, m2, sizeof m) != 0, "attestor bound");
    d = c;
    d.id[17] ^= 1;
    mint_claim_msg(&d, m2);
    CHECK(memcmp(m, m2, sizeof m) != 0, "id bound");
    d = c;
    d.kind = 1;
    mint_claim_msg(&d, m2);
    CHECK(memcmp(m, m2, sizeof m) != 0, "kind bound");
    d = c;
    d.juris ^= 0x01000000u;
    mint_claim_msg(&d, m2);
    CHECK(memcmp(m, m2, sizeof m) != 0, "jurisdiction bound (top byte)");
    d = c;
    d.qty ^= 1ull << 63;
    mint_claim_msg(&d, m2);
    CHECK(memcmp(m, m2, sizeof m) != 0, "qty bound (top byte)");
    d = c;
    d.expiry ^= 1ull << 63;
    mint_claim_msg(&d, m2);
    CHECK(memcmp(m, m2, sizeof m) != 0 && m2[73] == 0x80, "expiry bound (top byte)");
    (void) f;
}

/* real ML-DSA-65 */
static void test_signatures(void)
{
    setup();
    policy();
    mint_capacity_t c = cap_of(att_id[0], 1, MINT_KIND_EWHR, J_A, 400, 1000);
    sign_with(0, &c, SIG);
    uint32_t idx = 99;
    int vc = verify_calls;
    CHECK(mint_issue(&M, &c, SIG, sizeof SIG, MINT_TARGET_VINO, hv1, 10, &idx) == MINT_OK &&
              idx == 0 && verify_calls == vc + 1,
          "a genuine ML-DSA-65 claim mints");
    /* 400 * 250 = 100000; 5% -> 95000; 10% -> 85500 */
    const mint_claim_t *k = mint_find(&M, c.id);
    CHECK(k && k->gross == 100000 && k->net == 85500 && k->units == 85500 && k->face == 85500 &&
              k->remainder == 0 && deb(hv1) == 85500 && cred(viss) == 85500,
          "value: 400 x 2.50 less 5% then 10%: 855.00 Vino issued to the holder");
    CHECK(k->coins.top == 0 && k->coins.n > 0 && mint_coin_face(k->coins.rung[0]) == 75025 &&
              k->backing == 0,
          "a Vino claim records its Fibonacci coins (85500 = 75025 + ...), no backing");
    {
        uint64_t sum = 0;
        for (uint32_t i = 0; i < k->coins.n; i++) sum += mint_coin_face(k->coins.rung[i]);
        CHECK(sum == 85500, "the coins sum to the units");
    }
    CHECK(mint_check(&M), "conserved");

    mint_capacity_t t = cap_of(att_id[0], 2, MINT_KIND_EWHR, J_A, 400, 1000);
    sign_with(0, &t, SIG);
    uint64_t seq0 = L.seq;
    mint_capacity_t tq = t;
    tq.qty = 401;
    CHECK(mint_issue(&M, &tq, SIG, sizeof SIG, MINT_TARGET_VINO, hv1, 10, 0) == MINT_ERR_SIG,
          "tampered qty refused");
    tq = t;
    tq.expiry = 1001;
    CHECK(mint_issue(&M, &tq, SIG, sizeof SIG, MINT_TARGET_VINO, hv1, 10, 0) == MINT_ERR_SIG,
          "tampered expiry refused");
    tq = t;
    tq.id[5] = 1;
    CHECK(mint_issue(&M, &tq, SIG, sizeof SIG, MINT_TARGET_VINO, hv1, 10, 0) == MINT_ERR_SIG,
          "tampered id refused");
    tq = t;
    tq.kind = MINT_KIND_KWH;
    CHECK(mint_issue(&M, &tq, SIG, sizeof SIG, MINT_TARGET_VINO, hv1, 10, 0) == MINT_ERR_SIG,
          "tampered kind refused");
    tq = t;
    tq.attestor = att_id[1];
    CHECK(mint_issue(&M, &tq, SIG, sizeof SIG, MINT_TARGET_VINO, hv1, 10, 0) == MINT_ERR_SIG,
          "claim re-attributed to another attestor refused");
    CHECK(mint_issue(&M, &t, SIG, sizeof SIG - 1, MINT_TARGET_VINO, hv1, 10, 0) == MINT_ERR_SIG,
          "truncated signature refused");
    SIG[100] ^= 0x10;
    CHECK(mint_issue(&M, &t, SIG, sizeof SIG, MINT_TARGET_VINO, hv1, 10, 0) == MINT_ERR_SIG,
          "flipped signature bit refused");
    sign_with(2, &t, SIG); /* key 2 belongs to 103, not 101 */
    CHECK(mint_issue(&M, &t, SIG, sizeof SIG, MINT_TARGET_VINO, hv1, 10, 0) == MINT_ERR_SIG,
          "wrong key refused");
    sign_with(1, &t, SIG);
    CHECK(mint_issue(&M, &t, SIG, sizeof SIG, MINT_TARGET_VINO, hv1, 10, 0) == MINT_ERR_SIG,
          "another registered attestor's key refused");
    uint8_t other[MINT_CLAIM_MSG];
    mint_claim_msg(&t, other);
    other[4] = 'X'; /* another domain */
    pq_mldsa65_sign(sk[0], other, sizeof other, NULL, 0, NULL, SIG);
    CHECK(mint_issue(&M, &t, SIG, sizeof SIG, MINT_TARGET_VINO, hv1, 10, 0) == MINT_ERR_SIG,
          "a signature over another domain refused");
    CHECK(L.seq == seq0 && M.claim[1].state == MINT_FREE && mint_check(&M),
          "refused signatures post and record nothing");
    sign_with(0, &t, SIG);
    CHECK(mint_issue(&M, &t, SIG, sizeof SIG, MINT_TARGET_VBILL, hb1, 10, &idx) == MINT_OK &&
              idx == 1,
          "the genuine signature then mints (V-Bills)");
    /* 85500 / 100 = 855 V-Bills, remainder 0 */
    k = mint_find(&M, t.id);
    CHECK(k && k->units == 855 && k->face == 85500 && k->backing == 100 && deb(hb1) == 855 &&
              cred(biss) == 855 && k->coins.n == 0 && k->coins.top == 0,
          "855 V-Bills issued by the V-Bill issuer");
    CHECK(mint_issue(&M, &t, SIG, sizeof SIG, MINT_TARGET_VBILL, hb1, 10, 0) == MINT_ERR_DUP &&
              deb(hb1) == 855,
          "replay of the same signed claim refused");
    CHECK(mint_issue(&M, &c, SIG, sizeof SIG, MINT_TARGET_VBILL, hb1, 10, 0) == MINT_ERR_SIG,
          "the signature is checked before the duplicate test");
    sign_with(0, &c, SIG);
    CHECK(mint_issue(&M, &c, SIG, sizeof SIG, MINT_TARGET_VINO, hv2, 10, 0) == MINT_ERR_DUP,
          "a valid duplicate id refused");
    CHECK(mint_check(&M), "conserved");
}

/* fail closed, with the cheap verify */
static void setup_fake(void)
{
    setup();
    CHECK(mint_init(&M, &L, viss, biss, 100, true, fake_verify, NULL, SEED) == MINT_OK, "fake");
    policy();
}

static mint_status_t issue(const mint_capacity_t *c, uint8_t target, uint32_t holder, uint64_t now)
{
    uint8_t s[32];
    fake_sign(c, s);
    return mint_issue(&M, c, s, 32, target, holder, now, 0);
}

static void test_fail_closed(void)
{
    setup_fake();
    uint64_t seq0 = L.seq;
    mint_capacity_t c = cap_of(att_id[0], 1, MINT_KIND_EWHR, J_B, 10, 100);
    CHECK(issue(&c, MINT_TARGET_VINO, hv1, 1) == MINT_ERR_JURIS, "unknown jurisdiction refused");
    mint_juris_set(&M, J_B, false, 0, 1000000);
    CHECK(issue(&c, MINT_TARGET_VINO, hv1, 1) == MINT_ERR_JURIS, "disabled jurisdiction refused");
    mint_juris_set(&M, J_B, true, 0, 1000000);
    c.attestor = 999;
    CHECK(issue(&c, MINT_TARGET_VINO, hv1, 1) == MINT_ERR_UNKNOWN, "unknown attestor refused");
    mint_attestor_set(&M, 999, false, 0);
    CHECK(issue(&c, MINT_TARGET_VINO, hv1, 1) == MINT_ERR_UNKNOWN, "disabled attestor refused");
    c.juris = J_C;
    CHECK(issue(&c, MINT_TARGET_VINO, hv1, 1) == MINT_ERR_UNKNOWN,
          "attestor is checked before jurisdiction");
    c = cap_of(att_id[0], 1, MINT_KIND_KWH, J_B, 10, 100);
    mint_unit_set(&M, MINT_KIND_KWH, 0, 1);
    CHECK(issue(&c, MINT_TARGET_VINO, hv1, 1) == MINT_ERR_PRICE, "unpriced kind refused");
    c.juris = J_C;
    CHECK(issue(&c, MINT_TARGET_VINO, hv1, 1) == MINT_ERR_JURIS,
          "jurisdiction is checked before the unit value");
    c = cap_of(att_id[0], 1, MINT_KIND_EWHR, J_B, 10, 100);
    CHECK(issue(&c, MINT_TARGET_VINO, hv1, 100) == MINT_ERR_EXPIRED, "expiry == now refused");
    CHECK(issue(&c, MINT_TARGET_VINO, hv1, 101) == MINT_ERR_EXPIRED, "expired refused");
    c.kind = MINT_KIND_KWH;
    CHECK(issue(&c, MINT_TARGET_VINO, hv1, 101) == MINT_ERR_PRICE,
          "unit value is checked before expiry");
    c.kind = MINT_KIND_EWHR;
    uint8_t s[32];
    fake_sign(&c, s);
    s[3] ^= 1;
    CHECK(mint_issue(&M, &c, s, 32, MINT_TARGET_VINO, hv1, 101, 0) == MINT_ERR_EXPIRED,
          "expiry is checked before the signature");
    CHECK(mint_issue(&M, &c, s, 32, MINT_TARGET_VINO, hv1, 99, 0) == MINT_ERR_SIG, "bad sig");
    /* arguments */
    CHECK(mint_issue(NULL, &c, s, 32, 0, hv1, 1, 0) == MINT_ERR_ARG &&
              mint_issue(&M, NULL, s, 32, 0, hv1, 1, 0) == MINT_ERR_ARG &&
              mint_issue(&M, &c, NULL, 32, 0, hv1, 1, 0) == MINT_ERR_ARG,
          "NULL refused");
    mint_capacity_t d = c;
    d.kind = MINT_KINDS;
    CHECK(issue(&d, MINT_TARGET_VINO, hv1, 1) == MINT_ERR_ARG, "bad kind");
    d = c;
    d.qty = 0;
    CHECK(issue(&d, MINT_TARGET_VINO, hv1, 1) == MINT_ERR_ARG, "qty 0");
    CHECK(issue(&c, MINT_TARGETS, hv1, 1) == MINT_ERR_ARG, "bad target");
    CHECK(issue(&c, MINT_TARGET_VINO, hb1, 1) == MINT_ERR_ARG &&
              issue(&c, MINT_TARGET_VBILL, hv1, 1) == MINT_ERR_ARG &&
              issue(&c, MINT_TARGET_VINO, husd, 1) == MINT_ERR_ARG,
          "holder of the wrong asset refused");
    CHECK(issue(&c, MINT_TARGET_VINO, viss, 1) == MINT_ERR_ARG &&
              issue(&c, MINT_TARGET_VBILL, biss, 1) == MINT_ERR_ARG,
          "the issuer as holder refused");
    uint32_t viss2 = open_acct(L.vfv_asset, 5, PAY_ACCT_ISSUER);
    CHECK(issue(&c, MINT_TARGET_VINO, viss2, 1) == MINT_ERR_ARG, "any issuer as holder refused");
    CHECK(issue(&c, MINT_TARGET_VINO, L.n_accounts, 1) == MINT_ERR_ARG, "missing holder refused");
    d = c;
    d.attestor = 999;
    CHECK(issue(&d, MINT_TARGET_VINO, hb1, 1) == MINT_ERR_ARG, "holder checked before attestor");
    CHECK(L.seq == seq0 && mint_check(&M) && M.claim[0].state == MINT_FREE,
          "nothing posted or recorded by any refusal");
    mint_t blank;
    memset(&blank, 0, sizeof blank);
    blank.L = &L;
    CHECK(mint_issue(&blank, &c, s, 32, 0, hv1, 1, 0) == MINT_ERR_ARG, "not set up");

    /* value too large / zero */
    mint_unit_set(&M, MINT_KIND_COMPUTE, UINT64_MAX, 1);
    d = cap_of(att_id[1], 7, MINT_KIND_COMPUTE, J_B, 2, 100);
    CHECK(issue(&d, MINT_TARGET_VINO, hv1, 1) == MINT_ERR_OVERFLOW, "gross over 64 bits");
    mint_unit_set(&M, MINT_KIND_COMPUTE, 1, 3);
    d.qty = 2;
    CHECK(issue(&d, MINT_TARGET_VINO, hv1, 1) == MINT_ERR_ZERO, "value floor 0: ZERO");
    d.qty = 3;
    CHECK(issue(&d, MINT_TARGET_VINO, hv1, 1) == MINT_OK && deb(hv1) == 1, "value 1 mints 1");
    d.id[0] = 8;
    d.qty = 299;
    CHECK(issue(&d, MINT_TARGET_VBILL, hb1, 1) == MINT_ERR_ZERO, "99 < backing 100: ZERO");
    d.qty = 300;
    CHECK(issue(&d, MINT_TARGET_VBILL, hb1, 1) == MINT_OK && deb(hb1) == 1, "100 -> 1 V-Bill");
    d.id[0] = 9;
    d.qty = 3 * 12345;
    CHECK(issue(&d, MINT_TARGET_VBILL, hb1, 1) == MINT_OK && deb(hb1) == 124 &&
              mint_find(&M, d.id)->remainder == 45 && mint_find(&M, d.id)->face == 12300,
          "12345 -> 123 V-Bills, remainder 45 recorded, not minted");
    mint_set_backing(&M, 7);
    d.id[0] = 10;
    d.qty = 3 * 50;
    CHECK(issue(&d, MINT_TARGET_VBILL, hb2, 1) == MINT_OK && deb(hb2) == 7 &&
              mint_find(&M, d.id)->backing == 7 && mint_find(&M, d.id)->remainder == 1 &&
              mint_find(&M, 0) == NULL,
          "a new backing applies to later mints only");
    CHECK(mint_find(NULL, d.id) == NULL, "find NULL");
    CHECK(mint_check(&M), "conserved");
    /* ledger refusal: a frozen holder can still receive; an amount too large
     * for the ledger refuses */
    mint_unit_set(&M, MINT_KIND_COMPUTE, PAY_BAL_MAX, 1);
    mint_juris_set(&M, J_C, true, 0, PAY_BAL_MAX - 1);
    d = cap_of(att_id[1], 11, MINT_KIND_COMPUTE, J_C, 1, 100);
    mint_juris_set(&M, J_C, true, 0, PAY_BAL_MAX - 1);
    CHECK(issue(&d, MINT_TARGET_VINO, hv1, 1) == MINT_ERR_CAP, "value above the cap");
    mint_unit_set(&M, MINT_KIND_COMPUTE, PAY_BAL_MAX - 1, 1);
    seq0 = L.seq;
    CHECK(issue(&d, MINT_TARGET_VINO, hv1, 1) == MINT_ERR_LEDGER && M.last_ledger == PAY_ERR_ARG &&
              mint_find(&M, d.id) == NULL && mint_juris(&M, J_C)->used_epoch == 0 && L.seq == seq0,
          "the ledger refuses a balance past 2^62: nothing recorded");
    CHECK(mint_check(&M), "conserved after a ledger refusal");
}

static void test_extra(void)
{
    setup_fake();
    /* jurisdiction code 0 is an ordinary code (unused table rows also read 0) */
    mint_juris_set(&M, 0, true, 0, 1000);
    mint_unit_set(&M, MINT_KIND_KWH, 1, 1);
    mint_capacity_t c = cap_of(att_id[1], 1, MINT_KIND_KWH, 0, 300, 100);
    CHECK(issue(&c, MINT_TARGET_VINO, hv1, 42) == MINT_OK && mint_juris(&M, 0)->used_epoch == 300,
          "jurisdiction 0 mints");
    CHECK(mint_check(&M), "unused table rows are not mistaken for jurisdiction 0");
    const pay_journal_t *j = &L.journal[(L.seq - 1) % PAY_JOURNAL_MAX];
    CHECK(j->kind == PAY_KIND_ISSUE && j->tick == 42 && strncmp(j->e2e, "MINT-", 5) == 0 &&
              j->n_lines == 2 && j->lines[0].account == hv1 && j->lines[0].d_debit == 300 &&
              j->lines[1].account == viss && j->lines[1].d_credit == 300,
          "one ISSUE posting at the mint tick, e2e MINT-<n>");
    c.id[0] = 2;
    CHECK(issue(&c, MINT_TARGET_VINO, hv1, 43) == MINT_OK, "a second mint");
    const pay_journal_t *j2 = &L.journal[(L.seq - 1) % PAY_JOURNAL_MAX];
    CHECK(strcmp(j->uetr, j2->uetr) != 0 && memcmp(j->idem_key, j2->idem_key, 32) != 0 &&
              strcmp(j->e2e, j2->e2e) != 0,
          "distinct derived identifiers");
    M.issued[0] = UINT64_MAX - 100;
    c.id[0] = 3;
    uint64_t seq0 = L.seq;
    CHECK(issue(&c, MINT_TARGET_VINO, hv1, 43) == MINT_ERR_OVERFLOW && L.seq == seq0 &&
              mint_juris(&M, 0)->used_epoch == 600,
          "lifetime counter overflow refused before posting");
    M.issued[0] = 600;
    CHECK(mint_check(&M), "restored");
    M.claim[0].face = UINT64_MAX;
    CHECK(mint_backing(&M, 1) == UINT64_MAX, "backing saturates instead of wrapping");
    M.claim[0].face = 300;
    CHECK(mint_backing(&M, 1) == 600 && mint_check(&M), "restored");
}

static void test_cap_epoch(void)
{
    setup_fake();
    mint_juris_set(&M, J_B, true, 0, 1000);
    mint_unit_set(&M, MINT_KIND_KWH, 1, 1);
    mint_capacity_t c = cap_of(att_id[1], 1, MINT_KIND_KWH, J_B, 600, 100);
    CHECK(issue(&c, MINT_TARGET_VINO, hv1, 1) == MINT_OK && mint_juris(&M, J_B)->used_epoch == 600,
          "600 of 1000");
    c.id[0] = 2;
    c.qty = 401;
    CHECK(issue(&c, MINT_TARGET_VINO, hv1, 1) == MINT_ERR_CAP &&
              mint_juris(&M, J_B)->used_epoch == 600 && deb(hv1) == 600,
          "401 more would pass the cap: refused whole");
    c.qty = 400;
    CHECK(issue(&c, MINT_TARGET_VINO, hv1, 1) == MINT_OK && mint_juris(&M, J_B)->used_epoch == 1000,
          "exactly the cap");
    c.id[0] = 3;
    c.qty = 1;
    CHECK(issue(&c, MINT_TARGET_VINO, hv1, 1) == MINT_ERR_CAP, "full");
    CHECK(mint_juris_set(&M, J_B, true, 0, 999) == MINT_ERR_STATE &&
              mint_juris(&M, J_B)->cap == 1000,
          "cap may not drop below this epoch's usage");
    CHECK(mint_juris_set(&M, J_B, true, 0, 1000) == MINT_OK, "equal cap accepted");
    /* the cap counts face: V-Bill remainders do not use it */
    mint_juris_set(&M, J_C, true, 0, 250);
    mint_capacity_t v = cap_of(att_id[1], 9, MINT_KIND_KWH, J_C, 299, 100);
    CHECK(issue(&v, MINT_TARGET_VBILL, hb1, 1) == MINT_OK && mint_juris(&M, J_C)->used_epoch == 200,
          "299 -> 2 V-Bills: usage 200 (face), not 299");
    CHECK(mint_check(&M), "cap accounting audits");
    CHECK(mint_epoch_advance(&M) == MINT_OK && M.epoch == 1 &&
              mint_juris(&M, J_B)->used_epoch == 0 && mint_juris(&M, J_C)->used_epoch == 0,
          "epoch advance resets every usage");
    CHECK(mint_check(&M), "old-epoch claims are not counted in the new epoch");
    CHECK(issue(&c, MINT_TARGET_VINO, hv1, 1) == MINT_OK && mint_juris(&M, J_B)->used_epoch == 1,
          "minting resumes");
    CHECK(mint_juris_set(&M, J_B, true, 0, 1) == MINT_OK, "cap down to usage");
    CHECK(mint_check(&M), "conserved");
    M.juris[1].used_epoch++;
    CHECK(!mint_check(&M), "usage off by one detected");
    M.juris[1].used_epoch--;
    M.juris[1].cap = 0;
    CHECK(!mint_check(&M), "usage above cap detected");
    M.juris[1].cap = 1;
    CHECK(mint_check(&M), "restored");
}

static void test_lifecycle(void)
{
    setup_fake();
    mint_unit_set(&M, MINT_KIND_KWH, 1, 1);
    mint_juris_set(&M, J_B, true, 0, 100000);
    mint_capacity_t a = cap_of(att_id[1], 1, MINT_KIND_KWH, J_B, 1000, 50);
    mint_capacity_t b = cap_of(att_id[1], 2, MINT_KIND_KWH, J_B, 2000, 80);
    mint_capacity_t v = cap_of(att_id[1], 3, MINT_KIND_KWH, J_B, 3050, 80);
    CHECK(issue(&a, MINT_TARGET_VINO, hv1, 1) == MINT_OK &&
              issue(&b, MINT_TARGET_VINO, hv2, 1) == MINT_OK &&
              issue(&v, MINT_TARGET_VBILL, hb1, 1) == MINT_OK,
          "three claims");
    CHECK(mint_backing(&M, 1) == 6000 && mint_backing(&M, 49) == 6000 &&
              mint_backing(&M, 50) == 5000 && mint_backing(&M, 80) == 0 &&
              mint_backing(NULL, 1) == 0,
          "backing: live and unexpired (V-Bill face 3000)");
    CHECK(M.live[0] == 3000 && M.live[1] == 30 && M.issued[0] == 3000 && M.issued[1] == 30,
          "live / issued counters");
    uint64_t face = 0;
    CHECK(mint_quarantine(&M, b.id, &face) == MINT_OK && face == 2000 &&
              mint_find(&M, b.id)->state == MINT_QUARANTINED,
          "quarantine returns the face");
    CHECK(deb(hv2) == 2000 && cred(viss) == 3000, "quarantine moves nothing: holders keep units");
    CHECK(mint_backing(&M, 1) == 4000 && M.live[0] == 1000 && M.quarantined[0] == 2000,
          "a quarantined claim stops counting as backing");
    CHECK(mint_check(&M), "conserved");
    CHECK(mint_quarantine(&M, b.id, &face) == MINT_ERR_STATE, "quarantined twice refused");
    uint8_t nid[32] = {77};
    CHECK(mint_quarantine(&M, nid, NULL) == MINT_ERR_UNKNOWN &&
              mint_quarantine(&M, NULL, NULL) == MINT_ERR_ARG &&
              mint_quarantine(NULL, nid, NULL) == MINT_ERR_ARG,
          "unknown / NULL");
    CHECK(mint_quarantine(&M, v.id, NULL) == MINT_OK && M.quarantined[1] == 30, "V-Bill claim too");

    /* retire */
    uint64_t seq0 = L.seq;
    CHECK(mint_retire(&M, a.id, hv2) == MINT_OK, "retire claim a from hv2 (it holds 2000)");
    CHECK(L.journal[(L.seq - 1) % PAY_JOURNAL_MAX].kind == PAY_KIND_REDEEM &&
              L.journal[(L.seq - 1) % PAY_JOURNAL_MAX].tick == 0,
          "retire is one REDEEM posting (tick 0)");
    CHECK(mint_find(&M, a.id)->state == MINT_RETIRED && deb(hv2) == 1000 && cred(viss) == 2000 &&
              L.seq == seq0 + 1,
          "anyone returning the units retires the claim at par");
    CHECK(M.live[0] == 0 && M.retired[0] == 1000 && mint_check(&M), "counters");
    CHECK(mint_retire(&M, a.id, hv1) == MINT_ERR_STATE, "retired twice refused");
    seq0 = L.seq;
    CHECK(mint_retire(&M, b.id, hb1) == MINT_ERR_ARG &&
              mint_retire(&M, b.id, husd) == MINT_ERR_ARG &&
              mint_retire(&M, b.id, L.n_accounts) == MINT_ERR_ARG &&
              mint_find(&M, b.id)->state == MINT_QUARANTINED && L.seq == seq0,
          "an account of another asset (or none) refused, nothing changes");
    CHECK(mint_retire(&M, b.id, hv2) == MINT_ERR_LEDGER && M.last_ledger == PAY_ERR_FUNDS &&
              M.quarantined[0] == 2000,
          "an account without enough units: refused, nothing changes");
    CHECK(mint_retire(&M, b.id, viss) == MINT_ERR_ARG, "the issuer cannot return units");
    pay_receipt_t rc;
    pay_posting_req_t r;
    memset(&r, 0, sizeof r);
    r.idem_key[0] = 1;
    pay_uetr_from_random(r.idem_key, r.uetr);
    strcpy(r.e2e, "T1");
    CHECK(pay_ledger_transfer(&L, &r, hv1, hv2, 1000, &rc) == PAY_OK, "holder gathers units");
    CHECK(mint_retire(&M, b.id, hv2) == MINT_OK && M.quarantined[0] == 0 && M.retired[0] == 3000 &&
              cred(viss) == 0,
          "a quarantined claim retires when its units come back");
    CHECK(mint_retire(&M, v.id, hb1) == MINT_OK && cred(biss) == 0 && M.retired[1] == 30,
          "V-Bill claim retires");
    CHECK(mint_retire(&M, nid, hb1) == MINT_ERR_UNKNOWN &&
              mint_retire(NULL, nid, 1) == MINT_ERR_ARG && mint_retire(&M, NULL, 1) == MINT_ERR_ARG,
          "unknown / NULL");
    CHECK(mint_quarantine(&M, a.id, NULL) == MINT_ERR_STATE,
          "a retired claim cannot be quarantined");
    CHECK(mint_check(&M), "conserved");

    /* M9 slot reuse */
    CHECK(issue(&a, MINT_TARGET_VINO, hv1, 1) == MINT_ERR_DUP,
          "a retired claim's id is still held: replay refused");
    {
        mint_capacity_t a2 = a;
        a2.id[31] ^= 1;
        CHECK(issue(&a2, MINT_TARGET_VINO, hv1, 1) == MINT_OK,
              "an id differing only in its last byte is another claim");
        mint_retire(&M, a2.id, hv1);
    }
    for (uint32_t i = 3; i < MINT_MAX_CLAIMS; i++) {
        mint_capacity_t x = cap_of(att_id[1], (uint8_t) (10 + i), MINT_KIND_KWH, J_B, 1, 50);
        issue(&x, MINT_TARGET_VINO, hv1, 1);
    }
    mint_capacity_t y = cap_of(att_id[1], 200, MINT_KIND_KWH, J_B, 1, 500);
    CHECK(issue(&y, MINT_TARGET_VINO, hv1, 60) == MINT_ERR_FULL,
          "full: a retired, expired slot of THIS epoch is not reused");
    mint_epoch_advance(&M);
    CHECK(issue(&y, MINT_TARGET_VINO, hv1, 49) == MINT_ERR_FULL,
          "retired, earlier epoch but not expired: not reused");
    uint32_t idx = 99;
    CHECK(mint_issue(&M, &y, (uint8_t[32]){0}, 32, 0, hv1, 50, &idx) == MINT_ERR_SIG, "sig");
    uint8_t s[32];
    fake_sign(&y, s);
    CHECK(mint_issue(&M, &y, s, 32, MINT_TARGET_VINO, hv1, 50, &idx) == MINT_OK && idx == 0,
          "retired, expired, earlier epoch: slot 0 reused");
    CHECK(issue(&a, MINT_TARGET_VINO, hv1, 50) == MINT_ERR_EXPIRED,
          "the old claim's replay is refused as expired");
    CHECK(issue(&b, MINT_TARGET_VINO, hv1, 50) == MINT_ERR_DUP, "b still held");
    CHECK(mint_check(&M), "conserved");
}

static void test_check_tamper(void)
{
    setup_fake();
    mint_unit_set(&M, MINT_KIND_KWH, 1, 1);
    mint_juris_set(&M, J_B, true, 0, 100000);
    mint_capacity_t a = cap_of(att_id[1], 1, MINT_KIND_KWH, J_B, 1000, 50);
    mint_capacity_t v = cap_of(att_id[1], 3, MINT_KIND_KWH, J_B, 3050, 80);
    issue(&a, MINT_TARGET_VINO, hv1, 1);
    issue(&v, MINT_TARGET_VBILL, hb1, 1);
    mint_quarantine(&M, v.id, NULL);
    CHECK(mint_check(&M), "clean");
    CHECK(!mint_check(NULL), "NULL");
    mint_t k = M;
    M.live[0]++;
    CHECK(!mint_check(&M), "live counter");
    M = k;
    M.quarantined[1]--;
    CHECK(!mint_check(&M), "quarantined counter");
    M = k;
    M.issued[0]++;
    CHECK(!mint_check(&M), "issued counter");
    M = k;
    M.retired[1] = 1;
    CHECK(!mint_check(&M), "retired counter");
    M = k;
    M.retired[0] = 1;
    M.issued[0]++;
    CHECK(mint_check(&M), "issued - retired consistent");
    M = k;
    M.issued[0] = 0;
    M.retired[0] = UINT64_MAX - 999;
    CHECK(!mint_check(&M), "retired above issued");
    M = k;
    M.claim[0].face++;
    CHECK(!mint_check(&M), "Vino face != units");
    M = k;
    M.claim[0].remainder = 1;
    M.claim[0].net++;
    M.claim[0].gross++;
    CHECK(!mint_check(&M), "Vino remainder must be 0");
    M = k;
    M.claim[0].net = M.claim[0].gross + 1;
    CHECK(!mint_check(&M), "net above gross");
    M = k;
    M.claim[1].remainder++;
    CHECK(!mint_check(&M), "face + remainder != net");
    M = k;
    M.claim[1].backing = 0;
    CHECK(!mint_check(&M), "V-Bill backing 0");
    M = k;
    M.claim[1].remainder = 100;
    M.claim[1].net += 50;
    M.claim[1].gross += 50;
    CHECK(!mint_check(&M), "V-Bill remainder >= backing");
    M = k;
    M.claim[1].face += 100;
    M.claim[1].net += 100;
    M.claim[1].gross += 100;
    CHECK(!mint_check(&M), "V-Bill face != units * backing");
    M = k;
    M.claim[1].backing = UINT64_MAX;
    CHECK(!mint_check(&M), "units * backing overflow");
    M = k;
    M.claim[0].units = 0;
    M.claim[0].face = 0;
    M.claim[0].net = 0;
    M.live[0] = 0;
    M.issued[0] = 0;
    CHECK(!mint_check(&M), "a claim with no units");
    M = k;
    M.claim[0].target = 2;
    CHECK(!mint_check(&M), "bad target");
    M = k;
    M.claim[0].state = 4;
    CHECK(!mint_check(&M), "bad state");
    M = k;
    M.claim[0].face = UINT64_MAX;
    M.claim[0].remainder = 1;
    CHECK(!mint_check(&M), "face + remainder overflow");
    M = k;
    M.claim[0].units = UINT64_MAX;
    M.claim[0].face = UINT64_MAX;
    M.claim[0].net = M.claim[0].gross = UINT64_MAX;
    M.claim[2] = M.claim[0];
    M.claim[2].cap.id[0] = 9;
    CHECK(!mint_check(&M), "live sum overflow");
    M = k;
    M.claim[1].units = UINT64_MAX - 5;
    M.claim[1].backing = 1;
    M.claim[1].face = M.claim[1].units;
    M.claim[1].net = M.claim[1].gross = M.claim[1].face;
    M.claim[1].remainder = 0;
    M.claim[1].cap.juris = 1;
    M.claim[2] = M.claim[1];
    M.claim[2].cap.id[0] = 9;
    CHECK(!mint_check(&M), "quarantined sum overflow");
    M = k;
    M.claim[0].cap.juris = J_C;
    CHECK(!mint_check(&M), "usage not matched by the claims");
    M = k;
    M.claim[1].face = UINT64_MAX - 10;
    M.claim[1].units = 1;
    M.claim[1].backing = UINT64_MAX - 10;
    M.claim[1].net = M.claim[1].gross = UINT64_MAX - 10;
    M.claim[1].remainder = 0;
    M.quarantined[1] = 1;
    M.issued[1] = 1;
    M.exclusive = false;
    CHECK(!mint_check(&M), "usage sum overflow");
    M = k;
    M.juris[0].used_epoch = 1; /* J_A: no claims */
    CHECK(!mint_check(&M), "usage of the first table row checked");
    M = k;
    M.claim[1].net++;
    M.claim[1].remainder++;
    CHECK(!mint_check(&M), "net above gross (V-Bill, otherwise consistent)");
    M = k;
    M.claim[1].target = 2;
    CHECK(!mint_check(&M), "bad target on a V-Bill claim");
    M = k;
    M.claim[0].units = 0;
    M.claim[0].face = 0;
    M.claim[0].net = 0;
    M.live[0] = 0;
    M.issued[0] = 0;
    M.exclusive = false;
    CHECK(!mint_check(&M), "a claim with no units (non-exclusive)");
    M = k;
    M.exclusive = false;
    CHECK(mint_check(&M), "non-exclusive: clean");
    pay_receipt_t rc;
    pay_posting_req_t r;
    memset(&r, 0, sizeof r);
    r.idem_key[0] = 2;
    pay_uetr_from_random(r.idem_key, r.uetr);
    strcpy(r.e2e, "T2");
    pay_ledger_issue(&L, &r, viss, hv2, 5, &rc);
    CHECK(mint_check(&M), "non-exclusive: a foreign issue is allowed");
    M.exclusive = true;
    CHECK(!mint_check(&M), "exclusive: a foreign issue breaks the audit");
    M.exclusive = false;
    r.idem_key[0] = 3;
    pay_uetr_from_random(r.idem_key, r.uetr);
    strcpy(r.e2e, "T3");
    pay_ledger_redeem(&L, &r, hv1, viss, 10, &rc);
    CHECK(!mint_check(&M), "issuer CREDIT below what mint issued");
    M = k;
    M.ready = false;
    CHECK(!mint_check(&M), "not ready");
    M = k;
    uint32_t na = L.n_accounts;
    L.n_accounts = biss;
    CHECK(!mint_check(&M), "issuer missing");
    L.n_accounts = na;
}

/* deterministic randomized property loop against a simple model */
static uint64_t rs = 0x243f6a8885a308d3ull;
static uint64_t rnd(uint64_t n)
{
    rs ^= rs << 13;
    rs ^= rs >> 7;
    rs ^= rs << 17;
    return n ? rs % n : 0;
}

static void test_random(void)
{
    setup_fake();
    const uint32_t J[3] = {J_A, J_B, J_C};
    uint64_t jcap[3] = {1000000, 20000, 0}, jbps[3] = {1000, 0, 2500}, jused[3] = {0, 0, 0};
    mint_juris_set(&M, J_B, true, 0, 20000);
    mint_juris_set(&M, J_C, true, 2500, 0);
    mint_attestor_set(&M, att_id[2], true, 300);
    const uint64_t un[3] = {250, 3, 12}, ud[3] = {1, 2, 1};
    const uint32_t abps[3] = {500, 0, 300};
    uint64_t out[2] = {0, 0};
    uint32_t holder[2][2] = {{hv1, hv2}, {hb1, hb2}};
    bool ok = true, audit = true, model = true;
    uint8_t ids[400][32];
    int nids = 0;
    for (int it = 0; it < 1500; it++) {
        uint64_t now = (uint64_t) it;
        uint64_t op = rnd(10);
        if (op < 6) {
            mint_capacity_t c = cap_of(att_id[rnd(3)], 0, (uint8_t) rnd(3), J[rnd(3)],
                                       1 + rnd(it % 3 ? 300 : 30000), now + rnd(400));
            if (nids < 400 && rnd(8)) {
                memcpy(c.id, &it, sizeof it);
                c.id[30] = 0xEE;
            } else if (nids) {
                memcpy(c.id, ids[rnd((uint64_t) nids)], 32); /* duplicate */
            }
            uint8_t t = (uint8_t) rnd(2);
            int ai = (int) (c.attestor - att_id[0]);
            int ji = c.juris == J_A ? 0 : c.juris == J_B ? 1 : 2;
            uint64_t g = c.qty * un[c.kind] / ud[c.kind];
            uint64_t n1 = g * (10000 - abps[ai]) / 10000;
            uint64_t net = n1 * (10000 - jbps[ji]) / 10000;
            uint64_t units = t ? net / M.vbill_backing : net;
            uint64_t face = t ? units * M.vbill_backing : net;
            bool held = mint_find(&M, c.id) != NULL;
            uint32_t freeslots = 0;
            for (uint32_t i = 0; i < MINT_MAX_CLAIMS; i++)
                freeslots += M.claim[i].state == MINT_FREE ||
                             (M.claim[i].state == MINT_RETIRED && M.claim[i].cap.expiry <= now &&
                              M.claim[i].epoch < M.epoch);
            mint_status_t want = c.expiry <= now               ? MINT_ERR_EXPIRED
                                 : held                        ? MINT_ERR_DUP
                                 : units == 0                  ? MINT_ERR_ZERO
                                 : jused[ji] + face > jcap[ji] ? MINT_ERR_CAP
                                 : freeslots == 0              ? MINT_ERR_FULL
                                                               : MINT_OK;
            mint_status_t got = issue(&c, t, holder[t][rnd(2)], now);
            model = model && got == want;
            if (got == MINT_OK) {
                jused[ji] += face;
                out[t] += units;
                if (nids < 400) memcpy(ids[nids++], c.id, 32);
                const mint_claim_t *k = mint_find(&M, c.id);
                model =
                    model && k->gross == g && k->net == net && k->units == units && k->face == face;
            }
        } else if (op < 7) {
            mint_epoch_advance(&M);
            jused[0] = jused[1] = jused[2] = 0;
        } else if (op < 8 && nids) {
            const uint8_t *id = ids[rnd((uint64_t) nids)];
            const mint_claim_t *k = mint_find(&M, id);
            uint64_t f = 0;
            uint8_t before = k ? k->state : MINT_FREE;
            mint_status_t st = mint_quarantine(&M, id, &f);
            model =
                model && (k ? (before == MINT_LIVE
                                   ? (st == MINT_OK && f == k->face && k->state == MINT_QUARANTINED)
                                   : (st == MINT_ERR_STATE && k->state == before))
                            : st == MINT_ERR_UNKNOWN);
        } else if (nids) {
            const uint8_t *id = ids[rnd((uint64_t) nids)];
            const mint_claim_t *k = mint_find(&M, id);
            if (k && k->state != MINT_RETIRED) {
                uint8_t t = k->target;
                uint32_t h = deb(holder[t][0]) >= k->units ? holder[t][0] : holder[t][1];
                bool can = deb(h) >= k->units;
                mint_status_t st = mint_retire(&M, id, h);
                ok = ok && (can ? st == MINT_OK : st == MINT_ERR_LEDGER);
                if (st == MINT_OK) out[t] -= k->units;
            }
        }
        audit = audit && mint_check(&M) && pay_ledger_check(&L) && cred(viss) == out[0] &&
                cred(biss) == out[1] && deb(hv1) + deb(hv2) == out[0] &&
                deb(hb1) + deb(hb2) == out[1];
        uint64_t bk = 0;
        for (uint32_t i = 0; i < MINT_MAX_CLAIMS; i++)
            if (M.claim[i].state == MINT_LIVE && now < M.claim[i].cap.expiry) bk += M.claim[i].face;
        model = model && mint_backing(&M, now) == bk;
        for (int j = 0; j < 3; j++) model = model && mint_juris(&M, J[j])->used_epoch == jused[j];
    }
    CHECK(model, "random: every status, value, usage and backing matches the model");
    CHECK(ok, "random: retirements succeed exactly when the units are there");
    CHECK(audit, "random: audit, ledger and outstanding units hold after every step");
    CHECK(M.issued[0] > 0 && M.issued[1] > 0 && M.retired[0] + M.retired[1] > 0 &&
              M.quarantined[0] + M.quarantined[1] > 0,
          "random: every path exercised");
}

int main(void)
{
    for (int i = 0; i < NKEYS; i++) {
        uint8_t seed[32];
        memset(seed, 0x30 + i, sizeof seed);
        pq_mldsa65_keygen(seed, pk[i], sk[i]);
    }
    test_init();
    test_config();
    test_pure();
    test_message();
    test_signatures();
    test_fail_closed();
    test_extra();
    test_cap_epoch();
    test_lifecycle();
    test_check_tamper();
    test_random();
    printf("PASS %d FAIL %d\n", pass, fail);
    return fail ? 1 : 0;
}
