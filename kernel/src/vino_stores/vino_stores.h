/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* vino_stores.h — the Vino floating-voucher SETTLEMENT ENGINE.
 *
 * "Pieces of eight that are one coin in two bodies — a bearer note in your hand
 *  or its ghost on the ledger, never both. Money is equity, never debt: no
 *  interest aboard this ship."
 *
 * This does NOT reimplement double-entry. It EXTENDS finance/triple_ledger's
 * floating_voucher_t and posts every value event THROUGH triple_ledger_post as
 * three atomic rails (Debit : Credit : Equity). On top of the ledger it adds:
 *
 *   - the single-active-state invariant: a Vino unit is ACTIVE on exactly one
 *     of two rails (physical bearer note or digital ledger-ghost). This is
 *     enforced by the STATE MACHINE — a spend is only permitted on the currently
 *     active rail (vino_can_spend checks state == rail); the other rail is
 *     unspendable, so the same unit can never be spent as both a bearer note and
 *     a ledger ghost, and supply can never double. (This is a state invariant,
 *     not a cryptographic lock — it does not rely on any crypto primitive.)
 *   - a hard SYSTEM-SOLVENCY gate on EVERY event: after the three rails post, the
 *     WHOLE ledger must still satisfy coverage = total_assets/total_liabilities
 *     >= 1.8x AND total_equity = total_assets - total_liabilities >= 0, or the
 *     entire event rolls back with NO partial write. Note this is a ledger-wide
 *     over-collateralisation floor (a stronger, system-level invariant), not a
 *     per-account ratio.
 *   - a usury veto composed from onepolicy: an act that implies interest
 *     (equity claimed above the backing) is not a term at all — it is refused.
 *
 * The equity rail carries an immutable proof_cid (a 32-byte IPFS/SHA-256 content
 * address) as a STORED EXTERNAL WITNESS. The CID is an OPS BOUNDARY: it is
 * SUPPLIED by whoever pins the backing, stored, and re-anchored on a swap — the
 * module never invents one AND never device-certifies it (it does not gate a
 * decision on the CID; a real deployment verifies the pin out of band).
 *
 * Freestanding: integer-only surplus_real_t, fixed-size arrays, no libc, no
 * allocation, no floating point on target.
 */
#ifndef ZXV_VINO_STORES_H
#define ZXV_VINO_STORES_H

#include <stdint.h>
#include <stdbool.h>
#include "surplus.h"
#include "triple_ledger.h" /* floating_voucher_t, triple_ledger_t, post/verify */

/* ===== Rail numeric codes =====
 * Vino's three rails: DEBIT 555, CREDIT 777, EQUITY 888. None is an ISO 4217
 * currency: all three numerics are unassigned in ISO 4217 list one (the
 * cbank test proves it from the generated table). They are internal rail
 * numerics; external messages settle in a real currency and carry these
 * only in proprietary fields (see iso20022_ccy_caveat).
 *
 * Each rail carries a platform jurisdiction code. These are NOT ISO 3166
 * country codes and are never written into a Ctry field; they travel only
 * as /ZXV/... remittance data:
 *   DEBIT  555  NCR  New California Republic
 *   CREDIT 777  NRE  Neo Roman Empire
 *   EQUITY 888  PNS  Principality of New Sicily */
#include "../pay/pay_rails.h"                /* the canonical rail numerics */
#define VINO_ISO_DEBIT  ZXV_RAIL_CODE_DEBIT  /* 555 the asset/backing rail   (NCR) */
#define VINO_ISO_CREDIT ZXV_RAIL_CODE_CREDIT /* 777 the claim/liability rail (NRE) */
#define VINO_ISO_EQUITY ZXV_RAIL_CODE_EQUITY /* 888 the live equity rail     (PNS) */

#define VINO_JURIS_DEBIT  ZXV_RAIL_CODE_DEBIT_JURIS  /* New California Republic    */
#define VINO_JURIS_CREDIT ZXV_RAIL_CODE_CREDIT_JURIS /* Neo Roman Empire           */
#define VINO_JURIS_EQUITY ZXV_RAIL_CODE_EQUITY_JURIS /* Principality of New Sicily */

/* Proof-CID witness length — one SHA-256 digest, same as ipfs IPFS_CID_LEN. */
#define VINO_PROOF_CID_LEN 32u

/* The 1.8x coverage floor, as a fixed-point ratio (18/10). */
#define VINO_COVERAGE_NUM 18
#define VINO_COVERAGE_DEN 10

/* φ − 1 ≈ 0.6180339887 — the bounded forward-equity draw fraction. NEVER
 * interest: this is a cap on how much live equity may be drawn forward, not a
 * charge added to a principal. On the Q32.32 target this is Knuth's constant
 * 0x9E3779B9 = floor(0.6180339887 * 2^32), the exact fixed-point φ−1. */
#ifdef TEST_HOST
#    define PHI_MINUS_1 (0.61803398874989485)
#else
#    define PHI_MINUS_1 ((surplus_real_t) 0x9E3779B9LL)
#endif

/* ===== Result codes ===== */
#define VINO_OK            0
#define VINO_ERR_NULL      (-1)
#define VINO_ERR_COVERAGE  (-2) /* would drive coverage < 1.8x               */
#define VINO_ERR_EQUITY    (-3) /* would drive equity < 0                    */
#define VINO_ERR_USURY     (-4) /* interest implied — onepolicy vetoes it    */
#define VINO_ERR_CAPACITY  (-5) /* ledger/voucher table full                 */
#define VINO_ERR_NOT_FOUND (-6) /* no such voucher                           */
#define VINO_ERR_STATE     (-7) /* single-active-state violation             */

/* ===== The single-active-state pair ===== */
typedef enum {
    VINO_ACTIVE_PHYSICAL = 0, /* the bearer note is in your hand             */
    VINO_ACTIVE_DIGITAL = 1   /* the ghost is live on the ledger             */
} vino_state_t;

/* ===== Lifecycle ===== */
typedef enum {
    VINO_MINT = 0,
    VINO_CIRCULATE = 1,
    VINO_SWAP = 2,
    VINO_REDEEM = 3,
    VINO_RETIRE = 4
} vino_lifecycle_t;

/* ===== Mint tiers (the four mints of the standard) ===== */
typedef enum {
    MINT_ZERO = 0, /* the ceremonial V0 origin                      */
    FIRST_MINTS_VINO = 1,
    SECOND_MINTS_OIL = 2,
    THIRD_MINTS_BREAD = 3,
    FOURTH_MINT_BUTTER = 4
} mint_tier_t;

/* ===== The Vino voucher — extends the ledger's floating_voucher_t ===== */
typedef struct {
    floating_voucher_t base;               /* the existing double-entry voucher */
    vino_state_t state;                    /* which single rail is active       */
    uint8_t proof_cid[VINO_PROOF_CID_LEN]; /* equity-rail witness (CID) */
    surplus_real_t denomination;           /* Fibonacci-ladder face value       */
    mint_tier_t mint_tier;
    vino_lifecycle_t lifecycle;
    bool counterpart_locked; /* the ghost/twin is cryptolocked    */
    bool in_use;
} vino_voucher_t;

/* ===== The settlement engine ===== */
#define VINO_STORES_MAX_VOUCHERS 256u

typedef struct {
    triple_ledger_t *ledger; /* we post THROUGH this, never around */
    uint32_t vino_account;   /* the ledger account for our rails   */
    vino_voucher_t vouchers[VINO_STORES_MAX_VOUCHERS];
    uint32_t num_vouchers;
    uint64_t next_voucher_id;
} vino_stores_t;

/* ===== Lifecycle ===== */

/* Bind the engine to a triple_ledger and open its rail account. */
void vino_stores_init(vino_stores_t *vs, triple_ledger_t *ledger);

/* Register a Vino voucher (single-active-state, counterpart locked). The
 * proof_cid is SUPPLIED (ops boundary — pinned externally, never invented).
 * Returns the new voucher id (>0) via *out_id, or a VINO_ERR_* code. */
int32_t vino_mint(vino_stores_t *vs, uint64_t *out_id, surplus_real_t denomination,
                  mint_tier_t tier, vino_state_t initial_state,
                  const uint8_t proof_cid[VINO_PROOF_CID_LEN]);

/* THE choke-point. Atomic 3-rail post (Debit:Credit:Equity) through the triple
 * ledger, gated on coverage >= 1.8x AND equity >= 0, with a usury veto. On any
 * failure the entire event ROLLS BACK — no partial write. `equity` is the
 * claimed live-equity delta; claiming more than (debit - credit) implies
 * interest and is refused via onepolicy. proof_cid re-anchors the equity rail. */
int32_t vino_ledger_act(vino_stores_t *vs, uint64_t voucher_id, vino_lifecycle_t to,
                        surplus_real_t debit, surplus_real_t credit, surplus_real_t equity,
                        const uint8_t proof_cid[VINO_PROOF_CID_LEN]);

/* Flip the active rail, re-anchoring the SAME proof_cid; the counterpart stays
 * locked so exactly one rail is ever spendable. */
int32_t vino_swap_to_physical(vino_stores_t *vs, uint64_t voucher_id);
int32_t vino_swap_to_digital(vino_stores_t *vs, uint64_t voucher_id);

/* True iff the voucher is spendable on exactly one rail (counterpart locked). */
bool vino_single_active_state_ok(const vino_voucher_t *v);

/* True iff `rail` is the one active rail of this voucher. A spend attempt on
 * the locked rail must fail — this is how the ghost never double-spends. */
bool vino_can_spend(const vino_voucher_t *v, vino_state_t rail);

/* Find a registered voucher by id (NULL if absent). */
vino_voucher_t *vino_find(vino_stores_t *vs, uint64_t voucher_id);

/* ===== Pure geometry helpers (no ledger, no state) ===== */

/* Bounded forward-equity draw: equity * (φ − 1) ≈ 0.618 * equity. Never interest. */
surplus_real_t vino_phi_draw_max(surplus_real_t equity);

/* Fibonacci denomination ladder: rung 0 → V0 ceremonial (0), rung 1..12 →
 * 1,2,3,5,8,13,21,34,55,89,144,233. Returns 0 for out-of-range rungs. */
uint64_t vino_ladder_denomination(uint32_t rung);
#define VINO_LADDER_RUNGS 12u

/* The 112% Gratuity geometry: split realized `yield` into 11/11/11/66/1 plus a
 * 12% Shiva buffer. Writes six parts to out[0..5]; they sum to 112% of yield.
 * Returns VINO_OK, or VINO_ERR_NULL. */
int32_t vino_gratuity_112(surplus_real_t yield, surplus_real_t out[6]);

#endif /* ZXV_VINO_STORES_H */
