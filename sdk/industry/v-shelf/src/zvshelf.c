/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zvshelf.c — v-shelf: exact per-location stock, signed movements, cycle
 * counts with variances held for an approver, and a full journal replay.
 * The live store and the replay share one book and one set of rules, so a
 * replay that disagrees means the records and the state have diverged.
 * Integer code only (R11). */
#include <stdlib.h>
#include <string.h>

#include "zvshelf.h"

#define DOMAIN "v-shelf"
#define MAX_APPROVERS 16u
#define NO_LOC 0xffu

typedef struct {
    zind_qty qty;
    uint8_t held;
    uint8_t tag;
    zind_qty variance;
    uint8_t var_hash[32];
} pos_t;

typedef struct {
    char sku[ZVS_ID_MAX + 1];
    int32_t unit;
    char unit_name[9];
    pos_t pos[ZVS_MAX_LOCATIONS];
    zind_qty rcv, sold, shr, adj;
} item_t;

typedef struct {
    char locs[ZVS_MAX_LOCATIONS][ZVS_LOC_MAX + 1];
    uint32_t n_locs;
    item_t *items;
    uint32_t n_items, alloc;
} book_t;

struct zvs_store {
    const zxv_cap *cap;
    const zind_signer *signer;
    zind_qsink *qs;
    uint8_t approvers[MAX_APPROVERS][32];
    uint32_t n_approvers;
    zind_seen *refs;
    zind_chain *chain;
    book_t book;
};

const char *zvs_status_name(int32_t status) {
    switch (status) {
    case ZVS_E_UNIT: return "ZVS_E_UNIT";
    case ZVS_E_SKU: return "ZVS_E_SKU";
    case ZVS_E_LOCATION: return "ZVS_E_LOCATION";
    case ZVS_E_VARIANCE: return "ZVS_E_VARIANCE";
    case ZVS_E_APPROVER: return "ZVS_E_APPROVER";
    default: return zind_status_name(status);
    }
}

/* ===== the book: shared by the live store and the replay ===== */

static uint8_t tag_of(int32_t lp) {
    uint8_t t = 0;
    (void) zind_tag(lp, ZXV_HEX_POL_NEUTRAL, ZXV_HEX_PHASE_EMERGING, &t);
    return t;
}

static int find_item(const book_t *b, const char *sku) {
    if (!sku) return -1;
    for (uint32_t i = 0; i < b->n_items; i++)
        if (strcmp(b->items[i].sku, sku) == 0) return (int) i;
    return -1;
}

static int32_t book_add_loc(book_t *b, const char *code) {
    if (!zind_ident_ok(code, ZVS_LOC_MAX)) return ZXV_E_ARG;
    for (uint32_t i = 0; i < b->n_locs; i++)
        if (strcmp(b->locs[i], code) == 0) return ZXV_E_DUPLICATE;
    if (b->n_locs >= ZVS_MAX_LOCATIONS) return ZXV_E_CAPACITY;
    memcpy(b->locs[b->n_locs++], code, strlen(code) + 1);
    return ZXV_OK;
}

static int32_t book_add_item(book_t *b, const char *sku, int32_t unit, const char *name) {
    item_t it;
    if (!zind_ident_ok(sku, ZVS_ID_MAX) || !zind_ident_ok(name, 8) ||
        (unit != ZVS_UNIT_EACH && unit != ZVS_UNIT_MEASURE))
        return ZXV_E_ARG;
    if (find_item(b, sku) >= 0) return ZXV_E_DUPLICATE;
    if (b->n_items >= ZVS_MAX_ITEMS) return ZXV_E_CAPACITY;
    if (b->n_items == b->alloc) {
        uint32_t a = b->alloc ? b->alloc * 2 : 16;
        item_t *ni = (item_t *) realloc(b->items, (size_t) a * sizeof *ni);
        if (!ni) return ZXV_E_NOMEM;
        b->items = ni;
        b->alloc = a;
    }
    memset(&it, 0, sizeof it);
    memcpy(it.sku, sku, strlen(sku) + 1);
    memcpy(it.unit_name, name, strlen(name) + 1);
    it.unit = unit;
    it.rcv = it.sold = it.shr = it.adj = ZIND_QTY_ZERO;
    for (uint32_t l = 0; l < ZVS_MAX_LOCATIONS; l++) {
        it.pos[l].qty = ZIND_QTY_ZERO;
        it.pos[l].variance = ZIND_QTY_ZERO;
        it.pos[l].tag = tag_of(ZXV_LP_NEITHER);
    }
    b->items[b->n_items++] = it;
    return ZXV_OK;
}

static void book_free(book_t *b) {
    free(b->items);
    memset(b, 0, sizeof *b);
}

/* Apply one movement to the book, all or nothing. kind: ZVS_REC_RECEIVE,
 * SELL, TRANSFER or SHRINK. */
static int32_t book_move(book_t *b, int32_t kind, int ii, uint32_t loc, uint32_t loc2,
                         zind_qty q) {
    item_t *it;
    zind_qty a, c, flow;
    int32_t st;
    if (ii < 0 || (uint32_t) ii >= b->n_items) return ZVS_E_SKU;
    it = &b->items[ii];
    if (loc >= b->n_locs || (kind == ZVS_REC_TRANSFER && (loc2 >= b->n_locs || loc2 == loc)))
        return ZVS_E_LOCATION;
    if (!zind_qty_valid(q) || zind_qty_sign(q) <= 0) return ZXV_E_ARG;
    if (it->unit == ZVS_UNIT_EACH && q.den != 1) return ZVS_E_UNIT;
    if (it->pos[loc].held || (kind == ZVS_REC_TRANSFER && it->pos[loc2].held)) return ZIND_E_HELD;
    if (kind == ZVS_REC_RECEIVE) {
        st = zind_qty_add(it->pos[loc].qty, q, &a);
        if (st == ZXV_OK) st = zind_qty_add(it->rcv, q, &flow);
        if (st != ZXV_OK) return st;
        it->pos[loc].qty = a;
        it->rcv = flow;
        it->pos[loc].tag = tag_of(ZXV_LP_NEITHER);
        return ZXV_OK;
    }
    st = zind_qty_sub(it->pos[loc].qty, q, &a);
    if (st != ZXV_OK) return st;
    if (zind_qty_sign(a) < 0) return ZIND_E_NEGATIVE;
    if (kind == ZVS_REC_TRANSFER) {
        st = zind_qty_add(it->pos[loc2].qty, q, &c);
        if (st != ZXV_OK) return st;
        it->pos[loc].qty = a;
        it->pos[loc2].qty = c;
        it->pos[loc].tag = tag_of(ZXV_LP_NEITHER);
        it->pos[loc2].tag = tag_of(ZXV_LP_NEITHER);
        return ZXV_OK;
    }
    st = zind_qty_add(kind == ZVS_REC_SELL ? it->sold : it->shr, q, &flow);
    if (st != ZXV_OK) return st;
    it->pos[loc].qty = a;
    if (kind == ZVS_REC_SELL) it->sold = flow;
    else it->shr = flow;
    it->pos[loc].tag = tag_of(ZXV_LP_NEITHER);
    return ZXV_OK;
}

/* ===== store ===== */

int32_t zvs_store_create(const zxv_cap *cap, const zind_signer *signer, zxv_hexlog *qlog,
                         zvs_store **out) {
    zvs_store *s;
    int32_t st;
    if (!cap || !signer || !out) return ZXV_E_ARG;
    *out = NULL;
    st = zxv_cap_check(cap, ZXV_CAP_CRYPTO_SIGN);
    if (st != ZXV_OK) {
        zind_set_error("zvs_store_create: the cap needs CRYPTO_SIGN");
        return st;
    }
    s = (zvs_store *) calloc(1, sizeof *s);
    if (!s) return ZXV_E_NOMEM;
    s->cap = cap;
    s->signer = signer;
    st = zind_chain_create(DOMAIN, ZIND_CHAIN_MAX, &s->chain);
    if (st == ZXV_OK) st = zind_seen_create(ZVS_MAX_REFS, &s->refs);
    if (st == ZXV_OK && qlog) st = zind_qsink_create(cap, qlog, NULL, &s->qs);
    if (st != ZXV_OK) {
        zvs_store_destroy(s);
        return st;
    }
    *out = s;
    return ZXV_OK;
}

void zvs_store_destroy(zvs_store *s) {
    if (!s) return;
    zind_chain_destroy(s->chain);
    zind_seen_destroy(s->refs);
    zind_qsink_destroy(s->qs);
    book_free(&s->book);
    free(s);
}

const zind_chain *zvs_records(const zvs_store *s) { return s ? s->chain : NULL; }

static int32_t append(zvs_store *s, uint16_t kind, const zind_wr *w, uint8_t hash[32]) {
    if (w->overflow) return ZXV_E_INTERNAL;
    return zind_chain_append(s->cap, s->chain, s->signer, kind, w->buf, w->len, NULL, hash);
}

int32_t zvs_location_add(zvs_store *s, const char *code, uint32_t *out_loc) {
    uint8_t buf[32];
    zind_wr w;
    int32_t st;
    book_t probe;
    if (!s) return ZXV_E_ARG;
    /* validate on a copy of the location table first */
    probe = s->book;
    st = book_add_loc(&probe, code);
    if (st != ZXV_OK) {
        zind_set_error("zvs_location_add: a location code is 1..16 of [A-Za-z0-9._:/-], unique, "
                       "at most %u",
                       ZVS_MAX_LOCATIONS);
        return st;
    }
    zind_wr_init(&w, buf, sizeof buf);
    zind_wr_str(&w, code);
    st = append(s, ZVS_REC_LOCATION, &w, NULL);
    if (st != ZXV_OK) return st;
    (void) book_add_loc(&s->book, code);
    if (out_loc) *out_loc = s->book.n_locs - 1;
    return ZXV_OK;
}

int32_t zvs_item_add(zvs_store *s, const char *sku, int32_t unit_kind, const char *unit_name) {
    uint8_t buf[64];
    zind_wr w;
    int32_t st;
    if (!s) return ZXV_E_ARG;
    if (!zind_ident_ok(sku, ZVS_ID_MAX) || !zind_ident_ok(unit_name, 8) ||
        (unit_kind != ZVS_UNIT_EACH && unit_kind != ZVS_UNIT_MEASURE)) {
        zind_set_error("zvs_item_add: SKU 1..32 and unit 1..8 of [A-Za-z0-9._:/-], kind EACH or "
                       "MEASURE");
        return ZXV_E_ARG;
    }
    if (find_item(&s->book, sku) >= 0) return ZXV_E_DUPLICATE;
    if (s->book.n_items >= ZVS_MAX_ITEMS) return ZXV_E_CAPACITY;
    zind_wr_init(&w, buf, sizeof buf);
    zind_wr_str(&w, sku);
    zind_wr_u8(&w, (uint8_t) unit_kind);
    zind_wr_str(&w, unit_name);
    st = append(s, ZVS_REC_ITEM, &w, NULL);
    if (st != ZXV_OK) return st;
    return book_add_item(&s->book, sku, unit_kind, unit_name);
}

int32_t zvs_approver_add(zvs_store *s, const uint8_t pk[ZIND_PK_BYTES]) {
    uint8_t id[32];
    int32_t st;
    if (!s || !pk) return ZXV_E_ARG;
    st = zind_key_id(pk, id);
    if (st != ZXV_OK) return st;
    for (uint32_t i = 0; i < s->n_approvers; i++)
        if (memcmp(s->approvers[i], id, 32) == 0) return ZXV_E_DUPLICATE;
    if (s->n_approvers >= MAX_APPROVERS) return ZXV_E_CAPACITY;
    memcpy(s->approvers[s->n_approvers++], id, 32);
    return ZXV_OK;
}

static int32_t ref_check(zvs_store *s, const char *ref, uint8_t h[32]) {
    int32_t present = 0, st;
    if (!zind_ident_ok(ref, ZVS_ID_MAX)) {
        zind_set_error("zvs: a reference is 1..32 of [A-Za-z0-9._:/-]");
        return ZXV_E_ARG;
    }
    st = zind_subject("v-shelf/ref", (const uint8_t *) ref, strlen(ref), h);
    if (st != ZXV_OK) return st;
    st = zind_seen_has(s->refs, h, &present);
    if (st != ZXV_OK) return st;
    if (present) {
        zind_set_error("zvs: reference %s was already used", ref);
        return ZIND_E_REPLAY;
    }
    return ZXV_OK;
}

static int32_t movement(zvs_store *s, int32_t kind, const char *ref, const char *sku, uint32_t loc,
                        uint32_t loc2, zind_qty q, int32_t reason) {
    uint8_t rh[32], buf[160];
    zind_wr w;
    book_t probe;
    item_t saved;
    int ii;
    int32_t st;
    if (!s) return ZXV_E_ARG;
    st = ref_check(s, ref, rh);
    if (st != ZXV_OK) return st;
    ii = find_item(&s->book, sku);
    if (ii < 0) {
        zind_set_error("zvs: unknown SKU");
        return ZVS_E_SKU;
    }
    /* dry run on a copy of this item; the book changes only after the record */
    saved = s->book.items[ii];
    probe = s->book;
    st = book_move(&probe, kind, ii, loc, loc2, q);
    s->book.items[ii] = saved;
    if (st != ZXV_OK) {
        if (st == ZIND_E_NEGATIVE)
            zind_set_error("zvs: %s at location %u would go below zero", sku, loc);
        else if (st == ZIND_E_HELD)
            zind_set_error("zvs: %s at that location is held for a variance decision", sku);
        else if (st == ZVS_E_UNIT)
            zind_set_error("zvs: %s is counted in whole units", sku);
        return st;
    }
    zind_wr_init(&w, buf, sizeof buf);
    zind_wr_str(&w, ref);
    zind_wr_str(&w, sku);
    zind_wr_u8(&w, (uint8_t) loc);
    zind_wr_u8(&w, kind == ZVS_REC_TRANSFER ? (uint8_t) loc2 : (uint8_t) NO_LOC);
    zind_wr_qty(&w, q);
    zind_wr_u8(&w, (uint8_t) reason);
    st = append(s, (uint16_t) kind, &w, NULL);
    if (st != ZXV_OK) return st;
    st = book_move(&s->book, kind, ii, loc, loc2, q);
    if (st != ZXV_OK) return ZXV_E_INTERNAL; /* the dry run passed */
    (void) zind_seen_add(s->refs, rh);
    return ZXV_OK;
}

int32_t zvs_receive(zvs_store *s, const char *ref, const char *sku, uint32_t loc, zind_qty qty) {
    return movement(s, ZVS_REC_RECEIVE, ref, sku, loc, NO_LOC, qty, 0);
}

int32_t zvs_sell(zvs_store *s, const char *ref, const char *sku, uint32_t loc, zind_qty qty) {
    return movement(s, ZVS_REC_SELL, ref, sku, loc, NO_LOC, qty, 0);
}

int32_t zvs_transfer(zvs_store *s, const char *ref, const char *sku, uint32_t from, uint32_t to,
                     zind_qty qty) {
    if (from == to) return ZVS_E_LOCATION;
    return movement(s, ZVS_REC_TRANSFER, ref, sku, from, to, qty, 0);
}

int32_t zvs_shrink(zvs_store *s, const char *ref, const char *sku, uint32_t loc, zind_qty qty,
                   int32_t reason) {
    if (reason < ZVS_SHRINK_THEFT || reason > ZVS_SHRINK_OTHER) return ZXV_E_ARG;
    return movement(s, ZVS_REC_SHRINK, ref, sku, loc, NO_LOC, qty, reason);
}

int32_t zvs_count(zvs_store *s, const char *ref, const char *sku, uint32_t loc, zind_qty counted,
                  zind_qty *out_variance) {
    uint8_t rh[32], h[32], buf[192];
    zind_wr w;
    item_t *it;
    zind_qty var;
    int ii;
    int32_t st;
    if (!s) return ZXV_E_ARG;
    st = ref_check(s, ref, rh);
    if (st != ZXV_OK) return st;
    ii = find_item(&s->book, sku);
    if (ii < 0) return ZVS_E_SKU;
    if (loc >= s->book.n_locs) return ZVS_E_LOCATION;
    it = &s->book.items[ii];
    if (!zind_qty_valid(counted) || zind_qty_sign(counted) < 0) return ZXV_E_ARG;
    if (it->unit == ZVS_UNIT_EACH && counted.den != 1) return ZVS_E_UNIT;
    if (it->pos[loc].held) return ZIND_E_HELD;
    st = zind_qty_sub(counted, it->pos[loc].qty, &var);
    if (st != ZXV_OK) return st;
    zind_wr_init(&w, buf, sizeof buf);
    zind_wr_str(&w, ref);
    zind_wr_str(&w, sku);
    zind_wr_u8(&w, (uint8_t) loc);
    zind_wr_qty(&w, counted);
    if (var.num == 0) {
        st = append(s, ZVS_REC_COUNT_OK, &w, NULL);
        if (st != ZXV_OK) return st;
        (void) zind_seen_add(s->refs, rh);
        it->pos[loc].tag = tag_of(ZXV_LP_TRUE);
        if (out_variance) *out_variance = var;
        return ZXV_OK;
    }
    zind_wr_qty(&w, it->pos[loc].qty);
    zind_wr_qty(&w, var);
    st = append(s, ZVS_REC_VARIANCE, &w, h);
    if (st != ZXV_OK) return st;
    (void) zind_seen_add(s->refs, rh);
    it->pos[loc].held = 1;
    it->pos[loc].variance = var;
    it->pos[loc].tag = tag_of(ZXV_LP_BOTH);
    memcpy(it->pos[loc].var_hash, h, 32);
    if (s->qs) {
        uint8_t subj[32];
        if (zind_subject("v-shelf/variance", h, 32, subj) == ZXV_OK)
            (void) zind_qsink_event(s->qs, it->pos[loc].tag, ZIND_REASON_MISMATCH,
                                    (uint32_t) ii, subj, NULL);
    }
    if (out_variance) *out_variance = var;
    zind_set_error("zvs_count: %s at location %u: count differs from the book; held", sku, loc);
    return ZVS_E_VARIANCE;
}

static int32_t var_msg(const item_t *it, uint32_t loc, int32_t decision, uint8_t *buf, size_t cap,
                       size_t *out_len) {
    zind_wr w;
    zind_wr_init(&w, buf, cap);
    zind_wr_bytes(&w, "zvs/variance/v1", 15);
    zind_wr_str(&w, it->sku);
    zind_wr_u8(&w, (uint8_t) loc);
    zind_wr_bytes(&w, it->pos[loc].var_hash, 32);
    zind_wr_u8(&w, (uint8_t) decision);
    *out_len = 15 + 1 + strlen(it->sku) + 1 + 32 + 1;
    return w.overflow ? ZXV_E_BUFFER : ZXV_OK;
}

static int32_t held_pos(const zvs_store *s, const char *sku, uint32_t loc, int *ii) {
    if (!s) return ZXV_E_ARG;
    *ii = find_item(&s->book, sku);
    if (*ii < 0) return ZVS_E_SKU;
    if (loc >= s->book.n_locs) return ZVS_E_LOCATION;
    if (!s->book.items[*ii].pos[loc].held) return ZXV_E_STATE;
    return ZXV_OK;
}

int32_t zvs_variance_message(const zvs_store *s, const char *sku, uint32_t loc, int32_t decision,
                             uint8_t *buf, size_t cap, size_t *out_len) {
    int ii = -1;
    int32_t st;
    if (!out_len || (decision != ZVS_ACCEPT_COUNT && decision != ZVS_KEEP_BOOK)) return ZXV_E_ARG;
    st = held_pos(s, sku, loc, &ii);
    if (st != ZXV_OK) return st;
    return var_msg(&s->book.items[ii], loc, decision, buf, cap, out_len);
}

int32_t zvs_variance_resolve(zvs_store *s, const char *sku, uint32_t loc,
                             const uint8_t approver_pk[ZIND_PK_BYTES], int32_t decision,
                             const uint8_t sig[ZIND_SIG_BYTES]) {
    uint8_t id[32], msg[128], sh[32], buf[192];
    size_t len = 0;
    uint32_t i;
    item_t *it;
    zind_qty nq = ZIND_QTY_ZERO, nadj = ZIND_QTY_ZERO;
    zind_wr w;
    int ii = -1;
    int32_t st;
    if (!approver_pk || !sig || (decision != ZVS_ACCEPT_COUNT && decision != ZVS_KEEP_BOOK))
        return ZXV_E_ARG;
    st = held_pos(s, sku, loc, &ii);
    if (st != ZXV_OK) return st;
    st = zxv_cap_check(s->cap, ZXV_CAP_ATTEST);
    if (st != ZXV_OK) {
        zind_set_error("zvs_variance_resolve: the store cap needs ATTEST to apply a decision");
        return st;
    }
    it = &s->book.items[ii];
    st = zind_key_id(approver_pk, id);
    if (st != ZXV_OK) return st;
    for (i = 0; i < s->n_approvers; i++)
        if (memcmp(s->approvers[i], id, 32) == 0) break;
    if (i == s->n_approvers) {
        zind_set_error("zvs_variance_resolve: the key is not a registered approver");
        return ZVS_E_APPROVER;
    }
    st = var_msg(it, loc, decision, msg, sizeof msg, &len);
    if (st == ZXV_OK) st = zind_verify(s->cap, approver_pk, msg, len, ZVS_VARIANCE_CTX, sig);
    if (st != ZXV_OK) {
        uint8_t ftag = 0, subj[32];
        zind_set_error("zvs_variance_resolve: the approver's signature does not verify");
        if (st == ZXV_E_VERIFY && s->qs &&
            zind_tag(ZXV_LP_FALSE, ZXV_HEX_POL_FORBIDDEN, ZXV_HEX_PHASE_EMERGING, &ftag) ==
                ZXV_OK &&
            zind_subject("v-shelf/forged", sig, ZIND_SIG_BYTES, subj) == ZXV_OK)
            (void) zind_qsink_event(s->qs, ftag, ZIND_REASON_TAMPER, (uint32_t) ii, subj, NULL);
        return st;
    }
    if (decision == ZVS_ACCEPT_COUNT) {
        st = zind_qty_add(it->pos[loc].qty, it->pos[loc].variance, &nq);
        if (st == ZXV_OK) st = zind_qty_add(it->adj, it->pos[loc].variance, &nadj);
        if (st != ZXV_OK) return st;
    }
    st = zxv_sha3_256(sig, ZIND_SIG_BYTES, sh);
    if (st != ZXV_OK) return st;
    zind_wr_init(&w, buf, sizeof buf);
    zind_wr_str(&w, sku);
    zind_wr_u8(&w, (uint8_t) loc);
    zind_wr_qty(&w, it->pos[loc].variance);
    zind_wr_bytes(&w, id, 32);
    zind_wr_bytes(&w, sh, 32);
    st = append(s, decision == ZVS_ACCEPT_COUNT ? ZVS_REC_ADJUST : ZVS_REC_RECOUNT, &w, NULL);
    if (st != ZXV_OK) return st;
    if (decision == ZVS_ACCEPT_COUNT) {
        it->pos[loc].qty = nq;
        it->adj = nadj;
        it->pos[loc].tag = tag_of(ZXV_LP_TRUE);
    } else {
        it->pos[loc].tag = tag_of(ZXV_LP_NEITHER);
    }
    it->pos[loc].held = 0;
    it->pos[loc].variance = ZIND_QTY_ZERO;
    return ZXV_OK;
}

int32_t zvs_on_hand(const zvs_store *s, const char *sku, uint32_t loc, zind_qty *out) {
    int ii;
    if (!s || !out) return ZXV_E_ARG;
    ii = find_item(&s->book, sku);
    if (ii < 0) return ZVS_E_SKU;
    if (loc >= s->book.n_locs) return ZVS_E_LOCATION;
    *out = s->book.items[ii].pos[loc].qty;
    return ZXV_OK;
}

int32_t zvs_total(const zvs_store *s, const char *sku, zind_qty *out) {
    zind_qty t = ZIND_QTY_ZERO;
    int ii;
    if (!s || !out) return ZXV_E_ARG;
    ii = find_item(&s->book, sku);
    if (ii < 0) return ZVS_E_SKU;
    for (uint32_t l = 0; l < s->book.n_locs; l++) {
        int32_t st = zind_qty_add(t, s->book.items[ii].pos[l].qty, &t);
        if (st != ZXV_OK) return st;
    }
    *out = t;
    return ZXV_OK;
}

int32_t zvs_position_state(const zvs_store *s, const char *sku, uint32_t loc, int32_t *out_held,
                           uint8_t *out_tag) {
    int ii;
    if (!s) return ZXV_E_ARG;
    ii = find_item(&s->book, sku);
    if (ii < 0) return ZVS_E_SKU;
    if (loc >= s->book.n_locs) return ZVS_E_LOCATION;
    if (out_held) *out_held = s->book.items[ii].pos[loc].held;
    if (out_tag) *out_tag = s->book.items[ii].pos[loc].tag;
    return ZXV_OK;
}

int32_t zvs_flows(const zvs_store *s, const char *sku, zind_qty *received, zind_qty *sold,
                  zind_qty *shrunk, zind_qty *adjusted) {
    int ii;
    if (!s) return ZXV_E_ARG;
    ii = find_item(&s->book, sku);
    if (ii < 0) return ZVS_E_SKU;
    if (received) *received = s->book.items[ii].rcv;
    if (sold) *sold = s->book.items[ii].sold;
    if (shrunk) *shrunk = s->book.items[ii].shr;
    if (adjusted) *adjusted = s->book.items[ii].adj;
    return ZXV_OK;
}

/* ===== replay ===== */

static int qeq(zind_qty a, zind_qty b) { return a.num == b.num && a.den == b.den; }

static int32_t replay_one(book_t *b, const zind_record *r) {
    zind_rd rd;
    char ref[256], sku[256], name[256];
    int32_t st = ZXV_OK;
    zind_rd_init(&rd, r->payload, r->payload_len);
    switch (r->kind) {
    case ZVS_REC_LOCATION:
        zind_rd_str(&rd, name, sizeof name);
        if (rd.bad) return ZXV_E_FORMAT;
        st = book_add_loc(b, name);
        break;
    case ZVS_REC_ITEM: {
        uint8_t unit;
        zind_rd_str(&rd, sku, sizeof sku);
        unit = zind_rd_u8(&rd);
        zind_rd_str(&rd, name, sizeof name);
        if (rd.bad) return ZXV_E_FORMAT;
        st = book_add_item(b, sku, unit, name);
        break;
    }
    case ZVS_REC_RECEIVE:
    case ZVS_REC_SELL:
    case ZVS_REC_TRANSFER:
    case ZVS_REC_SHRINK: {
        uint8_t l1, l2;
        zind_qty q;
        zind_rd_str(&rd, ref, sizeof ref);
        zind_rd_str(&rd, sku, sizeof sku);
        l1 = zind_rd_u8(&rd);
        l2 = zind_rd_u8(&rd);
        zind_rd_qty(&rd, &q);
        (void) zind_rd_u8(&rd);
        if (rd.bad) return ZXV_E_FORMAT;
        st = book_move(b, r->kind, find_item(b, sku), l1, l2, q);
        break;
    }
    case ZVS_REC_COUNT_OK:
    case ZVS_REC_VARIANCE: {
        uint8_t l;
        zind_qty counted, bookq = ZIND_QTY_ZERO, var = ZIND_QTY_ZERO, chk;
        int ii;
        zind_rd_str(&rd, ref, sizeof ref);
        zind_rd_str(&rd, sku, sizeof sku);
        l = zind_rd_u8(&rd);
        zind_rd_qty(&rd, &counted);
        if (r->kind == ZVS_REC_VARIANCE) {
            zind_rd_qty(&rd, &bookq);
            zind_rd_qty(&rd, &var);
        }
        ii = find_item(b, sku);
        if (rd.bad || ii < 0 || l >= b->n_locs) return ZXV_E_FORMAT;
        if (b->items[ii].pos[l].held) return ZXV_E_INVARIANT;
        if (zind_qty_sub(counted, b->items[ii].pos[l].qty, &chk) != ZXV_OK) return ZXV_E_INVARIANT;
        if (r->kind == ZVS_REC_COUNT_OK) {
            if (chk.num != 0) return ZXV_E_INVARIANT;
            b->items[ii].pos[l].tag = tag_of(ZXV_LP_TRUE);
        } else {
            if (chk.num == 0 || !qeq(chk, var) || !qeq(bookq, b->items[ii].pos[l].qty))
                return ZXV_E_INVARIANT;
            b->items[ii].pos[l].held = 1;
            b->items[ii].pos[l].variance = var;
            b->items[ii].pos[l].tag = tag_of(ZXV_LP_BOTH);
            memcpy(b->items[ii].pos[l].var_hash, r->hash, 32);
        }
        break;
    }
    case ZVS_REC_ADJUST:
    case ZVS_REC_RECOUNT: {
        uint8_t l;
        zind_qty var;
        int ii;
        item_t *it;
        zind_rd_str(&rd, sku, sizeof sku);
        l = zind_rd_u8(&rd);
        zind_rd_qty(&rd, &var);
        ii = find_item(b, sku);
        if (rd.bad || ii < 0 || l >= b->n_locs) return ZXV_E_FORMAT;
        it = &b->items[ii];
        if (!it->pos[l].held || !qeq(var, it->pos[l].variance)) return ZXV_E_INVARIANT;
        if (r->kind == ZVS_REC_ADJUST) {
            st = zind_qty_add(it->pos[l].qty, var, &it->pos[l].qty);
            if (st == ZXV_OK) st = zind_qty_add(it->adj, var, &it->adj);
            if (st != ZXV_OK) return ZXV_E_INVARIANT;
            if (zind_qty_sign(it->pos[l].qty) < 0) return ZXV_E_INVARIANT;
            it->pos[l].tag = tag_of(ZXV_LP_TRUE);
        } else {
            it->pos[l].tag = tag_of(ZXV_LP_NEITHER);
        }
        it->pos[l].held = 0;
        it->pos[l].variance = ZIND_QTY_ZERO;
        break;
    }
    default: return ZXV_E_FORMAT;
    }
    return st == ZXV_OK ? ZXV_OK : ZXV_E_INVARIANT;
}

int32_t zvs_reconcile(const zvs_store *s, uint32_t *out_bad) {
    const uint8_t *trust[1];
    book_t b;
    uint32_t n = 0, bad = UINT32_MAX;
    int32_t st;
    if (out_bad) *out_bad = UINT32_MAX;
    if (!s) return ZXV_E_ARG;
    trust[0] = zind_signer_pk(s->signer);
    st = zind_chain_verify(s->cap, s->chain, trust, 1, &bad);
    if (st != ZXV_OK) {
        if (out_bad) *out_bad = bad;
        return st;
    }
    memset(&b, 0, sizeof b);
    zind_chain_count(s->chain, &n);
    for (uint32_t i = 0; i < n; i++) {
        zind_record r;
        st = zind_chain_at(s->chain, i, &r);
        if (st == ZXV_OK) st = replay_one(&b, &r);
        if (st != ZXV_OK) {
            zind_set_error("zvs_reconcile: record %u does not replay (%s)", i,
                           zvs_status_name(st));
            book_free(&b);
            if (out_bad) *out_bad = i;
            return ZXV_E_INVARIANT;
        }
    }
    st = ZXV_OK;
    if (b.n_locs != s->book.n_locs || b.n_items != s->book.n_items) st = ZXV_E_INVARIANT;
    for (uint32_t i = 0; st == ZXV_OK && i < b.n_items; i++) {
        const item_t *x = &b.items[i], *y = &s->book.items[i];
        if (strcmp(x->sku, y->sku) != 0 || x->unit != y->unit || !qeq(x->rcv, y->rcv) ||
            !qeq(x->sold, y->sold) || !qeq(x->shr, y->shr) || !qeq(x->adj, y->adj))
            st = ZXV_E_INVARIANT;
        for (uint32_t l = 0; st == ZXV_OK && l < b.n_locs; l++)
            if (!qeq(x->pos[l].qty, y->pos[l].qty) || x->pos[l].held != y->pos[l].held ||
                x->pos[l].tag != y->pos[l].tag || !qeq(x->pos[l].variance, y->pos[l].variance))
                st = ZXV_E_INVARIANT;
    }
    if (st != ZXV_OK) zind_set_error("zvs_reconcile: the replayed journal disagrees with the book");
    book_free(&b);
    if (st != ZXV_OK && out_bad) *out_bad = n;
    return st;
}
