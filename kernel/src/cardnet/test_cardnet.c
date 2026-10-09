/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_cardnet.c — host tests for kernel/src/cardnet.
 *
 * Published check-digit vectors, every single-digit error and adjacent
 * transposition of 3000 random numbers per algorithm, the
 * VSS C5 exclusivity rule over generated PANs, the charge-card lifecycle
 * (issue, authorize with real ML-DSA-65 signatures, settle, reverse,
 * statement, delinquency, payment), the no-interest guarantees, C1/C2,
 * ISO 8583 and EMV TLV round trips and malformed-input rejection, and the
 * mobile-money push / USSD state machines.
 */
#include <stdio.h>
#include <string.h>
#include <stddef.h>

#include "cardnet.h"
#include "cn_check.h"
#include "cn_iso8583.h"
#include "cn_emv.h"
#include "cn_mobile.h"
#include "cn_pq.h"
#include "keccak.h"
/* The real headers, in the same translation unit as cn_pq.h and the
 * mirrored constants, so any drift is a compile error. */
#include "pq_security.h"
#include "capital_forms.h"
#include "vino_stores.h"

_Static_assert(CN_PK_BYTES == PQ_MLDSA65_PK_BYTES, "pk size");
_Static_assert(CN_SK_BYTES == PQ_MLDSA65_SK_BYTES, "sk size");
_Static_assert(CN_SIG_BYTES == PQ_MLDSA65_SIG_BYTES, "sig size");
_Static_assert(CN_RAIL_DEBIT == VINO_ISO_DEBIT, "rail 846");
_Static_assert(CN_RAIL_CREDIT == VINO_ISO_CREDIT, "rail 810");
_Static_assert(CN_RAIL_EQUITY == VINO_ISO_EQUITY, "rail 888");
_Static_assert((int) CN_FORM_SOCIAL == (int) CAPITAL_SOCIAL, "form 0");
_Static_assert((int) CN_FORM_NATURAL == (int) CAPITAL_NATURAL, "form 1");
_Static_assert((int) CN_FORM_HERITAGE == (int) CAPITAL_HERITAGE_INTELLECTUAL, "form 2");
_Static_assert((int) CN_FORM_GOVERNANCE == (int) CAPITAL_GOVERNANCE_INSTITUTIONAL, "form 3");
_Static_assert((int) CN_FORM_FINANCIAL == (int) CAPITAL_FINANCIAL, "form 4");
_Static_assert((int) CN_FORM_MATERIAL == (int) CAPITAL_MATERIAL, "form 5");
_Static_assert((int) CN_FORM_LIVING == (int) CAPITAL_LIVING, "form 6");
_Static_assert((int) CN_FORM_KNOWLEDGE == (int) CAPITAL_KNOWLEDGE, "form 7");
_Static_assert((int) CN_FORM_BUILT == (int) CAPITAL_BUILT, "form 8");
_Static_assert((int) CN_FORM_COUNT == (int) CAPITAL_FORM_COUNT, "form count");
/* The fee schedule is exactly two fields: a closed kind and a flat amount.
 * No rate, no period, no basis: a percentage-of-balance-over-time charge
 * has nowhere to live. */
_Static_assert(sizeof(cn_fee_t) == 16, "cn_fee_t gained a field");
_Static_assert(offsetof(cn_fee_t, kind) == 0, "fee layout");
_Static_assert(offsetof(cn_fee_t, amount_minor) == 8, "fee layout");

static int g_pass, g_fail;
#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (c)                                                                                     \
            g_pass++;                                                                              \
        else {                                                                                     \
            g_fail++;                                                                              \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                                  \
        }                                                                                          \
    } while (0)

static uint64_t g_rng = 0x9E3779B97F4A7C15ull;
static uint32_t rnd32(void)
{
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 7;
    g_rng ^= g_rng << 17;
    return (uint32_t) (g_rng >> 16);
}
static void rnd_digits(char *d, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) d[i] = (char) ('0' + rnd32() % 10u);
    d[n] = 0;
}

/* ===================================================================== */
static void test_vectors(void)
{
    printf("[check digits: published vectors]\n");
    /* Luhn: ISO/IEC 7812-1 Annex B example and common test PANs. */
    CHECK(cn_luhn_digit("7992739871", 10) == 3);
    CHECK(cn_luhn_valid("79927398713", 11));
    CHECK(!cn_luhn_valid("79927398710", 11));
    CHECK(cn_luhn_valid("4111111111111111", 16));
    CHECK(!cn_luhn_valid("4111111111111112", 16));
    CHECK(cn_luhn_valid("49927398716", 11));
    CHECK(!cn_luhn_valid("49927398717", 11));
    CHECK(cn_luhn_valid("1234567812345670", 16));
    CHECK(!cn_luhn_valid("1234567812345678", 16));
    /* Damm: the worked example from Damm's construction, 572 -> 4. */
    CHECK(cn_damm_digit("572", 3) == 4);
    CHECK(cn_damm_valid("5724", 4));
    CHECK(!cn_damm_valid("5727", 4));
    CHECK(!cn_damm_valid("7524", 4));
    /* Verhoeff: 236 -> 3, 12345 -> 1, 123456789012 -> 0. */
    CHECK(cn_verhoeff_digit("236", 3) == 3);
    CHECK(cn_verhoeff_valid("2363", 4));
    CHECK(!cn_verhoeff_valid("2364", 4));
    CHECK(cn_verhoeff_digit("12345", 5) == 1);
    CHECK(cn_verhoeff_valid("123451", 6));
    CHECK(cn_verhoeff_digit("123456789012", 12) == 0);
    CHECK(cn_verhoeff_valid("1234567890120", 13));
    /* Bad inputs */
    CHECK(cn_luhn_digit(NULL, 3) == -1 && cn_damm_digit("", 0) == -1);
    CHECK(cn_verhoeff_digit("12a", 3) == -1 && !cn_luhn_valid("7", 1));
    CHECK(!cn_damm_valid(NULL, 4) && !cn_verhoeff_valid("23 3", 4));
}

/* Every single-digit substitution and adjacent transposition over random
 * 16-digit numbers: Damm and Verhoeff catch all, Luhn all but 09<->90. */
static void test_detection(void)
{
    printf("[check digits: error-detection properties]\n");
    uint32_t miss_sub[3] = {0, 0, 0}, miss_tr[3] = {0, 0, 0}, luhn_tr_other = 0;
    for (uint32_t it = 0; it < 3000; it++) {
        char b[17];
        rnd_digits(b, 15);
        for (uint32_t a = 0; a < 3; a++) {
            char n[17];
            memcpy(n, b, 16);
            int cd = cn_network_check_digit((cn_network_t) a, n, 15);
            n[15] = (char) ('0' + cd);
            n[16] = 0;
            if (!cn_network_check_valid((cn_network_t) a, n, 16)) miss_sub[a] += 1000;
            for (uint32_t p = 0; p < 16; p++) {
                for (char d = '0'; d <= '9'; d++) {
                    if (d == n[p]) continue;
                    char m[17];
                    memcpy(m, n, 17);
                    m[p] = d;
                    if (cn_network_check_valid((cn_network_t) a, m, 16)) miss_sub[a]++;
                }
                if (p < 15 && n[p] != n[p + 1]) {
                    char m[17];
                    memcpy(m, n, 17);
                    m[p] = n[p + 1];
                    m[p + 1] = n[p];
                    if (cn_network_check_valid((cn_network_t) a, m, 16)) {
                        miss_tr[a]++;
                        bool is09 =
                            (n[p] == '0' && n[p + 1] == '9') || (n[p] == '9' && n[p + 1] == '0');
                        if (a == 0 && !is09) luhn_tr_other++;
                    }
                }
            }
        }
    }
    CHECK(miss_sub[0] == 0 && miss_sub[1] == 0 && miss_sub[2] == 0);
    CHECK(miss_tr[1] == 0 && miss_tr[2] == 0);
    CHECK(luhn_tr_other == 0);
    printf("  substitutions missed L/D/V = %u/%u/%u, transpositions missed L/D/V = %u/%u/%u "
           "(Luhn misses are all 09<->90)\n",
           miss_sub[0], miss_sub[1], miss_sub[2], miss_tr[0], miss_tr[1], miss_tr[2]);
}

/* VSS invariant C5. */
static void test_c5(void)
{
    printf("[C5: one number, one network]\n");
    /* Appendix D bases, taken literally. */
    static const char *bases[3] = {"880000000000000", "882000000000000", "884000000000000"};
    for (uint32_t a = 0; a < 3; a++) {
        char n[17];
        memcpy(n, bases[a], 15);
        n[15] = (char) ('0' + cn_network_check_digit((cn_network_t) a, n, 15));
        n[16] = 0;
        bool own = cn_network_check_valid((cn_network_t) a, n, 16);
        bool f1 = cn_network_check_valid((cn_network_t) ((a + 1) % 3), n, 16);
        bool f2 = cn_network_check_valid((cn_network_t) ((a + 2) % 3), n, 16);
        CHECK(own);
        /* cn_pan_valid accepts it exactly when the raw vector cross-fails. */
        CHECK(cn_pan_valid((cn_network_t) a, n, 16) == (!f1 && !f2));
        printf("  Appendix D %-11s %s  own=%d foreign=%d,%d  -> %s\n",
               cn_network_name((cn_network_t) a), n, own, f1, f2,
               (!f1 && !f2) ? "C5 holds" : "C5 FAILS for this raw vector; minting would skip it");
    }
    /* Raw cross-pass rate of a single check digit: about one in ten. This is
     * why exclusivity cannot come from the check digit alone. */
    uint32_t cross[3][3];
    memset(cross, 0, sizeof(cross));
    const uint32_t N = 100000;
    for (uint32_t i = 0; i < N; i++) {
        char n[17];
        rnd_digits(n, 15);
        for (uint32_t a = 0; a < 3; a++) {
            n[15] = (char) ('0' + cn_network_check_digit((cn_network_t) a, n, 15));
            for (uint32_t b = 0; b < 3; b++)
                if (b != a && cn_network_check_valid((cn_network_t) b, n, 16)) cross[a][b]++;
        }
    }
    for (uint32_t a = 0; a < 3; a++)
        for (uint32_t b = 0; b < 3; b++)
            if (a != b) CHECK(cross[a][b] > N / 20 && cross[a][b] < N * 3 / 20);
    printf("  raw single-digit cross-pass (per 100000): L->D %u L->V %u D->L %u D->V %u "
           "V->L %u V->D %u\n",
           cross[0][1], cross[0][2], cross[1][0], cross[1][2], cross[2][0], cross[2][1]);
    /* The enforced rule: no string is valid for two networks. Random strings
     * with each prefix, forced to pass their own check. */
    uint32_t multi = 0, accepted = 0, wrong_prefix = 0;
    for (uint32_t i = 0; i < 300000; i++) {
        char n[17];
        rnd_digits(n, 16);
        uint32_t a = i % 3;
        memcpy(n, cn_network_prefix((cn_network_t) a), 3);
        n[15] = (char) ('0' + cn_network_check_digit((cn_network_t) a, n, 15));
        uint32_t c = 0;
        for (uint32_t b = 0; b < 3; b++) c += cn_pan_valid((cn_network_t) b, n, 16) ? 1u : 0u;
        if (c > 1) multi++;
        accepted += c;
        /* prefix mismatch is never accepted */
        for (uint32_t b = 0; b < 3; b++)
            if (b != a && cn_pan_valid((cn_network_t) b, n, 16)) wrong_prefix++;
    }
    CHECK(multi == 0 && wrong_prefix == 0);
    printf("  300000 own-check numbers: %u accepted under strict C5, %u valid for >1 network\n",
           accepted, multi);
    CHECK(cn_pan_network("8800000000000000", 15) == CN_NET_NONE);
    CHECK(cn_pan_network("4111111111111111", 16) == CN_NET_NONE);
}

/* ===================================================================== */
/* Keys and fixtures */

static uint8_t pkA[CN_PK_BYTES], skA[CN_SK_BYTES], pkB[CN_PK_BYTES], skB[CN_SK_BYTES];
static uint8_t g_sig[CN_SIG_BYTES];
static cn_issuer_t g_iss, g_iss2, g_snap;

static void keys(void)
{
    uint8_t s[32];
    for (int i = 0; i < 32; i++) s[i] = (uint8_t) (i * 7 + 1);
    pq_mldsa65_keygen(s, pkA, skA);
    s[0] ^= 0xFF;
    pq_mldsa65_keygen(s, pkB, skB);
}

static void entropy(uint8_t e[32], uint32_t k)
{
    for (int i = 0; i < 32; i++) e[i] = (uint8_t) (k * 31u + (uint32_t) i * 17u);
}

/* 2026-10-09 12:00:00Z */
static cn_time_t T0(void)
{
    cn_civil_t c = {2026, 10, 9, 12, 0, 0};
    cn_time_t t = 0;
    cn_time_from_civil(&c, &t);
    return t;
}

static void mkreq(cn_auth_req_t *r, const char *pan, uint64_t amt, uint32_t form, uint32_t atc,
                  cn_time_t t)
{
    memset(r, 0, sizeof(*r));
    memcpy(r->pan, pan, 16);
    r->amount_minor = amt;
    r->currency = CN_RAIL_DEBIT;
    r->form = form;
    r->atc = atc;
    r->un[0] = 0xDE;
    r->un[1] = 0xAD;
    r->un[2] = (uint8_t) atc;
    r->un[3] = 0x42;
    r->time = t;
    r->stan = 100000u + atc;
    memcpy(r->terminal_id, "TERM0001", 8);
    memcpy(r->merchant_id, "MERCHANT0000042", 15);
}

/* Sign with sk and authorize; returns the auth record. */
static const cn_auth_t *auth_with(cn_issuer_t *iss, const cn_auth_req_t *r, const uint8_t *sk)
{
    uint32_t ai = 0;
    cn_holder_sign(sk, r, NULL, g_sig);
    if (cn_authorize(iss, r, g_sig, &ai) != CN_OK) return NULL;
    return &iss->auths[ai];
}

static void test_calendar(void)
{
    printf("[calendar]\n");
    cn_civil_t c;
    cn_civil_from_time(0, &c);
    CHECK(c.year == 2000 && c.month == 1 && c.day == 1 && c.hour == 0);
    cn_civil_t in = {2024, 2, 29, 23, 59, 58};
    cn_time_t t;
    CHECK(cn_time_from_civil(&in, &t));
    cn_civil_from_time(t, &c);
    CHECK(c.year == 2024 && c.month == 2 && c.day == 29 && c.hour == 23 && c.minute == 59 &&
          c.second == 58);
    cn_civil_t bad = {2023, 2, 29, 0, 0, 0};
    CHECK(!cn_time_from_civil(&bad, &t));
    /* Round trip every day for 100 years. */
    uint32_t bad_days = 0;
    for (uint32_t d = 0; d < 36525; d++) {
        cn_civil_from_time(d * CN_SECS_PER_DAY + 3661, &c);
        if (!cn_time_from_civil(&c, &t) || t != d * CN_SECS_PER_DAY + 3661) bad_days++;
    }
    CHECK(bad_days == 0);
}

static void test_config_and_fees(void)
{
    printf("[configuration and fees: no interest can be expressed]\n");
    cn_issuer_cfg_t cfg;
    cn_cfg_default(&cfg, CN_NET_DRAGON);
    CHECK(cn_cfg_validate(&cfg) == CN_OK);
    CHECK(cfg.merchant_fee.kind == CN_FEE_NONE && cfg.merchant_fee.amount_minor == 0);
    cn_issuer_cfg_t c2 = cfg;
    c2.per_txn_limit[CN_FORM_SOCIAL] = 1;
    CHECK(cn_cfg_validate(&c2) == CN_ERR_CONFIG); /* inalienable form */
    c2 = cfg;
    c2.per_cycle_limit[CN_FORM_GOVERNANCE] = 5;
    CHECK(cn_cfg_validate(&c2) == CN_ERR_CONFIG);
    c2 = cfg;
    c2.per_txn_limit[CN_FORM_FINANCIAL] = c2.per_cycle_limit[CN_FORM_FINANCIAL] + 1;
    CHECK(cn_cfg_validate(&c2) == CN_ERR_CONFIG);
    c2 = cfg;
    memcpy(c2.issuer_code, "0A1", 4);
    CHECK(cn_cfg_validate(&c2) == CN_ERR_CONFIG);
    c2 = cfg;
    c2.cycle_days = 3;
    CHECK(cn_cfg_validate(&c2) == CN_ERR_CONFIG);
    c2 = cfg;
    c2.grace_days = 0;
    CHECK(cn_cfg_validate(&c2) == CN_ERR_CONFIG);
    c2 = cfg;
    c2.network = 7;
    CHECK(cn_cfg_validate(&c2) == CN_ERR_CONFIG);

    /* The fee kind is a closed set: only NONE and FLAT_PER_TXN validate.
     * Any other value someone might use to smuggle in a "percentage" or a
     * "per-day" charge is rejected by both the fee and the config check. */
    uint32_t accepted = 0;
    for (uint32_t k = 0; k < 4096; k++) {
        cn_fee_t f = {k, k == 0 ? 0 : 100};
        if (cn_fee_validate(&f)) accepted++;
        c2 = cfg;
        c2.merchant_fee = f;
        if (k > 1) CHECK(cn_cfg_validate(&c2) == CN_ERR_CONFIG);
    }
    CHECK(accepted == 2);
    cn_fee_t f0 = {CN_FEE_NONE, 1};
    CHECK(!cn_fee_validate(&f0));
    cn_fee_t fbig = {CN_FEE_FLAT_PER_TXN, CN_FEE_MAX_MINOR + 1};
    CHECK(!cn_fee_validate(&fbig));

    /* A flat fee does not scale with the charge (so it is not a percentage),
     * and cn_fee_for_charge has no balance or time input at all (so it can
     * not accrue). Prove the first; the second is its signature. */
    cn_fee_t flat = {CN_FEE_FLAT_PER_TXN, 25};
    CHECK(cn_fee_for_charge(&flat, 26) == 25);
    CHECK(cn_fee_for_charge(&flat, 1000) == 25);
    CHECK(cn_fee_for_charge(&flat, 999999999999ull) == 25);
    CHECK(cn_fee_for_charge(&flat, 25) == 0); /* waived: payout never negative */
    CHECK(cn_fee_for_charge(&flat, 1) == 0);
    cn_fee_t none = {CN_FEE_NONE, 0};
    CHECK(cn_fee_for_charge(&none, 1000000) == 0);
}

static void test_lifecycle(void)
{
    printf("[charge card lifecycle with ML-DSA-65 authorization]\n");
    cn_issuer_cfg_t cfg;
    cn_cfg_default(&cfg, CN_NET_PHOENIX);
    memcpy(cfg.issuer_code, "042", 4);
    for (int i = 0; i < 32; i++) cfg.issuer_seed[i] = (uint8_t) (0xA0 + i);
    cfg.merchant_fee.kind = CN_FEE_FLAT_PER_TXN;
    cfg.merchant_fee.amount_minor = 25;
    CHECK(cn_issuer_init(&g_iss, &cfg) == CN_OK);
    cn_time_t t0 = T0();
    uint32_t acct, card, other;
    uint8_t e[32];
    CHECK(cn_open_account(&g_iss, t0, &acct) == CN_OK);
    entropy(e, 1);
    CHECK(cn_issue_card(&g_iss, acct, CN_FORM_SOCIAL, pkA, e, t0, &card) == CN_ERR_FORM);
    CHECK(cn_issue_card(&g_iss, acct, CN_FORM_FINANCIAL, pkA, e, t0, &card) == CN_OK);
    const cn_card_t *c = &g_iss.cards[card];
    CHECK(cn_pan_valid(CN_NET_PHOENIX, c->pan, 16));
    CHECK(cn_pan_network(c->pan, 16) == CN_NET_PHOENIX);
    CHECK(memcmp(c->pan, "882042", 6) == 0);
    CHECK(c->rail_debit == 846 && c->rail_credit == 810 && c->rail_equity == 888);
    CHECK(c->exp_year == 2029 && c->exp_month == 10);
    printf("  issued Phoenix PAN %s exp %u-%02u\n", c->pan, c->exp_year, c->exp_month);
    entropy(e, 2);
    CHECK(cn_issue_card(&g_iss, acct, CN_FORM_MATERIAL, pkB, e, t0, &other) == CN_OK);

    cn_auth_req_t r;
    const cn_auth_t *au;
    mkreq(&r, c->pan, 10000, CN_FORM_FINANCIAL, 1, t0);
    au = auth_with(&g_iss, &r, skA);
    CHECK(au && au->state == CN_AUTH_APPROVED && strcmp(au->rc, "00") == 0);
    CHECK(au && strlen(au->approval) == 6);
    uint32_t a1 = (uint32_t) (au - g_iss.auths);
    CHECK(g_iss.accounts[acct].held == 10000);

    /* replay of the same ATC, even with a valid signature */
    au = auth_with(&g_iss, &r, skA);
    CHECK(au && au->decline == CN_DECL_REPLAY && strcmp(au->rc, "94") == 0);
    /* signature over different data */
    mkreq(&r, c->pan, 10000, CN_FORM_FINANCIAL, 2, t0);
    cn_holder_sign(skA, &r, NULL, g_sig);
    r.amount_minor = 1;
    uint32_t ai;
    CHECK(cn_authorize(&g_iss, &r, g_sig, &ai) == CN_OK);
    CHECK(g_iss.auths[ai].decline == CN_DECL_BAD_SIG && strcmp(g_iss.auths[ai].rc, "05") == 0);
    /* wrong holder key */
    mkreq(&r, c->pan, 500, CN_FORM_FINANCIAL, 3, t0);
    au = auth_with(&g_iss, &r, skB);
    CHECK(au && au->decline == CN_DECL_BAD_SIG);
    /* a bad signature did not consume the ATC: 3 still works */
    au = auth_with(&g_iss, &r, skA);
    CHECK(au && au->state == CN_AUTH_APPROVED);
    /* form not on this card */
    mkreq(&r, c->pan, 500, CN_FORM_LIVING, 4, t0);
    au = auth_with(&g_iss, &r, skA);
    CHECK(au && au->decline == CN_DECL_FORM && strcmp(au->rc, "57") == 0);
    /* currency other than rail 846 */
    mkreq(&r, c->pan, 500, CN_FORM_FINANCIAL, 5, t0);
    r.currency = 840;
    au = auth_with(&g_iss, &r, skA);
    CHECK(au && au->decline == CN_DECL_CURRENCY);
    /* per-transaction limit */
    mkreq(&r, c->pan, 1000001, CN_FORM_FINANCIAL, 6, t0);
    au = auth_with(&g_iss, &r, skA);
    CHECK(au && au->decline == CN_DECL_TXN_LIMIT && strcmp(au->rc, "61") == 0);
    /* frozen */
    CHECK(cn_set_frozen(&g_iss, card, true) == CN_OK);
    mkreq(&r, c->pan, 500, CN_FORM_FINANCIAL, 7, t0);
    au = auth_with(&g_iss, &r, skA);
    CHECK(au && au->decline == CN_DECL_FROZEN && strcmp(au->rc, "62") == 0);
    CHECK(cn_set_frozen(&g_iss, card, false) == CN_OK);
    /* expired: November 2029 */
    cn_civil_t ex = {2029, 11, 1, 0, 0, 0};
    cn_time_t tex;
    cn_time_from_civil(&ex, &tex);
    mkreq(&r, c->pan, 500, CN_FORM_FINANCIAL, 8, tex);
    au = auth_with(&g_iss, &r, skA);
    CHECK(au && au->decline == CN_DECL_EXPIRED && strcmp(au->rc, "54") == 0);
    /* not a Phoenix number */
    char wrong[17];
    memcpy(wrong, c->pan, 17);
    wrong[15] = (char) ('0' + (wrong[15] - '0' + 1) % 10);
    mkreq(&r, wrong, 500, CN_FORM_FINANCIAL, 9, t0);
    au = auth_with(&g_iss, &r, skA);
    CHECK(au && au->decline == CN_DECL_BAD_PAN);
    /* malformed request: terminal id too short */
    mkreq(&r, c->pan, 500, CN_FORM_FINANCIAL, 10, t0);
    r.terminal_id[3] = 0;
    CHECK(cn_authorize(&g_iss, &r, g_sig, &ai) == CN_OK &&
          g_iss.auths[ai].decline == CN_DECL_FORMAT);

    /* settle the first approval with a flat fee of 25 */
    CHECK(cn_settle(&g_iss, a1) == CN_OK);
    CHECK(cn_settle(&g_iss, a1) == CN_ERR_STATE);
    CHECK(g_iss.n_legs == 3 && g_iss.n_txns == 1);
    CHECK(g_iss.legs[0].side == CN_DR && g_iss.legs[0].rail == 846 &&
          g_iss.legs[0].amount == 10000);
    CHECK(g_iss.legs[1].side == CN_CR && g_iss.legs[1].rail == 810 && g_iss.legs[1].amount == 9975);
    CHECK(g_iss.legs[2].side == CN_CR && g_iss.legs[2].rail == 810 && g_iss.legs[2].amount == 25);
    CHECK(cn_txn_balanced(&g_iss, 0));
    CHECK(g_iss.accounts[acct].unbilled == 10000);
    CHECK(g_iss.receipts[0].equity_rail == 888 && g_iss.receipts[0].fee == 25);

    /* reverse an approved (unsettled) auth: hold released, nothing posted */
    mkreq(&r, c->pan, 700, CN_FORM_FINANCIAL, 11, t0);
    au = auth_with(&g_iss, &r, skA);
    uint32_t a2 = (uint32_t) (au - g_iss.auths);
    uint64_t held = g_iss.accounts[acct].held;
    CHECK(cn_reverse(&g_iss, a2, t0) == CN_OK);
    CHECK(g_iss.accounts[acct].held == held - 700 && g_iss.n_txns == 1);
    /* settle then refund */
    mkreq(&r, c->pan, 2000, CN_FORM_FINANCIAL, 12, t0);
    au = auth_with(&g_iss, &r, skA);
    uint32_t a3 = (uint32_t) (au - g_iss.auths);
    CHECK(cn_settle(&g_iss, a3) == CN_OK);
    CHECK(cn_reverse(&g_iss, a3, t0 + 60) == CN_OK);
    CHECK(g_iss.auths[a3].state == CN_AUTH_REVERSED);
    CHECK(cn_txn_balanced(&g_iss, g_iss.n_txns - 1));
    CHECK(cn_reverse(&g_iss, a3, t0 + 60) == CN_ERR_STATE);

    /* C1 and C2 over everything so far */
    CHECK(cn_ledger_balanced(&g_iss));
    CHECK(cn_receipts_verify(&g_iss));
    /* Appendix D T2: a 1000 / 999 posting fails C1 */
    memcpy(&g_snap, &g_iss, sizeof(g_iss));
    g_snap.legs[1].amount -= 1;
    CHECK(!cn_txn_balanced(&g_snap, 0) && !cn_ledger_balanced(&g_snap));
    memcpy(&g_snap, &g_iss, sizeof(g_iss));
    g_snap.receipts[0].amount += 1;
    CHECK(!cn_receipts_verify(&g_snap));

    /* settle the remaining approved auth (ATC 3, 500) */
    for (uint32_t i = 0; i < g_iss.n_auths; i++)
        if (g_iss.auths[i].state == CN_AUTH_APPROVED) CHECK(cn_settle(&g_iss, i) == CN_OK);
    CHECK(g_iss.accounts[acct].held == 0);
    uint64_t charged = g_iss.accounts[acct].unbilled;
    CHECK(charged == 10500);

    /* ---- statement, pay in full, no interest ---- */
    printf("[statement cycle: paid in full, no interest, no late charge]\n");
    CHECK(cn_advance(&g_iss, t0 + 29 * CN_SECS_PER_DAY) == CN_OK);
    CHECK(g_iss.n_statements == 0);
    CHECK(cn_advance(&g_iss, t0 + 30 * CN_SECS_PER_DAY) == CN_OK);
    CHECK(g_iss.n_statements == 1);
    const cn_statement_t *st = &g_iss.statements[0];
    CHECK(st->amount_due == 10500 && st->new_charges == 10500);
    CHECK(st->due_day == st->close_day + 21);
    CHECK(g_iss.accounts[acct].billed == 10500 && g_iss.accounts[acct].unbilled == 0);
    CHECK(cn_pay(&g_iss, acct, 0, t0) == CN_ERR_AMOUNT);
    CHECK(cn_pay(&g_iss, acct, 10501, t0) == CN_ERR_AMOUNT); /* no credit balance */
    CHECK(cn_pay(&g_iss, acct, 500, t0 + 31 * CN_SECS_PER_DAY) == CN_OK);
    CHECK(g_iss.accounts[acct].billed == 10000);
    /* Run 1000 days past due with no payment. */
    uint32_t legs_before = g_iss.n_legs, rcpt_before = g_iss.n_receipts;
    uint64_t owed_before = cn_account_owed(&g_iss, acct);
    for (uint32_t d = 31; d < 1031; d++)
        CHECK(cn_advance(&g_iss, t0 + d * CN_SECS_PER_DAY) == CN_OK);
    CHECK(g_iss.accounts[acct].state == CN_ACCT_DELINQUENT);
    CHECK(cn_account_owed(&g_iss, acct) == owed_before); /* exactly: no interest, no late fee */
    CHECK(g_iss.n_legs == legs_before && g_iss.n_receipts == rcpt_before);
    printf("  owed after 1000 days unpaid: %llu (was %llu)\n",
           (unsigned long long) cn_account_owed(&g_iss, acct), (unsigned long long) owed_before);
    /* delinquent: spending paused (a different card on the same account) */
    mkreq(&r, g_iss.cards[other].pan, 100, CN_FORM_MATERIAL, 1, t0 + 1030 * CN_SECS_PER_DAY);
    au = auth_with(&g_iss, &r, skB);
    CHECK(au && au->decline == CN_DECL_DELINQUENT && strcmp(au->rc, "05") == 0);
    CHECK(cn_pay(&g_iss, acct, 10000, t0 + 1031 * CN_SECS_PER_DAY) == CN_OK);
    CHECK(g_iss.accounts[acct].state == CN_ACCT_ACTIVE && cn_account_owed(&g_iss, acct) == 0);
    mkreq(&r, g_iss.cards[other].pan, 100, CN_FORM_MATERIAL, 2, t0 + 1031 * CN_SECS_PER_DAY);
    au = auth_with(&g_iss, &r, skB);
    CHECK(au && au->state == CN_AUTH_APPROVED);
    CHECK(cn_ledger_balanced(&g_iss) && cn_receipts_verify(&g_iss));

    /* cycle limit and ceiling */
    printf("[limits per capital form]\n");
    cn_issuer_cfg_t lc;
    cn_cfg_default(&lc, CN_NET_DRAGON);
    lc.per_txn_limit[CN_FORM_KNOWLEDGE] = 600;
    lc.per_cycle_limit[CN_FORM_KNOWLEDGE] = 1000;
    lc.per_txn_limit[CN_FORM_BUILT] = 800;
    lc.per_cycle_limit[CN_FORM_BUILT] = 800;
    lc.account_ceiling = 1500;
    lc.per_cycle_limit[CN_FORM_FINANCIAL] = 1500;
    lc.per_txn_limit[CN_FORM_FINANCIAL] = 1500;
    for (uint32_t f = CN_FORM_MATERIAL; f <= CN_FORM_LIVING; f++) {
        lc.per_txn_limit[f] = 1000;
        lc.per_cycle_limit[f] = 1000;
    }
    CHECK(cn_issuer_init(&g_iss2, &lc) == CN_OK);
    uint32_t a, ck, cb;
    cn_open_account(&g_iss2, t0, &a);
    entropy(e, 9);
    CHECK(cn_issue_card(&g_iss2, a, CN_FORM_KNOWLEDGE, pkA, e, t0, &ck) == CN_OK);
    entropy(e, 10);
    CHECK(cn_issue_card(&g_iss2, a, CN_FORM_BUILT, pkA, e, t0, &cb) == CN_OK);
    CHECK(cn_pan_network(g_iss2.cards[ck].pan, 16) == CN_NET_DRAGON);
    mkreq(&r, g_iss2.cards[ck].pan, 600, CN_FORM_KNOWLEDGE, 1, t0);
    CHECK(auth_with(&g_iss2, &r, skA)->state == CN_AUTH_APPROVED);
    mkreq(&r, g_iss2.cards[ck].pan, 600, CN_FORM_KNOWLEDGE, 2, t0);
    au = auth_with(&g_iss2, &r, skA);
    CHECK(au->decline == CN_DECL_CYCLE_LIMIT && strcmp(au->rc, "51") == 0);
    mkreq(&r, g_iss2.cards[cb].pan, 800, CN_FORM_BUILT, 1, t0);
    au = auth_with(&g_iss2, &r, skA);
    CHECK(au->state == CN_AUTH_APPROVED);
    mkreq(&r, g_iss2.cards[ck].pan, 400, CN_FORM_KNOWLEDGE, 3, t0);
    au = auth_with(&g_iss2, &r, skA);
    CHECK(au->decline == CN_DECL_CEILING); /* 600 + 800 + 400 > 1500 */
    /* new cycle resets per-form spend but not the ceiling */
    cn_advance(&g_iss2, t0 + 30 * CN_SECS_PER_DAY);
    CHECK(g_iss2.accounts[a].cycle_spent[CN_FORM_KNOWLEDGE] == 0);
}

static void test_reissue(void)
{
    printf("[self-service replacement]\n");
    cn_issuer_cfg_t cfg;
    cn_cfg_default(&cfg, CN_NET_THUNDERBIRD);
    for (int i = 0; i < 32; i++) cfg.issuer_seed[i] = (uint8_t) i;
    CHECK(cn_issuer_init(&g_iss, &cfg) == CN_OK);
    cn_time_t t0 = T0();
    uint32_t acct, card, nc;
    uint8_t e[32];
    cn_open_account(&g_iss, t0, &acct);
    entropy(e, 100);
    CHECK(cn_issue_card(&g_iss, acct, CN_FORM_LIVING, pkA, e, t0, &card) == CN_OK);
    char oldpan[17];
    memcpy(oldpan, g_iss.cards[card].pan, 17);
    CHECK(cn_pan_network(oldpan, 16) == CN_NET_THUNDERBIRD && memcmp(oldpan, "884", 3) == 0);

    /* determinism: the same state and entropy give the same PAN */
    memcpy(&g_snap, &g_iss, sizeof(g_iss));
    entropy(e, 101);
    CHECK(cn_reissue_card(&g_iss, card, NULL, e, t0, &nc) == CN_OK);
    uint32_t nc2;
    CHECK(cn_reissue_card(&g_snap, card, NULL, e, t0, &nc2) == CN_OK);
    CHECK(nc == nc2 && memcmp(g_iss.cards[nc].pan, g_snap.cards[nc2].pan, 17) == 0);
    CHECK(strcmp(g_iss.cards[nc].pan, oldpan) != 0);
    CHECK(g_iss.cards[card].state == CN_CARD_REVOKED && g_iss.cards[nc].replaces == card);
    CHECK(g_iss.cards[nc].account == acct && g_iss.cards[nc].form == CN_FORM_LIVING);
    printf("  %s -> %s\n", oldpan, g_iss.cards[nc].pan);

    cn_auth_req_t r;
    mkreq(&r, oldpan, 100, CN_FORM_LIVING, 1, t0);
    const cn_auth_t *au = auth_with(&g_iss, &r, skA);
    CHECK(au->decline == CN_DECL_REVOKED && strcmp(au->rc, "14") == 0);
    mkreq(&r, g_iss.cards[nc].pan, 100, CN_FORM_LIVING, 1, t0);
    CHECK(auth_with(&g_iss, &r, skA)->state == CN_AUTH_APPROVED);
    CHECK(cn_reissue_card(&g_iss, card, NULL, e, t0, &nc2) == CN_ERR_STATE); /* already revoked */

    /* new key on replacement */
    entropy(e, 102);
    uint32_t nk;
    CHECK(cn_reissue_card(&g_iss, nc, pkB, e, t0, &nk) == CN_OK);
    mkreq(&r, g_iss.cards[nk].pan, 100, CN_FORM_LIVING, 1, t0);
    CHECK(auth_with(&g_iss, &r, skA)->decline == CN_DECL_BAD_SIG);
    mkreq(&r, g_iss.cards[nk].pan, 100, CN_FORM_LIVING, 2, t0);
    CHECK(auth_with(&g_iss, &r, skB)->state == CN_AUTH_APPROVED);

    /* atomicity: with every slot holding a live card, reissue fails and
     * leaves the issuer byte-for-byte unchanged */
    cn_issuer_t *full = &g_iss2;
    CHECK(cn_issuer_init(full, &cfg) == CN_OK);
    cn_open_account(full, t0, &acct);
    for (uint32_t i = 0; i < CN_MAX_CARDS; i++) {
        entropy(e, 200 + i);
        CHECK(cn_issue_card(full, acct, CN_FORM_FINANCIAL, pkA, e, t0, &card) == CN_OK);
    }
    entropy(e, 999);
    CHECK(cn_issue_card(full, acct, CN_FORM_FINANCIAL, pkA, e, t0, &card) == CN_ERR_FULL);
    memcpy(&g_snap, full, sizeof(*full));
    CHECK(cn_reissue_card(full, 3, NULL, e, t0, &nc) == CN_ERR_FULL);
    CHECK(memcmp(&g_snap, full, sizeof(*full)) == 0);
    CHECK(full->cards[3].state == CN_CARD_ACTIVE);
    /* freeing one slot by an earlier replacement lets later ones recycle it */
    printf("[C5 over generated PANs: every network, many reissues]\n");
    uint32_t bad = 0, total = 0, dup = 0;
    for (uint32_t net = 0; net < 3; net++) {
        cn_cfg_default(&cfg, (cn_network_t) net);
        for (int i = 0; i < 32; i++) cfg.issuer_seed[i] = (uint8_t) (net * 50 + (uint32_t) i);
        CHECK(cn_issuer_init(&g_iss2, &cfg) == CN_OK);
        cn_open_account(&g_iss2, t0, &acct);
        entropy(e, 7000 + net);
        CHECK(cn_issue_card(&g_iss2, acct, CN_FORM_FINANCIAL, pkA, e, t0, &card) == CN_OK);
        for (uint32_t k = 0; k < 2000; k++) {
            entropy(e, 10000 + k * 3 + net);
            if (cn_reissue_card(&g_iss2, card, NULL, e, t0, &nc) != CN_OK) {
                bad++;
                break;
            }
            const char *p = g_iss2.cards[nc].pan;
            total++;
            bool own = cn_network_check_valid((cn_network_t) net, p, 16);
            bool f1 = cn_network_check_valid((cn_network_t) ((net + 1) % 3), p, 16);
            bool f2 = cn_network_check_valid((cn_network_t) ((net + 2) % 3), p, 16);
            if (!own || f1 || f2 || cn_pan_network(p, 16) != (cn_network_t) net) bad++;
            for (uint32_t j = 0; j < CN_MAX_CARDS; j++)
                if (j != nc && g_iss2.cards[j].used && memcmp(g_iss2.cards[j].pan, p, 16) == 0)
                    dup++;
            card = nc;
        }
    }
    CHECK(bad == 0 && dup == 0 && total == 6000);
    printf("  %u minted PANs: %u violate C5, %u duplicates in table\n", total, bad, dup);
}

/* ===================================================================== */
static void test_iso8583(void)
{
    printf("[ISO 8583:1987]\n");
    static cn8583_msg_t m, m2;
    static uint8_t wire[CN8583_MAX_WIRE], wire2[CN8583_MAX_WIRE];
    uint32_t n, n2;
    cn_auth_req_t r, back;
    /* a Dragon PAN minted by an issuer */
    cn_issuer_cfg_t cfg;
    cn_cfg_default(&cfg, CN_NET_DRAGON);
    CHECK(cn_issuer_init(&g_iss, &cfg) == CN_OK);
    uint32_t acct, card;
    uint8_t e[32];
    cn_time_t t0 = T0();
    cn_open_account(&g_iss, t0, &acct);
    entropy(e, 5);
    CHECK(cn_issue_card(&g_iss, acct, CN_FORM_FINANCIAL, pkA, e, t0, &card) == CN_OK);
    mkreq(&r, g_iss.cards[card].pan, 123456, CN_FORM_FINANCIAL, 7, t0 + 3723);
    cn_holder_sign(skA, &r, NULL, g_sig);

    CHECK(cn8583_from_auth_req(&m, 100, &r, g_sig) == CN8583_OK);
    CHECK(cn8583_check_mandatory(&m) == CN8583_OK);
    CHECK(cn8583_pack(&m, wire, sizeof(wire), &n) == CN8583_OK);
    CHECK(memcmp(wire, "0100", 4) == 0);
    static const uint8_t bm[8] = {0x72, 0x38, 0x00, 0x00, 0x00, 0xC0, 0x80, 0x1A};
    CHECK(memcmp(wire + 4, bm, 8) == 0); /* hand-computed primary bitmap */
    CHECK(memcmp(wire + 12, "16", 2) == 0 && memcmp(wire + 14, r.pan, 16) == 0); /* LLVAR PAN */
    CHECK(memcmp(wire + 30, "000000", 6) == 0 && memcmp(wire + 36, "000000123456", 12) == 0);
    CHECK(memcmp(wire + 48, "1009130203", 10) == 0); /* field 7 MMDDhhmmss */
    CHECK(cn8583_unpack(&m2, wire, n) == CN8583_OK);
    CHECK(cn8583_pack(&m2, wire2, sizeof(wire2), &n2) == CN8583_OK);
    CHECK(n == n2 && memcmp(wire, wire2, n) == 0);
    CHECK(cn8583_to_auth_req(&m2, 2026, &back) == CN8583_OK);
    CHECK(strcmp(back.pan, r.pan) == 0 && back.amount_minor == r.amount_minor);
    CHECK(back.time == r.time && back.stan == r.stan && back.atc == r.atc && back.form == r.form &&
          back.currency == 846);
    CHECK(strcmp(back.terminal_id, r.terminal_id) == 0 &&
          strcmp(back.merchant_id, r.merchant_id) == 0);
    cn_vss_meta_t v;
    CHECK(cn8583_get_vss(&m2, &v) == CN8583_OK);
    uint8_t dg[32];
    sha3_256(g_sig, CN_SIG_BYTES, dg);
    CHECK(v.network == CN_NET_DRAGON && v.rail_dr == 846 && v.rail_cr == 810 && v.rail_eq == 888 &&
          memcmp(v.sig_digest, dg, 32) == 0);
    /* the 8583 view plus the terminal UN and the signature authorizes */
    memcpy(back.un, r.un, 4);
    uint32_t ai;
    CHECK(cn_authorize(&g_iss, &back, g_sig, &ai) == CN_OK);
    CHECK(g_iss.auths[ai].state == CN_AUTH_APPROVED);

    /* response 0110 */
    CHECK(cn8583_response_for(&m, 110, &g_iss.auths[ai]) == CN8583_OK);
    CHECK(cn8583_check_mandatory(&m) == CN8583_OK);
    CHECK(cn8583_pack(&m, wire, sizeof(wire), &n) == CN8583_OK);
    CHECK(cn8583_unpack(&m2, wire, n) == CN8583_OK);
    const char *d;
    uint32_t dl;
    CHECK(cn8583_get(&m2, 39, &d, &dl) == CN8583_OK && dl == 2 && memcmp(d, "00", 2) == 0);
    CHECK(cn8583_get(&m2, 38, &d, &dl) == CN8583_OK && memcmp(d, g_iss.auths[ai].approval, 6) == 0);

    /* every MTI round-trips; 0400/0410 carry field 90 in the secondary bitmap */
    static const uint32_t mtis[6] = {100, 110, 200, 210, 400, 410};
    for (uint32_t i = 0; i < 6; i++) {
        CHECK(cn8583_init(&m, mtis[i]) == CN8583_OK);
        cn8583_set(&m, 2, r.pan, 16);
        cn8583_set_num(&m, 3, 0);
        cn8583_set_num(&m, 4, 42);
        cn8583_set_num(&m, 7, 1009120000ull);
        cn8583_set_num(&m, 11, 123);
        cn8583_set_num(&m, 12, 120000);
        cn8583_set_num(&m, 13, 1009);
        cn8583_set(&m, 37, "000000000123", 12);
        cn8583_set(&m, 38, "A1B2C3", 6);
        cn8583_set(&m, 39, "00", 2);
        cn8583_set(&m, 41, "TERM0001", 8);
        cn8583_set(&m, 42, "MERCHANT0000042", 15);
        cn8583_set_num(&m, 49, 846);
        CHECK(cn8583_put_vss(&m, &v) == CN8583_OK);
        if (mtis[i] >= 400)
            CHECK(cn8583_set(&m, 90, "010000012310091200000000000000000000000000", 42) ==
                  CN8583_OK);
        CHECK(cn8583_check_mandatory(&m) == CN8583_OK);
        CHECK(cn8583_pack(&m, wire, sizeof(wire), &n) == CN8583_OK);
        CHECK(((wire[4] & 0x80) != 0) == (mtis[i] >= 400));
        CHECK(cn8583_unpack(&m2, wire, n) == CN8583_OK && m2.mti == mtis[i]);
        CHECK(cn8583_pack(&m2, wire2, sizeof(wire2), &n2) == CN8583_OK);
        CHECK(n == n2 && memcmp(wire, wire2, n) == 0);
        for (uint32_t f = 2; f <= 128; f++) {
            CHECK(cn8583_has(&m, f) == cn8583_has(&m2, f));
            if (!cn8583_has(&m, f)) continue;
            const char *x, *y;
            uint32_t xl, yl;
            cn8583_get(&m, f, &x, &xl);
            cn8583_get(&m2, f, &y, &yl);
            CHECK(xl == yl && memcmp(x, y, xl) == 0);
        }
        /* truncation at every length is rejected, never read past */
        uint32_t ok_trunc = 0;
        for (uint32_t k = 0; k < n; k++)
            if (cn8583_unpack(&m2, wire, k) == CN8583_OK) ok_trunc++;
        CHECK(ok_trunc == 0);
    }
    CHECK(cn8583_check_mandatory(&m) == CN8583_OK);

    /* format and structure rejection */
    CHECK(cn8583_init(&m, 300) == CN8583_ERR_MTI);
    cn8583_init(&m, 100);
    CHECK(cn8583_set(&m, 4, "00000000012A", 12) == CN8583_ERR_FORMAT);
    CHECK(cn8583_set(&m, 41, "TERM1", 5) == CN8583_ERR_FORMAT);
    CHECK(cn8583_set(&m, 55, "x", 1) == CN8583_ERR_FIELD);
    CHECK(cn8583_set(&m, 2, "12345678901234567890", 20) == CN8583_ERR_FORMAT);
    CHECK(cn8583_set(&m, 60, "bad\x01", 4) == CN8583_ERR_FORMAT);
    CHECK(cn8583_set_num(&m, 11, 1000000) == CN8583_ERR_FORMAT);
    CHECK(cn8583_check_mandatory(&m) == CN8583_ERR_MISSING);
    /* a bitmap naming an unsupported field (55) */
    uint8_t raw[32] = {'0', '1', '0', '0', 0, 0, 0, 0, 0, 0, 0x20, 0};
    CHECK(cn8583_unpack(&m2, raw, 13) == CN8583_ERR_FIELD);
    /* trailing garbage */
    cn8583_init(&m, 100);
    cn8583_set_num(&m, 3, 0);
    cn8583_pack(&m, wire, sizeof(wire), &n);
    wire[n] = '9';
    CHECK(cn8583_unpack(&m2, wire, n + 1) == CN8583_ERR_FORMAT);
    /* output buffer too small */
    CHECK(cn8583_pack(&m, wire, n - 1, &n2) == CN8583_ERR_SPACE);
    /* malformed VSS 60 */
    cn8583_init(&m, 100);
    cn8583_set(&m, 60, "VSS1;NET=X;FORM=4;DR=846;CR=810;EQ=888", 38);
    cn8583_set(&m, 61, "RCPT=0;ATC=1", 12);
    CHECK(cn8583_get_vss(&m, &v) == CN8583_ERR_FORMAT);
    cn8583_set(&m, 60, "VSS1;NET=D;FORM=04;DR=846;CR=810;EQ=888", 39);
    CHECK(cn8583_get_vss(&m, &v) == CN8583_ERR_FORMAT);
}

/* ===================================================================== */
static void test_tlv(void)
{
    printf("[BER-TLV]\n");
    uint8_t buf[600];
    cn_tlv_writer_t w;
    cn_tlv_writer_init(&w, buf, sizeof(buf));
    uint8_t amt[6];
    CHECK(cn_bcd_from_u64(1000, 6, amt) == CN_TLV_OK);
    CHECK(cn_tlv_put(&w, 0x9F02, amt, 6) == CN_TLV_OK);
    static const uint8_t exp1[9] = {0x9F, 0x02, 0x06, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00};
    CHECK(w.len == 9 && memcmp(buf, exp1, 9) == 0);
    uint8_t pan[8];
    uint32_t pl;
    CHECK(cn_bcd_from_digits("4111111111111111", 16, pan, 8, &pl) == CN_TLV_OK && pl == 8);
    cn_tlv_writer_init(&w, buf, sizeof(buf));
    cn_tlv_put(&w, 0x5A, pan, 8);
    static const uint8_t exp2[10] = {0x5A, 0x08, 0x41, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11};
    CHECK(w.len == 10 && memcmp(buf, exp2, 10) == 0);
    CHECK(cn_bcd_from_digits("12345", 5, pan, 8, &pl) == CN_TLV_OK && pl == 3 && pan[2] == 0x5F);
    uint64_t x;
    CHECK(cn_bcd_to_u64(amt, 6, &x) == CN_TLV_OK && x == 1000);
    uint8_t badbcd[2] = {0x1A, 0x00};
    CHECK(cn_bcd_to_u64(badbcd, 2, &x) == CN_TLV_ERR_VALUE);
    /* length forms */
    static uint8_t big[300];
    cn_tlv_writer_init(&w, buf, sizeof(buf));
    cn_tlv_put(&w, 0x95, big, 127);
    CHECK(buf[1] == 127 && w.len == 129);
    cn_tlv_writer_init(&w, buf, sizeof(buf));
    cn_tlv_put(&w, 0x95, big, 128);
    CHECK(buf[1] == 0x81 && buf[2] == 0x80 && w.len == 131);
    cn_tlv_writer_init(&w, buf, sizeof(buf));
    cn_tlv_put(&w, 0xDF8101, big, 256);
    CHECK(buf[0] == 0xDF && buf[1] == 0x81 && buf[2] == 0x01 && buf[3] == 0x82 && buf[4] == 0x01 &&
          buf[5] == 0x00 && w.len == 262);
    uint32_t pos = 0, t, l;
    const uint8_t *v;
    CHECK(cn_tlv_next(buf, w.len, &pos, &t, &v, &l) == CN_TLV_OK && t == 0xDF8101 && l == 256 &&
          pos == 262);
    /* writer overflow is sticky */
    uint8_t small[4];
    cn_tlv_writer_init(&w, small, 4);
    CHECK(cn_tlv_put(&w, 0x9F02, amt, 6) == CN_TLV_ERR_SPACE);
    CHECK(cn_tlv_put(&w, 0x82, amt, 0) == CN_TLV_ERR_SPACE);
    CHECK(!cn_tlv_tag_valid(0x1F) && !cn_tlv_tag_valid(0x9F) && !cn_tlv_tag_valid(0x5F80) &&
          cn_tlv_tag_valid(0x9F37) && cn_tlv_tag_valid(0x82) && !cn_tlv_tag_valid(0xDF0101));
    /* malformed inputs */
    static const uint8_t indef[] = {0x70, 0x80, 0x00, 0x00};
    static const uint8_t len83[] = {0x82, 0x83, 0x00, 0x00, 0x02, 1, 2};
    static const uint8_t nonmin[] = {0x82, 0x81, 0x02, 1, 2};
    static const uint8_t trunc[] = {0x9F, 0x02, 0x06, 0x00, 0x00};
    static const uint8_t tag4[] = {0xDF, 0x81, 0x81, 0x01, 0x01, 0x00};
    static const uint8_t tag0[] = {0x00, 0x01, 0x00};
    pos = 0;
    CHECK(cn_tlv_next(indef, 4, &pos, &t, &v, &l) == CN_TLV_ERR_LEN);
    pos = 0;
    CHECK(cn_tlv_next(len83, 7, &pos, &t, &v, &l) == CN_TLV_ERR_LEN);
    pos = 0;
    CHECK(cn_tlv_next(nonmin, 5, &pos, &t, &v, &l) == CN_TLV_ERR_LEN);
    pos = 0;
    CHECK(cn_tlv_next(trunc, 5, &pos, &t, &v, &l) == CN_TLV_ERR_LEN);
    pos = 0;
    CHECK(cn_tlv_next(tag4, 6, &pos, &t, &v, &l) == CN_TLV_ERR_TAG);
    pos = 0;
    CHECK(cn_tlv_next(tag0, 3, &pos, &t, &v, &l) == CN_TLV_ERR_TAG);
    /* nested find: 77 { 9F27 , 70 { 9F36 } } */
    uint8_t in1[16], in2[32];
    cn_tlv_writer_t w1, w2;
    uint8_t atc[2] = {0x00, 0x2A}, cid = 0x80;
    cn_tlv_writer_init(&w1, in1, sizeof(in1));
    cn_tlv_put(&w1, 0x9F36, atc, 2);
    cn_tlv_writer_init(&w2, in2, sizeof(in2));
    cn_tlv_put(&w2, 0x9F27, &cid, 1);
    cn_tlv_put(&w2, 0x70, in1, w1.len);
    cn_tlv_writer_init(&w, buf, sizeof(buf));
    cn_tlv_put(&w, 0x77, in2, w2.len);
    CHECK(cn_tlv_find(buf, w.len, 0x9F36, &v, &l) == CN_TLV_OK && l == 2 && v[1] == 0x2A);
    CHECK(cn_tlv_find(buf, w.len, 0x9F27, &v, &l) == CN_TLV_OK && v[0] == 0x80);
    CHECK(cn_tlv_find(buf, w.len, 0x9F26, &v, &l) == CN_TLV_ERR_MISSING);
    /* depth limit: six nested E1 */
    uint8_t a[64], b[64];
    uint32_t al = 0;
    cn_tlv_writer_init(&w, a, sizeof(a));
    cn_tlv_put(&w, 0x9F36, atc, 2);
    al = w.len;
    for (int i = 0; i < 6; i++) {
        cn_tlv_writer_init(&w, b, sizeof(b));
        cn_tlv_put(&w, 0xE1, a, al);
        memcpy(a, b, w.len);
        al = w.len;
    }
    CHECK(cn_tlv_find(a, al, 0x9F36, &v, &l) == CN_TLV_ERR_DEPTH);
}

static void test_emv(void)
{
    printf("[EMV-style container for the networks]\n");
    static uint8_t buf[CN_EMV_AUTH_MAX], sig2[CN_SIG_BYTES];
    uint32_t n;
    cn_issuer_cfg_t cfg;
    cn_cfg_default(&cfg, CN_NET_PHOENIX);
    CHECK(cn_issuer_init(&g_iss, &cfg) == CN_OK);
    uint32_t acct, card;
    uint8_t e[32];
    cn_time_t t0 = T0();
    cn_open_account(&g_iss, t0, &acct);
    entropy(e, 77);
    cn_issue_card(&g_iss, acct, CN_FORM_FINANCIAL, pkA, e, t0, &card);
    const cn_card_t *c = &g_iss.cards[card];
    cn_auth_req_t r, back;
    mkreq(&r, c->pan, 4999, CN_FORM_FINANCIAL, 1, t0 + 45296);
    cn_holder_sign(skA, &r, NULL, g_sig);
    CHECK(cn_emv_encode_auth(&r, c->exp_year, c->exp_month, g_sig, buf, sizeof(buf), &n) ==
          CN_TLV_OK);
    printf("  container: %u bytes\n", n);
    CHECK(buf[0] == 0xE1 && buf[1] == 0x82);
    const uint8_t *v;
    uint32_t l;
    CHECK(cn_tlv_find(buf, n, CN_EMV_EXPIRY, &v, &l) == CN_TLV_OK && l == 3 && v[0] == 0x29 &&
          v[1] == 0x10 && v[2] == 0x31);
    CHECK(cn_tlv_find(buf, n, CN_EMV_AMOUNT, &v, &l) == CN_TLV_OK && l == 6 && v[4] == 0x49 &&
          v[5] == 0x99);
    CHECK(cn_tlv_find(buf, n, CN_EMV_CURRENCY, &v, &l) == CN_TLV_OK && v[0] == 0x08 &&
          v[1] == 0x46);
    CHECK(cn_tlv_find(buf, n, CN_EMV_UN, &v, &l) == CN_TLV_OK && memcmp(v, r.un, 4) == 0);
    CHECK(cn_tlv_find(buf, n, CN_EMV_ATC, &v, &l) == CN_TLV_OK && v[0] == 0 && v[1] == 1);
    CHECK(cn_tlv_find(buf, n, CN_EMV_AID, &v, &l) == CN_TLV_OK && v[0] == 0xF0 &&
          v[6] == CN_NET_PHOENIX);
    CHECK(cn_tlv_find(buf, n, CN_EMV_TXN_TIME, &v, &l) == CN_TLV_OK && v[0] == 0x00 &&
          v[1] == 0x34 && v[2] == 0x56); /* 12:00 + 45296 s = 00:34:56 next day */
    CHECK(cn_emv_decode_auth(buf, n, &back, sig2) == CN_TLV_OK);
    CHECK(memcmp(&back, &r, sizeof(r)) == 0);
    CHECK(memcmp(sig2, g_sig, CN_SIG_BYTES) == 0);
    uint32_t ai;
    CHECK(cn_authorize(&g_iss, &back, sig2, &ai) == CN_OK &&
          g_iss.auths[ai].state == CN_AUTH_APPROVED);
    CHECK(cn_tlv_find(buf, n, CN_EMV_CRYPTOGRAM, &v, &l) == CN_TLV_OK && l == 8 &&
          memcmp(g_iss.auths[ai].cryptogram, v, 8) == 0);

    /* any signature byte flip breaks the 9F26 binding */
    uint32_t sigoff = 0;
    CHECK(cn_tlv_find(buf, n, CN_EMV_ZXV_SIG, &v, &l) == CN_TLV_OK);
    sigoff = (uint32_t) (v - buf);
    buf[sigoff + 100] ^= 1;
    CHECK(cn_emv_decode_auth(buf, n, &back, sig2) == CN_TLV_ERR_VALUE);
    buf[sigoff + 100] ^= 1;
    /* AID naming another network */
    CHECK(cn_tlv_find(buf, n, CN_EMV_AID, &v, &l) == CN_TLV_OK);
    uint32_t aidoff = (uint32_t) (v - buf);
    buf[aidoff + 6] = CN_NET_DRAGON;
    CHECK(cn_emv_decode_auth(buf, n, &back, sig2) == CN_TLV_ERR_VALUE);
    buf[aidoff + 6] = CN_NET_PHOENIX;
    CHECK(cn_emv_decode_auth(buf, n, &back, sig2) == CN_TLV_OK);
    /* truncation anywhere fails */
    uint32_t ok = 0;
    for (uint32_t k = 0; k < n; k++)
        if (cn_emv_decode_auth(buf, k, &back, sig2) == CN_TLV_OK) ok++;
    CHECK(ok == 0);
    /* encoder refuses a too-small buffer and a non-network PAN */
    CHECK(cn_emv_encode_auth(&r, c->exp_year, c->exp_month, g_sig, buf, 100, &n) ==
          CN_TLV_ERR_SPACE);
    memcpy(r.pan, "4111111111111111", 16);
    CHECK(cn_emv_encode_auth(&r, 2029, 10, g_sig, buf, sizeof(buf), &n) == CN_TLV_ERR_VALUE);
}

/* ===================================================================== */
static void test_mobile(void)
{
    printf("[mobile money: wallets, push requests, USSD]\n");
    static cn_mm_hub_t hub;
    char out[256];
    uint64_t mn;
    CHECK(cn_mm_parse_amount("12.50", &mn) && mn == 1250);
    CHECK(cn_mm_parse_amount("12", &mn) && mn == 1200);
    CHECK(cn_mm_parse_amount("0.5", &mn) && mn == 50);
    CHECK(!cn_mm_parse_amount("12.", &mn) && !cn_mm_parse_amount("1.234", &mn) &&
          !cn_mm_parse_amount("", &mn) && !cn_mm_parse_amount("-1", &mn) &&
          !cn_mm_parse_amount("12345678901", &mn) && !cn_mm_parse_amount(".5", &mn));
    CHECK(cn_mm_format_amount(1250, out, sizeof(out)) == 5 && strcmp(out, "12.50") == 0);
    CHECK(cn_mm_format_amount(7, out, sizeof(out)) == 4 && strcmp(out, "0.07") == 0);
    CHECK(cn_mm_format_amount(18446744073709551615ull, out, sizeof(out)) == 21 &&
          strcmp(out, "184467440737095516.15") == 0);
    CHECK(cn_mm_format_amount(1250, out, 5) == 0);

    cn_issuer_cfg_t cfg;
    cn_cfg_default(&cfg, CN_NET_THUNDERBIRD);
    CHECK(cn_issuer_init(&g_iss, &cfg) == CN_OK);
    uint32_t acct, card, w, w2;
    uint8_t e[32], salt[16];
    cn_time_t t0 = T0();
    cn_open_account(&g_iss, t0, &acct);
    entropy(e, 300);
    cn_issue_card(&g_iss, acct, CN_FORM_FINANCIAL, pkA, e, t0, &card);
    for (int i = 0; i < 16; i++) salt[i] = (uint8_t) (i ^ 0x5A);
    CHECK(cn_mm_init(&hub, 120) == CN_MM_OK);
    CHECK(cn_mm_register(&hub, "254700000001", g_iss.cards[card].pan, "1234", salt, &w) ==
          CN_MM_OK);
    CHECK(cn_mm_register(&hub, "254700000001", g_iss.cards[card].pan, "1234", salt, &w2) ==
          CN_MM_ERR_DUP);
    CHECK(cn_mm_register(&hub, "0700000001", g_iss.cards[card].pan, "1234", salt, &w2) ==
          CN_MM_ERR_ARG);
    CHECK(cn_mm_register(&hub, "254700000002", g_iss.cards[card].pan, "12", salt, &w2) ==
          CN_MM_ERR_ARG);
    CHECK(cn_mm_register(&hub, "254700000002", "4111111111111111", "1234", salt, &w2) ==
          CN_MM_ERR_ARG);
    CHECK(memcmp(hub.wallets[w].pin_hash, "1234", 4) != 0);

    /* PIN lockout */
    CHECK(cn_mm_verify_pin(&hub, w, "1234") == CN_MM_OK);
    CHECK(cn_mm_verify_pin(&hub, w, "0000") == CN_MM_ERR_PIN);
    CHECK(cn_mm_verify_pin(&hub, w, "0001") == CN_MM_ERR_PIN);
    CHECK(cn_mm_verify_pin(&hub, w, "0002") == CN_MM_ERR_LOCKED);
    CHECK(cn_mm_verify_pin(&hub, w, "1234") == CN_MM_ERR_LOCKED);
    CHECK(cn_mm_unlock(&hub, w) == CN_MM_OK && cn_mm_verify_pin(&hub, w, "1234") == CN_MM_OK);

    /* push lifecycle */
    uint32_t p1, p2, p3;
    CHECK(cn_mm_push_create(&hub, "254700000001", 5000, "MERCHANT0000042", "TERM0001", "INV-1001",
                            t0, &p1) == CN_MM_OK);
    cn_mm_callback_t cb = {p1, 0, "OPRCPT0001"};
    CHECK(cn_mm_push_callback(&hub, &cb, t0 + 30) == CN_MM_OK);
    CHECK(hub.push[p1].state == CN_PUSH_ACCEPTED);
    CHECK(cn_mm_push_callback(&hub, &cb, t0 + 31) == CN_MM_OK); /* idempotent */
    cn_mm_callback_t cbx = {p1, 17, "OPRCPT0001"};
    CHECK(cn_mm_push_callback(&hub, &cbx, t0 + 32) == CN_MM_ERR_CONFLICT);
    cn_auth_req_t r;
    uint8_t un[4] = {1, 2, 3, 4};
    CHECK(cn_mm_push_to_auth(&hub, p1, CN_FORM_FINANCIAL, 1, un, 1, t0 + 33, &r) == CN_MM_OK);
    CHECK(cn_mm_push_to_auth(&hub, p1, CN_FORM_FINANCIAL, 2, un, 2, t0 + 33, &r) ==
          CN_MM_ERR_STATE);
    CHECK(cn_mm_push_callback(&hub, &cb, t0 + 34) == CN_MM_OK); /* replay after authorization */
    /* the custodial signer signs, the issuer authorizes */
    const cn_auth_t *au = auth_with(&g_iss, &r, skA);
    CHECK(au && au->state == CN_AUTH_APPROVED && au->req.amount_minor == 5000);
    CHECK(cn_mm_push_create(&hub, "254700000001", 100, "MERCHANT0000042", "TERM0001", "INV-1002",
                            t0, &p2) == CN_MM_OK);
    cn_mm_expire(&hub, t0 + 121);
    CHECK(hub.push[p2].state == CN_PUSH_EXPIRED);
    cn_mm_callback_t cb2 = {p2, 0, "LATE"};
    CHECK(cn_mm_push_callback(&hub, &cb2, t0 + 122) == CN_MM_ERR_CONFLICT);
    CHECK(cn_mm_push_create(&hub, "254700000001", 100, "MERCHANT0000042", "TERM0001", "INV-1003",
                            t0, &p3) == CN_MM_OK);
    cn_mm_callback_t cb3 = {p3, 0, "LATE"};
    CHECK(cn_mm_push_callback(&hub, &cb3, t0 + 500) == CN_MM_ERR_STATE); /* past deadline */
    CHECK(cn_mm_push_cancel(&hub, p3) == CN_MM_ERR_STATE);
    CHECK(cn_mm_push_create(&hub, "254799999999", 100, "MERCHANT0000042", "TERM0001", "X", t0,
                            &p3) == CN_MM_ERR_NOT_FOUND);

    /* USSD: pay a merchant */
    cn_ussd_t s;
    cn_mm_action_t act;
    cn_time_t t = t0 + 1000;
    CHECK(cn_ussd_begin(&hub, &s, "254700000001", t, out, sizeof(out)) == CN_MM_OK);
    CHECK(strncmp(out, "CON Thunderbird card ..", 23) == 0 && strlen(out) <= CN_USSD_MAX_TEXT);
    printf("  ---\n%s\n  ---\n", out);
    CHECK(cn_ussd_input(&hub, &s, &g_iss, "9", t, out, sizeof(out), &act) == CN_MM_OK);
    CHECK(strncmp(out, "CON Thunderbird", 15) == 0); /* unknown choice: menu again */
    CHECK(cn_ussd_input(&hub, &s, &g_iss, "1", t, out, sizeof(out), &act) == CN_MM_OK);
    CHECK(cn_ussd_input(&hub, &s, &g_iss, "12", t, out, sizeof(out), &act) == CN_MM_OK);
    CHECK(strstr(out, "4-10 digits") != NULL && s.state == CN_USSD_PAY_TILL);
    CHECK(cn_ussd_input(&hub, &s, &g_iss, "12345", t, out, sizeof(out), &act) == CN_MM_OK);
    CHECK(cn_ussd_input(&hub, &s, &g_iss, "abc", t, out, sizeof(out), &act) == CN_MM_OK);
    CHECK(s.state == CN_USSD_PAY_AMOUNT);
    CHECK(cn_ussd_input(&hub, &s, &g_iss, "12.50", t, out, sizeof(out), &act) == CN_MM_OK);
    CHECK(cn_ussd_input(&hub, &s, &g_iss, "1234", t, out, sizeof(out), &act) == CN_MM_OK);
    CHECK(strstr(out, "Pay 12.50 to till 000000000012345") != NULL &&
          strstr(out, "No interest") != NULL && strlen(out) <= CN_USSD_MAX_TEXT);
    CHECK(cn_ussd_input(&hub, &s, &g_iss, "1", t, out, sizeof(out), &act) == CN_MM_OK);
    CHECK(strncmp(out, "END ", 4) == 0 && act.kind == CN_MM_ACT_PAY && act.amount_minor == 1250 &&
          strcmp(act.merchant_id, "000000000012345") == 0);
    {
        cn_mm_action_t after;
        CHECK(cn_ussd_input(&hub, &s, &g_iss, "1", t, out, sizeof(out), &after) == CN_MM_ERR_STATE);
        CHECK(after.kind == CN_MM_ACT_NONE); /* an ended session yields no second action */
    }
    CHECK(cn_mm_action_to_auth(&hub, &act, "USSDGW01", CN_FORM_FINANCIAL, 2, un, 2, t, &r) ==
          CN_MM_OK);
    au = auth_with(&g_iss, &r, skA);
    CHECK(au && au->state == CN_AUTH_APPROVED);
    for (uint32_t i = 0; i < g_iss.n_auths; i++)
        if (g_iss.auths[i].state == CN_AUTH_APPROVED) cn_settle(&g_iss, i);

    /* USSD: balance */
    cn_advance(&g_iss, t0 + 30 * CN_SECS_PER_DAY);
    t = t0 + 31 * CN_SECS_PER_DAY;
    cn_ussd_begin(&hub, &s, "254700000001", t, out, sizeof(out));
    cn_ussd_input(&hub, &s, &g_iss, "2", t, out, sizeof(out), &act);
    CHECK(cn_ussd_input(&hub, &s, &g_iss, "1234", t, out, sizeof(out), &act) == CN_MM_OK);
    printf("  ---\n%s\n  ---\n", out);
    CHECK(strncmp(out, "END Statement due: 62.50 by 2026-11-29", 38) == 0);
    CHECK(act.kind == CN_MM_ACT_BALANCE && strlen(out) <= CN_USSD_MAX_TEXT);

    /* USSD: wrong PIN ends the session */
    cn_ussd_begin(&hub, &s, "254700000001", t, out, sizeof(out));
    cn_ussd_input(&hub, &s, &g_iss, "3", t, out, sizeof(out), &act);
    CHECK(cn_ussd_input(&hub, &s, &g_iss, "9999", t, out, sizeof(out), &act) == CN_MM_ERR_PIN);
    CHECK(strncmp(out, "END Wrong PIN", 13) == 0 && act.kind == CN_MM_ACT_NONE);
    /* USSD: freeze */
    cn_ussd_begin(&hub, &s, "254700000001", t, out, sizeof(out));
    cn_ussd_input(&hub, &s, &g_iss, "3", t, out, sizeof(out), &act);
    CHECK(cn_ussd_input(&hub, &s, &g_iss, "1234", t, out, sizeof(out), &act) == CN_MM_OK);
    CHECK(act.kind == CN_MM_ACT_FREEZE);
    uint32_t ci;
    CHECK(cn_find_card(&g_iss, hub.wallets[act.wallet].pan, &ci) == CN_OK);
    CHECK(cn_set_frozen(&g_iss, ci, true) == CN_OK);
    mkreq(&r, hub.wallets[w].pan, 100, CN_FORM_FINANCIAL, 3, t);
    CHECK(auth_with(&g_iss, &r, skA)->decline == CN_DECL_FROZEN);
    /* USSD: replace -> host reissues and relinks */
    cn_ussd_begin(&hub, &s, "254700000001", t, out, sizeof(out));
    cn_ussd_input(&hub, &s, &g_iss, "4", t, out, sizeof(out), &act);
    CHECK(cn_ussd_input(&hub, &s, &g_iss, "1234", t, out, sizeof(out), &act) == CN_MM_OK);
    CHECK(act.kind == CN_MM_ACT_REPLACE);
    uint32_t nc;
    entropy(e, 301);
    CHECK(cn_reissue_card(&g_iss, ci, NULL, e, t, &nc) == CN_OK);
    CHECK(cn_mm_relink(&hub, act.wallet, g_iss.cards[nc].pan) == CN_MM_OK);
    CHECK(strcmp(hub.wallets[w].pan, g_iss.cards[nc].pan) == 0);
    mkreq(&r, hub.wallets[w].pan, 100, CN_FORM_FINANCIAL, 1, t);
    CHECK(auth_with(&g_iss, &r, skA)->state == CN_AUTH_APPROVED);

    /* timeout, unknown number, locked wallet */
    cn_ussd_begin(&hub, &s, "254700000001", t, out, sizeof(out));
    CHECK(cn_ussd_input(&hub, &s, &g_iss, "1", t + CN_USSD_TIMEOUT + 1, out, sizeof(out), &act) ==
          CN_MM_ERR_STATE);
    CHECK(strstr(out, "timed out") != NULL);
    CHECK(cn_ussd_begin(&hub, &s, "254711111111", t, out, sizeof(out)) == CN_MM_ERR_NOT_FOUND);
    CHECK(strncmp(out, "END ", 4) == 0);
    cn_mm_verify_pin(&hub, w, "0");
    cn_mm_verify_pin(&hub, w, "0");
    cn_mm_verify_pin(&hub, w, "0");
    CHECK(cn_ussd_begin(&hub, &s, "254700000001", t, out, sizeof(out)) == CN_MM_ERR_LOCKED);
    /* a tiny screen buffer is an error, not an overflow */
    cn_mm_unlock(&hub, w);
    char tiny[8];
    CHECK(cn_ussd_begin(&hub, &s, "254700000001", t, tiny, sizeof(tiny)) == CN_MM_ERR_ARG);
    CHECK(strlen(tiny) < sizeof(tiny));
    CHECK(cn_ledger_balanced(&g_iss) && cn_receipts_verify(&g_iss));
}

int main(void)
{
    printf("=== cardnet: Dragon / Phoenix / Thunderbird charge-card networks ===\n");
    keys();
    test_vectors();
    test_detection();
    test_c5();
    test_calendar();
    test_config_and_fees();
    test_lifecycle();
    test_reissue();
    test_iso8583();
    test_tlv();
    test_emv();
    test_mobile();
    printf("=== cardnet: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
