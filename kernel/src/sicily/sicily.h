/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* sicily.h — the Sicily Account view, the six Player Card tiers, the founder
 * steward threshold and organisation card programs, on pay_ledger.
 *
 * OWNER SPEC (quoted): "the Sicily Account is a tripartite view: Assets
 * "Neo-Roman", Liabilities "New California", Equity "Sicilian Crown"; the UX
 * shows Spendable / Pending / Net Worth. Player Card tiers, in order:
 * Copper/Obsidian, Bronze/Carnelian, Silver/Jade, Gold/Lapis, Platinum/Ruby,
 * Curzi/Admas." Agreed: equity is DERIVED (A - L), never stored; escrow is an
 * encumbered asset (in Net Worth, not in Spendable); tiers are EARNED from
 * credibility, bond and uptime; tiers give NO batch-auction priority; no
 * claim or loyalty framing. DECIDED: the founder tier Curzi/Admas holds
 * founder powers (master seed, basket parameters, top routing, final
 * arbitration) only through a STEWARD THRESHOLD: 3-of-5 steward keys plus a
 * 7-day time lock; the founder holds one key and has no solo control.
 *
 * NOTE ON NAMES. The labels match the rail jurisdictions (owner decision
 * 2026-10-11 15:42Z, which swapped the rails to fit these labels): assets sit
 * on DEBIT 555 "NRE" Neo Roman Empire, liabilities on CREDIT 777 "NCR" New
 * California Republic, equity on EQUITY 888 "PNS". This module only labels
 * the three totals and emits no jurisdiction code.
 *
 *   S1 VIEW. sic_view sums every active ledger account of one owner and one
 *      asset (all capital forms): liquid = DEBIT of the accounts not named as
 *      escrow, escrow = DEBIT of the accounts the caller names as escrow
 *      (each must belong to that owner and asset), assets (Neo-Roman) =
 *      liquid + escrow, liabilities (New California) = sum of CREDIT.
 *      Pending in / out come from the caller. spendable = liquid -
 *      pending_out, or 0 when that would be negative; net worth = assets -
 *      liabilities (signed, overflow-checked); equity (Sicilian Crown) is
 *      reported equal to net worth: it is derived, never stored. Pending in
 *      is shown, not counted. sic_view_compute does the same from numbers.
 *   S2 TIERS. sic_tier_eval(credibility, bond, uptime_bps, giving) returns
 *      Copper..Platinum (0..4). Effective credibility = credibility +
 *      min(giving, floor(credibility / 3)), so giving is never more than 1/4
 *      of the effective score. A tier needs ALL THREE of its thresholds AND
 *      every tier below it (the climb stops at the first tier not met, so an
 *      odd operator table can never skip a rung). DEFAULT thresholds, chosen
 *      by this module and overridable by the operator (cred / bond /
 *      uptime bps): Bronze 100 / 1,000 / 9000; Silver 250 / 10,000 / 9500;
 *      Gold 500 / 100,000 / 9800; Platinum 800 / 1,000,000 / 9950. An uptime
 *      above 10000 bps is malformed and evaluates to Copper. Curzi/Admas (5)
 *      is never returned by evaluation: it is the steward role (S3).
 *   S3 PERKS. A perk bitmask per tier, cumulative upward: Bronze free
 *      registration and storefronts; Silver forwards and IP notes; Gold adds
 *      none; Platinum cross-border licensing, eligibility only (the licensing
 *      itself goes through the jurisdiction ABI). Curzi/Admas carries the
 *      Platinum set and no power of its own. THERE IS NO PRIORITY: nothing in
 *      this module computes or exports a batch-auction priority, fee
 *      discount, credit line or ordering preference for any tier.
 *   S4 STEWARDS. Five distinct steward key ids, one the founder's. Anyone may
 *      propose (action kind, 32-byte params hash, tick); a proposal id is a
 *      counter from 1. A steward approves by signing (op 'A', id, kind,
 *      params hash) and cancels by signing (op 'C', ...), checked by the
 *      caller's verify hook (ML-DSA in production); a repeated approval or
 *      cancel vote returns SIC_DUPLICATE and is not counted twice. Execute
 *      succeeds only when approvals >= 3 AND tick >= proposed + 7 days (ticks
 *      are seconds), once. Three cancel votes cancel it. A proposal expires
 *      at proposed + window (default 60 days, configurable, always above
 *      7 days); after that nothing can be done with it. The founder's key
 *      counts as one vote like any other.
 *   S5 ORG PROGRAMS. An organisation's card program is a signed manifest
 *      (org id, scale level, sweep num/den, version) extending the root: it
 *      has NO field for fee, credit, Vino, auction priority or the root tier,
 *      so it cannot set any of them. Opening needs a deposit of at least the
 *      floor for its scale (default table, operator-overridable). Points are
 *      escrow minor units, 1:1; earned, unpaid points (owed) are a liability
 *      of the escrow.
 *   S6 SWEEP. sic_org_sweep(gross): fee = pay_assure_fee(gross) (the
 *      protocol fee is taken FIRST), net = gross - fee, x = net * num + rem,
 *      swept = floor(x / den), rem = x mod den. Over any run of sales the
 *      total swept is exactly floor(sum(net) * num / den). num <= den (at
 *      most 100%); num == 0 is no sweep.
 *   S7 AWARD / REDEEM. A NEW award of p points to an opted-in member needs
 *      escrow >= floor + owed + p; below that new rewards pause. A member may
 *      redeem up to what they earned (and what escrow holds) at any time,
 *      even below the floor: earned points are always paid, from escrow.
 *      Users opt in with sic_org_join; sic_org_leave stops new awards and
 *      keeps earned points redeemable. No org function reads or writes a
 *      root tier.
 *
 * HONEST LIMITS. Tiers are reputational: they are not a loyalty scheme, a
 * claim on anything or a financial product. The thresholds and floors are
 * defaults chosen by this module, not anyone's standard. An org program may
 * still be regulated (gift card, stored value, promotion law) where it runs;
 * this module is designed to support a review, not to replace one. The org
 * escrow figures here are accounting the caller mirrors into a real ledger
 * escrow account; this module posts nothing. Signature checks are only as
 * good as the caller's hook. Freestanding C11: no allocation, no float.
 */
#ifndef ZXV_SICILY_H
#define ZXV_SICILY_H

#include <stdbool.h>
#include <stdint.h>
#include "../pay/pay_ledger.h"

typedef enum {
    SIC_OK = 0,
    SIC_DUPLICATE = 1, /* S4: vote already counted (nothing changed) */
    SIC_ERR_ARG = -1,
    SIC_ERR_FULL = -2,
    SIC_ERR_OVERFLOW = -3,
    SIC_ERR_SIG = -4,
    SIC_ERR_NOT_FOUND = -5,
    SIC_ERR_STATE = -6,     /* S4: proposal not open */
    SIC_ERR_THRESHOLD = -7, /* S4: fewer than 3 approvals */
    SIC_ERR_TIMELOCK = -8,  /* S4: before proposed + 7 days */
    SIC_ERR_EXPIRED = -9,   /* S4: at or past proposed + window */
    SIC_ERR_PAUSED = -10,   /* S7: escrow below floor + owed + points */
    SIC_ERR_FUNDS = -11,    /* S7: more than earned or than escrow holds */
    SIC_ERR_MEMBER = -12    /* S7: not opted in */
} sic_status_t;

/* ===== S1: the view ===== */
#define SIC_MAX_ESCROW 16u

typedef struct {
    uint64_t liquid;      /* unencumbered DEBIT                         */
    uint64_t escrow;      /* encumbered DEBIT                           */
    uint64_t assets;      /* "Neo-Roman": liquid + escrow               */
    uint64_t liabilities; /* "New California": sum of CREDIT            */
    int64_t equity;       /* "Sicilian Crown": assets - liabilities     */
    uint64_t pending_in;  /* shown, not counted                         */
    uint64_t pending_out;
    uint64_t spendable; /* max(0, liquid - pending_out)               */
    int64_t net_worth;  /* assets - liabilities (== equity)           */
} sic_view_t;

/* From numbers. On error *out is zeroed. */
sic_status_t sic_view_compute(uint64_t liquid, uint64_t escrow, uint64_t liabilities,
                              uint64_t pending_in, uint64_t pending_out, sic_view_t *out);
/* From the ledger. `escrow` names n_escrow (<= SIC_MAX_ESCROW) account ids of
 * this owner and asset that are encumbered; any other id is SIC_ERR_ARG. */
sic_status_t sic_view(const pay_ledger_t *L, uint32_t owner, uint16_t asset, const uint32_t *escrow,
                      uint32_t n_escrow, uint64_t pending_in, uint64_t pending_out,
                      sic_view_t *out);

/* ===== S2 / S3: tiers ===== */
typedef enum {
    SIC_TIER_COPPER = 0,   /* Copper / Obsidian   */
    SIC_TIER_BRONZE = 1,   /* Bronze / Carnelian  */
    SIC_TIER_SILVER = 2,   /* Silver / Jade       */
    SIC_TIER_GOLD = 3,     /* Gold / Lapis        */
    SIC_TIER_PLATINUM = 4, /* Platinum / Ruby     */
    SIC_TIER_CURZI = 5,    /* Curzi / Admas: the steward role, never earned here */
    SIC_TIER_COUNT = 6
} sic_tier_t;

#define SIC_EARNED_TIERS 4u /* Bronze..Platinum have thresholds */
#define SIC_BPS_MAX      10000u

typedef struct {
    uint64_t cred;
    uint64_t bond;
    uint32_t uptime_bps;
} sic_threshold_t;

typedef struct {
    sic_threshold_t t[SIC_EARNED_TIERS]; /* t[0] = Bronze ... t[3] = Platinum */
} sic_tier_table_t;

void sic_tier_table_default(sic_tier_table_t *tab);
/* credibility + min(giving, floor(credibility / 3)), saturating at UINT64_MAX */
uint64_t sic_effective_cred(uint64_t credibility, uint64_t giving);
/* `tab` NULL uses the defaults. Never returns SIC_TIER_CURZI. */
sic_tier_t sic_tier_eval(const sic_tier_table_t *tab, uint64_t credibility, uint64_t bond,
                         uint32_t uptime_bps, uint64_t giving);
/* which 0 = metal name, 1 = stone name; "" for anything out of range */
const char *sic_tier_name(sic_tier_t t, uint32_t which);

#define SIC_PERK_FREE_REGISTRATION 0x01u /* Bronze   */
#define SIC_PERK_STOREFRONTS       0x02u /* Bronze   */
#define SIC_PERK_FORWARDS          0x04u /* Silver   */
#define SIC_PERK_IP_NOTES          0x08u /* Silver   */
#define SIC_PERK_XB_LICENSING      0x10u /* Platinum: via the jurisdiction ABI */
/* Cumulative upward; 0 for an out-of-range tier. */
uint32_t sic_tier_perks(sic_tier_t t);

/* ===== S4: the steward threshold ===== */
#define SIC_STEWARDS           5u
#define SIC_STEWARD_THRESHOLD  3u
#define SIC_DAY                86400u /* ticks are seconds */
#define SIC_TIMELOCK           ((uint64_t) 7u * SIC_DAY)
#define SIC_WINDOW_DEFAULT     ((uint64_t) 60u * SIC_DAY)
#define SIC_MAX_PROPOSALS      16u
#define SIC_STEWARD_MSG        (18u + 1u + 4u + 1u + 32u)
#define SIC_STEWARD_OP_APPROVE 'A'
#define SIC_STEWARD_OP_CANCEL  'C'

typedef enum {
    SIC_ACT_MASTER_SEED = 1,
    SIC_ACT_BASKET_PARAMS = 2,
    SIC_ACT_TOP_ROUTING = 3,
    SIC_ACT_ARBITRATION = 4
} sic_action_t;

typedef enum {
    SIC_PROP_FREE = 0,
    SIC_PROP_OPEN = 1,
    SIC_PROP_EXECUTED = 2,
    SIC_PROP_CANCELLED = 3,
    SIC_PROP_EXPIRED = 4
} sic_prop_state_t;

/* verify(ctx, key, msg, len, sig, sig_len): true when sig is key's
 * (the same shape as vhub_verify_fn). */
typedef bool (*sic_verify_fn)(void *ctx, uint32_t key, const uint8_t *msg, uint32_t len,
                              const uint8_t *sig, uint32_t sig_len);

typedef struct {
    uint32_t id;
    uint8_t kind;
    uint8_t state;    /* sic_prop_state_t */
    uint8_t approved; /* bitmask of steward indices */
    uint8_t cancel;   /* bitmask of steward indices */
    uint8_t params[32];
    uint64_t proposed;
} sic_proposal_t;

typedef struct {
    uint32_t key[SIC_STEWARDS];
    uint32_t founder; /* index of the founder's key (one vote, no more) */
    uint64_t window;
    sic_verify_fn verify;
    void *vctx;
    sic_proposal_t p[SIC_MAX_PROPOSALS];
    uint32_t next_id;
} sic_steward_t;

/* Keys must be distinct; founder < SIC_STEWARDS; window 0 = default, else it
 * must exceed SIC_TIMELOCK. */
sic_status_t sic_steward_init(sic_steward_t *s, const uint32_t key[SIC_STEWARDS], uint32_t founder,
                              uint64_t window, sic_verify_fn verify, void *vctx);
/* "zxv/sic-steward/v1" || op || id (LE) || kind || params */
void sic_steward_msg(uint8_t op, uint32_t id, uint8_t kind, const uint8_t params[32],
                     uint8_t out[SIC_STEWARD_MSG]);
sic_status_t sic_steward_propose(sic_steward_t *s, uint8_t kind, const uint8_t params[32],
                                 uint64_t tick, uint32_t *out_id);
sic_status_t sic_steward_approve(sic_steward_t *s, uint32_t id, uint32_t steward,
                                 const uint8_t *sig, uint32_t sig_len, uint64_t tick);
sic_status_t sic_steward_cancel(sic_steward_t *s, uint32_t id, uint32_t steward, const uint8_t *sig,
                                uint32_t sig_len, uint64_t tick);
sic_status_t sic_steward_execute(sic_steward_t *s, uint32_t id, uint64_t tick);
/* The proposal's state at `tick` (an open one past its window reads
 * EXPIRED); SIC_PROP_FREE when the id is unknown. */
sic_prop_state_t sic_steward_state(const sic_steward_t *s, uint32_t id, uint64_t tick);
/* Number of distinct approvals (0 for an unknown id). */
uint32_t sic_steward_approvals(const sic_steward_t *s, uint32_t id);

/* ===== S5-S7: org card programs ===== */
#define SIC_ORG_SCALES      5u
#define SIC_ORG_MAX_MEMBERS 32u
#define SIC_ORG_MSG         (14u + 4u + 1u + 8u + 8u + 4u)

/* Default minimum deposit per scale level, escrow minor units. */
extern const uint64_t sic_org_floor_default[SIC_ORG_SCALES];

typedef struct {
    uint32_t user;
    bool joined;
    uint64_t points; /* earned, unpaid */
} sic_member_t;

typedef struct {
    uint32_t org_id;
    uint32_t signer; /* the manifest's key */
    uint32_t version;
    uint8_t scale;
    uint64_t floor;
    uint64_t escrow;
    uint64_t owed; /* sum of members' points */
    uint64_t sweep_num, sweep_den, sweep_rem;
    uint64_t gross_total, fee_total, swept_total;
    sic_member_t m[SIC_ORG_MAX_MEMBERS];
    uint32_t n_members;
} sic_org_t;

/* "zxv/sic-org/v1" || org_id || scale || num || den || version (LE) */
void sic_org_manifest_msg(uint32_t org_id, uint8_t scale, uint64_t num, uint64_t den,
                          uint32_t version, uint8_t out[SIC_ORG_MSG]);
/* floors NULL = sic_org_floor_default. deposit >= floor[scale]. */
sic_status_t sic_org_open(sic_org_t *o, uint32_t org_id, uint32_t signer, uint8_t scale,
                          uint64_t num, uint64_t den, uint32_t version, uint64_t deposit,
                          const uint64_t floors[SIC_ORG_SCALES], const uint8_t *sig,
                          uint32_t sig_len, sic_verify_fn verify, void *vctx);
sic_status_t sic_org_deposit(sic_org_t *o, uint64_t amount);
/* S6. fee_out / swept_out optional. */
sic_status_t sic_org_sweep(sic_org_t *o, uint64_t gross, uint64_t *fee_out, uint64_t *swept_out);
sic_status_t sic_org_join(sic_org_t *o, uint32_t user);
sic_status_t sic_org_leave(sic_org_t *o, uint32_t user);
/* S7 */
sic_status_t sic_org_award(sic_org_t *o, uint32_t user, uint64_t points);
sic_status_t sic_org_redeem(sic_org_t *o, uint32_t user, uint64_t points);
/* The member's earned, unpaid points (0 when unknown). */
uint64_t sic_org_points(const sic_org_t *o, uint32_t user);
/* True when new awards are paused (escrow < floor + owed). */
bool sic_org_paused(const sic_org_t *o);

#endif /* ZXV_SICILY_H */
