/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* svault.c — the protocol solvency vault. See svault.h S1-S6. */
#include "svault.h"

static const pay_account_t *acct(const pay_ledger_t *L, uint32_t a)
{
    return (a < L->n_accounts && L->acct[a].active) ? &L->acct[a] : 0;
}

/* The four accounts in the order reserve, cash, issuer, vault V-Bills. */
static bool resolve(const svault_t *v, const pay_ledger_t *L, const pay_account_t *a[4])
{
    if (!v || !L || !v->ready) return false;
    const uint32_t id[4] = {v->reserve, v->cash, v->vbill_issuer, v->vbills};
    for (int i = 0; i < 4; i++)
        if (!(a[i] = acct(L, id[i]))) return false;
    return true;
}

svault_status_t svault_init(svault_t *v, const pay_ledger_t *L, uint32_t reserve, uint32_t cash,
                            uint32_t vbill_issuer, uint32_t vbills)
{
    if (!v || !L) return SVAULT_ERR_ARG;
    pay_memset(v, 0, sizeof *v);
    const pay_account_t *r = acct(L, reserve), *c = acct(L, cash), *i = acct(L, vbill_issuer),
                        *b = acct(L, vbills);
    if (!r || !c || !i || !b) return SVAULT_ERR_STATE;
    if (reserve == cash || vbill_issuer == vbills) return SVAULT_ERR_STATE;
    if (r->asset != c->asset || i->asset != b->asset || r->asset == i->asset)
        return SVAULT_ERR_STATE;
    if (!(i->flags & PAY_ACCT_ISSUER) || ((r->flags | c->flags | b->flags) & PAY_ACCT_ISSUER))
        return SVAULT_ERR_STATE;
    if (c->debit || b->debit) return SVAULT_ERR_STATE; /* credit is 0: not issuers */
    v->reserve = reserve;
    v->cash = cash;
    v->vbill_issuer = vbill_issuer;
    v->vbills = vbills;
    v->last_ledger = PAY_OK;
    v->ready = true;
    return SVAULT_OK;
}

bool svault_carry(uint64_t rem_in, uint64_t share, uint64_t *part, uint64_t *rem_out)
{
    uint64_t x, r;
    if (!part || !rem_out || rem_in >= SVAULT_SHARE_DEN || !pay_add_ok(share, rem_in, &x))
        return false;
    *part = pay_udiv64(x, SVAULT_SHARE_DEN, &r);
    *rem_out = r;
    return true;
}

uint64_t svault_cap(uint64_t n)
{
    return pay_udiv64(n, SVAULT_CAP_DEN, 0);
}

static uint64_t room_of(uint64_t n, uint64_t h)
{
    uint64_t cap = svault_cap(n);
    return h < cap ? cap - h : 0;
}

bool svault_balances(const svault_t *v, const pay_ledger_t *L, uint64_t *n, uint64_t *h,
                     uint64_t *cash, uint64_t *reserve)
{
    const pay_account_t *a[4];
    if (!resolve(v, L, a)) return false;
    if (n) *n = a[2]->credit;
    if (h) *h = a[3]->debit;
    if (cash) *cash = a[1]->debit;
    if (reserve) *reserve = a[0]->debit;
    return true;
}

uint64_t svault_room(const svault_t *v, const pay_ledger_t *L)
{
    uint64_t n, h;
    return svault_balances(v, L, &n, &h, 0, 0) ? room_of(n, h) : 0;
}

/* ---- S1 ---- */
svault_status_t svault_sweep(svault_t *v, pay_ledger_t *L, pay_posting_req_t *req,
                             uint64_t reserve_share)
{
    uint64_t n, h, part, rem, total;
    if (!req || !svault_balances(v, L, &n, &h, 0, 0)) return SVAULT_ERR_ARG;
    /* share_total >= carry, so this also bounds share + carry; and since
     * swept + overflow == floor(share_total / 3), neither of those can
     * overflow once share_total does not. */
    if (!pay_add_ok(v->share_total, reserve_share, &total)) return SVAULT_ERR_OVERFLOW;
    if (!svault_carry(v->carry, reserve_share, &part, &rem)) return SVAULT_ERR_STATE;
    svault_status_t st = SVAULT_ZERO;
    if (part && room_of(n, h) == 0) {
        v->overflow_total += part;
        st = SVAULT_OVERFLOW;
    } else if (part) {
        pay_receipt_t rc;
        req->kind = PAY_KIND_TRANSFER;
        v->last_ledger = pay_ledger_transfer(L, req, v->reserve, v->cash, part, &rc);
        if (v->last_ledger != PAY_OK) return SVAULT_ERR_LEDGER;
        v->swept_total += part;
        st = SVAULT_OK;
    }
    v->carry = rem;
    v->share_total = total;
    return st;
}

svault_status_t svault_sweep_split(svault_t *v, pay_ledger_t *L, pay_posting_req_t *req,
                                   const uint64_t parts[PAY_ASSURE_BUCKETS])
{
    if (!parts) return SVAULT_ERR_ARG;
    return svault_sweep(v, L, req, parts[PAY_ASSURE_RESERVE_FLOOR]);
}

svault_status_t svault_sweep_fee(svault_t *v, pay_ledger_t *L, pay_posting_req_t *req, uint64_t fee)
{
    uint64_t parts[PAY_ASSURE_BUCKETS];
    pay_assure_split(fee, parts);
    return svault_sweep_split(v, L, req, parts);
}

/* ---- S3 ---- */
static bool is_vault_acct(const svault_t *v, uint32_t a)
{
    return a == v->reserve || a == v->cash || a == v->vbill_issuer || a == v->vbills;
}

svault_status_t svault_buy(svault_t *v, pay_ledger_t *L, pay_posting_req_t *req,
                           uint32_t seller_fiat_acct, uint32_t seller_vbill_acct, uint64_t price,
                           uint64_t max_qty)
{
    uint64_t n, h, cash, bought;
    if (!req || price == 0 || !svault_balances(v, L, &n, &h, &cash, 0)) return SVAULT_ERR_ARG;
    if (is_vault_acct(v, seller_fiat_acct) || is_vault_acct(v, seller_vbill_acct))
        return SVAULT_ERR_ARG;
    v->last_qty = v->last_cost = 0;
    uint64_t qty = pay_udiv64(cash, price, 0), room = room_of(n, h);
    if (qty > room) qty = room;
    if (qty > max_qty) qty = max_qty;
    if (qty == 0) return SVAULT_ZERO;
    /* qty <= floor(cash / price), so cost <= cash < 2^62: no overflow; and
     * spent_total + cost <= swept_total by S6, so neither can spent_total. */
    uint64_t cost = qty * price;
    if (!pay_add_ok(v->vbills_bought, qty, &bought)) return SVAULT_ERR_OVERFLOW;
    const pay_line_t ln[4] = {{v->cash, -(int64_t) cost, 0},
                              {seller_fiat_acct, (int64_t) cost, 0},
                              {seller_vbill_acct, -(int64_t) qty, 0},
                              {v->vbills, (int64_t) qty, 0}};
    for (int i = 0; i < 4; i++) req->lines[i] = ln[i];
    req->n_lines = 4;
    req->kind = PAY_KIND_TRADE;
    pay_receipt_t rc;
    v->last_ledger = pay_ledger_post(L, req, &rc);
    if (v->last_ledger != PAY_OK) return SVAULT_ERR_LEDGER;
    v->spent_total += cost;
    v->vbills_bought = bought;
    v->last_qty = qty;
    v->last_cost = cost;
    return SVAULT_OK;
}

/* ---- S4 ---- */
bool zxv_verify_vault_solvency(uint64_t reserve, uint64_t n, uint64_t h, uint64_t floor_num,
                               uint64_t floor_den, uint64_t backing)
{
    if (floor_den == 0 || h > n) return false;
    pay_u128 lhs = pay_mul64(reserve, floor_den);
    pay_u128 t = pay_mul64(floor_num, n - h);
    /* rhs = t * backing = lo + (hi << 64), up to 192 bits */
    pay_u128 lo = pay_mul64(t.lo, backing), hi = pay_mul64(t.hi, backing);
    uint64_t top;
    if (hi.hi || !pay_add_ok(lo.hi, hi.lo, &top)) return false; /* rhs >= 2^128 > lhs */
    pay_u128 rhs = {top, lo.lo};
    return pay_u128_cmp(lhs, rhs) >= 0;
}

svault_status_t svault_redemption_floor(uint64_t reserve, uint64_t n, uint64_t h, uint64_t *num,
                                        uint64_t *den)
{
    if (!num || !den || h > n) return SVAULT_ERR_ARG;
    *num = n == h ? 0 : reserve;
    *den = n - h;
    return n == h ? SVAULT_ZERO : SVAULT_OK;
}

bool svault_verify_solvency(const svault_t *v, const pay_ledger_t *L, uint64_t floor_num,
                            uint64_t floor_den, uint64_t backing)
{
    uint64_t n, h, reserve;
    return svault_balances(v, L, &n, &h, 0, &reserve) &&
           zxv_verify_vault_solvency(reserve, n, h, floor_num, floor_den, backing);
}

svault_status_t svault_floor(const svault_t *v, const pay_ledger_t *L, uint64_t *num, uint64_t *den)
{
    uint64_t n, h, reserve;
    if (!svault_balances(v, L, &n, &h, 0, &reserve)) return SVAULT_ERR_ARG;
    return svault_redemption_floor(reserve, n, h, num, den);
}

/* ---- S5 ---- */
svault_status_t svault_first_loss(svault_t *v, pay_ledger_t *L, pay_posting_req_t *req,
                                  uint64_t loss_vbills, uint64_t *uncovered)
{
    uint64_t h;
    if (uncovered) *uncovered = loss_vbills;
    if (!req || !svault_balances(v, L, 0, &h, 0, 0)) return SVAULT_ERR_ARG;
    uint64_t b = loss_vbills < h ? loss_vbills : h;
    if (b == 0) return SVAULT_ZERO;
    pay_receipt_t rc;
    v->last_ledger = pay_ledger_redeem(L, req, v->vbills, v->vbill_issuer, b, &rc);
    if (v->last_ledger != PAY_OK) return SVAULT_ERR_LEDGER;
    v->vbills_burned += b; /* burned <= bought: S6 */
    if (uncovered) *uncovered = loss_vbills - b;
    return SVAULT_OK;
}

/* ---- S6 ---- */
bool svault_check(const svault_t *v, const pay_ledger_t *L)
{
    const pay_account_t *a[4];
    uint64_t s, t;
    if (!resolve(v, L, a)) return false;
    if (v->carry >= SVAULT_SHARE_DEN || !pay_add_ok(v->swept_total, v->overflow_total, &s) ||
        __builtin_mul_overflow(s, (uint64_t) SVAULT_SHARE_DEN, &t) ||
        !pay_add_ok(t, v->carry, &t) || t != v->share_total)
        return false;
    if (!pay_sub_ok(v->swept_total, v->spent_total, &s) || a[1]->debit != s || a[1]->credit)
        return false;
    if (!pay_sub_ok(v->vbills_bought, v->vbills_burned, &s) || a[3]->debit != s || a[3]->credit)
        return false;
    return true;
}
