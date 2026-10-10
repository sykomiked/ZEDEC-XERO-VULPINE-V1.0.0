/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* pa_runtime.c — pay_ledger and swarm_budget transitions for peer_audit
 * replay. See pa_runtime.h. */
#include "pa_runtime.h"
#include "../robin_debanks/sha256.h"

static void put32(uint8_t *p, uint32_t v)
{
    for (uint32_t i = 0; i < 4; i++) p[i] = (uint8_t) (v >> (8u * i));
}
static void put64(uint8_t *p, uint64_t v)
{
    for (uint32_t i = 0; i < 8; i++) p[i] = (uint8_t) (v >> (8u * i));
}
static uint32_t get32(const uint8_t *p)
{
    return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) |
           ((uint32_t) p[3] << 24);
}
static uint64_t get64(const uint8_t *p)
{
    return (uint64_t) get32(p) | ((uint64_t) get32(p + 4) << 32);
}
static void h32(sha256_ctx_t *h, uint32_t v)
{
    uint8_t b[4];
    put32(b, v);
    sha256_update(h, b, 4);
}
static void h64(sha256_ctx_t *h, uint64_t v)
{
    uint8_t b[8];
    put64(b, v);
    sha256_update(h, b, 8);
}
static void zero(uint8_t *p, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) p[i] = 0;
}

/* ===== Encoding ===== */
void pa_xfer_encode(const pa_xfer_t *x, uint8_t out[PA_XFER_IN_LEN])
{
    zero(out, PA_XFER_IN_LEN);
    put32(out + 0, x->from);
    put32(out + 4, x->to);
    put64(out + 8, (uint64_t) x->amount);
    put32(out + 16, x->initiator);
    put64(out + 20, x->tick);
    put64(out + 28, x->nonce);
    pay_memcpy(out + 36, x->idem, 32);
    pay_memcpy(out + 68, x->rnd, 16);
    put32(out + 84, x->attestor);
    put32(out + 88, 0);
    put32(out + 92, 0);
}

bool pa_xfer_decode(const uint8_t *in, uint32_t len, pa_xfer_t *x)
{
    if (!in || !x || len != PA_XFER_IN_LEN) return false;
    if (get32(in + 88) != 0 || get32(in + 92) != 0) return false;
    x->from = get32(in + 0);
    x->to = get32(in + 4);
    x->amount = (int64_t) get64(in + 8);
    x->initiator = get32(in + 16);
    x->tick = get64(in + 20);
    x->nonce = get64(in + 28);
    pay_memcpy(x->idem, in + 36, 32);
    pay_memcpy(x->rnd, in + 68, 16);
    x->attestor = get32(in + 84);
    return true;
}

void pa_budget_encode(const pa_budget_req_t *b, uint8_t out[PA_BUDGET_IN_LEN])
{
    put32(out + 0, b->op);
    put32(out + 4, b->model);
    put64(out + 8, b->requested);
}

/* ===== State hashes ===== */
void pa_ledger_state_hash(const pay_ledger_t *L, uint8_t out[PA_HASH_LEN])
{
    sha256_ctx_t h;
    sha256_init(&h);
    sha256_update(&h, (const uint8_t *) "zxv-pa-ledger-v1", 16);
    h64(&h, L->seq);
    sha256_update(&h, L->chain_head, PAY_HASH_LEN);
    h32(&h, L->n_accounts);
    for (uint32_t i = 0; i < L->n_accounts && i < PAY_MAX_ACCOUNTS; i++) {
        const pay_account_t *a = &L->acct[i];
        h32(&h, a->owner);
        h32(&h, (uint32_t) a->asset | ((uint32_t) a->cap << 16) | ((uint32_t) a->flags << 24));
        h64(&h, a->debit);
        h64(&h, a->credit);
        h64(&h, (uint64_t) a->equity);
        h32(&h, a->active ? 1u : 0u);
    }
    sha256_final(&h, out);
}

void pa_budget_state_hash(const swarm_budget_t *b, uint8_t out[PA_HASH_LEN])
{
    sha256_ctx_t h;
    sha256_init(&h);
    sha256_update(&h, (const uint8_t *) "zxv-pa-budget-v1", 16);
    h32(&h, b->num_levels);
    h64(&h, b->tokens_per_cycle);
    h64(&h, b->cycle);
    h32(&h, b->cycle_open ? 1u : 0u);
    h32(&h, b->num_slots);
    for (uint32_t i = 0; i < b->num_slots && i < SWARM_MAX_MODELS; i++) {
        const swarm_slot_t *s = &b->slots[i];
        h32(&h, s->model_id);
        h32(&h, (uint32_t) s->level | (s->active ? 0x100u : 0u));
        h64(&h, s->allotted);
        h64(&h, s->allotted_im);
        h64(&h, s->allotted_mk);
        h64(&h, s->used);
    }
    for (uint32_t i = 0; i < SWARM_MAX_LEVELS; i++) h64(&h, b->level_budget[i]);
    h64(&h, b->last_unused);
    sha256_final(&h, out);
}

/* ===== Transitions ===== */
static void hexbyte(char *o, uint8_t v)
{
    static const char hx[] = "0123456789abcdef";
    o[0] = hx[v >> 4];
    o[1] = hx[v & 15u];
}

void pa_ledger_apply(pay_ledger_t *L, const uint8_t *in, uint32_t in_len,
                     uint8_t out[PA_XFER_OUT_LEN])
{
    zero(out, PA_XFER_OUT_LEN);
    pa_xfer_t x;
    pay_status_t st = PAY_ERR_ARG;
    pay_receipt_t rc;
    pay_memset(&rc, 0, sizeof rc);
    if (L && pa_xfer_decode(in, in_len, &x) && x.amount > 0 && (uint64_t) x.amount < PAY_BAL_MAX &&
        x.from < L->n_accounts && x.to < L->n_accounts) {
        pay_posting_req_t req;
        pay_memset(&req, 0, sizeof req);
        pay_memcpy(req.idem_key, x.idem, 32);
        pay_uetr_from_random(x.rnd, req.uetr);
        /* end-to-end id derived from the idempotency key: "PA" + 16 hex */
        req.e2e[0] = 'P';
        req.e2e[1] = 'A';
        for (uint32_t i = 0; i < 8; i++) hexbyte(&req.e2e[2 + 2 * i], x.idem[i]);
        req.e2e[18] = 0;
        req.initiator = x.initiator;
        req.nonce = x.nonce;
        req.tick = x.tick;
        req.attestor = x.attestor;
        req.kind = PAY_KIND_TRANSFER;
        st = pay_ledger_transfer(L, &req, x.from, x.to, (uint64_t) x.amount, &rc);
    }
    put32(out + 0, (uint32_t) (int32_t) st);
    if (st == PAY_OK) {
        put64(out + 8, L->acct[x.from].debit);
        put64(out + 16, L->acct[x.to].debit);
        pay_memcpy(out + 24, rc.hash, 32);
    }
}

void pa_budget_apply(swarm_budget_t *b, const uint8_t *in, uint32_t in_len,
                     uint8_t out[PA_BUDGET_OUT_LEN])
{
    zero(out, PA_BUDGET_OUT_LEN);
    swarm_status_t st = SWARM_ERR_ARG;
    uint64_t granted = 0, remaining = 0;
    if (b && in && in_len == PA_BUDGET_IN_LEN) {
        uint32_t op = get32(in), model = get32(in + 4);
        uint64_t req = get64(in + 8);
        if (op == PA_BUDGET_BEGIN && model == 0 && req == 0)
            st = swarm_budget_begin_cycle(b);
        else if (op == PA_BUDGET_CONSUME)
            st = swarm_budget_consume(b, model, req, &granted);
        else if (op == PA_BUDGET_END && model == 0 && req == 0)
            st = swarm_budget_end_cycle(b);
        remaining = swarm_budget_remaining(b, model);
    }
    put32(out + 0, (uint32_t) (int32_t) st);
    put64(out + 8, granted);
    put64(out + 16, remaining);
}

pa_status_t pa_runtime_replay(void *ctx, uint32_t kind, const uint8_t prev[PA_HASH_LEN],
                              const uint8_t *in, uint32_t in_len, uint8_t *out, uint32_t *out_len,
                              uint8_t new_state[PA_HASH_LEN])
{
    pa_runtime_t *rt = (pa_runtime_t *) ctx;
    if (!rt || !prev || !out || !out_len || !new_state) return PA_ERR_ARG;
    uint8_t h[PA_HASH_LEN];
    if (kind == PA_KIND_LEDGER_TRANSFER) {
        if (!rt->ledger || !rt->ledger_tmp || *out_len < PA_XFER_OUT_LEN) return PA_ERR_ARG;
        pa_ledger_state_hash(rt->ledger, h);
        if (!pay_memeq(h, prev, PA_HASH_LEN)) return PA_ERR_NO_STATE;
        pay_memcpy(rt->ledger_tmp, rt->ledger, sizeof *rt->ledger);
        pa_ledger_apply(rt->ledger_tmp, in, in_len, out);
        *out_len = PA_XFER_OUT_LEN;
        if (!pay_ledger_check(rt->ledger_tmp)) return PA_ERR_INVARIANT;
        pa_ledger_state_hash(rt->ledger_tmp, new_state);
        return PA_OK;
    }
    if (kind == PA_KIND_BUDGET) {
        if (!rt->budget || !rt->budget_tmp || *out_len < PA_BUDGET_OUT_LEN) return PA_ERR_ARG;
        pa_budget_state_hash(rt->budget, h);
        if (!pay_memeq(h, prev, PA_HASH_LEN)) return PA_ERR_NO_STATE;
        pay_memcpy(rt->budget_tmp, rt->budget, sizeof *rt->budget);
        pa_budget_apply(rt->budget_tmp, in, in_len, out);
        *out_len = PA_BUDGET_OUT_LEN;
        pa_budget_state_hash(rt->budget_tmp, new_state);
        return PA_OK;
    }
    return PA_ERR_KIND;
}

/* ===== Self-audit inputs ===== */
static uint64_t debit_total(const pay_ledger_t *L, uint16_t asset)
{
    uint64_t sum = 0;
    for (uint32_t c = 0; c < PAY_CAP_COUNT; c++) {
        uint64_t d = 0;
        pay_ledger_totals(L, asset, (pay_cap_t) c, &d, (uint64_t *) 0, (int64_t *) 0);
        if (!pay_add_ok(sum, d, &sum)) return UINT64_MAX;
    }
    return sum;
}

void pa_ledger_precommit(const pay_ledger_t *before, const pay_ledger_t *after, uint16_t asset,
                         int64_t headroom, pa_precommit_t *p)
{
    pay_memset(p, 0, sizeof *p);
    p->sum_before = debit_total(before, asset);
    p->sum_after = debit_total(after, asset);
    /* A balance broken beyond L2/L3 is a conservation failure on its own. */
    if (!pay_ledger_check(after)) p->sum_after = p->sum_before ^ 1u;
    p->headroom = headroom;
    p->size = PA_XFER_OUT_LEN;
    p->size_max = PA_MAX_IO;
}

static uint64_t remaining_total(const swarm_budget_t *b)
{
    uint64_t sum = 0;
    for (uint32_t i = 0; i < b->num_slots && i < SWARM_MAX_MODELS; i++) {
        const swarm_slot_t *s = &b->slots[i];
        uint64_t r = s->allotted >= s->used ? s->allotted - s->used : 0;
        if (!pay_add_ok(sum, r, &sum)) return UINT64_MAX;
    }
    return sum;
}

void pa_budget_precommit(const swarm_budget_t *before, const swarm_budget_t *after,
                         const pa_budget_req_t *req, int64_t headroom, pa_precommit_t *p)
{
    pay_memset(p, 0, sizeof *p);
    /* granted tokens leave the pool: remaining_after + granted == before */
    uint64_t granted = 0;
    if (req->op == PA_BUDGET_CONSUME) {
        uint64_t used_b = 0, used_a = 0;
        for (uint32_t i = 0; i < before->num_slots && i < SWARM_MAX_MODELS; i++)
            if (before->slots[i].model_id == req->model) used_b = before->slots[i].used;
        for (uint32_t i = 0; i < after->num_slots && i < SWARM_MAX_MODELS; i++)
            if (after->slots[i].model_id == req->model) used_a = after->slots[i].used;
        granted = used_a >= used_b ? used_a - used_b : UINT64_MAX;
        p->sum_before = remaining_total(before);
        p->sum_after = remaining_total(after);
        p->burned = granted;
        p->tokens_requested = granted;
        p->tokens_remaining = swarm_budget_remaining(before, req->model);
    }
    /* begin/end cycle re-allot or expire tokens by rule: no conservation
     * claim (both sides 0); replay still checks them bit for bit. */
    p->headroom = headroom;
    p->size = PA_BUDGET_OUT_LEN;
    p->size_max = PA_MAX_IO;
}

pa_status_t pa_runtime_apply(pa_runtime_t *rt, const pa_record_t *r)
{
    if (!rt || !r) return PA_ERR_ARG;
    uint8_t h[PA_HASH_LEN], out[PA_MAX_IO];
    if (r->kind == PA_KIND_LEDGER_TRANSFER && rt->ledger) {
        pa_ledger_state_hash(rt->ledger, h);
        if (!pay_memeq(h, r->prev_state, PA_HASH_LEN)) return PA_ERR_NO_STATE;
        pa_ledger_apply(rt->ledger, r->in, r->in_len, out);
        return PA_OK;
    }
    if (r->kind == PA_KIND_BUDGET && rt->budget) {
        pa_budget_state_hash(rt->budget, h);
        if (!pay_memeq(h, r->prev_state, PA_HASH_LEN)) return PA_ERR_NO_STATE;
        pa_budget_apply(rt->budget, r->in, r->in_len, out);
        return PA_OK;
    }
    return PA_ERR_KIND;
}
