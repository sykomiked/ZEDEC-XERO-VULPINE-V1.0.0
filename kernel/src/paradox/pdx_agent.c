/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* pdx_agent.c — four-valued agents with contradictory premises (P2.2). See
 * pdx_agent.h and docs/PARACONSISTENT_LEDGER.md. */
#include "pdx_agent.h"
#include "../robin_debanks/sha256.h"

uint32_t pag_count(pag_set_t s)
{
    uint32_t n = 0;
    while (s) {
        s &= s - 1u;
        n++;
    }
    return n;
}

bool pag_init(pag_agent_t *a, uint32_t n_prop, pag_set_t *ft, pag_set_t *ff, pag_set_t *t,
              pag_set_t *f, uint32_t *pos0, uint32_t *neg0, const pag_rule_t *rules,
              uint32_t n_rule)
{
    if (!a || !n_prop || !ft || !ff || !t || !f || !pos0 || !neg0 || (n_rule && !rules))
        return false;
    for (uint32_t r = 0; r < n_rule; r++) {
        if (rules[r].head >= n_prop || !rules[r].n || rules[r].n > PAG_MAX_BODY) return false;
        for (uint32_t k = 0; k < rules[r].n; k++)
            if (rules[r].lit[k] >= n_prop) return false;
    }
    a->n_prop = n_prop;
    a->ft = ft;
    a->ff = ff;
    a->t = t;
    a->f = f;
    a->pos0 = pos0;
    a->neg0 = neg0;
    a->rule = rules;
    a->n_rule = n_rule;
    a->bad = a->qlogged = 0;
    a->rounds = 0;
    for (uint32_t p = 0; p < n_prop; p++) {
        ft[p] = ff[p] = t[p] = f[p] = 0;
        pos0[p] = neg0[p] = 0;
    }
    return true;
}

bool pag_tell(pag_agent_t *a, uint32_t prop, uint32_t src, bool value)
{
    if (!a || prop >= a->n_prop || src >= PAG_MAX_SOURCES) return false;
    pag_set_t bit = (pag_set_t) 1u << src;
    if (value)
        a->ft[prop] |= bit;
    else
        a->ff[prop] |= bit;
    return true;
}

bool pag_merge(pag_agent_t *dst, const pag_agent_t *src)
{
    if (!dst || !src || dst->n_prop != src->n_prop) return false;
    for (uint32_t p = 0; p < dst->n_prop; p++) {
        dst->ft[p] |= src->ft[p];
        dst->ff[p] |= src->ff[p];
    }
    return true;
}

/* support of one literal */
static void lit_sets(const pag_agent_t *a, uint32_t p, bool neg, pag_set_t *lt, pag_set_t *lf)
{
    *lt = neg ? a->f[p] : a->t[p];
    *lf = neg ? a->t[p] : a->f[p];
}

bool pag_run(pag_agent_t *a, lpx_qlog_t *q)
{
    if (!a) return false;
    /* G4: sources that told both p and not-p, for any p */
    pag_set_t bad = 0;
    for (uint32_t p = 0; p < a->n_prop; p++) bad |= a->ft[p] & a->ff[p];
    a->bad = bad;
    pag_set_t fresh = bad & ~a->qlogged;
    if (q && fresh) {
        if (q->n > q->cap || q->cap - q->n < pag_count(fresh)) return false;
        for (uint32_t s = 0; s < PAG_MAX_SOURCES; s++) {
            if (!(fresh >> s & 1u)) continue;
            uint8_t b[14 + 4], subj[32]; /* tag || source (u32 LE) */
            const char *tagname = "zxv/pag-bad/v1";
            uint32_t k = 0;
            while (tagname[k]) {
                b[k] = (uint8_t) tagname[k];
                k++;
            }
            for (uint32_t i = 0; i < 4; i++) b[k++] = (uint8_t) (s >> (8 * i));
            sha256(b, k, subj);
            lpx_qlog_append(q, lpx_make(LPX_B_BOTH, LPX_P_FORBIDDEN, LPX_PH_MANIFEST),
                            PAG_Q_SELF_CONTRADICTION, s, subj);
        }
        a->qlogged |= fresh;
    }
    /* G2: fixed point */
    for (uint32_t p = 0; p < a->n_prop; p++) {
        a->t[p] = a->ft[p] & ~bad;
        a->f[p] = a->ff[p] & ~bad;
    }
    uint32_t limit = 2u * PAG_MAX_SOURCES * a->n_prop + 1u;
    for (a->rounds = 1; a->rounds <= limit; a->rounds++) {
        bool grew = false;
        for (uint32_t p = 0; p < a->n_prop; p++) {
            pag_set_t ht = 0, hf = 0;
            bool any = false, all_false = true;
            for (uint32_t r = 0; r < a->n_rule; r++) {
                const pag_rule_t *ru = &a->rule[r];
                if (ru->head != p) continue;
                any = true;
                pag_set_t ct = 0, cf = 0;
                bool all_true = true;
                for (uint32_t k = 0; k < ru->n; k++) {
                    pag_set_t lt, lf;
                    lit_sets(a, ru->lit[k], ru->neg[k], &lt, &lf);
                    if (!lt) all_true = false;
                    ct |= lt;
                    cf |= lf;
                }
                if (all_true) ht |= ct;
                if (cf)
                    hf |= cf;
                else
                    all_false = false;
            }
            if (!any) continue;
            pag_set_t nt = a->t[p] | ht, nf = a->f[p] | (all_false ? hf : 0);
            if (nt != a->t[p] || nf != a->f[p]) grew = true;
            a->t[p] = nt;
            a->f[p] = nf;
        }
        if (!grew) return true;
    }
    return false;
}

lpres_state_t pag_value(const pag_agent_t *a, uint32_t prop)
{
    if (!a || prop >= a->n_prop) return LPRES_STATE_NEITHER;
    bool t = a->t[prop] != 0, f = a->f[prop] != 0;
    if (t && f) return LPRES_STATE_BOTH;
    if (t) return LPRES_STATE_TRUE;
    if (f) return LPRES_STATE_FALSE;
    return LPRES_STATE_NEITHER;
}

void pag_snapshot(pag_agent_t *a)
{
    if (!a) return;
    for (uint32_t p = 0; p < a->n_prop; p++) {
        a->pos0[p] = pag_count(a->t[p]);
        a->neg0[p] = pag_count(a->f[p]);
    }
}

pag_decision_t pag_decide(const pag_agent_t *a, uint32_t prop, bool stale,
                          const pdx_registry_t *reg, const uint8_t sig[PDX_HASH])
{
    pag_decision_t d;
    d.pos = d.neg = 0;
    pdx_evidence_t ev = {0, 0, 0, 0, stale};
    if (a && prop < a->n_prop) {
        d.pos = pag_count(a->t[prop]);
        d.neg = pag_count(a->f[prop]);
        ev.pos0 = a->pos0[prop];
        ev.neg0 = a->neg0[prop];
        ev.pos1 = d.pos;
        ev.neg1 = d.neg;
    }
    lpx_t tag = lpx_make(lpx_base_from_lpres(pag_value(a, prop)), LPX_P_NEUTRAL, LPX_PH_MANIFEST);
    pdx_class_t c = pdx_classify(tag, &ev, reg, sig);
    d.tag = c.tag;
    d.kind = c.kind;
    d.act = lpx_action(c.tag);
    return d;
}
