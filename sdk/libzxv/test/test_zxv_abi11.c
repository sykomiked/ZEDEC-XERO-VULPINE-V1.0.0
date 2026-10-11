/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zxv_abi11.c — conformance test of the ABI 1.1 groups of zxv_api.h,
 * linked against the shared library only (no kernel headers).
 * Usage: test_zxv_abi11 <fixtures dir> */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zxv_api.h"

static int checks, failures;
#define CHECK(c, ...)                                     \
    do {                                                  \
        checks++;                                         \
        if (!(c)) {                                       \
            failures++;                                   \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);   \
            printf(__VA_ARGS__);                          \
            printf("\n");                                 \
        }                                                 \
    } while (0)
#define OK(call) CHECK((call) == ZXV_OK, "%s -> %s", #call, zxv_status_name(call))

static void hex(const char *h, uint8_t *out, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        unsigned v;
        sscanf(h + 2 * i, "%2x", &v);
        out[i] = (uint8_t) v;
    }
}

static int same_hex(const uint8_t *b, size_t n, const char *h)
{
    uint8_t x[256];
    hex(h, x, n);
    return memcmp(b, x, n) == 0;
}

static void test_rational(void)
{
    int64_t n, d;
    int32_t c;
    OK(zxv_rat_add(1, 10, 2, 10, &n, &d));
    CHECK(n == 3 && d == 10, "0.1 + 0.2 == 3/10 exactly (%lld/%lld)", (long long) n, (long long) d);
    OK(zxv_rat_normalize(6, -4, &n, &d));
    CHECK(n == -3 && d == 2, "6/-4 normalizes to -3/2");
    CHECK(zxv_rat_div(1, 2, 0, 1, &n, &d) == ZXV_E_ARG, "division by zero refused");
    CHECK(zxv_rat_mul(INT64_MAX, 1, 2, 1, &n, &d) == ZXV_E_RANGE, "overflow detected, not wrapped");
    CHECK(zxv_rat_normalize(INT64_MIN, 1, &n, &d) == ZXV_E_RANGE, "INT64_MIN refused");
    OK(zxv_rat_cmp(1, 3, 333333, 1000000, &c));
    CHECK(c == 1, "1/3 > 0.333333");
    OK(zxv_rat_parse("-12.75", &n, &d));
    CHECK(n == -51 && d == 4, "parse -12.75 = -51/4");
    OK(zxv_rat_parse("3/4", &n, &d));
    CHECK(n == 3 && d == 4, "parse 3/4");
    CHECK(zxv_rat_parse("1e5", &n, &d) == ZXV_E_FORMAT, "exponent refused");
    CHECK(zxv_rat_parse(" 1", &n, &d) == ZXV_E_FORMAT, "leading space refused");
    CHECK(zxv_rat_parse("NaN", &n, &d) == ZXV_E_FORMAT, "NaN text refused");
    CHECK(zxv_rat_parse("1/0", &n, &d) == ZXV_E_ARG, "zero denominator refused");
    CHECK(zxv_rat_parse("99999999999999999999", &n, &d) == ZXV_E_RANGE, "too large refused");
    char buf[64];
    size_t len;
    OK(zxv_rat_format(-51, 4, buf, sizeof buf, &len));
    CHECK(strcmp(buf, "-12.75") == 0, "format -51/4 = -12.75 (%s)", buf);
    OK(zxv_rat_format(1, 3, buf, sizeof buf, &len));
    CHECK(strcmp(buf, "1/3") == 0, "format 1/3 stays 1/3 (%s)", buf);
    /* 0.1 as a double is NOT 1/10: the boundary makes the drift visible. */
    double tenth = 0.1;
    uint64_t bits;
    memcpy(&bits, &tenth, 8);
    OK(zxv_rat_from_ieee754(bits, &n, &d));
    CHECK(n == 3602879701896397LL && d == 36028797018963968LL, "0.1 maps to its exact dyadic value");
    double half = -0.5;
    memcpy(&bits, &half, 8);
    OK(zxv_rat_from_ieee754(bits, &n, &d));
    CHECK(n == -1 && d == 2, "-0.5 maps to -1/2");
    CHECK(zxv_rat_from_ieee754(0x7FF8000000000000ull, &n, &d) == ZXV_E_FLOAT, "NaN refused");
    CHECK(zxv_rat_from_ieee754(0x7FF0000000000000ull, &n, &d) == ZXV_E_FLOAT, "+inf refused");
    double tiny = 1e-300;
    memcpy(&bits, &tiny, 8);
    CHECK(zxv_rat_from_ieee754(bits, &n, &d) == ZXV_E_RANGE, "1e-300 cannot be exact in int64");
    int32_t q;
    OK(zxv_rat_to_q16(7, 10, &q));
    CHECK(q == 45875, "0.7 -> Q16 45875 (round half away)");
    OK(zxv_rat_to_q16(-1, 2, &q));
    CHECK(q == -32768, "-1/2 -> -32768");
}

static void test_logic(void)
{
    /* The four-valued tables, checked against Belnap's FOUR. */
    const int32_t NOT[4] = {ZXV_LP_NEITHER, ZXV_LP_FALSE, ZXV_LP_TRUE, ZXV_LP_BOTH};
    for (int a = 0; a < 4; a++) {
        int32_t r;
        OK(zxv_lp_not(a, &r));
        CHECK(r == NOT[a], "not %s", zxv_lp_name(a));
    }
    int32_t r;
    OK(zxv_lp_and(ZXV_LP_BOTH, ZXV_LP_TRUE, &r));
    CHECK(r == ZXV_LP_BOTH, "BOTH and TRUE = BOTH (contradiction survives)");
    OK(zxv_lp_and(ZXV_LP_BOTH, ZXV_LP_NEITHER, &r));
    CHECK(r == ZXV_LP_NEITHER || r == ZXV_LP_FALSE, "BOTH and NEITHER");
    OK(zxv_lp_or(ZXV_LP_BOTH, ZXV_LP_FALSE, &r));
    CHECK(r == ZXV_LP_BOTH, "BOTH or FALSE = BOTH");
    OK(zxv_lp_from_evidence(2, 1, &r));
    CHECK(r == ZXV_LP_BOTH, "evidence both ways = BOTH");
    CHECK(zxv_lp_not(7, &r) == ZXV_E_ARG, "invalid lp value refused");
    CHECK(strcmp(zxv_lp_name(ZXV_LP_BOTH), "BOTH") == 0, "name");
}

static void test_caps_and_poison(void)
{
    zxv_ctx *ctx;
    OK(zxv_ctx_create(&ctx));
    zxv_cap *root, *ro;
    OK(zxv_cap_create(ctx, 7, NULL, ZXV_CAP_ALL, &root));
    CHECK(zxv_cap_create(ctx, 7, NULL, UINT64_C(1) << 40, &ro) == ZXV_E_ARG, "unknown bits refused");
    OK(zxv_cap_attenuate(root, ZXV_CAP_LEDGER_READ, &ro));
    CHECK(zxv_cap_check(ro, ZXV_CAP_LEDGER_READ) == ZXV_OK, "child keeps LEDGER_READ");
    CHECK(zxv_cap_check(ro, ZXV_CAP_LEDGER_POST) == ZXV_E_CAP, "child cannot post");
    zxv_cap *wider;
    OK(zxv_cap_attenuate(ro, ZXV_CAP_ALL, &wider));
    CHECK(zxv_cap_check(wider, ZXV_CAP_LEDGER_POST) == ZXV_E_CAP, "attenuation never widens");
    zxv_cap_destroy(wider);
    OK(zxv_cap_set_attestation(root, ZXV_LP_BOTH));
    CHECK(zxv_cap_check(root, ZXV_CAP_LEDGER_READ) == ZXV_E_CAP, "BOTH attestation blocks");
    char err[256];
    size_t el;
    zxv_last_error(err, sizeof err, &el);
    CHECK(strstr(err, "contradictory") != NULL, "detail names the contradiction (%s)", err);
    OK(zxv_cap_set_attestation(root, ZXV_LP_TRUE));

    OK(zxv_ccy_enable_with_minor(ctx, "NGN", 2));
    uint32_t a, b;
    OK(zxv_ledger_open_account(ctx, "issuer", "NGN", ZXV_ACCT_ISSUER, &a));
    OK(zxv_ledger_open_account(ctx, "alice", "NGN", ZXV_ACCT_HOLDER, &b));
    uint64_t e;
    OK(zxv_ledger_post_cap(root, a, b, 500, "NGN", "r1", &e));
    CHECK(zxv_ledger_post_cap(ro, a, b, 1, "NGN", "r2", &e) == ZXV_E_CAP, "read-only cap cannot post");
    int64_t eq;
    OK(zxv_ledger_balance_cap(ro, b, NULL, NULL, &eq));
    CHECK(eq == 500, "balance through read cap");

    OK(zxv_cap_revoke(root));
    CHECK(zxv_cap_check(ro, ZXV_CAP_LEDGER_READ) == ZXV_E_CAP, "revoking the parent revokes the child");

    /* Fail closed: a halted context refuses everything, and is FATAL class. */
    zxv_cap *c2;
    OK(zxv_cap_create(ctx, 7, NULL, ZXV_CAP_ALL, &c2));
    OK(zxv_ctx_halt(ctx, "test halt"));
    int32_t p = 0;
    OK(zxv_ctx_is_poisoned(ctx, &p));
    CHECK(p == 1, "context is poisoned");
    CHECK(zxv_ledger_post(ctx, a, b, 1, "NGN", "r3", &e) == ZXV_E_POISONED, "post refused when halted");
    CHECK(zxv_ledger_balance_cap(c2, b, NULL, NULL, &eq) == ZXV_E_POISONED, "cap call refused when halted");
    CHECK(zxv_status_class(ZXV_E_POISONED) == ZXV_CLASS_FATAL, "POISONED is FATAL");
    CHECK(zxv_status_class(ZXV_E_INVARIANT) == ZXV_CLASS_FATAL, "INVARIANT is FATAL");
    CHECK(zxv_status_class(ZXV_E_BUDGET) == ZXV_CLASS_REFUSED, "BUDGET is REFUSED");
    CHECK(zxv_status_class(ZXV_E_FLOAT) == ZXV_CLASS_USAGE, "FLOAT is USAGE");
    zxv_cap_destroy(c2);
    zxv_cap_destroy(ro);
    zxv_cap_destroy(root);
    zxv_ctx_destroy(ctx);
}

static void test_lpres_rmag_phase(void)
{
    zxv_ctx *ctx;
    zxv_cap *cap;
    OK(zxv_ctx_create(&ctx));
    OK(zxv_cap_create(ctx, 1, NULL, ZXV_CAP_ALL, &cap));

    zxv_lpres *reg;
    OK(zxv_lpres_create(ctx, &reg));
    uint32_t i;
    OK(zxv_lpres_attest(cap, reg, "model.gguf", 6, ZXV_LP_TRUE, "loader", NULL, 0, &i));
    int32_t clear;
    OK(zxv_lpres_gate(reg, &clear));
    CHECK(clear == 1, "gate clear with one TRUE attestation");
    OK(zxv_lpres_evidence(cap, reg, i, 1));
    OK(zxv_lpres_evidence(cap, reg, i, 0));
    int32_t st;
    uint32_t pos, neg;
    OK(zxv_lpres_state(reg, i, &st, &pos, &neg));
    CHECK(st == ZXV_LP_BOTH && pos == 2 && neg == 1, "conflicting evidence holds BOTH (attesting TRUE counts as one positive)");
    OK(zxv_lpres_gate(reg, &clear));
    CHECK(clear == 0, "BOTH blocks the actuator gate");
    zxv_lpres_destroy(reg);

    zxv_rmag *rm;
    OK(zxv_rmag_create(ctx, &rm));
    uint32_t bud;
    OK(zxv_rmag_budget(cap, rm, "tokens", ZXV_RES_TOKENS, 10, 1, 1, &bud));
    OK(zxv_rmag_consume(cap, rm, bud, 7, 2));
    OK(zxv_rmag_consume(cap, rm, bud, 13, 2));
    CHECK(zxv_rmag_consume(cap, rm, bud, 1, 3) == ZXV_E_BUDGET, "1/3 more than 10 is denied");
    CHECK(zxv_rmag_consume(cap, rm, bud, -1, 1) == ZXV_E_RMAG_INVALID, "negative is INVALID");
    CHECK(zxv_rmag_consume(cap, rm, bud, INT64_MAX, 1) == ZXV_E_BUDGET ||
              zxv_rmag_consume(cap, rm, bud, INT64_MAX, 1) == ZXV_E_RMAG_INVALID,
          "huge amount never wraps");
    int64_t rn, rd, un, ud;
    uint64_t ep;
    OK(zxv_rmag_status(rm, bud, &rn, &rd, &un, &ud, &ep));
    CHECK(rn == 0 && un == 10 && ep == 0, "exactly spent (rem %lld, used %lld)", (long long) rn, (long long) un);
    OK(zxv_rmag_release(cap, rm, bud, 1, 2));
    OK(zxv_rmag_status(rm, bud, &rn, &rd, NULL, NULL, &ep));
    CHECK(rn == 1 && rd == 2 && ep == 1, "release replenishes and bumps the epoch");
    zxv_rmag_destroy(rm);

    zxv_phase *ph;
    OK(zxv_phase_create(ctx, &ph));
    uint64_t ev, now, due;
    uint32_t cleared;
    OK(zxv_phase_defer(cap, ph, ZXV_PHASE_CYCLE, &ev));
    OK(zxv_phase_next_due(ph, &due));
    CHECK(due == ZXV_PHASE_CYCLE, "next due is one fundamental");
    OK(zxv_phase_advance(cap, ph, ZXV_PHASE_CYCLE - 1, &now, &cleared));
    OK(zxv_phase_event(ph, ev, &st, NULL));
    CHECK(st == ZXV_EV_PENDING && cleared == 0, "still S0 one tick early");
    OK(zxv_phase_advance(cap, ph, 1, &now, &cleared));
    OK(zxv_phase_event(ph, ev, &st, NULL));
    CHECK(st == ZXV_EV_CLEARED && cleared == 1 && now == 27720, "clears on tick 27720");
    int32_t dec;
    uint64_t tok;
    OK(zxv_phase_admit(cap, ph, 6, 1, ZXV_STEP_S0_PENDING, &dec, &tok));
    CHECK(dec == ZXV_DEFER && tok == 0, "S0-pending step is deferred");
    OK(zxv_phase_set_health(cap, ph, 6, ZXV_HEALTH_HEALTHY));
    OK(zxv_phase_admit(cap, ph, 6, 2, ZXV_STEP_REQUIRES_HEALTH, &dec, &tok));
    CHECK(dec == ZXV_ADMIT && tok != 0, "healthy step is admitted");
    for (int k = 0; k < 600; k++) OK(zxv_phase_admit(cap, ph, 6, 3, 0, &dec, &tok));
    zxv_phase_destroy(ph);

    zxv_cap_destroy(cap);
    zxv_ctx_destroy(ctx);
}

static void test_swarm(void)
{
    zxv_ctx *ctx;
    zxv_cap *cap;
    OK(zxv_ctx_create(&ctx));
    OK(zxv_cap_create(ctx, 1, NULL, ZXV_CAP_ALL, &cap));
    zxv_swarm *sw;
    OK(zxv_swarm_create(ctx, 3, 1000, &sw));
    OK(zxv_swarm_register(cap, sw, 1, 0));
    OK(zxv_swarm_register(cap, sw, 2, 1));
    OK(zxv_swarm_register(cap, sw, 3, 1));
    CHECK(zxv_swarm_register(cap, sw, 4, 0) == ZXV_E_CAPACITY, "level 0 holds F(2) = 1 model");
    OK(zxv_swarm_set_mood(cap, sw, 0x1F914, 5));
    OK(zxv_swarm_set_feeling(cap, sw, 2, 0x1F604, 3));
    CHECK(zxv_swarm_set_mood(cap, sw, 0x1F600, 1) == ZXV_E_NOT_FOUND, "emoji outside the palette");
    uint64_t cyc, al[4] = {0}, im[4] = {0}, used, g, total = 0;
    OK(zxv_swarm_begin_cycle(cap, sw, &cyc));
    for (uint32_t m = 1; m <= 3; m++) {
        OK(zxv_swarm_allotment(sw, m, &al[m], &im[m], &used));
        total += al[m];
    }
    CHECK(total == 1000, "every token of the cycle is allotted (%llu)", (unsigned long long) total);
    CHECK(im[2] > 0 && im[3] == 0, "only the feeling model gets imaginary tokens");
    OK(zxv_swarm_consume(cap, sw, 1, 10, &g));
    CHECK(zxv_swarm_consume(cap, sw, 1, al[1], &g) == ZXV_E_BUDGET && g == al[1] - 10,
          "over-ask grants the rest and reports BUDGET");
    uint64_t expired;
    OK(zxv_swarm_end_cycle(cap, sw, &expired));
    uint32_t capn;
    OK(zxv_swarm_level_capacity(4, &capn));
    CHECK(capn == 8, "level 4 holds F(6) = 8");
    int32_t val;
    uint32_t t, x, v, pr;
    OK(zxv_swarm_emotion_profile(0x1F914, &val, &t, &x, &v, &pr));
    zxv_swarm_destroy(sw);
    zxv_cap_destroy(cap);
    zxv_ctx_destroy(ctx);
}

static void test_wire_and_hash(void)
{
    uint8_t tag, f[21], t2;
    OK(zxv_wire_tag(3, 2, 1, &tag));
    CHECK(tag == 123, "tag = 100*sync + 10*shell + trunk");
    const uint32_t sp[3] = {0x01020304, 0x05060708, 0x090A0B0C}, sm[2] = {0x11121314, 0x15161718};
    OK(zxv_wire_pack(tag, sp, sm, f));
    CHECK(f[0] == 123 && f[1] == 0x04 && f[4] == 0x01, "W0 little-endian");
    CHECK(f[5] == 0x11 && f[8] == 0x14, "W1 big-endian");
    CHECK(f[9] == 0x08 && f[13] == 0x15 && f[17] == 0x0C, "W2 LE, W3 BE, W4 LE");
    uint32_t rp[3], rm[2];
    OK(zxv_wire_unpack(f, &t2, rp, rm));
    CHECK(t2 == tag && memcmp(rp, sp, 12) == 0 && memcmp(rm, sm, 8) == 0, "round trip");
    int32_t truth;
    uint8_t idle[21];
    OK(zxv_wire_idle(0, idle));
    OK(zxv_wire_resolve(idle, &truth));
    CHECK(truth == ZXV_TRUTH_NEUTRAL, "idle pilot resolves NEUTRAL");

    uint8_t h[32];
    OK(zxv_keccak256(NULL, 0, h));
    CHECK(same_hex(h, 32, "c5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470"),
          "keccak256(\"\")");
    OK(zxv_keccak256((const uint8_t *) "abc", 3, h));
    CHECK(same_hex(h, 32, "4e03657aea45a94fc7d47ba826c8d667c0d1e6e33a64a036ec44f58fa12d6c45"),
          "keccak256(\"abc\")");
    uint8_t big[300];
    memset(big, 'a', sizeof big);
    OK(zxv_keccak256(big, 136, h)); /* exactly one rate block: padding goes in a second block */
    OK(zxv_sha3_256((const uint8_t *) "abc", 3, h));
    CHECK(same_hex(h, 32, "3a985da74fe225b2045c172d6bd390bd855f086e3e9d525b46bfe24511431532"),
          "sha3-256(\"abc\")");
    uint8_t z[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    OK(zxv_secure_zero(z, sizeof z));
    CHECK(z[0] == 0 && z[7] == 0, "secure_zero");
    uint8_t r1[32], r2[32];
    OK(zxv_random(r1, 32));
    OK(zxv_random(r2, 32));
    CHECK(memcmp(r1, r2, 32) != 0, "two random draws differ");
}

static void test_crypto(void)
{
    uint32_t feat;
    zxv_features(&feat);
    if (!(feat & ZXV_FEAT_MLDSA87)) {
        printf("skip crypto: built without PQC\n");
        return;
    }
    zxv_ctx *ctx;
    zxv_cap *cap, *nosign;
    OK(zxv_ctx_create(&ctx));
    OK(zxv_cap_create(ctx, 1, NULL, ZXV_CAP_ALL, &cap));
    OK(zxv_cap_attenuate(cap, ZXV_CAP_CRYPTO_KEM, &nosign));

    /* RFC 8032 section 7.1, TEST 1. */
    uint8_t sk[32], pk[32], sig[64];
    hex("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60", sk, 32);
    OK(zxv_ed25519_public(cap, sk, pk));
    CHECK(same_hex(pk, 32, "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a"),
          "Ed25519 RFC 8032 public key");
    OK(zxv_ed25519_sign(cap, sk, NULL, 0, sig));
    CHECK(same_hex(sig, 64,
                   "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b"),
          "Ed25519 RFC 8032 signature");
    OK(zxv_ed25519_verify(cap, pk, NULL, 0, sig));
    sig[0] ^= 1;
    CHECK(zxv_ed25519_verify(cap, pk, NULL, 0, sig) == ZXV_E_VERIFY, "tampered Ed25519 rejected");
    CHECK(zxv_ed25519_sign(nosign, sk, NULL, 0, sig) == ZXV_E_CAP, "signing needs CRYPTO_SIGN");

    static uint8_t dpk[ZXV_MLDSA87_PK_BYTES], dsk[ZXV_MLDSA87_SK_BYTES], dsig[ZXV_MLDSA87_SIG_BYTES];
    OK(zxv_mldsa87_keygen(cap, dpk, dsk));
    const uint8_t msg[] = "agent message";
    OK(zxv_mldsa87_sign(cap, dsk, msg, sizeof msg, (const uint8_t *) "zxv", 3, dsig));
    OK(zxv_mldsa87_verify(cap, dpk, msg, sizeof msg, (const uint8_t *) "zxv", 3, dsig));
    CHECK(zxv_mldsa87_verify(cap, dpk, msg, sizeof msg, (const uint8_t *) "zxw", 3, dsig) == ZXV_E_VERIFY,
          "wrong ML-DSA context rejected");

    static uint8_t ek[ZXV_MLKEM1024_EK_BYTES], dk[ZXV_MLKEM1024_DK_BYTES], ct[ZXV_MLKEM1024_CT_BYTES];
    uint8_t ss1[32], ss2[32];
    OK(zxv_mlkem1024_keygen(cap, ek, dk));
    OK(zxv_mlkem1024_encaps(nosign, ek, ct, ss1));
    OK(zxv_mlkem1024_decaps(nosign, dk, ct, ss2));
    CHECK(memcmp(ss1, ss2, 32) == 0, "ML-KEM-1024 shared secrets agree");
    memset(ek, 0xFF, 32);
    CHECK(zxv_mlkem1024_encaps(cap, ek, ct, ss1) == ZXV_E_FORMAT, "malformed ek refused");

    zxv_cap_destroy(nosign);
    zxv_cap_destroy(cap);
    zxv_ctx_destroy(ctx);
}

typedef struct {
    int32_t ids[64];
    uint32_t n;
    uint32_t stop_after;
} gen_cb;

static int32_t on_token(void *user, int32_t token, const uint8_t *piece, size_t len)
{
    (void) piece;
    (void) len;
    gen_cb *g = user;
    if (g->n < 64) g->ids[g->n] = token;
    g->n++;
    return !g->stop_after || g->n < g->stop_after;
}

static void test_tensor(const char *dir)
{
    uint32_t feat;
    zxv_features(&feat);
    if (!(feat & ZXV_FEAT_TENSOR)) {
        printf("skip tensor: built without the engine\n");
        return;
    }
    CHECK(!(feat & ZXV_FEAT_LORA), "LoRA is honestly reported as not built");
    char mp[1024], tp[1024], pp[1024];
    snprintf(mp, sizeof mp, "%s/tiny-qwen2.gguf", dir);
    snprintf(tp, sizeof tp, "%s/tiny-tokenizer.gguf", dir);
    snprintf(pp, sizeof pp, "%s/prompt.txt", dir);
    FILE *f = fopen(pp, "rb");
    if (!f) {
        CHECK(0, "prompt fixture missing at %s (run make fixtures)", pp);
        return;
    }
    char prompt[256];
    size_t plen = fread(prompt, 1, sizeof prompt, f);
    fclose(f);

    zxv_ctx *ctx;
    zxv_cap *cap, *noai;
    OK(zxv_ctx_create(&ctx));
    OK(zxv_cap_create(ctx, 1, NULL, ZXV_CAP_ALL, &cap));
    OK(zxv_cap_attenuate(cap, ZXV_CAP_LEDGER_READ, &noai));
    zxv_model *m;
    CHECK(zxv_model_open(noai, mp, tp, &m) == ZXV_E_CAP, "inference needs AI_INFERENCE");
    CHECK(zxv_model_open(cap, "/nonexistent.gguf", NULL, &m) == ZXV_E_IO, "missing file is IO");
    OK(zxv_model_open(cap, mp, tp, &m));
    int64_t nv;
    OK(zxv_model_info(m, ZXV_MODEL_N_VOCAB, &nv));
    CHECK(nv == 1200, "vocab 1200");
    size_t nt;
    CHECK(zxv_tokenize(m, (const uint8_t *) prompt, plen, 1, NULL, 0, &nt) == ZXV_E_BUFFER && nt > 0,
          "two-call tokenize reports the count");
    int32_t ids[64];
    OK(zxv_tokenize(m, (const uint8_t *) prompt, plen, 1, ids, 64, &nt));
    char back[512];
    size_t bl;
    OK(zxv_detokenize(m, ids, nt, back, sizeof back, &bl));
    CHECK(bl == plen && memcmp(back, prompt, plen) == 0, "detokenize(tokenize(x)) == x");

    zxv_session *s;
    OK(zxv_session_create(m, 128, ZXV_KV_Q16, &s));
    gen_cb g;
    memset(&g, 0, sizeof g);
    uint32_t np, ng;
    int32_t stop;
    OK(zxv_generate(cap, s, (const uint8_t *) prompt, plen, 16, 0, 0, 0, 0, 0, 0, on_token, &g, &np,
                    &ng, &stop));
    static const int32_t GREEDY[16] = {670, 414, 246, 431, 245, 968, 207, 67,
                                       822, 263, 635, 1044, 992, 504, 1127, 245};
    CHECK(ng == 16 && stop == ZXV_STOP_MAX && memcmp(g.ids, GREEDY, sizeof GREEDY) == 0,
          "greedy generation matches the engine's reference tokens");

    /* Budget binding: 8 tokens of budget stops generation early. */
    zxv_rmag *rm;
    uint32_t bud;
    OK(zxv_rmag_create(ctx, &rm));
    OK(zxv_rmag_budget(cap, rm, "gen", ZXV_RES_TOKENS, (int64_t) np + 8, 1, 1, &bud));
    OK(zxv_session_reset(s));
    OK(zxv_session_bind_budget(s, rm, bud));
    memset(&g, 0, sizeof g);
    OK(zxv_generate(cap, s, (const uint8_t *) prompt, plen, 16, 0, 0, 0, 0, 0, 0, on_token, &g, NULL,
                    &ng, &stop));
    CHECK(stop == ZXV_STOP_BUDGET && g.n == 8, "budget stops after 8 tokens (%u, stop %d)", g.n, stop);
    OK(zxv_session_reset(s));
    CHECK(zxv_generate(cap, s, (const uint8_t *) prompt, plen, 16, 0, 0, 0, 0, 0, 0, on_token, &g,
                       NULL, NULL, NULL) == ZXV_E_BUDGET,
          "an exhausted budget refuses the prompt up front");
    OK(zxv_session_bind_budget(s, NULL, 0));

    /* Seeded sampling is reproducible. */
    gen_cb a, b;
    memset(&a, 0, sizeof a);
    memset(&b, 0, sizeof b);
    OK(zxv_session_reset(s));
    OK(zxv_generate(cap, s, (const uint8_t *) prompt, plen, 12, 45875, 40, 62259, 0, 0, 42, on_token,
                    &a, NULL, NULL, NULL));
    OK(zxv_session_reset(s));
    OK(zxv_generate(cap, s, (const uint8_t *) prompt, plen, 12, 45875, 40, 62259, 0, 0, 42, on_token,
                    &b, NULL, NULL, NULL));
    CHECK(a.n == b.n && memcmp(a.ids, b.ids, a.n * 4) == 0, "same seed, same tokens");

    /* Callback stop. */
    memset(&g, 0, sizeof g);
    g.stop_after = 3;
    OK(zxv_session_reset(s));
    OK(zxv_generate(cap, s, (const uint8_t *) prompt, plen, 16, 0, 0, 0, 0, 0, 0, on_token, &g, NULL,
                    NULL, &stop));
    CHECK(stop == ZXV_STOP_CALLBACK && g.n == 3, "callback stops generation");

    zxv_lora *l;
    CHECK(zxv_lora_load(cap, m, "x", 65536, &l) == ZXV_E_NOTIMPL, "LoRA reports NOTIMPL");

    zxv_rmag_destroy(rm);
    zxv_session_destroy(s);
    zxv_model_destroy(m);
    zxv_cap_destroy(noai);
    zxv_cap_destroy(cap);
    zxv_ctx_destroy(ctx);
}

static void test_evm(void)
{
    zxv_ctx *ctx;
    zxv_cap *cap;
    OK(zxv_ctx_create(&ctx));
    OK(zxv_cap_create(ctx, 1, NULL, ZXV_CAP_ALL, &cap));
    OK(zxv_ccy_enable_with_minor(ctx, "USD", 2));
    uint32_t acc[2];
    OK(zxv_ledger_open_account(ctx, "a", "USD", ZXV_ACCT_ISSUER, &acc[0]));
    OK(zxv_ledger_open_account(ctx, "b", "USD", ZXV_ACCT_HOLDER, &acc[1]));
    uint64_t e;
    OK(zxv_ledger_post_cap(cap, acc[0], acc[1], 250, "USD", "x", &e));
    uint8_t id[32] = {0}, addr[40], dg1[32], dg2[32], buf[512];
    for (int i = 0; i < 40; i++) addr[i] = (uint8_t) (i + 1);
    size_t len;
    CHECK(zxv_evm_ledger_state(cap, id, 1, "USD", acc, addr, 2, NULL, 0, &len, dg1) == ZXV_E_BUFFER &&
              len == 10 * 32,
          "encoding size (6 + 2n) * 32");
    OK(zxv_evm_ledger_state(cap, id, 1, "USD", acc, addr, 2, buf, sizeof buf, &len, dg1));
    const int64_t bal[2] = {-250, 250};
    OK(zxv_evm_encode_state(cap, id, 1, addr, bal, 2, buf, sizeof buf, &len, dg2));
    CHECK(memcmp(dg1, dg2, 32) == 0, "ledger state == explicit balances");
    CHECK(buf[8 * 32] == 0xFF && buf[9 * 32 + 31] == 250, "int256 sign extension");
    zxv_cap_destroy(cap);
    zxv_ctx_destroy(ctx);
}

int main(int argc, char **argv)
{
    uint32_t maj, min;
    zxv_version(&maj, &min, NULL);
    CHECK(maj == 1 && min >= 1, "ABI 1.1+");
    test_rational();
    test_logic();
    test_caps_and_poison();
    test_lpres_rmag_phase();
    test_swarm();
    test_wire_and_hash();
    test_crypto();
    test_tensor(argc > 1 ? argv[1] : "../conformance/fixtures");
    test_evm();
    printf("test_zxv_abi11: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
