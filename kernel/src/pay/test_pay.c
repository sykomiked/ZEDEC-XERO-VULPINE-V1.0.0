/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_pay.c — host tests for kernel/src/pay (build with -DTEST_HOST and
 * -fsanitize=address,undefined; libc is used by the TEST only).
 *
 *   test_pay TITHE_REF XSD_DIR TMP_DIR
 *
 * TITHE_REF  output of gen_tithe_ref.py (decimal + isqrt + exact-integer
 *            agreement in Python); every line is checked against
 *            pay_tithe_phi.
 * XSD_DIR    kernel/src/pay/xsd. Every message this module builds is written
 *            to TMP_DIR and validated with `xmllint --noout --schema` via
 *            system(); a missing xmllint counts as a failure, not a skip.
 * TMP_DIR    scratch directory for the generated XML.
 *
 * HONEST LIMITS. Passing these tests means the code does what the tests
 * check. Schema validity is not certification; there is no SWIFT or CIPS
 * connectivity; licences are needed to operate; VFV's store-credit status is
 * a legal question for counsel.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "pay_util.h"
#include "pay_tithe.h"
#include "pay_tables.h"
#include "pay_ledger.h"
#include "pay_roles.h"
#include "pay_iso.h"
#include "pay_crypto.h"
#include "pay_equity.h"
#include "pay_treasury.h"
#include "pay_farm.h"
#include "zcapital.h"

static int g_fail, g_pass;
#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (c)                                                                                     \
            g_pass++;                                                                              \
        else {                                                                                     \
            g_fail++;                                                                              \
            if (g_fail < 60) printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                   \
        }                                                                                          \
    } while (0)

static const char *g_ref, *g_xsd, *g_tmp;

/* deterministic xorshift for the tests */
static uint64_t g_rng = 0x9E3779B97F4A7C15ull;
static uint64_t rnd(void)
{
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 7;
    g_rng ^= g_rng << 17;
    return g_rng;
}
static uint64_t rnd_below(uint64_t n)
{
    return n ? rnd() % n : 0;
}

/* ===================================================================== */
static void test_util(void)
{
    /* isqrt128 against a few exact values */
    CHECK(pay_isqrt128(pay_u128_from(0)) == 0);
    CHECK(pay_isqrt128(pay_u128_from(15)) == 3);
    CHECK(pay_isqrt128(pay_u128_from(16)) == 4);
    pay_u128 m = {UINT64_MAX, UINT64_MAX};
    CHECK(pay_isqrt128(m) == UINT64_MAX);
    for (int i = 0; i < 20000; i++) {
        uint64_t r = rnd();
        pay_u128 sq = pay_mul64(r, r);
        CHECK(pay_isqrt128(sq) == r);
        if (r) {
            pay_u128 one_less = pay_u128_sub(sq, pay_u128_from(1));
            if (pay_isqrt128(one_less) != r - 1) CHECK(0);
        }
    }
    /* udiv128 */
    uint64_t rem;
    pay_u128 q = pay_udiv128_64(pay_mul64(123456789012345ull, 1000003ull), 1000003ull, &rem);
    CHECK(q.hi == 0 && q.lo == 123456789012345ull && rem == 0);
    /* hashes */
    uint8_t h[32];
    char hex[65];
    pay_w w;
    pay_keccak256((const uint8_t *) "", 0, h);
    pay_w_init(&w, hex, sizeof hex);
    pay_w_hex(&w, h, 32);
    pay_w_finish(&w);
    CHECK(strcmp(hex, "c5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470") == 0);
    pay_sha3_256((const uint8_t *) "", 0, h);
    pay_w_init(&w, hex, sizeof hex);
    pay_w_hex(&w, h, 32);
    pay_w_finish(&w);
    CHECK(strcmp(hex, "a7ffc6f8bf1ed76651c14756a061d662f580ff4de43b49fa82d80a4b80f8434a") == 0);
    pay_keccak256((const uint8_t *) "abc", 3, h);
    pay_w_init(&w, hex, sizeof hex);
    pay_w_hex(&w, h, 32);
    pay_w_finish(&w);
    CHECK(strcmp(hex, "4e03657aea45a94fc7d47ba826c8d667c0d1e6e33a64a036ec44f58fa12d6c45") == 0);
    /* UETR */
    uint8_t r16[16] = {0};
    char u[PAY_UETR_LEN + 1];
    pay_uetr_from_random(r16, u);
    CHECK(pay_uetr_valid(u));
    CHECK(!pay_uetr_valid("8a562c67-ca16-48ba-b074-65581be6f00"));
    CHECK(!pay_uetr_valid("8A562C67-CA16-48BA-B074-65581BE6F001"));
    CHECK(!pay_uetr_valid("8a562c67-ca16-38ba-b074-65581be6f001"));
    /* amounts */
    uint64_t un;
    CHECK(pay_parse_amount("123.45", 6, 2, &un) && un == 12345);
    CHECK(pay_parse_amount("7", 1, 2, &un) && un == 700);
    CHECK(!pay_parse_amount("1.234", 5, 2, &un));
    CHECK(!pay_parse_amount("1e3", 3, 2, &un));
    char ab[32];
    pay_w_init(&w, ab, sizeof ab);
    pay_w_amount(&w, 5, 2);
    pay_w_finish(&w);
    CHECK(strcmp(ab, "0.05") == 0);
}

/* ===================================================================== */
static void test_tithe(void)
{
    FILE *f = fopen(g_ref, "r");
    CHECK(f != NULL);
    if (f) {
        unsigned long long a, t;
        long n = 0, bad = 0;
        while (fscanf(f, "%llu %llu", &a, &t) == 2) {
            if (pay_tithe_phi((uint64_t) a) != (uint64_t) t) {
                if (bad < 5) printf("  tithe mismatch a=%llu want %llu\n", a, t);
                bad++;
            }
            n++;
        }
        fclose(f);
        printf("  tithe: %ld reference amounts, %ld mismatches\n", n, bad);
        CHECK(n >= 100000);
        CHECK(bad == 0);
    }
    CHECK(pay_tithe_phi(0) == 0);
    CHECK(pay_tithe_phi(100) == 1);         /* 1.618 */
    CHECK(pay_tithe_phi(10000) == 161);     /* 161.80 */
    CHECK(pay_tithe_phi(1000000) == 16180); /* 16180.33 */

    pay_tithe_policy_t p;
    pay_tithe_result_t r;
    pay_tithe_policy_default(&p);
    CHECK(pay_tithe_compute(&p, PAY_UNIT_MONEY, 1000000, 0, 0, &r) == PAY_TITHE_OK);
    CHECK(r.contribution == 16180 && r.excess == 0 && r.vfv_credit == 0);
    pay_contrib_t c = {PAY_CONTRIB_RATE, {5, 100}, 0}; /* 5% */
    CHECK(pay_tithe_compute(&p, PAY_UNIT_MONEY, 1000000, &c, 0, &r) == PAY_TITHE_OK);
    CHECK(r.contribution == 50000 && r.excess == 50000 - 16180 && r.vfv_credit == 33820);
    c.mode = PAY_CONTRIB_ABSOLUTE;
    c.absolute = 10; /* below the floor: raised */
    CHECK(pay_tithe_compute(&p, PAY_UNIT_MONEY, 1000000, &c, 0, &r) == PAY_TITHE_OK);
    CHECK(r.contribution == 16180 && r.shortfall == 0);
    p.allow_below_phi = true;
    CHECK(pay_tithe_compute(&p, PAY_UNIT_MONEY, 1000000, &c, 0, &r) == PAY_TITHE_OK);
    CHECK(r.contribution == 10 && r.shortfall == 16170);
    p.allow_below_phi = false;
    c.absolute = 100000;
    CHECK(pay_tithe_compute(&p, PAY_UNIT_COMPUTE, 1000000, &c, 0, &r) == PAY_TITHE_RATE_UNSET);
    pay_rat_t posted = {3, 2};
    CHECK(pay_tithe_compute(&p, PAY_UNIT_COMPUTE, 1000000, &c, &posted, &r) == PAY_TITHE_OK);
    CHECK(r.vfv_credit == (100000 - 16180) * 3 / 2);
    p.credit_mult.num = 2;
    CHECK(pay_tithe_compute(&p, PAY_UNIT_BANDWIDTH, 1000000, &c, &posted, &r) == PAY_TITHE_OK);
    CHECK(r.vfv_credit == (100000 - 16180) * 3 / 2 * 2);

    /* commons: conservation and the no-monopoly split */
    pay_commons_t cm;
    pay_commons_init(&cm);
    CHECK(pay_commons_deposit(&cm, PAY_UNIT_COMPUTE, 21000));
    uint64_t w[3] = {100, 1, 1}, out[3];
    pay_rat_t cap = {8, 21};
    CHECK(pay_commons_allocate(&cm, PAY_UNIT_COMPUTE, w, 3, cap, out));
    CHECK(out[0] <= 8000 && out[0] + out[1] + out[2] + cm.pool[PAY_UNIT_COMPUTE] == 21000);
    CHECK(pay_commons_conserved(&cm));
    uint64_t w2[2] = {5, 1}, o2[2];
    uint64_t left = pay_commons_split(1000, w2, 2, cap, o2); /* equal-share floor 500 */
    CHECK(o2[0] <= 500 && o2[0] + o2[1] + left == 1000);
}

/* ===================================================================== */
static uint64_t g_ctr = 1;
static void mkreq(pay_posting_req_t *r, uint32_t initiator)
{
    uint8_t d[32];
    pay_hbuf h;
    memset(r, 0, sizeof *r);
    pay_hbuf_init(&h);
    pay_hbuf_str(&h, "TEST-REQ");
    pay_hbuf_u64(&h, g_ctr);
    pay_hbuf_final(&h, d);
    memcpy(r->idem_key, d, 32);
    pay_uetr_from_random(d, r->uetr);
    snprintf(r->e2e, sizeof r->e2e, "E2E-%" PRIu64, g_ctr);
    r->initiator = initiator;
    r->tick = g_ctr;
    g_ctr++;
}

#define NA 3 /* assets */
#define NC 3 /* caps   */
#define NO 6 /* owners */
static void test_ledger(void)
{
    static pay_ledger_t L;
    pay_posting_req_t rq;
    pay_receipt_t rc;
    uint16_t usd, btc, as[NA];
    pay_ledger_init(&L, 0);
    CHECK(L.vfv_asset == 0);
    const pay_asset_t *vfv = pay_ledger_asset(&L, 0);
    CHECK(strcmp(vfv->code, "VFV") == 0 && vfv->numeric == 846 && vfv->minor == 2 &&
          !vfv->iso4217 && vfv->store_credit);
    CHECK(strcmp(L.platform.jurisdiction, "NCR") == 0 && !L.platform.jurisdiction_iso);
    CHECK(pay_ledger_add_fiat(&L, "USD", 840, 2, &usd) == PAY_OK);
    CHECK(pay_ledger_add_fiat(&L, "VFV", 999, 2, 0) != PAY_OK); /* never as fiat */
    CHECK(pay_ledger_add_fiat(&L, "XAA", 846, 2, 0) != PAY_OK); /* rail numerics */
    CHECK(pay_ledger_add_fiat(&L, "XAB", 810, 2, 0) != PAY_OK);
    CHECK(pay_ledger_add_fiat(&L, "XAC", 888, 2, 0) != PAY_OK);
    CHECK(pay_ledger_add_fiat(&L, "usd", 840, 2, 0) != PAY_OK);
    CHECK(pay_ledger_add_crypto(&L, "BTC", 8, "4H95J0R2X", &btc) == PAY_OK);
    CHECK(pay_ledger_add_crypto(&L, "XYZ", 8, "4H95J0R2I", 0) != PAY_OK); /* I not allowed */
    CHECK(pay_dti_format_ok("4H95J0R2X") && !pay_dti_format_ok("4H95J0R2"));
    CHECK(pay_rail_code(PAY_RAIL_DEBIT) == 846 && pay_rail_code(PAY_RAIL_CREDIT) == 810 &&
          pay_rail_code(PAY_RAIL_EQUITY) == 888);
    as[0] = 0;
    as[1] = usd;
    as[2] = btc;
    const pay_cap_t caps[NC] = {PAY_CAP_FINANCIAL, PAY_CAP_INTELLECTUAL, PAY_CAP_SOCIAL};

    uint32_t acc[NA][NC][NO], iss[NA][NC];
    for (int a = 0; a < NA; a++)
        for (int c = 0; c < NC; c++) {
            CHECK(pay_ledger_open(&L, 100, as[a], caps[c], PAY_ACCT_ISSUER, &iss[a][c]) == PAY_OK);
            for (int o = 0; o < NO; o++)
                CHECK(pay_ledger_open(&L, (uint32_t) (o + 1), as[a], caps[c], 0, &acc[a][c][o]) ==
                      PAY_OK);
        }
    /* Crown: issue on SOCIAL to another owner is refused; same-owner moves ok */
    mkreq(&rq, 100);
    CHECK(pay_ledger_issue(&L, &rq, iss[0][2], acc[0][2][0], 10, &rc) == PAY_ERR_CROWN);
    CHECK(pay_cap_is_crown(PAY_CAP_SOCIAL) && pay_cap_is_crown(PAY_CAP_SPIRITUAL) &&
          !pay_cap_is_crown(PAY_CAP_FINANCIAL));

    /* shadow balances */
    static int64_t sd[PAY_MAX_ACCOUNTS], sc[PAY_MAX_ACCOUNTS];
    memset(sd, 0, sizeof sd);
    memset(sc, 0, sizeof sc);
    static char uetrs[4096][PAY_UETR_LEN + 1];
    int n_uetr = 0;
    long ok = 0, refused = 0;
    for (int it = 0; it < 4000; it++) {
        int a = (int) rnd_below(NA), c = (int) rnd_below(NC);
        int o1 = (int) rnd_below(NO), o2 = (int) rnd_below(NO);
        uint64_t amt = 1 + rnd_below(1000000);
        pay_status_t st;
        int op = (int) rnd_below(7);
        mkreq(&rq, (uint32_t) (o1 + 1));
        switch (op) {
        case 0:
        case 1:
            st = pay_ledger_issue(&L, &rq, iss[a][c], acc[a][c][o1], amt, &rc);
            break;
        case 2:
        case 3:
            st = pay_ledger_transfer(&L, &rq, acc[a][c][o1], acc[a][c][o2], amt, &rc);
            break;
        case 4:
            st = pay_ledger_redeem(&L, &rq, acc[a][c][o1], iss[a][c], amt, &rc);
            break;
        case 5:
            if (n_uetr == 0) {
                st = PAY_ERR_ARG;
                break;
            }
            st = pay_ledger_reverse(&L, &rq, uetrs[rnd_below((uint64_t) n_uetr)], &rc);
            break;
        default: /* random multi-line, often unbalanced or crossing assets */
            rq.n_lines = 2 + (uint32_t) rnd_below(3);
            for (uint32_t k = 0; k < rq.n_lines; k++) {
                int aa = rnd_below(4) ? a : (int) rnd_below(NA);
                rq.lines[k].account = acc[aa][c][rnd_below(NO)];
                rq.lines[k].d_debit = (int64_t) rnd_below(2000) - 1000;
                rq.lines[k].d_credit = rnd_below(8) ? 0 : (int64_t) rnd_below(100);
            }
            st = pay_ledger_post(&L, &rq, &rc);
            break;
        }
        bool crown_cross =
            caps[c] == PAY_CAP_SOCIAL && (op <= 1 || op == 4 || ((op == 2 || op == 3) && o1 != o2));
        if (crown_cross) CHECK(st != PAY_OK);
        if (st == PAY_OK) {
            ok++;
            for (uint32_t k = 0; k < rq.n_lines; k++) {
                sd[rq.lines[k].account] += rq.lines[k].d_debit;
                sc[rq.lines[k].account] += rq.lines[k].d_credit;
            }
            if (op != 5 && n_uetr < 4096) strcpy(uetrs[n_uetr++], rq.uetr);
            /* idempotent replay of the identical request */
            pay_receipt_t rc2;
            CHECK(pay_ledger_post(&L, &rq, &rc2) == PAY_DUPLICATE);
            CHECK(rc2.seq == rc.seq);
            /* same key, different request */
            if (rq.n_lines >= 2 && op != 5) {
                pay_posting_req_t r2 = rq;
                r2.lines[0].d_debit += 1;
                r2.lines[1].d_debit -= 1;
                CHECK(pay_ledger_post(&L, &r2, &rc2) == PAY_ERR_REPLAY);
            }
        } else {
            refused++;
        }
        if (!pay_ledger_check(&L)) {
            CHECK(0);
            break;
        }
    }
    printf("  ledger: %ld postings applied, %ld refused, journal seq %" PRIu64 "\n", ok, refused,
           L.seq);
    CHECK(ok > 1000 && refused > 200);
    /* shadow == ledger, per account, per rail */
    int mism = 0;
    for (uint32_t i = 0; i < L.n_accounts; i++) {
        const pay_account_t *x = pay_ledger_account(&L, i);
        if ((int64_t) x->debit != sd[i] || (int64_t) x->credit != sc[i] ||
            x->equity != sd[i] - sc[i])
            mism++;
        if (x->credit && !(x->flags & PAY_ACCT_ISSUER)) mism++;
    }
    CHECK(mism == 0);
    /* per rail and per capital form */
    for (int a = 0; a < NA; a++)
        for (int c = 0; c < NC; c++) {
            uint64_t d, cr;
            int64_t e;
            pay_ledger_totals(&L, as[a], caps[c], &d, &cr, &e);
            CHECK(d == cr && e == 0);
        }
    CHECK(pay_ledger_verify_chain(&L));

    /* explicit refusals */
    mkreq(&rq, 1);
    rq.n_lines = 2;
    rq.lines[0].account = acc[1][0][0];
    rq.lines[0].d_debit = 5;
    rq.lines[1].account = acc[1][0][1];
    rq.lines[1].d_debit = -4;
    CHECK(pay_ledger_post(&L, &rq, &rc) == PAY_ERR_UNBALANCED);
    mkreq(&rq, 1);
    rq.n_lines = 2;
    rq.lines[0].account = acc[1][0][0];
    rq.lines[0].d_credit = 5;
    rq.lines[1].account = acc[1][0][0];
    rq.lines[1].d_debit = 5;
    CHECK(pay_ledger_post(&L, &rq, &rc) == PAY_ERR_CREDIT); /* holder debt */
    mkreq(&rq, 1);
    uint64_t have = pay_ledger_account(&L, acc[1][0][0])->debit;
    CHECK(pay_ledger_transfer(&L, &rq, acc[1][0][0], acc[1][0][1], have + 1, &rc) == PAY_ERR_FUNDS);
    mkreq(&rq, 1);
    CHECK(pay_ledger_transfer(&L, &rq, acc[1][0][0], acc[2][0][1], 1, &rc) ==
          PAY_ERR_UNBALANCED); /* across assets */
    /* duplicate UETR with a fresh key */
    mkreq(&rq, 100);
    CHECK(pay_ledger_issue(&L, &rq, iss[1][0], acc[1][0][0], 50, &rc) == PAY_OK);
    pay_posting_req_t r3;
    mkreq(&r3, 100);
    strcpy(r3.uetr, rq.uetr);
    CHECK(pay_ledger_issue(&L, &r3, iss[1][0], acc[1][0][0], 50, &rc) == PAY_ERR_DUP_UETR);
    mkreq(&r3, 100);
    strcpy(r3.uetr, "not-a-uetr");
    CHECK(pay_ledger_issue(&L, &r3, iss[1][0], acc[1][0][0], 50, &rc) == PAY_ERR_UETR);
    /* nonce must increase */
    mkreq(&r3, 7);
    r3.nonce = 10;
    CHECK(pay_ledger_issue(&L, &r3, iss[1][0], acc[1][0][0], 1, &rc) == PAY_OK);
    mkreq(&r3, 7);
    r3.nonce = 10;
    CHECK(pay_ledger_issue(&L, &r3, iss[1][0], acc[1][0][0], 1, &rc) == PAY_ERR_REPLAY);
    /* reverse twice */
    mkreq(&r3, 100);
    CHECK(pay_ledger_reverse(&L, &r3, rq.uetr, &rc) == PAY_OK);
    mkreq(&r3, 100);
    CHECK(pay_ledger_reverse(&L, &r3, rq.uetr, &rc) == PAY_ERR_STATE);
    /* frozen */
    CHECK(pay_ledger_set_flags(&L, acc[1][0][0], PAY_ACCT_FROZEN) == PAY_OK);
    mkreq(&r3, 1);
    CHECK(pay_ledger_transfer(&L, &r3, acc[1][0][0], acc[1][0][1], 1, &rc) == PAY_ERR_POLICY);
    CHECK(pay_ledger_set_flags(&L, acc[1][0][0], 0) == PAY_OK);
    /* tithed payment, one posting */
    uint32_t commons;
    CHECK(pay_ledger_open(&L, 999, usd, PAY_CAP_FINANCIAL, PAY_ACCT_COMMONS, &commons) == PAY_OK);
    mkreq(&r3, 100);
    CHECK(pay_ledger_issue(&L, &r3, iss[1][0], acc[1][0][2], 1000000, &rc) == PAY_OK);
    uint64_t before = pay_ledger_account(&L, acc[1][0][2])->debit;
    mkreq(&r3, 3);
    CHECK(pay_ledger_pay_tithed(&L, &r3, acc[1][0][2], acc[1][0][3], 100000, commons,
                                pay_tithe_phi(100000), iss[0][0], acc[0][0][2], 7, &rc) == PAY_OK);
    CHECK(pay_ledger_account(&L, acc[1][0][2])->debit == before - 100000 - 1618);
    CHECK(pay_ledger_account(&L, commons)->debit == 1618);
    CHECK(pay_ledger_check(&L) && pay_ledger_verify_chain(&L));
    CHECK(L.seq > PAY_JOURNAL_MAX); /* the journal ring has wrapped */
}

/* ===================================================================== */
static void test_usury(void)
{
    CHECK(pay_usury_check(100, 100, false) == PAY_OK);
    CHECK(pay_usury_check(100, 90, false) == PAY_OK);
    CHECK(pay_usury_check(100, 101, false) == PAY_ERR_USURY);
    CHECK(pay_usury_check(100, 100, true) == PAY_ERR_USURY); /* time/balance based */
    pay_fee_rule_t f = {PAY_FEE_INTEREST, 0, {1, 100}};
    uint64_t fee;
    CHECK(pay_fee_compute(&f, 1000, &fee) == PAY_ERR_USURY);
    f.kind = PAY_FEE_LATE;
    CHECK(pay_fee_compute(&f, 1000, &fee) == PAY_ERR_USURY);
    f.kind = PAY_FEE_PROPORTIONAL;
    CHECK(pay_fee_compute(&f, 1000, &fee) == PAY_OK && fee == 10);
    f.kind = PAY_FEE_FLAT;
    f.flat = 3;
    CHECK(pay_fee_compute(&f, 1000, &fee) == PAY_OK && fee == 3);
    pay_role_cfg_t r;
    pay_role_init(&r, PAY_ROLE_INDIVIDUAL, 1, "Ann");
    r.fee.kind = PAY_FEE_INTEREST;
    CHECK(pay_role_validate(&r) == PAY_ERR_USURY);
}

static void test_zcap_order(void)
{
    CHECK((int) PAY_CAP_FINANCIAL == (int) ZCAP_FINANCIAL);
    CHECK((int) PAY_CAP_MANUFACTURED == (int) ZCAP_MANUFACTURED);
    CHECK((int) PAY_CAP_INTELLECTUAL == (int) ZCAP_INTELLECTUAL);
    CHECK((int) PAY_CAP_HUMAN == (int) ZCAP_HUMAN);
    CHECK((int) PAY_CAP_SOCIAL == (int) ZCAP_SOCIAL);
    CHECK((int) PAY_CAP_NATURAL == (int) ZCAP_NATURAL);
    CHECK((int) PAY_CAP_CULTURAL == (int) ZCAP_CULTURAL);
    CHECK((int) PAY_CAP_SPIRITUAL == (int) ZCAP_SPIRITUAL);
    CHECK((int) PAY_CAP_SYSTEM == (int) ZCAP_SYSTEM);
    CHECK((int) PAY_CAP_COUNT == ZCAP_FORM_COUNT);
}

/* ===================================================================== */
static void test_ids(void)
{
    const char *lei_ok[] = {"5493001KJTIIGC8Y1R12", "506700GE1G29325QX363", "HWUPKR0MPOU8FGXBT394",
                            "7LTWFZYICNSX8D621K86", "529900T8BM49AURSDO55", "549300GKFG0RYRRQ1414",
                            "INR2EJN1ERAN0W5ZP974", "213800D1EI4B9WTWWD28"};
    for (unsigned i = 0; i < sizeof lei_ok / sizeof *lei_ok; i++) CHECK(pay_lei_valid(lei_ok[i]));
    CHECK(!pay_lei_valid("5493001KJTIIGC8Y1R13"));
    CHECK(!pay_lei_valid("5493001KJTIIGC8Y1R1"));
    CHECK(!pay_lei_valid("5493001kjtiigc8y1r12"));
    const char *bic_ok[] = {"DEUTDEFF",    "DEUTDEFF500", "CHASUS33",
                            "BNPAFRPPXXX", "NEDSZAJJ",    "BOFAUS3NXXX"};
    for (unsigned i = 0; i < sizeof bic_ok / sizeof *bic_ok; i++) CHECK(pay_bic_valid(bic_ok[i]));
    CHECK(!pay_bic_valid("DEUTDEF"));
    CHECK(!pay_bic_valid("DEUTXXFF")); /* XX is not an ISO 3166 country */
    CHECK(!pay_bic_valid("DEUT1EFF"));
    CHECK(!pay_bic_valid("deutdeff"));
    const char *iban_ok[] = {"GB82WEST12345698765432",
                             "DE89370400440532013000",
                             "FR1420041010050500013M02606",
                             "NL91ABNA0417164300",
                             "BE68539007547034",
                             "CH9300762011623852957",
                             "NO9386011117947",
                             "GB33BUKB20201555555555",
                             "SA0380000000608010167519",
                             "MT84MALT011000012345MTLCAST001S"};
    for (unsigned i = 0; i < sizeof iban_ok / sizeof *iban_ok; i++)
        CHECK(pay_iban_valid(iban_ok[i]));
    CHECK(!pay_iban_valid("GB82WEST12345698765433"));
    CHECK(!pay_iban_valid("GB82WEST1234569876543"));
    CHECK(!pay_iban_valid("GB82 WEST 1234 5698 7654 32"));
    CHECK(pay_iban_length("DE") == 22 && pay_iban_length("NO") == 15);
    CHECK(pay_iso3166_valid("DE") && pay_iso3166_valid("NC") && !pay_iso3166_valid("XX"));
    CHECK(!pay_iso3166_valid("NCR"));
}

static void test_crypto_addr(void)
{
    CHECK(pay_btc_addr_check("1BvBMSEYstWetqTFn5Au4m4GFg7xJaNVN2", false) == PAY_ADDR_BTC_P2PKH);
    CHECK(pay_btc_addr_check("1A1zP1eP5QGefi2DMPTfTL5SLmv7DivfNa", false) == PAY_ADDR_BTC_P2PKH);
    CHECK(pay_btc_addr_check("3J98t1WpEZ73CNmQviecrnyiWrnqRhWNLy", false) == PAY_ADDR_BTC_P2SH);
    CHECK(pay_btc_addr_check("1BvBMSEYstWetqTFn5Au4m4GFg7xJaNVN3", false) == PAY_ADDR_INVALID);
    CHECK(pay_btc_addr_check("BC1QW508D6QEJXTDG4Y5R3ZARVARY0C5XW7KV8F3T4", false) ==
          PAY_ADDR_BTC_P2WPKH);
    CHECK(pay_btc_addr_check("bc1qrp33g0q5c5txsp9arysrx4k6zdkfs4nce4xj0gdcccefvpysxf3qccfmv3",
                             false) == PAY_ADDR_BTC_P2WSH);
    CHECK(pay_btc_addr_check("tb1qrp33g0q5c5txsp9arysrx4k6zdkfs4nce4xj0gdcccefvpysxf3q0sl5k7",
                             true) == PAY_ADDR_BTC_P2WSH);
    CHECK(pay_btc_addr_check("bc1p0xlxvlhemja6c4dqv22uapctqupfhlxm9h8z3k2e72q4k9hcz7vqzk5jj0",
                             false) == PAY_ADDR_BTC_P2TR);
    /* BIP-350: v1 with a bech32 (not bech32m) checksum */
    CHECK(pay_btc_addr_check("bc1p0xlxvlhemja6c4dqv22uapctqupfhlxm9h8z3k2e72q4k9hcz7vqh2y7hd",
                             false) == PAY_ADDR_INVALID);
    CHECK(pay_btc_addr_check("bc1qw508d6qejxtdg4y5r3zarvary0c5xw7kv8f3t5", false) ==
          PAY_ADDR_INVALID);
    CHECK(pay_btc_addr_check("bc1qw508d6qejxtdg4y5r3zarvary0c5xW7kv8f3t4", false) ==
          PAY_ADDR_INVALID); /* mixed case */
    CHECK(pay_btc_addr_check("BC1QW508D6QEJXTDG4Y5R3ZARVARY0C5XW7KV8F3T4", true) ==
          PAY_ADDR_INVALID); /* wrong network */
    const char *eth[] = {
        "0x5aAeb6053F3E94C9b9A09f33669435E7Ef1BeAed", "0xfB6916095ca1df60bB79Ce92cE3Ea74c37c5d359",
        "0xdbF03B407c01E7cD3CBea99509d93f8DDDC8C6FB", "0xD1220A0cf47c7B9Be7A2E6BA89F429762e7b9aDb"};
    for (unsigned i = 0; i < 4; i++) {
        CHECK(pay_eth_addr_check(eth[i]) == PAY_ADDR_ETH_CHECKSUMMED);
        char low[43];
        strcpy(low, eth[i]);
        for (int k = 2; k < 42; k++)
            if (low[k] >= 'A' && low[k] <= 'F') low[k] = (char) (low[k] + 32);
        CHECK(pay_eth_addr_check(low) == PAY_ADDR_ETH_NO_CHECKSUM);
        char bad[43];
        strcpy(bad, eth[i]);
        for (int k = 2; k < 42; k++)
            if (bad[k] >= 'a' && bad[k] <= 'f') {
                bad[k] = (char) (bad[k] - 32);
                break;
            }
        CHECK(pay_eth_addr_check(bad) == PAY_ADDR_INVALID);
    }
}

/* ===================================================================== */
static int gw_calls;
static int32_t gw_submit(void *ctx, const pay_gw_msg_t *m)
{
    (void) ctx;
    gw_calls++;
    return (m && m->doc && m->doc_len) ? 0 : -1;
}
static pay_screen_t screen_block_name(void *ctx, const pay_screen_req_t *r)
{
    (void) ctx;
    return (r->counterparty_name && strcmp(r->counterparty_name, "Blocked Person") == 0)
               ? PAY_SCREEN_BLOCK
               : PAY_SCREEN_PASS;
}

static void test_roles_and_intent(void)
{
    static pay_role_cfg_t ind, sb, inst;
    static pay_ledger_t L;
    CHECK(pay_role_init(&ind, PAY_ROLE_INDIVIDUAL, 1, "Ann") == PAY_OK);
    CHECK(strcmp(ind.jurisdiction, "NCR") == 0 && !ind.equity_enabled);
    CHECK(pay_role_validate(&ind) == PAY_OK);
    CHECK(pay_role_set_gateway(&ind, PAY_GW_SWIFT, gw_submit, 0, "x") == PAY_ERR_POLICY);
    CHECK(pay_role_set_gateway(&ind, PAY_GW_CHAIN, gw_submit, 0, "wallet") == PAY_OK);
    CHECK(pay_role_init(&sb, PAY_ROLE_SELF_BANK, 2, "Coop") == PAY_OK);
    CHECK(pay_role_validate(&sb) == PAY_ERR_STATE);
    pay_ledger_init(&L, 0);
    sb.ledger = &L;
    CHECK(pay_role_validate(&sb) == PAY_OK);
    CHECK(pay_role_init(&inst, PAY_ROLE_INSTITUTION, 3, "Bank") == PAY_OK);
    CHECK(pay_role_validate(&inst) == PAY_ERR_ARG);
    strcpy(inst.lei, "529900T8BM49AURSDO55");
    strcpy(inst.bic, "DEUTDEFF");
    strcpy(inst.iban, "DE89370400440532013000");
    CHECK(pay_role_validate(&inst) == PAY_OK);
    strcpy(inst.country, "NC"); /* a real country (New Caledonia) is fine here */
    CHECK(pay_role_validate(&inst) == PAY_OK);
    strcpy(inst.country, "XX");
    CHECK(pay_role_validate(&inst) == PAY_ERR_ARG);
    inst.country[0] = 0;
    CHECK(pay_role_set_gateway(&inst, PAY_GW_SWIFT, gw_submit, 0, "operator-swift") == PAY_OK);

    /* authorize: KYC, limits, screening, travel rule */
    inst.compliance.kyc_tier_required = 2;
    inst.limits.per_tx_max = 1000;
    inst.limits.per_period_max = 1500;
    inst.limits.period_ticks = 100;
    inst.compliance.screening_required = true;
    pay_screen_req_t rq = {3, "Bob", "US", 800, "USD", false, 0};
    pay_screen_t v;
    CHECK(pay_role_authorize(&inst, &rq, 1, 10, &v) == PAY_ERR_POLICY); /* KYC */
    CHECK(pay_role_authorize(&inst, &rq, 2, 10, &v) == PAY_ERR_STATE);  /* no hook */
    inst.screen = screen_block_name;
    CHECK(pay_role_authorize(&inst, &rq, 2, 10, &v) == PAY_OK && v == PAY_SCREEN_PASS);
    CHECK(pay_role_authorize(&inst, &rq, 2, 20, &v) == PAY_ERR_LIMIT); /* 1600 > 1500 */
    CHECK(pay_role_authorize(&inst, &rq, 2, 115, &v) == PAY_OK);       /* new period */
    rq.amount = 1001;
    CHECK(pay_role_authorize(&inst, &rq, 2, 300, &v) == PAY_ERR_LIMIT);
    rq.amount = 10;
    rq.counterparty_name = "Blocked Person";
    CHECK(pay_role_authorize(&inst, &rq, 2, 300, &v) == PAY_ERR_POLICY && v == PAY_SCREEN_BLOCK);

    /* crypto intent: wallet boundary, travel rule */
    uint16_t btc;
    CHECK(pay_ledger_add_crypto(&L, "BTC", 8, "4H95J0R2X", &btc) == PAY_OK);
    ind.compliance.travel_rule_required = true;
    ind.compliance.travel_rule_threshold = 100000;
    pay_crypto_intent_t it;
    CHECK(pay_crypto_intent_init(&it, &L, btc, PAY_CHAIN_BITCOIN, 0,
                                 "1BvBMSEYstWetqTFn5Au4m4GFg7xJaNVN2",
                                 "bc1qw508d6qejxtdg4y5r3zarvary0c5xw7kv8f3t4", 250000, 1000, 3,
                                 "8a562c67-ca16-48ba-b074-65581be6f001") == PAY_OK);
    CHECK(pay_crypto_intent_validate(&it) == PAY_OK);
    CHECK(pay_crypto_intent_handoff(&it, &ind, 0, 1) == PAY_ERR_POLICY); /* no travel data */
    it.travel.present = true;
    strcpy(it.travel.originator.name, "Ann");
    strcpy(it.travel.originator.account, "1BvBMSEYstWetqTFn5Au4m4GFg7xJaNVN2");
    strcpy(it.travel.originator.address, "1 Main St, Berlin, DE");
    strcpy(it.travel.beneficiary.name, "Bob");
    strcpy(it.travel.beneficiary.account, "bc1qw508d6qejxtdg4y5r3zarvary0c5xw7kv8f3t4");
    CHECK(pay_travel_rule_complete(&it.travel));
    int before = gw_calls;
    CHECK(pay_crypto_intent_handoff(&it, &ind, 0, 1) == PAY_OK);
    CHECK(gw_calls == before + 1 && it.state == PAY_INTENT_HANDED_TO_WALLET);
    uint8_t h[32] = {1};
    CHECK(pay_crypto_record_hash(&it, h) == PAY_OK);
    CHECK(pay_crypto_record_confirmations(&it, 1) == PAY_OK && it.state == PAY_INTENT_BROADCAST);
    CHECK(pay_crypto_record_confirmations(&it, 0) == PAY_ERR_STATE);
    CHECK(pay_crypto_record_confirmations(&it, 3) == PAY_OK && it.state == PAY_INTENT_CONFIRMED);
    CHECK(pay_crypto_record_failure(&it) == PAY_ERR_STATE);
    pay_crypto_intent_t bad;
    CHECK(pay_crypto_intent_init(&bad, &L, btc, PAY_CHAIN_EVM, 0,
                                 "0x5aAeb6053F3E94C9b9A09f33669435E7Ef1BeAed",
                                 "0xfB6916095ca1df60bB79Ce92cE3Ea74c37c5d359", 1, 0, 1,
                                 "8a562c67-ca16-48ba-b074-65581be6f001") == PAY_OK);
    CHECK(pay_crypto_intent_validate(&bad) == PAY_ERR_ARG); /* EVM needs a chain id */
    CHECK(pay_crypto_intent_init(&bad, &L, 0 /* VFV */, PAY_CHAIN_BITCOIN, 0, "a", "b", 1, 0, 1,
                                 "8a562c67-ca16-48ba-b074-65581be6f001") == PAY_ERR_NO_ASSET);
}

/* ===================================================================== */
static char g_buf[65536];
static int write_file(const char *name, const char *buf, int32_t n)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", g_tmp, name);
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fwrite(buf, 1, (size_t) n, f);
    fclose(f);
    return 0;
}
static int xsd_valid(const char *name, const char *xsd)
{
    char cmd[1600];
    snprintf(cmd, sizeof cmd, "xmllint --noout --schema '%s/%s' '%s/%s' >/dev/null 2>&1", g_xsd,
             xsd, g_tmp, name);
    return system(cmd) == 0;
}
static int g_xsd_ok, g_xsd_n;
static void validate(const char *name, const char *xsd, int32_t n)
{
    CHECK(n > 0);
    if (n <= 0) return;
    CHECK(write_file(name, g_buf, n) == 0);
    int ok = xsd_valid(name, xsd);
    CHECK(ok);
    g_xsd_n++;
    g_xsd_ok += ok;
    printf("  xsd %-12s vs %-22s %s\n", name, xsd, ok ? "VALID" : "INVALID");
}

static pay_ledger_t g_L;
static uint16_t g_usd, g_btc;
static pay_pacs008_t g_p8;
static char g_p8_xml[65536];
static int32_t g_p8_len;
static char g_c53_xml[65536];
static int32_t g_c53_len;

static void fill_tx(pay_ct_tx_t *t)
{
    memset(t, 0, sizeof *t);
    strcpy(t->instr_id, "I1");
    strcpy(t->e2e, "E2E-1");
    strcpy(t->tx_id, "TX-1");
    strcpy(t->uetr, "8a562c67-ca16-48ba-b074-65581be6f001");
    pay_iso_amt_from_asset(pay_ledger_asset(&g_L, g_usd), 123456, &t->amt);
    strcpy(t->chrg_br, "SHAR");
    strcpy(t->dbtr.name, "Alice & Co");
    t->dbtr.has_adr = true;
    strcpy(t->dbtr.adr.strt, "Taunusanlage");
    strcpy(t->dbtr.adr.bldg, "12");
    strcpy(t->dbtr.adr.pst_cd, "60325");
    strcpy(t->dbtr.adr.twn, "Frankfurt");
    strcpy(t->dbtr.adr.ctry, "DE");
    strcpy(t->dbtr_acct.iban, "DE89370400440532013000");
    strcpy(t->dbtr_agt.bic, "DEUTDEFF");
    strcpy(t->cdtr_agt.bic, "CHASUS33");
    strcpy(t->cdtr.name, "Bob");
    t->cdtr.has_adr = true;
    strcpy(t->cdtr.adr.twn, "New York");
    strcpy(t->cdtr.adr.ctry, "US");
    strcpy(t->cdtr_acct.othr, "123456789");
    strcpy(t->ustrd, "Invoice 42");
    t->note.jurisdiction = true;
    t->note.has_vfv = true;
    t->note.vfv_units = 1234;
}

static void test_iso_build(void)
{
    pay_ledger_init(&g_L, 0);
    pay_ledger_add_fiat(&g_L, "USD", 840, 2, &g_usd);
    pay_ledger_add_crypto(&g_L, "BTC", 8, "4H95J0R2X", &g_btc);
    pay_iso_ctx_t x = {PAY_ISO_CBPR, 0};
    pay_iso_ctx_t xb = {PAY_ISO_BASE, 0};

    pay_bah_t h;
    memset(&h, 0, sizeof h);
    strcpy(h.fr_bic, "DEUTDEFF");
    strcpy(h.to_bic, "CHASUS33XXX");
    strcpy(h.biz_msg_idr, "MSG-1");
    strcpy(h.msg_def_idr, "pacs.008.001.08");
    strcpy(h.biz_svc, "swift.cbprplus.02");
    strcpy(h.cre_dt, "2026-10-09T12:00:00+00:00");
    validate("head.xml", "head.001.001.02.xsd", pay_iso_bah(&x, &h, g_buf, sizeof g_buf));

    memset(&g_p8, 0, sizeof g_p8);
    strcpy(g_p8.msg_id, "MSG-1");
    strcpy(g_p8.cre_dt_tm, "2026-10-09T12:00:00Z");
    strcpy(g_p8.sttlm_mtd, "INDA");
    strcpy(g_p8.intr_bk_sttlm_dt, "2026-10-09");
    strcpy(g_p8.instg_agt.bic, "DEUTDEFF");
    strcpy(g_p8.instd_agt.bic, "CHASUS33");
    g_p8.n_tx = 1;
    fill_tx(&g_p8.tx[0]);
    int32_t n = pay_iso_pacs008(&x, &g_p8, g_buf, sizeof g_buf);
    validate("pacs008.xml", "pacs.008.001.08.xsd", n);
    if (n > 0) {
        memcpy(g_p8_xml, g_buf, (size_t) n);
        g_p8_len = n;
        g_p8_xml[n] = 0;
        CHECK(strstr(g_p8_xml, "/ZXV/JURIS/NCR") != NULL);
        CHECK(strstr(g_p8_xml, "/ZXV/VFV/12.34") != NULL);
        CHECK(strstr(g_p8_xml, "<Ctry>NC") == NULL);
        CHECK(strstr(g_p8_xml, "Ccy=\"VFV\"") == NULL);
        CHECK(strstr(g_p8_xml, "Intrst") == NULL);
        CHECK(strstr(g_p8_xml, "Ccy=\"USD\">1234.56<") != NULL);
    }
    /* BASE profile, two transactions */
    pay_pacs008_t p2 = g_p8;
    p2.n_tx = 2;
    p2.tx[1] = p2.tx[0];
    strcpy(p2.tx[1].e2e, "E2E-2");
    strcpy(p2.tx[1].instr_id, "I2");
    strcpy(p2.tx[1].tx_id, "TX-2");
    strcpy(p2.tx[1].uetr, "8a562c67-ca16-48ba-b074-65581be6f002");
    validate("pacs008b.xml", "pacs.008.001.08.xsd", pay_iso_pacs008(&xb, &p2, g_buf, sizeof g_buf));
    CHECK(pay_iso_pacs008(&x, &p2, g_buf, sizeof g_buf) == PAY_ISO_ERR_PROFILE); /* CBPR: 1 tx */

    /* VFV / crypto never in Ccy */
    pay_amt_t am;
    CHECK(pay_iso_amt_from_asset(pay_ledger_asset(&g_L, 0), 100, &am) == PAY_ISO_ERR_CCY);
    CHECK(pay_iso_amt_from_asset(pay_ledger_asset(&g_L, g_btc), 100, &am) == PAY_ISO_ERR_CCY);
    pay_pacs008_t p3 = g_p8;
    strcpy(p3.tx[0].amt.ccy, "VFV");
    p3.tx[0].amt.iso4217 = false;
    CHECK(pay_iso_pacs008(&x, &p3, g_buf, sizeof g_buf) == PAY_ISO_ERR_CCY);
    p3.tx[0].amt.iso4217 = true; /* forged flag */
    CHECK(pay_iso_pacs008(&x, &p3, g_buf, sizeof g_buf) < 0);
    /* countries: NCR never, NC only when confirmed */
    p3 = g_p8;
    strcpy(p3.tx[0].cdtr.adr.ctry, "NC");
    CHECK(pay_iso_pacs008(&x, &p3, g_buf, sizeof g_buf) == PAY_ISO_ERR_COUNTRY);
    p3.tx[0].cdtr.adr.ctry_confirmed = true;
    CHECK(pay_iso_pacs008(&x, &p3, g_buf, sizeof g_buf) > 0);
    CHECK(pay_iso_country_ok(&x, "NCR", true) == PAY_ISO_ERR_COUNTRY);
    CHECK(pay_iso_country_ok(&x, "XX", true) == PAY_ISO_ERR_COUNTRY);
    CHECK(pay_iso_country_ok(&x, "DE", false) == 0);
    /* operator override: a different platform jurisdiction/VFV code */
    pay_platform_t op;
    pay_platform_default(&op);
    strcpy(op.jurisdiction, "ZZQ");
    strcpy(op.vfv_alpha, "VFX");
    pay_iso_ctx_t xo = {PAY_ISO_CBPR, &op};
    n = pay_iso_pacs008(&xo, &g_p8, g_buf, sizeof g_buf);
    CHECK(n > 0);
    if (n > 0) {
        g_buf[n] = 0;
        CHECK(strstr(g_buf, "/ZXV/JURIS/ZZQ") != NULL && strstr(g_buf, "NCR") == NULL);
    }
    /* CBPR charset */
    p3 = g_p8;
    strcpy(p3.tx[0].ustrd, "caf\xc3\xa9");
    CHECK(pay_iso_pacs008(&x, &p3, g_buf, sizeof g_buf) < 0);
    p3 = g_p8;
    p3.tx[0].uetr[0] = 0;
    CHECK(pay_iso_pacs008(&x, &p3, g_buf, sizeof g_buf) < 0); /* UETR mandatory */
    CHECK(pay_iso_pacs008(&x, &g_p8, g_buf, 200) == PAY_ISO_ERR_TRUNC);

    /* pain.001 */
    static pay_pain001_t q;
    memset(&q, 0, sizeof q);
    strcpy(q.msg_id, "P1");
    strcpy(q.cre_dt_tm, "2026-10-09T12:00:00Z");
    strcpy(q.initg_pty.name, "Alice");
    strcpy(q.pmt_inf_id, "PI1");
    strcpy(q.reqd_exctn_dt, "2026-10-10");
    q.dbtr = g_p8.tx[0].dbtr;
    q.dbtr_acct = g_p8.tx[0].dbtr_acct;
    q.dbtr_agt = g_p8.tx[0].dbtr_agt;
    q.tx[0] = g_p8.tx[0];
    q.n_tx = 1;
    n = pay_iso_pain001(&x, &q, g_buf, sizeof g_buf);
    validate("pain001.xml", "pain.001.001.09.xsd", n);
    if (n > 0) {
        g_buf[n] = 0;
        CHECK(strstr(g_buf, "/ZXV/JURIS/NCR") && !strstr(g_buf, "Ccy=\"VFV\""));
    }

    /* pain.002 */
    static pay_pain002_t s;
    memset(&s, 0, sizeof s);
    strcpy(s.msg_id, "S1");
    strcpy(s.cre_dt_tm, "2026-10-09T12:00:00Z");
    strcpy(s.orgnl_msg_id, "P1");
    strcpy(s.orgnl_msg_nm_id, "pain.001.001.09");
    strcpy(s.grp_sts, "ACCP");
    strcpy(s.orgnl_pmt_inf_id, "PI1");
    s.n_tx = 1;
    strcpy(s.tx[0].orgnl_e2e, "E2E-1");
    strcpy(s.tx[0].orgnl_uetr, g_p8.tx[0].uetr);
    strcpy(s.tx[0].sts, "RJCT");
    strcpy(s.tx[0].rsn_cd, "AC04");
    s.tx[0].note.jurisdiction = true;
    s.tx[0].note.has_vfv = true;
    s.tx[0].note.vfv_units = 99;
    n = pay_iso_pain002(&x, &s, g_buf, sizeof g_buf);
    validate("pain002.xml", "pain.002.001.11.xsd", n);
    if (n > 0) {
        g_buf[n] = 0;
        CHECK(strstr(g_buf, "/ZXV/JURIS/NCR") && strstr(g_buf, "/ZXV/VFV/0.99"));
    }

    /* camt.052/053/054 */
    static pay_camt_t c;
    for (int k = 52; k <= 54; k++) {
        memset(&c, 0, sizeof c);
        c.kind = (pay_camt_kind_t) k;
        strcpy(c.msg_id, "C1");
        strcpy(c.cre_dt_tm, "2026-10-09T12:00:00Z");
        strcpy(c.rpt_id, "R1");
        strcpy(c.acct.iban, "DE89370400440532013000");
        strcpy(c.acct_ccy, "USD");
        if (k != 54) {
            c.n_bal = 2;
            strcpy(c.bal[0].code, "OPBD");
            c.bal[0].amt = g_p8.tx[0].amt;
            strcpy(c.bal[0].dt, "2026-10-09");
            strcpy(c.bal[1].code, "CLBD");
            c.bal[1].amt = g_p8.tx[0].amt;
            c.bal[1].debit = true;
            strcpy(c.bal[1].dt, "2026-10-09");
        }
        c.n_ntry = 2;
        for (int e = 0; e < 2; e++) {
            c.ntry[e].amt = g_p8.tx[0].amt;
            c.ntry[e].debit = e == 1;
            strcpy(c.ntry[e].sts, "BOOK");
            strcpy(c.ntry[e].bookg_dt, "2026-10-09");
            strcpy(c.ntry[e].val_dt, "2026-10-09");
            snprintf(c.ntry[e].e2e, sizeof c.ntry[e].e2e, "E2E-%d", e + 1);
            strcpy(c.ntry[e].uetr, g_p8.tx[0].uetr);
            c.ntry[e].note.has_vfv = true;
            c.ntry[e].note.vfv_units = 5;
            c.ntry[e].note.jurisdiction = e == 0;
        }
        char name[32], xsd[40];
        snprintf(name, sizeof name, "camt%d.xml", k);
        snprintf(xsd, sizeof xsd, "camt.0%d.001.08.xsd", k);
        n = pay_iso_camt(&x, &c, g_buf, sizeof g_buf);
        validate(name, xsd, n);
        if (k == 53 && n > 0) {
            memcpy(g_c53_xml, g_buf, (size_t) n);
            g_c53_len = n;
        }
    }
    /* negative control: a document the schema must reject */
    if (g_p8_len > 0) {
        char *bad = malloc((size_t) g_p8_len + 1);
        memcpy(bad, g_p8_xml, (size_t) g_p8_len + 1);
        char *u = strstr(bad, "<UETR>");
        if (u) memcpy(u + 6, "XXXXXXXX", 8);
        write_file("bad_pacs008.xml", bad, g_p8_len);
        CHECK(!xsd_valid("bad_pacs008.xml", "pacs.008.001.08.xsd"));
        free(bad);
    }
    printf("  xsd: %d of %d generated documents valid\n", g_xsd_ok, g_xsd_n);
}

/* ===================================================================== */
static void test_parse_fuzz(void)
{
    static pay_pacs008_in_t in8;
    static pay_camt053_in_t in53;
    /* round trip */
    CHECK(g_p8_len > 0 && g_c53_len > 0);
    if (g_p8_len <= 0 || g_c53_len <= 0) return;
    CHECK(pay_iso_parse_pacs008(g_p8_xml, (uint32_t) g_p8_len, &in8) == 0);
    CHECK(in8.n_tx == 1 && strcmp(in8.tx[0].uetr, g_p8.tx[0].uetr) == 0);
    CHECK(in8.tx[0].amt.units == 123456 && in8.tx[0].amt.frac == 2 &&
          strcmp(in8.tx[0].amt.ccy, "USD") == 0);
    CHECK(strcmp(in8.tx[0].e2e, "E2E-1") == 0 && strcmp(in8.tx[0].dbtr_name, "Alice & Co") == 0);
    CHECK(strcmp(in8.tx[0].dbtr_acct, "DE89370400440532013000") == 0);
    CHECK(strcmp(in8.tx[0].cdtr_agt_bic, "CHASUS33") == 0);
    CHECK(strcmp(in8.msg_id, "MSG-1") == 0 && in8.nb_of_txs == 1);
    CHECK(pay_iso_parse_camt053(g_c53_xml, (uint32_t) g_c53_len, &in53) == 0);
    CHECK(in53.n_bal == 2 && in53.n_ntry == 2 && in53.bal[1].debit && in53.ntry[1].debit &&
          strcmp(in53.acct_id, "DE89370400440532013000") == 0);

    /* hostile inputs */
    const char *hostile[] = {
        "<?xml version=\"1.0\"?><!DOCTYPE x [<!ENTITY a \"b\">]><Document/>",
        "<Document xmlns=\"urn:iso:std:iso:20022:tech:xsd:pacs.008.001.08\"><!-- c --></Document>",
        "<Document "
        "xmlns=\"urn:iso:std:iso:20022:tech:xsd:pacs.008.001.08\"><![CDATA[x]]></Document>",
        "<Document xmlns=\"urn:iso:std:iso:20022:tech:xsd:pacs.008.001.08\">&#65;</Document>",
        "<a:Document xmlns:a=\"urn:iso:std:iso:20022:tech:xsd:pacs.008.001.08\"/>",
        "",
        "<",
        "<Document",
        "</Document>",
    };
    for (unsigned i = 0; i < sizeof hostile / sizeof *hostile; i++)
        CHECK(pay_iso_parse_pacs008(hostile[i], (uint32_t) strlen(hostile[i]), &in8) < 0);
    /* deep nesting */
    static char deep[8192];
    int p = sprintf(deep, "<Document xmlns=\"urn:iso:std:iso:20022:tech:xsd:pacs.008.001.08\">");
    for (int i = 0; i < 100; i++) p += sprintf(deep + p, "<A>");
    for (int i = 0; i < 100; i++) p += sprintf(deep + p, "</A>");
    p += sprintf(deep + p, "</Document>");
    CHECK(pay_iso_parse_pacs008(deep, (uint32_t) p, &in8) < 0);

    /* mutation fuzzing */
    static char m[70000];
    const char *seeds[2] = {g_p8_xml, g_c53_xml};
    int32_t lens[2] = {g_p8_len, g_c53_len};
    for (int s = 0; s < 2; s++) {
        long accepted = 0, rejected = 0, bad_ok = 0;
        for (int it = 0; it < 25000; it++) {
            uint32_t len = (uint32_t) lens[s];
            memcpy(m, seeds[s], len);
            int nm = 1 + (int) rnd_below(4);
            for (int k = 0; k < nm; k++) {
                int kind = (int) rnd_below(6);
                uint32_t pos = (uint32_t) rnd_below(len ? len : 1);
                switch (kind) {
                case 0:
                    m[pos] = (char) rnd();
                    break; /* byte */
                case 1:
                    m[pos] = "<>/=\"&;! ?x0-:"[rnd_below(14)];
                    break; /* syntax */
                case 2:
                    len = pos;
                    break; /* truncate */
                case 3:    /* delete span */
                {
                    uint32_t d = (uint32_t) rnd_below(16) + 1;
                    if (pos + d > len) d = len - pos;
                    memmove(m + pos, m + pos + d, len - pos - d);
                    len -= d;
                    break;
                }
                case 4: /* duplicate span */
                {
                    uint32_t d = (uint32_t) rnd_below(64) + 1;
                    if (pos + d > len) d = len - pos;
                    if (len + d < sizeof m - 1) {
                        memmove(m + pos + d, m + pos, len - pos);
                        len += d;
                    }
                    break;
                }
                default:
                    m[pos] = (char) ('0' + rnd_below(10));
                    break; /* digits */
                }
            }
            int32_t r;
            if (s == 0) {
                r = pay_iso_parse_pacs008(m, len, &in8);
                if (r == 0) {
                    for (uint32_t t = 0; t < in8.n_tx; t++)
                        if (in8.tx[t].uetr[0] && !pay_uetr_valid(in8.tx[t].uetr)) bad_ok++;
                    if (in8.n_tx == 0 || in8.n_tx > PAY_ISO_MAX_TX) bad_ok++;
                }
            } else {
                r = pay_iso_parse_camt053(m, len, &in53);
                if (r == 0 && (in53.n_bal > PAY_ISO_MAX_BAL || in53.n_ntry > PAY_ISO_MAX_NTRY))
                    bad_ok++;
            }
            if (r == 0)
                accepted++;
            else
                rejected++;
        }
        printf("  fuzz %s: 25000 mutants, %ld accepted, %ld rejected\n",
               s == 0 ? "pacs.008" : "camt.053", accepted, rejected);
        CHECK(bad_ok == 0);
        CHECK(rejected > 1000);
    }
    /* random garbage */
    for (int it = 0; it < 5000; it++) {
        uint32_t len = (uint32_t) rnd_below(512);
        for (uint32_t k = 0; k < len; k++) m[k] = (char) rnd();
        CHECK(pay_iso_parse_pacs008(m, len, &in8) < 0);
    }
    CHECK(pay_iso_parse_pacs008(g_p8_xml, PAY_ISO_IN_MAX + 1, &in8) < 0); /* size bound */
}

/* ===================================================================== */
static pay_ledger_t g_EL;
static pay_equity_t g_ex;
static uint32_t g_vfv_issuer;

static uint32_t fund(pay_ledger_t *L, uint32_t owner, uint64_t amt)
{
    uint32_t a;
    pay_posting_req_t rq;
    pay_receipt_t rc;
    if (pay_ledger_open(L, owner, L->vfv_asset, PAY_CAP_FINANCIAL, 0, &a) != PAY_OK) return 0;
    if (amt) {
        mkreq(&rq, 0);
        pay_ledger_issue(L, &rq, g_vfv_issuer, a, amt, &rc);
    }
    return a;
}

static void test_equity(void)
{
    pay_ledger_init(&g_EL, 0);
    CHECK(pay_ledger_open(&g_EL, 0, 0, PAY_CAP_FINANCIAL, PAY_ACCT_ISSUER, &g_vfv_issuer) ==
          PAY_OK);
    pay_equity_cfg_t cfg;
    pay_equity_cfg_default(&cfg);
    CHECK(!cfg.enabled && cfg.default_cap.num == 8 && cfg.default_cap.den == 21);
    CHECK(pay_equity_init(&g_ex, &g_EL, &cfg) == PAY_OK);
    CHECK(pay_equity_register(&g_ex, 1, PAY_ROLE_INDIVIDUAL, "NCR") == PAY_ERR_POLICY); /* off */
    cfg.enabled = true;
    cfg.role_mask = (1u << PAY_ROLE_INDIVIDUAL) | (1u << PAY_ROLE_SELF_BANK);
    strcpy(cfg.disabled_juris[0], "BLK");
    cfg.n_disabled = 1;
    CHECK(pay_equity_init(&g_ex, &g_EL, &cfg) == PAY_OK);
    CHECK(pay_equity_register(&g_ex, 50, PAY_ROLE_INSTITUTION, "NCR") == PAY_ERR_POLICY);
    CHECK(pay_equity_register(&g_ex, 51, PAY_ROLE_INDIVIDUAL, "BLK") == PAY_ERR_POLICY);
    uint32_t money[8];
    for (uint32_t o = 1; o <= 7; o++) {
        CHECK(pay_equity_register(&g_ex, o, PAY_ROLE_INDIVIDUAL, "NCR") == PAY_OK);
        money[o] = fund(&g_EL, o, 10000000);
    }
    money[0] = 0;

    /* voting vs non-voting (type-level) and the currency */
    pay_obj_ref_t cur = {PAY_OBJ_CURRENCY, {0}};
    pay_eq_voting_t vh;
    pay_eq_nonvoting_t nh;
    CHECK(pay_equity_create_voting(&g_ex, &cur, 1, 0, PAY_SUPPLY_FIXED, 1000, 0, 0, &vh) ==
          PAY_ERR_POLICY);
    CHECK(pay_equity_create_nonvoting(&g_ex, &cur, 1, 0, PAY_SUPPLY_FIXED, 1000, 0, 0, &nh) ==
          PAY_ERR_POLICY);
    pay_obj_ref_t grp = {PAY_OBJ_GROUP, {1, 2, 3}};
    CHECK(pay_equity_create_voting(&g_ex, &grp, 1, 0, PAY_SUPPLY_FIXED, 2100, 0, 0, &vh) == PAY_OK);
    uint32_t gi = pay_eq_v_index(vh);
    CHECK(pay_equity_has_votes(&g_ex, gi));
    CHECK(pay_equity_votes(&g_ex, vh, 1) == 2100); /* issuer holds all */
    pay_obj_ref_t ds = {PAY_OBJ_DATASET, {9}};
    CHECK(pay_equity_create_nonvoting(&g_ex, &ds, 2, 0, PAY_SUPPLY_ISSUABLE, 1000, 2100, 0, &nh) ==
          PAY_OK);
    uint32_t di = pay_eq_nv_index(nh);
    CHECK(!pay_equity_has_votes(&g_ex, di));
    CHECK(pay_equity_issue(&g_ex, gi, 10) == PAY_ERR_POLICY); /* fixed */
    CHECK(pay_equity_issue(&g_ex, di, 1100) == PAY_OK);
    CHECK(pay_equity_issue(&g_ex, di, 1) == PAY_ERR_LIMIT);
    CHECK(pay_equity_create_voting(&g_ex, &grp, 1, 0, PAY_SUPPLY_FIXED, 5, 0, 0, &vh) ==
          PAY_ERR_STATE); /* one instrument per object */
    CHECK(pay_equity_check(&g_ex));

    /* cap: 8/21 of 2100 = 800 */
    CHECK(pay_equity_allocate(&g_ex, gi, 2, 801) == PAY_ERR_LIMIT);
    CHECK(pay_equity_allocate(&g_ex, gi, 2, 800) == PAY_OK);
    CHECK(pay_equity_allocate(&g_ex, gi, 3, 500) == PAY_OK);
    CHECK(pay_equity_votes(&g_ex, vh, 2) == 800);

    /* matching: price-time priority */
    uint64_t o1, o2, o3, ob;
    CHECK(pay_equity_order(&g_ex, gi, 2, money[2], PAY_SIDE_SELL, PAY_ORD_LIMIT, 105, 100, &o1) ==
          PAY_OK);
    CHECK(pay_equity_order(&g_ex, gi, 3, money[3], PAY_SIDE_SELL, PAY_ORD_LIMIT, 100, 100, &o2) ==
          PAY_OK);
    CHECK(pay_equity_order(&g_ex, gi, 1, money[1], PAY_SIDE_SELL, PAY_ORD_LIMIT, 100, 100, &o3) ==
          PAY_OK);
    CHECK(o1 && o2 && o3);
    uint64_t m4 = pay_ledger_account(&g_EL, money[4])->debit;
    uint64_t m3 = pay_ledger_account(&g_EL, money[3])->debit;
    CHECK(pay_equity_order(&g_ex, gi, 4, money[4], PAY_SIDE_BUY, PAY_ORD_LIMIT, 110, 150, &ob) ==
          PAY_OK);
    CHECK(ob == 0); /* fully filled */
    CHECK(pay_equity_holding(&g_ex, gi, 4) == 150);
    /* 100 @100 from owner 3 (older), then 50 @100 from owner 1 */
    CHECK(pay_ledger_account(&g_EL, money[4])->debit == m4 - 15000);
    CHECK(pay_ledger_account(&g_EL, money[3])->debit == m3 + 10000);
    CHECK(pay_equity_holding(&g_ex, gi, 3) == 400);
    CHECK(pay_equity_check(&g_ex));
    /* E3: owner 2 is at the cap (800): an incoming buy is cancelled, not rested */
    uint64_t sb;
    CHECK(pay_equity_order(&g_ex, gi, 2, money[2], PAY_SIDE_BUY, PAY_ORD_LIMIT, 106, 10, &sb) ==
          PAY_OK);
    CHECK(sb == 0 && pay_equity_holding(&g_ex, gi, 2) == 800);
    CHECK(pay_equity_reserved(&g_ex, money[2]) == 0);
    /* E2 self-trade prevention: owner 3 rests a sell @99, then buys @101;
     * its own resting sell is cancelled and it fills against owner 1 @100 */
    uint64_t own, sb2;
    uint64_t ev0 = g_ex.n_events;
    CHECK(pay_equity_order(&g_ex, gi, 3, money[3], PAY_SIDE_SELL, PAY_ORD_LIMIT, 99, 10, &own) ==
          PAY_OK);
    CHECK(own != 0);
    uint64_t h3 = pay_equity_holding(&g_ex, gi, 3);
    CHECK(pay_equity_order(&g_ex, gi, 3, money[3], PAY_SIDE_BUY, PAY_ORD_LIMIT, 101, 10, &sb2) ==
          PAY_OK);
    CHECK(sb2 == 0 && pay_equity_holding(&g_ex, gi, 3) == h3 + 10);
    CHECK(pay_equity_cancel(&g_ex, own, 3) == PAY_ERR_NOT_FOUND); /* already STP-cancelled */
    int stp = 0;
    for (uint64_t e = ev0; e < g_ex.n_events; e++)
        stp += g_ex.audit[e & (PAY_EQ_AUDIT - 1u)].kind == PAY_EQ_EV_STP_CANCEL;
    CHECK(stp == 1);
    CHECK(pay_equity_check(&g_ex));
    /* market buy by 5 with a budget: never rests, never over the cap */
    uint64_t mb;
    CHECK(pay_equity_order(&g_ex, gi, 5, money[5], PAY_SIDE_BUY, PAY_ORD_MARKET, 1000000, 900,
                           &mb) == PAY_OK);
    CHECK(mb == 0 && pay_equity_holding(&g_ex, gi, 5) > 0 &&
          pay_equity_holding(&g_ex, gi, 5) <= 800);
    CHECK(pay_equity_reserved(&g_ex, money[5]) == 0);
    CHECK(pay_equity_check(&g_ex));
    /* cancel: only the owner */
    uint64_t co;
    CHECK(pay_equity_order(&g_ex, gi, 3, money[3], PAY_SIDE_SELL, PAY_ORD_LIMIT, 500, 5, &co) ==
          PAY_OK);
    CHECK(pay_equity_cancel(&g_ex, co, 2) == PAY_ERR_POLICY);
    CHECK(pay_equity_cancel(&g_ex, co, 3) == PAY_OK);
    CHECK(pay_equity_check(&g_ex));

    /* fuzzed orders: conservation of money and shares, cap, book consistency */
    uint64_t money_total = 0;
    for (uint32_t o = 1; o <= 7; o++) money_total += pay_ledger_account(&g_EL, money[o])->debit;
    long fills0 = (long) g_ex.fills;
    uint64_t ids[256];
    int nid = 0;
    for (int it = 0; it < 6000; it++) {
        uint32_t inst = rnd_below(2) ? gi : di;
        uint32_t o = 1 + (uint32_t) rnd_below(7);
        int act = (int) rnd_below(10);
        if (act == 0 && nid) {
            int k = (int) rnd_below((uint64_t) nid);
            pay_equity_cancel(&g_ex, ids[k], o);
        } else {
            pay_side_t side = rnd_below(2) ? PAY_SIDE_BUY : PAY_SIDE_SELL;
            pay_ord_type_t ty = rnd_below(5) ? PAY_ORD_LIMIT : PAY_ORD_MARKET;
            uint64_t price = 90 + rnd_below(30), qty = 1 + rnd_below(120), id;
            if (ty == PAY_ORD_MARKET && side == PAY_SIDE_BUY) price = 1 + rnd_below(20000);
            pay_equity_order(&g_ex, inst, o, money[o], side, ty, price, qty, &id);
            if (id && nid < 256) ids[nid++] = id;
        }
        if (!pay_equity_check(&g_ex)) {
            CHECK(0);
            break;
        }
    }
    uint64_t money_after = 0;
    for (uint32_t o = 1; o <= 7; o++) money_after += pay_ledger_account(&g_EL, money[o])->debit;
    CHECK(money_after == money_total); /* trading only moves money */
    for (uint32_t k = 0; k < 2; k++) {
        uint32_t inst = k ? di : gi;
        const pay_eq_inst_t *in = pay_equity_inst(&g_ex, inst);
        uint64_t capm = in->supply * 8 / 21,
                 sum = pay_equity_holding(&g_ex, inst, in->issuer_owner);
        for (uint32_t o = 1; o <= 7; o++) {
            uint64_t hld = pay_equity_holding(&g_ex, inst, o);
            if (o != in->issuer_owner) CHECK(hld <= capm);
            if (o != in->issuer_owner) sum += hld;
        }
        CHECK(sum == in->supply);
    }
    printf("  equity: %ld fills in the fuzz run, %" PRIu64 " audit events\n",
           (long) g_ex.fills - fills0, g_ex.n_events);
    CHECK(g_ex.fills > (uint64_t) fills0 + 50);
    CHECK(pay_equity_verify_audit(&g_ex));

    /* disabling by jurisdiction stops trading */
    strcpy(g_ex.cfg.disabled_juris[1], "NCR");
    g_ex.cfg.n_disabled = 2;
    uint64_t id;
    CHECK(pay_equity_order(&g_ex, gi, 4, money[4], PAY_SIDE_BUY, PAY_ORD_LIMIT, 100, 1, &id) ==
          PAY_ERR_POLICY);
    g_ex.cfg.n_disabled = 1;

    /* VFV currency equity: non-voting, fractal reserve 5:3:2:1 */
    pay_eq_nonvoting_t vfv_eq;
    CHECK(pay_equity_register(&g_ex, 900, PAY_ROLE_SELF_BANK, "NCR") == PAY_OK); /* node */
    CHECK(pay_equity_register(&g_ex, 901, PAY_ROLE_SELF_BANK, "NCR") == PAY_OK); /* alliance */
    CHECK(pay_equity_register(&g_ex, 902, PAY_ROLE_SELF_BANK, "NCR") == PAY_OK); /* global */
    CHECK(pay_vfv_equity_init(&g_ex, 0, 0, 4, &vfv_eq) == PAY_OK);
    uint32_t vi = pay_eq_nv_index(vfv_eq);
    CHECK(!pay_equity_has_votes(&g_ex, vi));
    CHECK(pay_equity_issue(&g_ex, vi, 5) == PAY_ERR_POLICY);
    uint32_t path[3] = {900, 901, 902};
    CHECK(pay_vfv_equity_on_mint(&g_ex, 6, path, 1100) == PAY_OK);
    CHECK(pay_equity_holding(&g_ex, vi, 6) == 500 && pay_equity_holding(&g_ex, vi, 900) == 300 &&
          pay_equity_holding(&g_ex, vi, 901) == 200 && pay_equity_holding(&g_ex, vi, 902) == 100);
    CHECK(pay_vfv_equity_on_mint(&g_ex, 7, path, 7) == PAY_OK);
    CHECK(pay_vfv_equity_audit(&g_ex) && pay_equity_check(&g_ex));
    /* tradable for market returns */
    uint64_t vs;
    CHECK(pay_equity_order(&g_ex, vi, 6, money[6], PAY_SIDE_SELL, PAY_ORD_LIMIT, 3, 50, &vs) ==
          PAY_OK);
    CHECK(pay_equity_order(&g_ex, vi, 4, money[4], PAY_SIDE_BUY, PAY_ORD_LIMIT, 3, 50, &vs) ==
          PAY_OK);
    CHECK(pay_equity_holding(&g_ex, vi, 4) == 50 && pay_vfv_equity_audit(&g_ex));
    CHECK(pay_equity_check(&g_ex) && pay_equity_verify_audit(&g_ex));
}

/* ===================================================================== */
/* A toy "signature" for tests only: sig = SHA3(secret(signer) || msg). The
 * real verify callback is an ML-DSA / hybrid verifier supplied by the
 * operator. */
static void toy_sign(uint32_t signer, const uint8_t *msg, size_t n, uint8_t sig[32])
{
    pay_hbuf h;
    pay_hbuf_init(&h);
    pay_hbuf_str(&h, "TOY-KEY");
    pay_hbuf_u64(&h, signer * 2654435761u);
    pay_hbuf_put(&h, msg, n);
    pay_hbuf_final(&h, sig);
}
static bool toy_verify(void *ctx, uint32_t signer, const uint8_t *msg, size_t n, const uint8_t *sig,
                       size_t sl)
{
    uint8_t w[32];
    (void) ctx;
    if (sl != 32) return false;
    toy_sign(signer, msg, n, w);
    return memcmp(w, sig, 32) == 0;
}

static void test_treasury(void)
{
    static pay_ledger_t L;
    static pay_treasury_t T;
    pay_posting_req_t rq;
    pay_receipt_t rc;
    uint32_t iss, bob;
    pay_ledger_init(&L, 0);
    pay_ledger_open(&L, 0, 0, PAY_CAP_FINANCIAL, PAY_ACCT_ISSUER, &iss);
    pay_ledger_open(&L, 77, 0, PAY_CAP_FINANCIAL, 0, &bob);
    pay_treasury_cfg_t c;
    memset(&c, 0, sizeof c);
    memcpy(c.id, "syndicate-alpha", 15);
    c.group_owner = 500;
    c.asset = 0;
    c.n_members = 5;
    for (uint32_t i = 0; i < 5; i++) c.members[i] = 10 + i;
    c.m = 3;
    c.m_large = 4;
    c.large_amount = 50000;
    c.limit_per_tx = 100000;
    c.limit_per_period = 120000;
    c.period_len = 1000;
    pay_treasury_cfg_t badc = c;
    badc.m = 6;
    CHECK(pay_treasury_init(&T, &L, &badc, toy_verify, 0) == PAY_ERR_ARG);
    badc = c;
    badc.members[1] = badc.members[0];
    CHECK(pay_treasury_init(&T, &L, &badc, toy_verify, 0) == PAY_ERR_ARG);
    CHECK(pay_treasury_init(&T, &L, &c, toy_verify, 0) == PAY_OK);
    mkreq(&rq, 0);
    CHECK(pay_ledger_issue(&L, &rq, iss, T.acct, 1000000, &rc) == PAY_OK);

    uint64_t id;
    CHECK(pay_treasury_propose(&T, 99, bob, 1000, 1, 100, "x", &id) == PAY_ERR_POLICY);
    CHECK(pay_treasury_propose(&T, 10, bob, 100001, 1, 100, "x", &id) == PAY_ERR_LIMIT);
    CHECK(pay_treasury_propose(&T, 10, bob, 20000, 1, 100, "payroll", &id) == PAY_OK);
    CHECK(pay_treasury_proposal(&T, id)->required == 3);
    uint8_t msg[PAY_TR_MSG_LEN], sig[32];
    CHECK(pay_treasury_execute(&T, id, 2, &rc) == PAY_ERR_STATE); /* no approvals */
    for (uint32_t s = 10; s < 12; s++) {
        pay_treasury_approval_msg(&T, id, s, true, msg);
        toy_sign(s, msg, sizeof msg, sig);
        CHECK(pay_treasury_approve(&T, id, s, sig, 32, 2) == PAY_OK);
    }
    CHECK(pay_treasury_approve(&T, id, 11, sig, 32, 2) == PAY_DUPLICATE);
    pay_treasury_approval_msg(&T, id, 12, true, msg);
    toy_sign(13, msg, sizeof msg, sig); /* wrong key */
    CHECK(pay_treasury_approve(&T, id, 12, sig, 32, 2) == PAY_ERR_POLICY);
    toy_sign(99, msg, sizeof msg, sig);
    CHECK(pay_treasury_approve(&T, id, 99, sig, 32, 2) == PAY_ERR_POLICY); /* non-member */
    CHECK(pay_treasury_execute(&T, id, 3, &rc) == PAY_ERR_STATE);          /* 2 of 3 */
    toy_sign(12, msg, sizeof msg, sig);
    CHECK(pay_treasury_approve(&T, id, 12, sig, 32, 3) == PAY_OK);
    CHECK(pay_treasury_proposal(&T, id)->state == PAY_TR_APPROVED);
    CHECK(pay_treasury_execute(&T, id, 4, &rc) == PAY_OK);
    CHECK(pay_ledger_account(&L, bob)->debit == 20000 && pay_treasury_balance(&T) == 980000);
    CHECK(pay_treasury_execute(&T, id, 5, &rc) == PAY_DUPLICATE);
    CHECK(pay_ledger_account(&L, bob)->debit == 20000);

    /* large spend needs 4 */
    uint64_t big;
    CHECK(pay_treasury_propose(&T, 11, bob, 90000, 10, 100, "big", &big) == PAY_OK);
    CHECK(pay_treasury_proposal(&T, big)->required == 4);
    for (uint32_t s = 10; s < 13; s++) {
        pay_treasury_approval_msg(&T, big, s, true, msg);
        toy_sign(s, msg, sizeof msg, sig);
        CHECK(pay_treasury_approve(&T, big, s, sig, 32, 11) == PAY_OK);
    }
    CHECK(pay_treasury_execute(&T, big, 12, &rc) == PAY_ERR_STATE);
    pay_treasury_approval_msg(&T, big, 13, true, msg);
    toy_sign(13, msg, sizeof msg, sig);
    CHECK(pay_treasury_approve(&T, big, 13, sig, 32, 12) == PAY_OK);
    /* period limit: 20000 + 90000 = 110000 <= 120000 */
    CHECK(pay_treasury_execute(&T, big, 13, &rc) == PAY_OK);
    uint64_t p3;
    CHECK(pay_treasury_propose(&T, 10, bob, 20000, 20, 2000, "over", &p3) == PAY_OK);
    for (uint32_t s = 10; s < 13; s++) {
        pay_treasury_approval_msg(&T, p3, s, true, msg);
        toy_sign(s, msg, sizeof msg, sig);
        pay_treasury_approve(&T, p3, s, sig, 32, 21);
    }
    CHECK(pay_treasury_execute(&T, p3, 22, &rc) == PAY_ERR_LIMIT); /* 130000 > 120000 */
    CHECK(pay_treasury_execute(&T, p3, 1500, &rc) == PAY_OK);      /* next period */

    /* rejection and expiry */
    uint64_t rj, ex;
    CHECK(pay_treasury_propose(&T, 10, bob, 10, 1600, 100, "no", &rj) == PAY_OK);
    for (uint32_t s = 10; s < 13; s++) {
        pay_treasury_approval_msg(&T, rj, s, false, msg);
        toy_sign(s, msg, sizeof msg, sig);
        CHECK(pay_treasury_reject(&T, rj, s, sig, 32, 1601) == PAY_OK);
    }
    CHECK(pay_treasury_proposal(&T, rj)->state == PAY_TR_REJECTED);
    CHECK(pay_treasury_propose(&T, 10, bob, 10, 1600, 5, "late", &ex) == PAY_OK);
    pay_treasury_approval_msg(&T, ex, 10, true, msg);
    toy_sign(10, msg, sizeof msg, sig);
    CHECK(pay_treasury_approve(&T, ex, 10, sig, 32, 1605) == PAY_ERR_STATE);
    CHECK(pay_treasury_proposal(&T, ex)->state == PAY_TR_EXPIRED);
    /* the approval message of one proposal does not approve another */
    uint64_t pa, pb;
    CHECK(pay_treasury_propose(&T, 10, bob, 5, 1700, 100, "a", &pa) == PAY_OK);
    CHECK(pay_treasury_propose(&T, 10, bob, 5, 1700, 100, "b", &pb) == PAY_OK);
    pay_treasury_approval_msg(&T, pa, 10, true, msg);
    toy_sign(10, msg, sizeof msg, sig);
    CHECK(pay_treasury_approve(&T, pb, 10, sig, 32, 1701) == PAY_ERR_POLICY);
    /* overdraft impossible */
    uint64_t od;
    CHECK(pay_treasury_propose(&T, 10, bob, 100000, 3000, 100, "od", &od) == PAY_OK);
    CHECK(pay_treasury_check(&T));
    printf("  treasury: balance %" PRIu64 ", spent %" PRIu64 ", %" PRIu64 " signed records\n",
           pay_treasury_balance(&T), T.spent_total, T.n_log);
    CHECK(T.spent_total == 130000 && pay_treasury_balance(&T) == 870000);
}

/* ===================================================================== */
typedef struct {
    uint32_t cheater; /* farm whose results never replicate */
    bool unavailable; /* replica unavailable */
} farm_ctx_t;
static bool farm_verify(void *ctx, uint32_t farm, const uint8_t d[32], const uint8_t *sig,
                        size_t sl)
{
    return toy_verify(ctx, farm, d, 32, sig, sl);
}
static void job_result(const pay_work_receipt_t *r, uint8_t out[32])
{
    pay_hbuf h;
    pay_hbuf_init(&h);
    pay_hbuf_str(&h, "JOB-RESULT");
    pay_hbuf_put(&h, r->job_id, 32);
    pay_hbuf_final(&h, out);
}
static bool farm_replicate(void *ctx, const pay_work_receipt_t *r, uint8_t out[32])
{
    farm_ctx_t *fc = ctx;
    if (fc->unavailable) return false;
    job_result(r, out);
    if (r->farm == fc->cheater) out[0] ^= 1; /* the cheater's results are wrong */
    return true;
}
static void mk_receipt(pay_work_receipt_t *r, uint32_t farm, uint64_t job, pay_work_kind_t k,
                       uint64_t qty, uint64_t period)
{
    memset(r, 0, sizeof *r);
    r->farm = farm;
    snprintf((char *) r->job_id, 32, "job-%" PRIu64, job);
    r->kind = (uint8_t) k;
    r->qty = qty;
    r->period = period;
    job_result(r, r->result);
}
static pay_status_t submit(pay_farm_ctx_t *F, const pay_work_receipt_t *r)
{
    uint8_t d[32], sig[32];
    pay_farm_receipt_digest(r, d);
    toy_sign(r->farm, d, 32, sig);
    return pay_farm_submit(F, r, sig, 32);
}

static void test_farm(void)
{
    static pay_ledger_t L;
    static pay_farm_ctx_t F;
    static pay_equity_t ex;
    farm_ctx_t fc = {0, false};
    pay_ledger_init(&L, 0);
    uint32_t commons;
    CHECK(pay_ledger_open(&L, 999, 0, PAY_CAP_FINANCIAL, PAY_ACCT_COMMONS, &commons) == PAY_OK);
    pay_equity_cfg_t ec;
    pay_equity_cfg_default(&ec);
    ec.enabled = true;
    ec.role_mask = 0x7;
    CHECK(pay_equity_init(&ex, &L, &ec) == PAY_OK);
    pay_eq_nonvoting_t vh;
    CHECK(pay_vfv_equity_init(&ex, 0, 0, 4, &vh) == PAY_OK);

    pay_farm_cfg_t cfg;
    pay_farm_cfg_default(&cfg);
    CHECK(cfg.cap_share.num == 8 && cfg.cap_share.den == 21 && cfg.max_strikes == 3);
    cfg.rate[PAY_WORK_COMPUTE] = (pay_rat_t){1, 10};   /* 1 VFV minor per 10 token-cycles */
    cfg.rate[PAY_WORK_STORAGE] = (pay_rat_t){1, 1000}; /* per 1000 byte-hours */
    cfg.rate[PAY_WORK_BANDWIDTH] = (pay_rat_t){0, 1};  /* unset */
    cfg.sample_rate = (pay_rat_t){1, 4};
    CHECK(pay_farm_init(&F, &L, &cfg, 0, commons, farm_verify, farm_replicate, &fc, &ex) == PAY_OK);
    uint32_t path[3] = {800, 801, 802};
    for (uint32_t f = 1; f <= 5; f++) CHECK(pay_farm_register(&F, f, path) == PAY_OK);
    CHECK(pay_farm_register(&F, 1, path) == PAY_DUPLICATE);

    /* swarm-derived rate */
    swarm_market_t sm;
    memset(&sm, 0, sizeof sm);
    pay_rat_t sr;
    CHECK(!pay_farm_rate_from_swarm(&sm, (pay_rat_t){1, 1}, &sr));
    sm.pot = 1300;
    sm.last_market = 13000;
    CHECK(pay_farm_rate_from_swarm(&sm, (pay_rat_t){1, 1}, &sr) && sr.num == 1300 &&
          sr.den == 13000);

    /* the W4 cap on its own */
    uint64_t e1[3] = {1000, 1000, 1000};
    CHECK(pay_farm_cap(e1, 3, cfg.cap_share) == 1000); /* 1/3 < 8/21? no: 1/3 < 0.381 ok */
    uint64_t e2[3] = {100000, 100, 100};
    uint64_t c2 = pay_farm_cap(e2, 3, cfg.cap_share);
    CHECK(c2 * 21 <= 8 * (c2 + 200) && (c2 + 1) * 21 > 8 * (c2 + 1 + 200)); /* exact max */
    uint64_t e3[2] = {100000, 1};
    uint64_t c3 = pay_farm_cap(e3, 2, cfg.cap_share); /* share raised to 1/2 */
    CHECK(c3 == 1);

    /* replay, signature, period checks */
    pay_work_receipt_t r;
    mk_receipt(&r, 1, 1, PAY_WORK_COMPUTE, 1000, 0);
    CHECK(submit(&F, &r) == PAY_OK);
    CHECK(submit(&F, &r) == PAY_ERR_REPLAY);
    pay_work_receipt_t r2 = r;
    r2.farm = 2; /* the same job claimed by another farm */
    CHECK(submit(&F, &r2) == PAY_ERR_REPLAY);
    mk_receipt(&r, 1, 2, PAY_WORK_COMPUTE, 1000, 7);
    CHECK(submit(&F, &r) == PAY_ERR_STATE);
    mk_receipt(&r, 1, 3, PAY_WORK_COMPUTE, 1000, 0);
    uint8_t d[32], sig[32];
    pay_farm_receipt_digest(&r, d);
    toy_sign(2, d, 32, sig);
    CHECK(pay_farm_submit(&F, &r, sig, 32) == PAY_ERR_POLICY); /* bad signature */
    mk_receipt(&r, 42, 4, PAY_WORK_COMPUTE, 1000, 0);
    CHECK(submit(&F, &r) == PAY_ERR_NO_ACCOUNT);

    /* honest period: farms 1..5 submit; farm 4 is a cheater */
    fc.cheater = 4;
    uint64_t job = 100;
    for (uint64_t period = 0; period < 6; period++) {
        for (uint32_t f = 1; f <= 5; f++)
            for (int k = 0; k < 12; k++) {
                pay_work_kind_t kind = (pay_work_kind_t) (k % 3);
                uint64_t qty = kind == PAY_WORK_STORAGE ? 5000000 : 20000 + 1000 * f;
                if (f == 1) qty *= 20; /* one big farm */
                mk_receipt(&r, f, job++, kind, qty, period);
                pay_status_t st = submit(&F, &r);
                const pay_farm_t *fa = pay_farm_get(&F, f);
                CHECK(st == (fa->suspended ? PAY_ERR_POLICY : PAY_OK));
            }
        uint8_t beacon[32];
        memset(beacon, 0, 32);
        beacon[0] = (uint8_t) period;
        beacon[1] = 0xA5;
        uint64_t before[6], issued = 0, nz = 0;
        for (uint32_t f = 1; f <= 5; f++) before[f] = pay_farm_get(&F, f)->minted;
        CHECK(pay_farm_close_period(&F, beacon, 1000 + period) == PAY_OK);
        CHECK(pay_farm_audit(&F));
        /* W4: no farm above max(8/21, 1/n) of this period's issuance */
        for (uint32_t f = 1; f <= 5; f++) {
            uint64_t dlt = pay_farm_get(&F, f)->minted - before[f];
            issued += dlt;
            nz += dlt != 0;
        }
        for (uint32_t f = 1; f <= 5; f++) {
            uint64_t dlt = pay_farm_get(&F, f)->minted - before[f];
            CHECK(dlt * 21 <= 8 * issued || dlt * nz <= issued);
        }
        CHECK(issued > 0);
    }
    const pay_farm_t *big = pay_farm_get(&F, 1), *ch = pay_farm_get(&F, 4);
    CHECK(ch->strikes >= 1 && ch->forfeited > 0);
    CHECK(ch->suspended == (ch->strikes >= 3));
    CHECK(big->capped > 0); /* the big farm hit the cap */
    for (uint32_t f = 1; f <= 5; f++) {
        const pay_farm_t *fa = pay_farm_get(&F, f);
        CHECK(fa->minted <= fa->verified);
    }
    CHECK(F.tithed == F.minted - F.net && F.tithed > 0);
    CHECK(pay_ledger_account(&L, commons)->debit == F.tithed);
    printf("  farm: minted %" PRIu64 " (tithe %" PRIu64 "), sampled %" PRIu64
           ", mismatches %" PRIu64 ", cheater strikes %u suspended %d\n",
           F.minted, F.tithed, F.stats.sampled, F.stats.mismatches, ch->strikes, ch->suspended);

    /* sampling is deterministic and close to the rate */
    uint8_t beacon[32] = {7};
    int hits = 0;
    for (int i = 0; i < 20000; i++) {
        uint8_t jid[32] = {0};
        snprintf((char *) jid, 32, "s-%d", i);
        hits += pay_farm_sampled(beacon, jid, (pay_rat_t){1, 4});
        CHECK(pay_farm_sampled(beacon, jid, (pay_rat_t){1, 4}) ==
              pay_farm_sampled(beacon, jid, (pay_rat_t){1, 4}));
    }
    CHECK(hits > 4500 && hits < 5500);

    /* replica unavailable: carried to the next period, not credited */
    fc.unavailable = true;
    uint64_t carried0 = F.stats.carried;
    for (int k = 0; k < 40; k++) {
        mk_receipt(&r, 2, job++, PAY_WORK_COMPUTE, 10000, F.period);
        CHECK(submit(&F, &r) == PAY_OK);
    }
    uint8_t bz[32] = {9};
    CHECK(pay_farm_close_period(&F, bz, 2000) == PAY_OK);
    CHECK(F.stats.carried > carried0 && F.n_queue > 0);
    fc.unavailable = false;
    CHECK(pay_farm_close_period(&F, bz, 2001) == PAY_OK);
    CHECK(F.n_queue == 0 && pay_farm_audit(&F));

    /* burn: supply = issuance - burns */
    const pay_farm_t *f2 = pay_farm_get(&F, 2);
    uint64_t bal = pay_ledger_account(&L, f2->vfv_acct)->debit;
    CHECK(bal > 10);
    CHECK(pay_farm_burn(&F, f2->vfv_acct, 10, 3000) == PAY_OK);
    CHECK(F.burned == 10 && pay_farm_audit(&F));
    CHECK(pay_farm_burn(&F, f2->vfv_acct, bal, 3001) == PAY_ERR_FUNDS);

    /* VFV equity generated for every VFV minted (full reserve) */
    CHECK(ex.vfv.minted_seen == F.minted && F.stats.equity_errors == 0);
    CHECK(pay_vfv_equity_audit(&ex) && pay_equity_check(&ex));
    CHECK(!pay_equity_has_votes(&ex, pay_eq_nv_index(vh)));
}

/* ===================================================================== */
int main(int argc, char **argv)
{
    g_ref = argc > 1 ? argv[1] : "tithe_ref.txt";
    g_xsd = argc > 2 ? argv[2] : "src/pay/xsd";
    g_tmp = argc > 3 ? argv[3] : "/tmp";
    printf("test_pay: util\n");
    test_util();
    printf("test_pay: tithe\n");
    test_tithe();
    printf("test_pay: ledger\n");
    test_ledger();
    test_usury();
    test_zcap_order();
    printf("test_pay: identifiers and addresses\n");
    test_ids();
    test_crypto_addr();
    printf("test_pay: roles and crypto intents\n");
    test_roles_and_intent();
    printf("test_pay: ISO 20022 build + XSD\n");
    test_iso_build();
    printf("test_pay: parsers + fuzz\n");
    test_parse_fuzz();
    printf("test_pay: equity\n");
    test_equity();
    printf("test_pay: treasury\n");
    test_treasury();
    printf("test_pay: farm\n");
    test_farm();
    printf("test_pay: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
