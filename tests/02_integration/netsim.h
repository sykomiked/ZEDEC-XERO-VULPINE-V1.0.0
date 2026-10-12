/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* netsim.h — a seeded, deterministic datagram network for the integration
 * tests. Hosted only (tests/02_integration), never in the kernel image.
 *
 * Each ns_send() either loses the datagram or schedules it for delivery at
 *   now + base + U[0, jitter]                       (every datagram)
 *       + U[1, reorder_extra]   with reorder_ppm    (overtaken by later ones)
 * and, with dup_ppm, schedules a second copy with its own delay. A link that
 * is partitioned (ns_partition) loses everything sent on it until the
 * partition heals, in both directions. ns_poll() hands out due datagrams in
 * (delivery time, send order) order, so a run is a pure function of the seed
 * and the profile.
 *
 * Every datagram carries simulation-only metadata the receiver's code under
 * test never sees: a unique id, the id of the original for a duplicate copy,
 * and a session generation label the sender attaches. Tests use it as ground
 * truth (for example: a record sealed under an old session must never open).
 *
 * Counters: sent, dropped (loss + partition + queue full), duplicated, and
 * reordered (a datagram delivered after a later-sent one on the same
 * direction). Capacities are fixed; a full queue counts as a drop.
 */
#ifndef ZXV_NETSIM_H
#define ZXV_NETSIM_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define NS_NODES   3u
#define NS_MAX_PKT 512u
#define NS_MTU     12288u

typedef struct {
    uint32_t drop_ppm;    /* loss per datagram, parts per million        */
    uint32_t dup_ppm;     /* a second copy                               */
    uint32_t reorder_ppm; /* an extra delay that lets later ones pass    */
    uint32_t base_ms;
    uint32_t jitter_ms;
    uint32_t reorder_extra_ms;
} ns_profile_t;

typedef struct {
    bool used;
    uint8_t src, dst;
    uint32_t gen;  /* sender's label (session generation), ground truth only */
    uint64_t id;   /* unique per datagram copy                               */
    uint64_t orig; /* id of the original send (== id unless a duplicate)     */
    uint64_t at;   /* delivery time, ms                                      */
    uint32_t len;
    uint8_t b[NS_MTU];
} ns_pkt_t;

typedef struct {
    uint64_t rng;
    ns_profile_t prof;
    ns_pkt_t q[NS_MAX_PKT];
    uint64_t next_id;
    uint64_t down_until[NS_NODES][NS_NODES];
    uint64_t last_orig[NS_NODES][NS_NODES]; /* highest original id delivered */
    uint64_t sent, dropped, duplicated, reordered, delivered, partitioned;
} ns_t;

/* splitmix64: the only randomness in a run */
static inline uint64_t ns_rand(uint64_t *s)
{
    uint64_t z = (*s += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

static inline uint32_t ns_below(uint64_t *s, uint32_t n)
{
    return n ? (uint32_t) (ns_rand(s) % n) : 0;
}

static inline bool ns_chance(uint64_t *s, uint32_t ppm)
{
    return ns_below(s, 1000000u) < ppm;
}

static inline void ns_init(ns_t *n, uint64_t seed, const ns_profile_t *p)
{
    memset(n, 0, sizeof *n);
    n->rng = seed ^ 0x6E657473696D3031ull; /* "netsim01" */
    n->prof = *p;
    n->next_id = 1;
}

/* Partitions overlap: the link heals when the last one ends. */
static inline void ns_partition(ns_t *n, uint8_t a, uint8_t b, uint64_t until)
{
    if (until > n->down_until[a][b]) n->down_until[a][b] = n->down_until[b][a] = until;
}

static inline bool ns_link_up(const ns_t *n, uint8_t a, uint8_t b, uint64_t now)
{
    return now >= n->down_until[a][b];
}

static inline bool ns_enqueue(ns_t *n, uint8_t src, uint8_t dst, uint32_t gen, uint64_t orig,
                              const uint8_t *b, uint32_t len, uint64_t at)
{
    for (uint32_t i = 0; i < NS_MAX_PKT; i++) {
        ns_pkt_t *p = &n->q[i];
        if (p->used) continue;
        p->used = true;
        p->src = src;
        p->dst = dst;
        p->gen = gen;
        p->id = n->next_id++;
        p->orig = orig ? orig : p->id;
        p->at = at;
        p->len = len;
        memcpy(p->b, b, len);
        return true;
    }
    return false;
}

static inline uint64_t ns_delay(ns_t *n)
{
    uint64_t d = n->prof.base_ms + ns_below(&n->rng, n->prof.jitter_ms + 1);
    if (ns_chance(&n->rng, n->prof.reorder_ppm))
        d += 1 + ns_below(&n->rng, n->prof.reorder_extra_ms);
    return d;
}

/* Send one datagram. Returns false when it was lost (any reason). */
static inline bool ns_send(ns_t *n, uint8_t src, uint8_t dst, uint32_t gen, const uint8_t *b,
                           uint32_t len, uint64_t now)
{
    n->sent++;
    if (src >= NS_NODES || dst >= NS_NODES || src == dst || len > NS_MTU) {
        n->dropped++;
        return false;
    }
    if (!ns_link_up(n, src, dst, now)) {
        n->dropped++;
        n->partitioned++;
        return false;
    }
    if (ns_chance(&n->rng, n->prof.drop_ppm)) {
        n->dropped++;
        return false;
    }
    uint64_t orig = n->next_id;
    if (!ns_enqueue(n, src, dst, gen, 0, b, len, now + ns_delay(n))) {
        n->dropped++;
        return false;
    }
    if (ns_chance(&n->rng, n->prof.dup_ppm) &&
        ns_enqueue(n, src, dst, gen, orig, b, len, now + ns_delay(n)))
        n->duplicated++;
    return true;
}

/* The next datagram due at or before `now`, earliest first, ties by send
 * order. The caller reads it and then calls ns_done. */
static inline ns_pkt_t *ns_poll(ns_t *n, uint64_t now)
{
    ns_pkt_t *best = 0;
    for (uint32_t i = 0; i < NS_MAX_PKT; i++) {
        ns_pkt_t *p = &n->q[i];
        if (!p->used || p->at > now) continue;
        if (!best || p->at < best->at || (p->at == best->at && p->id < best->id)) best = p;
    }
    if (best) {
        n->delivered++;
        uint64_t *last = &n->last_orig[best->src][best->dst];
        if (best->orig < *last)
            n->reordered++;
        else
            *last = best->orig;
    }
    return best;
}

static inline void ns_done(ns_pkt_t *p)
{
    p->used = false;
}

static inline uint32_t ns_in_flight(const ns_t *n)
{
    uint32_t k = 0;
    for (uint32_t i = 0; i < NS_MAX_PKT; i++) k += n->q[i].used;
    return k;
}

#endif /* ZXV_NETSIM_H */
