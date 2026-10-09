/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* swarm_ledger.c — witness and triple ledger. See swarm_ledger.h. */
#include "swarm_ledger.h"

uint32_t swarm_witness_for(const swarm_budget_t *b, uint32_t slot) {
    if (!b || slot >= b->num_slots || !b->slots[slot].active) return 0;
    uint32_t active[SWARM_MAX_MODELS];
    uint32_t n = 0, j = 0;
    for (uint32_t i = 0; i < b->num_slots; i++) {
        if (!b->slots[i].active) continue;
        if (i == slot) j = n;
        active[n++] = i;
    }
    if (n < 2) return 0;                                   /* the kernel witnesses */
    uint64_t r;
    swarm_muldiv(b->cycle, 1, n - 1u, &r);                 /* cycle mod (n-1), no libgcc */
    uint32_t off = 1u + (uint32_t)r;
    return b->slots[active[(j + off) % n]].model_id;
}

uint32_t swarm_witness_cycle(const swarm_budget_t *posted,
                             const swarm_budget_t *pre_b,
                             const swarm_market_t *pre_m,
                             const swarm_emotion_state_t *pre_e,
                             swarm_witness_t *out) {
    if (!posted || !pre_b || !pre_m || !out) return 0;
    swarm_budget_t        b = *pre_b;
    swarm_market_t        m = *pre_m;
    swarm_emotion_state_t e;
    if (pre_e) e = *pre_e;
    swarm_market_begin_cycle(&b, &m, pre_e ? &e : 0);

    uint64_t total = 0;
    for (uint32_t i = 0; i < posted->num_slots; i++) total += posted->slots[i].allotted;
    bool adds_up = (total == posted->tokens_per_cycle);

    uint32_t k = 0;
    for (uint32_t i = 0; i < posted->num_slots; i++) {
        const swarm_slot_t *p = &posted->slots[i];
        if (!p->active) continue;
        swarm_verdict_t v = SWARM_WIT_FALSE;
        if (adds_up) {
            bool same = i < b.num_slots && b.slots[i].model_id == p->model_id &&
                        b.slots[i].allotted == p->allotted &&
                        b.slots[i].allotted_im == p->allotted_im &&
                        b.slots[i].allotted_mk == p->allotted_mk;
            v = same ? SWARM_WIT_TRUE : SWARM_WIT_GLUT;
        }
        out[k].model_id   = p->model_id;
        out[k].witness_id = swarm_witness_for(posted, i);
        out[k].verdict    = v;
        k++;
    }
    return k;
}

void swarm_witness_reward(swarm_market_t *m, const swarm_witness_t *w, uint32_t n) {
    if (!m || !w) return;
    for (uint32_t k = 0; k < n; k++)
        if (w[k].witness_id) swarm_market_credit(m, w[k].witness_id, SWARM_CAP_SOCIAL, 1);
}

void swarm_ledger_init(swarm_ledger_t *l) {
    if (!l) return;
    l->head = 0;
    l->posted = l->settled_cycles = l->held_cycles = 0;
}

static void post(swarm_ledger_t *l, swarm_ledger_entry_t e) {
    l->e[l->head] = e;
    l->head = (l->head + 1u) % SWARM_LEDGER_CAP;
    l->posted++;
}

bool swarm_ledger_post_cycle(swarm_ledger_t *l, const swarm_budget_t *b,
                             const swarm_market_t *m, uint64_t imag_pool,
                             const swarm_witness_t *w, uint32_t nw) {
    if (!l || !b || !m || (!w && nw)) return false;
    uint64_t total = 0, imag = 0;
    for (uint32_t i = 0; i < b->num_slots; i++) {
        const swarm_slot_t *s = &b->slots[i];
        const swarm_trader_t *t = swarm_market_trader(m, s->model_id);
        total += s->allotted;
        imag  += s->allotted_im;
        swarm_ledger_entry_t fe = { b->cycle, SWARM_LEDGER_FINANCIAL, s->model_id, 0,
                                    s->allotted - s->allotted_im, t ? t->paid : 0,
                                    SWARM_WIT_TRUE };
        post(l, fe);
        swarm_ledger_entry_t xe = { b->cycle, SWARM_LEDGER_EXTERNALITY, s->model_id, 0,
                                    s->allotted_im, 0, SWARM_WIT_TRUE };
        post(l, xe);
    }
    bool all_true = true;
    uint32_t active = 0;
    for (uint32_t i = 0; i < b->num_slots; i++) active += b->slots[i].active ? 1u : 0u;
    for (uint32_t k = 0; k < nw; k++) {
        swarm_ledger_entry_t pe = { b->cycle, SWARM_LEDGER_PROVENANCE, w[k].model_id,
                                    w[k].witness_id, 0, 0, w[k].verdict };
        post(l, pe);
        if (w[k].verdict != SWARM_WIT_TRUE) all_true = false;
    }
    bool settles = all_true && nw == active && total == b->tokens_per_cycle &&
                   imag == imag_pool && swarm_market_conserved(m);
    if (settles) l->settled_cycles++; else l->held_cycles++;
    return settles;
}
