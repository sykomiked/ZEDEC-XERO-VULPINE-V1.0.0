/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* meta_rational.c — Tier 3 metamorphic relations for exact rationals:
 * kernel/src/rational (rat_*) and kernel/src/rmag/rmag_core (rmag_*_quotas).
 *
 * No oracle here: each relation compares the module with ITSELF on related
 * inputs (commutativity, associativity when nothing overflows, identities,
 * inverses, idempotent normalization, scaling invariance, split-sums-to-
 * total, render/parse round trip). Operands are the tier.h edge set plus a
 * deterministic pseudo-random sample. The table runs forward, again and in
 * reverse (meta.h) to expose hidden static state.
 */
#include "tier.h"
#include "meta.h"
#include "rational.h"
#include "rmag_core.h"

static const tier_known_t KNOWN_FAILURES[] = {{"-", "unused"}};

#define NS 48
static rat_t S[NS + 12];
static unsigned nS;

static void build(void)
{
    static const int64_t D[] = {1, 2, 3, 7, 10, 1000000007};
    uint64_t seed = 0x5EED;
    nS = 0;
    for (unsigned i = 0; i < 8; i++) /* small exact values and edges */
        S[nS++] = rat_make(TIER_I64_EDGES[(i * 3) % TIER_N(TIER_I64_EDGES)], D[i % TIER_N(D)]);
    while (nS < NS) {
        int64_t n = (int64_t) (tier_rand(&seed) % 2000001) - 1000000;
        int64_t d = (int64_t) (tier_rand(&seed) % 999) + 1;
        S[nS++] = rat_make(n, d);
    }
    S[nS++] = rat_make(INT64_MAX, 1);
    S[nS++] = rat_make(-INT64_MAX, 3);
    S[nS++] = rat_make(1, INT64_MAX);
    S[nS++] = rat_zero();
}

static uint64_t dig(uint64_t h, rat_t r)
{
    h = meta_mix(h, (uint64_t) r.valid);
    if (r.valid) {
        h = meta_mix(h, (uint64_t) r.num);
        h = meta_mix(h, (uint64_t) r.den);
    }
    return h;
}
static bool eqr(rat_t a, rat_t b)
{
    return a.valid == b.valid && (!a.valid || (a.num == b.num && a.den == b.den));
}

static uint64_t rel_commutative(void)
{
    uint64_t h = 0;
    for (unsigned i = 0; i < nS; i++)
        for (unsigned j = 0; j < nS; j++) {
            rat_t a = S[i], b = S[j];
            CHECK(eqr(rat_add(a, b), rat_add(b, a)), "a+b == b+a (%u,%u)", i, j);
            CHECK(eqr(rat_mul(a, b), rat_mul(b, a)), "a*b == b*a (%u,%u)", i, j);
            CHECK(rat_cmp(a, b) == -rat_cmp(b, a), "cmp antisymmetric (%u,%u)", i, j);
            CHECK(rat_eq(a, b) == rat_eq(b, a), "eq symmetric (%u,%u)", i, j);
            h = dig(dig(h, rat_add(a, b)), rat_mul(a, b));
        }
    return h;
}

static uint64_t rel_associative(void)
{
    uint64_t h = 0;
    for (unsigned i = 0; i < nS; i += 3)
        for (unsigned j = 0; j < nS; j += 2)
            for (unsigned k = 0; k < nS; k += 5) {
                rat_t a = S[i], b = S[j], c = S[k];
                rat_t l = rat_add(rat_add(a, b), c), r = rat_add(a, rat_add(b, c));
                if (l.valid && r.valid)
                    CHECK(eqr(l, r), "(a+b)+c == a+(b+c) when nothing overflows (%u,%u,%u)", i, j,
                          k);
                rat_t lm = rat_mul(rat_mul(a, b), c), rm = rat_mul(a, rat_mul(b, c));
                if (lm.valid && rm.valid) CHECK(eqr(lm, rm), "(ab)c == a(bc) (%u,%u,%u)", i, j, k);
                rat_t d1 = rat_mul(a, rat_add(b, c)), d2 = rat_add(rat_mul(a, b), rat_mul(a, c));
                if (d1.valid && d2.valid) CHECK(eqr(d1, d2), "a(b+c) == ab+ac (%u,%u,%u)", i, j, k);
                h = dig(dig(h, l), lm);
            }
    return h;
}

static uint64_t rel_identity_inverse(void)
{
    uint64_t h = 0;
    rat_t one = rat_make(1, 1), zero = rat_zero(), m1 = rat_make(-1, 1);
    for (unsigned i = 0; i < nS; i++) {
        rat_t a = S[i];
        CHECK(eqr(rat_mul(a, one), a) && eqr(rat_div(a, one), a), "a*1 == a/1 == a (%u)", i);
        CHECK(eqr(rat_add(a, zero), a) && eqr(rat_sub(a, zero), a), "a+0 == a-0 == a (%u)", i);
        CHECK(eqr(rat_neg(rat_neg(a)), a), "-(-a) == a (%u)", i);
        CHECK(eqr(rat_mul(a, m1), rat_neg(a)), "a*(-1) == -a (%u)", i);
        CHECK(!a.valid || rat_is_zero(rat_sub(a, a)), "a-a == 0 (%u)", i);
        CHECK(eqr(rat_abs(a), rat_abs(rat_neg(a))), "|a| == |-a| (%u)", i);
        if (a.valid && !rat_is_zero(a)) {
            rat_t q = rat_div(a, a);
            CHECK(eqr(q, one), "a/a == 1 (%u)", i);
        }
        for (unsigned j = 0; j < nS; j++) {
            rat_t b = S[j];
            rat_t d = rat_sub(a, b), back = rat_add(d, b);
            if (d.valid && back.valid) CHECK(eqr(back, a), "(a-b)+b == a (%u,%u)", i, j);
            if (!rat_is_zero(b)) {
                rat_t q = rat_div(a, b), m = rat_mul(q, b);
                if (q.valid && m.valid) CHECK(eqr(m, a), "(a/b)*b == a (%u,%u)", i, j);
            }
            h = dig(h, d);
        }
    }
    return h;
}

static uint64_t rel_normalize(void)
{
    uint64_t h = 0;
    for (unsigned i = 0; i < nS; i++) {
        rat_t a = S[i];
        if (!a.valid) continue;
        CHECK(eqr(rat_make(a.num, a.den), a), "normalize is idempotent (%u)", i);
        CHECK(eqr(rat_make(-a.num, -a.den), a), "sign moves to the numerator (%u)", i);
        const int64_t K[] = {2, 3, 1000, 2147483647};
        for (unsigned k = 0; k < TIER_N(K); k++) {
            __int128 n = (__int128) a.num * K[k], d = (__int128) a.den * K[k];
            if (n > INT64_MAX || n < -INT64_MAX || d > INT64_MAX) continue;
            CHECK(eqr(rat_make((int64_t) n, (int64_t) d), a), "k*n / k*d == n/d (%u, k=%lld)", i,
                  (long long) K[k]);
        }
        h = dig(h, rat_make(a.num, a.den));
    }
    return h;
}

static uint64_t rel_split_sums(void)
{
    uint64_t h = 0;
    static rat_t out[97];
    const uint32_t N[] = {1, 2, 3, 7, 10, 96};
    for (unsigned i = 0; i < nS; i++)
        for (unsigned k = 0; k < TIER_N(N); k++) {
            if (!rat_split(S[i], N[k], out)) continue;
            rat_t sum = rat_zero();
            for (uint32_t p = 0; p < N[k]; p++) sum = rat_add(sum, out[p]);
            if (sum.valid) CHECK(eqr(sum, S[i]), "split(x, %u) sums to x (%u)", N[k], i);
            CHECK(eqr(rat_mul(out[0], rat_from_int(N[k])), S[i]), "share * n == x (%u, %u)", i,
                  N[k]);
            h = dig(h, out[0]);
        }
    return h;
}

static uint64_t rel_round_trip(void)
{
    uint64_t h = 0;
    char buf[96];
    for (unsigned i = 0; i < nS; i++) {
        rat_t a = S[i];
        uint32_t n = rat_to_string(a, buf, sizeof buf);
        if (a.valid) CHECK(eqr(rat_from_string(buf), a), "parse(render(a)) == a (%u)", i);
        for (uint32_t c = 0; c < n; c++) h = meta_mix(h, (uint8_t) buf[c]);
        /* the decimal rendering at 18 places is exact iff 10^18 * a is an integer */
        bool exact = false;
        char fx[64];
        rat_to_fixed(a, 18, fx, sizeof fx, &exact);
        if (a.valid && exact) CHECK(eqr(rat_from_string(fx), a), "exact fixed round trip (%u)", i);
    }
    return h;
}

static uint64_t rel_rmag(void)
{
    uint64_t h = 0;
    rmag_init(16);
    const int64_t SM[] = {-7, -3, -1, 0, 1, 2, 5, 9};
    const int64_t SD[] = {1, 2, 3, 4, 6};
    for (unsigned i = 0; i < TIER_N(SM); i++)
        for (unsigned j = 0; j < TIER_N(SD); j++)
            for (unsigned k = 0; k < TIER_N(SM); k++)
                for (unsigned l = 0; l < TIER_N(SD); l++) {
                    rational_t a = {SM[i], SD[j]}, b = {SM[k], SD[l]};
                    rational_t s1 = rmag_add_quotas(a, b), s2 = rmag_add_quotas(b, a);
                    CHECK(s1.num == s2.num && s1.den == s2.den, "rmag a+b == b+a");
                    rational_t m1 = rmag_mul_quotas(a, b), m2 = rmag_mul_quotas(b, a);
                    CHECK(m1.num == m2.num && m1.den == m2.den, "rmag a*b == b*a");
                    rational_t back = rmag_add_quotas(rmag_sub_quotas(a, b), b);
                    rat_t ra = rat_make(a.num, a.den);
                    CHECK(back.num == ra.num && back.den == ra.den, "rmag (a-b)+b == a (reduced)");
                    /* a transfer between two slots and back restores both */
                    rmag_set_quota(1, a);
                    rmag_set_quota(2, b);
                    rmag_set_quota(1, rmag_sub_quotas(rmag_get_quota(1), b));
                    rmag_set_quota(2, rmag_add_quotas(rmag_get_quota(2), b));
                    rmag_set_quota(1, rmag_add_quotas(rmag_get_quota(1), b));
                    rmag_set_quota(2, rmag_sub_quotas(rmag_get_quota(2), b));
                    rational_t q1 = rmag_get_quota(1), q2 = rmag_get_quota(2);
                    rat_t rb = rat_make(b.num, b.den);
                    CHECK(q1.num == ra.num && q1.den == ra.den && q2.num == rb.num &&
                              q2.den == rb.den,
                          "rmag transfer then back is the identity");
                    h = meta_mix(meta_mix(h, (uint64_t) s1.num), (uint64_t) s1.den);
                }
    return h;
}

static const meta_rel_t RELS[] = {
    {"commutative", rel_commutative},
    {"associative", rel_associative},
    {"identity/inverse", rel_identity_inverse},
    {"normalize", rel_normalize},
    {"split-sums", rel_split_sums},
    {"round-trip", rel_round_trip},
    {"rmag", rel_rmag},
};

int main(void)
{
    tier_begin("tier3/meta_rational", KNOWN_FAILURES, 0);
    (void) KNOWN_FAILURES;
    build();
    meta_run(RELS, TIER_N(RELS));
    return tier_end();
}
