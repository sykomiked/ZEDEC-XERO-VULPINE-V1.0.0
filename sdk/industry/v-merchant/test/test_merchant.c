/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_merchant.c — v-merchant: the proof wire form (round trips, every
 * truncation, malformed fields), card check digits on all three networks,
 * setup refusals, offline acceptance (Ed25519 and ML-DSA-87; wrong
 * merchant, amount, currency, time, payer, key type; forged and flipped
 * signatures; replayed nonces; paid and voided invoices), batch settlement
 * (settled, bounced and reopened, a proof settled by another till refused
 * as a duplicate), fees against zxv_fee_* including the carry, the exact
 * end-of-day report in three currencies, day closing, and the signed
 * record chain. */
#include "zind_test.h"
#include "zvmerch.h"

#undef CHECK_RC
#define CHECK_RC(expr, want)                                                                   \
    do {                                                                                       \
        int32_t rc_ = (expr);                                                                  \
        CHECK(rc_ == (want), "%s -> %d (%s), want %d (%s)", #expr, (int) rc_,                  \
              zvm_status_name(rc_), (int) (want), zvm_status_name(want));                     \
    } while (0)

#define T0 1760000000ULL

typedef struct {
    zt_env e;
    zind_signer *key, *key2, *payer_ml;
    uint8_t ed_sk[32], ed_pk[32], ed2_sk[32], ed2_pk[32], rogue_sk[32];
    zxv_hexlog *qlog;
    zvm_terminal *t;
    uint32_t issuer, merchant, payer, poor, payer_ml_acct, fee[4];
    uint32_t jpy_iss, jpy_merchant, kwd_iss, kwd_merchant;
} fx_t;

static int fx_open(fx_t *f) {
    memset(f, 0, sizeof *f);
    if (zt_env_open(&f->e, "EUR") != 0) return -1;
    f->key = zt_signer(f->e.cap, 1);
    f->key2 = zt_signer(f->e.cap, 2);
    f->payer_ml = zt_signer(f->e.cap, 3);
    if (!f->key || !f->key2 || !f->payer_ml) return -1;
    zt_seed(f->ed_sk, 20);
    zt_seed(f->ed2_sk, 21);
    zt_seed(f->rogue_sk, 22);
    if (zxv_ed25519_public(f->e.cap, f->ed_sk, f->ed_pk) != ZXV_OK ||
        zxv_ed25519_public(f->e.cap, f->ed2_sk, f->ed2_pk) != ZXV_OK)
        return -1;
    if (zxv_hexlog_create(f->e.cap, 1024, &f->qlog) != ZXV_OK) return -1;
    if (zvm_terminal_create(f->e.cap, "SHOP-1", f->key, f->qlog, &f->t) != ZXV_OK) return -1;
    f->issuer = zt_issuer(f->e.cap, "EUR");
    if (zxv_ledger_open_account_cap(f->e.cap, "merchant", "EUR", ZXV_ACCT_HOLDER, &f->merchant) !=
            ZXV_OK ||
        zxv_ledger_open_account_cap(f->e.cap, "payer", "EUR", ZXV_ACCT_HOLDER, &f->payer) !=
            ZXV_OK ||
        zxv_ledger_open_account_cap(f->e.cap, "poor", "EUR", ZXV_ACCT_HOLDER, &f->poor) != ZXV_OK ||
        zxv_ledger_open_account_cap(f->e.cap, "payer-ml", "EUR", ZXV_ACCT_HOLDER,
                                    &f->payer_ml_acct) != ZXV_OK)
        return -1;
    if (zt_fund(f->e.cap, "EUR", f->issuer, f->payer, 100000000, "f1") != 0 ||
        zt_fund(f->e.cap, "EUR", f->issuer, f->poor, 100, "f2") != 0 ||
        zt_fund(f->e.cap, "EUR", f->issuer, f->payer_ml_acct, 100000000, "f3") != 0)
        return -1;
    if (zvm_ccy_add(f->t, "EUR", f->merchant) != ZXV_OK) return -1;
    if (zvm_fee_accounts(f->t, "EUR", f->fee) != ZXV_OK) return -1;
    if (zvm_payer_add(f->t, f->payer, ZVM_ALG_ED25519, f->ed_pk, 32, 0, NULL) != ZXV_OK ||
        zvm_payer_add(f->t, f->poor, ZVM_ALG_ED25519, f->ed2_pk, 32, 0, NULL) != ZXV_OK ||
        zvm_payer_add(f->t, f->payer_ml_acct, ZVM_ALG_MLDSA87, zind_signer_pk(f->payer_ml),
                      ZIND_PK_BYTES, 0, NULL) != ZXV_OK)
        return -1;
    return 0;
}

static void fx_close(fx_t *f) {
    zvm_terminal_destroy(f->t);
    zxv_hexlog_destroy(f->qlog);
    zind_signer_destroy(f->key);
    zind_signer_destroy(f->key2);
    zind_signer_destroy(f->payer_ml);
    zt_env_close(&f->e);
}

static zvm_proof mkproof(const char *merchant, const char *inv, const char *ccy, int64_t amt,
                         uint32_t payer, uint64_t nonce, uint64_t time) {
    zvm_proof p;
    memset(&p, 0, sizeof p);
    snprintf(p.merchant, sizeof p.merchant, "%s", merchant);
    snprintf(p.invoice, sizeof p.invoice, "%s", inv);
    snprintf(p.ccy, sizeof p.ccy, "%s", ccy);
    p.amount = amt;
    p.payer = payer;
    p.nonce = nonce;
    p.time = time;
    p.alg = ZVM_ALG_ED25519;
    p.sig_len = 64;
    return p;
}

/* The customer's device signs, offline. */
static int sign_ed(const zxv_cap *cap, zvm_proof *p, const uint8_t sk[32]) {
    uint8_t m[160];
    size_t len = 0;
    p->alg = ZVM_ALG_ED25519;
    p->sig_len = 64;
    if (zvm_pay_bytes(p, m, sizeof m, &len) != ZXV_OK) return -1;
    return zxv_ed25519_sign(cap, sk, m, len, p->sig) == ZXV_OK ? 0 : -1;
}

static int sign_ml(const zxv_cap *cap, zvm_proof *p, const zind_signer *k) {
    uint8_t m[160];
    size_t len = 0;
    p->alg = ZVM_ALG_MLDSA87;
    p->sig_len = ZIND_SIG_BYTES;
    if (zvm_pay_bytes(p, m, sizeof m, &len) != ZXV_OK) return -1;
    return zind_sign(cap, k, m, len, ZVM_PAY_CTX, p->sig) == ZXV_OK ? 0 : -1;
}

static int same_proof(const zvm_proof *a, const zvm_proof *b) {
    return strcmp(a->merchant, b->merchant) == 0 && strcmp(a->invoice, b->invoice) == 0 &&
           strcmp(a->ccy, b->ccy) == 0 && a->amount == b->amount && a->payer == b->payer &&
           a->nonce == b->nonce && a->time == b->time && a->alg == b->alg &&
           a->sig_len == b->sig_len && memcmp(a->sig, b->sig, a->sig_len) == 0;
}

static uint32_t qcount(fx_t *f) {
    uint32_t n = 0;
    zxv_hexlog_count(f->qlog, &n);
    return n;
}

static void test_names(void) {
    static const int32_t codes[] = {ZVM_E_EXPIRED, ZVM_E_MISMATCH, ZVM_E_PAYER, ZVM_E_CARD,
                                    ZVM_E_PENDING};
    for (size_t i = 0; i < sizeof codes / sizeof codes[0]; i++) {
        CHECK(codes[i] <= -4500 && codes[i] > -4600, "code in block");
        CHECK(strncmp(zvm_status_name(codes[i]), "ZVM_E_", 6) == 0, "name");
    }
    CHECK(strcmp(zvm_status_name(ZXV_E_FUNDS), zxv_status_name(ZXV_E_FUNDS)) == 0, "fallback");
}

static void test_wire(fx_t *f) {
    uint8_t buf[ZVM_PROOF_MAX + 8];
    size_t len = 0;
    zvm_proof p = mkproof("SHOP-1", "INV-1", "EUR", 1250, 7, 99, T0), q;
    CHECK(sign_ed(f->e.cap, &p, f->ed_sk) == 0, "sign");
    CHECK_RC(zvm_proof_encode(&p, buf, sizeof buf, &len), ZXV_OK);
    CHECK(len == 4 + 1 + 1 + 6 + 1 + 5 + 1 + 3 + 8 + 4 + 8 + 8 + 1 + 2 + 64, "length %zu", len);
    CHECK_RC(zvm_proof_decode(buf, len, &q), ZXV_OK);
    CHECK(same_proof(&p, &q), "round trip");
    /* every truncation and one extra byte */
    for (size_t k = 0; k < len; k++) CHECK_RC(zvm_proof_decode(buf, k, &q), ZXV_E_FORMAT);
    buf[len] = 0;
    CHECK_RC(zvm_proof_decode(buf, len + 1, &q), ZXV_E_FORMAT);
    /* the header bytes */
    for (size_t k = 0; k < 5; k++) {
        buf[k] ^= 0x40;
        CHECK_RC(zvm_proof_decode(buf, len, &q), ZXV_E_FORMAT);
        buf[k] ^= 0x40;
    }
    /* an ML-DSA-87 proof */
    {
        zvm_proof m = mkproof("SHOP-1", "INV-ML", "EUR", 5, 8, 1, T0);
        uint8_t big[ZVM_PROOF_MAX];
        CHECK(sign_ml(f->e.cap, &m, f->payer_ml) == 0, "sign");
        CHECK_RC(zvm_proof_encode(&m, big, sizeof big, &len), ZXV_OK);
        CHECK_RC(zvm_proof_decode(big, len, &q), ZXV_OK);
        CHECK(same_proof(&m, &q), "ML round trip");
        CHECK_RC(zvm_proof_encode(&m, big, 100, &len), ZXV_E_BUFFER);
        /* the algorithm byte no longer matches the signature length */
        big[len - 2 - ZIND_SIG_BYTES - 1] = ZVM_ALG_ED25519;
        CHECK_RC(zvm_proof_decode(big, len, &q), ZXV_E_FORMAT);
    }
    /* malformed fields refuse to encode and to decode */
    {
        static const struct {
            const char *m, *i, *c;
            int64_t amt;
        } bad[] = {{"", "I", "EUR", 1},    {"S", "", "EUR", 1},     {"S P", "I", "EUR", 1},
                   {"S", "I", "eur", 1},   {"S", "I", "E1R", 1},   {"S", "I", "EU", 1},
                   {"S", "I", "EUR", 0},   {"S", "I", "EUR", -1},
                   {"S", "I", "EUR", ZXV_AMOUNT_MAX + 1}};
        for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
            zvm_proof b = mkproof(bad[i].m, bad[i].i, bad[i].c, bad[i].amt, 1, 1, T0);
            uint8_t m[256];
            CHECK_RC(zvm_proof_encode(&b, buf, sizeof buf, &len), ZXV_E_ARG);
            CHECK_RC(zvm_pay_bytes(&b, m, sizeof m, &len), ZXV_E_ARG);
        }
        p.sig_len = 63;
        CHECK_RC(zvm_proof_encode(&p, buf, sizeof buf, &len), ZXV_E_ARG);
        p.sig_len = 64;
        p.alg = 9;
        CHECK_RC(zvm_proof_encode(&p, buf, sizeof buf, &len), ZXV_E_ARG);
    }
    CHECK_RC(zvm_proof_decode(NULL, 10, &q), ZXV_E_ARG);
    CHECK_RC(zvm_proof_decode(buf, 10, NULL), ZXV_E_ARG);
}

static void test_cards(void) {
    static const char *luhn_ok[] = {"4111111111111111", "5500005555555559", "79927398713"};
    static const char *luhn_bad[] = {"4111111111111112", "79927398710", "1234567812345678"};
    for (size_t i = 0; i < 3; i++) {
        CHECK_RC(zvm_card_check(ZXV_NET_DRAGON, luhn_ok[i]), ZXV_OK);
        CHECK_RC(zvm_card_check(ZXV_NET_DRAGON, luhn_bad[i]), ZVM_E_CARD);
    }
    /* each network: exactly one last digit is valid for a body */
    for (int32_t net = ZXV_NET_PHOENIX; net <= ZXV_NET_THUNDERBIRD; net++)
        for (int b = 0; b < 12; b++) {
            char pan[24];
            int valid = 0;
            snprintf(pan, sizeof pan, "62%013d", 1000003 * b + 17);
            for (int dgt = 0; dgt < 10; dgt++) {
                pan[15] = (char) ('0' + dgt);
                pan[16] = 0;
                if (zvm_card_check(net, pan) == ZXV_OK) valid++;
            }
            CHECK(valid == 1, "network %d body %d: %d valid check digits", (int) net, b, valid);
        }
    CHECK_RC(zvm_card_check(ZXV_NET_DRAGON, NULL), ZXV_E_ARG);
    CHECK(zvm_card_check(9, "4111111111111111") != ZXV_OK, "unknown network");
}

static void test_setup(fx_t *f) {
    zvm_terminal *t2 = NULL;
    zxv_cap *weak = NULL;
    uint32_t usd = 0, inv = 0, acct = 0;
    CHECK_RC(zvm_terminal_create(f->e.cap, "bad id", f->key, NULL, &t2), ZXV_E_ARG);
    CHECK_RC(zvm_terminal_create(f->e.cap, "SHOP-1", NULL, NULL, &t2), ZXV_E_ARG);
    CHECK_RC(zxv_cap_attenuate(f->e.cap, ZXV_CAP_LEDGER_READ | ZXV_CAP_CRYPTO_SIGN, &weak), ZXV_OK);
    CHECK_RC(zvm_terminal_create(weak, "SHOP-1", f->key, NULL, &t2), ZXV_E_CAP);
    zxv_cap_destroy(weak);
    CHECK_RC(zvm_ccy_add(f->t, "EUR", f->merchant), ZXV_E_DUPLICATE);
    CHECK_RC(zvm_ccy_add(f->t, "XAU", f->merchant), ZXV_E_CURRENCY);
    CHECK_RC(zvm_ccy_add(f->t, "VFV", f->merchant), ZXV_E_CURRENCY);
    CHECK_RC(zvm_ccy_add(f->t, "eur", f->merchant), ZXV_E_CURRENCY);
    CHECK_RC(zvm_ccy_add(f->t, NULL, f->merchant), ZXV_E_ARG);
    CHECK_RC(zxv_ccy_enable_cap(f->e.cap, "USD"), ZXV_OK);
    CHECK_RC(zvm_ccy_add(f->t, "USD", f->merchant), ZXV_E_CURRENCY); /* a EUR account */
    CHECK_RC(zvm_ccy_add(f->t, "USD", 99999), ZXV_E_NOT_FOUND);
    CHECK_RC(zxv_ledger_open_account_cap(f->e.cap, "m-usd", "USD", ZXV_ACCT_HOLDER, &usd), ZXV_OK);
    CHECK_RC(zvm_ccy_add(f->t, "USD", usd), ZXV_OK);
    {
        uint32_t fa[4];
        CHECK_RC(zvm_fee_accounts(f->t, "USD", fa), ZXV_OK);
        CHECK_RC(zvm_fee_accounts(f->t, "GBP", fa), ZXV_E_NOT_FOUND);
        for (int i = 0; i < 4; i++) {
            char c[8];
            size_t cl = 0;
            CHECK_RC(zxv_ledger_account_ccy_cap(f->e.cap, fa[i], c, sizeof c, &cl), ZXV_OK);
            CHECK(strcmp(c, "USD") == 0, "USD fee account %d", i);
        }
    }
    /* payers */
    CHECK_RC(zvm_payer_add(f->t, f->payer, ZVM_ALG_ED25519, f->ed_pk, 32, 0, NULL),
             ZXV_E_DUPLICATE);
    CHECK_RC(zxv_ledger_open_account_cap(f->e.cap, "p2", "EUR", ZXV_ACCT_HOLDER, &acct), ZXV_OK);
    CHECK_RC(zvm_payer_add(f->t, acct, ZVM_ALG_ED25519, f->ed_pk, 31, 0, NULL), ZXV_E_ARG);
    CHECK_RC(zvm_payer_add(f->t, acct, ZVM_ALG_MLDSA87, f->ed_pk, 32, 0, NULL), ZXV_E_ARG);
    CHECK_RC(zvm_payer_add(f->t, acct, 5, f->ed_pk, 32, 0, NULL), ZXV_E_ARG);
    CHECK_RC(zvm_payer_add(f->t, acct, ZVM_ALG_ED25519, f->ed_pk, 32, ZXV_NET_DRAGON,
                           "4111111111111112"),
             ZVM_E_CARD);
    CHECK_RC(zvm_payer_add(f->t, 77777, ZVM_ALG_ED25519, f->ed_pk, 32, 0, NULL), ZXV_E_NOT_FOUND);
    CHECK_RC(zvm_payer_add(f->t, acct, ZVM_ALG_ED25519, f->ed_pk, 32, ZXV_NET_DRAGON,
                           "4111111111111111"),
             ZXV_OK);
    /* invoices */
    CHECK_RC(zvm_invoice_create(f->t, "", "EUR", 100, T0, T0 + 60, &inv), ZXV_E_ARG);
    CHECK_RC(zvm_invoice_create(f->t, "I 1", "EUR", 100, T0, T0 + 60, &inv), ZXV_E_ARG);
    CHECK_RC(zvm_invoice_create(f->t, "I1", "GBP", 100, T0, T0 + 60, &inv), ZXV_E_CURRENCY);
    CHECK_RC(zvm_invoice_create(f->t, "I1", "EUR", 0, T0, T0 + 60, &inv), ZXV_E_RANGE);
    CHECK_RC(zvm_invoice_create(f->t, "I1", "EUR", -3, T0, T0 + 60, &inv), ZXV_E_RANGE);
    CHECK_RC(zvm_invoice_create(f->t, "I1", "EUR", ZXV_AMOUNT_MAX + 1, T0, T0 + 60, &inv),
             ZXV_E_RANGE);
    CHECK_RC(zvm_invoice_create(f->t, "I1", "EUR", 100, T0, T0, &inv), ZXV_E_ARG);
    CHECK_RC(zvm_invoice_create(f->t, "I1", "EUR", 100, T0, T0 + 60, NULL), ZXV_E_ARG);
    CHECK_RC(zvm_invoice_create(f->t, "I1", "EUR", 100, T0, T0 + 60, &inv), ZXV_OK);
    CHECK_RC(zvm_invoice_create(f->t, "I1", "EUR", 100, T0, T0 + 60, &inv), ZXV_E_DUPLICATE);
    {
        int32_t st = 0;
        uint8_t tag = 0, base = 0, ph = 0;
        CHECK_RC(zvm_invoice_state(f->t, inv, &st, &tag), ZXV_OK);
        zxv_hex_unpack(tag, &base, NULL, &ph);
        CHECK(st == ZVM_INV_OPEN && base == ZXV_HEX_BASE_NEITHER && ph == ZXV_HEX_PHASE_POTENTIAL,
              "open invoice is potential");
        CHECK_RC(zvm_invoice_state(f->t, 0, &st, NULL), ZXV_E_NOT_FOUND);
        CHECK_RC(zvm_invoice_state(f->t, 999, &st, NULL), ZXV_E_NOT_FOUND);
    }
}

static void test_accept(fx_t *f) {
    uint32_t i1 = 0, i2 = 0, i3 = 0, iv = 0, pr = 0, q0;
    int32_t st = 0;
    zvm_proof p;
    CHECK_RC(zvm_invoice_create(f->t, "A-1", "EUR", 1250, T0, T0 + 600, &i1), ZXV_OK);
    CHECK_RC(zvm_invoice_create(f->t, "A-2", "EUR", 99900, T0, T0 + 600, &i2), ZXV_OK);
    CHECK_RC(zvm_invoice_create(f->t, "A-3", "EUR", 70, T0, T0 + 600, &i3), ZXV_OK);
    CHECK_RC(zvm_invoice_create(f->t, "A-V", "EUR", 70, T0, T0 + 600, &iv), ZXV_OK);
    CHECK_RC(zvm_invoice_void(f->t, iv), ZXV_OK);
    CHECK_RC(zvm_invoice_void(f->t, iv), ZXV_E_STATE);

    /* mismatches, each correctly signed */
    {
        static const struct {
            const char *m, *i, *c;
            int64_t amt;
            uint64_t time;
            int32_t want;
        } v[] = {
            {"SHOP-2", "A-1", "EUR", 1250, T0 + 5, ZVM_E_MISMATCH},
            {"SHOP-1", "A-1", "EUR", 1249, T0 + 5, ZVM_E_MISMATCH},
            {"SHOP-1", "A-1", "EUR", 1251, T0 + 5, ZVM_E_MISMATCH},
            {"SHOP-1", "A-1", "USD", 1250, T0 + 5, ZVM_E_MISMATCH},
            {"SHOP-1", "A-1", "GBP", 1250, T0 + 5, ZVM_E_MISMATCH},
            {"SHOP-1", "A-1", "EUR", 1250, T0 - 1, ZVM_E_EXPIRED},
            {"SHOP-1", "A-1", "EUR", 1250, T0 + 601, ZVM_E_EXPIRED},
            {"SHOP-1", "NONE", "EUR", 1250, T0 + 5, ZXV_E_NOT_FOUND},
            {"SHOP-1", "A-V", "EUR", 70, T0 + 5, ZXV_E_STATE},
        };
        for (size_t k = 0; k < sizeof v / sizeof v[0]; k++) {
            p = mkproof(v[k].m, v[k].i, v[k].c, v[k].amt, f->payer, 1000 + k, v[k].time);
            CHECK(sign_ed(f->e.cap, &p, f->ed_sk) == 0, "sign");
            CHECK_RC(zvm_accept(f->t, &p, &pr), v[k].want);
        }
    }
    /* unknown payer; known payer with the other key type */
    p = mkproof("SHOP-1", "A-1", "EUR", 1250, 4242, 1, T0 + 5);
    CHECK(sign_ed(f->e.cap, &p, f->ed_sk) == 0, "sign");
    CHECK_RC(zvm_accept(f->t, &p, &pr), ZVM_E_PAYER);
    p = mkproof("SHOP-1", "A-1", "EUR", 1250, f->payer_ml_acct, 1, T0 + 5);
    CHECK(sign_ed(f->e.cap, &p, f->ed_sk) == 0, "sign");
    CHECK_RC(zvm_accept(f->t, &p, &pr), ZVM_E_PAYER);
    /* forged: another key, flipped bits, changed nonce after signing */
    q0 = qcount(f);
    p = mkproof("SHOP-1", "A-1", "EUR", 1250, f->payer, 1, T0 + 5);
    CHECK(sign_ed(f->e.cap, &p, f->rogue_sk) == 0, "sign");
    CHECK_RC(zvm_accept(f->t, &p, &pr), ZXV_E_VERIFY);
    CHECK(sign_ed(f->e.cap, &p, f->ed_sk) == 0, "sign");
    for (int k = 0; k < 24; k++) {
        p.sig[(k * 11) % 64] ^= (uint8_t) (1u << (k % 8));
        CHECK_RC(zvm_accept(f->t, &p, &pr), ZXV_E_VERIFY);
        p.sig[(k * 11) % 64] ^= (uint8_t) (1u << (k % 8));
    }
    p.nonce = 2;
    CHECK_RC(zvm_accept(f->t, &p, &pr), ZXV_E_VERIFY);
    p.nonce = 1;
    CHECK(qcount(f) == q0 + 26, "forged proofs quarantined (%u)", qcount(f) - q0);
    {
        int32_t s2 = 0;
        CHECK_RC(zvm_invoice_state(f->t, i1, &s2, NULL), ZXV_OK);
        CHECK(s2 == ZVM_INV_OPEN, "still open");
    }
    /* the genuine proof, through the wire form */
    {
        uint8_t wire[ZVM_PROOF_MAX];
        size_t len = 0;
        zvm_proof q;
        uint8_t tag = 0, ph = 0;
        CHECK_RC(zvm_proof_encode(&p, wire, sizeof wire, &len), ZXV_OK);
        CHECK_RC(zvm_proof_decode(wire, len, &q), ZXV_OK);
        CHECK_RC(zvm_accept(f->t, &q, &pr), ZXV_OK);
        CHECK_RC(zvm_proof_state(f->t, pr, &st, NULL), ZXV_OK);
        CHECK(st == ZVM_PRF_QUEUED, "queued");
        CHECK_RC(zvm_invoice_state(f->t, i1, &st, &tag), ZXV_OK);
        zxv_hex_unpack(tag, NULL, NULL, &ph);
        CHECK(st == ZVM_INV_ACCEPTED && ph == ZXV_HEX_PHASE_EMERGING, "accepted, emerging");
        CHECK(zt_equity(f->e.cap, f->merchant) == 0, "offline: no money moved yet");
        /* the same proof again; the same nonce on another invoice */
        CHECK_RC(zvm_accept(f->t, &q, &pr), ZIND_E_REPLAY);
        q0 = qcount(f);
        p = mkproof("SHOP-1", "A-3", "EUR", 70, f->payer, 1, T0 + 6);
        CHECK(sign_ed(f->e.cap, &p, f->ed_sk) == 0, "sign");
        CHECK_RC(zvm_accept(f->t, &p, &pr), ZIND_E_REPLAY);
        CHECK(qcount(f) == q0 + 1, "replayed nonce quarantined");
    }
    /* an ML-DSA-87 payer, and the poor payer whose payment will bounce */
    p = mkproof("SHOP-1", "A-2", "EUR", 99900, f->payer_ml_acct, 1, T0 + 9);
    CHECK(sign_ml(f->e.cap, &p, f->key2) == 0, "sign");
    CHECK_RC(zvm_accept(f->t, &p, &pr), ZXV_E_VERIFY);
    CHECK(sign_ml(f->e.cap, &p, f->payer_ml) == 0, "sign");
    p.sig_len = 64;
    CHECK_RC(zvm_accept(f->t, &p, &pr), ZXV_E_ARG);
    p.sig_len = ZIND_SIG_BYTES;
    CHECK_RC(zvm_accept(f->t, &p, &pr), ZXV_OK);
    p = mkproof("SHOP-1", "A-3", "EUR", 70, f->poor, 5, T0 + 9);
    CHECK(sign_ed(f->e.cap, &p, f->ed2_sk) == 0, "sign");
    CHECK_RC(zvm_accept(f->t, &p, &pr), ZXV_OK);
    CHECK_RC(zvm_accept(NULL, &p, &pr), ZXV_E_ARG);
    CHECK_RC(zvm_accept(f->t, NULL, &pr), ZXV_E_ARG);
}

static void test_settle(fx_t *f) {
    zvm_batch b;
    zvm_totals tot;
    uint32_t i3 = 0, pr = 0;
    int64_t pay0 = zt_equity(f->e.cap, f->payer), ml0 = zt_equity(f->e.cap, f->payer_ml_acct);
    int64_t poor0 = zt_equity(f->e.cap, f->poor);
    uint64_t fee1 = 0, fee2 = 0, rem = 0, parts1[4], parts2[4];
    int32_t st = 0;
    CHECK_RC(zvm_eod_close(f->t, 1, NULL), ZVM_E_PENDING);
    memset(&b, 0xff, sizeof b);
    /* the first poor payment of 70 bounces: the poor payer holds 100 but we drain it first */
    CHECK_RC(zxv_ledger_post_cap(f->e.cap, f->poor, f->issuer, 100, "EUR", "drain", NULL), ZXV_OK);
    CHECK_RC(zvm_settle(f->t, &b), ZXV_OK);
    CHECK(b.settled == 2 && b.bounced == 1 && b.duplicates == 0, "batch %u/%u/%u", b.settled,
          b.bounced, b.duplicates);
    CHECK_RC(zxv_fee_assure_carry(0, 1250, &fee1, &rem), ZXV_OK);
    CHECK_RC(zxv_fee_assure_carry(rem, 99900, &fee2, &rem), ZXV_OK);
    zxv_fee_split(fee1, parts1);
    zxv_fee_split(fee2, parts2);
    CHECK(zt_equity(f->e.cap, f->payer) == pay0 - 1250, "payer paid");
    CHECK(zt_equity(f->e.cap, f->payer_ml_acct) == ml0 - 99900, "ML payer paid");
    CHECK(zt_equity(f->e.cap, f->poor) == poor0 - 100, "the poor payer paid nothing");
    CHECK(zt_equity(f->e.cap, f->merchant) == 1250 + 99900 - (int64_t) (fee1 + fee2),
          "merchant net");
    for (int i = 0; i < 4; i++)
        CHECK(zt_equity(f->e.cap, f->fee[i]) == (int64_t) (parts1[i] + parts2[i]), "bucket %d", i);
    CHECK_RC(zvm_day_totals(f->t, "EUR", &tot), ZXV_OK);
    CHECK(tot.settled == 2 && tot.bounced == 1 && tot.gross == 101150 &&
              tot.fee == (int64_t) (fee1 + fee2) && tot.net == tot.gross - tot.fee,
          "day totals");
    CHECK_RC(zvm_day_totals(f->t, "GBP", &tot), ZXV_E_NOT_FOUND);
    /* the bounced invoice reopened; the payer pays again with a fresh nonce */
    CHECK_RC(zvm_invoice_find(f->t, "A-3", &i3), ZXV_OK);
    CHECK_RC(zvm_invoice_state(f->t, i3, &st, NULL), ZXV_OK);
    CHECK(st == ZVM_INV_OPEN, "reopened");
    {
        zvm_proof p = mkproof("SHOP-1", "A-3", "EUR", 70, f->poor, 5, T0 + 20);
        CHECK(sign_ed(f->e.cap, &p, f->ed2_sk) == 0, "sign");
        CHECK_RC(zvm_accept(f->t, &p, &pr), ZIND_E_REPLAY); /* nonce 5 was used */
        p.nonce = 6;
        CHECK(sign_ed(f->e.cap, &p, f->ed2_sk) == 0, "sign");
        CHECK_RC(zvm_accept(f->t, &p, &pr), ZXV_OK);
        CHECK(zt_fund(f->e.cap, "EUR", f->issuer, f->poor, 70, "topup") == 0, "fund");
        CHECK_RC(zvm_settle(f->t, &b), ZXV_OK);
        CHECK(b.settled == 1 && b.bounced == 0, "second batch");
        CHECK_RC(zvm_invoice_state(f->t, i3, &st, NULL), ZXV_OK);
        CHECK(st == ZVM_INV_SETTLED, "settled");
        CHECK_RC(zvm_proof_state(f->t, pr, &st, NULL), ZXV_OK);
        CHECK(st == ZVM_PRF_SETTLED, "proof settled");
    }
    /* an empty batch */
    CHECK_RC(zvm_settle(f->t, &b), ZXV_OK);
    CHECK(b.settled == 0 && b.bounced == 0 && b.duplicates == 0, "empty batch");
    CHECK_RC(zxv_ledger_check_cap(f->e.cap), ZXV_OK);
}

/* Two tills of one merchant accept the same proof offline: one settles,
 * the other is refused by the ledger reference and quarantined. */
static void test_duplicate(fx_t *f) {
    zvm_terminal *t2 = NULL;
    zvm_batch b;
    uint32_t i_a = 0, i_b = 0, pa = 0, pb = 0, q0;
    int32_t st = 0;
    int64_t pay0, m0;
    zvm_proof p = mkproof("SHOP-1", "DUP-1", "EUR", 4000, f->payer, 777, T0 + 30);
    CHECK_RC(zvm_terminal_create(f->e.cap, "SHOP-1", f->key2, f->qlog, &t2), ZXV_OK);
    if (!t2) return;
    CHECK_RC(zvm_ccy_add(t2, "EUR", f->merchant), ZXV_OK);
    CHECK_RC(zvm_payer_add(t2, f->payer, ZVM_ALG_ED25519, f->ed_pk, 32, 0, NULL), ZXV_OK);
    CHECK_RC(zvm_payer_add(t2, f->poor, ZVM_ALG_ED25519, f->ed2_pk, 32, 0, NULL), ZXV_OK);
    CHECK_RC(zvm_invoice_create(f->t, "DUP-1", "EUR", 4000, T0, T0 + 600, &i_a), ZXV_OK);
    CHECK_RC(zvm_invoice_create(t2, "DUP-1", "EUR", 4000, T0, T0 + 600, &i_b), ZXV_OK);
    CHECK(sign_ed(f->e.cap, &p, f->ed_sk) == 0, "sign");
    CHECK_RC(zvm_accept(f->t, &p, &pa), ZXV_OK);
    CHECK_RC(zvm_accept(t2, &p, &pb), ZXV_OK); /* offline, the till cannot know */
    pay0 = zt_equity(f->e.cap, f->payer);
    m0 = zt_equity(f->e.cap, f->merchant);
    CHECK_RC(zvm_settle(f->t, &b), ZXV_OK);
    CHECK(b.settled == 1, "first till settles");
    q0 = qcount(f);
    CHECK_RC(zvm_settle(t2, &b), ZXV_OK);
    CHECK(b.settled == 0 && b.duplicates == 1, "second till: duplicate");
    CHECK(qcount(f) == q0 + 1, "duplicate quarantined");
    CHECK(zt_equity(f->e.cap, f->payer) == pay0 - 4000, "the payer paid once");
    {
        uint64_t fee = 0;
        zvm_totals tot;
        CHECK_RC(zvm_day_totals(t2, "EUR", &tot), ZXV_OK);
        CHECK(tot.duplicates == 1 && tot.settled == 0 && tot.gross == 0, "t2 totals");
        (void) fee;
        CHECK(zt_equity(f->e.cap, f->merchant) > m0 && zt_equity(f->e.cap, f->merchant) < m0 + 4000,
              "merchant got 4000 minus the fee, once");
    }
    CHECK_RC(zvm_proof_state(t2, pb, &st, NULL), ZXV_OK);
    CHECK(st == ZVM_PRF_DUPLICATE, "duplicate proof");
    CHECK_RC(zvm_invoice_state(t2, i_b, &st, NULL), ZXV_OK);
    CHECK(st == ZVM_INV_VOID, "t2 invoice void");
    CHECK_RC(zxv_ledger_check_cap(f->e.cap), ZXV_OK);
    /* a duplicate whose payer is now short is still a duplicate, not a bounce
     * (the ledger reports funds before references; the journal tells them apart) */
    {
        int64_t all;
        zvm_proof p2 = mkproof("SHOP-1", "DUP-2", "EUR", 3000, f->poor, 900, T0 + 31);
        CHECK_RC(zvm_invoice_create(f->t, "DUP-2", "EUR", 3000, T0, T0 + 600, &i_a), ZXV_OK);
        CHECK_RC(zvm_invoice_create(t2, "DUP-2", "EUR", 3000, T0, T0 + 600, &i_b), ZXV_OK);
        CHECK(sign_ed(f->e.cap, &p2, f->ed2_sk) == 0, "sign");
        CHECK_RC(zvm_accept(f->t, &p2, &pa), ZXV_OK);
        CHECK_RC(zvm_accept(t2, &p2, &pb), ZXV_OK);
        CHECK(zt_fund(f->e.cap, "EUR", f->issuer, f->poor, 3000, "dup2-fund") == 0, "fund");
        CHECK_RC(zvm_settle(f->t, &b), ZXV_OK);
        CHECK(b.settled == 1, "settled on till 1");
        all = zt_equity(f->e.cap, f->poor);
        CHECK(all >= 0, "poor balance");
        if (all > 0)
            CHECK_RC(zxv_ledger_post_cap(f->e.cap, f->poor, f->issuer, all, "EUR", "dup2-drain",
                                         NULL),
                     ZXV_OK);
        CHECK_RC(zvm_settle(t2, &b), ZXV_OK);
        CHECK(b.duplicates == 1 && b.bounced == 0, "duplicate, not bounced (%u/%u)",
              b.duplicates, b.bounced);
        CHECK_RC(zvm_invoice_state(t2, i_b, &st, NULL), ZXV_OK);
        CHECK(st == ZVM_INV_VOID, "not reopened");
    }
    zvm_terminal_destroy(t2);
}

/* 200 payments of 5.00 pay exactly the fee of one payment of 1000.00. */
static void test_carry(void) {
    fx_t f;
    uint64_t one = 0;
    zvm_batch b;
    zvm_totals tot;
    if (fx_open(&f) != 0) {
        CHECK(0, "fixture");
        return;
    }
    for (int i = 0; i < 200; i++) {
        char id[32];
        uint32_t inv = 0, pr = 0;
        zvm_proof p;
        snprintf(id, sizeof id, "C-%d", i);
        CHECK_RC(zvm_invoice_create(f.t, id, "EUR", 500, T0, T0 + 600, &inv), ZXV_OK);
        p = mkproof("SHOP-1", id, "EUR", 500, f.payer, (uint64_t) i, T0 + 1);
        CHECK(sign_ed(f.e.cap, &p, f.ed_sk) == 0, "sign");
        CHECK_RC(zvm_accept(f.t, &p, &pr), ZXV_OK);
    }
    CHECK_RC(zvm_settle(f.t, &b), ZXV_OK);
    CHECK(b.settled == 200, "settled 200");
    CHECK_RC(zxv_fee_assure(100000, &one), ZXV_OK);
    CHECK_RC(zvm_day_totals(f.t, "EUR", &tot), ZXV_OK);
    CHECK(tot.fee == (int64_t) one && one == 88, "carry: %lld == %llu", (long long) tot.fee,
          (unsigned long long) one);
    CHECK(tot.fee_part[0] + tot.fee_part[1] + tot.fee_part[2] + tot.fee_part[3] == tot.fee,
          "parts sum");
    fx_close(&f);
}

static void test_eod(void) {
    fx_t f;
    uint32_t jiss, jm, kiss, km, jp, kp, inv = 0, pr = 0;
    zvm_batch b;
    char rep[1024];
    size_t len = 0;
    uint8_t dig[32], dig2[32];
    static const char want[] =
        "EOD SHOP-1 day 20261012\n"
        "EUR settled 2 gross 1012.5 fee 0.9 net 1011.6 bounced 1 duplicates 0\n"
        "EUR fee reserve-floor 0.47 vbill-pool 0.22 infrastructure 0.13 regenerative 0.08\n"
        "JPY settled 1 gross 5000 fee 4 net 4996 bounced 0 duplicates 0\n"
        "JPY fee reserve-floor 3 vbill-pool 1 infrastructure 0 regenerative 0\n"
        "KWD settled 1 gross 12.345 fee 0.01 net 12.335 bounced 0 duplicates 0\n"
        "KWD fee reserve-floor 0.006 vbill-pool 0.002 infrastructure 0.001 regenerative 0.001\n";
    if (fx_open(&f) != 0) {
        CHECK(0, "fixture");
        return;
    }
    CHECK_RC(zxv_ccy_enable_cap(f.e.cap, "JPY"), ZXV_OK);
    CHECK_RC(zxv_ccy_enable_cap(f.e.cap, "KWD"), ZXV_OK);
    jiss = zt_issuer(f.e.cap, "JPY");
    kiss = zt_issuer(f.e.cap, "KWD");
    CHECK_RC(zxv_ledger_open_account_cap(f.e.cap, "m-jpy", "JPY", ZXV_ACCT_HOLDER, &jm), ZXV_OK);
    CHECK_RC(zxv_ledger_open_account_cap(f.e.cap, "m-kwd", "KWD", ZXV_ACCT_HOLDER, &km), ZXV_OK);
    CHECK_RC(zxv_ledger_open_account_cap(f.e.cap, "p-jpy", "JPY", ZXV_ACCT_HOLDER, &jp), ZXV_OK);
    CHECK_RC(zxv_ledger_open_account_cap(f.e.cap, "p-kwd", "KWD", ZXV_ACCT_HOLDER, &kp), ZXV_OK);
    CHECK(zt_fund(f.e.cap, "JPY", jiss, jp, 100000, "fj") == 0, "fund");
    CHECK(zt_fund(f.e.cap, "KWD", kiss, kp, 100000, "fk") == 0, "fund");
    CHECK_RC(zvm_ccy_add(f.t, "JPY", jm), ZXV_OK);
    CHECK_RC(zvm_ccy_add(f.t, "KWD", km), ZXV_OK);
    CHECK_RC(zvm_payer_add(f.t, jp, ZVM_ALG_ED25519, f.ed_pk, 32, 0, NULL), ZXV_OK);
    CHECK_RC(zvm_payer_add(f.t, kp, ZVM_ALG_ED25519, f.ed_pk, 32, 0, NULL), ZXV_OK);
    {
        static const struct {
            const char *id, *ccy;
            int64_t amt;
            int who; /* 0 payer, 1 poor, 2 jpy, 3 kwd */
        } v[] = {{"E-1", "EUR", 1250, 0},  {"E-2", "EUR", 100000, 0}, {"E-3", "EUR", 5000, 1},
                 {"J-1", "JPY", 5000, 2},  {"K-1", "KWD", 12345, 3}};
        for (size_t k = 0; k < sizeof v / sizeof v[0]; k++) {
            uint32_t who = v[k].who == 0 ? f.payer : v[k].who == 1 ? f.poor : v[k].who == 2 ? jp : kp;
            zvm_proof p;
            CHECK_RC(zvm_invoice_create(f.t, v[k].id, v[k].ccy, v[k].amt, T0, T0 + 600, &inv),
                     ZXV_OK);
            p = mkproof("SHOP-1", v[k].id, v[k].ccy, v[k].amt, who, 50 + k, T0 + 1);
            CHECK(sign_ed(f.e.cap, &p, v[k].who == 1 ? f.ed2_sk : f.ed_sk) == 0, "sign");
            CHECK_RC(zvm_accept(f.t, &p, &pr), ZXV_OK);
        }
    }
    CHECK_RC(zvm_settle(f.t, &b), ZXV_OK);
    CHECK(b.settled == 4 && b.bounced == 1, "batch");
    CHECK_RC(zvm_eod_report(f.t, 20261012, NULL, 0, &len), ZXV_E_BUFFER);
    CHECK(len == strlen(want), "report length %zu vs %zu", len, strlen(want));
    CHECK_RC(zvm_eod_report(f.t, 20261012, rep, len, &len), ZXV_E_BUFFER);
    CHECK_RC(zvm_eod_report(f.t, 20261012, rep, sizeof rep, &len), ZXV_OK);
    CHECK(strcmp(rep, want) == 0, "report:\n%s", rep);
    CHECK_RC(zvm_eod_close(f.t, 20261012, dig), ZXV_OK);
    zxv_sha3_256((const uint8_t *) want, strlen(want), dig2);
    CHECK(memcmp(dig, dig2, 32) == 0, "the signed digest is the report's");
    CHECK_RC(zvm_eod_close(f.t, 20261012, dig), ZXV_E_STATE);
    CHECK_RC(zvm_eod_close(f.t, 20261011, dig), ZXV_E_STATE);
    {
        zvm_totals tot;
        CHECK_RC(zvm_day_totals(f.t, "EUR", &tot), ZXV_OK);
        CHECK(tot.settled == 0 && tot.gross == 0 && tot.fee == 0 && tot.bounced == 0,
              "new day starts at zero");
    }
    {
        const zind_chain *c = zvm_records(f.t);
        const uint8_t *trust[1] = {zind_signer_pk(f.key)};
        zind_record r;
        uint32_t n = 0, day = 0;
        zind_rd rd;
        uint8_t d3[32];
        CHECK_RC(zind_chain_verify(f.e.cap, c, trust, 1, NULL), ZXV_OK);
        CHECK_RC(zind_chain_count(c, &n), ZXV_OK);
        CHECK_RC(zind_chain_at(c, n - 1, &r), ZXV_OK);
        CHECK(r.kind == ZVM_REC_EOD, "last record is the EOD");
        zind_rd_init(&rd, r.payload, r.payload_len);
        day = zind_rd_u32(&rd);
        for (int i = 0; i < 32; i++) d3[i] = zind_rd_u8(&rd);
        CHECK(day == 20261012 && memcmp(d3, dig, 32) == 0 && !rd.bad, "EOD record");
    }
    CHECK_RC(zvm_eod_close(f.t, 20261013, NULL), ZXV_OK);
    fx_close(&f);
}

static void test_chain(fx_t *f) {
    const zind_chain *c = zvm_records(f->t);
    const uint8_t *trust[1] = {zind_signer_pk(f->key)};
    const uint8_t *wrong[1] = {zind_signer_pk(f->key2)};
    uint32_t bad = 0;
    size_t len = 0;
    uint8_t *buf;
    CHECK_RC(zind_chain_verify(f->e.cap, c, trust, 1, &bad), ZXV_OK);
    CHECK_RC(zind_chain_verify(f->e.cap, c, wrong, 1, &bad), ZIND_E_UNTRUSTED);
    zind_chain_export(c, NULL, 0, &len);
    buf = (uint8_t *) malloc(len);
    if (buf) {
        zind_chain *imp = NULL;
        CHECK_RC(zind_chain_export(c, buf, len, &len), ZXV_OK);
        CHECK_RC(zind_chain_import("v-merchant", buf, len, &imp), ZXV_OK);
        CHECK_RC(zind_chain_verify(f->e.cap, imp, trust, 1, &bad), ZXV_OK);
        zind_chain_destroy(imp);
        imp = NULL;
        buf[len - 100] ^= 1; /* inside the last signature */
        if (zind_chain_import("v-merchant", buf, len, &imp) == ZXV_OK) {
            CHECK(zind_chain_verify(f->e.cap, imp, trust, 1, &bad) == ZIND_E_CHAIN, "tamper");
            zind_chain_destroy(imp);
        }
        free(buf);
    }
    CHECK(zxv_hexlog_verify(f->qlog) == ZXV_OK, "quarantine log");
}

int main(void) {
    fx_t f;
    test_names();
    if (fx_open(&f) != 0) {
        fprintf(stderr, "fixture failed: %s\n", zt_last_err());
        return 1;
    }
    test_wire(&f);
    test_cards();
    test_setup(&f);
    test_accept(&f);
    test_settle(&f);
    test_duplicate(&f);
    test_chain(&f);
    fx_close(&f);
    test_carry();
    test_eod();
    return zt_report("test_merchant");
}
