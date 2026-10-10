/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* settle.c — the settlement spine. Design and invariants: settle.h. */
#include "settle.h"
#include "../mlkem/keccak.h"

#define SETTLE_POT_OWNER    0xFFFFFFFEu
#define SETTLE_BUCKET_OWNER SETTLE_RESERVED_IDS /* + bucket index */
#define SETTLE_NODE_OWNER   0u

static settle_status_t halt(settle_spine_t *s, settle_status_t why)
{
    s->halted = true;
    s->why = why;
    return why;
}

/* SHAKE256(seed || tag || seq) -> out: the posting identifiers. */
static void derive(const settle_spine_t *s, uint8_t tag, uint64_t seq, uint8_t *out, size_t n)
{
    uint8_t in[32 + 1 + 8];
    pay_memcpy(in, s->seed, 32);
    in[32] = tag;
    for (int i = 0; i < 8; i++) in[33 + i] = (uint8_t) (seq >> (8 * i));
    shake256(in, sizeof in, out, n);
}

static void new_request(settle_spine_t *s, pay_posting_req_t *req, uint64_t tick)
{
    uint8_t rnd[16];
    pay_memset(req, 0, sizeof *req);
    derive(s, 'I', s->seq, req->idem_key, sizeof req->idem_key);
    derive(s, 'U', s->seq, rnd, sizeof rnd);
    pay_uetr_from_random(rnd, req->uetr);
    pay_w w;
    pay_w_init(&w, req->e2e, sizeof req->e2e);
    pay_w_s(&w, "SWC-");
    pay_w_u64(&w, s->seq);
    pay_w_finish(&w);
    pay_strlcpy(req->memo, "swarm settle", sizeof req->memo);
    req->initiator = SETTLE_NODE_OWNER;
    req->tick = tick;
    req->attestor = SETTLE_NODE_OWNER;
    req->kind = PAY_KIND_TRANSFER;
    req->ext_cap = PAY_CAP_FINANCIAL;
}

settle_status_t settle_init(settle_spine_t *s, const uint8_t seed[32])
{
    if (!s || !seed) return SETTLE_ERR_ARG;
    pay_memset(s, 0, sizeof *s);
    pay_memcpy(s->seed, seed, 32);
    pay_ledger_init(&s->L, 0);
    if (pay_ledger_add_asset(&s->L, "SWC", 0, PAY_ASSET_UNIT, &s->asset) != PAY_OK ||
        pay_ledger_open(&s->L, SETTLE_NODE_OWNER, s->asset, PAY_CAP_FINANCIAL, PAY_ACCT_ISSUER,
                        &s->issuer) != PAY_OK ||
        pay_ledger_open(&s->L, SETTLE_POT_OWNER, s->asset, PAY_CAP_FINANCIAL, PAY_ACCT_COMMONS,
                        &s->pot) != PAY_OK)
        return halt(s, SETTLE_ERR_LEDGER);
    for (uint32_t i = 0; i < PAY_ASSURE_BUCKETS; i++)
        if (pay_ledger_open(&s->L, SETTLE_BUCKET_OWNER + i, s->asset, PAY_CAP_FINANCIAL,
                            PAY_ACCT_COMMONS, &s->bucket[i]) != PAY_OK)
            return halt(s, SETTLE_ERR_LEDGER);
    return SETTLE_OK;
}

static int32_t find(const settle_spine_t *s, uint32_t model_id)
{
    for (uint32_t i = 0; i < s->n; i++)
        if (s->model_id[i] == model_id) return (int32_t) i;
    return -1;
}

uint64_t settle_balance(const settle_spine_t *s, uint32_t model_id)
{
    if (!s) return 0;
    int32_t i = find(s, model_id);
    if (i < 0) return 0;
    const pay_account_t *a = pay_ledger_account(&s->L, s->acct[i]);
    return a ? a->debit : 0;
}

static uint64_t pot_balance(const settle_spine_t *s)
{
    const pay_account_t *a = pay_ledger_account(&s->L, s->pot);
    return a ? a->debit : 0;
}

uint64_t settle_bucket(const settle_spine_t *s, uint32_t bucket)
{
    if (!s || bucket >= PAY_ASSURE_BUCKETS) return 0;
    const pay_account_t *a = pay_ledger_account(&s->L, s->bucket[bucket]);
    return a ? a->debit : 0;
}

/* One balanced two-line transfer on the ledger of record. */
static bool post(settle_spine_t *s, uint32_t from, uint32_t to, uint64_t amount, uint64_t tick)
{
    pay_posting_req_t req;
    pay_receipt_t rc;
    new_request(s, &req, tick);
    if (pay_ledger_transfer(&s->L, &req, from, to, amount, &rc) != PAY_OK) return false;
    s->seq++;
    return true;
}

settle_status_t settle_join(settle_spine_t *s, uint32_t model_id, uint64_t endowment, uint64_t tick)
{
    if (!s) return SETTLE_ERR_ARG;
    if (s->halted) return SETTLE_ERR_HALTED;
    if (model_id == SETTLE_NODE_OWNER || model_id >= SETTLE_RESERVED_IDS || find(s, model_id) >= 0)
        return SETTLE_ERR_ARG;
    if (s->n >= SWARM_MAX_MODELS) return SETTLE_ERR_FULL;
    uint64_t issued;
    if (!pay_add_ok(s->issued, endowment, &issued) || issued >= SETTLE_ISSUE_MAX)
        return SETTLE_ERR_ARG;
    uint32_t acct;
    if (pay_ledger_open(&s->L, model_id, s->asset, PAY_CAP_FINANCIAL, 0, &acct) != PAY_OK)
        return SETTLE_ERR_LEDGER;
    if (endowment) {
        pay_posting_req_t req;
        pay_receipt_t rc;
        new_request(s, &req, tick);
        if (pay_ledger_issue(&s->L, &req, s->issuer, acct, endowment, &rc) != PAY_OK)
            return halt(s, SETTLE_ERR_LEDGER);
        s->seq++;
    }
    s->model_id[s->n] = model_id;
    s->acct[s->n] = acct;
    s->n++;
    s->issued = issued;
    return SETTLE_OK;
}

static const swarm_trader_t *trader(const swarm_market_t *m, uint32_t model_id)
{
    for (uint32_t k = 0; k < m->n; k++)
        if (m->t[k].model_id == model_id) return &m->t[k];
    return 0;
}

bool settle_reconciled(const settle_spine_t *s, const swarm_market_t *m)
{
    if (!s || !m || m->n != s->n) return false;
    for (uint32_t i = 0; i < s->n; i++) {
        const swarm_trader_t *t = trader(m, s->model_id[i]);
        if (!t || settle_balance(s, s->model_id[i]) != t->cap[SWARM_CAP_FINANCIAL]) return false;
    }
    uint64_t levied = 0;
    for (uint32_t i = 0; i < PAY_ASSURE_BUCKETS; i++) levied += settle_bucket(s, i);
    return pot_balance(s) == m->pot && levied == m->levied && pay_ledger_check(&s->L);
}

settle_status_t settle_sync(settle_spine_t *s, const swarm_market_t *m, uint64_t tick)
{
    if (!s || !m) return SETTLE_ERR_ARG;
    if (s->halted) return SETTLE_ERR_HALTED;
    if (m->n != s->n) return halt(s, SETTLE_ERR_UNKNOWN_MODEL);

    /* R1: the market conserves money and holds exactly what was issued.
     * Since issued < SETTLE_ISSUE_MAX (settle_join), every holding and the pot
     * are below it too once this passes, so the deltas below fit int64. */
    uint64_t sum = m->pot;
    if (!pay_add_ok(sum, m->levied, &sum)) return halt(s, SETTLE_ERR_CONSERVATION);
    for (uint32_t i = 0; i < s->n; i++) {
        const swarm_trader_t *t = trader(m, s->model_id[i]);
        if (!t) return halt(s, SETTLE_ERR_UNKNOWN_MODEL);
        if (!pay_add_ok(sum, t->cap[SWARM_CAP_FINANCIAL], &sum))
            return halt(s, SETTLE_ERR_CONSERVATION);
    }
    if (sum != m->money_supply || sum != s->issued) return halt(s, SETTLE_ERR_CONSERVATION);
    int64_t d[SWARM_MAX_MODELS + 1];
    uint32_t ac[SWARM_MAX_MODELS + 1];
    for (uint32_t i = 0; i < s->n; i++) {
        d[i] = (int64_t) trader(m, s->model_id[i])->cap[SWARM_CAP_FINANCIAL] -
               (int64_t) settle_balance(s, s->model_id[i]);
        ac[i] = s->acct[i];
    }
    d[s->n] = (int64_t) m->pot - (int64_t) pot_balance(s);
    ac[s->n] = s->pot;

    /* S3: largest decrease pays largest increase. Each step zeroes at least
     * one entry, so at most n + 1 postings. Every payer holds at least what
     * it gives up, because its target is >= 0. */
    uint32_t n = s->n + 1;
    for (uint32_t step = 0; step <= n; step++) {
        uint32_t lo = 0, hi = 0;
        for (uint32_t i = 1; i < n; i++) {
            if (d[i] < d[lo]) lo = i;
            if (d[i] > d[hi]) hi = i;
        }
        if (d[lo] >= 0 || d[hi] <= 0) break;
        uint64_t amt = (uint64_t) -d[lo] < (uint64_t) d[hi] ? (uint64_t) -d[lo] : (uint64_t) d[hi];
        if (!post(s, ac[lo], ac[hi], amt, tick)) return halt(s, SETTLE_ERR_LEDGER);
        d[lo] += (int64_t) amt;
        d[hi] -= (int64_t) amt;
    }

    /* R2 */
    if (!settle_reconciled(s, m)) return halt(s, SETTLE_ERR_MISMATCH);
    s->cycles++;
    return SETTLE_OK;
}

settle_status_t settle_levy(settle_spine_t *s, swarm_market_t *m, uint64_t tick)
{
    settle_status_t st = settle_sync(s, m, tick); /* F1 (and the NULL checks) */
    if (st != SETTLE_OK) return st;

    /* F2: last cycle's bounties go back into circulation */
    uint64_t back = settle_bucket(s, PAY_ASSURE_INFRA_BOUNTY);
    if (back) {
        if (!post(s, s->bucket[PAY_ASSURE_INFRA_BOUNTY], s->pot, back, tick))
            return halt(s, SETTLE_ERR_LEDGER);
        if (swarm_market_release(m, back) != SWARM_OK) return halt(s, SETTLE_ERR_MISMATCH);
        s->recycled += back;
    }

    /* F3: the fee on the pot, into the four buckets. The split is taken over
     * the running total, so small fees are not all rounded into the reserve:
     * the whole fee goes to the reserve floor, which passes each other bucket
     * what its share of the total has grown by (never negative, and never
     * more than the reserve then holds, since its own share stays >= 0). */
    uint64_t fee = 0, was[PAY_ASSURE_BUCKETS], now[PAY_ASSURE_BUCKETS];
    if (!pay_assure_charge(&s->carry, m->pot, &fee)) return halt(s, SETTLE_ERR_CONSERVATION);
    if (fee) {
        pay_assure_split(s->fees, was);
        pay_assure_split(s->fees + fee, now);
        uint32_t r = PAY_ASSURE_RESERVE_FLOOR; /* bucket 0; the others follow it */
        if (!post(s, s->pot, s->bucket[r], fee, tick)) return halt(s, SETTLE_ERR_LEDGER);
        for (uint32_t i = r + 1u; i < PAY_ASSURE_BUCKETS; i++)
            if (now[i] > was[i] &&
                !post(s, s->bucket[r], s->bucket[i], now[i] - was[i], tick))
                return halt(s, SETTLE_ERR_LEDGER);
        if (swarm_market_levy(m, fee) != SWARM_OK) return halt(s, SETTLE_ERR_MISMATCH);
        s->fees += fee;
    }

    if (!settle_reconciled(s, m)) return halt(s, SETTLE_ERR_MISMATCH); /* R2 */
    return SETTLE_OK;
}
