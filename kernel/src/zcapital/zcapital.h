/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* zcapital.h — the canonical nine-form capital substrate, the inalienability
 * wall, and a tiny symbiosis market. Composes The One Policy.
 *
 * WHAT THIS IS
 * -----------
 * The platform recognises nine forms of capital, and — crucially — not all of
 * them are for sale. Four of them belong to the Crown and are inalienable: you
 * cannot buy a language, you cannot buy a river, you cannot buy a people's
 * culture, you cannot buy their spirit — and you cannot SELL them either, in
 * either direction. The other five (Financial, Manufactured, Intellectual,
 * Human, System) are priceable and may be exchanged.
 *
 * This is a mechanical SUBSTRATE: an exchange here is a conserved conversion
 * within one holding, and its admissibility is fully defined by four hard
 * invariants — the amount is non-negative, neither side is an inalienable Crown
 * form, the source balance covers it, and value is conserved. The One Policy
 * (the Symbiotic Maxim over harm/coercion/interest/reciprocity) governs DEALS
 * with counterparties and terms — it is composed at the settlement layer
 * (see ministry_settle, which reachably voids a usurious term), NOT re-run on
 * every integer move. Substrate integrity is the mechanical invariants; the
 * maxim is the constitutional court for deals.
 *
 * This is the CANONICAL platform capital enum. Do NOT reuse vino.h's
 * capital_type_t; that one is a different, older taxonomy.
 */
#ifndef ZXV_ZCAPITAL_H
#define ZXV_ZCAPITAL_H

#include <stdint.h>
#include <stdbool.h>
#include "surplus.h"

/* ===== The nine canonical forms of capital ===== */
typedef enum {
    ZCAP_FINANCIAL = 0,
    ZCAP_MANUFACTURED,
    ZCAP_INTELLECTUAL,
    ZCAP_HUMAN,
    ZCAP_SOCIAL,
    ZCAP_NATURAL,
    ZCAP_CULTURAL,
    ZCAP_SPIRITUAL,
    ZCAP_SYSTEM
} zcap_form_t;

#define ZCAP_FORM_COUNT 9

/* ===== Who holds jurisdiction over each form (proposal Sec 3.5) =====
 * Ministry governs the four priceable material/knowledge forms; the Crown holds
 * the four inalienable forms; the Co-Juris holds the System form (the substrate
 * itself, shared and self-governing). */
typedef enum {
    ZCAP_AUTH_MINISTRY,
    ZCAP_AUTH_CROWN,
    ZCAP_AUTH_COJURIS
} zcap_authority_t;

/* Which authority owns/governs this form of capital. */
zcap_authority_t zcap_who_owns_this(zcap_form_t form);

/* True iff this form may bear a price at all. The four Crown forms
 * (Social/Natural/Cultural/Spiritual) are inalienable and are NOT priceable —
 * you cannot buy a language. Ministry forms and the System form ARE priceable. */
bool zcap_is_priceable(zcap_form_t form);

/* ===== A holding of capital across all nine forms ===== */
typedef struct {
    surplus_real_t bal[ZCAP_FORM_COUNT];
} zcap_vec_t;

/* ===== Result of an exchange attempt ===== */
typedef enum {
    ZCAP_OK = 0,
    ZCAP_INALIENABLE,   /* either side is a non-priceable Crown form            */
    ZCAP_INSUFFICIENT   /* amount is negative, or source balance cannot cover it */
} zcap_result_t;

/* Move `amount` of value from form `from` to form `to` within holding `v`.
 * Refuses (and leaves `v` untouched) when:
 *   - `amount` is negative                            -> ZCAP_INSUFFICIENT
 *     (you cannot move value you do not hold, and negative value is not held —
 *      this is the guard that stops a reverse move from MINTING value)
 *   - `to` OR `from` is a non-priceable Crown form    -> ZCAP_INALIENABLE
 *     (you can neither buy nor sell a language)
 *   - `v->bal[from]` < amount                         -> ZCAP_INSUFFICIENT
 * Otherwise moves the value (from -= amount, to += amount) and returns ZCAP_OK.
 * Value is conserved: bal[from] + bal[to] is invariant across a successful call. */
zcap_result_t zcap_exchange(zcap_vec_t *v, zcap_form_t from, zcap_form_t to,
                            surplus_real_t amount);

/* The gratuity: 11% of `amount`, returned SEPARATELY. It is never deducted from
 * the principal — it is added on top by whoever chooses to give it. */
surplus_real_t zcap_gratuity(surplus_real_t amount);

/* ===== A tiny fixed-capacity per-form symbiosis market ===== */
#define ZMARKET_SLOTS 32  /* offers/seeks retained per form before we stop taking */

typedef struct {
    surplus_real_t units;
    uint32_t party;
    bool active;
} zcap_book_entry_t;

typedef struct {
    zcap_book_entry_t offers[ZCAP_FORM_COUNT][ZMARKET_SLOTS];
    zcap_book_entry_t seeks[ZCAP_FORM_COUNT][ZMARKET_SLOTS];
    uint32_t offer_count[ZCAP_FORM_COUNT];
    uint32_t seek_count[ZCAP_FORM_COUNT];
} zmarket_t;

/* A matched, One-Policy-recognised commitment between a giver and a taker. */
typedef struct {
    zcap_form_t form;
    surplus_real_t units;
    uint32_t giver, taker;
} zcap_commitment_t;

/* Reset the market to empty. */
void zmarket_init(zmarket_t *m);

/* Post an offer to give `units` of `form` from `party`.
 * Returns the slot index (>= 0), or -1 if `units` is not positive or the book
 * for that form is full. */
int32_t zmarket_offer(zmarket_t *m, zcap_form_t form, surplus_real_t units,
                      uint32_t party);

/* Post a seek to receive `units` of `form` for `party`.
 * Returns the slot index (>= 0), or -1 if `units` is not positive or the book
 * for that form is full. */
int32_t zmarket_seek(zmarket_t *m, zcap_form_t form, surplus_real_t units,
                     uint32_t party);

/* FIFO-pair the oldest active offer with the oldest active seek for `form`,
 * emit the resulting commitment in *out, consume both entries, and return true.
 * Returns false (and leaves *out untouched) when there is no counterpart, or
 * when the only pairing available would be a party with itself (a wash trade is
 * not a real reciprocal deal). */
bool zmarket_match(zmarket_t *m, zcap_form_t form, zcap_commitment_t *out);

#endif /* ZXV_ZCAPITAL_H */
