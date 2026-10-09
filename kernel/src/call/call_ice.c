/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* call_ice.c — candidates, STUN-like binding checks, ICE-style agent and DHT
 * rendezvous message formats. */
#include "call_ice.h"
#include "call_common.h"
#include "call_rtp.h"

#define AT_USERNAME        0x0006
#define AT_INTEGRITY       0x0008
#define AT_ERROR           0x0009
#define AT_XOR_MAPPED      0x0020
#define AT_PRIORITY        0x0024
#define AT_USE_CANDIDATE   0x0025
#define AT_ICE_CONTROLLED  0x8029
#define AT_ICE_CONTROLLING 0x802A
#define ADDR_WIRE          19

bool call_addr_eq(const call_addr_t *a, const call_addr_t *b)
{
    if (a->family != b->family || a->port != b->port) return false;
    return call_cmp(a->ip, b->ip, a->family == 4 ? 4 : 16) == 0;
}

static uint32_t xs32(uint32_t *s)
{
    uint32_t x = *s ? *s : 0x9e3779b9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return x;
}

static void rand_txid(uint32_t *seed, uint8_t *t)
{
    for (int i = 0; i < 12; i += 4) call_put32(t + i, xs32(seed));
}

/* ---- candidates ---- */

uint32_t call_cand_priority(call_cand_type_t type, uint16_t local_pref, uint8_t component)
{
    uint32_t tp = 0;
    switch (type) {
    case CALL_CAND_HOST:
        tp = 126;
        break;
    case CALL_CAND_PRFLX:
        tp = 110;
        break;
    case CALL_CAND_SRFLX:
        tp = 100;
        break;
    default:
        tp = 0;
        break;
    }
    if (component == 0) component = 1;
    return (tp << 24) | ((uint32_t) local_pref << 8) | (256u - component);
}

void call_cand_make(call_cand_t *c, call_cand_type_t type, const call_addr_t *addr,
                    const call_addr_t *base, uint16_t local_pref, uint8_t base_idx)
{
    call_fill(c, 0, sizeof(*c));
    c->addr = *addr;
    c->base = base ? *base : *addr;
    c->type = (uint8_t) type;
    c->component = 1;
    c->priority = call_cand_priority(type, local_pref, 1);
    c->base_idx = base_idx;
    /* foundation: type + base address */
    uint32_t f = 2166136261u ^ type;
    for (int i = 0; i < 16; i++) f = (f ^ c->base.ip[i]) * 16777619u;
    c->foundation = call_mix32(f ^ c->base.family);
}

static void addr_write(const call_addr_t *a, uint8_t *o)
{
    o[0] = a->family;
    call_copy(o + 1, a->ip, 16);
    call_put16(o + 17, a->port);
}

static bool addr_parse(const uint8_t *in, call_addr_t *a)
{
    call_fill(a, 0, sizeof(*a));
    a->family = in[0];
    if (a->family != 4 && a->family != 6) return false;
    call_copy(a->ip, in + 1, 16);
    if (a->family == 4)
        for (int i = 4; i < 16; i++)
            if (a->ip[i]) return false;
    a->port = call_get16(in + 17);
    return true;
}

int call_cand_write(const call_cand_t *c, uint8_t *out, uint32_t cap)
{
    if (!c || !out) return CALL_ERR_ARG;
    if (cap < CALL_CAND_WIRE) return CALL_ERR_SPACE;
    out[0] = c->type;
    out[1] = c->component;
    call_put32(out + 2, c->priority);
    call_put32(out + 6, c->foundation);
    addr_write(&c->addr, out + 10);
    return CALL_CAND_WIRE;
}

int call_cand_parse(const uint8_t *in, uint32_t len, call_cand_t *c)
{
    if (!in || !c) return CALL_ERR_ARG;
    if (len < CALL_CAND_WIRE) return CALL_ERR_SHORT;
    call_fill(c, 0, sizeof(*c));
    c->type = in[0];
    c->component = in[1];
    if (c->type > CALL_CAND_RELAY || c->component == 0) return CALL_ERR_FORMAT;
    c->priority = call_get32(in + 2);
    c->foundation = call_get32(in + 6);
    if (!addr_parse(in + 10, &c->addr)) return CALL_ERR_FORMAT;
    c->base = c->addr;
    return CALL_CAND_WIRE;
}

/* ---- binding messages ---- */

static uint8_t *put_attr(uint8_t *p, uint16_t type, uint16_t len)
{
    call_put16(p, type);
    call_put16(p + 2, len);
    return p + 4;
}

int call_stun_write(const call_stun_t *m, const uint8_t *key, uint32_t klen, call_mac_fn mac,
                    void *mctx, uint8_t *out, uint32_t cap)
{
    if (!m || !out) return CALL_ERR_ARG;
    if (m->type != CALL_STUN_REQ && m->type != CALL_STUN_RESP && m->type != CALL_STUN_ERR)
        return CALL_ERR_ARG;
    uint32_t need = CALL_STUN_HDR_LEN;
    if (m->has_username) need += 20;
    if (m->has_priority) need += 8;
    if (m->use_candidate) need += 4;
    if (m->has_controlling || m->has_controlled) need += 12;
    if (m->has_mapped) need += 4 + (m->mapped.family == 4 ? 8 : 20);
    if (m->has_error) need += 8;
    if (mac) need += 20;
    if (cap < need) return CALL_ERR_SPACE;
    call_fill(out, 0, need);
    call_put16(out, m->type);
    call_put16(out + 2, (uint16_t) (need - CALL_STUN_HDR_LEN));
    call_put32(out + 4, CALL_STUN_MAGIC);
    call_copy(out + 8, m->txid, 12);
    uint8_t *p = out + CALL_STUN_HDR_LEN;
    if (m->has_username) {
        p = put_attr(p, AT_USERNAME, 16);
        call_copy(p, m->username, 16);
        p += 16;
    }
    if (m->has_priority) {
        p = put_attr(p, AT_PRIORITY, 4);
        call_put32(p, m->priority);
        p += 4;
    }
    if (m->use_candidate) p = put_attr(p, AT_USE_CANDIDATE, 0);
    if (m->has_controlling || m->has_controlled) {
        p = put_attr(p, m->has_controlling ? AT_ICE_CONTROLLING : AT_ICE_CONTROLLED, 8);
        call_put32(p, (uint32_t) (m->tie >> 32));
        call_put32(p + 4, (uint32_t) m->tie);
        p += 8;
    }
    if (m->has_mapped) {
        uint32_t alen = m->mapped.family == 4 ? 8 : 20;
        p = put_attr(p, AT_XOR_MAPPED, (uint16_t) alen);
        if (m->mapped.family != 4 && m->mapped.family != 6) return CALL_ERR_ARG;
        p[1] = m->mapped.family == 4 ? 1 : 2;
        call_put16(p + 2, (uint16_t) (m->mapped.port ^ (CALL_STUN_MAGIC >> 16)));
        for (uint32_t i = 0; i < alen - 4; i++) p[4 + i] = m->mapped.ip[i] ^ out[4 + i];
        p += alen;
    }
    if (m->has_error) {
        p = put_attr(p, AT_ERROR, 4);
        p[2] = (uint8_t) (m->error / 100);
        p[3] = (uint8_t) (m->error % 100);
        p += 4;
    }
    if (mac) {
        uint32_t off = (uint32_t) (p - out);
        p = put_attr(p, AT_INTEGRITY, 16);
        if (!mac(mctx, key, klen, out, off, p)) return CALL_ERR_AUTH;
        p += 16;
    }
    return (int) (p - out);
}

int call_stun_parse(const uint8_t *in, uint32_t len, call_stun_t *m)
{
    if (!in || !m) return CALL_ERR_ARG;
    if (len < CALL_STUN_HDR_LEN) return CALL_ERR_SHORT;
    call_fill(m, 0, sizeof(*m));
    m->type = call_get16(in);
    if (m->type != CALL_STUN_REQ && m->type != CALL_STUN_RESP && m->type != CALL_STUN_ERR)
        return CALL_ERR_FORMAT;
    uint32_t blen = call_get16(in + 2);
    if (blen % 4 || blen + CALL_STUN_HDR_LEN != len) return CALL_ERR_FORMAT;
    if (call_get32(in + 4) != CALL_STUN_MAGIC) return CALL_ERR_FORMAT;
    call_copy(m->txid, in + 8, 12);
    uint32_t o = CALL_STUN_HDR_LEN;
    uint32_t seen = 0;
    while (o < len) {
        if (m->has_integrity) return CALL_ERR_FORMAT; /* integrity must be last */
        if (o + 4 > len) return CALL_ERR_SHORT;
        uint16_t t = call_get16(in + o), al = call_get16(in + o + 2);
        uint32_t padded = ((uint32_t) al + 3u) & ~3u;
        if (o + 4 + padded > len) return CALL_ERR_SHORT;
        const uint8_t *v = in + o + 4;
        uint32_t bit = 0;
        switch (t) {
        case AT_USERNAME:
            if (al != 16) return CALL_ERR_FORMAT;
            m->has_username = 1;
            call_copy(m->username, v, 16);
            bit = 1;
            break;
        case AT_PRIORITY:
            if (al != 4) return CALL_ERR_FORMAT;
            m->has_priority = 1;
            m->priority = call_get32(v);
            bit = 2;
            break;
        case AT_USE_CANDIDATE:
            if (al != 0) return CALL_ERR_FORMAT;
            m->use_candidate = 1;
            bit = 4;
            break;
        case AT_ICE_CONTROLLED:
        case AT_ICE_CONTROLLING:
            if (al != 8) return CALL_ERR_FORMAT;
            if (t == AT_ICE_CONTROLLING)
                m->has_controlling = 1;
            else
                m->has_controlled = 1;
            m->tie = ((uint64_t) call_get32(v) << 32) | call_get32(v + 4);
            bit = 8;
            break;
        case AT_XOR_MAPPED: {
            if (al != 8 && al != 20) return CALL_ERR_FORMAT;
            uint8_t fam = v[1];
            if (v[0] != 0 || (fam == 1 && al != 8) || (fam == 2 && al != 20) ||
                (fam != 1 && fam != 2))
                return CALL_ERR_FORMAT;
            m->has_mapped = 1;
            m->mapped.family = fam == 1 ? 4 : 6;
            m->mapped.port = (uint16_t) (call_get16(v + 2) ^ (CALL_STUN_MAGIC >> 16));
            for (uint32_t i = 0; i < (uint32_t) al - 4; i++) m->mapped.ip[i] = v[4 + i] ^ in[4 + i];
            bit = 16;
            break;
        }
        case AT_ERROR:
            if (al != 4) return CALL_ERR_FORMAT;
            if (v[2] < 3 || v[2] > 6 || v[3] > 99) return CALL_ERR_FORMAT;
            m->has_error = 1;
            m->error = (uint16_t) (v[2] * 100u + v[3]);
            bit = 32;
            break;
        case AT_INTEGRITY:
            if (al != 16) return CALL_ERR_FORMAT;
            m->has_integrity = 1;
            m->integrity_off = o;
            call_copy(m->integrity, v, 16);
            bit = 64;
            break;
        default:
            if (t < 0x8000) return CALL_ERR_FORMAT; /* comprehension-required */
            break;
        }
        if (bit) {
            if (seen & bit) return CALL_ERR_FORMAT;
            seen |= bit;
        }
        for (uint32_t i = al; i < padded; i++)
            if (v[i]) return CALL_ERR_FORMAT;
        o += 4 + padded;
    }
    if (m->has_controlling && m->has_controlled) return CALL_ERR_FORMAT;
    if (m->type == CALL_STUN_ERR && !m->has_error) return CALL_ERR_FORMAT;
    return (int) len;
}

bool call_stun_verify(const call_stun_t *m, const uint8_t *in, uint32_t len, const uint8_t *key,
                      uint32_t klen, call_mac_fn mac, void *mctx)
{
    if (!mac) return true;
    if (!m->has_integrity || m->integrity_off + 20 != len) return false;
    uint8_t want[16];
    if (!mac(mctx, key, klen, in, m->integrity_off, want)) return false;
    uint8_t d = 0;
    for (int i = 0; i < 16; i++) d |= (uint8_t) (want[i] ^ m->integrity[i]);
    return d == 0;
}

/* ---- gathering ---- */

void call_gather_init(call_gather_t *g, call_cand_t *store, uint32_t cap, uint32_t seed,
                      call_ice_send_fn send, void *sctx)
{
    call_fill(g, 0, sizeof(*g));
    g->c = store;
    g->cap = cap > CALL_ICE_MAX_CANDS ? CALL_ICE_MAX_CANDS : cap;
    g->seed = seed;
    g->send = send;
    g->sctx = sctx;
}

int call_gather_add_host(call_gather_t *g, const call_addr_t *addr, uint16_t local_pref)
{
    if (!g || !addr || (addr->family != 4 && addr->family != 6)) return CALL_ERR_ARG;
    if (g->n >= g->cap || g->n != g->nhost) return CALL_ERR_FULL;
    call_cand_make(&g->c[g->n], CALL_CAND_HOST, addr, addr, local_pref, (uint8_t) g->n);
    g->n++;
    g->nhost++;
    return (int) (g->n - 1);
}

static void gather_send_all(call_gather_t *g)
{
    for (uint32_t i = 0; i < g->nhost; i++)
        for (uint32_t j = 0; j < g->nrefl; j++) {
            uint32_t k = i * 4 + j;
            if (!g->pending[k]) continue;
            if (g->refl[j].family != g->c[i].addr.family) {
                g->pending[k] = 0;
                continue;
            }
            call_stun_t m;
            call_fill(&m, 0, sizeof(m));
            m.type = CALL_STUN_REQ;
            call_copy(m.txid, g->txid[k], 12);
            uint8_t buf[CALL_STUN_MAX];
            int w = call_stun_write(&m, NULL, 0, NULL, NULL, buf, sizeof(buf));
            if (w > 0 && g->send) g->send(g->sctx, i, &g->refl[j], buf, (uint32_t) w);
        }
}

int call_gather_start(call_gather_t *g, const call_addr_t *reflectors, uint32_t n, uint32_t now)
{
    if (!g || (n && !reflectors)) return CALL_ERR_ARG;
    if (n > 4) n = 4;
    g->nrefl = n;
    for (uint32_t j = 0; j < n; j++) g->refl[j] = reflectors[j];
    for (uint32_t i = 0; i < g->nhost; i++)
        for (uint32_t j = 0; j < n; j++) {
            rand_txid(&g->seed, g->txid[i * 4 + j]);
            g->pending[i * 4 + j] = 1;
        }
    g->tries = 1;
    g->next_ms = now + 200;
    g->done = (n == 0 || g->nhost == 0);
    gather_send_all(g);
    return CALL_OK;
}

int call_gather_on_packet(call_gather_t *g, uint32_t local_idx, const call_addr_t *from,
                          const uint8_t *msg, uint32_t len)
{
    call_stun_t m;
    if (!g || !from || local_idx >= g->nhost) return 0;
    if (call_stun_parse(msg, len, &m) < 0 || m.type != CALL_STUN_RESP || !m.has_mapped) return 0;
    for (uint32_t j = 0; j < g->nrefl; j++) {
        uint32_t k = local_idx * 4 + j;
        if (!g->pending[k] || call_cmp(g->txid[k], m.txid, 12) || !call_addr_eq(from, &g->refl[j]))
            continue;
        g->pending[k] = 0;
        bool dup = false;
        for (uint32_t i = 0; i < g->n; i++)
            if (call_addr_eq(&g->c[i].addr, &m.mapped)) dup = true;
        bool left = false;
        for (uint32_t q = 0; q < 4 * CALL_ICE_MAX_CANDS; q++) left = left || g->pending[q];
        if (!left) g->done = 1;
        if (dup || g->n >= g->cap) return 0;
        call_cand_make(&g->c[g->n], CALL_CAND_SRFLX, &m.mapped, &g->c[local_idx].addr,
                       (uint16_t) (65535u - local_idx), (uint8_t) local_idx);
        g->n++;
        return 1;
    }
    return 0;
}

void call_gather_tick(call_gather_t *g, uint32_t now)
{
    if (g->done || !call_time_ge(now, g->next_ms)) return;
    if (g->tries >= 3) {
        g->done = 1;
        return;
    }
    g->tries++;
    g->next_ms = now + 200u * g->tries;
    gather_send_all(g);
}

/* ---- connectivity-check agent ---- */

void call_ice_init(call_ice_t *a, bool controlling, uint64_t tie, const uint8_t *ufrag,
                   const uint8_t *pwd, uint32_t seed, call_ice_send_fn send, void *sctx,
                   call_mac_fn mac, void *mctx)
{
    call_fill(a, 0, sizeof(*a));
    a->controlling = controlling ? 1 : 0;
    a->tie = tie;
    call_copy(a->lufrag, ufrag, CALL_ICE_UFRAG_LEN);
    call_copy(a->lpwd, pwd, CALL_ICE_PWD_LEN);
    a->seed = seed;
    a->send = send;
    a->sctx = sctx;
    a->mac = mac;
    a->mctx = mctx;
    a->selected = -1;
}

int call_ice_add_local(call_ice_t *a, const call_cand_t *c)
{
    if (!a || !c) return CALL_ERR_ARG;
    if (a->nlocal >= CALL_ICE_MAX_CANDS) return CALL_ERR_FULL;
    CALL_SET(a->local[a->nlocal], *c);
    a->nlocal++;
    return (int) (a->nlocal - 1);
}

int call_ice_set_remote(call_ice_t *a, const uint8_t *ufrag, const uint8_t *pwd,
                        const call_cand_t *c, uint32_t n)
{
    if (!a || !ufrag || !pwd || (n && !c)) return CALL_ERR_ARG;
    if (n > CALL_ICE_MAX_CANDS) n = CALL_ICE_MAX_CANDS;
    call_copy(a->rufrag, ufrag, CALL_ICE_UFRAG_LEN);
    call_copy(a->rpwd, pwd, CALL_ICE_PWD_LEN);
    for (uint32_t i = 0; i < n; i++) CALL_SET(a->remote[i], c[i]);
    a->nremote = n;
    return CALL_OK;
}

static uint64_t pair_prio(const call_ice_t *a, const call_pair_t *p)
{
    uint32_t lp = a->local[p->l].priority, rp = a->remote[p->r].priority;
    uint32_t g = a->controlling ? lp : rp, d = a->controlling ? rp : lp;
    uint32_t mn = g < d ? g : d, mx = g < d ? d : g;
    return ((uint64_t) mn << 32) + 2u * (uint64_t) mx + (g > d ? 1u : 0u);
}

static void sort_pairs(call_ice_t *a)
{
    for (uint32_t i = 0; i < a->npairs; i++) a->pairs[i].prio = pair_prio(a, &a->pairs[i]);
    for (uint32_t i = 1; i < a->npairs; i++) {
        call_pair_t t;
        CALL_SET(t, a->pairs[i]);
        uint32_t j = i;
        while (j > 0 && a->pairs[j - 1].prio < t.prio) {
            CALL_SET(a->pairs[j], a->pairs[j - 1]);
            j--;
        }
        CALL_SET(a->pairs[j], t);
    }
    /* selection index may have moved */
    a->selected = -1;
    for (uint32_t i = 0; i < a->npairs; i++)
        if (a->state == CALL_ICE_COMPLETED && a->pairs[i].state == CALL_PAIR_SUCCEEDED &&
            (a->controlling || a->pairs[i].nominated)) {
            a->selected = (int32_t) i;
            break;
        }
}

static int32_t add_pair(call_ice_t *a, uint32_t l, uint32_t r)
{
    for (uint32_t i = 0; i < a->npairs; i++)
        if (a->pairs[i].l == l && a->pairs[i].r == r) return (int32_t) i;
    if (a->npairs >= CALL_ICE_MAX_PAIRS) return -1;
    call_pair_t *p = &a->pairs[a->npairs];
    call_fill(p, 0, sizeof(*p));
    p->l = (uint8_t) l;
    p->r = (uint8_t) r;
    p->state = CALL_PAIR_WAITING;
    p->rto_ms = CALL_ICE_RTO_MS;
    p->prio = pair_prio(a, p);
    return (int32_t) a->npairs++;
}

int call_ice_start(call_ice_t *a, uint32_t now)
{
    if (!a) return CALL_ERR_ARG;
    a->npairs = 0;
    for (uint32_t l = 0; l < a->nlocal; l++) {
        if (a->local[l].type != CALL_CAND_HOST) continue; /* checks leave from the base */
        for (uint32_t r = 0; r < a->nremote; r++)
            if (a->local[l].addr.family == a->remote[r].addr.family) add_pair(a, l, r);
    }
    sort_pairs(a);
    a->state = a->npairs ? CALL_ICE_RUNNING : CALL_ICE_FAILED;
    a->next_tick = now;
    return a->npairs ? CALL_OK : CALL_ERR_STATE;
}

static void send_check(call_ice_t *a, call_pair_t *p, uint32_t now)
{
    call_stun_t m;
    call_fill(&m, 0, sizeof(m));
    m.type = CALL_STUN_REQ;
    if (p->tries == 0 || p->state != CALL_PAIR_IN_PROGRESS) rand_txid(&a->seed, p->txid);
    call_copy(m.txid, p->txid, 12);
    m.has_username = 1;
    call_copy(m.username, a->rufrag, 8);
    call_copy(m.username + 8, a->lufrag, 8);
    m.has_priority = 1;
    m.priority = call_cand_priority(CALL_CAND_PRFLX, (uint16_t) (a->local[p->l].priority >> 8), 1);
    if (a->controlling) {
        m.has_controlling = 1;
        m.use_candidate = 1;
    } else {
        m.has_controlled = 1;
    }
    m.tie = a->tie;
    uint8_t buf[CALL_STUN_MAX];
    int w = call_stun_write(&m, a->rpwd, CALL_ICE_PWD_LEN, a->mac, a->mctx, buf, sizeof(buf));
    if (w > 0 && a->send) a->send(a->sctx, p->l, &a->remote[p->r].addr, buf, (uint32_t) w);
    a->checks_sent++;
    p->state = CALL_PAIR_IN_PROGRESS;
    p->tries++;
    p->deadline = now + p->rto_ms;
    p->triggered = 0;
}

static void select_pair(call_ice_t *a, int32_t i)
{
    if (a->selected >= 0) return;
    a->selected = i;
    a->state = CALL_ICE_COMPLETED;
}

void call_ice_tick(call_ice_t *a, uint32_t now)
{
    if (!a || a->state != CALL_ICE_RUNNING) return;
    for (uint32_t i = 0; i < a->npairs; i++) {
        call_pair_t *p = &a->pairs[i];
        if (p->state != CALL_PAIR_IN_PROGRESS || !call_time_ge(now, p->deadline)) continue;
        if (p->tries >= CALL_ICE_MAX_TRIES) {
            p->state = CALL_PAIR_FAILED;
            continue;
        }
        p->rto_ms = p->rto_ms * 2 > CALL_ICE_RTO_MAX_MS ? CALL_ICE_RTO_MAX_MS : p->rto_ms * 2;
        send_check(a, p, now);
    }
    if (call_time_ge(now, a->next_tick)) {
        int32_t pick = -1;
        for (uint32_t i = 0; i < a->npairs && pick < 0; i++)
            if (a->pairs[i].state == CALL_PAIR_WAITING && a->pairs[i].triggered) pick = (int32_t) i;
        for (uint32_t i = 0; i < a->npairs && pick < 0; i++)
            if (a->pairs[i].state == CALL_PAIR_WAITING) pick = (int32_t) i;
        if (pick >= 0) {
            a->pairs[pick].tries = 0;
            a->pairs[pick].rto_ms = CALL_ICE_RTO_MS;
            send_check(a, &a->pairs[pick], now);
            a->next_tick = now + CALL_ICE_TA_MS;
        }
    }
    bool all_failed = a->npairs > 0;
    for (uint32_t i = 0; i < a->npairs; i++)
        if (a->pairs[i].state != CALL_PAIR_FAILED) all_failed = false;
    if (all_failed) a->state = CALL_ICE_FAILED;
}

static void respond(call_ice_t *a, uint32_t local_idx, const call_addr_t *to, const uint8_t *txid,
                    uint16_t error)
{
    call_stun_t r;
    call_fill(&r, 0, sizeof(r));
    call_copy(r.txid, txid, 12);
    if (error) {
        r.type = CALL_STUN_ERR;
        r.has_error = 1;
        r.error = error;
    } else {
        r.type = CALL_STUN_RESP;
        r.has_mapped = 1;
        r.mapped = *to;
    }
    uint8_t buf[CALL_STUN_MAX];
    int w = call_stun_write(&r, a->lpwd, CALL_ICE_PWD_LEN, a->mac, a->mctx, buf, sizeof(buf));
    if (w > 0 && a->send) a->send(a->sctx, local_idx, to, buf, (uint32_t) w);
}

static void switch_role(call_ice_t *a)
{
    a->controlling = !a->controlling;
    a->role_switches++;
    sort_pairs(a);
}

int call_ice_on_packet(call_ice_t *a, uint32_t local_idx, const call_addr_t *from,
                       const uint8_t *msg, uint32_t len, uint32_t now)
{
    (void) now;
    call_stun_t m;
    if (!a || !from || !msg || local_idx >= a->nlocal) return 0;
    if (call_stun_parse(msg, len, &m) < 0) return 0;
    if (m.type == CALL_STUN_REQ) {
        if (!m.has_username || call_cmp(m.username, a->lufrag, 8) ||
            call_cmp(m.username + 8, a->rufrag, 8))
            return 0;
        if (!call_stun_verify(&m, msg, len, a->lpwd, CALL_ICE_PWD_LEN, a->mac, a->mctx)) {
            a->bad_integrity++;
            return 1;
        }
        if (!m.has_priority || (!m.has_controlling && !m.has_controlled)) return 1;
        /* role conflict, RFC 8445 7.3.1.1 */
        if (a->controlling && m.has_controlling) {
            if (a->tie >= m.tie) {
                respond(a, local_idx, from, m.txid, 487);
                return 1;
            }
            switch_role(a);
        } else if (!a->controlling && m.has_controlled) {
            if (a->tie >= m.tie) {
                switch_role(a);
            } else {
                respond(a, local_idx, from, m.txid, 487);
                return 1;
            }
        }
        respond(a, local_idx, from, m.txid, 0);
        if (a->state != CALL_ICE_RUNNING && a->state != CALL_ICE_COMPLETED) return 1;
        int32_t r = -1;
        for (uint32_t i = 0; i < a->nremote; i++)
            if (call_addr_eq(&a->remote[i].addr, from)) r = (int32_t) i;
        if (r < 0 && a->nremote < CALL_ICE_MAX_CANDS) {
            call_cand_t *c = &a->remote[a->nremote];
            call_cand_make(c, CALL_CAND_PRFLX, from, from, 0, 0);
            c->priority = m.priority;
            r = (int32_t) a->nremote++;
        }
        if (r < 0) return 1;
        int32_t pi = add_pair(a, local_idx, (uint32_t) r);
        if (pi < 0) return 1;
        call_pair_t *p = &a->pairs[pi];
        if (m.use_candidate && !a->controlling) p->nominated = 1;
        if (p->state == CALL_PAIR_SUCCEEDED) {
            if (!a->controlling && p->nominated) select_pair(a, pi);
        } else if (p->state != CALL_PAIR_IN_PROGRESS) {
            p->state = CALL_PAIR_WAITING;
            p->triggered = 1;
            p->tries = 0;
        }
        return 1;
    }
    /* response or error: match the transaction */
    for (uint32_t i = 0; i < a->npairs; i++) {
        call_pair_t *p = &a->pairs[i];
        if (p->state != CALL_PAIR_IN_PROGRESS || call_cmp(p->txid, m.txid, 12)) continue;
        if (!call_stun_verify(&m, msg, len, a->rpwd, CALL_ICE_PWD_LEN, a->mac, a->mctx)) {
            a->bad_integrity++;
            return 1;
        }
        if (!call_addr_eq(from, &a->remote[p->r].addr) || p->l != local_idx) {
            p->state = CALL_PAIR_FAILED; /* not symmetric */
            return 1;
        }
        if (m.type == CALL_STUN_ERR) {
            if (m.error == 487) {
                p->state = CALL_PAIR_WAITING;
                p->triggered = 1;
                switch_role(a); /* re-sorts: p is stale after this */
            } else {
                p->state = CALL_PAIR_FAILED;
            }
            return 1;
        }
        p->state = CALL_PAIR_SUCCEEDED;
        if (a->controlling || p->nominated) select_pair(a, (int32_t) i);
        return 1;
    }
    return 0;
}

const call_pair_t *call_ice_selected(const call_ice_t *a)
{
    if (!a || a->selected < 0 || (uint32_t) a->selected >= a->npairs) return NULL;
    return &a->pairs[a->selected];
}

/* ---- DHT rendezvous ---- */

int call_rv_closer(const uint8_t *target, const uint8_t *a, const uint8_t *b)
{
    for (int i = 0; i < CALL_RV_ID_LEN; i++) {
        uint8_t da = a[i] ^ target[i], db = b[i] ^ target[i];
        if (da != db) return da < db ? -1 : 1;
    }
    return 0;
}

static int record_write(const call_rv_record_t *r, uint8_t *o, uint32_t cap, bool with_sig)
{
    if (r->ncands > CALL_ICE_MAX_CANDS) return CALL_ERR_ARG;
    uint32_t need = CALL_RV_ID_LEN + 5u + r->ncands * CALL_CAND_WIRE + (with_sig ? 64u : 0u);
    if (cap < need) return CALL_ERR_SPACE;
    call_copy(o, r->id, CALL_RV_ID_LEN);
    call_put32(o + 32, r->expires_s);
    o[36] = r->ncands;
    uint32_t off = 37;
    for (uint32_t i = 0; i < r->ncands; i++, off += CALL_CAND_WIRE)
        call_cand_write(&r->cands[i], o + off, CALL_CAND_WIRE);
    if (with_sig) call_copy(o + off, r->sig, CALL_RV_SIG_LEN);
    return (int) need;
}

int call_rv_record_signed_bytes(const call_rv_record_t *r, uint8_t *out, uint32_t cap)
{
    if (!r || !out) return CALL_ERR_ARG;
    return record_write(r, out, cap, false);
}

static int record_parse(const uint8_t *in, uint32_t len, call_rv_record_t *r)
{
    if (len < CALL_RV_ID_LEN + 5u + CALL_RV_SIG_LEN) return CALL_ERR_SHORT;
    call_copy(r->id, in, CALL_RV_ID_LEN);
    r->expires_s = call_get32(in + 32);
    r->ncands = in[36];
    if (r->ncands > CALL_ICE_MAX_CANDS) return CALL_ERR_FORMAT;
    uint32_t need = 37u + r->ncands * CALL_CAND_WIRE + CALL_RV_SIG_LEN;
    if (len < need) return CALL_ERR_SHORT;
    for (uint32_t i = 0; i < r->ncands; i++) {
        int c = call_cand_parse(in + 37 + i * CALL_CAND_WIRE, CALL_CAND_WIRE, &r->cands[i]);
        if (c < 0) return c;
    }
    call_copy(r->sig, in + 37 + r->ncands * CALL_CAND_WIRE, CALL_RV_SIG_LEN);
    return (int) need;
}

int call_rv_write(const call_rv_msg_t *m, uint8_t *out, uint32_t cap)
{
    if (!m || !out) return CALL_ERR_ARG;
    if (cap < 12) return CALL_ERR_SPACE;
    call_put16(out, CALL_RV_MAGIC);
    out[2] = 1;
    out[3] = m->type;
    call_put32(out + 4, m->txid);
    call_put32(out + 8, 0);
    uint8_t *p = out + 12;
    uint32_t room = cap - 12;
    int w;
    switch (m->type) {
    case CALL_RV_REGISTER:
    case CALL_RV_FOUND:
        w = record_write(&m->rec, p, room, true);
        return w < 0 ? w : 12 + w;
    case CALL_RV_REGISTERED:
        if (room < 36) return CALL_ERR_SPACE;
        call_copy(p, m->rec.id, CALL_RV_ID_LEN);
        call_put32(p + 32, m->rec.expires_s);
        return 48;
    case CALL_RV_LOOKUP:
        if (room < 32) return CALL_ERR_SPACE;
        call_copy(p, m->target, CALL_RV_ID_LEN);
        return 44;
    case CALL_RV_NOT_FOUND: {
        if (m->npeers > CALL_RV_MAX_PEERS) return CALL_ERR_ARG;
        uint32_t need = 33u + m->npeers * (CALL_RV_ID_LEN + ADDR_WIRE);
        if (room < need) return CALL_ERR_SPACE;
        call_copy(p, m->target, CALL_RV_ID_LEN);
        p[32] = m->npeers;
        for (uint32_t i = 0; i < m->npeers; i++) {
            uint8_t *q = p + 33 + i * (CALL_RV_ID_LEN + ADDR_WIRE);
            call_copy(q, m->peers[i].id, CALL_RV_ID_LEN);
            addr_write(&m->peers[i].addr, q + CALL_RV_ID_LEN);
        }
        return (int) (12 + need);
    }
    case CALL_RV_INTRODUCE:
        if (room < 32) return CALL_ERR_SPACE;
        call_copy(p, m->target, CALL_RV_ID_LEN);
        w = record_write(&m->rec, p + 32, room - 32, true);
        return w < 0 ? w : 44 + w;
    default:
        return CALL_ERR_ARG;
    }
}

int call_rv_parse(const uint8_t *in, uint32_t len, call_rv_msg_t *m)
{
    if (!in || !m) return CALL_ERR_ARG;
    if (len < 12) return CALL_ERR_SHORT;
    if (call_get16(in) != CALL_RV_MAGIC || in[2] != 1 || call_get32(in + 8) != 0)
        return CALL_ERR_FORMAT;
    call_fill(m, 0, sizeof(*m));
    m->type = in[3];
    m->txid = call_get32(in + 4);
    const uint8_t *p = in + 12;
    uint32_t room = len - 12;
    int r;
    switch (m->type) {
    case CALL_RV_REGISTER:
    case CALL_RV_FOUND:
        r = record_parse(p, room, &m->rec);
        if (r < 0) return r;
        return (uint32_t) r == room ? (int) len : CALL_ERR_FORMAT;
    case CALL_RV_REGISTERED:
        if (room != 36) return room < 36 ? CALL_ERR_SHORT : CALL_ERR_FORMAT;
        call_copy(m->rec.id, p, CALL_RV_ID_LEN);
        m->rec.expires_s = call_get32(p + 32);
        return (int) len;
    case CALL_RV_LOOKUP:
        if (room != 32) return room < 32 ? CALL_ERR_SHORT : CALL_ERR_FORMAT;
        call_copy(m->target, p, CALL_RV_ID_LEN);
        return (int) len;
    case CALL_RV_NOT_FOUND: {
        if (room < 33) return CALL_ERR_SHORT;
        call_copy(m->target, p, CALL_RV_ID_LEN);
        m->npeers = p[32];
        if (m->npeers > CALL_RV_MAX_PEERS) return CALL_ERR_FORMAT;
        if (room != 33u + m->npeers * (CALL_RV_ID_LEN + ADDR_WIRE)) return CALL_ERR_FORMAT;
        for (uint32_t i = 0; i < m->npeers; i++) {
            const uint8_t *q = p + 33 + i * (CALL_RV_ID_LEN + ADDR_WIRE);
            call_copy(m->peers[i].id, q, CALL_RV_ID_LEN);
            if (!addr_parse(q + CALL_RV_ID_LEN, &m->peers[i].addr)) return CALL_ERR_FORMAT;
        }
        return (int) len;
    }
    case CALL_RV_INTRODUCE:
        if (room < 32) return CALL_ERR_SHORT;
        call_copy(m->target, p, CALL_RV_ID_LEN);
        r = record_parse(p + 32, room - 32, &m->rec);
        if (r < 0) return r;
        return (uint32_t) r == room - 32 ? (int) len : CALL_ERR_FORMAT;
    default:
        return CALL_ERR_FORMAT;
    }
}

static void lookup_insert(call_lookup_t *l, const call_rv_peer_t *p)
{
    for (uint32_t i = 0; i < l->ncand; i++)
        if (!call_cmp(l->cand[i].id, p->id, CALL_RV_ID_LEN)) return;
    uint32_t pos = l->ncand;
    while (pos > 0 && call_rv_closer(l->target, p->id, l->cand[pos - 1].id) < 0) pos--;
    if (pos >= 16) return;
    uint32_t last = l->ncand < 16 ? l->ncand : 15;
    for (uint32_t i = last; i > pos; i--) {
        CALL_SET(l->cand[i], l->cand[i - 1]);
        l->queried[i] = l->queried[i - 1];
    }
    CALL_SET(l->cand[pos], *p);
    l->queried[pos] = 0;
    if (l->ncand < 16) l->ncand++;
    if (l->inflight >= (int32_t) pos) l->inflight++;
    if (l->inflight >= 16) l->inflight = -1;
}

void call_lookup_init(call_lookup_t *l, const uint8_t *target, const call_rv_peer_t *seeds,
                      uint32_t nseeds, uint32_t timeout_ms, uint32_t txid0,
                      call_rv_verify_fn verify, void *vctx)
{
    call_fill(l, 0, sizeof(*l));
    call_copy(l->target, target, CALL_RV_ID_LEN);
    l->inflight = -1;
    l->timeout_ms = timeout_ms ? timeout_ms : 500;
    l->txid = txid0;
    l->verify = verify;
    l->vctx = vctx;
    for (uint32_t i = 0; i < nseeds; i++) lookup_insert(l, &seeds[i]);
    l->state = CALL_LOOKUP_RUNNING;
}

bool call_lookup_next(call_lookup_t *l, uint32_t now, call_rv_peer_t *to, call_rv_msg_t *msg)
{
    if (l->state != CALL_LOOKUP_RUNNING) return false;
    if (l->inflight >= 0) {
        if (!call_time_ge(now, l->deadline)) return false;
        l->inflight = -1; /* timed out; it stays marked as queried */
    }
    for (uint32_t i = 0; i < l->ncand; i++) {
        if (l->queried[i]) continue;
        l->queried[i] = 1;
        l->inflight = (int32_t) i;
        l->txid++;
        l->deadline = now + l->timeout_ms;
        l->queries++;
        if (to) CALL_SET(*to, l->cand[i]);
        if (msg) {
            call_fill(msg, 0, sizeof(*msg));
            msg->type = CALL_RV_LOOKUP;
            msg->txid = l->txid;
            call_copy(msg->target, l->target, CALL_RV_ID_LEN);
        }
        return true;
    }
    l->state = CALL_LOOKUP_FAILED;
    return false;
}

void call_lookup_on_reply(call_lookup_t *l, const call_rv_msg_t *msg)
{
    if (l->state != CALL_LOOKUP_RUNNING || l->inflight < 0 || msg->txid != l->txid) return;
    if (msg->type == CALL_RV_FOUND) {
        uint8_t buf[CALL_RV_MAX_MSG];
        int n = call_rv_record_signed_bytes(&msg->rec, buf, sizeof(buf));
        if (n > 0 && !call_cmp(msg->rec.id, l->target, CALL_RV_ID_LEN) && l->verify &&
            l->verify(l->vctx, msg->rec.id, buf, (uint32_t) n, msg->rec.sig)) {
            CALL_SET(l->found, msg->rec);
            l->state = CALL_LOOKUP_FOUND;
        } else {
            l->bad_sig++;
        }
        l->inflight = -1;
        return;
    }
    if (msg->type == CALL_RV_NOT_FOUND && !call_cmp(msg->target, l->target, CALL_RV_ID_LEN)) {
        l->inflight = -1;
        for (uint32_t i = 0; i < msg->npeers && i < CALL_RV_MAX_PEERS; i++)
            lookup_insert(l, &msg->peers[i]);
    }
}
