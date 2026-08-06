/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* battering_ram.h — The Battering Ram Exchange.
 *
 * "Wall Street is a walled garden — battering rams bring down walled gardens.
 *  No margin, no forced losers, no house that takes a cut of your losses; a term
 *  that produces one-sided harm is void by definition."
 *
 * WHAT THIS IS
 * -----------
 * A venue to swap any one of the NINE canonical forms of capital (see
 * zcapital.h — we REUSE its zcap_form_t, we do NOT define a second enum) for
 * another, and to write futures ON the nine capitals (reusing finance/
 * financial.c's pricers, never a new one). It is NOT an order book and NOT an
 * AMM: there is no matching engine and no price discovery. Settlement is
 * posted-rate and ATOMIC — both legs at the alliance-agreed proportion, or
 * neither leg. Principal only: NO margin, NO debt, NO liquidation, NO
 * margin-call path exists anywhere in this module.
 *
 * WHAT IS AND IS NOT INVENTED (ops boundaries — the cardinal rule)
 * ---------------------------------------------------------------
 *   - The cross-capital RATE is a NEGOTIATED, SUPPLIED input (cross_cap_swap_t
 *     .agreed_rate). This module never discovers, quotes, or invents a rate.
 *   - Outcome verification is a HARD external dependency. br_distribute consumes
 *     an alliance ONLY on a VERIFIED attestation: we verify the ATTESTOR'S
 *     SIGNATURE, never the real-world fact behind it. With no verifier bound we
 *     fail closed (BR_ERR_NO_ORACLE) — we never assume achievement.
 *   - Settlement of a money leg posts THROUGH a bound vino_stores triple rail
 *     when one is bound; with none bound, a swap moves principal on the
 *     exchange's own nine-form book (a real conserved move, not an invented
 *     fill) and no external money event is fabricated.
 *   - Only 11% tribute / 11% gratuity and the "contributors retain the
 *     majority" invariant are numeric here. Every other split rate is a config
 *     parameter, never a hardcoded guess.
 *
 * Freestanding: integer-only surplus_real_t via the SR_ macros, fixed-size
 * arrays, no libc, no allocation, no floating point on target.
 */
#ifndef ZXV_BATTERING_RAM_H
#define ZXV_BATTERING_RAM_H

#include <stdint.h>
#include <stdbool.h>
#include "surplus.h"
#include "zcapital.h"      /* zcap_form_t, ZCAP_FORM_COUNT, zcap_vec_t — REUSED  */
#include "vino_stores.h"   /* the triple-rail settlement engine (bound rail)     */

/* ===== Result / status codes ===== */
#define BR_OK                0
#define BR_NOT_ACHIEVED      1    /* verified, but outcome=false: forgiven, zero debt */
#define BR_ERR_NULL        (-1)
#define BR_ERR_RANGE       (-2)   /* form out of range, negative amount, rate <= 0 */
#define BR_ERR_CAPACITY    (-3)   /* alliance table full, or too many contributors */
#define BR_ERR_INSUFFICIENT (-4)  /* a swap leg cannot complete on the book        */
#define BR_ERR_VOID_TERM   (-5)   /* onepolicy vetoed the term (harm / usury)      */
#define BR_ERR_NO_ORACLE   (-6)   /* no attestation verifier bound — fail closed   */
#define BR_ERR_UNVERIFIED  (-7)   /* attestation signature did not verify          */
#define BR_ERR_NOT_FOUND   (-8)   /* no alliance for that outcome_id               */
#define BR_ERR_STATE       (-9)   /* alliance not in the required state            */
#define BR_ERR_SETTLEMENT  (-10)  /* a bound vino rail rejected the money leg      */
#define BR_ERR_NO_RAIL     (-11)  /* a FINANCIAL (money) leg with no rail to back it*/

/* ===== Capacities (fixed, freestanding) ===== */
#define BR_MAX_ALLIANCES   64u
#define BR_MAX_PLEDGES     16u    /* contributors per alliance / parties per split */

/* ===== A single contributor's pledge into an alliance ===== */
typedef struct {
    uint64_t    contributor;   /* the contributor's zxv id                        */
    zcap_form_t form;          /* which of the nine forms they pledge             */
    surplus_real_t units;      /* how many units of it (>= 0)                      */
} pledge_t;

/* ===== Alliance lifecycle ===== */
typedef enum {
    BR_ALLIANCE_NONE = 0,      /* empty slot                                      */
    BR_PROPOSED,               /* opened, not yet formed                          */
    BR_ALLIANCE_FORMED,        /* pledges bound; awaiting a verified outcome       */
    BR_DISTRIBUTED,            /* a verified achievement was distributed          */
    BR_FORGIVEN                /* a non-achieved outcome — zero debt, no clawback  */
} br_alliance_state_t;

typedef struct {
    uint64_t           outcome_id;
    br_alliance_state_t state;
    pledge_t           pledges[BR_MAX_PLEDGES];
    uint32_t           n;
    surplus_real_t     total_units;   /* sum of pledged units — the split base     */
} br_alliance_t;

/* ===== A cross-capital swap request ===== */
typedef struct {
    zcap_form_t    from;         /* form paid out of the book                      */
    zcap_form_t    to;           /* form received into the book                    */
    surplus_real_t amount;       /* units of `from` to pay (>= 0)                  */
    surplus_real_t agreed_rate;  /* SUPPLIED proportion; received = amount * rate  */
} cross_cap_swap_t;

/* ===== An external outcome attestation (ops boundary) =====
 * `attestor` is treated as the 32-byte Ed25519 public key of the attesting
 * authority; `sig` is its signature over (outcome_id LE64 || achieved byte).
 * We verify THIS signature — never the fact it asserts. */
typedef struct {
    uint64_t outcome_id;
    bool     achieved;
    uint8_t  attestor[32];
    uint8_t  sig[64];
} ext_attestation_t;

/* ===== The result of a distribution / revenue split ===== */
typedef struct {
    surplus_real_t realized;          /* value distributed                        */
    surplus_real_t tribute;           /* 11% (revenue split only; 0 in distribute) */
    surplus_real_t gratuity;          /* 11% (revenue split only; 0 in distribute) */
    surplus_real_t contributor_pool;  /* the MAJORITY retained by contributors    */
    uint32_t       n;
    uint64_t       party_id[BR_MAX_PLEDGES];
    zcap_form_t    party_form[BR_MAX_PLEDGES];
    surplus_real_t party[BR_MAX_PLEDGES];  /* each contributor's proportional share */
    bool           achieved;          /* whether the outcome was achieved         */
    bool           zero_debt;         /* invariant witness: nobody owes anything   */
} allocation_t;

/* ===== A term offered to the void-by-definition guard ===== */
typedef struct {
    surplus_real_t give_a;      /* value A conveys to B                            */
    surplus_real_t give_b;      /* value B conveys to A                            */
    surplus_real_t harm_a;      /* harm imposed on A (>= 0)                        */
    surplus_real_t harm_b;      /* harm imposed on B (>= 0)                        */
    surplus_real_t interest;    /* interest charged (must be 0)                    */
    bool           reciprocal;  /* a matched return commitment binds both sides    */
    bool           on_suffering;/* a derivative written ON someone's suffering     */
} br_term_t;

/* ===== Pay-It-Forward dispositions for an unrealized outcome ===== */
typedef enum {
    PIF_FORGIVEN = 0,        /* the obligation simply dissolves — zero debt        */
    PIF_FORWARDED,           /* the goodwill is passed to the next alliance        */
    PIF_LEARNING_CREDIT      /* recorded as a learning credit, not a liability     */
} payitforward_status_t;

/* ===== Attestation verifier hook (ops boundary) =====
 * Returns true iff the attestation's signature verifies. Model your real oracle
 * as this function pointer. NULL => br_distribute fails closed. */
typedef bool (*br_attest_verify_fn)(const ext_attestation_t *att);

/* ===== The exchange ===== */
typedef struct {
    zcap_vec_t          book;          /* the exchange's own nine-form holdings    */
    br_alliance_t       alliances[BR_MAX_ALLIANCES];
    uint32_t            num_alliances;
    br_attest_verify_fn verify;        /* external outcome verifier (ops boundary) */
    vino_stores_t      *rail;          /* optional triple-rail money settlement     */
    uint64_t            rail_voucher;  /* voucher id the money leg posts through    */
    uint8_t             rail_cid[VINO_PROOF_CID_LEN]; /* supplied equity witness    */
} br_exchange_t;

/* ===== Lifecycle ===== */

/* Zero the exchange: empty book, no alliances, no verifier, no rail. */
void br_exchange_init(br_exchange_t *ex);

/* Install the external attestation verifier (ops boundary). */
void br_set_verifier(br_exchange_t *ex, br_attest_verify_fn fn);

/* Bind a vino_stores triple rail for money-leg settlement. The voucher id and
 * proof CID are SUPPLIED (pinned externally, never invented). */
void br_bind_rail(br_exchange_t *ex, vino_stores_t *rail, uint64_t voucher_id,
                  const uint8_t proof_cid[VINO_PROOF_CID_LEN]);

/* Seed a form's balance on the exchange book (test/bootstrap helper). */
void br_book_credit(br_exchange_t *ex, zcap_form_t form, surplus_real_t units);

/* ===== Alliances ===== */

/* Open an alliance for `outcome_id` from `n` contributor pledges. Moves the
 * slot PROPOSED -> ALLIANCE_FORMED and records total pledged units. Returns the
 * alliance index (>= 0) or a BR_ERR_* code. */
int32_t br_alliance_open(br_exchange_t *ex, uint64_t outcome_id,
                         const pledge_t *contributors, uint32_t n);

/* ===== Cross-capital swap ===== */

/* Settle a swap ATOMICALLY at the SUPPLIED rate: both legs (pay `from`, receive
 * `to = amount*rate`) or neither. No price discovery. If a rail is bound and a
 * leg is Financial, the money event posts through vino_ledger_act and the whole
 * swap rolls back if the rail rejects it. Returns BR_OK or a BR_ERR_* code; on
 * any error NEITHER balance changes. */
int32_t br_swap_settle(br_exchange_t *ex, const cross_cap_swap_t *s);

/* ===== Distribution & Pay-It-Forward ===== */

/* Consume a formed alliance on a VERIFIED attestation only. Verifies the
 * attestor's signature (never the fact). On achievement, distributes the pooled
 * principal proportionally — contributors retain the majority. On a verified
 * non-achievement, marks the alliance FORGIVEN with ZERO debt. Fails closed
 * (BR_ERR_NO_ORACLE) with no verifier; ignores a bad signature
 * (BR_ERR_UNVERIFIED) with no distribution. Returns BR_OK, BR_NOT_ACHIEVED, or
 * a BR_ERR_* code. */
int32_t br_distribute(br_exchange_t *ex, uint64_t outcome_id,
                      const ext_attestation_t *v, allocation_t *out);

/* Dispose of an unrealized outcome. Produces ZERO debt for every party — there
 * is NO liquidation and NO margin call. Returns BR_OK or a BR_ERR_* code. */
int32_t br_payitforward(br_exchange_t *ex, uint64_t outcome_id,
                        payitforward_status_t how);

/* ===== Revenue split ===== */

/* Carve a realized surplus into 11% tribute + 11% gratuity, leaving the
 * MAJORITY (78%) as the contributor pool. tribute and gratuity are computed as
 * true fractions of `realized`; the pool is the exact remainder so the three
 * sum to `realized`. Fills a fresh allocation_t. */
void br_revenue_split(surplus_real_t realized, allocation_t *out);

/* True iff the allocation leaves contributors strictly more than half. */
bool br_contributors_hold_majority(const allocation_t *a);

/* ===== The void-by-definition guard ===== */

/* True iff the term is admissible. Composes The One Policy (op_symbiotic_ok):
 * rejects asymmetric harm, unilateral extraction, non-reciprocal burden, and
 * usury/interest — and additionally rejects a derivative written on suffering. */
bool symbiotic_ok(const br_term_t *t);

/* ===== Futures ON the nine capitals (reuses finance/financial.c) ===== */

/* Price a future written on `notional` units of capital `form`, carried at the
 * SUPPLIED rate `r` over time `T` (dividend/convenience yields zero, coverage
 * neutral). Composes financial_price_future — it does NOT reimplement a pricer.
 * Returns the future price. */
surplus_real_t br_price_capital_future(zcap_form_t form, surplus_real_t notional,
                                       surplus_real_t r, surplus_real_t T);

/* Built-in verifier composing Ed25519 over (outcome_id LE64 || achieved). Use
 * as br_set_verifier(ex, br_ed25519_attest_verify) to require real signatures. */
bool br_ed25519_attest_verify(const ext_attestation_t *att);

#endif /* ZXV_BATTERING_RAM_H */
