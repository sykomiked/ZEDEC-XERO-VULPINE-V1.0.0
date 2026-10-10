/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* pay_equity.c — equity rails, order book, VFV currency equity. */
#include "pay_equity.h"
#include "swarm_budget.h"
#include "swarm_market.h"

#define EQ_MAX_AMOUNT ((uint64_t) 1 << 59)

void pay_equity_cfg_default(pay_equity_cfg_t *c)
{
    if (!c) return;
    pay_memset(c, 0, sizeof *c);
    c->enabled = false;
    c->role_mask = 0;
    c->default_cap.num = SWARM_MKT_FLOOR_NUM;
    c->default_cap.den = 21;
}

pay_status_t pay_equity_init(pay_equity_t *ex, pay_ledger_t *L, const pay_equity_cfg_t *cfg)
{
    if (!ex || !L) return PAY_ERR_ARG;
    pay_memset(ex, 0, sizeof *ex);
    ex->L = L;
    if (cfg)
        ex->cfg = *cfg;
    else
        pay_equity_cfg_default(&ex->cfg);
    if (ex->cfg.default_cap.den == 0) return PAY_ERR_ARG;
    ex->next_order_id = 1;
    return PAY_OK;
}

/* ===== Audit trail ===== */

static void audit(pay_equity_t *ex, pay_eq_event_kind_t k, uint32_t inst, uint64_t oa, uint64_t ob,
                  uint32_t wa, uint32_t wb, uint64_t qty, uint64_t price)
{
    pay_eq_event_t *e = &ex->audit[ex->n_events & (PAY_EQ_AUDIT - 1u)];
    pay_hbuf h;
    e->seq = ex->n_events;
    e->kind = (uint8_t) k;
    e->inst = inst;
    e->order_a = oa;
    e->order_b = ob;
    e->owner_a = wa;
    e->owner_b = wb;
    e->qty = qty;
    e->price = price;
    pay_hbuf_init(&h);
    pay_hbuf_str(&h, "ZXV-EQ-AUDIT-v1");
    pay_hbuf_put(&h, ex->audit_head, PAY_HASH_LEN);
    pay_hbuf_u64(&h, e->seq);
    pay_hbuf_u64(&h, e->kind);
    pay_hbuf_u64(&h, inst);
    pay_hbuf_u64(&h, oa);
    pay_hbuf_u64(&h, ob);
    pay_hbuf_u64(&h, wa);
    pay_hbuf_u64(&h, wb);
    pay_hbuf_u64(&h, qty);
    pay_hbuf_u64(&h, price);
    pay_hbuf_final(&h, e->hash);
    pay_memcpy(ex->audit_head, e->hash, PAY_HASH_LEN);
    ex->n_events++;
}

bool pay_equity_verify_audit(const pay_equity_t *ex)
{
    /* Re-derive each retained event's hash from its predecessor. */
    uint64_t n = ex->n_events < PAY_EQ_AUDIT ? ex->n_events : PAY_EQ_AUDIT;
    if (n == 0) return true;
    for (uint64_t s = ex->n_events - n + 1; s < ex->n_events; s++) {
        const pay_eq_event_t *p = &ex->audit[(s - 1) & (PAY_EQ_AUDIT - 1u)];
        const pay_eq_event_t *e = &ex->audit[s & (PAY_EQ_AUDIT - 1u)];
        pay_hbuf h;
        uint8_t d[PAY_HASH_LEN];
        if (e->seq != s) return false;
        pay_hbuf_init(&h);
        pay_hbuf_str(&h, "ZXV-EQ-AUDIT-v1");
        pay_hbuf_put(&h, p->hash, PAY_HASH_LEN);
        pay_hbuf_u64(&h, e->seq);
        pay_hbuf_u64(&h, e->kind);
        pay_hbuf_u64(&h, e->inst);
        pay_hbuf_u64(&h, e->order_a);
        pay_hbuf_u64(&h, e->order_b);
        pay_hbuf_u64(&h, e->owner_a);
        pay_hbuf_u64(&h, e->owner_b);
        pay_hbuf_u64(&h, e->qty);
        pay_hbuf_u64(&h, e->price);
        pay_hbuf_final(&h, d);
        if (!pay_memeq(d, e->hash, PAY_HASH_LEN)) return false;
    }
    return pay_memeq(ex->audit[(ex->n_events - 1) & (PAY_EQ_AUDIT - 1u)].hash, ex->audit_head,
                     PAY_HASH_LEN);
}

/* ===== Ledger plumbing ===== */

/* Unique identifiers for module-generated postings: UUIDv4 text derived from
 * SHA3(domain, ledger sequence, salt). Unique because the ledger sequence is. */
static void make_ids(pay_equity_t *ex, pay_posting_req_t *rq, uint64_t salt)
{
    pay_hbuf h;
    uint8_t d[PAY_HASH_LEN];
    pay_w w;
    pay_memset(rq, 0, sizeof *rq);
    pay_hbuf_init(&h);
    pay_hbuf_str(&h, "ZXV-EQ-POSTING-v1");
    pay_hbuf_u64(&h, ex->L->seq);
    pay_hbuf_u64(&h, salt);
    pay_hbuf_final(&h, d);
    pay_memcpy(rq->idem_key, d, 32);
    pay_uetr_from_random(d, rq->uetr);
    pay_w_init(&w, rq->e2e, sizeof rq->e2e);
    pay_w_s(&w, "EQ");
    pay_w_hex(&w, d + 16, 12);
    pay_w_finish(&w);
    rq->kind = PAY_KIND_TRADE;
}

static pay_cap_t cap_of(pay_obj_kind_t k)
{
    switch (k) {
    case PAY_OBJ_DATASET:
    case PAY_OBJ_MODEL:
    case PAY_OBJ_POST:
        return PAY_CAP_INTELLECTUAL;
    case PAY_OBJ_FARM:
        return PAY_CAP_SYSTEM;
    default:
        return PAY_CAP_FINANCIAL;
    }
}

static pay_status_t share_acct(pay_equity_t *ex, uint32_t inst, uint32_t owner, bool create,
                               uint32_t *acct)
{
    pay_eq_inst_t *in = &ex->inst[inst];
    if (owner == in->issuer_owner) {
        *acct = in->issuer_acct;
        return PAY_OK;
    }
    for (uint32_t i = 0; i < in->n_holders; i++)
        if (in->holder_owner[i] == owner) {
            *acct = in->holder_acct[i];
            return PAY_OK;
        }
    if (!create) return PAY_ERR_NOT_FOUND;
    if (in->n_holders >= PAY_EQ_MAX_HOLDERS) return PAY_ERR_FULL;
    pay_status_t st = pay_ledger_open(ex->L, owner, in->share_asset, cap_of(in->obj.kind), 0, acct);
    if (st != PAY_OK) return st;
    in->holder_owner[in->n_holders] = owner;
    in->holder_acct[in->n_holders] = *acct;
    in->n_holders++;
    return PAY_OK;
}

static uint64_t debit_of(const pay_equity_t *ex, uint32_t acct)
{
    const pay_account_t *a = pay_ledger_account(ex->L, acct);
    return a ? a->debit : 0;
}

static uint64_t avail(const pay_equity_t *ex, uint32_t acct)
{
    uint64_t d = debit_of(ex, acct), r = ex->reserved[acct];
    return d > r ? d - r : 0;
}

/* ===== Participants and gate ===== */

static const pay_eq_part_t *part_of(const pay_equity_t *ex, uint32_t owner)
{
    for (uint32_t i = 0; i < ex->n_part; i++)
        if (ex->part[i].active && ex->part[i].owner == owner) return &ex->part[i];
    return 0;
}

static bool gate_ok(const pay_equity_t *ex, uint8_t role, const char *juris)
{
    if (!ex->cfg.enabled) return false;
    if (role >= 8 || !(ex->cfg.role_mask & (1u << role))) return false;
    for (uint32_t i = 0; i < ex->cfg.n_disabled; i++)
        if (pay_streq(ex->cfg.disabled_juris[i], juris)) return false;
    return true;
}

pay_status_t pay_equity_register(pay_equity_t *ex, uint32_t owner, pay_role_t role,
                                 const char *jurisdiction)
{
    if (!ex || (unsigned) role >= PAY_ROLE_COUNT || !jurisdiction) return PAY_ERR_ARG;
    if (!gate_ok(ex, (uint8_t) role, jurisdiction)) return PAY_ERR_POLICY;
    if (part_of(ex, owner)) return PAY_ERR_STATE;
    if (ex->n_part >= PAY_EQ_MAX_PART) return PAY_ERR_FULL;
    pay_eq_part_t *p = &ex->part[ex->n_part++];
    p->active = true;
    p->owner = owner;
    p->role = (uint8_t) role;
    pay_strlcpy(p->juris, jurisdiction, sizeof p->juris);
    return PAY_OK;
}

static bool may_trade(const pay_equity_t *ex, uint32_t owner)
{
    const pay_eq_part_t *p = part_of(ex, owner);
    return p && gate_ok(ex, p->role, p->juris);
}

/* ===== Instruments ===== */

static pay_status_t issue_to_issuer(pay_equity_t *ex, uint32_t inst, uint64_t qty)
{
    pay_eq_inst_t *in = &ex->inst[inst];
    pay_posting_req_t rq;
    uint64_t ns;
    if (qty == 0) return PAY_OK;
    if (qty > EQ_MAX_AMOUNT || !pay_add_ok(in->supply, qty, &ns) || ns > EQ_MAX_AMOUNT)
        return PAY_ERR_OVERFLOW;
    make_ids(ex, &rq, inst);
    rq.kind = PAY_KIND_ISSUE;
    rq.lines[0].account = in->issuer_acct;
    rq.lines[0].d_debit = (int64_t) qty;
    rq.lines[0].d_credit = (int64_t) qty;
    rq.n_lines = 1;
    pay_status_t st = pay_ledger_post(ex->L, &rq, 0);
    if (st != PAY_OK) return st;
    in->supply = ns;
    audit(ex, PAY_EQ_EV_ISSUE, inst, 0, 0, in->issuer_owner, 0, qty, 0);
    return PAY_OK;
}

static pay_status_t create(pay_equity_t *ex, const pay_obj_ref_t *obj, uint32_t issuer_owner,
                           uint16_t quote_asset, pay_supply_t mode, uint64_t supply,
                           uint64_t max_supply, const pay_rat_t *cap, pay_eq_class_t cls,
                           uint32_t *out)
{
    char code[PAY_CODE_MAX + 1];
    pay_w w;
    uint16_t sa;
    if (!ex || !obj || (unsigned) mode > PAY_SUPPLY_ISSUABLE) return PAY_ERR_ARG;
    if (ex->n_inst >= PAY_EQ_MAX_INST) return PAY_ERR_FULL;
    const pay_asset_t *qa = pay_ledger_asset(ex->L, quote_asset);
    if (!qa || qa->kind == PAY_ASSET_SHARE) return PAY_ERR_NO_ASSET;
    if (cls == PAY_EQ_VOTING && obj->kind == PAY_OBJ_CURRENCY) return PAY_ERR_POLICY;
    if (mode == PAY_SUPPLY_FIXED && supply == 0) return PAY_ERR_ARG;
    if (mode == PAY_SUPPLY_ISSUABLE && max_supply && supply > max_supply) return PAY_ERR_ARG;
    pay_rat_t c = cap ? *cap : ex->cfg.default_cap;
    if (c.den == 0 || c.num > c.den) return PAY_ERR_ARG;
    for (uint32_t i = 0; i < ex->n_inst; i++)
        if (ex->inst[i].obj.kind == obj->kind && pay_memeq(ex->inst[i].obj.id, obj->id, 32))
            return PAY_ERR_STATE;

    uint32_t idx = ex->n_inst;
    pay_w_init(&w, code, sizeof code);
    pay_w_s(&w, cls == PAY_EQ_VOTING ? "EQV" : "EQN");
    pay_w_hex(&w, obj->id, 4);
    pay_w_u64(&w, idx);
    if (pay_w_finish(&w) < 0) return PAY_ERR_ARG;
    pay_status_t st = pay_ledger_add_asset(ex->L, code, 0, PAY_ASSET_SHARE, &sa);
    if (st != PAY_OK) return st;

    pay_eq_inst_t *in = &ex->inst[idx];
    pay_memset(in, 0, sizeof *in);
    in->obj = *obj;
    in->cls = cls;
    in->supply_mode = mode;
    in->max_supply = mode == PAY_SUPPLY_FIXED ? supply : max_supply;
    in->share_asset = sa;
    in->quote_asset = quote_asset;
    in->issuer_owner = issuer_owner;
    in->cap = c;
    st = pay_ledger_open(ex->L, issuer_owner, sa, cap_of(obj->kind), PAY_ACCT_ISSUER,
                         &in->issuer_acct);
    if (st != PAY_OK) return st;
    in->active = true;
    ex->n_inst++;
    audit(ex, PAY_EQ_EV_CREATE, idx, 0, 0, issuer_owner, (uint32_t) cls, supply, 0);
    st = issue_to_issuer(ex, idx, supply);
    if (st != PAY_OK) return st;
    *out = idx;
    return PAY_OK;
}

pay_status_t pay_equity_create_voting(pay_equity_t *ex, const pay_obj_ref_t *obj,
                                      uint32_t issuer_owner, uint16_t quote_asset,
                                      pay_supply_t mode, uint64_t supply, uint64_t max_supply,
                                      const pay_rat_t *cap, pay_eq_voting_t *out)
{
    uint32_t i;
    if (!out) return PAY_ERR_ARG;
    pay_status_t st = create(ex, obj, issuer_owner, quote_asset, mode, supply, max_supply, cap,
                             PAY_EQ_VOTING, &i);
    if (st == PAY_OK) out->idx = i;
    return st;
}

pay_status_t pay_equity_create_nonvoting(pay_equity_t *ex, const pay_obj_ref_t *obj,
                                         uint32_t issuer_owner, uint16_t quote_asset,
                                         pay_supply_t mode, uint64_t supply, uint64_t max_supply,
                                         const pay_rat_t *cap, pay_eq_nonvoting_t *out)
{
    uint32_t i;
    if (!out) return PAY_ERR_ARG;
    if (obj && obj->kind == PAY_OBJ_CURRENCY) return PAY_ERR_POLICY; /* pay_vfv_equity_init */
    pay_status_t st = create(ex, obj, issuer_owner, quote_asset, mode, supply, max_supply, cap,
                             PAY_EQ_NONVOTING, &i);
    if (st == PAY_OK) out->idx = i;
    return st;
}

uint32_t pay_eq_v_index(pay_eq_voting_t h)
{
    return h.idx;
}

uint32_t pay_eq_nv_index(pay_eq_nonvoting_t h)
{
    return h.idx;
}

const pay_eq_inst_t *pay_equity_inst(const pay_equity_t *ex, uint32_t inst)
{
    if (!ex || inst >= ex->n_inst || !ex->inst[inst].active) return 0;
    return &ex->inst[inst];
}

pay_status_t pay_equity_issue(pay_equity_t *ex, uint32_t inst, uint64_t qty)
{
    if (!pay_equity_inst(ex, inst) || qty == 0) return PAY_ERR_ARG;
    pay_eq_inst_t *in = &ex->inst[inst];
    if (in->supply_mode != PAY_SUPPLY_ISSUABLE) return PAY_ERR_POLICY;
    if (ex->vfv.ready && inst == ex->vfv.inst) return PAY_ERR_POLICY; /* only V1 issues VFV-EQ */
    if (in->max_supply && (qty > in->max_supply || in->supply > in->max_supply - qty))
        return PAY_ERR_LIMIT;
    return issue_to_issuer(ex, inst, qty);
}

static uint64_t cap_max(const pay_eq_inst_t *in)
{
    uint64_t m = 0;
    pay_muldiv(in->supply, in->cap.num, in->cap.den, &m, 0);
    return m;
}

static uint64_t cap_room(const pay_equity_t *ex, uint32_t inst, uint32_t owner)
{
    const pay_eq_inst_t *in = &ex->inst[inst];
    uint32_t acct;
    if (owner == in->issuer_owner) return UINT64_MAX;
    uint64_t have = 0, m = cap_max(in);
    if (share_acct((pay_equity_t *) ex, inst, owner, false, &acct) == PAY_OK)
        have = debit_of(ex, acct);
    return have >= m ? 0 : m - have;
}

pay_status_t pay_equity_allocate(pay_equity_t *ex, uint32_t inst, uint32_t to_owner, uint64_t qty)
{
    uint32_t acct;
    pay_posting_req_t rq;
    if (!pay_equity_inst(ex, inst) || qty == 0) return PAY_ERR_ARG;
    pay_eq_inst_t *in = &ex->inst[inst];
    if (!may_trade(ex, to_owner)) return PAY_ERR_POLICY;
    if (to_owner == in->issuer_owner) return PAY_ERR_ARG;
    if (qty > avail(ex, in->issuer_acct)) return PAY_ERR_FUNDS;
    if (qty > cap_room(ex, inst, to_owner)) {
        audit(ex, PAY_EQ_EV_CAP_CUT, inst, 0, 0, to_owner, 0, qty, 0);
        return PAY_ERR_LIMIT;
    }
    pay_status_t st = share_acct(ex, inst, to_owner, true, &acct);
    if (st != PAY_OK) return st;
    make_ids(ex, &rq, 0x10000u + inst);
    st = pay_ledger_transfer(ex->L, &rq, in->issuer_acct, acct, qty, 0);
    if (st == PAY_OK) audit(ex, PAY_EQ_EV_FILL, inst, 0, 0, in->issuer_owner, to_owner, qty, 0);
    return st;
}

uint64_t pay_equity_holding(const pay_equity_t *ex, uint32_t inst, uint32_t owner)
{
    uint32_t acct;
    if (!pay_equity_inst(ex, inst)) return 0;
    if (share_acct((pay_equity_t *) ex, inst, owner, false, &acct) != PAY_OK) return 0;
    const pay_account_t *a = pay_ledger_account(ex->L, acct);
    return a ? a->debit : 0; /* EQUITY == DEBIT for holders */
}

uint64_t pay_equity_reserved(const pay_equity_t *ex, uint32_t acct)
{
    return (ex && acct < PAY_MAX_ACCOUNTS) ? ex->reserved[acct] : 0;
}

bool pay_equity_has_votes(const pay_equity_t *ex, uint32_t inst)
{
    const pay_eq_inst_t *in = pay_equity_inst(ex, inst);
    return in && in->cls == PAY_EQ_VOTING && in->obj.kind != PAY_OBJ_CURRENCY;
}

uint64_t pay_equity_votes(const pay_equity_t *ex, pay_eq_voting_t h, uint32_t owner)
{
    if (!pay_equity_has_votes(ex, h.idx)) return 0;
    return pay_equity_holding(ex, h.idx, owner); /* one share, one vote */
}

/* ===== Order book ===== */

static pay_eq_order_t *find_order(pay_equity_t *ex, uint64_t id)
{
    for (uint32_t i = 0; i < PAY_EQ_MAX_ORDERS; i++)
        if (ex->ord[i].active && ex->ord[i].id == id) return &ex->ord[i];
    return 0;
}

static void release(pay_equity_t *ex, pay_eq_order_t *o)
{
    uint32_t acct = o->side == PAY_SIDE_BUY ? o->money_acct : o->share_acct;
    ex->reserved[acct] -= o->reserved;
    o->reserved = 0;
    o->active = false;
}

/* Best resting order on `side` for `inst`: buys by highest price, sells by
 * lowest price; ties by lowest seq (time priority). */
static pay_eq_order_t *best(pay_equity_t *ex, uint32_t inst, uint8_t side)
{
    pay_eq_order_t *b = 0;
    for (uint32_t i = 0; i < PAY_EQ_MAX_ORDERS; i++) {
        pay_eq_order_t *o = &ex->ord[i];
        if (!o->active || o->inst != inst || o->side != side) continue;
        if (!b) {
            b = o;
            continue;
        }
        bool better = side == PAY_SIDE_BUY ? o->price > b->price : o->price < b->price;
        if (better || (o->price == b->price && o->seq < b->seq)) b = o;
    }
    return b;
}

/* E1: one atomic posting for one fill. */
static pay_status_t settle(pay_equity_t *ex, uint32_t inst, uint32_t buyer_money,
                           uint32_t seller_money, uint32_t seller_shares, uint32_t buyer_shares,
                           uint64_t qty, uint64_t cost)
{
    pay_posting_req_t rq;
    make_ids(ex, &rq, 0x20000u + inst);
    rq.lines[0].account = buyer_money;
    rq.lines[0].d_debit = -(int64_t) cost;
    rq.lines[1].account = seller_money;
    rq.lines[1].d_debit = (int64_t) cost;
    rq.lines[2].account = seller_shares;
    rq.lines[2].d_debit = -(int64_t) qty;
    rq.lines[3].account = buyer_shares;
    rq.lines[3].d_debit = (int64_t) qty;
    rq.n_lines = 4;
    return pay_ledger_post(ex->L, &rq, 0);
}

pay_status_t pay_equity_order(pay_equity_t *ex, uint32_t inst, uint32_t owner, uint32_t money_acct,
                              pay_side_t side, pay_ord_type_t type, uint64_t price, uint64_t qty,
                              uint64_t *order_id)
{
    uint64_t need;
    uint32_t sh;
    pay_eq_order_t *in_o = 0;
    if (order_id) *order_id = 0;
    if (!pay_equity_inst(ex, inst) || qty == 0 || qty > EQ_MAX_AMOUNT ||
        (unsigned) side > PAY_SIDE_SELL || (unsigned) type > PAY_ORD_MARKET)
        return PAY_ERR_ARG;
    pay_eq_inst_t *I = &ex->inst[inst];
    if (!may_trade(ex, owner)) return PAY_ERR_POLICY;
    const pay_account_t *ma = pay_ledger_account(ex->L, money_acct);
    if (!ma || ma->owner != owner || ma->asset != I->quote_asset) return PAY_ERR_NO_ACCOUNT;
    if (type == PAY_ORD_LIMIT && (price == 0 || price > EQ_MAX_AMOUNT)) return PAY_ERR_ARG;
    if (type == PAY_ORD_MARKET && side == PAY_SIDE_BUY && (price == 0 || price > EQ_MAX_AMOUNT))
        return PAY_ERR_ARG; /* market buy: price is the spend limit */
    pay_status_t st = share_acct(ex, inst, owner, true, &sh);
    if (st != PAY_OK) return st;

    /* Reserve. */
    if (side == PAY_SIDE_BUY) {
        if (type == PAY_ORD_LIMIT) {
            if (!pay_muldiv(price, qty, 1, &need, 0) || need > EQ_MAX_AMOUNT)
                return PAY_ERR_OVERFLOW;
        } else {
            need = price;
        }
        if (avail(ex, money_acct) < need) return PAY_ERR_FUNDS;
    } else {
        need = qty;
        if (avail(ex, sh) < need) return PAY_ERR_FUNDS;
    }
    for (uint32_t i = 0; i < PAY_EQ_MAX_ORDERS; i++)
        if (!ex->ord[i].active) {
            in_o = &ex->ord[i];
            break;
        }
    if (!in_o) return PAY_ERR_FULL;
    pay_memset(in_o, 0, sizeof *in_o);
    in_o->active = true;
    in_o->id = ex->next_order_id++;
    in_o->seq = ex->seq++;
    in_o->inst = inst;
    in_o->owner = owner;
    in_o->money_acct = money_acct;
    in_o->share_acct = sh;
    in_o->side = (uint8_t) side;
    in_o->type = (uint8_t) type;
    in_o->price = type == PAY_ORD_LIMIT ? price : 0;
    in_o->remaining = qty;
    in_o->reserved = need;
    ex->reserved[side == PAY_SIDE_BUY ? money_acct : sh] += need;
    audit(ex, PAY_EQ_EV_ORDER, inst, in_o->id, 0, owner, (uint32_t) side, qty, price);

    uint8_t opp = side == PAY_SIDE_BUY ? PAY_SIDE_SELL : PAY_SIDE_BUY;
    bool cap_cancel = false;
    while (in_o->active && in_o->remaining > 0) {
        pay_eq_order_t *r = best(ex, inst, opp);
        if (!r) break;
        if (type == PAY_ORD_LIMIT &&
            (side == PAY_SIDE_BUY ? r->price > in_o->price : r->price < in_o->price))
            break;               /* does not cross */
        if (r->owner == owner) { /* E2 */
            audit(ex, PAY_EQ_EV_STP_CANCEL, inst, r->id, in_o->id, owner, owner, r->remaining,
                  r->price);
            release(ex, r);
            continue;
        }
        pay_eq_order_t *buy = side == PAY_SIDE_BUY ? in_o : r;
        pay_eq_order_t *sell = side == PAY_SIDE_BUY ? r : in_o;
        uint64_t p = r->price; /* trade at the resting price */
        uint64_t q = in_o->remaining < r->remaining ? in_o->remaining : r->remaining;
        if (buy->type == PAY_ORD_MARKET) {
            uint64_t afford = pay_udiv64(buy->reserved, p, 0);
            if (afford < q) q = afford;
            if (q == 0) break; /* budget exhausted */
        }
        uint64_t room = cap_room(ex, inst, buy->owner); /* E3 */
        if (room < q) {
            audit(ex, PAY_EQ_EV_CAP_CUT, inst, buy->id, sell->id, buy->owner, sell->owner, q, p);
            q = room;
        }
        if (q == 0) {
            if (buy == in_o) { /* E3: an incoming buyer with no room is cancelled */
                cap_cancel = true;
                break;
            }
            audit(ex, PAY_EQ_EV_CANCEL, inst, buy->id, 0, buy->owner, 0, buy->remaining, p);
            release(ex, buy);
            continue;
        }
        uint64_t cost = p * q; /* p, q <= 2^59 and cost <= reserved <= 2^59 checked below */
        if (!pay_muldiv(p, q, 1, &cost, 0) || cost > EQ_MAX_AMOUNT) {
            st = PAY_ERR_OVERFLOW;
            break;
        }
        st = settle(ex, inst, buy->money_acct, sell->money_acct, sell->share_acct, buy->share_acct,
                    q, cost);
        if (st != PAY_OK) break;
        /* Reservations: a limit buy reserved its own limit price per share. */
        uint64_t rel_buy = buy->type == PAY_ORD_LIMIT ? buy->price * q : cost;
        buy->reserved -= rel_buy;
        ex->reserved[buy->money_acct] -= rel_buy;
        sell->reserved -= q;
        ex->reserved[sell->share_acct] -= q;
        buy->remaining -= q;
        sell->remaining -= q;
        I->last_price = p;
        ex->fills++;
        audit(ex, PAY_EQ_EV_FILL, inst, buy->id, sell->id, buy->owner, sell->owner, q, p);
        if (r->remaining == 0) release(ex, r);
    }
    if (in_o->active &&
        (in_o->remaining == 0 || type == PAY_ORD_MARKET || st != PAY_OK || cap_cancel)) {
        if (in_o->remaining)
            audit(ex, PAY_EQ_EV_CANCEL, inst, in_o->id, 0, owner, 0, in_o->remaining, 0);
        release(ex, in_o);
    }
    if (in_o->active && order_id) *order_id = in_o->id;
    return st;
}

pay_status_t pay_equity_cancel(pay_equity_t *ex, uint64_t order_id, uint32_t owner)
{
    pay_eq_order_t *o = ex ? find_order(ex, order_id) : 0;
    if (!o) return PAY_ERR_NOT_FOUND;
    if (o->owner != owner) return PAY_ERR_POLICY;
    audit(ex, PAY_EQ_EV_CANCEL, o->inst, o->id, 0, owner, 0, o->remaining, o->price);
    release(ex, o);
    return PAY_OK;
}

bool pay_equity_check(const pay_equity_t *ex)
{
    static uint64_t res[PAY_MAX_ACCOUNTS];
    if (!ex || !pay_ledger_check(ex->L)) return false;
    for (uint32_t a = 0; a < PAY_MAX_ACCOUNTS; a++) res[a] = 0;
    for (uint32_t i = 0; i < PAY_EQ_MAX_ORDERS; i++) {
        const pay_eq_order_t *o = &ex->ord[i];
        if (!o->active) continue;
        res[o->side == PAY_SIDE_BUY ? o->money_acct : o->share_acct] += o->reserved;
    }
    for (uint32_t a = 0; a < PAY_MAX_ACCOUNTS; a++) {
        if (res[a] != ex->reserved[a]) return false;
        if (res[a] && res[a] > debit_of(ex, a)) return false;
    }
    for (uint32_t k = 0; k < ex->n_inst; k++) {
        const pay_eq_inst_t *in = &ex->inst[k];
        const pay_account_t *ia = pay_ledger_account(ex->L, in->issuer_acct);
        uint64_t held = ia->debit;
        if (ia->credit != in->supply) return false;
        for (uint32_t h = 0; h < in->n_holders; h++) {
            const pay_account_t *a = pay_ledger_account(ex->L, in->holder_acct[h]);
            if (a->credit != 0 || a->equity != (int64_t) a->debit) return false;
            held += a->debit;
        }
        if (held != in->supply) return false;
    }
    return true;
}

/* ===== VFV currency equity ===== */

pay_status_t pay_vfv_equity_init(pay_equity_t *ex, uint32_t platform_owner, uint16_t quote_asset,
                                 uint32_t n_levels, pay_eq_nonvoting_t *out)
{
    pay_obj_ref_t obj;
    uint32_t i;
    if (!ex || !out || ex->vfv.ready || n_levels < 2 || n_levels > PAY_EQ_LEVELS)
        return PAY_ERR_ARG;
    pay_memset(&obj, 0, sizeof obj);
    obj.kind = PAY_OBJ_CURRENCY;
    pay_strlcpy((char *) obj.id, ex->L->platform.vfv_alpha, sizeof obj.id);
    pay_status_t st = create(ex, &obj, platform_owner, quote_asset, PAY_SUPPLY_ISSUABLE, 0, 0, 0,
                             PAY_EQ_NONVOTING, &i);
    if (st != PAY_OK) return st;
    ex->vfv.ready = true;
    ex->vfv.inst = i;
    ex->vfv.n_levels = n_levels;
    for (uint32_t d = 0; d < n_levels; d++) ex->vfv.weights[d] = swarm_level_weight(d, n_levels);
    out->idx = i;
    return PAY_OK;
}

pay_status_t pay_vfv_equity_on_mint(pay_equity_t *ex, uint32_t holder, const uint32_t *path,
                                    uint64_t amount)
{
    uint64_t part[PAY_EQ_LEVELS], ns, nm;
    uint32_t acct[PAY_EQ_LEVELS];
    pay_posting_req_t rq;
    if (!ex || !ex->vfv.ready || amount == 0 || amount > EQ_MAX_AMOUNT) return PAY_ERR_ARG;
    if (ex->vfv.n_levels > 1 && !path) return PAY_ERR_ARG;
    pay_vfv_equity_t *v = &ex->vfv;
    pay_eq_inst_t *in = &ex->inst[v->inst];
    if (!pay_add_ok(in->supply, amount, &ns) || ns > EQ_MAX_AMOUNT ||
        !pay_add_ok(v->minted_seen, amount, &nm))
        return PAY_ERR_OVERFLOW;
    swarm_split_lr(amount, v->weights, v->n_levels, part); /* V1 */
    for (uint32_t d = 0; d < v->n_levels; d++) {
        uint32_t who = d == 0 ? holder : path[d - 1];
        pay_status_t st = share_acct(ex, v->inst, who, true, &acct[d]);
        if (st != PAY_OK) return st;
    }
    make_ids(ex, &rq, 0x30000u);
    rq.kind = PAY_KIND_ISSUE;
    uint32_t n = 0;
    for (uint32_t d = 0; d < v->n_levels; d++) {
        if (!part[d]) continue;
        uint32_t k = 0;
        while (k < n && rq.lines[k].account != acct[d]) k++; /* same owner on two levels */
        if (k == n) {
            rq.lines[n].account = acct[d];
            rq.lines[n].d_debit = 0;
            rq.lines[n].d_credit = 0;
            n++;
        }
        rq.lines[k].d_debit += (int64_t) part[d];
    }
    rq.lines[n].account = in->issuer_acct;
    rq.lines[n].d_debit = 0;
    rq.lines[n].d_credit = (int64_t) amount;
    rq.n_lines = n + 1;
    pay_status_t st = pay_ledger_post(ex->L, &rq, 0);
    if (st != PAY_OK) return st;
    in->supply = ns;
    v->minted_seen = nm;
    for (uint32_t d = 0; d < v->n_levels; d++) v->level_total[d] += part[d]; /* V2 */
    audit(ex, PAY_EQ_EV_VFV_MINT, v->inst, 0, 0, holder, 0, amount, 0);
    return PAY_OK;
}

bool pay_vfv_equity_audit(const pay_equity_t *ex)
{
    if (!ex || !ex->vfv.ready) return false;
    const pay_vfv_equity_t *v = &ex->vfv;
    const pay_eq_inst_t *in = &ex->inst[v->inst];
    uint64_t sum = 0;
    for (uint32_t d = 0; d < v->n_levels; d++) sum += v->level_total[d];
    const pay_account_t *ia = pay_ledger_account(ex->L, in->issuer_acct);
    return sum == v->minted_seen && in->supply == v->minted_seen && ia->credit == in->supply &&
           in->cls == PAY_EQ_NONVOTING && !pay_equity_has_votes(ex, v->inst);
}
