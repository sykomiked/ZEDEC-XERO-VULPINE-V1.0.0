/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* pay_assure.h — the 0.08889% assurance fee, its four-bucket partition, the
 * per-account sub-unit carry, user contributions above the fee (VFV store
 * credit), and the network commons pool.
 *
 * OWNER DECISION 2026-10-10: this fee REPLACES the former phi-percent tithe
 * (floor(a * phi / 100), about 1.618%) everywhere. The phi formula and its
 * 192-bit square root are gone from the tree; nothing else needed them.
 *
 * THE FEE (exact, integer only)
 * -----------------------------
 *   F1  For a gross amount g >= 0 in the unit's own minor measure (money minor
 *       units, compute token-cycles, bandwidth bytes, storage byte-hours):
 *           fee(g) = floor(g * 8889 / 10,000,000)       (0.08889%)
 *       g * 8889 is a full 64x64 -> 128-bit product built from 64-bit limbs
 *       (pay_mul64: no __int128) and the division is 128/64 long division
 *       (pay_udiv128_64: no 64-bit hardware or libgcc division). fee(g) < g
 *       always fits 64 bits, for every g up to UINT64_MAX.
 *       fee(1124) = 0 and fee(1125) = 1: 1125 minor units is the smallest
 *       gross that pays one whole unit without a carry.
 *   F2  CARRY. The sub-unit remainder is not lost. With a per-account
 *       remainder r in [0, 10^7):
 *           x = g * 8889 + r,  fee = floor(x / 10^7),  r' = x mod 10^7.
 *       Over any sequence of charges on one account the total fee equals
 *       floor((sum g) * 8889 / 10^7) exactly (starting from r = 0), so many
 *       micro-payments pay what one payment of their sum would pay.
 *       pay_ledger keeps r per account (pay_account_t.fee_carry) and commits
 *       it only when the posting that carries the fee commits.
 *   F3  ONCE PER TRANSACTION. The fee applies to the gross of the payment,
 *       never to a fee, never to itself (no compounding). Modules that
 *       release a sale in parts (kernel/src/market) charge
 *       fee(cumulative base) - fee already charged, which is the same carry
 *       rule kept per order.
 *
 * THE FOUR BUCKETS (exact partition)
 * ----------------------------------
 *   B1  50% reserve floor (backs Vino), 25% V-Bill dividend pool
 *       (perpetual-equity holders; the V-Bill module is future work, so for
 *       now this is only a named pool account), 15% infrastructure / node
 *       bounties, 10% regenerative capital. Each share is floor(fee * pct /
 *       100) and every remainder unit goes to the reserve floor, so the four
 *       parts always sum to the fee exactly and none exceeds it.
 *       (The SLA solvency bond is NOT a fee bucket; it is to be funded later
 *       by slashed dispute bonds and salvage-auction proceeds.)
 *
 * CONTRIBUTIONS
 * -------------
 *   C1  A user picks a contribution: the fee itself (default), an exact
 *       rational fraction num/den >= 0 of the amount (floor), or an absolute
 *       amount in the same unit.
 *   C2  By default the fee is the floor: a contribution below it is raised to
 *       it. An operator may set allow_below_fee, in which case a smaller
 *       contribution is accepted and the shortfall is recorded (no credit).
 *   C3  Excess = contribution - fee (when positive) earns VFV store credit:
 *           vfv = floor(floor(excess * rate.num / rate.den) * mult.num / mult.den)
 *       where `rate` is the POSTED rate in VFV minor units per minor unit of
 *       the contributed unit and `mult` is the operator's credit multiplier.
 *       DEFAULTS: money rate 1/1, compute / bandwidth / storage rate UNSET
 *       (0/1: the excess is still taken and recorded but earns no VFV until a
 *       rate is posted), multiplier 1/1. Every rounding is a floor.
 *       A voluntary excess is not part of the fee; pay_ledger posts it to the
 *       reserve-floor bucket on top of the fee's own reserve share.
 *
 * THE COMMONS
 * -----------
 *   K1  Non-money fee proceeds (compute, bandwidth, storage) are quantities
 *       held in pay_commons_t.
 *   K2  The commons is allocated by the cooperative no-monopoly rule of
 *       docs/SWARM_ECONOMY.md section 6 (swarm_capped_split, exact integers).
 *       What nobody may take stays in the pool.
 *
 * HONEST LIMITS. Schema validity is not certification; there is no SWIFT or
 * CIPS connectivity here; operating as a bank or money transmitter needs
 * licences; how a fee, a contribution, a reserve or a dividend pool is treated
 * for tax, securities or consumer-protection law is a question for counsel.
 * Freestanding: no libc, no allocation, no floating point.
 */
#ifndef ZXV_PAY_ASSURE_H
#define ZXV_PAY_ASSURE_H

#include <stdint.h>
#include <stdbool.h>
#include "pay_util.h"

/* ===== F1-F3: the fee ===== */
#define PAY_ASSURE_NUM 8889u     /* 0.08889% = 8889 / 10^7 */
#define PAY_ASSURE_DEN 10000000u /* 10^7                  */

/* F1: floor(g * 8889 / 10^7), no carry. Exact for every g in [0, 2^64). */
uint64_t pay_assure_fee(uint64_t gross);

/* F2 (pure): the fee on `gross` with incoming remainder `rem_in`. Returns
 * false (outputs untouched) if rem_in >= 10^7 (a corrupt carry). */
bool pay_assure_fee_carry(uint64_t rem_in, uint64_t gross, uint64_t *fee, uint64_t *rem_out);

typedef struct {
    uint64_t rem; /* sub-unit remainder, always < PAY_ASSURE_DEN */
} pay_assure_carry_t;

/* F2 (stateful): charge `gross` against carry `c`. On false nothing changes. */
bool pay_assure_charge(pay_assure_carry_t *c, uint64_t gross, uint64_t *fee);

/* ===== B1: the four buckets ===== */
typedef enum {
    PAY_ASSURE_RESERVE_FLOOR = 0,  /* 50% + every remainder unit: backs Vino */
    PAY_ASSURE_VBILL_DIVIDEND = 1, /* 25%: V-Bill dividend pool              */
    PAY_ASSURE_INFRA_BOUNTY = 2,   /* 15%: infrastructure / node bounties    */
    PAY_ASSURE_REGEN_CAPITAL = 3,  /* 10%: regenerative capital              */
    PAY_ASSURE_BUCKETS = 4
} pay_assure_bucket_t;

/* Percent share of each bucket; sums to 100 (static-asserted in the .c). */
extern const uint8_t pay_assure_bucket_pct[PAY_ASSURE_BUCKETS];
extern const char *const pay_assure_bucket_name[PAY_ASSURE_BUCKETS];

/* B1: out[i] = floor(fee * pct[i] / 100) for i >= 1, out[0] = the rest.
 * sum(out) == fee exactly and every out[i] <= fee. */
void pay_assure_split(uint64_t fee, uint64_t out[PAY_ASSURE_BUCKETS]);

/* ===== Units ===== */
typedef enum {
    PAY_UNIT_MONEY = 0,     /* minor units of a ledger asset              */
    PAY_UNIT_COMPUTE = 1,   /* token-cycles                               */
    PAY_UNIT_BANDWIDTH = 2, /* bytes                                      */
    PAY_UNIT_STORAGE = 3,   /* byte-hours                                 */
    PAY_UNIT_KIND_COUNT = 4
} pay_unit_kind_t;

typedef struct {
    uint64_t num, den;
} pay_rat_t;

typedef enum {
    PAY_CONTRIB_FEE = 0,     /* exactly the fee (default)              */
    PAY_CONTRIB_RATE = 1,    /* floor(amount * rate.num / rate.den)    */
    PAY_CONTRIB_ABSOLUTE = 2 /* a fixed amount in the same unit        */
} pay_contrib_mode_t;

typedef struct {
    pay_contrib_mode_t mode;
    pay_rat_t rate;
    uint64_t absolute;
} pay_contrib_t;

typedef struct {
    bool allow_below_fee;                    /* C2 (default false)            */
    pay_rat_t vfv_rate[PAY_UNIT_KIND_COUNT]; /* C3 posted-rate defaults       */
    pay_rat_t credit_mult;                   /* C3 multiplier (default 1/1)   */
    pay_rat_t commons_cap_share;             /* K2 (default 8/21)             */
} pay_assure_policy_t;

typedef enum {
    PAY_ASSURE_OK = 0,
    PAY_ASSURE_RATE_UNSET = 1, /* excess taken, no VFV: rate not posted */
    PAY_ASSURE_ERR_ARG = -1,
    PAY_ASSURE_ERR_OVERFLOW = -2,
    PAY_ASSURE_ERR_RATE = -3 /* zero denominator */
} pay_assure_status_t;

typedef struct {
    uint64_t amount;       /* the gross the fee applies to                  */
    uint64_t fee;          /* floor(amount * 8889 / 10^7) (no carry)        */
    uint64_t contribution; /* what is actually taken                        */
    uint64_t excess;       /* contribution - fee (>= 0)                     */
    uint64_t shortfall;    /* fee - requested, when allow_below_fee          */
    uint64_t vfv_credit;   /* VFV minor units credited to the contributor   */
    pay_assure_status_t status;
} pay_assure_result_t;

/* Defaults described in C2, C3 and K2. */
void pay_assure_policy_default(pay_assure_policy_t *p);

/* Apply the policy. `contrib` NULL means PAY_CONTRIB_FEE. `posted_rate` NULL
 * uses the policy default for the unit. */
pay_assure_status_t pay_assure_compute(const pay_assure_policy_t *p, pay_unit_kind_t unit,
                                       uint64_t amount, const pay_contrib_t *contrib,
                                       const pay_rat_t *posted_rate, pay_assure_result_t *out);

/* ===== The commons pool for non-money units (K1) ===== */
typedef struct {
    uint64_t pool[PAY_UNIT_KIND_COUNT];
    uint64_t received[PAY_UNIT_KIND_COUNT];  /* lifetime                    */
    uint64_t allocated[PAY_UNIT_KIND_COUNT]; /* lifetime                    */
} pay_commons_t;

void pay_commons_init(pay_commons_t *c);
bool pay_commons_deposit(pay_commons_t *c, pay_unit_kind_t unit, uint64_t amount);
/* Conservation: pool == received - allocated for every unit. */
bool pay_commons_conserved(const pay_commons_t *c);

#define PAY_COMMONS_MAX_RECIPIENTS 64u /* SWARM_MAX_MODELS */

/* K2: split `total` by weights w[0..n) with no recipient above
 * max(floor(total * cap.num / cap.den), ceil(total / n_nonzero)). Writes
 * out[0..n); returns what nobody may take. n <= PAY_COMMONS_MAX_RECIPIENTS. */
uint64_t pay_commons_split(uint64_t total, const uint64_t *w, uint32_t n, pay_rat_t cap,
                           uint64_t *out);

/* Allocate the whole pool of `unit` by K2, deducting what was handed out. */
bool pay_commons_allocate(pay_commons_t *c, pay_unit_kind_t unit, const uint64_t *w, uint32_t n,
                          pay_rat_t cap, uint64_t *out);

#endif /* ZXV_PAY_ASSURE_H */
