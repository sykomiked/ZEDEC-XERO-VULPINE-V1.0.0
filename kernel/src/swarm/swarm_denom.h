/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* swarm_denom.h — denominations of the swarm's AI tokens: plain tokens,
 *                 compressed tokens and super tokens, with a per-wallet
 *                 "octane" (how dense the tokens it holds may be).
 *
 * WHAT THIS IS
 * ------------
 * swarm_budget.h gives every model an allotment of N tokens per cycle. This
 * module lets that allotment be HELD in several denominations, the way money
 * is held in coins and notes: N plain tokens, or a few dense tokens whose
 * face values add up to exactly N. A dense token is a compressed token; the
 * densest tiers are called super tokens.
 *
 * THE RULES
 * ---------
 *   D1  TIERS.        Tier k (0 .. SWARM_DENOM_TIERS-1) has the face value
 *                     F(k+2): 1, 2, 3, 5, 8, 13, 21, ... (Fibonacci, the same
 *                     ladder as the budget's level rule and Vino's coins).
 *                     Tier 0 is the plain, uncompressed token. Tiers at or
 *                     above SWARM_DENOM_SUPER_TIER are super tokens.
 *
 *   D2  OCTANE.       Every wallet has an octane: the densest tier it may
 *                     hold. Octane 0 holds only plain tokens; a higher octane
 *                     lets the wallet hold denser tokens. The named grades
 *                     (PLAIN, REGULAR, MIDGRADE, PREMIUM, RACING) are tier
 *                     ceilings, like the grades at a fuel pump; any octane
 *                     0 .. SWARM_DENOM_TIERS-1 is allowed.
 *
 *   D3  CANONICAL.    A wallet of value v at octane o is in canonical form
 *                     when the top tier o holds floor(v / F(o+2)) tokens and
 *                     the remainder is written in Zeckendorf form (each lower
 *                     tier holds 0 or 1 token, no two adjacent tiers both
 *                     hold one). Every value has exactly one canonical form,
 *                     found greedily, top tier first.
 *
 *   D4  WRAP/UNWRAP.  Compression is lossless and exact, because
 *                     F(k+2) + F(k+3) = F(k+4):
 *                       fuse(k):  one tier-k + one tier-(k+1) -> one tier-(k+2)
 *                       split(k): one tier-k -> one tier-(k-1) + one tier-(k-2)
 *                                 (tier 1 splits into two plain tokens)
 *                     Tier 0 is atomic. A fuse that would pass the octane
 *                     fails closed.
 *
 *   D5  CONSERVATION. Every operation keeps value = sum count[k] * F(k+2)
 *                     exactly, across all wallets it touches. An operation
 *                     that would overflow 64 bits fails and changes nothing.
 *
 *   D6  NO PREMIUM.   A dense token is worth exactly its face value in plain
 *                     tokens. Compression adds no value, earns no interest,
 *                     costs no fee, and buys no extra allotment or budget
 *                     priority: it is a denomination, not leverage. Raising
 *                     or lowering the octane (going up to racing fuel or back
 *                     down) re-expresses the same value and never changes it.
 *
 *   D7  CHANGE.       Spending or transferring from a wallet returns the
 *                     change in the wallet's canonical form at its octane.
 *                     The receiver gets the payment in canonical form at the
 *                     receiver's own octane, so each system runs at its own
 *                     token density whatever the payer uses.
 *
 *   D8  WIRE CODE.    Amounts travel as Fibonacci codes (Zeckendorf bits of
 *                     v+1, lowest first, then a closing 1): self-delimiting,
 *                     so no length field is needed, and a corrupted run can be
 *                     re-synchronised at the next "11".
 *
 *   D9  LOCKSTEP.     swarm_denom_payout pays each model's remaining allotment
 *                     into its wallet; swarm_denom_consume spends through
 *                     swarm_budget_consume and the wallet together; the
 *                     wallets expire with the cycle (R6). swarm_denom_reconciles
 *                     checks every wallet holds exactly allotted - used.
 *
 * WHAT DENSITY BUYS, AND WHAT IT DOES NOT
 * ---------------------------------------
 * Density cuts the NUMBER OF TOKEN PIECES a wallet holds: 1,000,000 plain
 * tokens are 1,000,000 pieces; at RACING octane they are a handful. That
 * matters wherever tokens are handled as pieces (per-piece receipts,
 * witnesses, signatures, ledger rows). It does not make a model generate
 * faster, does not raise its allotment, and does not shrink an AMOUNT on the
 * wire: the Fibonacci code of a plain amount is already about 1.44 bits per
 * binary digit. docs/TOKEN_DENOMINATIONS.md has the measured numbers.
 *
 * Freestanding: no libc, no allocation, no floating point.
 */
#ifndef SWARM_DENOM_H
#define SWARM_DENOM_H

#include <stdint.h>
#include <stdbool.h>
#include "swarm_budget.h"

#define SWARM_DENOM_TIERS      64u /* faces F(2) .. F(65) */
#define SWARM_DENOM_SUPER_TIER 10u /* face F(12) = 144 and up */

/* The pump grades (D2): octane = the densest tier held. */
#define SWARM_OCTANE_PLAIN    0u  /* face 1: plain tokens only      */
#define SWARM_OCTANE_REGULAR  4u  /* up to face 8                   */
#define SWARM_OCTANE_MIDGRADE 9u  /* up to face 89                  */
#define SWARM_OCTANE_PREMIUM  14u /* up to face 987 (super tokens)  */
#define SWARM_OCTANE_RACING   23u /* up to face 75025               */
#define SWARM_OCTANE_MAX      63u /* up to face F(65)               */

/* The longest Fibonacci code of a 64-bit amount: 92 Zeckendorf bits + 1. */
#define SWARM_DENOM_CODE_MAX_BITS 93u

typedef struct {
    uint32_t model_id;
    uint32_t octane;                   /* densest tier allowed (D2) */
    uint64_t count[SWARM_DENOM_TIERS]; /* tokens held per tier      */
} swarm_wallet_t;

/* D1: face value of tier k, F(k+2); 0 if k is out of range. */
uint64_t swarm_denom_face(uint32_t tier);

/* D1: true for the super-token tiers. */
bool swarm_denom_is_super(uint32_t tier);

/* An empty wallet at the given octane. */
swarm_status_t swarm_denom_init(swarm_wallet_t *w, uint32_t model_id, uint32_t octane);

/* D5: total value in plain tokens. False (and *out = 0) on 64-bit overflow. */
bool swarm_denom_value(const swarm_wallet_t *w, uint64_t *out);

/* Number of token pieces held (sum of counts), saturating at UINT64_MAX. */
uint64_t swarm_denom_pieces(const swarm_wallet_t *w);

/* D3: true if the wallet is in canonical form at its octane. */
bool swarm_denom_is_canonical(const swarm_wallet_t *w);

/* D3: replace the contents with the canonical form of `value`. */
swarm_status_t swarm_denom_set(swarm_wallet_t *w, uint64_t value);

/* D3: rewrite the wallet into canonical form; value unchanged. */
swarm_status_t swarm_denom_compress(swarm_wallet_t *w);

/* D4: one fuse or split. Fails (wallet unchanged) if the pieces are missing,
 * the tier is out of range, or a fuse would pass the octane. */
swarm_status_t swarm_denom_fuse(swarm_wallet_t *w, uint32_t tier);
swarm_status_t swarm_denom_split(swarm_wallet_t *w, uint32_t tier);

/* D2/D6: change the octane and re-express the same value canonically.
 * Up = denser ("racing fuel"), down = plainer. Value never changes. */
swarm_status_t swarm_denom_set_octane(swarm_wallet_t *w, uint32_t octane);

/* D7: spend `amount` out of the wallet (it is gone, e.g. burned by a model's
 * generation). Fails if the wallet holds less. */
swarm_status_t swarm_denom_spend(swarm_wallet_t *w, uint64_t amount);

/* D5/D7: move `amount` from one wallet to another. Fails, changing neither,
 * if `from` holds less or `to` would overflow. from == to is a no-op check. */
swarm_status_t swarm_denom_transfer(swarm_wallet_t *from, swarm_wallet_t *to, uint64_t amount);

/* D8: Fibonacci code of v (v <= UINT64_MAX - 1) appended at bit *pos of buf
 * (cap bits long, LSB-first within each byte). Advances *pos. Fails without
 * writing past cap. */
swarm_status_t swarm_denom_code_put(uint8_t *buf, uint32_t cap, uint32_t *pos, uint64_t v);

/* D8: read one Fibonacci code from bit *pos of buf (cap bits). Fails on a
 * truncated code or a value past 64 bits; *pos is advanced only on success. */
swarm_status_t swarm_denom_code_get(const uint8_t *buf, uint32_t cap, uint32_t *pos, uint64_t *v);

/* D9: pay each active or parked slot's remaining allotment (allotted - used)
 * into wallets[i] (slot order, n == b->num_slots), at each wallet's octane.
 * The wallet's model_id is set from the slot. */
swarm_status_t swarm_denom_payout(const swarm_budget_t *b, swarm_wallet_t *wallets, uint32_t n);

/* D9: spend through the budget and the model's wallet together. */
swarm_status_t swarm_denom_consume(swarm_budget_t *b, swarm_wallet_t *wallets, uint32_t n,
                                   uint32_t model_id, uint64_t requested, uint64_t *granted);

/* D9: true iff wallets[i] belongs to slot i and holds exactly allotted - used
 * of it, for every slot. */
bool swarm_denom_reconciles(const swarm_budget_t *b, const swarm_wallet_t *wallets, uint32_t n);

/* D9 + R6: end the budget cycle and empty every wallet (octanes kept). */
swarm_status_t swarm_denom_end_cycle(swarm_budget_t *b, swarm_wallet_t *wallets, uint32_t n);

#endif /* SWARM_DENOM_H */
