/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* pay_bounty.h — community bounties paid by the system: anyone posts a bug
 * or platform-improvement bounty for free, and the network mints Vino
 * Floating Vouchers to pay the person who fixes it, only after the fix
 * passes an audit and an independent peer review.
 *
 * OWNER DECISION (2026-10-15, in substance): "Anybody can post a request,
 * but the user that posts the request doesn't pay for it. The system pays
 * for its own upkeep by issuing Vino floating vouchers upon successful
 * delivery and audit peer review." Bounty minting is one more way VFV is
 * issued for verified work, beside pay_farm.h.
 *
 * LIFECYCLE (post, deliver, audit, review, mint)
 *   B1  POST IS FREE. pay_bounty_post records a bounty: poster, kind (bug,
 *       security, improvement, docs), tier 1..5 and a 32-byte subject digest
 *       (the hash of the issue text). Nothing is posted to the ledger and the
 *       poster pays nothing. A subject may be posted once (duplicates are
 *       refused), a poster may hold at most max_open open bounties, and a
 *       kind/tier with no value in the schedule cannot be posted.
 *   B2  THE POSTER DOES NOT SET THE PRICE. A bounty is worth
 *           value(kind, tier) = base[kind] * FIB(tier)   FIB = 1, 2, 3, 5, 8
 *       VFV minor units, from the operator's schedule (market rates). A
 *       poster who pays nothing cannot be allowed to name the amount.
 *   B3  DELIVER. A fixer signs a delivery (bounty id, fixer, 32-byte
 *       deliverable digest, e.g. the commit or patch hash); the operator's
 *       verify callback checks the signature over pay_bounty_delivery_digest.
 *       One delivery per bounty at a time; a deliverable digest may be used
 *       once, ever, across all bounties (replay table); a fixer may have at
 *       most max_active deliveries in flight. The poster may not deliver on
 *       their own bounty unless allow_self_fix is set: posting is free, so
 *       one person could otherwise write both the question and the answer.
 *   B4  AUDIT. pay_bounty_audit runs the operator's audit callback on the
 *       delivery (for a bug: it reproduced before, it does not reproduce
 *       now, the regression suite passes). PASS draws the review panel;
 *       FAIL rejects the delivery and the bounty reopens; UNAVAILABLE
 *       leaves it waiting for a later audit call.
 *   B5  INDEPENDENT PANEL. On PASS, panel_size reviewers are drawn from the
 *       reviewer pool by a SHA3 stream seeded with ("ZXV-BOUNTY-PANEL-v1" ||
 *       beacon || bounty id || deliverable). The poster, the fixer and
 *       suspended reviewers are never drawn, so nobody picks their own
 *       reviewers and a fixer can never approve their own work. The beacon
 *       must not be known when the delivery was made. Too few eligible
 *       reviewers: the audit is refused (PAY_ERR_LIMIT) and can be retried.
 *   B6  FOUR-VALUED VERDICT (the paraconsistent rule; values equal
 *       LPRES_STATE_* in lpres.h). Only panel members vote, once each,
 *       signed over pay_bounty_vote_digest; an approving vote names the tier
 *       it judges the work to deserve (<= the posted tier). With a approvals
 *       and r rejections and quorum q:
 *           a >= q and r == 0  TRUE     approved
 *           r >= q and a == 0  FALSE    rejected, the bounty reopens
 *           a > 0 and r > 0    BOTH     held: nothing mints until the
 *                                       arbiter rules (pay_bounty_rule)
 *           otherwise          NEITHER  still waiting
 *       The verdict is taken at epoch close, so a dissent cast any time
 *       before then holds the bounty. The paid tier is the LOWEST tier any
 *       approving reviewer named, so the system never overpays.
 *   B7  EPOCH CAP. pay_bounty_close_epoch mints approved bounties in the
 *       order they were approved while the epoch total stays within
 *       epoch_cap and the fixer's own epoch total stays within
 *       fixer_share * epoch_cap (default 8/21, as pay_farm W4; a fixer's
 *       first bounty of the epoch always fits that share). A bounty that does
 *       not fit is CARRIED to the next epoch in the same order, never lost:
 *       it is work already verified. Every schedule value must fit in
 *       epoch_cap, so every approved bounty mints eventually.
 *   B8  FEE AND MINT. As pay_farm W5: gross g = the bounty's value, the
 *       0.08889% assurance fee t on g with the fixer's sub-unit carry, and
 *       ONE pay_ledger posting: issuer CREDIT +g, fixer DEBIT +(g - t), and
 *       t split exactly into the four fee buckets (50% reserve floor, 25%
 *       V-Bill dividend, 15% infrastructure/node bounties, 10% regenerative).
 *       This module mints; it never draws on the 15% node-bounty bucket,
 *       which swarm token yield feeds and spends (kernel/src/swarm).
 *   B9  FRAUD, ISOLATION NOT CONFISCATION. pay_bounty_fraud takes an
 *       arbiter-signed finding against a delivery. An unpaid bounty reopens
 *       (nothing mints; the issue still needs a real fix); a paid one is
 *       marked, never clawed back (as peer_audit C).
 *       Either way the fixer and every reviewer who approved get a strike,
 *       and at max_strikes they are suspended: a suspended fixer cannot
 *       deliver, a suspended reviewer is not drawn.
 *
 * INVARIANTS (pay_bounty_check)
 *   I1  No mint without verified work: every PAID bounty passed the audit
 *       and has a TRUE verdict or an arbiter approval, and was paid exactly
 *       value(kind, paid tier).
 *   I2  The current epoch's minting is <= epoch_cap; the issuer's CREDIT ==
 *       total minted; minted == fees + net == sum of paid bounties.
 *   I3  No panel member of any bounty is its poster or its fixer.
 *   I4  The ledger invariants L1-L3 hold.
 *
 * HONEST LIMITS. Peer review trusts the panel: if q of the drawn reviewers
 * collude with the fixer, a fake fix can mint, which is why the panel is
 * drawn by a beacon, any dissent holds the bounty, and fraud strikes the
 * approvers. Who may join the reviewer pool, and how costly a fake identity
 * is, is the operator's and ident's job (Sybil cost), not this module's.
 * The audit is only as good as the operator's callback. A panel that never
 * reaches quorum leaves the bounty waiting; there is no timeout yet. Paying reviewers
 * for their reviews is not built. Whether VFV is store credit, and whether a
 * bounty payment is income, wages or something else, is a legal and tax
 * question for counsel (zxv-legal/VINO_CLOSED_LOOP_FRAMING_REVIEW.md; every
 * point there is pending counsel). Not thread-safe. Freestanding C11: no
 * libc, no allocation, no floating point.
 */
#ifndef ZXV_PAY_BOUNTY_H
#define ZXV_PAY_BOUNTY_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "pay_util.h"
#include "pay_ledger.h"
#include "pay_assure.h"

#define PAY_BTY_MAX       256u  /* bounties                         */
#define PAY_BTY_PEOPLE    128u  /* fixers and reviewers             */
#define PAY_BTY_PANEL_MAX 9u    /* largest review panel             */
#define PAY_BTY_SEEN      1024u /* deliverable replay table (2^k)   */
#define PAY_BTY_TIERS     5u    /* tier 1..5, worth FIB 1,2,3,5,8   */

typedef enum {
    PAY_BTY_BUG = 0,
    PAY_BTY_SECURITY = 1,
    PAY_BTY_IMPROVEMENT = 2, /* platform improvement */
    PAY_BTY_DOCS = 3,
    PAY_BTY_KIND_COUNT = 4
} pay_bty_kind_t;

typedef enum {
    PAY_BTY_OPEN = 0,  /* waiting for a delivery                   */
    PAY_BTY_DELIVERED, /* waiting for the audit                    */
    PAY_BTY_REVIEW,    /* audit passed; the panel is voting        */
    PAY_BTY_HELD,      /* BOTH: approvals and rejections; arbiter  */
    PAY_BTY_APPROVED,  /* queued to mint at the next epoch close   */
    PAY_BTY_PAID,      /* minted                                   */
    PAY_BTY_WITHDRAWN  /* the poster withdrew it before a delivery */
} pay_bty_state_t;

/* Four-valued verdict, value-equal to LPRES_STATE_* (lpres.h). */
typedef enum {
    PAY_BTY_V_NEITHER = 0,
    PAY_BTY_V_TRUE = 1,
    PAY_BTY_V_FALSE = 2,
    PAY_BTY_V_BOTH = 3
} pay_bty_verdict_t;

typedef enum {
    PAY_BTY_AUDIT_PASS = 0,
    PAY_BTY_AUDIT_FAIL,
    PAY_BTY_AUDIT_UNAVAILABLE
} pay_bty_audit_t;

/* Signature check: `signer` signed the 32-byte digest. The arbiter's
 * rulings are checked with signer == cfg.arbiter. */
typedef bool (*pay_bty_verify_fn)(void *ctx, uint32_t signer, const uint8_t digest[32],
                                  const uint8_t *sig, size_t sig_len);
/* Audit the deliverable against the bounty's subject (B4). */
typedef pay_bty_audit_t (*pay_bty_audit_fn)(void *ctx, uint8_t kind, const uint8_t subject[32],
                                            const uint8_t deliverable[32]);

typedef struct {
    uint64_t base[PAY_BTY_KIND_COUNT]; /* VFV minor units at tier 1; 0 = kind closed */
    uint64_t epoch_cap;                /* VFV minor units minted per epoch at most   */
    pay_rat_t fixer_share;             /* of epoch_cap per fixer; default 8/21       */
    uint32_t panel_size;               /* default 5                                   */
    uint32_t quorum;                   /* default 3                                   */
    uint32_t max_open;                 /* open bounties per poster; default 8         */
    uint32_t max_active;               /* deliveries in flight per fixer; default 3   */
    uint32_t max_strikes;              /* default 3                                   */
    uint32_t arbiter;                  /* signer id of the dispute funnel             */
    bool allow_self_fix;               /* default false (B3)                          */
} pay_bty_cfg_t;

typedef struct {
    bool used;
    uint32_t owner;
    uint32_t vfv_acct; /* opened on first delivery */
    bool reviewer;     /* in the reviewer pool     */
    bool suspended;
    uint32_t strikes;
    uint32_t active;       /* deliveries in flight */
    uint64_t fee_carry;    /* pay_assure F2 remainder */
    uint64_t earned;       /* lifetime gross minted to this fixer */
    uint64_t epoch_minted; /* gross minted to this fixer this epoch */
} pay_bty_person_t;

typedef struct {
    uint32_t id;   /* index + 1 */
    uint8_t state; /* pay_bty_state_t */
    uint8_t kind;  /* pay_bty_kind_t  */
    uint8_t tier;  /* posted tier 1..5 */
    uint8_t paid_tier;
    uint32_t poster;
    uint8_t subject[32];
    /* the current (or last) delivery */
    uint32_t fixer;
    uint8_t deliverable[32];
    bool audited;
    bool ruled; /* the arbiter approved it (B6 BOTH) */
    uint32_t panel[PAY_BTY_PANEL_MAX];
    uint32_t n_panel;
    uint8_t vote[PAY_BTY_PANEL_MAX]; /* 0 none, 1 approve, 2 reject */
    uint8_t vote_tier[PAY_BTY_PANEL_MAX];
    uint64_t approve_seq; /* order of approval, for B7 */
    uint64_t paid;        /* gross minted */
    uint64_t paid_epoch;
    bool fraud;
} pay_bounty_t;

typedef struct {
    uint64_t posted, delivered, audit_pass, audit_fail, rejected, held, paid, carried, frauds;
} pay_bty_stats_t;

typedef struct {
    pay_ledger_t *L;
    pay_bty_cfg_t cfg;
    pay_bty_verify_fn verify;
    pay_bty_audit_fn audit;
    void *cb_ctx;
    uint32_t issuer_acct;
    uint32_t fee_acct[PAY_ASSURE_BUCKETS];
    pay_bounty_t b[PAY_BTY_MAX];
    uint32_t n_b;
    pay_bty_person_t p[PAY_BTY_PEOPLE];
    uint32_t n_p;
    uint8_t seen[PAY_BTY_SEEN][32];
    uint8_t seen_used[PAY_BTY_SEEN];
    uint64_t epoch;
    uint64_t epoch_minted;
    uint64_t approve_ctr;
    uint64_t minted, fees, net;
    pay_bty_stats_t stats;
} pay_bounty_ctx_t;

void pay_bounty_cfg_default(pay_bty_cfg_t *c);

/* B2: base[kind] * FIB(tier); 0 for a closed kind or a bad tier. */
uint64_t pay_bounty_value(const pay_bty_cfg_t *c, uint8_t kind, uint8_t tier);

/* Opens a dedicated VFV ISSUER account (owner platform_owner) and binds the
 * four fee-bucket accounts (VFV, COMMONS, indexed by pay_assure_bucket_t).
 * Refuses a schedule value above epoch_cap (B7). */
pay_status_t pay_bounty_init(pay_bounty_ctx_t *B, pay_ledger_t *L, const pay_bty_cfg_t *cfg,
                             uint32_t platform_owner, const uint32_t fee_acct[PAY_ASSURE_BUCKETS],
                             pay_bty_verify_fn verify, pay_bty_audit_fn audit, void *cb_ctx);

/* Adds `owner` to the reviewer pool (the operator's call). */
pay_status_t pay_bounty_add_reviewer(pay_bounty_ctx_t *B, uint32_t owner);
const pay_bty_person_t *pay_bounty_person(const pay_bounty_ctx_t *B, uint32_t owner);
const pay_bounty_t *pay_bounty_get(const pay_bounty_ctx_t *B, uint32_t id);

/* B1. *id_out gets the bounty id (>= 1). */
pay_status_t pay_bounty_post(pay_bounty_ctx_t *B, uint32_t poster, uint8_t kind, uint8_t tier,
                             const uint8_t subject[32], uint32_t *id_out);
/* The poster withdraws an OPEN bounty. */
pay_status_t pay_bounty_withdraw(pay_bounty_ctx_t *B, uint32_t id, uint32_t poster);

/* B3 */
void pay_bounty_delivery_digest(uint32_t id, uint32_t fixer, const uint8_t deliverable[32],
                                uint8_t out[32]);
pay_status_t pay_bounty_deliver(pay_bounty_ctx_t *B, uint32_t id, uint32_t fixer,
                                const uint8_t deliverable[32], const uint8_t *sig, size_t sig_len);

/* B4 + B5. PAY_OK on PASS (panel drawn), PAY_ERR_POLICY on FAIL (bounty
 * reopened), PAY_ERR_STATE when the audit is unavailable. */
pay_status_t pay_bounty_audit(pay_bounty_ctx_t *B, uint32_t id, const uint8_t beacon[32]);

/* B6 */
void pay_bounty_vote_digest(uint32_t id, const uint8_t deliverable[32], uint32_t reviewer,
                            bool approve, uint8_t tier, uint8_t out[32]);
pay_status_t pay_bounty_vote(pay_bounty_ctx_t *B, uint32_t id, uint32_t reviewer, bool approve,
                             uint8_t tier, const uint8_t *sig, size_t sig_len);
pay_bty_verdict_t pay_bounty_verdict(const pay_bounty_ctx_t *B, uint32_t id);

/* B6 BOTH: the arbiter approves (at `tier`) or rejects a HELD bounty. */
void pay_bounty_rule_digest(uint32_t id, const uint8_t deliverable[32], bool approve, uint8_t tier,
                            uint8_t out[32]);
pay_status_t pay_bounty_rule(pay_bounty_ctx_t *B, uint32_t id, bool approve, uint8_t tier,
                             const uint8_t *sig, size_t sig_len);

/* B9 */
void pay_bounty_fraud_digest(uint32_t id, const uint8_t deliverable[32], uint8_t out[32]);
pay_status_t pay_bounty_fraud(pay_bounty_ctx_t *B, uint32_t id, const uint8_t *sig, size_t sig_len);

/* B6-B8: take the verdicts, mint within the caps, advance the epoch. */
pay_status_t pay_bounty_close_epoch(pay_bounty_ctx_t *B, uint64_t tick);

/* I1-I4 */
bool pay_bounty_check(const pay_bounty_ctx_t *B);

#endif /* ZXV_PAY_BOUNTY_H */
