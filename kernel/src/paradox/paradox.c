/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* paradox.c — the paradox engine: log, registry, classifier (P0.1) and the
 * four operators (P0.2). See paradox.h. The graph (P0.4) is paradox_graph.c. */
#include "paradox.h"
#include "../robin_debanks/sha256.h"

/* ---- hashing helpers (little-endian integers) ---- */
static void h_str(sha256_ctx_t *c, const char *s)
{
    uint32_t n = 0;
    while (s[n]) n++;
    sha256_update(c, (const uint8_t *) s, n);
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

/* ---- names and actions ---- */
lpx_action_t pdx_out_action(pdx_out_t o)
{
    static const uint8_t m[] = {
        LPX_ACT_PROCEED,  /* APPLIED */
        LPX_ACT_PROCEED,  /* KEPT */
        LPX_ACT_ESCALATE, /* PROPOSAL_FOR_OWNER */
        LPX_ACT_ROLLBACK, /* ROLLED_BACK */
        LPX_ACT_ROLLBACK, /* CONTAINED */
        LPX_ACT_WAIT,     /* DEFERRED */
        LPX_ACT_PROCEED,  /* SETTLED */
        LPX_ACT_WAIT,     /* RECORDED */
        LPX_ACT_PROCEED,  /* ACCEPTED */
        LPX_ACT_WAIT,     /* PENDING */
        LPX_ACT_PROCEED,  /* RESOLVED */
        LPX_ACT_ESCALATE, /* ESCALATE_TO_OWNER */
        LPX_ACT_REFUSE,   /* REFUSED */
        LPX_ACT_REFUSE,   /* LOG_FULL */
    };
    return (unsigned) o < sizeof m ? (lpx_action_t) m[o] : LPX_ACT_REFUSE;
}

const char *pdx_kind_name(uint8_t kind)
{
    static const char *const n[5] = {"constructive", "destructive", "neutral", "special", "none"};
    return kind < 5u ? n[kind] : "?";
}

const char *pdx_out_name(pdx_out_t o)
{
    static const char *const n[14] = {"applied",     "kept",      "proposal-for-owner",
                                      "rolled-back", "contained", "deferred",
                                      "settled",     "recorded",  "accepted",
                                      "pending",     "resolved",  "escalate-to-owner",
                                      "refused",     "log-full"};
    return (unsigned) o < 14u ? n[o] : "?";
}

/* ---- the operator log ---- */
void pdx_log_init(pdx_log_t *l, pdx_rec_t *storage, uint32_t cap)
{
    if (!l) return;
    l->rec = storage;
    l->cap = storage ? cap : 0;
    l->n = 0;
    cp32(l->head, 0);
}

/* SHA-256 over "zxv/pdx-log/v1" || seq || op || kind || tag || outcome ||
 * aux || a || b || prev */
static void rec_hash(const pdx_rec_t *r, uint8_t out[PDX_HASH])
{
    sha256_ctx_t c;
    sha256_init(&c);
    h_str(&c, "zxv/pdx-log/v1");
    h_u64(&c, r->seq);
    uint8_t f[4] = {r->op, r->kind, r->tag, r->outcome};
    sha256_update(&c, f, 4);
    h_u32(&c, r->aux);
    sha256_update(&c, r->a, PDX_HASH);
    sha256_update(&c, r->b, PDX_HASH);
    sha256_update(&c, r->prev, PDX_HASH);
    sha256_final(&c, out);
}

static bool log_room(const pdx_log_t *l, uint32_t k)
{
    return l->n <= l->cap && l->cap - l->n >= k;
}

bool pdx_log_append(pdx_log_t *l, uint8_t op, uint8_t kind, lpx_t tag, uint8_t outcome,
                    uint32_t aux, const uint8_t a[PDX_HASH], const uint8_t b[PDX_HASH])
{
    if (!l || !log_room(l, 1)) return false;
    pdx_rec_t *r = &l->rec[l->n];
    r->seq = l->n;
    r->op = op;
    r->kind = kind;
    r->tag = tag;
    r->outcome = outcome;
    r->aux = aux;
    cp32(r->a, a);
    cp32(r->b, b);
    cp32(r->prev, l->head);
    rec_hash(r, r->hash);
    cp32(l->head, r->hash);
    l->n++;
    return true;
}

bool pdx_log_verify(const pdx_log_t *l)
{
    if (!l || l->n > l->cap) return false;
    uint8_t prev[PDX_HASH] = {0}, h[PDX_HASH];
    for (uint32_t k = 0; k < l->n; k++) {
        const pdx_rec_t *r = &l->rec[k];
        if (r->seq != k || !eq32(r->prev, prev)) return false;
        rec_hash(r, h);
        if (!eq32(h, r->hash)) return false;
        cp32(prev, h);
    }
    return eq32(l->head, prev);
}

/* ---- P0.1 ---- */
void pdx_signature(const uint8_t *data, size_t len, uint8_t out[PDX_HASH])
{
    sha256_ctx_t c;
    sha256_init(&c);
    h_str(&c, "zxv/pdx-sig/v1");
    if (data) sha256_update(&c, data, len);
    sha256_final(&c, out);
}

static bool registry_kind(uint8_t kind)
{
    return kind == PDX_KIND_CONSTRUCTIVE || kind == PDX_KIND_DESTRUCTIVE ||
           kind == PDX_KIND_NEUTRAL;
}

void pdx_reg_init(pdx_registry_t *r, pdx_sig_t *storage, uint32_t cap)
{
    if (!r) return;
    r->ent = storage;
    r->cap = storage ? cap : 0;
    r->n = 0;
}

int32_t pdx_reg_find(const pdx_registry_t *r, const uint8_t sig[PDX_HASH])
{
    if (!r || !sig) return -1;
    for (uint32_t i = 0; i < r->n; i++)
        if (eq32(r->ent[i].sig, sig)) return (int32_t) i;
    return -1;
}

bool pdx_reg_add(pdx_registry_t *r, const uint8_t sig[PDX_HASH], uint8_t kind, uint8_t reason,
                 uint8_t flags, uint64_t src)
{
    if (!r || !sig || !registry_kind(kind) || r->n >= r->cap || pdx_reg_find(r, sig) >= 0)
        return false;
    pdx_sig_t *e = &r->ent[r->n];
    cp32(e->sig, sig);
    e->kind = kind;
    e->reason = reason;
    e->flags = flags;
    e->src = src;
    r->n++;
    return true;
}

uint8_t pdx_kind_of_tag(lpx_t t)
{
    uint8_t base = lpx_base(t), pol = lpx_pol(t);
    if (pol == LPX_P_FORBIDDEN) return PDX_KIND_NONE;
    if (base == LPX_B_NEITHER) return PDX_KIND_NEUTRAL;
    if (base != LPX_B_BOTH) return PDX_KIND_NONE;
    if (pol == LPX_P_NEGENTROPIC) return PDX_KIND_CONSTRUCTIVE;
    if (pol == LPX_P_ENTROPIC) return PDX_KIND_DESTRUCTIVE;
    return lpx_phase(t) == LPX_PH_POTENTIAL ? PDX_KIND_SPECIAL : PDX_KIND_NEUTRAL;
}

static uint8_t kind_pol(uint8_t kind)
{
    if (kind == PDX_KIND_CONSTRUCTIVE) return LPX_P_NEGENTROPIC;
    if (kind == PDX_KIND_DESTRUCTIVE) return LPX_P_ENTROPIC;
    return LPX_P_NEUTRAL;
}

pdx_class_t pdx_classify(lpx_t tag, const pdx_evidence_t *ev, const pdx_registry_t *reg,
                         const uint8_t sig[PDX_HASH])
{
    pdx_class_t c;
    tag = (lpx_t) (tag & (LPX_STATES - 1u));
    c.forbidden = lpx_forbidden(tag);
    c.reg_index = -1;
    uint8_t base = lpx_base(tag);
    uint8_t pol =
        ev ? lpx_polarity(ev->pos0, ev->neg0, ev->pos1, ev->neg1, ev->stale) : LPX_P_NEUTRAL;
    if (base != LPX_B_BOTH && base != LPX_B_NEITHER) {
        c.kind = PDX_KIND_NONE;
    } else {
        c.reg_index = pdx_reg_find(reg, sig);
        if (c.reg_index >= 0)
            c.kind = reg->ent[c.reg_index].kind;
        else
            c.kind = pdx_kind_of_tag(lpx_make(base, pol, lpx_phase(tag)));
        pol = kind_pol(c.kind);
    }
    c.tag = lpx_set(tag, base, pol);
    return c;
}

/* ---- P2.1 ---- */
pdx_class_t pdx_classify_answer(uint8_t base, uint32_t n_tokens, const pdx_evidence_t *ev,
                                bool finished, const pdx_registry_t *reg,
                                const uint8_t sig[PDX_HASH])
{
    uint8_t phase = !n_tokens ? LPX_PH_POTENTIAL : finished ? LPX_PH_MANIFEST : LPX_PH_EMERGING;
    return pdx_classify(lpx_make((uint8_t) (base & 3u), LPX_P_NEUTRAL, phase), ev, reg, sig);
}

/* ---- P0.2 constructive ---- */
static void params_hash(const int64_t *v, uint32_t n, uint8_t out[PDX_HASH])
{
    sha256_ctx_t c;
    sha256_init(&c);
    h_str(&c, "zxv/pdx-params/v1");
    h_u32(&c, n);
    for (uint32_t i = 0; i < n; i++) h_u64(&c, (uint64_t) v[i]);
    sha256_final(&c, out);
}

static bool in_bounds(const pdx_params_t *p, const int64_t *s)
{
    for (uint32_t i = 0; i < p->n; i++)
        if (p->lo[i] > p->hi[i] || s[i] < p->lo[i] || s[i] > p->hi[i]) return false;
    return true;
}

pdx_out_t pdx_constructive(pdx_params_t *p, const int64_t *synth, uint32_t tension,
                           const pdx_class_t *c, pdx_log_t *log)
{
    if (!p || !c || !log || !p->val || !p->prev || !p->lo || !p->hi) return PDX_OUT_REFUSED;
    pdx_out_t o;
    bool restore = false, apply = false;
    if (c->forbidden) {
        o = PDX_OUT_ROLLED_BACK;
        restore = p->armed;
    } else if (c->kind != PDX_KIND_CONSTRUCTIVE) {
        o = PDX_OUT_REFUSED;
    } else if (p->armed && tension > p->tension_at) {
        o = PDX_OUT_ROLLED_BACK;
        restore = true;
    } else if (!synth) {
        o = PDX_OUT_KEPT;
    } else if (!in_bounds(p, synth)) {
        o = PDX_OUT_PROPOSAL_FOR_OWNER;
    } else {
        o = PDX_OUT_APPLIED;
        apply = true;
    }
    uint8_t live[PDX_HASH], prop[PDX_HASH] = {0};
    params_hash(restore ? p->prev : apply ? synth : p->val, p->n, live);
    if (synth) params_hash(synth, p->n, prop);
    if (!pdx_log_append(log, PDX_OP_CONSTRUCTIVE, c->kind, c->tag, (uint8_t) o, p->n, live, prop))
        return PDX_OUT_LOG_FULL;
    if (o == PDX_OUT_REFUSED) return o;
    for (uint32_t i = 0; i < p->n; i++) {
        if (restore) p->val[i] = p->prev[i];
        if (apply) {
            p->prev[i] = p->val[i];
            p->val[i] = synth[i];
        }
    }
    p->armed = apply;
    if (apply) p->tension_at = tension;
    return o;
}

/* ---- P0.2 destructive ---- */
uint32_t pdx_detect(const pdx_watch_t *w, const uint8_t *state, uint32_t len)
{
    if (!w) return 0;
    uint32_t t = 0;
    if (w->step_limit && w->steps >= w->step_limit) t |= PDX_TRIP_STEPS;
    if (w->budget && w->spent >= w->budget) t |= PDX_TRIP_BUDGET;
    if (w->trace)
        for (uint32_t i = 1; i < w->trace_n && !(t & PDX_TRIP_CYCLE); i++)
            for (uint32_t j = 0; j < i; j++)
                if (w->trace[i] == w->trace[j]) {
                    t |= PDX_TRIP_CYCLE;
                    break;
                }
    if (w->invariant && !w->invariant(state, len, w->ctx)) t |= PDX_TRIP_INVARIANT;
    return t;
}

void pdx_state_hash(const uint8_t *s, uint32_t len, uint8_t out[PDX_HASH])
{
    sha256_ctx_t c;
    sha256_init(&c);
    h_str(&c, "zxv/pdx-state/v1");
    h_u32(&c, len);
    if (s && len) sha256_update(&c, s, len);
    sha256_final(&c, out);
}

/* RepairProof.what = SHA-256 over "zxv/pdx-repair/v1" || trips || steps ||
 * spent || trace_n || submitter || len || before || after || sig (u32s
 * little-endian). */
pdx_out_t pdx_destructive(const pdx_class_t *c, const pdx_watch_t *w, uint8_t *state,
                          const uint8_t *checkpoint, uint32_t len, const uint8_t *pattern,
                          uint32_t pattern_len, uint32_t submitter, lpx_qlog_t *q, pdx_log_t *log,
                          pdx_repair_t *proof)
{
    if (!c || !state || !checkpoint || !q || !log || !proof || (!pattern && pattern_len))
        return PDX_OUT_REFUSED;
    uint32_t trips = pdx_detect(w, state, len) | (c->forbidden ? PDX_TRIP_FORBIDDEN : 0u);
    if (!trips && c->kind != PDX_KIND_DESTRUCTIVE) {
        if (!pdx_log_append(log, PDX_OP_DESTRUCTIVE, c->kind, c->tag, PDX_OUT_REFUSED, 0, 0, 0))
            return PDX_OUT_LOG_FULL;
        return PDX_OUT_REFUSED;
    }
    pdx_state_hash(state, len, proof->before);
    for (uint32_t i = 0; i < len; i++) state[i] = checkpoint[i];
    pdx_state_hash(state, len, proof->after);
    pdx_signature(pattern, pattern_len, proof->sig);
    proof->steps = w ? w->steps : 0;
    proof->trips = trips;
    sha256_ctx_t h;
    sha256_init(&h);
    h_str(&h, "zxv/pdx-repair/v1");
    h_u32(&h, trips);
    h_u32(&h, proof->steps);
    h_u32(&h, w ? w->spent : 0);
    h_u32(&h, w ? w->trace_n : 0);
    h_u32(&h, submitter);
    h_u32(&h, len);
    sha256_update(&h, proof->before, PDX_HASH);
    sha256_update(&h, proof->after, PDX_HASH);
    sha256_update(&h, proof->sig, PDX_HASH);
    sha256_final(&h, proof->what);
    proof->qseq = q->n;
    if (q->n >= q->cap || !log_room(log, 1)) return PDX_OUT_LOG_FULL;
    lpx_qlog_append(q, lpx_forbid(c->tag), (uint8_t) trips, submitter, proof->what);
    pdx_log_append(log, PDX_OP_DESTRUCTIVE, c->kind, c->tag, PDX_OUT_CONTAINED, proof->steps,
                   proof->what, proof->sig);
    return PDX_OUT_CONTAINED;
}

/* ---- P0.2 neutral ---- */
void pdx_hold_init(pdx_hold_t *h, pdx_branch_t *storage, uint32_t cap, uint32_t decisive)
{
    if (!h) return;
    h->br = storage;
    h->cap = storage ? cap : 0;
    h->n = 0;
    h->decisive = decisive ? decisive : 1u;
    h->settled = -1;
}

static int32_t hold_find(const pdx_hold_t *h, const uint8_t id[PDX_HASH])
{
    for (uint32_t i = 0; i < h->n; i++)
        if (eq32(h->br[i].id, id)) return (int32_t) i;
    return -1;
}

bool pdx_hold_add(pdx_hold_t *h, const uint8_t id[PDX_HASH])
{
    if (!h || !id || h->settled >= 0) return false;
    if (hold_find(h, id) >= 0) return true;
    if (h->n >= h->cap) return false;
    pdx_branch_t *b = &h->br[h->n++];
    cp32(b->id, id);
    b->pos = b->neg = 0;
    return true;
}

bool pdx_hold_evidence(pdx_hold_t *h, const uint8_t id[PDX_HASH], bool positive)
{
    if (!h || !id || h->settled >= 0) return false;
    int32_t i = hold_find(h, id);
    if (i < 0) return false;
    uint32_t *e = positive ? &h->br[i].pos : &h->br[i].neg;
    if (*e != UINT32_MAX) (*e)++;
    return true;
}

int32_t pdx_hold_decisive(const pdx_hold_t *h)
{
    if (!h) return -1;
    for (uint32_t i = 0; i < h->n; i++) {
        int64_t net = (int64_t) h->br[i].pos - (int64_t) h->br[i].neg;
        int64_t best_other = 0; /* alone: lead over zero */
        bool any = false;
        for (uint32_t j = 0; j < h->n; j++) {
            if (j == i) continue;
            int64_t o = (int64_t) h->br[j].pos - (int64_t) h->br[j].neg;
            if (!any || o > best_other) best_other = o;
            any = true;
        }
        if (net - best_other >= (int64_t) h->decisive) return (int32_t) i;
    }
    return -1;
}

static void branches_hash(const pdx_hold_t *h, uint8_t out[PDX_HASH])
{
    sha256_ctx_t c;
    sha256_init(&c);
    h_str(&c, "zxv/pdx-branches/v1");
    h_u32(&c, h->n);
    for (uint32_t i = 0; i < h->n; i++) {
        sha256_update(&c, h->br[i].id, PDX_HASH);
        h_u32(&c, h->br[i].pos);
        h_u32(&c, h->br[i].neg);
    }
    sha256_final(&c, out);
}

pdx_out_t pdx_neutral(pdx_hold_t *h, const pdx_class_t *c, int32_t owner_pick, pdx_log_t *log)
{
    if (!h || !c || !log || (h->n && !h->br)) return PDX_OUT_REFUSED;
    pdx_out_t o;
    int32_t pick = h->settled;
    if (c->forbidden) {
        o = PDX_OUT_ROLLED_BACK;
    } else if (c->kind != PDX_KIND_NEUTRAL) {
        o = PDX_OUT_REFUSED;
    } else {
        if (pick < 0 && owner_pick >= 0 && (uint32_t) owner_pick < h->n) pick = owner_pick;
        if (pick < 0) pick = pdx_hold_decisive(h);
        o = pick >= 0 ? PDX_OUT_SETTLED : PDX_OUT_DEFERRED;
    }
    bool settle = o == PDX_OUT_SETTLED;
    uint8_t set[PDX_HASH];
    branches_hash(h, set);
    if (!pdx_log_append(log, PDX_OP_NEUTRAL, c->kind, c->tag, (uint8_t) o,
                        settle ? (uint32_t) pick : h->n, set, settle ? h->br[pick].id : 0))
        return PDX_OUT_LOG_FULL;
    if (settle) h->settled = pick;
    return o;
}

/* ---- P0.2 special ---- */
pdx_out_t pdx_special(pdx_log_t *frontier, const pdx_class_t *c, const uint8_t sig[PDX_HASH],
                      const uint8_t subject[PDX_HASH], pdx_log_t *log, uint32_t *rec_out)
{
    if (!frontier || !c || !sig || !subject || !log) return PDX_OUT_REFUSED;
    pdx_out_t o = c->forbidden                  ? PDX_OUT_ROLLED_BACK
                  : c->kind != PDX_KIND_SPECIAL ? PDX_OUT_REFUSED
                                                : PDX_OUT_RECORDED;
    if (o != PDX_OUT_RECORDED) {
        if (!pdx_log_append(log, PDX_OP_SPECIAL, c->kind, c->tag, (uint8_t) o, 0, sig, 0))
            return PDX_OUT_LOG_FULL;
        return o;
    }
    if (!log_room(frontier, 1) || !log_room(log, frontier == log ? 2u : 1u))
        return PDX_OUT_LOG_FULL;
    uint32_t seq = frontier->n;
    pdx_log_append(frontier, PDX_OP_SPECIAL, c->kind, c->tag, (uint8_t) o, 0, sig, subject);
    pdx_log_append(log, PDX_OP_SPECIAL, c->kind, c->tag, (uint8_t) o, seq, sig,
                   frontier->rec[seq].hash);
    if (rec_out) *rec_out = seq;
    return o;
}

static const pdx_rec_t *frontier_rec(const pdx_log_t *f, uint32_t rec)
{
    if (!f || rec >= f->n || rec >= f->cap || f->rec[rec].op != PDX_OP_SPECIAL) return 0;
    return &f->rec[rec];
}

/* SHA-256 of "zxv/pdx-draft/v1" || rec || rec_hash || sig || kind ||
 * reason || flags: what a DRAFT record commits to, so a review can tell the
 * draft it is handed from the one that was recorded. */
static void draft_bind(uint32_t rec, const uint8_t rec_hash[PDX_HASH], const uint8_t sig[PDX_HASH],
                       uint8_t kind, uint8_t reason, uint8_t flags, uint8_t out[PDX_HASH])
{
    sha256_ctx_t c;
    sha256_init(&c);
    h_str(&c, "zxv/pdx-draft/v1");
    h_u32(&c, rec);
    sha256_update(&c, rec_hash, PDX_HASH);
    sha256_update(&c, sig, PDX_HASH);
    uint8_t f[3] = {kind, reason, flags};
    sha256_update(&c, f, 3);
    sha256_final(&c, out);
}

/* The DRAFT record in log that commits to exactly this draft. */
static bool draft_recorded(const pdx_log_t *log, const pdx_draft_t *d)
{
    uint8_t bind[PDX_HASH];
    draft_bind(d->rec, d->rec_hash, d->sig, d->kind, d->reason, d->flags, bind);
    for (uint32_t k = log->n; k > 0; k--) {
        const pdx_rec_t *x = &log->rec[k - 1];
        if (x->op == PDX_OP_DRAFT && x->outcome == PDX_OUT_RECORDED && x->aux == d->rec &&
            x->kind == d->kind && eq32(x->a, d->rec_hash) && eq32(x->b, bind))
            return true;
    }
    return false;
}

pdx_out_t pdx_draft(const pdx_log_t *frontier, uint32_t rec, uint8_t kind, uint8_t reason,
                    uint8_t flags, pdx_draft_t *d, pdx_log_t *log)
{
    const pdx_rec_t *r = frontier_rec(frontier, rec);
    if (!r || !d || !log || !registry_kind(kind)) return PDX_OUT_REFUSED;
    uint8_t bind[PDX_HASH];
    draft_bind(rec, r->hash, r->a, kind, reason, flags, bind);
    if (!pdx_log_append(log, PDX_OP_DRAFT, kind, r->tag, PDX_OUT_RECORDED, rec, r->hash, bind))
        return PDX_OUT_LOG_FULL;
    d->rec = rec;
    cp32(d->rec_hash, r->hash);
    cp32(d->sig, r->a);
    d->kind = kind;
    d->reason = reason;
    d->flags = flags;
    return PDX_OUT_RECORDED;
}

pdx_out_t pdx_review(pdx_registry_t *reg, const pdx_log_t *frontier, const pdx_draft_t *d,
                     bool owner_ok, bool consensus_ok, pdx_log_t *log)
{
    const pdx_rec_t *r = d ? frontier_rec(frontier, d->rec) : 0;
    if (!reg || !r || !log || !registry_kind(d->kind) || !eq32(r->hash, d->rec_hash) ||
        !eq32(r->a, d->sig) || !pdx_log_verify(frontier) || !pdx_log_verify(log) ||
        !draft_recorded(log, d))
        return PDX_OUT_REFUSED;
    bool needs_owner = (d->flags & (PDX_DRAFT_MONEY | PDX_DRAFT_DEVICE)) != 0;
    pdx_out_t o;
    if (!(owner_ok || (!needs_owner && consensus_ok)))
        o = PDX_OUT_PROPOSAL_FOR_OWNER;
    else if (pdx_reg_find(reg, d->sig) >= 0 || reg->n >= reg->cap)
        o = PDX_OUT_REFUSED;
    else
        o = PDX_OUT_ACCEPTED;
    if (!pdx_log_append(log, PDX_OP_REVIEW, d->kind, r->tag, (uint8_t) o, reg->n, d->rec_hash,
                        d->sig))
        return PDX_OUT_LOG_FULL;
    if (o == PDX_OUT_ACCEPTED) pdx_reg_add(reg, d->sig, d->kind, d->reason, d->flags, r->seq);
    return o;
}
