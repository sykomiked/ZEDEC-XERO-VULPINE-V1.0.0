/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* pce_entry.c — the engines as block entries (pce_entry.h B1-B3). */
#include "pce_entry.h"
#include "pce_prob.h"
#include "pce_strat.h"
#include "pce_fin.h"

static const pce_contract_t ENTRIES[] = {
    {"prob.join", PCE_KIND_NUM, {"a", "b"}, 2},
    {"prob.verdict", PCE_KIND_TRUTH, {"m", "th"}, 2},
    {"prob.bayes", PCE_KIND_NUM, {"prior", "like", "den"}, 3},
    {"strat.decide", PCE_KIND_TRUTH, {"payoff", "nopt", "lo", "hi", "den"}, 5},
    {"strat.leader", PCE_KIND_NUM, {"payoff", "nopt", "lo", "hi", "den"}, 5},
    {"strat.zopa", PCE_KIND_TRUTH, {"seller_min", "buyer_max"}, 2},
    {"fin.npv", PCE_KIND_NUM, {"cf", "rate_num", "rate_den"}, 3},
    {"fin.npv_sign", PCE_KIND_TRUTH, {"cf", "rate_num", "rate_den"}, 3},
};
#define NENTRIES (sizeof ENTRIES / sizeof ENTRIES[0])

static bool streq(const char *a, const char *b)
{
    if (a == NULL || b == NULL) return false;
    while (*a != '\0' && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

const pce_contract_t *pce_entries(uint32_t *n)
{
    if (n) *n = (uint32_t) NENTRIES;
    return ENTRIES;
}

const pce_contract_t *pce_entry_find(const char *entry)
{
    for (uint32_t i = 0; i < NENTRIES; i++)
        if (streq(ENTRIES[i].entry, entry)) return &ENTRIES[i];
    return NULL;
}

static pce_num_t num_of(rat_t r)
{
    pce_num_t x = {r.num, r.den};
    return x;
}

static pce_num_t truth_of(uint8_t base)
{
    pce_num_t x = {(int64_t) lpx_base_to_lpres(base), 0};
    return x;
}

static int mass_of(const pce_arg_t *a, pce_mass_t *m)
{
    if (a->n != 4u) return PCE_E_ARG;
    for (uint32_t i = 0; i < 4u; i++)
        if (a->v[i] < 0) return PCE_E_ARG;
    return pce_mass_make(m, (uint64_t) a->v[0], (uint64_t) a->v[1], (uint64_t) a->v[2],
                         (uint64_t) a->v[3]);
}

/* strategy: payoff nopt lo hi den -> a decision */
static int strat_of(const pce_arg_t *a, pce_strat_result_t *r)
{
    if (a[1].n != 1u || a[4].n != 1u || a[4].v[0] <= 0) return PCE_E_ARG;
    uint32_t ns = a[2].n;
    if (a[3].n != ns || ns == 0u || ns > PCE_STRAT_SCEN) return PCE_E_ARG;
    int64_t no = a[1].v[0];
    if (no <= 0 || no > (int64_t) PCE_STRAT_OPTS || a[0].n != (uint32_t) no * ns) return PCE_E_ARG;
    rat_t lo[PCE_STRAT_SCEN], hi[PCE_STRAT_SCEN];
    for (uint32_t s = 0; s < ns; s++) {
        lo[s] = rat_make(a[2].v[s], a[4].v[0]);
        hi[s] = rat_make(a[3].v[s], a[4].v[0]);
    }
    pce_strat_in_t in = {(uint32_t) no,  ns, a[0].v, lo, hi, NULL, PCE_PURPOSE_BUSINESS,
                         LPX_PH_MANIFEST};
    return pce_strat_decide(&in, NULL, NULL, r);
}

static int npv_of(const pce_arg_t *a, pce_npv_t *r)
{
    uint32_t nr = a[1].n;
    if (a[2].n != nr || nr == 0u || nr > PCE_FIN_RATES) return PCE_E_ARG;
    rat_t rates[PCE_FIN_RATES];
    for (uint32_t k = 0; k < nr; k++) {
        if (a[2].v[k] <= 0) return PCE_E_ARG;
        rates[k] = rat_make(a[1].v[k], a[2].v[k]);
    }
    int rc = pce_fin_npv(a[0].v, a[0].n, rates, nr, LPX_PH_MANIFEST, NULL, r);
    if (rc != PCE_OK) return rc;
    for (uint32_t k = 0; k < nr; k++)
        if (!r->npv[k].valid) return PCE_E_OVERFLOW;
    return PCE_OK;
}

int pce_entry_call(const char *entry, const pce_arg_t *args, uint32_t nargs, pce_num_t *out,
                   uint32_t cap, uint32_t *n_out)
{
    const pce_contract_t *c = pce_entry_find(entry);
    if (c == NULL) return PCE_E_NAME;
    if (n_out == NULL || (args == NULL && nargs > 0u) || nargs != c->ninputs) return PCE_E_ARG;
    for (uint32_t i = 0; i < nargs; i++)
        if (!streq(args[i].name, c->input[i]) || args[i].v == NULL || args[i].n == 0u)
            return PCE_E_ARG;

    pce_num_t res[PCE_N_MAX];
    uint32_t nres = 0;
    int rc = PCE_OK;
    if (streq(c->entry, "prob.join")) {
        pce_mass_t a, b, j;
        rc = mass_of(&args[0], &a);
        if (rc == PCE_OK) rc = mass_of(&args[1], &b);
        if (rc == PCE_OK) rc = pce_mass_join(&a, &b, &j);
        if (rc == PCE_OK) {
            static const uint8_t ord[4] = {LPRES_STATE_TRUE, LPRES_STATE_FALSE, LPRES_STATE_BOTH,
                                           LPRES_STATE_NEITHER};
            for (uint32_t i = 0; i < 4u; i++) res[nres++] = num_of(j.m[ord[i]]);
        }
    } else if (streq(c->entry, "prob.verdict")) {
        pce_mass_t m;
        rc = mass_of(&args[0], &m);
        if (rc == PCE_OK && args[1].n != 4u) rc = PCE_E_ARG;
        if (rc == PCE_OK) {
            const int64_t *t = args[1].v;
            pce_thresh_t th = {rat_make(t[0], t[1]), rat_make(t[2], t[3])};
            pce_verdict_t v;
            rc = pce_mass_decide(NULL, &m, &th, false, LPX_PH_MANIFEST, 0, NULL, &v);
            if (rc == PCE_OK) res[nres++] = truth_of(lpx_base(v.tag));
        }
    } else if (streq(c->entry, "prob.bayes")) {
        uint32_t n = args[0].n;
        if (args[1].n != n || args[2].n != 1u || args[2].v[0] <= 0 || n > PCE_N_MAX) {
            rc = PCE_E_ARG;
        } else {
            rat_t prior[PCE_N_MAX], like[PCE_N_MAX], post[PCE_N_MAX];
            int64_t total = 0;
            for (uint32_t i = 0; i < n && rc == PCE_OK; i++) {
                if (args[0].v[i] < 0 || args[0].v[i] > INT64_MAX - total)
                    rc = PCE_E_ARG;
                else
                    total += args[0].v[i];
            }
            if (rc == PCE_OK && total == 0) rc = PCE_E_ARG;
            for (uint32_t i = 0; i < n && rc == PCE_OK; i++) {
                prior[i] = rat_make(args[0].v[i], total);
                like[i] = rat_make(args[1].v[i], args[2].v[0]);
            }
            if (rc == PCE_OK) rc = pce_bayes(prior, like, n, post);
            for (uint32_t i = 0; i < n && rc == PCE_OK; i++) res[nres++] = num_of(post[i]);
        }
    } else if (streq(c->entry, "strat.decide") || streq(c->entry, "strat.leader")) {
        pce_strat_result_t r;
        rc = strat_of(args, &r);
        if (rc == PCE_OK) {
            if (c->kind == PCE_KIND_TRUTH)
                res[nres++] = truth_of(lpx_base(r.v.tag));
            else
                res[nres++] = (pce_num_t){r.leader == PCE_STRAT_NONE ? -1 : (int64_t) r.leader, 1};
        }
    } else if (streq(c->entry, "strat.zopa")) {
        uint32_t n = args[0].n;
        pce_est_t est[PCE_STRAT_EST];
        pce_zopa_t z;
        if (args[1].n != n || n > PCE_STRAT_EST) {
            rc = PCE_E_ARG;
        } else {
            for (uint32_t i = 0; i < n; i++) {
                est[i].seller_min = args[0].v[i];
                est[i].buyer_max = args[1].v[i];
            }
            rc = pce_strat_zopa(est, n, PCE_PURPOSE_NEGOTIATION, PCE_ADV_MONEY, LPX_PH_MANIFEST,
                                NULL, &z);
            if (rc == PCE_OK) res[nres++] = truth_of(lpx_base(z.v.tag));
        }
    } else { /* fin.npv, fin.npv_sign */
        pce_npv_t r;
        rc = npv_of(args, &r);
        if (rc == PCE_OK) {
            if (c->kind == PCE_KIND_TRUTH)
                res[nres++] = truth_of(lpx_base(r.v.tag));
            else
                for (uint32_t k = 0; k < r.nrates; k++) res[nres++] = num_of(r.npv[k]);
        }
    }
    if (rc != PCE_OK) return rc;
    *n_out = nres;
    if (nres > cap || (out == NULL && nres > 0u)) return PCE_E_BUFFER;
    for (uint32_t i = 0; i < nres; i++) out[i] = res[i];
    return PCE_OK;
}
