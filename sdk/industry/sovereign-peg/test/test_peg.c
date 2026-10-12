/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_peg.c — sovereign-peg: operator-only refusals for every mutating call
 * (read-only, revoked, unattested and foreign-context caps), exact peg
 * parsing, issuer-signed pegs (forged, stale, replayed), quotes checked
 * against the peg arithmetic, backed-only issuance at the exact boundary,
 * the 64-entry policy table and its locks, flows through every outcome
 * (allowed, held as Both, held as Neither, refused, Forbidden and
 * quarantined, refused by the library's limit / funds / exactness rules),
 * officer decisions (forged, unregistered, replayed, cross-flow), epoch
 * rollover, conservation on the hub and the signed operator log. */
#include "zind_test.h"
#include "zspeg.h"

#undef CHECK_RC
#define CHECK_RC(expr, want)                                                                   \
    do {                                                                                       \
        int32_t rc_ = (expr);                                                                  \
        CHECK(rc_ == (want), "%s -> %d (%s), want %d (%s)", #expr, (int) rc_,                  \
              zsp_status_name(rc_), (int) (want), zsp_status_name(want));                     \
    } while (0)

#define SA 0 /* "AAA": 2 minor units, peg 1.50 per V-Bill = 150/1 */
#define SB 1 /* "BBB": 2 minor units, peg 3 per V-Bill      = 300/1 */
#define SC 2 /* "CCC": 0 minor units, peg 1/3 per V-Bill     = 1/3   */

typedef struct {
    zt_env e;
    zxv_cap *rd; /* LEDGER_READ only */
    zind_signer *desk_key, *officer, *rogue;
    uint8_t iss_pk[3][ZIND_PK_BYTES], iss_sk[3][4896];
    uint8_t rogue_pk[ZIND_PK_BYTES], rogue_sk[4896];
    zxv_hexlog *qlog;
    zsp_desk *d;
    uint32_t a1, a2, b1, b2, c1;
} fx_t;

static uint8_t g_sig[ZIND_SIG_BYTES];

static int sign_peg(fx_t *f, const uint8_t sk[4896], uint8_t slot, uint64_t num, uint64_t den,
                    uint64_t epoch, uint8_t sig[ZIND_SIG_BYTES]) {
    uint8_t m[ZXV_VHUB_PEG_MSG_BYTES];
    if (zsp_peg_message(slot, num, den, epoch, m) != ZXV_OK) return -1;
    return zxv_mldsa87_sign_deterministic(f->e.cap, sk, m, sizeof m, NULL, 0, sig) == ZXV_OK ? 0
                                                                                            : -1;
}

static int fx_open(fx_t *f, int with_ccys) {
    static const uint8_t seed[32] = {7};
    memset(f, 0, sizeof *f);
    if (zt_env_open(&f->e, NULL) != 0) return -1;
    if (zxv_cap_attenuate(f->e.cap, ZXV_CAP_LEDGER_READ, &f->rd) != ZXV_OK) return -1;
    f->desk_key = zt_signer(f->e.cap, 1);
    f->officer = zt_signer(f->e.cap, 2);
    f->rogue = zt_signer(f->e.cap, 3);
    if (!f->desk_key || !f->officer || !f->rogue) return -1;
    for (int i = 0; i < 3; i++) {
        uint8_t s[32];
        zt_seed(s, (uint8_t) (40 + i));
        if (zxv_mldsa87_keygen_seed(f->e.cap, s, f->iss_pk[i], f->iss_sk[i]) != ZXV_OK) return -1;
    }
    {
        uint8_t s[32];
        zt_seed(s, 99);
        if (zxv_mldsa87_keygen_seed(f->e.cap, s, f->rogue_pk, f->rogue_sk) != ZXV_OK) return -1;
    }
    if (zxv_hexlog_create(f->e.cap, 1024, &f->qlog) != ZXV_OK) return -1;
    if (zsp_desk_create(f->e.cap, seed, f->desk_key, f->qlog, &f->d) != ZXV_OK) return -1;
    if (zsp_officer_add(f->d, f->e.cap, zind_signer_pk(f->officer)) != ZXV_OK) return -1;
    if (!with_ccys) return 0;
    if (zsp_ccy_add(f->d, f->e.cap, SA, "AAA", 2, f->iss_pk[0], 10000000, 1000) != ZXV_OK ||
        zsp_ccy_add(f->d, f->e.cap, SB, "BBB", 2, f->iss_pk[1], 10000000, 1000) != ZXV_OK ||
        zsp_ccy_add(f->d, f->e.cap, SC, "CCC", 0, f->iss_pk[2], 10000000, 3000) != ZXV_OK)
        return -1;
    if (sign_peg(f, f->iss_sk[0], SA, 150, 1, 1, g_sig) != 0 ||
        zsp_peg_set(f->d, f->e.cap, SA, 150, 1, 1, g_sig) != ZXV_OK ||
        sign_peg(f, f->iss_sk[1], SB, 300, 1, 1, g_sig) != 0 ||
        zsp_peg_set(f->d, f->e.cap, SB, 300, 1, 1, g_sig) != ZXV_OK ||
        sign_peg(f, f->iss_sk[2], SC, 1, 3, 1, g_sig) != 0 ||
        zsp_peg_set(f->d, f->e.cap, SC, 1, 3, 1, g_sig) != ZXV_OK)
        return -1;
    if (zsp_account_open(f->d, f->e.cap, SA, 101, &f->a1) != ZXV_OK ||
        zsp_account_open(f->d, f->e.cap, SA, 102, &f->a2) != ZXV_OK ||
        zsp_account_open(f->d, f->e.cap, SB, 201, &f->b1) != ZXV_OK ||
        zsp_account_open(f->d, f->e.cap, SB, 202, &f->b2) != ZXV_OK ||
        zsp_account_open(f->d, f->e.cap, SC, 301, &f->c1) != ZXV_OK)
        return -1;
    return 0;
}

static void fx_close(fx_t *f) {
    zsp_desk_destroy(f->d);
    zxv_hexlog_destroy(f->qlog);
    zind_signer_destroy(f->desk_key);
    zind_signer_destroy(f->officer);
    zind_signer_destroy(f->rogue);
    zxv_cap_destroy(f->rd);
    zt_env_close(&f->e);
}

static int64_t bal(fx_t *f, uint32_t a) {
    int64_t eq = -1;
    if (zsp_balance(f->d, f->rd, a, &eq) != ZXV_OK) return -999;
    return eq;
}

static uint32_t qcount(fx_t *f) {
    uint32_t n = 0;
    (void) zxv_hexlog_count(f->qlog, &n);
    return n;
}

static zsp_flow flow(uint8_t from, uint8_t to, uint32_t s, uint32_t r, uint64_t amt,
                     uint32_t yes, uint32_t no) {
    zsp_flow fl;
    memset(&fl, 0, sizeof fl);
    fl.from_slot = from;
    fl.to_slot = to;
    fl.sender = s;
    fl.receiver = r;
    fl.amount = amt;
    fl.tick = 1000;
    fl.approvals = yes;
    fl.objections = no;
    return fl;
}

static int decide_sig(fx_t *f, const zind_signer *k, uint32_t id, int32_t dec,
                      uint8_t sig[ZIND_SIG_BYTES]) {
    uint8_t m[128];
    size_t len = 0;
    if (zsp_decision_message(f->d, id, dec, m, sizeof m, &len) != ZXV_OK) return -1;
    return zind_sign(f->e.cap, k, m, len, ZSP_DECISION_CTX, sig) == ZXV_OK ? 0 : -1;
}

static void test_names(void) {
    static const int32_t codes[] = {ZSP_E_UNBACKED, ZSP_E_POLICY,     ZSP_E_OFFICER,
                                    ZSP_E_REFUSED,  ZSP_E_QUARANTINED, ZSP_E_OPERATOR};
    for (size_t i = 0; i < sizeof codes / sizeof codes[0]; i++) {
        CHECK(codes[i] <= -4300 && codes[i] > -4400, "code %d in block", (int) codes[i]);
        CHECK(strncmp(zsp_status_name(codes[i]), "ZSP_E_", 6) == 0, "name %s",
              zsp_status_name(codes[i]));
    }
    CHECK(strcmp(zsp_status_name(ZIND_E_HELD), "ZIND_E_HELD") == 0, "falls back to zind");
    CHECK(strcmp(zsp_status_name(ZXV_E_LIMIT), zxv_status_name(ZXV_E_LIMIT)) == 0,
          "falls back to libzxv");
}

static void test_peg_parse(void) {
    static const struct {
        const char *text;
        uint8_t minor;
        int32_t st;
        uint64_t num, den;
    } v[] = {
        {"1550", 2, ZXV_OK, 155000, 1},
        {"1.50", 2, ZXV_OK, 150, 1},
        {"0.25", 2, ZXV_OK, 25, 1},
        {"1/3", 2, ZXV_OK, 100, 3},
        {"1/3", 0, ZXV_OK, 1, 3},
        {"0.001", 2, ZXV_OK, 1, 10},
        {"0.0001", 4, ZXV_OK, 1, 1},
        {"7/6", 1, ZXV_OK, 35, 3},
        {"12.345", 3, ZXV_OK, 12345, 1},
        {"2/4", 0, ZXV_OK, 1, 2},
        {"0", 2, ZXV_E_RANGE, 0, 0},
        {"-1", 2, ZXV_E_RANGE, 0, 0},
        {"-0.5", 2, ZXV_E_RANGE, 0, 0},
        {"1e5", 2, ZXV_E_FORMAT, 0, 0},
        {"abc", 2, ZXV_E_FORMAT, 0, 0},
        {"", 2, ZXV_E_FORMAT, 0, 0},
        {"1.5.0", 2, ZXV_E_FORMAT, 0, 0},
        {"NaN", 2, ZXV_E_FORMAT, 0, 0},
        {"576460752303423489", 0, ZXV_E_RANGE, 0, 0}, /* 2^59 + 1 */
        {"1/576460752303423489", 0, ZXV_E_RANGE, 0, 0},
        {"100000000000000000", 4, ZXV_E_RANGE, 0, 0},
        {"1", 5, ZXV_E_ARG, 0, 0},
    };
    for (size_t i = 0; i < sizeof v / sizeof v[0]; i++) {
        uint64_t n = 0, dd = 0;
        int32_t st = zsp_peg_parse(v[i].text, v[i].minor, &n, &dd);
        CHECK(st == v[i].st, "peg_parse(\"%s\", %u) -> %d want %d", v[i].text, v[i].minor,
              (int) st, (int) v[i].st);
        if (v[i].st == ZXV_OK)
            CHECK(n == v[i].num && dd == v[i].den, "peg_parse(\"%s\") = %llu/%llu", v[i].text,
                  (unsigned long long) n, (unsigned long long) dd);
    }
    {
        uint64_t n, dd;
        CHECK_RC(zsp_peg_parse(NULL, 2, &n, &dd), ZXV_E_ARG);
        CHECK_RC(zsp_peg_parse("1", 2, NULL, &dd), ZXV_E_ARG);
    }
    /* the message is the library's, byte for byte */
    for (uint64_t e = 1; e < 6; e++) {
        uint8_t a[40], b[40];
        CHECK_RC(zsp_peg_message(3, 1000 + e, 7, e, a), ZXV_OK);
        CHECK_RC(zxv_vhub_peg_msg(3, 1000 + e, 7, e, b), ZXV_OK);
        CHECK(memcmp(a, b, 40) == 0, "peg message %llu", (unsigned long long) e);
    }
}

/* Every mutating call refuses a cap that is not an operator cap of this desk. */
static void test_operator_only(void) {
    fx_t f;
    zt_env other;
    zxv_cap *noattest = NULL, *revoked = NULL, *unattested = NULL;
    const zxv_cap *bad[4];
    uint32_t a = 0, id = 0;
    uint64_t ep = 0;
    zsp_flow fl;
    if (fx_open(&f, 1) != 0 || zt_env_open(&other, NULL) != 0) {
        CHECK(0, "fixture");
        return;
    }
    CHECK_RC(zxv_cap_attenuate(f.e.cap, ZXV_CAP_ALL & ~ZXV_CAP_ATTEST, &noattest), ZXV_OK);
    CHECK_RC(zxv_cap_attenuate(f.e.cap, ZXV_CAP_ALL, &revoked), ZXV_OK);
    CHECK_RC(zxv_cap_revoke(revoked), ZXV_OK);
    CHECK_RC(zxv_cap_attenuate(f.e.cap, ZXV_CAP_ALL, &unattested), ZXV_OK);
    CHECK_RC(zxv_cap_set_attestation(unattested, ZXV_LP_BOTH), ZXV_OK);
    bad[0] = f.rd;
    bad[1] = noattest;
    bad[2] = revoked;
    bad[3] = unattested;
    fl = flow(SA, SB, f.a1, f.b1, 150, 1, 0);
    for (int i = 0; i < 5; i++) {
        const zxv_cap *c = i < 4 ? bad[i] : other.cap; /* other: all bits, another context */
        uint32_t before = 0, after = 0;
        (void) zind_chain_count(zsp_log(f.d), &before);
        CHECK_RC(zsp_officer_add(f.d, c, zind_signer_pk(f.rogue)), ZSP_E_OPERATOR);
        CHECK_RC(zsp_ccy_add(f.d, c, 5, "EEE", 2, f.iss_pk[0], 0, 0), ZSP_E_OPERATOR);
        CHECK_RC(zsp_peg_set(f.d, c, SA, 150, 1, 9, g_sig), ZSP_E_OPERATOR);
        CHECK_RC(zsp_limit_add(f.d, c, SA, 100), ZSP_E_OPERATOR);
        CHECK_RC(zsp_outflow_cap_set(f.d, c, SA, 100), ZSP_E_OPERATOR);
        CHECK_RC(zsp_block_sender(f.d, c, f.a1), ZSP_E_OPERATOR);
        CHECK_RC(zsp_account_open(f.d, c, SA, 7, &a), ZSP_E_OPERATOR);
        CHECK_RC(zsp_issue(f.d, c, f.a1, 100), ZSP_E_OPERATOR);
        CHECK_RC(zsp_policy_set(f.d, c, 0, ZSP_REFUSE), ZSP_E_OPERATOR);
        CHECK_RC(zsp_flow_submit(f.d, c, &fl, &id), ZSP_E_OPERATOR);
        CHECK_RC(zsp_decide(f.d, c, 1, zind_signer_pk(f.officer), ZSP_RELEASE, 0, g_sig),
                 ZSP_E_OPERATOR);
        CHECK_RC(zsp_epoch_next(f.d, c, &ep), ZSP_E_OPERATOR);
        (void) zind_chain_count(zsp_log(f.d), &after);
        CHECK(before == after, "cap %d: nothing logged", i);
        CHECK(bal(&f, f.a1) == 0, "cap %d: nothing issued", i);
    }
    /* creating a desk needs an operator cap too */
    {
        zsp_desk *d2 = NULL;
        CHECK_RC(zsp_desk_create(f.rd, NULL, f.desk_key, NULL, &d2), ZSP_E_OPERATOR);
        CHECK_RC(zsp_desk_create(noattest, NULL, f.desk_key, NULL, &d2), ZSP_E_OPERATOR);
        CHECK_RC(zsp_desk_create(f.e.cap, NULL, NULL, NULL, &d2), ZXV_E_ARG);
        CHECK_RC(zsp_desk_create(f.e.cap, NULL, f.desk_key, NULL, NULL), ZXV_E_ARG);
        CHECK(d2 == NULL, "no desk");
    }
    /* reads work with the read-only cap; a NULL cap is an argument error */
    {
        uint64_t n = 0, dd = 0, ep2 = 0;
        CHECK_RC(zsp_peg_get(f.d, f.rd, SA, &n, &dd, &ep2), ZXV_OK);
        CHECK(n == 150 && dd == 1 && ep2 == 1, "peg read");
        CHECK_RC(zsp_peg_get(f.d, NULL, SA, &n, &dd, &ep2), ZXV_E_ARG);
        CHECK_RC(zsp_issue(f.d, NULL, f.a1, 1), ZXV_E_ARG);
        CHECK_RC(zsp_issue(NULL, f.e.cap, f.a1, 1), ZXV_E_ARG);
        CHECK_RC(zsp_quote(f.d, other.cap, SA, SB, 150, NULL, NULL, NULL), ZXV_E_CAP);
    }
    zxv_cap_destroy(unattested);
    zxv_cap_destroy(revoked);
    zxv_cap_destroy(noattest);
    fx_close(&f);
    zt_env_close(&other);
}

static void test_ccy_and_pegs(void) {
    fx_t f;
    uint64_t n = 0, dd = 0, ep = 0;
    uint32_t q0, q1;
    if (fx_open(&f, 0) != 0) {
        CHECK(0, "fixture");
        return;
    }
    CHECK_RC(zsp_ccy_add(f.d, f.e.cap, 0, "USD", 2, f.iss_pk[0], 0, 0), ZXV_E_CURRENCY);
    CHECK_RC(zsp_ccy_add(f.d, f.e.cap, 0, "VBL", 2, f.iss_pk[0], 0, 0), ZXV_E_DUPLICATE);
    CHECK_RC(zsp_ccy_add(f.d, f.e.cap, 8, "AAA", 2, f.iss_pk[0], 0, 0), ZXV_E_RANGE);
    CHECK_RC(zsp_ccy_add(f.d, f.e.cap, 0, "AAA", 5, f.iss_pk[0], 0, 0), ZXV_E_RANGE);
    CHECK_RC(zsp_ccy_add(f.d, f.e.cap, 0, "aaa", 2, f.iss_pk[0], 0, 0), ZXV_E_FORMAT);
    CHECK_RC(zsp_ccy_add(f.d, f.e.cap, 0, NULL, 2, f.iss_pk[0], 0, 0), ZXV_E_ARG);
    CHECK_RC(zsp_ccy_add(f.d, f.e.cap, 0, "AAA", 2, NULL, 0, 0), ZXV_E_ARG);
    CHECK_RC(zsp_ccy_add(f.d, f.e.cap, SA, "AAA", 2, f.iss_pk[0], 10000000, 1000), ZXV_OK);
    CHECK_RC(zsp_ccy_add(f.d, f.e.cap, SA, "ZZZ", 2, f.iss_pk[0], 0, 0), ZXV_E_DUPLICATE);
    CHECK_RC(zsp_ccy_add(f.d, f.e.cap, SB, "AAA", 2, f.iss_pk[1], 0, 0), ZXV_E_DUPLICATE);
    CHECK_RC(zsp_ccy_add(f.d, f.e.cap, SB, "BBB", 2, f.iss_pk[1], 10000000, 1000), ZXV_OK);

    /* unset peg: quotes refused, holder issuance refused */
    CHECK_RC(zsp_peg_get(f.d, f.rd, SA, &n, &dd, &ep), ZXV_OK);
    CHECK(n == 0 && dd == 0 && ep == 0, "peg unset");
    CHECK_RC(zsp_quote(f.d, f.rd, SA, SB, 150, NULL, NULL, NULL), ZXV_E_PEG);
    CHECK_RC(zsp_account_open(f.d, f.e.cap, SA, 1, &f.a1), ZXV_OK);
    CHECK_RC(zsp_issue(f.d, f.e.cap, f.a1, 100), ZXV_E_PEG);

    /* forged: the rogue key signs the right numbers; the issuer signs other numbers */
    q0 = qcount(&f);
    CHECK(sign_peg(&f, f.rogue_sk, SA, 150, 1, 1, g_sig) == 0, "sign");
    CHECK_RC(zsp_peg_set(f.d, f.e.cap, SA, 150, 1, 1, g_sig), ZXV_E_VERIFY);
    CHECK(sign_peg(&f, f.iss_sk[0], SA, 150, 1, 1, g_sig) == 0, "sign");
    CHECK_RC(zsp_peg_set(f.d, f.e.cap, SA, 151, 1, 1, g_sig), ZXV_E_VERIFY);
    CHECK_RC(zsp_peg_set(f.d, f.e.cap, SA, 150, 2, 1, g_sig), ZXV_E_VERIFY);
    CHECK_RC(zsp_peg_set(f.d, f.e.cap, SA, 150, 1, 2, g_sig), ZXV_E_VERIFY);
    CHECK_RC(zsp_peg_set(f.d, f.e.cap, SB, 150, 1, 1, g_sig), ZXV_E_VERIFY); /* other slot */
    q1 = qcount(&f);
    CHECK(q1 == q0 + 5, "five forged pegs quarantined (%u -> %u)", q0, q1);
    CHECK_RC(zsp_peg_set(f.d, f.e.cap, SA, 0, 1, 1, g_sig), ZXV_E_RANGE);
    CHECK_RC(zsp_peg_set(f.d, f.e.cap, SA, 1, 0, 1, g_sig), ZXV_E_RANGE);
    CHECK_RC(zsp_peg_set(f.d, f.e.cap, SA, ZXV_VHUB_AMOUNT_MAX + 1, 1, 1, g_sig), ZXV_E_RANGE);
    CHECK_RC(zsp_peg_set(f.d, f.e.cap, 6, 150, 1, 1, g_sig), ZXV_E_NOT_FOUND);
    CHECK_RC(zsp_peg_set(f.d, f.e.cap, SA, 150, 1, 1, NULL), ZXV_E_ARG);
    /* the genuine peg, then the same signature again: stale epoch */
    CHECK_RC(zsp_peg_set(f.d, f.e.cap, SA, 150, 1, 1, g_sig), ZXV_OK);
    CHECK_RC(zsp_peg_set(f.d, f.e.cap, SA, 150, 1, 1, g_sig), ZXV_E_PEG);
    CHECK(sign_peg(&f, f.iss_sk[0], SA, 140, 1, 1, g_sig) == 0, "sign");
    CHECK_RC(zsp_peg_set(f.d, f.e.cap, SA, 140, 1, 1, g_sig), ZXV_E_PEG); /* same epoch */
    CHECK(sign_peg(&f, f.iss_sk[0], SA, 140, 1, 0, g_sig) == 0, "sign");
    CHECK_RC(zsp_peg_set(f.d, f.e.cap, SA, 140, 1, 0, g_sig), ZXV_E_PEG); /* older */
    CHECK_RC(zsp_peg_get(f.d, f.rd, SA, &n, &dd, &ep), ZXV_OK);
    CHECK(n == 150 && dd == 1 && ep == 1, "refused pegs change nothing");
    /* a later epoch replaces it */
    CHECK(sign_peg(&f, f.iss_sk[0], SA, 120, 1, 5, g_sig) == 0, "sign");
    CHECK_RC(zsp_peg_set(f.d, f.e.cap, SA, 120, 1, 5, g_sig), ZXV_OK);
    CHECK_RC(zsp_peg_get(f.d, f.rd, SA, &n, &dd, &ep), ZXV_OK);
    CHECK(n == 120 && dd == 1 && ep == 5, "new peg");
    fx_close(&f);
}

/* Quotes are the library's: the largest exact amount, through the V-Bill. */
static void test_quotes(void) {
    fx_t f;
    if (fx_open(&f, 1) != 0) {
        CHECK(0, "fixture");
        return;
    }
    for (uint64_t want = 0; want < 1600; want += 37) {
        uint64_t amt = 1, vb = 1, rc = 1;
        CHECK_RC(zsp_quote(f.d, f.rd, SA, SB, want, &amt, &vb, &rc), ZXV_OK);
        /* A: 150 per V-Bill, B: 300 per V-Bill */
        CHECK(amt == (want / 150) * 150 && vb == want / 150 && rc == vb * 300,
              "A->B want %llu: %llu %llu %llu", (unsigned long long) want,
              (unsigned long long) amt, (unsigned long long) vb, (unsigned long long) rc);
    }
    for (uint64_t want = 0; want < 40; want++) {
        uint64_t amt = 1, vb = 1, rc = 1;
        /* C: 1 unit per 3 V-Bills -> 1 C = 3 V-Bills = 900 B minor */
        CHECK_RC(zsp_quote(f.d, f.rd, SC, SB, want, &amt, &vb, &rc), ZXV_OK);
        CHECK(amt == want && vb == want * 3 && rc == want * 900, "C->B %llu",
              (unsigned long long) want);
        /* B -> C: whole C units need 900 B minor each */
        CHECK_RC(zsp_quote(f.d, f.rd, SB, SC, want * 450, &amt, &vb, &rc), ZXV_OK);
        CHECK(amt == (want / 2) * 900 && rc == want / 2, "B->C %llu", (unsigned long long) want);
    }
    CHECK_RC(zsp_quote(f.d, f.rd, SA, SA, 150, NULL, NULL, NULL), ZXV_E_ARG);
    CHECK_RC(zsp_quote(f.d, f.rd, SA, 7, 150, NULL, NULL, NULL), ZXV_E_NOT_FOUND);
    CHECK_RC(zsp_quote(f.d, f.rd, SA, SB, ZXV_VHUB_AMOUNT_MAX + 1, NULL, NULL, NULL),
             ZXV_E_RANGE);
    fx_close(&f);
}

static void test_issue(void) {
    fx_t f;
    uint64_t circ = 0, flt = 0;
    int32_t ok = -1;
    uint32_t iss = 0, pool = 0, vf = 0;
    if (fx_open(&f, 1) != 0) {
        CHECK(0, "fixture");
        return;
    }
    CHECK_RC(zsp_ccy_accounts(f.d, f.rd, SA, &iss, &pool, &vf), ZXV_OK);
    CHECK_RC(zsp_coverage(f.d, f.rd, SA, &circ, &flt, &ok), ZXV_OK);
    CHECK(circ == 0 && flt == 1000 && ok == 1, "A: nothing in circulation, 1000 V-Bills");
    /* A at 150/1 with 1000 V-Bills covers 150000 minor units exactly */
    CHECK_RC(zsp_issue(f.d, f.e.cap, f.a1, 100000), ZXV_OK);
    CHECK_RC(zsp_issue(f.d, f.e.cap, f.a2, 50001), ZSP_E_UNBACKED);
    CHECK_RC(zsp_issue(f.d, f.e.cap, f.a2, 50000), ZXV_OK);
    CHECK_RC(zsp_issue(f.d, f.e.cap, f.a2, 1), ZSP_E_UNBACKED);
    CHECK_RC(zsp_coverage(f.d, f.rd, SA, &circ, &flt, &ok), ZXV_OK);
    CHECK(circ == 150000 && flt == 1000 && ok == 1, "A at the boundary");
    CHECK(bal(&f, f.a1) == 100000 && bal(&f, f.a2) == 50000, "holders");
    /* pool liquidity does not count as circulation */
    CHECK_RC(zsp_issue(f.d, f.e.cap, pool, 500000), ZXV_OK);
    CHECK_RC(zsp_coverage(f.d, f.rd, SA, &circ, NULL, &ok), ZXV_OK);
    CHECK(circ == 150000 && ok == 1, "pool issue leaves circulation alone");
    /* more V-Bills in the float allow more circulation */
    CHECK_RC(zsp_issue(f.d, f.e.cap, vf, 10), ZXV_OK);
    CHECK_RC(zsp_issue(f.d, f.e.cap, f.a2, 1501), ZSP_E_UNBACKED);
    CHECK_RC(zsp_issue(f.d, f.e.cap, f.a2, 1500), ZXV_OK);
    /* the library's own refusals pass through */
    CHECK_RC(zsp_issue(f.d, f.e.cap, iss, 10), ZXV_E_ARG);
    CHECK_RC(zsp_issue(f.d, f.e.cap, 9999, 10), ZXV_E_NOT_FOUND);
    CHECK_RC(zsp_issue(f.d, f.e.cap, f.a1, 0), ZXV_E_RANGE);
    CHECK_RC(zsp_issue(f.d, f.e.cap, f.a1, ZXV_VHUB_AMOUNT_MAX + 1), ZXV_E_RANGE);
    /* a peg that would uncover circulation is refused, even signed */
    CHECK(sign_peg(&f, f.iss_sk[0], SA, 149, 1, 2, g_sig) == 0, "sign");
    CHECK_RC(zsp_peg_set(f.d, f.e.cap, SA, 149, 1, 2, g_sig), ZSP_E_UNBACKED);
    {
        uint64_t n = 0, dd = 0;
        CHECK_RC(zsp_peg_get(f.d, f.rd, SA, &n, &dd, NULL), ZXV_OK);
        CHECK(n == 150 && dd == 1, "unbacked peg not applied");
    }
    CHECK(sign_peg(&f, f.iss_sk[0], SA, 300, 2, 2, g_sig) == 0, "sign"); /* same value */
    CHECK_RC(zsp_peg_set(f.d, f.e.cap, SA, 300, 2, 2, g_sig), ZXV_OK);
    CHECK(sign_peg(&f, f.iss_sk[0], SA, 151, 1, 3, g_sig) == 0, "sign"); /* weaker currency */
    CHECK_RC(zsp_peg_set(f.d, f.e.cap, SA, 151, 1, 3, g_sig), ZXV_OK);
    CHECK_RC(zsp_check(f.d, f.rd, NULL), ZXV_OK);
    /* boundary sweep on C (0 minor units, 1/3): float 3000 V-Bills covers 1000 C */
    for (int i = 0; i < 50; i++) {
        CHECK_RC(zsp_issue(f.d, f.e.cap, f.c1, 20), ZXV_OK);
        CHECK_RC(zsp_coverage(f.d, f.rd, SC, &circ, NULL, &ok), ZXV_OK);
        CHECK(circ == (uint64_t) (i + 1) * 20 && ok == 1, "C step %d", i);
    }
    CHECK_RC(zsp_issue(f.d, f.e.cap, f.c1, 1), ZSP_E_UNBACKED);
    fx_close(&f);
}

static void test_policy(void) {
    fx_t f;
    if (fx_open(&f, 1) != 0) {
        CHECK(0, "fixture");
        return;
    }
    for (uint8_t t = 0; t < 64; t++) {
        int32_t dec = 0, act = -1, want, set;
        uint8_t base = 0, pol = 0;
        CHECK_RC(zsp_policy_get(f.d, t, &dec), ZXV_OK);
        zxv_hex_action(t, &act);
        want = act == ZXV_HEX_ACT_PROCEED                                ? ZSP_ALLOW
               : (act == ZXV_HEX_ACT_WAIT || act == ZXV_HEX_ACT_ESCALATE) ? ZSP_HOLD
                                                                          : ZSP_REFUSE;
        CHECK(dec == want, "default policy %s -> %d", zxv_hex_name(t), (int) dec);
        zxv_hex_unpack(t, &base, &pol, NULL);
        if (pol == ZXV_HEX_POL_FORBIDDEN) {
            CHECK_RC(zsp_policy_set(f.d, f.e.cap, t, ZSP_ALLOW), ZSP_E_POLICY);
            CHECK_RC(zsp_policy_set(f.d, f.e.cap, t, ZSP_HOLD), ZSP_E_POLICY);
            set = ZSP_REFUSE;
        } else if (base == ZXV_HEX_BASE_BOTH || base == ZXV_HEX_BASE_FALSE) {
            CHECK_RC(zsp_policy_set(f.d, f.e.cap, t, ZSP_ALLOW), ZSP_E_POLICY);
            set = want == ZSP_HOLD ? ZSP_REFUSE : ZSP_HOLD; /* flip between the two allowed */
        } else {
            set = want == ZSP_ALLOW ? ZSP_HOLD : ZSP_ALLOW;
        }
        CHECK_RC(zsp_policy_set(f.d, f.e.cap, t, set), ZXV_OK);
        CHECK_RC(zsp_policy_get(f.d, t, &dec), ZXV_OK);
        CHECK(dec == set, "policy after set %u", t);
    }
    CHECK_RC(zsp_policy_set(f.d, f.e.cap, 64, ZSP_REFUSE), ZXV_E_ARG);
    CHECK_RC(zsp_policy_set(f.d, f.e.cap, 0, 0), ZXV_E_ARG);
    CHECK_RC(zsp_policy_set(f.d, f.e.cap, 0, 4), ZXV_E_ARG);
    {
        int32_t dec;
        CHECK_RC(zsp_policy_get(f.d, 64, &dec), ZXV_E_ARG);
        CHECK_RC(zsp_policy_get(f.d, 0, NULL), ZXV_E_ARG);
    }
    fx_close(&f);
}

static void test_flows(void) {
    fx_t f;
    uint32_t id = 0, q0, n_ok = 0;
    int32_t st = 0, status = 0;
    uint8_t tag = 0, t2 = 0, base = 0, pol = 0, ph = 0;
    zsp_flow fl;
    if (fx_open(&f, 1) != 0) {
        CHECK(0, "fixture");
        return;
    }
    CHECK_RC(zsp_issue(f.d, f.e.cap, f.a1, 120000), ZXV_OK);
    CHECK_RC(zsp_issue(f.d, f.e.cap, f.a2, 30000), ZXV_OK);

    /* no library rule yet: the hub fails closed and the flow is FAILED */
    fl = flow(SA, SB, f.a1, f.b1, 150, 1, 0);
    CHECK_RC(zsp_flow_submit(f.d, f.e.cap, &fl, &id), ZXV_E_STATE);
    CHECK_RC(zsp_flow_state(f.d, id, &st, NULL, &status), ZXV_OK);
    CHECK(st == ZSP_FLOW_FAILED && status == ZXV_E_STATE, "failed closed");
    CHECK(bal(&f, f.a1) == 120000, "nothing moved");
    CHECK_RC(zsp_limit_add(f.d, f.e.cap, SA, 30000), ZXV_OK);
    CHECK_RC(zsp_limit_add(f.d, f.e.cap, SB, 1000000), ZXV_OK);
    CHECK_RC(zsp_limit_add(f.d, f.e.cap, SC, 1000000), ZXV_OK);
    CHECK_RC(zsp_limit_add(f.d, f.e.cap, 7, 10), ZXV_E_NOT_FOUND);

    /* argument refusals write nothing */
    {
        uint32_t c0 = 0, c1 = 0;
        zsp_flow bad;
        zind_chain_count(zsp_log(f.d), &c0);
        bad = flow(SA, SA, f.a1, f.a2, 150, 1, 0);
        CHECK_RC(zsp_flow_submit(f.d, f.e.cap, &bad, &id), ZXV_E_ARG);
        bad = flow(SA, SB, f.a1, f.b1, 0, 1, 0);
        CHECK_RC(zsp_flow_submit(f.d, f.e.cap, &bad, &id), ZXV_E_RANGE);
        bad = flow(SA, SB, f.a1, f.b1, ZXV_VHUB_AMOUNT_MAX + 1, 1, 0);
        CHECK_RC(zsp_flow_submit(f.d, f.e.cap, &bad, &id), ZXV_E_RANGE);
        bad = flow(SA, SB, f.b1, f.b2, 150, 1, 0);
        CHECK_RC(zsp_flow_submit(f.d, f.e.cap, &bad, &id), ZXV_E_CURRENCY);
        bad = flow(SA, SB, 9999, f.b1, 150, 1, 0);
        CHECK_RC(zsp_flow_submit(f.d, f.e.cap, &bad, &id), ZXV_E_NOT_FOUND);
        bad = flow(SA, 6, f.a1, f.b1, 150, 1, 0);
        CHECK_RC(zsp_flow_submit(f.d, f.e.cap, &bad, &id), ZXV_E_NOT_FOUND);
        CHECK_RC(zsp_flow_submit(f.d, f.e.cap, NULL, &id), ZXV_E_ARG);
        CHECK_RC(zsp_flow_submit(f.d, f.e.cap, &fl, NULL), ZXV_E_ARG);
        zind_chain_count(zsp_log(f.d), &c1);
        CHECK(c0 == c1, "argument errors log nothing");
    }

    /* one approval: TRUE/neutral -> allowed, swapped exactly */
    for (int i = 1; i <= 19; i++) {
        fl = flow(SA, SB, f.a1, f.b1, 150 * (uint64_t) i, 1, 0);
        CHECK_RC(zsp_flow_tag(f.d, &fl, &tag), ZXV_OK);
        CHECK(tag == 0x34 /* true/neutral/potential */, "tag %s", zxv_hex_name(tag));
        CHECK_RC(zsp_flow_submit(f.d, f.e.cap, &fl, &id), ZXV_OK);
        CHECK_RC(zsp_flow_state(f.d, id, &st, &t2, &status), ZXV_OK);
        zxv_hex_unpack(t2, &base, NULL, &ph);
        CHECK(st == ZSP_FLOW_DONE && base == ZXV_HEX_BASE_TRUE && ph == ZXV_HEX_PHASE_MANIFEST,
              "done %d", i);
        n_ok++;
    }
    /* 150 * (1 + .. + 19) = 28500 used of the 30000 limit: 1650 more is refused */
    fl = flow(SA, SB, f.a1, f.b1, 1650, 1, 0);
    CHECK_RC(zsp_flow_submit(f.d, f.e.cap, &fl, &id), ZXV_E_LIMIT);
    CHECK_RC(zsp_flow_state(f.d, id, &st, NULL, &status), ZXV_OK);
    CHECK(st == ZSP_FLOW_FAILED && status == ZXV_E_LIMIT, "limit");
    CHECK(bal(&f, f.a1) == 120000 - 28500 && bal(&f, f.b1) == 57000, "balances %lld %lld",
          (long long) bal(&f, f.a1), (long long) bal(&f, f.b1));
    /* another sender has its own limit */
    fl = flow(SA, SB, f.a2, f.b2, 1500, 1, 0);
    CHECK_RC(zsp_flow_submit(f.d, f.e.cap, &fl, &id), ZXV_OK);
    /* the epoch rolls: the limit starts again */
    {
        uint64_t ep = 0;
        CHECK_RC(zsp_epoch_next(f.d, f.e.cap, &ep), ZXV_OK);
        CHECK(ep == 1, "epoch %llu", (unsigned long long) ep);
    }
    fl = flow(SA, SB, f.a1, f.b1, 1500, 1, 0);
    CHECK_RC(zsp_flow_submit(f.d, f.e.cap, &fl, &id), ZXV_OK);
    /* not exact under the pegs; more than the sender has */
    fl = flow(SA, SB, f.a1, f.b1, 151, 1, 0);
    CHECK_RC(zsp_flow_submit(f.d, f.e.cap, &fl, &id), ZXV_E_RANGE);
    fl = flow(SA, SB, f.a2, f.b1, 29850, 1, 0); /* a2 has 28500 */
    CHECK_RC(zsp_flow_submit(f.d, f.e.cap, &fl, &id), ZXV_E_FUNDS);
    CHECK_RC(zsp_flow_state(f.d, id, &st, NULL, &status), ZXV_OK);
    CHECK(st == ZSP_FLOW_FAILED && status == ZXV_E_FUNDS, "funds");

    /* two approvals: negentropic TRUE -> allowed */
    fl = flow(SB, SC, f.b1, f.c1, 900, 2, 0);
    CHECK_RC(zsp_flow_tag(f.d, &fl, &tag), ZXV_OK);
    zxv_hex_unpack(tag, &base, &pol, NULL);
    CHECK(base == ZXV_HEX_BASE_TRUE && pol == ZXV_HEX_POL_NEGENTROPIC, "negentropic");
    CHECK_RC(zsp_flow_submit(f.d, f.e.cap, &fl, &id), ZXV_OK);
    CHECK(bal(&f, f.c1) == 1, "one C unit");

    /* no authorisation: NEITHER -> held, not quarantined */
    q0 = qcount(&f);
    fl = flow(SA, SB, f.a1, f.b1, 300, 0, 0);
    CHECK_RC(zsp_flow_submit(f.d, f.e.cap, &fl, &id), ZIND_E_HELD);
    CHECK_RC(zsp_flow_state(f.d, id, &st, &tag, NULL), ZXV_OK);
    zxv_hex_unpack(tag, &base, NULL, NULL);
    CHECK(st == ZSP_FLOW_HELD && base == ZXV_HEX_BASE_NEITHER, "held neither");
    CHECK(qcount(&f) == q0, "neither is not quarantined");
    /* conflicting authorisations: BOTH -> held and logged as held */
    fl = flow(SA, SB, f.a1, f.b1, 300, 3, 1);
    CHECK_RC(zsp_flow_submit(f.d, f.e.cap, &fl, &id), ZIND_E_HELD);
    CHECK_RC(zsp_flow_state(f.d, id, &st, &tag, NULL), ZXV_OK);
    zxv_hex_unpack(tag, &base, NULL, NULL);
    CHECK(st == ZSP_FLOW_HELD && base == ZXV_HEX_BASE_BOTH, "held both");
    CHECK(qcount(&f) == q0 + 1, "both quarantined as held");
    {
        uint8_t qt = 0, qr = 0, qb = 0;
        zxv_hexlog_at(f.qlog, q0, NULL, &qt, &qr, NULL, NULL, NULL, NULL);
        zxv_hex_unpack(qt, &qb, NULL, NULL);
        CHECK(qr == ZIND_REASON_HELD_BOTH && qb == ZXV_HEX_BASE_BOTH, "log keeps base BOTH");
    }
    /* objections only: FALSE -> refused */
    fl = flow(SA, SB, f.a1, f.b1, 300, 0, 2);
    CHECK_RC(zsp_flow_submit(f.d, f.e.cap, &fl, &id), ZSP_E_REFUSED);
    CHECK_RC(zsp_flow_state(f.d, id, &st, NULL, NULL), ZXV_OK);
    CHECK(st == ZSP_FLOW_REFUSED, "refused");

    /* outflow over the cap: Forbidden, refused and quarantined, whatever the approvals */
    CHECK_RC(zsp_outflow_cap_set(f.d, f.e.cap, SA, 3000), ZXV_OK);
    CHECK_RC(zsp_outflow_cap_set(f.d, f.e.cap, 7, 3000), ZXV_E_NOT_FOUND);
    CHECK_RC(zsp_outflow_cap_set(f.d, f.e.cap, SA, ZXV_VHUB_AMOUNT_MAX + 1), ZXV_E_RANGE);
    for (uint32_t yes = 0; yes < 4; yes++)
        for (uint32_t no = 0; no < 3; no++) {
            uint32_t qb = qcount(&f);
            int64_t before = bal(&f, f.a1);
            fl = flow(SA, SB, f.a1, f.b1, 3150, yes, no);
            CHECK_RC(zsp_flow_submit(f.d, f.e.cap, &fl, &id), ZSP_E_QUARANTINED);
            CHECK_RC(zsp_flow_state(f.d, id, &st, &tag, NULL), ZXV_OK);
            zxv_hex_unpack(tag, NULL, &pol, NULL);
            CHECK(st == ZSP_FLOW_QUARANTINED && pol == ZXV_HEX_POL_FORBIDDEN, "forbidden %u/%u",
                  yes, no);
            CHECK(qcount(&f) == qb + 1, "quarantined %u/%u", yes, no);
            CHECK(bal(&f, f.a1) == before, "nothing moved %u/%u", yes, no);
        }
    fl = flow(SA, SB, f.a1, f.b1, 3000, 1, 0); /* at the cap: fine */
    CHECK_RC(zsp_flow_submit(f.d, f.e.cap, &fl, &id), ZXV_OK);
    /* even an operator cannot let a Forbidden tag through */
    {
        uint8_t ft = 0;
        zxv_hex_pack(ZXV_HEX_BASE_TRUE, ZXV_HEX_POL_FORBIDDEN, ZXV_HEX_PHASE_POTENTIAL, &ft);
        CHECK_RC(zsp_policy_set(f.d, f.e.cap, ft, ZSP_ALLOW), ZSP_E_POLICY);
    }
    /* a blocked sender */
    CHECK_RC(zsp_block_sender(f.d, f.e.cap, f.a2), ZXV_OK);
    CHECK_RC(zsp_block_sender(f.d, f.e.cap, f.a2), ZXV_E_DUPLICATE);
    CHECK_RC(zsp_block_sender(f.d, f.e.cap, 9999), ZXV_E_NOT_FOUND);
    fl = flow(SA, SB, f.a2, f.b1, 150, 5, 0);
    CHECK_RC(zsp_flow_submit(f.d, f.e.cap, &fl, &id), ZSP_E_QUARANTINED);
    /* the blocked account can still receive */
    fl = flow(SB, SA, f.b1, f.a2, 300, 1, 0);
    CHECK_RC(zsp_flow_submit(f.d, f.e.cap, &fl, &id), ZXV_OK);

    /* an operator choice: unauthorised small flows allowed */
    {
        uint8_t nt = 0;
        zxv_hex_pack(ZXV_HEX_BASE_NEITHER, ZXV_HEX_POL_NEUTRAL, ZXV_HEX_PHASE_POTENTIAL, &nt);
        CHECK_RC(zsp_policy_set(f.d, f.e.cap, nt, ZSP_ALLOW), ZXV_OK);
        fl = flow(SA, SB, f.a1, f.b1, 150, 0, 0);
        CHECK_RC(zsp_flow_submit(f.d, f.e.cap, &fl, &id), ZXV_OK);
        CHECK_RC(zsp_policy_set(f.d, f.e.cap, nt, ZSP_REFUSE), ZXV_OK);
        CHECK_RC(zsp_flow_submit(f.d, f.e.cap, &fl, &id), ZSP_E_REFUSED);
    }
    CHECK_RC(zsp_check(f.d, f.rd, NULL), ZXV_OK);
    CHECK(n_ok == 19, "allowed flows");
    CHECK_RC(zsp_flow_state(f.d, 0, &st, NULL, NULL), ZXV_E_NOT_FOUND);
    CHECK_RC(zsp_flow_state(f.d, 100000, &st, NULL, NULL), ZXV_E_NOT_FOUND);
    fx_close(&f);
}

static void test_decisions(void) {
    fx_t f;
    uint32_t held1 = 0, held2 = 0, held3 = 0, done = 0, q0;
    int32_t st = 0;
    uint8_t sig[ZIND_SIG_BYTES], sig2[ZIND_SIG_BYTES], tag = 0, base = 0, ph = 0;
    zsp_flow fl;
    if (fx_open(&f, 1) != 0) {
        CHECK(0, "fixture");
        return;
    }
    CHECK_RC(zsp_issue(f.d, f.e.cap, f.a1, 120000), ZXV_OK);
    CHECK_RC(zsp_limit_add(f.d, f.e.cap, SA, 3000), ZXV_OK);
    CHECK_RC(zsp_limit_add(f.d, f.e.cap, SB, 1000000), ZXV_OK);
    CHECK_RC(zsp_officer_add(f.d, f.e.cap, zind_signer_pk(f.officer)), ZXV_E_DUPLICATE);
    fl = flow(SA, SB, f.a1, f.b1, 1500, 2, 1);
    CHECK_RC(zsp_flow_submit(f.d, f.e.cap, &fl, &held1), ZIND_E_HELD);
    fl = flow(SA, SB, f.a1, f.b1, 1500, 0, 0);
    CHECK_RC(zsp_flow_submit(f.d, f.e.cap, &fl, &held2), ZIND_E_HELD);
    fl = flow(SA, SB, f.a1, f.b2, 3000, 1, 1);
    CHECK_RC(zsp_flow_submit(f.d, f.e.cap, &fl, &held3), ZIND_E_HELD);
    fl = flow(SA, SB, f.a1, f.b1, 150, 1, 0);
    CHECK_RC(zsp_flow_submit(f.d, f.e.cap, &fl, &done), ZXV_OK);

    {
        uint8_t m[128];
        size_t len = 0;
        CHECK_RC(zsp_decision_message(f.d, done, ZSP_RELEASE, m, sizeof m, &len), ZXV_E_STATE);
        CHECK_RC(zsp_decision_message(f.d, held1, 3, m, sizeof m, &len), ZXV_E_ARG);
        CHECK_RC(zsp_decision_message(f.d, held1, ZSP_RELEASE, m, 10, &len), ZXV_E_BUFFER);
        CHECK(len == 52, "decision message is 52 bytes (%zu)", len);
        CHECK_RC(zsp_decision_message(f.d, 999, ZSP_RELEASE, m, sizeof m, &len), ZXV_E_NOT_FOUND);
    }
    /* forged by the rogue, unregistered rogue key, wrong decision, other flow */
    q0 = qcount(&f);
    CHECK(decide_sig(&f, f.rogue, held1, ZSP_RELEASE, sig) == 0, "sign");
    CHECK_RC(zsp_decide(f.d, f.e.cap, held1, zind_signer_pk(f.officer), ZSP_RELEASE, 1, sig),
             ZXV_E_VERIFY);
    CHECK_RC(zsp_decide(f.d, f.e.cap, held1, zind_signer_pk(f.rogue), ZSP_RELEASE, 1, sig),
             ZSP_E_OFFICER);
    CHECK(decide_sig(&f, f.officer, held1, ZSP_DENY, sig) == 0, "sign");
    CHECK_RC(zsp_decide(f.d, f.e.cap, held1, zind_signer_pk(f.officer), ZSP_RELEASE, 1, sig),
             ZXV_E_VERIFY);
    CHECK_RC(zsp_decide(f.d, f.e.cap, held2, zind_signer_pk(f.officer), ZSP_DENY, 1, sig),
             ZXV_E_VERIFY);
    {
        uint8_t flip[ZIND_SIG_BYTES];
        CHECK(decide_sig(&f, f.officer, held1, ZSP_RELEASE, flip) == 0, "sign");
        for (int i = 0; i < 8; i++) {
            flip[(i * 577) % ZIND_SIG_BYTES] ^= (uint8_t) (1u << i);
            CHECK_RC(zsp_decide(f.d, f.e.cap, held1, zind_signer_pk(f.officer), ZSP_RELEASE, 1,
                                flip),
                     ZXV_E_VERIFY);
            flip[(i * 577) % ZIND_SIG_BYTES] ^= (uint8_t) (1u << i);
        }
    }
    CHECK(qcount(&f) == q0 + 11, "forged decisions quarantined (%u)", qcount(&f) - q0);
    CHECK_RC(zsp_flow_state(f.d, held1, &st, NULL, NULL), ZXV_OK);
    CHECK(st == ZSP_FLOW_HELD, "still held");
    CHECK(bal(&f, f.a1) == 120000 - 150, "nothing moved");

    /* the genuine release */
    CHECK(decide_sig(&f, f.officer, held1, ZSP_RELEASE, sig) == 0, "sign");
    CHECK_RC(zsp_decide(f.d, f.e.cap, held1, zind_signer_pk(f.officer), ZSP_RELEASE, 50, sig),
             ZXV_OK);
    CHECK_RC(zsp_flow_state(f.d, held1, &st, &tag, NULL), ZXV_OK);
    zxv_hex_unpack(tag, &base, NULL, &ph);
    CHECK(st == ZSP_FLOW_RELEASED && base == ZXV_HEX_BASE_TRUE && ph == ZXV_HEX_PHASE_MANIFEST,
          "released");
    CHECK(bal(&f, f.a1) == 120000 - 1650 && bal(&f, f.b1) == 300 + 3000, "swapped");
    CHECK_RC(zsp_decide(f.d, f.e.cap, held1, zind_signer_pk(f.officer), ZSP_RELEASE, 50, sig),
             ZIND_E_REPLAY);
    /* deny */
    CHECK(decide_sig(&f, f.officer, held2, ZSP_DENY, sig2) == 0, "sign");
    CHECK_RC(zsp_decide(f.d, f.e.cap, held2, zind_signer_pk(f.officer), ZSP_DENY, 50, sig2),
             ZXV_OK);
    CHECK_RC(zsp_decide(f.d, f.e.cap, held2, zind_signer_pk(f.officer), ZSP_DENY, 50, sig2),
             ZIND_E_REPLAY);
    CHECK_RC(zsp_flow_state(f.d, held2, &st, &tag, NULL), ZXV_OK);
    zxv_hex_unpack(tag, &base, NULL, NULL);
    CHECK(st == ZSP_FLOW_DENIED && base == ZXV_HEX_BASE_FALSE, "denied");
    CHECK_RC(zsp_decide(f.d, f.e.cap, done, zind_signer_pk(f.officer), ZSP_DENY, 50, sig2),
             ZXV_E_STATE);
    /* a release the library refuses (the sender's limit is used up) stays held */
    CHECK(decide_sig(&f, f.officer, held3, ZSP_RELEASE, sig) == 0, "sign");
    CHECK_RC(zsp_decide(f.d, f.e.cap, held3, zind_signer_pk(f.officer), ZSP_RELEASE, 60, sig),
             ZXV_E_LIMIT);
    CHECK_RC(zsp_flow_state(f.d, held3, &st, NULL, NULL), ZXV_OK);
    CHECK(st == ZSP_FLOW_HELD, "still held after a library refusal");
    CHECK_RC(zsp_epoch_next(f.d, f.e.cap, NULL), ZXV_OK);
    CHECK_RC(zsp_decide(f.d, f.e.cap, held3, zind_signer_pk(f.officer), ZSP_RELEASE, 61, sig),
             ZXV_OK);
    CHECK(bal(&f, f.b2) == 6000, "released after rollover");
    CHECK_RC(zsp_check(f.d, f.rd, NULL), ZXV_OK);
    fx_close(&f);
}

static void test_log(void) {
    fx_t f;
    const zind_chain *c;
    uint32_t n = 0, bad = 0, id = 0;
    zsp_flow fl;
    if (fx_open(&f, 1) != 0) {
        CHECK(0, "fixture");
        return;
    }
    CHECK_RC(zsp_issue(f.d, f.e.cap, f.a1, 3000), ZXV_OK);
    CHECK_RC(zsp_limit_add(f.d, f.e.cap, SA, 3000), ZXV_OK);
    CHECK_RC(zsp_limit_add(f.d, f.e.cap, SB, 3000), ZXV_OK);
    fl = flow(SA, SB, f.a1, f.b1, 1500, 1, 0);
    CHECK_RC(zsp_flow_submit(f.d, f.e.cap, &fl, &id), ZXV_OK);
    c = zsp_log(f.d);
    CHECK_RC(zind_chain_count(c, &n), ZXV_OK);
    /* create, officer, 3 ccys, 3 pegs, 5 accounts, issue, 2 limits, flow */
    CHECK(n == 17, "17 records (%u)", n);
    {
        const uint8_t *trust[1] = {zind_signer_pk(f.desk_key)};
        const uint8_t *wrong[1] = {zind_signer_pk(f.rogue)};
        CHECK_RC(zind_chain_verify(f.e.cap, c, trust, 1, &bad), ZXV_OK);
        CHECK_RC(zind_chain_verify(f.e.cap, c, wrong, 1, &bad), ZIND_E_UNTRUSTED);
    }
    for (uint32_t i = 0; i < n; i++) {
        zind_record r;
        uint32_t pid = 99;
        zind_rd rd;
        CHECK_RC(zind_chain_at(c, i, &r), ZXV_OK);
        zind_rd_init(&rd, r.payload, r.payload_len);
        pid = zind_rd_u32(&rd);
        CHECK(!rd.bad && pid == 1, "record %u names operator process 1", i);
    }
    {
        size_t len = 0;
        uint8_t *buf;
        zind_chain *imp = NULL;
        const uint8_t *trust[1] = {zind_signer_pk(f.desk_key)};
        zind_chain_export(c, NULL, 0, &len);
        buf = (uint8_t *) malloc(len);
        CHECK(buf != NULL, "alloc");
        if (buf) {
            CHECK_RC(zind_chain_export(c, buf, len, &len), ZXV_OK);
            CHECK_RC(zind_chain_import("sovereign-peg", buf, len, &imp), ZXV_OK);
            CHECK_RC(zind_chain_verify(f.e.cap, imp, trust, 1, &bad), ZXV_OK);
            zind_chain_destroy(imp);
            imp = NULL;
            /* flip one payload byte of the last record (the flow's approvals) */
            buf[len - ZIND_SIG_BYTES - 96 - 30] ^= 1;
            if (zind_chain_import("sovereign-peg", buf, len, &imp) == ZXV_OK) {
                CHECK_RC(zind_chain_verify(f.e.cap, imp, trust, 1, &bad), ZIND_E_CHAIN);
                CHECK(bad == n - 1, "tamper found at the last record (%u)", bad);
            } else {
                CHECK(1, "tamper refused at import");
            }
            zind_chain_destroy(imp);
            free(buf);
        }
    }
    fx_close(&f);
}

/* After a halt, every call fails closed. */
static void test_poisoned(void) {
    fx_t f;
    uint32_t id = 0;
    zsp_flow fl;
    if (fx_open(&f, 1) != 0) {
        CHECK(0, "fixture");
        return;
    }
    CHECK_RC(zsp_issue(f.d, f.e.cap, f.a1, 3000), ZXV_OK);
    CHECK_RC(zxv_ctx_halt(f.e.ctx, "test"), ZXV_OK);
    fl = flow(SA, SB, f.a1, f.b1, 150, 1, 0);
    CHECK(zsp_flow_submit(f.d, f.e.cap, &fl, &id) != ZXV_OK, "flow after halt");
    CHECK(zsp_issue(f.d, f.e.cap, f.a1, 1) != ZXV_OK, "issue after halt");
    CHECK(zsp_epoch_next(f.d, f.e.cap, NULL) != ZXV_OK, "epoch after halt");
    CHECK_RC(zsp_quote(f.d, f.rd, SA, SB, 150, NULL, NULL, NULL), ZXV_E_POISONED);
    CHECK_RC(zsp_check(f.d, f.rd, NULL), ZXV_E_POISONED);
    fx_close(&f);
}

int main(void) {
    test_names();
    test_peg_parse();
    test_operator_only();
    test_ccy_and_pegs();
    test_quotes();
    test_issue();
    test_policy();
    test_flows();
    test_decisions();
    test_log();
    test_poisoned();
    return zt_report("test_peg");
}
