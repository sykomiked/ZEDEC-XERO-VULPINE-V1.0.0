/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* mk_pay.c — the market's settle hook on pay_ledger. See mk_pay.h P1-P5. */
#include "mk_pay.h"
#include "../robin_debanks/sha256.h"

#define MK_PAY_AMT_MAX ((uint64_t) 1 << 60)

void mk_pay_init(mk_pay_t *p, pay_ledger_t *L, mk_pay_account_fn account_of, void *ctx,
                 const uint8_t seed[32], uint32_t owner)
{
    if (!p) return;
    pay_memset(p, 0, sizeof *p);
    p->L = L;
    p->account_of = account_of;
    p->ctx = ctx;
    if (seed) pay_memcpy(p->seed, seed, 32);
    p->owner = owner;
}

bool mk_pay_bind(mk_pay_t *p, uint8_t ccy, uint32_t escrow, uint32_t commons, uint32_t tax)
{
    if (!p || ccy == MK_NONE || ccy > MK_MAX_CCY) return false;
    p->pool[ccy] = (mk_pay_pool_t){true, escrow, commons, tax};
    return true;
}

/* P4: SHA-256(seed || tag || seq) */
static void derive(const mk_pay_t *p, uint8_t tag, uint8_t out[32])
{
    uint8_t in[32 + 1 + 8];
    pay_memcpy(in, p->seed, 32);
    in[32] = tag;
    for (int i = 0; i < 8; i++) in[33 + i] = (uint8_t) (p->seq >> (8 * i));
    sha256(in, sizeof in, out);
}

/* P1: at most five accounts (buyer, seller, escrow, commons, tax) */
_Static_assert(PAY_MAX_LINES >= 5, "one posting must hold every role");

/* add `net` to account a's line, merging coincident accounts; zero nets are
 * dropped after merging */
static void add_line(pay_posting_req_t *r, uint32_t a, int64_t net)
{
    for (uint32_t i = 0; i < r->n_lines; i++)
        if (r->lines[i].account == a) {
            r->lines[i].d_debit += net;
            return;
        }
    r->lines[r->n_lines++] = (pay_line_t){a, net, 0};
}

static bool refuse(mk_pay_t *p, pay_status_t why)
{
    p->refused++;
    p->last = why;
    return false;
}

bool mk_pay_settle(void *ctx, const mk_settle_t *s)
{
    mk_pay_t *p = ctx;
    if (!p) return false;
    if (!s || !p->L || !p->account_of || !s->buyer || !s->seller) return refuse(p, PAY_ERR_ARG);
    const uint64_t v[8] = {s->from_buyer, s->from_seller, s->from_commons, s->from_tax,
                           s->to_seller,  s->to_commons,  s->to_tax,       s->to_buyer};
    for (int i = 0; i < 8; i++)
        if (v[i] >= MK_PAY_AMT_MAX) return refuse(p, PAY_ERR_ARG); /* P5 */
    if (s->ccy == MK_NONE || s->ccy > MK_MAX_CCY || !p->pool[s->ccy].bound)
        return refuse(p, PAY_ERR_ARG); /* P3 */
    const mk_pay_pool_t *pl = &p->pool[s->ccy];
    uint32_t buyer = p->account_of(p->ctx, s->buyer, s->ccy);
    uint32_t seller = p->account_of(p->ctx, s->seller, s->ccy);
    if (buyer == MK_PAY_NONE || seller == MK_PAY_NONE) return refuse(p, PAY_ERR_ARG);

    int64_t in = (int64_t) (s->from_buyer + s->from_seller + s->from_commons + s->from_tax);
    int64_t out = (int64_t) (s->to_seller + s->to_commons + s->to_tax + s->to_buyer);
    pay_posting_req_t r;
    pay_memset(&r, 0, sizeof r);
    if (s->kind == MK_SETTLE_INVOICE && in != out) return refuse(p, PAY_ERR_UNBALANCED); /* P2 */
    add_line(&r, buyer, (int64_t) s->to_buyer - (int64_t) s->from_buyer);
    add_line(&r, seller, (int64_t) s->to_seller - (int64_t) s->from_seller);
    add_line(&r, pl->commons, (int64_t) s->to_commons - (int64_t) s->from_commons);
    add_line(&r, pl->tax, (int64_t) s->to_tax - (int64_t) s->from_tax);
    if (s->kind != MK_SETTLE_INVOICE) add_line(&r, pl->escrow, in - out);
    /* merged lines can cancel out */
    uint32_t k = 0;
    for (uint32_t i = 0; i < r.n_lines; i++)
        if (r.lines[i].d_debit != 0) r.lines[k++] = r.lines[i];
    r.n_lines = k;
    if (r.n_lines == 0) return true; /* P5: nothing moves */

    uint8_t h[32];
    derive(p, 'I', r.idem_key);
    derive(p, 'U', h);
    pay_uetr_from_random(h, r.uetr);
    pay_w w;
    pay_w_init(&w, r.e2e, sizeof r.e2e);
    pay_w_s(&w, "MKT-");
    pay_w_u64(&w, s->ref);
    pay_w_s(&w, "-");
    pay_w_u64(&w, p->seq);
    pay_w_finish(&w);
    pay_strlcpy(r.memo, "market settle", sizeof r.memo);
    r.initiator = p->owner;
    r.attestor = p->owner;
    r.tick = p->tick;
    r.kind = s->kind == MK_SETTLE_REFUND    ? PAY_KIND_RETURN
             : s->kind == MK_SETTLE_INVOICE ? PAY_KIND_TRANSFER
                                            : PAY_KIND_TRADE;
    r.ext_cap = PAY_CAP_FINANCIAL;
    p->seq++;
    pay_receipt_t rc;
    pay_status_t st = pay_ledger_post(p->L, &r, &rc);
    p->last = st;
    if (st != PAY_OK) return refuse(p, st);
    p->posted++;
    return true;
}
