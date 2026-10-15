/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* allot_ladder.h — many concurrent Sicilian Crown Allotment pots (up to 144
 * levels), each an ordinary allot_t, fed by one payment.
 *
 * LEVELS
 * ------
 *   K1  A ladder holds n levels, 1 <= n <= ALLOT_LADDER_MAX (144). Each
 *       level is a full allotment (allot.h): its own threshold, proportional
 *       odds, share cap, commit-reveal draw and three-way split. Every level
 *       uses the same rate limits and split ratios.
 *   K2  THRESHOLDS. Level j (0 = lowest) closes at
 *           T(j) = F(i) + floor((F(i+1) - F(i)) * r / (n - 1)),
 *           i = idx_lo + floor(j * (idx_hi - idx_lo) / (n - 1)),
 *           r = j * (idx_hi - idx_lo) mod (n - 1),
 *       a Fibonacci curve with straight steps between consecutive Fibonacci
 *       numbers, so the lowest level closes at F(idx_lo), the apex at
 *       F(idx_hi), and every level in between is strictly larger than the
 *       one below. Defaults idx_lo = 13 (233), idx_hi = 40 (102,334,155).
 *       A pure T(k) = F(k) for k up to 144 is NOT used: F(144) is about
 *       5.6e29, which no network reaches, and F(94) already overflows 64
 *       bits. Thresholds count ballots, i.e. fee units (F3 of allot.h).
 *
 * ONE PAYMENT, MANY POTS
 * ----------------------
 *   P1  The fee is computed once (allot.h F2/F5) and split by a weight
 *       vector: share(k) = floor(fee * w(k) / sum(w)). The default vector
 *       is equal weights; a player card may pass its own vector to favour
 *       chosen levels (an override), with any zero weights it likes.
 *   P2  CASCADE UP. A level that cannot take its share now (closed and
 *       awaiting its draw, or open with no committed parties yet, so its
 *       draw could never run) passes it to the next level above. A level
 *       that can take only part of it (allot.h F6: the player's share cap
 *       there) keeps what fits and passes the excess up. The floor
 *       remainders of P1, plus anything that cascaded past the top, go to
 *       the highest level that can take them. If no level can, the payment
 *       is refused.
 *   P3  ALL OR NOTHING. Every planned share is checked first (share cap,
 *       range table, overflow); if any fails the whole payment is refused
 *       and nothing changes. Then each share is minted into its level.
 *   P4  CONSERVATION. The shares minted always sum to the fee.
 *   P5  SPAN. A payment may be confined to levels [lo, hi] (0-based; a
 *       token's tier map, allot_token.h T8). P1-P4 then run on the span
 *       only: weights outside it are ignored, nothing cascades past hi,
 *       and if no level in the span can take the fee the payment is
 *       refused rather than spilling outside it.
 *
 * COST. One payment does at most n share-cap scans and one SHA3-256 chain
 * update per level that receives a share. test_allot prints the measured
 * time per payment at 144 levels.
 *
 * LAW. More pots do not change the legal position in allot.h L1: the
 * ladder only accepts entries while every level's jurisdiction switch is on.
 */
#ifndef ZXV_ALLOT_LADDER_H
#define ZXV_ALLOT_LADDER_H

#include "allot.h"

#define ALLOT_LADDER_MAX         144u
#define ALLOT_LADDER_IDX_LO      13u /* F(13) = 233         */
#define ALLOT_LADDER_IDX_HI      40u /* F(40) = 102,334,155 */
#define ALLOT_LADDER_LUCK_LEVELS 24u /* allot.h W2: luck weight on levels 0..23 */

typedef struct {
    uint32_t n;
    allot_t *lv; /* caller storage: n levels */
    uint64_t weight[ALLOT_LADDER_MAX];
    uint64_t payments;    /* accepted, lifetime          */
    uint64_t fees;        /* fee units minted, lifetime  */
    uint64_t cascaded;    /* fee units moved up by P2    */
    uint32_t luck_levels; /* W2: the luck weight applies below this level */
} allot_ladder_t;

typedef struct {
    uint64_t fee;                        /* total fee of the payment        */
    uint64_t share[ALLOT_LADDER_MAX];   /* fee units per level               */
    uint64_t ballots[ALLOT_LADDER_MAX]; /* ballots minted per level (W1)     */
    uint64_t first[ALLOT_LADDER_MAX];   /* first ballot id per level        */
    uint64_t cascaded;                   /* units moved up by P2             */
    uint32_t closed;                     /* levels this payment closed       */
} allot_ladder_receipt_t;

/* K2: the threshold of level j of n. 0 when the arguments are invalid. */
uint64_t allot_ladder_threshold(uint32_t j, uint32_t n, uint32_t idx_lo, uint32_t idx_hi);

/* K1/K2: open n levels as epoch 1 from `base` (its fib_index and threshold
 * are replaced per level). `ranges` is caller storage for n * per_level
 * runs; `tallies` (optional, NULL = none) for n * tally_per_level slots,
 * which makes the share cap O(1) per level (allot_set_tally). `weights`
 * NULL = equal weights. */
allot_status_t allot_ladder_init(allot_ladder_t *L, const allot_config_t *base, uint32_t n,
                                   uint32_t idx_lo, uint32_t idx_hi, allot_t *levels,
                                   allot_range_t *ranges, uint32_t per_level,
                                   allot_tally_t *tallies, uint32_t tally_per_level,
                                   const uint64_t *weights);

/* P2: can level k take a share now (open and with committed parties)? */
bool allot_ladder_ready(const allot_ladder_t *L, uint32_t k);

/* P1-P3 without state change: the per-level plan for a payment. */
allot_status_t allot_ladder_plan(const allot_ladder_t *L, const uint8_t owner[ALLOT_ID_LEN],
                                   uint64_t amount, const allot_rate_t *rate,
                                   const uint64_t *weights, allot_ladder_receipt_t *plan);

/* P1-P3 for a fee already computed by the caller (e.g. prize-linked
 * savings, allot_token.h T6). */
allot_status_t allot_ladder_plan_fee(const allot_ladder_t *L, const uint8_t owner[ALLOT_ID_LEN],
                                       uint64_t fee, const uint64_t *weights,
                                       allot_ladder_receipt_t *plan);

/* P1-P4: enter a payment into the ladder. `weights` NULL = the ladder's. */
allot_status_t allot_ladder_enter(allot_ladder_t *L, const uint8_t owner[ALLOT_ID_LEN],
                                    uint64_t amount, const allot_rate_t *rate,
                                    const uint64_t *weights, allot_ladder_receipt_t *out);

/* P5: P1-P3 confined to levels [lo, hi]. `luck` (NULL = none) is the
 * player's weight (allot.h W1), applied on levels below luck_levels. */
allot_status_t allot_ladder_plan_span(const allot_ladder_t *L,
                                        const uint8_t owner[ALLOT_ID_LEN], uint64_t fee,
                                        const uint64_t *weights, const allot_rate_t *luck,
                                        uint32_t lo, uint32_t hi, allot_ladder_receipt_t *plan);

/* P1-P5: enter into levels [lo, hi]. `fee` 0 = quote `amount` at `rate`
 * (PAID mode only); otherwise the caller's fee, e.g. a savings tick
 * (amount and rate recorded for audit). `luck` as in plan_span. */
allot_status_t allot_ladder_enter_span(allot_ladder_t *L, const uint8_t owner[ALLOT_ID_LEN],
                                         uint64_t amount, const allot_rate_t *rate, uint64_t fee,
                                         const allot_rate_t *luck, const uint64_t *weights,
                                         uint32_t lo, uint32_t hi, allot_ladder_receipt_t *out);

/* P1-P4 with a fee the caller computed; `amount` and `rate` are recorded in
 * each level's chain for audit. */
allot_status_t allot_ladder_enter_fee(allot_ladder_t *L, const uint8_t owner[ALLOT_ID_LEN],
                                        uint64_t amount, const allot_rate_t *rate, uint64_t fee,
                                        const uint64_t *weights, allot_ladder_receipt_t *out);

#endif /* ZXV_ALLOT_LADDER_H */
