/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* vswap.h — the Player Card swap: liquid Vino <-> V-Bills ("V-Notes"), lock
 * tiers, and the V-Bill dividend distribution, on pay_ledger.
 *
 * OWNER REQUEST (2026-10-11, paraphrased): a user can swap idle liquid Vino
 * into V-Notes to receive a share of the protocol dividend, and swap back on
 * demand for spending; the swap is exact-rational against the reserve ratio.
 * Positions can be held in deeper lock tiers (the owner's word is
 * "compression" tiers; here it only ever means lock depth) for a larger
 * dividend weight. PROJECT DECISIONS: V-Notes ARE the existing V-Bills
 * (perpetual musharakah-style equity paid the fee's 25% dividend share,
 * pay_assure.h B1) pending the owner's confirmation. NO USURY: the return is a
 * variable share of REALISED fee surplus actually sitting in the dividend
 * pool, never a fixed, promised or time-based rate. There is no rate field
 * anywhere and nothing grows with time or with a balance. Vault-held V-Bills
 * are excluded from the redemption base. Closed clearing by default.
 *
 *   W1 SWITCH. Swapping in is OFF by default. vswap_enable takes a
 *      jurisdiction manifest (jurisdiction code, enable flag, epoch) signed by
 *      the configured jurisdiction gateway key, checked by the caller's
 *      verify hook (ML-DSA in production); the code must be the one this
 *      instance was configured for and the epoch must be above the last one,
 *      so a later manifest can disable it again. While disabled, swap-in,
 *      lock and dividend return VSWAP_ERR_DISABLED. LEAVING IS NEVER BLOCKED:
 *      swap-out, exit notice and unlock work whatever the switch says.
 *   W2 RATE. Redemption base B = N - h: N is the V-Bills outstanding (the
 *      issuer account's CREDIT), h the vault's holding (DEBIT of the vault
 *      V-Bill account, or 0 with no vault). Reserve R = DEBIT of the reserve
 *      Vino account. When B == 0 or R == 0 the swap-in rate is the configured
 *      par num/den (Vino units per V-Bill); otherwise the rate is R/B exactly,
 *      kept as the unreduced pair (R, B). h > N fails closed (ERR_STATE).
 *   W3 SWAP IN (Vino -> V-Bills). At rate rn/rd the user offering a Vino gets
 *          v  = floor(a * rd / rn)            V-Bills, and is charged
 *          a' = ceil(v * rn / rd) <= a        Vino (the rest stays with them).
 *      ONE posting: user Vino -a', reserve +a'; the issuer issues v (CREDIT
 *      +v) to the user's V-Bill account (+v). v == 0 is VSWAP_ERR_DUST and
 *      posts nothing. The ratio never falls: a' * B >= v * R, so
 *      (R + a') * B >= R * (B + v).
 *   W4 SWAP OUT (V-Bills -> Vino). Always at R/B, never at par (par could pay
 *      out more than the reserve backs): the user returns v V-Bills (CREDIT
 *      and DEBIT both -v) and receives o = floor(v * R / B) Vino from the
 *      reserve, in ONE posting. o == 0 (for example R == 0) is DUST. The ratio
 *      never falls: o * B <= v * R, so (R - o) * B >= R * (B - v).
 *      NO GUARANTEE: o can be LESS than the user paid in. If the reserve
 *      shrinks (a loss, a haircut) every V-Bill redeems for less.
 *   W5 DIVIDEND. vswap_dividend pays the WHOLE current balance P of the
 *      dividend-pool Vino account, and nothing when it is empty
 *      (VSWAP_ERR_EMPTY). Each listed holder (a V-Bill account plus the Vino
 *      account it is paid into) has weight units u = balance * weight[LOW];
 *      each open lock position has u = vbills * weight[tier]. The vault, when
 *      listed, is a holder like any other unless exclude_vault is set (then
 *      its units are 0). With U = sum u (must fit 64 bits, else
 *      ERR_OVERFLOW):
 *          share_i = floor(P * u_i / U), and the P - sum share_i < count
 *      leftover units go one each to the largest remainders (P * u_i mod U),
 *      ties to the lower index; holders are indexed first in the order given,
 *      then positions in table order. sum share_i == P exactly.
 *      It pays only what is in the pool (realised); nothing is promised.
 *   W6 QUOTES. vswap_quote_in / vswap_quote_out return what a swap would do
 *      now, without posting and whatever the switch says.
 *
 * LOCK TIERS ("compression" in the owner's word = lock depth only)
 *   T1 LOW is liquid: V-Bills in the user's own V-Bill account, no record, no
 *      lock, redeem any time (notice period 0). vswap_lock moves v V-Bills
 *      from the user's account to the tier's lock account (a ledger transfer)
 *      and opens a position {owner, payee, tier, vbills, reinvest} in a fixed
 *      table of VSWAP_MAX_POS. Positions are PERPETUAL: no end date.
 *   T2 WEIGHTS. weight[LOW, MEDIUM, DEEP] default 1, 2, 3: defaults chosen by
 *      this module, operator-settable in [1, VSWAP_WEIGHT_MAX]. A weight
 *      scales a share of what is in the pool; it is not a rate. REINVEST
 *      (the owner's "compounding"): a position may ask that its dividend
 *      share be swapped in by W3 at the rate before the dividend and added
 *      to the same position (the V-Bills are issued straight to the lock
 *      account); the part of the share that does not buy a whole V-Bill is
 *      paid to its payee. Nothing is promised.
 *   T3 EXIT BY NOTICE. A locked position stays in its tier until its owner
 *      files an exit notice (vswap_notice at tick t). vswap_unlock succeeds
 *      at tick >= t + period[tier] and returns every V-Bill of the position
 *      to the owner's V-Bill account; before that, or without a notice, it is
 *      VSWAP_ERR_LOCKED. period[MEDIUM] = F(k_medium) and period[DEEP] =
 *      F(k_deep) phase-ticks, Fibonacci numbers from the operator's choice of
 *      k in [VSWAP_FIB_KMIN, VSWAP_FIB_KMAX], with DEEP strictly the longest.
 *      Nothing is forfeited, ever. DECISION: during the notice period the
 *      position keeps its tier weight (and its reinvest flag) until unlock.
 *      Locked V-Bills cannot be swapped out: they are not in the owner's
 *      account, and a lock account is refused as a swap party.
 *   T4 EPOCH CHECK. vswap_epoch_check re-verifies that for each locked tier
 *      the sum of its positions equals the lock account's ledger balance, and
 *      that pay_ledger_check passes. Deep tiers are audited exactly like
 *      shallow ones: a tier is a lock length plus the state kept. The only
 *      saving claimed is that a locked position makes no transitions between
 *      epochs; nothing is claimed about entropy or compression of data.
 *
 * POSTINGS AND BATCHING. A swap, a lock and an unlock are each ONE atomic
 * posting. A dividend first posts ONE reinvest posting (pool -sum a', reserve
 * +sum a', issuer CREDIT +sum v, lock accounts +v per tier) when any position
 * reinvests, then the Vino payouts in batches of at most PAY_MAX_LINES - 1
 * payees, each batch one posting with the pool line. Everything is checked
 * against the ledger before the first posting; should a later posting still
 * be refused (VSWAP_ERR_PARTIAL), the earlier ones stand and paid[] and each
 * position's last_vino / last_vbills say exactly who was paid what.
 * Identifiers are SHA-256(seed || tag || seq) as in vhub; e2e is VSWAP-<seq>.
 *
 * HONEST LIMITS. A redeemable instrument paying a share of protocol revenue
 * is likely a security, a collective investment scheme or a deposit-like
 * product in many jurisdictions, whether or not a company holds custody;
 * that is why swapping in ships OFF behind the jurisdiction switch, and a
 * switch is not a legal opinion. Exact arithmetic removes rounding runs, not
 * market or reserve risk: a V-Bill can redeem for less than it cost, and the
 * dividend can be zero. At par (B == 0 or R == 0) the first buyer pays par
 * whatever the reserve holds. Signatures are only as good as the verify hook.
 * Freestanding C11: no allocation, no float.
 */
#ifndef ZXV_VSWAP_H
#define ZXV_VSWAP_H

#include <stdbool.h>
#include <stdint.h>
#include "../pay/pay_ledger.h"

#define VSWAP_NONE       0xFFFFFFFFu
#define VSWAP_MAX_POS    64u
#define VSWAP_MAX_HOLD   64u
#define VSWAP_WEIGHT_MAX 1000u
#define VSWAP_FIB_KMIN   1u
#define VSWAP_FIB_KMAX   90u                  /* F(90) < 2^62 */
#define VSWAP_LINE_MAX   ((uint64_t) 1 << 59) /* pay_ledger's per-line limit */

typedef enum {
    VSWAP_OK = 0,
    VSWAP_ERR_ARG = -1,
    VSWAP_ERR_DISABLED = -2, /* W1 */
    VSWAP_ERR_SIG = -3,      /* W1: bad manifest signature */
    VSWAP_ERR_EPOCH = -4,    /* W1: epoch not above the last */
    VSWAP_ERR_JURIS = -5,    /* W1: manifest for another jurisdiction */
    VSWAP_ERR_DUST = -6,     /* W3/W4: nothing whole to give */
    VSWAP_ERR_FUNDS = -7,    /* the payer does not hold enough */
    VSWAP_ERR_OVERFLOW = -8,
    VSWAP_ERR_STATE = -9,   /* h > N, a broken tier sum, a bad ledger */
    VSWAP_ERR_LEDGER = -10, /* the ledger refused (see last_ledger) */
    VSWAP_ERR_EMPTY = -11,  /* W5: the dividend pool is empty */
    VSWAP_ERR_NOHOLD = -12, /* W5: no weight to pay to */
    VSWAP_ERR_FULL = -13,   /* T1: position table full */
    VSWAP_ERR_LOCKED = -14, /* T3: no notice, or the notice period runs */
    VSWAP_ERR_PARTIAL = -15 /* W5: a later batch was refused */
} vswap_status_t;

typedef enum {
    VSWAP_TIER_LOW = 0,
    VSWAP_TIER_MEDIUM = 1,
    VSWAP_TIER_DEEP = 2,
    VSWAP_TIERS = 3
} vswap_tier_t;

/* verify(ctx, key, msg, len, sig, sig_len): true when sig is key's */
typedef bool (*vswap_verify_fn)(void *ctx, uint32_t key, const uint8_t *msg, uint32_t len,
                                const uint8_t *sig, uint32_t sig_len);

typedef struct {
    uint16_t vino, vbill;       /* ledger assets */
    uint32_t reserve;           /* reserve Vino account (R) */
    uint32_t issuer;            /* V-Bill issuer account (N), ISSUER flag */
    uint32_t vault;             /* vault V-Bill account (h) or VSWAP_NONE */
    uint32_t lock[VSWAP_TIERS]; /* lock[MEDIUM], lock[DEEP]: V-Bill accounts;
                                   lock[LOW] is unused (VSWAP_NONE) */
    uint32_t gateway;           /* jurisdiction gateway key id (verify) */
    uint32_t juris;             /* jurisdiction code this instance serves */
    uint64_t par_num, par_den;  /* W2 par: par_num Vino per par_den V-Bills */
    uint32_t weight[VSWAP_TIERS];
    uint32_t k_medium, k_deep; /* T3 Fibonacci indices */
    vswap_verify_fn verify;
    void *vctx;
    uint8_t seed[32];
} vswap_cfg_t;

typedef struct {
    bool used;
    bool reinvest;
    bool noticed;
    uint8_t tier;
    uint32_t owner;  /* the owner's V-Bill account (V-Bills return here) */
    uint32_t payee;  /* the Vino account dividends are paid into */
    uint64_t vbills; /* V-Bills in the position */
    uint64_t opened, notice_tick;
    uint64_t last_vino, last_vbills; /* the last dividend: paid / reinvested */
} vswap_pos_t;

typedef struct {
    pay_ledger_t *L;
    vswap_cfg_t cfg;
    uint64_t period[VSWAP_TIERS]; /* T3 notice periods, in ticks */
    bool enabled;
    uint64_t switch_epoch;
    vswap_pos_t pos[VSWAP_MAX_POS];
    uint64_t seq, posted;
    pay_status_t last_ledger;
} vswap_t;

typedef struct {
    uint64_t num, den; /* the rate used: Vino per V-Bill = num/den */
    bool par;
    uint64_t vbills; /* V-Bills issued (in) or returned (out) */
    uint64_t vino;   /* Vino charged (in) or paid (out) */
} vswap_quote_t;

typedef struct {
    uint32_t vbill; /* the holder's V-Bill account (its weight) */
    uint32_t payee; /* the Vino account it is paid into */
} vswap_holder_t;

typedef struct {
    uint64_t pool;     /* P */
    uint64_t units;    /* U */
    uint64_t paid;     /* Vino paid out to payees */
    uint64_t reinvest; /* Vino swapped in for reinvesting positions */
    uint64_t vbills;   /* V-Bills issued to reinvesting positions */
    uint32_t postings; /* postings that committed */
} vswap_div_t;

/* Defaults: no vault, no lock accounts, par 1/1, weights 1/2/3, k_medium 12
 * (F = 144 ticks), k_deep 20 (F = 6765 ticks). Accounts must still be set. */
void vswap_cfg_default(vswap_cfg_t *c);
/* Check the configuration against the ledger and start (switch OFF). */
vswap_status_t vswap_init(vswap_t *s, pay_ledger_t *L, const vswap_cfg_t *c);

/* F(k): F(1) = F(2) = 1; 0 for k == 0 or k > VSWAP_FIB_KMAX. */
uint64_t vswap_fib(uint32_t k);

/* W1: "zxv/vswap-sw/v1" || juris (4, LE) || enable (1) || epoch (8, LE). */
#define VSWAP_SWITCH_MSG (15u + 4u + 1u + 8u)
void vswap_switch_msg(uint32_t juris, bool enable, uint64_t epoch, uint8_t out[VSWAP_SWITCH_MSG]);
vswap_status_t vswap_enable(vswap_t *s, uint32_t juris, bool enable, uint64_t epoch,
                            const uint8_t *sig, uint32_t sig_len);

/* W2: the swap-in rate now (q->vbills, q->vino = 0). */
vswap_status_t vswap_rate(const vswap_t *s, vswap_quote_t *q);
/* W6 */
vswap_status_t vswap_quote_in(const vswap_t *s, uint64_t a, vswap_quote_t *q);
vswap_status_t vswap_quote_out(const vswap_t *s, uint64_t v, vswap_quote_t *q);
/* W3 / W4. q (optional) gets what was done. */
vswap_status_t vswap_swap_in(vswap_t *s, uint32_t user_vino, uint32_t user_vbill, uint64_t a,
                             uint64_t tick, vswap_quote_t *q);
vswap_status_t vswap_swap_out(vswap_t *s, uint32_t user_vbill, uint32_t user_vino, uint64_t v,
                              uint64_t tick, vswap_quote_t *q);

/* T1: lock v of owner's V-Bills in tier MEDIUM or DEEP. *id gets the slot. */
vswap_status_t vswap_lock(vswap_t *s, uint32_t owner, uint32_t payee, uint8_t tier, uint64_t v,
                          bool reinvest, uint64_t tick, uint32_t *id);
/* T3 */
vswap_status_t vswap_notice(vswap_t *s, uint32_t id, uint32_t owner, uint64_t tick);
vswap_status_t vswap_unlock(vswap_t *s, uint32_t id, uint32_t owner, uint64_t tick);

/* W5 + T2. paid (optional, n entries) gets each holder's Vino; out optional. */
vswap_status_t vswap_dividend(vswap_t *s, uint32_t pool, const vswap_holder_t *h, uint32_t n,
                              bool exclude_vault, uint64_t tick, uint64_t *paid, vswap_div_t *out);

/* T4 */
vswap_status_t vswap_epoch_check(const vswap_t *s);

#endif /* ZXV_VSWAP_H */
