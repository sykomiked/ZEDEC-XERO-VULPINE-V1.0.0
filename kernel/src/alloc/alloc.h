/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* alloc.h — the sustainable symbiosis engine + a coin no king can seize.
 *
 * TWO RESPONSIBILITIES
 * --------------------
 * (1) AUTOMATIC + SUSTAINABLE CAPITAL ALLOCATION. Contributions of a given form
 *     of capital arrive; the pool of that form flows back to the very parties who
 *     supplied it, in proportion to what they gave (symbiosis: you receive as you
 *     contribute). A distribution is a CONSERVED split (the shares sum to the pool
 *     exactly) back to willing contributors; The One Policy's harm/coercion/
 *     interest gate is applied where it can actually bite — on a DRAW with terms,
 *     via alloc_sustainable_ok — not on a bare conserved split. The sustainability
 *     bound will not draw a steward stock below its SUPPLIED
 *     sustainable-yield floor, nor deduct more than the customary rate.
 *
 * (2) A NON-REPLICABLE, KILL-SWITCH-FREE PLATFORM TOKEN. Minted with a
 *     content-addressed provenance (the same SHA-256 CID primitive src/ipfs uses),
 *     recorded on an append-only ledger. A token can be transferred by its owner
 *     and no one else; it cannot be double-spent; and there is — BY CONSTRUCTION —
 *     no confiscation, no clawback, no seizure, no back-door path. For the people, by the
 *     people: a coin no king can seize, and no king means no kill switch.
 *
 * FREESTANDING: integer only (surplus_real_t), fixed-size arrays, no libc, no
 * allocation, no floating point on target. Composes zcapital, onepolicy, surplus.
 *
 * OPS BOUNDARIES (stated honestly, never faked): the sustainable-yield FLOOR and
 * any exchange RATE are SUPPLIED inputs, not discovered on-device. And
 * "non-replicable" here is a LOCAL single-spend ledger invariant plus a
 * content-addressed provenance — global uniqueness across a real network needs
 * consensus, which this module does not pretend to provide.
 */
#ifndef ZXV_ALLOC_H
#define ZXV_ALLOC_H

#include <stdint.h>
#include <stdbool.h>
#include "surplus.h"
#include "zcapital.h"
#include "onepolicy.h"

/* ============================================================= *
 *  PART 1 — sustainable capital allocation                       *
 * ============================================================= */

/* The customary rate is a single tunable constant: 11%. Anything above it is not
 * a customary deduction — it is a taking, and the sustainability bound refuses it.
 * Expressed as a fraction (never a float): NUM/DEN. */
#define ALLOC_CUSTOMARY_RATE_NUM 11
#define ALLOC_CUSTOMARY_RATE_DEN 100

#define ALLOC_MAX_CONTRIBUTORS 32   /* distinct contributors retained per form */

typedef struct {
    uint32_t       contributor;   /* opaque party id                          */
    surplus_real_t units;         /* accumulated contribution of this form    */
    bool           active;
} alloc_entry_t;

/* The pool: one contribution book per capital form, plus the running total of
 * each form (the amount that will be distributed back). Fixed size, zero heap. */
typedef struct {
    alloc_entry_t  book[ZCAP_FORM_COUNT][ALLOC_MAX_CONTRIBUTORS];
    uint32_t       count[ZCAP_FORM_COUNT];
    surplus_real_t pooled[ZCAP_FORM_COUNT];
} alloc_pool_t;

/* One party's share of a distribution. */
typedef struct {
    uint32_t       contributor;
    surplus_real_t amount;
} alloc_share_t;

/* The result of distributing a form's pool. `total` is the sum of all shares and
 * equals the pool EXACTLY (the last share absorbs any rounding remainder so the
 * conservation is exact on the fixed-point target, not merely approximate). */
typedef struct {
    zcap_form_t    form;
    uint32_t       count;
    surplus_real_t total;
    alloc_share_t  shares[ALLOC_MAX_CONTRIBUTORS];
} alloc_result_t;

typedef enum {
    ALLOC_OK = 0,
    ALLOC_ERR_ARG,           /* NULL / out-of-range form / non-positive units   */
    ALLOC_ERR_FULL,          /* the per-form contributor book is full           */
    ALLOC_ERR_EMPTY,         /* nothing pooled for this form — no fabrication    */
    ALLOC_ERR_NON_SYMBIOTIC, /* onepolicy refused: not a reciprocal allocation  */
    ALLOC_ERR_UNSUSTAINABLE  /* would breach the sustainable-yield floor         */
} alloc_status_t;

void alloc_pool_init(alloc_pool_t *p);

/* Record a contribution. Accumulates onto the contributor's existing entry for
 * that form if present, else claims a new slot. Returns the slot index (>= 0),
 * or a negative alloc_status_t on a bad argument or a full book. */
int32_t alloc_contribute(alloc_pool_t *p, uint32_t contributor,
                         zcap_form_t form, surplus_real_t units);

/* Distribute the pooled contributions of `form` back to that form's contributors,
 * proportionally to their supplied units. It is a CONSERVED split — the shares
 * sum to the pool exactly (the last active share absorbs the sub-tick fixed-point
 * remainder). Returns ALLOC_OK and fills *out on success; a negative
 * alloc_status_t otherwise. The pool is NOT consumed (idempotent — the same pool
 * can be re-distributed). */
int32_t alloc_distribute(alloc_pool_t *p, zcap_form_t form, alloc_result_t *out);

/* ---- the sustainability bound ----
 * A transfer/allocation is described here in measurable form. The FLOOR is a
 * SUPPLIED input (an ops boundary — a real yield is measured off-device by whoever
 * stewards the stock), as is any RATE. */
typedef struct {
    op_term_t      term;         /* reciprocity descriptor for The One Policy    */
    surplus_real_t stock_before; /* the steward stock before this draw           */
    surplus_real_t draw;         /* amount drawn from the steward stock          */
    surplus_real_t yield_floor;  /* SUPPLIED sustainable-yield floor (ops bound)  */
    surplus_real_t principal;    /* base the deduction is taken from             */
    surplus_real_t deduction;    /* amount deducted (fee/gratuity)               */
} alloc_txn_t;

/* The sustainability bound. Refuses, and only permits when ALL of:
 *   (a) the term is reciprocal          — op_symbiotic_ok(&txn->term)
 *   (b) stock_before - draw >= yield_floor   (never draw a stock below its floor)
 *   (c) deduction <= principal * 11/100      (no more than the customary rate)
 * Returns true iff sustainable. Pure predicate: no state, no clock. */
bool alloc_sustainable_ok(const alloc_txn_t *txn);

/* ---- the one place financial capital may be SPENT for another form ----
 * Composes zcapital's inalienability wall. Financial capital is the ONLY form
 * that may be spent to acquire another; the source is hard-wired to
 * ZCAP_FINANCIAL, so no non-financial form can be liquidated through here. A
 * non-priceable Crown form (Social/Natural/Cultural/Spiritual) can never be
 * bought: zcap_exchange returns ZCAP_INALIENABLE and the holding is untouched. */
zcap_result_t alloc_acquire_with_financial(zcap_vec_t *v, zcap_form_t to,
                                           surplus_real_t amount);

/* ============================================================= *
 *  PART 2 — the non-replicable, kill-switch-free platform token  *
 * ============================================================= */

#define PTOKEN_ID_LEN     32u   /* a content-address is one SHA-256 digest      */
#define PTOKEN_LEDGER_CAP 64u   /* fixed-capacity append-only ledger            */

typedef struct {
    uint8_t  id[PTOKEN_ID_LEN]; /* content-address of this token's provenance   */
    uint32_t owner;             /* the sole party who may transfer it           */
    uint64_t nonce;             /* mint/transfer ordinal (phase-tick, not clock) */
    bool     spent;             /* append-only: set once, on transfer out       */
} ptoken_t;

/* The ledger is append-only. There is exactly one mutation to any existing entry
 * — flipping `spent` false->true when its value is transferred out — and there is
 * NO function anywhere that seizes, clears, or nullifies a token. That absence is
 * the guarantee. */
typedef struct {
    ptoken_t entries[PTOKEN_LEDGER_CAP];
    uint32_t count;
} ptoken_ledger_t;

typedef enum {
    PTOKEN_OK = 0,
    PTOKEN_ERR_ARG,        /* NULL argument                                    */
    PTOKEN_ERR_FULL,       /* ledger at capacity                               */
    PTOKEN_ERR_NOT_FOUND,  /* no live token with that id                       */
    PTOKEN_ERR_NOT_OWNER,  /* `from` is not the current owner                  */
    PTOKEN_ERR_SPENT       /* already spent — the double-spend refusal         */
} ptoken_status_t;

void ptoken_ledger_init(ptoken_ledger_t *l);

/* Mint a fresh token for `owner`. Its id is the content-address (SHA-256) of its
 * provenance = genesis || owner || nonce, so the id commits to the founding
 * genesis it was minted against. Returns PTOKEN_OK and fills *out. */
int32_t ptoken_mint(ptoken_ledger_t *l, uint32_t owner,
                    const uint8_t genesis[PTOKEN_ID_LEN], ptoken_t *out);

/* Transfer the live token with id `id` from `from` to `to`: marks the old token
 * spent and APPENDS a new token (new content-addressed id) owned by `to`.
 * NON-REPLICABLE: a second transfer of the same id finds the token already spent
 * and returns PTOKEN_ERR_SPENT — the double-spend is refused and no balance moves.
 * There is deliberately no seizure path: `from` must be the current owner. */
int32_t ptoken_transfer(ptoken_ledger_t *l, const uint8_t id[PTOKEN_ID_LEN],
                       uint32_t from, uint32_t to);

/* Count live (unspent) tokens owned by `owner` — a simple "balance". */
uint32_t ptoken_balance(const ptoken_ledger_t *l, uint32_t owner);

#endif /* ZXV_ALLOC_H */
