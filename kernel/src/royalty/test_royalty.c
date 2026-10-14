/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_royalty.c — IP royalties (royalty.h R1-R13) on a real pay_ledger:
 * signed work manifests, derivative chains, realised-sale receipts, the
 * assurance fee, exact upstream floors, largest-remainder owner splits, the
 * opt-in giving / investing split, quote digests that go stale on any change,
 * one atomic posting per payment, and the audit after every step. A
 * deterministic xorshift property loop checks every payment against an
 * independent reference computed here with 128-bit host arithmetic.
 * Integer only: no floating point anywhere. */
#include <stdio.h>
#include <string.h>
#include "royalty.h"
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

typedef unsigned __int128 u128;
#define ROY_MAX_ACCOUNTS_TEST 64u

static pay_ledger_t L;
static roy_t R;
static uint16_t usd, eur;
static uint32_t iss, fee, buyer, payer, payer2, eur_acct, nat_acct, oacct[12], gacct[4], iacct[4];
static uint64_t nreq;
static int nverify;

/* ---- signatures: sig = SHA-256(signer || msg) ---- */
static void sign(uint32_t who, const uint8_t *m, uint32_t n, uint8_t out[32])
{
    sha256_ctx_t c;
    uint8_t w[4] = {(uint8_t) who, (uint8_t) (who >> 8), (uint8_t) (who >> 16),
                    (uint8_t) (who >> 24)};
    sha256_init(&c);
    sha256_update(&c, w, 4);
    sha256_update(&c, m, n);
    sha256_final(&c, out);
}

static bool verify(void *ctx, uint32_t who, const uint8_t *m, uint32_t n, const uint8_t *sig,
                   uint32_t sl)
{
    uint8_t want[32];
    (*(int *) ctx)++;
    if (sl != 32) return false;
    sign(who, m, n, want);
    return !memcmp(want, sig, 32);
}

/* ---- ledger helpers ---- */
static pay_posting_req_t *mk(void)
{
    static pay_posting_req_t r;
    uint8_t in[9], d[32];
    memset(&r, 0, sizeof r);
    nreq++;
    memcpy(in, &nreq, 8);
    in[8] = 'R';
    sha256(in, sizeof in, d);
    memcpy(r.idem_key, d, 32);
    pay_uetr_from_random(d + 8, r.uetr);
    snprintf(r.e2e, sizeof r.e2e, "RY-%llu", (unsigned long long) nreq);
    return &r;
}

static uint32_t open_acct(uint16_t as, uint32_t owner, pay_cap_t cap, uint8_t flags)
{
    uint32_t a = 0;
    pay_ledger_open(&L, owner, as, cap, flags, &a);
    return a;
}

static uint64_t deb(uint32_t a)
{
    return pay_ledger_account(&L, a)->debit;
}

/* a realised sale: buyer pays `to` amt; returns the provenance hash */
static void sale_to(uint32_t to, uint64_t amt, uint8_t out[32])
{
    pay_receipt_t rc;
    pay_status_t st = pay_ledger_transfer(&L, mk(), buyer, to, amt, &rc);
    if (st != PAY_OK) printf("sale refused %d\n", st);
    memcpy(out, rc.hash, 32);
}
static void sale(uint64_t amt, uint8_t out[32])
{
    sale_to(payer, amt, out);
}

static void setup(void)
{
    pay_ledger_init(&L, NULL);
    memset(&R, 0, sizeof R);
    nreq = 0;
    nverify = 0;
    pay_ledger_add_fiat(&L, "USD", 840, 2, &usd);
    pay_ledger_add_fiat(&L, "EUR", 978, 2, &eur);
    iss = open_acct(usd, 1, PAY_CAP_FINANCIAL, PAY_ACCT_ISSUER);
    fee = open_acct(usd, 2, PAY_CAP_FINANCIAL, 0);
    buyer = open_acct(usd, 3, PAY_CAP_FINANCIAL, 0);
    payer = open_acct(usd, 4, PAY_CAP_FINANCIAL, 0);
    payer2 = open_acct(usd, 5, PAY_CAP_FINANCIAL, 0);
    eur_acct = open_acct(eur, 6, PAY_CAP_FINANCIAL, 0);
    nat_acct = open_acct(usd, 7, PAY_CAP_INTELLECTUAL, 0);
    for (int i = 0; i < 12; i++) oacct[i] = open_acct(usd, 100 + i, PAY_CAP_FINANCIAL, 0);
    for (int i = 0; i < 4; i++) gacct[i] = open_acct(usd, 200 + i, PAY_CAP_FINANCIAL, 0);
    for (int i = 0; i < 4; i++) iacct[i] = open_acct(usd, 300 + i, PAY_CAP_FINANCIAL, 0);
    pay_receipt_t rc;
    pay_ledger_issue(&L, mk(), iss, buyer, (uint64_t) 1 << 58, &rc);
    roy_status_t st = roy_init(&R, &L, fee, verify, &nverify);
    if (st != ROY_OK) printf("setup: init %d\n", st);
}

/* ---- manifests ---- */
static void man(roy_manifest_t *m, uint8_t tag)
{
    memset(m, 0, sizeof *m);
    uint8_t t[2] = {'W', tag};
    sha256(t, 2, m->id);
    m->version = 1;
    m->terms = ROY_TERMS_ZXV_HW_COMMERCIAL;
    m->n_owners = 1;
    m->owner[0].id = 1000u + tag;
    m->owner[0].account = oacct[0];
    m->owner[0].weight = 1;
}

static void own(roy_manifest_t *m, uint32_t i, uint32_t id, uint32_t acct, uint64_t w)
{
    m->owner[i].id = id;
    m->owner[i].account = acct;
    m->owner[i].weight = w;
    if (m->n_owners < i + 1) m->n_owners = i + 1;
}

static void child_of(roy_manifest_t *m, const roy_manifest_t *p, uint64_t n, uint64_t d)
{
    m->has_parent = true;
    memcpy(m->parent, p->id, 32);
    m->up_num = n;
    m->up_den = d;
}

static void msig(const roy_manifest_t *m, uint32_t who, uint8_t s[32])
{
    uint8_t msg[ROY_MANIFEST_MSG];
    roy_manifest_msg(m, msg);
    sign(who, msg, ROY_MANIFEST_MSG, s);
}

static roy_status_t reg(const roy_manifest_t *m)
{
    uint8_t s[32];
    msig(m, m->owner[0].id, s);
    return roy_register(&R, &L, m, s, 32);
}

/* update signed by every current owner of the registered work */
static roy_status_t upd(const roy_manifest_t *m)
{
    uint8_t s[ROY_MAX_OWNERS][32];
    roy_sig_t sg[ROY_MAX_OWNERS];
    const roy_work_t *w = roy_work(&R, m->id);
    uint32_t n = w ? w->m.n_owners : 0;
    for (uint32_t i = 0; i < n; i++) {
        msig(m, w->m.owner[i].id, s[i]);
        sg[i].sig = s[i];
        sg[i].len = 32;
    }
    return roy_update(&R, &L, m, sg, n);
}

static roy_status_t opt(const uint8_t id[32], uint32_t owner, uint32_t ga, uint16_t gb, uint32_t ia,
                        uint16_t ib, uint64_t seq)
{
    roy_split_t s = {ga, ia, gb, ib, seq};
    uint8_t msg[ROY_SPLIT_MSG], sg[32];
    const roy_work_t *w = roy_work(&R, id);
    roy_split_msg(id, w ? w->m.version : 0, owner, &s, msg);
    sign(owner, msg, ROY_SPLIT_MSG, sg);
    return roy_split_set(&R, &L, id, owner, &s, sg, 32);
}

/* quote then pay with a fresh realised sale of `gross` to payer */
static roy_status_t qpay(const uint8_t id[32], uint64_t gross, roy_quote_t *q)
{
    uint8_t rcpt[32];
    roy_quote_t qq;
    roy_status_t st = roy_quote(&R, id, gross, &qq);
    if (st != ROY_OK) return st;
    sale(gross, rcpt);
    return roy_pay(&R, &L, mk(), payer, id, gross, rcpt, qq.digest, q);
}

/* ================= tests ================= */

static void test_init(void)
{
    setup();
    CHECK(R.ready && R.fee_acct == fee && R.asset == usd && R.cap == PAY_CAP_FINANCIAL &&
              R.last_ledger == PAY_OK && R.n_works == 0,
          "init records the fee account, asset and form");
    CHECK(roy_check(&R, &L), "a fresh registry audits clean");
    roy_t x;
    CHECK(roy_init(NULL, &L, fee, verify, 0) == ROY_ERR_ARG &&
              roy_init(&x, NULL, fee, verify, 0) == ROY_ERR_ARG &&
              roy_init(&x, &L, fee, NULL, 0) == ROY_ERR_ARG,
          "NULL arguments refused");
    CHECK(roy_init(&x, &L, L.n_accounts, verify, 0) == ROY_ERR_ARG && !x.ready,
          "missing fee account refused, not ready");
    CHECK(roy_init(&x, &L, iss, verify, 0) == ROY_ERR_ARG, "issuer fee account refused");
    L.acct[fee].active = false;
    CHECK(roy_init(&x, &L, fee, verify, 0) == ROY_ERR_ARG, "inactive fee account refused");
    L.acct[fee].active = true;
    CHECK(roy_init(&x, &L, eur_acct, verify, 0) == ROY_OK && x.asset == eur, "EUR fee account");
    CHECK(roy_init(&x, &L, nat_acct, verify, 0) == ROY_OK && x.cap == PAY_CAP_INTELLECTUAL,
          "fee account form recorded");
    x.n_works = 5;
    CHECK(roy_init(&x, &L, fee, verify, 0) == ROY_OK && x.n_works == 0, "init clears state");
    roy_t nr;
    memset(&nr, 0, sizeof nr);
    uint8_t id[32] = {0};
    roy_quote_t q;
    roy_manifest_t m;
    man(&m, 1);
    CHECK(roy_quote(&nr, id, 1, &q) == ROY_ERR_ARG && !roy_check(&nr, &L) &&
              roy_register(&nr, &L, &m, id, 32) == ROY_ERR_ARG,
          "not-ready registry refuses everything");
}

static void test_terms(void)
{
    roy_terms_t t, hw;
    uint64_t v = 7;
    roy_terms_zxv_hw(&hw);
    roy_terms_zxv_hw(NULL);
    CHECK(hw.kind == ROY_TERMS_PERCENT && hw.num == 18 && hw.den == 100 && hw.flat == 0,
          "hardware preset is 18/100 of gross");
    CHECK(roy_terms_ok(&hw) && !roy_terms_ok(NULL), "preset valid; NULL invalid");
    t = (roy_terms_t){ROY_TERMS_PERCENT, 1, 1, 0};
    CHECK(roy_terms_ok(&t), "100% valid");
    t = (roy_terms_t){ROY_TERMS_PERCENT, 2, 1, 0};
    CHECK(!roy_terms_ok(&t), "num > den invalid");
    t = (roy_terms_t){ROY_TERMS_PERCENT, 0, 1, 0};
    CHECK(!roy_terms_ok(&t), "num 0 invalid");
    t = (roy_terms_t){ROY_TERMS_PERCENT, 0, 0, 0};
    CHECK(!roy_terms_ok(&t), "den 0 invalid");
    t = (roy_terms_t){ROY_TERMS_PERCENT, 1, 2, 1};
    CHECK(!roy_terms_ok(&t), "percent with a flat invalid");
    t = (roy_terms_t){ROY_TERMS_FLAT, 0, 0, 5};
    CHECK(roy_terms_ok(&t), "flat 5 valid");
    t = (roy_terms_t){ROY_TERMS_FLAT, 0, 0, 0};
    CHECK(!roy_terms_ok(&t), "flat 0 invalid");
    t = (roy_terms_t){ROY_TERMS_FLAT, 1, 0, 5};
    CHECK(!roy_terms_ok(&t), "flat with num invalid");
    t = (roy_terms_t){ROY_TERMS_FLAT, 0, 1, 5};
    CHECK(!roy_terms_ok(&t), "flat with den invalid");
    t = (roy_terms_t){0, 1, 2, 0};
    CHECK(!roy_terms_ok(&t), "kind 0 invalid");
    t = (roy_terms_t){ROY_TERMS_FLAT, 0, 0, 1};
    CHECK(roy_terms_ok(&t), "flat 1 valid");
    t = (roy_terms_t){3, 0, 0, 5};
    CHECK(!roy_terms_ok(&t), "kind 3 shaped like a flat term invalid");
    t = (roy_terms_t){0, 0, 0, 5};
    CHECK(!roy_terms_ok(&t), "kind 0 shaped like a flat term invalid");
    t = (roy_terms_t){3, 1, 2, 0};
    CHECK(!roy_terms_ok(&t), "kind 3 invalid");

    CHECK(roy_royalty(&hw, 1000, &v) && v == 180, "18% of 1000 = 180");
    CHECK(roy_royalty(&hw, 999, &v) && v == 179, "18% of 999 floors to 179");
    CHECK(roy_royalty(&hw, 5, &v) && v == 0, "18% of 5 is 0");
    CHECK(roy_royalty(&hw, 6, &v) && v == 1, "18% of 6 is 1");
    CHECK(roy_royalty(&hw, PAY_BAL_MAX - 1, &v) &&
              v == (uint64_t) (((u128) (PAY_BAL_MAX - 1) * 18) / 100),
          "largest gross");
    v = 7;
    CHECK(!roy_royalty(&hw, PAY_BAL_MAX, &v) && v == 7, "gross at PAY_BAL_MAX refused");
    CHECK(!roy_royalty(&hw, 0, &v) && v == 7, "gross 0 refused");
    CHECK(!roy_royalty(&hw, 1, NULL) && !roy_royalty(NULL, 1, &v), "NULL refused");
    t = (roy_terms_t){ROY_TERMS_FLAT, 0, 0, 50};
    CHECK(roy_royalty(&t, 50, &v) && v == 50, "flat at gross == flat");
    CHECK(roy_royalty(&t, 51, &v) && v == 50, "flat above");
    v = 7;
    CHECK(!roy_royalty(&t, 49, &v) && v == 7, "flat above the gross refused");
    t = (roy_terms_t){ROY_TERMS_PERCENT, 1, 1, 0};
    CHECK(roy_royalty(&t, 77, &v) && v == 77, "100% is the gross");
    t = (roy_terms_t){ROY_TERMS_PERCENT, 2, 1, 0};
    CHECK(!roy_royalty(&t, 77, &v), "invalid terms refused");
    CHECK(roy_grant_cap(10, 3) == 3 && roy_grant_cap(3, 10) == 3 && roy_grant_cap(5, 5) == 5 &&
              roy_grant_cap(0, 9) == 0 && roy_grant_cap(9, 0) == 0 &&
              roy_grant_cap(UINT64_MAX, UINT64_MAX - 1) == UINT64_MAX - 1,
          "R12 grant cap = min(surplus, requested)");
}

static void test_lrm(void)
{
    uint64_t o[8], w[8];
    w[0] = w[1] = w[2] = 1;
    CHECK(roy_lrm(10, w, 3, o) && o[0] == 4 && o[1] == 3 && o[2] == 3, "10/3: tie to index 0");
    CHECK(roy_lrm(11, w, 3, o) && o[0] == 4 && o[1] == 4 && o[2] == 3, "11/3: two ties");
    CHECK(roy_lrm(12, w, 3, o) && o[0] == 4 && o[1] == 4 && o[2] == 4, "12/3 even");
    w[0] = 3;
    w[1] = 1;
    CHECK(roy_lrm(7, w, 2, o) && o[0] == 5 && o[1] == 2, "7 by 3:1 -> 5,2 (largest remainder)");
    w[0] = 1;
    w[1] = 3;
    CHECK(roy_lrm(7, w, 2, o) && o[0] == 2 && o[1] == 5, "7 by 1:3 -> 2,5");
    w[0] = 2;
    w[1] = 3;
    w[2] = 5;
    CHECK(roy_lrm(9, w, 3, o) && o[0] == 2 && o[1] == 3 && o[2] == 4,
          "9 by 2:3:5 -> remainders 8,7,5: one unit to the first");
    CHECK(roy_lrm(1, w, 3, o) && o[0] == 0 && o[1] == 0 && o[2] == 1,
          "1 by 2:3:5 -> the largest remainder (5) wins");
    w[0] = 5;
    w[1] = 3;
    w[2] = 2;
    CHECK(roy_lrm(1, w, 3, o) && o[0] == 1 && o[1] == 0 && o[2] == 0, "1 by 5:3:2 -> first");
    w[0] = 1;
    w[1] = 2;
    CHECK(roy_lrm(2, w, 2, o) && o[0] == 1 && o[1] == 1,
          "2 by 1:2: remainders 2,1 -> unit to index 0");
    CHECK(roy_lrm(0, w, 2, o) && o[0] == 0 && o[1] == 0, "0 splits to zeros");
    w[0] = UINT64_MAX;
    CHECK(roy_lrm(UINT64_MAX, w, 1, o) && o[0] == UINT64_MAX, "one owner takes all");
    w[0] = UINT64_MAX - 1;
    w[1] = 1;
    CHECK(roy_lrm(UINT64_MAX, w, 2, o) && o[0] + o[1] == UINT64_MAX && o[1] == 1,
          "huge weights: exact");
    w[1] = 2;
    o[0] = 77;
    CHECK(!roy_lrm(5, w, 2, o) && o[0] == 77, "weight sum overflow refused, out untouched");
    w[0] = 1;
    w[1] = 0;
    CHECK(!roy_lrm(5, w, 2, o) && o[0] == 77, "zero weight refused");
    w[1] = 1;
    CHECK(!roy_lrm(5, w, 0, o) && !roy_lrm(5, w, 9, o) && !roy_lrm(5, NULL, 1, o) &&
              !roy_lrm(5, w, 1, NULL),
          "bad n / NULL refused");
    for (int i = 0; i < 8; i++) w[i] = 1;
    CHECK(roy_lrm(15, w, 8, o) && o[0] == 2 && o[6] == 2 && o[7] == 1, "8 owners: 7 leftovers");
}

static void test_msgs(void)
{
    roy_manifest_t m;
    uint8_t a[ROY_MANIFEST_MSG], b[ROY_MANIFEST_MSG];
    man(&m, 1);
    own(&m, 1, 77, oacct[1], 0x0102030405060708ull);
    m.version = 0x1122;
    roy_manifest_msg(&m, a);
    CHECK(ROY_MANIFEST_MSG == 265 && !memcmp(a, "zxv/royalty-work/v1", 19) &&
              !memcmp(a + 19, m.id, 32),
          "manifest message: domain and id");
    CHECK(a[51] == 0x22 && a[52] == 0x11 && a[53] == 0 && a[59] == 2 && a[60] == 0,
          "version and owner count little-endian");
    uint32_t o1 = 63 + 16;
    CHECK(a[63] == (uint8_t) m.owner[0].id && a[o1] == 77 && a[o1 + 4] == (uint8_t) oacct[1] &&
              a[o1 + 8] == 8 && a[o1 + 15] == 1,
          "owner slots");
    uint32_t tail = 63 + 128;
    bool z = true;
    for (uint32_t i = 63 + 32; i < tail; i++) z = z && a[i] == 0;
    CHECK(z && a[tail] == 0, "unused slots zero, no parent");
    CHECK(a[tail + 1 + 32 + 16] == ROY_TERMS_PERCENT && a[tail + 1 + 32 + 17] == 18 &&
              a[tail + 1 + 32 + 25] == 100 && a[264] == 0,
          "terms encoded");
    m.owner[5].id = 999; /* beyond n_owners: ignored */
    m.owner[2] = (roy_owner_t){55, 66, 77};
    roy_manifest_msg(&m, b);
    CHECK(!memcmp(a, b, sizeof a), "unused slots never signed");
    roy_manifest_t p;
    man(&p, 2);
    child_of(&m, &p, 3, 0x0405);
    roy_manifest_msg(&m, b);
    CHECK(b[tail] == 1 && !memcmp(b + tail + 1, p.id, 32) && b[tail + 33] == 3 &&
              b[tail + 41] == 5 && b[tail + 42] == 4,
          "parent and up share encoded");
    m.terms = (roy_terms_t){ROY_TERMS_FLAT, 0, 0, 0x99};
    roy_manifest_msg(&m, b);
    CHECK(b[tail + 49] == ROY_TERMS_FLAT && b[257] == 0x99, "flat encoded last");
    m.has_parent = false;
    roy_manifest_msg(&m, b);
    z = true;
    for (uint32_t i = tail + 1; i < tail + 33; i++) z = z && b[i] == 0;
    CHECK(z, "no parent: parent bytes zero whatever the struct holds");

    roy_split_t s = {0x0A0B0C0D, 0x01020304, 0x1234, 0x5678, 0x99};
    uint8_t sm[ROY_SPLIT_MSG];
    roy_split_msg(m.id, 0x77, 0xAABBCCDD, &s, sm);
    CHECK(ROY_SPLIT_MSG == 84 && !memcmp(sm, "zxv/royalty-split/v1", 20) &&
              !memcmp(sm + 20, m.id, 32) && sm[52] == 0x77 && sm[60] == 0xDD && sm[63] == 0xAA &&
              sm[64] == 0x0D && sm[68] == 0x34 && sm[69] == 0x12 && sm[70] == 0x04 &&
              sm[74] == 0x78 && sm[75] == 0x56 && sm[76] == 0x99 && sm[83] == 0,
          "split message layout");
}

static void test_register(void)
{
    setup();
    roy_manifest_t m, x;
    uint8_t s[32];
    man(&m, 1);
    own(&m, 1, 2001, oacct[1], 3);
    CHECK(reg(&m) == ROY_OK && R.n_works == 1 && roy_find(&R, m.id) == 0, "registered");
    CHECK(nverify == 1, "one signature checked");
    const roy_work_t *w = roy_work(&R, m.id);
    CHECK(w && w->m.version == 1 && w->m.n_owners == 2 && w->depth == 0 && !w->m.has_parent &&
              w->split[0].giving_bps == 0 && w->split[1].invest_bps == 0,
          "stored, default split 0 (opt-in only)");
    CHECK(roy_check(&R, &L), "audit clean");
    CHECK(reg(&m) == ROY_ERR_DUP && R.n_works == 1, "duplicate id refused");
    x = m;
    x.version = 9;
    CHECK(reg(&x) == ROY_ERR_DUP, "duplicate id with another version refused");
    CHECK(roy_lines(&R, m.id) == 4, "lines: payer, fee, two owners");

    man(&x, 2);
    msig(&x, x.owner[0].id + 1, s);
    CHECK(roy_register(&R, &L, &x, s, 32) == ROY_ERR_SIG, "wrong signer refused");
    own(&x, 1, 2002, oacct[1], 1);
    msig(&x, 2002, s);
    CHECK(roy_register(&R, &L, &x, s, 32) == ROY_ERR_SIG, "second owner may not register");
    msig(&x, x.owner[0].id, s);
    CHECK(roy_register(&R, &L, &x, s, 31) == ROY_ERR_SIG, "short signature refused");
    CHECK(roy_register(&R, &L, &x, NULL, 32) == ROY_ERR_ARG &&
              roy_register(&R, &L, NULL, s, 32) == ROY_ERR_ARG &&
              roy_register(&R, NULL, &x, s, 32) == ROY_ERR_ARG &&
              roy_register(NULL, &L, &x, s, 32) == ROY_ERR_ARG,
          "NULL refused");
    s[0] ^= 1;
    CHECK(roy_register(&R, &L, &x, s, 32) == ROY_ERR_SIG, "tampered signature refused");
    CHECK(R.n_works == 1 && roy_find(&R, x.id) < 0, "nothing recorded by refusals");

    /* argument checks: each mutation of a valid manifest */
    struct {
        const char *what;
        int k;
    } bad[] = {{"version 0", 0},
               {"no owners", 1},
               {"9 owners", 2},
               {"weight 0", 3},
               {"weight sum overflow", 4},
               {"duplicate owner id", 5},
               {"fee account", 6},
               {"other asset", 7},
               {"other form", 8},
               {"inactive account", 9},
               {"no such account", 10},
               {"bad terms", 11},
               {"up share 0", 12},
               {"up num > den", 13},
               {"no parent, up num", 14},
               {"no parent, up den", 15},
               {"no parent, id bytes", 16},
               {"duplicate ids of owners 1 and 2", 17},
               {"no parent, first id byte", 18}};
    for (unsigned b = 0; b < sizeof bad / sizeof bad[0]; b++) {
        man(&x, 3);
        own(&x, 1, 3002, oacct[2], 1);
        switch (bad[b].k) {
        case 0:
            x.version = 0;
            break;
        case 1:
            x.n_owners = 0;
            break;
        case 2:
            x.n_owners = 9;
            break;
        case 3:
            x.owner[1].weight = 0;
            break;
        case 4:
            x.owner[0].weight = UINT64_MAX;
            break;
        case 5:
            x.owner[1].id = x.owner[0].id;
            break;
        case 6:
            x.owner[1].account = fee;
            break;
        case 7:
            x.owner[1].account = eur_acct;
            break;
        case 8:
            x.owner[1].account = nat_acct;
            break;
        case 9:
            L.acct[oacct[2]].active = false;
            break;
        case 10:
            x.owner[1].account = L.n_accounts;
            break;
        case 11:
            x.terms.num = 101;
            break;
        case 12:
            child_of(&x, &m, 0, 5);
            break;
        case 13:
            child_of(&x, &m, 6, 5);
            break;
        case 14:
            x.up_num = 1;
            break;
        case 15:
            x.up_den = 1;
            break;
        case 16:
            x.parent[31] = 1;
            break;
        case 17:
            own(&x, 2, 3002, oacct[3], 1);
            break;
        case 18:
            x.parent[0] = 1;
            break;
        }
        roy_status_t st = reg(&x);
        L.acct[oacct[2]].active = true;
        char msg[64];
        snprintf(msg, sizeof msg, "refused: %s", bad[b].what);
        CHECK(st == ROY_ERR_ARG && R.n_works == 1, msg);
    }
    man(&x, 3);
    x.owner[0].weight = UINT64_MAX;
    CHECK(reg(&x) == ROY_OK, "one owner with weight UINT64_MAX fits");
    man(&x, 4);
    own(&x, 1, 4002, oacct[0], 5);
    CHECK(reg(&x) == ROY_OK && roy_lines(&R, x.id) == 3, "owners may share an account");
    CHECK(roy_lines(&R, m.parent) == 0, "unknown work has no lines");
    CHECK(roy_find(NULL, m.id) == -1 && roy_find(&R, NULL) == -1 && roy_find(&R, m.parent) == -1 &&
              roy_work(NULL, m.id) == NULL && roy_work(&R, m.parent) == NULL &&
              roy_find(&R, x.id) == 2,
          "roy_find: -1 for NULL / unknown, the index otherwise");
    man(&x, 6);
    x.owner[3] = (roy_owner_t){9, 9, 9}; /* beyond n_owners */
    x.owner[7] = (roy_owner_t){1, 2, 3};
    CHECK(reg(&x) == ROY_OK && roy_work(&R, x.id)->m.owner[3].id == 0 &&
              roy_work(&R, x.id)->m.owner[3].account == 0 &&
              roy_work(&R, x.id)->m.owner[7].weight == 0 && roy_work(&R, x.id)->parent == 0 &&
              roy_check(&R, &L),
          "unused owner slots are stored zeroed");

    /* parents */
    man(&x, 5);
    roy_manifest_t ghost;
    man(&ghost, 99);
    child_of(&x, &ghost, 1, 2);
    CHECK(reg(&x) == ROY_ERR_UNKNOWN, "unregistered parent refused");
    child_of(&x, &m, 1, 1);
    CHECK(reg(&x) == ROY_OK && roy_work(&R, x.id)->depth == 1 && roy_work(&R, x.id)->parent == 0,
          "child of a registered work, upstream 1/1 allowed");
    CHECK(roy_check(&R, &L), "audit clean");

    /* depth: a chain of ROY_MAX_DEPTH + 1 works fits, one more is refused */
    setup();
    roy_manifest_t prev, cur;
    man(&prev, 10);
    CHECK(reg(&prev) == ROY_OK, "chain root");
    bool ok = true;
    for (uint32_t d = 1; d <= ROY_MAX_DEPTH; d++) {
        man(&cur, (uint8_t) (10 + d));
        child_of(&cur, &prev, 1, 2);
        ok = ok && reg(&cur) == ROY_OK && roy_work(&R, cur.id)->depth == d;
        prev = cur;
    }
    CHECK(ok && roy_lines(&R, prev.id) == 3, "depth 8 chain (shared account): 3 lines");
    {
        roy_manifest_t ch[ROY_MAX_LEVELS];
        man(&ch[0], 150);
        for (uint32_t i = 0; i < 6; i++) own(&ch[0], i, 1500 + i, oacct[1 + i], 1);
        bool ok2 = reg(&ch[0]) == ROY_OK;
        for (uint32_t d = 1; d < ROY_MAX_LEVELS; d++) {
            man(&ch[d], (uint8_t) (150 + d));
            ch[d].owner[0].account = oacct[1];
            child_of(&ch[d], &ch[d - 1], 1, 2);
            ok2 = ok2 && reg(&ch[d]) == ROY_OK;
        }
        CHECK(ok2 && roy_lines(&R, ch[ROY_MAX_DEPTH].id) == 8,
              "depth 8 leaf counts the root's six accounts: 8 lines");
        roy_quote_t q;
        CHECK(roy_quote(&R, ch[ROY_MAX_DEPTH].id, 100000000, &q) == ROY_OK && q.n_levels == 9 &&
                  q.n_legs == 7 && q.n_payouts == 14,
              "a 9-level quote fits one posting");
        CHECK(qpay(ch[ROY_MAX_DEPTH].id, 100000000, NULL) == ROY_OK && roy_check(&R, &L),
              "and pays");
        uint8_t s2[32];
        man(&cur, 170);
        child_of(&cur, &ch[ROY_MAX_DEPTH], 1, 2);
        msig(&cur, cur.owner[0].id, s2);
        CHECK(roy_register(&R, &L, &cur, s2, 32) == ROY_ERR_DEPTH, "below depth 8 refused");
    }
    man(&cur, 30);
    child_of(&cur, &prev, 1, 2);
    CHECK(reg(&cur) == ROY_ERR_DEPTH && R.n_works == 2 * ROY_MAX_LEVELS, "depth 9 refused");
    CHECK(roy_check(&R, &L), "audit clean");

    /* full registry */
    ok = true;
    for (uint32_t i = R.n_works; i < ROY_MAX_WORKS; i++) {
        man(&cur, (uint8_t) (40 + i));
        ok = ok && reg(&cur) == ROY_OK;
    }
    man(&cur, 200);
    CHECK(ok && reg(&cur) == ROY_ERR_FULL && R.n_works == ROY_MAX_WORKS, "full registry refused");
    CHECK(roy_check(&R, &L), "audit clean when full");

    /* R9 lines */
    setup();
    man(&x, 50);
    for (uint32_t i = 1; i < 6; i++) own(&x, i, 5000 + i, oacct[i], 1);
    CHECK(reg(&x) == ROY_OK && roy_lines(&R, x.id) == 8, "6 distinct owners: exactly 8 lines");
    man(&cur, 51);
    for (uint32_t i = 1; i < 7; i++) own(&cur, i, 5100 + i, oacct[i], 1);
    int nv = nverify;
    CHECK(reg(&cur) == ROY_ERR_LINES && R.n_works == 1 && roy_find(&R, cur.id) < 0 &&
              !R.work[1].used && nverify == nv + 1,
          "7 distinct owners: 9 lines refused, nothing kept");
    man(&cur, 52);
    child_of(&cur, &x, 1, 2);
    cur.owner[0].account = oacct[7];
    CHECK(reg(&cur) == ROY_ERR_LINES && R.n_works == 1, "a child adding a 9th line refused");
    cur.owner[0].account = oacct[3];
    CHECK(reg(&cur) == ROY_OK && roy_lines(&R, cur.id) == 8, "a child on shared accounts fits");
    CHECK(roy_check(&R, &L), "audit clean");
    for (uint32_t i = 0; i < 8; i++) own(&cur, i, 7000 + i, oacct[0], 1);
    man(&cur, 53);
    for (uint32_t i = 0; i < 8; i++) own(&cur, i, 7000 + i, oacct[0], 1);
    CHECK(reg(&cur) == ROY_OK && roy_work(&R, cur.id)->m.n_owners == 8, "8 owners on one account");
}

static void test_update(void)
{
    setup();
    roy_manifest_t m, p, x;
    man(&p, 1);
    CHECK(reg(&p) == ROY_OK, "parent");
    man(&m, 2);
    own(&m, 1, 2002, oacct[1], 1);
    child_of(&m, &p, 1, 10);
    CHECK(reg(&m) == ROY_OK, "child");
    CHECK(opt(m.id, 2002, gacct[0], 1000, 0, 0, 1) == ROY_OK, "owner 2 opts in");
    CHECK(opt(m.id, m.owner[0].id, 0, 0, iacct[0], 700, 5) == ROY_OK, "owner 1 opts in");
    x = m;
    x.version = 2;
    x.terms.num = 20;
    own(&x, 1, 2002, oacct[2], 2);
    own(&x, 2, 2003, oacct[3], 1);
    int nv = nverify;
    CHECK(upd(&x) == ROY_OK && nverify == nv + 2, "update signed by both current owners");
    const roy_work_t *w = roy_work(&R, m.id);
    CHECK(w->m.version == 2 && w->m.terms.num == 20 && w->m.n_owners == 3 &&
              w->m.owner[1].account == oacct[2],
          "new terms and owners in force");
    CHECK(w->split[1].giving_bps == 1000 && w->split[1].seq == 1 && w->split[2].seq == 0 &&
              w->split[2].giving_bps == 0 && w->split[0].giving_bps == 0,
          "a remaining owner keeps its opt-in; a new owner starts at none");
    CHECK(w->split[0].invest_bps == 700 && w->split[0].seq == 5 &&
              w->split[0].invest_acct == iacct[0],
          "owner 1 (slot 0) keeps its opt-in too");
    CHECK(roy_check(&R, &L), "audit clean");
    CHECK(upd(&x) == ROY_ERR_VERSION, "same version refused (replay)");
    x.version = 1;
    CHECK(upd(&x) == ROY_ERR_VERSION, "older version refused");
    x.version = 3;
    roy_manifest_t y = x;
    y.up_num = 2;
    CHECK(upd(&y) == ROY_ERR_PARENT, "upstream share change refused");
    y = x;
    y.up_den = 20;
    CHECK(upd(&y) == ROY_ERR_PARENT, "upstream den change refused");
    y = x;
    y.has_parent = false;
    y.up_num = y.up_den = 0;
    memset(y.parent, 0, 32);
    CHECK(upd(&y) == ROY_ERR_PARENT, "dropping the parent refused");
    y = x;
    roy_manifest_t q;
    man(&q, 3);
    reg(&q);
    memcpy(y.parent, q.id, 32);
    CHECK(upd(&y) == ROY_ERR_PARENT, "changing the parent refused");
    y = p;
    y.version = 2;
    child_of(&y, &q, 1, 2);
    CHECK(upd(&y) == ROY_ERR_PARENT, "adopting a parent refused");
    y = x;
    y.owner[0].weight = 0;
    CHECK(upd(&y) == ROY_ERR_ARG, "invalid manifest refused");
    man(&y, 77);
    CHECK(upd(&y) == ROY_ERR_UNKNOWN, "unknown work refused");
    /* signatures: every current owner */
    uint8_t s[3][32];
    roy_sig_t sg[3];
    for (int i = 0; i < 3; i++) {
        msig(&x, roy_work(&R, m.id)->m.owner[i].id, s[i]);
        sg[i].sig = s[i];
        sg[i].len = 32;
    }
    CHECK(roy_update(&R, &L, &x, sg, 2) == ROY_ERR_ARG, "too few signatures refused");
    CHECK(roy_update(&R, &L, &x, sg, 4) == ROY_ERR_ARG, "too many signatures refused");
    CHECK(roy_update(&R, &L, &x, NULL, 3) == ROY_ERR_ARG &&
              roy_update(&R, &L, NULL, sg, 3) == ROY_ERR_ARG &&
              roy_update(&R, NULL, &x, sg, 3) == ROY_ERR_ARG &&
              roy_update(NULL, &L, &x, sg, 3) == ROY_ERR_ARG,
          "NULL refused");
    sg[2].sig = NULL;
    CHECK(roy_update(&R, &L, &x, sg, 3) == ROY_ERR_SIG, "a missing signature refused");
    sg[2].sig = s[2];
    sg[2].len = 31;
    CHECK(roy_update(&R, &L, &x, sg, 3) == ROY_ERR_SIG, "a short signature refused");
    sg[2].len = 32;
    sg[0].sig = s[1];
    CHECK(roy_update(&R, &L, &x, sg, 3) == ROY_ERR_SIG, "a signature by the wrong owner refused");
    sg[0].sig = s[0];
    CHECK(roy_work(&R, m.id)->m.version == 2, "refusals changed nothing");
    CHECK(roy_update(&R, &L, &x, sg, 3) == ROY_OK && roy_work(&R, m.id)->m.version == 3,
          "all three sign: accepted");
    /* lines: an update that breaks the fit is reverted */
    y = x;
    y.version = 4;
    for (uint32_t i = 0; i < 7; i++) own(&y, i, 2100 + i, oacct[i], 1);
    w = roy_work(&R, m.id);
    roy_work_t before = *w;
    CHECK(upd(&y) == ROY_ERR_LINES && !memcmp(&before, w, sizeof before),
          "update over 8 lines refused and fully reverted");
    /* a parent update that breaks a child's fit */
    y = p;
    y.version = 2;
    for (uint32_t i = 0; i < 4; i++) own(&y, i, 1100 + i, oacct[4 + i], 1);
    before = *roy_work(&R, p.id);
    CHECK(upd(&y) == ROY_ERR_LINES && !memcmp(&before, roy_work(&R, p.id), sizeof before),
          "parent update that would break its child refused");
    CHECK(roy_check(&R, &L), "audit clean");
}

static void test_update8(void)
{
    setup();
    roy_manifest_t m, x;
    man(&m, 1);
    for (uint32_t i = 0; i < 8; i++) own(&m, i, 100 + i, oacct[i & 1], 1 + i);
    CHECK(reg(&m) == ROY_OK, "8 owners");
    for (uint32_t i = 0; i < 8; i++) opt(m.id, 100 + i, 0, 0, iacct[0], (uint16_t) (10 + i), 1);
    x = m;
    x.version = 2;
    for (uint32_t i = 0; i < 8; i++) own(&x, i, 107 - i, oacct[i & 1], 8 - i);
    CHECK(upd(&x) == ROY_OK, "8 owners reordered");
    const roy_work_t *w = roy_work(&R, m.id);
    bool ok = true;
    for (uint32_t i = 0; i < 8; i++)
        ok = ok && w->split[i].invest_bps == 17 - i && w->split[i].seq == 1;
    CHECK(ok, "every opt-in follows its owner id to the new slot");
    roy_quote_t q;
    CHECK(roy_quote(&R, m.id, 1000000, &q) == ROY_OK && q.n_payouts == 8 && q.n_legs == 4 &&
              q.fee + q.owners_total + q.investing_total == q.royalty,
          "8-owner quote: merged legs, conserved");
    CHECK(qpay(m.id, 1000000, NULL) == ROY_OK && roy_check(&R, &L), "8-owner payment");
    x.version = 3;
    for (uint32_t i = 0; i < 8; i++) x.owner[i].id = 200 + i;
    CHECK(upd(&x) == ROY_OK, "all owners replaced");
    ok = true;
    for (uint32_t i = 0; i < 8; i++) ok = ok && w->split[i].invest_bps == 0 && w->split[i].seq == 0;
    CHECK(ok && roy_check(&R, &L), "replaced owners start with no opt-in");
}

static void test_split(void)
{
    setup();
    roy_manifest_t m, c;
    man(&m, 1);
    own(&m, 1, 2002, oacct[1], 1);
    CHECK(reg(&m) == ROY_OK, "registered");
    uint32_t o0 = m.owner[0].id;
    CHECK(opt(m.id, o0, gacct[0], 500, iacct[0], 1500, 1) == ROY_OK, "opt in 5% / 15%");
    const roy_work_t *w = roy_work(&R, m.id);
    CHECK(w->split[0].giving_acct == gacct[0] && w->split[0].giving_bps == 500 &&
              w->split[0].invest_acct == iacct[0] && w->split[0].invest_bps == 1500 &&
              w->split[0].seq == 1,
          "split stored");
    CHECK(opt(m.id, o0, gacct[0], 500, iacct[0], 1500, 1) == ROY_ERR_VERSION, "seq replay refused");
    CHECK(opt(m.id, o0, 0, 0, 0, 0, 0) == ROY_ERR_VERSION, "lower seq refused");
    CHECK(opt(m.id, o0, gacct[0], 5000, iacct[0], 5000, 2) == ROY_OK, "10000 bps total allowed");
    CHECK(opt(m.id, o0, gacct[0], 5001, iacct[0], 5000, 3) == ROY_ERR_ARG, "10001 bps refused");
    CHECK(opt(m.id, o0, gacct[0], 0, 0, 0, 3) == ROY_ERR_ARG, "bps 0 with an account refused");
    CHECK(opt(m.id, o0, 0, 0, iacct[0], 0, 3) == ROY_ERR_ARG, "invest bps 0 with account refused");
    CHECK(opt(m.id, o0, fee, 10, 0, 0, 3) == ROY_ERR_ARG, "giving to the fee account refused");
    CHECK(opt(m.id, o0, 0, 0, eur_acct, 10, 3) == ROY_ERR_ARG, "other asset refused");
    CHECK(opt(m.id, o0, 0, 0, nat_acct, 10, 3) == ROY_ERR_ARG, "other form refused");
    CHECK(opt(m.id, o0, L.n_accounts, 10, 0, 0, 3) == ROY_ERR_ARG, "no such account refused");
    CHECK(opt(m.id, 4242, 0, 0, 0, 0, 3) == ROY_ERR_UNKNOWN, "not an owner refused");
    man(&c, 9);
    CHECK(opt(c.id, o0, 0, 0, 0, 0, 3) == ROY_ERR_UNKNOWN, "unknown work refused");
    roy_split_t s = {0, 0, 0, 0, 3};
    uint8_t msg[ROY_SPLIT_MSG], sg[32];
    roy_split_msg(m.id, 1, o0, &s, msg);
    sign(2002, msg, ROY_SPLIT_MSG, sg);
    CHECK(roy_split_set(&R, &L, m.id, o0, &s, sg, 32) == ROY_ERR_SIG,
          "another owner may not set my split");
    sign(o0, msg, ROY_SPLIT_MSG, sg);
    CHECK(roy_split_set(&R, &L, m.id, o0, &s, sg, 31) == ROY_ERR_SIG, "short sig refused");
    CHECK(roy_split_set(&R, &L, m.id, o0, &s, NULL, 32) == ROY_ERR_ARG &&
              roy_split_set(&R, &L, m.id, o0, NULL, sg, 32) == ROY_ERR_ARG &&
              roy_split_set(&R, &L, NULL, o0, &s, sg, 32) == ROY_ERR_ARG &&
              roy_split_set(&R, NULL, m.id, o0, &s, sg, 32) == ROY_ERR_ARG &&
              roy_split_set(NULL, &L, m.id, o0, &s, sg, 32) == ROY_ERR_ARG,
          "NULL refused");
    CHECK(roy_split_set(&R, &L, m.id, o0, &s, sg, 32) == ROY_OK && w->split[0].giving_bps == 0,
          "opt back out (seq 3)");
    /* an opt-in signed for version 1 cannot be replayed after an update */
    roy_split_t s4 = {gacct[1], 0, 100, 0, 4};
    roy_split_msg(m.id, 1, o0, &s4, msg);
    sign(o0, msg, ROY_SPLIT_MSG, sg);
    roy_manifest_t x = m;
    x.version = 2;
    CHECK(upd(&x) == ROY_OK, "update to version 2");
    CHECK(roy_split_set(&R, &L, m.id, o0, &s4, sg, 32) == ROY_ERR_SIG,
          "opt-in signed for the old version refused");
    CHECK(opt(m.id, o0, gacct[1], 100, 0, 0, 4) == ROY_OK, "re-signed for version 2: accepted");
    /* lines */
    setup();
    man(&m, 1);
    for (uint32_t i = 1; i < 5; i++) own(&m, i, 2000 + i, oacct[i], 1);
    reg(&m);
    CHECK(roy_lines(&R, m.id) == 7, "5 owners: 7 lines");
    CHECK(opt(m.id, 2001, gacct[0], 10, 0, 0, 1) == ROY_OK && roy_lines(&R, m.id) == 8,
          "a giving account takes the 8th line");
    CHECK(opt(m.id, 2002, gacct[0], 10, 0, 0, 1) == ROY_OK && roy_lines(&R, m.id) == 8,
          "a shared giving account adds no line");
    CHECK(opt(m.id, 2003, oacct[1], 10, oacct[0], 20, 1) == ROY_OK && roy_lines(&R, m.id) == 8,
          "routing to owner accounts adds no line");
    roy_split_t keep = roy_work(&R, m.id)->split[3];
    CHECK(opt(m.id, 2003, iacct[3], 10, 0, 0, 2) == ROY_ERR_LINES &&
              !memcmp(&keep, &roy_work(&R, m.id)->split[3], sizeof keep),
          "a 9th line refused, opt-in unchanged");
    CHECK(opt(m.id, 2004, 0, 0, iacct[3], 10, 1) == ROY_ERR_LINES &&
              roy_work(&R, m.id)->split[4].seq == 0,
          "investing account as 9th line refused");
    /* an ancestor's opt-in that would break a descendant */
    man(&c, 2);
    child_of(&c, &m, 1, 2);
    c.owner[0].account = oacct[1];
    CHECK(reg(&c) == ROY_OK && roy_lines(&R, c.id) == 8, "child on shared accounts");
    CHECK(opt(c.id, c.owner[0].id, iacct[2], 10, 0, 0, 1) == ROY_ERR_LINES,
          "child opt-in over 8 refused");
    CHECK(opt(m.id, m.owner[0].id, gacct[0], 10, 0, 0, 1) == ROY_OK, "parent shared account ok");
    CHECK(roy_check(&R, &L), "audit clean");
}

/* the default single-owner work: everything to oacct[0] */
static void test_pay_basic(void)
{
    setup();
    roy_manifest_t m;
    man(&m, 1);
    own(&m, 1, 2002, oacct[1], 2);
    CHECK(reg(&m) == ROY_OK, "work");
    roy_quote_t q, out;
    CHECK(roy_quote(&R, m.id, 1000000, &q) == ROY_OK, "quote");
    /* R = 180000, fee = floor(180000 * 8889 / 1e7) = 160, x = 179840, 1:2 */
    CHECK(q.gross == 1000000 && q.royalty == 180000 && q.fee == 160, "royalty and fee");
    CHECK(q.n_levels == 1 && q.level[0].received == 179840 && q.level[0].upstream == 0 &&
              q.level[0].kept == 179840,
          "one level keeps all");
    CHECK(q.n_payouts == 2 && q.payout[0].gross == 59947 && q.payout[1].gross == 119893 &&
              q.payout[0].net == 59947 && q.payout[1].giving == 0,
          "1:2 largest remainder (179840 = 59946.67 + 119893.33)");
    CHECK(q.owners_total == 179840 && q.upstream_total == 0 && q.giving_total == 0 &&
              q.investing_total == 0,
          "totals");
    CHECK(q.n_legs == 3 && q.leg[2].account == fee && q.leg[2].d_debit == 160 &&
              q.leg[0].account == oacct[0] && q.leg[1].d_debit == 119893 && q.leg[1].d_credit == 0,
          "legs: owners, then the fee");
    CHECK(q.payout[1].owner_id == 2002 && q.payout[1].level == 0 && q.payout[1].account == oacct[1],
          "payout identities");
    roy_quote_t q2;
    roy_quote(&R, m.id, 1000000, &q2);
    CHECK(!memcmp(q.digest, q2.digest, 32), "quote is deterministic");
    roy_quote(&R, m.id, 1000001, &q2);
    CHECK(memcmp(q.digest, q2.digest, 32), "another gross: another digest");
    CHECK(roy_quote(&R, m.id, 0, &q2) == ROY_ERR_ARG &&
              roy_quote(&R, m.id, PAY_BAL_MAX, &q2) == ROY_ERR_ARG,
          "gross 0 / too large refused");
    CHECK(roy_quote(NULL, m.id, 1, &q2) == ROY_ERR_ARG &&
              roy_quote(&R, NULL, 1, &q2) == ROY_ERR_ARG &&
              roy_quote(&R, m.id, 1, NULL) == ROY_ERR_ARG,
          "NULL refused");
    uint8_t ghost[32] = {1};
    CHECK(roy_quote(&R, ghost, 1, &q2) == ROY_ERR_UNKNOWN, "unknown work");

    uint8_t rc1[32];
    sale(1000000, rc1);
    uint64_t p0 = deb(payer), f0 = deb(fee), a0 = deb(oacct[0]), a1 = deb(oacct[1]);
    uint64_t seq0 = L.seq;
    memset(&out, 0, sizeof out);
    CHECK(roy_pay(&R, &L, mk(), payer, m.id, 1000000, rc1, q.digest, &out) == ROY_OK, "paid");
    CHECK(L.seq == seq0 + 1, "ONE ledger posting");
    CHECK(deb(payer) == p0 - 180000 && deb(fee) == f0 + 160 && deb(oacct[0]) == a0 + 59947 &&
              deb(oacct[1]) == a1 + 119893,
          "balances moved exactly");
    CHECK(!memcmp(&out, &q, sizeof q), "out is the quote that was shown");
    CHECK(R.n_receipts == 2 && roy_receipt_used(&R, rc1), "receipt and payout hash consumed");
    uint8_t zid[32] = {0};
    CHECK(!roy_receipt_used(&R, zid) && !roy_receipt_used(NULL, rc1) && !roy_receipt_used(&R, NULL),
          "an unused id, NULL registry or id: not used");
    const pay_journal_t *j = &L.journal[(L.seq - 1) % PAY_JOURNAL_MAX];
    CHECK(roy_receipt_used(&R, j->hash) && j->n_lines == 4 && j->kind == PAY_KIND_TRANSFER,
          "the royalty posting's own hash is consumed; 4 lines; transfer");
    const roy_totals_t *t = &roy_work(&R, m.id)->tot;
    CHECK(t->payments == 1 && t->gross == 1000000 && t->royalty == 180000 && t->fee == 160 &&
              t->owners == 179840 && t->up_in == 0 && t->up_out == 0,
          "lifetime totals");
    CHECK(roy_check(&R, &L), "audit clean after a payment");

    /* replay and look-alikes */
    uint64_t pb = deb(payer);
    CHECK(roy_pay(&R, &L, mk(), payer, m.id, 1000000, rc1, q.digest, NULL) == ROY_ERR_DUP,
          "the same receipt twice refused");
    CHECK(roy_pay(&R, &L, mk(), payer, m.id, 1000000, j->hash, q.digest, NULL) == ROY_ERR_DUP,
          "a royalty payout presented as a sale refused");
    CHECK(deb(payer) == pb && R.n_receipts == 2, "nothing moved");

    /* stale quote */
    uint8_t rc2[32];
    sale(1000000, rc2);
    uint8_t bad[32];
    memcpy(bad, q.digest, 32);
    bad[5] ^= 1;
    CHECK(roy_pay(&R, &L, mk(), payer, m.id, 1000000, rc2, bad, NULL) == ROY_ERR_STALE_QUOTE,
          "wrong digest refused");
    CHECK(roy_pay(&R, &L, mk(), payer, m.id, 999999, rc2, q.digest, NULL) == ROY_ERR_STALE_QUOTE,
          "digest of another gross refused");
    CHECK(opt(m.id, 2002, gacct[0], 250, iacct[0], 750, 1) == ROY_OK, "owner 2 opts in");
    CHECK(roy_pay(&R, &L, mk(), payer, m.id, 1000000, rc2, q.digest, NULL) == ROY_ERR_STALE_QUOTE,
          "split changed after the quote: refused");
    CHECK(roy_quote(&R, m.id, 1000000, &q) == ROY_OK, "re-quote");
    CHECK(q.payout[1].gross == 119893 && q.payout[1].giving == 2997 &&
              q.payout[1].investing == 8991 && q.payout[1].net == 119893 - 2997 - 8991 &&
              q.payout[1].giving_acct == gacct[0] && q.payout[1].invest_acct == iacct[0],
          "giving 2.5%, investing 7.5% of the owner's own payout");
    CHECK(q.giving_total == 2997 && q.investing_total == 8991 &&
              q.owners_total + q.giving_total + q.investing_total + q.fee == q.royalty &&
              q.n_legs == 5,
          "conserved; 5 legs");
    roy_quote_t qs = q;
    roy_manifest_t x = m;
    x.version = 2;
    CHECK(upd(&x) == ROY_OK, "version bump with nothing else changed");
    CHECK(roy_pay(&R, &L, mk(), payer, m.id, 1000000, rc2, qs.digest, NULL) == ROY_ERR_STALE_QUOTE,
          "version changed: refused");
    roy_quote(&R, m.id, 1000000, &q);
    CHECK(!memcmp(q.payout, qs.payout, sizeof q.payout) && memcmp(q.digest, qs.digest, 32),
          "same amounts, different digest (version in it)");
    x.version = 3;
    x.owner[1].weight = 3;
    upd(&x);
    CHECK(roy_pay(&R, &L, mk(), payer, m.id, 1000000, rc2, q.digest, NULL) == ROY_ERR_STALE_QUOTE,
          "weights changed: refused");
    roy_quote(&R, m.id, 1000000, &q);
    CHECK(roy_pay(&R, &L, mk(), payer, m.id, 1000000, rc2, q.digest, NULL) == ROY_OK,
          "fresh quote pays");
    CHECK(roy_check(&R, &L), "audit clean");

    /* the payer */
    uint8_t rc3[32];
    sale(5000, rc3);
    roy_quote(&R, m.id, 5000, &q);
    CHECK(roy_pay(&R, &L, mk(), oacct[0], m.id, 5000, rc3, q.digest, NULL) == ROY_ERR_ARG,
          "an owner account may not be the payer");
    CHECK(roy_pay(&R, &L, mk(), gacct[0], m.id, 5000, rc3, q.digest, NULL) == ROY_ERR_ARG,
          "a giving account may not be the payer");
    CHECK(roy_pay(&R, &L, mk(), fee, m.id, 5000, rc3, q.digest, NULL) == ROY_ERR_ARG,
          "the fee account may not be the payer");
    CHECK(roy_pay(&R, &L, mk(), eur_acct, m.id, 5000, rc3, q.digest, NULL) == ROY_ERR_ARG,
          "payer in another asset refused");
    CHECK(roy_pay(&R, &L, mk(), nat_acct, m.id, 5000, rc3, q.digest, NULL) == ROY_ERR_ARG,
          "payer in another form refused");
    CHECK(roy_pay(&R, &L, mk(), L.n_accounts, m.id, 5000, rc3, q.digest, NULL) == ROY_ERR_ARG,
          "no such payer refused");
    CHECK(roy_pay(&R, &L, mk(), payer2, m.id, 5000, rc3, q.digest, NULL) == ROY_ERR_UNREALISED,
          "the sale went to another account: unrealised for payer2");
    CHECK(roy_pay(&R, &L, mk(), payer, m.id, 5000, rc3, q.digest, NULL) == ROY_OK, "payer pays");
    /* NULLs and unknowns */
    uint8_t rc4[32];
    sale(5000, rc4);
    CHECK(roy_pay(NULL, &L, mk(), payer, m.id, 5000, rc4, q.digest, NULL) == ROY_ERR_ARG &&
              roy_pay(&R, NULL, mk(), payer, m.id, 5000, rc4, q.digest, NULL) == ROY_ERR_ARG &&
              roy_pay(&R, &L, NULL, payer, m.id, 5000, rc4, q.digest, NULL) == ROY_ERR_ARG &&
              roy_pay(&R, &L, mk(), payer, NULL, 5000, rc4, q.digest, NULL) == ROY_ERR_ARG &&
              roy_pay(&R, &L, mk(), payer, m.id, 5000, NULL, q.digest, NULL) == ROY_ERR_ARG &&
              roy_pay(&R, &L, mk(), payer, m.id, 5000, rc4, NULL, NULL) == ROY_ERR_ARG,
          "NULL refused");
    CHECK(roy_pay(&R, &L, mk(), payer, ghost, 5000, rc4, q.digest, NULL) == ROY_ERR_UNKNOWN,
          "unknown work refused");
    CHECK(roy_pay(&R, &L, mk(), payer, m.id, 0, rc4, q.digest, NULL) == ROY_ERR_ARG,
          "gross 0 refused");
    CHECK(roy_check(&R, &L), "audit clean");
}

static void test_realised(void)
{
    setup();
    roy_manifest_t m;
    man(&m, 1);
    reg(&m);
    roy_quote_t q;
    uint8_t rc[32], fake[32];
    memset(fake, 0xAB, 32);
    roy_quote(&R, m.id, 1000, &q);
    sale(1000, rc);
    CHECK(roy_pay(&R, &L, mk(), payer, m.id, 1000, fake, q.digest, NULL) == ROY_ERR_UNREALISED,
          "a receipt that is no ledger posting refused");
    roy_quote_t q2;
    roy_quote(&R, m.id, 1001, &q2);
    CHECK(roy_pay(&R, &L, mk(), payer, m.id, 1001, rc, q2.digest, NULL) == ROY_ERR_UNREALISED,
          "a gross above what the payer received refused");
    roy_quote(&R, m.id, 999, &q2);
    uint64_t p0 = deb(payer);
    CHECK(roy_pay(&R, &L, mk(), payer, m.id, 999, rc, q2.digest, NULL) == ROY_OK &&
              deb(payer) == p0 - 179,
          "a gross below what was received is accepted (declared gross governs)");
    CHECK(roy_pay(&R, &L, mk(), payer, m.id, 1000, rc, q.digest, NULL) == ROY_ERR_DUP,
          "then the receipt is spent");
    /* a posting where the payer PAID (not received) */
    pay_receipt_t prc;
    pay_ledger_transfer(&L, mk(), payer, buyer, 100, &prc);
    roy_quote(&R, m.id, 100, &q);
    CHECK(roy_pay(&R, &L, mk(), payer, m.id, 100, prc.hash, q.digest, NULL) == ROY_ERR_UNREALISED,
          "a posting where the payer paid out is no sale");
    /* reversed sale */
    pay_posting_req_t *sr = mk();
    pay_ledger_transfer(&L, sr, buyer, payer, 2000, &prc);
    char uetr[PAY_UETR_LEN + 1];
    memcpy(uetr, sr->uetr, sizeof uetr);
    pay_receipt_t rrc;
    CHECK(pay_ledger_reverse(&L, mk(), uetr, &rrc) == PAY_OK, "sale reversed on the ledger");
    roy_quote(&R, m.id, 2000, &q);
    CHECK(roy_pay(&R, &L, mk(), payer, m.id, 2000, prc.hash, q.digest, NULL) == ROY_ERR_UNREALISED,
          "a reversed sale is not realised");
    /* a sale with two lines to the payer (net gain counts) */
    pay_posting_req_t *two = mk();
    two->lines[0] = (pay_line_t){buyer, -3000, 0};
    two->lines[1] = (pay_line_t){payer, 1000, 0};
    two->lines[2] = (pay_line_t){payer, 2000, 0};
    two->n_lines = 3;
    CHECK(pay_ledger_post(&L, two, &prc) == PAY_OK, "split-line sale posted");
    roy_quote(&R, m.id, 3000, &q);
    CHECK(roy_pay(&R, &L, mk(), payer, m.id, 3000, prc.hash, q.digest, NULL) == ROY_OK,
          "net gain over several lines counts");
    pay_posting_req_t *mix = mk();
    mix->lines[0] = (pay_line_t){buyer, -3000, 0};
    mix->lines[1] = (pay_line_t){payer, 4000, 0};
    mix->lines[2] = (pay_line_t){payer, -1000, 0};
    mix->lines[3] = (pay_line_t){payer2, 0, 0};
    mix->n_lines = 3;
    CHECK(pay_ledger_post(&L, mix, &prc) == PAY_OK, "mixed-line sale posted");
    roy_quote(&R, m.id, 3001, &q);
    CHECK(roy_pay(&R, &L, mk(), payer, m.id, 3001, prc.hash, q.digest, NULL) == ROY_ERR_UNREALISED,
          "gain is the NET of the payer's lines (3000 < 3001)");
    roy_quote(&R, m.id, 3000, &q);
    CHECK(roy_pay(&R, &L, mk(), payer, m.id, 3000, prc.hash, q.digest, NULL) == ROY_OK,
          "net gain exactly the gross");
    /* zero net gain */
    pay_posting_req_t *z = mk();
    z->lines[0] = (pay_line_t){payer, 500, 0};
    z->lines[1] = (pay_line_t){payer, -500, 0};
    z->n_lines = 2;
    CHECK(pay_ledger_post(&L, z, &prc) == PAY_OK, "zero-gain posting");
    roy_quote(&R, m.id, 1, &q);
    CHECK(roy_pay(&R, &L, mk(), payer, m.id, 1, prc.hash, q.digest, NULL) == ROY_ERR_UNREALISED,
          "zero net gain is no sale");
    /* the very first posting (journal slot 0) is a valid receipt */
    CHECK(L.journal[0].used && L.journal[0].n_lines == 2, "slot 0 holds the issuance");
    roy_quote(&R, m.id, 1000, &q);
    CHECK(roy_pay(&R, &L, mk(), buyer, m.id, 1000, L.journal[0].hash, q.digest, NULL) == ROY_OK,
          "the buyer's issuance in slot 0 is a realised receipt for the buyer");
    /* stale fields in a reused request are overwritten */
    uint8_t rg[32];
    sale(1000, rg);
    pay_posting_req_t *gr = mk();
    for (uint32_t i = 0; i < PAY_MAX_LINES; i++) gr->lines[i] = (pay_line_t){payer2, 9, 9};
    gr->n_lines = 8;
    gr->kind = PAY_KIND_FEE;
    uint64_t pp = deb(payer);
    CHECK(roy_pay(&R, &L, gr, payer, m.id, 1000, rg, q.digest, NULL) == ROY_OK &&
              deb(payer) == pp - 180 && gr->n_lines == 2 && gr->kind == PAY_KIND_TRANSFER &&
              gr->lines[0].d_credit == 0,
          "garbage lines in the request are replaced");
    /* royalty exactly 1 */
    roy_quote(&R, m.id, 6, &q);
    uint32_t n1 = R.n_receipts;
    CHECK(q.royalty == 1 && qpay(m.id, 6, NULL) == ROY_OK && R.n_receipts == n1 + 2,
          "royalty 1 posts and consumes two ids");
    /* FLAT terms need gross >= flat */
    roy_manifest_t f;
    man(&f, 2);
    f.terms = (roy_terms_t){ROY_TERMS_FLAT, 0, 0, 250};
    CHECK(reg(&f) == ROY_OK, "flat work");
    CHECK(roy_quote(&R, f.id, 249, &q) == ROY_ERR_ARG, "gross below flat: no quote");
    CHECK(roy_quote(&R, f.id, 250, &q) == ROY_OK && q.royalty == 250 && q.fee == 0, "flat 250");
    CHECK(qpay(f.id, 300, &q) == ROY_OK && q.royalty == 250, "flat paid on a 300 sale");
    /* royalty 0: receipt consumed, nothing posted */
    uint8_t rz[32];
    roy_quote(&R, m.id, 5, &q);
    sale(5, rz);
    uint64_t s0 = L.seq, n0 = R.n_receipts, g0 = roy_work(&R, m.id)->tot.gross;
    roy_quote_t out;
    CHECK(q.royalty == 0 && q.n_legs == 0 &&
              roy_pay(&R, &L, mk(), payer, m.id, 5, rz, q.digest, &out) == ROY_ZERO,
          "royalty 0 returns ROY_ZERO");
    CHECK(L.seq == s0 && R.n_receipts == n0 + 1 && roy_receipt_used(&R, rz) &&
              roy_work(&R, m.id)->tot.gross == g0 + 5 && out.royalty == 0,
          "nothing posted, receipt consumed, gross counted");
    CHECK(roy_pay(&R, &L, mk(), payer, m.id, 5, rz, q.digest, NULL) == ROY_ERR_DUP, "and spent");
    CHECK(roy_check(&R, &L), "audit clean");
}

static void test_refusals(void)
{
    setup();
    roy_manifest_t m;
    man(&m, 1);
    reg(&m);
    roy_quote_t q;
    uint8_t rc[32];
    roy_quote(&R, m.id, 10000, &q);
    sale(10000, rc);
    /* ledger refusal: a frozen payer */
    pay_ledger_set_flags(&L, payer, PAY_ACCT_FROZEN);
    roy_work_t before = R.work[0];
    uint32_t nr = R.n_receipts;
    uint64_t s0 = L.seq;
    CHECK(roy_pay(&R, &L, mk(), payer, m.id, 10000, rc, q.digest, NULL) == ROY_ERR_LEDGER &&
              R.last_ledger == PAY_ERR_POLICY,
          "ledger refusal reported");
    CHECK(L.seq == s0 && R.n_receipts == nr && !memcmp(&before, &R.work[0], sizeof before) &&
              !roy_receipt_used(&R, rc),
          "a refused posting changes nothing");
    pay_ledger_set_flags(&L, payer, 0);
    /* the request is replayed with the same idempotency key: PAY_DUPLICATE is not paid */
    pay_posting_req_t keep = *mk();
    pay_posting_req_t r1 = keep;
    CHECK(roy_pay(&R, &L, &r1, payer, m.id, 10000, rc, q.digest, NULL) == ROY_OK, "pays");
    uint8_t rc2[32];
    sale(10000, rc2);
    pay_posting_req_t r2 = keep;
    CHECK(roy_pay(&R, &L, &r2, payer, m.id, 10000, rc2, q.digest, NULL) == ROY_ERR_LEDGER &&
              !roy_receipt_used(&R, rc2),
          "an idempotent ledger duplicate does not count as a payment");
    /* counter overflow */
    R.work[0].tot.gross = UINT64_MAX - 5;
    R.work[0].tot.royalty = 0;
    before = R.work[0];
    nr = R.n_receipts;
    CHECK(roy_pay(&R, &L, mk(), payer, m.id, 10000, rc2, q.digest, NULL) == ROY_ERR_OVERFLOW &&
              !memcmp(&before, &R.work[0], sizeof before) && R.n_receipts == nr,
          "gross counter overflow refused, nothing changed");
    const uint32_t fld[] = {0, 2, 3, 6, 7, 8};
    for (unsigned k = 0; k < sizeof fld / sizeof fld[0]; k++) {
        setup();
        man(&m, 1);
        reg(&m);
        opt(m.id, m.owner[0].id, gacct[0], 1000, iacct[0], 1000, 1);
        roy_quote(&R, m.id, 10000, &q);
        sale(10000, rc);
        uint64_t *f = &R.work[0].tot.payments + fld[k];
        *f = UINT64_MAX;
        char msg[64];
        snprintf(msg, sizeof msg, "counter %u overflow refused", fld[k]);
        CHECK(roy_pay(&R, &L, mk(), payer, m.id, 10000, rc, q.digest, NULL) == ROY_ERR_OVERFLOW &&
                  R.n_receipts == 0,
              msg);
    }
    /* chain counters */
    setup();
    roy_manifest_t p, c;
    man(&p, 1);
    reg(&p);
    man(&c, 2);
    child_of(&c, &p, 1, 2);
    reg(&c);
    roy_quote(&R, c.id, 10000, &q);
    sale(10000, rc);
    R.work[0].tot.up_in = UINT64_MAX;
    CHECK(roy_pay(&R, &L, mk(), payer, c.id, 10000, rc, q.digest, NULL) == ROY_ERR_OVERFLOW,
          "parent up_in overflow refused");
    R.work[0].tot.up_in = 0;
    R.work[1].tot.up_out = UINT64_MAX;
    CHECK(roy_pay(&R, &L, mk(), payer, c.id, 10000, rc, q.digest, NULL) == ROY_ERR_OVERFLOW,
          "child up_out overflow refused");
    R.work[1].tot.up_out = 0;
    R.work[0].tot.owners = UINT64_MAX;
    CHECK(roy_pay(&R, &L, mk(), payer, c.id, 10000, rc, q.digest, NULL) == ROY_ERR_OVERFLOW,
          "parent owners overflow refused");
    R.work[0].tot.owners = 0;
    CHECK(roy_pay(&R, &L, mk(), payer, c.id, 10000, rc, q.digest, NULL) == ROY_OK, "then pays");
    /* receipts full */
    setup();
    man(&m, 1);
    reg(&m);
    roy_quote(&R, m.id, 10000, &q);
    sale(10000, rc);
    R.n_receipts = ROY_MAX_RECEIPTS - 1;
    CHECK(roy_pay(&R, &L, mk(), payer, m.id, 10000, rc, q.digest, NULL) == ROY_ERR_FULL,
          "no room for receipt + payout hash refused");
    R.n_receipts = ROY_MAX_RECEIPTS - 2;
    CHECK(roy_pay(&R, &L, mk(), payer, m.id, 10000, rc, q.digest, NULL) == ROY_OK &&
              R.n_receipts == ROY_MAX_RECEIPTS && roy_check(&R, &L),
          "exactly two slots left: pays; a full table audits clean");
}

/* a three-level chain with hand-computed numbers */
static void test_chain(void)
{
    setup();
    roy_manifest_t a, b, c;
    man(&a, 1); /* root: owners 1:1 on oacct[0], oacct[1] */
    own(&a, 1, 1100, oacct[1], 1);
    man(&b, 2);                    /* middle: 1/3 upstream to a */
    b.owner[0].account = oacct[0]; /* shares the root owner's account */
    child_of(&b, &a, 1, 3);
    man(&c, 3); /* leaf: 1/4 upstream to b, 10% royalty */
    c.owner[0].account = oacct[3];
    own(&c, 1, 1300, oacct[4], 2);
    c.terms = (roy_terms_t){ROY_TERMS_PERCENT, 1, 10, 0};
    child_of(&c, &b, 1, 4);
    CHECK(reg(&a) == ROY_OK && reg(&b) == ROY_OK && reg(&c) == ROY_OK, "chain registered");
    CHECK(roy_lines(&R, c.id) == 6, "6 lines (b shares oacct[0])");
    CHECK(opt(a.id, 1100, gacct[0], 3333, iacct[0], 1, 1) == ROY_OK, "root owner 2 opts in");
    roy_quote_t q;
    /* gross 12345678: R = 1234567, fee = floor(1234567*8889/1e7) = 1097, x0 = 1233470
     * u1 = floor(1233470/4) = 308367, k0 = 925103 -> c owners 1:2: 308368 (rem) / 616735
     *   925103/3 = 308367.67: q 308367 r 2 / 616735 r 1 -> leftover 1 to owner 0
     * x1 = 308367: u2 = floor(308367/3) = 102789, k1 = 205578 -> b owner
     * x2 = 102789: root, 1:1 -> 51395 / 51394; owner 2 giving floor(51394*3333/1e4) = 17129,
     *   investing floor(51394/1e4) = 5, net 34260 */
    CHECK(roy_quote(&R, c.id, 12345678, &q) == ROY_OK, "quote");
    CHECK(q.royalty == 1234567 && q.fee == 1097 && q.n_levels == 3, "royalty, fee, levels");
    CHECK(q.level[0].received == 1233470 && q.level[0].upstream == 308367 &&
              q.level[0].kept == 925103,
          "leaf keeps its share minus floor(x/4)");
    CHECK(q.level[1].received == 308367 && q.level[1].upstream == 102789 &&
              q.level[1].kept == 205578,
          "middle takes 1/4 of what reached the leaf, passes floor(1/3) on");
    CHECK(q.level[2].received == 102789 && q.level[2].upstream == 0 && q.level[2].kept == 102789,
          "root keeps what reaches it");
    CHECK(!memcmp(q.level[2].id, a.id, 32) && q.level[2].version == 1 && q.level[1].work == 1,
          "level identities");
    CHECK(q.n_payouts == 5 && q.payout[0].gross == 308368 && q.payout[1].gross == 616735 &&
              q.payout[2].gross == 205578 && q.payout[3].gross == 51395 &&
              q.payout[4].gross == 51394,
          "owner grosses by largest remainder per level");
    CHECK(q.payout[4].giving == 17129 && q.payout[4].investing == 5 && q.payout[4].net == 34260 &&
              q.payout[4].level == 2 && q.payout[2].level == 1,
          "root owner's opt-in split");
    CHECK(q.upstream_total == 308367 + 102789, "upstream total");
    CHECK(q.fee + q.owners_total + q.giving_total + q.investing_total == q.royalty,
          "royalty conserved exactly");
    uint64_t before[12];
    for (int i = 0; i < 5; i++) before[i] = deb(oacct[i]);
    uint64_t g0 = deb(gacct[0]), i0 = deb(iacct[0]), f0 = deb(fee);
    CHECK(qpay(c.id, 12345678, NULL) == ROY_OK, "chain paid in one posting");
    CHECK(deb(oacct[3]) == before[3] + 308368 && deb(oacct[4]) == before[4] + 616735 &&
              deb(oacct[0]) == before[0] + 205578 + 51395 && deb(oacct[1]) == before[1] + 34260 &&
              deb(gacct[0]) == g0 + 17129 && deb(iacct[0]) == i0 + 5 && deb(fee) == f0 + 1097,
          "every account got exactly its quote");
    const roy_totals_t *ta = &roy_work(&R, a.id)->tot, *tb = &roy_work(&R, b.id)->tot,
                       *tc = &roy_work(&R, c.id)->tot;
    CHECK(tc->payments == 1 && tc->royalty == 1234567 && tc->fee == 1097 && tc->up_out == 308367 &&
              tc->owners == 925103 && tc->up_in == 0,
          "leaf totals");
    CHECK(tb->payments == 0 && tb->gross == 0 && tb->up_in == 308367 && tb->up_out == 102789 &&
              tb->owners == 205578,
          "middle totals");
    CHECK(ta->up_in == 102789 && ta->up_out == 0 && ta->owners == 51395 + 34260 &&
              ta->giving == 17129 && ta->investing == 5,
          "root totals");
    CHECK(roy_check(&R, &L), "audit clean");
    /* a change at the root makes a quote on the leaf stale */
    roy_quote(&R, c.id, 1000, &q);
    CHECK(opt(a.id, 1100, gacct[0], 3333, iacct[0], 1, 2) == ROY_OK, "root re-signs same values");
    uint8_t rc[32];
    sale(1000, rc);
    CHECK(roy_pay(&R, &L, mk(), payer, c.id, 1000, rc, q.digest, NULL) == ROY_ERR_STALE_QUOTE,
          "any change up the chain (even a seq) stales the leaf's quote");
    roy_manifest_t a2 = a;
    a2.version = 2;
    roy_quote(&R, c.id, 1000, &q);
    upd(&a2);
    CHECK(roy_pay(&R, &L, mk(), payer, c.id, 1000, rc, q.digest, NULL) == ROY_ERR_STALE_QUOTE,
          "root version change stales the leaf's quote");
    /* paying the middle work directly */
    CHECK(qpay(b.id, 1000000, &q) == ROY_OK && q.n_levels == 2, "middle paid directly");
    CHECK(roy_check(&R, &L), "audit clean");
}

/* ---- R13 audit catches corruption ---- */
static void test_check(void)
{
    setup();
    roy_manifest_t a, b;
    man(&a, 1);
    reg(&a);
    man(&b, 2);
    child_of(&b, &a, 1, 2);
    reg(&b);
    qpay(b.id, 100000, NULL);
    CHECK(roy_check(&R, &L), "clean");
    CHECK(!roy_check(NULL, &L) && !roy_check(&R, NULL), "NULL fails");
    roy_t s = R;
#define BREAK(stmt, msg)                                                                           \
    do {                                                                                           \
        stmt;                                                                                      \
        CHECK(!roy_check(&R, &L), msg);                                                            \
        R = s;                                                                                     \
        CHECK(roy_check(&R, &L), "restored");                                                      \
    } while (0)
    BREAK(R.work[1].tot.fee++, "fee counter off by one");
    BREAK(R.work[1].tot.owners--, "owners counter off by one");
    BREAK(R.work[1].tot.giving++, "giving counter");
    BREAK(R.work[1].tot.investing++, "investing counter");
    BREAK(R.work[1].tot.royalty++, "royalty counter");
    BREAK(R.work[0].tot.up_in++, "up_in counter");
    BREAK((R.work[1].tot.up_out++, R.work[1].tot.owners--), "up_out without up_in");
    BREAK((R.work[0].tot.up_in++, R.work[0].tot.owners++), "up_in without up_out");
    BREAK((R.work[0].tot.up_out++, R.work[0].tot.owners--), "a root with up_out");
    BREAK((R.work[1].tot.royalty = R.work[1].tot.gross + 1,
           R.work[1].tot.owners += 1 + R.work[1].tot.gross - s.work[1].tot.royalty),
          "royalty above gross");
    BREAK(R.work[1].tot.payments = R.work[1].tot.gross + 1, "more payments than gross units");
    BREAK(R.work[1].tot.fee = UINT64_MAX, "fee + up_out overflow");
    BREAK(R.work[1].tot.owners = UINT64_MAX, "+ owners overflow");
    BREAK(R.work[1].tot.giving = UINT64_MAX, "+ giving overflow");
    BREAK(R.work[1].tot.investing = UINT64_MAX, "+ investing overflow");
    BREAK((R.work[0].tot.royalty = R.work[0].tot.gross = UINT64_MAX), "royalty + up_in overflow");
    BREAK(R.work[0].split[1].seq = 1, "split seq on the first unused slot");
    BREAK(R.work[1].depth = 0, "child depth wrong");
    BREAK(R.work[0].depth = 1, "root depth wrong");
    BREAK(R.work[1].parent = 1, "parent index not before the child");
    BREAK(R.work[1].m.parent[0] ^= 1, "parent id mismatch");
    BREAK(R.work[1].depth = ROY_MAX_DEPTH + 1, "depth beyond the bound");
    BREAK(R.work[1].m.owner[0].weight = 0, "zero weight");
    BREAK((R.work[1].split[0].giving_bps = 1, R.work[1].split[0].giving_acct = L.n_accounts),
          "bps without an account");
    BREAK(R.work[1].split[0].giving_acct = gacct[0], "an account without bps");
    BREAK(R.work[1].split[7].seq = 1, "split on an unused slot");
    BREAK(R.work[1].used = false, "used flag cleared");
    BREAK(R.work[0].parent = 1, "a root with a parent index");
    BREAK(R.work[1].m.owner[3].id = 1, "garbage id in an unused owner slot");
    BREAK(R.work[1].m.owner[3].account = 1, "garbage account in an unused owner slot");
    BREAK(R.work[1].m.owner[3].weight = 1, "garbage weight in an unused owner slot");
    BREAK(R.work[5].used = true, "slot beyond n_works used");
    BREAK(R.n_works = ROY_MAX_WORKS + 1, "n_works beyond max");
    BREAK(R.n_receipts = ROY_MAX_RECEIPTS + 1, "n_receipts beyond max");
    BREAK(R.ready = false, "not ready");
    BREAK(R.fee_acct = L.n_accounts, "fee account missing");
    L.acct[fee].active = false;
    CHECK(!roy_check(&R, &L), "fee account inactive");
    L.acct[fee].active = true;
    CHECK(roy_check(&R, &L), "restored");
    L.acct[oacct[0]].active = false;
    CHECK(!roy_check(&R, &L), "owner account deactivated");
    L.acct[oacct[0]].active = true;
    L.acct[fee].debit++;
    CHECK(!roy_check(&R, &L), "ledger imbalance fails the audit");
    L.acct[fee].debit--;
    R.work[0].split[0] = (roy_split_t){gacct[0], 0, 1, 0, 1};
    R.work[1].m.owner[0].account = oacct[1];
    R.work[1].split[0] = (roy_split_t){gacct[1], iacct[1], 1, 1, 1};
    R.work[0].m.n_owners = 2;
    R.work[0].m.owner[1] = (roy_owner_t){1777, oacct[2], 1};
    R.work[0].split[1] = (roy_split_t){gacct[2], iacct[2], 1, 1, 1};
    CHECK(!roy_check(&R, &L), "a work over 8 lines fails the audit");
    R = s;
    CHECK(roy_check(&R, &L), "restored");
    /* R9 bypassed by corruption: the quote refuses rather than overflow its legs */
    R.work[1].m.n_owners = 3;
    R.work[1].m.owner[1] = (roy_owner_t){1801, oacct[1], 1};
    R.work[1].m.owner[2] = (roy_owner_t){1802, oacct[2], 1};
    R.work[1].split[0] = (roy_split_t){gacct[0], iacct[0], 1, 1, 1};
    R.work[1].split[1] = (roy_split_t){gacct[1], iacct[1], 1, 1, 1};
    roy_quote_t q;
    CHECK(roy_quote(&R, b.id, 100000000, &q) == ROY_ERR_LINES, "8 receivers: 9 lines refused");
    R.work[1].split[1] = (roy_split_t){gacct[1], 0, 1, 0, 1};
    CHECK(roy_quote(&R, b.id, 100000000, &q) == ROY_OK && q.n_legs == 7, "7 receivers fit");
    R = s;
    /* sums of up_out across works that overflow */
    roy_manifest_t c;
    man(&c, 3);
    child_of(&c, &a, 1, 2);
    reg(&c);
    qpay(c.id, 100000, NULL);
    CHECK(roy_check(&R, &L), "second child clean");
    for (uint32_t i = 1; i < 3; i++) {
        R.work[i].tot.gross += 1ull << 63;
        R.work[i].tot.royalty += 1ull << 63;
        R.work[i].tot.up_out += 1ull << 63;
    }
    CHECK(!roy_check(&R, &L), "upstream sums overflow: audit fails");
    for (uint32_t i = 1; i < 3; i++) {
        R.work[i].tot.gross -= 1ull << 63;
        R.work[i].tot.royalty -= 1ull << 63;
        R.work[i].tot.up_out -= 1ull << 63;
    }
    R.work[0].tot.up_in += 1ull << 63;
    R.work[0].tot.owners += 1ull << 63;
    R.work[1].tot.up_in += 1ull << 63; /* not a root: allowed, conserving */
    R.work[1].tot.owners += 1ull << 63;
    CHECK(!roy_check(&R, &L), "up_in sums overflow: audit fails");
    R.work[0].tot.up_in -= 1ull << 63;
    R.work[0].tot.owners -= 1ull << 63;
    R.work[1].tot.up_in -= 1ull << 63;
    R.work[1].tot.owners -= 1ull << 63;
    CHECK(roy_check(&R, &L), "restored");
#undef BREAK
}

/* ---- property loop against an independent reference ---- */
static uint64_t xs = 0x9E3779B97F4A7C15ull;
static uint64_t rnd(void)
{
    xs ^= xs << 13;
    xs ^= xs >> 7;
    xs ^= xs << 17;
    return xs;
}
static uint64_t rn(uint64_t n)
{
    return n ? rnd() % n : 0;
}

/* reference largest remainder: sort indices by remainder desc, index asc */
static void ref_lrm(uint64_t a, const uint64_t *w, uint32_t n, uint64_t *o)
{
    u128 W = 0;
    uint64_t rem[8];
    uint32_t ord[8];
    for (uint32_t i = 0; i < n; i++) W += w[i];
    uint64_t s = 0;
    for (uint32_t i = 0; i < n; i++) {
        o[i] = (uint64_t) (((u128) a * w[i]) / W);
        rem[i] = (uint64_t) (((u128) a * w[i]) % W);
        s += o[i];
        ord[i] = i;
    }
    for (uint32_t i = 1; i < n; i++)
        for (uint32_t j = i; j > 0; j--) {
            uint32_t x = ord[j - 1], y = ord[j];
            if (rem[y] > rem[x] || (rem[y] == rem[x] && y < x)) {
                ord[j - 1] = y;
                ord[j] = x;
            }
        }
    for (uint64_t k = 0; k < a - s; k++) o[ord[k]]++;
}

static void hu(sha256_ctx_t *c, uint64_t v, uint32_t n)
{
    uint8_t b[8];
    for (uint32_t i = 0; i < n; i++) b[i] = (uint8_t) (v >> (8 * i));
    sha256_update(c, b, n);
}

/* R10 digest recomputed from the header's description */
static void ref_digest(const roy_quote_t *q, uint8_t out[32])
{
    sha256_ctx_t c;
    sha256_init(&c);
    sha256_update(&c, (const uint8_t *) "zxv/royalty-quote/v1", 20);
    hu(&c, R.fee_acct, 4);
    hu(&c, q->gross, 8);
    hu(&c, q->royalty, 8);
    hu(&c, q->fee, 8);
    hu(&c, q->n_levels, 4);
    uint32_t p = 0;
    for (uint32_t l = 0; l < q->n_levels; l++) {
        const roy_work_t *w = roy_work(&R, q->level[l].id);
        sha256_update(&c, w->m.id, 32);
        hu(&c, w->m.version, 8);
        hu(&c, w->m.terms.kind, 1);
        hu(&c, w->m.terms.num, 8);
        hu(&c, w->m.terms.den, 8);
        hu(&c, w->m.terms.flat, 8);
        hu(&c, w->m.up_num, 8);
        hu(&c, w->m.up_den, 8);
        hu(&c, q->level[l].received, 8);
        hu(&c, q->level[l].upstream, 8);
        hu(&c, q->level[l].kept, 8);
        hu(&c, w->m.n_owners, 4);
        for (uint32_t j = 0; j < w->m.n_owners; j++, p++) {
            hu(&c, w->m.owner[j].id, 4);
            hu(&c, w->m.owner[j].account, 4);
            hu(&c, w->m.owner[j].weight, 8);
            hu(&c, w->split[j].giving_acct, 4);
            hu(&c, w->split[j].giving_bps, 2);
            hu(&c, w->split[j].invest_acct, 4);
            hu(&c, w->split[j].invest_bps, 2);
            hu(&c, w->split[j].seq, 8);
            hu(&c, q->payout[p].gross, 8);
            hu(&c, q->payout[p].giving, 8);
            hu(&c, q->payout[p].investing, 8);
            hu(&c, q->payout[p].net, 8);
        }
    }
    sha256_final(&c, out);
}

static void test_property(void)
{
    int bad_ref = 0, bad_cons = 0, bad_ledger = 0, bad_audit = 0, paid = 0, zero = 0, digest = 0;
    for (int round = 0; round < 30; round++) {
        setup();
        uint32_t nw = 1 + (uint32_t) rn(10);
        roy_manifest_t ms[10];
        uint32_t made = 0;
        for (uint32_t k = 0; k < nw; k++) {
            roy_manifest_t *m = &ms[made];
            man(m, (uint8_t) (k + 1));
            uint32_t no = 1 + (uint32_t) rn(4);
            for (uint32_t i = 0; i < no; i++)
                own(m, i, 10000 + 100 * k + i, oacct[rn(5)], 1 + rn(rn(2) ? 1000000 : 5));
            if (rn(3) == 0)
                m->terms = (roy_terms_t){ROY_TERMS_FLAT, 0, 0, 1 + rn(5000)};
            else {
                uint64_t d = 1 + rn(rn(2) ? 1000 : 1ull << 40);
                m->terms = (roy_terms_t){ROY_TERMS_PERCENT, 1 + rn(d), d, 0};
            }
            if (made && rn(4)) {
                uint64_t d = 1 + rn(rn(2) ? 10 : 1ull << 50);
                child_of(m, &ms[rn(made)], 1 + rn(d), d);
            }
            if (reg(m) == ROY_OK) made++;
        }
        for (uint32_t k = 0; k < made; k++)
            for (uint32_t i = 0; i < ms[k].n_owners; i++)
                if (rn(2)) {
                    uint16_t gb = (uint16_t) rn(5001), ib = (uint16_t) rn(5001);
                    opt(ms[k].id, ms[k].owner[i].id, gb ? gacct[rn(2)] : 0, gb,
                        ib ? iacct[rn(2)] : 0, ib, 1);
                }
        for (int p = 0; p < 12; p++) {
            const roy_manifest_t *m = &ms[rn(made)];
            uint64_t pick = rn(4), gross = 1 + rn(pick == 0   ? 8
                                                  : pick == 1 ? (1ull << 40)
                                                              : 100000);
            roy_quote_t q, out;
            if (roy_quote(&R, m->id, gross, &q) != ROY_OK) continue; /* flat > gross */
            /* reference */
            int32_t idx = roy_find(&R, m->id);
            const roy_work_t *w = &R.work[idx];
            uint64_t Rr = w->m.terms.kind == ROY_TERMS_FLAT
                              ? w->m.terms.flat
                              : (uint64_t) (((u128) gross * w->m.terms.num) / w->m.terms.den);
            uint64_t ff = (uint64_t) (((u128) Rr * 8889) / 10000000);
            uint64_t x = Rr - ff, sum = ff;
            uint64_t expect[ROY_MAX_ACCOUNTS_TEST];
            memset(expect, 0, sizeof expect);
            expect[fee] += ff;
            bool ok = q.royalty == Rr && q.fee == ff;
            uint32_t pi = 0, lev = 0;
            while (1) {
                uint64_t u =
                    w->m.has_parent ? (uint64_t) (((u128) x * w->m.up_num) / w->m.up_den) : 0;
                uint64_t ws[8], sh[8];
                for (uint32_t j = 0; j < w->m.n_owners; j++) ws[j] = w->m.owner[j].weight;
                ref_lrm(x - u, ws, w->m.n_owners, sh);
                ok = ok && q.level[lev].received == x && q.level[lev].upstream == u;
                for (uint32_t j = 0; j < w->m.n_owners; j++, pi++) {
                    uint64_t g = sh[j] * w->split[j].giving_bps / 10000,
                             iv = sh[j] * w->split[j].invest_bps / 10000; /* sh < 2^50 */
                    if (sh[j] >= (1ull << 50)) {
                        g = (uint64_t) (((u128) sh[j] * w->split[j].giving_bps) / 10000);
                        iv = (uint64_t) (((u128) sh[j] * w->split[j].invest_bps) / 10000);
                    }
                    ok = ok && q.payout[pi].gross == sh[j] && q.payout[pi].giving == g &&
                         q.payout[pi].investing == iv;
                    expect[w->m.owner[j].account] += sh[j] - g - iv;
                    expect[w->split[j].giving_acct] += g;
                    expect[w->split[j].invest_acct] += iv;
                    sum += sh[j];
                }
                lev++;
                if (!w->m.has_parent) break;
                x = u;
                w = &R.work[w->parent];
            }
            ok = ok && q.n_levels == lev && q.n_payouts == pi;
            uint8_t rd[32];
            ref_digest(&q, rd);
            if (memcmp(rd, q.digest, 32)) digest++;
            if (!ok) bad_ref++;
            if (sum != Rr || q.fee + q.owners_total + q.giving_total + q.investing_total != Rr)
                bad_cons++;
            uint8_t rc[32];
            sale(gross, rc);
            uint64_t pre[ROY_MAX_ACCOUNTS_TEST];
            for (uint32_t a = 0; a < L.n_accounts; a++) pre[a] = deb(a);
            /* a mutated digest is always refused */
            uint8_t dg[32];
            memcpy(dg, q.digest, 32);
            dg[rn(32)] ^= (uint8_t) (1u << rn(8));
            if (roy_pay(&R, &L, mk(), payer, m->id, gross, rc, dg, NULL) != ROY_ERR_STALE_QUOTE)
                digest++;
            roy_status_t st = roy_pay(&R, &L, mk(), payer, m->id, gross, rc, q.digest, &out);
            if (st == ROY_OK)
                paid++;
            else if (st == ROY_ZERO)
                zero++;
            else
                bad_ledger++;
            for (uint32_t a = 0; a < L.n_accounts; a++)
                if (deb(a) != (a == payer ? pre[a] - Rr : pre[a] + expect[a])) bad_ledger++;
            if (!roy_check(&R, &L)) bad_audit++;
        }
    }
    CHECK(paid > 150, "property loop made many payments");
    CHECK(zero > 0, "some payments had a zero royalty");
    CHECK(bad_ref == 0, "every quote matches the independent reference");
    CHECK(bad_cons == 0, "fee + owners + giving + investing == royalty, always");
    CHECK(bad_ledger == 0, "every ledger balance moved exactly as quoted");
    CHECK(bad_audit == 0, "audit clean after every payment");
    CHECK(digest == 0, "digest matches the reference encoding; every mutated digest refused");
    printf("property: %d paid, %d zero\n", paid, zero);
}

int main(void)
{
    test_init();
    test_terms();
    test_lrm();
    test_msgs();
    test_register();
    test_update();
    test_update8();
    test_split();
    test_pay_basic();
    test_realised();
    test_refusals();
    test_chain();
    test_check();
    test_property();
    printf("PASS %d FAIL %d\n", pass, fail);
    return fail ? 1 : 0;
}
