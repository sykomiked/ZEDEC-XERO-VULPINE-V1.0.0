/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* pce_prob.c — the probability engine (pce_prob.h P1-P6). */
#include "pce_prob.h"

#define I64_MAX ((uint64_t) 0x7FFFFFFFFFFFFFFFull)

static bool is_unit(rat_t r) /* valid and in [0, 1] */
{
    return pce_rat_nonneg(r) && rat_cmp(r, rat_from_int(1)) <= 0;
}

bool pce_mass_valid(const pce_mass_t *m)
{
    if (m == NULL) return false;
    rat_t s = rat_zero();
    for (uint32_t i = 0; i < 4u; i++) {
        if (!pce_rat_nonneg(m->m[i])) return false;
        s = rat_add(s, m->m[i]);
    }
    return s.valid && rat_eq(s, rat_from_int(1));
}

int pce_mass_set(pce_mass_t *out, rat_t t, rat_t f, rat_t b, rat_t n)
{
    if (out == NULL) return PCE_E_ARG;
    pce_mass_t m;
    m.m[LPRES_STATE_TRUE] = t;
    m.m[LPRES_STATE_FALSE] = f;
    m.m[LPRES_STATE_BOTH] = b;
    m.m[LPRES_STATE_NEITHER] = n;
    if (!pce_mass_valid(&m)) return PCE_E_ARG;
    *out = m;
    return PCE_OK;
}

int pce_mass_make(pce_mass_t *out, uint64_t t, uint64_t f, uint64_t b, uint64_t n)
{
    if (out == NULL) return PCE_E_ARG;
    if (t > I64_MAX || f > I64_MAX || b > I64_MAX || n > I64_MAX) return PCE_E_ARG;
    uint64_t sum = t;
    if (f > I64_MAX - sum) return PCE_E_ARG;
    sum += f;
    if (b > I64_MAX - sum) return PCE_E_ARG;
    sum += b;
    if (n > I64_MAX - sum) return PCE_E_ARG;
    sum += n;
    if (sum == 0u) return pce_mass_set(out, rat_zero(), rat_zero(), rat_zero(), rat_from_int(1));
    int64_t d = (int64_t) sum;
    return pce_mass_set(out, rat_make((int64_t) t, d), rat_make((int64_t) f, d),
                        rat_make((int64_t) b, d), rat_make((int64_t) n, d));
}

/* m(k) = sum of a(i) b(j) over the pairs the table sends to k. */
static int combine(const pce_mass_t *a, const pce_mass_t *b, const uint8_t tab[4][4],
                   pce_mass_t *out)
{
    if (out == NULL || !pce_mass_valid(a) || !pce_mass_valid(b)) return PCE_E_ARG;
    pce_mass_t r;
    for (uint32_t k = 0; k < 4u; k++) r.m[k] = rat_zero();
    for (uint32_t i = 0; i < 4u; i++)
        for (uint32_t j = 0; j < 4u; j++) {
            uint8_t k = tab[i][j] & 3u;
            r.m[k] = rat_add(r.m[k], rat_mul(a->m[i], b->m[j]));
        }
    for (uint32_t k = 0; k < 4u; k++)
        if (!r.m[k].valid) return PCE_E_OVERFLOW;
    *out = r;
    return PCE_OK;
}

typedef lpres_state_t (*op2_fn)(lpres_state_t, lpres_state_t);

static void table_of(op2_fn f, uint8_t tab[4][4])
{
    for (uint32_t i = 0; i < 4u; i++)
        for (uint32_t j = 0; j < 4u; j++)
            tab[i][j] = (uint8_t) f((lpres_state_t) i, (lpres_state_t) j);
}

int pce_mass_join(const pce_mass_t *a, const pce_mass_t *b, pce_mass_t *out)
{
    uint8_t tab[4][4];
    for (uint32_t i = 0; i < 4u; i++)
        for (uint32_t j = 0; j < 4u; j++) tab[i][j] = (uint8_t) (i | j);
    return combine(a, b, tab, out);
}

int pce_mass_meet(const pce_mass_t *a, const pce_mass_t *b, pce_mass_t *out)
{
    uint8_t tab[4][4];
    for (uint32_t i = 0; i < 4u; i++)
        for (uint32_t j = 0; j < 4u; j++) tab[i][j] = (uint8_t) (i & j);
    return combine(a, b, tab, out);
}

int pce_mass_and(const pce_mass_t *a, const pce_mass_t *b, pce_mass_t *out)
{
    uint8_t tab[4][4];
    table_of(lpres_conjoin, tab);
    return combine(a, b, tab, out);
}

int pce_mass_or(const pce_mass_t *a, const pce_mass_t *b, pce_mass_t *out)
{
    uint8_t tab[4][4];
    table_of(lpres_disjoin, tab);
    return combine(a, b, tab, out);
}

int pce_mass_not(const pce_mass_t *a, pce_mass_t *out)
{
    if (out == NULL || !pce_mass_valid(a)) return PCE_E_ARG;
    pce_mass_t r;
    for (uint32_t k = 0; k < 4u; k++) r.m[k] = rat_zero();
    for (uint32_t i = 0; i < 4u; i++) {
        uint8_t k = (uint8_t) lpres_negate((lpres_state_t) i) & 3u;
        r.m[k] = rat_add(r.m[k], a->m[i]);
    }
    *out = r;
    return PCE_OK;
}

int pce_mass_dempster(const pce_mass_t *a, const pce_mass_t *b, pce_mass_t *out, rat_t *conflict)
{
    if (out == NULL || !pce_mass_valid(a) || !pce_mass_valid(b)) return PCE_E_ARG;
    if (!rat_is_zero(a->m[LPRES_STATE_BOTH]) || !rat_is_zero(b->m[LPRES_STATE_BOTH]))
        return PCE_E_ARG;
    const rat_t aT = a->m[LPRES_STATE_TRUE], aF = a->m[LPRES_STATE_FALSE];
    const rat_t aN = a->m[LPRES_STATE_NEITHER];
    const rat_t bT = b->m[LPRES_STATE_TRUE], bF = b->m[LPRES_STATE_FALSE];
    const rat_t bN = b->m[LPRES_STATE_NEITHER];
    /* Frame {t, f}: TRUE is {t}, FALSE is {f}, NEITHER is the whole frame. */
    rat_t k = rat_add(rat_mul(aT, bF), rat_mul(aF, bT));
    rat_t t = rat_add(rat_add(rat_mul(aT, bT), rat_mul(aT, bN)), rat_mul(aN, bT));
    rat_t f = rat_add(rat_add(rat_mul(aF, bF), rat_mul(aF, bN)), rat_mul(aN, bF));
    rat_t n = rat_mul(aN, bN);
    if (!k.valid || !t.valid || !f.valid || !n.valid) return PCE_E_OVERFLOW;
    rat_t norm = rat_sub(rat_from_int(1), k);
    if (!norm.valid) return PCE_E_OVERFLOW;
    if (rat_is_zero(norm)) return PCE_E_CONFLICT;
    pce_mass_t r;
    r.m[LPRES_STATE_TRUE] = rat_div(t, norm);
    r.m[LPRES_STATE_FALSE] = rat_div(f, norm);
    r.m[LPRES_STATE_NEITHER] = rat_div(n, norm);
    r.m[LPRES_STATE_BOTH] = rat_zero();
    for (uint32_t i = 0; i < 4u; i++)
        if (!r.m[i].valid) return PCE_E_OVERFLOW;
    *out = r;
    if (conflict) *conflict = k;
    return PCE_OK;
}

int pce_mass_read(const pce_mass_t *m, pce_read_t *out)
{
    if (out == NULL || !pce_mass_valid(m)) return PCE_E_ARG;
    pce_read_t r;
    r.conflict = m->m[LPRES_STATE_BOTH];
    r.ignorance = m->m[LPRES_STATE_NEITHER];
    r.support = rat_add(m->m[LPRES_STATE_TRUE], r.conflict);
    r.against = rat_add(m->m[LPRES_STATE_FALSE], r.conflict);
    r.lo = m->m[LPRES_STATE_TRUE];
    r.hi = rat_sub(rat_from_int(1), m->m[LPRES_STATE_FALSE]);
    if (!r.support.valid || !r.against.valid || !r.hi.valid) return PCE_E_OVERFLOW;
    *out = r;
    return PCE_OK;
}

rat_t pce_mass_margin(const pce_mass_t *m)
{
    if (!pce_mass_valid(m)) return rat_make(0, 0);
    return rat_abs(rat_sub(m->m[LPRES_STATE_TRUE], m->m[LPRES_STATE_FALSE]));
}

static bool thresh_valid(const pce_thresh_t *th)
{
    return th != NULL && is_unit(th->decide) && is_unit(th->conflict) && !rat_is_zero(th->decide) &&
           !rat_is_zero(th->conflict);
}

uint8_t pce_mass_base(const pce_mass_t *m, const pce_thresh_t *th)
{
    if (!pce_mass_valid(m) || !thresh_valid(th)) return LPX_B_NEITHER;
    if (rat_cmp(m->m[LPRES_STATE_BOTH], th->conflict) >= 0) return LPX_B_BOTH;
    if (rat_cmp(m->m[LPRES_STATE_TRUE], th->decide) >= 0) return LPX_B_TRUE;
    if (rat_cmp(m->m[LPRES_STATE_FALSE], th->decide) >= 0) return LPX_B_FALSE;
    return LPX_B_NEITHER;
}

static void hash_mass(pce_hash_t *h, const pce_mass_t *m)
{
    for (uint32_t i = 0; i < 4u; i++) pce_hash_rat(h, m->m[i]);
}

int pce_mass_decide(const pce_mass_t *prev, const pce_mass_t *cur, const pce_thresh_t *th,
                    bool stale, uint8_t phase, uint32_t advisory, pdx_log_t *log,
                    pce_verdict_t *out)
{
    if (out == NULL || !pce_mass_valid(cur) || !thresh_valid(th) || phase > 3u) return PCE_E_ARG;
    if (prev != NULL && !pce_mass_valid(prev)) return PCE_E_ARG;
    uint8_t base = pce_mass_base(cur, th);
    uint8_t pol = prev != NULL ? pce_trend(pce_mass_margin(prev), pce_mass_margin(cur), stale)
                               : (stale ? LPX_P_ENTROPIC : LPX_P_NEUTRAL);
    pce_verdict_t v = pce_verdict(base, pol, phase, advisory);

    uint8_t in[PDX_HASH], res[PDX_HASH];
    pce_hash_t h;
    pce_hash_begin(&h, PCE_OP_PROB);
    pce_hash_u64(&h, prev != NULL ? 1u : 0u);
    if (prev != NULL) hash_mass(&h, prev);
    hash_mass(&h, cur);
    pce_hash_rat(&h, th->decide);
    pce_hash_rat(&h, th->conflict);
    pce_hash_u64(&h, ((uint64_t) (stale ? 1u : 0u) << 8) | phase);
    pce_hash_u64(&h, advisory);
    pce_hash_end(&h, in);
    pce_hash_begin(&h, PCE_OP_PROB);
    pce_hash_u64(&h, ((uint64_t) v.tag << 16) | ((uint64_t) v.act << 8) | v.kind);
    pce_hash_end(&h, res);
    int rc = pce_log(log, PCE_OP_PROB, &v, 0u, in, res);
    if (rc != PCE_OK) return rc;
    *out = v;
    return PCE_OK;
}

int pce_dist_check(const rat_t *p, uint32_t n)
{
    if (p == NULL || n == 0u || n > PCE_N_MAX) return PCE_E_ARG;
    rat_t s = rat_zero();
    for (uint32_t i = 0; i < n; i++) {
        if (!pce_rat_nonneg(p[i])) return PCE_E_ARG;
        s = rat_add(s, p[i]);
    }
    if (!s.valid) return PCE_E_OVERFLOW;
    return rat_eq(s, rat_from_int(1)) ? PCE_OK : PCE_E_ARG;
}

int pce_bayes(const rat_t *prior, const rat_t *like, uint32_t n, rat_t *post)
{
    int rc = pce_dist_check(prior, n);
    if (rc != PCE_OK) return rc;
    if (like == NULL || post == NULL) return PCE_E_ARG;
    rat_t joint[PCE_N_MAX];
    rat_t z = rat_zero();
    for (uint32_t i = 0; i < n; i++) {
        if (!is_unit(like[i])) return PCE_E_ARG;
        joint[i] = rat_mul(prior[i], like[i]);
        z = rat_add(z, joint[i]);
        if (!joint[i].valid) return PCE_E_OVERFLOW;
    }
    if (!z.valid) return PCE_E_OVERFLOW;
    if (rat_is_zero(z)) return PCE_E_CONFLICT;
    for (uint32_t i = 0; i < n; i++) {
        joint[i] = rat_div(joint[i], z);
        if (!joint[i].valid) return PCE_E_OVERFLOW;
    }
    for (uint32_t i = 0; i < n; i++) post[i] = joint[i];
    return PCE_OK;
}

int pce_rate(uint64_t s, uint64_t f, uint64_t a, uint64_t b, rat_t *out)
{
    if (out == NULL || a > I64_MAX || b > I64_MAX || s > I64_MAX || f > I64_MAX) return PCE_E_ARG;
    if (a == 0u && b == 0u) return PCE_E_ARG;
    if (s > I64_MAX - a) return PCE_E_OVERFLOW;
    uint64_t num = a + s;
    if (b > I64_MAX - num) return PCE_E_OVERFLOW;
    uint64_t den = num + b;
    if (f > I64_MAX - den) return PCE_E_OVERFLOW;
    den += f;
    rat_t r = rat_make((int64_t) num, (int64_t) den);
    if (!r.valid) return PCE_E_OVERFLOW;
    *out = r;
    return PCE_OK;
}

int pce_expect(const rat_t *p, const int64_t *v, uint32_t n, rat_t *out)
{
    int rc = pce_dist_check(p, n);
    if (rc != PCE_OK) return rc;
    if (v == NULL || out == NULL) return PCE_E_ARG;
    rat_t e = rat_zero();
    for (uint32_t i = 0; i < n; i++) e = rat_add(e, rat_mul(p[i], rat_from_int(v[i])));
    if (!e.valid) return PCE_E_OVERFLOW;
    *out = e;
    return PCE_OK;
}

int pce_tv(const rat_t *p, const rat_t *q, uint32_t n, rat_t *out)
{
    int rc = pce_dist_check(p, n);
    if (rc == PCE_OK) rc = pce_dist_check(q, n);
    if (rc != PCE_OK) return rc;
    if (out == NULL) return PCE_E_ARG;
    rat_t s = rat_zero();
    for (uint32_t i = 0; i < n; i++) s = rat_add(s, rat_abs(rat_sub(p[i], q[i])));
    s = rat_div(s, rat_from_int(2));
    if (!s.valid) return PCE_E_OVERFLOW;
    *out = s;
    return PCE_OK;
}

uint8_t pce_witness(const rat_t *p, const rat_t *q, uint32_t n, rat_t tol, rat_t *tv)
{
    rat_t d;
    if (!pce_rat_nonneg(tol) || pce_tv(p, q, n, &d) != PCE_OK) return LPX_B_NEITHER;
    if (tv) *tv = d;
    return rat_cmp(d, tol) <= 0 ? LPX_B_TRUE : LPX_B_BOTH;
}
