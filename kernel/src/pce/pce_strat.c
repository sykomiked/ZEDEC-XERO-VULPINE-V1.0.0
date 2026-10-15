/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* pce_strat.c — the strategy engine (pce_strat.h S1-S6). */
#include "pce_strat.h"

static bool bounds_ok(const rat_t *lo, const rat_t *hi, uint32_t n)
{
    if (lo == NULL || hi == NULL || n == 0u || n > PCE_STRAT_SCEN) return false;
    rat_t one = rat_from_int(1), slo = rat_zero(), shi = rat_zero();
    for (uint32_t s = 0; s < n; s++) {
        if (!pce_rat_nonneg(lo[s]) || !hi[s].valid) return false;
        if (rat_cmp(lo[s], hi[s]) > 0 || rat_cmp(hi[s], one) > 0) return false;
        slo = rat_add(slo, lo[s]);
        shi = rat_add(shi, hi[s]);
    }
    return slo.valid && shi.valid && rat_cmp(slo, one) <= 0 && rat_cmp(shi, one) >= 0;
}

/* Scenario indexes by payoff, ascending, ties by index (insertion sort). */
static void order(const int64_t *payoff, uint32_t n, uint32_t *idx)
{
    for (uint32_t i = 0; i < n; i++) {
        uint32_t j = i;
        while (j > 0u && payoff[idx[j - 1u]] > payoff[i]) {
            idx[j] = idx[j - 1u];
            j--;
        }
        idx[j] = i;
    }
}

/* Spread 1 - sum(lo) over the scenarios in idx order, each up to hi - lo. */
static rat_t extreme(const int64_t *payoff, const rat_t *lo, const rat_t *hi, uint32_t n,
                     const uint32_t *idx, bool worst_first)
{
    rat_t p[PCE_STRAT_SCEN];
    rat_t rem = rat_from_int(1);
    for (uint32_t s = 0; s < n; s++) {
        p[s] = lo[s];
        rem = rat_sub(rem, lo[s]);
    }
    for (uint32_t k = 0; k < n; k++) {
        uint32_t s = worst_first ? idx[k] : idx[n - 1u - k];
        rat_t give = pce_rat_min(rat_sub(hi[s], lo[s]), rem);
        p[s] = rat_add(p[s], give);
        rem = rat_sub(rem, give);
    }
    rat_t e = rat_zero();
    for (uint32_t s = 0; s < n; s++) e = rat_add(e, rat_mul(p[s], rat_from_int(payoff[s])));
    return e;
}

int pce_strat_bounds(const int64_t *payoff, const rat_t *lo, const rat_t *hi, uint32_t nscen,
                     rat_t *lower, rat_t *upper)
{
    if (payoff == NULL || lower == NULL || upper == NULL || !bounds_ok(lo, hi, nscen))
        return PCE_E_ARG;
    for (uint32_t s = 0; s < nscen; s++)
        if (payoff[s] > PCE_STRAT_PAYOFF_MAX || payoff[s] < -PCE_STRAT_PAYOFF_MAX) return PCE_E_ARG;
    uint32_t idx[PCE_STRAT_SCEN];
    order(payoff, nscen, idx);
    rat_t l = extreme(payoff, lo, hi, nscen, idx, true);
    rat_t u = extreme(payoff, lo, hi, nscen, idx, false);
    if (!l.valid || !u.valid) return PCE_E_OVERFLOW;
    *lower = l;
    *upper = u;
    return PCE_OK;
}

static void hash_out_verdict(pce_hash_t *h, const pce_verdict_t *v)
{
    pce_hash_u64(h, ((uint64_t) v->tag << 16) | ((uint64_t) v->act << 8) | v->kind);
}

int pce_strat_decide(const pce_strat_in_t *in, const pce_strat_result_t *prev, pdx_log_t *log,
                     pce_strat_result_t *out)
{
    if (in == NULL || out == NULL || in->payoff == NULL) return PCE_E_ARG;
    if (!pce_purpose_ok(in->purpose) || in->purpose == PCE_PURPOSE_FINANCE) return PCE_E_PURPOSE;
    if (in->nopt == 0u || in->nopt > PCE_STRAT_OPTS || in->phase > 3u) return PCE_E_ARG;
    if (!bounds_ok(in->lo, in->hi, in->nscen)) return PCE_E_ARG;
    const uint32_t no = in->nopt, ns = in->nscen;
    for (uint32_t i = 0; i < no * ns; i++)
        if (in->payoff[i] > PCE_STRAT_PAYOFF_MAX || in->payoff[i] < -PCE_STRAT_PAYOFF_MAX)
            return PCE_E_ARG;

    pce_strat_result_t r;
    r.contention = r.refused = 0;
    for (uint32_t k = 0; k < PCE_RULES; k++) r.pick[k] = PCE_STRAT_NONE;
    for (uint32_t o = 0; o < PCE_STRAT_OPTS; o++) {
        r.lower[o] = r.upper[o] = rat_zero();
        r.worst[o] = r.regret[o] = 0;
    }
    uint32_t allowed = 0;
    for (uint32_t o = 0; o < no; o++) {
        if (in->flags != NULL && (in->flags[o] & PCE_OPT_PROHIBITED) != 0u)
            r.refused |= 1u << o;
        else
            allowed |= 1u << o;
    }

    for (uint32_t o = 0; o < no; o++) {
        if ((allowed & (1u << o)) == 0u) continue;
        const int64_t *row = in->payoff + (size_t) o * ns;
        int rc = pce_strat_bounds(row, in->lo, in->hi, ns, &r.lower[o], &r.upper[o]);
        if (rc != PCE_OK) return rc;
        bool first = true;
        for (uint32_t s = 0; s < ns; s++) {
            if (rat_is_zero(in->hi[s])) continue; /* S1: cannot happen */
            if (first || row[s] < r.worst[o]) r.worst[o] = row[s];
            first = false;
        }
    }
    for (uint32_t s = 0; s < ns; s++) {
        if (rat_is_zero(in->hi[s])) continue;
        bool first = true;
        int64_t best = 0;
        for (uint32_t o = 0; o < no; o++) {
            if ((allowed & (1u << o)) == 0u) continue;
            int64_t v = in->payoff[(size_t) o * ns + s];
            if (first || v > best) best = v;
            first = false;
        }
        for (uint32_t o = 0; o < no; o++) {
            if ((allowed & (1u << o)) == 0u) continue;
            int64_t g = best - in->payoff[(size_t) o * ns + s]; /* <= 2^62: no overflow */
            if (g > r.regret[o]) r.regret[o] = g;
        }
    }

    for (uint32_t o = 0; o < no; o++) {
        if ((allowed & (1u << o)) == 0u) continue;
        uint32_t *p = r.pick;
        if (p[PCE_RULE_LOWER_EV] == PCE_STRAT_NONE ||
            rat_cmp(r.lower[o], r.lower[p[PCE_RULE_LOWER_EV]]) > 0)
            p[PCE_RULE_LOWER_EV] = o;
        if (p[PCE_RULE_MAXIMIN] == PCE_STRAT_NONE || r.worst[o] > r.worst[p[PCE_RULE_MAXIMIN]])
            p[PCE_RULE_MAXIMIN] = o;
        if (p[PCE_RULE_REGRET] == PCE_STRAT_NONE || r.regret[o] < r.regret[p[PCE_RULE_REGRET]])
            p[PCE_RULE_REGRET] = o;
        bool out_of_it = false;
        for (uint32_t q = 0; q < no; q++)
            if ((allowed & (1u << q)) != 0u && rat_cmp(r.lower[q], r.upper[o]) > 0)
                out_of_it = true;
        if (!out_of_it) r.contention |= 1u << o;
    }

    r.leader = PCE_STRAT_NONE;
    r.votes = 0;
    for (uint32_t o = 0; o < no; o++) {
        uint32_t c = 0;
        for (uint32_t k = 0; k < PCE_RULES; k++) c += r.pick[k] == o;
        if (c > r.votes) {
            r.votes = c;
            r.leader = o;
        }
    }
    uint8_t base, pol = LPX_P_NEUTRAL;
    uint32_t adv = 0;
    if (r.leader == PCE_STRAT_NONE) {
        base = LPX_B_NEITHER;
    } else {
        base = r.votes == PCE_RULES ? LPX_B_TRUE : LPX_B_BOTH;
        if (prev != NULL && prev->leader != PCE_STRAT_NONE && prev->votes <= PCE_RULES)
            pol = lpx_polarity(prev->votes, PCE_RULES - prev->votes, r.votes, PCE_RULES - r.votes,
                               false);
        if (in->flags != NULL) adv = in->flags[r.leader] & PCE_ADV_ALL;
    }
    r.v = pce_verdict(base, pol, in->phase, adv);

    pce_hash_t h;
    pce_hash_begin(&h, PCE_OP_STRAT);
    pce_hash_u64(&h, ((uint64_t) in->purpose << 8) | in->phase);
    pce_hash_u64(&h, ((uint64_t) no << 32) | ns);
    for (uint32_t i = 0; i < no * ns; i++) pce_hash_i64(&h, in->payoff[i]);
    for (uint32_t s = 0; s < ns; s++) {
        pce_hash_rat(&h, in->lo[s]);
        pce_hash_rat(&h, in->hi[s]);
    }
    for (uint32_t o = 0; o < no; o++) pce_hash_u64(&h, in->flags ? in->flags[o] : 0u);
    pce_hash_u64(&h, prev != NULL && prev->leader != PCE_STRAT_NONE ? 1u + prev->votes : 0u);
    pce_hash_end(&h, r.in);
    pce_hash_begin(&h, PCE_OP_STRAT);
    for (uint32_t k = 0; k < PCE_RULES; k++) pce_hash_u64(&h, r.pick[k]);
    for (uint32_t o = 0; o < no; o++) {
        pce_hash_rat(&h, r.lower[o]);
        pce_hash_rat(&h, r.upper[o]);
        pce_hash_i64(&h, r.worst[o]);
        pce_hash_i64(&h, r.regret[o]);
    }
    pce_hash_u64(&h, ((uint64_t) r.contention << 32) | r.refused);
    pce_hash_u64(&h, ((uint64_t) r.leader << 32) | r.votes);
    hash_out_verdict(&h, &r.v);
    pce_hash_end(&h, r.out);
    int rc = pce_log(log, PCE_OP_STRAT, &r.v, r.votes, r.in, r.out);
    if (rc != PCE_OK) return rc;
    pce_copy(out, &r, sizeof r);
    return PCE_OK;
}

int pce_strat_zopa(const pce_est_t *est, uint32_t n, uint32_t purpose, uint32_t advisory,
                   uint8_t phase, pdx_log_t *log, pce_zopa_t *out)
{
    if (out == NULL || n > PCE_STRAT_EST || phase > 3u || (n > 0u && est == NULL)) return PCE_E_ARG;
    if (purpose != PCE_PURPOSE_NEGOTIATION && purpose != PCE_PURPOSE_BUSINESS) return PCE_E_PURPOSE;
    pce_zopa_t z;
    z.yes = z.no = 0;
    z.range_lo = z.range_hi = 0;
    int64_t clo = 0, chi = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (est[i].seller_min <= est[i].buyer_max)
            z.yes++;
        else
            z.no++;
        if (i == 0u || est[i].seller_min > clo) clo = est[i].seller_min;
        if (i == 0u || est[i].buyer_max < chi) chi = est[i].buyer_max;
    }
    uint8_t base;
    if (n == 0u)
        base = LPX_B_NEITHER;
    else if (z.yes == 0u)
        base = LPX_B_FALSE;
    else if (z.no == 0u && clo <= chi) {
        base = LPX_B_TRUE;
        z.range_lo = clo;
        z.range_hi = chi;
    } else
        base = LPX_B_BOTH;
    z.v = pce_verdict(base, LPX_P_NEUTRAL, phase, advisory);

    pce_hash_t h;
    pce_hash_begin(&h, PCE_OP_STRAT);
    pce_hash_u64(&h, 0x5A4F5041u); /* "ZOPA" */
    pce_hash_u64(&h, ((uint64_t) purpose << 40) | ((uint64_t) advisory << 8) | phase);
    pce_hash_u64(&h, n);
    for (uint32_t i = 0; i < n; i++) {
        pce_hash_i64(&h, est[i].seller_min);
        pce_hash_i64(&h, est[i].buyer_max);
    }
    pce_hash_end(&h, z.in);
    pce_hash_begin(&h, PCE_OP_STRAT);
    pce_hash_u64(&h, ((uint64_t) z.yes << 32) | z.no);
    pce_hash_i64(&h, z.range_lo);
    pce_hash_i64(&h, z.range_hi);
    hash_out_verdict(&h, &z.v);
    pce_hash_end(&h, z.out);
    int rc = pce_log(log, PCE_OP_STRAT, &z.v, z.yes, z.in, z.out);
    if (rc != PCE_OK) return rc;
    pce_copy(out, &z, sizeof z);
    return PCE_OK;
}
