/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* mk_extra.c — reviews, ISF discovery, subscriptions, agent confirmation and
 * B2B purchase orders / invoices. See market.h. */
#include "mk_internal.h"
#include "pay_assure.h"

/* ===== reviews ===== */
mk_status_t mk_review(mk_market_t *m, uint16_t h, const mk_id_t *actor, uint8_t rating,
                      const char *cid)
{
    const mk_order_t *oc = mk_order(m, h);
    if (!oc) return MK_ERR_NOT_FOUND;
    mk_order_t *o = &m->order[h - 1];
    if (!mk__id_eq(&o->buyer, actor)) return MK_ERR_AUTH;
    /* Only a sale that actually completed: the seller was paid. */
    if (!o->settled || (o->state != MK_ORD_COMPLETED && o->state != MK_ORD_RESOLVED &&
                        o->state != MK_ORD_REFUNDED && o->state != MK_ORD_RETURN_OPEN))
        return MK_ERR_STATE;
    if (o->reviewed) return MK_ERR_DUPLICATE; /* one review per order */
    if (rating < 1 || rating > 5 || (cid && pay_strnlen(cid, MK_CID_MAX) >= MK_CID_MAX))
        return MK_ERR_ARG;
    mk_store_t *st = mk__store(m, o->store);
    if (!st) return MK_ERR_NOT_FOUND;
    mk_review_t *r = 0;
    for (uint32_t i = 0; i < MK_MAX_REVIEWS; i++)
        if (!m->review[i].used) {
            r = &m->review[i];
            break;
        }
    if (!r) return MK_ERR_FULL;
    uint32_t w = MK_Q16;
    if (m->hooks.review_weight) {
        w = m->hooks.review_weight(m->hooks.ctx, actor, o->store);
        if (w > MK_Q16) w = MK_Q16;
    }
    pay_memset(r, 0, sizeof *r);
    r->used = true;
    r->order = h;
    r->store = o->store;
    r->listing = o->line[0].listing;
    mk__id_copy(&r->reviewer, actor);
    r->rating = rating;
    if (cid) pay_strlcpy(r->cid, cid, MK_CID_MAX);
    r->weight_q16 = w;
    r->at = m->now;
    st->rating_wsum += (uint64_t) (rating - 1) * w;
    st->weight_sum += w;
    st->n_reviews++;
    o->reviewed = true;
    return MK_OK;
}

uint32_t mk_store_quality(const mk_market_t *m, uint16_t store)
{
    const mk_store_t *st = mk__store_c(m, store);
    if (!st) return MK_Q16 / 2;
    /* ((wsum / 4) + 1/2 * prior) / (weights + prior), prior = two reviews. */
    const uint64_t prior = 2u * MK_Q16;
    uint64_t num = st->rating_wsum * (MK_Q16 / 4) + (MK_Q16 / 2) * prior;
    uint64_t den = st->weight_sum + prior;
    uint64_t q = pay_udiv64(num, den, 0);
    return q > MK_Q16 ? MK_Q16 : (uint32_t) q;
}

/* ===== discovery (ISF, never ad spend) ===== */
static uint32_t tiebreak(const uint8_t digest[32], const mk_id_t *buyer)
{
    uint32_t k = 0;
    for (uint32_t i = 0; i < 4; i++)
        k = (k << 8) | (uint32_t) (digest[i] ^ (buyer ? buyer->b[i] : 0));
    return k;
}

uint32_t mk_discover(const mk_market_t *m, const mk_query_t *q, mk_hit_t *out, uint32_t max)
{
    if (!m || !q || !out || max == 0) return 0;
    mk_hit_t cand[MK_MAX_LISTINGS];
    uint32_t key[MK_MAX_LISTINGS];
    uint32_t n = 0;
    for (uint32_t i = 0; i < MK_MAX_LISTINGS; i++) {
        const mk_listing_t *L = &m->listing[i];
        if (!L->used || !L->active) continue;
        const mk_store_t *st = mk__store_c(m, L->spec.store);
        if (!st || !st->active) continue;
        if (q->category >= 0 && L->spec.category != (uint16_t) q->category) continue;
        if (q->kind >= 0 && (int32_t) L->spec.kind != q->kind) continue;
        if (q->ccy >= 0 && L->spec.ccy != (uint8_t) q->ccy) continue;
        if (q->buyer && mk__id_eq(q->buyer, &st->spec.owner)) continue;
        if (mk_available(m, (uint16_t) (i + 1)) == 0) continue;
        if (m->hooks.can_see &&
            !m->hooks.can_see(m->hooks.ctx, q->buyer_concord, st->spec.concord_id))
            continue; /* a mutual divide hides both sides */
        uint32_t isf = MK_Q16;
        if (m->hooks.isf) {
            isf = m->hooks.isf(m->hooks.ctx, q->buyer_concord, st->spec.concord_id);
            if (isf > MK_Q16) isf = MK_Q16;
        }
        uint32_t qual = mk_store_quality(m, L->spec.store);
        mk_hit_t *c = &cand[n];
        c->listing = (uint16_t) (i + 1);
        c->isf_q16 = isf;
        c->quality_q16 = qual;
        /* score = isf * (1 + quality) / 2, all Q16 */
        c->score_q16 = (uint32_t) (((uint64_t) isf * (MK_Q16 + qual)) >> 17);
        key[n] = tiebreak(L->digest, q->buyer);
        n++;
    }
    /* insertion sort: score desc, then the buyer-rotated tie-break */
    for (uint32_t i = 1; i < n; i++) {
        mk_hit_t x;
        pay_memcpy(&x, &cand[i], sizeof x);
        uint32_t kx = key[i];
        uint32_t j = i;
        while (j > 0 && (cand[j - 1].score_q16 < x.score_q16 ||
                         (cand[j - 1].score_q16 == x.score_q16 && key[j - 1] > kx))) {
            pay_memcpy(&cand[j], &cand[j - 1], sizeof x);
            key[j] = key[j - 1];
            j--;
        }
        pay_memcpy(&cand[j], &x, sizeof x);
        key[j] = kx;
    }
    uint8_t cap = q->per_store_cap ? q->per_store_cap : m->policy.per_store_cap;
    uint8_t per[MK_MAX_STORES];
    pay_memset(per, 0, sizeof per);
    uint32_t w = 0;
    for (uint32_t i = 0; i < n && w < max; i++) {
        uint16_t s = mk__listing_c(m, cand[i].listing)->spec.store;
        if (per[s - 1] >= cap) continue; /* anti-monopoly: no store floods a page */
        per[s - 1]++;
        pay_memcpy(&out[w++], &cand[i], sizeof cand[i]);
    }
    return w;
}

/* ===== subscriptions ===== */
int32_t mk__sub_on_paid(mk_market_t *m, uint16_t h)
{
    mk_order_t *o = mk__order(m, h);
    if (!o || o->n_lines != 1) return 0;
    const mk_listing_t *L = mk__listing_c(m, o->line[0].listing);
    if (!L || L->spec.kind != MK_KIND_SUBSCRIPTION) return 0;
    if (o->sub) { /* a renewal of an existing subscription */
        mk_sub_t *s = &m->sub[o->sub - 1];
        s->current_order = h;
        return o->sub;
    }
    for (uint32_t i = 0; i < MK_MAX_SUBS; i++) {
        mk_sub_t *s = &m->sub[i];
        if (s->used) continue;
        pay_memset(s, 0, sizeof *s);
        s->used = true;
        mk__id_copy(&s->buyer, &o->buyer);
        s->listing = o->line[0].listing;
        pay_strlcpy(s->region, o->region, MK_REGION_MAX);
        s->auto_renew = false; /* never on by default */
        s->price_lock = o->line[0].unit_price;
        s->period_end = m->now + L->spec.period;
        s->current_order = h;
        o->sub = (uint8_t) (i + 1);
        return (int32_t) (i + 1);
    }
    return MK_ERR_FULL;
}

int32_t mk_sub_start(mk_market_t *m, uint16_t h)
{
    const mk_order_t *o = mk_order(m, h);
    if (!o) return MK_ERR_NOT_FOUND;
    return o->sub ? o->sub : MK_ERR_STATE;
}

static mk_sub_t *sub_get(mk_market_t *m, uint16_t h)
{
    if (!m || h == 0 || h > MK_MAX_SUBS || !m->sub[h - 1].used) return 0;
    return &m->sub[h - 1];
}

mk_status_t mk_sub_opt_in(mk_market_t *m, uint16_t h, const mk_id_t *actor, uint32_t max_renewals)
{
    mk_sub_t *s = sub_get(m, h);
    if (!s) return MK_ERR_NOT_FOUND;
    if (!mk__id_eq(&s->buyer, actor)) return MK_ERR_AUTH;
    if (s->cancelled || max_renewals == 0) return MK_ERR_STATE;
    const mk_listing_t *L = mk__listing_c(m, s->listing);
    if (!L) return MK_ERR_NOT_FOUND;
    s->auto_renew = true;
    s->max_renewals = s->renewals + max_renewals;
    s->price_lock = L->spec.price; /* consent is to THIS price */
    return MK_OK;
}

mk_status_t mk_sub_cancel(mk_market_t *m, uint16_t h, const mk_id_t *actor)
{
    mk_sub_t *s = sub_get(m, h);
    if (!s) return MK_ERR_NOT_FOUND;
    if (!mk__id_eq(&s->buyer, actor)) return MK_ERR_AUTH;
    s->cancelled = true;
    s->auto_renew = false;
    return MK_OK;
}

void mk__sub_tick(mk_market_t *m)
{
    for (uint32_t i = 0; i < MK_MAX_SUBS; i++) {
        mk_sub_t *s = &m->sub[i];
        uint16_t sh = (uint16_t) (i + 1);
        if (!s->used || !s->auto_renew || s->cancelled || s->renewals >= s->max_renewals) continue;
        const mk_listing_t *L = mk__listing_c(m, s->listing);
        if (!L || !L->active) {
            s->auto_renew = false;
            continue;
        }
        uint64_t notice = m->policy.renewal_notice;
        uint64_t remind_at = s->period_end > notice ? s->period_end - notice : 0;
        if (L->spec.price > s->price_lock) {
            s->auto_renew = false; /* a price rise needs fresh consent */
            mk__notify(m, MK_NOTE_RENEWAL_PRICE_CHANGED, sh);
            continue;
        }
        if (!s->reminded && m->now >= remind_at) {
            s->reminded = true;
            s->reminded_at = m->now;
            mk__notify(m, MK_NOTE_RENEWAL_REMINDER, sh);
        }
        /* Renew only at period end AND at least `notice` after the reminder. */
        if (!s->reminded || m->now < s->period_end || m->now < s->reminded_at + notice) continue;
        mk_cart_line_t ln = {s->listing, 1, 0};
        mk__build_t b = {&s->buyer, s->region, false, 0, 0};
        int32_t oh = mk__order_build(m, &b, &ln, 1);
        if (oh < 0) {
            s->auto_renew = false;
            continue;
        }
        mk_order_t *o = mk__order(m, (uint16_t) oh);
        o->sub = (uint8_t) sh;
        mk__order_digest(m, o, (uint16_t) oh);
        if (mk_order_pay(m, (uint16_t) oh, &s->buyer) != MK_OK) {
            s->auto_renew = false; /* the order stays unpaid and expires */
            continue;
        }
        s->renewals++;
        s->period_end += L->spec.period;
        s->reminded = false;
        mk__notify(m, MK_NOTE_RENEWED, sh);
    }
}

/* ===== agents ===== */
int32_t mk_mandate_grant(mk_market_t *m, const mk_id_t *user, const mk_id_t *agent, uint8_t ccy,
                         uint64_t max_per_order, uint64_t max_total, uint64_t categories,
                         uint64_t expires)
{
    if (!m || !user || !agent || mk__id_eq(user, agent) || !mk__ccy(m, ccy) || max_per_order == 0 ||
        max_total < max_per_order || expires <= m->now)
        return MK_ERR_ARG;
    for (uint32_t i = 0; i < MK_MAX_MANDATES; i++) {
        mk_mandate_t *d = &m->mandate[i];
        if (d->used) continue;
        pay_memset(d, 0, sizeof *d);
        d->used = true;
        mk__id_copy(&d->user, user);
        mk__id_copy(&d->agent, agent);
        d->ccy = ccy;
        d->max_per_order = max_per_order;
        d->max_total = max_total;
        d->categories = categories;
        d->expires = expires;
        return (int32_t) (i + 1);
    }
    return MK_ERR_FULL;
}

mk_status_t mk_mandate_revoke(mk_market_t *m, uint16_t h, const mk_id_t *user)
{
    if (!m || h == 0 || h > MK_MAX_MANDATES || !m->mandate[h - 1].used) return MK_ERR_NOT_FOUND;
    if (!mk__id_eq(&m->mandate[h - 1].user, user)) return MK_ERR_AUTH;
    m->mandate[h - 1].revoked = true;
    return MK_OK;
}

void mk_confirm_message(const mk_market_t *m, uint16_t h, uint8_t out[32])
{
    const mk_order_t *o = mk_order(m, h);
    pay_hbuf hb;
    pay_hbuf_init(&hb);
    pay_hbuf_put(&hb, "ZXV-MKT-CONFIRM-1", 17);
    pay_hbuf_put(&hb, o ? o->digest : out, 32);
    pay_hbuf_u64(&hb, o ? o->nonce : 0);
    pay_hbuf_put(&hb, o ? o->buyer.b : out, 32);
    pay_hbuf_put(&hb, o ? o->agent.b : out, 32);
    (void) pay_hbuf_final(&hb, out);
}

mk_status_t mk_order_user_confirm(mk_market_t *m, uint16_t h, const uint8_t *token,
                                  uint32_t token_len)
{
    mk_order_t *o = mk__order(m, h);
    if (!o) return MK_ERR_NOT_FOUND;
    if (!o->needs_confirm || o->confirmed || o->state != MK_ORD_PENDING_PAYMENT)
        return MK_ERR_STATE;
    if (m->now >= o->confirm_by) return MK_ERR_WINDOW;
    mk_status_t rc = mk__mandate_ok(m, o, 0);
    if (rc != MK_OK) return rc;
    uint8_t msg[32];
    mk_confirm_message(m, h, msg);
    if (!mk__verify(m, &o->buyer, msg, 32, token, token_len)) return MK_ERR_AUTH;
    o->confirmed = true;
    m->mandate[o->mandate - 1].committed += o->total;
    return MK_OK;
}

/* ===== B2B ===== */
static mk_po_t *po_get(mk_market_t *m, uint16_t h)
{
    if (!m || h == 0 || h > MK_MAX_POS || !m->po[h - 1].used) return 0;
    return &m->po[h - 1];
}

static mk_invoice_t *inv_get(mk_market_t *m, uint16_t h)
{
    if (!m || h == 0 || h > MK_MAX_INVOICES || !m->invoice[h - 1].used) return 0;
    return &m->invoice[h - 1];
}

void mk_po_digest(const mk_po_spec_t *po, uint8_t out[32])
{
    pay_hbuf hb;
    pay_hbuf_init(&hb);
    pay_hbuf_put(&hb, "ZXV-MKT-PO-1", 12);
    uint32_t n = (uint32_t) pay_strnlen(po->po_number, sizeof po->po_number);
    pay_hbuf_u64(&hb, n);
    pay_hbuf_put(&hb, po->po_number, n);
    pay_hbuf_put(&hb, po->buyer_org.b, 32);
    pay_hbuf_u64(&hb, po->store);
    pay_hbuf_u64(&hb, po->ccy);
    n = (uint32_t) pay_strnlen(po->region, MK_REGION_MAX);
    pay_hbuf_u64(&hb, n);
    pay_hbuf_put(&hb, po->region, n);
    pay_hbuf_u64(&hb, po->n_lines);
    for (uint32_t i = 0; i < po->n_lines && i < MK_ORDER_LINES; i++) {
        pay_hbuf_u64(&hb, po->line[i].listing);
        pay_hbuf_u64(&hb, po->line[i].qty);
        pay_hbuf_u64(&hb, po->line[i].unit_price);
    }
    pay_hbuf_u64(&hb, po->net_days);
    pay_hbuf_u64(&hb, po->late_fee);
    pay_hbuf_u64(&hb, po->service_fee);
    (void) pay_hbuf_final(&hb, out);
}

int32_t mk_po_draft(mk_market_t *m, const mk_po_spec_t *spec)
{
    if (!m || !spec || spec->n_lines == 0 || spec->n_lines > MK_ORDER_LINES ||
        pay_strnlen(spec->po_number, sizeof spec->po_number) >= sizeof spec->po_number ||
        spec->po_number[0] == 0 || pay_strnlen(spec->region, MK_REGION_MAX) >= MK_REGION_MAX ||
        spec->net_days > 365)
        return MK_ERR_ARG;
    const mk_store_t *st = mk__store_c(m, spec->store);
    if (!st || !st->active) return MK_ERR_NOT_FOUND;
    if (mk__id_eq(&spec->buyer_org, &st->spec.owner)) return MK_ERR_POLICY;
    if (!mk__ccy(m, spec->ccy)) return MK_ERR_CCY;
    if (spec->late_fee > m->policy.max_flat_fee || spec->service_fee > m->policy.max_flat_fee)
        return MK_ERR_POLICY; /* flat fees only, and capped */
    for (uint32_t i = 0; i < spec->n_lines; i++) {
        const mk_listing_t *L = mk__listing_c(m, spec->line[i].listing);
        if (!L || L->spec.store != spec->store || spec->line[i].qty == 0 ||
            L->spec.ccy != spec->ccy || L->spec.kind == MK_KIND_BOOKING ||
            L->spec.kind == MK_KIND_SUBSCRIPTION)
            return MK_ERR_ARG;
    }
    for (uint32_t i = 0; i < MK_MAX_POS; i++) {
        mk_po_t *p = &m->po[i];
        if (p->used) continue;
        pay_memset(p, 0, sizeof *p);
        p->used = true;
        pay_memcpy(&p->spec, spec, sizeof *spec);
        mk_po_digest(spec, p->digest);
        p->state = MK_PO_DRAFT;
        return (int32_t) (i + 1);
    }
    return MK_ERR_FULL;
}

mk_status_t mk_po_submit(mk_market_t *m, uint16_t h, const uint8_t *sig, uint32_t sig_len)
{
    mk_po_t *p = po_get(m, h);
    if (!p) return MK_ERR_NOT_FOUND;
    if (p->state != MK_PO_DRAFT) return MK_ERR_STATE;
    if (!mk__verify(m, &p->spec.buyer_org, p->digest, 32, sig, sig_len)) return MK_ERR_AUTH;
    p->state = MK_PO_SUBMITTED;
    return MK_OK;
}

static bool po_seller(const mk_market_t *m, const mk_po_t *p, const mk_id_t *a)
{
    const mk_store_t *st = mk__store_c(m, p->spec.store);
    return st && mk__id_eq(&st->spec.owner, a);
}

mk_status_t mk_po_accept(mk_market_t *m, uint16_t h, const mk_id_t *actor)
{
    mk_po_t *p = po_get(m, h);
    if (!p) return MK_ERR_NOT_FOUND;
    if (p->state != MK_PO_SUBMITTED) return MK_ERR_STATE;
    if (!po_seller(m, p, actor)) return MK_ERR_AUTH;
    p->state = MK_PO_ACCEPTED;
    return MK_OK;
}

mk_status_t mk_po_reject(mk_market_t *m, uint16_t h, const mk_id_t *actor)
{
    mk_po_t *p = po_get(m, h);
    if (!p) return MK_ERR_NOT_FOUND;
    if (p->state != MK_PO_SUBMITTED) return MK_ERR_STATE;
    if (!po_seller(m, p, actor)) return MK_ERR_AUTH;
    p->state = MK_PO_REJECTED;
    return MK_OK;
}

mk_status_t mk_po_cancel(mk_market_t *m, uint16_t h, const mk_id_t *actor)
{
    mk_po_t *p = po_get(m, h);
    if (!p) return MK_ERR_NOT_FOUND;
    if (p->state != MK_PO_DRAFT && p->state != MK_PO_SUBMITTED) return MK_ERR_STATE;
    if (!mk__id_eq(&p->spec.buyer_org, actor)) return MK_ERR_AUTH;
    p->state = MK_PO_CANCELLED;
    return MK_OK;
}

int32_t mk_invoice_issue(mk_market_t *m, uint16_t h, const mk_id_t *actor, const char *number)
{
    mk_po_t *p = po_get(m, h);
    if (!p) return MK_ERR_NOT_FOUND;
    if (p->state != MK_PO_ACCEPTED) return MK_ERR_STATE;
    if (!po_seller(m, p, actor)) return MK_ERR_AUTH;
    if (!number || number[0] == 0 || pay_strnlen(number, 24) >= 24) return MK_ERR_ARG;
    for (uint32_t i = 0; i < MK_MAX_INVOICES; i++)
        if (m->invoice[i].used && m->invoice[i].store == p->spec.store &&
            pay_streq(m->invoice[i].number, number))
            return MK_ERR_DUPLICATE;
    int32_t fi = -1;
    for (uint32_t i = 0; i < MK_MAX_INVOICES; i++)
        if (!m->invoice[i].used) {
            fi = (int32_t) i;
            break;
        }
    if (fi < 0) return MK_ERR_FULL;
    uint64_t base = 0, tax = 0;
    bool unconf = false;
    for (uint32_t i = 0; i < p->spec.n_lines; i++) {
        const mk_po_line_t *pl = &p->spec.line[i];
        const mk_listing_t *L = mk__listing_c(m, pl->listing);
        if (!L) return MK_ERR_NOT_FOUND;
        uint32_t avail = mk_available(m, pl->listing);
        if (avail != MK_UNLIMITED && avail < pl->qty) return MK_ERR_STOCK;
        pay_u128 g = pay_mul64(pl->unit_price, pl->qty);
        uint64_t lt, t;
        if (g.hi) return MK_ERR_OVERFLOW;
        mk_status_t rc =
            mk__line_tax(m, p->spec.region, L->spec.tax_class, g.lo, 0, &lt, &t, &unconf);
        if (rc != MK_OK) return rc;
        if (!pay_add_ok(base, lt - t, &base) || !pay_add_ok(tax, t, &tax)) return MK_ERR_OVERFLOW;
    }
    /* Shipped: stock leaves the shelf. */
    for (uint32_t i = 0; i < p->spec.n_lines; i++) {
        mk_listing_t *L = mk__listing(m, p->spec.line[i].listing);
        if (L->spec.stock != MK_UNLIMITED) L->spec.stock -= p->spec.line[i].qty;
    }
    const mk_store_t *st = mk__store_c(m, p->spec.store);
    mk_invoice_t *v = &m->invoice[fi];
    pay_memset(v, 0, sizeof *v);
    v->used = true;
    v->state = MK_INV_ISSUED;
    v->po = h;
    v->store = p->spec.store;
    v->ccy = p->spec.ccy;
    mk__id_copy(&v->buyer, &p->spec.buyer_org);
    mk__id_copy(&v->seller, &st->spec.owner);
    pay_strlcpy(v->number, number, sizeof v->number);
    v->base = base;
    v->tax = tax;
    v->tax_unconfigured = unconf;
    if (p->spec.service_fee) {
        mk_fee_t *f = &v->fee[v->n_fees++];
        pay_strlcpy(f->label, "service fee (flat)", sizeof f->label);
        f->amount = p->spec.service_fee;
        f->applied = true;
        v->fees += f->amount;
    }
    if (p->spec.late_fee) {
        mk_fee_t *f = &v->fee[v->n_fees++];
        pay_strlcpy(f->label, "late fee (flat, once)", sizeof f->label);
        f->amount = p->spec.late_fee;
        f->late = true;
    }
    v->issued_at = m->now;
    v->due_at = m->now + (uint64_t) p->spec.net_days * MK_DAY;
    p->invoice = (uint16_t) (fi + 1);
    p->state = MK_PO_INVOICED;
    return fi + 1;
}

uint64_t mk_invoice_amount_due(const mk_market_t *m, uint16_t h)
{
    if (!m || h == 0 || h > MK_MAX_INVOICES || !m->invoice[h - 1].used) return 0;
    const mk_invoice_t *v = &m->invoice[h - 1];
    if (v->state == MK_INV_PAID || v->state == MK_INV_VOID) return 0;
    return v->base + v->tax + v->fees;
}

static void w_s(char *dst, uint32_t cap, const char *a, const char *b)
{
    pay_w w;
    pay_w_init(&w, dst, cap);
    pay_w_s(&w, a);
    pay_w_s(&w, b);
    (void) pay_w_finish(&w);
}

int32_t mk_invoice_pain001(mk_market_t *m, uint16_t h, char *out, uint32_t cap)
{
    mk_invoice_t *v = inv_get(m, h);
    if (!v) return MK_ERR_NOT_FOUND;
    if (v->state != MK_INV_ISSUED && v->state != MK_INV_OVERDUE) return MK_ERR_STATE;
    const mk_ccy_t *c = mk__ccy(m, v->ccy);
    if (!c || c->is_vfv) return MK_ERR_CCY; /* VFV never goes into an ISO 20022 Ccy field */
    if (!m->hooks.pain001) return MK_ERR_HOOK;
    char e2e[40], rmt[40];
    w_s(e2e, sizeof e2e, "ZXV-INV-", v->number);
    w_s(rmt, sizeof rmt, "/ZXV/INV/", v->number);
    mk_pain001_req_t rq = {
        v->number, e2e, &v->buyer, &v->seller, c->alpha, c->minor, mk_invoice_amount_due(m, h),
        v->due_at, rmt};
    int32_t n = m->hooks.pain001(m->hooks.ctx, &rq, out, cap);
    if (n < 0) return MK_ERR_HOOK;
    if (v->state == MK_INV_ISSUED) v->state = MK_INV_INITIATED;
    return n;
}

mk_status_t mk_invoice_settle(mk_market_t *m, uint16_t h, uint64_t amount, bool internal)
{
    mk_invoice_t *v = inv_get(m, h);
    if (!v) return MK_ERR_NOT_FOUND;
    if (v->state != MK_INV_ISSUED && v->state != MK_INV_INITIATED && v->state != MK_INV_OVERDUE)
        return MK_ERR_STATE;
    uint64_t due = mk_invoice_amount_due(m, h);
    if (amount != due) return MK_ERR_ARG; /* exact amount; no partials in this version */
    uint64_t rev = v->base + v->fees;
    uint64_t afee = pay_assure_fee(rev); /* once, on the seller's revenue */
    bool remit = m->policy.facilitator_remits_tax;
    mk_settle_t s;
    pay_memset(&s, 0, sizeof s);
    s.kind = MK_SETTLE_INVOICE;
    s.ref = h;
    s.ccy = v->ccy;
    s.buyer = &v->buyer;
    s.seller = &v->seller;
    s.to_commons = afee;
    s.to_tax = remit ? v->tax : 0;
    if (internal) {
        s.from_buyer = amount;
        s.to_seller = amount - afee - s.to_tax;
    } else {
        s.from_seller = afee + s.to_tax; /* the bank already paid the seller */
    }
    if (!mk__settle(m, &s)) return MK_ERR_SETTLE;
    v->assure_fee = afee;
    v->paid_at = m->now;
    v->state = MK_INV_PAID;
    mk_po_t *p = po_get(m, v->po);
    if (p) p->state = MK_PO_CLOSED;
    return MK_OK;
}

mk_status_t mk_invoice_void(mk_market_t *m, uint16_t h, const mk_id_t *actor)
{
    mk_invoice_t *v = inv_get(m, h);
    if (!v) return MK_ERR_NOT_FOUND;
    if (v->state != MK_INV_ISSUED && v->state != MK_INV_OVERDUE) return MK_ERR_STATE;
    if (!mk__id_eq(&v->seller, actor)) return MK_ERR_AUTH;
    v->state = MK_INV_VOID;
    mk_po_t *p = po_get(m, v->po);
    if (p) p->state = MK_PO_CLOSED;
    return MK_OK;
}

void mk__inv_tick(mk_market_t *m)
{
    for (uint32_t i = 0; i < MK_MAX_INVOICES; i++) {
        mk_invoice_t *v = &m->invoice[i];
        /* Payment initiated before the due date is never late. */
        if (!v->used || v->state != MK_INV_ISSUED || m->now <= v->due_at) continue;
        v->state = MK_INV_OVERDUE;
        for (uint32_t f = 0; f < v->n_fees; f++)
            if (v->fee[f].late && !v->fee[f].applied) {
                v->fee[f].applied = true; /* flat, once: it never grows */
                v->fees += v->fee[f].amount;
            }
        mk__notify(m, MK_NOTE_INVOICE_OVERDUE, i + 1);
    }
}
