/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* peer_audit.c — self-audit, deterministic replay, committees, quorum,
 * canaries, evidence and inference spot checks. See peer_audit.h. */
#include "peer_audit.h"
#include "../robin_debanks/sha256.h"

/* ===== Byte helpers (no libc) ===== */
static void pa_zero(void *p, uint32_t n)
{
    volatile uint8_t *d = (volatile uint8_t *) p;
    for (uint32_t i = 0; i < n; i++) d[i] = 0;
}

static void pa_cpy(void *dst, const void *src, uint32_t n)
{
    volatile uint8_t *d = (volatile uint8_t *) dst;
    const uint8_t *s = (const uint8_t *) src;
    for (uint32_t i = 0; i < n; i++) d[i] = s[i];
}

static bool pa_eq(const void *a, const void *b, uint32_t n)
{
    const uint8_t *x = (const uint8_t *) a, *y = (const uint8_t *) b;
    uint8_t acc = 0;
    for (uint32_t i = 0; i < n; i++) acc |= (uint8_t) (x[i] ^ y[i]);
    return acc == 0;
}

static uint32_t pa_strlen(const char *s)
{
    uint32_t n = 0;
    while (s[n]) n++;
    return n;
}

static void h_u32(sha256_ctx_t *h, uint32_t v)
{
    uint8_t b[4];
    for (uint32_t i = 0; i < 4; i++) b[i] = (uint8_t) (v >> (8u * i));
    sha256_update(h, b, 4);
}

static void h_u64(sha256_ctx_t *h, uint64_t v)
{
    uint8_t b[8];
    for (uint32_t i = 0; i < 8; i++) b[i] = (uint8_t) (v >> (8u * i));
    sha256_update(h, b, 8);
}

static void h_str(sha256_ctx_t *h, const char *s)
{
    sha256_update(h, (const uint8_t *) s, pa_strlen(s));
}

static void pa_sign(const pa_ctx_t *c, const char *ctx, const uint8_t msg[PA_HASH_LEN],
                    uint8_t sig[PA_SIG_LEN])
{
    /* Deterministic ML-DSA variant (rnd = NULL): the module has no RNG. */
    pq_mldsa65_sign(c->sk, msg, PA_HASH_LEN, (const uint8_t *) ctx, pa_strlen(ctx), (void *) 0,
                    sig);
}

static bool pa_verify(const uint8_t pk[PA_PK_LEN], const char *ctx, const uint8_t msg[PA_HASH_LEN],
                      const uint8_t sig[PA_SIG_LEN])
{
    return pq_mldsa65_verify(pk, msg, PA_HASH_LEN, (const uint8_t *) ctx, pa_strlen(ctx), sig);
}

/* ===== Setup ===== */
void pa_config_default(pa_config_t *c)
{
    if (!c) return;
    c->canary_per_window = 4;
    c->canary_window = 1000;
    c->strike_limit = 2;
}

void pa_init(pa_ctx_t *c, uint32_t self, const uint8_t *sk, const pa_config_t *cfg,
             pa_replay_fn replay, void *replay_ctx, pa_revoke_fn revoke, void *revoke_ctx)
{
    if (!c) return;
    pa_zero(c, (uint32_t) sizeof *c);
    c->self = self;
    c->sk = sk;
    c->state = PA_NODE_ACTIVE;
    c->self_audit = true;
    if (cfg)
        c->cfg = *cfg;
    else
        pa_config_default(&c->cfg);
    c->replay = replay;
    c->replay_ctx = replay_ctx;
    c->revoke = revoke;
    c->revoke_ctx = revoke_ctx;
}

static pa_peer_t *peer_find(pa_ctx_t *c, uint32_t id)
{
    for (uint32_t i = 0; i < c->n_peers; i++)
        if (c->peers[i].used && c->peers[i].id == id) return &c->peers[i];
    return (pa_peer_t *) 0;
}

const pa_peer_t *pa_peer_get(const pa_ctx_t *c, uint32_t id)
{
    if (!c) return (const pa_peer_t *) 0;
    return peer_find((pa_ctx_t *) c, id);
}

bool pa_peer_blocked(const pa_ctx_t *c, uint32_t id)
{
    const pa_peer_t *p = pa_peer_get(c, id);
    return p ? p->blocked : false;
}

pa_status_t pa_peer_add(pa_ctx_t *c, uint32_t id, const uint8_t pk[PA_PK_LEN])
{
    if (!c || !pk) return PA_ERR_ARG;
    if (peer_find(c, id)) return PA_ERR_ARG; /* a key is never silently replaced */
    if (c->n_peers >= PA_MAX_PEERS) return PA_ERR_FULL;
    pa_peer_t *p = &c->peers[c->n_peers++];
    pa_zero(p, (uint32_t) sizeof *p);
    p->id = id;
    p->used = true;
    pa_cpy(p->pk, pk, PA_PK_LEN);
    return PA_OK;
}

/* ===== Layer 1: self-audit ===== */
static void event_log(pa_ctx_t *c, uint32_t kind, pa_status_t st)
{
    if (c->n_events < PA_MAX_EVENTS) {
        pa_event_t *e = &c->events[c->n_events];
        e->seq = c->seq;
        e->status = (int32_t) st;
        e->kind = kind;
    }
    if (c->n_events != 0xFFFFFFFFu) c->n_events++;
}

static pa_status_t precommit_eval(const pa_precommit_t *p)
{
    /* before + minted and after + burned, each without overflow. */
    uint64_t lhs = p->sum_before + p->minted;
    if (lhs < p->sum_before) return PA_ERR_OVERFLOW;
    uint64_t rhs = p->sum_after + p->burned;
    if (rhs < p->sum_after) return PA_ERR_OVERFLOW;
    if (p->tokens_requested > p->tokens_remaining) return PA_ERR_BUDGET;
    if (lhs != rhs) return PA_ERR_CONSERVATION;
    if (p->headroom <= 0) return PA_ERR_HEADROOM;
    if (p->size > p->size_max) return PA_ERR_SIZE;
    return PA_OK;
}

pa_status_t pa_precommit_check(pa_ctx_t *c, uint32_t kind, const pa_precommit_t *p)
{
    if (!c) return PA_ERR_ARG;
    if (c->state == PA_NODE_CONTRADICTION) return PA_ERR_QUARANTINED;
    if (!c->self_audit) return PA_OK;
    pa_status_t st = p ? precommit_eval(p) : PA_ERR_ARG;
    if (st != PA_OK) {
        event_log(c, kind, st);
        c->state = PA_NODE_CONTRADICTION;
    }
    return st;
}

void pa_node_release(pa_ctx_t *c)
{
    if (c) c->state = PA_NODE_ACTIVE;
}

/* ===== Records ===== */
bool pa_record_hash(const pa_record_t *r, uint8_t out[PA_HASH_LEN])
{
    pa_zero(out, PA_HASH_LEN);
    if (!r || r->in_len > PA_MAX_IO || r->out_len > PA_MAX_IO) return false;
    sha256_ctx_t h;
    sha256_init(&h);
    h_str(&h, "zxv-pa-record-v1");
    h_u32(&h, r->kind);
    h_u32(&h, r->node);
    h_u64(&h, r->seq);
    sha256_update(&h, r->prev_state, PA_HASH_LEN);
    sha256_update(&h, r->new_state, PA_HASH_LEN);
    h_u32(&h, r->in_len);
    sha256_update(&h, r->in, r->in_len);
    h_u32(&h, r->out_len);
    sha256_update(&h, r->out, r->out_len);
    sha256_final(&h, out);
    return true;
}

pa_status_t pa_emit_record(pa_ctx_t *c, uint32_t kind, const uint8_t prev[PA_HASH_LEN],
                           const uint8_t *in, uint32_t in_len, const uint8_t *out, uint32_t out_len,
                           const uint8_t new_state[PA_HASH_LEN], const pa_precommit_t *p,
                           pa_record_t *rec)
{
    if (!c || !c->sk || !prev || !new_state || !rec || (in_len && !in) || (out_len && !out))
        return PA_ERR_ARG;
    pa_zero(rec, (uint32_t) sizeof *rec); /* S0: nothing emitted unless all passes */
    pa_status_t st = pa_precommit_check(c, kind, p);
    if (st != PA_OK) return st;
    if (in_len > PA_MAX_IO || out_len > PA_MAX_IO) {
        if (c->self_audit) {
            event_log(c, kind, PA_ERR_SIZE);
            c->state = PA_NODE_CONTRADICTION;
        }
        return PA_ERR_SIZE;
    }
    rec->kind = kind;
    rec->node = c->self;
    rec->seq = c->seq++;
    pa_cpy(rec->prev_state, prev, PA_HASH_LEN);
    pa_cpy(rec->new_state, new_state, PA_HASH_LEN);
    rec->in_len = in_len;
    rec->out_len = out_len;
    if (in_len) pa_cpy(rec->in, in, in_len);
    if (out_len) pa_cpy(rec->out, out, out_len);
    uint8_t rh[PA_HASH_LEN];
    pa_record_hash(rec, rh);
    pa_sign(c, PA_CTX_RECORD, rh, rec->sig);
    return PA_OK;
}

pa_status_t pa_record_verify_sig(const pa_ctx_t *c, const pa_record_t *r)
{
    if (!c || !r) return PA_ERR_ARG;
    const pa_peer_t *p = pa_peer_get(c, r->node);
    if (!p) return PA_ERR_UNKNOWN_PEER;
    uint8_t rh[PA_HASH_LEN];
    if (!pa_record_hash(r, rh)) return PA_ERR_SIZE;
    return pa_verify(p->pk, PA_CTX_RECORD, rh, r->sig) ? PA_OK : PA_ERR_SIG;
}

/* ===== Layer 2A ===== */
/* Replay without any check of who signed: the pure state-machine part. */
static pa_status_t replay_raw(pa_ctx_t *c, const pa_record_t *r, uint8_t *out, uint32_t *out_len,
                              uint8_t st[PA_HASH_LEN])
{
    if (!c->replay) return PA_ERR_KIND;
    if (r->in_len > PA_MAX_IO || r->out_len > PA_MAX_IO) return PA_ERR_SIZE;
    *out_len = PA_MAX_IO;
    pa_status_t s =
        c->replay(c->replay_ctx, r->kind, r->prev_state, r->in, r->in_len, out, out_len, st);
    if (s == PA_OK && *out_len > PA_MAX_IO) return PA_ERR_SIZE;
    return s;
}

static bool result_matches(const pa_record_t *r, const uint8_t *out, uint32_t out_len,
                           const uint8_t st[PA_HASH_LEN])
{
    return out_len == r->out_len && pa_eq(out, r->out, out_len) &&
           pa_eq(st, r->new_state, PA_HASH_LEN);
}

pa_status_t pa_replay_check(pa_ctx_t *c, const pa_record_t *r, uint8_t *exp_out, uint32_t *exp_len,
                            uint8_t exp_state[PA_HASH_LEN])
{
    if (!c || !r) return PA_ERR_ARG;
    const pa_peer_t *p = pa_peer_get(c, r->node);
    if (!p) return PA_ERR_UNKNOWN_PEER;
    if (p->blocked) return PA_ERR_BLOCKED;
    pa_status_t s = pa_record_verify_sig(c, r);
    if (s != PA_OK) return s;
    uint8_t out[PA_MAX_IO], st[PA_HASH_LEN];
    uint32_t n = 0;
    s = replay_raw(c, r, out, &n, st);
    if (s != PA_OK) return s;
    if (exp_out && exp_len) {
        pa_cpy(exp_out, out, n);
        *exp_len = n;
    }
    if (exp_state) pa_cpy(exp_state, st, PA_HASH_LEN);
    return result_matches(r, out, n, st) ? PA_OK : PA_ERR_MISMATCH;
}

void pa_vote_hash(const pa_vote_t *v, uint8_t out[PA_HASH_LEN])
{
    sha256_ctx_t h;
    sha256_init(&h);
    h_str(&h, "zxv-pa-vote-v1");
    sha256_update(&h, v->record_hash, PA_HASH_LEN);
    h_u32(&h, v->voter);
    sha256_update(&h, &v->vote, 1);
    sha256_update(&h, v->replay_state, PA_HASH_LEN);
    sha256_final(&h, out);
}

pa_status_t pa_vote(pa_ctx_t *c, const pa_record_t *r, pa_vote_t *v)
{
    if (!c || !c->sk || !r || !v) return PA_ERR_ARG;
    pa_zero(v, (uint32_t) sizeof *v);
    uint8_t st[PA_HASH_LEN];
    pa_zero(st, PA_HASH_LEN);
    pa_status_t s = pa_replay_check(c, r, (uint8_t *) 0, (uint32_t *) 0, st);
    if (s == PA_OK)
        v->vote = PA_VOTE_ACCEPT;
    else if (s == PA_ERR_MISMATCH || s == PA_ERR_SIG || s == PA_ERR_BLOCKED)
        v->vote = PA_VOTE_REJECT;
    else
        v->vote = PA_VOTE_ABSTAIN; /* no state, unknown author: no opinion */
    pa_record_hash(r, v->record_hash);
    v->voter = c->self;
    pa_cpy(v->replay_state, st, PA_HASH_LEN);
    uint8_t vh[PA_HASH_LEN];
    pa_vote_hash(v, vh);
    pa_sign(c, PA_CTX_VOTE, vh, v->sig);
    return s;
}

/* 32-bit word stream from SHA-256(domain || seed || counter). */
typedef struct {
    uint8_t seed[PA_HASH_LEN];
    uint8_t blk[PA_HASH_LEN];
    uint32_t ctr, pos;
} pa_stream_t;

static uint32_t stream_u32(pa_stream_t *s)
{
    if (s->pos >= PA_HASH_LEN) {
        sha256_ctx_t h;
        sha256_init(&h);
        h_str(&h, "zxv-pa-committee");
        sha256_update(&h, s->seed, PA_HASH_LEN);
        h_u32(&h, s->ctr++);
        sha256_final(&h, s->blk);
        s->pos = 0;
    }
    uint32_t v = (uint32_t) s->blk[s->pos] | ((uint32_t) s->blk[s->pos + 1] << 8) |
                 ((uint32_t) s->blk[s->pos + 2] << 16) | ((uint32_t) s->blk[s->pos + 3] << 24);
    s->pos += 4;
    return v;
}

/* Uniform in [0, m) for 1 <= m <= PA_MAX_PEERS, by rejection (32-bit ops). */
static uint32_t stream_below(pa_stream_t *s, uint32_t m)
{
    uint32_t lim = 0xFFFFFFFFu - (0xFFFFFFFFu % m) - 1u; /* accept v <= lim */
    for (;;) {
        uint32_t v = stream_u32(s);
        if (v <= lim) return v % m;
    }
}

pa_status_t pa_committee(const uint32_t *peers, uint32_t n, uint32_t author,
                         const uint8_t record_hash[PA_HASH_LEN], uint32_t k, uint32_t *out)
{
    if (!peers || !record_hash || !out || n > PA_MAX_PEERS || k == 0 || k > PA_MAX_COMMITTEE)
        return PA_ERR_ARG;
    uint32_t c[PA_MAX_PEERS];
    uint32_t m = 0;
    /* insertion sort with de-duplication, author removed */
    for (uint32_t i = 0; i < n; i++) {
        uint32_t v = peers[i];
        if (v == author) continue;
        uint32_t j = 0;
        while (j < m && c[j] < v) j++;
        if (j < m && c[j] == v) continue;
        for (uint32_t t = m; t > j; t--) c[t] = c[t - 1];
        c[j] = v;
        m++;
    }
    if (k > m) return PA_ERR_FULL;
    pa_stream_t s;
    pa_cpy(s.seed, record_hash, PA_HASH_LEN);
    s.ctr = 0;
    s.pos = PA_HASH_LEN;
    for (uint32_t i = 0; i < k; i++) {
        uint32_t j = i + stream_below(&s, m - i);
        uint32_t t = c[i];
        c[i] = c[j];
        c[j] = t;
        out[i] = c[i];
    }
    return PA_OK;
}

pa_verdict_t pa_quorum(const pa_ctx_t *c, const uint8_t record_hash[PA_HASH_LEN],
                       const uint32_t *committee, uint32_t k, uint32_t f, const pa_vote_t *votes,
                       uint32_t n_votes, uint32_t *counted)
{
    if (counted) *counted = 0;
    if (!c || !record_hash || !committee || !votes || k > PA_MAX_COMMITTEE || f > 10u ||
        k < 3u * f + 1u)
        return PA_VERDICT_UNDECIDED;
    bool seen[PA_MAX_COMMITTEE];
    for (uint32_t i = 0; i < PA_MAX_COMMITTEE; i++) seen[i] = false;
    uint32_t acc = 0, rej = 0, valid = 0;
    for (uint32_t i = 0; i < n_votes; i++) {
        const pa_vote_t *v = &votes[i];
        uint32_t slot = k;
        for (uint32_t j = 0; j < k; j++)
            if (committee[j] == v->voter) slot = j;
        if (slot == k || seen[slot]) continue;
        if (!pa_eq(v->record_hash, record_hash, PA_HASH_LEN)) continue;
        const pa_peer_t *p = pa_peer_get(c, v->voter);
        if (!p) continue;
        uint8_t vh[PA_HASH_LEN];
        pa_vote_hash(v, vh);
        if (!pa_verify(p->pk, PA_CTX_VOTE, vh, v->sig)) continue;
        seen[slot] = true;
        valid++;
        if (v->vote == PA_VOTE_ACCEPT) acc++;
        if (v->vote == PA_VOTE_REJECT) rej++;
    }
    if (counted) *counted = valid;
    uint32_t need = 2u * f + 1u;
    if (acc >= need && rej < need) return PA_VERDICT_ACCEPT;
    if (rej >= need && acc < need) return PA_VERDICT_REJECT;
    return PA_VERDICT_UNDECIDED;
}

/* ===== Layer 2C: evidence ===== */
bool pa_evidence_hash(const pa_evidence_t *e, uint8_t out[PA_HASH_LEN])
{
    pa_zero(out, PA_HASH_LEN);
    if (!e || e->correct_len > PA_MAX_IO) return false;
    uint8_t rh[PA_HASH_LEN];
    if (!pa_record_hash(&e->record, rh)) return false;
    sha256_ctx_t h;
    sha256_init(&h);
    h_str(&h, "zxv-pa-evidence-v1");
    sha256_update(&h, rh, PA_HASH_LEN);
    /* the accused's signature is part of the evidence */
    sha256_update(&h, e->record.sig, PA_SIG_LEN);
    h_u32(&h, e->accuser);
    h_u32(&h, e->correct_len);
    sha256_update(&h, e->correct_out, e->correct_len);
    sha256_update(&h, e->correct_state, PA_HASH_LEN);
    sha256_final(&h, out);
    return true;
}

pa_status_t pa_evidence_make(pa_ctx_t *c, const pa_record_t *r, pa_evidence_t *e)
{
    if (!c || !c->sk || !r || !e) return PA_ERR_ARG;
    pa_zero(e, (uint32_t) sizeof *e);
    pa_status_t s = pa_replay_check(c, r, e->correct_out, &e->correct_len, e->correct_state);
    if (s != PA_ERR_MISMATCH) {
        pa_zero(e, (uint32_t) sizeof *e);
        return s == PA_OK ? PA_ERR_FALSE_ACCUSATION : s; /* never accuse an honest record */
    }
    pa_cpy(&e->record, r, (uint32_t) sizeof *r);
    e->accuser = c->self;
    uint8_t eh[PA_HASH_LEN];
    pa_evidence_hash(e, eh);
    pa_sign(c, PA_CTX_EVIDENCE, eh, e->sig);
    return PA_OK;
}

static void isolate(pa_ctx_t *c, pa_peer_t *p, pa_revoke_reason_t why, const uint8_t *eh)
{
    if (p->blocked) return;
    p->blocked = true;
    if (c->revoke) c->revoke(c->revoke_ctx, p->id, why, eh);
}

static pa_status_t strike(pa_ctx_t *c, pa_peer_t *accuser, const uint8_t eh[PA_HASH_LEN])
{
    if (accuser->strikes != 0xFFFFFFFFu) accuser->strikes++;
    if (accuser->strikes >= c->cfg.strike_limit) isolate(c, accuser, PA_REVOKE_FALSE_ACCUSER, eh);
    return PA_ERR_FALSE_ACCUSATION;
}

pa_status_t pa_evidence_check(pa_ctx_t *c, const pa_evidence_t *e)
{
    if (!c || !e) return PA_ERR_ARG;
    uint8_t eh[PA_HASH_LEN];
    if (!pa_evidence_hash(e, eh)) return PA_ERR_SIZE;
    pa_peer_t *acc = peer_find(c, e->accuser);
    if (!acc) return PA_ERR_UNKNOWN_PEER;
    if (!pa_verify(acc->pk, PA_CTX_EVIDENCE, eh, e->sig)) return PA_ERR_SIG;
    pa_peer_t *accused = peer_find(c, e->record.node);
    if (!accused) return PA_ERR_INCONCLUSIVE;
    /* A record the accused never signed is a forgery by the accuser. */
    if (pa_record_verify_sig(c, &e->record) != PA_OK) return strike(c, acc, eh);
    uint8_t out[PA_MAX_IO], st[PA_HASH_LEN];
    uint32_t n = 0;
    pa_status_t s = replay_raw(c, &e->record, out, &n, st);
    if (s != PA_OK) return PA_ERR_INCONCLUSIVE;
    if (result_matches(&e->record, out, n, st)) return strike(c, acc, eh);
    if (n != e->correct_len || !pa_eq(out, e->correct_out, n) ||
        !pa_eq(st, e->correct_state, PA_HASH_LEN))
        return PA_ERR_INCONCLUSIVE;
    isolate(c, accused, PA_REVOKE_MISBEHAVIOUR, eh);
    return PA_OK;
}

/* ===== Layer 2B: canaries ===== */
bool pa_request_hash(const pa_request_t *q, uint8_t out[PA_HASH_LEN])
{
    pa_zero(out, PA_HASH_LEN);
    if (!q || q->in_len > PA_MAX_IO) return false;
    sha256_ctx_t h;
    sha256_init(&h);
    h_str(&h, "zxv-pa-request-v1");
    h_u32(&h, q->kind);
    sha256_update(&h, q->nonce, PA_REQ_NONCE_LEN);
    sha256_update(&h, q->prev_state, PA_HASH_LEN);
    h_u32(&h, q->in_len);
    sha256_update(&h, q->in, q->in_len);
    sha256_final(&h, out);
    return true;
}

pa_status_t pa_canary_issue(pa_ctx_t *c, uint32_t target, uint64_t tick, const pa_request_t *q)
{
    if (!c || !q || q->in_len > PA_MAX_IO || !c->replay) return PA_ERR_ARG;
    pa_peer_t *p = peer_find(c, target);
    if (!p) return PA_ERR_UNKNOWN_PEER;
    if (tick < p->canary_win || tick - p->canary_win >= c->cfg.canary_window) {
        p->canary_win = tick;
        p->canary_n = 0;
    }
    if (p->canary_n >= c->cfg.canary_per_window) return PA_ERR_RATE;
    uint32_t slot = PA_MAX_CANARIES;
    for (uint32_t i = 0; i < PA_MAX_CANARIES; i++)
        if (!c->canaries[i].used) {
            slot = i;
            break;
        }
    if (slot == PA_MAX_CANARIES) return PA_ERR_FULL;
    pa_canary_t *k = &c->canaries[slot];
    uint32_t n = PA_MAX_IO;
    pa_status_t s = c->replay(c->replay_ctx, q->kind, q->prev_state, q->in, q->in_len, k->exp_out,
                              &n, k->exp_state);
    if (s != PA_OK) return s;
    if (n > PA_MAX_IO) return PA_ERR_SIZE;
    k->exp_len = n;
    k->target = target;
    pa_cpy(k->prev, q->prev_state, PA_HASH_LEN);
    pa_request_hash(q, k->req_hash);
    k->used = true;
    p->canary_n++;
    return PA_OK;
}

pa_status_t pa_canary_check(pa_ctx_t *c, const pa_request_t *q, const pa_record_t *a,
                            pa_evidence_t *ev)
{
    if (!c || !q || !a) return PA_ERR_ARG;
    uint8_t qh[PA_HASH_LEN];
    if (!pa_request_hash(q, qh)) return PA_ERR_SIZE;
    pa_canary_t *k = (pa_canary_t *) 0;
    for (uint32_t i = 0; i < PA_MAX_CANARIES; i++)
        if (c->canaries[i].used && c->canaries[i].target == a->node &&
            pa_eq(c->canaries[i].req_hash, qh, PA_HASH_LEN))
            k = &c->canaries[i];
    if (!k) return PA_ERR_NOT_FOUND;
    k->used = false;
    pa_status_t s = pa_record_verify_sig(c, a);
    /* The answer must be to this request: same kind, state and input. */
    bool same_req = a->kind == q->kind && a->in_len == q->in_len &&
                    pa_eq(a->in, q->in, q->in_len) && pa_eq(a->prev_state, k->prev, PA_HASH_LEN);
    if (s != PA_OK || !same_req) return s != PA_OK ? s : PA_ERR_ARG;
    if (a->out_len == k->exp_len && pa_eq(a->out, k->exp_out, k->exp_len) &&
        pa_eq(a->new_state, k->exp_state, PA_HASH_LEN))
        return PA_OK;
    pa_peer_t *p = peer_find(c, a->node);
    if (p && p->canary_failed != 0xFFFFFFFFu) p->canary_failed++;
    if (ev) pa_evidence_make(c, a, ev);
    return PA_ERR_MISMATCH;
}

/* ===== Inference spot checks ===== */
void pa_infer_commit(const uint8_t model_cid[PA_HASH_LEN], uint64_t seed, const uint8_t *prompt,
                     uint32_t prompt_len, const int32_t *tokens, uint32_t n,
                     uint8_t out[PA_HASH_LEN])
{
    sha256_ctx_t h;
    sha256_init(&h);
    h_str(&h, "zxv-pa-infer-v1");
    sha256_update(&h, model_cid, PA_HASH_LEN);
    h_u64(&h, seed);
    h_u32(&h, prompt_len);
    if (prompt_len) sha256_update(&h, prompt, prompt_len);
    h_u32(&h, n);
    for (uint32_t i = 0; i < n; i++) h_u32(&h, (uint32_t) tokens[i]);
    sha256_final(&h, out);
}

bool pa_spot_sampled(const uint8_t beacon[PA_HASH_LEN], const uint8_t commit[PA_HASH_LEN],
                     uint32_t rate_q16)
{
    if (rate_q16 >= 65536u) return true;
    if (rate_q16 == 0) return false;
    sha256_ctx_t h;
    uint8_t d[PA_HASH_LEN];
    sha256_init(&h);
    h_str(&h, "zxv-pa-spot-v1");
    sha256_update(&h, beacon, PA_HASH_LEN);
    sha256_update(&h, commit, PA_HASH_LEN);
    sha256_final(&h, d);
    uint32_t v = (uint32_t) d[0] | ((uint32_t) d[1] << 8); /* uniform 16 bits */
    return v < rate_q16;
}

#define PA_SPOT_MAX_TOKENS 256u
pa_status_t pa_spot_check(pa_infer_fn infer, void *ictx, const uint8_t model_cid[PA_HASH_LEN],
                          uint64_t seed, const uint8_t *prompt, uint32_t prompt_len,
                          uint32_t n_tokens, const uint8_t commit[PA_HASH_LEN])
{
    if (!infer || !model_cid || !commit || n_tokens > PA_SPOT_MAX_TOKENS) return PA_ERR_ARG;
    int32_t tok[PA_SPOT_MAX_TOKENS];
    int32_t got = infer(ictx, model_cid, seed, prompt, prompt_len, tok, n_tokens);
    if (got < 0 || (uint32_t) got != n_tokens) return PA_ERR_MISMATCH;
    uint8_t h[PA_HASH_LEN];
    pa_infer_commit(model_cid, seed, prompt, prompt_len, tok, n_tokens, h);
    return pa_eq(h, commit, PA_HASH_LEN) ? PA_OK : PA_ERR_MISMATCH;
}
