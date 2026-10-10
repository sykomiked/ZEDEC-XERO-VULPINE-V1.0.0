/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* social_store.c — bounded record store and gossip. See social_store.h S1-S5.
 * Freestanding: no libc, no allocation, no floating point. */
#include "social_store.h"

/* ---------------- index ---------------- */

static uint32_t home_of(const ss_store_t *s, const uint8_t *id8)
{
    uint64_t x = 0;
    for (uint32_t i = 0; i < SS_SHORT_LEN; i++) x |= (uint64_t) id8[i] << (8 * i);
    x ^= s->salt;
    x *= 0x9E3779B97F4A7C15ull;
    x ^= x >> 29;
    return (uint32_t) (x >> 32) & (s->index_cap - 1u);
}

bool ss_init(ss_store_t *s, sp_record_t *slots, uint32_t cap, uint32_t *index, uint32_t index_cap,
             uint64_t salt)
{
    if (!s || !slots || !index || cap == 0 || index_cap < 2u * cap || index_cap > (1u << 30) ||
        (index_cap & (index_cap - 1u)))
        return false;
    s->slot = slots;
    s->cap = cap;
    s->index = index;
    s->index_cap = index_cap;
    s->head = 0;
    s->count = 0;
    s->salt = salt;
    s->evicted = 0;
    s->on_new = 0;
    s->on_new_ctx = 0;
    for (uint32_t i = 0; i < index_cap; i++) index[i] = 0;
    return true;
}

/* Index position of a short or full id; returns the empty position where it
 * would go if absent. */
static uint32_t probe(const ss_store_t *s, const uint8_t *id, uint32_t n, bool *found)
{
    uint32_t mask = s->index_cap - 1u, i = home_of(s, id);
    for (uint32_t step = 0; step < s->index_cap; step++, i = (i + 1u) & mask) {
        uint32_t e = s->index[i];
        if (e == 0) break;
        if (sp_eq(s->slot[e - 1u].id, id, n)) {
            *found = true;
            return i;
        }
    }
    *found = false;
    return i;
}

/* Backward-shift deletion keeps linear probing correct with no tombstones. */
static void index_remove(ss_store_t *s, uint32_t pos)
{
    uint32_t mask = s->index_cap - 1u, i = pos, j = pos;
    for (;;) {
        j = (j + 1u) & mask;
        uint32_t e = s->index[j];
        if (e == 0) break;
        uint32_t k = home_of(s, s->slot[e - 1u].id);
        /* Move e back to i unless its home lies cyclically in (i, j]. */
        bool stays = (i <= j) ? (k > i && k <= j) : (k > i || k <= j);
        if (!stays) {
            s->index[i] = e;
            i = j;
        }
    }
    s->index[i] = 0;
}

int32_t ss_put(ss_store_t *s, const sp_record_t *r)
{
    if (!s || !r) return SS_ERR_ARG;
    int32_t v = sp_validate(r);
    if (v != SP_OK) return v;
    bool found;
    probe(s, r->id, SP_ID_LEN, &found);
    if (found) return SS_DUP;
    uint32_t at = s->head;
    if (s->count == s->cap) { /* S1: evict the oldest */
        bool f2;
        uint32_t pos = probe(s, s->slot[at].id, SP_ID_LEN, &f2);
        if (f2) index_remove(s, pos);
        s->count--;
        s->evicted++;
    }
    sp_copy((uint8_t *) &s->slot[at], (const uint8_t *) r, (uint32_t) sizeof *r);
    bool f3;
    uint32_t pos = probe(s, r->id, SP_ID_LEN, &f3);
    s->index[pos] = at + 1u;
    s->head = (at + 1u) % s->cap;
    s->count++;
    if (s->on_new) s->on_new(s->on_new_ctx, &s->slot[at]);
    return SS_ADDED;
}

const sp_record_t *ss_get(const ss_store_t *s, const uint8_t id[SP_ID_LEN])
{
    if (!s || !id) return 0;
    bool found;
    uint32_t pos = probe(s, id, SP_ID_LEN, &found);
    return found ? &s->slot[s->index[pos] - 1u] : 0;
}

const sp_record_t *ss_get_short(const ss_store_t *s, const uint8_t sid[SS_SHORT_LEN])
{
    if (!s || !sid) return 0;
    bool found;
    uint32_t pos = probe(s, sid, SS_SHORT_LEN, &found);
    return found ? &s->slot[s->index[pos] - 1u] : 0;
}

uint32_t ss_count(const ss_store_t *s)
{
    return s ? s->count : 0;
}

const sp_record_t *ss_at(const ss_store_t *s, uint32_t i)
{
    if (!s || i >= s->count) return 0;
    uint32_t oldest = (s->head + s->cap - s->count) % s->cap;
    return &s->slot[(oldest + i) % s->cap];
}

/* ---------------- threads ---------------- */

static bool before(const sp_record_t *a, const sp_record_t *b)
{
    if (a->lamport != b->lamport) return a->lamport < b->lamport;
    return sp_cmp(a->id, b->id, SP_ID_LEN) < 0;
}

uint32_t ss_children(const ss_store_t *s, const uint8_t parent[SP_ID_LEN], const sp_record_t **out,
                     uint32_t max)
{
    if (!s || !parent || !out) return 0;
    uint32_t n = 0;
    for (uint32_t i = 0; i < s->count && n < max; i++) {
        const sp_record_t *r = ss_at(s, i);
        if (!r->has_parent || !sp_eq(r->parent, parent, SP_ID_LEN)) continue;
        uint32_t j = n++;
        while (j > 0 && before(r, out[j - 1])) {
            out[j] = out[j - 1];
            j--;
        }
        out[j] = r;
    }
    return n;
}

const sp_record_t *ss_root(const ss_store_t *s, const sp_record_t *r)
{
    if (!s || !r) return 0;
    /* At most count hops: a cycle would need a hash collision, but the bound
     * keeps even a corrupted store from looping. */
    for (uint32_t hops = 0; hops < s->count && r->has_parent; hops++) {
        const sp_record_t *p = ss_get(s, r->parent);
        if (!p) break;
        r = p;
    }
    return r;
}

/* ---------------- fingerprint ---------------- */

void ss_fingerprint(const ss_store_t *s, const uint8_t self[SP_NODEID_LEN],
                    const uint8_t peer[SP_NODEID_LEN], const sp_policy_t *p, ss_fingerprint_t *out)
{
    if (!out) return;
    out->count = 0;
    for (uint32_t i = 0; i < SP_ID_LEN; i++) out->xor_ids[i] = 0;
    if (!s) return;
    for (uint32_t i = 0; i < s->count; i++) {
        const sp_record_t *r = ss_at(s, i);
        if (!sp_may_share(r, self, peer, p)) continue;
        out->count++;
        for (uint32_t b = 0; b < SP_ID_LEN; b++) out->xor_ids[b] ^= r->id[b];
    }
}

/* ---------------- gossip ---------------- */

static bool gossip_ok(const ss_gossip_t *g)
{
    return g && g->store && g->self && g->peer && g->policy && g->verifier && g->send && g->tx &&
           g->tmp;
}

static int32_t send_ids(ss_gossip_t *g, uint8_t type, uint32_t n)
{
    g->tx[0] = type;
    g->tx[1] = (uint8_t) (n >> 8);
    g->tx[2] = (uint8_t) n;
    return g->send(g->io_ctx, g->tx, 3u + n * SS_SHORT_LEN) == 0 ? (int32_t) n : SS_ERR_SEND;
}

int32_t ss_gossip_have(ss_gossip_t *g, uint32_t *cursor)
{
    if (!gossip_ok(g) || !cursor) return SS_ERR_ARG;
    uint32_t n = 0, i = *cursor;
    for (; i < g->store->count && n < SS_HAVE_MAX; i++) {
        const sp_record_t *r = ss_at(g->store, i);
        if (!sp_may_share(r, g->self, g->peer, g->policy)) continue;
        sp_copy(g->tx + 3u + n * SS_SHORT_LEN, r->id, SS_SHORT_LEN);
        n++;
    }
    *cursor = i;
    if (n == 0) return 0;
    return send_ids(g, SS_MSG_HAVE, n);
}

/* Strict id-list framing: exactly 3 + 8n bytes, 1 <= n <= SS_HAVE_MAX. */
static int32_t id_list(const uint8_t *msg, uint32_t len, uint32_t *n)
{
    if (len < 3) return SS_ERR_FORMAT;
    uint32_t c = ((uint32_t) msg[1] << 8) | msg[2];
    if (c == 0 || c > SS_HAVE_MAX || len != 3u + c * SS_SHORT_LEN) return SS_ERR_FORMAT;
    *n = c;
    return 0;
}

int32_t ss_gossip_handle(ss_gossip_t *g, const uint8_t *msg, uint32_t len)
{
    if (!gossip_ok(g) || !msg) return SS_ERR_ARG;
    if (len == 0) {
        g->msgs_rejected++;
        return SS_ERR_FORMAT;
    }
    uint32_t n = 0;
    switch (msg[0]) {
    case SS_MSG_HAVE: {
        if (id_list(msg, len, &n) != 0) break;
        uint32_t want = 0;
        for (uint32_t i = 0; i < n; i++) {
            const uint8_t *sid = msg + 3u + i * SS_SHORT_LEN;
            if (ss_get_short(g->store, sid)) continue;
            sp_copy(g->tx + 3u + want * SS_SHORT_LEN, sid, SS_SHORT_LEN);
            want++;
        }
        if (want == 0) return 0;
        int32_t st = send_ids(g, SS_MSG_WANT, want);
        return st < 0 ? st : 0;
    }
    case SS_MSG_WANT: {
        if (id_list(msg, len, &n) != 0) break;
        for (uint32_t i = 0; i < n; i++) {
            const sp_record_t *r = ss_get_short(g->store, msg + 3u + i * SS_SHORT_LEN);
            /* A WANT is a request, not a right: consent is checked again. */
            if (!r || !sp_may_share(r, g->self, g->peer, g->policy)) continue;
            g->tx[0] = SS_MSG_REC;
            int32_t el = sp_encode(r, g->tx + 1, SS_MSG_MAX - 1u);
            if (el < 0) continue;
            if (g->send(g->io_ctx, g->tx, 1u + (uint32_t) el) != 0) return SS_ERR_SEND;
            g->recs_sent++;
        }
        return 0;
    }
    case SS_MSG_REC: {
        int32_t st = sp_decode(msg + 1, len - 1u, g->tmp, g->verifier);
        if (st != SP_OK) {
            g->recs_rejected++;
            return st;
        }
        if (g->clock) sp_clock_observe(g->clock, g->tmp->lamport);
        st = ss_put(g->store, g->tmp);
        if (st == SS_ADDED) g->recs_added++;
        if (st == SS_DUP) g->recs_dup++;
        return st;
    }
    default:
        break;
    }
    g->msgs_rejected++;
    return SS_ERR_FORMAT;
}

uint32_t ss_gossip_pump(ss_gossip_t *g, uint32_t max)
{
    if (!gossip_ok(g) || !g->recv || !g->rx) return 0;
    uint32_t handled = 0;
    while (handled < max) {
        int32_t n = g->recv(g->io_ctx, g->rx, SS_MSG_MAX);
        if (n <= 0) break;
        ss_gossip_handle(g, g->rx, (uint32_t) n);
        handled++;
    }
    return handled;
}
