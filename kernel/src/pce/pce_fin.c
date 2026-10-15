/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* pce_fin.c — the financial analysis engine (pce_fin.h F1-F5). */
#include "pce_fin.h"
#include "pce_prob.h"

static bool money_ok(int64_t v)
{
    return v <= PCE_FIN_MONEY_MAX && v >= -PCE_FIN_MONEY_MAX;
}

static void hash_verdict(pce_hash_t *h, const pce_verdict_t *v)
{
    pce_hash_u64(h, ((uint64_t) v->tag << 16) | ((uint64_t) v->act << 8) | v->kind);
}

/* F3: x / y over the ranges, by the four corners */
static pce_ratio_t ratio(const pce_field_t *x, const pce_field_t *y)
{
    pce_ratio_t r;
    r.base = LPX_B_NEITHER;
    r.lo = r.hi = rat_zero();
    if (x->base == LPX_B_NEITHER || y->base == LPX_B_NEITHER) return r;
    if (y->lo <= 0 && y->hi >= 0) return r; /* touches zero */
    rat_t c[4] = {rat_make(x->lo, y->lo), rat_make(x->lo, y->hi), rat_make(x->hi, y->lo),
                  rat_make(x->hi, y->hi)};
    rat_t lo = c[0], hi = c[0];
    for (uint32_t i = 1; i < 4u; i++) {
        lo = pce_rat_min(lo, c[i]);
        hi = pce_rat_max(hi, c[i]);
    }
    if (!lo.valid || !hi.valid) return r;
    r.lo = lo;
    r.hi = hi;
    r.base = rat_eq(lo, hi) ? LPX_B_TRUE : LPX_B_BOTH;
    return r;
}

int pce_fin_analyse(const pce_stmt_t *s, uint32_t n, uint8_t phase, pdx_log_t *log,
                    pce_fin_report_t *out)
{
    if (s == NULL || out == NULL || n == 0u || n > PCE_FIN_SRC || phase > 3u) return PCE_E_ARG;
    for (uint32_t i = 0; i < n; i++) {
        if (s[i].standard > PCE_STD_IFRS || (s[i].present >> PCE_FIN_FIELDS) != 0u)
            return PCE_E_ARG;
        for (uint32_t f = 0; f < PCE_FIN_FIELDS; f++)
            if ((s[i].present >> f) & 1u && !money_ok(s[i].v[f])) return PCE_E_ARG;
    }

    pce_fin_report_t r;
    r.quarantined = r.used = 0;
    const uint32_t ale =
        (1u << PCE_FIN_ASSETS) | (1u << PCE_FIN_LIABILITIES) | (1u << PCE_FIN_EQUITY);
    for (uint32_t i = 0; i < n; i++) {
        bool broken = (s[i].present & ale) == ale &&
                      s[i].v[PCE_FIN_ASSETS] !=
                          s[i].v[PCE_FIN_LIABILITIES] + s[i].v[PCE_FIN_EQUITY]; /* < 2^62 */
        if (broken)
            r.quarantined |= 1u << i;
        else
            r.used |= 1u << i;
    }

    bool any_both = false, any_same = false, any_field = false;
    for (uint32_t f = 0; f < PCE_FIN_FIELDS; f++) {
        pce_field_t *fd = &r.field[f];
        fd->base = LPX_B_NEITHER;
        fd->lo = fd->hi = 0;
        fd->n = 0;
        fd->same_std = false;
        for (uint32_t i = 0; i < n; i++) {
            if (((r.used >> i) & 1u) == 0u || ((s[i].present >> f) & 1u) == 0u) continue;
            int64_t v = s[i].v[f];
            if (fd->n == 0u || v < fd->lo) fd->lo = v;
            if (fd->n == 0u || v > fd->hi) fd->hi = v;
            fd->n++;
            for (uint32_t j = 0; j < i; j++)
                if ((r.used >> j) & 1u && (s[j].present >> f) & 1u && s[j].v[f] != v &&
                    s[j].standard == s[i].standard)
                    fd->same_std = true;
        }
        if (fd->n == 0u) continue;
        any_field = true;
        fd->base = fd->lo == fd->hi ? LPX_B_TRUE : LPX_B_BOTH;
        if (fd->base == LPX_B_BOTH) any_both = true;
        if (fd->same_std) any_same = true;
    }
    r.ratio[PCE_RATIO_CURRENT] = ratio(&r.field[PCE_FIN_CUR_ASSETS], &r.field[PCE_FIN_CUR_LIAB]);
    r.ratio[PCE_RATIO_DEBT_EQUITY] = ratio(&r.field[PCE_FIN_LIABILITIES], &r.field[PCE_FIN_EQUITY]);
    r.ratio[PCE_RATIO_NET_MARGIN] = ratio(&r.field[PCE_FIN_NET_INCOME], &r.field[PCE_FIN_REVENUE]);
    r.ratio[PCE_RATIO_INTEREST_COVER] = ratio(&r.field[PCE_FIN_EBIT], &r.field[PCE_FIN_INTEREST]);

    uint8_t base = !any_field ? LPX_B_NEITHER : any_both ? LPX_B_BOTH : LPX_B_TRUE;
    uint8_t pol = !any_both ? LPX_P_NEUTRAL : any_same ? LPX_P_ENTROPIC : LPX_P_NEGENTROPIC;
    r.v = pce_verdict(base, pol, phase, PCE_ADV_MONEY);

    pce_hash_t h;
    pce_hash_begin(&h, PCE_OP_FIN);
    pce_hash_u64(&h, ((uint64_t) n << 8) | phase);
    for (uint32_t i = 0; i < n; i++) {
        pce_hash_u64(&h, ((uint64_t) s[i].standard << 32) | s[i].present);
        for (uint32_t f = 0; f < PCE_FIN_FIELDS; f++)
            pce_hash_i64(&h, (s[i].present >> f) & 1u ? s[i].v[f] : 0);
    }
    pce_hash_end(&h, r.in);
    pce_hash_begin(&h, PCE_OP_FIN);
    pce_hash_u64(&h, ((uint64_t) r.quarantined << 32) | r.used);
    for (uint32_t f = 0; f < PCE_FIN_FIELDS; f++) {
        const pce_field_t *fd = &r.field[f];
        pce_hash_u64(&h, ((uint64_t) fd->base << 40) | ((uint64_t) fd->same_std << 32) | fd->n);
        pce_hash_i64(&h, fd->lo);
        pce_hash_i64(&h, fd->hi);
    }
    for (uint32_t k = 0; k < PCE_FIN_RATIOS; k++) {
        pce_hash_u64(&h, r.ratio[k].base);
        pce_hash_rat(&h, r.ratio[k].lo);
        pce_hash_rat(&h, r.ratio[k].hi);
    }
    hash_verdict(&h, &r.v);
    pce_hash_end(&h, r.out);

    /* F2: one forbidden record per quarantined source, then the report */
    if (log != NULL) {
        uint32_t q = 0;
        for (uint32_t i = 0; i < n; i++) q += (r.quarantined >> i) & 1u;
        if (log->n + q + 1u > log->cap || log->n + q + 1u < log->n) return PCE_E_LOG;
        for (uint32_t i = 0; i < n; i++) {
            if (((r.quarantined >> i) & 1u) == 0u) continue;
            uint8_t sd[PDX_HASH];
            pce_hash_begin(&h, PCE_OP_FIN);
            pce_hash_u64(&h, i);
            pce_hash_i64(&h, s[i].v[PCE_FIN_ASSETS]);
            pce_hash_i64(&h, s[i].v[PCE_FIN_LIABILITIES]);
            pce_hash_i64(&h, s[i].v[PCE_FIN_EQUITY]);
            pce_hash_end(&h, sd);
            pce_verdict_t fv = pce_verdict(LPX_B_FALSE, LPX_P_FORBIDDEN, phase, PCE_ADV_MONEY);
            if (pce_log(log, PCE_OP_FIN, &fv, i, r.in, sd) != PCE_OK) return PCE_E_LOG;
        }
    }
    int rc = pce_log(log, PCE_OP_FIN, &r.v, r.used, r.in, r.out);
    if (rc != PCE_OK) return rc;
    pce_copy(out, &r, sizeof r);
    return PCE_OK;
}

int pce_fin_npv(const int64_t *cf, uint32_t n, const rat_t *rates, uint32_t nr, uint8_t phase,
                pdx_log_t *log, pce_npv_t *out)
{
    if (cf == NULL || rates == NULL || out == NULL || n == 0u || n > PCE_FIN_CF || nr == 0u ||
        nr > PCE_FIN_RATES || phase > 3u)
        return PCE_E_ARG;
    for (uint32_t t = 0; t < n; t++)
        if (!money_ok(cf[t])) return PCE_E_ARG;
    rat_t one = rat_from_int(1);
    for (uint32_t k = 0; k < nr; k++)
        if (!rates[k].valid || rat_cmp(rates[k], rat_neg(one)) <= 0) return PCE_E_ARG;

    pce_npv_t r;
    r.nrates = nr;
    bool all_ok = true;
    uint32_t pos = 0, neg = 0;
    for (uint32_t k = 0; k < PCE_FIN_RATES; k++) r.npv[k] = rat_zero();
    for (uint32_t k = 0; k < nr; k++) {
        rat_t g = rat_add(one, rates[k]);
        rat_t v = rat_from_int(cf[n - 1u]);
        for (uint32_t t = n - 1u; t > 0u; t--) v = rat_add(rat_from_int(cf[t - 1u]), rat_div(v, g));
        r.npv[k] = v;
        if (!v.valid) {
            all_ok = false;
            continue;
        }
        pos += v.num > 0;
        neg += v.num < 0;
    }
    r.lo = r.hi = rat_zero();
    uint8_t base;
    if (!all_ok) {
        base = LPX_B_NEITHER;
    } else {
        r.lo = r.hi = r.npv[0];
        for (uint32_t k = 1; k < nr; k++) {
            r.lo = pce_rat_min(r.lo, r.npv[k]);
            r.hi = pce_rat_max(r.hi, r.npv[k]);
        }
        base = pos == nr         ? LPX_B_TRUE
               : neg == nr       ? LPX_B_FALSE
               : pos + neg == 0u ? LPX_B_NEITHER
                                 : LPX_B_BOTH;
    }
    r.v = pce_verdict(base, LPX_P_NEUTRAL, phase, PCE_ADV_MONEY);

    pce_hash_t h;
    pce_hash_begin(&h, PCE_OP_FIN);
    pce_hash_u64(&h, 0x4E5056u); /* "NPV" */
    pce_hash_u64(&h, ((uint64_t) n << 16) | ((uint64_t) nr << 8) | phase);
    for (uint32_t t = 0; t < n; t++) pce_hash_i64(&h, cf[t]);
    for (uint32_t k = 0; k < nr; k++) pce_hash_rat(&h, rates[k]);
    pce_hash_end(&h, r.in);
    pce_hash_begin(&h, PCE_OP_FIN);
    for (uint32_t k = 0; k < nr; k++) pce_hash_rat(&h, r.npv[k]);
    pce_hash_rat(&h, r.lo);
    pce_hash_rat(&h, r.hi);
    hash_verdict(&h, &r.v);
    pce_hash_end(&h, r.out);
    int rc = pce_log(log, PCE_OP_FIN, &r.v, nr, r.in, r.out);
    if (rc != PCE_OK) return rc;
    pce_copy(out, &r, sizeof r);
    return PCE_OK;
}

/* F5 for one model: expected loss and value at risk */
static int one_model(const int64_t *loss, const rat_t *p, uint32_t n, rat_t level, rat_t *el,
                     int64_t *var)
{
    int rc = pce_expect(p, loss, n, el);
    if (rc != PCE_OK) return rc;
    uint32_t idx[PCE_FIN_OUTCOMES];
    for (uint32_t i = 0; i < n; i++) { /* losses ascending, ties by index */
        uint32_t j = i;
        while (j > 0u && loss[idx[j - 1u]] > loss[i]) {
            idx[j] = idx[j - 1u];
            j--;
        }
        idx[j] = i;
    }
    rat_t cum = rat_zero();
    for (uint32_t k = 0; k < n; k++) {
        cum = rat_add(cum, p[idx[k]]);
        if (rat_cmp(cum, level) >= 0) {
            *var = loss[idx[k]];
            return PCE_OK;
        }
    }
    return PCE_E_ARG; /* unreachable: the probabilities sum to 1 >= level */
}

int pce_fin_risk(const int64_t *loss, const rat_t *p, const rat_t *q, uint32_t n, rat_t level,
                 uint8_t phase, pdx_log_t *log, pce_risk_t *out)
{
    if (loss == NULL || p == NULL || out == NULL || n == 0u || n > PCE_FIN_OUTCOMES || phase > 3u)
        return PCE_E_ARG;
    if (!level.valid || level.num <= 0 || rat_cmp(level, rat_from_int(1)) > 0) return PCE_E_ARG;
    for (uint32_t i = 0; i < n; i++)
        if (!money_ok(loss[i])) return PCE_E_ARG;

    pce_risk_t r;
    r.nmodels = q != NULL ? 2u : 1u;
    r.expected[1] = rat_zero();
    r.var[1] = 0;
    int rc = one_model(loss, p, n, level, &r.expected[0], &r.var[0]);
    if (rc == PCE_OK && q != NULL) rc = one_model(loss, q, n, level, &r.expected[1], &r.var[1]);
    if (rc != PCE_OK) return rc;
    uint8_t base = LPX_B_NEITHER; /* one model: unwitnessed */
    if (q != NULL)
        base =
            rat_eq(r.expected[0], r.expected[1]) && r.var[0] == r.var[1] ? LPX_B_TRUE : LPX_B_BOTH;
    r.v = pce_verdict(base, LPX_P_NEUTRAL, phase, PCE_ADV_MONEY);

    pce_hash_t h;
    pce_hash_begin(&h, PCE_OP_FIN);
    pce_hash_u64(&h, 0x5249534Bu); /* "RISK" */
    pce_hash_u64(&h, ((uint64_t) n << 16) | ((uint64_t) r.nmodels << 8) | phase);
    pce_hash_rat(&h, level);
    for (uint32_t i = 0; i < n; i++) {
        pce_hash_i64(&h, loss[i]);
        pce_hash_rat(&h, p[i]);
        if (q != NULL) pce_hash_rat(&h, q[i]);
    }
    pce_hash_end(&h, r.in);
    pce_hash_begin(&h, PCE_OP_FIN);
    for (uint32_t m = 0; m < 2u; m++) {
        pce_hash_rat(&h, r.expected[m]);
        pce_hash_i64(&h, r.var[m]);
    }
    hash_verdict(&h, &r.v);
    pce_hash_end(&h, r.out);
    rc = pce_log(log, PCE_OP_FIN, &r.v, r.nmodels, r.in, r.out);
    if (rc != PCE_OK) return rc;
    pce_copy(out, &r, sizeof r);
    return PCE_OK;
}
