/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zcapital.c — see zcapital.h. Nine forms of capital, a wall around the four
 * that are not for sale, and a little market for the ones that are. */
#include "zcapital.h"

/* ===== Jurisdiction table (proposal Sec 3.5) =====
 * Financial/Manufactured/Intellectual/Human -> Ministry
 * Social/Natural/Cultural/Spiritual         -> Crown (inalienable)
 * System                                    -> Co-Juris
 * Indexed by zcap_form_t; kept static const so it lives in .rodata and needs no
 * initialisation at boot. */
static const zcap_authority_t k_jurisdiction[ZCAP_FORM_COUNT] = {
    [ZCAP_FINANCIAL]    = ZCAP_AUTH_MINISTRY,
    [ZCAP_MANUFACTURED] = ZCAP_AUTH_MINISTRY,
    [ZCAP_INTELLECTUAL] = ZCAP_AUTH_MINISTRY,
    [ZCAP_HUMAN]        = ZCAP_AUTH_MINISTRY,
    [ZCAP_SOCIAL]       = ZCAP_AUTH_CROWN,
    [ZCAP_NATURAL]      = ZCAP_AUTH_CROWN,
    [ZCAP_CULTURAL]     = ZCAP_AUTH_CROWN,
    [ZCAP_SPIRITUAL]    = ZCAP_AUTH_CROWN,
    [ZCAP_SYSTEM]       = ZCAP_AUTH_COJURIS,
};

zcap_authority_t zcap_who_owns_this(zcap_form_t form) {
    if ((uint32_t)form >= ZCAP_FORM_COUNT) return ZCAP_AUTH_COJURIS;
    return k_jurisdiction[form];
}

bool zcap_is_priceable(zcap_form_t form) {
    if ((uint32_t)form >= ZCAP_FORM_COUNT) return false;
    /* Only the Crown forms are inalienable. Everything Ministry or Co-Juris
     * governs can bear a price. */
    return k_jurisdiction[form] != ZCAP_AUTH_CROWN;
}

zcap_result_t zcap_exchange(zcap_vec_t *v, zcap_form_t from, zcap_form_t to,
                            surplus_real_t amount) {
    if (!v) return ZCAP_INSUFFICIENT;
    if ((uint32_t)from >= ZCAP_FORM_COUNT || (uint32_t)to >= ZCAP_FORM_COUNT)
        return ZCAP_INALIENABLE;

    /* A negative amount is not value you hold. Without this guard a reverse move
     * would INFLATE the source and drive the destination negative — minting value
     * out of nothing. It is the substrate's first invariant. */
    if (SR_CMP(amount, SR_ZERO) < 0) return ZCAP_INSUFFICIENT;

    /* The Crown declines — in BOTH directions. You cannot buy a language, and you
     * cannot sell one either; an inalienable form may not be a leg of an exchange. */
    if (!zcap_is_priceable(to) || !zcap_is_priceable(from)) return ZCAP_INALIENABLE;

    /* You cannot move value you do not hold. */
    if (SR_CMP(v->bal[from], amount) < 0) return ZCAP_INSUFFICIENT;

    /* Conservative move: what leaves `from` arrives at `to`, to the token. */
    v->bal[from] = SR_SUB(v->bal[from], amount);
    v->bal[to]   = SR_ADD(v->bal[to], amount);
    return ZCAP_OK;
}

surplus_real_t zcap_gratuity(surplus_real_t amount) {
    /* 11% — the eleven, given on top, never taken out. */
    return SR_DIV(SR_MUL(amount, SR_FROM_INT(11)), SR_FROM_INT(100));
}

/* ===== The tiny symbiosis market ===== */

void zmarket_init(zmarket_t *m) {
    if (!m) return;
    for (uint32_t f = 0; f < ZCAP_FORM_COUNT; f++) {
        m->offer_count[f] = 0;
        m->seek_count[f] = 0;
        for (uint32_t s = 0; s < ZMARKET_SLOTS; s++) {
            m->offers[f][s].active = false;
            m->offers[f][s].units = SR_ZERO;
            m->offers[f][s].party = 0;
            m->seeks[f][s].active = false;
            m->seeks[f][s].units = SR_ZERO;
            m->seeks[f][s].party = 0;
        }
    }
}

/* Drop consumed entries, keeping the active ones in FIFO order. Without this a
 * book that ever held ZMARKET_SLOTS posts stays "full" forever, even after
 * every one of them has been matched. */
static void compact(zcap_book_entry_t *book, uint32_t *count)
{
    uint32_t w = 0;
    for (uint32_t r = 0; r < *count; r++)
        if (book[r].active) book[w++] = book[r];
    for (uint32_t i = w; i < *count; i++) {
        book[i].active = false;
        book[i].units = SR_ZERO;
        book[i].party = 0;
    }
    *count = w;
}

int32_t zmarket_offer(zmarket_t *m, zcap_form_t form, surplus_real_t units,
                      uint32_t party) {
    if (!m || (uint32_t)form >= ZCAP_FORM_COUNT) return -1;
    if (SR_CMP(units, SR_ZERO) <= 0) return -1;  /* no zero/negative-unit posts */
    if (m->offer_count[form] >= ZMARKET_SLOTS) compact(m->offers[form], &m->offer_count[form]);
    uint32_t n = m->offer_count[form];
    if (n >= ZMARKET_SLOTS) return -1;  /* book is full — no hollow fills */
    m->offers[form][n].units = units;
    m->offers[form][n].party = party;
    m->offers[form][n].active = true;
    m->offer_count[form] = n + 1;
    return (int32_t)n;
}

int32_t zmarket_seek(zmarket_t *m, zcap_form_t form, surplus_real_t units,
                     uint32_t party) {
    if (!m || (uint32_t)form >= ZCAP_FORM_COUNT) return -1;
    if (SR_CMP(units, SR_ZERO) <= 0) return -1;  /* no zero/negative-unit posts */
    if (m->seek_count[form] >= ZMARKET_SLOTS) compact(m->seeks[form], &m->seek_count[form]);
    uint32_t n = m->seek_count[form];
    if (n >= ZMARKET_SLOTS) return -1;
    m->seeks[form][n].units = units;
    m->seeks[form][n].party = party;
    m->seeks[form][n].active = true;
    m->seek_count[form] = n + 1;
    return (int32_t)n;
}

/* Find the oldest still-active entry in a per-form book. Returns index or -1. */
static int32_t first_active(const zcap_book_entry_t *book, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        if (book[i].active) return (int32_t)i;
    }
    return -1;
}

bool zmarket_match(zmarket_t *m, zcap_form_t form, zcap_commitment_t *out) {
    if (!m || !out || (uint32_t)form >= ZCAP_FORM_COUNT) return false;

    int32_t oi = first_active(m->offers[form], m->offer_count[form]);
    if (oi < 0) return false; /* no counterpart — no fabricated fill */

    /* The oldest seek from a DIFFERENT party: a party matching with itself is
     * a wash trade, not a reciprocal deal, and is skipped rather than allowed
     * to block every other pairing behind it. */
    int32_t si = -1;
    for (uint32_t i = 0; i < m->seek_count[form]; i++) {
        const zcap_book_entry_t *s = &m->seeks[form][i];
        if (s->active && s->party != m->offers[form][oi].party) {
            si = (int32_t) i;
            break;
        }
    }
    if (si < 0) return false;

    zcap_book_entry_t *offer = &m->offers[form][oi];
    zcap_book_entry_t *seek  = &m->seeks[form][si];

    /* Fill the smaller side: the taker never receives more than it sought,
     * the giver never conveys more than it offered. The larger side keeps
     * its remainder on the book. */
    surplus_real_t fill = SR_CMP(offer->units, seek->units) <= 0 ? offer->units : seek->units;
    out->form  = form;
    out->units = fill;
    out->giver = offer->party;
    out->taker = seek->party;

    offer->units = SR_SUB(offer->units, fill);
    seek->units = SR_SUB(seek->units, fill);
    if (SR_CMP(offer->units, SR_ZERO) <= 0) offer->active = false;
    if (SR_CMP(seek->units, SR_ZERO) <= 0) seek->active = false;
    return true;
}
