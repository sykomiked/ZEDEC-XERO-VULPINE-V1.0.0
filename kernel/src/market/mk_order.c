/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* mk_order.c — the order state machine, escrow book, assurance fee, refunds,
 * disputes and returns. See market.h (M2..M4). */
#include "mk_internal.h"
#include "pay_assure.h"

#define S_NONE  MK_ORD_FREE
#define S_PRIOR MK_ORD_STATE_COUNT

/* The whole transition table. Rows: state; columns: mk_event_t. */
static const uint8_t k_next[MK_ORD_STATE_COUNT][MK_EV_COUNT] = {
    /*                 PAY          CANCEL           EXPIRE         FULFIL
                       CONFIRM          AUTO_REL         FUL_TIMEOUT      DISPUTE
                       RULING           REQ_RETURN       RET_RECEIVED     RET_DECLINED
                       REFUND_ALL */
    [MK_ORD_PENDING_PAYMENT] = {MK_ORD_PAID, MK_ORD_CANCELLED, MK_ORD_EXPIRED, S_NONE, S_NONE,
                                S_NONE, S_NONE, S_NONE, S_NONE, S_NONE, S_NONE, S_NONE, S_NONE},
    [MK_ORD_PAID] = {S_NONE, MK_ORD_REFUNDED, S_NONE, MK_ORD_FULFILLED, MK_ORD_COMPLETED, S_NONE,
                     MK_ORD_REFUNDED, MK_ORD_DISPUTED, S_NONE, S_NONE, S_NONE, S_NONE,
                     MK_ORD_REFUNDED},
    [MK_ORD_FULFILLED] = {S_NONE, S_NONE, S_NONE, S_NONE, MK_ORD_COMPLETED, MK_ORD_COMPLETED,
                          S_NONE, MK_ORD_DISPUTED, S_NONE, MK_ORD_RETURN_OPEN, S_NONE, S_NONE,
                          MK_ORD_REFUNDED},
    [MK_ORD_COMPLETED] = {S_NONE, S_NONE, S_NONE, S_NONE, S_NONE, S_NONE, S_NONE, S_NONE, S_NONE,
                          MK_ORD_RETURN_OPEN, S_NONE, S_NONE, MK_ORD_REFUNDED},
    [MK_ORD_DISPUTED] = {S_NONE, S_NONE, S_NONE, S_NONE, S_NONE, S_NONE, S_NONE, S_NONE,
                         MK_ORD_RESOLVED, S_NONE, S_NONE, S_NONE, S_NONE},
    [MK_ORD_RETURN_OPEN] = {S_NONE, S_NONE, S_NONE, S_NONE, S_NONE, S_NONE, S_NONE, S_NONE, S_NONE,
                            S_NONE, S_PRIOR, S_PRIOR, MK_ORD_REFUNDED},
};

mk_order_state_t mk_order_next(mk_order_state_t from, mk_event_t ev)
{
    if ((uint32_t) from >= MK_ORD_STATE_COUNT || (uint32_t) ev >= MK_EV_COUNT) return MK_ORD_FREE;
    return (mk_order_state_t) k_next[from][ev];
}

const char *mk_order_state_name(mk_order_state_t s)
{
    static const char *const n[MK_ORD_STATE_COUNT] = {
        "free",     "pending-payment", "paid",      "fulfilled", "completed", "disputed",
        "resolved", "return-open",     "cancelled", "refunded",  "expired"};
    return (uint32_t) s < MK_ORD_STATE_COUNT ? n[s] : "?";
}

static bool allowed(const mk_order_t *o, mk_event_t ev)
{
    return mk_order_next(o->state, ev) != MK_ORD_FREE;
}

static bool is_buyer(const mk_order_t *o, const mk_id_t *a)
{
    return mk__id_eq(&o->buyer, a);
}

static bool is_seller(const mk_order_t *o, const mk_id_t *a)
{
    return mk__id_eq(&o->seller, a);
}

static uint64_t held(const mk_order_t *o)
{
    return o->held_base + o->held_tax;
}

static void settle_init(mk_settle_t *s, mk_settle_kind_t k, uint16_t h, const mk_order_t *o)
{
    pay_memset(s, 0, sizeof *s);
    s->kind = k;
    s->ref = h;
    s->ccy = o->ccy;
    s->buyer = &o->buyer;
    s->seller = &o->seller;
}

/* ----- inventory ----- */
static void unreserve(mk_market_t *m, mk_order_t *o)
{
    for (uint32_t i = 0; i < o->n_lines; i++) {
        mk_listing_t *L = mk__listing(m, o->line[i].listing);
        if (!L) continue;
        if (L->spec.stock != MK_UNLIMITED) L->reserved -= o->line[i].qty;
        if (L->spec.kind == MK_KIND_BOOKING) L->spec.slot[o->line[i].slot].booked -= o->line[i].qty;
    }
}

/* Paid but never fulfilled: units go back on the shelf / slot. */
static void restore_units(mk_market_t *m, const mk_line_t *ln, uint32_t qty)
{
    mk_listing_t *L = mk__listing(m, ln->listing);
    if (!L) return;
    if (L->spec.stock != MK_UNLIMITED && qty < MK_UNLIMITED - L->spec.stock) L->spec.stock += qty;
    if (L->spec.kind == MK_KIND_BOOKING) L->spec.slot[ln->slot].booked -= qty;
}

/* ----- release: escrow to seller / commons / tax, afee once (M3) ----- */
static mk_status_t release_all(mk_market_t *m, uint16_t h, mk_order_t *o, mk_settle_kind_t k)
{
    uint64_t base = o->held_base, tax = o->held_tax;
    uint64_t rel = o->released_base + base;
    uint64_t t_new = pay_assure_fee(rel - o->returned_base);
    uint64_t delta = t_new - o->fee_charged;
    if (delta > base) return MK_ERR_OVERFLOW; /* cannot happen: fee(a+b)-fee(a) <= b */
    mk_settle_t s;
    settle_init(&s, k, h, o);
    s.to_commons = delta;
    if (m->policy.facilitator_remits_tax) {
        s.to_seller = base - delta;
        s.to_tax = tax;
    } else {
        s.to_seller = base - delta + tax;
    }
    if (!mk__settle(m, &s)) return MK_ERR_SETTLE;
    o->to_seller += s.to_seller;
    o->to_commons += s.to_commons;
    o->to_tax += s.to_tax;
    o->released_base = rel;
    o->fee_charged = t_new;
    o->held_base = 0;
    o->held_tax = 0;
    if (base || tax) o->settled = true;
    return MK_OK;
}

static mk_status_t complete(mk_market_t *m, uint16_t h, mk_order_t *o)
{
    mk_status_t rc = release_all(m, h, o, MK_SETTLE_RELEASE);
    if (rc != MK_OK) return rc;
    o->state = MK_ORD_COMPLETED;
    o->completed_at = m->now;
    o->settled = true; /* a completed sale is review-eligible */
    mk__notify(m, MK_NOTE_COMPLETED, h);
    return MK_OK;
}

/* ----- refunds (M4) ----- */
static void line_refund_amounts(const mk_line_t *ln, uint32_t k, uint64_t *R, uint64_t *T)
{
    uint32_t r = ln->refunded_qty + k;
    uint64_t base = ln->line_total - ln->tax;
    uint64_t b_after, t_after;
    if (r == ln->qty) {
        b_after = base;
        t_after = ln->tax;
    } else {
        (void) pay_muldiv(base, r, ln->qty, &b_after, 0);
        (void) pay_muldiv(ln->tax, r, ln->qty, &t_after, 0);
    }
    *T = t_after - ln->refunded_tax;
    *R = (b_after + t_after) - ln->refunded_total;
}

static bool all_refunded(const mk_order_t *o)
{
    for (uint32_t i = 0; i < o->n_lines; i++)
        if (o->line[i].refunded_qty != o->line[i].qty) return false;
    return true;
}

/* Escrow has not been released yet (refunds come out of it). */
static bool prerelease(const mk_order_t *o)
{
    mk_order_state_t st = o->state == MK_ORD_RETURN_OPEN ? o->ret.from_state : o->state;
    return st == MK_ORD_PAID || st == MK_ORD_FULFILLED;
}

/* Refund k units of line `li`. Pre-release money comes out of escrow;
 * post-release the seller funds it, the commons returns the afee
 * difference and (facilitator mode) the tax account returns the tax. */
static mk_status_t refund_units(mk_market_t *m, uint16_t h, mk_order_t *o, uint8_t li, uint32_t k,
                                bool *seller_failed)
{
    mk_line_t *ln = &o->line[li];
    uint64_t R, T;
    line_refund_amounts(ln, k, &R, &T);
    uint64_t B = R - T;
    mk_settle_t s;
    settle_init(&s, MK_SETTLE_REFUND, h, o);
    s.to_buyer = R;
    uint64_t ret_base = o->returned_base, afee = o->fee_charged;
    bool from_escrow = prerelease(o);
    if (!from_escrow) {
        /* Post-release: what the seller actually received for these units. */
        ret_base = o->returned_base + B;
        uint64_t t_new = pay_assure_fee(o->released_base - ret_base);
        uint64_t back = o->fee_charged - t_new;
        s.from_commons = back;
        s.from_tax = m->policy.facilitator_remits_tax ? T : 0;
        s.from_seller = R - back - s.from_tax;
        afee = t_new;
    }
    if (!mk__settle(m, &s)) {
        if (seller_failed) *seller_failed = !from_escrow;
        return MK_ERR_SETTLE;
    }
    o->to_buyer += R;
    if (from_escrow) {
        o->held_base -= B;
        o->held_tax -= T;
    } else {
        o->seller_funded += s.from_seller;
        o->commons_returned += s.from_commons;
        o->tax_returned += s.from_tax;
        o->returned_base = ret_base;
        o->fee_charged = afee;
    }
    ln->refunded_qty += k;
    ln->refunded_total += R;
    ln->refunded_tax += T;
    return MK_OK;
}

static mk_status_t refund_everything(mk_market_t *m, uint16_t h, mk_order_t *o, bool restore)
{
    for (uint8_t i = 0; i < o->n_lines; i++) {
        uint32_t k = o->line[i].qty - o->line[i].refunded_qty;
        if (!k) continue;
        mk_status_t rc = refund_units(m, h, o, i, k, 0);
        if (rc != MK_OK) return rc;
        if (restore) restore_units(m, &o->line[i], k);
    }
    return MK_OK;
}

/* ===== public transitions ===== */
mk_status_t mk_order_pay(mk_market_t *m, uint16_t h, const mk_id_t *actor)
{
    mk_order_t *o = mk__order(m, h);
    if (!o) return MK_ERR_NOT_FOUND;
    if (!allowed(o, MK_EV_PAY)) return MK_ERR_STATE;
    if (!is_buyer(o, actor)) return MK_ERR_AUTH;
    if (o->needs_confirm && !o->confirmed) return MK_ERR_CONFIRM;
    if (m->now >= o->pay_by) return MK_ERR_WINDOW;
    mk_settle_t s;
    settle_init(&s, MK_SETTLE_CAPTURE, h, o);
    s.from_buyer = o->total;
    if (!mk__settle(m, &s)) return MK_ERR_SETTLE;
    o->captured = o->total;
    o->held_base = o->total - o->tax;
    o->held_tax = o->tax;
    /* Reserved units become sold units. */
    uint64_t fulfil_by = 0;
    const mk_store_t *st = mk__store_c(m, o->store);
    uint32_t within =
        st && st->spec.fulfil_within ? st->spec.fulfil_within : m->policy.default_fulfil;
    for (uint32_t i = 0; i < o->n_lines; i++) {
        mk_listing_t *L = mk__listing(m, o->line[i].listing);
        if (L->spec.stock != MK_UNLIMITED) {
            L->reserved -= o->line[i].qty;
            L->spec.stock -= o->line[i].qty;
        }
        uint64_t by = m->now + within;
        if (L->spec.kind == MK_KIND_BOOKING) {
            const mk_slot_t *sl = &L->spec.slot[o->line[i].slot];
            by = sl->start + sl->duration + m->policy.booking_grace;
        }
        if (by > fulfil_by) fulfil_by = by;
    }
    o->fulfil_by = fulfil_by;
    o->paid_at = m->now;
    o->state = MK_ORD_PAID;
    if (mk__sub_on_paid(m, h) > 0) {
        /* Subscription access starts at payment; escrow runs to period end
         * so the buyer can dispute during the period. */
        const mk_listing_t *L = mk__listing_c(m, o->line[0].listing);
        o->fulfilled_at = m->now;
        o->proof_verified = true;
        o->release_at = m->now + L->spec.period;
        o->state = MK_ORD_FULFILLED;
    }
    return MK_OK;
}

mk_status_t mk_order_cancel(mk_market_t *m, uint16_t h, const mk_id_t *actor)
{
    mk_order_t *o = mk__order(m, h);
    if (!o) return MK_ERR_NOT_FOUND;
    if (!allowed(o, MK_EV_CANCEL)) return MK_ERR_STATE;
    bool buyer = is_buyer(o, actor), seller = is_seller(o, actor);
    if (!buyer && !seller) return MK_ERR_AUTH;
    if (o->state == MK_ORD_PENDING_PAYMENT) {
        unreserve(m, o);
        o->state = MK_ORD_CANCELLED;
        return MK_OK;
    }
    /* PAID: a buyer cancelling a booking after its cutoff pays the flat,
     * disclosed cancellation fee; nobody else ever pays a fee to cancel. */
    uint64_t fee = 0;
    if (buyer) {
        for (uint32_t i = 0; i < o->n_lines; i++) {
            const mk_listing_t *L = mk__listing_c(m, o->line[i].listing);
            if (L && L->spec.kind == MK_KIND_BOOKING) {
                const mk_slot_t *sl = &L->spec.slot[o->line[i].slot];
                if (m->now + L->spec.cancel_cutoff >= sl->start) {
                    pay_u128 f = pay_mul64(L->spec.cancel_fee, o->line[i].qty);
                    if (f.hi || !pay_add_ok(fee, f.lo, &fee)) return MK_ERR_OVERFLOW;
                }
            }
        }
    }
    if (fee > o->held_base) fee = o->held_base;
    if (fee == 0) {
        mk_status_t rc = refund_everything(m, h, o, true);
        if (rc != MK_OK) return rc;
        o->state = MK_ORD_REFUNDED;
        return MK_OK;
    }
    /* One atomic settlement: buyer gets everything but the fee; the fee is the
     * seller's revenue (fee charged once); all tax goes back to the buyer. */
    uint64_t t_new = pay_assure_fee(fee);
    mk_settle_t s;
    settle_init(&s, MK_SETTLE_REFUND, h, o);
    s.to_buyer = held(o) - fee;
    s.to_commons = t_new;
    s.to_seller = fee - t_new;
    if (!mk__settle(m, &s)) return MK_ERR_SETTLE;
    o->to_buyer += s.to_buyer;
    o->to_commons += s.to_commons;
    o->to_seller += s.to_seller;
    o->released_base = fee;
    o->cancel_fee = fee;
    o->fee_charged = t_new;
    o->held_base = 0;
    o->held_tax = 0;
    o->settled = true;
    for (uint32_t i = 0; i < o->n_lines; i++)
        restore_units(m, &o->line[i], o->line[i].qty - o->line[i].refunded_qty);
    o->state = MK_ORD_REFUNDED;
    return MK_OK;
}

static uint32_t clamp_release(const mk_market_t *m, uint32_t v)
{
    if (v < m->policy.min_auto_release) v = m->policy.min_auto_release;
    if (v > m->policy.max_auto_release) v = m->policy.max_auto_release;
    return v;
}

mk_status_t mk_order_fulfil(mk_market_t *m, uint16_t h, const mk_id_t *actor, const uint8_t *proof,
                            uint32_t proof_len)
{
    mk_order_t *o = mk__order(m, h);
    if (!o) return MK_ERR_NOT_FOUND;
    if (!allowed(o, MK_EV_FULFIL)) return MK_ERR_STATE;
    if (!is_seller(o, actor)) return MK_ERR_AUTH;
    if (!proof || proof_len == 0) return MK_ERR_ARG;
    bool physical = false, verified = true;
    uint64_t window = 0;
    for (uint32_t i = 0; i < o->n_lines; i++) {
        const mk_listing_t *L = mk__listing_c(m, o->line[i].listing);
        if (!L) return MK_ERR_NOT_FOUND;
        uint64_t w = clamp_release(m, L->spec.auto_release);
        switch (L->spec.kind) {
        case MK_KIND_PHYSICAL:
            physical = true;
            break;
        case MK_KIND_DIGITAL: {
            /* The delivered CID must be exactly the listed content. */
            uint32_t n = (uint32_t) pay_strnlen(L->spec.cid, MK_CID_MAX);
            if (proof_len != n || !pay_memeq(proof, L->spec.cid, n)) return MK_ERR_AUTH;
            break;
        }
        case MK_KIND_SUBSCRIPTION:
            w = L->spec.period; /* the buyer can dispute during the period */
            break;
        default:
            break;
        }
        if (w > window) window = w;
    }
    if (physical) {
        verified =
            m->hooks.verify_delivery && m->hooks.verify_delivery(m->hooks.ctx, h, proof, proof_len);
        if (!verified) window *= 2; /* unverified shipment: the buyer gets twice as long */
    }
    pay_sha3_256(proof, proof_len, o->proof_digest);
    o->proof_verified = verified;
    o->fulfilled_at = m->now;
    o->release_at = m->now + window;
    o->state = MK_ORD_FULFILLED;
    return MK_OK;
}

mk_status_t mk_order_confirm(mk_market_t *m, uint16_t h, const mk_id_t *actor)
{
    mk_order_t *o = mk__order(m, h);
    if (!o) return MK_ERR_NOT_FOUND;
    if (!allowed(o, MK_EV_CONFIRM)) return MK_ERR_STATE;
    if (!is_buyer(o, actor)) return MK_ERR_AUTH;
    if (o->state == MK_ORD_PAID && !o->fulfilled_at)
        o->fulfilled_at = m->now; /* the buyer confirms receipt directly */
    return complete(m, h, o);
}

mk_status_t mk_order_refund_line(mk_market_t *m, uint16_t h, const mk_id_t *actor, uint8_t line,
                                 uint32_t qty)
{
    mk_order_t *o = mk__order(m, h);
    if (!o) return MK_ERR_NOT_FOUND;
    if (!allowed(o, MK_EV_REFUND_ALL) || o->state == MK_ORD_RETURN_OPEN) return MK_ERR_STATE;
    if (!is_seller(o, actor)) return MK_ERR_AUTH;
    if (line >= o->n_lines || qty == 0 || qty > o->line[line].qty - o->line[line].refunded_qty)
        return MK_ERR_ARG;
    mk_order_state_t st = o->state;
    mk_status_t rc = refund_units(m, h, o, line, qty, 0);
    if (rc != MK_OK) return rc;
    if (st == MK_ORD_PAID) restore_units(m, &o->line[line], qty);
    if (all_refunded(o)) o->state = MK_ORD_REFUNDED;
    return MK_OK;
}

/* ===== disputes ===== */
mk_status_t mk_dispute_open(mk_market_t *m, uint16_t h, const mk_id_t *actor)
{
    mk_order_t *o = mk__order(m, h);
    if (!o) return MK_ERR_NOT_FOUND;
    if (!allowed(o, MK_EV_DISPUTE)) return MK_ERR_STATE;
    if (!is_buyer(o, actor) && !is_seller(o, actor)) return MK_ERR_AUTH;
    if (held(o) == 0) return MK_ERR_STATE;
    mk_dispute_t *d = &o->dispute;
    mk_id_t arb;
    mk__id_copy(&arb, &d->arbiter);
    pay_memset(d, 0, sizeof *d);
    mk__id_copy(&d->arbiter, &arb);
    d->community = o->community_mediation;
    if (d->community) {
        if (!m->hooks.select_jurors) return MK_ERR_HOOK;
        mk_id_t cand[MK_MAX_JURORS];
        uint32_t want = m->policy.jurors;
        uint32_t n = m->hooks.select_jurors(m->hooks.ctx, h, cand, want);
        if (n > want) n = want;
        for (uint32_t i = 0; i < n; i++) {
            bool dup = false;
            for (uint32_t j = 0; j < d->n_jurors; j++) dup |= mk__id_eq(&d->juror[j], &cand[i]);
            if (dup || mk__id_zero(&cand[i]) || mk__id_eq(&cand[i], &o->buyer) ||
                mk__id_eq(&cand[i], &o->seller))
                continue; /* parties never judge their own case */
            mk__id_copy(&d->juror[d->n_jurors++], &cand[i]);
        }
        if (d->n_jurors == 0) return MK_ERR_POLICY;
    }
    d->open = true;
    mk__id_copy(&d->opened_by, actor);
    d->opened_at = m->now;
    d->deadline = m->now + m->policy.dispute_window;
    d->from_state = o->state;
    o->state = MK_ORD_DISPUTED;
    return MK_OK;
}

mk_status_t mk_dispute_evidence(mk_market_t *m, uint16_t h, const mk_id_t *actor, const char *cid,
                                const uint8_t digest[32])
{
    mk_order_t *o = mk__order(m, h);
    if (!o) return MK_ERR_NOT_FOUND;
    if (o->state != MK_ORD_DISPUTED) return MK_ERR_STATE;
    if (!is_buyer(o, actor) && !is_seller(o, actor)) return MK_ERR_AUTH;
    if (!cid || !digest || pay_strnlen(cid, MK_CID_MAX) >= MK_CID_MAX || cid[0] == 0)
        return MK_ERR_ARG;
    mk_dispute_t *d = &o->dispute;
    if (d->n_ev >= MK_MAX_EVIDENCE) return MK_ERR_FULL;
    mk_evidence_t *e = &d->ev[d->n_ev++];
    mk__id_copy(&e->from, actor);
    pay_strlcpy(e->cid, cid, MK_CID_MAX);
    pay_memcpy(e->digest, digest, 32);
    e->at = m->now;
    return MK_OK;
}

void mk_ruling_digest(const mk_market_t *m, uint16_t h, uint64_t award, uint8_t out[32])
{
    const mk_order_t *o = mk_order(m, h);
    pay_hbuf hb;
    pay_hbuf_init(&hb);
    pay_hbuf_put(&hb, "ZXV-MKT-RULING-1", 16);
    pay_hbuf_put(&hb, o ? o->digest : out, 32);
    pay_hbuf_u64(&hb, o ? o->dispute.opened_at : 0);
    pay_hbuf_u64(&hb, award);
    (void) pay_hbuf_final(&hb, out);
}

/* Split held escrow: `award` to the buyer (tax proportionally, floor), the
 * rest released to the seller with the afee once. */
static mk_status_t finalize_ruling(mk_market_t *m, uint16_t h, mk_order_t *o, uint64_t award)
{
    uint64_t H = held(o);
    if (award > H) return MK_ERR_ARG;
    uint64_t tax_b = 0;
    if (H) (void) pay_muldiv(award, o->held_tax, H, &tax_b, 0);
    uint64_t base_b = award - tax_b;
    uint64_t base_s = o->held_base - base_b, tax_s = o->held_tax - tax_b;
    uint64_t rel = o->released_base + base_s;
    uint64_t t_new = pay_assure_fee(rel - o->returned_base);
    uint64_t delta = t_new - o->fee_charged;
    if (delta > base_s) return MK_ERR_OVERFLOW;
    mk_settle_t s;
    settle_init(&s, MK_SETTLE_RULING, h, o);
    s.to_buyer = award;
    s.to_commons = delta;
    if (m->policy.facilitator_remits_tax) {
        s.to_seller = base_s - delta;
        s.to_tax = tax_s;
    } else {
        s.to_seller = base_s - delta + tax_s;
    }
    if (!mk__settle(m, &s)) return MK_ERR_SETTLE;
    o->to_buyer += s.to_buyer;
    o->to_seller += s.to_seller;
    o->to_commons += s.to_commons;
    o->to_tax += s.to_tax;
    o->released_base = rel;
    o->fee_charged = t_new;
    o->held_base = 0;
    o->held_tax = 0;
    if (base_s || tax_s) o->settled = true;
    o->dispute.buyer_award = award;
    o->dispute.open = false;
    o->state = MK_ORD_RESOLVED;
    return MK_OK;
}

static uint64_t median_votes(const mk_dispute_t *d, uint32_t *count)
{
    uint64_t v[MK_MAX_JURORS];
    uint32_t n = 0;
    for (uint32_t i = 0; i < d->n_jurors; i++)
        if (d->voted[i]) v[n++] = d->vote[i];
    for (uint32_t i = 1; i < n; i++) {
        uint64_t x = v[i];
        uint32_t j = i;
        while (j > 0 && v[j - 1] > x) {
            v[j] = v[j - 1];
            j--;
        }
        v[j] = x;
    }
    *count = n;
    return n ? v[(n - 1) >> 1] : 0; /* lower median */
}

mk_status_t mk_dispute_rule(mk_market_t *m, uint16_t h, const mk_id_t *mediator, uint64_t award,
                            const uint8_t *sig, uint32_t sig_len)
{
    mk_order_t *o = mk__order(m, h);
    if (!o) return MK_ERR_NOT_FOUND;
    if (!allowed(o, MK_EV_RULING)) return MK_ERR_STATE;
    if (award > held(o)) return MK_ERR_ARG;
    mk_dispute_t *d = &o->dispute;
    uint8_t dg[32];
    mk_ruling_digest(m, h, award, dg);
    if (!d->community) {
        if (!mk__id_eq(mediator, &d->arbiter)) return MK_ERR_AUTH;
        if (!mk__verify(m, mediator, dg, 32, sig, sig_len)) return MK_ERR_AUTH;
        return finalize_ruling(m, h, o, award);
    }
    uint32_t j = 0;
    while (j < d->n_jurors && !mk__id_eq(&d->juror[j], mediator)) j++;
    if (j == d->n_jurors) return MK_ERR_AUTH;
    if (d->voted[j]) return MK_ERR_DUPLICATE;
    if (!mk__verify(m, mediator, dg, 32, sig, sig_len)) return MK_ERR_AUTH;
    d->voted[j] = true;
    d->vote[j] = award;
    uint32_t n;
    uint64_t med = median_votes(d, &n);
    if (n == d->n_jurors) return finalize_ruling(m, h, o, med);
    return MK_OK;
}

/* ===== returns ===== */
mk_status_t mk_return_request(mk_market_t *m, uint16_t h, const mk_id_t *actor, uint8_t line,
                              uint32_t qty)
{
    mk_order_t *o = mk__order(m, h);
    if (!o) return MK_ERR_NOT_FOUND;
    if (!allowed(o, MK_EV_REQUEST_RETURN)) return MK_ERR_STATE;
    if (!is_buyer(o, actor)) return MK_ERR_AUTH;
    if (line >= o->n_lines || qty == 0 || qty > o->line[line].qty - o->line[line].refunded_qty)
        return MK_ERR_ARG;
    const mk_listing_t *L = mk__listing_c(m, o->line[line].listing);
    if (!L || L->spec.return_window == 0) return MK_ERR_POLICY;
    if (m->now > o->fulfilled_at + L->spec.return_window) return MK_ERR_WINDOW;
    mk_return_t *r = &o->ret;
    pay_memset(r, 0, sizeof *r);
    r->open = true;
    r->line = line;
    r->qty = qty;
    r->requested_at = m->now;
    r->from_state = o->state;
    o->state = MK_ORD_RETURN_OPEN;
    return MK_OK;
}

mk_status_t mk_return_shipped(mk_market_t *m, uint16_t h, const mk_id_t *actor)
{
    mk_order_t *o = mk__order(m, h);
    if (!o) return MK_ERR_NOT_FOUND;
    if (o->state != MK_ORD_RETURN_OPEN || o->ret.shipped_back) return MK_ERR_STATE;
    if (!is_buyer(o, actor)) return MK_ERR_AUTH;
    o->ret.shipped_back = true;
    o->ret.shipped_at = m->now;
    return MK_OK;
}

static mk_status_t return_close(mk_market_t *m, uint16_t h, mk_order_t *o, bool refund,
                                bool restock)
{
    mk_return_t *r = &o->ret;
    if (refund) {
        bool seller_failed = false;
        mk_status_t rc = refund_units(m, h, o, r->line, r->qty, &seller_failed);
        if (rc != MK_OK) {
            if (!seller_failed) return rc;
            /* Post-release and the seller did not fund it: record the default
             * (reputation input) and close the return. */
            mk_store_t *st = mk__store(m, o->store);
            if (st) st->seller_defaults++;
            r->open = false;
            o->state = r->from_state;
            return MK_ERR_SETTLE;
        }
        if (restock) {
            mk_listing_t *L = mk__listing(m, o->line[r->line].listing);
            if (L && L->spec.stock != MK_UNLIMITED && r->qty < MK_UNLIMITED - L->spec.stock)
                L->spec.stock += r->qty;
        }
    }
    r->open = false;
    o->state = all_refunded(o) ? MK_ORD_REFUNDED : r->from_state;
    return MK_OK;
}

mk_status_t mk_return_received(mk_market_t *m, uint16_t h, const mk_id_t *actor, bool restock)
{
    mk_order_t *o = mk__order(m, h);
    if (!o) return MK_ERR_NOT_FOUND;
    if (!allowed(o, MK_EV_RETURN_RECEIVED)) return MK_ERR_STATE;
    if (!is_seller(o, actor)) return MK_ERR_AUTH;
    return return_close(m, h, o, true, restock);
}

mk_status_t mk_return_decline(mk_market_t *m, uint16_t h, const mk_id_t *actor)
{
    mk_order_t *o = mk__order(m, h);
    if (!o) return MK_ERR_NOT_FOUND;
    if (!allowed(o, MK_EV_RETURN_DECLINED)) return MK_ERR_STATE;
    if (!is_seller(o, actor)) return MK_ERR_AUTH;
    /* Back where it was; with escrow still held the buyer may dispute. */
    return return_close(m, h, o, false, false);
}

/* ===== conservation (M2, M3) ===== */
bool mk_order_conserved(const mk_market_t *m, uint16_t h)
{
    const mk_order_t *o = mk_order(m, h);
    if (!o) return false;
    uint64_t in = o->captured + o->seller_funded + o->commons_returned + o->tax_returned;
    uint64_t out = o->to_seller + o->to_commons + o->to_tax + o->to_buyer;
    if (in < out || in - out != held(o)) return false;
    if (o->held_tax > o->tax || o->captured > o->total) return false;
    switch (o->state) {
    case MK_ORD_COMPLETED:
    case MK_ORD_RESOLVED:
    case MK_ORD_CANCELLED:
    case MK_ORD_REFUNDED:
    case MK_ORD_EXPIRED:
    case MK_ORD_PENDING_PAYMENT:
        if (held(o) != 0) return false;
        break;
    default:
        break;
    }
    if (o->to_commons < o->commons_returned ||
        o->to_commons - o->commons_returned != o->fee_charged)
        return false;
    if (o->returned_base > o->released_base ||
        o->fee_charged != pay_assure_fee(o->released_base - o->returned_base))
        return false;
    uint64_t rsum = 0;
    for (uint32_t i = 0; i < o->n_lines; i++) {
        const mk_line_t *ln = &o->line[i];
        if (ln->refunded_qty > ln->qty || ln->refunded_total > ln->line_total ||
            ln->refunded_tax > ln->tax)
            return false;
        if (ln->refunded_qty == ln->qty &&
            (ln->refunded_total != ln->line_total || ln->refunded_tax != ln->tax))
            return false;
        rsum += ln->refunded_total;
    }
    /* Everything the buyer got back is line refunds, a ruling award, or a
     * booking cancellation (everything but the flat fee). */
    uint64_t expect = rsum + o->dispute.buyer_award;
    if (o->cancel_fee) expect = o->captured - o->cancel_fee;
    if (o->to_buyer != expect) return false;
    return true;
}

bool mk_market_conserved(const mk_market_t *m)
{
    for (uint32_t i = 0; i < MK_MAX_ORDERS; i++)
        if (m->order[i].used && !mk_order_conserved(m, (uint16_t) (i + 1))) return false;
    return true;
}

/* ===== timers ===== */
void mk__order_tick(mk_market_t *m)
{
    for (uint32_t i = 0; i < MK_MAX_ORDERS; i++) {
        mk_order_t *o = &m->order[i];
        uint16_t h = (uint16_t) (i + 1);
        if (!o->used) continue;
        switch (o->state) {
        case MK_ORD_PENDING_PAYMENT:
            if (m->now >= o->pay_by ||
                (o->needs_confirm && !o->confirmed && m->now >= o->confirm_by)) {
                unreserve(m, o);
                o->state = MK_ORD_EXPIRED;
            }
            break;
        case MK_ORD_PAID:
            if (m->now >= o->fulfil_by && refund_everything(m, h, o, true) == MK_OK) {
                o->state = MK_ORD_REFUNDED;
                mk__notify(m, MK_NOTE_AUTO_CANCELLED, h);
            }
            break;
        case MK_ORD_FULFILLED:
            if (m->now >= o->release_at && complete(m, h, o) == MK_OK)
                mk__notify(m, MK_NOTE_AUTO_RELEASED, h);
            break;
        case MK_ORD_RETURN_OPEN:
            if (o->ret.shipped_back && m->now >= o->ret.shipped_at + m->policy.return_accept)
                (void) return_close(m, h, o, true, false); /* seller silent: refund */
            else if (!o->ret.shipped_back &&
                     m->now >= o->ret.requested_at + m->policy.return_accept)
                (void) return_close(m, h, o, false, false); /* buyer never shipped */
            break;
        case MK_ORD_DISPUTED: {
            uint32_t n;
            uint64_t med = median_votes(&o->dispute, &n);
            if (o->dispute.community && m->now >= o->dispute.deadline &&
                2u * n > o->dispute.n_jurors)
                (void) finalize_ruling(m, h, o, med);
            /* Otherwise the dispute stays open: people decide, not a timer. */
            break;
        }
        default:
            break;
        }
    }
}
