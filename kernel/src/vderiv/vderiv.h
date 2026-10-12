/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* vderiv.h — V-Calls and V-Perps on V-Bill auction clearing velocity, on
 * pay_ledger.
 *
 * OWNER REQUEST (2026-10-11, paraphrased): V-Calls and V-Perps are
 * exact-rational instruments that let users hedge or speculate on how fast
 * V-Bills clear at auction, without touching fiat or any external feed, and
 * that route excess speculative volatility into the solvency vault's reserve
 * buffer. PROJECT DECISIONS: derivatives are the most heavily regulated class
 * (CFTC/SEC-type rules), so the whole module ships OFF per jurisdiction behind
 * a signed manifest switch and is never enabled by default. LEVERAGE IS OFF:
 * every position is fully collateralised up front with its maximum possible
 * loss; there is no margin call, no liquidation engine, no borrowing. NO
 * USURY: there is no funding rate and nothing accrues or grows with time or
 * with a balance. Closed clearing: every position is bilateral between two
 * signed parties; there is no order book. Integer and exact-rational only.
 *
 *   V0 INDEX. The underlying is computed from auction epochs only: the
 *      velocity of epoch e is I(e) = qty(e), the number of V-Bills issued at
 *      that epoch's clearing (an exact integer; the clearing price is kept
 *      beside it for audit but is not part of the index). vderiv_index_push
 *      appends (epoch, price, qty) to a ring of VDERIV_RING epochs. Epochs
 *      are pushed once and in order: the first push must be cfg.first_epoch
 *      and each later one exactly the last plus one (no gaps, so no expiry
 *      can be skipped); anything else is VDERIV_ERR_EPOCH. Pushing works
 *      whatever the switch says: the index is data, not an offer.
 *   V1 V-CALL (capped call, fully funded). Terms: strike K, cap C > K, size
 *      n > 0, expiry epoch, premium. Payoff at expiry with I = I(expiry):
 *          payoff = n * (min(I, C) - K)   if I > K,   else 0
 *      in Vino minor units, never above n * (C - K). Opening is ONE posting:
 *      the buyer pays the premium to the writer (less V3's fee) and the
 *      writer locks lock = n * (C - K) in the call escrow account. Both
 *      parties sign the terms. When the expiry epoch is pushed its index is
 *      recorded as the call's fixing (no posting); vderiv_call_settle then
 *      pays, in ONE posting, payoff (less V3's fee) to the buyer and
 *      lock - payoff back to the writer, so the escrow's share of this call
 *      ends at zero exactly. Anyone may trigger a settlement; it only pays
 *      the two signed accounts per the signed terms.
 *   V2 V-PERP (no expiry, fully collateralised, capped). Terms: size n > 0,
 *      band > 0 (the largest index move settled per epoch), and the two
 *      locks lock_long, lock_short, each at least n * band (DECISION: a side
 *      may lock more than n * band so that one loss does not end the
 *      position; the brief's minimum is n * band). Opening is ONE posting:
 *      the long locks lock_long in the long escrow account and the short
 *      locks lock_short in the short escrow account; the reference index is
 *      the last pushed one. On each push, every open perp settles
 *          v = n * min(|I_t - I_{t-1}|, band)
 *      from the loser's escrow balance to the winner's (long wins when the
 *      index rose), less V3's fee on v. An open perp's sides each hold at
 *      least n * band before a move, so no balance can go negative. If after
 *      the move a side holds less than n * band, the position STOPS at that
 *      epoch (auto-close: no further moves, no penalty, no fee beyond V3).
 *      All perps of one push settle in ONE posting (long escrow, short
 *      escrow, reserve); if the ledger refuses it the push is refused and
 *      nothing changes. vderiv_perp_close pays both sides their balances in
 *      ONE posting: for an open perp either side may ask, with its signature
 *      over (position nonce, side); a stopped perp may be paid out by anyone.
 *      Every moment between two pushes is an epoch boundary. There is NO
 *      funding rate and nothing accrues with time.
 *   V3 VOLATILITY ROUTING. The 0.08889% assurance fee, pay_assure_fee (F1,
 *      floor, no carry: a sub-unit remainder is simply not charged), is
 *      charged once on each premium, each call payoff and each perp
 *      variation, borne by whoever receives that amount, and posted whole to
 *      the reserve-floor bucket account (cfg.reserve), from which svault
 *      takes its third by its own rules. Nothing else is skimmed: returned
 *      collateral pays no fee. This is the ONLY routing; it moves fees into
 *      a buffer and does not stabilise anything by itself.
 *   V4 CHECK. vderiv_check verifies that the call escrow's DEBIT equals the
 *      sum of the locks of calls not yet settled, that the long and short
 *      escrows' DEBITs equal the sums of the perps' long and short balances,
 *      that each perp's two balances plus the fees it paid equal its two
 *      locks, that no escrow carries CREDIT, and pay_ledger_check.
 *   V5 SWITCH. Opening is OFF by default. vderiv_enable takes a manifest
 *      (jurisdiction, enable flag, epoch) signed by the jurisdiction gateway
 *      key through the caller's verify hook (ML-DSA in production), for this
 *      instance's jurisdiction and with an epoch above the last, so a later
 *      manifest disables it again. While OFF vderiv_call_open and
 *      vderiv_perp_open return VDERIV_ERR_DISABLED; settling, closing and
 *      pushing always work, so nobody's collateral is ever stuck.
 *   V6 SIGNATURES. Terms carry a nonce that must equal the instance's next
 *      nonce (vderiv_t.nonce); a successful open consumes it, so a signed
 *      terms message opens at most one position. Both parties' keys must
 *      differ and both must sign the same terms message. Each key id must be
 *      the ledger owner id of the account it names (writer_key owns writer,
 *      and so on; VDERIV_ERR_SIG otherwise), so nobody can commit another
 *      owner's Vino with keys of its own.
 *
 * Bounds: every lock, premium and balance is at most VDERIV_LINE_MAX (2^59,
 * pay_ledger's per-line cap), and each escrow account's total stays at most
 * VDERIV_LINE_MAX, so a push's aggregate lines can never overflow. Every
 * product is overflow-checked before anything posts. Identifiers are
 * SHA-256(seed || tag || seq) as in vhub; e2e is VDERIV-<seq>.
 *
 * HONEST LIMITS. Options and perpetual swaps are derivatives regulated in
 * most jurisdictions, often restricted to eligible participants; the switch
 * is not a legal opinion and this is a mechanism, not an offering. Full
 * collateral removes leverage and counterparty default, not loss: a buyer can
 * lose the whole premium and a perp side its whole lock. An index of auction
 * quantities can be moved by large bidders. Signatures are only as good as
 * the verify hook. Freestanding C11: no allocation, no float.
 */
#ifndef ZXV_VDERIV_H
#define ZXV_VDERIV_H

#include <stdbool.h>
#include <stdint.h>
#include "../pay/pay_ledger.h"

#define VDERIV_NONE      0xFFFFFFFFu
#define VDERIV_RING      64u
#define VDERIV_MAX_CALLS 32u
#define VDERIV_MAX_PERPS 32u
#define VDERIV_LINE_MAX  ((uint64_t) 1 << 59) /* pay_ledger's per-line cap */

typedef enum {
    VDERIV_OK = 0,
    VDERIV_ERR_ARG = -1,
    VDERIV_ERR_DISABLED = -2, /* V5 */
    VDERIV_ERR_SIG = -3,      /* V5/V6: a bad signature */
    VDERIV_ERR_EPOCH = -4,    /* V0/V5: epoch out of order, or expiry passed */
    VDERIV_ERR_JURIS = -5,    /* V5: manifest for another jurisdiction */
    VDERIV_ERR_NONCE = -6,    /* V6 */
    VDERIV_ERR_FULL = -7,     /* position table full */
    VDERIV_ERR_OVERFLOW = -8, /* a product or total past VDERIV_LINE_MAX */
    VDERIV_ERR_STATE = -9,    /* wrong position state (e.g. not yet fixed) */
    VDERIV_ERR_FUNDS = -10,   /* a payer does not hold enough */
    VDERIV_ERR_LEDGER = -11   /* the ledger refused (see last_ledger) */
} vderiv_status_t;

typedef enum {
    VDERIV_FREE = 0,
    VDERIV_OPEN = 1,
    VDERIV_FIXED = 2,  /* V1: the expiry epoch is pushed */
    VDERIV_STOPPED = 3 /* V2: auto-closed, awaiting payout */
} vderiv_state_t;

typedef enum { VDERIV_LONG = 0, VDERIV_SHORT = 1 } vderiv_side_t;

/* verify(ctx, key, msg, len, sig, sig_len): true when sig is key's */
typedef bool (*vderiv_verify_fn)(void *ctx, uint32_t key, const uint8_t *msg, uint32_t len,
                                 const uint8_t *sig, uint32_t sig_len);

typedef struct {
    uint16_t vino;      /* the ledger asset everything is paid in */
    uint32_t escrow;    /* V1 call escrow account */
    uint32_t esc_long;  /* V2 long escrow account */
    uint32_t esc_short; /* V2 short escrow account */
    uint32_t reserve;   /* V3 reserve-floor bucket account */
    uint32_t gateway;   /* V5 jurisdiction gateway key id */
    uint32_t juris;     /* V5 jurisdiction this instance serves */
    uint64_t first_epoch;
    vderiv_verify_fn verify;
    void *vctx;
    uint8_t seed[32];
} vderiv_cfg_t;

typedef struct {
    uint64_t nonce; /* V6 */
    uint32_t writer_key, writer;
    uint32_t buyer_key, buyer;
    uint64_t strike, cap, size, expiry, premium;
} vderiv_call_terms_t;

typedef struct {
    uint64_t nonce; /* V6 */
    uint32_t long_key, long_acct;
    uint32_t short_key, short_acct;
    uint64_t size, band, lock_long, lock_short;
} vderiv_perp_terms_t;

typedef struct {
    uint8_t state; /* FREE, OPEN, FIXED */
    vderiv_call_terms_t t;
    uint64_t lock;   /* n * (C - K) */
    uint64_t fixing; /* I(expiry) once FIXED */
} vderiv_call_t;

typedef struct {
    uint8_t state; /* FREE, OPEN, STOPPED */
    vderiv_perp_terms_t t;
    uint64_t bal[2]; /* escrow balances, by vderiv_side_t */
    uint64_t ref;    /* the index at the last settlement */
    uint64_t fees;   /* V3 fees paid out of this position */
    uint64_t opened, stop_epoch;
} vderiv_perp_t;

typedef struct {
    uint64_t epoch, price, qty;
} vderiv_obs_t;

typedef struct {
    uint64_t payoff, fee, to_buyer, to_writer;
} vderiv_settle_t;

typedef struct {
    uint32_t moved, stopped, fixed; /* perps moved / stopped, calls fixed */
    uint64_t fees;                  /* V3 fees of this push */
} vderiv_push_t;

typedef struct {
    pay_ledger_t *L;
    vderiv_cfg_t cfg;
    bool enabled;
    uint64_t switch_epoch;
    vderiv_obs_t ring[VDERIV_RING];
    uint64_t n_pushed, last_epoch;
    uint64_t nonce; /* V6: the next terms nonce */
    vderiv_call_t call[VDERIV_MAX_CALLS];
    vderiv_perp_t perp[VDERIV_MAX_PERPS];
    uint64_t fees_total;
    uint64_t seq, posted;
    pay_status_t last_ledger;
} vderiv_t;

/* Defaults: every account VDERIV_NONE, first_epoch 1, no hook. */
void vderiv_cfg_default(vderiv_cfg_t *c);
/* Check the accounts against the ledger (all distinct, active, of asset
 * vino, not issuers, escrows empty) and start with the switch OFF. */
vderiv_status_t vderiv_init(vderiv_t *s, pay_ledger_t *L, const vderiv_cfg_t *c);

/* V5: "zxv/vderiv-sw/v1" || juris (4, LE) || enable (1) || epoch (8, LE). */
#define VDERIV_SWITCH_MSG (16u + 4u + 1u + 8u)
void vderiv_switch_msg(uint32_t juris, bool enable, uint64_t epoch, uint8_t out[VDERIV_SWITCH_MSG]);
vderiv_status_t vderiv_enable(vderiv_t *s, uint32_t juris, bool enable, uint64_t epoch,
                              const uint8_t *sig, uint32_t sig_len);

/* V0. Fixes due calls, settles every open perp in ONE posting. out optional. */
vderiv_status_t vderiv_index_push(vderiv_t *s, uint64_t epoch, uint64_t price, uint64_t qty,
                                  uint64_t tick, vderiv_push_t *out);
/* V0: I(epoch) while it is in the ring. False otherwise. */
bool vderiv_index_at(const vderiv_t *s, uint64_t epoch, uint64_t *index);

/* V1 (pure): the capped payoff. False when cap <= strike or it overflows. */
bool vderiv_call_payoff(uint64_t strike, uint64_t cap, uint64_t size, uint64_t index,
                        uint64_t *payoff);
/* V2 (pure): v = size * min(|cur - prev|, band); *dir +1 long wins, -1 short
 * wins, 0 no move. False on overflow. */
bool vderiv_perp_variation(uint64_t size, uint64_t band, uint64_t prev, uint64_t cur, uint64_t *v,
                           int *dir);

/* V6: "zxv/vderiv-cl/v1" || nonce || writer_key || writer || buyer_key ||
 * buyer || strike || cap || size || expiry || premium, little-endian. */
#define VDERIV_CALL_MSG (16u + 8u + 4u * 4u + 8u * 5u)
void vderiv_call_msg(const vderiv_call_terms_t *t, uint8_t out[VDERIV_CALL_MSG]);
/* "zxv/vderiv-pp/v1" || nonce || long_key || long_acct || short_key ||
 * short_acct || size || band || lock_long || lock_short, little-endian. */
#define VDERIV_PERP_MSG (16u + 8u + 4u * 4u + 8u * 4u)
void vderiv_perp_msg(const vderiv_perp_terms_t *t, uint8_t out[VDERIV_PERP_MSG]);
/* "zxv/vderiv-cx/v1" || position nonce (8, LE) || side (1). */
#define VDERIV_CLOSE_MSG (16u + 8u + 1u)
void vderiv_close_msg(uint64_t nonce, uint8_t side, uint8_t out[VDERIV_CLOSE_MSG]);

/* V1. *id (optional) gets the slot. */
vderiv_status_t vderiv_call_open(vderiv_t *s, const vderiv_call_terms_t *t, const uint8_t *wsig,
                                 uint32_t wsig_len, const uint8_t *bsig, uint32_t bsig_len,
                                 uint64_t tick, uint32_t *id);
vderiv_status_t vderiv_call_settle(vderiv_t *s, uint32_t id, uint64_t tick, vderiv_settle_t *out);

/* V2. */
vderiv_status_t vderiv_perp_open(vderiv_t *s, const vderiv_perp_terms_t *t, const uint8_t *lsig,
                                 uint32_t lsig_len, const uint8_t *ssig, uint32_t ssig_len,
                                 uint64_t tick, uint32_t *id);
/* An open perp needs `side`'s signature; a stopped one ignores sig. out
 * (optional, 2 entries) gets what each side was paid. */
vderiv_status_t vderiv_perp_close(vderiv_t *s, uint32_t id, uint8_t side, const uint8_t *sig,
                                  uint32_t sig_len, uint64_t tick, uint64_t *out);

/* V4 */
vderiv_status_t vderiv_check(const vderiv_t *s);

#endif /* ZXV_VDERIV_H */
