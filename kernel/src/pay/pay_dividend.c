/* SPDX-License-Identifier: Apache-2.0 */
#include "pay_dividend.h"

pay_status_t pay_dividend_distribute(pay_ledger_t *ledger, const pay_posting_req_t *metadata,
                                     uint16_t eligible_share_asset, uint32_t fee_pool,
                                     uint64_t fee_budget, uint32_t external_pool,
                                     uint64_t external_budget, const pay_dividend_member_t *members,
                                     uint32_t n, pay_dividend_result_t *result,
                                     pay_receipt_t *receipt)
{
    if (!ledger || !metadata || !members || !result || n == 0) return PAY_ERR_ARG;
    if (n > PAY_DIVIDEND_MAX_RECIPIENTS) return PAY_ERR_LIMIT;
    const pay_asset_t *asset = pay_ledger_asset(ledger, eligible_share_asset);
    if (!asset || asset->kind != PAY_ASSET_SHARE) return PAY_ERR_NO_ASSET;
    const pay_account_t *fees = pay_ledger_account(ledger, fee_pool);
    const pay_account_t *external = pay_ledger_account(ledger, external_pool);
    if (!fees || !external) return PAY_ERR_NO_ACCOUNT;
    if (fee_pool == external_pool || fees->asset != external->asset ||
        fees->cap != PAY_CAP_FINANCIAL || external->cap != PAY_CAP_FINANCIAL ||
        fees->asset == eligible_share_asset)
        return PAY_ERR_POLICY;

    uint64_t weights[PAY_DIVIDEND_MAX_RECIPIENTS];
    pay_dividend_result_t staged;
    pay_memset(&staged, 0, sizeof(staged));
    staged.n_members = n;
    for (uint32_t i = 0; i < n; i++) {
        weights[i] = 0;
        for (uint32_t j = 0; j < i; j++) {
            if (members[i].holding_account == members[j].holding_account ||
                members[i].payout_account == members[j].payout_account)
                return PAY_ERR_ARG;
        }
        const pay_account_t *holding = pay_ledger_account(ledger, members[i].holding_account);
        const pay_account_t *payout = pay_ledger_account(ledger, members[i].payout_account);
        if (!holding || !payout) return PAY_ERR_NO_ACCOUNT;
        if (holding->asset != eligible_share_asset || holding->credit != 0 ||
            holding->cap != PAY_CAP_FINANCIAL || payout->cap != PAY_CAP_FINANCIAL ||
            payout->asset != fees->asset || holding->owner != payout->owner ||
            members[i].payout_account == fee_pool || members[i].payout_account == external_pool)
            return PAY_ERR_POLICY;
        if (!(payout->flags & PAY_ACCT_DIVIDEND_OPT_IN)) continue;
        weights[i] = holding->debit;
        if (!pay_add_ok(staged.eligible_holdings, weights[i], &staged.eligible_holdings))
            return PAY_ERR_OVERFLOW;
    }
    if (staged.eligible_holdings == 0) return PAY_ERR_POLICY;

    pay_posting_req_t posting;
    pay_memcpy(&posting, metadata, sizeof(posting));
    posting.kind = PAY_KIND_ADJUST;
    posting.n_lines = 2;
    for (uint32_t i = 0; i < n; i++) {
        uint64_t payout;
        if (!pay_muldiv(fee_budget, weights[i], staged.eligible_holdings, &staged.from_fees[i],
                        0) ||
            !pay_muldiv(external_budget, weights[i], staged.eligible_holdings,
                        &staged.from_external[i], 0) ||
            !pay_add_ok(staged.from_fees[i], staged.from_external[i], &payout) ||
            payout > INT64_MAX)
            return PAY_ERR_OVERFLOW;
        if (!pay_add_ok(staged.paid_fees, staged.from_fees[i], &staged.paid_fees) ||
            !pay_add_ok(staged.paid_external, staged.from_external[i], &staged.paid_external))
            return PAY_ERR_OVERFLOW;
        if (payout) {
            posting.lines[posting.n_lines++] =
                (pay_line_t){members[i].payout_account, (int64_t) payout, 0};
        }
    }
    if (!staged.paid_fees && !staged.paid_external) return PAY_ERR_LIMIT;
    if (staged.paid_fees > INT64_MAX || staged.paid_external > INT64_MAX) return PAY_ERR_OVERFLOW;
    posting.lines[0] = (pay_line_t){fee_pool, -(int64_t) staged.paid_fees, 0};
    posting.lines[1] = (pay_line_t){external_pool, -(int64_t) staged.paid_external, 0};
    staged.undistributed_fees = fee_budget - staged.paid_fees;
    staged.undistributed_external = external_budget - staged.paid_external;
    pay_status_t status = pay_ledger_post(ledger, &posting, receipt);
    if (status == PAY_OK || status == PAY_DUPLICATE) pay_memcpy(result, &staged, sizeof(staged));
    return status;
}
