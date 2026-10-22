/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* swarm_deleg.c — the delegator position and the delegation tree. See
 * swarm_deleg.h. */
#include "swarm_deleg.h"

swd_status_t swd_position(const swd_cand_t *c, uint32_t n, uint32_t holder, swd_handoff_t *h)
{
    if (!h || (n && !c)) return SWD_EARG;
    uint32_t best = 0, held_score = 0;
    bool holder_in = false;
    for (uint32_t i = 0; i < n; i++) {
        if (!c[i].resident) continue;
        if (c[i].score > best) best = c[i].score;
        if (c[i].cell == holder) held_score = c[i].score;
    }
    h->from = holder;
    h->to = holder;
    h->score_from = held_score;
    h->score_to = held_score;
    h->moved = false;
    if (best == 0) {
        h->tv = LPRES_STATE_NEITHER;
        return SWD_OK;
    }
    /* the tied set: its first cell in placement order, whether the holder is
     * in it, and whether it spans more than one lineage */
    uint32_t first = SWD_NONE, lin = 0;
    bool many = false;
    for (uint32_t i = 0; i < n; i++) {
        if (!c[i].resident || c[i].score != best) continue;
        if (first == SWD_NONE) {
            first = i;
            lin = c[i].lineage;
        } else if (c[i].lineage != lin)
            many = true;
        if (c[i].cell == holder) holder_in = true;
    }
    h->tv = many ? LPRES_STATE_BOTH : LPRES_STATE_TRUE;
    if (holder_in) return SWD_OK;
    h->to = c[first].cell;
    h->score_to = best;
    h->moved = true;
    return SWD_OK;
}

swd_status_t swd_init(swd_tree_t *t, uint32_t max_depth, uint64_t token_budget)
{
    if (!t || max_depth < 1 || max_depth > SWD_MAX_DEPTH) return SWD_EARG;
    t->n = 0;
    t->max_depth = max_depth;
    t->budget = token_budget;
    t->returned = 0;
    t->phase = SWD_OUT;
    t->root_tv = LPRES_STATE_NEITHER;
    return SWD_OK;
}

static bool known(const swd_tree_t *t, uint32_t id)
{
    return t && id < t->n;
}

swd_status_t swd_open(swd_tree_t *t, uint32_t parent, uint32_t holder, uint64_t est_tokens,
                      swd_join_t join, uint32_t *id)
{
    if (!t || !id || (join != SWD_CONJ && join != SWD_DISJ)) return SWD_EARG;
    if (t->phase != SWD_OUT) return SWD_EPHASE;
    if (parent == SWD_NONE ? t->n != 0 : !known(t, parent)) return SWD_EARG;
    if (parent != SWD_NONE && t->node[parent].state != SWD_RUN) return SWD_EARG;
    /* check, then reserve, then record */
    swd_state_t st = SWD_RUN;
    uint32_t depth = parent == SWD_NONE ? 0 : t->node[parent].depth + 1;
    if (t->n >= SWD_MAX_NODES)
        st = SWD_REF_FULL;
    else if (depth > t->max_depth)
        st = SWD_REF_DEPTH;
    else if (parent != SWD_NONE && t->node[parent].n_child >= SWD_FANOUT)
        st = SWD_REF_FANOUT;
    else if (est_tokens > t->budget)
        st = SWD_REF_BUDGET;
    if (st == SWD_REF_FULL || st == SWD_REF_FANOUT) { /* no slot: lost, joins as NEITHER */
        t->node[parent].n_lost++;
        *id = SWD_NONE;
        return SWD_OK;
    }
    swd_node_t *nd = &t->node[t->n];
    nd->parent = parent;
    nd->holder = holder;
    nd->depth = depth;
    nd->reserved = st == SWD_RUN ? est_tokens : 0;
    nd->spent = 0;
    nd->join = join;
    nd->state = st;
    nd->witnessed = LPRES_STATE_NEITHER;
    nd->tv = LPRES_STATE_NEITHER;
    nd->n_child = 0;
    nd->n_lost = 0;
    if (st == SWD_RUN) t->budget -= est_tokens;
    if (parent != SWD_NONE) {
        swd_node_t *p = &t->node[parent];
        p->child[p->n_child++] = t->n;
    }
    *id = t->n++;
    return SWD_OK;
}

swd_status_t swd_turn(swd_tree_t *t)
{
    if (!t) return SWD_EARG;
    if (t->phase != SWD_OUT) return SWD_EPHASE;
    t->phase = SWD_IN;
    return SWD_OK;
}

static swd_status_t running_in(swd_tree_t *t, uint32_t id)
{
    if (!known(t, id)) return SWD_EARG;
    if (t->phase != SWD_IN) return SWD_EPHASE;
    if (t->node[id].state != SWD_RUN) return SWD_EARG;
    return SWD_OK;
}

swd_status_t swd_spend(swd_tree_t *t, uint32_t id, uint64_t tokens)
{
    swd_status_t r = running_in(t, id);
    if (r != SWD_OK) return r;
    swd_node_t *nd = &t->node[id];
    if (tokens > nd->reserved - nd->spent) return SWD_ESPEND;
    nd->spent += tokens;
    return SWD_OK;
}

swd_status_t swd_witness(swd_tree_t *t, uint32_t id, lpres_state_t v)
{
    swd_status_t r = running_in(t, id);
    if (r != SWD_OK) return r;
    if ((uint32_t) v > LPRES_STATE_BOTH) return SWD_EARG;
    t->node[id].witnessed = lpres_kjoin(t->node[id].witnessed, v);
    return SWD_OK;
}

swd_status_t swd_close(swd_tree_t *t, uint32_t id)
{
    swd_status_t r = running_in(t, id);
    if (r != SWD_OK) return r;
    swd_node_t *nd = &t->node[id];
    lpres_state_t v = nd->witnessed;
    if (nd->n_child || nd->n_lost) {
        lpres_state_t acc = nd->join == SWD_CONJ ? LPRES_STATE_TRUE : LPRES_STATE_FALSE;
        for (uint32_t k = 0; k < nd->n_child; k++) {
            const swd_node_t *ch = &t->node[nd->child[k]];
            if (ch->state == SWD_RUN) return SWD_EOPEN;
            lpres_state_t cv = ch->state == SWD_CLOSED ? ch->tv : LPRES_STATE_NEITHER;
            acc = nd->join == SWD_CONJ ? lpres_conjoin(acc, cv) : lpres_disjoin(acc, cv);
        }
        if (nd->n_lost)
            acc = nd->join == SWD_CONJ ? lpres_conjoin(acc, LPRES_STATE_NEITHER)
                                       : lpres_disjoin(acc, LPRES_STATE_NEITHER);
        v = lpres_kjoin(acc, nd->witnessed);
    }
    nd->tv = v;
    nd->state = SWD_CLOSED;
    return SWD_OK;
}

swd_status_t swd_seal(swd_tree_t *t)
{
    if (!t) return SWD_EARG;
    if (t->phase != SWD_IN) return SWD_EPHASE;
    uint64_t back = t->budget;
    for (uint32_t i = 0; i < t->n; i++) {
        if (t->node[i].state == SWD_RUN) return SWD_EOPEN;
        back += t->node[i].reserved - t->node[i].spent;
    }
    t->returned = back;
    t->budget = 0;
    t->root_tv = t->n && t->node[0].state == SWD_CLOSED ? t->node[0].tv : LPRES_STATE_NEITHER;
    t->phase = SWD_SEALED;
    return SWD_OK;
}

uint32_t swd_conflicts(const swd_tree_t *t, uint32_t *ids, uint32_t cap)
{
    uint32_t k = 0;
    for (uint32_t i = 0; t && i < t->n; i++)
        if (t->node[i].state == SWD_CLOSED && t->node[i].tv == LPRES_STATE_BOTH) {
            if (ids && k < cap) ids[k] = i;
            k++;
        }
    return k;
}

void swd_tally(const swd_tree_t *t, uint32_t cell, uint32_t counts[4])
{
    for (int s = 0; s < 4; s++) counts[s] = 0;
    for (uint32_t i = 0; t && i < t->n; i++)
        if (t->node[i].state == SWD_CLOSED && t->node[i].holder == cell)
            counts[t->node[i].tv & 3u]++;
}

bool swd_reliability(const uint32_t counts[4], uint32_t *permille)
{
    uint64_t checked =
        (uint64_t) counts[LPRES_STATE_TRUE] + counts[LPRES_STATE_FALSE] + counts[LPRES_STATE_BOTH];
    if (!checked) return false;
    uint64_t r = (uint64_t) counts[LPRES_STATE_TRUE] * 1000u;
    uint32_t q = 0; /* r / checked without a 64-bit division helper: checked < 2^34 */
    for (uint32_t step = 1000; step; step /= 2)
        while ((uint64_t) (q + step) * checked <= r) q += step;
    *permille = q;
    return true;
}
