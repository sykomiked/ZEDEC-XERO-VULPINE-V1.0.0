/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* dispute.c — the three-level dispute funnel. See dispute.h D1-D10. */
#include "dispute.h"
#include "../robin_debanks/sha256.h"

static const char TAG_PROOF[] = "zxv/dispute-proof/v1";
static const char TAG_COMMIT[] = "zxv/dispute-commit/v1";
static const char TAG_VOTE[] = "zxv/dispute-vote/v1";
static const char TAG_GW[] = "zxv/dispute-gw/v1";
static const char TAG_SEED[] = "zxv/dispute-seed/v1";
static const char TAG_JURY[] = "zxv/dispute-jury/v1";
static const char TAG_RULING[] = "zxv/dispute-ruling/v1";
_Static_assert(sizeof TAG_PROOF - 1 == 20, "proof tag");
_Static_assert(sizeof TAG_COMMIT - 1 == 21, "commit tag");
_Static_assert(sizeof TAG_GW - 1 == 17, "gw tag");

static void put32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++) p[i] = (uint8_t) (v >> (8 * i));
}

static void put64(uint8_t *p, uint64_t v)
{
    for (int i = 0; i < 8; i++) p[i] = (uint8_t) (v >> (8 * i));
}

static uint32_t tag(uint8_t *out, const char *t)
{
    uint32_t n = 0;
    while (t[n]) {
        out[n] = (uint8_t) t[n];
        n++;
    }
    return n;
}

/* ---- config ---- */
void dsp_config_default(dsp_config_t *cfg)
{
    if (!cfg) return;
    pay_memset(cfg, 0, sizeof *cfg);
    cfg->base_bond = 1000;
    cfg->juror_bond = 500;
    cfg->jury[0] = 3;
    cfg->jury[1] = 7;
    cfg->jury[2] = 15;
    cfg->commit_ticks = (uint64_t) 3u * 86400u;
    cfg->reveal_ticks = (uint64_t) 2u * 86400u;
    cfg->appeal_ticks = (uint64_t) 3u * 86400u;
    cfg->cred_weighted = false;
}

bool dsp_config_ok(const dsp_config_t *f)
{
    if (!f || !f->base_bond || f->base_bond >= PAY_BAL_MAX / 16u) return false;
    if (!f->juror_bond || f->juror_bond >= PAY_BAL_MAX / DSP_MAX_JURY) return false;
    if (!f->commit_ticks || !f->reveal_ticks || !f->appeal_ticks) return false;
    uint32_t sum = 0, prev = 1;
    for (uint32_t r = 0; r < DSP_ROUNDS; r++) {
        uint32_t j = f->jury[r];
        if (!(j & 1u) || j < prev || j > DSP_MAX_JURY) return false;
        prev = j;
        sum += j;
    }
    return sum <= DSP_MAX_POOL;
}

static const pay_account_t *acct(const dsp_t *D, uint32_t a)
{
    return pay_ledger_account(D->L, a);
}

dsp_status_t dsp_init(dsp_t *D, pay_ledger_t *L, const dsp_config_t *cfg, uint32_t escrow,
                      uint32_t reserve, uint32_t gateway, dsp_verify_fn verify, dsp_vrf_fn vrf,
                      dsp_pred_fn pred, void *ctx, const uint8_t seed[32])
{
    dsp_config_t d;
    if (!D || !L || !verify || !vrf || !seed) return DSP_ERR_ARG;
    if (!cfg) {
        dsp_config_default(&d);
        cfg = &d;
    }
    if (!dsp_config_ok(cfg)) return DSP_ERR_ARG;
    pay_memset(D, 0, sizeof *D);
    D->L = L;
    const pay_account_t *e = acct(D, escrow), *r = acct(D, reserve);
    if (!e || !r || escrow == reserve || e->asset != r->asset) return DSP_ERR_ARG;
    if ((e->flags | r->flags) & PAY_ACCT_ISSUER) return DSP_ERR_ARG;
    if (e->debit) return DSP_ERR_STATE;
    D->escrow = escrow;
    D->reserve = reserve;
    D->asset = e->asset;
    D->gateway = gateway;
    D->cfg = *cfg;
    D->verify = verify;
    D->vrf = vrf;
    D->pred = pred;
    D->ctx = ctx;
    pay_memcpy(D->seed, seed, 32);
    D->next_id = 1;
    D->last_ledger = PAY_OK;
    D->ready = true;
    return DSP_OK;
}

/* an account of the module's asset that is not one of its own */
static bool user_acct(const dsp_t *D, uint32_t a)
{
    const pay_account_t *p = acct(D, a);
    return p && p->asset == D->asset && a != D->escrow && a != D->reserve;
}

/* ---- pool ---- */
static int32_t juror_index(const dsp_t *D, uint32_t id)
{
    for (uint32_t i = 0; i < D->n_pool; i++)
        if (D->pool[i].id == id) return (int32_t) i;
    return -1;
}

dsp_status_t dsp_juror_add(dsp_t *D, uint32_t id, uint32_t a, uint64_t stake)
{
    if (!D || !D->ready || !stake || stake > DSP_STAKE_MAX || !user_acct(D, a)) return DSP_ERR_ARG;
    if (juror_index(D, id) >= 0) return DSP_ERR_DUP;
    if (D->n_pool >= DSP_MAX_POOL) return DSP_ERR_FULL;
    dsp_juror_t *j = &D->pool[D->n_pool++];
    j->id = id;
    j->acct = a;
    j->stake = stake;
    j->cred_bps = DSP_BPS;
    j->active = true;
    return DSP_OK;
}

dsp_status_t dsp_juror_set_cred(dsp_t *D, uint32_t id, uint32_t cred_bps)
{
    if (!D || cred_bps > DSP_BPS) return DSP_ERR_ARG;
    int32_t i = juror_index(D, id);
    if (i < 0) return DSP_ERR_UNKNOWN;
    D->pool[i].cred_bps = cred_bps;
    return DSP_OK;
}

dsp_status_t dsp_juror_remove(dsp_t *D, uint32_t id)
{
    if (!D) return DSP_ERR_ARG;
    int32_t i = juror_index(D, id);
    if (i < 0) return DSP_ERR_UNKNOWN;
    D->pool[i].active = false;
    return DSP_OK;
}

uint64_t dsp_juror_weight(const dsp_t *D, uint32_t k)
{
    if (!D || k >= D->n_pool || !D->pool[k].active) return 0;
    const dsp_juror_t *j = &D->pool[k];
    if (!D->cfg.cred_weighted) return j->stake;
    uint64_t w;
    pay_muldiv(j->stake, j->cred_bps, DSP_BPS, &w, 0); /* <= stake */
    return w;
}

/* ---- D1 ---- */
static dsp_case_t *find(dsp_t *D, uint32_t id)
{
    for (uint32_t i = 0; D && id && i < DSP_MAX_CASES; i++)
        if (D->c[i].state != DSP_ST_FREE && D->c[i].id == id) return &D->c[i];
    return 0;
}

const dsp_case_t *dsp_case(const dsp_t *D, uint32_t id)
{
    return find((dsp_t *) D, id);
}

dsp_status_t dsp_open(dsp_t *D, const uint8_t order[32], const uint8_t terms[32], uint32_t buyer,
                      uint32_t seller, uint32_t buyer_acct, uint32_t seller_acct, uint64_t escrow,
                      uint64_t tick, uint32_t *out_id)
{
    if (!D || !D->ready || !order || !terms || buyer == seller || buyer_acct == seller_acct)
        return DSP_ERR_ARG;
    if (!user_acct(D, buyer_acct) || !user_acct(D, seller_acct)) return DSP_ERR_ARG;
    dsp_case_t *slot = 0;
    for (uint32_t i = 0; i < DSP_MAX_CASES; i++) {
        if (D->c[i].state == DSP_ST_FREE) {
            if (!slot) slot = &D->c[i];
        } else if (pay_memeq(D->c[i].order, order, 32)) {
            return DSP_ERR_DUP;
        }
    }
    if (!slot) return DSP_ERR_FULL;
    pay_memset(slot, 0, sizeof *slot);
    slot->id = D->next_id++;
    slot->state = DSP_ST_PROOFS;
    slot->level = 1;
    pay_memcpy(slot->order, order, 32);
    pay_memcpy(slot->terms, terms, 32);
    slot->buyer = buyer;
    slot->seller = seller;
    slot->buyer_acct = buyer_acct;
    slot->seller_acct = seller_acct;
    slot->escrow = escrow;
    slot->opened = tick;
    if (out_id) *out_id = slot->id;
    return DSP_OK;
}

dsp_status_t dsp_release(dsp_t *D, uint32_t id)
{
    dsp_case_t *c = find(D, id);
    if (!c) return DSP_ERR_UNKNOWN;
    if (c->state != DSP_ST_FINAL) return DSP_ERR_STATE;
    pay_memset(c, 0, sizeof *c);
    return DSP_OK;
}

/* ---- ledger ---- */
static void derive(const dsp_t *D, uint8_t t, uint8_t out[32])
{
    uint8_t in[41];
    pay_memcpy(in, D->seed, 32);
    in[32] = t;
    put64(in + 33, D->seq);
    sha256(in, sizeof in, out);
}

static bool move(dsp_t *D, uint32_t from, uint32_t to, uint64_t amt, uint64_t tick,
                 const char *memo)
{
    pay_posting_req_t r;
    pay_receipt_t rc;
    uint8_t d[32];
    pay_w w;
    pay_memset(&r, 0, sizeof r);
    derive(D, 'I', r.idem_key);
    derive(D, 'U', d);
    pay_uetr_from_random(d, r.uetr);
    pay_w_init(&w, r.e2e, sizeof r.e2e);
    pay_w_s(&w, "DSP-");
    pay_w_u64(&w, D->seq);
    pay_w_finish(&w);
    pay_strlcpy(r.memo, memo, sizeof r.memo);
    r.tick = tick;
    r.kind = PAY_KIND_TRANSFER;
    r.ext_cap = PAY_CAP_FINANCIAL;
    D->seq++;
    D->last_ledger = pay_ledger_transfer(D->L, &r, from, to, amt, &rc);
    return D->last_ledger == PAY_OK;
}

static bool lock(dsp_t *D, uint32_t from, uint64_t amt, uint64_t tick)
{
    if (!move(D, from, D->escrow, amt, tick, "dispute bond lock")) return false;
    D->locked_total += amt;
    return true;
}

static bool give_back(dsp_t *D, uint32_t to, uint64_t amt, uint64_t tick)
{
    if (!move(D, D->escrow, to, amt, tick, "dispute bond return")) return false;
    D->returned_total += amt;
    return true;
}

static bool slash(dsp_t *D, uint64_t amt, uint64_t tick)
{
    if (!move(D, D->escrow, D->reserve, amt, tick, "dispute bond slash")) return false;
    D->slashed_total += amt;
    return true;
}

/* ---- D2 ---- */
void dsp_proof_msg(uint32_t id, const uint8_t order[32], uint8_t party, uint8_t kind,
                   const uint8_t hash[32], uint8_t out[DSP_PROOF_MSG])
{
    uint32_t n = tag(out, TAG_PROOF);
    put32(out + n, id);
    pay_memcpy(out + n + 4, order, 32);
    out[n + 36] = party;
    out[n + 37] = kind;
    pay_memcpy(out + n + 38, hash, 32);
}

static uint32_t party_id(const dsp_case_t *c, uint8_t party)
{
    return party == DSP_BUYER ? c->buyer : c->seller;
}

static uint32_t party_acct(const dsp_case_t *c, uint8_t party)
{
    return party == DSP_BUYER ? c->buyer_acct : c->seller_acct;
}

static void make_final(dsp_case_t *c, uint32_t award)
{
    c->award_bps = award;
    c->state = DSP_ST_FINAL;
}

dsp_status_t dsp_proof(dsp_t *D, uint32_t id, uint8_t party, uint8_t kind, const uint8_t hash[32],
                       const uint8_t *sig, uint32_t sig_len)
{
    if (!hash || (party != DSP_BUYER && party != DSP_SELLER)) return DSP_ERR_ARG;
    dsp_case_t *c = find(D, id);
    if (!c) return DSP_ERR_UNKNOWN;
    if (c->state != DSP_ST_PROOFS) return DSP_ERR_STATE;
    uint8_t m[DSP_PROOF_MSG];
    dsp_proof_msg(id, c->order, party, kind, hash, m);
    if (!D->verify(D->ctx, party_id(c, party), m, sizeof m, sig, sig_len)) return DSP_ERR_SIG;
    c->n_proofs++;
    int v = D->pred ? D->pred(D->ctx, c, party, kind, hash) : DSP_PRED_NONE;
    if (v == DSP_PRED_BUYER) {
        c->winner = DSP_BUYER;
        make_final(c, DSP_BPS);
        return DSP_OK;
    }
    if (v == DSP_PRED_SELLER) {
        c->winner = DSP_SELLER;
        make_final(c, 0);
        return DSP_OK;
    }
    return DSP_PENDING;
}

/* ---- D3 ---- */
uint64_t dsp_appeal_bond(const dsp_config_t *cfg, uint32_t round)
{
    if (!cfg || round < 1 || round > DSP_ROUNDS) return 0;
    return cfg->base_bond << (2u * (round - 1u)); /* base < 2^62 / 16 */
}

/* enter level-2 round r (1..3): SELECT, with an optional live appeal bond */
static void enter_round(dsp_case_t *c, uint8_t r)
{
    c->level = 2;
    c->round = r;
    c->state = DSP_ST_SELECT;
    c->n_jury = 0;
    c->jbond = 0;
    c->winner = DSP_NONE;
    c->appellant = DSP_NONE;
    c->appeal_bond = 0;
    c->appeal_live = false;
}

dsp_status_t dsp_escalate(dsp_t *D, uint32_t id, uint8_t party, uint64_t tick)
{
    if (party != DSP_BUYER && party != DSP_SELLER) return DSP_ERR_ARG;
    dsp_case_t *c = find(D, id);
    if (!c) return DSP_ERR_UNKNOWN;
    uint8_t next;
    if (c->state == DSP_ST_PROOFS) {
        next = 1;
    } else if (c->state == DSP_ST_APPEAL) {
        if (party == c->winner) return DSP_ERR_PARTY;
        if (tick >= c->appeal_deadline) return DSP_ERR_DEADLINE;
        if (c->round >= DSP_ROUNDS) {
            c->level = 3;
            c->state = DSP_ST_GATEWAY;
            return DSP_OK;
        }
        next = (uint8_t) (c->round + 1u);
    } else {
        return DSP_ERR_STATE;
    }
    uint64_t bond = dsp_appeal_bond(&D->cfg, next);
    if (!lock(D, party_acct(c, party), bond, tick)) return DSP_ERR_LEDGER;
    enter_round(c, next);
    c->appellant = party;
    c->appeal_bond = bond;
    c->appeal_live = true;
    return DSP_OK;
}

/* ---- D4 ---- */
void dsp_vrf_seed(const dsp_t *D, uint32_t id, uint8_t out[32])
{
    uint8_t m[19 + 32 + 4 + 1 + 32];
    const dsp_case_t *c = dsp_case(D, id);
    pay_memset(m, 0, sizeof m);
    uint32_t n = tag(m, TAG_SEED);
    if (D) pay_memcpy(m + n, D->seed, 32);
    put32(m + n + 32, id);
    if (c) {
        m[n + 36] = c->round;
        pay_memcpy(m + n + 37, c->order, 32);
    }
    sha256(m, sizeof m, out);
}

static uint64_t draw(const uint8_t out[32], uint32_t *ctr, uint64_t W)
{
    uint8_t t[19], c4[4], h[32];
    uint32_t n = tag(t, TAG_JURY);
    uint64_t r0, r;
    pay_udiv64(UINT64_MAX, W, &r0);
    r0 = r0 + 1 == W ? 0 : r0 + 1; /* 2^64 mod W */
    for (;;) {
        sha256_ctx_t sc;
        put32(c4, (*ctr)++);
        sha256_init(&sc);
        sha256_update(&sc, t, n);
        sha256_update(&sc, out, 32);
        sha256_update(&sc, c4, 4);
        sha256_final(&sc, h);
        uint64_t x = 0;
        for (int i = 7; i >= 0; i--) x = (x << 8) | h[i];
        if (r0 && x > UINT64_MAX - r0) continue; /* rejection: no modulo bias */
        pay_udiv64(x, W, &r);
        return r;
    }
}

dsp_status_t dsp_select(dsp_t *D, uint32_t id, const uint8_t *proof, uint32_t proof_len,
                        const uint8_t out[32], uint64_t tick)
{
    if (!out) return DSP_ERR_ARG;
    dsp_case_t *c = find(D, id);
    if (!c) return DSP_ERR_UNKNOWN;
    if (c->state != DSP_ST_SELECT) return DSP_ERR_STATE;
    uint64_t cd, rd;
    if (!pay_add_ok(tick, D->cfg.commit_ticks, &cd) || !pay_add_ok(cd, D->cfg.reveal_ticks, &rd))
        return DSP_ERR_OVERFLOW;
    uint8_t seed[32];
    dsp_vrf_seed(D, id, seed);
    if (!D->vrf(D->ctx, seed, proof, proof_len, out)) return DSP_ERR_VRF;
    uint32_t J = D->cfg.jury[c->round - 1u], cnt = 0;
    bool elig[DSP_MAX_POOL];
    uint64_t wt[DSP_MAX_POOL], W = 0;
    for (uint32_t k = 0; k < D->n_pool; k++) {
        const dsp_juror_t *j = &D->pool[k];
        const pay_account_t *a = acct(D, j->acct);
        wt[k] = dsp_juror_weight(D, k);
        elig[k] = wt[k] && j->id != c->buyer && j->id != c->seller && j->acct != c->buyer_acct &&
                  j->acct != c->seller_acct && !(c->used & (1u << k)) && a &&
                  a->debit >= D->cfg.juror_bond;
        if (elig[k]) {
            W += wt[k];
            cnt++;
        }
    }
    if (cnt < J) return DSP_ERR_POOL;
    uint8_t seat[DSP_MAX_JURY];
    uint32_t ctr = 0;
    for (uint32_t d = 0; d < J; d++) {
        uint64_t r = draw(out, &ctr, W);
        uint32_t k = 0;
        for (;; k++) {
            if (!elig[k]) continue;
            if (r < wt[k]) break;
            r -= wt[k];
        }
        seat[d] = (uint8_t) k;
        elig[k] = false;
        W -= wt[k];
    }
    for (uint32_t d = 0; d < J; d++) {
        if (!lock(D, D->pool[seat[d]].acct, D->cfg.juror_bond, tick)) {
            pay_status_t why = D->last_ledger;
            for (uint32_t e = 0; e < d; e++)
                give_back(D, D->pool[seat[e]].acct, D->cfg.juror_bond, tick);
            D->last_ledger = why;
            return DSP_ERR_LEDGER;
        }
    }
    c->n_jury = (uint8_t) J;
    for (uint32_t d = 0; d < J; d++) {
        c->seat[d] = seat[d];
        c->used |= 1u << seat[d];
        c->committed[d] = c->revealed[d] = c->settled[d] = false;
        c->vote[d] = 0;
        pay_memset(c->commit[d], 0, 32);
    }
    c->jbond = D->cfg.juror_bond;
    c->commit_deadline = cd;
    c->reveal_deadline = rd;
    c->state = DSP_ST_VOTE;
    return DSP_OK;
}

/* ---- D5 ---- */
void dsp_vote_commit(uint32_t id, uint32_t juror, uint8_t vote, const uint8_t salt[32],
                     uint8_t out[32])
{
    uint8_t m[19 + 4 + 4 + 1 + 32];
    uint32_t n = tag(m, TAG_VOTE);
    put32(m + n, id);
    put32(m + n + 4, juror);
    m[n + 8] = vote;
    pay_memcpy(m + n + 9, salt, 32);
    sha256(m, sizeof m, out);
}

void dsp_commit_msg(uint32_t id, uint8_t round, uint32_t juror, const uint8_t commit[32],
                    uint8_t out[DSP_COMMIT_MSG])
{
    uint32_t n = tag(out, TAG_COMMIT);
    put32(out + n, id);
    out[n + 4] = round;
    put32(out + n + 5, juror);
    pay_memcpy(out + n + 9, commit, 32);
}

static int32_t seat_of(const dsp_t *D, const dsp_case_t *c, uint32_t juror)
{
    for (uint32_t d = 0; d < c->n_jury; d++)
        if (D->pool[c->seat[d]].id == juror) return (int32_t) d;
    return -1;
}

dsp_status_t dsp_commit(dsp_t *D, uint32_t id, uint32_t juror, const uint8_t commit[32],
                        const uint8_t *sig, uint32_t sig_len, uint64_t tick)
{
    if (!commit) return DSP_ERR_ARG;
    dsp_case_t *c = find(D, id);
    if (!c) return DSP_ERR_UNKNOWN;
    if (c->state != DSP_ST_VOTE) return DSP_ERR_STATE;
    int32_t d = seat_of(D, c, juror);
    if (d < 0) return DSP_ERR_PARTY;
    if (tick >= c->commit_deadline) return DSP_ERR_DEADLINE;
    if (c->committed[d]) return DSP_ERR_DUP;
    uint8_t m[DSP_COMMIT_MSG];
    dsp_commit_msg(id, c->round, juror, commit, m);
    if (!D->verify(D->ctx, juror, m, sizeof m, sig, sig_len)) return DSP_ERR_SIG;
    pay_memcpy(c->commit[d], commit, 32);
    c->committed[d] = true;
    return DSP_OK;
}

static bool all(const dsp_case_t *c, const bool *flag)
{
    for (uint32_t d = 0; d < c->n_jury; d++)
        if (!flag[d]) return false;
    return true;
}

dsp_status_t dsp_reveal(dsp_t *D, uint32_t id, uint32_t juror, uint8_t vote, const uint8_t salt[32],
                        uint64_t tick)
{
    if (!salt || (vote != DSP_BUYER && vote != DSP_SELLER)) return DSP_ERR_ARG;
    dsp_case_t *c = find(D, id);
    if (!c) return DSP_ERR_UNKNOWN;
    if (c->state != DSP_ST_VOTE) return DSP_ERR_STATE;
    int32_t d = seat_of(D, c, juror);
    if (d < 0) return DSP_ERR_PARTY;
    if ((tick < c->commit_deadline && !all(c, c->committed)) || tick >= c->reveal_deadline)
        return DSP_ERR_DEADLINE;
    if (!c->committed[d]) return DSP_ERR_STATE;
    if (c->revealed[d]) return DSP_ERR_DUP;
    uint8_t h[32];
    dsp_vote_commit(id, juror, vote, salt, h);
    if (!pay_memeq(h, c->commit[d], 32)) return DSP_ERR_COMMIT;
    c->vote[d] = vote;
    c->revealed[d] = true;
    return DSP_OK;
}

/* ---- D6 / D7 ---- */
dsp_status_t dsp_tally(dsp_t *D, uint32_t id, uint64_t tick)
{
    dsp_case_t *c = find(D, id);
    if (!c) return DSP_ERR_UNKNOWN;
    if (c->state != DSP_ST_VOTE) return DSP_ERR_STATE;
    if (tick < c->reveal_deadline && !all(c, c->revealed)) return DSP_ERR_DEADLINE;
    uint64_t ad;
    if (!pay_add_ok(tick, D->cfg.appeal_ticks, &ad)) return DSP_ERR_OVERFLOW;
    uint32_t nb = 0, ns = 0;
    for (uint32_t d = 0; d < c->n_jury; d++)
        if (c->revealed[d]) {
            nb += c->vote[d] == DSP_BUYER;
            ns += c->vote[d] == DSP_SELLER;
        }
    uint8_t win = nb > ns ? DSP_BUYER : ns > nb ? DSP_SELLER : DSP_NONE;
    for (uint32_t d = 0; d < c->n_jury; d++) {
        if (c->settled[d]) continue;
        bool keep = c->revealed[d] && (win == DSP_NONE || c->vote[d] == win);
        bool ok = keep ? give_back(D, D->pool[c->seat[d]].acct, c->jbond, tick)
                       : slash(D, c->jbond, tick);
        if (!ok) return DSP_ERR_LEDGER;
        c->settled[d] = true;
    }
    if (c->appeal_live) {
        bool ok = (win == DSP_NONE || win == c->appellant)
                      ? give_back(D, party_acct(c, c->appellant), c->appeal_bond, tick)
                      : slash(D, c->appeal_bond, tick);
        if (!ok) return DSP_ERR_LEDGER;
        c->appeal_live = false;
    }
    if (win == DSP_NONE) {
        if (c->round >= DSP_ROUNDS) {
            c->level = 3;
            c->state = DSP_ST_GATEWAY;
        } else {
            enter_round(c, (uint8_t) (c->round + 1u));
        }
        return DSP_TIE;
    }
    c->winner = win;
    c->award_bps = win == DSP_BUYER ? DSP_BPS : 0;
    c->appeal_deadline = ad;
    c->state = DSP_ST_APPEAL;
    return DSP_OK;
}

dsp_status_t dsp_finalize(dsp_t *D, uint32_t id, uint64_t tick)
{
    dsp_case_t *c = find(D, id);
    if (!c) return DSP_ERR_UNKNOWN;
    if (c->state != DSP_ST_APPEAL) return DSP_ERR_STATE;
    if (tick < c->appeal_deadline) return DSP_ERR_DEADLINE;
    c->state = DSP_ST_FINAL;
    return DSP_OK;
}

/* ---- D8 ---- */
void dsp_gw_msg(uint32_t id, uint32_t award_bps, uint8_t out[DSP_GW_MSG])
{
    uint32_t n = tag(out, TAG_GW);
    put32(out + n, id);
    put32(out + n + 4, award_bps);
}

dsp_status_t dsp_gateway_rule(dsp_t *D, uint32_t id, uint32_t award_bps, const uint8_t *sig,
                              uint32_t sig_len)
{
    if (award_bps > DSP_BPS) return DSP_ERR_ARG;
    dsp_case_t *c = find(D, id);
    if (!c) return DSP_ERR_UNKNOWN;
    if (c->state != DSP_ST_GATEWAY) return DSP_ERR_STATE;
    uint8_t m[DSP_GW_MSG];
    dsp_gw_msg(id, award_bps, m);
    if (!D->verify(D->ctx, D->gateway, m, sizeof m, sig, sig_len)) return DSP_ERR_SIG;
    c->winner = award_bps > DSP_BPS / 2   ? DSP_BUYER
                : award_bps < DSP_BPS / 2 ? DSP_SELLER
                                          : DSP_NONE;
    make_final(c, award_bps);
    return DSP_OK;
}

/* ---- D9 ---- */
dsp_status_t dsp_ruling(const dsp_t *D, uint32_t id, dsp_ruling_t *out)
{
    if (!out) return DSP_ERR_ARG;
    const dsp_case_t *c = dsp_case(D, id);
    if (!c) return DSP_ERR_UNKNOWN;
    if (c->state != DSP_ST_FINAL) return DSP_ERR_STATE;
    out->award_bps = c->award_bps;
    out->level = c->level;
    out->round = c->level == 2 ? c->round : 0;
    out->loser = c->winner == DSP_BUYER    ? DSP_SELLER
                 : c->winner == DSP_SELLER ? DSP_BUYER
                                           : DSP_NONE;
    out->loser_id = out->loser ? party_id(c, out->loser) : 0;
    return DSP_OK;
}

dsp_status_t dsp_ruling_digest(const dsp_t *D, uint32_t id, uint8_t out[32])
{
    dsp_ruling_t r;
    if (!out) return DSP_ERR_ARG;
    dsp_status_t st = dsp_ruling(D, id, &r);
    if (st != DSP_OK) return st;
    uint8_t m[21 + 4 + 32 + 4 + 1 + 1];
    uint32_t n = tag(m, TAG_RULING);
    put32(m + n, id);
    pay_memcpy(m + n + 4, dsp_case(D, id)->order, 32);
    put32(m + n + 36, r.award_bps);
    m[n + 40] = r.level;
    m[n + 41] = r.round;
    sha256(m, sizeof m, out);
    return DSP_OK;
}

bool dsp_award_amount(uint64_t escrow, uint32_t award_bps, uint64_t *buyer, uint64_t *seller)
{
    uint64_t b;
    if (!buyer || !seller || award_bps > DSP_BPS) return false;
    pay_muldiv(escrow, award_bps, DSP_BPS, &b, 0); /* b <= escrow */
    *buyer = b;
    *seller = escrow - b;
    return true;
}

/* ---- D10 ---- */
static uint64_t live(const dsp_case_t *c)
{
    uint64_t s = c->appeal_live ? c->appeal_bond : 0;
    for (uint32_t d = 0; d < c->n_jury; d++)
        if (!c->settled[d]) s += c->jbond;
    return s;
}

uint64_t dsp_case_locked(const dsp_t *D, uint32_t id)
{
    const dsp_case_t *c = dsp_case(D, id);
    return c ? live(c) : 0;
}

bool dsp_check(const dsp_t *D)
{
    if (!D || !D->ready) return false;
    const pay_account_t *e = acct(D, D->escrow);
    if (!e || e->credit) return false;
    uint64_t sum = 0, out, net;
    for (uint32_t i = 0; i < DSP_MAX_CASES; i++) {
        const dsp_case_t *c = &D->c[i];
        uint64_t l = live(c);
        if (l && c->state != DSP_ST_SELECT && c->state != DSP_ST_VOTE) return false;
        sum += l;
    }
    if (!pay_add_ok(D->returned_total, D->slashed_total, &out) ||
        !pay_sub_ok(D->locked_total, out, &net))
        return false;
    return net == sum && e->debit == sum;
}
