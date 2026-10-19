/* SPDX-License-Identifier: Apache-2.0 */
#ifndef ZXV_PAY_DIVIDEND_H
#define ZXV_PAY_DIVIDEND_H

#include "pay_ledger.h"

/* Opt-in belongs to the payout account, not to an online session. Changes to
 * account flags and designation of the eligible share asset require caller
 * authorization, just like existing ledger configuration operations. */
#define PAY_ACCT_DIVIDEND_OPT_IN    0x10u
#define PAY_DIVIDEND_MAX_RECIPIENTS (PAY_MAX_LINES - 2u)

typedef struct {
    uint32_t holding_account;
    uint32_t payout_account;
} pay_dividend_member_t;

typedef struct {
    uint32_t n_members;
    uint64_t eligible_holdings;
    uint64_t from_fees[PAY_DIVIDEND_MAX_RECIPIENTS];
    uint64_t from_external[PAY_DIVIDEND_MAX_RECIPIENTS];
    uint64_t paid_fees, paid_external;
    uint64_t undistributed_fees, undistributed_external;
} pay_dividend_result_t;

/* Distribute from two distinct, already funded financial accounts in ONE
 * existing ledger posting, without minting or charging another fee. Each
 * source is allocated separately: floor(source_budget * holdings / total).
 * Whole-unit rounding remains in its own source account. Result fields are
 * committed only on PAY_OK or PAY_DUPLICATE. Source budgets are ceilings;
 * the actual source debits must be funded, as enforced by pay_ledger_post.
 *
 * The caller supplies a complete authorized snapshot of at most six mappings.
 * Duplicate holding/payout accounts, pools as recipients, cross-owner mapping,
 * wrong assets/capital forms and total-holdings overflow are refused. Weight
 * comes from current eligible-share DEBIT balances, never caller numbers.
 * Non-opted-in and zero-holding mappings receive nothing. Offline accounts
 * remain eligible. A zero-payable distribution is refused without mutation.
 *
 * Caller supplies normal journal identifiers/nonces in metadata. No heap,
 * float or native 128-bit type. Serialize access to the ledger and keep its
 * normal bounded-journal replay limits. This primitive does not discover all
 * participants, authorize snapshots or orchestrate larger distributions.
 */
pay_status_t pay_dividend_distribute(pay_ledger_t *ledger, const pay_posting_req_t *metadata,
                                     uint16_t eligible_share_asset, uint32_t fee_pool,
                                     uint64_t fee_budget, uint32_t external_pool,
                                     uint64_t external_budget, const pay_dividend_member_t *members,
                                     uint32_t n, pay_dividend_result_t *result,
                                     pay_receipt_t *receipt);

#endif
