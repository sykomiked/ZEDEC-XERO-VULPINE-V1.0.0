/* mirror_ui.c — the SASOS mirror projection (see mirror_ui.h).
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 *
 * Freestanding: no libc, no float, no dynamic allocation. Every bound
 * register is read through its DIRECT pointer at projection time; the frame
 * is a pure function of (registers, tick), so identical inputs produce
 * identical frames — the reproducibility the mirror-UI criterion demands.
 */
#include "mirror_ui.h"

/* ---- small helpers -------------------------------------------------------- */

/* freestanding helpers (no libc memset/memcpy/strlen in kernel images). */
static void mu_zero(void *dst, uint32_t n)
{
    uint8_t *d = (uint8_t *) dst;
    for (uint32_t i = 0; i < n; i++) d[i] = 0;
}

static uint32_t mu_strlen(const char *s)
{
    uint32_t n = 0u;
    while (s[n]) n++;
    return n;
}

static void mu_copy(void *dst, const void *src, uint32_t n)
{
    const uint8_t *s = (const uint8_t *) src;
    uint8_t *d = (uint8_t *) dst;
    for (uint32_t i = 0; i < n; i++) d[i] = s[i];
}

static uint32_t fold32(uint32_t a, uint32_t b)
{
    /* A 32-bit mixer: exact integer, no float, avalanche-friendly. */
    uint32_t x = a * 0x9E3779B1u;
    x ^= x >> 16;
    x += b * 0x85EBCA6Bu;
    x ^= x >> 13;
    x += 0xC2B2AE35u;
    x ^= x >> 16;
    return x;
}

MIRROR_KEEP void mirror_ui_init(mirror_ui_t *m)
{
    if (!m) return;
    mu_zero(m, (uint32_t) sizeof *m);
}

MIRROR_KEEP int mirror_bind(mirror_ui_t *m, mirror_kind_t kind, const char *name, const void *ptr,
                uint32_t seed)
{
    uint32_t i;
    if (!m || !name || kind >= MIRROR_KIND_COUNT) return -1;
    if (m->reg_count >= MIRROR_MAX_REGS) return -1;
    if (mu_strlen(name) >= MIRROR_NAME_LEN) return -1;
    i = m->reg_count;
    m->regs[i].kind = kind;
    mu_copy(m->regs[i].name, name, mu_strlen(name) + 1u);
    m->regs[i].ptr = ptr;
    m->regs[i].seed = seed ? seed : 1u;
    m->reg_count++;
    return (int) i;
}

MIRROR_KEEP bool mirror_notify(mirror_ui_t *m, uint32_t idx)
{
    uint32_t next;
    if (!m || idx >= m->reg_count) return false;
    next = (m->ring_head + 1u) % MIRROR_RING_CAP;
    if (next == m->ring_tail) { /* full: backpressure, not loss    */
        m->ring_full = true;
        return false;
    }
    m->ring[m->ring_head] = idx;
    m->ring_head = next;
    return true;
}

/* The four-valued lpres state maps to a visual valence. NEITHER and BOTH are
 * NOT approximated to a binary — they render as ambiguity, per the mirror
 * criterion. The chain is the kernel's own lpres_rank, never re-derived. */
static mirror_valence_t lpres_valence(lpres_state_t s)
{
    switch (s) {
    case LPRES_STATE_TRUE:
        return MIRROR_VALENCE_CERTAIN;
    case LPRES_STATE_FALSE:
        return MIRROR_VALENCE_ABSENT;
    case LPRES_STATE_BOTH:
        return MIRROR_VALENCE_BOTH;
    default:
        return MIRROR_VALENCE_NEITHER;
    }
}

/* One register's projected valence, computed from its kind's view. */
static mirror_valence_t reg_valence(const mirror_reg_t *r)
{
    const mirror_ring_view_t *ring;
    const mirror_queue_view_t *que;
    const mirror_trust_view_t *trust;
    const mirror_tris_view_t *tris;
    const mirror_lpres_view_t *lp;
    const mirror_net_view_t *net;
    const mirror_cid_view_t *cid;
    const mirror_pq_view_t *pq;
    const mirror_clock_view_t *clk;

    if (!r->ptr) return MIRROR_VALENCE_NEITHER; /* unbound: fail closed   */

    switch (r->kind) {
    case MIRROR_KIND_RING:
        ring = (const mirror_ring_view_t *) r->ptr;
        if (ring->capacity == 0u) return MIRROR_VALENCE_NEITHER;
        /* a ring whose head==tail is EMPTY: that is a CERTAIN empty, not
         * an unknown. A full ring (tail - head == capacity) is BOTH: it is
         * saturated and cannot take another slot. */
        if (ring->head == ring->tail) return MIRROR_VALENCE_CERTAIN;
        {
            uint32_t used = (ring->tail >= ring->head) ? ring->tail - ring->head
                                                       : ring->capacity - (ring->head - ring->tail);
            if (used >= ring->capacity) return MIRROR_VALENCE_BOTH;
        }
        return MIRROR_VALENCE_CERTAIN;

    case MIRROR_KIND_QUEUE:
        que = (const mirror_queue_view_t *) r->ptr;
        if (que->depth > que->high_water) return MIRROR_VALENCE_BOTH;
        return MIRROR_VALENCE_CERTAIN;

    case MIRROR_KIND_TRUST:
        trust = (const mirror_trust_view_t *) r->ptr;
        if (trust->sealed == 1u && trust->verified == 1u) return MIRROR_VALENCE_CERTAIN;
        if (trust->sealed == 0u && trust->verified == 0u) return MIRROR_VALENCE_ABSENT;
        /* sealed-but-unverified or open-but-verified is a contradictory
         * state: render it as ambiguity, never as a binary. */
        return MIRROR_VALENCE_BOTH;

    case MIRROR_KIND_TRIS:
        tris = (const mirror_tris_view_t *) r->ptr;
        if (tris->depth > MIRROR_PROJ_DEPTH) return MIRROR_VALENCE_BOTH;
        switch (tris->role) {
        case TRI_POSITIVE:
            return MIRROR_VALENCE_CERTAIN; /* S+ protrudes  */
        case TRI_NEGATIVE:
            return MIRROR_VALENCE_ABSENT; /* S- recedes    */
        default:
            return MIRROR_VALENCE_NEITHER; /* S0 screen     */
        }

    case MIRROR_KIND_LPRES:
        lp = (const mirror_lpres_view_t *) r->ptr;
        return lpres_valence(lp->state);

    case MIRROR_KIND_NET:
        /* network topology: a known, non-empty topology that is fully up is
         * CERTAIN; a partially-up ring is BOTH (split-brain risk rendered as
         * ambiguity); zero peers with a non-zero total is ABSENT; an unknown
         * total is NEITHER. */
        net = (const mirror_net_view_t *) r->ptr;
        if (net->peers_total == 0u) return MIRROR_VALENCE_NEITHER;
        if (net->peers_up == 0u) return MIRROR_VALENCE_ABSENT;
        if (net->peers_up >= net->peers_total) return MIRROR_VALENCE_CERTAIN;
        return MIRROR_VALENCE_BOTH;

    case MIRROR_KIND_CID:
        /* content-addressed identity: a CID verifies ITSELF. verified==1 is
         * CERTAIN regardless of pinning; verified==0 with a pinned flag is a
         * contradictory claim (pinned but unverified) -> BOTH. */
        cid = (const mirror_cid_view_t *) r->ptr;
        if (cid->verified == 1u) return MIRROR_VALENCE_CERTAIN;
        if (cid->pinned == 1u) return MIRROR_VALENCE_BOTH;
        return MIRROR_VALENCE_ABSENT;

    case MIRROR_KIND_PQ:
        /* post-quantum session: both decapsulation AND authentication must
         * hold. A failure is IMPLICIT REJECTION — rendered ABSENT (silent),
         * never an error shade that could oracle the cluster. */
        pq = (const mirror_pq_view_t *) r->ptr;
        if (pq->decapsulated == 1u && pq->authenticated == 1u) return MIRROR_VALENCE_CERTAIN;
        if (pq->decapsulated == 0u && pq->authenticated == 0u) return MIRROR_VALENCE_ABSENT;
        return MIRROR_VALENCE_BOTH; /* half-proven: ambiguous, fail closed */

    case MIRROR_KIND_CLOCK:
        /* the global temporal manifold: local ordinal vs the highest peer
         * ordinal. A peer ahead beyond tolerance is BOTH (drift, rendered as
         * ambiguity — the kernel refuses to guess which clock is right). */
        clk = (const mirror_clock_view_t *) r->ptr;
        if (clk->peer_max_tick == 0u) return MIRROR_VALENCE_NEITHER;
        {
            uint32_t ahead = (clk->peer_max_tick >= clk->local_tick)
                                 ? clk->peer_max_tick - clk->local_tick
                                 : clk->local_tick - clk->peer_max_tick;
            if (ahead > clk->skew_tolerance) return MIRROR_VALENCE_BOTH;
        }
        return MIRROR_VALENCE_CERTAIN;

    default:
        return MIRROR_VALENCE_NEITHER;
    }
}

MIRROR_KEEP mirror_valence_t mirror_valence_of(const mirror_ui_t *m, uint32_t idx)
{
    if (!m || idx >= m->reg_count) return MIRROR_VALENCE_NEITHER;
    return reg_valence(&m->regs[idx]);
}

/* The exact integer value a register exposes to the frame. */
static uint32_t reg_value(const mirror_reg_t *r)
{
    const mirror_ring_view_t *ring;
    const mirror_queue_view_t *que;
    const mirror_trust_view_t *trust;
    const mirror_tris_view_t *tris;
    const mirror_lpres_view_t *lp;
    const mirror_net_view_t *net;
    const mirror_cid_view_t *cid;
    const mirror_pq_view_t *pq;
    const mirror_clock_view_t *clk;

    if (!r->ptr) return 0u;
    switch (r->kind) {
    case MIRROR_KIND_RING:
        ring = (const mirror_ring_view_t *) r->ptr;
        return (ring->tail >= ring->head) ? ring->tail - ring->head
                                          : ring->capacity - (ring->head - ring->tail);
    case MIRROR_KIND_QUEUE:
        que = (const mirror_queue_view_t *) r->ptr;
        return que->depth;
    case MIRROR_KIND_TRUST:
        trust = (const mirror_trust_view_t *) r->ptr;
        return (trust->sealed << 1u) | trust->verified;
    case MIRROR_KIND_TRIS:
        tris = (const mirror_tris_view_t *) r->ptr;
        return ((uint32_t) tris->role << 16u) | tris->depth;
    case MIRROR_KIND_LPRES:
        lp = (const mirror_lpres_view_t *) r->ptr;
        return (uint32_t) lp->state;
    case MIRROR_KIND_NET:
        net = (const mirror_net_view_t *) r->ptr;
        return (net->peers_up << 16u) | net->peers_total;
    case MIRROR_KIND_CID:
        cid = (const mirror_cid_view_t *) r->ptr;
        return (cid->verified << 1u) | cid->pinned;
    case MIRROR_KIND_PQ:
        pq = (const mirror_pq_view_t *) r->ptr;
        return (pq->decapsulated << 1u) | pq->authenticated;
    case MIRROR_KIND_CLOCK:
        clk = (const mirror_clock_view_t *) r->ptr;
        return (clk->local_tick << 1u) | (clk->peer_max_tick != 0u);
    default:
        return 0u;
    }
}

static uint32_t reg_depth(const mirror_reg_t *r)
{
    const mirror_tris_view_t *tris;
    if (!r->ptr || r->kind != MIRROR_KIND_TRIS) return 0u;
    tris = (const mirror_tris_view_t *) r->ptr;
    return tris->depth;
}

MIRROR_KEEP void mirror_project(const mirror_ui_t *m, uint64_t tick, mirror_frame_t *out)
{
    uint32_t i, digest = 0u;

    if (!m || !out) return;
    mu_zero(out, (uint32_t) sizeof *out);
    out->tick = tick;
    out->ring_full = m->ring_full;

    for (i = 0; i < m->reg_count && i < MIRROR_MAX_REGS; i++) {
        mirror_proj_t *p = &out->proj[i];
        const mirror_reg_t *r = &m->regs[i];
        p->kind = r->kind;
        mu_copy(p->name, r->name, MIRROR_NAME_LEN);
        p->valence = reg_valence(r);
        p->value = reg_value(r);
        p->depth = reg_depth(r);
        /* the deterministic fold: register seed + value + valence + tick
         * low bits, so the frame is a pure function of its inputs. */
        digest = fold32(digest, fold32(r->seed, p->value));
        digest = fold32(digest, (uint32_t) p->valence);
        digest = fold32(digest, (uint32_t) (tick & 0xFFFFFFFFu));
    }
    out->proj_count = m->reg_count;
    out->digest = digest;
}

MIRROR_KEEP const char *mirror_valence_name(mirror_valence_t v)
{
    static const char *const n[MIRROR_VALENCE_COUNT] = {
        "certain",
        "absent",
        "neither",
        "both",
    };
    if (v >= MIRROR_VALENCE_COUNT) return "?";
    return n[v];
}

MIRROR_KEEP const char *mirror_kind_name(mirror_kind_t k)
{
    static const char *const n[MIRROR_KIND_COUNT] = {
        "ring", "queue", "trust", "tris", "lpres", "net", "cid", "pq", "clock",
    };
    if (k >= MIRROR_KIND_COUNT) return "?";
    return n[k];
}