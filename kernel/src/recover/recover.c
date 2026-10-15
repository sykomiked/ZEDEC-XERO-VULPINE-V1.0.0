/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* recover.c — the recovery framework: refuse, roll back, isolate, each with
 * a signed RecoveryProof. See recover.h. Crypto-free: signing is the
 * caller's callback (recover_pq.c binds ML-DSA-87). */
#include "recover.h"
#include "../robin_debanks/sha256.h"

/* ---- hashing helpers (little-endian integers) ---- */
static void h_str(sha256_ctx_t *c, const char *s)
{
    uint32_t n = 0;
    while (s[n]) n++;
    sha256_update(c, (const uint8_t *) s, n);
}

static void h_u8(sha256_ctx_t *c, uint8_t v)
{
    sha256_update(c, &v, 1);
}

static void h_u32(sha256_ctx_t *c, uint32_t v)
{
    uint8_t b[4];
    for (uint32_t i = 0; i < 4; i++) b[i] = (uint8_t) (v >> (8 * i));
    sha256_update(c, b, 4);
}

static void h_u64(sha256_ctx_t *c, uint64_t v)
{
    uint8_t b[8];
    for (uint32_t i = 0; i < 8; i++) b[i] = (uint8_t) (v >> (8 * i));
    sha256_update(c, b, 8);
}

static bool eq32(const uint8_t *a, const uint8_t *b)
{
    uint8_t d = 0;
    for (uint32_t i = 0; i < PDX_HASH; i++) d |= (uint8_t) (a[i] ^ b[i]);
    return d == 0;
}

static void cp32(uint8_t *dst, const uint8_t *src)
{
    for (uint32_t i = 0; i < PDX_HASH; i++) dst[i] = src ? src[i] : 0;
}

static void sig_digest(const uint8_t *sig, uint32_t len, uint8_t out[PDX_HASH])
{
    sha256_ctx_t c;
    sha256_init(&c);
    sha256_update(&c, sig, len);
    sha256_final(&c, out);
}

static bool log_room(const pdx_log_t *l, uint32_t k)
{
    return l->n <= l->cap && l->cap - l->n >= k;
}

static bool q_room(const lpx_qlog_t *q)
{
    return q->n < q->cap;
}

/* ---- names ---- */
const char *rcv_action_name(uint8_t a)
{
    static const char *const n[3] = {"refuse", "roll-back", "isolate"};
    return a < 3u ? n[a] : "?";
}

const char *rcv_out_name(rcv_out_t o)
{
    static const char *const n[7] = {"refused", "rolled-back", "isolated", "unsigned",
                                     "full",    "halted",      "bad-input"};
    return (unsigned) o < 7u ? n[o] : "?";
}

/* ---- shadow state ---- */
bool rcv_shadow_init(rcv_shadow_t *sh, uint8_t *slot_a, uint8_t *slot_b, uint32_t len)
{
    if (!sh || !slot_a || !slot_b || slot_a == slot_b || len == 0) return false;
    sh->slot[0] = slot_a;
    sh->slot[1] = slot_b;
    sh->len = len;
    for (uint32_t i = 0; i < 2; i++) {
        cp32(sh->digest[i], 0);
        sh->gen[i] = 0;
        sh->valid[i] = 0;
    }
    sh->good = RCV_NO_SLOT;
    sh->next_gen = 1;
    return true;
}

bool rcv_init(rcv_t *rc, pdx_log_t *log, lpx_qlog_t *q, rcv_shadow_t *shadow, uint32_t *iso,
              uint32_t iso_cap, pdx_invariant_fn hard, void *hard_ctx, rcv_sign_fn sign,
              void *sign_ctx)
{
    if (!rc) return false;
    rc->log = log;
    rc->q = q;
    rc->shadow = shadow;
    rc->iso = iso;
    rc->iso_cap = iso ? iso_cap : 0;
    rc->iso_n = 0;
    rc->hard = hard;
    rc->hard_ctx = hard_ctx;
    rc->sign = sign;
    rc->sign_ctx = sign_ctx;
    /* Without logs or a signer nothing could be proven: start halted. */
    rc->halted = !log || !q || !iso || !sign;
    return !rc->halted;
}

bool rcv_isolated(const rcv_t *rc, uint32_t component)
{
    if (!rc || !rc->iso) return false;
    for (uint32_t i = 0; i < rc->iso_n && i < rc->iso_cap; i++)
        if (rc->iso[i] == component) return true;
    return false;
}

/* Every hard invariant (the framework's and the case's) holds on s. */
static bool hard_ok(const rcv_t *rc, const rcv_case_t *k, const uint8_t *s, uint32_t len)
{
    if (rc->hard && !rc->hard(s, len, rc->hard_ctx)) return false;
    if (k->inv && !k->inv(s, len, k->inv_ctx)) return false;
    return true;
}

/* Both invariants as one pdx_invariant_fn, for pdx_destructive's watch. */
typedef struct {
    const rcv_t *rc;
    const rcv_case_t *k;
} both_t;

static bool both_inv(const uint8_t *s, uint32_t len, void *ctx)
{
    const both_t *b = (const both_t *) ctx;
    return hard_ok(b->rc, b->k, s, len);
}

/* The anomaly's canonical description: component || code (u32 LE). */
static void pattern_of(const rcv_case_t *k, uint8_t p[8])
{
    for (uint32_t i = 0; i < 4; i++) {
        p[i] = (uint8_t) (k->component >> (8 * i));
        p[4 + i] = (uint8_t) (k->code >> (8 * i));
    }
}

static void what_hash(uint8_t action, uint32_t component, uint32_t len, pdx_repair_t *r)
{
    sha256_ctx_t c;
    sha256_init(&c);
    h_str(&c, "zxv/rcv-what/v1");
    h_u8(&c, action);
    h_u32(&c, r->trips);
    h_u32(&c, component);
    h_u32(&c, len);
    sha256_update(&c, r->before, PDX_HASH);
    sha256_update(&c, r->after, PDX_HASH);
    sha256_update(&c, r->sig, PDX_HASH);
    sha256_final(&c, r->what);
}

void rcv_proof_digest(const rcv_proof_t *p, uint8_t out[PDX_HASH])
{
    sha256_ctx_t c;
    sha256_init(&c);
    h_str(&c, "zxv/rcv-proof/v1");
    h_u8(&c, p->action);
    h_u8(&c, p->outcome);
    h_u8(&c, p->why);
    h_u8(&c, p->tag);
    h_u32(&c, p->component);
    h_u32(&c, p->code);
    h_u32(&c, p->repair.trips);
    h_u32(&c, p->repair.steps);
    h_u64(&c, p->gen);
    h_u64(&c, p->repair.qseq);
    h_u64(&c, p->seq);
    sha256_update(&c, p->prev, PDX_HASH);
    sha256_update(&c, p->repair.what, PDX_HASH);
    sha256_update(&c, p->repair.before, PDX_HASH);
    sha256_update(&c, p->repair.after, PDX_HASH);
    sha256_update(&c, p->repair.sig, PDX_HASH);
    sha256_final(&c, out);
}

static rcv_out_t halt(rcv_t *rc, rcv_out_t o)
{
    rc->halted = true;
    return o;
}

/* Fill the recovery fields, sign, and append the RCV record. The caller
 * has checked that the log has room for it. */
static rcv_out_t finish(rcv_t *rc, const rcv_case_t *k, uint8_t action, uint8_t why, uint64_t gen,
                        rcv_proof_t *p)
{
    p->action = action;
    p->outcome = action; /* REFUSED, ROLLED_BACK, ISOLATED line up with the actions */
    p->why = why;
    p->tag = k->tag;
    p->component = k->component;
    p->code = k->code;
    p->gen = gen;
    p->seq = rc->log->n;
    cp32(p->prev, rc->log->head);
    rcv_proof_digest(p, p->msg);
    uint32_t n = rc->sign(rc->sign_ctx, p->msg, p->sig, RCV_SIG_MAX);
    bool ok = n > 0 && n <= RCV_SIG_MAX;
    p->sig_len = ok ? n : 0;
    uint8_t sd[PDX_HASH] = {0};
    if (ok) sig_digest(p->sig, p->sig_len, sd);
    uint8_t out = ok ? p->outcome : (uint8_t) RCV_OUT_UNSIGNED;
    pdx_log_append(rc->log, (uint8_t) (RCV_OP_REFUSE + action), why, k->tag, out, k->component,
                   p->msg, sd);
    cp32(p->head, rc->log->head);
    if (!ok) {
        p->outcome = RCV_OUT_UNSIGNED;
        return halt(rc, RCV_OUT_UNSIGNED);
    }
    return (rcv_out_t) p->outcome;
}

static void repair_clear(pdx_repair_t *r)
{
    cp32(r->what, 0);
    cp32(r->before, 0);
    cp32(r->after, 0);
    cp32(r->sig, 0);
    r->steps = 0;
    r->trips = 0;
    r->qseq = RCV_NO_QSEQ;
}

static rcv_out_t do_refuse(rcv_t *rc, const rcv_case_t *k, const uint8_t *state, uint32_t len,
                           uint8_t why, rcv_proof_t *p)
{
    uint32_t trips = lpx_forbidden(k->tag) ? (uint32_t) PDX_TRIP_FORBIDDEN : 0u;
    if (state && !hard_ok(rc, k, state, len)) trips |= PDX_TRIP_INVARIANT;
    if (!log_room(rc->log, 1) || (trips && !q_room(rc->q))) return halt(rc, RCV_OUT_FULL);
    repair_clear(&p->repair);
    pdx_state_hash(state, state ? len : 0, p->repair.before);
    cp32(p->repair.after, p->repair.before);
    uint8_t pat[8];
    pattern_of(k, pat);
    pdx_signature(pat, sizeof pat, p->repair.sig);
    p->repair.trips = trips;
    what_hash(RCV_ACT_REFUSE, k->component, state ? len : 0, &p->repair);
    if (trips) {
        p->repair.qseq = rc->q->n;
        lpx_qlog_append(rc->q, lpx_forbid(k->tag), (uint8_t) trips, k->component, p->repair.what);
    }
    return finish(rc, k, RCV_ACT_REFUSE, why, 0, p);
}

static rcv_out_t do_isolate(rcv_t *rc, const rcv_case_t *k, uint8_t why, rcv_proof_t *p)
{
    bool already = rcv_isolated(rc, k->component);
    if (!log_room(rc->log, 1) || !q_room(rc->q) || (!already && rc->iso_n >= rc->iso_cap))
        return halt(rc, RCV_OUT_FULL);
    repair_clear(&p->repair);
    uint8_t pat[8];
    pattern_of(k, pat);
    pdx_signature(pat, sizeof pat, p->repair.sig);
    p->repair.trips = lpx_forbidden(k->tag) ? (uint32_t) PDX_TRIP_FORBIDDEN : 0u;
    what_hash(RCV_ACT_ISOLATE, k->component, 0, &p->repair);
    p->repair.qseq = rc->q->n;
    lpx_qlog_append(rc->q, lpx_forbid(k->tag), (uint8_t) p->repair.trips, k->component,
                    p->repair.what);
    if (!already) rc->iso[rc->iso_n++] = k->component;
    return finish(rc, k, RCV_ACT_ISOLATE, why, 0, p);
}

/* The newest usable checkpoint slot, or RCV_NO_SLOT with *why set. A slot
 * whose bytes no longer match its digest is marked bad for good. */
static uint32_t pick_slot(rcv_t *rc, const rcv_case_t *k, uint32_t len, uint8_t *why)
{
    rcv_shadow_t *sh = rc->shadow;
    *why = RCV_WHY_NO_CHECKPOINT;
    if (!sh || sh->len != len || !sh->slot[0] || !sh->slot[1]) return RCV_NO_SLOT;
    uint32_t order[2] = {0, 1};
    if (sh->gen[1] > sh->gen[0]) {
        order[0] = 1;
        order[1] = 0;
    }
    bool tampered = false, broken = false;
    for (uint32_t j = 0; j < 2; j++) {
        uint32_t i = order[j];
        if (!sh->valid[i]) continue;
        uint8_t d[PDX_HASH];
        pdx_state_hash(sh->slot[i], len, d);
        if (!eq32(d, sh->digest[i])) {
            sh->valid[i] = 0;
            if (sh->good == i) sh->good = RCV_NO_SLOT;
            tampered = true;
            continue;
        }
        if (!hard_ok(rc, k, sh->slot[i], len)) {
            broken = true;
            continue;
        }
        return i;
    }
    if (broken)
        *why = RCV_WHY_BROKEN;
    else if (tampered)
        *why = RCV_WHY_TAMPERED;
    return RCV_NO_SLOT;
}

static rcv_out_t do_rollback(rcv_t *rc, const rcv_case_t *k, uint8_t *state, uint32_t len,
                             uint32_t slot, uint8_t why, rcv_proof_t *p)
{
    if (!log_room(rc->log, 2) || !q_room(rc->q)) return halt(rc, RCV_OUT_FULL);
    pdx_class_t c = pdx_classify(k->tag, 0, 0, 0);
    c.kind = PDX_KIND_DESTRUCTIVE; /* a rollback is paradox's containment */
    both_t b = {rc, k};
    pdx_watch_t w = {0, 0, 0, 0, 0, 0, both_inv, &b};
    uint8_t pat[8];
    pattern_of(k, pat);
    pdx_out_t o = pdx_destructive(&c, &w, state, rc->shadow->slot[slot], len, pat, sizeof pat,
                                  k->component, rc->q, rc->log, &p->repair);
    if (o != PDX_OUT_CONTAINED) return halt(rc, RCV_OUT_FULL); /* room was checked */
    return finish(rc, k, RCV_ACT_ROLLBACK, why, rc->shadow->gen[slot], p);
}

/* ---- public actions ---- */
bool rcv_checkpoint(rcv_t *rc, const rcv_case_t *k, const uint8_t *state, uint32_t len,
                    uint64_t *gen_out)
{
    if (!rc || !k || !state || len == 0 || rc->halted) return false;
    rcv_shadow_t *sh = rc->shadow;
    if (!sh || sh->len != len || !sh->slot[0] || !sh->slot[1]) return false;
    if (lpx_forbidden(k->tag) || !hard_ok(rc, k, state, len)) return false;
    if (!log_room(rc->log, 1)) {
        rc->halted = true;
        return false;
    }
    uint32_t dst;
    if (sh->good < 2)
        dst = 1u - sh->good;
    else if (!sh->valid[0])
        dst = 0;
    else if (!sh->valid[1])
        dst = 1;
    else
        dst = sh->gen[0] <= sh->gen[1] ? 0u : 1u;
    uint8_t want[PDX_HASH], got[PDX_HASH];
    pdx_state_hash(state, len, want); /* the state as checked, before the copy */
    sh->valid[dst] = 0;
    for (uint32_t i = 0; i < len; i++) sh->slot[dst][i] = state[i];
    pdx_state_hash(sh->slot[dst], len, got); /* read back */
    if (!eq32(want, got)) return false;      /* torn: the known-good slot stays */
    cp32(sh->digest[dst], want);
    sh->gen[dst] = sh->next_gen++;
    sh->valid[dst] = 1;
    sh->good = dst;
    pdx_log_append(rc->log, RCV_OP_CHECKPOINT, 0, k->tag, 0, dst, want, 0);
    if (gen_out) *gen_out = sh->gen[dst];
    return true;
}

rcv_out_t rcv_refuse(rcv_t *rc, const rcv_case_t *k, const uint8_t *state, uint32_t len,
                     rcv_proof_t *proof)
{
    if (!rc || !k || !proof || (!state && len)) return RCV_OUT_BAD_INPUT;
    if (rc->halted) return RCV_OUT_HALTED;
    return do_refuse(rc, k, state, len, RCV_WHY_ASKED, proof);
}

rcv_out_t rcv_rollback(rcv_t *rc, const rcv_case_t *k, uint8_t *state, uint32_t len,
                       rcv_proof_t *proof)
{
    if (!rc || !k || !proof || !state || len == 0) return RCV_OUT_BAD_INPUT;
    if (rc->halted) return RCV_OUT_HALTED;
    if (rcv_isolated(rc, k->component))
        return do_refuse(rc, k, state, len, RCV_WHY_ISOLATED, proof);
    uint8_t why;
    uint32_t slot = pick_slot(rc, k, len, &why);
    if (slot == RCV_NO_SLOT) return do_refuse(rc, k, state, len, why, proof);
    return do_rollback(rc, k, state, len, slot, RCV_WHY_ASKED, proof);
}

rcv_out_t rcv_isolate(rcv_t *rc, const rcv_case_t *k, rcv_proof_t *proof)
{
    if (!rc || !k || !proof) return RCV_OUT_BAD_INPUT;
    if (rc->halted) return RCV_OUT_HALTED;
    return do_isolate(rc, k, RCV_WHY_ASKED, proof);
}

rcv_out_t rcv_recover(rcv_t *rc, const rcv_case_t *k, uint8_t *state, uint32_t len,
                      rcv_proof_t *proof)
{
    if (!rc || !k || !proof || (!state && len)) return RCV_OUT_BAD_INPUT;
    if (rc->halted) return RCV_OUT_HALTED;
    if (rcv_isolated(rc, k->component))
        return do_refuse(rc, k, state, len, RCV_WHY_ISOLATED, proof);
    bool broken = lpx_forbidden(k->tag) || (state && !hard_ok(rc, k, state, len));
    if (!broken) return do_refuse(rc, k, state, len, RCV_WHY_SOFT, proof);
    uint8_t why = RCV_WHY_NO_CHECKPOINT;
    if (state && len) {
        uint32_t slot = pick_slot(rc, k, len, &why);
        if (slot != RCV_NO_SLOT) return do_rollback(rc, k, state, len, slot, RCV_WHY_HARD, proof);
    }
    return do_isolate(rc, k, why, proof);
}

/* ---- verification ---- */
bool rcv_proof_verify(const rcv_proof_t *p, rcv_verify_fn verify, void *ctx, const uint8_t *pk,
                      uint32_t pk_len)
{
    if (!p || !verify || p->sig_len == 0 || p->sig_len > RCV_SIG_MAX) return false;
    if (p->action > RCV_ACT_ISOLATE || p->outcome != p->action) return false;
    uint8_t m[PDX_HASH];
    rcv_proof_digest(p, m);
    if (!eq32(m, p->msg)) return false;
    return verify(ctx, pk, pk_len, m, p->sig, p->sig_len);
}

bool rcv_proof_in_log(const pdx_log_t *log, const rcv_proof_t *p)
{
    if (!log || !p || p->sig_len == 0 || p->sig_len > RCV_SIG_MAX) return false;
    if (p->action > RCV_ACT_ISOLATE || p->outcome != p->action) return false;
    if (!pdx_log_verify(log) || p->seq >= log->n) return false;
    uint8_t m[PDX_HASH], sd[PDX_HASH];
    rcv_proof_digest(p, m);
    sig_digest(p->sig, p->sig_len, sd);
    const pdx_rec_t *r = &log->rec[p->seq];
    if (r->op != RCV_OP_REFUSE + p->action || r->kind != p->why || r->tag != p->tag ||
        r->outcome != p->outcome || r->aux != p->component || !eq32(m, p->msg) || !eq32(r->a, m) ||
        !eq32(r->b, sd) || !eq32(r->prev, p->prev) || !eq32(r->hash, p->head))
        return false;
    if (p->action == RCV_ACT_ROLLBACK) {
        if (p->seq == 0) return false;
        const pdx_rec_t *d = &log->rec[p->seq - 1];
        if (d->op != PDX_OP_DESTRUCTIVE || d->outcome != PDX_OUT_CONTAINED ||
            !eq32(d->a, p->repair.what) || !eq32(d->b, p->repair.sig))
            return false;
    }
    return true;
}
