/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* pay_tithe.h — the exact golden-ratio-percent tithe, user contributions,
 * VFV store credit for contributions above the tithe, and the network
 * commons pool.
 *
 * THE TITHE (exact, integer only)
 * -------------------------------
 *   T1  For an amount a >= 0 in the unit's own minor measure (money minor
 *       units, compute token-cycles, bandwidth bytes, storage byte-hours):
 *           tithe(a) = floor(a * phi / 100),  phi = (1 + sqrt 5) / 2
 *                    = floor((a + isqrt(5 a^2)) / 200).
 *       Exactness: a*sqrt5 is irrational for a > 0, and floor(x / n) =
 *       floor(floor(x) / n) for integer n > 0, so floor((a + a*sqrt5)/200) =
 *       floor((a + floor(a*sqrt5))/200) and floor(a*sqrt5) = isqrt(5a^2).
 *       5a^2 needs up to 131 bits for a < 2^64, so the square root runs on
 *       three 64-bit limbs (pay_isqrt192); pay_isqrt128 is the two-limb form.
 *       No float, no __int128, no 64-bit division (128/64 long division).
 *   T2  The rate is phi percent = 1.6180339887...%, NOT 61.8% and NOT phi.
 *
 * CONTRIBUTIONS
 * -------------
 *   C1  A user picks a contribution: the tithe itself (default), an exact
 *       rational fraction num/den >= 0 of the amount (floor), or an absolute
 *       amount in the same unit.
 *   C2  By default the tithe is the floor: a contribution below it is raised
 *       to it. An operator may set allow_below_phi, in which case a smaller
 *       contribution is accepted and the shortfall is recorded (no credit).
 *   C3  Excess = contribution - tithe (when positive) earns VFV store credit:
 *           vfv = floor(floor(excess * rate.num / rate.den) * mult.num / mult.den)
 *       where `rate` is the POSTED rate in VFV minor units per minor unit of
 *       the contributed unit and `mult` is the operator's credit multiplier.
 *       DEFAULTS: money rate 1/1 (1:1 in value, which assumes the contributed
 *       asset is VFV-denominated or the operator posts the real rate for each
 *       asset), compute / bandwidth / storage rate UNSET (0/1: the excess is
 *       still taken and recorded but earns no VFV until a rate is posted),
 *       multiplier 1/1. Every rounding is a floor, i.e. in favour of the
 *       commons and never minting a fraction.
 *
 * THE COMMONS
 * -----------
 *   K1  Tithe proceeds (the full contribution, tithe plus excess) go to the
 *       network commons pool of that unit. Money tithes are posted to the
 *       commons account of that asset in pay_ledger; compute and bandwidth
 *       tithes are quantities held in pay_commons_t.
 *   K2  The commons is allocated by the existing cooperative no-monopoly rule
 *       of docs/SWARM_ECONOMY.md section 6: a proportional split where no
 *       recipient may take more than max(cap_share of the total, an equal
 *       share). This calls swarm_capped_split from kernel/src/swarm/
 *       swarm_market.c (water-filling, exact integers). What nobody may take
 *       stays in the pool.
 *
 * HONEST LIMITS. Schema validity is not certification; there is no SWIFT or
 * CIPS connectivity here; operating as a bank or money transmitter needs
 * licences; whether VFV is "store credit" (and how a tithe or contribution is
 * treated for tax, charity or consumer-protection law) is a legal question
 * for counsel. Freestanding: no libc, no allocation, no floating point.
 */
#ifndef ZXV_PAY_TITHE_H
#define ZXV_PAY_TITHE_H

#include <stdint.h>
#include <stdbool.h>
#include "pay_util.h"

/* ===== Wide square roots ===== */
typedef struct {
    uint64_t w[3]; /* w[0] least significant */
} pay_u192;

/* floor(sqrt(v)) of a 192-bit value; the result fits in 96 bits and is
 * returned as a 128-bit value. */
pay_u128 pay_isqrt192(pay_u192 v);

/* floor(a * sqrt(5)) exactly (= isqrt(5 a^2)); fits in 128 bits. */
pay_u128 pay_floor_a_sqrt5(uint64_t a);

/* T1: floor(a * phi / 100). Exact for every a in [0, 2^64). */
uint64_t pay_tithe_phi(uint64_t a);

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
    PAY_CONTRIB_PHI = 0,     /* exactly the tithe (default)            */
    PAY_CONTRIB_RATE = 1,    /* floor(amount * rate.num / rate.den)    */
    PAY_CONTRIB_ABSOLUTE = 2 /* a fixed amount in the same unit        */
} pay_contrib_mode_t;

typedef struct {
    pay_contrib_mode_t mode;
    pay_rat_t rate;
    uint64_t absolute;
} pay_contrib_t;

typedef struct {
    bool allow_below_phi;                    /* C2 (default false)            */
    pay_rat_t vfv_rate[PAY_UNIT_KIND_COUNT]; /* C3 posted-rate defaults       */
    pay_rat_t credit_mult;                   /* C3 multiplier (default 1/1)   */
    pay_rat_t commons_cap_share;             /* K2 (default 8/21)             */
} pay_tithe_policy_t;

typedef enum {
    PAY_TITHE_OK = 0,
    PAY_TITHE_RATE_UNSET = 1, /* excess taken, no VFV: rate not posted */
    PAY_TITHE_ERR_ARG = -1,
    PAY_TITHE_ERR_OVERFLOW = -2,
    PAY_TITHE_ERR_RATE = -3 /* zero denominator */
} pay_tithe_status_t;

typedef struct {
    uint64_t amount;       /* the amount the tithe applies to               */
    uint64_t tithe;        /* floor(amount * phi / 100)                    */
    uint64_t contribution; /* what is actually taken for the commons        */
    uint64_t excess;       /* contribution - tithe (>= 0)                   */
    uint64_t shortfall;    /* tithe - requested, when allow_below_phi        */
    uint64_t vfv_credit;   /* VFV minor units credited to the contributor   */
    pay_tithe_status_t status;
} pay_tithe_result_t;

/* Defaults described in C2, C3 and K2. */
void pay_tithe_policy_default(pay_tithe_policy_t *p);

/* Apply the policy. `contrib` NULL means PAY_CONTRIB_PHI. `posted_rate` NULL
 * uses the policy default for the unit. */
pay_tithe_status_t pay_tithe_compute(const pay_tithe_policy_t *p, pay_unit_kind_t unit,
                                     uint64_t amount, const pay_contrib_t *contrib,
                                     const pay_rat_t *posted_rate, pay_tithe_result_t *out);

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

#endif /* ZXV_PAY_TITHE_H */
