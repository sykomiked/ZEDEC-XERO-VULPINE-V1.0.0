/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* meta_ledger.c — Tier 3 metamorphic relations for kernel/src/pay
 * (pay_ledger and pay_tithe).
 *
 *   transfer(x) then transfer back       == identity on balances
 *   issue(x) then redeem(x)               == identity on balances
 *   a batch of transfers in two orders    == the same final balances
 *   reverse(p)                            == undoing p
 *   tithe t(a) = floor(a*phi/100):
 *     t(a) == (a + floor(a*sqrt5)) / 200  (a second, independent formula
 *                                          through pay_floor_a_sqrt5)
 *     t(a)+t(b) <= t(a+b) <= t(a)+t(b)+1  (floor is super-additive by < 1)
 *     a <= b  =>  t(a) <= t(b)
 *   pay_commons_split(total, w): out[] + left == total for every weight vector
 * The table runs forward, again and in reverse (meta.h).
 */
#include "tier.h"
#include "meta.h"
#include "pay_ledger.h"
#include "pay_tithe.h"

static const tier_known_t KNOWN_FAILURES[] = {{"-", "unused"}};

static uint64_t g_ctr;
static void mkreq(pay_posting_req_t *r)
{
    uint8_t d[32];
    pay_hbuf h;
    memset(r, 0, sizeof *r);
    pay_hbuf_init(&h);
    pay_hbuf_str(&h, "T3");
    pay_hbuf_u64(&h, ++g_ctr);
    pay_hbuf_final(&h, d);
    memcpy(r->idem_key, d, 32);
    pay_uetr_from_random(d, r->uetr);
    snprintf(r->e2e, sizeof r->e2e, "T3-%llu", (unsigned long long) g_ctr);
    r->tick = g_ctr;
    r->initiator = 1;
}

static pay_ledger_t L, L2;
static uint32_t ISS, A[4];
static void chart(pay_ledger_t *l)
{
    uint16_t usd;
    pay_ledger_init(l, NULL);
    pay_ledger_add_fiat(l, "USD", 840, 2, &usd);
    pay_ledger_open(l, 100, usd, PAY_CAP_FINANCIAL, PAY_ACCT_ISSUER, &ISS);
    for (uint32_t i = 0; i < 4; i++) pay_ledger_open(l, i + 1, usd, PAY_CAP_FINANCIAL, 0, &A[i]);
    pay_posting_req_t rq;
    for (uint32_t i = 0; i < 4; i++) {
        mkreq(&rq);
        pay_ledger_issue(l, &rq, ISS, A[i], 1000000 * (i + 1), NULL);
    }
}

static bool same_bal(const pay_ledger_t *x, const pay_ledger_t *y)
{
    for (uint32_t i = 0; i < x->n_accounts; i++)
        if (x->acct[i].debit != y->acct[i].debit || x->acct[i].credit != y->acct[i].credit ||
            x->acct[i].equity != y->acct[i].equity)
            return false;
    return true;
}
static uint64_t dig_bal(uint64_t h, const pay_ledger_t *x)
{
    for (uint32_t i = 0; i < x->n_accounts; i++) {
        h = meta_mix(h, x->acct[i].debit);
        h = meta_mix(h, x->acct[i].credit);
    }
    return h;
}

static const uint64_t AMT[] = {1, 2, 99, 100, 101, 999999, 1000000, 1000001, 4000000};

static uint64_t rel_transfer_back(void)
{
    uint64_t h = 0;
    chart(&L);
    pay_posting_req_t rq;
    for (unsigned k = 0; k < TIER_N(AMT); k++)
        for (uint32_t i = 0; i < 4; i++)
            for (uint32_t j = 0; j < 4; j++) {
                if (i == j) continue;
                L2 = L;
                mkreq(&rq);
                pay_status_t s1 = pay_ledger_transfer(&L, &rq, A[i], A[j], AMT[k], NULL);
                h = meta_mix(h, (uint64_t) s1);
                if (s1 != PAY_OK) {
                    CHECK(same_bal(&L, &L2), "a refused transfer moves nothing");
                    continue;
                }
                mkreq(&rq);
                CHECK(pay_ledger_transfer(&L, &rq, A[j], A[i], AMT[k], NULL) == PAY_OK,
                      "the way back is always funded");
                CHECK(same_bal(&L, &L2), "transfer(%llu) then back is the identity",
                      (unsigned long long) AMT[k]);
            }
    return dig_bal(h, &L);
}

static uint64_t rel_issue_redeem(void)
{
    uint64_t h = 0;
    chart(&L);
    pay_posting_req_t rq;
    for (unsigned k = 0; k < TIER_N(AMT); k++) {
        L2 = L;
        mkreq(&rq);
        CHECK(pay_ledger_issue(&L, &rq, ISS, A[1], AMT[k], NULL) == PAY_OK, "issue");
        mkreq(&rq);
        CHECK(pay_ledger_redeem(&L, &rq, A[1], ISS, AMT[k], NULL) == PAY_OK, "redeem");
        CHECK(same_bal(&L, &L2), "issue(%llu) then redeem is the identity",
              (unsigned long long) AMT[k]);
        /* reverse(p) undoes p */
        mkreq(&rq);
        CHECK(pay_ledger_transfer(&L, &rq, A[3], A[2], AMT[k], NULL) == PAY_OK, "funded");
        char u[PAY_UETR_LEN + 1];
        memcpy(u, rq.uetr, sizeof u);
        mkreq(&rq);
        CHECK(pay_ledger_reverse(&L, &rq, u, NULL) == PAY_OK && same_bal(&L, &L2),
              "reverse(transfer %llu) is the identity", (unsigned long long) AMT[k]);
        h = dig_bal(h, &L);
    }
    return h;
}

static uint64_t rel_order(void)
{
    /* a fixed batch, then the same batch reversed, on two copies */
    static const struct {
        int f, t;
        uint64_t x;
    } B[] = {{0, 1, 500}, {1, 2, 70000}, {2, 3, 3}, {3, 0, 999}, {1, 3, 1}, {0, 2, 12345}};
    chart(&L);
    L2 = L;
    pay_posting_req_t rq;
    for (unsigned k = 0; k < TIER_N(B); k++) {
        mkreq(&rq);
        CHECK(pay_ledger_transfer(&L, &rq, A[B[k].f], A[B[k].t], B[k].x, NULL) == PAY_OK, "fwd");
    }
    for (unsigned k = TIER_N(B); k-- > 0;) {
        mkreq(&rq);
        CHECK(pay_ledger_transfer(&L2, &rq, A[B[k].f], A[B[k].t], B[k].x, NULL) == PAY_OK, "rev");
    }
    CHECK(same_bal(&L, &L2), "the same funded batch in two orders gives the same balances");
    return dig_bal(0, &L);
}

static uint64_t tithe_alt(uint64_t a)
{
    pay_u128 s = pay_floor_a_sqrt5(a); /* floor(a*sqrt5) */
    s = pay_u128_add64(s, a);          /* a + floor(a*sqrt5) */
    uint64_t rem;
    pay_u128 q = pay_udiv128_64(s, 200, &rem);
    return q.lo;
}

static uint64_t rel_tithe(void)
{
    uint64_t h = 0, seed = 0x7173;
    uint64_t v[64];
    unsigned n = 0;
    for (unsigned i = 0; i < TIER_N(TIER_U64_EDGES); i++) v[n++] = TIER_U64_EDGES[i];
    while (n < TIER_N(v)) {
        uint64_t r = tier_rand(&seed);
        v[n] = n & 1 ? r : r >> (r % 60);
        n++;
    }
    for (unsigned i = 0; i < n; i++) {
        uint64_t t = pay_tithe_phi(v[i]);
        CHECK(t == tithe_alt(v[i]), "t(a) == (a + floor(a*sqrt5)) / 200 for a=%llu",
              (unsigned long long) v[i]);
        h = meta_mix(h, t);
        for (unsigned j = 0; j < n; j++) {
            uint64_t a = v[i], b = v[j];
            if (a > UINT64_MAX - b) continue;
            uint64_t ta = pay_tithe_phi(a), tb = pay_tithe_phi(b), tab = pay_tithe_phi(a + b);
            CHECK(ta + tb <= tab && tab <= ta + tb + 1, "t(a)+t(b) <= t(a+b) <= t(a)+t(b)+1");
            if (a <= b) CHECK(ta <= tb, "a <= b implies t(a) <= t(b)");
        }
    }
    return h;
}

static uint64_t rel_commons_split(void)
{
    uint64_t h = 0, seed = 0xC0FFEE;
    uint64_t w[PAY_COMMONS_MAX_RECIPIENTS], out[PAY_COMMONS_MAX_RECIPIENTS];
    const pay_rat_t caps[] = {{8, 21}, {1, 1}, {1, 64}, {0, 1}};
    for (int trial = 0; trial < 200; trial++) {
        uint32_t n = 1 + (uint32_t) (tier_rand(&seed) % PAY_COMMONS_MAX_RECIPIENTS);
        uint64_t total = tier_rand(&seed) >> (tier_rand(&seed) % 64);
        for (uint32_t i = 0; i < n; i++) w[i] = tier_rand(&seed) % 5; /* zeros included */
        pay_rat_t cap = caps[trial % TIER_N(caps)];
        uint64_t left = pay_commons_split(total, w, n, cap, out);
        unsigned __int128 sum = left;
        for (uint32_t i = 0; i < n; i++) {
            sum += out[i];
            if (w[i] == 0) CHECK(out[i] == 0, "a zero weight receives nothing");
        }
        CHECK(sum == total, "split(total) sums to total for every share vector (trial %d)", trial);
        /* scaling every weight by 3 changes nothing */
        uint64_t out3[PAY_COMMONS_MAX_RECIPIENTS], w3[PAY_COMMONS_MAX_RECIPIENTS];
        for (uint32_t i = 0; i < n; i++) w3[i] = w[i] * 3;
        uint64_t left3 = pay_commons_split(total, w3, n, cap, out3);
        CHECK(left3 == left && memcmp(out3, out, n * sizeof out[0]) == 0,
              "split is invariant under scaling the weights (trial %d)", trial);
        h = meta_mix(h, left);
    }
    return h;
}

static const meta_rel_t RELS[] = {
    {"transfer-back", rel_transfer_back},
    {"issue-redeem-reverse", rel_issue_redeem},
    {"order", rel_order},
    {"tithe", rel_tithe},
    {"commons-split", rel_commons_split},
};

int main(void)
{
    tier_begin("tier3/meta_ledger", KNOWN_FAILURES, 0);
    (void) KNOWN_FAILURES;
    meta_run(RELS, TIER_N(RELS));
    return tier_end();
}
