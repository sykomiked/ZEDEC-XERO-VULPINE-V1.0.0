/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* airc.c — AIRC's protocol core (airc.h A1-A19). */
#include "airc.h"

#include "../mlkem/keccak.h"
#include "../pay/pay_util.h"
#include "../pqsec/pq_matrix_algs.h"

static const uint8_t CTX_POST[] = "zxv/airc/v1/post";
static const uint8_t CTX_VOTE[] = "zxv/airc/v1/vote";
static const uint8_t CTX_REQ[] = "zxv/airc/v1/req";
#define CTXLEN(c) (sizeof(c) - 1u)

/* ---------------- small helpers ---------------- */

static void cpy(uint8_t *d, const uint8_t *s, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) d[i] = s[i];
}
static bool eq(const uint8_t *a, const uint8_t *b, uint32_t n)
{
    uint8_t x = 0;
    for (uint32_t i = 0; i < n; i++) x |= (uint8_t) (a[i] ^ b[i]);
    return x == 0;
}
static void zero(void *p, uint32_t n)
{
    volatile uint8_t *v = p;
    while (n--) *v++ = 0;
}
static bool is_zero(const uint8_t *p, uint32_t n)
{
    uint8_t x = 0;
    for (uint32_t i = 0; i < n; i++) x |= p[i];
    return x == 0;
}
/* lexicographic compare of 32-byte ids */
static int cmp32(const uint8_t *a, const uint8_t *b)
{
    for (uint32_t i = 0; i < 32; i++)
        if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    return 0;
}
static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t) (v >> 24);
    p[1] = (uint8_t) (v >> 16);
    p[2] = (uint8_t) (v >> 8);
    p[3] = (uint8_t) v;
}
static void put64(uint8_t *p, uint64_t v)
{
    put32(p, (uint32_t) (v >> 32));
    put32(p + 4, (uint32_t) v);
}
static uint32_t get32(const uint8_t *p)
{
    return (uint32_t) p[0] << 24 | (uint32_t) p[1] << 16 | (uint32_t) p[2] << 8 | p[3];
}
static uint64_t get64(const uint8_t *p)
{
    return (uint64_t) get32(p) << 32 | get32(p + 4);
}

const char *airc_status_str(airc_status_t s)
{
    switch (s) {
    case AIRC_OK:
        return "ok";
    case AIRC_ERR_ARG:
        return "bad argument";
    case AIRC_ERR_FORMAT:
        return "not a canonical AIRC post";
    case AIRC_ERR_SIG:
        return "the signature does not verify";
    case AIRC_ERR_AUTHOR:
        return "the author is not the signer";
    case AIRC_ERR_BOARD:
        return "the post belongs to another board";
    case AIRC_ERR_DUP:
        return "already held";
    case AIRC_ERR_RATE:
        return "the author's post bucket is empty";
    case AIRC_ERR_DROPPED:
        return "the author is dropped";
    case AIRC_ERR_LEASE:
        return "no usable lease for a jumbo post";
    case AIRC_ERR_MISMATCH:
        return "the body does not match the author's request";
    case AIRC_ERR_FULL:
        return "table full";
    case AIRC_ERR_NAME:
        return "not a board name";
    }
    return "unknown";
}

void airc_key_id(const uint8_t pk[AIRC_PK_LEN], uint8_t id[32])
{
    sha3_256(pk, AIRC_PK_LEN, id);
}

/* ---------------- posts (A1-A6) ---------------- */

static bool truth_ok(uint8_t t)
{
    return t <= AIRC_T;
}

static bool post_ok(const airc_post_t *p)
{
    if (p->kind < AIRC_TEXT || p->kind > AIRC_REVIEW || !truth_ok(p->truth) || p->nrefs > AIRC_REFS)
        return false;
    if (p->body_len > AIRC_JUMBO_MAX || (p->body_len && !p->body)) return false;
    bool jumbo = p->body_len > AIRC_FRAME_MAX, leased = !is_zero(p->lease, 32);
    if (jumbo != leased) return false;
    for (uint32_t i = 0; i < p->nrefs; i++)
        if (p->refs[i].kind != AIRC_REF_CHUNK && p->refs[i].kind != AIRC_REF_FREIGHT) return false;
    if ((p->kind == AIRC_OFFER || p->kind == AIRC_REVIEW) && p->nrefs < 1) return false;
    if (p->kind == AIRC_REVIEW && is_zero(p->parent, 32)) return false;
    return true;
}

uint32_t airc_post_encode(const airc_post_t *p, uint8_t *out, uint32_t cap)
{
    if (!p || !out || !post_ok(p)) return 0;
    uint32_t need = AIRC_HDR_LEN + p->body_len + p->nrefs * AIRC_REF_LEN;
    if (need > cap) return 0;
    uint8_t *o = out;
    o[0] = 'A', o[1] = 'I', o[2] = 'R', o[3] = 'C';
    o[4] = AIRC_VERSION;
    o[5] = p->kind;
    o[6] = p->truth;
    o[7] = p->nrefs;
    o += 8;
    cpy(o, p->board, 32), o += 32;
    cpy(o, p->author, 32), o += 32;
    put64(o, p->seq), o += 8;
    put64(o, p->ms), o += 8;
    cpy(o, p->parent, 32), o += 32;
    cpy(o, p->lease, 32), o += 32;
    put32(o, p->body_len), o += 4;
    if (p->body_len) cpy(o, p->body, p->body_len);
    o += p->body_len;
    for (uint32_t i = 0; i < p->nrefs; i++) {
        cpy(o, p->refs[i].hash, 32), o += 32;
        put64(o, p->refs[i].size), o += 8;
        *o++ = p->refs[i].kind;
    }
    return need;
}

airc_status_t airc_post_decode(const uint8_t *in, uint32_t len, airc_post_t *p)
{
    if (!in || !p) return AIRC_ERR_ARG;
    zero(p, sizeof *p);
    if (len < AIRC_HDR_LEN) return AIRC_ERR_FORMAT;
    if (in[0] != 'A' || in[1] != 'I' || in[2] != 'R' || in[3] != 'C' || in[4] != AIRC_VERSION)
        return AIRC_ERR_FORMAT;
    p->kind = in[5];
    p->truth = in[6];
    p->nrefs = in[7];
    const uint8_t *q = in + 8;
    cpy(p->board, q, 32), q += 32;
    cpy(p->author, q, 32), q += 32;
    p->seq = get64(q), q += 8;
    p->ms = get64(q), q += 8;
    cpy(p->parent, q, 32), q += 32;
    cpy(p->lease, q, 32), q += 32;
    p->body_len = get32(q), q += 4;
    if (p->nrefs > AIRC_REFS || p->body_len > AIRC_JUMBO_MAX) return AIRC_ERR_FORMAT;
    uint64_t need = (uint64_t) AIRC_HDR_LEN + p->body_len + (uint64_t) p->nrefs * AIRC_REF_LEN;
    if (need != len) return AIRC_ERR_FORMAT; /* short, or bytes left over */
    p->body = p->body_len ? q : 0;
    q += p->body_len;
    for (uint32_t i = 0; i < p->nrefs; i++) {
        cpy(p->refs[i].hash, q, 32), q += 32;
        p->refs[i].size = get64(q), q += 8;
        p->refs[i].kind = *q++;
    }
    if (!post_ok(p)) return AIRC_ERR_FORMAT;
    return AIRC_OK;
}

uint32_t airc_post_sign(airc_post_t *p, const uint8_t pk[AIRC_PK_LEN],
                        const uint8_t sk[AIRC_SK_LEN], const uint8_t *rnd, uint8_t *wire,
                        uint32_t cap)
{
    if (!p || !pk || !sk || !wire || cap < AIRC_SIG_LEN + AIRC_PK_LEN) return 0;
    airc_key_id(pk, p->author);
    uint32_t n = airc_post_encode(p, wire, cap - AIRC_SIG_LEN - AIRC_PK_LEN);
    if (!n) return 0;
    pqm_mldsa87_sign(sk, wire, n, CTX_POST, CTXLEN(CTX_POST), rnd, wire + n);
    cpy(wire + n + AIRC_SIG_LEN, pk, AIRC_PK_LEN);
    return n + AIRC_SIG_LEN + AIRC_PK_LEN;
}

airc_status_t airc_post_open(const uint8_t *wire, uint32_t len, airc_signed_t *out)
{
    if (!wire || !out) return AIRC_ERR_ARG;
    zero(out, sizeof *out);
    if (len < AIRC_HDR_LEN + AIRC_SIG_LEN + AIRC_PK_LEN) return AIRC_ERR_FORMAT;
    uint32_t n = len - AIRC_SIG_LEN - AIRC_PK_LEN;
    airc_status_t st = airc_post_decode(wire, n, &out->p);
    if (st != AIRC_OK) return st;
    const uint8_t *sig = wire + n, *pk = sig + AIRC_SIG_LEN;
    if (!pqm_mldsa87_verify(pk, wire, n, CTX_POST, CTXLEN(CTX_POST), sig)) return AIRC_ERR_SIG;
    uint8_t kid[32];
    airc_key_id(pk, kid);
    if (!eq(kid, out->p.author, 32)) return AIRC_ERR_AUTHOR;
    sha3_256(wire, (size_t) n + AIRC_SIG_LEN, out->id);
    out->pk = pk;
    out->sig = sig;
    out->enc_len = n;
    return AIRC_OK;
}

airc_status_t airc_board_id(const char *name, uint8_t id[32])
{
    static const char PFX[] = "zxv/airc/v1/board\n";
    uint8_t buf[sizeof PFX - 1 + 1 + AIRC_NAME_MAX];
    if (!name || !id || name[0] != '#') return AIRC_ERR_NAME;
    uint32_t n = 0;
    for (; n < sizeof PFX - 1; n++) buf[n] = (uint8_t) PFX[n];
    uint32_t k = 0;
    for (const char *c = name; *c; c++, k++) {
        uint8_t ch = (uint8_t) *c;
        if (k > AIRC_NAME_MAX) return AIRC_ERR_NAME;
        if (ch <= ' ' || ch == ',' || ch == ':' || ch == 0x7f) return AIRC_ERR_NAME;
        if (ch >= 'A' && ch <= 'Z') ch = (uint8_t) (ch + 32);
        buf[n++] = ch;
    }
    if (k < 2) return AIRC_ERR_NAME;
    sha3_256(buf, n, id);
    return AIRC_OK;
}

/* ---------------- trust (A14-A18) ---------------- */

void airc_trust_config_default(airc_trust_config_t *cfg)
{
    /* half the average credibility (cred.h C7: 100 = average). A peer with
     * no settled trade from anyone trusted scores 0; in a small swarm the
     * pretrusted owners keep a large share, so the average is too high a bar. */
    cfg->warm_min = 50;
    cfg->cold_burst = 4;
    cfg->cold_per_min = 6;
    cfg->warm_burst = 20;
    cfg->warm_per_min = 60;
}

airc_status_t airc_trust_init(airc_trust_t *t, const airc_trust_config_t *cfg)
{
    if (!t) return AIRC_ERR_ARG;
    zero(t, sizeof *t);
    if (cfg)
        t->cfg = *cfg;
    else
        airc_trust_config_default(&t->cfg);
    if (!t->cfg.cold_burst || !t->cfg.warm_burst) return AIRC_ERR_ARG;
    cred_config_t cc;
    cred_config_default(&cc);
    if (cred_init(&t->cred, &cc) != CRED_OK) return AIRC_ERR_ARG;
    t->next_subject = 1;
    t->overflow.tokens_milli = UINT64_MAX; /* starts full */
    return AIRC_OK;
}

static airc_peer_t *find_peer(const airc_trust_t *t, const uint8_t id[32])
{
    for (uint32_t i = 0; i < AIRC_PEERS; i++)
        if (t->peer[i].used && eq(t->peer[i].id, id, 32)) return (airc_peer_t *) &t->peer[i];
    return 0;
}

const airc_peer_t *airc_trust_peer(const airc_trust_t *t, const uint8_t id[32])
{
    return t && id ? find_peer(t, id) : 0;
}

/* A18: evictable = cold, no penalty, no fault, never traded */
static bool evictable(const airc_peer_t *p)
{
    return !p->dropped && !p->penalty && !p->subject && p->tag == 0;
}

/* the peer's slot, made if there is room or an evictable one (A18) */
static airc_peer_t *slot(airc_trust_t *t, const uint8_t id[32], uint64_t now)
{
    airc_peer_t *p = find_peer(t, id);
    if (p) return p;
    airc_peer_t *victim = 0;
    for (uint32_t i = 0; i < AIRC_PEERS; i++) {
        airc_peer_t *q = &t->peer[i];
        if (!q->used) {
            victim = q;
            break;
        }
        if (evictable(q) && (!victim || q->last_ms < victim->last_ms)) victim = q;
    }
    if (!victim) return 0;
    zero(victim, sizeof *victim);
    victim->used = true;
    cpy(victim->id, id, 32);
    victim->last_ms = now;
    victim->refill_ms = now;
    victim->tokens_milli = UINT64_MAX; /* starts full */
    return victim;
}

airc_tier_t airc_trust_tier(const airc_trust_t *t, const uint8_t id[32])
{
    const airc_peer_t *p = t && id ? find_peer(t, id) : 0;
    if (!p) return AIRC_COLD;
    if (p->dropped) return AIRC_DROPPED;
    if (p->penalty || !p->subject) return AIRC_COLD;
    return cred_credibility(&t->cred, p->subject) >= t->cfg.warm_min ? AIRC_WARM : AIRC_COLD;
}

static airc_status_t subject_of(airc_trust_t *t, airc_peer_t *p, bool pretrusted)
{
    if (p->subject)
        return pretrusted
                   ? (cred_set_pretrusted(&t->cred, p->subject, true) == CRED_OK ? AIRC_OK
                                                                                 : AIRC_ERR_ARG)
                   : AIRC_OK;
    if (cred_subject_add(&t->cred, t->next_subject, pretrusted) != CRED_OK) return AIRC_ERR_FULL;
    p->subject = t->next_subject++;
    return AIRC_OK;
}

airc_status_t airc_trust_pretrust(airc_trust_t *t, const uint8_t id[32], uint64_t now_ms)
{
    if (!t || !id) return AIRC_ERR_ARG;
    airc_peer_t *p = slot(t, id, now_ms);
    if (!p) return AIRC_ERR_FULL;
    airc_status_t st = subject_of(t, p, true);
    if (st == AIRC_OK) cred_trust_compute(&t->cred);
    return st;
}

airc_status_t airc_trust_settle(airc_trust_t *t, const uint8_t rater[32], const uint8_t ratee[32],
                                uint64_t value, const uint8_t receipt[32], uint64_t now_ms)
{
    if (!t || !rater || !ratee || !receipt || !value || eq(rater, ratee, 32)) return AIRC_ERR_ARG;
    airc_peer_t *a = slot(t, rater, now_ms);
    if (!a) return AIRC_ERR_FULL;
    a->last_ms = now_ms;
    airc_status_t st = subject_of(t, a, false);
    if (st != AIRC_OK) return st;
    airc_peer_t *b = slot(t, ratee, now_ms);
    if (!b) return AIRC_ERR_FULL;
    b->last_ms = now_ms;
    if ((st = subject_of(t, b, false)) != AIRC_OK) return st;
    cred_status_t cs = cred_fulfil(&t->cred, a->subject, b->subject, value, receipt);
    if (cs == CRED_ERR_DUP) return AIRC_ERR_DUP;
    if (cs != CRED_OK) return AIRC_ERR_ARG;
    cred_trust_compute(&t->cred);
    return AIRC_OK;
}

static void log_add(airc_trust_t *t, const uint8_t author[32], const uint8_t req[32],
                    uint8_t reason, uint64_t now)
{
    airc_log_t *l = &t->log[t->log_head];
    cpy(l->author, author, 32);
    if (req)
        cpy(l->req, req, 32);
    else
        zero(l->req, 32);
    l->reason = reason;
    l->ms = now;
    t->log_head = (t->log_head + 1) % AIRC_LOG;
    if (t->log_n < AIRC_LOG) t->log_n++;
}

/* A17: one table decides what a fault does */
static void fault(airc_trust_t *t, const uint8_t id[32], lpx_t tag, const uint8_t *ev0,
                  const uint8_t *ev1, uint64_t now)
{
    lpx_action_t act = lpx_action(tag);
    airc_peer_t *p = slot(t, id, now);
    if (act == LPX_ACT_ROLLBACK) {
        log_add(t, id, ev0, AIRC_LOG_EQUIVOCATION, now);
        if (!p) return; /* no slot to keep the drop in: the board still holds both */
        p->dropped = true;
        p->tag = tag;
        if (ev0) cpy(p->evidence[0], ev0, 32);
        if (ev1) cpy(p->evidence[1], ev1, 32);
    } else if (act == LPX_ACT_ESCALATE) {
        t->escalations++;
        log_add(t, id, ev0, AIRC_LOG_ESCALATE, now);
        if (p) p->tag = tag;
    }
}

void airc_trust_suspect(airc_trust_t *t, const uint8_t id[32], const uint8_t subject[32],
                        uint64_t now_ms)
{
    if (!t || !id) return;
    fault(t, id, lpx_make(LPX_B_BOTH, LPX_P_NEUTRAL, LPX_PH_EMERGING), subject, 0, now_ms);
}

void airc_trust_clear(airc_trust_t *t, const uint8_t id[32])
{
    airc_peer_t *p = t && id ? find_peer(t, id) : 0;
    if (!p) return;
    p->dropped = false;
    p->tag = 0;
    zero(p->evidence, sizeof p->evidence);
}

void airc_trust_epoch(airc_trust_t *t)
{
    if (!t) return;
    for (uint32_t i = 0; i < AIRC_PEERS; i++)
        if (t->peer[i].used && t->peer[i].penalty) t->peer[i].penalty--;
    cred_decay_epoch(&t->cred);
    cred_trust_compute(&t->cred);
}

static void penalise(airc_trust_t *t, const uint8_t author[32], const uint8_t req[32], uint64_t now)
{
    airc_peer_t *p = slot(t, author, now);
    log_add(t, author, req, AIRC_LOG_MISMATCH, now);
    if (!p) return;
    p->penalty =
        p->penalty + AIRC_PENALTY > AIRC_PENALTY_MAX ? AIRC_PENALTY_MAX : p->penalty + AIRC_PENALTY;
}

/* A9: one post from the author's bucket. Slotless authors share one. */
static bool take_token(airc_trust_t *t, const uint8_t id[32], uint64_t budget, uint64_t now)
{
    airc_tier_t tier = airc_trust_tier(t, id);
    airc_peer_t *p = slot(t, id, now);
    if (!p) p = &t->overflow; /* A18 */
    p->last_ms = now;
    uint64_t burst = tier == AIRC_WARM ? t->cfg.warm_burst : t->cfg.cold_burst;
    uint64_t rate = tier == AIRC_WARM ? t->cfg.warm_per_min : t->cfg.cold_per_min;
    if (budget) { /* the TPC share in permille of the average, at most x4 */
        uint64_t b = budget > 4000 ? 4000 : budget;
        burst = pay_udiv64(burst * b, 1000, 0); /* no 64-bit divide helper on 32-bit */
        rate = pay_udiv64(rate * b, 1000, 0);
        if (!burst) burst = 1;
    }
    uint64_t cap = burst * 1000;
    if (p->tokens_milli == UINT64_MAX) {
        p->tokens_milli = cap;
    } else if (now > p->refill_ms) {
        uint64_t dt = now - p->refill_ms;
        uint64_t add = dt > 3600000u
                           ? cap
                           : pay_udiv64(dt * rate, 60, 0); /* rate per minute, in milli-posts */
        p->tokens_milli = p->tokens_milli + add > cap ? cap : p->tokens_milli + add;
    }
    if (now > p->refill_ms) p->refill_ms = now;
    if (p->tokens_milli > cap) p->tokens_milli = cap;
    if (p->tokens_milli < 1000) return false;
    p->tokens_milli -= 1000;
    return true;
}

/* ---------------- the quorum gate (A10-A14) ---------------- */

uint32_t airc_tier_max(uint8_t tier)
{
    static const uint32_t KIB[AIRC_TIERS] = {5, 8, 13, 21, 34, 55, 64};
    return tier >= 1 && tier <= AIRC_TIERS ? KIB[tier - 1] * 1024u : 0;
}

void airc_req_encode(const airc_req_t *r, uint8_t out[AIRC_REQ_LEN])
{
    uint8_t *o = out;
    o[0] = 'A', o[1] = 'I', o[2] = 'R', o[3] = 'Q';
    o[4] = AIRC_VERSION;
    o[5] = r->tier;
    o += 6;
    cpy(o, r->board, 32), o += 32;
    cpy(o, r->author, 32), o += 32;
    put32(o, r->len), o += 4;
    cpy(o, r->hash, 32), o += 32;
    cpy(o, r->intent, 32), o += 32;
    put64(o, r->ms);
}

static bool req_ok(const airc_req_t *r)
{
    uint32_t max = airc_tier_max(r->tier);
    return max && r->len > AIRC_FRAME_MAX && r->len <= max;
}

airc_status_t airc_req_decode(const uint8_t *in, uint32_t len, airc_req_t *r)
{
    if (!in || !r) return AIRC_ERR_ARG;
    zero(r, sizeof *r);
    if (len != AIRC_REQ_LEN || in[0] != 'A' || in[1] != 'I' || in[2] != 'R' || in[3] != 'Q' ||
        in[4] != AIRC_VERSION)
        return AIRC_ERR_FORMAT;
    const uint8_t *q = in + 5;
    r->tier = *q++;
    cpy(r->board, q, 32), q += 32;
    cpy(r->author, q, 32), q += 32;
    r->len = get32(q), q += 4;
    cpy(r->hash, q, 32), q += 32;
    cpy(r->intent, q, 32), q += 32;
    r->ms = get64(q);
    return req_ok(r) ? AIRC_OK : AIRC_ERR_FORMAT;
}

void airc_req_id(const airc_req_t *r, uint8_t id[32])
{
    uint8_t e[AIRC_REQ_LEN];
    airc_req_encode(r, e);
    sha3_256(e, sizeof e, id);
}

uint32_t airc_req_sign(airc_req_t *r, const uint8_t pk[AIRC_PK_LEN], const uint8_t sk[AIRC_SK_LEN],
                       const uint8_t *rnd, uint8_t out[AIRC_REQ_WIRE])
{
    if (!r || !pk || !sk || !out) return 0;
    airc_key_id(pk, r->author);
    if (!req_ok(r)) return 0;
    airc_req_encode(r, out);
    pqm_mldsa87_sign(sk, out, AIRC_REQ_LEN, CTX_REQ, CTXLEN(CTX_REQ), rnd, out + AIRC_REQ_LEN);
    cpy(out + AIRC_REQ_LEN + AIRC_SIG_LEN, pk, AIRC_PK_LEN);
    return AIRC_REQ_WIRE;
}

uint32_t airc_vote_sign(const uint8_t req_id[32], uint8_t value, const uint8_t pk[AIRC_PK_LEN],
                        const uint8_t sk[AIRC_SK_LEN], const uint8_t *rnd,
                        uint8_t out[AIRC_VOTE_WIRE])
{
    if (!req_id || !pk || !sk || !out || !truth_ok(value)) return 0;
    out[0] = 'A', out[1] = 'I', out[2] = 'R', out[3] = 'V';
    cpy(out + 4, req_id, 32);
    airc_key_id(pk, out + 36);
    out[68] = value;
    pqm_mldsa87_sign(sk, out, AIRC_VOTE_LEN, CTX_VOTE, CTXLEN(CTX_VOTE), rnd, out + AIRC_VOTE_LEN);
    cpy(out + AIRC_VOTE_LEN + AIRC_SIG_LEN, pk, AIRC_PK_LEN);
    return AIRC_VOTE_WIRE;
}

void airc_gate_init(airc_gate_t *g)
{
    if (g) zero(g, sizeof *g);
}

static airc_lease_t *find_lease(airc_gate_t *g, const uint8_t id[32])
{
    for (uint32_t i = 0; i < AIRC_REQS; i++)
        if (g->lease[i].used && eq(g->lease[i].id, id, 32)) return &g->lease[i];
    return 0;
}

airc_status_t airc_gate_request(airc_gate_t *g, const airc_trust_t *t, const uint8_t *wire,
                                uint32_t len, uint64_t now_ms, uint8_t id[32])
{
    if (!g || !wire || !id) return AIRC_ERR_ARG;
    if (len != AIRC_REQ_WIRE) return AIRC_ERR_FORMAT;
    airc_req_t r;
    airc_status_t st = airc_req_decode(wire, AIRC_REQ_LEN, &r);
    if (st != AIRC_OK) return st;
    const uint8_t *sig = wire + AIRC_REQ_LEN, *pk = sig + AIRC_SIG_LEN;
    if (!pqm_mldsa87_verify(pk, wire, AIRC_REQ_LEN, CTX_REQ, CTXLEN(CTX_REQ), sig))
        return AIRC_ERR_SIG;
    uint8_t kid[32];
    airc_key_id(pk, kid);
    if (!eq(kid, r.author, 32)) return AIRC_ERR_AUTHOR;
    if (t && airc_trust_tier(t, r.author) == AIRC_DROPPED) return AIRC_ERR_DROPPED;
    airc_req_id(&r, id);
    if (find_lease(g, id)) return AIRC_ERR_DUP;
    airc_lease_t *free_l = 0;
    for (uint32_t i = 0; i < AIRC_REQS; i++) {
        airc_lease_t *l = &g->lease[i];
        bool stale =
            l->used && (l->spent || l->verdict == AIRC_REFUSED ||
                        (l->verdict == AIRC_GRANTED && now_ms > l->granted_ms + AIRC_LEASE_MS));
        if (!l->used || stale) {
            free_l = l;
            break;
        }
    }
    if (!free_l) return AIRC_ERR_FULL;
    zero(free_l, sizeof *free_l);
    free_l->used = true;
    free_l->r = r;
    cpy(free_l->id, id, 32);
    return AIRC_OK;
}

airc_status_t airc_gate_vote(airc_gate_t *g, const airc_trust_t *t, const uint8_t *wire,
                             uint32_t len, uint64_t weight)
{
    if (!g || !wire) return AIRC_ERR_ARG;
    if (len != AIRC_VOTE_WIRE || wire[0] != 'A' || wire[1] != 'I' || wire[2] != 'R' ||
        wire[3] != 'V' || !truth_ok(wire[68]))
        return AIRC_ERR_FORMAT;
    if (!weight) return AIRC_ERR_ARG; /* A11: not in the swarm */
    const uint8_t *sig = wire + AIRC_VOTE_LEN, *pk = sig + AIRC_SIG_LEN;
    if (!pqm_mldsa87_verify(pk, wire, AIRC_VOTE_LEN, CTX_VOTE, CTXLEN(CTX_VOTE), sig))
        return AIRC_ERR_SIG;
    uint8_t kid[32];
    airc_key_id(pk, kid);
    if (!eq(kid, wire + 36, 32)) return AIRC_ERR_AUTHOR;
    if (t && airc_trust_tier(t, kid) == AIRC_DROPPED) return AIRC_ERR_DROPPED;
    airc_lease_t *l = find_lease(g, wire + 4);
    if (!l) return AIRC_ERR_LEASE;
    if (weight > AIRC_WEIGHT_MAX) weight = AIRC_WEIGHT_MAX;
    for (uint32_t i = 0; i < l->n; i++)
        if (eq(l->b[i].voter, kid, 32)) {
            if (l->b[i].value != wire[68]) l->b[i].value = AIRC_B; /* A11: two votes */
            return AIRC_OK;
        }
    if (l->n >= AIRC_VOTERS) return AIRC_ERR_FULL;
    airc_ballot_t *b = &l->b[l->n++];
    cpy(b->voter, kid, 32);
    b->value = wire[68];
    b->weight = weight;
    return AIRC_OK;
}

airc_verdict_t airc_gate_tally(airc_gate_t *g, const airc_trust_t *t, const uint8_t id[32],
                               uint64_t total_weight, uint64_t now_ms)
{
    airc_lease_t *l = g && id ? find_lease(g, id) : 0;
    if (!l) return AIRC_REFUSED;
    if (l->verdict == AIRC_GRANTED || l->verdict == AIRC_REFUSED) return l->verdict;
    uint64_t yes = 0, no = 0, hold = 0, sum = 0;
    for (uint32_t i = 0; i < l->n; i++) {
        uint64_t w = l->b[i].weight;
        sum += w;
        if (l->b[i].value == AIRC_T)
            yes += w;
        else if (l->b[i].value == AIRC_F)
            no += w;
        else
            hold += w;
    }
    if (total_weight < sum) total_weight = sum; /* the swarm is at least who voted */
    bool warm = t && airc_trust_tier(t, l->r.author) == AIRC_WARM;
    uint64_t num = warm ? 1 : 2, den = 3;
    if (!total_weight) {
        l->verdict = AIRC_PENDING;
    } else if (hold) {
        l->verdict = AIRC_HELD; /* A12: BOTH or NEITHER never counts as yes */
    } else if (yes * den >= num * total_weight) {
        l->verdict = AIRC_GRANTED;
        l->granted_ms = now_ms;
    } else if ((total_weight - no) * den < num * total_weight) {
        l->verdict = AIRC_REFUSED;
    } else {
        l->verdict = AIRC_PENDING;
    }
    return l->verdict;
}

/* ---------------- boards (A6-A9, A13, A14, A17) ---------------- */

airc_status_t airc_board_init(airc_board_t *b, const char *name)
{
    if (!b) return AIRC_ERR_ARG;
    zero(b, sizeof *b);
    return airc_board_id(name, b->id);
}

const airc_entry_t *airc_board_find(const airc_board_t *b, const uint8_t id[32])
{
    for (uint32_t i = 0; b && id && i < AIRC_BOARD_POSTS; i++)
        if (b->e[i].used && eq(b->e[i].id, id, 32)) return &b->e[i];
    return 0;
}

static airc_entry_t *room(airc_board_t *b)
{
    airc_entry_t *v = 0;
    for (uint32_t i = 0; i < AIRC_BOARD_POSTS; i++) {
        airc_entry_t *e = &b->e[i];
        if (!e->used) return e;
        if (!v || e->ms < v->ms || (e->ms == v->ms && cmp32(e->id, v->id) < 0)) v = e;
    }
    return v; /* A7: the oldest makes room */
}

airc_status_t airc_board_accept(airc_board_t *b, airc_trust_t *t, airc_gate_t *g,
                                const airc_signed_t *s, uint64_t budget, uint64_t now_ms,
                                bool *held)
{
    if (!b || !t || !s) return AIRC_ERR_ARG;
    if (held) *held = false;
    const airc_post_t *p = &s->p;
    if (!eq(p->board, b->id, 32)) return AIRC_ERR_BOARD;
    if (airc_board_find(b, s->id)) return AIRC_ERR_DUP;
    if (airc_trust_tier(t, p->author) == AIRC_DROPPED) {
        b->refused++;
        return AIRC_ERR_DROPPED;
    }
    /* A13: a jumbo post spends its lease, checked every time */
    if (p->body_len > AIRC_FRAME_MAX) {
        airc_lease_t *l = g ? find_lease(g, p->lease) : 0;
        if (!l || l->verdict != AIRC_GRANTED || l->spent ||
            now_ms > l->granted_ms + AIRC_LEASE_MS || !eq(l->r.board, p->board, 32) ||
            !eq(l->r.author, p->author, 32)) {
            b->refused++;
            return AIRC_ERR_LEASE;
        }
        l->spent = true;
        uint8_t h[32];
        sha3_256(p->body, p->body_len, h);
        if (l->r.len != p->body_len || !eq(h, l->r.hash, 32)) {
            penalise(t, p->author, l->id, now_ms); /* A14 */
            b->refused++;
            return AIRC_ERR_MISMATCH;
        }
    }
    /* A8: the same author and seq, another post */
    airc_entry_t *twin = 0;
    for (uint32_t i = 0; i < AIRC_BOARD_POSTS; i++) {
        airc_entry_t *e = &b->e[i];
        if (e->used && e->seq == p->seq && eq(e->author, p->author, 32)) twin = e;
    }
    if (!twin && !take_token(t, p->author, budget, now_ms)) {
        b->refused++;
        return AIRC_ERR_RATE;
    }
    airc_entry_t *e = room(b);
    uint8_t twin_id[32];
    if (twin) cpy(twin_id, twin->id, 32);
    if (e == twin) { /* the only room is the twin's: keep it, held, and refuse no evidence */
        twin->held = true;
    } else {
        zero(e, sizeof *e);
        e->used = true;
        cpy(e->id, s->id, 32);
        cpy(e->author, p->author, 32);
        e->seq = p->seq;
        e->ms = p->ms;
        e->kind = p->kind;
        e->truth = p->truth;
    }
    if (twin) {
        twin->held = true;
        e->held = true;
        b->held++;
        if (held) *held = true;
        fault(t, p->author, lpx_make(LPX_B_BOTH, LPX_P_FORBIDDEN, LPX_PH_MANIFEST), twin_id, s->id,
              now_ms);
    }
    b->accepted++;
    return AIRC_OK;
}

/* ---------------- shared improvements (A19) ---------------- */

static bool listed(const uint8_t (*ids)[32], uint32_t n, const uint8_t id[32])
{
    for (uint32_t i = 0; ids && i < n; i++)
        if (eq(ids[i], id, 32)) return true;
    return false;
}

airc_install_t airc_offer_decide(const airc_signed_t *offer, const uint8_t (*publishers)[32],
                                 uint32_t n_publishers, const uint8_t (*reviewers)[32],
                                 uint32_t n_reviewers, const airc_signed_t *reviews,
                                 uint32_t n_reviews)
{
    if (!offer || offer->p.kind != AIRC_OFFER || offer->p.nrefs < 1) return AIRC_INSTALL_NO;
    if (!listed(publishers, n_publishers, offer->p.author)) return AIRC_INSTALL_UNTRUSTED;
    bool yes = false, no = false;
    for (uint32_t i = 0; reviews && i < n_reviews; i++) {
        const airc_post_t *r = &reviews[i].p;
        if (r->kind != AIRC_REVIEW || !eq(r->parent, offer->id, 32) ||
            !eq(r->refs[0].hash, offer->p.refs[0].hash, 32) ||
            !listed(reviewers, n_reviewers, r->author))
            continue;
        if (r->truth == AIRC_T) yes = true;
        if (r->truth == AIRC_F) no = true;
    }
    if (no) return AIRC_INSTALL_REJECTED; /* a rejection wins */
    return yes ? AIRC_INSTALL_ALLOWED : AIRC_INSTALL_UNREVIEWED;
}
