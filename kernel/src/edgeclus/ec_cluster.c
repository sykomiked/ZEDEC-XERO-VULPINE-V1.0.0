/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* ec_cluster.c — the advert table (A2, A3), the per-cycle budget over
 * swarm_budget (A4-A6), the scheduler (A7) and failover (A8, A9). See
 * edgeclus.h. */
#include "edgeclus.h"

#define NO_LEVEL 0xFFu

/* -1, 0, +1: lexicographic order of two 32-byte values */
static int cmp32(const uint8_t *a, const uint8_t *b)
{
    for (uint32_t i = 0; i < 32; i++) {
        if (a[i] < b[i]) return -1;
        if (a[i] > b[i]) return 1;
    }
    return 0;
}

ec_status_t ec_cluster_init(ec_cluster_t *c, uint32_t pow_bits, uint64_t skew_ms,
                            uint32_t cap_permille)
{
    if (!c || pow_bits > 32 || cap_permille == 0 || cap_permille > 1000) return EC_ERR_ARG;
    vna_zero(c, (uint32_t) sizeof *c);
    c->pow_bits = pow_bits;
    c->skew_ms = skew_ms;
    c->cap_permille = cap_permille;
    return EC_OK;
}

static bool live(const ec_advert_t *a, uint64_t now)
{
    /* issued_ms + ttl_ms cannot wrap a sane clock; saturate anyway */
    uint64_t end = vna_sat_add(a->issued_ms, a->ttl_ms);
    return now < end;
}

bool ec_node_eligible(const ec_node_t *n, uint64_t now_ms)
{
    return n && !n->failed && (n->ad.flags & EC_F_DRAINING) == 0 && live(&n->ad, now_ms);
}

static void remove_at(ec_cluster_t *c, uint32_t i)
{
    for (uint32_t k = i; k + 1 < c->n; k++)
        vna_copy(&c->nodes[k], &c->nodes[k + 1], (uint32_t) sizeof c->nodes[k]);
    c->n--;
    vna_zero(&c->nodes[c->n], (uint32_t) sizeof c->nodes[c->n]);
    c->gen++;
}

uint32_t ec_cluster_expire(ec_cluster_t *c, uint64_t now_ms)
{
    if (!c) return 0;
    uint32_t dropped = 0, i = 0;
    while (i < c->n) {
        if (!live(&c->nodes[i].ad, now_ms)) {
            remove_at(c, i);
            dropped++;
        } else {
            i++;
        }
    }
    return dropped;
}

int32_t ec_cluster_find(const ec_cluster_t *c, const vna_id_t *node)
{
    if (!c || !node) return -1;
    for (uint32_t i = 0; i < c->n; i++)
        if (vna_id_eq(&c->nodes[i].ad.node, node)) return (int32_t) i;
    return -1;
}

static void set_entry(ec_node_t *e, const ec_advert_t *a, const uint8_t h[32])
{
    vna_copy(&e->ad, a, (uint32_t) sizeof *a);
    vna_copy(e->body_hash, h, 32);
}

ec_status_t ec_cluster_ingest(ec_cluster_t *c, const uint8_t *buf, uint32_t len, uint64_t now_ms)
{
    if (!c || !buf) return EC_ERR_ARG;
    ec_cluster_expire(c, now_ms);
    ec_advert_t a;
    uint8_t h[32];
    ec_status_t st = ec_advert_open(buf, len, c->pow_bits, &a, h);
    if (st != EC_OK) return st;
    if (a.issued_ms > vna_sat_add(now_ms, c->skew_ms)) return EC_ERR_FUTURE;
    if (!live(&a, now_ms)) return EC_ERR_EXPIRED;

    int32_t at = ec_cluster_find(c, &a.node);
    if (at >= 0) {
        ec_node_t *e = &c->nodes[at];
        if (a.seq < e->ad.seq) return EC_ERR_STALE;
        if (a.seq == e->ad.seq) {
            int d = cmp32(h, e->body_hash);
            if (d == 0) return EC_OK; /* the same bytes again */
            c->equivocations++;
            if (d > 0) return EC_ERR_EQUIVOCATION; /* A3: the lower hash stays */
        }
        set_entry(e, &a, h);
        if (e->failed && a.issued_ms > e->failed_at) {
            e->failed = false;
            e->failed_at = 0;
        }
        c->gen++;
        return EC_OK;
    }

    if (c->n == EC_MAX_NODES) {
        /* A3: keep the EC_MAX_NODES lowest ids */
        if (cmp32(a.node.b, c->nodes[c->n - 1].ad.node.b) >= 0) return EC_ERR_FULL;
        remove_at(c, c->n - 1);
    }
    uint32_t pos = 0;
    while (pos < c->n && cmp32(c->nodes[pos].ad.node.b, a.node.b) < 0) pos++;
    for (uint32_t k = c->n; k > pos; k--)
        vna_copy(&c->nodes[k], &c->nodes[k - 1], (uint32_t) sizeof c->nodes[k]);
    vna_zero(&c->nodes[pos], (uint32_t) sizeof c->nodes[pos]);
    set_entry(&c->nodes[pos], &a, h);
    c->n++;
    c->gen++;
    return EC_OK;
}

/* ---------------- budget (A4-A6) ---------------- */

static uint32_t best_decode(const ec_advert_t *a)
{
    uint32_t b = 0;
    for (uint32_t i = 0; i < a->n_models; i++)
        if (a->models[i].decode_mtps > b) b = a->models[i].decode_mtps;
    return b;
}

/* A5: clip everything over `cap`, give the cut-off tokens to the eligible
 * nodes under it in proportion to what they hold (or to their room when
 * they hold nothing), repeat. Returns what could not be placed. An excess
 * after the first pass can only come from a node newly pushed over the cap
 * (capped nodes get nothing more), so each further pass caps at least one
 * more node and the loop ends within n + 2 passes. */
static uint64_t water_fill(uint64_t *alloc, const bool *elig, uint32_t n, uint64_t cap,
                           uint64_t excess)
{
    uint64_t w[EC_MAX_NODES], add[EC_MAX_NODES];
    for (;;) {
        for (uint32_t i = 0; i < n; i++) {
            if (elig[i] && alloc[i] > cap) {
                excess += alloc[i] - cap;
                alloc[i] = cap;
            }
        }
        if (excess == 0) return 0;
        uint64_t sumw = 0, room = 0;
        for (uint32_t i = 0; i < n; i++) {
            w[i] = (elig[i] && alloc[i] < cap) ? alloc[i] : 0;
            sumw += w[i];
            if (elig[i] && alloc[i] < cap) room += cap - alloc[i];
        }
        if (room == 0) return excess; /* every eligible node is at the cap */
        if (sumw == 0 || excess >= room) {
            /* by room: never overshoots when excess <= room */
            for (uint32_t i = 0; i < n; i++) w[i] = (elig[i] && alloc[i] < cap) ? cap - alloc[i] : 0;
            uint64_t give = excess < room ? excess : room;
            swarm_split_lr(give, w, n, add);
            for (uint32_t i = 0; i < n; i++) alloc[i] += add[i];
            return excess - give;
        }
        swarm_split_lr(excess, w, n, add);
        for (uint32_t i = 0; i < n; i++) alloc[i] += add[i];
        excess = 0;
    }
}

ec_status_t ec_cycle_begin(ec_cluster_t *c, uint64_t now_ms, uint64_t total, ec_cycle_t *cy)
{
    if (!c || !cy || total > SWARM_MAX_TOKENS_PER_CYCLE) return EC_ERR_ARG;
    vna_zero(cy, (uint32_t) sizeof *cy);
    cy->now_ms = now_ms;
    cy->gen = c->gen;
    cy->total = total;

    bool elig[EC_MAX_NODES];
    uint32_t order[EC_MAX_NODES], ne = 0;
    for (uint32_t i = 0; i < c->n; i++) {
        elig[i] = ec_node_eligible(&c->nodes[i], now_ms);
        cy->level[i] = NO_LEVEL;
        if (!elig[i]) continue;
        /* insertion by (best decode desc, index asc = id asc) */
        uint32_t bd = best_decode(&c->nodes[i].ad), k = ne;
        while (k > 0 && best_decode(&c->nodes[order[k - 1]].ad) < bd) {
            order[k] = order[k - 1];
            k--;
        }
        order[k] = i;
        ne++;
    }
    if (ne == 0) {
        cy->unassigned = total;
        return EC_OK;
    }
    /* A4: rank r -> level d, level d holding F(d+2) nodes */
    uint32_t d = 0, used_in_level = 0;
    for (uint32_t r = 0; r < ne; r++) {
        if (used_in_level == swarm_level_capacity(d)) {
            d++;
            used_in_level = 0;
        }
        cy->level[order[r]] = (uint8_t) d;
        used_in_level++;
    }
    if (swarm_budget_init(&c->sb, d + 1, total) != SWARM_OK) return EC_ERR_ARG;
    for (uint32_t i = 0; i < c->n; i++)
        if (elig[i] && swarm_budget_register(&c->sb, i, cy->level[i]) != SWARM_OK)
            return EC_ERR_ARG;
    if (swarm_budget_begin_cycle(&c->sb) != SWARM_OK) return EC_ERR_ARG;
    for (uint32_t s = 0; s < c->sb.num_slots; s++)
        cy->alloc[c->sb.slots[s].model_id] = c->sb.slots[s].allotted;
    (void) swarm_budget_end_cycle(&c->sb);

    uint64_t cap = swarm_muldiv(total, c->cap_permille, 1000u, (uint64_t *) 0);
    cy->unassigned = water_fill(cy->alloc, elig, c->n, cap, 0);
    return EC_OK;
}

/* ---------------- scheduling (A7) ---------------- */

static uint64_t ceil_ms(uint64_t tokens, uint32_t mtps)
{
    uint64_t rem = 0;
    uint64_t q = swarm_muldiv(tokens, 1000000u, mtps, &rem);
    return q + (rem ? 1u : 0u);
}

static bool pick(const ec_cluster_t *c, const ec_cycle_t *cy, const ec_req_t *r, ec_assign_t *o)
{
    uint8_t why = EC_WHY_NO_MODEL;
    int32_t best = -1;
    uint8_t best_m = 0;
    uint64_t best_est = 0;
    uint64_t need = (uint64_t) r->prompt_tokens + r->max_new_tokens;
    for (uint32_t i = 0; i < c->n; i++) {
        const ec_node_t *e = &c->nodes[i];
        if (cy->level[i] == NO_LEVEL || !ec_node_eligible(e, cy->now_ms)) continue;
        int32_t m = -1;
        for (uint32_t k = 0; k < e->ad.n_models; k++) {
            const ec_model_t *md = &e->ad.models[k];
            if (!vna_eq(md->name, r->model, EC_NAME_LEN)) continue;
            if (r->quant != EC_Q_ANY && md->quant != r->quant) continue;
            if (m < 0 || md->decode_mtps > e->ad.models[m].decode_mtps) m = (int32_t) k;
        }
        if (m < 0) continue;
        if (why < EC_WHY_NO_MEMORY) why = EC_WHY_NO_MEMORY;
        if (cy->mem_used[i] > e->ad.free_mem_mb || e->ad.free_mem_mb - cy->mem_used[i] < r->kv_mem_mb)
            continue;
        if (why < EC_WHY_NO_BUDGET) why = EC_WHY_NO_BUDGET;
        if (cy->alloc[i] - cy->used[i] < need) continue;
        if (why < EC_WHY_LATENCY) why = EC_WHY_LATENCY;
        const ec_model_t *md = &e->ad.models[m];
        uint64_t svc = ceil_ms(r->prompt_tokens, md->prefill_mtps) +
                       ceil_ms(r->max_new_tokens, md->decode_mtps);
        uint64_t depth = (uint64_t) e->ad.queue_depth + cy->queued[i] + 1u;
        uint64_t est = swarm_muldiv(svc, depth, 1u, (uint64_t *) 0);
        if (r->latency_ms != 0 && est > r->latency_ms) continue;
        if (best < 0 || est < best_est ||
            (est == best_est && e->ad.price < c->nodes[best].ad.price)) {
            best = (int32_t) i;
            best_m = (uint8_t) m;
            best_est = est;
        }
    }
    o->req_id = r->req_id;
    if (best < 0) {
        o->node = -1;
        vna_zero(o->node_id.b, VNA_ID_LEN);
        o->model_idx = 0;
        o->why = why;
        o->est_ms = 0;
        return false;
    }
    o->node = best;
    vna_copy(o->node_id.b, c->nodes[best].ad.node.b, VNA_ID_LEN);
    o->model_idx = best_m;
    o->why = EC_WHY_OK;
    o->est_ms = best_est > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t) best_est;
    return true;
}

static void commit(ec_cycle_t *cy, const ec_req_t *r, int32_t node)
{
    cy->used[node] += (uint64_t) r->prompt_tokens + r->max_new_tokens;
    cy->mem_used[node] += r->kv_mem_mb;
    cy->queued[node]++;
}

/* order[] = indices of reqs by increasing req_id; false on a duplicate */
static bool by_req_id(const ec_req_t *reqs, uint32_t n, uint16_t *order)
{
    for (uint32_t i = 0; i < n; i++) {
        uint32_t k = i;
        while (k > 0 && reqs[order[k - 1]].req_id > reqs[i].req_id) {
            order[k] = order[k - 1];
            k--;
        }
        if (k > 0 && reqs[order[k - 1]].req_id == reqs[i].req_id) return false;
        order[k] = (uint16_t) i;
    }
    return true;
}

ec_status_t ec_schedule(const ec_cluster_t *c, ec_cycle_t *cy, const ec_req_t *reqs, uint32_t n,
                        ec_assign_t *out)
{
    if (!c || !cy || (n && (!reqs || !out)) || n > EC_MAX_REQS) return EC_ERR_ARG;
    if (cy->gen != c->gen) return EC_ERR_CYCLE;
    uint16_t order[EC_MAX_REQS];
    if (!by_req_id(reqs, n, order)) return EC_ERR_ARG;
    for (uint32_t j = 0; j < n; j++) {
        uint32_t i = order[j];
        if (pick(c, cy, &reqs[i], &out[i])) commit(cy, &reqs[i], out[i].node);
    }
    return EC_OK;
}

/* ---------------- failover (A8) ---------------- */

ec_status_t ec_failover(ec_cluster_t *c, ec_cycle_t *cy, const vna_id_t *node, const ec_req_t *reqs,
                        uint32_t n, ec_assign_t *asg, uint32_t *moved)
{
    if (moved) *moved = 0;
    if (!c || !cy || !node || (n && (!reqs || !asg)) || n > EC_MAX_REQS) return EC_ERR_ARG;
    if (cy->gen != c->gen) return EC_ERR_CYCLE;
    int32_t f = ec_cluster_find(c, node);
    if (f < 0) return EC_ERR_UNKNOWN;
    uint16_t order[EC_MAX_REQS];
    if (!by_req_id(reqs, n, order)) return EC_ERR_ARG;

    c->nodes[f].failed = true;
    c->nodes[f].failed_at = cy->now_ms;
    c->gen++;
    cy->gen = c->gen;

    /* its allotment goes back to the others (A5), so A6 still holds */
    uint64_t back = cy->alloc[f];
    cy->alloc[f] = 0;
    cy->used[f] = 0;
    cy->mem_used[f] = 0;
    cy->queued[f] = 0;
    cy->level[f] = NO_LEVEL;
    bool elig[EC_MAX_NODES];
    for (uint32_t i = 0; i < c->n; i++) elig[i] = cy->level[i] != NO_LEVEL;
    uint64_t cap = swarm_muldiv(cy->total, c->cap_permille, 1000u, (uint64_t *) 0);
    /* nodes keep what they hold; only `back` is placed */
    cy->unassigned += water_fill(cy->alloc, elig, c->n, cap, back);

    uint32_t mv = 0;
    for (uint32_t j = 0; j < n; j++) {
        uint32_t i = order[j];
        if (asg[i].node != f || asg[i].req_id != reqs[i].req_id) continue;
        mv++;
        if (pick(c, cy, &reqs[i], &asg[i])) commit(cy, &reqs[i], asg[i].node);
    }
    if (moved) *moved = mv;
    return EC_OK;
}
