/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* vna_econ.c — external economy, triple ledger, gate, negotiation. See vna_econ.h. */
#include "vna_econ.h"
#include "vna_cmd.h"

#define GOLD_DEN           21u                  /* F(8) */
#define GOLD_LO            8u                   /* F(6) */
#define GOLD_HI            13u                  /* F(7) */
#define VNA_ECON_MAX_TOTAL ((uint64_t) 1 << 48) /* SWARM_MAX_TOKENS_PER_CYCLE */

/* ---- schemas ---- */
#define T vna_receipt_t
static const vna_field_t rc_fields[] = {
    VNA_FCONST(T, magic, VNA_RCPT_MAGIC),
    VNA_FU8(T, version, VNA_VERSION, VNA_AXIS_NONE),
    VNA_FU8(T, mode, VNA_TR_MODE_MAX, VNA_AXIS_FINANCIAL),
    VNA_FU8(T, kind, VNA_TK_KINDS - 1u, VNA_AXIS_FINANCIAL),
    VNA_FU8(T, form, VNA_FORMS - 1u, VNA_AXIS_FINANCIAL),
    VNA_FU8(T, resource, VNA_RES_COUNT - 1u, VNA_AXIS_FINANCIAL),
    VNA_FFIX(T, seller, VNA_AXIS_PROVENANCE),
    VNA_FFIX(T, buyer, VNA_AXIS_PROVENANCE),
    VNA_FU64(T, units, 0, VNA_AXIS_FINANCIAL),
    VNA_FU64(T, price, 0, VNA_AXIS_FINANCIAL),
    VNA_FU32(T, demand_x1000, 0, VNA_AXIS_EXTERNALITY),
    VNA_FU64(T, pair_seq, 0, VNA_AXIS_PROVENANCE),
    VNA_FFIX(T, prev, VNA_AXIS_PROVENANCE),
    VNA_FU64(T, ts, 0, VNA_AXIS_PROVENANCE),
    VNA_FFIX(T, agreement_cid, VNA_AXIS_PROVENANCE),
    VNA_FHK(T, hk, hk_len, VNA_AXIS_EXTERNALITY),
    VNA_FSIG(T, sig_seller),
    VNA_FSIG(T, sig_buyer),
};
#undef T
/* UBH class 1 = CAUSAL_EVENT */
const vna_schema_t vna_receipt_schema = {"trade_receipt", 0x0040, 1, rc_fields,
                                         sizeof rc_fields / sizeof rc_fields[0]};

#define L vna_lentry_t
static const vna_field_t le_fields[] = {
    VNA_FCONST(L, magic, VNA_LEDGER_MAGIC),
    VNA_FU64(L, seq, 0, VNA_AXIS_PROVENANCE),
    VNA_FFIX(L, prev, VNA_AXIS_PROVENANCE),
    VNA_FU8(L, axis, 2, VNA_AXIS_NONE),
    VNA_FU8(L, kind, VNA_TK_KINDS - 1u, VNA_AXIS_FINANCIAL),
    VNA_FU8(L, form, VNA_FORMS - 1u, VNA_AXIS_FINANCIAL),
    VNA_FU8(L, mode, 0, VNA_AXIS_FINANCIAL),
    VNA_FFIX(L, counterparty, VNA_AXIS_PROVENANCE),
    VNA_FU64(L, amount, 0, VNA_AXIS_FINANCIAL),
    VNA_FU16(L, debit, 0, VNA_AXIS_FINANCIAL),
    VNA_FU16(L, credit, 0, VNA_AXIS_FINANCIAL),
    VNA_FFIX(L, ref, VNA_AXIS_PROVENANCE),
    VNA_FU64(L, aux, 0, VNA_AXIS_EXTERNALITY),
    VNA_FU64(L, ts, 0, VNA_AXIS_PROVENANCE),
};
#undef L
const vna_schema_t vna_lentry_schema = {"ledger_entry", 0x0041, 1, le_fields,
                                        sizeof le_fields / sizeof le_fields[0]};

static uint8_t g_rc[VNA_RCPT_MAX]; /* single-threaded scratch */
static vna_receipt_t g_r;

static bool priceable(uint8_t form)
{
    return form < VNA_FORMS && !swarm_cap_is_crown((swarm_cap_t) form);
}

/* ---- ledger ---- */
static void entry_hash(const vna_lentry_t *e, uint8_t out[32])
{
    uint8_t buf[256];
    int32_t n = vna_schema_pack(&vna_lentry_schema, e, buf, sizeof buf, true);
    vna_sha3(buf, n > 0 ? (uint32_t) n : 0, out);
}

static void ledger_init(vna_ledger_t *l)
{
    vna_zero(l, sizeof *l);
    vna_htag("vinea/v2/ledger-genesis", 0, 0, l->base);
    vna_copy(l->tip, l->base, 32);
}

static void ledger_post(vna_ledger_t *l, uint8_t axis, uint8_t kind, uint8_t form, uint8_t mode,
                        const vna_id_t *cp, uint64_t amount, uint16_t debit, uint16_t credit,
                        const uint8_t ref[32], uint64_t aux, uint64_t ts)
{
    if (l->n == VNA_LEDGER_RING) { /* the oldest entry leaves the ring; base moves past it */
        entry_hash(&l->ring[l->head], l->base);
        l->head = l->head + 1u == VNA_LEDGER_RING ? 0 : l->head + 1u;
        l->n--;
    }
    uint32_t i = l->head + l->n;
    if (i >= VNA_LEDGER_RING) i -= VNA_LEDGER_RING;
    vna_lentry_t *e = &l->ring[i];
    e->magic = VNA_LEDGER_MAGIC;
    e->seq = ++l->count;
    vna_copy(e->prev, l->tip, 32);
    e->axis = axis;
    e->kind = kind;
    e->form = form;
    e->mode = mode;
    if (cp)
        e->counterparty = *cp;
    else
        vna_zero(&e->counterparty, sizeof e->counterparty);
    e->amount = amount;
    e->debit = debit;
    e->credit = credit;
    vna_copy(e->ref, ref, 32);
    e->aux = aux;
    e->ts = ts;
    l->n++;
    entry_hash(e, l->tip);
    if (axis == VNA_LAX_FINANCIAL) {
        l->debits += amount;
        l->credits += amount;
    }
}

static void ledger_triple(vna_book_t *b, uint8_t kind, uint8_t form, uint8_t mode,
                          const vna_id_t *cp, uint64_t amount, uint16_t debit, uint16_t credit,
                          const uint8_t ref[32], uint64_t sigs, uint64_t demand, uint64_t aux,
                          uint64_t ts, const uint8_t rnd[32])
{
    vna_ledger_t *l = &b->ledger;
    ledger_post(l, VNA_LAX_FINANCIAL, kind, form, mode, cp, amount, debit, credit, ref, 0, ts);
    ledger_post(l, VNA_LAX_PROVENANCE, kind, form, mode, cp, sigs, 0, 0, ref, aux, ts);
    ledger_post(l, VNA_LAX_EXTERNALITY, kind, form, mode, cp, demand, 0, 0, ref, aux, ts);
    vna_sign(b->idn->sk, VNA_CTX_LEDGER, l->tip, 32, rnd, l->tip_sig);
}

bool vna_ledger_verify(const vna_ledger_t *l, const uint8_t pk[VNA_PK_LEN])
{
    uint8_t h[32];
    vna_copy(h, l->base, 32);
    for (uint32_t k = 0; k < l->n; k++) {
        uint32_t i = l->head + k;
        if (i >= VNA_LEDGER_RING) i -= VNA_LEDGER_RING;
        const vna_lentry_t *e = &l->ring[i];
        if (!vna_eq(e->prev, h, 32)) return false;
        entry_hash(e, h);
    }
    if (!vna_eq(h, l->tip, 32)) return false;
    if (l->count == 0) return true;
    return vna_verify(pk, VNA_CTX_LEDGER, l->tip, 32, l->tip_sig);
}

/* ---- book ---- */
void vna_book_init(vna_book_t *b, const vna_identity_t *idn, vna_account_t *acct, uint32_t cap)
{
    vna_zero(b, sizeof *b);
    b->idn = idn;
    b->acct = acct;
    b->cap = cap;
    for (uint32_t i = 0; i < cap; i++) acct[i].used = false;
    for (uint32_t r = 0; r < VNA_RES_COUNT; r++) b->supply[r] = 1;
    ledger_init(&b->ledger);
}

vna_account_t *vna_book_find(vna_book_t *b, const vna_id_t *id)
{
    for (uint32_t i = 0; i < b->n; i++)
        if (b->acct[i].used && vna_id_eq(&b->acct[i].id, id)) return &b->acct[i];
    return 0;
}

static uint16_t acct_index(const vna_book_t *b, const vna_account_t *a)
{
    return (uint16_t) (1u + (uint32_t) (a - b->acct));
}

vna_status_t vna_book_add_peer(vna_book_t *b, const vna_id_t *id, const uint8_t pk[VNA_PK_LEN],
                               uint64_t pow, uint32_t pow_bits, bool member)
{
    if (!b || !id || !pk) return VNA_ERR_ARG;
    if (vna_id_eq(id, &b->idn->id)) return VNA_ERR_ARG;
    vna_status_t st = vna_keycache_check(0, id, pk, pow, pow_bits);
    if (st != VNA_OK) return st;
    vna_account_t *a = vna_book_find(b, id);
    if (!a) {
        if (b->n >= b->cap || b->n >= SWARM_MAX_MODELS) return VNA_ERR_SPACE;
        a = &b->acct[b->n++];
        vna_zero(a, sizeof *a);
        a->id = *id;
        vna_copy(a->pk, pk, VNA_PK_LEN);
        vna_htag("vinea/v2/pair-genesis", 0, 0, a->pair_prev);
        a->used = true;
    }
    a->member = member;
    return VNA_OK;
}

uint64_t vna_book_trust(void *book, const vna_id_t *peer)
{
    vna_book_t *b = (vna_book_t *) book;
    vna_account_t *a = b ? vna_book_find(b, peer) : 0;
    return a ? vna_sat_add(a->contrib_total, a->social) : 0;
}

vna_status_t vna_receipt_prepare(vna_book_t *b, vna_receipt_t *r, uint8_t mode, uint8_t form,
                                 uint8_t resource, const vna_id_t *seller, const vna_id_t *buyer,
                                 uint64_t units, uint64_t price, uint32_t demand_x1000, uint64_t ts,
                                 const uint8_t agreement_cid[32], const char *hk)
{
    if (!b || !r || !seller || !buyer || !hk) return VNA_ERR_ARG;
    const vna_id_t *peer = vna_id_eq(seller, &b->idn->id) ? buyer : seller;
    vna_account_t *a = vna_book_find(b, peer);
    if (!a) return VNA_ERR_ARG;
    vna_zero(r, sizeof *r);
    r->magic = VNA_RCPT_MAGIC;
    r->version = VNA_VERSION;
    r->mode = mode;
    r->kind = VNA_TK_EXTERNAL;
    r->form = form;
    r->resource = resource;
    r->seller = *seller;
    r->buyer = *buyer;
    r->units = units;
    r->price = price;
    r->demand_x1000 = demand_x1000;
    r->pair_seq = a->pair_seq + 1u;
    vna_copy(r->prev, a->pair_prev, 32);
    r->ts = ts;
    if (agreement_cid) vna_copy(r->agreement_cid, agreement_cid, 32);
    int32_t n = vna_hk_canonicalize(hk, r->hk, sizeof r->hk);
    if (n < 0) return VNA_ERR_HK;
    r->hk_len = (uint16_t) n;
    return VNA_OK;
}

int32_t vna_receipt_sign_seller(vna_receipt_t *r, const vna_identity_t *idn, const uint8_t rnd[32],
                                uint8_t *out, uint32_t cap)
{
    if (!r || !idn || !vna_id_eq(&r->seller, &idn->id)) return -1;
    vna_zero(r->sig_seller, VNA_SIG_LEN);
    vna_zero(r->sig_buyer, VNA_SIG_LEN);
    int32_t n = vna_schema_pack(&vna_receipt_schema, r, out, cap, false);
    if (n < 0) return -1;
    vna_sign(idn->sk, VNA_CTX_RECEIPT, out, (uint32_t) n, rnd, r->sig_seller);
    return vna_schema_pack(&vna_receipt_schema, r, out, cap, true);
}

int32_t vna_receipt_countersign(const uint8_t *in, uint32_t len,
                                const uint8_t seller_pk[VNA_PK_LEN], const vna_identity_t *idn,
                                const uint8_t rnd[32], vna_receipt_t *r, uint8_t *out, uint32_t cap)
{
    uint32_t sig_off = 0;
    if (!in || !seller_pk || !idn || !r || !out) return VNA_ERR_ARG;
    if (vna_schema_unpack(&vna_receipt_schema, in, len, r, &sig_off) < 0) return VNA_ERR_PARSE;
    if (!vna_id_eq(&r->buyer, &idn->id) || r->mode == VNA_TR_DISTRIBUTE) return VNA_ERR_ARG;
    vna_id_t sid;
    vna_node_id(seller_pk, &sid);
    if (!vna_id_eq(&sid, &r->seller)) return VNA_ERR_BINDING;
    if (!vna_verify(seller_pk, VNA_CTX_RECEIPT, in, sig_off, r->sig_seller)) return VNA_ERR_SIG;
    vna_sign(idn->sk, VNA_CTX_RECEIPT, in, sig_off, rnd, r->sig_buyer);
    return vna_schema_pack(&vna_receipt_schema, r, out, cap, true);
}

static bool all_zero(const uint8_t *p, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
        if (p[i]) return false;
    return true;
}

/* The seller's agreement must allow the buyer this resource. File chunks
 * carry their root and are checked (and charged) per chunk by
 * vna_file_serve; the provisioning path commits compute/storage/memory, so
 * here the check is a read-only gate that refuses trades the agreement would
 * never honour. Memory leases are checked at the agreement's longest lease. */
static bool trade_allowed(vna_book_t *b, const vna_agreement_t *agr, vna_usage_t *us,
                          const vna_account_t *a, uint8_t resource, uint64_t units, uint64_t now)
{
    if (resource == VNA_RES_ROUTE) return true;
    if (!agr) return false;
    if (resource == VNA_RES_FILE) {
        static const vna_id_t none;
        return agr->degree >= VNA_DEG_SERVE &&
               vna_agree_check(agr, us, &a->id, vna_book_trust(b, &a->id), VNA_RES_ROUTE, &none, 0,
                               0, now, false) == VNA_OK;
    }
    uint32_t cycles = resource == VNA_RES_MEMORY ? 1u : 0u;
    return vna_agree_check(agr, us, &a->id, vna_book_trust(b, &a->id), (vna_resource_t) resource, 0,
                           units, cycles, now, false) == VNA_OK;
}

static vna_status_t apply_parsed(vna_book_t *b, const vna_receipt_t *r, const uint8_t *bytes,
                                 uint32_t len, uint32_t sig_off, const vna_agreement_t *agr,
                                 vna_usage_t *us, uint64_t now, const uint8_t rnd[32],
                                 bool internal)
{
    const vna_id_t *me = &b->idn->id;
    if (r->version != VNA_VERSION || r->kind != VNA_TK_EXTERNAL || r->mode == 0)
        return VNA_ERR_PARSE;
    if (!priceable(r->form)) return VNA_ERR_INALIENABLE; /* Crown capital is never traded */
    bool i_sell = vna_id_eq(&r->seller, me), i_buy = vna_id_eq(&r->buyer, me);
    if (i_sell == i_buy) return VNA_ERR_DST; /* must be exactly one side */
    vna_account_t *a = vna_book_find(b, i_sell ? &r->buyer : &r->seller);
    if (!a) return VNA_ERR_ARG;
    const uint8_t *seller_pk = i_sell ? b->idn->pk : a->pk;
    const uint8_t *buyer_pk = i_sell ? a->pk : b->idn->pk;
    /* signatures: both for PAY/CREDIT, the issuer's for DISTRIBUTE */
    if (!vna_verify(seller_pk, VNA_CTX_RECEIPT, bytes, sig_off, r->sig_seller)) return VNA_ERR_SIG;
    uint64_t sigs = 1;
    if (r->mode == VNA_TR_DISTRIBUTE) {
        if (!all_zero(r->sig_buyer, VNA_SIG_LEN)) return VNA_ERR_PARSE;
        if (i_sell && !internal) return VNA_ERR_STATE; /* only settle issues my DISTRIBUTEs */
    } else {
        if (!vna_verify(buyer_pk, VNA_CTX_RECEIPT, bytes, sig_off, r->sig_buyer))
            return VNA_ERR_SIG;
        sigs = 2;
    }
    /* per-pair chain: no replay, no gap, no fork */
    if (r->pair_seq <= a->pair_seq) return VNA_ERR_REPLAY;
    if (r->pair_seq != a->pair_seq + 1u || !vna_eq(r->prev, a->pair_prev, 32)) return VNA_ERR_FORK;

    uint8_t f = r->form;
    uint64_t p = r->price;
    uint16_t ai = acct_index(b, a), debit, credit;
    switch (r->mode) {
    case VNA_TR_PAY:
        if (i_sell) { /* the peer spends tokens I issued, for my resource */
            if (a->bal[VNA_TK_EXTERNAL][f] < p) return VNA_ERR_FUNDS;
            if (!trade_allowed(b, agr, us, a, r->resource, r->units, now)) return VNA_ERR_DENIED;
            a->bal[VNA_TK_EXTERNAL][f] -= p;
            b->burned[VNA_TK_EXTERNAL][f] += p;
            debit = ai;
            credit = VNA_ACC_BURN;
        } else { /* I spend tokens the peer issued, for its resource */
            if (a->held[f] < p) return VNA_ERR_FUNDS;
            a->held[f] -= p;
            b->held_out[f] += p;
            b->received_units[r->resource] += r->units;
            debit = VNA_ACC_SELF;
            credit = ai;
        }
        break;
    case VNA_TR_CREDIT:
        if (i_buy) { /* the peer contributed to me: demand-weighted value, paid at my settle */
            a->contrib[f] = vna_sat_add(a->contrib[f], p);
            a->contrib_total = vna_sat_add(a->contrib_total, p);
            b->received_units[r->resource] += r->units;
            debit = VNA_ACC_SELF;
            credit = ai;
        } else {
            if (!trade_allowed(b, agr, us, a, r->resource, r->units, now)) return VNA_ERR_DENIED;
            a->receivable[f] = vna_sat_add(a->receivable[f], p);
            debit = ai;
            credit = VNA_ACC_SELF;
        }
        break;
    case VNA_TR_DISTRIBUTE:
        if (i_sell) {
            if (b->pool[f] < p) return VNA_ERR_FUNDS;
            b->pool[f] -= p;
            b->burned[VNA_TK_NEUTRAL][f] += p;
            a->bal[VNA_TK_EXTERNAL][f] += p;
            b->minted[VNA_TK_EXTERNAL][f] += p;
            debit = ai;
            credit = VNA_ACC_POOL;
        } else {
            a->held[f] = vna_sat_add(a->held[f], p);
            b->held_in[f] += p;
            debit = VNA_ACC_SELF;
            credit = ai;
        }
        break;
    default:
        return VNA_ERR_PARSE;
    }
    a->pair_seq = r->pair_seq;
    vna_sha3(bytes, len, a->pair_prev);
    a->social++; /* Crown (Social) capital: earned by trading honestly, never moved */
    ledger_triple(b, VNA_TK_EXTERNAL, f, r->mode, &a->id, p, debit, credit, a->pair_prev, sigs,
                  r->demand_x1000, r->pair_seq, r->ts, rnd);
    return VNA_OK;
}

vna_status_t vna_book_check_sale(vna_book_t *b, const vna_agreement_t *agr, vna_usage_t *us,
                                 const vna_id_t *buyer, uint8_t mode, uint8_t form,
                                 uint8_t resource, uint64_t units, uint64_t price, uint64_t now)
{
    if (!b || !buyer || (mode != VNA_TR_PAY && mode != VNA_TR_CREDIT) ||
        resource >= VNA_RES_COUNT || form >= VNA_FORMS)
        return VNA_ERR_ARG;
    if (!priceable(form)) return VNA_ERR_INALIENABLE;
    vna_account_t *a = vna_book_find(b, buyer);
    if (!a) return VNA_ERR_ARG;
    if (mode == VNA_TR_PAY && a->bal[VNA_TK_EXTERNAL][form] < price) return VNA_ERR_FUNDS;
    return trade_allowed(b, agr, us, a, resource, units, now) ? VNA_OK : VNA_ERR_DENIED;
}

vna_status_t vna_book_apply(vna_book_t *b, const uint8_t *bytes, uint32_t len,
                            const vna_agreement_t *agr, vna_usage_t *us, uint64_t now,
                            const uint8_t rnd[32])
{
    uint32_t sig_off = 0;
    if (!b || !bytes || !rnd) return VNA_ERR_ARG;
    if (vna_schema_unpack(&vna_receipt_schema, bytes, len, &g_r, &sig_off) < 0) {
        b->rejected++;
        return VNA_ERR_PARSE;
    }
    vna_status_t st = apply_parsed(b, &g_r, bytes, len, sig_off, agr, us, now, rnd, false);
    if (st != VNA_OK) b->rejected++;
    return st;
}

/* ---- the golden split ---- */
static bool id_less(const vna_id_t *a, const vna_id_t *b)
{
    for (uint32_t i = 0; i < VNA_ID_LEN; i++)
        if (a->b[i] != b->b[i]) return a->b[i] < b->b[i];
    return false;
}

uint64_t vna_econ_split(uint64_t pool, const uint64_t *cyc, const uint64_t *life,
                        const vna_id_t *ids, uint32_t n, uint64_t *share, uint64_t *commons_out,
                        uint64_t *level_of, uint64_t *commons_share)
{
    static swarm_budget_t sb;
    uint64_t mk[SWARM_MAX_MODELS], cm[SWARM_MAX_MODELS];
    uint32_t rank[SWARM_MAX_MODELS];
    if (n == 0 || n > SWARM_MAX_MODELS || pool > VNA_ECON_MAX_TOTAL) return pool;
    uint64_t floor_ = swarm_muldiv(pool, GOLD_LO, GOLD_DEN, 0); /* G1 commons 8/21 */
    uint64_t market = pool - floor_;                            /* G2 market 13/21 */
    uint64_t wsum = 0;
    for (uint32_t i = 0; i < n; i++) wsum = vna_sat_add(wsum, cyc[i]);
    uint64_t unsold;
    if (wsum == 0 || wsum == UINT64_MAX) {
        for (uint32_t i = 0; i < n; i++) mk[i] = 0;
        unsold = market;
    } else {
        uint64_t cap = swarm_muldiv(market, GOLD_LO, GOLD_DEN, 0); /* anti-monopoly */
        unsold = swarm_capped_split(market, cyc, n, cap, mk);
    }
    uint64_t commons = floor_ + unsold;
    if (commons_out) *commons_out = commons;

    /* rank by lifetime contribution (desc), ties by NodeID (asc) */
    for (uint32_t i = 0; i < n; i++) rank[i] = i;
    for (uint32_t i = 1; i < n; i++) {
        uint32_t x = rank[i], j = i;
        while (j > 0) {
            uint32_t y = rank[j - 1];
            bool before = life[x] > life[y] || (life[x] == life[y] && id_less(&ids[x], &ids[y]));
            if (!before) break;
            rank[j] = y;
            j--;
        }
        rank[j] = x;
    }
    /* levels: capacities F(d+2) = 1, 2, 3, 5, ... (R1) */
    uint32_t levels = 0, held = 0;
    while (held < n) held += swarm_level_capacity(levels++);
    swarm_budget_init(&sb, levels, commons);
    uint32_t pos = 0;
    for (uint32_t d = 0; d < levels; d++) {
        uint32_t c = swarm_level_capacity(d);
        for (uint32_t k = 0; k < c && pos < n; k++, pos++) level_of[rank[pos]] = d;
    }
    for (uint32_t i = 0; i < n; i++) swarm_budget_register(&sb, i + 1u, (uint32_t) level_of[i]);
    swarm_budget_begin_cycle(&sb); /* R2-R5: Fibonacci weights, equal in level, exact */
    for (uint32_t i = 0; i < n; i++) {
        cm[i] = sb.slots[i].allotted;
        if (commons_share) commons_share[i] = cm[i];
    }

    /* G3: per-peer cap max(8/21 of pool, ceil(pool / n)); with exactly two
     * members the cap is the golden major ceil(13/21 of pool) instead, so a
     * contributor can still out-earn the other member. Excess shared equally. */
    uint64_t r;
    uint64_t cap = swarm_muldiv(pool, GOLD_LO, GOLD_DEN, 0);
    uint64_t equal = swarm_muldiv(pool, 1, n, &r) + (r ? 1u : 0u);
    if (equal > cap) cap = equal;
    if (n == 2) cap = swarm_muldiv(pool, GOLD_DEN - GOLD_LO, GOLD_DEN, &r) + (r ? 1u : 0u);
    uint64_t excess = 0;
    for (uint32_t i = 0; i < n; i++) {
        share[i] = mk[i] + cm[i];
        if (share[i] > cap) {
            excess += share[i] - cap;
            share[i] = cap;
        }
    }
    while (excess > 0) {
        uint64_t ones[SWARM_MAX_MODELS], add[SWARM_MAX_MODELS];
        uint32_t under = 0;
        for (uint32_t i = 0; i < n; i++) {
            ones[i] = share[i] < cap ? 1u : 0u;
            under += (uint32_t) ones[i];
        }
        if (under == 0) break;
        swarm_split_lr(excess, ones, n, add);
        excess = 0;
        for (uint32_t i = 0; i < n; i++) {
            share[i] += add[i];
            if (share[i] > cap) {
                excess += share[i] - cap;
                share[i] = cap;
            }
        }
    }
    uint64_t given = 0;
    for (uint32_t i = 0; i < n; i++) given += share[i];
    return pool - given; /* residue (0 unless every member is at the cap) */
}

static const char *const RES_NAMES[VNA_RES_COUNT] = {"route",   "record",  "file",
                                                     "compute", "storage", "memory"};
static const char *const FORM_NAMES[VNA_FORMS] = {"financial", "manufactured", "intellectual",
                                                  "human",     "social",       "natural",
                                                  "cultural",  "spiritual",    "system"};

static void make_hk(char *buf, uint32_t cap, const char *verb, uint8_t res, uint8_t form)
{
    uint32_t p = 0;
    const char *parts[5] = {verb, "(", RES_NAMES[res], ", form: ", FORM_NAMES[form]};
    for (uint32_t k = 0; k < 5; k++)
        for (const char *s = parts[k]; *s && p + 2 < cap; s++) buf[p++] = *s;
    buf[p++] = ')';
    buf[p] = 0;
}

vna_status_t vna_book_settle(vna_book_t *b, uint64_t now, vna_drbg_t *rng, vna_rcpt_out_t *out)
{
    uint64_t cyc[SWARM_MAX_MODELS], life[SWARM_MAX_MODELS], share[SWARM_MAX_MODELS];
    uint64_t lvl[SWARM_MAX_MODELS];
    vna_id_t ids[SWARM_MAX_MODELS];
    uint32_t idx[SWARM_MAX_MODELS];
    uint8_t rnd[32];
    char hk[64];
    if (!b || !rng) return VNA_ERR_ARG;
    if (out) out->n = 0;
    for (uint8_t f = 0; f < VNA_FORMS; f++) {
        if (!priceable(f) || b->pool[f] == 0) continue;
        uint32_t n = 0;
        for (uint32_t i = 0; i < b->n && n < SWARM_MAX_MODELS; i++) {
            const vna_account_t *a = &b->acct[i];
            if (!a->used || !a->member) continue;
            cyc[n] = a->contrib[f];
            life[n] = a->contrib_total;
            ids[n] = a->id;
            idx[n] = i;
            n++;
        }
        if (n == 0) continue; /* nobody to give it to: it expires below */
        uint64_t pool = b->pool[f];
        (void) vna_econ_split(pool, cyc, life, ids, n, share, 0, lvl, 0);
        for (uint32_t k = 0; k < n; k++) {
            if (share[k] == 0) continue;
            vna_account_t *a = &b->acct[idx[k]];
            make_hk(hk, sizeof hk, "offer", VNA_RES_COMPUTE, f);
            if (vna_receipt_prepare(b, &g_r, VNA_TR_DISTRIBUTE, f, VNA_RES_COMPUTE, &b->idn->id,
                                    &a->id, share[k], share[k], 1000, now, 0, hk) != VNA_OK)
                return VNA_ERR_ARG;
            vna_drbg_gen(rng, rnd, 32);
            int32_t len = vna_receipt_sign_seller(&g_r, b->idn, rnd, g_rc, sizeof g_rc);
            if (len < 0) return VNA_ERR_SPACE;
            uint32_t sig_off = (uint32_t) len - 2u * VNA_SIG_LEN;
            vna_drbg_gen(rng, rnd, 32);
            vna_status_t st =
                apply_parsed(b, &g_r, g_rc, (uint32_t) len, sig_off, 0, 0, now, rnd, true);
            if (st != VNA_OK) return st;
            if (out && out->n < out->cap) {
                vna_copy(out->buf[out->n], g_rc, (uint32_t) len);
                out->len[out->n] = (uint32_t) len;
                out->n++;
            }
        }
    }
    /* G4 / R6: what was not distributed expires (no hoarding) */
    for (uint8_t f = 0; f < VNA_FORMS; f++) {
        if (b->pool[f] == 0) continue;
        uint64_t x = b->pool[f];
        b->pool[f] = 0;
        b->burned[VNA_TK_NEUTRAL][f] += x;
        uint8_t ref[32];
        vna_htag("vinea/v2/expire", (const uint8_t *) &b->cycle, sizeof b->cycle, ref);
        vna_drbg_gen(rng, rnd, 32);
        ledger_triple(b, VNA_TK_NEUTRAL, f, 0x13, 0, x, VNA_ACC_BURN, VNA_ACC_POOL, ref, 1, 0,
                      b->cycle, now, rnd);
    }
    for (uint32_t i = 0; i < b->n; i++)
        for (uint8_t f = 0; f < VNA_FORMS; f++) b->acct[i].contrib[f] = 0;
    for (uint32_t r = 0; r < VNA_RES_COUNT; r++) {
        b->received_units[r] = 0;
        b->demand[r] = swarm_muldiv(b->demand[r], GOLD_HI, GOLD_DEN, 0); /* decay 13/21 */
    }
    b->cycle++;
    return VNA_OK;
}

bool vna_book_conserved(const vna_book_t *b)
{
    for (uint8_t f = 0; f < VNA_FORMS; f++) {
        uint64_t ext = 0, held = 0;
        for (uint32_t i = 0; i < b->n; i++) {
            const vna_account_t *a = &b->acct[i];
            if (!a->used) continue;
            if (a->bal[VNA_TK_INTERNAL][f] || a->bal[VNA_TK_NEUTRAL][f]) return false;
            ext += a->bal[VNA_TK_EXTERNAL][f];
            held += a->held[f];
        }
        if (b->minted[VNA_TK_NEUTRAL][f] - b->burned[VNA_TK_NEUTRAL][f] != b->pool[f]) return false;
        if (b->minted[VNA_TK_EXTERNAL][f] - b->burned[VNA_TK_EXTERNAL][f] != ext) return false;
        if (b->held_in[f] - b->held_out[f] != held) return false;
        if (!priceable(f) && (ext || held || b->pool[f] || b->minted[VNA_TK_NEUTRAL][f] ||
                              b->minted[VNA_TK_EXTERNAL][f]))
            return false;
    }
    return b->ledger.debits == b->ledger.credits && vna_ledger_verify(&b->ledger, b->idn->pk);
}

/* ---- pricing ---- */
void vna_book_note_demand(vna_book_t *b, vna_resource_t r, uint64_t requests)
{
    if (r < VNA_RES_COUNT) b->demand[r] = vna_sat_add(b->demand[r], requests);
}

void vna_book_set_supply(vna_book_t *b, vna_resource_t r, uint64_t providers)
{
    if (r < VNA_RES_COUNT) b->supply[r] = providers ? providers : 1;
}

uint64_t vna_econ_price(uint64_t base, uint64_t demand, uint64_t supply)
{
    if (supply == 0) supply = 1;
    uint64_t cap = swarm_muldiv(base, GOLD_DEN, GOLD_LO, 0); /* base * 21/8 */
    uint64_t sum = vna_sat_add(supply, demand);
    uint64_t p = swarm_muldiv(base, sum, supply, 0);
    return p > cap ? cap : p;
}

uint64_t vna_book_price(const vna_book_t *b, vna_resource_t r, uint64_t base)
{
    if (r >= VNA_RES_COUNT) return base;
    return vna_econ_price(base, b->demand[r], b->supply[r]);
}

/* ---- gate ---- */
void vna_gate_init(vna_gate_t *g, const uint8_t owner_secret[32], uint64_t export_cap,
                   uint64_t import_cap, uint32_t max_ops)
{
    vna_zero(g, sizeof *g);
    vna_sha3(owner_secret, 32, g->owner_tag);
    g->export_cap = export_cap;
    g->import_cap = import_cap;
    g->max_ops = max_ops;
}

static bool gate_auth(const vna_gate_t *g, const uint8_t secret[32])
{
    uint8_t h[32];
    if (!secret) return false;
    vna_sha3(secret, 32, h);
    return vna_ct_eq(h, g->owner_tag, 32);
}

vna_status_t vna_gate_begin_cycle(vna_gate_t *g, const uint8_t owner_secret[32])
{
    if (!g || !gate_auth(g, owner_secret)) return VNA_ERR_AUTH;
    g->cycle++;
    g->exported = g->imported = 0;
    g->ops = 0;
    return VNA_OK;
}

vna_status_t vna_gate_export(vna_gate_t *g, const uint8_t owner_secret[32], swarm_budget_t *sb,
                             uint32_t model_id, vna_book_t *b, uint8_t form, uint64_t amount,
                             uint64_t *granted)
{
    uint8_t rnd[32], ref[32];
    if (granted) *granted = 0;
    if (!g || !gate_auth(g, owner_secret)) return VNA_ERR_AUTH;
    if (!sb || !b || amount == 0) return VNA_ERR_ARG;
    if (!priceable(form)) return VNA_ERR_INALIENABLE;
    if (g->ops >= g->max_ops) return VNA_ERR_CAP;
    if (amount > g->export_cap - g->exported) return VNA_ERR_CAP;
    uint64_t got = 0;
    if (swarm_budget_consume(sb, model_id, amount, &got) != SWARM_OK || got == 0)
        return VNA_ERR_FUNDS;
    g->ops++;
    g->exported += got;
    b->pool[form] += got;
    b->minted[VNA_TK_NEUTRAL][form] += got;
    b->burned[VNA_TK_INTERNAL][form] += got;
    vna_htag("vinea/v2/gate-export", (const uint8_t *) &g->cycle, sizeof g->cycle, ref);
    vna_sha3(ref, 32, rnd); /* signature randomness is hedged; derived nonce is fine */
    ledger_triple(b, VNA_TK_NEUTRAL, form, 0x11, 0, got, VNA_ACC_POOL, VNA_ACC_MINT, ref, 1, 0,
                  g->cycle, b->cycle, rnd);
    if (granted) *granted = got;
    return VNA_OK;
}

vna_status_t vna_gate_import(vna_gate_t *g, const uint8_t owner_secret[32], vna_book_t *b,
                             uint8_t form, uint64_t amount, swarm_budget_t *sb, uint64_t base_rate)
{
    uint8_t rnd[32], ref[32];
    if (!g || !gate_auth(g, owner_secret)) return VNA_ERR_AUTH;
    if (!sb || !b || amount == 0) return VNA_ERR_ARG;
    if (!priceable(form)) return VNA_ERR_INALIENABLE;
    if (g->ops >= g->max_ops) return VNA_ERR_CAP;
    if (amount > g->import_cap - g->imported) return VNA_ERR_CAP;
    uint64_t recv = b->received_units[VNA_RES_COMPUTE]; /* verified, countersigned receipts */
    if (g->imported > recv || amount > recv - g->imported) return VNA_ERR_FUNDS;
    uint64_t rate = base_rate + g->imported + amount;
    if (swarm_budget_set_rate(sb, rate) != SWARM_OK) return VNA_ERR_CAP;
    g->ops++;
    g->imported += amount;
    b->minted[VNA_TK_INTERNAL][form] += amount;
    vna_htag("vinea/v2/gate-import", (const uint8_t *) &g->cycle, sizeof g->cycle, ref);
    vna_sha3(ref, 32, rnd);
    ledger_triple(b, VNA_TK_INTERNAL, form, 0x12, 0, amount, VNA_ACC_SELF, VNA_ACC_MINT, ref, 1, 0,
                  g->cycle, b->cycle, rnd);
    return VNA_OK;
}

/* ---- negotiation ---- */
vna_status_t vna_neg_init(vna_neg_t *n, vna_neg_role_t role, uint64_t limit, uint64_t opening,
                          uint32_t max_rounds)
{
    if (!n || max_rounds == 0) return VNA_ERR_ARG;
    if (role == VNA_NEG_BUYER ? opening > limit : opening < limit) return VNA_ERR_ARG;
    n->role = (uint8_t) role;
    n->status = VNA_NEG_OPEN;
    n->limit = limit;
    n->current = opening;
    n->deal = 0;
    n->round = 0;
    n->max_rounds = max_rounds;
    return VNA_OK;
}

static uint64_t concede(uint64_t gap)
{
    uint64_t r;
    uint64_t step = swarm_muldiv(gap, GOLD_LO, GOLD_DEN, &r) + (r ? 1u : 0u); /* ceil(8/21 gap) */
    return step > gap ? gap : step;
}

vna_neg_status_t vna_neg_receive(vna_neg_t *n, uint64_t theirs, uint64_t *mine)
{
    if (!n || n->status == VNA_NEG_ACCEPT || n->status == VNA_NEG_FAIL)
        return n ? (vna_neg_status_t) n->status : VNA_NEG_FAIL;
    bool buyer = n->role == VNA_NEG_BUYER;
    if (buyer ? theirs <= n->current : theirs >= n->current) { /* within what I already offer */
        n->status = VNA_NEG_ACCEPT;
        n->deal = theirs;
        return VNA_NEG_ACCEPT;
    }
    if (++n->round >= n->max_rounds) {
        n->status = VNA_NEG_FAIL;
        return VNA_NEG_FAIL;
    }
    if (buyer)
        n->current += concede(n->limit - n->current);
    else
        n->current -= concede(n->current - n->limit);
    if (buyer ? theirs <= n->current : theirs >= n->current) {
        n->status = VNA_NEG_ACCEPT;
        n->deal = theirs;
        return VNA_NEG_ACCEPT;
    }
    if (mine) *mine = n->current;
    n->status = VNA_NEG_COUNTER;
    return VNA_NEG_COUNTER;
}

void vna_econ_plan(uint64_t total, const uint64_t *priority, uint32_t n, uint64_t *out)
{
    if (n > SWARM_MAX_MODELS) n = SWARM_MAX_MODELS;
    swarm_split_lr(total, priority, n, out);
}
