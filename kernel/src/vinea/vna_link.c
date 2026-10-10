/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* vna_link.c — the trade loop over receipts. Design: vna_link.h. */
#include "vna_link.h"

void vna_link_init(vna_link_t *l, vna_book_t *book, const vna_agreement_t *agr, vna_usage_t *us,
                   const uint8_t seed[32], uint64_t retry_ms, uint32_t max_tries)
{
    if (!l) return;
    vna_zero(l, sizeof *l);
    l->book = book;
    l->agr = agr;
    l->us = us;
    vna_drbg_seed(&l->rng, seed, 32);
    l->retry_ms = retry_ms ? retry_ms : 1000u;
    l->max_tries = max_tries ? max_tries : 8u;
}

int32_t vna_link_want(vna_link_t *l, uint8_t resource, uint8_t form, uint64_t units,
                      uint64_t max_price)
{
    if (!l || resource >= VNA_RES_COUNT || form >= VNA_FORMS || units == 0) return -1;
    for (uint32_t i = 0; i < VNA_LINK_WANTS; i++) {
        vna_link_want_t *w = &l->want[i];
        if (w->used) continue;
        w->used = true;
        w->resource = resource;
        w->form = form;
        w->units_left = units;
        w->max_price = max_price;
        return (int32_t) i;
    }
    return -1;
}

static bool queue_out(vna_link_t *l, const vna_id_t *dst, const uint8_t *b, uint32_t len)
{
    if (l->out_n >= VNA_LINK_OUT || len > VNA_RCPT_MAX) {
        l->out_dropped++;
        return false;
    }
    uint32_t i = l->out_head + l->out_n;
    if (i >= VNA_LINK_OUT) i -= VNA_LINK_OUT;
    l->out[i].dst = *dst;
    l->out[i].len = len;
    vna_copy(l->out[i].bytes, b, len);
    l->out_n++;
    return true;
}

bool vna_link_next_out(vna_link_t *l, vna_id_t *dst, const uint8_t **bytes, uint32_t *len)
{
    if (!l || !dst || !bytes || !len || l->out_n == 0) return false;
    const vna_link_msg_t *m = &l->out[l->out_head];
    *dst = m->dst;
    *bytes = m->bytes;
    *len = m->len;
    l->out_head = l->out_head + 1u == VNA_LINK_OUT ? 0 : l->out_head + 1u;
    l->out_n--;
    return true;
}

static vna_link_pend_t *pend_of(vna_link_t *l, const vna_id_t *peer)
{
    for (uint32_t i = 0; i < VNA_LINK_PEND; i++)
        if (l->pend[i].used && vna_id_eq(&l->pend[i].peer, peer)) return &l->pend[i];
    return 0;
}

uint32_t vna_link_pending(const vna_link_t *l, const vna_id_t *peer)
{
    uint32_t n = 0;
    if (!l) return 0;
    for (uint32_t i = 0; i < VNA_LINK_PEND; i++)
        if (l->pend[i].used && (!peer || vna_id_eq(&l->pend[i].peer, peer))) n++;
    return n;
}

static vna_link_pend_t *pend_new(vna_link_t *l, uint8_t role, const vna_id_t *peer,
                                 uint64_t pair_seq, uint64_t units, int32_t want, uint64_t now)
{
    for (uint32_t i = 0; i < VNA_LINK_PEND; i++) {
        vna_link_pend_t *p = &l->pend[i];
        if (p->used) continue;
        p->used = true;
        p->role = role;
        p->peer = *peer;
        p->pair_seq = pair_seq;
        p->units = units;
        p->want = want;
        p->tries = 1;
        p->next_ms = now + l->retry_ms;
        return p;
    }
    return 0;
}

/* The want a BUY reserved from gets its units back (the trade never happened). */
static void pend_drop(vna_link_t *l, vna_link_pend_t *p, bool restore)
{
    if (restore && p->role == VNA_LINK_BUY && p->want >= 0 && l->want[p->want].used)
        l->want[p->want].units_left += p->units;
    p->used = false;
}

vna_status_t vna_link_sell(vna_link_t *l, const vna_id_t *buyer, uint8_t mode, uint8_t form,
                           uint8_t resource, uint64_t units, uint64_t price, const char *hk,
                           uint64_t now)
{
    if (!l || !buyer || !hk || units == 0) return VNA_ERR_ARG;
    if (pend_of(l, buyer)) return VNA_ERR_STATE; /* L1 */
    vna_status_t st =
        vna_book_check_sale(l->book, l->agr, l->us, buyer, mode, form, resource, units, price, now);
    if (st != VNA_OK) return st;
    vna_receipt_t *r = &l->scratch;
    uint8_t cid[32];
    bool has_cid = l->agr && vna_agree_cid(l->agr, cid) == VNA_OK;
    st = vna_receipt_prepare(l->book, r, mode, form, resource, &l->book->idn->id, buyer, units,
                             price, 1000, now, has_cid ? cid : 0, hk);
    if (st != VNA_OK) return st;
    vna_link_pend_t *p = pend_new(l, VNA_LINK_SELL, buyer, r->pair_seq, units, -1, now);
    if (!p) return VNA_ERR_SPACE;
    uint8_t rnd[32];
    vna_drbg_gen(&l->rng, rnd, 32);
    int32_t n = vna_receipt_sign_seller(r, l->book->idn, rnd, p->bytes, sizeof p->bytes);
    if (n < 0) {
        p->used = false;
        return VNA_ERR_SPACE;
    }
    p->len = (uint32_t) n;
    queue_out(l, buyer, p->bytes, p->len);
    l->offered++;
    return VNA_OK;
}

static bool zero(const uint8_t *b, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
        if (b[i]) return false;
    return true;
}

/* T2: a seller-signed offer to this node */
static vna_status_t on_offer(vna_link_t *l, const vna_receipt_t *r, const vna_id_t *peer,
                             const uint8_t *bytes, uint32_t len, uint64_t now)
{
    if (r->mode != VNA_TR_PAY && r->mode != VNA_TR_CREDIT) return VNA_ERR_PARSE;
    vna_link_pend_t *p = pend_of(l, peer);
    if (p) { /* L2: the seller resent an offer we already countersigned */
        if (p->role != VNA_LINK_BUY || p->pair_seq != r->pair_seq) return VNA_ERR_STATE;
        queue_out(l, peer, p->bytes, p->len);
        l->resent++;
        return VNA_OK;
    }
    vna_account_t *a = vna_book_find(l->book, peer);
    if (!a) return VNA_ERR_ARG;
    if (r->pair_seq != a->pair_seq + 1u || !vna_eq(r->prev, a->pair_prev, 32)) return VNA_ERR_FORK;
    int32_t w = -1;
    for (uint32_t i = 0; i < VNA_LINK_WANTS && w < 0; i++) {
        const vna_link_want_t *x = &l->want[i];
        if (x->used && x->resource == r->resource && x->form == r->form &&
            x->units_left >= r->units && x->max_price >= r->price)
            w = (int32_t) i;
    }
    if (w < 0) return VNA_ERR_DENIED; /* not asked for, or over the ceiling */
    if (r->mode == VNA_TR_PAY && a->held[r->form] < r->price) return VNA_ERR_FUNDS;
    vna_link_pend_t *q = pend_new(l, VNA_LINK_BUY, peer, r->pair_seq, r->units, w, now);
    if (!q) return VNA_ERR_SPACE;
    uint8_t rnd[32];
    vna_drbg_gen(&l->rng, rnd, 32);
    int32_t n = vna_receipt_countersign(bytes, len, a->pk, l->book->idn, rnd, &l->scratch, q->bytes,
                                        sizeof q->bytes);
    if (n < 0) {
        q->used = false;
        return (vna_status_t) n;
    }
    q->len = (uint32_t) n;
    l->want[w].units_left -= r->units; /* reserved until committed or abandoned */
    queue_out(l, peer, q->bytes, q->len);
    l->accepted++;
    return VNA_OK;
}

/* T3 (seller) and T4 (buyer): a fully signed receipt */
static vna_status_t on_signed(vna_link_t *l, const vna_receipt_t *r, const vna_id_t *peer,
                              bool i_sell, const uint8_t *bytes, uint32_t len, uint64_t now)
{
    uint8_t rnd[32];
    vna_drbg_gen(&l->rng, rnd, 32);
    uint64_t seq = r->pair_seq; /* the book reuses its own scratch; keep what we need */
    vna_status_t st =
        vna_book_apply(l->book, bytes, len, i_sell ? l->agr : 0, i_sell ? l->us : 0, now, rnd);
    vna_link_pend_t *p = pend_of(l, peer);
    if (st == VNA_OK) {
        if (p && p->pair_seq == seq) {
            if (p->role == VNA_LINK_BUY && p->want >= 0 && l->want[p->want].units_left == 0)
                l->want[p->want].used = false;
            pend_drop(l, p, false);
        }
        if (i_sell) {
            queue_out(l, peer, bytes, len); /* T3: the confirmation */
            l->committed++;
        } else {
            l->confirmed++;
        }
        return VNA_OK;
    }
    if (st == VNA_ERR_REPLAY) { /* L2: already applied? */
        vna_account_t *a = vna_book_find(l->book, peer);
        uint8_t h[32];
        vna_sha3(bytes, len, h);
        if (a && vna_eq(h, a->pair_prev, 32)) {
            l->book->rejected--; /* a duplicate is not a refusal */
            if (i_sell) {
                queue_out(l, peer, bytes, len);
                l->resent++;
            }
            return VNA_OK;
        }
    }
    if (i_sell && p && p->role == VNA_LINK_SELL && p->pair_seq == seq) {
        pend_drop(l, p, false); /* the commit failed: the buyer never applies it */
        l->abandoned++;
    }
    return st;
}

vna_status_t vna_link_on_rcpt(void *ctx, const vna_id_t *from, const uint8_t *rcpt, uint32_t len,
                              uint64_t now)
{
    vna_link_t *l = (vna_link_t *) ctx;
    if (!l || !from || !rcpt || len == 0 || len > VNA_RCPT_MAX) return VNA_ERR_ARG;
    vna_receipt_t *r = &l->scratch;
    uint32_t sig_off = 0;
    vna_status_t st;
    if (vna_schema_unpack(&vna_receipt_schema, rcpt, len, r, &sig_off) < 0) {
        st = VNA_ERR_PARSE;
    } else {
        const vna_id_t *me = &l->book->idn->id;
        bool i_sell = vna_id_eq(&r->seller, me), i_buy = vna_id_eq(&r->buyer, me);
        const vna_id_t *peer = i_sell ? &r->buyer : &r->seller;
        if (i_sell == i_buy)
            st = VNA_ERR_DST;
        else if (!vna_id_eq(from, peer))
            st = VNA_ERR_UNEXPECTED; /* only the counterparty may deliver it */
        else if (r->mode == VNA_TR_DISTRIBUTE) {
            uint8_t rnd[32];
            vna_drbg_gen(&l->rng, rnd, 32);
            st = i_buy ? vna_book_apply(l->book, rcpt, len, 0, 0, now, rnd) : VNA_ERR_STATE;
        } else if (zero(r->sig_buyer, VNA_SIG_LEN))
            st = i_buy ? on_offer(l, r, peer, rcpt, len, now) : VNA_ERR_STATE;
        else {
            vna_id_t p = *peer; /* r is scratch the book may overwrite */
            st = on_signed(l, r, &p, i_sell, rcpt, len, now);
        }
    }
    if (st != VNA_OK) l->refused++;
    return st;
}

void vna_link_tick(vna_link_t *l, uint64_t now)
{
    if (!l) return;
    for (uint32_t i = 0; i < VNA_LINK_PEND; i++) {
        vna_link_pend_t *p = &l->pend[i];
        if (!p->used || p->next_ms > now) continue;
        if (p->tries >= l->max_tries) {
            pend_drop(l, p, true);
            l->abandoned++;
            continue;
        }
        if (queue_out(l, &p->peer, p->bytes, p->len)) l->resent++;
        p->tries++;
        p->next_ms = now + l->retry_ms;
    }
}

vna_status_t vna_link_cycle(vna_link_t *l, vna_gate_t *g, const uint8_t owner_secret[32],
                            swarm_budget_t *sb, uint64_t base_rate, uint64_t now,
                            uint64_t *imported_out)
{
    if (imported_out) *imported_out = 0;
    if (!l || !g || !owner_secret || !sb) return VNA_ERR_ARG;
    /* L4: this cycle's verified remote compute -> next cycle's internal rate */
    uint64_t recv = l->book->received_units[VNA_RES_COMPUTE];
    uint64_t room = g->import_cap - g->imported, amount = 0;
    if (recv > g->imported) amount = recv - g->imported;
    if (amount > room) amount = room;
    vna_status_t st = VNA_OK;
    if (amount > 0)
        st = vna_gate_import(g, owner_secret, l->book, SWARM_CAP_SYSTEM, amount, sb, base_rate);
    else if (swarm_budget_set_rate(sb, base_rate) != SWARM_OK)
        st = VNA_ERR_CAP;
    if (st != VNA_OK) return st;
    l->imported += amount;
    if (imported_out) *imported_out = amount;

    vna_rcpt_out_t ro = {l->dist, l->dist_len, 16, 0};
    st = vna_book_settle(l->book, now, &l->rng, &ro);
    if (st != VNA_OK) return st;
    for (uint32_t i = 0; i < ro.n; i++) {
        uint32_t so = 0;
        if (vna_schema_unpack(&vna_receipt_schema, l->dist[i], l->dist_len[i], &l->scratch, &so) <
            0)
            return VNA_ERR_PARSE;
        vna_id_t to = l->scratch.buyer;
        queue_out(l, &to, l->dist[i], l->dist_len[i]);
    }
    return vna_gate_begin_cycle(g, owner_secret);
}
