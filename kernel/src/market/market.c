/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* market.c — setup, currencies, tax rates, signed storefronts and listings,
 * inventory, carts, checkout and the clock. See market.h. */
#include "mk_internal.h"

/* ===== small helpers ===== */
bool mk__id_eq(const mk_id_t *a, const mk_id_t *b)
{
    return a && b && pay_memeq(a->b, b->b, 32);
}

bool mk__id_zero(const mk_id_t *a)
{
    for (uint32_t i = 0; i < 32; i++)
        if (a->b[i]) return false;
    return true;
}

void mk__id_copy(mk_id_t *dst, const mk_id_t *src)
{
    pay_memcpy(dst->b, src->b, 32);
}

mk_store_t *mk__store(mk_market_t *m, uint16_t h)
{
    if (!m || h == MK_NONE || h > MK_MAX_STORES || !m->store[h - 1].used) return 0;
    return &m->store[h - 1];
}

const mk_store_t *mk__store_c(const mk_market_t *m, uint16_t h)
{
    if (!m || h == MK_NONE || h > MK_MAX_STORES || !m->store[h - 1].used) return 0;
    return &m->store[h - 1];
}

mk_listing_t *mk__listing(mk_market_t *m, uint16_t h)
{
    if (!m || h == MK_NONE || h > MK_MAX_LISTINGS || !m->listing[h - 1].used) return 0;
    return &m->listing[h - 1];
}

const mk_listing_t *mk__listing_c(const mk_market_t *m, uint16_t h)
{
    if (!m || h == MK_NONE || h > MK_MAX_LISTINGS || !m->listing[h - 1].used) return 0;
    return &m->listing[h - 1];
}

mk_order_t *mk__order(mk_market_t *m, uint16_t h)
{
    if (!m || h == MK_NONE || h > MK_MAX_ORDERS || !m->order[h - 1].used) return 0;
    return &m->order[h - 1];
}

const mk_order_t *mk_order(const mk_market_t *m, uint16_t h)
{
    if (!m || h == MK_NONE || h > MK_MAX_ORDERS || !m->order[h - 1].used) return 0;
    return &m->order[h - 1];
}

const mk_ccy_t *mk__ccy(const mk_market_t *m, uint8_t h)
{
    if (!m || h == 0 || h > MK_MAX_CCY || !m->ccy[h - 1].used) return 0;
    return &m->ccy[h - 1];
}

bool mk__muldiv_round(uint64_t a, uint64_t b, uint64_t c, mk_round_t mode, uint64_t *out)
{
    uint64_t q, r;
    if (!pay_muldiv(a, b, c, &q, &r)) return false;
    bool up = false;
    if (mode == MK_ROUND_CEIL)
        up = r != 0;
    else if (mode == MK_ROUND_HALF_UP)
        up = r >= c - r; /* 2r >= c without overflow */
    if (up && !pay_add_ok(q, 1, &q)) return false;
    *out = q;
    return true;
}

bool mk__settle(mk_market_t *m, const mk_settle_t *s)
{
    if (!(s->from_buyer | s->from_seller | s->from_commons | s->from_tax | s->to_seller |
          s->to_commons | s->to_tax | s->to_buyer))
        return true;
    if (!m->hooks.settle) return false;
    return m->hooks.settle(m->hooks.ctx, s);
}

void mk__notify(mk_market_t *m, mk_note_t k, uint32_t ref)
{
    if (m->hooks.notify) m->hooks.notify(m->hooks.ctx, k, ref);
}

bool mk__verify(const mk_market_t *m, const mk_id_t *signer, const uint8_t *msg, uint32_t len,
                const uint8_t *sig, uint32_t sig_len)
{
    if (!m->hooks.verify || !sig || sig_len == 0) return false;
    return m->hooks.verify(m->hooks.ctx, signer, msg, len, sig, sig_len);
}

static bool region_eq(const char *a, const char *b)
{
    for (uint32_t i = 0; i < MK_REGION_MAX; i++) {
        if (a[i] != b[i]) return false;
        if (!a[i]) return true;
    }
    return true;
}

static bool str_bounded(const char *s, uint32_t cap)
{
    return s && pay_strnlen(s, cap) < cap;
}

/* ===== setup ===== */
void mk_policy_default(mk_policy_t *p)
{
    pay_memset(p, 0, sizeof *p);
    p->payment_window = 3600u;
    p->default_fulfil = 7u * MK_DAY;
    p->min_auto_release = 1u * MK_DAY;
    p->max_auto_release = 30u * MK_DAY;
    p->dispute_window = 14u * MK_DAY;
    p->return_accept = 7u * MK_DAY;
    p->confirm_window = 1u * MK_DAY;
    p->renewal_notice = 3u * MK_DAY;
    p->booking_grace = 1u * MK_DAY;
    p->max_flat_fee = 10000000u; /* in minor units; the operator sets its own */
    p->facilitator_remits_tax = false;
    p->per_store_cap = 2;
    p->jurors = 3;
}

void mk_init(mk_market_t *m, const mk_hooks_t *hooks, const mk_policy_t *policy)
{
    pay_memset(m, 0, sizeof *m);
    if (hooks) pay_memcpy(&m->hooks, hooks, sizeof *hooks);
    if (policy)
        pay_memcpy(&m->policy, policy, sizeof *policy);
    else
        mk_policy_default(&m->policy);
    if (m->policy.jurors == 0 || m->policy.jurors > MK_MAX_JURORS) m->policy.jurors = 3;
    if (m->policy.per_store_cap == 0) m->policy.per_store_cap = 2;
    (void) mk_ccy_register(m, "VFV", 555u, 2u, true);
}

int32_t mk_ccy_find(const mk_market_t *m, const char *alpha)
{
    if (!m || !alpha) return MK_ERR_ARG;
    for (uint32_t i = 0; i < MK_MAX_CCY; i++)
        if (m->ccy[i].used && pay_streq(m->ccy[i].alpha, alpha)) return (int32_t) (i + 1);
    return MK_ERR_NOT_FOUND;
}

int32_t mk_ccy_register(mk_market_t *m, const char *alpha, uint16_t numeric, uint8_t minor,
                        bool is_vfv)
{
    if (!m || !alpha || pay_strnlen(alpha, 4) != 3 || minor > 4) return MK_ERR_ARG;
    for (uint32_t i = 0; i < 3; i++)
        if (!pay_is_upper(alpha[i])) return MK_ERR_ARG;
    if (mk_ccy_find(m, alpha) > 0) return MK_ERR_DUPLICATE;
    for (uint32_t i = 0; i < MK_MAX_CCY; i++) {
        if (m->ccy[i].used) continue;
        mk_ccy_t *c = &m->ccy[i];
        c->used = true;
        c->is_vfv = is_vfv;
        pay_strlcpy(c->alpha, alpha, sizeof c->alpha);
        c->numeric = numeric;
        c->minor = minor;
        return (int32_t) (i + 1);
    }
    return MK_ERR_FULL;
}

int32_t mk_tax_set(mk_market_t *m, const mk_tax_rate_t *r)
{
    if (!m || !r || r->den == 0 || !str_bounded(r->region, MK_REGION_MAX) ||
        r->rounding > MK_ROUND_CEIL)
        return MK_ERR_ARG;
    int32_t freei = -1;
    for (uint32_t i = 0; i < MK_MAX_TAX_RATES; i++) {
        mk_tax_rate_t *t = &m->tax[i];
        if (t->used && t->tax_class == r->tax_class && region_eq(t->region, r->region)) {
            pay_memcpy(t, r, sizeof *t);
            t->used = true;
            return (int32_t) (i + 1);
        }
        if (!t->used && freei < 0) freei = (int32_t) i;
    }
    if (freei < 0) return MK_ERR_FULL;
    pay_memcpy(&m->tax[freei], r, sizeof *r);
    m->tax[freei].used = true;
    return freei + 1;
}

mk_status_t mk__line_tax(const mk_market_t *m, const char *region, uint16_t tax_class,
                         uint64_t goods, uint64_t ship, uint64_t *line_total, uint64_t *tax,
                         bool *unconfigured)
{
    const mk_tax_rate_t *r = 0;
    for (uint32_t i = 0; i < MK_MAX_TAX_RATES; i++)
        if (m->tax[i].used && m->tax[i].tax_class == tax_class &&
            region_eq(m->tax[i].region, region)) {
            r = &m->tax[i];
            break;
        }
    uint64_t gross;
    if (!pay_add_ok(goods, ship, &gross)) return MK_ERR_OVERFLOW;
    if (!r) {
        *unconfigured = true;
        *line_total = gross;
        *tax = 0;
        return MK_OK;
    }
    uint64_t taxable = r->tax_shipping ? gross : goods;
    uint64_t t;
    if (!r->inclusive) {
        if (!mk__muldiv_round(taxable, r->num, r->den, r->rounding, &t)) return MK_ERR_OVERFLOW;
        if (!pay_add_ok(gross, t, line_total)) return MK_ERR_OVERFLOW;
    } else {
        /* net = taxable * den / (den + num); tax = taxable - net */
        uint64_t net, dd = (uint64_t) r->den + r->num;
        mk_round_t inv = r->rounding == MK_ROUND_FLOOR  ? MK_ROUND_CEIL
                         : r->rounding == MK_ROUND_CEIL ? MK_ROUND_FLOOR
                                                        : MK_ROUND_HALF_UP;
        if (!mk__muldiv_round(taxable, r->den, dd, inv, &net)) return MK_ERR_OVERFLOW;
        t = taxable - net;
        *line_total = gross;
    }
    *tax = t;
    return MK_OK;
}

/* ===== canonical digests ===== */
static void h_str(pay_hbuf *h, const char *s, uint32_t cap)
{
    uint32_t n = (uint32_t) pay_strnlen(s, cap);
    pay_hbuf_u64(h, n);
    pay_hbuf_put(h, s, n);
}

void mk_store_digest(const mk_store_spec_t *s, uint8_t out[32])
{
    pay_hbuf h;
    pay_hbuf_init(&h);
    h_str(&h, "ZXV-MKT-STORE-1", 32);
    h_str(&h, s->name, MK_NAME_MAX);
    pay_hbuf_put(&h, s->owner.b, 32);
    h_str(&h, s->region, MK_REGION_MAX);
    pay_hbuf_u64(&h, s->concord_id);
    pay_hbuf_u64(&h, s->institution);
    pay_hbuf_u64(&h, s->has_arbiter);
    pay_hbuf_put(&h, s->arbiter.b, 32);
    pay_hbuf_u64(&h, s->fulfil_within);
    pay_hbuf_u64(&h, s->version);
    (void) pay_hbuf_final(&h, out);
}

void mk_listing_digest(const mk_market_t *m, const mk_listing_spec_t *l, uint8_t out[32])
{
    pay_hbuf h;
    pay_hbuf_init(&h);
    h_str(&h, "ZXV-MKT-LISTING-1", 32);
    const mk_store_t *st = mk__store_c(m, l->store);
    if (st)
        pay_hbuf_put(&h, st->digest, 32); /* binds the listing to one store version's key */
    else
        pay_hbuf_u64(&h, 0);
    pay_hbuf_u64(&h, l->kind);
    h_str(&h, l->title, sizeof l->title);
    pay_hbuf_u64(&h, l->category);
    const mk_ccy_t *c = mk__ccy(m, l->ccy);
    h_str(&h, c ? c->alpha : "", 4);
    pay_hbuf_u64(&h, c ? c->minor : 0xFF);
    pay_hbuf_u64(&h, l->price);
    pay_hbuf_u64(&h, l->shipping);
    pay_hbuf_u64(&h, l->stock);
    pay_hbuf_u64(&h, l->tax_class);
    h_str(&h, l->cid, MK_CID_MAX);
    pay_hbuf_u64(&h, l->return_window);
    pay_hbuf_u64(&h, l->auto_release);
    pay_hbuf_u64(&h, l->cancel_fee);
    pay_hbuf_u64(&h, l->cancel_cutoff);
    pay_hbuf_u64(&h, l->period);
    pay_hbuf_u64(&h, l->n_slots);
    for (uint32_t i = 0; i < l->n_slots && i < MK_MAX_SLOTS; i++) {
        pay_hbuf_u64(&h, l->slot[i].start);
        pay_hbuf_u64(&h, l->slot[i].duration);
        pay_hbuf_u64(&h, l->slot[i].capacity);
    }
    pay_hbuf_u64(&h, l->version);
    (void) pay_hbuf_final(&h, out);
}

/* ===== storefronts ===== */
mk_status_t mk_store_publish(mk_market_t *m, const mk_store_spec_t *spec, const uint8_t *sig,
                             uint32_t sig_len, uint16_t *handle)
{
    if (!m || !spec || !handle || !str_bounded(spec->name, MK_NAME_MAX) ||
        !str_bounded(spec->region, MK_REGION_MAX) || spec->name[0] == 0 ||
        mk__id_zero(&spec->owner))
        return MK_ERR_ARG;
    if (spec->has_arbiter &&
        (mk__id_zero(&spec->arbiter) || mk__id_eq(&spec->arbiter, &spec->owner)))
        return MK_ERR_ARG;
    uint8_t d[32];
    mk_store_digest(spec, d);
    if (!mk__verify(m, &spec->owner, d, 32, sig, sig_len)) return MK_ERR_AUTH;
    mk_store_t *s;
    if (*handle != MK_NONE) {
        s = mk__store(m, *handle);
        if (!s) return MK_ERR_NOT_FOUND;
        if (!mk__id_eq(&s->spec.owner, &spec->owner)) return MK_ERR_AUTH;
        if (spec->version <= s->spec.version) return MK_ERR_DUPLICATE; /* replay or rollback */
    } else {
        s = 0;
        for (uint32_t i = 0; i < MK_MAX_STORES; i++)
            if (!m->store[i].used) {
                s = &m->store[i];
                pay_memset(s, 0, sizeof *s);
                *handle = (uint16_t) (i + 1);
                break;
            }
        if (!s) return MK_ERR_FULL;
        s->used = true;
    }
    pay_memcpy(&s->spec, spec, sizeof *spec);
    pay_memcpy(s->digest, d, 32);
    s->active = true;
    return MK_OK;
}

mk_status_t mk_store_close(mk_market_t *m, const mk_id_t *actor, uint16_t store)
{
    mk_store_t *s = mk__store(m, store);
    if (!s) return MK_ERR_NOT_FOUND;
    if (!mk__id_eq(actor, &s->spec.owner)) return MK_ERR_AUTH;
    s->active = false; /* open orders still run to completion */
    return MK_OK;
}

/* ===== listings ===== */
static mk_status_t listing_check(const mk_market_t *m, const mk_listing_spec_t *l)
{
    if (l->kind >= MK_KIND_COUNT || l->category > 63 || !str_bounded(l->title, sizeof l->title) ||
        l->title[0] == 0 || !str_bounded(l->cid, MK_CID_MAX) || l->n_slots > MK_MAX_SLOTS)
        return MK_ERR_ARG;
    if (!mk__ccy(m, l->ccy)) return MK_ERR_CCY;
    if (l->kind == MK_KIND_DIGITAL && l->cid[0] == 0)
        return MK_ERR_ARG; /* digital goods are delivered by CID */
    if (l->kind == MK_KIND_BOOKING && l->n_slots == 0) return MK_ERR_ARG;
    if (l->kind != MK_KIND_BOOKING && l->n_slots) return MK_ERR_ARG;
    if (l->kind == MK_KIND_SUBSCRIPTION && l->period == 0) return MK_ERR_ARG;
    if (l->kind == MK_KIND_BOOKING && l->cancel_fee > l->price) return MK_ERR_POLICY;
    for (uint32_t i = 0; i < l->n_slots; i++)
        if (l->slot[i].capacity == 0 || l->slot[i].duration == 0) return MK_ERR_ARG;
    return MK_OK;
}

mk_status_t mk_listing_publish(mk_market_t *m, const mk_listing_spec_t *spec, const uint8_t *sig,
                               uint32_t sig_len, uint16_t *handle)
{
    if (!m || !spec || !handle) return MK_ERR_ARG;
    mk_store_t *st = mk__store(m, spec->store);
    if (!st) return MK_ERR_NOT_FOUND;
    if (!st->active) return MK_ERR_STATE;
    mk_status_t rc = listing_check(m, spec);
    if (rc != MK_OK) return rc;
    uint8_t d[32];
    mk_listing_digest(m, spec, d);
    if (!mk__verify(m, &st->spec.owner, d, 32, sig, sig_len)) return MK_ERR_AUTH;
    mk_listing_t *L;
    if (*handle != MK_NONE) {
        L = mk__listing(m, *handle);
        if (!L) return MK_ERR_NOT_FOUND;
        if (L->spec.store != spec->store) return MK_ERR_AUTH;
        if (spec->version <= L->spec.version) return MK_ERR_DUPLICATE;
        /* Booked slot counts carry over; a slot may not shrink below them. */
        for (uint32_t i = 0; i < L->spec.n_slots; i++) {
            if (L->spec.slot[i].booked &&
                (i >= spec->n_slots || spec->slot[i].capacity < L->spec.slot[i].booked ||
                 spec->slot[i].start != L->spec.slot[i].start))
                return MK_ERR_STATE;
        }
        uint32_t booked[MK_MAX_SLOTS];
        for (uint32_t i = 0; i < MK_MAX_SLOTS; i++)
            booked[i] = i < L->spec.n_slots ? L->spec.slot[i].booked : 0;
        if (spec->stock != MK_UNLIMITED && spec->stock < L->reserved) return MK_ERR_STOCK;
        pay_memcpy(&L->spec, spec, sizeof *spec);
        for (uint32_t i = 0; i < MK_MAX_SLOTS; i++)
            L->spec.slot[i].booked = i < spec->n_slots ? booked[i] : 0;
    } else {
        L = 0;
        for (uint32_t i = 0; i < MK_MAX_LISTINGS; i++)
            if (!m->listing[i].used) {
                L = &m->listing[i];
                pay_memset(L, 0, sizeof *L);
                *handle = (uint16_t) (i + 1);
                break;
            }
        if (!L) return MK_ERR_FULL;
        L->used = true;
        pay_memcpy(&L->spec, spec, sizeof *spec);
        for (uint32_t i = 0; i < MK_MAX_SLOTS; i++) L->spec.slot[i].booked = 0;
    }
    /* Digital goods, bookings (bounded by slot capacity) and subscriptions
     * are not stocked; physical goods and services keep the seller's count. */
    if (spec->kind == MK_KIND_DIGITAL || spec->kind == MK_KIND_BOOKING ||
        spec->kind == MK_KIND_SUBSCRIPTION)
        L->spec.stock = MK_UNLIMITED;
    pay_memcpy(L->digest, d, 32);
    L->active = true;
    return MK_OK;
}

mk_status_t mk_listing_withdraw(mk_market_t *m, const mk_id_t *actor, uint16_t listing)
{
    mk_listing_t *L = mk__listing(m, listing);
    if (!L) return MK_ERR_NOT_FOUND;
    const mk_store_t *st = mk__store_c(m, L->spec.store);
    if (!st || !mk__id_eq(actor, &st->spec.owner)) return MK_ERR_AUTH;
    L->active = false;
    return MK_OK;
}

mk_status_t mk_restock(mk_market_t *m, const mk_id_t *actor, uint16_t listing, uint32_t qty)
{
    mk_listing_t *L = mk__listing(m, listing);
    if (!L) return MK_ERR_NOT_FOUND;
    const mk_store_t *st = mk__store_c(m, L->spec.store);
    if (!st || !mk__id_eq(actor, &st->spec.owner)) return MK_ERR_AUTH;
    if (L->spec.stock == MK_UNLIMITED) return MK_OK;
    if (qty >= MK_UNLIMITED - L->spec.stock) return MK_ERR_OVERFLOW;
    L->spec.stock += qty;
    return MK_OK;
}

uint32_t mk_available(const mk_market_t *m, uint16_t listing)
{
    const mk_listing_t *L = mk__listing_c(m, listing);
    if (!L) return 0;
    if (L->spec.stock == MK_UNLIMITED) return MK_UNLIMITED;
    return L->spec.stock > L->reserved ? L->spec.stock - L->reserved : 0;
}

/* ===== carts ===== */
int32_t mk_cart_open(mk_market_t *m, const mk_id_t *owner)
{
    if (!m || !owner || mk__id_zero(owner)) return MK_ERR_ARG;
    for (uint32_t i = 0; i < MK_MAX_CARTS; i++)
        if (!m->cart[i].used) {
            pay_memset(&m->cart[i], 0, sizeof m->cart[i]);
            m->cart[i].used = true;
            mk__id_copy(&m->cart[i].owner, owner);
            return (int32_t) (i + 1);
        }
    return MK_ERR_FULL;
}

static mk_cart_t *cart_get(mk_market_t *m, uint16_t h)
{
    if (!m || h == MK_NONE || h > MK_MAX_CARTS || !m->cart[h - 1].used) return 0;
    return &m->cart[h - 1];
}

mk_status_t mk_cart_add(mk_market_t *m, uint16_t cart, uint16_t listing, uint32_t qty, uint8_t slot)
{
    mk_cart_t *c = cart_get(m, cart);
    const mk_listing_t *L = mk__listing_c(m, listing);
    if (!c) return MK_ERR_NOT_FOUND;
    if (!L || !L->active) return MK_ERR_NOT_FOUND;
    if (qty == 0) return MK_ERR_ARG;
    if (L->spec.kind == MK_KIND_BOOKING && slot >= L->spec.n_slots) return MK_ERR_ARG;
    if (L->spec.kind == MK_KIND_SUBSCRIPTION && qty != 1) return MK_ERR_ARG;
    for (uint32_t i = 0; i < c->n; i++)
        if (c->line[i].listing == listing && c->line[i].slot == slot) {
            if (L->spec.kind == MK_KIND_SUBSCRIPTION) return MK_ERR_DUPLICATE;
            if (qty > MK_UNLIMITED - 1 - c->line[i].qty) return MK_ERR_OVERFLOW;
            c->line[i].qty += qty;
            return MK_OK;
        }
    if (c->n >= MK_CART_LINES) return MK_ERR_FULL;
    c->line[c->n].listing = listing;
    c->line[c->n].qty = qty;
    c->line[c->n].slot = L->spec.kind == MK_KIND_BOOKING ? slot : 0;
    c->n++;
    return MK_OK;
}

mk_status_t mk_cart_remove(mk_market_t *m, uint16_t cart, uint16_t listing)
{
    mk_cart_t *c = cart_get(m, cart);
    if (!c) return MK_ERR_NOT_FOUND;
    for (uint32_t i = 0; i < c->n; i++)
        if (c->line[i].listing == listing) {
            for (uint32_t j = i + 1; j < c->n; j++)
                pay_memcpy(&c->line[j - 1], &c->line[j], sizeof c->line[j]);
            c->n--;
            return MK_OK;
        }
    return MK_ERR_NOT_FOUND;
}

void mk_cart_clear(mk_market_t *m, uint16_t cart)
{
    mk_cart_t *c = cart_get(m, cart);
    if (c) c->n = 0;
}

/* ===== order construction ===== */
void mk__order_digest(const mk_market_t *m, mk_order_t *o, uint16_t h)
{
    pay_hbuf hb;
    pay_hbuf_init(&hb);
    h_str(&hb, "ZXV-MKT-ORDER-1", 32);
    pay_hbuf_u64(&hb, h);
    pay_hbuf_u64(&hb, o->nonce);
    const mk_store_t *st = mk__store_c(m, o->store);
    pay_hbuf_put(&hb, st ? st->digest : o->seller.b, 32);
    pay_hbuf_put(&hb, o->buyer.b, 32);
    pay_hbuf_put(&hb, o->seller.b, 32);
    const mk_ccy_t *c = mk__ccy(m, o->ccy);
    h_str(&hb, c ? c->alpha : "", 4);
    pay_hbuf_u64(&hb, o->n_lines);
    for (uint32_t i = 0; i < o->n_lines; i++) {
        const mk_line_t *ln = &o->line[i];
        const mk_listing_t *L = mk__listing_c(m, ln->listing);
        pay_hbuf_put(&hb, L ? L->digest : o->buyer.b, 32);
        pay_hbuf_u64(&hb, ln->qty);
        pay_hbuf_u64(&hb, ln->slot);
        pay_hbuf_u64(&hb, ln->unit_price);
        pay_hbuf_u64(&hb, ln->shipping);
        pay_hbuf_u64(&hb, ln->line_total);
        pay_hbuf_u64(&hb, ln->tax);
    }
    pay_hbuf_u64(&hb, o->total);
    pay_hbuf_u64(&hb, o->tax);
    h_str(&hb, o->region, MK_REGION_MAX);
    pay_hbuf_u64(&hb, o->community_mediation);
    pay_hbuf_put(&hb, o->agent.b, 32);
    (void) pay_hbuf_final(&hb, o->digest);
}

int32_t mk__order_build(mk_market_t *m, const mk__build_t *b, const mk_cart_line_t *lines,
                        uint32_t n)
{
    if (n == 0 || n > MK_ORDER_LINES) return MK_ERR_FULL;
    const mk_listing_t *L0 = mk__listing_c(m, lines[0].listing);
    if (!L0) return MK_ERR_NOT_FOUND;
    mk_store_t *st = mk__store(m, L0->spec.store);
    if (!st || !st->active) return MK_ERR_STATE;
    if (mk__id_eq(b->buyer, &st->spec.owner)) return MK_ERR_POLICY; /* no self-dealing */
    int32_t slot_i = -1;
    for (uint32_t i = 0; i < MK_MAX_ORDERS; i++)
        if (!m->order[i].used) {
            slot_i = (int32_t) i;
            break;
        }
    if (slot_i < 0) return MK_ERR_FULL;
    mk_order_t *o = &m->order[slot_i];
    pay_memset(o, 0, sizeof *o);
    o->store = L0->spec.store;
    o->ccy = L0->spec.ccy;
    mk__id_copy(&o->buyer, b->buyer);
    mk__id_copy(&o->seller, &st->spec.owner);
    pay_strlcpy(o->region, b->region ? b->region : "", MK_REGION_MAX);
    o->community_mediation = b->community || !st->spec.has_arbiter;
    if (!o->community_mediation) mk__id_copy(&o->dispute.arbiter, &st->spec.arbiter);
    for (uint32_t i = 0; i < n; i++) {
        const mk_listing_t *L = mk__listing_c(m, lines[i].listing);
        if (!L || !L->active) return MK_ERR_NOT_FOUND;
        if (L->spec.store != o->store || L->spec.ccy != o->ccy) return MK_ERR_CCY;
        uint32_t avail = mk_available(m, lines[i].listing);
        if (avail != MK_UNLIMITED && avail < lines[i].qty) return MK_ERR_STOCK;
        if (L->spec.kind == MK_KIND_BOOKING) {
            if (lines[i].slot >= L->spec.n_slots) return MK_ERR_ARG;
            const mk_slot_t *s = &L->spec.slot[lines[i].slot];
            if (s->start <= m->now) return MK_ERR_WINDOW;
            if (s->capacity - s->booked < lines[i].qty) return MK_ERR_STOCK;
        }
        mk_line_t *ln = &o->line[i];
        ln->listing = lines[i].listing;
        ln->qty = lines[i].qty;
        ln->slot = lines[i].slot;
        ln->unit_price = L->spec.price;
        ln->shipping = L->spec.shipping;
        pay_u128 g = pay_mul64(L->spec.price, lines[i].qty);
        if (g.hi) return MK_ERR_OVERFLOW;
        mk_status_t rc = mk__line_tax(m, o->region, L->spec.tax_class, g.lo, L->spec.shipping,
                                      &ln->line_total, &ln->tax, &o->tax_unconfigured);
        if (rc != MK_OK) return rc;
        if (!pay_add_ok(o->total, ln->line_total, &o->total) ||
            !pay_add_ok(o->tax, ln->tax, &o->tax))
            return MK_ERR_OVERFLOW;
    }
    o->n_lines = (uint8_t) n;
    if (m->hooks.party_ok &&
        !m->hooks.party_ok(m->hooks.ctx, &o->buyer, &o->seller, o->ccy, o->total))
        return MK_ERR_POLICY;
    /* All checks passed: reserve inventory and slots. */
    for (uint32_t i = 0; i < n; i++) {
        mk_listing_t *L = mk__listing(m, lines[i].listing);
        if (L->spec.stock != MK_UNLIMITED) L->reserved += lines[i].qty;
        if (L->spec.kind == MK_KIND_BOOKING) L->spec.slot[lines[i].slot].booked += lines[i].qty;
    }
    o->used = true;
    o->state = MK_ORD_PENDING_PAYMENT;
    o->created_at = m->now;
    o->nonce = ++m->nonce;
    o->pay_by = m->now + m->policy.payment_window;
    if (b->agent) {
        o->needs_confirm = true;
        mk__id_copy(&o->agent, b->agent);
        o->mandate = (uint8_t) b->mandate;
        o->confirm_by = m->now + m->policy.confirm_window;
        o->pay_by = o->confirm_by + m->policy.payment_window;
    }
    uint16_t h = (uint16_t) (slot_i + 1);
    mk__order_digest(m, o, h);
    return h;
}

/* Agent mandate guard (re-checked at confirmation). `pending` is the total of
 * other orders built in the same checkout. */
mk_status_t mk__mandate_ok(const mk_market_t *m, const mk_order_t *o, uint64_t pending)
{
    uint16_t mh = o->mandate;
    if (mh == MK_NONE || mh > MK_MAX_MANDATES || !m->mandate[mh - 1].used) return MK_ERR_MANDATE;
    const mk_mandate_t *d = &m->mandate[mh - 1];
    if (d->revoked || m->now >= d->expires || !mk__id_eq(&d->user, &o->buyer) ||
        !mk__id_eq(&d->agent, &o->agent) || d->ccy != o->ccy || o->total > d->max_per_order)
        return MK_ERR_MANDATE;
    uint64_t t;
    if (!pay_add_ok(d->committed, o->total, &t) || !pay_add_ok(t, pending, &t) || t > d->max_total)
        return MK_ERR_MANDATE;
    for (uint32_t i = 0; i < o->n_lines; i++) {
        const mk_listing_t *L = mk__listing_c(m, o->line[i].listing);
        if (!L || !((d->categories >> L->spec.category) & 1u)) return MK_ERR_MANDATE;
    }
    return MK_OK;
}

int32_t mk_checkout(mk_market_t *m, uint16_t cart, const mk_checkout_opts_t *opts, uint16_t *out,
                    uint32_t max)
{
    mk_cart_t *c = cart_get(m, cart);
    if (!c || !opts || !out || !str_bounded(opts->region, MK_REGION_MAX)) return MK_ERR_ARG;
    if (c->n == 0) return MK_ERR_ARG;
    /* Group lines by (store, currency). */
    mk_cart_line_t grp[MK_CART_LINES][MK_ORDER_LINES];
    uint32_t gn[MK_CART_LINES];
    uint16_t gstore[MK_CART_LINES];
    uint8_t gccy[MK_CART_LINES];
    uint32_t ng = 0;
    for (uint32_t i = 0; i < c->n; i++) {
        const mk_listing_t *L = mk__listing_c(m, c->line[i].listing);
        if (!L || !L->active) return MK_ERR_NOT_FOUND;
        uint32_t g = 0;
        while (g < ng && !(gstore[g] == L->spec.store && gccy[g] == L->spec.ccy)) g++;
        if (g == ng) {
            gstore[g] = L->spec.store;
            gccy[g] = L->spec.ccy;
            gn[g] = 0;
            ng++;
        }
        if (gn[g] >= MK_ORDER_LINES) return MK_ERR_FULL;
        pay_memcpy(&grp[g][gn[g]], &c->line[i], sizeof c->line[i]);
        gn[g]++;
    }
    if (ng > max) return MK_ERR_FULL;
    /* Dry-run every group's checks before creating anything: stock across
     * groups cannot collide (distinct listings), so per-group checks hold. */
    uint32_t free_orders = 0;
    for (uint32_t i = 0; i < MK_MAX_ORDERS; i++) free_orders += !m->order[i].used;
    if (free_orders < ng) return MK_ERR_FULL;
    int32_t made = 0;
    mk__build_t b = {&c->owner, opts->region, opts->community_mediation, opts->agent,
                     opts->mandate};
    for (uint32_t g = 0; g < ng; g++) {
        int32_t h = mk__order_build(m, &b, grp[g], gn[g]);
        mk_status_t rc = MK_OK;
        if (h < 0)
            rc = (mk_status_t) h;
        else if (opts->agent) {
            const mk_order_t *o = mk_order(m, (uint16_t) h);
            uint64_t pending = 0;
            for (int32_t k = 0; k < made; k++) pending += mk_order(m, out[k])->total;
            rc = mk__mandate_ok(m, o, pending);
            if (rc != MK_OK) out[made++] = (uint16_t) h; /* roll it back below */
        }
        if (rc != MK_OK) {
            for (int32_t k = 0; k < made; k++) {
                mk_order_t *o = mk__order(m, out[k]);
                for (uint32_t i = 0; i < o->n_lines; i++) {
                    mk_listing_t *L = mk__listing(m, o->line[i].listing);
                    if (L->spec.stock != MK_UNLIMITED) L->reserved -= o->line[i].qty;
                    if (L->spec.kind == MK_KIND_BOOKING)
                        L->spec.slot[o->line[i].slot].booked -= o->line[i].qty;
                }
                pay_memset(o, 0, sizeof *o);
            }
            return rc;
        }
        out[made++] = (uint16_t) h;
    }
    c->n = 0;
    for (int32_t k = 0; k < made; k++)
        if (opts->agent) mk__notify(m, MK_NOTE_CONFIRM_NEEDED, out[k]); /* the user must confirm */
    return made;
}

/* ===== clock ===== */
void mk_tick(mk_market_t *m, uint64_t now)
{
    if (!m || now < m->now) return; /* the clock never runs backwards */
    m->now = now;
    mk__order_tick(m);
    mk__sub_tick(m);
    mk__inv_tick(m);
}
