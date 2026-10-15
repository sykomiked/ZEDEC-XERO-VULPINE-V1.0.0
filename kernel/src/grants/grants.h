/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* grants.h — Olive Oil Barrel startup grants for participants in good
 * standing, on pay_ledger.
 *
 * OWNER DECISIONS (2026-10-11, quoted in substance):
 *   - "Grants go to users and businesses in GOOD STANDING judged by
 *     deterministic ledger metrics only, no committee": node uptime (bps) at
 *     or above a threshold with no dropped-tick or malicious-routing reports,
 *     a clean peer-audit record (zero failed challenges and zero fraud
 *     findings in the window), a minimum volume over an epoch window, and a
 *     baseline stake held (slashable only on a deterministic invariant breach
 *     or fraud finding). An appeal path exists: a signed dispute-funnel
 *     ruling can override a failed check for one epoch.
 *   - "Grants are paid ONLY from the funded grant pool balance, never
 *     minted." The pool is fed by the allotment slice (default 5/55 of the pot,
 *     computed in the allotment module) through grant_pool_credit.
 *   - "VIRTUAL UNTIL BACKED": every barrel is a virtual ledger unit labelled
 *     exactly "Virtual Barrel Allocation (Unbacked Synthetic Unit)" with
 *     status "Pending Physical Underwriter Attestation". The protocol makes
 *     no custody, warehousing or delivery claim. A lot becomes BACKED only
 *     when an outside custodian or co-op signs an attestation over (lot id,
 *     barrel count, underwriter id, expiry). Unbacked units are for display
 *     and weighting only: NOT claimable as a Vino allotment and NOT lockable into
 *     V-Notes. A disputed or expired attestation reverts the lot to unbacked.
 *   The word is always "allotment".
 *
 *   G1 BARRELS. grant_init registers the barrel asset "BRL-V" (pay_ledger
 *      kind UNIT, 0 decimals, not ISO 4217) and opens three accounts for the
 *      grant operator: the barrel ISSUER (its CREDIT is every barrel
 *      outstanding), the grant POOL (Vino) and the grant RESERVE (Vino, the
 *      cost of every barrel granted). Every barrel belongs to one lot
 *      (grant_lot_open). The module keeps a lot table (id, count, attested,
 *      underwriter, attested count, expiry, disputed) and a holding table
 *      (holder account, lot, count) mirrored on the ledger: grant_check
 *      proves sum of holdings per lot == lot count, sum of lot counts ==
 *      issuer CREDIT, and each holder's DEBIT == its holdings. Barrels move
 *      only through this module (issue on grant, accept allotment on G5).
 *   G2 STANDING. grant_eval(metrics, epoch) is eligible iff no bit of the
 *      reason mask is set: UPTIME (uptime_bps < min_uptime_bps), DROPPED (any
 *      dropped-tick report), ROUTING (any malicious-routing report), AUDIT
 *      (any failed peer-audit challenge), FRAUD (any fraud finding), VOLUME
 *      (volume < min_volume), STAKE (stake < min_stake), INVALID (uptime_bps
 *      > 10000; never overridable). The report counts are zero-tolerance by
 *      owner decision. Operator thresholds; DEFAULTS CHOSEN BY THIS MODULE:
 *      min_uptime_bps 9500, min_volume 10000, min_stake 100000 (Vino minor
 *      units). APPEAL: grant_appeal records a ruling (subject, epoch, mask)
 *      signed by the dispute-funnel key; for that subject in that epoch only,
 *      the masked bits are cleared before the verdict.
 *   G3 DISTRIBUTION. grant_epoch_distribute(lot, recipients, price): every
 *      recipient is re-evaluated by G2 for the current epoch (one failure
 *      refuses the whole call, nothing posts) and its weight W_i counted into
 *      the epoch's active verified weight. With P the pool's DEBIT and the
 *      operator cap c_num/c_den (default 1/1):
 *          budget = floor(P * c_num / c_den),  B = floor(budget / price),
 *          share_i = floor(B * W_i / W), and the B - sum share_i < n
 *      leftover barrels go one each to the largest remainders (B*W_i mod W),
 *      ties to the lower index; sum share_i == B exactly. The cost B * price
 *      <= budget <= P, so the pool never goes below zero and nothing is
 *      minted. Postings: recipients with a share are grouped GRANT_BATCH at a
 *      time; each posting moves that batch's cost pool -> reserve and issues
 *      its barrels (issuer CREDIT, recipient DEBIT), so every posting is
 *      complete on its own. If the ledger refuses a later batch, the earlier
 *      ones stand, the tables reflect exactly what posted and the call
 *      returns GRANT_ERR_LEDGER. One distribution per epoch; a lot that has
 *      ever been attested takes no new barrels (open a new lot).
 *   G4 G_RATE. The owner's public metric for an epoch is the exact rational
 *      inflow / active verified weight, where inflow is what grant_pool_credit
 *      or grant_pool_record brought in during that epoch and weight is W from that epoch's
 *      distribution; den 0 gives 0/0 flagged undefined. The last
 *      GRANT_HIST epochs are kept. grant_snapshot gives aggregates only (no
 *      account, holder or subject data): epoch, inflow, weight, rate, barrels
 *      and Vino granted, pool and reserve balances, barrels outstanding,
 *      backed and unbacked, lot counts.
 *   G5 SUPPORT COMMITMENT AND ALLOTMENT. grant_attest accepts a signature from a listed
 *      underwriter over "zxv/grant-attest/v1" || lot || count || underwriter
 *      || expiry (little-endian) when count equals the lot's outstanding
 *      count, expiry > now and expiry is above the lot's last accepted
 *      expiry (so an old or disputed attestation cannot be replayed).
 *      grant_dispute takes a dispute-funnel ruling over "zxv/grant-dispute/v1"
 *      || lot || expiry of the attestation it voids. A lot is backed at tick
 *      now iff attested, not disputed and now < expiry. grant_accept_allotment
 *      (ABI name grant_redeem): only
 *      for a backed lot, the holder returns n barrels to the issuer and
 *      receives n * price Vino from the reserve in ONE posting; unbacked ->
 *      GRANT_ERR_UNBACKED, expired -> GRANT_ERR_EXPIRED, disputed ->
 *      GRANT_ERR_DISPUTED. grant_lock_ok tells a caller (vswap) whether a lot
 *      may be locked into a V-Note tier: backed only. This module never calls
 *      vswap.
 *
 * HONEST LIMITS. Barrels are software accounting units until an underwriter
 * attests; an attestation proves who signed, not that any oil exists. A pool
 * funded by allotment proceeds may share the allotment's legal treatment. A unit
 * that converts to money carries value whatever its label. Commodity custody
 * needs real custodians and insurance. The allotment price is supplied by
 * the caller and is paid only while the reserve holds it (the ledger refuses
 * otherwise). Standing metrics are only as true as the reports fed in.
 * Designed to support an auditable grant program; it claims no compliance.
 * Freestanding C11: no allocation, no float, no libc.
 */
#ifndef ZXV_GRANTS_H
#define ZXV_GRANTS_H

#include <stdbool.h>
#include <stdint.h>
#include "../pay/pay_ledger.h"

#define GRANT_MAX_LOTS        16u
#define GRANT_MAX_HOLD        64u
#define GRANT_MAX_RECIP       32u
#define GRANT_MAX_APPEALS     16u
#define GRANT_MAX_UNDERW      8u
#define GRANT_HIST            8u
#define GRANT_BATCH           (PAY_MAX_LINES - 3u) /* pool, reserve, issuer + recipients */
#define GRANT_BPS_MAX         10000u
#define GRANT_ASSET_CODE      "BRL-V"
#define GRANT_LABEL_UNBACKED  "Virtual Barrel Allocation (Unbacked Synthetic Unit)"
#define GRANT_STATUS_UNBACKED "Pending Physical Underwriter Attestation"
#define GRANT_LABEL_BACKED    "Virtual Barrel Allocation (Underwriter Attested Unit)"
#define GRANT_STATUS_BACKED   "Underwriter Signature On Record (Signature Only)"

/* G2 module defaults */
#define GRANT_DEF_UPTIME_BPS 9500u
#define GRANT_DEF_MIN_VOLUME 10000u
#define GRANT_DEF_MIN_STAKE  100000u

typedef enum {
    GRANT_OK = 0,
    GRANT_ERR_ARG = -1,
    GRANT_ERR_FULL = -2,
    GRANT_ERR_SIG = -3,      /* bad signature or unlisted underwriter */
    GRANT_ERR_UNBACKED = -4, /* G5 */
    GRANT_ERR_EXPIRED = -5,  /* G5 */
    GRANT_ERR_DISPUTED = -6, /* G5 */
    GRANT_ERR_LEDGER = -7,   /* the ledger refused (see last_ledger) */
    GRANT_ERR_EMPTY = -8,    /* G3: the budget buys no whole barrel */
    GRANT_ERR_FUNDS = -9,    /* G5: holder lacks barrels; G4: pool lacks uncounted Vino */
    GRANT_ERR_OVERFLOW = -10,
    GRANT_ERR_EPOCH = -11,      /* epoch or expiry not above the last */
    GRANT_ERR_STATE = -12,      /* already distributed / attested lot */
    GRANT_ERR_INELIGIBLE = -13, /* G3: a recipient failed G2 */
    GRANT_ERR_NOT_FOUND = -14
} grant_status_t;

/* G2 reason bits */
#define GRANT_FAIL_UPTIME  0x01u
#define GRANT_FAIL_DROPPED 0x02u
#define GRANT_FAIL_ROUTING 0x04u
#define GRANT_FAIL_AUDIT   0x08u
#define GRANT_FAIL_FRAUD   0x10u
#define GRANT_FAIL_VOLUME  0x20u
#define GRANT_FAIL_STAKE   0x40u
#define GRANT_FAIL_INVALID 0x80u /* never cleared by an appeal */
#define GRANT_FAIL_ALL     0x7Fu /* the bits an appeal may clear */

typedef struct {
    uint32_t subject;           /* participant (user or business) id */
    uint32_t uptime_bps;        /* 0..10000 */
    uint32_t dropped_reports;   /* dropped-tick reports in the window */
    uint32_t routing_reports;   /* malicious-routing reports */
    uint32_t failed_challenges; /* failed peer-audit challenges */
    uint32_t fraud_findings;
    uint64_t volume; /* Vino minor units over the epoch window */
    uint64_t stake;  /* baseline stake held, Vino minor units */
} grant_metrics_t;

typedef struct {
    uint32_t min_uptime_bps;
    uint64_t min_volume;
    uint64_t min_stake;
} grant_thresholds_t;

typedef struct {
    bool used;
    uint32_t id;
    uint64_t count; /* barrels outstanding in this lot */
    bool attested, disputed;
    uint32_t underwriter;
    uint64_t att_count, expiry;
} grant_lot_t;

typedef struct {
    bool used;
    uint32_t account; /* holder's BRL-V ledger account */
    uint32_t lot;     /* lot id */
    uint64_t count;
} grant_hold_t;

typedef struct {
    bool used;
    uint32_t subject;
    uint64_t epoch;
    uint32_t mask;
} grant_appeal_t;

typedef struct {
    uint64_t epoch;
    uint64_t inflow; /* Vino credited to the pool this epoch */
    uint64_t weight; /* active verified weight (G3) */
    uint64_t barrels, cost;
    bool distributed;
} grant_epoch_t;

typedef struct {
    grant_metrics_t m;
    uint32_t account; /* the recipient's BRL-V ledger account */
    uint64_t weight;  /* > 0 */
} grant_recipient_t;

typedef struct {
    uint64_t barrels, cost;
    uint32_t postings;
    uint32_t bad; /* index of the first ineligible recipient, or n */
    uint64_t share[GRANT_MAX_RECIP];
} grant_dist_t;

/* G4 aggregate snapshot: no per-account data. */
typedef struct {
    uint64_t epoch, inflow, weight;
    uint64_t rate_num, rate_den;
    bool rate_defined;
    uint64_t granted_barrels, granted_cost; /* this epoch */
    uint64_t total_barrels_granted;         /* ever */
    uint64_t pool_balance, reserve_balance;
    uint64_t barrels_outstanding, barrels_backed, barrels_unbacked;
    uint32_t lots, lots_backed;
} grant_snapshot_t;

/* verify(ctx, signer, msg, len, sig, sig_len): true when sig is signer's */
typedef bool (*grant_verify_fn)(void *ctx, uint32_t signer, const uint8_t *msg, uint32_t len,
                                const uint8_t *sig, uint32_t sig_len);

typedef struct {
    pay_ledger_t *L;
    uint16_t vino, barrel;
    uint32_t issuer, pool, reserve; /* ledger accounts */
    uint64_t pool_seen;             /* pool Vino already counted as inflow (G4) */
    uint32_t funnel;                /* dispute-funnel signer id */
    grant_verify_fn verify;
    void *vctx;
    uint8_t seed[32];
    uint64_t seq;
    grant_thresholds_t th;
    uint64_t cap_num, cap_den;
    grant_lot_t lot[GRANT_MAX_LOTS];
    grant_hold_t hold[GRANT_MAX_HOLD];
    grant_appeal_t appeal[GRANT_MAX_APPEALS];
    uint32_t underwriter[GRANT_MAX_UNDERW];
    uint32_t n_underw;
    grant_epoch_t hist[GRANT_HIST]; /* hist[epoch % GRANT_HIST] */
    uint64_t epoch;
    uint64_t total_barrels;
    pay_status_t last_ledger;
} grant_t;

/* G1: registers BRL-V on L and opens the issuer, pool and reserve accounts
 * (owner `owner`, PAY_CAP_FINANCIAL). Epoch starts at 0. */
grant_status_t grant_init(grant_t *g, pay_ledger_t *L, uint16_t vino_asset, uint32_t owner,
                          uint32_t funnel, grant_verify_fn verify, void *vctx,
                          const uint8_t seed[32]);
uint32_t grant_pool_account(const grant_t *g);
/* G2 thresholds (min_uptime_bps <= 10000) and G3 cap (0 < num <= den). */
grant_status_t grant_set_thresholds(grant_t *g, const grant_thresholds_t *th);
grant_status_t grant_set_cap(grant_t *g, uint64_t num, uint64_t den);
grant_status_t grant_underwriter_add(grant_t *g, uint32_t id);

/* Start epoch e (> the current one): a fresh G4 record, stale appeals freed. */
grant_status_t grant_epoch_begin(grant_t *g, uint64_t epoch);
/* Move `amount` Vino from `from` into the pool (the allotment slice) and count
 * it as this epoch's inflow. */
grant_status_t grant_pool_credit(grant_t *g, uint32_t from, uint64_t amount, uint64_t tick);
/* Record-only form, for a caller that posts the pool line itself inside a
 * larger all-or-nothing posting (the allotment's settlement): call it AFTER
 * that posting commits. It counts `amount` as this epoch's inflow only when
 * the pool's ledger balance holds at least that much Vino not yet counted,
 * so inflow can never be recorded for money that did not land. */
grant_status_t grant_pool_record(grant_t *g, uint64_t amount);

/* G2 */
#define GRANT_APPEAL_MSG (19u + 4u + 8u + 4u)
void grant_appeal_msg(uint32_t subject, uint64_t epoch, uint32_t mask,
                      uint8_t out[GRANT_APPEAL_MSG]);
grant_status_t grant_appeal(grant_t *g, uint32_t subject, uint64_t epoch, uint32_t mask,
                            const uint8_t *sig, uint32_t sig_len);
/* Eligible iff the returned mask (also written to *reasons) is 0. */
bool grant_eval(const grant_t *g, const grant_metrics_t *m, uint64_t epoch, uint32_t *reasons);

/* G3 */
grant_status_t grant_lot_open(grant_t *g, uint32_t lot);
grant_status_t grant_epoch_distribute(grant_t *g, uint32_t lot, const grant_recipient_t *r,
                                      uint32_t n, uint64_t price, uint64_t tick, grant_dist_t *out);
uint64_t grant_holding(const grant_t *g, uint32_t account, uint32_t lot);
const grant_lot_t *grant_lot(const grant_t *g, uint32_t lot);

/* G4 */
grant_status_t grant_rate(const grant_t *g, uint64_t epoch, uint64_t *num, uint64_t *den,
                          bool *defined);
void grant_snapshot(const grant_t *g, uint64_t now, grant_snapshot_t *s);

/* G5 */
#define GRANT_ATTEST_MSG  (19u + 4u + 8u + 4u + 8u)
#define GRANT_DISPUTE_MSG (20u + 4u + 8u)
void grant_attest_msg(uint32_t lot, uint64_t count, uint32_t underwriter, uint64_t expiry,
                      uint8_t out[GRANT_ATTEST_MSG]);
void grant_dispute_msg(uint32_t lot, uint64_t expiry, uint8_t out[GRANT_DISPUTE_MSG]);
grant_status_t grant_attest(grant_t *g, uint32_t lot, uint64_t count, uint32_t underwriter,
                            uint64_t expiry, uint64_t now, const uint8_t *sig, uint32_t sig_len);
grant_status_t grant_dispute(grant_t *g, uint32_t lot, const uint8_t *sig, uint32_t sig_len);
bool grant_backed(const grant_t *g, uint32_t lot, uint64_t now);
bool grant_lock_ok(const grant_t *g, uint32_t lot, uint64_t now);
/* The display label and status of a lot at tick now (unbacked text for an
 * unknown lot). */
const char *grant_label(const grant_t *g, uint32_t lot, uint64_t now, const char **status);
grant_status_t grant_redeem(grant_t *g, uint32_t holder, uint32_t holder_vino, uint32_t lot,
                            uint64_t n, uint64_t price, uint64_t now);
/* The mutual-aid name for grant_redeem; the exported symbol stays grant_redeem. */
static inline grant_status_t grant_accept_allotment(grant_t *g, uint32_t holder,
                                                    uint32_t holder_vino, uint32_t lot,
                                                    uint64_t n, uint64_t price, uint64_t now)
{
    return grant_redeem(g, holder, holder_vino, lot, n, price, now);
}

/* Mirror check (G1) plus pay_ledger_check. */
bool grant_check(const grant_t *g);

#endif /* ZXV_GRANTS_H */
