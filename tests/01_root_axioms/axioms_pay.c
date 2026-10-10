/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* axioms_pay.c — Tier 1 root axioms for kernel/src/pay: pay_ledger (the
 * three-rail ledger) and pay_assure (the exact 0.08889% assurance fee, its
 * sub-unit carry, the four-bucket split and the commons pool).
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
#include "pay_assure.h"

static const tier_known_t KNOWN_FAILURES[] = {
    {"F-PAY-NULL", "pay_ledger_verify_chain/pay_ledger_totals/pay_commons_conserved "
                   "dereference a NULL ledger"},
    {"F-PAY-FEE0", "pay_ledger_pay_with_fee refuses (PAY_ERR_ARG) every payment whose fee and "
                   "excess are both 0, i.e. any payment below 1125 minor units on a fresh carry"},
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

/* ===== pay_assure: the fee ===== */
typedef unsigned __int128 u128;

/* the independent oracle: floor(g * 8889 / 10^7) in 128-bit */
static uint64_t fee_oracle(uint64_t g)
{
    return (uint64_t) ((u128) g * PAY_ASSURE_NUM / PAY_ASSURE_DEN);
}

static void check_fee(uint64_t g)
{
    CHECK(pay_assure_fee(g) == fee_oracle(g), "fee(%llu) = floor(g*8889/10^7)",
          (unsigned long long) g);
    uint64_t f = 7, r = 7;
    CHECK(pay_assure_fee_carry(0, g, &f, &r) && f == fee_oracle(g) &&
              r == (uint64_t) ((u128) g * PAY_ASSURE_NUM % PAY_ASSURE_DEN),
          "fee_carry(0, %llu) gives the fee and the exact remainder", (unsigned long long) g);
    uint64_t part[PAY_ASSURE_BUCKETS];
    pay_assure_split(f, part);
    u128 sum = 0;
    bool each = true;
    for (int i = 0; i < PAY_ASSURE_BUCKETS; i++) {
        sum += part[i];
        each &= part[i] <= f;
        if (i) each &= part[i] == (uint64_t) ((u128) f * pay_assure_bucket_pct[i] / 100);
    }
    CHECK(sum == f && each, "split(%llu) is the exact 50/25/15/10 partition",
          (unsigned long long) f);
}

static void axiom_fee(void)
{
    for (unsigned i = 0; i < TIER_N(TIER_U64_EDGES); i++) check_fee(TIER_U64_EDGES[i]);
    for (uint64_t g = 0; g < 3000; g++) check_fee(g);
    uint64_t seed = 0xC0FFEE;
    for (int i = 0; i < 2000; i++) check_fee(tier_rand(&seed) >> (tier_rand(&seed) & 63));
    CHECK(pay_assure_fee(1124) == 0 && pay_assure_fee(1125) == 1, "first whole unit at g=1125");
    CHECK(pay_assure_fee(UINT64_MAX) == fee_oracle(UINT64_MAX) &&
              pay_assure_fee(UINT64_MAX) < UINT64_MAX,
          "fee(UINT64_MAX) fits and is below the gross");
    unsigned pct = 0;
    for (int i = 0; i < PAY_ASSURE_BUCKETS; i++) pct += pay_assure_bucket_pct[i];
    CHECK(pct == 100 && pay_assure_bucket_pct[PAY_ASSURE_RESERVE_FLOOR] == 50 &&
              pay_assure_bucket_pct[PAY_ASSURE_REGEN_CAPITAL] == 10 &&
              pay_assure_bucket_name[PAY_ASSURE_BUCKETS - 1] != NULL,
          "bucket shares sum to 100");
    pay_assure_split(5, NULL); /* NULL out is a no-op */

    /* F2 carry: the remainder bound, and outputs untouched when refused */
    uint64_t f = 77, r = 77;
    CHECK(!pay_assure_fee_carry(PAY_ASSURE_DEN, 1, &f, &r) && f == 77 && r == 77,
          "carry = 10^7 refused, outputs untouched");
    CHECK(pay_assure_fee_carry(PAY_ASSURE_DEN - 1, 1125, &f, &r) &&
              f == ((u128) 1125 * PAY_ASSURE_NUM + PAY_ASSURE_DEN - 1) / PAY_ASSURE_DEN &&
              r == ((u128) 1125 * PAY_ASSURE_NUM + PAY_ASSURE_DEN - 1) % PAY_ASSURE_DEN,
          "carry = 10^7 - 1 accepted");
    CHECK(pay_assure_fee_carry(PAY_ASSURE_DEN - 1, UINT64_MAX, &f, NULL) &&
              f == (uint64_t) (((u128) UINT64_MAX * PAY_ASSURE_NUM + PAY_ASSURE_DEN - 1) /
                               PAY_ASSURE_DEN),
          "the largest carry on the largest gross does not wrap");
    uint64_t r2 = 99;
    CHECK(pay_assure_fee_carry(5, 1125, NULL, &r2) &&
              r2 == (1125 * PAY_ASSURE_NUM + 5) % PAY_ASSURE_DEN,
          "fee_carry with a NULL fee output still reports the remainder");
    pay_assure_carry_t c = {0};
    CHECK(pay_assure_charge(&c, 2250, NULL) && c.rem == 2250 * PAY_ASSURE_NUM - 2 * PAY_ASSURE_DEN,
          "charge with a NULL fee output still advances the carry");
    CHECK(!pay_assure_charge(NULL, 1, &f), "charge NULL carry");
    c.rem = PAY_ASSURE_DEN;
    f = 5;
    CHECK(!pay_assure_charge(&c, 1000, &f) && c.rem == PAY_ASSURE_DEN && f == 5,
          "a corrupt carry is refused and left as it was");
    /* micro-payments pay exactly the fee on their sum */
    for (int run = 0; run < 50; run++) {
        c.rem = 0;
        uint64_t total = 0, paid = 0;
        for (int k = 0; k < 400; k++) {
            uint64_t g = tier_rand(&seed) % (run < 25 ? 2000 : 1000000000);
            uint64_t fk;
            CHECK(pay_assure_charge(&c, g, &fk), "charge");
            total += g;
            paid += fk;
        }
        CHECK(paid == fee_oracle(total) && c.rem < PAY_ASSURE_DEN,
              "400 charges pay floor(sum*8889/10^7) exactly (run %d)", run);
    }
    c.rem = 0;
    uint64_t one = 0, acc = 0;
    for (int k = 0; k < 1125; k++) {
        pay_assure_charge(&c, 1, &one);
        acc += one;
    }
    CHECK(acc == 1 && c.rem == (uint64_t) 1125 * PAY_ASSURE_NUM - PAY_ASSURE_DEN,
          "1125 payments of 1 pay exactly one unit, and keep the rest as carry");

    /* compute: policy, units, contributions */
    pay_assure_policy_t p;
    pay_assure_policy_default(&p);
    pay_assure_result_t res;
    uint32_t units[] = {PAY_UNIT_MONEY, PAY_UNIT_STORAGE, PAY_UNIT_KIND_COUNT, UINT32_MAX};
    for (unsigned i = 0; i < TIER_N(units); i++) {
        pay_assure_status_t st =
            pay_assure_compute(&p, (pay_unit_kind_t) units[i], 1000000, NULL, NULL, &res);
        CHECK((st >= 0) == (units[i] < PAY_UNIT_KIND_COUNT), "fee unit %u", units[i]);
    }
    CHECK(pay_assure_compute(NULL, PAY_UNIT_MONEY, 1, NULL, NULL, &res) == PAY_ASSURE_ERR_ARG &&
              pay_assure_compute(&p, PAY_UNIT_MONEY, 1, NULL, NULL, NULL) == PAY_ASSURE_ERR_ARG,
          "compute NULL policy/out");
    for (unsigned i = 0; i < TIER_N(TIER_U64_EDGES); i++) {
        uint64_t a = TIER_U64_EDGES[i];
        CHECK(pay_assure_compute(&p, PAY_UNIT_MONEY, a, NULL, NULL, &res) == PAY_ASSURE_OK &&
                  res.amount == a && res.fee == fee_oracle(a) && res.contribution == res.fee &&
                  res.excess == 0 && res.shortfall == 0,
              "default contribution = fee (a=%llu)", (unsigned long long) a);
    }
    pay_contrib_t cf = {PAY_CONTRIB_FEE, {0, 0}, 123};
    CHECK(pay_assure_compute(&p, PAY_UNIT_MONEY, 1000000, &cf, NULL, &res) == PAY_ASSURE_OK &&
              res.contribution == 888 && res.excess == 0,
          "explicit FEE mode ignores rate and absolute");
    pay_contrib_t cr = {PAY_CONTRIB_RATE, {1, 0}, 0};
    CHECK(pay_assure_compute(&p, PAY_UNIT_MONEY, 100, &cr, NULL, &res) == PAY_ASSURE_ERR_RATE,
          "rate den 0");
    cr.rate = (pay_rat_t){2, 1};
    CHECK(pay_assure_compute(&p, PAY_UNIT_MONEY, UINT64_MAX, &cr, NULL, &res) ==
              PAY_ASSURE_ERR_OVERFLOW,
          "rate 2/1 of UINT64_MAX overflows");
    cr.rate = (pay_rat_t){1, 1};
    CHECK(pay_assure_compute(&p, PAY_UNIT_MONEY, UINT64_MAX, &cr, NULL, &res) == PAY_ASSURE_OK &&
              res.contribution == UINT64_MAX && res.excess == UINT64_MAX - res.fee &&
              res.vfv_credit == res.excess,
          "rate 1/1 of UINT64_MAX: whole amount, excess credited 1:1");
    cr.rate = (pay_rat_t){3, 100};
    CHECK(pay_assure_compute(&p, PAY_UNIT_MONEY, 1000, &cr, NULL, &res) == PAY_ASSURE_OK &&
              res.contribution == 30 && res.excess == 30 && res.fee == 0,
          "rate 3/100 of 1000 = 30 (fee 0)");
    /* ABSOLUTE around the fee: {f-1, f, f+1} with and without allow_below_fee */
    uint64_t amt = 100000000, t = pay_assure_fee(amt);
    CHECK(t == 88890, "fee(10^8) = 88890");
    uint64_t abs_v[] = {0, t - 1, t, t + 1, UINT64_MAX};
    for (int allow = 0; allow < 2; allow++)
        for (unsigned i = 0; i < TIER_N(abs_v); i++) {
            p.allow_below_fee = allow;
            pay_contrib_t ca = {PAY_CONTRIB_ABSOLUTE, {0, 1}, abs_v[i]};
            CHECK(pay_assure_compute(&p, PAY_UNIT_MONEY, amt, &ca, NULL, &res) == PAY_ASSURE_OK,
                  "absolute %llu", (unsigned long long) abs_v[i]);
            if (abs_v[i] < t) {
                CHECK(res.contribution == (allow ? abs_v[i] : t) && res.excess == 0,
                      "below the fee: raised unless allowed");
                CHECK(res.shortfall == (allow ? t - abs_v[i] : 0) && res.vfv_credit == 0,
                      "shortfall recorded, no credit");
            } else
                CHECK(res.contribution == abs_v[i] && res.excess == abs_v[i] - t &&
                          res.vfv_credit == res.excess && res.shortfall == 0,
                      "at/above the fee: excess earns credit 1:1");
        }
    pay_assure_policy_default(&p);
    pay_contrib_t ca = {PAY_CONTRIB_ABSOLUTE, {0, 1}, 500};
    CHECK(pay_assure_compute(&p, PAY_UNIT_COMPUTE, 1000, &ca, NULL, &res) ==
                  PAY_ASSURE_RATE_UNSET &&
              res.contribution == 500 && res.excess == 500 && res.vfv_credit == 0,
          "compute unit, rate unset: excess taken, no credit");
    pay_rat_t posted = {3, 2};
    CHECK(pay_assure_compute(&p, PAY_UNIT_COMPUTE, 1000, &ca, &posted, &res) == PAY_ASSURE_OK &&
              res.vfv_credit == 750,
          "a posted rate 3/2 overrides the unset default");
    p.credit_mult = (pay_rat_t){1, 3};
    CHECK(pay_assure_compute(&p, PAY_UNIT_MONEY, 1000, &ca, NULL, &res) == PAY_ASSURE_OK &&
              res.vfv_credit == 166,
          "credit multiplier 1/3: floor(500/3)");
    pay_rat_t z = {1, 0};
    CHECK(pay_assure_compute(&p, PAY_UNIT_MONEY, 1000, &ca, &z, &res) == PAY_ASSURE_ERR_RATE,
          "posted rate den 0");
    p.credit_mult.den = 0;
    CHECK(pay_assure_compute(&p, PAY_UNIT_MONEY, 1000, &ca, NULL, &res) == PAY_ASSURE_ERR_RATE,
          "credit multiplier den 0");
    pay_assure_policy_default(&p);
    pay_rat_t huge = {UINT64_MAX, 1};
    pay_contrib_t cb = {PAY_CONTRIB_ABSOLUTE, {0, 1}, 3};
    CHECK(pay_assure_compute(&p, PAY_UNIT_MONEY, 0, &cb, &huge, &res) == PAY_ASSURE_ERR_OVERFLOW,
          "excess * rate overflow refused");
    pay_contrib_t bad = {(pay_contrib_mode_t) 7, {0, 1}, 0};
    CHECK(pay_assure_compute(&p, PAY_UNIT_MONEY, 1000, &bad, NULL, &res) == PAY_ASSURE_ERR_ARG,
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

/* pay_ledger behaviours a mutation run showed unobserved (see
 * tests/MUTATION_REPORT.md): defaults, registry bounds, the request digest,
 * the invariant checker and chain verifier fed corrupted state, nonce slots,
 * per-account balance ceilings and the exact line counts of the helpers. */
static pay_status_t xfer(uint32_t from, uint32_t to, uint64_t x)
{
    pay_posting_req_t rq;
    mkreq(&rq, 1);
    return pay_ledger_transfer(&L, &rq, from, to, x, NULL);
}
static pay_status_t issue(uint32_t iss, uint32_t to, uint64_t x)
{
    pay_posting_req_t rq;
    mkreq(&rq, 1);
    return pay_ledger_issue(&L, &rq, iss, to, x, NULL);
}

static void axiom_pay_more(void)
{
    pay_posting_req_t rq, rq2;
    pay_receipt_t rc;
    uint16_t id;

    /* platform defaults and custom platforms */
    CHECK(pay_rail_code(PAY_RAIL_COUNT) == 0 && pay_rail_code(PAY_RAIL_DEBIT) == 555,
          "rail code of an unknown rail is 0");
    pay_platform_default(NULL);
    pay_platform_t pf;
    memset(&pf, 0xFF, sizeof pf);
    pay_platform_default(&pf);
    CHECK(strcmp(pf.vfv_alpha, "VFV") == 0 && pf.vfv_alpha[7] == 0 && pf.jurisdiction[7] == 0 &&
              !pf.jurisdiction_iso && pf.vfv_minor == 2,
          "platform defaults overwrite a dirty struct completely");
    pay_ledger_init(NULL, NULL);
    pay_platform_default(&pf);
    strcpy(pf.vfv_alpha, "QQQ");
    pay_ledger_init(&L, &pf);
    CHECK(strcmp(L.platform.vfv_alpha, "QQQ") == 0 &&
              strcmp(pay_ledger_asset(&L, L.vfv_asset)->code, "QQQ") == 0,
          "a custom platform code is used");
    pf.vfv_alpha[0] = 0;
    pay_ledger_init(&L, &pf);
    CHECK(strcmp(L.platform.vfv_alpha, "VFV") == 0, "an empty platform code becomes VFV");

    /* asset registry */
    pay_ledger_init(&L, NULL);
    CHECK(pay_ledger_add_fiat(&L, "VFV", 840, 2, &id) == PAY_ERR_POLICY,
          "fiat may not take the platform code");
    CHECK(pay_ledger_add_fiat(&L, "1BC", 840, 2, &id) == PAY_ERR_ARG &&
              pay_ledger_add_fiat(&L, "AB1", 840, 2, &id) == PAY_ERR_ARG,
          "fiat code: every one of the three letters is checked");
    CHECK(pay_ledger_add_asset(&L, "VFV", 2, PAY_ASSET_UNIT, &id) == PAY_ERR_STATE,
          "duplicate of asset 0 refused");
    CHECK(pay_ledger_add_asset(NULL, "U", 2, PAY_ASSET_UNIT, &id) == PAY_ERR_ARG &&
              pay_ledger_add_asset(&L, "U", 2, PAY_ASSET_FIAT, &id) == PAY_ERR_ARG &&
              pay_ledger_add_asset(&L, "U", 2, PAY_ASSET_PLATFORM, &id) == PAY_ERR_ARG,
          "add_asset NULL / FIAT / PLATFORM refused");
    CHECK(pay_ledger_add_asset(&L, "ABCDEFGHIJKL", 19, PAY_ASSET_UNIT, &id) == PAY_OK &&
              pay_ledger_add_asset(&L, "ABCDEFGHIJKLM", 2, PAY_ASSET_UNIT, &id) == PAY_ERR_ARG &&
              pay_ledger_add_asset(&L, "MINOR20", 20, PAY_ASSET_UNIT, &id) == PAY_ERR_ARG,
          "code of PAY_CODE_MAX and minor 19 accepted; one more of either refused");
    CHECK(pay_ledger_add_asset(&L, "SHR", 0, PAY_ASSET_SHARE, &id) == PAY_OK &&
              pay_ledger_asset(&L, id)->numeric == 0 && pay_ledger_asset(&L, id)->dti[0] == 0 &&
              !pay_ledger_asset(&L, id)->iso4217,
          "a non-fiat asset has numeric 0 and no DTI");
    CHECK(pay_ledger_add_crypto(NULL, "BTC", 8, NULL, &id) == PAY_ERR_ARG &&
              pay_ledger_add_crypto(&L, "BTC", 8, "X", &id) == PAY_ERR_ARG,
          "crypto NULL ledger / one-character DTI refused");
    CHECK(pay_ledger_add_crypto(&L, "BTC", 8, "4H95J0R2X", &id) == PAY_OK &&
              strcmp(pay_ledger_asset(&L, id)->dti, "4H95J0R2X") == 0 &&
              pay_ledger_asset(&L, id)->numeric == 0,
          "crypto DTI stored");
    CHECK(!pay_dti_format_ok("A2345678B") && !pay_dti_format_ok("B2345678E") &&
              pay_dti_format_ok("B2345678C"),
          "DTI: first and last characters are checked");
    uint32_t acct;
    CHECK(pay_ledger_open(&L, 9, 0, PAY_CAP_FINANCIAL, 0, NULL) == PAY_OK &&
              pay_ledger_open(&L, 9, 0, PAY_CAP_FINANCIAL, 0, &acct) == PAY_OK && acct == 1,
          "open with a NULL out pointer");
    L.acct[L.n_accounts].active = true; /* stale slot */
    CHECK(pay_ledger_account(&L, L.n_accounts) == NULL, "account lookup stops at n_accounts");
    L.acct[L.n_accounts].active = false;

    /* the request digest covers every line: same idempotency key, any line changed */
    setup();
    issue(ISS, A, 1000);
    mkreq(&rq, 1);
    CHECK(pay_ledger_transfer(&L, &rq, A, B, 5, NULL) == PAY_OK, "transfer");
    rq2 = rq;
    rq2.lines[0].d_debit = -6;
    rq2.lines[1].d_debit = 6;
    CHECK(pay_ledger_post(&L, &rq2, NULL) == PAY_ERR_REPLAY, "key reused, both lines changed");
    rq2 = rq;
    rq2.lines[1].account = COM;
    CHECK(pay_ledger_post(&L, &rq2, NULL) == PAY_ERR_REPLAY, "key reused, line 1 changed");
    rq2 = rq;
    rq2.lines[0].account = ISS;
    rq2.lines[0].d_debit = 0;
    rq2.lines[0].d_credit = 0;
    CHECK(pay_ledger_post(&L, &rq2, NULL) == PAY_ERR_REPLAY, "key reused, line 0 changed");
    CHECK(pay_ledger_post(&L, &rq, &rc) == PAY_DUPLICATE, "identical request is a duplicate");
    CHECK(pay_ledger_find_uetr(&L, rq.uetr)->n_lines == 2, "a transfer journals two lines");
    /* keys differing only in their last byte are different keys */
    mkreq(&rq2, 1);
    memcpy(rq2.idem_key, rq.idem_key, 32);
    rq2.idem_key[31] ^= 1;
    CHECK(pay_ledger_transfer(&L, &rq2, A, B, 5, NULL) == PAY_OK, "last key byte differs");
    /* same e2e under a new UETR */
    mkreq(&rq2, 1);
    strcpy(rq2.e2e, rq.e2e);
    CHECK(pay_ledger_transfer(&L, &rq2, A, B, 5, NULL) == PAY_ERR_DUP_UETR, "e2e reused");
    /* e2e character set: 0x20..0x7e, every character */
    const struct {
        const char *e;
        pay_status_t want;
    } E[] = {{"\x01"
              "abc",
              PAY_ERR_ARG},
             {"a\x01", PAY_ERR_ARG},
             {"a\x1f", PAY_ERR_ARG},
             {"a\x7f", PAY_ERR_ARG},
             {"a b", PAY_OK},
             {"a~", PAY_OK}};
    for (unsigned i = 0; i < TIER_N(E); i++) {
        mkreq(&rq2, 1);
        strcpy(rq2.e2e, E[i].e);
        CHECK(pay_ledger_transfer(&L, &rq2, A, B, 1, NULL) == E[i].want, "e2e case %u", i);
    }
    /* a refused posting leaves a zeroed receipt */
    mkreq(&rq2, 1);
    memset(&rc, 0x5A, sizeof rc);
    CHECK(pay_ledger_transfer(&L, &rq2, A, B, (uint64_t) 1 << 40, &rc) == PAY_ERR_FUNDS &&
              rc.seq == 0 && rc.hash[0] == 0 && rc.digest[PAY_HASH_LEN - 1] == 0,
          "refused: receipt zeroed");
    /* an all-zero idempotency key is a key like any other */
    mkreq(&rq2, 1);
    memset(rq2.idem_key, 0, 32);
    CHECK(pay_ledger_transfer(&L, &rq2, A, B, 1, NULL) == PAY_OK, "zero idempotency key");

    /* nonces: one slot per principal, every slot searched */
    setup();
    issue(ISS, A, 1000);
    uint32_t nn = L.n_nonce;
    for (uint32_t who = 0; who < 3; who++) {
        mkreq(&rq, who);
        rq.nonce = 5;
        CHECK(pay_ledger_transfer(&L, &rq, A, B, 1, NULL) == PAY_OK, "nonce 5 for %u", who);
    }
    for (uint32_t who = 0; who < 3; who++) {
        mkreq(&rq, who);
        rq.nonce = 5;
        CHECK(pay_ledger_transfer(&L, &rq, A, B, 1, NULL) == PAY_ERR_REPLAY, "nonce 5 again for %u",
              who);
        mkreq(&rq, who);
        rq.nonce = 6;
        CHECK(pay_ledger_transfer(&L, &rq, A, B, 1, NULL) == PAY_OK, "nonce 6 for %u", who);
    }
    CHECK(L.n_nonce == nn + 3, "three principals, three nonce slots");

    /* lines: a frozen account may take a zero line; balance groups; crown */
    mkreq(&rq, 1);
    rq.n_lines = 3;
    rq.lines[0] = (pay_line_t){A, -2, 0};
    rq.lines[1] = (pay_line_t){B, 2, 0};
    rq.lines[2] = (pay_line_t){FRZ, 0, 0};
    CHECK(post_checked(&rq, "frozen zero line") == PAY_OK, "a frozen account in a zero line");
    mkreq(&rq, 1);
    rq.n_lines = 3;
    rq.lines[0] = (pay_line_t){A, -2, 0};
    rq.lines[1] = (pay_line_t){B, 2, 0};
    rq.lines[2] = (pay_line_t){CR1, 0, 3};
    CHECK(post_checked(&rq, "second group unbalanced") == PAY_ERR_UNBALANCED,
          "an unbalanced (asset, cap) group after a balanced one");
    L.acct[L.n_accounts].active = true;
    mkreq(&rq, 1);
    rq.n_lines = 2;
    rq.lines[0] = (pay_line_t){A, -1, 0};
    rq.lines[1] = (pay_line_t){L.n_accounts, 1, 0};
    CHECK(post_checked(&rq, "stale account") == PAY_ERR_NO_ACCOUNT, "line to n_accounts");
    L.acct[L.n_accounts].active = false;
    mkreq(&rq, 1);
    CHECK(pay_ledger_issue(&L, &rq, CR1, CR1B, 10, NULL) == PAY_OK, "crown issue, one owner");
    mkreq(&rq, 1);
    rq.n_lines = 3;
    rq.lines[0] = (pay_line_t){CR1B, -1, 0};
    rq.lines[1] = (pay_line_t){CR1, 0, -1};
    rq.lines[2] = (pay_line_t){CR2, 0, 0};
    CHECK(post_checked(&rq, "crown third owner") == PAY_ERR_CROWN,
          "a crown posting touching a second owner in line 2");

    /* issuer credit below zero is a CREDIT error, not an overflow */
    uint32_t ISS2;
    pay_ledger_open(&L, 101, usd, PAY_CAP_FINANCIAL, PAY_ACCT_ISSUER, &ISS2);
    issue(ISS2, B, 5000);
    mkreq(&rq, 1);
    CHECK(pay_ledger_redeem(&L, &rq, B, ISS, 2000, NULL) == PAY_ERR_CREDIT,
          "redeem beyond the issuer's credit");
    mkreq(&rq, 1);
    CHECK(pay_ledger_redeem(&L, &rq, B, ISS2, 7, NULL) == PAY_OK &&
              pay_ledger_find_uetr(&L, rq.uetr)->n_lines == 2,
          "a redemption journals two lines");
    mkreq(&rq, 1);
    CHECK(pay_ledger_redeem(&L, &rq, B, ISS2, 0, NULL) == PAY_ERR_ARG &&
              pay_ledger_redeem(&L, NULL, B, ISS2, 1, NULL) == PAY_ERR_ARG &&
              pay_ledger_redeem(&L, &rq, B, ISS2, LMAX + 1, NULL) == PAY_ERR_ARG,
          "redeem argument bounds");

    /* balance ceiling exactly PAY_BAL_MAX, on the debit side and on the credit side */
    uint32_t I2;
    setup();
    pay_ledger_open(&L, 102, usd, PAY_CAP_FINANCIAL, PAY_ACCT_ISSUER, &I2);
    for (int k = 0; k < 4; k++) issue(ISS, A, LMAX);
    for (int k = 0; k < 3; k++) issue(I2, A, LMAX);
    CHECK(L.acct[A].debit == PAY_BAL_MAX - LMAX, "A at 2^62 - 2^59");
    mkreq(&rq, 1);
    CHECK(pay_ledger_issue(&L, &rq, I2, A, LMAX, NULL) == PAY_ERR_OVERFLOW,
          "a debit reaching exactly PAY_BAL_MAX is refused");
    CHECK(pay_ledger_issue(&L, &rq, I2, A, LMAX - 1, NULL) == PAY_OK, "one below is accepted");
    setup();
    for (int k = 0; k < 4; k++) issue(ISS, A, LMAX);
    for (int k = 0; k < 3; k++) issue(ISS, B, LMAX);
    mkreq(&rq, 1);
    CHECK(pay_ledger_issue(&L, &rq, ISS, B, LMAX, NULL) == PAY_ERR_OVERFLOW &&
              L.acct[ISS].credit == PAY_BAL_MAX - LMAX,
          "an issuer credit reaching exactly PAY_BAL_MAX is refused");

    /* totals: exact, per (asset, cap), NULL outputs allowed, saturation */
    setup();
    issue(ISS, A, 700);
    issue(CR1, CR1B, 40);
    uint64_t d, c;
    int64_t e;
    pay_ledger_totals(&L, usd, PAY_CAP_FINANCIAL, &d, &c, &e);
    CHECK(d == 700 && c == 700 && e == 0, "financial totals exact");
    pay_ledger_totals(&L, usd, PAY_CAP_SOCIAL, &d, &c, &e);
    CHECK(d == 40 && c == 40 && e == 0, "social totals exact, separate from financial");
    pay_ledger_totals(&L, usd, PAY_CAP_FINANCIAL, NULL, NULL, NULL);
    SNAP = L;
    L.acct[A].equity = (int64_t) (PAY_BAL_MAX - 1);
    L.acct[B].equity = (int64_t) PAY_BAL_MAX;
    L.acct[ISS].equity = 0;
    pay_ledger_totals(&L, usd, PAY_CAP_FINANCIAL, &d, &c, &e);
    CHECK(e == INT64_MAX, "positive equity of exactly INT64_MAX is reported");
    L.acct[COM].equity = 5;
    pay_ledger_totals(&L, usd, PAY_CAP_FINANCIAL, &d, &c, &e);
    CHECK(e == INT64_MIN, "positive equity above INT64_MAX saturates");
    L = SNAP;
    L.acct[A].equity = 0;
    L.acct[ISS].equity = -(int64_t) (PAY_BAL_MAX - 1);
    L.acct[FRZ].equity = -(int64_t) PAY_BAL_MAX;
    pay_ledger_totals(&L, usd, PAY_CAP_FINANCIAL, &d, &c, &e);
    CHECK(e == -INT64_MAX, "negative equity of exactly INT64_MAX is reported");
    L = SNAP;

    /* pay_ledger_check on corrupted state; post restores on INVARIANT */
    /* each corruption below keeps every total balanced, so only the
     * per-account rule under test can catch it */
    CHECK(pay_ledger_check(&L), "clean ledger checks");
    CHECK(ISS == 0, "the issuer is account 0");
    L.acct[ISS].equity += 1;
    CHECK(!pay_ledger_check(&L), "equity != debit - credit on account 0");
    L = SNAP;
    L.acct[A].equity += 1;
    L.acct[B].equity -= 1; /* equity totals still balance */
    CHECK(!pay_ledger_check(&L), "equity != debit - credit, totals balanced");
    L = SNAP;
    L.acct[A].debit += 5;
    L.acct[A].credit += 5;
    CHECK(!pay_ledger_check(&L), "credit on a non-issuer, totals balanced");
    L = SNAP;
    L.acct[ISS].debit += PAY_BAL_MAX;
    L.acct[ISS].credit += PAY_BAL_MAX;
    CHECK(!pay_ledger_check(&L), "account 0 debit >= PAY_BAL_MAX, totals balanced");
    L = SNAP;
    {
        uint32_t I3;
        pay_ledger_open(&L, 103, usd, PAY_CAP_FINANCIAL, PAY_ACCT_ISSUER, &I3);
        L.acct[ISS].debit = PAY_BAL_MAX; /* exactly the bound, credit below it */
        L.acct[ISS].credit = PAY_BAL_MAX - 1;
        L.acct[ISS].equity = 1;
        L.acct[I3].credit = 701;
        L.acct[I3].equity = -701;
        CHECK(!pay_ledger_check(&L), "a debit of exactly PAY_BAL_MAX, totals balanced");
    }
    L = SNAP;
    L.acct[ISS].debit += PAY_BAL_MAX - 700;
    L.acct[ISS].credit += PAY_BAL_MAX - 700; /* credit exactly PAY_BAL_MAX */
    CHECK(L.acct[ISS].credit == PAY_BAL_MAX && L.acct[ISS].debit < PAY_BAL_MAX &&
              !pay_ledger_check(&L),
          "a credit of exactly PAY_BAL_MAX, totals balanced");
    L = SNAP;
    L.acct[A].debit = PAY_BAL_MAX;
    L.acct[A].equity = (int64_t) PAY_BAL_MAX;
    CHECK(!pay_ledger_check(&L), "a debit at PAY_BAL_MAX");
    L = SNAP;
    L.acct[A].debit += 5;
    L.acct[A].equity += 5;
    CHECK(!pay_ledger_check(&L), "unbalanced financial totals");
    L = SNAP;
    L.acct[CR1B].debit += 5;
    L.acct[CR1B].equity += 5;
    CHECK(!pay_ledger_check(&L), "unbalanced social totals");
    L = SNAP;
    uint32_t V0;
    pay_ledger_open(&L, 7, L.vfv_asset, PAY_CAP_FINANCIAL, 0, &V0);
    L.acct[V0].debit = 5;
    L.acct[V0].equity = 5;
    CHECK(!pay_ledger_check(&L), "unbalanced totals in asset 0");
    L = SNAP;
    L.acct[L.n_accounts - 1].active = false;
    L.acct[L.n_accounts - 1].debit = 99; /* garbage in an inactive account is ignored */
    CHECK(pay_ledger_check(&L), "inactive accounts are skipped");
    L = SNAP;
    L.acct[COM].debit += 3; /* corrupt an account the posting does not touch */
    L.acct[COM].equity += 3;
    pay_account_t a0 = L.acct[A], b0 = L.acct[B];
    mkreq(&rq, 1);
    CHECK(pay_ledger_transfer(&L, &rq, A, B, 10, NULL) == PAY_ERR_INVARIANT &&
              memcmp(&L.acct[A], &a0, sizeof a0) == 0 && memcmp(&L.acct[B], &b0, sizeof b0) == 0,
          "post refuses on a broken invariant and restores every touched account");
    L = SNAP;

    /* chain verification: empty, intact, and tampered */
    pay_ledger_t *T = &L;
    CHECK(pay_ledger_verify_chain(T), "intact chain");
    pay_journal_t *j1 = &L.journal[1];
    char sv = j1->e2e[0];
    j1->e2e[0] ^= 1;
    CHECK(!pay_ledger_verify_chain(T), "a changed record breaks its hash");
    j1->e2e[0] = sv;
    j1->seq += 7;
    CHECK(!pay_ledger_verify_chain(T), "a changed sequence number is detected");
    j1->seq -= 7;
    j1->prev[3] ^= 1;
    CHECK(!pay_ledger_verify_chain(T), "a broken prev link is detected");
    j1->prev[3] ^= 1;
    CHECK(pay_ledger_verify_chain(T), "restored chain verifies");
    /* a record whose own hash is consistent but whose prev link belongs to
     * another history: built by posting the same request on two ledgers that
     * differ in the record before it */
    {
        static pay_ledger_t Y, Xs;
        setup();
        issue(ISS, A, 1000);
        Y = L;
        CHECK(xfer(A, B, 1) == PAY_OK, "history X");
        Xs = L;
        L = Y;
        CHECK(xfer(A, B, 2) == PAY_OK, "history Y");
        Y = L;
        L = Xs;
        mkreq(&rq, 1);
        pay_posting_req_t same = rq;
        CHECK(pay_ledger_transfer(&L, &rq, A, B, 3, NULL) == PAY_OK &&
                  pay_ledger_transfer(&Y, &same, A, B, 3, NULL) == PAY_OK,
              "the same request on both");
        L.journal[2] = Y.journal[2];
        memcpy(L.chain_head, Y.chain_head, sizeof L.chain_head);
        CHECK(!pay_ledger_verify_chain(&L), "a self-consistent record from another history");
    }
    /* a single-record chain is verified too */
    pay_ledger_init(&L, NULL);
    CHECK(pay_ledger_verify_chain(&L), "an empty chain verifies");
    setup();
    issue(ISS, A, 10);
    L.journal[0].e2e[0] ^= 1;
    CHECK(!pay_ledger_verify_chain(&L), "a tampered single record is detected");

    /* reversal that fails is not marked reversed */
    setup();
    issue(ISS, A, 100);
    mkreq(&rq, 1);
    pay_ledger_transfer(&L, &rq, A, B, 60, NULL);
    char u[PAY_UETR_LEN + 1];
    memcpy(u, rq.uetr, sizeof u);
    CHECK(xfer(B, COM, 60) == PAY_OK, "B spends it");
    mkreq(&rq, 1);
    CHECK(pay_ledger_reverse(&L, &rq, u, NULL) == PAY_ERR_FUNDS, "reversal unfunded");
    CHECK(xfer(COM, B, 60) == PAY_OK, "B is refunded");
    mkreq(&rq, 1);
    CHECK(pay_ledger_reverse(&L, &rq, u, NULL) == PAY_OK, "the retry reverses");
    CHECK(pay_ledger_reverse(NULL, &rq, u, NULL) == PAY_ERR_ARG &&
              pay_ledger_reverse(&L, NULL, u, NULL) == PAY_ERR_ARG,
          "reverse NULL");

    /* payment with the assurance fee: line counts, the VFV bound, the buckets
     * and the per-account carry */
    uint32_t VI, VT, BK[PAY_ASSURE_BUCKETS];
    pay_ledger_open(&L, 200, L.vfv_asset, PAY_CAP_FINANCIAL, PAY_ACCT_ISSUER, &VI);
    pay_ledger_open(&L, 1, L.vfv_asset, PAY_CAP_FINANCIAL, 0, &VT);
    for (int i = 0; i < PAY_ASSURE_BUCKETS; i++)
        CHECK(pay_ledger_open(&L, 201 + (uint32_t) i, usd, PAY_CAP_FINANCIAL, 0, &BK[i]) == PAY_OK,
              "bucket account %d", i);
    issue(ISS, A, 1000);
    uint64_t fee = 99;
    mkreq(&rq, 1);
    pay_status_t st0 = pay_ledger_pay_with_fee(&L, &rq, A, B, 10, BK, 0, VI, VT, 0, NULL, &fee);
    CHECK_KNOWN("F-PAY-FEE0", st0 == PAY_OK, "a payment of 10 (fee 0, no excess) -> %d", (int) st0);
    if (st0 == PAY_OK)
        CHECK(fee == 0 && pay_ledger_find_uetr(&L, rq.uetr)->n_lines == 2 &&
                  pay_ledger_find_uetr(&L, rq.uetr)->lines[0].account == A &&
                  pay_ledger_find_uetr(&L, rq.uetr)->kind == PAY_KIND_FEE &&
                  L.acct[A].fee_carry == 10 * PAY_ASSURE_NUM,
              "fee 0, no excess, no VFV: two lines, payer first, carry kept");
    mkreq(&rq, 1);
    CHECK(pay_ledger_pay_with_fee(&L, &rq, A, B, 10, BK, 1, VI, VT, LMAX, NULL, NULL) == PAY_OK &&
              pay_ledger_find_uetr(&L, rq.uetr)->n_lines == 5,
          "an excess and a VFV credit of exactly LINE_MAX: five lines");
    uint64_t carry = L.acct[A].fee_carry;
    mkreq(&rq, 1);
    CHECK(pay_ledger_pay_with_fee(&L, &rq, A, B, 10, BK, 1, VI, VT, LMAX + 1, NULL, &fee) ==
                  PAY_ERR_ARG &&
              fee == 0 && L.acct[A].fee_carry == carry,
          "VFV credit above LINE_MAX refused, carry untouched");
    mkreq(&rq, 1);
    CHECK(pay_ledger_pay_with_fee(&L, &rq, A, B, 1000000, BK, 0, VI, VT, 0, NULL, &fee) ==
                  PAY_ERR_FUNDS &&
              L.acct[A].fee_carry == carry,
          "an unfunded fee payment leaves the carry as it was");
    issue(ISS, A, 2000000000);
    uint64_t before[PAY_ASSURE_BUCKETS];
    for (int i = 0; i < PAY_ASSURE_BUCKETS; i++) before[i] = L.acct[BK[i]].debit;
    uint64_t payer = L.acct[A].debit;
    uint64_t want = (uint64_t) (((u128) 1000000000 * PAY_ASSURE_NUM + carry) / PAY_ASSURE_DEN);
    mkreq(&rq, 1);
    CHECK(pay_ledger_pay_with_fee(&L, &rq, A, B, 1000000000, BK, 7, VI, VT, 0, NULL, &fee) ==
                  PAY_OK &&
              fee == want && L.acct[A].debit == payer - 1000000000 - fee - 7,
          "fee on 10^9 with the carry: %llu (payer pays amount + fee + excess)",
          (unsigned long long) fee);
    uint64_t split[PAY_ASSURE_BUCKETS];
    pay_assure_split(fee, split);
    bool bk = true;
    for (int i = 0; i < PAY_ASSURE_BUCKETS; i++)
        bk &= L.acct[BK[i]].debit - before[i] == split[i] + (i == 0 ? 7 : 0);
    CHECK(bk && healthy(), "each bucket gets its exact share; the excess goes to the reserve");
    L.acct[A].fee_carry = PAY_ASSURE_DEN;
    mkreq(&rq, 1);
    CHECK(pay_ledger_pay_with_fee(&L, &rq, A, B, 10, BK, 0, VI, VT, 0, NULL, NULL) == PAY_ERR_STATE,
          "a corrupt carry is refused");
    L.acct[A].fee_carry = 0;
    CHECK(pay_ledger_pay_with_fee(NULL, &rq, A, B, 10, BK, 0, VI, VT, 0, NULL, NULL) ==
                  PAY_ERR_ARG &&
              pay_ledger_pay_with_fee(&L, &rq, A, B, 10, NULL, 0, VI, VT, 0, NULL, NULL) ==
                  PAY_ERR_ARG &&
              pay_ledger_pay_with_fee(&L, &rq, L.n_accounts, B, 10, BK, 0, VI, VT, 0, NULL, NULL) ==
                  PAY_ERR_ARG,
          "pay_with_fee NULL ledger / buckets, unknown payer");
}

/* pay_assure behaviours a mutation run showed unobserved: the excess-free
 * path, commons conservation on corrupted state, and the commons split's cap
 * and equal-share rule. */
static void axiom_assure_more(void)
{
    /* contribution == fee takes no excess path, whatever the unit's rate */
    pay_assure_policy_t pol;
    pay_assure_policy_default(NULL);
    pay_assure_policy_default(&pol);
    pay_assure_result_t res;
    CHECK(pay_assure_compute(&pol, PAY_UNIT_COMPUTE, 1000000, NULL, NULL, &res) == PAY_ASSURE_OK &&
              res.excess == 0 && res.vfv_credit == 0,
          "fee contribution on a unit with no posted rate is OK");

    /* commons conservation on corrupted state, unit 0 included */
    pay_commons_t cm;
    pay_commons_init(NULL);
    pay_commons_init(&cm);
    pay_commons_deposit(&cm, PAY_UNIT_MONEY, 100);
    CHECK(pay_commons_conserved(&cm), "conserved after a deposit");
    cm.pool[PAY_UNIT_MONEY] += 1;
    CHECK(!pay_commons_conserved(&cm), "pool != received - allocated in unit 0 is detected");
    cm.pool[PAY_UNIT_MONEY] -= 1;
    cm.allocated[PAY_UNIT_STORAGE] = 1;
    CHECK(!pay_commons_conserved(&cm), "allocated > received is detected");
    cm.allocated[PAY_UNIT_STORAGE] = 0;
    uint64_t zw[3] = {0, 0, 0}, zo[4] = {9, 9, 9, 9};
    CHECK(pay_commons_allocate(&cm, PAY_UNIT_MONEY, zw, 3, (pay_rat_t){8, 21}, zo) &&
              cm.allocated[PAY_UNIT_MONEY] == 0 && cm.pool[PAY_UNIT_MONEY] == 100 &&
              pay_commons_conserved(&cm) && zo[0] == 0 && zo[2] == 0 && zo[3] == 9,
          "allocating to all-zero weights gives nothing and writes only n outputs");

    /* split: the cap, and the equal share ceil(total / nonzero) that overrides it */
    uint64_t w[3] = {1, 3, 7}, out[3];
    uint64_t left = pay_commons_split(100, w, 2, (pay_rat_t){1, 1}, out);
    CHECK(left == 0 && out[0] == 25 && out[1] == 75, "cap 1: plain 1:3 split (w[2] not read)");
    left = pay_commons_split(100, w, 2, (pay_rat_t){0, 1}, out);
    CHECK(left == 0 && out[0] == 50 && out[1] == 50, "cap 0: the equal share 50 caps both");
    uint64_t w2[3] = {1, 1, 7};
    left = pay_commons_split(101, w2, 2, (pay_rat_t){0, 1}, out);
    CHECK(left == 0 && out[0] + out[1] == 101 && out[0] <= 51 && out[1] <= 51,
          "equal share rounds up: 101 over two is fully given");
    uint64_t w3[3] = {1, 0, 3}, o3[3];
    left = pay_commons_split(100, w3, 3, (pay_rat_t){0, 1}, o3);
    CHECK(left == 0 && o3[0] == 50 && o3[1] == 0 && o3[2] == 50,
          "the equal share counts nonzero weights only (100 / 2)");
    uint64_t w4[2] = {0, 5}, o4[2] = {7, 7};
    left = pay_commons_split(100, w4, 2, (pay_rat_t){0, 1}, o4);
    CHECK(left == 0 && o4[0] == 0 && o4[1] == 100, "a single nonzero weight takes everything");
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
    axiom_fee();
    axiom_commons();
    axiom_pay_more();
    axiom_assure_more();
    return tier_end();
}
