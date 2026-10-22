/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* dispute.h — the three-level dispute funnel: deterministic proofs, a
 * VRF-selected, staked, blind commit-reveal jury with 1x/4x/16x appeals,
 * and a jurisdiction gateway; bonds on pay_ledger.
 *
 * OWNER DECISIONS (implemented here): "3-level dispute funnel (deterministic
 * proofs -> VRF blind commit-reveal staked jury with bonds, 1x/4x/16x appeals
 * -> jurisdiction gateway)" and "solvency funded by slashed bonds + salvage
 * auctions": every slashed bond goes to the reserve-floor account. No usury:
 * bonds are returned at par or slashed whole; nothing accrues with time.
 *
 * WHERE IT SITS. kernel/src/market holds the escrow, opens the dispute and
 * takes a mediator ruling (buyer award). This funnel PRODUCES that ruling
 * (award in bps of the escrow to the buyer, dsp_award_amount turns it into
 * minor units, dsp_ruling_digest is what the operator's mediator key
 * attests); it never moves the order's escrow. It moves only BONDS, between
 * the parties' / jurors' ledger accounts, the module's bond escrow account and
 * the reserve-floor account. It does not depend on kernel/src/cred: the final
 * ruling names the loser and the caller records it there (cred_fail).
 *
 * ACCOUNTS (fixed at dsp_init): `escrow` holds every live bond and must be
 * empty at init (so its balance is exactly the bonds, D10); `reserve` is the
 * reserve-floor bucket (pay_assure.h B1). Same asset, distinct, non-issuer.
 * Parties and jurors name their own accounts of that asset.
 *
 *   D1 OPEN. dsp_open(order[32], terms[32], buyer, seller, accounts, escrow
 *      amount) starts a case at LEVEL 1 (state PROOFS). `terms` is the
 *      order's committed hash the level-1 predicate may compare against.
 *      buyer != seller, distinct party accounts of the module's asset that
 *      are not the module's accounts; an order id already in the table is
 *      refused (DSP_ERR_DUP). Case ids count from 1. dsp_release frees a
 *      FINAL case's slot.
 *   D2 LEVEL 1, DETERMINISTIC PROOFS. A party submits (kind, 32-byte hash)
 *      signed over dsp_proof_msg = "zxv/dispute-proof/v1" || case (LE32) ||
 *      order || party || kind || hash, checked by verify(party's id). The
 *      caller's predicate hook decides: DSP_PRED_BUYER / DSP_PRED_SELLER end
 *      the case FINAL at level 1, award 10000 / 0 bps, no jury; anything
 *      else (or no predicate) records the proof and returns DSP_PENDING.
 *   D3 ESCALATION. The funnel never skips a level. From level 1 either party
 *      escalates to level 2 round 1 by locking the 1x appeal bond
 *      (base_bond). After a jury round is ruled, only its LOSER may escalate,
 *      before the appeal deadline: round 1 -> round 2 locks 4x, round 2 ->
 *      round 3 locks 16x (base_bond * 4^(r-1) for round r), and after round 3
 *      the loser escalates to LEVEL 3 with no protocol bond (the gateway's
 *      own costs are outside this module). A bond is locked by one ledger
 *      transfer party -> escrow; a refused transfer changes nothing.
 *   D4 SELECTION. A round's jury of jury[r-1] jurors (default 3, 7, 15) is
 *      drawn from the staked pool after dsp_select checks the caller's VRF
 *      output: vrf_verify(ctx, seed, proof, out) must be true for seed =
 *      SHA-256("zxv/dispute-seed/v1" || module seed || case || round ||
 *      order). Eligible: an active juror with weight > 0, whose id is not a
 *      party's, whose account is not a party's, not drawn in an earlier round
 *      of this case, and whose account holds at least juror_bond. Weight =
 *      stake, or floor(stake * cred_bps / 10000) when cred_weighted. Draws
 *      WITHOUT replacement: draw d takes x = the first 8 bytes (LE) of
 *      SHA-256("zxv/dispute-jury/v1" || out || ctr LE32), ctr counting every
 *      hash from 0; x is rejected (next ctr) when x >= 2^64 - (2^64 mod W),
 *      else r = x mod W picks the eligible juror whose cumulative weight
 *      interval (ascending pool index) holds r; it leaves the pool, W falls.
 *      Fewer eligible jurors than seats: DSP_ERR_POOL, nothing changes. Each
 *      drawn juror then locks juror_bond (transfer juror -> escrow) in draw
 *      order; if the ledger refuses one (frozen account, or an account
 *      shared by jurors that cannot cover every bond), the bonds already
 *      locked are returned and dsp_select fails with DSP_ERR_LEDGER (remove
 *      that juror and select again: the draw is deterministic).
 *   D5 BLIND COMMIT-REVEAL. Votes are BINARY: DSP_BUYER (1) or DSP_SELLER
 *      (2); the award is 10000 or 0 bps (partial awards exist only at level
 *      3). commit = SHA-256("zxv/dispute-vote/v1" || case (LE32) || juror
 *      (LE32) || vote || salt[32]) (dsp_vote_commit). A drawn juror commits
 *      once, signed over "zxv/dispute-commit/v1" || case || round || juror ||
 *      commit, before commit_deadline = select tick + commit_ticks. Reveal
 *      (vote, salt) once the commit deadline has passed or every juror has
 *      committed, before reveal_deadline = commit_deadline + reveal_ticks; it
 *      must reproduce the commit (DSP_ERR_COMMIT otherwise).
 *   D6 TALLY. When every juror has revealed, or at reveal_deadline, the
 *      majority of REVEALED votes wins. Jurors who did not reveal (or never
 *      committed) and jurors in the minority are SLASHED: their bond moves
 *      to `reserve`. Majority jurors get their bond back and NOTHING more
 *      (no reward from slashed bonds, so a majority gains nothing by voting
 *      against a minority). The round's appeal bond returns to the appellant
 *      when the round rules for them, else it is slashed. Then the round is
 *      RULED (state APPEAL) with an appeal deadline of tick + appeal_ticks.
 *      A TIE (equal revealed votes, including none) rules nothing: revealers
 *      get their bonds back, non-revealers are slashed, the appeal bond is
 *      returned (the appeal was not rejected), and the case moves at no bond
 *      cost to the next round (DSP_TIE), or to level 3 after round 3. Every
 *      bond is settled one posting at a time and marked; if the ledger
 *      refuses one, dsp_tally returns DSP_ERR_LEDGER and a later call resumes
 *      where it stopped.
 *   D7 FINAL AT LEVEL 2. A ruled round nobody appeals becomes FINAL at the
 *      appeal deadline (dsp_finalize).
 *   D8 LEVEL 3, JURISDICTION GATEWAY. The gateway key signs "zxv/dispute-gw/
 *      v1" || case (LE32) || award (LE32), award 0..10000 bps, checked by
 *      verify(gateway); that ruling is FINAL.
 *   D9 OUTPUT. dsp_ruling gives {award_bps, level, round, loser}: the loser
 *      is the side the award goes against (award > 5000: seller; < 5000:
 *      buyer; exactly 5000: none). dsp_ruling_digest = SHA-256("zxv/dispute-
 *      ruling/v1" || case || order || award (LE32) || level || round) is what
 *      the operator's mediator key signs alongside market's
 *      mk_ruling_digest. dsp_award_amount(escrow, bps): buyer = floor(escrow
 *      * bps / 10000), seller = the rest (the rounding unit goes to the
 *      seller, the status quo holder).
 *   D10 AUDIT. dsp_check: escrow CREDIT == 0 and escrow DEBIT == locked_total
 *      - returned_total - slashed_total == the sum over cases of their live
 *      bonds (recomputed from the unsettled juror bonds and the unsettled
 *      appeal bond); a case at APPEAL, GATEWAY, FINAL or PROOFS has none.
 *      slashed_total counts exactly what was posted to `reserve` (the test
 *      checks the reserve's inflow against it).
 *
 * Every posting is built here with identifiers derived from the module seed
 * and a counter (as vauction does), at the caller's tick.
 *
 * HONEST LIMITS. A jury is not a court: its ruling is a protocol outcome,
 * and level 3 exists so that a real jurisdiction can decide. Selection is
 * only as unpredictable as the VRF: src/vrf provides ECVRF-EDWARDS25519-
 * SHA512-TAI (RFC 9381) and its hook vrf_dsp_verify (ctx = vrf_dsp_ctx_t,
 * proof = the 80-byte pi, out = the first 32 bytes of beta), which the test
 * uses; the VRF key holder still learns each draw in advance and can
 * withhold it (stalling selection), so that key belongs to a party with no
 * stake in the case. Stake weighting favours the well-staked; pool
 * capture by one party's friends is limited only by stake and by the
 * cross-round exclusion. Binary votes cannot express a partial award below
 * level 3. Juror bribery is not prevented, only made costly by bonds.
 * Dispute resolution, arbitration and consumer law differ by jurisdiction
 * and need legal review. Freestanding C11: no allocation, no floating
 * point, no libc.
 */
#ifndef ZXV_DISPUTE_H
#define ZXV_DISPUTE_H

#include <stdbool.h>
#include <stdint.h>
#include "../pay/pay_ledger.h"

#define DSP_MAX_CASES  8u
#define DSP_MAX_POOL   32u
#define DSP_MAX_JURY   15u
#define DSP_ROUNDS     3u /* jury rounds at level 2 */
#define DSP_BPS        10000u
#define DSP_STAKE_MAX  ((uint64_t) 1 << 56)
#define DSP_PROOF_MSG  (20u + 4u + 32u + 1u + 1u + 32u)
#define DSP_COMMIT_MSG (21u + 4u + 1u + 4u + 32u)
#define DSP_GW_MSG     (17u + 4u + 4u)

typedef enum {
    DSP_OK = 0,
    DSP_PENDING = 1, /* D2: proof recorded, undecided */
    DSP_TIE = 2,     /* D6: tie, moved on at no bond cost */
    DSP_ERR_ARG = -1,
    DSP_ERR_FULL = -2,
    DSP_ERR_UNKNOWN = -3, /* case or juror */
    DSP_ERR_STATE = -4,   /* out of order */
    DSP_ERR_SIG = -5,
    DSP_ERR_VRF = -6,
    DSP_ERR_DEADLINE = -7, /* too early or too late */
    DSP_ERR_COMMIT = -8,   /* D5: reveal does not match */
    DSP_ERR_DUP = -9,      /* already done / order already open / juror known */
    DSP_ERR_POOL = -10,    /* D4: not enough eligible jurors */
    DSP_ERR_LEDGER = -11,  /* see last_ledger */
    DSP_ERR_PARTY = -12,   /* D3: not the party allowed to act */
    DSP_ERR_OVERFLOW = -13
} dsp_status_t;

typedef enum { DSP_NONE = 0, DSP_BUYER = 1, DSP_SELLER = 2 } dsp_party_t;
typedef enum { DSP_PRED_NONE = 0, DSP_PRED_BUYER = 1, DSP_PRED_SELLER = 2 } dsp_pred_t;

typedef enum {
    DSP_ST_FREE = 0,
    DSP_ST_PROOFS = 1,  /* level 1 */
    DSP_ST_SELECT = 2,  /* level 2, jury to draw */
    DSP_ST_VOTE = 3,    /* level 2, commit / reveal */
    DSP_ST_APPEAL = 4,  /* level 2, ruled, appeal window */
    DSP_ST_GATEWAY = 5, /* level 3 */
    DSP_ST_FINAL = 6
} dsp_state_t;

typedef struct {
    uint64_t base_bond;        /* D3: 1x appeal bond, 16x < PAY_BAL_MAX */
    uint64_t juror_bond;       /* D4: per seat, < PAY_BAL_MAX */
    uint32_t jury[DSP_ROUNDS]; /* odd, nondecreasing, <= DSP_MAX_JURY, sum <= DSP_MAX_POOL */
    uint64_t commit_ticks, reveal_ticks, appeal_ticks; /* > 0 */
    bool cred_weighted;                                /* D4 */
} dsp_config_t;

typedef struct {
    uint32_t id; /* signer id */
    uint32_t acct;
    uint64_t stake;    /* 1..DSP_STAKE_MAX */
    uint32_t cred_bps; /* 0..10000, default 10000 */
    bool active;
} dsp_juror_t;

typedef struct {
    uint32_t id;
    uint8_t state, level, round; /* round 1..3 at level 2 */
    uint8_t order[32], terms[32];
    uint32_t buyer, seller, buyer_acct, seller_acct;
    uint64_t escrow; /* the order's escrow (market's), for dsp_award_amount */
    uint64_t opened;
    uint32_t n_proofs;
    uint32_t used; /* pool indices drawn in earlier rounds (bitmask) */
    /* current round */
    uint8_t n_jury;
    uint8_t seat[DSP_MAX_JURY]; /* pool index */
    bool committed[DSP_MAX_JURY], revealed[DSP_MAX_JURY], settled[DSP_MAX_JURY];
    uint8_t vote[DSP_MAX_JURY];
    uint8_t commit[DSP_MAX_JURY][32];
    uint64_t jbond; /* juror bond locked this round */
    uint64_t commit_deadline, reveal_deadline, appeal_deadline;
    uint8_t appellant; /* dsp_party_t of the live appeal bond */
    uint64_t appeal_bond;
    bool appeal_live;
    /* ruled / final */
    uint8_t winner; /* dsp_party_t of the last ruled round */
    uint32_t award_bps;
} dsp_case_t;

typedef struct {
    uint32_t award_bps;
    uint8_t level, round; /* round 0 at levels 1 and 3 */
    uint8_t loser;        /* dsp_party_t */
    uint32_t loser_id;    /* 0 when no loser */
} dsp_ruling_t;

/* verify(ctx, signer, msg, len, sig, sig_len) */
typedef bool (*dsp_verify_fn)(void *ctx, uint32_t signer, const uint8_t *msg, uint32_t len,
                              const uint8_t *sig, uint32_t sig_len);
/* vrf_verify(ctx, seed, proof, proof_len, out): true when out is the VRF
 * output of seed proved by proof (ECVRF: vrf.h vrf_dsp_verify). */
typedef bool (*dsp_vrf_fn)(void *ctx, const uint8_t seed[32], const uint8_t *proof,
                           uint32_t proof_len, const uint8_t out[32]);
/* D2: deterministic predicate; returns a dsp_pred_t. */
typedef int (*dsp_pred_fn)(void *ctx, const dsp_case_t *c, uint8_t party, uint8_t kind,
                           const uint8_t hash[32]);

typedef struct {
    pay_ledger_t *L;
    uint32_t escrow, reserve;
    uint16_t asset;
    uint32_t gateway; /* D8 signer id */
    dsp_config_t cfg;
    dsp_verify_fn verify;
    dsp_vrf_fn vrf;
    dsp_pred_fn pred;
    void *ctx;
    uint8_t seed[32];
    uint64_t seq; /* postings made */
    uint32_t next_id;
    dsp_juror_t pool[DSP_MAX_POOL];
    uint32_t n_pool;
    dsp_case_t c[DSP_MAX_CASES];
    uint64_t locked_total, returned_total, slashed_total;
    pay_status_t last_ledger;
    bool ready;
} dsp_t;

void dsp_config_default(dsp_config_t *cfg);
bool dsp_config_ok(const dsp_config_t *cfg);
/* cfg NULL = defaults; pred may be NULL (level 1 then never decides). */
dsp_status_t dsp_init(dsp_t *D, pay_ledger_t *L, const dsp_config_t *cfg, uint32_t escrow,
                      uint32_t reserve, uint32_t gateway, dsp_verify_fn verify, dsp_vrf_fn vrf,
                      dsp_pred_fn pred, void *ctx, const uint8_t seed[32]);

/* Pool. */
dsp_status_t dsp_juror_add(dsp_t *D, uint32_t id, uint32_t acct, uint64_t stake);
dsp_status_t dsp_juror_set_cred(dsp_t *D, uint32_t id, uint32_t cred_bps);
dsp_status_t dsp_juror_remove(dsp_t *D, uint32_t id);
uint64_t dsp_juror_weight(const dsp_t *D, uint32_t pool_index);

/* D1 */
dsp_status_t dsp_open(dsp_t *D, const uint8_t order[32], const uint8_t terms[32], uint32_t buyer,
                      uint32_t seller, uint32_t buyer_acct, uint32_t seller_acct, uint64_t escrow,
                      uint64_t tick, uint32_t *out_id);
const dsp_case_t *dsp_case(const dsp_t *D, uint32_t id);
dsp_status_t dsp_release(dsp_t *D, uint32_t id);

/* D2 */
void dsp_proof_msg(uint32_t id, const uint8_t order[32], uint8_t party, uint8_t kind,
                   const uint8_t hash[32], uint8_t out[DSP_PROOF_MSG]);
dsp_status_t dsp_proof(dsp_t *D, uint32_t id, uint8_t party, uint8_t kind, const uint8_t hash[32],
                       const uint8_t *sig, uint32_t sig_len);

/* D3 */
uint64_t dsp_appeal_bond(const dsp_config_t *cfg, uint32_t round); /* 0 outside 1..3 */
dsp_status_t dsp_escalate(dsp_t *D, uint32_t id, uint8_t party, uint64_t tick);

/* D4 */
void dsp_vrf_seed(const dsp_t *D, uint32_t id, uint8_t out[32]);
dsp_status_t dsp_select(dsp_t *D, uint32_t id, const uint8_t *proof, uint32_t proof_len,
                        const uint8_t out[32], uint64_t tick);

/* D5 */
void dsp_vote_commit(uint32_t id, uint32_t juror, uint8_t vote, const uint8_t salt[32],
                     uint8_t out[32]);
void dsp_commit_msg(uint32_t id, uint8_t round, uint32_t juror, const uint8_t commit[32],
                    uint8_t out[DSP_COMMIT_MSG]);
dsp_status_t dsp_commit(dsp_t *D, uint32_t id, uint32_t juror, const uint8_t commit[32],
                        const uint8_t *sig, uint32_t sig_len, uint64_t tick);
dsp_status_t dsp_reveal(dsp_t *D, uint32_t id, uint32_t juror, uint8_t vote, const uint8_t salt[32],
                        uint64_t tick);

/* D6 / D7 */
dsp_status_t dsp_tally(dsp_t *D, uint32_t id, uint64_t tick);
dsp_status_t dsp_finalize(dsp_t *D, uint32_t id, uint64_t tick);

/* D8 */
void dsp_gw_msg(uint32_t id, uint32_t award_bps, uint8_t out[DSP_GW_MSG]);
dsp_status_t dsp_gateway_rule(dsp_t *D, uint32_t id, uint32_t award_bps, const uint8_t *sig,
                              uint32_t sig_len);

/* D9 */
dsp_status_t dsp_ruling(const dsp_t *D, uint32_t id, dsp_ruling_t *out);
dsp_status_t dsp_ruling_digest(const dsp_t *D, uint32_t id, uint8_t out[32]);
bool dsp_award_amount(uint64_t escrow, uint32_t award_bps, uint64_t *buyer, uint64_t *seller);

/* D10. The live bonds of one case (0 for an unknown id). */
uint64_t dsp_case_locked(const dsp_t *D, uint32_t id);
bool dsp_check(const dsp_t *D);

#endif /* ZXV_DISPUTE_H */
