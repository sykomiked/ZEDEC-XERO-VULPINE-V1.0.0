/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* prov_pay.c — settle hook over pay_ledger (see prov_pay.h). */
#include "prov_pay.h"

void prov_pay_init(prov_pay_t *p, pay_ledger_t *L, uint32_t initiator)
{
    if (!p) return;
    prov_memset(p, 0xFF, sizeof *p);
    p->L = L;
    p->initiator = initiator;
    p->tick = 0;
    p->last = PAY_OK;
}

static const char HEX[] = "0123456789abcdef";

static void ids(const prov_settlement_t *s, pay_posting_req_t *r)
{
    prov_hb_t h;
    uint8_t d[PROV_HASH_LEN];
    prov_hb_init(&h, "zxv-prov-pay-v1");
    prov_hb_u8(&h, s->kind);
    prov_hb_u32(&h, s->fill);
    prov_hb_put(&h, s->ref, PROV_HASH_LEN);
    prov_hb_final(&h, d);
    prov_memcpy(r->idem_key, d, 32);
    pay_uetr_from_random(d, r->uetr);
    prov_memcpy(r->e2e, "ZXVP-", 5);
    for (uint32_t i = 0; i < 15; i++) {
        r->e2e[5 + 2 * i] = HEX[d[16 + i] >> 4];
        r->e2e[6 + 2 * i] = HEX[d[16 + i] & 15];
    }
    r->e2e[35] = 0;
}

static bool line(pay_posting_req_t *r, uint32_t acct, int64_t d)
{
    if (acct == PROV_PAY_NOACCT) return false;
    if (d == 0) return true;
    r->lines[r->n_lines].account = acct;
    r->lines[r->n_lines].d_debit = d;
    r->lines[r->n_lines].d_credit = 0;
    r->n_lines++;
    return true;
}

#define AMT_MAX ((uint64_t) 1 << 62)

int prov_pay_settle(void *ctx, const prov_settlement_t *s)
{
    prov_pay_t *p = (prov_pay_t *) ctx;
    if (!p || !p->L || !s || s->asset >= PROV_MAX_ASSETS || s->user > PROV_MAX_USERS ||
        s->provider > PROV_MAX_PROVIDERS)
        return -1;
    if (s->hold >= AMT_MAX) return -1;
    uint32_t esc = p->escrow[s->asset], usr = p->user[s->asset][s->user];
    pay_posting_req_t r;
    prov_memset(&r, 0, sizeof r);
    ids(s, &r);
    r.initiator = p->initiator;
    r.tick = p->tick;
    r.kind = PAY_KIND_TRANSFER;
    bool ok = true;
    switch (s->kind) {
    case PROV_SETTLE_HOLD:
        ok = line(&r, usr, -(int64_t) s->hold) && line(&r, esc, (int64_t) s->hold);
        break;
    case PROV_SETTLE_RELEASE:
        ok = line(&r, esc, -(int64_t) s->hold) && line(&r, usr, (int64_t) s->hold);
        break;
    case PROV_SETTLE_FINAL:
        if (s->net + s->fee + s->refund != s->hold || s->net > s->hold || s->fee > s->hold)
            return -1;
        if (prov_charge_check(PROV_CHARGE_USAGE, s->hold, s->gross, false) != PROV_OK) return -1;
        r.kind = PAY_KIND_FEE;
        ok = line(&r, esc, -(int64_t) s->hold) &&
             line(&r, p->provider[s->asset][s->provider], (int64_t) s->net);
        {
            uint64_t part[PAY_ASSURE_BUCKETS];
            pay_assure_split(s->fee, part); /* sums to s->fee exactly */
            for (int b = 0; ok && b < PAY_ASSURE_BUCKETS; b++)
                ok = line(&r, p->fee_acct[s->asset][b], (int64_t) part[b]);
        }
        ok = ok && line(&r, usr, (int64_t) s->refund);
        break;
    default:
        return -1;
    }
    if (!ok) return -1;
    if (r.n_lines == 0) return 0; /* nothing moves (zero hold) */
    pay_receipt_t rc;
    p->last = pay_ledger_post(p->L, &r, &rc);
    return (p->last == PAY_OK || p->last == PAY_DUPLICATE) ? 0 : -1;
}
