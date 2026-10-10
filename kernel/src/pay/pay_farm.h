/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* pay_farm.h — VFV issuance for verified work: people plug in their own
 * servers and farm Vino Floating Vouchers for compute, storage and bandwidth.
 *
 * FLOW (commit, then reveal, then mint)
 * -------------------------------------
 *   W1  RECEIPT. A farm submits a signed work receipt (farm owner, job id,
 *       kind, quantity, period, 32-byte result hash). Its digest is
 *       SHA3-256("ZXV-FARM-RECEIPT-v1" || farm || job id || kind || qty ||
 *       period || result) and the signature over that digest is checked by
 *       the operator's verify callback. A job id may be credited once, ever
 *       (replay table); a receipt for another period, a suspended farm, an
 *       unregistered farm or a bad signature is refused. Accepted receipts
 *       are QUEUED for the period, not yet credited.
 *   W2  SPOT CHECKS. At period close the operator supplies a 32-byte beacon
 *       that was NOT known when receipts were committed (e.g. a randomness
 *       beacon published after the period's cut-off). A receipt is sampled
 *       iff  u64(SHA3("ZXV-FARM-SAMPLE-v1" || beacon || job id)) mod den <
 *       num  (sample_rate num/den), so the choice is deterministic and
 *       re-computable by anyone. A sampled job is re-run through the
 *       operator's replicate callback on an independent node; if its result
 *       hash differs, the receipt is rejected and the farm is PENALISED: one
 *       strike, and ALL its earnings for this period are forfeited (nothing
 *       is minted for it); at max_strikes the farm is suspended. If the
 *       replica is unavailable the receipt is carried to the next period.
 *   W3  VALUE. A credited receipt is worth floor(qty * rate.num / rate.den)
 *       VFV minor units, rate per kind from the operator's rate table, or
 *       for compute from the swarm market's clearing price via
 *       pay_farm_rate_from_swarm (kernel/src/swarm/swarm_market.h M3: price
 *       = pot / market tokens, meaningful between begin_cycle and settle),
 *       scaled by an operator factor. A kind with no rate earns nothing.
 *   W4  PER-FARM CAP. Of a period's issuance no farm may receive more than
 *       share s = max(cap_share, 1/n) of the total actually issued (n =
 *       farms with earnings). The cap is the largest c with
 *           c * den <= num * sum_j min(e_j, c)
 *       (found by bisection; the feasible set is an interval because the
 *       right side is concave in c), and farm j is issued min(e_j, c). What
 *       is over the cap is not minted. Default cap_share 8/21
 *       (docs/SWARM_ECONOMY.md section 6).
 *   W5  FEE AND MINT. For each farm, gross g = min(e_j, c), assurance fee
 *       t = the 0.08889% fee on g with the farm's own sub-unit carry
 *       (pay_assure.h F1-F2: floor((g * 8889 + carry) / 10^7)), and ONE
 *       pay_ledger posting issues g: issuer CREDIT +g, farm DEBIT +(g - t),
 *       and t split exactly into the four fee buckets (pay_assure_split:
 *       50% reserve floor + remainder, 25% V-Bill dividend pool, 15%
 *       infrastructure/node bounties, 10% regenerative capital). If a
 *       pay_equity with VFV equity is attached, every VFV minted gets its
 *       non-voting equity (pay_vfv_equity_on_mint): g - t for the farm, t
 *       for the reserve-floor owner, over the farm's reserve path.
 *   W6  BURN: pay_farm_burn redeems VFV at par (pay_ledger_redeem).
 *
 * INVARIANTS (pay_farm_audit)
 *   I1  No mint without a verified receipt: for every farm, VFV ever minted
 *       for it <= the value of its credited (verified, not forfeited)
 *       receipts.
 *   I2  Supply: the farm issuer's CREDIT == minted - burned.
 *   I3  Fee buckets received == sum of fees; minted == sum over farms.
 *   I4  The ledger invariants L1-L3 hold.
 *
 * HONEST LIMITS. Spot checks are probabilistic: with sample rate p a farm
 * that fakes k receipts in a period escapes with probability (1-p)^k, and
 * the deterrent is the forfeiture and strikes, not certainty. Replication
 * assumes deterministic jobs and an honest, independent replica; colluding
 * replicas defeat it. The beacon must be unpredictable to farms before the
 * cut-off or sampling can be gamed. The replay table is bounded
 * (PAY_FARM_SEEN); when it is full new receipts are refused until the
 * operator rotates epochs. Whether VFV is store credit, and whether farm
 * rewards are income, wages or something else, is a legal and tax question
 * for counsel. Schema validity is not certification; there is no SWIFT or
 * CIPS connectivity. Freestanding: no libc, no allocation, no floating point.
 */
#ifndef ZXV_PAY_FARM_H
#define ZXV_PAY_FARM_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "pay_util.h"
#include "pay_ledger.h"
#include "pay_assure.h"
#include "pay_equity.h"
#include "swarm_market.h"

#define PAY_FARM_MAX   64u   /* = SWARM_MAX_MODELS */
#define PAY_FARM_QUEUE 1024u /* receipts per period */
#define PAY_FARM_SEEN  4096u /* replay table (power of two) */
#define PAY_FARM_PATH  3u    /* node, alliance, global */

typedef enum {
    PAY_WORK_COMPUTE = 0,   /* token-cycles */
    PAY_WORK_STORAGE = 1,   /* byte-hours   */
    PAY_WORK_BANDWIDTH = 2, /* bytes        */
    PAY_WORK_KIND_COUNT = 3
} pay_work_kind_t;

typedef struct {
    uint32_t farm;
    uint8_t job_id[32];
    uint8_t kind; /* pay_work_kind_t */
    uint64_t qty;
    uint64_t period;
    uint8_t result[32];
} pay_work_receipt_t;

/* Signature check over the 32-byte receipt digest. */
typedef bool (*pay_farm_verify_fn)(void *ctx, uint32_t farm, const uint8_t digest[32],
                                   const uint8_t *sig, size_t sig_len);
/* Re-run the job on an independent node. Returns false when no replica is
 * available; otherwise writes the replica's result hash. */
typedef bool (*pay_farm_replicate_fn)(void *ctx, const pay_work_receipt_t *r,
                                      uint8_t result_out[32]);

typedef struct {
    pay_rat_t rate[PAY_WORK_KIND_COUNT]; /* VFV minor units per unit; 0/1 = unset */
    pay_rat_t sample_rate;               /* default 1/8 */
    pay_rat_t cap_share;                 /* default 8/21 */
    uint32_t max_strikes;                /* default 3 */
} pay_farm_cfg_t;

typedef struct {
    bool active;
    bool suspended;
    uint32_t owner;
    uint32_t vfv_acct;
    uint32_t path[PAY_FARM_PATH];
    uint32_t strikes;
    bool tainted;       /* mismatch this period: earnings forfeited */
    uint64_t pending;   /* value of this period's credited receipts */
    uint64_t verified;  /* lifetime credited value                 */
    uint64_t forfeited; /* lifetime forfeited value                */
    uint64_t minted;    /* lifetime gross minted for this farm     */
    uint64_t capped;    /* lifetime value over the W4 cap          */
    uint64_t fee_carry; /* pay_assure.h F2 sub-unit remainder      */
} pay_farm_t;

typedef struct {
    pay_work_receipt_t r;
    uint8_t digest[32];
} pay_farm_q_t;

typedef struct {
    uint64_t verified_receipts, sampled, mismatches, carried, refused, equity_errors;
} pay_farm_stats_t;

typedef struct {
    pay_ledger_t *L;
    pay_equity_t *eq; /* optional: VFV equity hook */
    pay_farm_cfg_t cfg;
    pay_farm_verify_fn verify;
    pay_farm_replicate_fn replicate;
    void *cb_ctx;
    uint32_t issuer_acct;                  /* VFV, ISSUER: dedicated to farm issuance */
    uint32_t fee_acct[PAY_ASSURE_BUCKETS]; /* VFV, COMMONS: pay_assure_bucket_t */
    uint32_t commons_owner;                /* owner of the reserve-floor account */
    pay_farm_t farm[PAY_FARM_MAX];
    uint32_t n_farms;
    uint64_t period;
    pay_farm_q_t queue[PAY_FARM_QUEUE];
    uint32_t n_queue;
    uint8_t seen[PAY_FARM_SEEN][32];
    uint8_t seen_used[PAY_FARM_SEEN];
    uint32_t n_seen;
    uint64_t minted, burned, fees, net;
    pay_farm_stats_t stats;
} pay_farm_ctx_t;

void pay_farm_cfg_default(pay_farm_cfg_t *c);

/* Opens a dedicated VFV ISSUER account (owner `platform_owner`) and binds
 * the four fee-bucket accounts (existing VFV accounts flagged COMMONS, indexed
 * by pay_assure_bucket_t). */
pay_status_t pay_farm_init(pay_farm_ctx_t *F, pay_ledger_t *L, const pay_farm_cfg_t *cfg,
                           uint32_t platform_owner, const uint32_t fee_acct[PAY_ASSURE_BUCKETS],
                           pay_farm_verify_fn verify, pay_farm_replicate_fn replicate, void *cb_ctx,
                           pay_equity_t *eq);

/* Register a farm. `path` (may be NULL) lists node, alliance and global
 * owner ids for the VFV equity reserve path; NULL uses the reserve-floor owner. */
pay_status_t pay_farm_register(pay_farm_ctx_t *F, uint32_t owner, const uint32_t *path);
const pay_farm_t *pay_farm_get(const pay_farm_ctx_t *F, uint32_t owner);

/* Post a rate. */
pay_status_t pay_farm_set_rate(pay_farm_ctx_t *F, pay_work_kind_t kind, pay_rat_t rate);
/* W3: the swarm market clearing price times `scale` (VFV minor units per
 * swarm Financial unit). False when the market has no price yet. */
bool pay_farm_rate_from_swarm(const swarm_market_t *m, pay_rat_t scale, pay_rat_t *out);

/* W1 */
void pay_farm_receipt_digest(const pay_work_receipt_t *r, uint8_t out[32]);
pay_status_t pay_farm_submit(pay_farm_ctx_t *F, const pay_work_receipt_t *r, const uint8_t *sig,
                             size_t sig_len);
/* W2: true iff job_id is sampled under beacon at `rate`. */
bool pay_farm_sampled(const uint8_t beacon[32], const uint8_t job_id[32], pay_rat_t rate);

/* W2-W5: spot-check, value, cap, charge the fee and mint the current period, then
 * advance to the next. `tick` is stamped on the postings. */
pay_status_t pay_farm_close_period(pay_farm_ctx_t *F, const uint8_t beacon[32], uint64_t tick);

/* W4 alone (exposed for tests): the cap for earnings e[0..n). */
uint64_t pay_farm_cap(const uint64_t *e, uint32_t n, pay_rat_t share);

/* W6 */
pay_status_t pay_farm_burn(pay_farm_ctx_t *F, uint32_t holder_acct, uint64_t amount, uint64_t tick);

/* I1-I4 */
bool pay_farm_audit(const pay_farm_ctx_t *F);

#endif /* ZXV_PAY_FARM_H */
