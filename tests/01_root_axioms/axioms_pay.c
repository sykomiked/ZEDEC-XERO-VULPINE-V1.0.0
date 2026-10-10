/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* axioms_pay.c — Tier 1 root axioms for kernel/src/pay: pay_ledger (the
 * three-rail ledger) and pay_tithe (the exact phi-percent tithe and the
 * commons pool).
 *
 * Boundaries are generated (tier.h) from the module's own documented limits:
 * LINE_MAX_DELTA = 2^59 per line, PAY_BAL_MAX = 2^62 per balance,
 * PAY_MAX_LINES, PAY_MAX_ASSETS, PAY_MAX_ACCOUNTS, PAY_MAX_PRINCIPALS,
 * PAY_JOURNAL_MAX, PAY_E2E_MAX. Every refused posting must leave the ledger
 * byte-for-byte unchanged (L4, fail closed); every accepted one must keep
 * pay_ledger_check (L2/L3) and the provenance chain true.
 */
#include "tier.h"
#include "pay_ledger.h"
#include "pay_tithe.h"

static const tier_known_t KNOWN_FAILURES[] = {
    {"F-PAY-NULL", "pay_ledger_verify_chain/pay_ledger_totals/pay_commons_conserved "
                   "dereference a NULL ledger"},
};

#define LMAX ((uint64_t) 1 << 59) /* pay_ledger.c LINE_MAX_DELTA */

static uint64_t g_ctr = 1;
static void mkreq(pay_posting_req_t *r, uint32_t initiator)
{
    uint8_t d[32];
    pay_hbuf h;
    memset(r, 0, sizeof *r);
    pay_hbuf_init(&h);
    pay_hbuf_str(&h, "TIER1-REQ");
    pay_hbuf_u64(&h, g_ctr);
    pay_hbuf_final(&h, d);
    memcpy(r->idem_key, d, 32);
    pay_uetr_from_random(d, r->uetr);
    snprintf(r->e2e, sizeof r->e2e, "T1-%llu", (unsigned long long) g_ctr);
    r->initiator = initiator;
    r->tick = g_ctr;
    g_ctr++;
}

static pay_ledger_t L, SNAP;

static bool unchanged(void)
{
    return memcmp(L.acct, SNAP.acct, sizeof L.acct) == 0 && L.seq == SNAP.seq &&
           memcmp(L.chain_head, SNAP.chain_head, sizeof L.chain_head) == 0 &&
           memcmp(L.externality, SNAP.externality, sizeof L.externality) == 0 &&
           L.n_nonce == SNAP.n_nonce;
}

static bool healthy(void)
{
    return pay_ledger_check(&L) && pay_ledger_verify_chain(&L);
}

/* Post and classify: an accepted posting keeps L healthy, a refused one
 * leaves L exactly as it was. */
static pay_status_t post_checked(pay_posting_req_t *rq, const char *what)
{
    pay_receipt_t rc;
    SNAP = L;
    pay_status_t st = pay_ledger_post(&L, rq, &rc);
    if (st == PAY_OK)
        CHECK(healthy() && L.seq == SNAP.seq + 1, "%s: accepted posting keeps L2/L3/chain", what);
    else if (st != PAY_DUPLICATE)
        CHECK(unchanged() && rc.status == st, "%s: refused (%d) posting leaves no state", what,
              (int) st);
    return st;
}

static uint16_t usd;
static uint32_t ISS, A, B, COM, FRZ, CR1, CR1B, CR2;

static void setup(void)
{
    pay_ledger_init(&L, NULL);
    CHECK(pay_ledger_add_fiat(&L, "USD", 840, 2, &usd) == PAY_OK, "add USD");
    CHECK(pay_ledger_open(&L, 100, usd, PAY_CAP_FINANCIAL, PAY_ACCT_ISSUER, &ISS) == PAY_OK,
          "open issuer");
    pay_ledger_open(&L, 1, usd, PAY_CAP_FINANCIAL, 0, &A);
    pay_ledger_open(&L, 2, usd, PAY_CAP_FINANCIAL, 0, &B);
    pay_ledger_open(&L, 3, usd, PAY_CAP_FINANCIAL, PAY_ACCT_COMMONS, &COM);
    pay_ledger_open(&L, 4, usd, PAY_CAP_FINANCIAL, PAY_ACCT_FROZEN, &FRZ);
    pay_ledger_open(&L, 1, usd, PAY_CAP_SOCIAL, PAY_ACCT_ISSUER, &CR1);
    pay_ledger_open(&L, 1, usd, PAY_CAP_SOCIAL, 0, &CR1B);
    pay_ledger_open(&L, 2, usd, PAY_CAP_SOCIAL, 0, &CR2);
}

static void axiom_amount_bounds(void)
{
    pay_posting_req_t rq;
    uint64_t b[5];
    unsigned n = tier_bounds_u64(LMAX, b); /* {0, 1, 2^59-1, 2^59, 2^59+1} */
    for (unsigned i = 0; i < n; i++) {
        mkreq(&rq, 100);
        SNAP = L;
        pay_status_t st = pay_ledger_issue(&L, &rq, ISS, A, b[i], NULL);
        bool legal = b[i] >= 1 && b[i] <= LMAX;
        CHECK((st == PAY_OK) == legal, "issue amount %llu -> %d", (unsigned long long) b[i],
              (int) st);
        if (!legal)
            CHECK(unchanged(), "issue %llu refused leaves no state", (unsigned long long) b[i]);
    }
    /* A now holds 1 + (2^59-1) + 2^59 = 2^60. Fill toward PAY_BAL_MAX = 2^62:
     * the largest legal balance is 2^62-1 (MAX), 2^62 is MAX+1. */
    const pay_account_t *a = pay_ledger_account(&L, A);
    CHECK(a && a->debit == ((uint64_t) 1 << 60), "A holds 2^60");
    for (int k = 0; k < 5; k++) { /* +5 * 2^59 = 2^62 - 2^59 - ... */
        mkreq(&rq, 100);
        CHECK(pay_ledger_issue(&L, &rq, ISS, A, LMAX, NULL) == PAY_OK, "fill %d", k);
    }
    /* A = 2^60 + 5*2^59 = 7*2^59 = 2^62 - 2^59 */
    mkreq(&rq, 100);
    SNAP = L;
    CHECK(pay_ledger_issue(&L, &rq, ISS, A, LMAX, NULL) == PAY_ERR_OVERFLOW && unchanged(),
          "issue to exactly PAY_BAL_MAX (MAX+1) refused, no state");
    mkreq(&rq, 100);
    CHECK(pay_ledger_issue(&L, &rq, ISS, A, LMAX - 1, NULL) == PAY_OK, "issue to PAY_BAL_MAX-1");
    a = pay_ledger_account(&L, A);
    CHECK(a->debit == PAY_BAL_MAX - 1 && healthy(), "A at MAX (2^62-1), ledger healthy");
    mkreq(&rq, 100);
    SNAP = L;
    CHECK(pay_ledger_issue(&L, &rq, ISS, A, 1, NULL) == PAY_ERR_OVERFLOW && unchanged(),
          "one more unit (MAX+1) refused");
    /* the issuer's CREDIT hit the same bound on the other rail */
    const pay_account_t *is = pay_ledger_account(&L, ISS);
    CHECK(is->credit == PAY_BAL_MAX - 1 && is->equity == -(int64_t) (PAY_BAL_MAX - 1),
          "issuer CREDIT mirrors, EQUITY = DEBIT - CREDIT");

    /* funds: drain A to 0 by legal transfers, then 1 more is refused */
    uint64_t left = a->debit;
    while (left) {
        uint64_t m = left > LMAX ? LMAX : left;
        mkreq(&rq, 1);
        CHECK(pay_ledger_transfer(&L, &rq, A, B, m, NULL) == PAY_OK, "drain %llu",
              (unsigned long long) m);
        left -= m;
    }
    mkreq(&rq, 1);
    SNAP = L;
    CHECK(pay_ledger_transfer(&L, &rq, A, B, 1, NULL) == PAY_ERR_FUNDS && unchanged(),
          "transfer from an empty account refused, no state");
    /* redeem B's whole balance back at par, then redeem 1 more */
    left = pay_ledger_account(&L, B)->debit;
    while (left) {
        uint64_t m = left > LMAX ? LMAX : left;
        mkreq(&rq, 2);
        CHECK(pay_ledger_redeem(&L, &rq, B, ISS, m, NULL) == PAY_OK, "redeem %llu",
              (unsigned long long) m);
        left -= m;
    }
    CHECK(pay_ledger_account(&L, ISS)->credit == 0, "all claims extinguished");
    mkreq(&rq, 2);
    SNAP = L;
    CHECK(pay_ledger_redeem(&L, &rq, B, ISS, 1, NULL) != PAY_OK && unchanged(),
          "redeem below zero refused");
}

static void axiom_line_bounds(void)
{
    pay_posting_req_t rq;
    mkreq(&rq, 100);
    CHECK(pay_ledger_issue(&L, &rq, ISS, A, 1000, NULL) == PAY_OK, "seed A");
    /* n_lines: {0, 1, MAX-1, MAX, MAX+1, UINT32_MAX} */
    uint32_t nl[] = {0, 1, PAY_MAX_LINES - 1, PAY_MAX_LINES, PAY_MAX_LINES + 1, UINT32_MAX};
    for (unsigned i = 0; i < TIER_N(nl); i++) {
        mkreq(&rq, 1);
        uint32_t k = nl[i] < PAY_MAX_LINES ? nl[i] : PAY_MAX_LINES;
        for (uint32_t j = 0; j < k; j++) { /* alternate -1/+1 so even counts balance */
            rq.lines[j].account = (j % 2) ? B : A;
            rq.lines[j].d_debit = (j % 2) ? 1 : -1;
        }
        rq.n_lines = nl[i];
        pay_status_t st = post_checked(&rq, "n_lines");
        bool legal = nl[i] >= 1 && nl[i] <= PAY_MAX_LINES && nl[i] % 2 == 0;
        CHECK((st == PAY_OK) == legal, "n_lines=%u -> %d", nl[i], (int) st);
        if (nl[i] == 0 || nl[i] > PAY_MAX_LINES) CHECK(st == PAY_ERR_ARG, "n_lines=%u ARG", nl[i]);
        if (nl[i] == 1 || nl[i] == PAY_MAX_LINES - 1)
            CHECK(st == PAY_ERR_UNBALANCED, "odd n_lines=%u unbalanced", nl[i]);
    }
    /* per-line delta bounds, signed: {-2^59-1, -2^59, -1, 0, 1, 2^59-1, 2^59, 2^59+1} */
    int64_t d[8];
    unsigned nd = tier_bounds_i64((int64_t) LMAX, d);
    for (unsigned i = 0; i < nd; i++) {
        mkreq(&rq, 1);
        rq.lines[0] = (pay_line_t){A, 0, 0};
        rq.lines[1] = (pay_line_t){B, 0, 0};
        rq.lines[0].d_debit = d[i];
        rq.lines[1].d_debit = d[i];
        rq.n_lines = 2;
        pay_status_t st = post_checked(&rq, "delta");
        if (d[i] > (int64_t) LMAX || d[i] < -(int64_t) LMAX)
            CHECK(st == PAY_ERR_OVERFLOW, "delta %lld beyond 2^59 -> OVERFLOW (%d)",
                  (long long) d[i], (int) st);
        else if (d[i] != 0)
            CHECK(st != PAY_OK, "delta %lld on both sides is unbalanced or unfunded",
                  (long long) d[i]);
    }
    /* account index at the table bound: {n-1, n, UINT32_MAX} */
    uint32_t idx[] = {L.n_accounts - 1, L.n_accounts, UINT32_MAX};
    for (unsigned i = 0; i < TIER_N(idx); i++) {
        mkreq(&rq, 1);
        rq.lines[0] = (pay_line_t){A, -1, 0};
        rq.lines[1] = (pay_line_t){idx[i], 1, 0};
        rq.n_lines = 2;
        pay_status_t st = post_checked(&rq, "acct index");
        if (idx[i] >= L.n_accounts)
            CHECK(st == PAY_ERR_NO_ACCOUNT, "account %u out of range -> NO_ACCOUNT", idx[i]);
        else
            CHECK(st == PAY_OK || st == PAY_ERR_CROWN || st == PAY_ERR_UNBALANCED,
                  "last account usable");
    }
}

static void axiom_identifiers(void)
{
    pay_posting_req_t rq;
    /* e2e length: {0, 1, MAX-1, MAX, MAX+1} */
    uint64_t lens[5];
    unsigned nl = tier_bounds_u64(PAY_E2E_MAX, lens);
    for (unsigned i = 0; i < nl; i++) {
        mkreq(&rq, 1);
        char e[64];
        int k = snprintf(e, sizeof e, "%llu-", (unsigned long long) g_ctr);
        while (k < 48) e[k++] = 'E';
        e[lens[i] < 48 ? lens[i] : 47] = 0;
        if (lens[i] > PAY_E2E_MAX) { /* the field cannot hold it: fill to the brim, unterminated */
            memset(rq.e2e, 'Z', sizeof rq.e2e);
        } else
            memcpy(rq.e2e, e, lens[i] + 1);
        rq.lines[0] = (pay_line_t){A, -1, 0};
        rq.lines[1] = (pay_line_t){B, 1, 0};
        rq.n_lines = 2;
        pay_status_t st = post_checked(&rq, "e2e length");
        bool legal = lens[i] >= 1 && lens[i] <= PAY_E2E_MAX;
        CHECK((st == PAY_OK) == legal, "e2e length %llu -> %d", (unsigned long long) lens[i],
              (int) st);
    }
    /* UETR: empty, wrong length, wrong version digit */
    const char *bad[] = {"",
                         "00000000-0000-4000-8000-00000000000",
                         "00000000-0000-3000-8000-000000000000",
                         "00000000-0000-4000-c000-000000000000",
                         "zzzzzzzz-0000-4000-8000-000000000000",
                         "00000000-0000-4000-8000-00000000000G",
                         "00000000-0000-4000-8000-00000000000A"};
    for (unsigned i = 0; i < TIER_N(bad); i++) {
        mkreq(&rq, 1);
        memset(rq.uetr, 0, sizeof rq.uetr);
        memcpy(rq.uetr, bad[i], strlen(bad[i]) < PAY_UETR_LEN ? strlen(bad[i]) : PAY_UETR_LEN);
        rq.lines[0] = (pay_line_t){A, -1, 0};
        rq.lines[1] = (pay_line_t){B, 1, 0};
        rq.n_lines = 2;
        CHECK(post_checked(&rq, "uetr") == PAY_ERR_UETR, "malformed UETR %u refused", i);
    }
    /* ext_cap / kind at their enum bounds */
    uint8_t caps[] = {PAY_CAP_COUNT - 1, PAY_CAP_COUNT, 255};
    for (unsigned i = 0; i < TIER_N(caps); i++) {
        mkreq(&rq, 1);
        rq.ext_cap = caps[i];
        rq.lines[0] = (pay_line_t){A, -1, 0};
        rq.lines[1] = (pay_line_t){B, 1, 0};
        rq.n_lines = 2;
        pay_status_t st = post_checked(&rq, "ext_cap");
        CHECK((st == PAY_OK) == (caps[i] < PAY_CAP_COUNT), "ext_cap %u", caps[i]);
    }
    uint8_t kinds[] = {PAY_KIND_ADJUST, PAY_KIND_ADJUST + 1, 255};
    for (unsigned i = 0; i < TIER_N(kinds); i++) {
        mkreq(&rq, 1);
        rq.kind = kinds[i];
        rq.lines[0] = (pay_line_t){A, -1, 0};
        rq.lines[1] = (pay_line_t){B, 1, 0};
        rq.n_lines = 2;
        pay_status_t st = post_checked(&rq, "kind");
        CHECK((st == PAY_OK) == (kinds[i] <= PAY_KIND_ADJUST), "kind %u", kinds[i]);
    }
    /* externality accumulator: MAX then MAX+1 */
    mkreq(&rq, 1);
    rq.ext_cap = PAY_CAP_NATURAL;
    rq.ext_value = INT64_MAX - L.externality[PAY_CAP_NATURAL];
    rq.lines[0] = (pay_line_t){A, -1, 0};
    rq.lines[1] = (pay_line_t){B, 1, 0};
    rq.n_lines = 2;
    CHECK(post_checked(&rq, "ext MAX") == PAY_OK, "externality to INT64_MAX");
    mkreq(&rq, 1);
    rq.ext_cap = PAY_CAP_NATURAL;
    rq.ext_value = 1;
    rq.lines[0] = (pay_line_t){A, -1, 0};
    rq.lines[1] = (pay_line_t){B, 1, 0};
    rq.n_lines = 2;
    CHECK(post_checked(&rq, "ext MAX+1") == PAY_ERR_OVERFLOW, "externality MAX+1 refused");

    /* replay: the same key + same request is a DUPLICATE, posts nothing */
    mkreq(&rq, 1);
    rq.lines[0] = (pay_line_t){A, -1, 0};
    rq.lines[1] = (pay_line_t){B, 1, 0};
    rq.n_lines = 2;
    CHECK(post_checked(&rq, "first") == PAY_OK, "first post");
    SNAP = L;
    CHECK(pay_ledger_post(&L, &rq, NULL) == PAY_DUPLICATE && unchanged(), "replay is DUPLICATE");
    rq.memo[0] = 'x';
    CHECK(post_checked(&rq, "replay-diff") == PAY_ERR_REPLAY, "same key, different request");
    /* nonce: 0 = none; strictly increasing per initiator */
    uint64_t nonces[] = {5, 5, 4, 6, UINT64_MAX, UINT64_MAX};
    pay_status_t want[] = {PAY_OK, PAY_ERR_REPLAY, PAY_ERR_REPLAY, PAY_OK, PAY_OK, PAY_ERR_REPLAY};
    for (unsigned i = 0; i < TIER_N(nonces); i++) {
        mkreq(&rq, 77);
        rq.nonce = nonces[i];
        rq.lines[0] = (pay_line_t){A, -1, 0};
        rq.lines[1] = (pay_line_t){B, 1, 0};
        rq.n_lines = 2;
        CHECK(post_checked(&rq, "nonce") == want[i], "nonce %llu -> %d",
              (unsigned long long) nonces[i], (int) want[i]);
    }
}

static void call_verify_null(void *u)
{
    (void) u;
    (void) pay_ledger_verify_chain(NULL);
}
static void call_totals_null(void *u)
{
    uint64_t d, c;
    int64_t e;
    (void) u;
    pay_ledger_totals(NULL, 0, PAY_CAP_FINANCIAL, &d, &c, &e);
}
static void call_conserved_null(void *u)
{
    (void) u;
    (void) pay_commons_conserved(NULL);
}

static void axiom_policy(void)
{
    pay_posting_req_t rq;
    /* crown forms never change hands; same-owner moves are fine */
    mkreq(&rq, 1);
    CHECK(pay_ledger_issue(&L, &rq, CR1, CR1B, 10, NULL) == PAY_OK, "crown same-owner issue");
    mkreq(&rq, 1);
    SNAP = L;
    CHECK(pay_ledger_transfer(&L, &rq, CR1B, CR2, 1, NULL) == PAY_ERR_CROWN && unchanged(),
          "crown across owners refused");
    /* frozen: no outgoing debit, incoming fine */
    mkreq(&rq, 1);
    CHECK(pay_ledger_transfer(&L, &rq, A, FRZ, 1, NULL) == PAY_OK, "credit a frozen account");
    mkreq(&rq, 4);
    SNAP = L;
    CHECK(pay_ledger_transfer(&L, &rq, FRZ, A, 1, NULL) == PAY_ERR_POLICY && unchanged(),
          "debit from a frozen account refused");
    /* CREDIT only on issuers */
    mkreq(&rq, 1);
    rq.lines[0] = (pay_line_t){A, 1, 0};
    rq.lines[1] = (pay_line_t){B, 0, 1};
    rq.n_lines = 2;
    CHECK(post_checked(&rq, "credit holder") == PAY_ERR_CREDIT, "CREDIT on a holder refused");
    CHECK(pay_ledger_set_flags(&L, ISS, 0) == PAY_ERR_CREDIT, "issuer with claims keeps ISSUER");
    CHECK(pay_ledger_set_flags(&L, L.n_accounts, 0) == PAY_ERR_NO_ACCOUNT, "set_flags bound");
    /* cross-asset two-liner is unbalanced per (asset, cap) */
    uint16_t eur;
    uint32_t E;
    pay_ledger_add_fiat(&L, "EUR", 978, 2, &eur);
    pay_ledger_open(&L, 2, eur, PAY_CAP_FINANCIAL, 0, &E);
    mkreq(&rq, 1);
    rq.lines[0] = (pay_line_t){A, -1, 0};
    rq.lines[1] = (pay_line_t){E, 1, 0};
    rq.n_lines = 2;
    CHECK(post_checked(&rq, "cross-asset") == PAY_ERR_UNBALANCED, "cross-asset unbalanced");
    /* reversal: unknown, then twice */
    mkreq(&rq, 1);
    CHECK(pay_ledger_reverse(&L, &rq, "ffffffff-0000-4000-8000-000000000000", NULL) ==
              PAY_ERR_NOT_FOUND,
          "reverse unknown UETR");
    mkreq(&rq, 1);
    CHECK(pay_ledger_transfer(&L, &rq, A, B, 3, NULL) == PAY_OK, "to reverse");
    char u[PAY_UETR_LEN + 1];
    memcpy(u, rq.uetr, sizeof u);
    mkreq(&rq, 1);
    CHECK(pay_ledger_reverse(&L, &rq, u, NULL) == PAY_OK && healthy(), "reverse once");
    mkreq(&rq, 1);
    CHECK(pay_ledger_reverse(&L, &rq, u, NULL) == PAY_ERR_STATE, "reverse twice refused");
    /* NULL / zero-length inputs */
    CHECK(pay_ledger_post(NULL, &rq, NULL) == PAY_ERR_ARG, "post NULL ledger");
    CHECK(pay_ledger_post(&L, NULL, NULL) == PAY_ERR_ARG, "post NULL request");
    CHECK(pay_ledger_transfer(&L, NULL, A, B, 1, NULL) == PAY_ERR_ARG, "transfer NULL request");
    CHECK(pay_ledger_reverse(&L, &rq, NULL, NULL) == PAY_ERR_NOT_FOUND, "reverse NULL uetr");
    CHECK(!pay_ledger_check(NULL), "check NULL");
    CHECK(pay_ledger_find_uetr(&L, NULL) == NULL && pay_ledger_find_uetr(NULL, "x") == NULL,
          "find_uetr NULL");
    CHECK(pay_ledger_account(&L, L.n_accounts) == NULL && pay_ledger_asset(&L, L.n_assets) == NULL,
          "lookups at the table bound");
    CHECK_KNOWN("F-PAY-NULL", !tier_crashes(call_verify_null, NULL),
                "pay_ledger_verify_chain(NULL) must refuse, not crash");
    CHECK_KNOWN("F-PAY-NULL", !tier_crashes(call_totals_null, NULL),
                "pay_ledger_totals(NULL, ...) must refuse, not crash");
    CHECK_KNOWN("F-PAY-NULL", !tier_crashes(call_conserved_null, NULL),
                "pay_commons_conserved(NULL) must refuse, not crash");
}

static void axiom_registries(void)
{
    static pay_ledger_t R;
    pay_ledger_init(&R, NULL);
    /* fiat numeric {0, 1, 998, 999, 1000} and the three rail numerics */
    uint16_t nums[] = {0, 1, 998, 999, 1000, 555, 777, 888};
    const char *codes[] = {"AAA", "AAB", "AAC", "AAD", "AAE", "AAF", "AAG", "AAH"};
    for (unsigned i = 0; i < TIER_N(nums); i++) {
        pay_status_t st = pay_ledger_add_fiat(&R, codes[i], nums[i], 2, NULL);
        bool legal =
            nums[i] >= 1 && nums[i] <= 999 && nums[i] != 555 && nums[i] != 777 && nums[i] != 888;
        CHECK((st == PAY_OK) == legal, "fiat numeric %u", nums[i]);
    }
    uint8_t minors[] = {0, 4, 5, 255};
    const char *mc[] = {"MAA", "MAB", "MAC", "MAD"};
    for (unsigned i = 0; i < TIER_N(minors); i++)
        CHECK((pay_ledger_add_fiat(&R, mc[i], 100 + i, minors[i], NULL) == PAY_OK) ==
                  (minors[i] <= 4),
              "fiat minor %u", minors[i]);
    const char *alphas[] = {"", "U", "US", "USDX", "usd", "U1D", NULL};
    for (unsigned i = 0; i < TIER_N(alphas); i++)
        CHECK(pay_ledger_add_fiat(&R, alphas[i], 300, 2, NULL) == PAY_ERR_ARG, "fiat alpha %u", i);
    CHECK(pay_ledger_add_fiat(&R, "AAB", 2, 2, NULL) == PAY_ERR_STATE, "duplicate code refused");
    /* generic code length {0, 1, MAX, MAX+1} */
    char code[PAY_CODE_MAX + 3];
    uint64_t cl[5];
    unsigned ncl = tier_bounds_u64(PAY_CODE_MAX, cl);
    for (unsigned i = 0; i < ncl; i++) {
        memset(code, 'Q', sizeof code);
        code[0] = (char) ('a' + i);
        code[cl[i]] = 0;
        pay_status_t st = pay_ledger_add_asset(&R, code, 0, PAY_ASSET_UNIT, NULL);
        CHECK((st == PAY_OK) == (cl[i] >= 1 && cl[i] <= PAY_CODE_MAX), "asset code length %llu",
              (unsigned long long) cl[i]);
    }
    /* DTI: length 8, 9, 10 and the excluded vowels */
    CHECK(pay_dti_format_ok("4H95J0R2X") && !pay_dti_format_ok("4H95J0R2") &&
              !pay_dti_format_ok("4H95J0R2XX") && !pay_dti_format_ok("4H95J0R2A") &&
              !pay_dti_format_ok("4H95J0R2x") && !pay_dti_format_ok(NULL),
          "DTI format bounds");
    /* asset table at 100%: fill, then one more fails closed */
    unsigned added = 0;
    for (unsigned i = 0; R.n_assets < PAY_MAX_ASSETS; i++) {
        char c[8];
        snprintf(c, sizeof c, "F%04u", i);
        if (pay_ledger_add_asset(&R, c, 0, PAY_ASSET_UNIT, NULL) == PAY_OK) added++;
    }
    uint32_t before = R.n_assets;
    CHECK(pay_ledger_add_asset(&R, "FULL", 0, PAY_ASSET_UNIT, NULL) == PAY_ERR_FULL &&
              R.n_assets == before && before == PAY_MAX_ASSETS,
          "asset table full -> FULL, nothing written");
    /* account table at 100% */
    pay_ledger_init(&R, NULL);
    uint32_t id = 0;
    for (uint32_t i = 0; i < PAY_MAX_ACCOUNTS; i++)
        if (pay_ledger_open(&R, i, 0, PAY_CAP_FINANCIAL, 0, &id) != PAY_OK) break;
    CHECK(R.n_accounts == PAY_MAX_ACCOUNTS && id == PAY_MAX_ACCOUNTS - 1, "512 accounts open");
    CHECK(pay_ledger_open(&R, 9, 0, PAY_CAP_FINANCIAL, 0, &id) == PAY_ERR_FULL &&
              R.n_accounts == PAY_MAX_ACCOUNTS,
          "account table full -> FULL");
    CHECK(pay_ledger_open(&R, 9, R.n_assets, PAY_CAP_FINANCIAL, 0, NULL) == PAY_ERR_NO_ASSET,
          "open on asset index = n_assets");
    CHECK(pay_ledger_open(&R, 9, 0, PAY_CAP_COUNT, 0, NULL) == PAY_ERR_ARG, "open cap = COUNT");
    (void) added;
}

/* PAY_MAX_PRINCIPALS nonce owners, then one more; and the journal ring wrap. */
static void axiom_capacity(void)
{
    static pay_ledger_t S;
    pay_posting_req_t rq;
    uint32_t iss, h1, h2;
    pay_ledger_init(&S, NULL);
    pay_ledger_open(&S, 100, 0, PAY_CAP_FINANCIAL, PAY_ACCT_ISSUER, &iss);
    pay_ledger_open(&S, 1, 0, PAY_CAP_FINANCIAL, 0, &h1);
    pay_ledger_open(&S, 2, 0, PAY_CAP_FINANCIAL, 0, &h2);
    char first_uetr[PAY_UETR_LEN + 1];
    mkreq(&rq, 100);
    memcpy(first_uetr, rq.uetr, sizeof first_uetr); /* seq 0: evicted by the wrap */
    CHECK(pay_ledger_issue(&S, &rq, iss, h1, 1u << 20, NULL) == PAY_OK, "seed");
    for (uint32_t p = 0; p < PAY_MAX_PRINCIPALS; p++) {
        mkreq(&rq, 1000 + p);
        rq.nonce = 1;
        if (pay_ledger_transfer(&S, &rq, h1, h2, 1, NULL) != PAY_OK) {
            CHECK(0, "principal %u refused early", p);
            break;
        }
    }
    CHECK(S.n_nonce == PAY_MAX_PRINCIPALS, "nonce table at 100%%");
    mkreq(&rq, 999999);
    rq.nonce = 1;
    uint64_t seq = S.seq;
    CHECK(pay_ledger_transfer(&S, &rq, h1, h2, 1, NULL) == PAY_ERR_FULL && S.seq == seq,
          "a new principal when the nonce table is full fails closed");
    mkreq(&rq, 1000);
    rq.nonce = 2;
    CHECK(pay_ledger_transfer(&S, &rq, h1, h2, 1, NULL) == PAY_OK, "a known principal still posts");
    /* journal ring: push past PAY_JOURNAL_MAX, chain stays verifiable */
    CHECK(pay_ledger_find_uetr(&S, first_uetr) != NULL, "seq 0 findable before the wrap");
    while (S.seq < PAY_JOURNAL_MAX + 3) {
        mkreq(&rq, 1);
        if (pay_ledger_transfer(&S, &rq, h1, h2, 1, NULL) != PAY_OK) {
            CHECK(0, "ring fill refused at seq %llu", (unsigned long long) S.seq);
            break;
        }
    }
    CHECK(pay_ledger_verify_chain(&S) && pay_ledger_check(&S), "chain verifies after the wrap");
    CHECK(pay_ledger_find_uetr(&S, first_uetr) == NULL,
          "the oldest record left the window (documented R2 window)");
    /* the oldest record still in the window is still deduplicated */
    const pay_journal_t *oldest = &S.journal[S.seq & (PAY_JOURNAL_MAX - 1)];
    CHECK(oldest->used && oldest->seq == S.seq - PAY_JOURNAL_MAX, "slot reuse is seq mod 1024");
}

/* ===== pay_tithe ===== */

/* 64x64 -> 128 and a 192-bit compare, for the independent isqrt oracle. */
typedef unsigned __int128 u128;
static void mul128(u128 a, u128 b, u128 *hi, u128 *lo) /* a, b < 2^96 */
{
    u128 m = ((u128) 1 << 64) - 1;
    u128 a0 = a & m, a1 = a >> 64, b0 = b & m, b1 = b >> 64;
    u128 p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1;
    u128 mid = (p00 >> 64) + (p01 & m) + (p10 & m);
    *lo = (p00 & m) | (mid << 64);
    *hi = p11 + (p01 >> 64) + (p10 >> 64) + (mid >> 64);
}
static int cmp256(u128 ah, u128 al, u128 bh, u128 bl)
{
    if (ah != bh) return ah < bh ? -1 : 1;
    if (al != bl) return al < bl ? -1 : 1;
    return 0;
}

static void check_tithe(uint64_t a)
{
    pay_u128 s = pay_floor_a_sqrt5(a);
    u128 S = ((u128) s.hi << 64) | s.lo;
    u128 fh, fl, sh, sl, th, tl;
    /* 5 a^2 as 256-bit: (5a) * a */
    mul128((u128) a * 5u, (u128) a, &fh, &fl);
    mul128(S, S, &sh, &sl);
    mul128(S + 1, S + 1, &th, &tl);
    CHECK(cmp256(sh, sl, fh, fl) <= 0 && cmp256(fh, fl, th, tl) < 0,
          "floor(a*sqrt5) is the integer square root of 5a^2 (a=%llu)", (unsigned long long) a);
    u128 t = ((u128) a + S) / 200u;
    CHECK(pay_tithe_phi(a) == (uint64_t) t, "tithe(%llu) = floor((a + isqrt(5a^2))/200)",
          (unsigned long long) a);
}

static void axiom_tithe(void)
{
    for (unsigned i = 0; i < TIER_N(TIER_U64_EDGES); i++) check_tithe(TIER_U64_EDGES[i]);
    /* around the first non-zero tithe: floor(a*phi/100) >= 1 iff a >= 62 */
    for (uint64_t a = 0; a < 400; a++) check_tithe(a);
    CHECK(pay_tithe_phi(61) == 0 && pay_tithe_phi(62) == 1, "first unit of tithe at a=62");
    uint64_t seed = 0xC0FFEE;
    for (int i = 0; i < 2000; i++) check_tithe(tier_rand(&seed) >> (tier_rand(&seed) & 63));

    pay_tithe_policy_t p;
    pay_tithe_policy_default(&p);
    pay_tithe_result_t r;
    /* unit at its enum bound */
    uint32_t units[] = {PAY_UNIT_MONEY, PAY_UNIT_STORAGE, PAY_UNIT_KIND_COUNT, UINT32_MAX};
    for (unsigned i = 0; i < TIER_N(units); i++) {
        pay_tithe_status_t st =
            pay_tithe_compute(&p, (pay_unit_kind_t) units[i], 1000, NULL, NULL, &r);
        CHECK((st >= 0) == (units[i] < PAY_UNIT_KIND_COUNT), "tithe unit %u", units[i]);
    }
    CHECK(pay_tithe_compute(NULL, PAY_UNIT_MONEY, 1, NULL, NULL, &r) == PAY_TITHE_ERR_ARG &&
              pay_tithe_compute(&p, PAY_UNIT_MONEY, 1, NULL, NULL, NULL) == PAY_TITHE_ERR_ARG,
          "tithe NULL policy/out");
    /* amounts over the u64 edges: default contribution is exactly the tithe */
    for (unsigned i = 0; i < TIER_N(TIER_U64_EDGES); i++) {
        uint64_t a = TIER_U64_EDGES[i];
        CHECK(pay_tithe_compute(&p, PAY_UNIT_MONEY, a, NULL, NULL, &r) == PAY_TITHE_OK &&
                  r.contribution == r.tithe && r.excess == 0 && r.shortfall == 0,
              "default contribution = tithe (a=%llu)", (unsigned long long) a);
    }
    /* RATE contribution: zero denominator, overflow, exact floor */
    pay_contrib_t c = {PAY_CONTRIB_RATE, {1, 0}, 0};
    CHECK(pay_tithe_compute(&p, PAY_UNIT_MONEY, 100, &c, NULL, &r) == PAY_TITHE_ERR_RATE,
          "rate den 0");
    c.rate = (pay_rat_t){2, 1};
    CHECK(pay_tithe_compute(&p, PAY_UNIT_MONEY, UINT64_MAX, &c, NULL, &r) == PAY_TITHE_ERR_OVERFLOW,
          "rate 2/1 of UINT64_MAX overflows");
    c.rate = (pay_rat_t){1, 1};
    CHECK(pay_tithe_compute(&p, PAY_UNIT_MONEY, UINT64_MAX, &c, NULL, &r) == PAY_TITHE_OK &&
              r.contribution == UINT64_MAX && r.excess == UINT64_MAX - r.tithe &&
              r.vfv_credit == r.excess,
          "rate 1/1 of UINT64_MAX: whole amount, excess credited 1:1");
    c.rate = (pay_rat_t){3, 100};
    CHECK(pay_tithe_compute(&p, PAY_UNIT_MONEY, 1000, &c, NULL, &r) == PAY_TITHE_OK &&
              r.contribution == 30 && r.excess == 30 - r.tithe,
          "rate 3/100 of 1000 = 30");
    /* ABSOLUTE around the tithe: {t-1, t, t+1} with and without allow_below_phi */
    uint64_t amt = 100000, t = pay_tithe_phi(amt);
    uint64_t abs_v[] = {0, t - 1, t, t + 1, UINT64_MAX};
    for (int allow = 0; allow < 2; allow++)
        for (unsigned i = 0; i < TIER_N(abs_v); i++) {
            p.allow_below_phi = allow;
            pay_contrib_t ca = {PAY_CONTRIB_ABSOLUTE, {0, 1}, abs_v[i]};
            pay_tithe_status_t st = pay_tithe_compute(&p, PAY_UNIT_MONEY, amt, &ca, NULL, &r);
            CHECK(st == PAY_TITHE_OK, "absolute %llu", (unsigned long long) abs_v[i]);
            if (abs_v[i] < t) {
                CHECK(r.contribution == (allow ? abs_v[i] : t),
                      "below tithe: raised unless allowed");
                CHECK(r.shortfall == (allow ? t - abs_v[i] : 0) && r.vfv_credit == 0,
                      "shortfall recorded, no credit");
            } else
                CHECK(r.contribution == abs_v[i] && r.excess == abs_v[i] - t &&
                          r.vfv_credit == r.excess,
                      "at/above tithe: excess earns credit 1:1");
        }
    pay_tithe_policy_default(&p);
    /* compute unit: rate unset -> excess taken, no credit */
    pay_contrib_t ca = {PAY_CONTRIB_ABSOLUTE, {0, 1}, 500};
    CHECK(pay_tithe_compute(&p, PAY_UNIT_COMPUTE, 1000, &ca, NULL, &r) == PAY_TITHE_RATE_UNSET &&
              r.contribution == 500 && r.vfv_credit == 0,
          "compute unit, rate unset");
    pay_rat_t z = {1, 0};
    CHECK(pay_tithe_compute(&p, PAY_UNIT_MONEY, 1000, &ca, &z, &r) == PAY_TITHE_ERR_RATE,
          "posted rate den 0");
    p.credit_mult.den = 0;
    CHECK(pay_tithe_compute(&p, PAY_UNIT_MONEY, 1000, &ca, NULL, &r) == PAY_TITHE_ERR_RATE,
          "credit multiplier den 0");
    pay_tithe_policy_default(&p);
    pay_contrib_t bad = {(pay_contrib_mode_t) 7, {0, 1}, 0};
    CHECK(pay_tithe_compute(&p, PAY_UNIT_MONEY, 1000, &bad, NULL, &r) == PAY_TITHE_ERR_ARG,
          "unknown contribution mode");
}

static void axiom_commons(void)
{
    pay_commons_t c;
    pay_commons_init(&c);
    CHECK(pay_commons_deposit(&c, PAY_UNIT_COMPUTE, UINT64_MAX - 1), "deposit MAX-1");
    CHECK(pay_commons_deposit(&c, PAY_UNIT_COMPUTE, 1), "deposit to MAX");
    pay_commons_t snap = c;
    CHECK(!pay_commons_deposit(&c, PAY_UNIT_COMPUTE, 1) && memcmp(&c, &snap, sizeof c) == 0,
          "deposit MAX+1 refused, no state");
    CHECK(!pay_commons_deposit(&c, PAY_UNIT_KIND_COUNT, 1) && !pay_commons_deposit(NULL, 0, 1),
          "deposit unit bound / NULL");
    CHECK(pay_commons_conserved(&c), "conserved");

    /* split: n at {0, 1, MAX, MAX+1}; totals over edges; sum(out) + left == total */
    uint64_t w[PAY_COMMONS_MAX_RECIPIENTS + 1], out[PAY_COMMONS_MAX_RECIPIENTS + 1];
    pay_rat_t cap = {8, 21};
    uint32_t ns[] = {0, 1, 2, PAY_COMMONS_MAX_RECIPIENTS, PAY_COMMONS_MAX_RECIPIENTS + 1};
    uint64_t totals[] = {0, 1, 2, 3, 1000, UINT32_MAX, (uint64_t) 1 << 62};
    for (unsigned a = 0; a < TIER_N(ns); a++)
        for (unsigned b = 0; b < TIER_N(totals); b++) {
            uint32_t n = ns[a];
            for (uint32_t i = 0; i <= PAY_COMMONS_MAX_RECIPIENTS; i++) {
                w[i] = (i % 3) + 1;
                out[i] = 0xDEAD;
            }
            uint64_t left = pay_commons_split(totals[b], w, n, cap, out);
            if (n == 0 || n > PAY_COMMONS_MAX_RECIPIENTS) {
                CHECK(left == totals[b] && out[0] == 0xDEAD, "split n=%u refused, out untouched",
                      n);
                continue;
            }
            u128 sum = left;
            uint64_t mx = 0;
            for (uint32_t i = 0; i < n; i++) {
                sum += out[i];
                if (out[i] > mx) mx = out[i];
            }
            CHECK(sum == totals[b], "split n=%u total=%llu conserves", n,
                  (unsigned long long) totals[b]);
            CHECK(out[n] == 0xDEAD, "split n=%u writes nothing past n", n);
            u128 capv = (u128) totals[b] * 8 / 21, eq = ((u128) totals[b] + n - 1) / n;
            CHECK(mx <= (capv > eq ? capv : eq), "no recipient above the cap (n=%u)", n);
        }
    for (uint32_t i = 0; i < 4; i++) w[i] = 0;
    CHECK(pay_commons_split(77, w, 4, cap, out) == 77 && out[0] == 0 && out[3] == 0,
          "all-zero weights: nothing handed out");
    CHECK(pay_commons_split(5, NULL, 2, cap, out) == 5, "split NULL weights");
    pay_commons_init(&c);
    pay_commons_deposit(&c, PAY_UNIT_BANDWIDTH, 1001);
    w[0] = 1;
    w[1] = 1;
    CHECK(pay_commons_allocate(&c, PAY_UNIT_BANDWIDTH, w, 2, cap, out) &&
              pay_commons_conserved(&c) && c.pool[PAY_UNIT_BANDWIDTH] + out[0] + out[1] == 1001,
          "allocate conserves the pool");
    CHECK(!pay_commons_allocate(&c, PAY_UNIT_KIND_COUNT, w, 2, cap, out), "allocate unit bound");
    CHECK(pay_usury_check(10, 10, false) == PAY_OK &&
              pay_usury_check(10, 11, false) == PAY_ERR_USURY &&
              pay_usury_check(10, 9, false) == PAY_OK &&
              pay_usury_check(10, 10, true) == PAY_ERR_USURY &&
              pay_usury_check(UINT64_MAX, UINT64_MAX, false) == PAY_OK &&
              pay_usury_check(0, 1, false) == PAY_ERR_USURY,
          "usury check at repaid = principal +/- 1");
}

int main(void)
{
    tier_begin("tier1/axioms_pay", KNOWN_FAILURES, TIER_N(KNOWN_FAILURES));
    setup();
    axiom_amount_bounds();
    axiom_line_bounds();
    axiom_identifiers();
    axiom_policy();
    CHECK(healthy(), "ledger healthy after every tier-1 probe");
    axiom_registries();
    axiom_capacity();
    axiom_tithe();
    axiom_commons();
    return tier_end();
}
