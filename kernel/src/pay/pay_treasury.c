/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* pay_treasury.c — m-of-n group treasury. See pay_treasury.h. */
#include "pay_treasury.h"

static const char DOM_PROP[] = "ZXV-TREASURY-PROPOSAL-v1";
static const char DOM_APPR[] = "ZXV-TREASURY-APPROVE-v1"; /* 23 bytes */
static const char DOM_REJ[] = "ZXV-TREASURY-REJECT-v1\0"; /* padded to 23 */

static int32_t member_index(const pay_treasury_t *T, uint32_t who)
{
    for (uint32_t i = 0; i < T->cfg.n_members; i++)
        if (T->cfg.members[i] == who) return (int32_t) i;
    return -1;
}

static uint32_t popcount32(uint32_t v)
{
    uint32_t c = 0;
    while (v) {
        v &= v - 1;
        c++;
    }
    return c;
}

bool pay_treasury_is_member(const pay_treasury_t *T, uint32_t who)
{
    return T && member_index(T, who) >= 0;
}

uint32_t pay_treasury_required(const pay_treasury_t *T, uint64_t amount)
{
    if (!T) return 0;
    if (T->cfg.large_amount && amount >= T->cfg.large_amount) return T->cfg.m_large;
    return T->cfg.m;
}

pay_status_t pay_treasury_init(pay_treasury_t *T, pay_ledger_t *L, const pay_treasury_cfg_t *cfg,
                               pay_tr_verify_fn verify, void *verify_ctx)
{
    if (!T || !L || !cfg || !verify) return PAY_ERR_ARG;
    if (cfg->n_members == 0 || cfg->n_members > PAY_TR_MAX_MEMBERS) return PAY_ERR_ARG;
    if (cfg->m == 0 || cfg->m > cfg->n_members) return PAY_ERR_ARG;
    if (cfg->m_large < cfg->m || cfg->m_large > cfg->n_members) return PAY_ERR_ARG;
    for (uint32_t i = 0; i < cfg->n_members; i++)
        for (uint32_t j = i + 1; j < cfg->n_members; j++)
            if (cfg->members[i] == cfg->members[j]) return PAY_ERR_ARG;
    if (!pay_ledger_asset(L, cfg->asset)) return PAY_ERR_NO_ASSET;
    pay_memset(T, 0, sizeof *T);
    T->L = L;
    T->cfg = *cfg;
    T->verify = verify;
    T->verify_ctx = verify_ctx;
    T->next_id = 1;
    pay_status_t st =
        pay_ledger_open(L, cfg->group_owner, cfg->asset, PAY_CAP_FINANCIAL, 0, &T->acct);
    return st;
}

static pay_tr_proposal_t *find(pay_treasury_t *T, uint64_t id)
{
    for (uint32_t i = 0; i < PAY_TR_MAX_PROPOSALS; i++)
        if (T->prop[i].used && T->prop[i].id == id) return &T->prop[i];
    return 0;
}

const pay_tr_proposal_t *pay_treasury_proposal(const pay_treasury_t *T, uint64_t id)
{
    if (!T) return 0;
    return find((pay_treasury_t *) T, id);
}

static void put_be(uint8_t *p, uint64_t v, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) p[i] = (uint8_t) (v >> (8 * (n - 1 - i)));
}

static void digest_of(const pay_treasury_t *T, const pay_tr_proposal_t *p, uint8_t out[32])
{
    pay_hbuf h;
    pay_hbuf_init(&h);
    pay_hbuf_str(&h, DOM_PROP);
    pay_hbuf_put(&h, T->cfg.id, 32);
    pay_hbuf_u64(&h, p->id);
    pay_hbuf_u64(&h, T->cfg.asset);
    pay_hbuf_u64(&h, p->to_acct);
    pay_hbuf_u64(&h, p->amount);
    pay_hbuf_u64(&h, p->created);
    pay_hbuf_u64(&h, p->expires);
    pay_hbuf_str(&h, p->memo);
    pay_hbuf_final(&h, out);
}

pay_status_t pay_treasury_propose(pay_treasury_t *T, uint32_t proposer, uint32_t to_acct,
                                  uint64_t amount, uint64_t tick, uint64_t ttl, const char *memo,
                                  uint64_t *out_id)
{
    if (out_id) *out_id = 0;
    if (!T || amount == 0 || ttl == 0) return PAY_ERR_ARG;
    if (member_index(T, proposer) < 0) return PAY_ERR_POLICY;
    const pay_account_t *to = pay_ledger_account(T->L, to_acct);
    if (!to || to_acct == T->acct) return PAY_ERR_NO_ACCOUNT;
    if (to->asset != T->cfg.asset) return PAY_ERR_ARG;
    if (T->cfg.limit_per_tx && amount > T->cfg.limit_per_tx) return PAY_ERR_LIMIT;
    if (T->cfg.limit_per_period && amount > T->cfg.limit_per_period) return PAY_ERR_LIMIT;
    uint64_t exp;
    if (!pay_add_ok(tick, ttl, &exp)) return PAY_ERR_OVERFLOW;
    pay_tr_proposal_t *slot = 0;
    for (uint32_t i = 0; i < PAY_TR_MAX_PROPOSALS && !slot; i++)
        if (!T->prop[i].used) slot = &T->prop[i];
    /* reuse a finished slot (oldest first by id) when the table is full */
    if (!slot) {
        for (uint32_t i = 0; i < PAY_TR_MAX_PROPOSALS; i++) {
            pay_tr_proposal_t *p = &T->prop[i];
            if (p->state == PAY_TR_OPEN || p->state == PAY_TR_APPROVED) continue;
            if (!slot || p->id < slot->id) slot = p;
        }
    }
    if (!slot) return PAY_ERR_FULL;
    pay_memset(slot, 0, sizeof *slot);
    slot->used = true;
    slot->id = T->next_id++;
    slot->state = PAY_TR_OPEN;
    slot->proposer = proposer;
    slot->to_acct = to_acct;
    slot->amount = amount;
    slot->created = tick;
    slot->expires = exp;
    if (memo && !pay_strlcpy(slot->memo, memo, sizeof slot->memo)) {
        slot->used = false;
        return PAY_ERR_ARG;
    }
    slot->required = pay_treasury_required(T, amount);
    digest_of(T, slot, slot->digest);
    if (out_id) *out_id = slot->id;
    return PAY_OK;
}

size_t pay_treasury_approval_msg(const pay_treasury_t *T, uint64_t id, uint32_t signer,
                                 bool approve, uint8_t out[PAY_TR_MSG_LEN])
{
    if (!T || !out) return 0;
    const pay_tr_proposal_t *p = pay_treasury_proposal(T, id);
    if (!p) return 0;
    pay_memcpy(out, approve ? DOM_APPR : DOM_REJ, 23);
    pay_memcpy(out + 23, T->cfg.id, 32);
    put_be(out + 55, id, 8);
    put_be(out + 63, signer, 4);
    pay_memcpy(out + 67, p->digest, 32);
    return PAY_TR_MSG_LEN;
}

static void log_add(pay_treasury_t *T, uint64_t id, uint32_t signer, bool approve, uint64_t tick,
                    const uint8_t *sig, size_t sig_len)
{
    pay_tr_log_t *e = &T->log[T->n_log % PAY_TR_LOG];
    pay_hbuf h;
    pay_memset(e, 0, sizeof *e);
    e->proposal = id;
    e->signer = signer;
    e->approve = approve ? 1u : 0u;
    e->tick = tick;
    pay_sha3_256(sig, sig_len, e->sig_hash);
    pay_memcpy(e->prev, T->log_head, PAY_HASH_LEN);
    pay_hbuf_init(&h);
    pay_hbuf_str(&h, "ZXV-TREASURY-LOG-v1");
    pay_hbuf_put(&h, e->prev, PAY_HASH_LEN);
    pay_hbuf_u64(&h, e->proposal);
    pay_hbuf_u64(&h, e->signer);
    pay_hbuf_u64(&h, e->approve);
    pay_hbuf_u64(&h, e->tick);
    pay_hbuf_put(&h, e->sig_hash, PAY_HASH_LEN);
    pay_hbuf_final(&h, e->hash);
    pay_memcpy(T->log_head, e->hash, PAY_HASH_LEN);
    T->n_log++;
}

static pay_status_t vote(pay_treasury_t *T, uint64_t id, uint32_t signer, const uint8_t *sig,
                         size_t sig_len, uint64_t tick, bool approve)
{
    if (!T || !sig || sig_len == 0) return PAY_ERR_ARG;
    pay_tr_proposal_t *p = find(T, id);
    if (!p) return PAY_ERR_NOT_FOUND;
    if ((p->state == PAY_TR_OPEN || p->state == PAY_TR_APPROVED) && tick >= p->expires)
        p->state = PAY_TR_EXPIRED;
    int32_t mi = member_index(T, signer);
    if (mi < 0) return PAY_ERR_POLICY;
    uint32_t bit = (uint32_t) 1 << (uint32_t) mi;
    if ((p->approved_mask | p->rejected_mask) & bit) return PAY_DUPLICATE;
    if (p->state != PAY_TR_OPEN) return PAY_ERR_STATE;
    uint8_t msg[PAY_TR_MSG_LEN];
    if (pay_treasury_approval_msg(T, id, signer, approve, msg) != PAY_TR_MSG_LEN)
        return PAY_ERR_ARG;
    if (!T->verify(T->verify_ctx, signer, msg, sizeof msg, sig, sig_len)) return PAY_ERR_POLICY;
    if (approve)
        p->approved_mask |= bit;
    else
        p->rejected_mask |= bit;
    log_add(T, id, signer, approve, tick, sig, sig_len);
    uint32_t yes = popcount32(p->approved_mask), no = popcount32(p->rejected_mask);
    if (yes >= p->required)
        p->state = PAY_TR_APPROVED;
    else if (T->cfg.n_members - no < p->required)
        p->state = PAY_TR_REJECTED;
    return PAY_OK;
}

pay_status_t pay_treasury_approve(pay_treasury_t *T, uint64_t id, uint32_t signer,
                                  const uint8_t *sig, size_t sig_len, uint64_t tick)
{
    return vote(T, id, signer, sig, sig_len, tick, true);
}

pay_status_t pay_treasury_reject(pay_treasury_t *T, uint64_t id, uint32_t signer,
                                 const uint8_t *sig, size_t sig_len, uint64_t tick)
{
    return vote(T, id, signer, sig, sig_len, tick, false);
}

static void roll_period(pay_treasury_t *T, uint64_t tick)
{
    if (T->cfg.period_len == 0) return;
    if (tick < T->period_start) return; /* ticks never go backwards here */
    uint64_t r;
    uint64_t k = pay_udiv64(tick - T->period_start, T->cfg.period_len, &r);
    if (k == 0) return;
    T->period_start = tick - r; /* start of the period containing tick */
    T->spent_period = 0;
}

pay_status_t pay_treasury_execute(pay_treasury_t *T, uint64_t id, uint64_t tick, pay_receipt_t *rc)
{
    if (!T) return PAY_ERR_ARG;
    pay_tr_proposal_t *p = find(T, id);
    if (!p) return PAY_ERR_NOT_FOUND;
    if (p->state == PAY_TR_EXECUTED) return PAY_DUPLICATE;
    if ((p->state == PAY_TR_OPEN || p->state == PAY_TR_APPROVED) && tick >= p->expires)
        p->state = PAY_TR_EXPIRED;
    if (p->state != PAY_TR_APPROVED) return PAY_ERR_STATE;
    if (popcount32(p->approved_mask) < p->required) return PAY_ERR_STATE;
    if (T->cfg.limit_per_tx && p->amount > T->cfg.limit_per_tx) return PAY_ERR_LIMIT;
    if (T->period_start == 0 && T->spent_period == 0 && T->spent_total == 0) T->period_start = tick;
    roll_period(T, tick);
    uint64_t ns;
    if (!pay_add_ok(T->spent_period, p->amount, &ns)) return PAY_ERR_OVERFLOW;
    if (T->cfg.limit_per_period && ns > T->cfg.limit_per_period) return PAY_ERR_LIMIT;

    pay_posting_req_t rq;
    pay_receipt_t r;
    pay_w w;
    pay_memset(&rq, 0, sizeof rq);
    pay_memcpy(rq.idem_key, p->digest, 32);
    pay_uetr_from_random(p->digest, rq.uetr);
    pay_w_init(&w, rq.e2e, sizeof rq.e2e);
    pay_w_s(&w, "TR");
    pay_w_hex(&w, p->digest + 16, 12);
    pay_w_finish(&w);
    pay_memcpy(rq.memo, p->memo, sizeof rq.memo);
    rq.initiator = T->cfg.group_owner;
    rq.tick = tick;
    rq.attestor = p->proposer;
    rq.kind = PAY_KIND_TRANSFER;
    pay_status_t st = pay_ledger_transfer(T->L, &rq, T->acct, p->to_acct, p->amount, &r);
    if (rc) *rc = r;
    if (st != PAY_OK && st != PAY_DUPLICATE) return st;
    p->state = PAY_TR_EXECUTED;
    p->ledger_seq = r.seq;
    if (st == PAY_OK) {
        T->spent_period = ns;
        T->spent_total += p->amount;
    }
    return st;
}

uint64_t pay_treasury_balance(const pay_treasury_t *T)
{
    if (!T) return 0;
    const pay_account_t *a = pay_ledger_account(T->L, T->acct);
    return a ? a->debit : 0;
}

bool pay_treasury_check(const pay_treasury_t *T)
{
    if (!T) return false;
    uint64_t total = 0;
    for (uint32_t i = 0; i < PAY_TR_MAX_PROPOSALS; i++) {
        const pay_tr_proposal_t *p = &T->prop[i];
        if (!p->used) continue;
        if (T->cfg.n_members < 32 && ((p->approved_mask | p->rejected_mask) >> T->cfg.n_members))
            return false;
        if (p->approved_mask & p->rejected_mask) return false;
        if (p->state == PAY_TR_EXECUTED) {
            if (popcount32(p->approved_mask) < p->required) return false;
            if (p->required < pay_treasury_required(T, p->amount)) return false;
            total += p->amount;
        }
    }
    /* Only proposals still in the table can be summed; if a finished slot
     * was reused the table total is a lower bound. */
    if (total > T->spent_total) return false;
    if (T->cfg.limit_per_period && T->spent_period > T->cfg.limit_per_period) return false;
    /* log chain over the window */
    uint64_t first = T->n_log > PAY_TR_LOG ? T->n_log - PAY_TR_LOG : 0;
    for (uint64_t k = first; k < T->n_log; k++) {
        const pay_tr_log_t *e = &T->log[k % PAY_TR_LOG];
        uint8_t hh[PAY_HASH_LEN];
        pay_hbuf h;
        if (k > first && !pay_memeq(e->prev, T->log[(k - 1) % PAY_TR_LOG].hash, PAY_HASH_LEN))
            return false;
        pay_hbuf_init(&h);
        pay_hbuf_str(&h, "ZXV-TREASURY-LOG-v1");
        pay_hbuf_put(&h, e->prev, PAY_HASH_LEN);
        pay_hbuf_u64(&h, e->proposal);
        pay_hbuf_u64(&h, e->signer);
        pay_hbuf_u64(&h, e->approve);
        pay_hbuf_u64(&h, e->tick);
        pay_hbuf_put(&h, e->sig_hash, PAY_HASH_LEN);
        pay_hbuf_final(&h, hh);
        if (!pay_memeq(hh, e->hash, PAY_HASH_LEN)) return false;
    }
    if (T->n_log && !pay_memeq(T->log_head, T->log[(T->n_log - 1) % PAY_TR_LOG].hash, PAY_HASH_LEN))
        return false;
    return pay_ledger_check(T->L);
}
