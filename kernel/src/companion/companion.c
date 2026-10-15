/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* companion.c — the Chiglet companion. See companion.h. */
#include "companion.h"
#include "../robin_debanks/sha256.h"
#include "../pqsec/pq_matrix_algs.h"

_Static_assert(CMP_PK_BYTES == PQM_MLDSA87_PK_BYTES, "ML-DSA-87 public key size");
_Static_assert(CMP_SK_BYTES == PQM_MLDSA87_SK_BYTES, "ML-DSA-87 secret key size");
_Static_assert(CMP_SIG_BYTES == PQM_MLDSA87_SIG_BYTES, "ML-DSA-87 signature size");

static void wr_le(uint8_t *p, uint64_t v, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) p[i] = (uint8_t) (v >> (8u * i));
}

static uint64_t rd_le(const uint8_t *p, uint32_t n)
{
    uint64_t v = 0;
    for (uint32_t i = 0; i < n; i++) v |= (uint64_t) p[i] << (8u * i);
    return v;
}

static void cpy(uint8_t *d, const uint8_t *s, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) d[i] = s[i];
}

static bool eq(const uint8_t *a, const uint8_t *b, uint32_t n)
{
    uint8_t d = 0;
    for (uint32_t i = 0; i < n; i++) d |= (uint8_t) (a[i] ^ b[i]);
    return d == 0;
}

/* ---- C.1 manifest ---- */

void cmp_manifest_encode(const cmp_manifest_t *m, uint8_t o[CMP_MANIFEST_BYTES])
{
    for (uint32_t i = 0; i < CMP_MANIFEST_BYTES; i++) o[i] = 0;
    wr_le(o, CMP_MANIFEST_MAGIC, 4);
    wr_le(o + 4, CMP_MANIFEST_VER, 4);
    wr_le(o + 8, m->epoch, 8);
    cpy(o + 16, m->companion_id, CMP_HASH);
    cpy(o + 48, m->owner_pk_hash, CMP_HASH);
    cpy(o + 80, (const uint8_t *) m->name, CMP_NAME);
    wr_le(o + 112, m->caps_max, 4);
    wr_le(o + 116, m->max_tokens_cycle, 4);
    wr_le(o + 120, m->auto_spend_max, 8);
    wr_le(o + 128, m->spend_cap, 8);
    wr_le(o + 136, m->mem_slots, 4);
    wr_le(o + 140, m->mem_slot_bytes, 4);
    wr_le(o + 144, m->n_models, 4);
    for (uint32_t i = 0; i < CMP_MAX_MODELS && i < m->n_models; i++) {
        cpy(o + 152 + 36u * i, m->models[i].sha, CMP_HASH);
        wr_le(o + 152 + 36u * i + 32, m->models[i].role, 4);
    }
}

cmp_status_t cmp_manifest_decode(const uint8_t in[CMP_MANIFEST_BYTES], cmp_manifest_t *m)
{
    if (!in || !m) return CMP_ERR_ARG;
    if (rd_le(in, 4) != CMP_MANIFEST_MAGIC || rd_le(in + 4, 4) != CMP_MANIFEST_VER)
        return CMP_ERR_FORMAT;
    m->epoch = rd_le(in + 8, 8);
    cpy(m->companion_id, in + 16, CMP_HASH);
    cpy(m->owner_pk_hash, in + 48, CMP_HASH);
    cpy((uint8_t *) m->name, in + 80, CMP_NAME);
    m->caps_max = (uint32_t) rd_le(in + 112, 4);
    m->max_tokens_cycle = (uint32_t) rd_le(in + 116, 4);
    m->auto_spend_max = rd_le(in + 120, 8);
    m->spend_cap = rd_le(in + 128, 8);
    m->mem_slots = (uint32_t) rd_le(in + 136, 4);
    m->mem_slot_bytes = (uint32_t) rd_le(in + 140, 4);
    m->n_models = (uint32_t) rd_le(in + 144, 4);
    if (rd_le(in + 148, 4) != 0) return CMP_ERR_FORMAT;
    if (m->n_models > CMP_MAX_MODELS || (m->caps_max & ~CMP_CAP_ALL)) return CMP_ERR_FORMAT;
    if (m->auto_spend_max > m->spend_cap) return CMP_ERR_FORMAT;
    for (uint32_t i = 0; i < CMP_MAX_MODELS; i++) {
        cpy(m->models[i].sha, in + 152 + 36u * i, CMP_HASH);
        m->models[i].role = (uint32_t) rd_le(in + 152 + 36u * i + 32, 4);
        if (i >= m->n_models) {
            /* unused slots must be zero, so one manifest has one encoding */
            for (uint32_t j = 0; j < 36u; j++)
                if (in[152 + 36u * i + j]) return CMP_ERR_FORMAT;
        } else if (m->models[i].role > CMP_ROLE_EVIDENCE) {
            return CMP_ERR_FORMAT;
        }
    }
    return CMP_OK;
}

void cmp_manifest_digest(const cmp_manifest_t *m, uint8_t out[CMP_HASH])
{
    static const char tag[] = "zxv/companion/manifest/v1";
    uint8_t enc[CMP_MANIFEST_BYTES];
    cmp_manifest_encode(m, enc);
    sha256_ctx_t c;
    sha256_init(&c);
    sha256_update(&c, (const uint8_t *) tag, sizeof tag - 1);
    sha256_update(&c, enc, CMP_MANIFEST_BYTES);
    sha256_final(&c, out);
}

static bool manifest_ok(const cmp_manifest_t *m)
{
    uint8_t enc[CMP_MANIFEST_BYTES];
    cmp_manifest_t back;
    cmp_manifest_encode(m, enc);
    return cmp_manifest_decode(enc, &back) == CMP_OK;
}

static bool sig_ok(const uint8_t pk[CMP_PK_BYTES], const uint8_t msg[CMP_HASH],
                   const uint8_t sig[CMP_SIG_BYTES])
{
    static const char ctx[] = CMP_SIG_CTX;
    return pqm_mldsa87_verify(pk, msg, CMP_HASH, (const uint8_t *) ctx, sizeof ctx - 1, sig);
}

/* ---- C.7 log ---- */

static void rec_hash(const cmp_rec_t *r, uint8_t out[CMP_HASH])
{
    static const char tag[] = "zxv/companion/log/v1";
    uint8_t b[8 * 3 + 6 + 2 + 4 + 2 * CMP_HASH];
    wr_le(b, r->seq, 8);
    wr_le(b + 8, r->epoch, 8);
    wr_le(b + 16, r->amount, 8);
    b[24] = r->kind;
    b[25] = r->action;
    b[26] = r->tag;
    b[27] = r->verdict;
    b[28] = r->why;
    b[29] = r->gate;
    wr_le(b + 30, r->r_permille, 2);
    wr_le(b + 32, r->caps, 4);
    cpy(b + 36, r->subject, CMP_HASH);
    cpy(b + 36 + CMP_HASH, r->prev, CMP_HASH);
    sha256_ctx_t c;
    sha256_init(&c);
    sha256_update(&c, (const uint8_t *) tag, sizeof tag - 1);
    sha256_update(&c, b, sizeof b);
    sha256_final(&c, out);
}

static bool log_append(cmp_log_t *l, cmp_rec_t *r)
{
    if (!l->rec || l->n >= l->cap) return false;
    r->seq = l->n;
    cpy(r->prev, l->head, CMP_HASH);
    rec_hash(r, r->hash);
    l->rec[l->n++] = *r;
    cpy(l->head, r->hash, CMP_HASH);
    return true;
}

bool cmp_log_verify(const cmp_log_t *l)
{
    if (!l || (l->n && !l->rec) || l->n > l->cap) return false;
    uint8_t prev[CMP_HASH] = {0};
    for (uint32_t i = 0; i < l->n; i++) {
        const cmp_rec_t *r = &l->rec[i];
        uint8_t h[CMP_HASH];
        if (r->seq != i || !eq(r->prev, prev, CMP_HASH)) return false;
        rec_hash(r, h);
        if (!eq(h, r->hash, CMP_HASH)) return false;
        cpy(prev, h, CMP_HASH);
    }
    return eq(prev, l->head, CMP_HASH);
}

static cmp_rec_t rec_new(const companion_t *c, uint8_t kind)
{
    cmp_rec_t r;
    for (uint32_t i = 0; i < sizeof r; i++) ((uint8_t *) &r)[i] = 0;
    r.kind = kind;
    r.epoch = c->man.epoch;
    r.caps = c->granted;
    return r;
}

/* ---- start, manifest changes ---- */

cmp_status_t cmp_start(companion_t *c, const cmp_manifest_t *m,
                       const uint8_t owner_pk[CMP_PK_BYTES], const uint8_t sig[CMP_SIG_BYTES],
                       cmp_rec_t *rec, uint32_t cap)
{
    if (!c || !m || !owner_pk || !sig || !rec || cap == 0) return CMP_ERR_ARG;
    c->live = false;
    if (!manifest_ok(m)) return CMP_ERR_FORMAT;
    uint8_t h[CMP_HASH], d[CMP_HASH];
    sha256(owner_pk, CMP_PK_BYTES, h);
    if (!eq(h, m->owner_pk_hash, CMP_HASH)) return CMP_ERR_OWNER;
    cmp_manifest_digest(m, d);
    if (!sig_ok(owner_pk, d, sig)) return CMP_ERR_SIG;
    c->man = *m;
    cpy(c->owner_pk, owner_pk, CMP_PK_BYTES);
    c->granted = 0;
    c->grant_seq = 0;
    chg_init(&c->gate, CHG_CAP_INFER);
    c->log.rec = rec;
    c->log.cap = cap;
    c->log.n = 0;
    for (uint32_t i = 0; i < CMP_HASH; i++) c->log.head[i] = 0;
    c->mood.emotion = SWARM_EMO_NEUTRAL;
    c->mood.intensity = 0;
    c->decisions = c->asks = c->refusals = 0;
    cmp_rec_t r = rec_new(c, CMP_REC_BORN);
    cpy(r.subject, d, CMP_HASH);
    if (!log_append(&c->log, &r)) return CMP_ERR_LOG;
    c->live = true;
    return CMP_OK;
}

/* C.1: a manifest with no name may gain one; once named, the name stays */
static bool named_same(const cmp_manifest_t *cur, const cmp_manifest_t *m)
{
    bool named = false;
    for (uint32_t i = 0; i < CMP_NAME; i++)
        if (cur->name[i] != 0) named = true;
    return !named || eq((const uint8_t *) cur->name, (const uint8_t *) m->name, CMP_NAME);
}

cmp_status_t cmp_update_manifest(companion_t *c, const cmp_manifest_t *m,
                                 const uint8_t *new_owner_pk, const uint8_t sig[CMP_SIG_BYTES])
{
    if (!c || !m || !sig) return CMP_ERR_ARG;
    if (!c->live) return CMP_ERR_STATE;
    if (!manifest_ok(m)) return CMP_ERR_FORMAT;
    if (m->epoch <= c->man.epoch) return CMP_ERR_EPOCH;
    if (!eq(m->companion_id, c->man.companion_id, CMP_HASH)) return CMP_ERR_ID;
    if (!named_same(&c->man, m)) return CMP_ERR_NAME;
    bool new_owner = !eq(m->owner_pk_hash, c->man.owner_pk_hash, CMP_HASH);
    if (new_owner) {
        uint8_t h[CMP_HASH];
        if (!new_owner_pk) return CMP_ERR_OWNER;
        sha256(new_owner_pk, CMP_PK_BYTES, h);
        if (!eq(h, m->owner_pk_hash, CMP_HASH)) return CMP_ERR_OWNER;
    }
    uint8_t d[CMP_HASH];
    cmp_manifest_digest(m, d);
    if (!sig_ok(c->owner_pk, d, sig)) return CMP_ERR_SIG; /* the CURRENT owner signs */
    if (c->log.n >= c->log.cap) return CMP_ERR_LOG;
    c->man = *m;
    if (new_owner) {
        cpy(c->owner_pk, new_owner_pk, CMP_PK_BYTES);
        c->granted = 0; /* a new owner grants afresh */
    }
    c->granted &= m->caps_max;
    cmp_rec_t r = rec_new(c, CMP_REC_MANIFEST);
    cpy(r.subject, d, CMP_HASH);
    log_append(&c->log, &r);
    return CMP_OK;
}

/* ---- C.2 capabilities ---- */

void cmp_grant_digest(const companion_t *c, uint32_t caps, uint64_t seq, uint8_t out[CMP_HASH])
{
    static const char tag[] = "zxv/companion/grant/v1";
    uint8_t b[CMP_HASH + 8 + 8 + 4];
    cpy(b, c->man.companion_id, CMP_HASH);
    wr_le(b + CMP_HASH, c->man.epoch, 8);
    wr_le(b + CMP_HASH + 8, seq, 8);
    wr_le(b + CMP_HASH + 16, caps, 4);
    sha256_ctx_t h;
    sha256_init(&h);
    sha256_update(&h, (const uint8_t *) tag, sizeof tag - 1);
    sha256_update(&h, b, sizeof b);
    sha256_final(&h, out);
}

cmp_status_t cmp_grant(companion_t *c, uint32_t caps, uint64_t seq,
                       const uint8_t sig[CMP_SIG_BYTES])
{
    if (!c || !sig || caps == 0) return CMP_ERR_ARG;
    if (!c->live) return CMP_ERR_STATE;
    if (seq != c->grant_seq) return CMP_ERR_SEQ;
    if (caps & ~c->man.caps_max) return CMP_ERR_CEILING;
    uint8_t d[CMP_HASH];
    cmp_grant_digest(c, caps, seq, d);
    if (!sig_ok(c->owner_pk, d, sig)) return CMP_ERR_SIG;
    if (c->log.n >= c->log.cap) return CMP_ERR_LOG;
    c->granted |= caps;
    c->grant_seq++;
    cmp_rec_t r = rec_new(c, CMP_REC_GRANT);
    cpy(r.subject, d, CMP_HASH);
    log_append(&c->log, &r);
    return CMP_OK;
}

cmp_status_t cmp_revoke(companion_t *c, uint32_t caps)
{
    if (!c) return CMP_ERR_ARG;
    if (!c->live) return CMP_ERR_STATE;
    /* Removing power never waits on the log: revoke first, then record if
     * there is room. */
    c->granted &= ~caps;
    cmp_rec_t r = rec_new(c, CMP_REC_REVOKE);
    wr_le(r.subject, caps, 4);
    return log_append(&c->log, &r) ? CMP_OK : CMP_ERR_LOG;
}

static const uint32_t act_caps[CMP_ACT_COUNT] = {
    [CMP_ACT_REPLY] = 0,
    [CMP_ACT_MEMORY_WRITE] = 0,
    [CMP_ACT_NET] = CMP_CAP_NET,
    [CMP_ACT_PEER] = CMP_CAP_VINEA,
    [CMP_ACT_LEDGER_POST] = CMP_CAP_LEDGER,
    [CMP_ACT_SPEND] = CMP_CAP_SPEND | CMP_CAP_LEDGER,
    [CMP_ACT_DEVICE] = CMP_CAP_DEVICE,
    [CMP_ACT_MEMORY_EXPORT] = CMP_CAP_MEM_EXPORT,
};

uint32_t cmp_action_caps(cmp_action_t a)
{
    return (unsigned) a < CMP_ACT_COUNT ? act_caps[a] : CMP_CAP_ALL;
}

bool cmp_action_guarded(cmp_action_t a)
{
    return a == CMP_ACT_LEDGER_POST || a == CMP_ACT_SPEND || a == CMP_ACT_DEVICE;
}

cmp_status_t cmp_load_gate(companion_t *c, const chg_model_t *m)
{
    if (!c || !m || m->L < 2) return CMP_ERR_ARG;
    if (!c->live) return CMP_ERR_STATE;
    return chg_load_model(&c->gate, m) == CHG_OK ? CMP_OK : CMP_ERR_ARG;
}

/* ---- C.3 the reasoning loop ---- */

uint8_t cmp_base_join(uint8_t a, uint8_t b)
{
    /* Each base as an (evidence for, evidence against) pair: True (1,0),
     * False (0,1), Both (1,1), Neither (0,0). The knowledge join ORs the
     * pairs, so True with False is Both and Neither adds nothing. */
    static const uint8_t fa[4] = {
        [LPX_B_FALSE] = 1, [LPX_B_NEITHER] = 0, [LPX_B_BOTH] = 3, [LPX_B_TRUE] = 2};
    static const uint8_t back[4] = {LPX_B_NEITHER, LPX_B_FALSE, LPX_B_TRUE, LPX_B_BOTH};
    return back[fa[a & 3u] | fa[b & 3u]];
}

static uint32_t permille(surplus_real_t r)
{
    uint32_t v = SR_TO_PERMILLE(r);
    return v > 65535u ? 65535u : v;
}

cmp_status_t cmp_decide(companion_t *c, const cmp_proposal_t *p, cmp_invariant_fn inv,
                        void *inv_ctx, cmp_decision_t *out)
{
    if (!c || !p || !out) return CMP_ERR_ARG;
    if (!c->live) return CMP_ERR_STATE;
    cmp_decision_t d = {
        CMP_REFUSE, CMP_WHY_OK, 0, LPX_ACT_REFUSE, CHG_UNAVAILABLE, CHG_REASON_NO_MODEL,
        0,          false,      0};
    *out = d;
    if (c->log.n >= c->log.cap) {
        out->why = CMP_WHY_LOG_FULL;
        out->tag = lpx_make(LPX_B_NEITHER, LPX_P_NEUTRAL, LPX_PH_POTENTIAL);
        c->refusals++;
        return CMP_ERR_LOG;
    }
    bool bad = (unsigned) p->action >= CMP_ACT_COUNT || p->n_goals > CMP_MAX_GOALS ||
               p->k > CHG_MAX_EXPERTS || (p->k && !p->ev);
    uint8_t base;
    if (bad) {
        d.why = CMP_WHY_BAD_INPUT;
        base = LPX_B_FALSE;
    } else if (cmp_action_caps(p->action) & ~c->granted) {
        d.why = CMP_WHY_NO_CAP;
        base = LPX_B_FALSE;
    } else if (p->action == CMP_ACT_SPEND && p->amount > c->man.spend_cap) {
        d.why = CMP_WHY_OVER_CAP;
        base = LPX_B_FALSE;
    } else {
        chg_result_t r;
        base = LPX_B_NEITHER;
        if (p->k && chg_infer(&c->gate, p->ev, p->k, &r) == CHG_OK) {
            d.gate = r.state;
            d.gate_reason = r.reason;
            d.r_permille = permille(r.R);
            if (r.state == CHG_DECIDED && r.label == 0)
                base = LPX_B_TRUE;
            else if (r.state == CHG_DECIDED && r.label == 1)
                base = LPX_B_FALSE;
            else if (r.state == CHG_UNCERTAIN && r.reason == CHG_REASON_LOW_MARGIN)
                base = LPX_B_BOTH;
        } else if (p->k) {
            d.gate_reason = c->gate.model.loaded ? CHG_REASON_NO_EVIDENCE : CHG_REASON_NO_MODEL;
        } else {
            d.gate_reason = CHG_REASON_NO_EVIDENCE;
        }
        for (uint32_t i = 0; i < p->n_goals; i++) base = cmp_base_join(base, p->goal[i]);
    }
    uint8_t pol = bad ? LPX_P_NEUTRAL : lpx_polarity(p->pos0, p->neg0, p->pos1, p->neg1, p->stale);
    d.tag = lpx_make(base, pol, LPX_PH_EMERGING);
    if (!bad && d.why == CMP_WHY_OK && cmp_action_guarded(p->action) && !(inv && inv(p, inv_ctx))) {
        d.tag = lpx_forbid(d.tag);
        d.why = CMP_WHY_INVARIANT;
    }
    d.lpx = lpx_action(d.tag);
    switch (d.lpx) {
    case LPX_ACT_PROCEED:
        d.verdict = CMP_PROCEED;
        break;
    case LPX_ACT_WAIT:
        d.verdict = CMP_WAIT;
        break;
    case LPX_ACT_ESCALATE:
        d.verdict = CMP_ASK;
        break;
    case LPX_ACT_ROLLBACK:
        d.verdict = CMP_REFUSE;
        d.quarantined = true;
        break;
    default:
        d.verdict = CMP_REFUSE;
        break;
    }
    if (d.verdict == CMP_PROCEED && p->action == CMP_ACT_SPEND &&
        p->amount > c->man.auto_spend_max) {
        d.verdict = CMP_ASK;
        d.why = CMP_WHY_OWNER_LIMIT;
    }
    if (d.why == CMP_WHY_OK) {
        uint8_t b = lpx_base(d.tag);
        d.why = b == LPX_B_FALSE        ? CMP_WHY_OPPOSED
                : b == LPX_B_NEITHER    ? CMP_WHY_UNKNOWN
                : b == LPX_B_BOTH       ? CMP_WHY_CONFLICT
                : d.verdict == CMP_WAIT ? CMP_WHY_WEAKENING
                                        : CMP_WHY_OK;
    }

    cmp_rec_t r = rec_new(c, CMP_REC_DECIDE);
    r.action = bad ? 0xFFu : (uint8_t) p->action;
    r.amount = p->amount;
    r.tag = d.tag;
    r.verdict = (uint8_t) d.verdict;
    r.why = (uint8_t) d.why;
    r.gate = (uint8_t) d.gate;
    r.r_permille = (uint16_t) d.r_permille;
    cpy(r.subject, p->digest, CMP_HASH);
    log_append(&c->log, &r);
    d.seq = r.seq;
    c->decisions++;
    if (d.verdict == CMP_ASK) c->asks++;
    if (d.verdict == CMP_REFUSE) c->refusals++;
    *out = d;
    return CMP_OK;
}

/* ---- C.4 mood: tone and budget, never permissions ---- */

cmp_status_t cmp_set_mood(companion_t *c, swarm_feeling_t mood)
{
    if (!c) return CMP_ERR_ARG;
    if ((unsigned) mood.emotion >= SWARM_EMO_COUNT || mood.intensity > SWARM_EMO_MAX_INTENSITY)
        return CMP_ERR_ARG;
    c->mood = mood;
    return CMP_OK;
}

const swarm_emotion_profile_t *cmp_tone(const companion_t *c)
{
    return swarm_emotion_profile(c ? c->mood.emotion : SWARM_EMO_NEUTRAL);
}

uint32_t cmp_token_budget(const companion_t *c, uint32_t base)
{
    if (!c) return 0;
    /* floor(base * (21 + charge) / 21) with 32-bit divisions only */
    uint32_t f = SWARM_EMO_CHARGE_DEN + (uint32_t) swarm_emotion_charge(c->mood.intensity);
    uint32_t q = base / SWARM_EMO_CHARGE_DEN, r = base % SWARM_EMO_CHARGE_DEN;
    uint64_t b = (uint64_t) q * f + r * f / SWARM_EMO_CHARGE_DEN;
    return b > c->man.max_tokens_cycle ? c->man.max_tokens_cycle : (uint32_t) b;
}

const char *cmp_verdict_name(cmp_verdict_t v)
{
    static const char *const n[] = {"proceed", "ask", "wait", "refuse"};
    return (unsigned) v < 4u ? n[v] : "?";
}

const char *cmp_why_name(cmp_why_t w)
{
    static const char *const n[] = {
        "ok",       "no capability", "over the spend cap",     "invariant", "opposed",  "unknown",
        "conflict", "weakening",     "over the owner's limit", "log full",  "bad input"};
    return (unsigned) w < sizeof n / sizeof n[0] ? n[w] : "?";
}
