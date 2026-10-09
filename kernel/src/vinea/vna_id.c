/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* vna_id.c — identity, PoW, key cache, replay window. See vna_id.h. */
#include "vna_id.h"

static uint32_t cstrlen(const char *s)
{
    uint32_t n = 0;
    while (s[n]) n++;
    return n;
}

void vna_sign(const uint8_t sk[VNA_SK_LEN], const char *ctx, const uint8_t *msg, uint32_t len,
              const uint8_t rnd[32], uint8_t sig[VNA_SIG_LEN])
{
    pq_mldsa65_sign(sk, msg, len, (const uint8_t *) ctx, cstrlen(ctx), rnd, sig);
}

bool vna_verify(const uint8_t pk[VNA_PK_LEN], const char *ctx, const uint8_t *msg, uint32_t len,
                const uint8_t sig[VNA_SIG_LEN])
{
    return pq_mldsa65_verify(pk, msg, len, (const uint8_t *) ctx, cstrlen(ctx), sig);
}

void vna_node_id(const uint8_t pk[VNA_PK_LEN], vna_id_t *out)
{
    vna_sha3(pk, VNA_PK_LEN, out->b);
}

bool vna_pow_ok(const vna_id_t *id, uint64_t nonce, uint32_t bits)
{
    static const char tag[] = "vinea/v2/pow";
    uint8_t buf[sizeof tag - 1 + VNA_ID_LEN + 8];
    uint8_t h[32];
    if (bits == 0) return true;
    if (bits > 64) return false;
    vna_copy(buf, tag, sizeof tag - 1);
    vna_copy(buf + sizeof tag - 1, id->b, VNA_ID_LEN);
    vna_put64(buf + sizeof tag - 1 + VNA_ID_LEN, nonce);
    vna_sha3(buf, sizeof buf, h);
    return vna_leading_zero_bits(h) >= bits;
}

vna_status_t vna_identity_create(vna_identity_t *idn, const uint8_t seed[32], uint32_t pow_bits,
                                 uint64_t max_tries)
{
    if (!idn || !seed || pow_bits > 32) return VNA_ERR_ARG;
    pq_mldsa65_keygen(seed, idn->pk, idn->sk);
    vna_node_id(idn->pk, &idn->id);
    idn->pow_bits = pow_bits;
    for (uint64_t n = 0; n < max_tries; n++) {
        if (vna_pow_ok(&idn->id, n, pow_bits)) {
            idn->pow_nonce = n;
            return VNA_OK;
        }
    }
    return VNA_ERR_CAP;
}

/* ---- key cache ---- */
void vna_keycache_init(vna_keycache_t *kc, vna_keycache_ent_t *storage, uint32_t cap)
{
    kc->e = storage;
    kc->cap = cap;
    kc->tick = 0;
    kc->hits = kc->misses = 0;
    for (uint32_t i = 0; i < cap; i++) storage[i].used = false;
}

static vna_keycache_ent_t *kc_find(vna_keycache_t *kc, const vna_id_t *id)
{
    for (uint32_t i = 0; i < kc->cap; i++)
        if (kc->e[i].used && vna_id_eq(&kc->e[i].id, id)) return &kc->e[i];
    return 0;
}

vna_status_t vna_keycache_check(vna_keycache_t *kc, const vna_id_t *id,
                                const uint8_t pk[VNA_PK_LEN], uint64_t pow_nonce, uint32_t pow_bits)
{
    vna_keycache_ent_t *e = kc ? kc_find(kc, id) : 0;
    if (e && e->pow_nonce == pow_nonce && e->pow_bits >= pow_bits &&
        vna_eq(e->pk, pk, VNA_PK_LEN)) {
        e->last_used = ++kc->tick;
        kc->hits++;
        return VNA_OK; /* exact: these bytes already hashed to this id */
    }
    vna_id_t h;
    vna_node_id(pk, &h);
    if (!vna_id_eq(&h, id)) return VNA_ERR_BINDING;
    if (!vna_pow_ok(id, pow_nonce, pow_bits)) return VNA_ERR_POW;
    if (!kc || kc->cap == 0) return VNA_OK;
    kc->misses++;
    if (!e) { /* take a free slot, else the least recently used */
        e = &kc->e[0];
        for (uint32_t i = 0; i < kc->cap; i++) {
            if (!kc->e[i].used) {
                e = &kc->e[i];
                break;
            }
            if (kc->e[i].last_used < e->last_used) e = &kc->e[i];
        }
    }
    e->id = *id;
    vna_copy(e->pk, pk, VNA_PK_LEN);
    e->pow_nonce = pow_nonce;
    e->pow_bits = pow_bits;
    e->last_used = ++kc->tick;
    e->used = true;
    return VNA_OK;
}

const uint8_t *vna_keycache_get(vna_keycache_t *kc, const vna_id_t *id)
{
    vna_keycache_ent_t *e = kc_find(kc, id);
    return e ? e->pk : 0;
}

/* ---- replay ---- */
void vna_replay_init(vna_replay_t *r, vna_replay_ent_t *storage, uint32_t cap, uint64_t window_ms)
{
    r->e = storage;
    r->cap = cap;
    r->tick = 0;
    for (uint32_t i = 0; i < VNA_REPLAY_FLOORS; i++) r->floor_ms[i] = 0;
    r->window_ms = window_ms;
    for (uint32_t i = 0; i < cap; i++) storage[i].used = false;
}

static uint32_t floor_slot(const vna_id_t *peer) /* NodeIDs are hashes: any byte is uniform */
{
    return (uint32_t) (peer->b[0] ^ peer->b[31]) & (VNA_REPLAY_FLOORS - 1u);
}

static const vna_replay_ent_t *rp_find(const vna_replay_t *r, const vna_id_t *peer)
{
    for (uint32_t i = 0; i < r->cap; i++)
        if (r->e[i].used && vna_id_eq(&r->e[i].peer, peer)) return &r->e[i];
    return 0;
}

vna_status_t vna_replay_check(const vna_replay_t *r, const vna_id_t *peer, uint64_t seq,
                              uint64_t ts, uint64_t now)
{
    if (vna_absdiff(ts, now) > r->window_ms) return VNA_ERR_STALE;
    if (seq == 0) return VNA_ERR_REPLAY; /* sequences start at 1 */
    const vna_replay_ent_t *e = rp_find(r, peer);
    if (!e) return ts > r->floor_ms[floor_slot(peer)] ? VNA_OK : VNA_ERR_REPLAY;
    if (seq > e->hi) return VNA_OK;
    if (seq == e->hi) return VNA_ERR_REPLAY;
    uint64_t back = e->hi - seq; /* >= 1 */
    if (back > 64) return VNA_ERR_REPLAY;
    return (e->bitmap >> (back - 1)) & 1u ? VNA_ERR_REPLAY : VNA_OK;
}

void vna_replay_commit(vna_replay_t *r, const vna_id_t *peer, uint64_t seq, uint64_t ts)
{
    vna_replay_ent_t *e = (vna_replay_ent_t *) rp_find(r, peer);
    if (!e) {
        e = &r->e[0];
        for (uint32_t i = 0; i < r->cap; i++) {
            if (!r->e[i].used) {
                e = &r->e[i];
                break;
            }
            if (r->e[i].last_used < e->last_used) e = &r->e[i];
        }
        if (e->used) { /* R3: nothing the evicted peer sent can come back */
            uint32_t f = floor_slot(&e->peer);
            if (e->ts_hi > r->floor_ms[f]) r->floor_ms[f] = e->ts_hi;
        }
        e->peer = *peer;
        e->hi = seq;
        e->bitmap = 0;
        e->ts_hi = ts;
        e->used = true;
        e->last_used = ++r->tick;
        return;
    }
    e->last_used = ++r->tick;
    if (ts > e->ts_hi) e->ts_hi = ts;
    if (seq > e->hi) {
        uint64_t shift = seq - e->hi;
        if (shift < 64)
            e->bitmap = (e->bitmap << shift) | ((uint64_t) 1 << (shift - 1));
        else if (shift == 64)
            e->bitmap = (uint64_t) 1 << 63;
        else
            e->bitmap = 0;
        e->hi = seq;
    } else if (seq < e->hi) {
        uint64_t back = e->hi - seq;
        if (back <= 64) e->bitmap |= (uint64_t) 1 << (back - 1);
    }
}
