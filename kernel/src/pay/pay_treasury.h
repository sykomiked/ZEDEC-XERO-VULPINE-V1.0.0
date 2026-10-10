/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* pay_treasury.h — a shared treasury for a group or syndicate on pay_ledger,
 * spent only by m-of-n signed approvals.
 *
 * MODEL
 * -----
 *   T1  A treasury is one pay_ledger account (owner = the group's owner id,
 *       one asset, Financial capital form) plus a member list of up to
 *       PAY_TR_MAX_MEMBERS signer ids, an approval threshold m (1 <= m <= n)
 *       and a higher threshold m_large (m <= m_large <= n) for spends of at
 *       least `large_amount`.
 *   T2  Spending is a PROPOSAL (to, amount, memo, expiry tick). Its digest is
 *       SHA3-256 over "ZXV-TREASURY-PROPOSAL-v1" || treasury id || proposal
 *       id || asset || to || amount || created || expires || memo.
 *   T3  An APPROVAL is a signed record. The signer signs the canonical
 *       message from pay_treasury_approval_msg:
 *           "ZXV-TREASURY-APPROVE-v1" || treasury id (32) || proposal id (8,
 *           big-endian) || signer (4, big-endian) || proposal digest (32)
 *       and the treasury checks it through the caller's verify callback (an
 *       ML-DSA / Ed25519 / hybrid verifier from kernel/src/pqsec or mlkem;
 *       this module does no signature maths itself). A rejection is signed
 *       the same way with "ZXV-TREASURY-REJECT-v1". Each accepted approval or
 *       rejection is logged with SHA3(signature) in a hash-chained log.
 *   T4  States: OPEN -> APPROVED (approvals >= required) -> EXECUTED; OPEN ->
 *       REJECTED (rejections make the threshold unreachable); OPEN or
 *       APPROVED -> EXPIRED (tick >= expires).
 *   T5  LIMITS: a per-transaction maximum (checked at proposal and at
 *       execution) and a per-period maximum over periods of `period_len`
 *       ticks (checked at execution). 0 = no limit.
 *   T6  EXECUTION is one pay_ledger transfer whose idempotency key is the
 *       proposal digest, so a proposal can move money at most once even if
 *       execute is called again (PAY_DUPLICATE is reported as such).
 *   No interest, no overdraft, no borrowing: the treasury can only spend
 *   what it holds (pay_ledger refuses DEBIT below zero).
 *
 * HONEST LIMITS. Multi-signature control is a governance tool, not a legal
 * structure: whether a group treasury is a partnership, trust, collective
 * investment scheme or client-money account is a legal question for
 * counsel, and holding money for others may need licences. The security of
 * the approvals is exactly the security of the verify callback and of the
 * members' keys. Schema validity is not certification; there is no SWIFT or
 * CIPS connectivity. Freestanding: no libc, no allocation, no floating point.
 */
#ifndef ZXV_PAY_TREASURY_H
#define ZXV_PAY_TREASURY_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "pay_util.h"
#include "pay_ledger.h"

#define PAY_TR_MAX_MEMBERS   32u
#define PAY_TR_MAX_PROPOSALS 64u
#define PAY_TR_LOG           256u
#define PAY_TR_MSG_LEN       (23u + 32u + 8u + 4u + 32u) /* 99 */

/* Returns true iff `sig` is a valid signature by `signer` over msg. */
typedef bool (*pay_tr_verify_fn)(void *ctx, uint32_t signer, const uint8_t *msg, size_t msg_len,
                                 const uint8_t *sig, size_t sig_len);

typedef enum {
    PAY_TR_OPEN = 0,
    PAY_TR_APPROVED = 1,
    PAY_TR_EXECUTED = 2,
    PAY_TR_REJECTED = 3,
    PAY_TR_EXPIRED = 4
} pay_tr_state_t;

typedef struct {
    uint8_t id[32];       /* the group / syndicate object id           */
    uint32_t group_owner; /* ledger owner id of the treasury account    */
    uint16_t asset;
    uint32_t members[PAY_TR_MAX_MEMBERS];
    uint32_t n_members;
    uint32_t m;                /* T1 threshold                              */
    uint32_t m_large;          /* T1 threshold for large spends             */
    uint64_t large_amount;     /* 0 = no large tier                         */
    uint64_t limit_per_tx;     /* T5, 0 = none                          */
    uint64_t limit_per_period; /* T5, 0 = none                          */
    uint64_t period_len;       /* ticks, 0 = no periods                 */
} pay_treasury_cfg_t;

typedef struct {
    bool used;
    uint64_t id;
    uint8_t state;
    uint32_t proposer;
    uint32_t to_acct;
    uint64_t amount;
    uint64_t created, expires;
    char memo[PAY_MEMO_MAX + 1];
    uint8_t digest[PAY_HASH_LEN];
    uint32_t approved_mask; /* bit i = members[i] approved */
    uint32_t rejected_mask;
    uint32_t required;
    uint64_t ledger_seq; /* set when executed */
} pay_tr_proposal_t;

typedef struct {
    uint64_t proposal;
    uint32_t signer;
    uint8_t approve; /* 1 approve, 0 reject */
    uint64_t tick;
    uint8_t sig_hash[PAY_HASH_LEN];
    uint8_t prev[PAY_HASH_LEN]; /* chain hash before this record */
    uint8_t hash[PAY_HASH_LEN];
} pay_tr_log_t;

typedef struct {
    pay_ledger_t *L;
    pay_treasury_cfg_t cfg;
    uint32_t acct;
    pay_tr_verify_fn verify;
    void *verify_ctx;
    pay_tr_proposal_t prop[PAY_TR_MAX_PROPOSALS];
    uint64_t next_id;
    uint64_t period_start;
    uint64_t spent_period;
    uint64_t spent_total;
    pay_tr_log_t log[PAY_TR_LOG]; /* ring */
    uint64_t n_log;
    uint8_t log_head[PAY_HASH_LEN];
} pay_treasury_t;

/* Validate the config (T1; members unique and non-empty), open the treasury
 * account on L and bind the verify callback (required). */
pay_status_t pay_treasury_init(pay_treasury_t *T, pay_ledger_t *L, const pay_treasury_cfg_t *cfg,
                               pay_tr_verify_fn verify, void *verify_ctx);
bool pay_treasury_is_member(const pay_treasury_t *T, uint32_t who);
/* Approvals needed for an amount (T1). */
uint32_t pay_treasury_required(const pay_treasury_t *T, uint64_t amount);

/* T2: a member proposes a spend to ledger account `to_acct` (same asset). */
pay_status_t pay_treasury_propose(pay_treasury_t *T, uint32_t proposer, uint32_t to_acct,
                                  uint64_t amount, uint64_t tick, uint64_t ttl, const char *memo,
                                  uint64_t *out_id);
const pay_tr_proposal_t *pay_treasury_proposal(const pay_treasury_t *T, uint64_t id);

/* T3: the canonical message a signer signs. `approve` false gives the
 * rejection message. Returns the length (PAY_TR_MSG_LEN) or 0. */
size_t pay_treasury_approval_msg(const pay_treasury_t *T, uint64_t id, uint32_t signer,
                                 bool approve, uint8_t out[PAY_TR_MSG_LEN]);

/* T3/T4: record a signed approval (or rejection). PAY_DUPLICATE if this
 * member already voted on this proposal; PAY_ERR_POLICY if the signature
 * does not verify or the signer is not a member; PAY_ERR_STATE if the
 * proposal is not OPEN (an expired one is moved to EXPIRED). */
pay_status_t pay_treasury_approve(pay_treasury_t *T, uint64_t id, uint32_t signer,
                                  const uint8_t *sig, size_t sig_len, uint64_t tick);
pay_status_t pay_treasury_reject(pay_treasury_t *T, uint64_t id, uint32_t signer,
                                 const uint8_t *sig, size_t sig_len, uint64_t tick);

/* T5/T6: execute an APPROVED, unexpired proposal. */
pay_status_t pay_treasury_execute(pay_treasury_t *T, uint64_t id, uint64_t tick, pay_receipt_t *rc);

/* Balance held by the treasury. */
uint64_t pay_treasury_balance(const pay_treasury_t *T);
/* Every EXECUTED proposal had enough distinct member approvals; period and
 * total spend agree with the executed proposals; the log chain verifies. */
bool pay_treasury_check(const pay_treasury_t *T);

#endif /* ZXV_PAY_TREASURY_H */
