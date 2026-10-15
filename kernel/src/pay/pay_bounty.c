/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* pay_bounty.c — community bounties paid by VFV issuance. See pay_bounty.h. */
#include "pay_bounty.h"

static const uint8_t k_fib[PAY_BTY_TIERS] = {1, 2, 3, 5, 8};

void pay_bounty_cfg_default(pay_bty_cfg_t *c)
{
    if (!c) return;
    pay_memset(c, 0, sizeof *c);
    c->fixer_share.num = 8;
    c->fixer_share.den = 21;
    c->panel_size = 5;
    c->quorum = 3;
    c->max_open = 8;
    c->max_active = 3;
    c->max_strikes = 3;
}

uint64_t pay_bounty_value(const pay_bty_cfg_t *c, uint8_t kind, uint8_t tier)
{
    if (!c || kind >= PAY_BTY_KIND_COUNT || tier < 1 || tier > PAY_BTY_TIERS) return 0;
    pay_u128 v = pay_mul64(c->base[kind], k_fib[tier - 1]);
    if (v.hi || v.lo >= PAY_BAL_MAX) return 0;
    return v.lo;
}

pay_status_t pay_bounty_init(pay_bounty_ctx_t *B, pay_ledger_t *L, const pay_bty_cfg_t *cfg,
                             uint32_t platform_owner, const uint32_t fee_acct[PAY_ASSURE_BUCKETS],
                             pay_bty_verify_fn verify, pay_bty_audit_fn audit, void *cb_ctx)
{
    if (!B || !L || !cfg || !fee_acct || !verify || !audit) return PAY_ERR_ARG;
    if (cfg->epoch_cap == 0 || cfg->epoch_cap >= PAY_BAL_MAX) return PAY_ERR_ARG;
    if (cfg->fixer_share.den == 0 || cfg->fixer_share.num == 0 ||
        cfg->fixer_share.num > cfg->fixer_share.den)
        return PAY_ERR_ARG;
    if (cfg->panel_size == 0 || cfg->panel_size > PAY_BTY_PANEL_MAX || cfg->quorum == 0 ||
        cfg->quorum > cfg->panel_size || cfg->max_open == 0 || cfg->max_active == 0 ||
        cfg->max_strikes == 0)
        return PAY_ERR_ARG;
    for (uint8_t k = 0; k < PAY_BTY_KIND_COUNT; k++) {
        if (!cfg->base[k]) continue;
        uint64_t top = pay_bounty_value(cfg, k, PAY_BTY_TIERS);
        if (top == 0 || top > cfg->epoch_cap) return PAY_ERR_LIMIT; /* B7: must fit */
    }
    for (int b = 0; b < PAY_ASSURE_BUCKETS; b++) {
        const pay_account_t *ba = pay_ledger_account(L, fee_acct[b]);
        if (!ba || ba->asset != L->vfv_asset || !(ba->flags & PAY_ACCT_COMMONS))
            return PAY_ERR_NO_ACCOUNT;
    }
    pay_memset(B, 0, sizeof *B);
    B->L = L;
    B->cfg = *cfg;
    B->verify = verify;
    B->audit = audit;
    B->cb_ctx = cb_ctx;
    for (int b = 0; b < PAY_ASSURE_BUCKETS; b++) B->fee_acct[b] = fee_acct[b];
    return pay_ledger_open(L, platform_owner, L->vfv_asset, PAY_CAP_FINANCIAL, PAY_ACCT_ISSUER,
                           &B->issuer_acct);
}

/* ===== people ===== */

static pay_bty_person_t *person_of(pay_bounty_ctx_t *B, uint32_t owner)
{
    for (uint32_t i = 0; i < B->n_p; i++)
        if (B->p[i].used && B->p[i].owner == owner) return &B->p[i];
    return 0;
}

static pay_status_t person_get(pay_bounty_ctx_t *B, uint32_t owner, pay_bty_person_t **out)
{
    pay_bty_person_t *p = person_of(B, owner);
    if (!p) {
        if (B->n_p >= PAY_BTY_PEOPLE) return PAY_ERR_FULL;
        p = &B->p[B->n_p];
        pay_memset(p, 0, sizeof *p);
        pay_status_t st =
            pay_ledger_open(B->L, owner, B->L->vfv_asset, PAY_CAP_FINANCIAL, 0, &p->vfv_acct);
        if (st != PAY_OK) return st;
        p->used = true;
        p->owner = owner;
        B->n_p++;
    }
    *out = p;
    return PAY_OK;
}

const pay_bty_person_t *pay_bounty_person(const pay_bounty_ctx_t *B, uint32_t owner)
{
    return B ? person_of((pay_bounty_ctx_t *) B, owner) : 0;
}

pay_status_t pay_bounty_add_reviewer(pay_bounty_ctx_t *B, uint32_t owner)
{
    if (!B) return PAY_ERR_ARG;
    pay_bty_person_t *p;
    pay_status_t st = person_get(B, owner, &p);
    if (st != PAY_OK) return st;
    if (p->reviewer) return PAY_DUPLICATE;
    p->reviewer = true;
    return PAY_OK;
}

static void strike(pay_bounty_ctx_t *B, uint32_t owner)
{
    pay_bty_person_t *p = person_of(B, owner);
    if (!p) return;
    p->strikes++;
    if (p->strikes >= B->cfg.max_strikes) p->suspended = true;
}

/* ===== bounties ===== */

static pay_bounty_t *bounty_of(pay_bounty_ctx_t *B, uint32_t id)
{
    if (id == 0 || id > B->n_b) return 0;
    return &B->b[id - 1];
}

const pay_bounty_t *pay_bounty_get(const pay_bounty_ctx_t *B, uint32_t id)
{
    return B ? bounty_of((pay_bounty_ctx_t *) B, id) : 0;
}

static bool live(const pay_bounty_t *b)
{
    return b->state == PAY_BTY_OPEN || b->state == PAY_BTY_DELIVERED ||
           b->state == PAY_BTY_REVIEW || b->state == PAY_BTY_HELD || b->state == PAY_BTY_APPROVED;
}

pay_status_t pay_bounty_post(pay_bounty_ctx_t *B, uint32_t poster, uint8_t kind, uint8_t tier,
                             const uint8_t subject[32], uint32_t *id_out)
{
    if (!B || !subject || !id_out) return PAY_ERR_ARG;
    if (pay_bounty_value(&B->cfg, kind, tier) == 0) return PAY_ERR_POLICY; /* B2 */
    uint32_t open = 0;
    for (uint32_t i = 0; i < B->n_b; i++) {
        const pay_bounty_t *b = &B->b[i];
        if (b->state != PAY_BTY_WITHDRAWN && pay_memeq(b->subject, subject, 32))
            return PAY_DUPLICATE;
        if (b->poster == poster && live(b)) open++;
    }
    if (open >= B->cfg.max_open) return PAY_ERR_LIMIT;
    if (B->n_b >= PAY_BTY_MAX) return PAY_ERR_FULL;
    pay_bounty_t *b = &B->b[B->n_b];
    pay_memset(b, 0, sizeof *b);
    b->id = ++B->n_b;
    b->state = PAY_BTY_OPEN;
    b->kind = kind;
    b->tier = tier;
    b->poster = poster;
    pay_memcpy(b->subject, subject, 32);
    B->stats.posted++;
    *id_out = b->id;
    return PAY_OK;
}

pay_status_t pay_bounty_withdraw(pay_bounty_ctx_t *B, uint32_t id, uint32_t poster)
{
    if (!B) return PAY_ERR_ARG;
    pay_bounty_t *b = bounty_of(B, id);
    if (!b) return PAY_ERR_NOT_FOUND;
    if (b->poster != poster) return PAY_ERR_POLICY;
    if (b->state != PAY_BTY_OPEN) return PAY_ERR_STATE;
    b->state = PAY_BTY_WITHDRAWN;
    return PAY_OK;
}

/* back to OPEN after a rejected or fraudulent delivery */
static void reopen(pay_bounty_ctx_t *B, pay_bounty_t *b)
{
    pay_bty_person_t *f = person_of(B, b->fixer);
    if (f && f->active) f->active--;
    b->state = PAY_BTY_OPEN;
    b->fixer = 0;
    b->audited = false;
    b->ruled = false;
    b->n_panel = 0;
    b->paid_tier = 0;
    b->approve_seq = 0;
    pay_memset(b->panel, 0, sizeof b->panel);
    pay_memset(b->vote, 0, sizeof b->vote);
    pay_memset(b->vote_tier, 0, sizeof b->vote_tier);
}

/* ===== B3 delivery ===== */

static uint64_t le64(const uint8_t *p)
{
    uint64_t v = 0;
    for (int i = 7; i >= 0; i--) v = (v << 8) | p[i];
    return v;
}

static int32_t seen_slot(const pay_bounty_ctx_t *B, const uint8_t d[32])
{
    uint8_t h[32];
    pay_sha3_256(d, 32, h);
    uint32_t i = (uint32_t) le64(h) & (PAY_BTY_SEEN - 1u);
    for (uint32_t k = 0; k < PAY_BTY_SEEN; k++) {
        uint32_t s = (i + k) & (PAY_BTY_SEEN - 1u);
        if (!B->seen_used[s]) return (int32_t) s;
        if (pay_memeq(B->seen[s], d, 32)) return (int32_t) s;
    }
    return -1;
}

void pay_bounty_delivery_digest(uint32_t id, uint32_t fixer, const uint8_t deliverable[32],
                                uint8_t out[32])
{
    pay_hbuf h;
    pay_hbuf_init(&h);
    pay_hbuf_str(&h, "ZXV-BOUNTY-DELIVER-v1");
    pay_hbuf_u64(&h, id);
    pay_hbuf_u64(&h, fixer);
    pay_hbuf_put(&h, deliverable, 32);
    pay_hbuf_final(&h, out);
}

pay_status_t pay_bounty_deliver(pay_bounty_ctx_t *B, uint32_t id, uint32_t fixer,
                                const uint8_t deliverable[32], const uint8_t *sig, size_t sig_len)
{
    if (!B || !deliverable || !sig || sig_len == 0) return PAY_ERR_ARG;
    pay_bounty_t *b = bounty_of(B, id);
    if (!b) return PAY_ERR_NOT_FOUND;
    if (b->state != PAY_BTY_OPEN) return PAY_ERR_STATE;
    if (fixer == b->poster && !B->cfg.allow_self_fix) return PAY_ERR_POLICY;
    pay_bty_person_t *f = person_of(B, fixer);
    if (f && f->suspended) return PAY_ERR_POLICY;
    if (f && f->active >= B->cfg.max_active) return PAY_ERR_LIMIT;
    int32_t s = seen_slot(B, deliverable);
    if (s < 0) return PAY_ERR_FULL;
    if (B->seen_used[s]) return PAY_ERR_REPLAY;
    uint8_t d[32];
    pay_bounty_delivery_digest(id, fixer, deliverable, d);
    if (!B->verify(B->cb_ctx, fixer, d, sig, sig_len)) return PAY_ERR_POLICY;
    if (!f) {
        pay_status_t st = person_get(B, fixer, &f);
        if (st != PAY_OK) return st;
    }
    pay_memcpy(B->seen[s], deliverable, 32);
    B->seen_used[s] = 1;
    f->active++;
    b->fixer = fixer;
    pay_memcpy(b->deliverable, deliverable, 32);
    b->state = PAY_BTY_DELIVERED;
    B->stats.delivered++;
    return PAY_OK;
}

/* ===== B4 + B5 audit and panel ===== */

static bool eligible(const pay_bty_person_t *p, const pay_bounty_t *b)
{
    return p->used && p->reviewer && !p->suspended && p->owner != b->poster && p->owner != b->fixer;
}

pay_status_t pay_bounty_audit(pay_bounty_ctx_t *B, uint32_t id, const uint8_t beacon[32])
{
    if (!B || !beacon) return PAY_ERR_ARG;
    pay_bounty_t *b = bounty_of(B, id);
    if (!b) return PAY_ERR_NOT_FOUND;
    if (b->state != PAY_BTY_DELIVERED) return PAY_ERR_STATE;
    uint32_t pool[PAY_BTY_PEOPLE], n = 0;
    for (uint32_t i = 0; i < B->n_p; i++)
        if (eligible(&B->p[i], b)) pool[n++] = B->p[i].owner;
    if (n < B->cfg.panel_size) return PAY_ERR_LIMIT; /* retry when the pool grows */

    pay_bty_audit_t a = B->audit(B->cb_ctx, b->kind, b->subject, b->deliverable);
    if (a == PAY_BTY_AUDIT_UNAVAILABLE) return PAY_ERR_STATE;
    if (a != PAY_BTY_AUDIT_PASS) {
        B->stats.audit_fail++;
        reopen(B, b);
        return PAY_ERR_POLICY;
    }
    B->stats.audit_pass++;
    b->audited = true;

    /* partial Fisher-Yates over the pool, driven by a SHA3 stream */
    uint8_t seed[32];
    pay_hbuf h;
    pay_hbuf_init(&h);
    pay_hbuf_str(&h, "ZXV-BOUNTY-PANEL-v1");
    pay_hbuf_put(&h, beacon, 32);
    pay_hbuf_u64(&h, id);
    pay_hbuf_put(&h, b->deliverable, 32);
    pay_hbuf_final(&h, seed);
    for (uint32_t k = 0; k < B->cfg.panel_size; k++) {
        uint8_t d[32];
        uint64_t rem;
        pay_hbuf_init(&h);
        pay_hbuf_put(&h, seed, 32);
        pay_hbuf_u64(&h, k);
        pay_hbuf_final(&h, d);
        pay_udiv64(le64(d), n - k, &rem);
        uint32_t j = k + (uint32_t) rem;
        uint32_t t = pool[k];
        pool[k] = pool[j];
        pool[j] = t;
        b->panel[k] = pool[k];
    }
    b->n_panel = B->cfg.panel_size;
    b->state = PAY_BTY_REVIEW;
    return PAY_OK;
}

/* ===== B6 votes and verdicts ===== */

void pay_bounty_vote_digest(uint32_t id, const uint8_t deliverable[32], uint32_t reviewer,
                            bool approve, uint8_t tier, uint8_t out[32])
{
    pay_hbuf h;
    pay_hbuf_init(&h);
    pay_hbuf_str(&h, "ZXV-BOUNTY-VOTE-v1");
    pay_hbuf_u64(&h, id);
    pay_hbuf_put(&h, deliverable, 32);
    pay_hbuf_u64(&h, reviewer);
    pay_hbuf_u64(&h, approve ? 1u : 0u);
    pay_hbuf_u64(&h, tier);
    pay_hbuf_final(&h, out);
}

pay_status_t pay_bounty_vote(pay_bounty_ctx_t *B, uint32_t id, uint32_t reviewer, bool approve,
                             uint8_t tier, const uint8_t *sig, size_t sig_len)
{
    if (!B || !sig || sig_len == 0) return PAY_ERR_ARG;
    pay_bounty_t *b = bounty_of(B, id);
    if (!b) return PAY_ERR_NOT_FOUND;
    if (b->state != PAY_BTY_REVIEW) return PAY_ERR_STATE;
    if (approve ? (tier < 1 || tier > b->tier) : tier != 0) return PAY_ERR_ARG;
    uint32_t k = 0;
    while (k < b->n_panel && b->panel[k] != reviewer) k++;
    if (k == b->n_panel) return PAY_ERR_POLICY; /* not on the panel */
    if (b->vote[k]) return PAY_DUPLICATE;
    const pay_bty_person_t *p = person_of(B, reviewer);
    if (!p || p->suspended) return PAY_ERR_POLICY;
    uint8_t d[32];
    pay_bounty_vote_digest(id, b->deliverable, reviewer, approve, tier, d);
    if (!B->verify(B->cb_ctx, reviewer, d, sig, sig_len)) return PAY_ERR_POLICY;
    b->vote[k] = approve ? 1u : 2u;
    b->vote_tier[k] = tier;
    return PAY_OK;
}

static pay_bty_verdict_t panel_verdict(const pay_bounty_t *b, uint32_t quorum, uint8_t *min_tier)
{
    uint32_t a = 0, r = 0;
    uint8_t mt = PAY_BTY_TIERS;
    for (uint32_t k = 0; k < b->n_panel; k++) {
        if (b->vote[k] == 1) {
            a++;
            if (b->vote_tier[k] < mt) mt = b->vote_tier[k];
        } else if (b->vote[k] == 2) {
            r++;
        }
    }
    if (min_tier) *min_tier = mt;
    if (a && r) return PAY_BTY_V_BOTH;
    if (a >= quorum) return PAY_BTY_V_TRUE;
    if (r >= quorum) return PAY_BTY_V_FALSE;
    return PAY_BTY_V_NEITHER;
}

pay_bty_verdict_t pay_bounty_verdict(const pay_bounty_ctx_t *B, uint32_t id)
{
    if (!B) return PAY_BTY_V_NEITHER;
    const pay_bounty_t *b = pay_bounty_get(B, id);
    if (!b) return PAY_BTY_V_NEITHER;
    switch (b->state) {
    case PAY_BTY_REVIEW:
        return panel_verdict(b, B->cfg.quorum, 0);
    case PAY_BTY_HELD:
        return PAY_BTY_V_BOTH;
    case PAY_BTY_APPROVED:
    case PAY_BTY_PAID:
        return PAY_BTY_V_TRUE;
    default:
        return PAY_BTY_V_NEITHER;
    }
}

void pay_bounty_rule_digest(uint32_t id, const uint8_t deliverable[32], bool approve, uint8_t tier,
                            uint8_t out[32])
{
    pay_hbuf h;
    pay_hbuf_init(&h);
    pay_hbuf_str(&h, "ZXV-BOUNTY-RULE-v1");
    pay_hbuf_u64(&h, id);
    pay_hbuf_put(&h, deliverable, 32);
    pay_hbuf_u64(&h, approve ? 1u : 0u);
    pay_hbuf_u64(&h, tier);
    pay_hbuf_final(&h, out);
}

static void approve_now(pay_bounty_ctx_t *B, pay_bounty_t *b, uint8_t tier)
{
    b->state = PAY_BTY_APPROVED;
    b->paid_tier = tier;
    b->approve_seq = ++B->approve_ctr;
}

pay_status_t pay_bounty_rule(pay_bounty_ctx_t *B, uint32_t id, bool approve, uint8_t tier,
                             const uint8_t *sig, size_t sig_len)
{
    if (!B || !sig || sig_len == 0) return PAY_ERR_ARG;
    pay_bounty_t *b = bounty_of(B, id);
    if (!b) return PAY_ERR_NOT_FOUND;
    if (b->state != PAY_BTY_HELD) return PAY_ERR_STATE;
    if (approve ? (tier < 1 || tier > b->tier) : tier != 0) return PAY_ERR_ARG;
    uint8_t d[32];
    pay_bounty_rule_digest(id, b->deliverable, approve, tier, d);
    if (!B->verify(B->cb_ctx, B->cfg.arbiter, d, sig, sig_len)) return PAY_ERR_POLICY;
    if (approve) {
        b->ruled = true;
        approve_now(B, b, tier);
    } else {
        B->stats.rejected++;
        reopen(B, b);
    }
    return PAY_OK;
}

/* ===== B9 fraud ===== */

void pay_bounty_fraud_digest(uint32_t id, const uint8_t deliverable[32], uint8_t out[32])
{
    pay_hbuf h;
    pay_hbuf_init(&h);
    pay_hbuf_str(&h, "ZXV-BOUNTY-FRAUD-v1");
    pay_hbuf_u64(&h, id);
    pay_hbuf_put(&h, deliverable, 32);
    pay_hbuf_final(&h, out);
}

pay_status_t pay_bounty_fraud(pay_bounty_ctx_t *B, uint32_t id, const uint8_t *sig, size_t sig_len)
{
    if (!B || !sig || sig_len == 0) return PAY_ERR_ARG;
    pay_bounty_t *b = bounty_of(B, id);
    if (!b) return PAY_ERR_NOT_FOUND;
    if (b->state != PAY_BTY_REVIEW && b->state != PAY_BTY_HELD && b->state != PAY_BTY_APPROVED &&
        b->state != PAY_BTY_PAID)
        return PAY_ERR_STATE;
    if (b->fraud) return PAY_DUPLICATE;
    uint8_t d[32];
    pay_bounty_fraud_digest(id, b->deliverable, d);
    if (!B->verify(B->cb_ctx, B->cfg.arbiter, d, sig, sig_len)) return PAY_ERR_POLICY;
    B->stats.frauds++;
    strike(B, b->fixer);
    for (uint32_t k = 0; k < b->n_panel; k++)
        if (b->vote[k] == 1) strike(B, b->panel[k]);
    if (b->state == PAY_BTY_PAID)
        b->fraud = true; /* isolation, never confiscation */
    else
        reopen(B, b); /* nothing mints; the issue itself still needs a fix */
    return PAY_OK;
}

/* ===== B6-B8 close an epoch ===== */

static void mint_ids(pay_bounty_ctx_t *B, pay_posting_req_t *rq, const pay_bounty_t *b,
                     uint64_t tick)
{
    pay_hbuf h;
    uint8_t d[32];
    pay_w w;
    pay_memset(rq, 0, sizeof *rq);
    pay_hbuf_init(&h);
    pay_hbuf_str(&h, "ZXV-BOUNTY-MINT-v1");
    pay_hbuf_u64(&h, B->epoch);
    pay_hbuf_u64(&h, b->id);
    pay_hbuf_put(&h, b->deliverable, 32);
    pay_hbuf_final(&h, d);
    pay_memcpy(rq->idem_key, d, 32);
    pay_uetr_from_random(d, rq->uetr);
    pay_w_init(&w, rq->e2e, sizeof rq->e2e);
    pay_w_s(&w, "BM");
    pay_w_hex(&w, d + 16, 12);
    pay_w_finish(&w);
    rq->tick = tick;
    rq->initiator = b->fixer;
    rq->kind = PAY_KIND_ISSUE;
    rq->ext_cap = PAY_CAP_SYSTEM;
}

static pay_status_t mint(pay_bounty_ctx_t *B, pay_bounty_t *b, pay_bty_person_t *f, uint64_t g,
                         uint64_t tick)
{
    uint64_t t, t_rem, part[PAY_ASSURE_BUCKETS];
    if (!pay_assure_fee_carry(f->fee_carry, g, &t, &t_rem)) return PAY_ERR_STATE;
    pay_assure_split(t, part);
    uint64_t net = g - t, Bm, Bf, Bn, fe;
    if (!pay_add_ok(B->minted, g, &Bm) || !pay_add_ok(B->fees, t, &Bf) ||
        !pay_add_ok(B->net, net, &Bn) || !pay_add_ok(f->earned, g, &fe))
        return PAY_ERR_OVERFLOW;
    pay_posting_req_t rq;
    mint_ids(B, &rq, b, tick);
    uint32_t n = 0;
    rq.lines[n].account = B->issuer_acct;
    rq.lines[n].d_debit = 0;
    rq.lines[n++].d_credit = (int64_t) g;
    if (net) {
        rq.lines[n].account = f->vfv_acct;
        rq.lines[n].d_debit = (int64_t) net;
        rq.lines[n++].d_credit = 0;
    }
    for (int k = 0; k < PAY_ASSURE_BUCKETS; k++) {
        if (!part[k]) continue;
        rq.lines[n].account = B->fee_acct[k];
        rq.lines[n].d_debit = (int64_t) part[k];
        rq.lines[n++].d_credit = 0;
    }
    rq.n_lines = n;
    pay_status_t st = pay_ledger_post(B->L, &rq, 0);
    if (st != PAY_OK) return st;
    B->minted = Bm;
    B->fees = Bf;
    B->net = Bn;
    f->earned = fe;
    f->fee_carry = t_rem;
    return PAY_OK;
}

pay_status_t pay_bounty_close_epoch(pay_bounty_ctx_t *B, uint64_t tick)
{
    if (!B) return PAY_ERR_ARG;
    pay_status_t err = PAY_OK;

    /* B6: take the verdicts of the panels still reviewing */
    for (uint32_t i = 0; i < B->n_b; i++) {
        pay_bounty_t *b = &B->b[i];
        if (b->state != PAY_BTY_REVIEW) continue;
        uint8_t mt;
        pay_bty_verdict_t v = panel_verdict(b, B->cfg.quorum, &mt);
        if (v == PAY_BTY_V_TRUE) {
            approve_now(B, b, mt);
        } else if (v == PAY_BTY_V_FALSE) {
            B->stats.rejected++;
            reopen(B, b);
        } else if (v == PAY_BTY_V_BOTH) {
            B->stats.held++;
            b->state = PAY_BTY_HELD;
        }
    }

    /* B7 + B8: mint in approval order within the caps; the rest carries */
    uint64_t fcap;
    if (!pay_muldiv(B->cfg.epoch_cap, B->cfg.fixer_share.num, B->cfg.fixer_share.den, &fcap, 0))
        return PAY_ERR_OVERFLOW;
    uint64_t last = 0;
    for (;;) {
        pay_bounty_t *nx = 0;
        for (uint32_t i = 0; i < B->n_b; i++) {
            pay_bounty_t *b = &B->b[i];
            if (b->state == PAY_BTY_APPROVED && b->approve_seq > last &&
                (!nx || b->approve_seq < nx->approve_seq))
                nx = b;
        }
        if (!nx) break;
        last = nx->approve_seq;
        pay_bty_person_t *f = person_of(B, nx->fixer);
        uint64_t g = pay_bounty_value(&B->cfg, nx->kind, nx->paid_tier);
        if (!f || g == 0) {
            err = PAY_ERR_STATE;
            continue;
        }
        if (B->epoch_minted + g > B->cfg.epoch_cap ||
            (f->epoch_minted && f->epoch_minted + g > fcap)) {
            B->stats.carried++;
            continue;
        }
        pay_status_t st = mint(B, nx, f, g, tick);
        if (st != PAY_OK) { /* stays APPROVED: tried again next epoch */
            err = st;
            continue;
        }
        B->epoch_minted += g;
        f->epoch_minted += g;
        if (f->active) f->active--;
        nx->paid = g;
        nx->paid_epoch = B->epoch;
        nx->state = PAY_BTY_PAID;
        B->stats.paid++;
    }

    B->epoch++;
    B->epoch_minted = 0;
    for (uint32_t i = 0; i < B->n_p; i++) B->p[i].epoch_minted = 0;
    return err;
}

/* ===== I1-I4 ===== */

bool pay_bounty_check(const pay_bounty_ctx_t *B)
{
    if (!B) return false;
    uint64_t sum = 0, earned = 0;
    for (uint32_t i = 0; i < B->n_b; i++) {
        const pay_bounty_t *b = &B->b[i];
        for (uint32_t k = 0; k < b->n_panel; k++) /* I3 */
            if (b->panel[k] == b->poster || b->panel[k] == b->fixer) return false;
        if (b->state != PAY_BTY_PAID) {
            if (b->paid) return false;
            continue;
        }
        if (!b->audited) return false; /* I1 */
        uint8_t mt;
        if (!b->ruled &&
            (panel_verdict(b, B->cfg.quorum, &mt) != PAY_BTY_V_TRUE || mt != b->paid_tier))
            return false;
        if (b->paid_tier < 1 || b->paid_tier > b->tier) return false;
        if (b->paid != pay_bounty_value(&B->cfg, b->kind, b->paid_tier)) return false;
        sum += b->paid;
    }
    for (uint32_t i = 0; i < B->n_p; i++) earned += B->p[i].earned;
    if (B->epoch_minted > B->cfg.epoch_cap) return false; /* I2 */
    if (sum != B->minted || earned != B->minted || B->minted != B->fees + B->net) return false;
    const pay_account_t *ia = pay_ledger_account(B->L, B->issuer_acct);
    if (!ia || ia->credit != B->minted) return false;
    return pay_ledger_check(B->L); /* I4 */
}
