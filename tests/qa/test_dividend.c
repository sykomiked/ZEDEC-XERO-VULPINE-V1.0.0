/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "pay_dividend.h"

static pay_ledger_t ledger, before;
static uint64_t serial;
static uint16_t money, shares;
static uint32_t fee_pool, external_pool, money_issuer, share_issuer;
static pay_dividend_member_t members[3];
static unsigned checks;

static pay_posting_req_t request(void)
{
    pay_posting_req_t req;
    memset(&req, 0, sizeof(req));
    uint8_t random[16] = {0};
    uint64_t id = ++serial;
    for (unsigned i = 0; i < 8; i++) {
        random[i] = (uint8_t) (id >> (i * 8));
        req.idem_key[i] = random[i];
    }
    pay_uetr_from_random(random, req.uetr);
    snprintf(req.e2e, sizeof(req.e2e), "div-%llu", (unsigned long long) id);
    req.initiator = 100;
    req.nonce = id;
    req.tick = id;
    return req;
}

static uint32_t open_account(uint32_t owner, uint16_t asset, uint8_t flags)
{
    uint32_t id;
    assert(pay_ledger_open(&ledger, owner, asset, PAY_CAP_FINANCIAL, flags, &id) == PAY_OK);
    return id;
}

static void issue(uint32_t issuer, uint32_t to, uint64_t amount)
{
    pay_posting_req_t req = request();
    assert(pay_ledger_issue(&ledger, &req, issuer, to, amount, 0) == PAY_OK);
}

static void setup(const uint64_t weights[3], uint64_t fees, uint64_t external)
{
    pay_ledger_init(&ledger, 0);
    assert(pay_ledger_add_fiat(&ledger, "USD", 840, 2, &money) == PAY_OK);
    assert(pay_ledger_add_asset(&ledger, "VBILL", 0, PAY_ASSET_SHARE, &shares) == PAY_OK);
    money_issuer = open_account(100, money, PAY_ACCT_ISSUER);
    share_issuer = open_account(100, shares, PAY_ACCT_ISSUER);
    fee_pool = open_account(100, money, 0);
    external_pool = open_account(100, money, 0);
    if (fees) issue(money_issuer, fee_pool, fees);
    if (external) issue(money_issuer, external_pool, external);
    for (unsigned i = 0; i < 3; i++) {
        members[i].holding_account = open_account(i + 1, shares, 0);
        members[i].payout_account = open_account(i + 1, money, PAY_ACCT_DIVIDEND_OPT_IN);
        if (weights[i]) issue(share_issuer, members[i].holding_account, weights[i]);
    }
    assert(pay_ledger_check(&ledger));
}

static pay_status_t distribute(const pay_posting_req_t *req, uint64_t fees, uint64_t external,
                               pay_dividend_result_t *out)
{
    return pay_dividend_distribute(&ledger, req, shares, fee_pool, fees, external_pool, external,
                                   members, 3, out, 0);
}

static void unchanged_failure(pay_posting_req_t *req, uint64_t fees, uint64_t external,
                              pay_status_t expected)
{
    memcpy(&before, &ledger, sizeof(ledger));
    pay_dividend_result_t result, saved;
    memset(&result, 0xa5, sizeof(result));
    memcpy(&saved, &result, sizeof(result));
    assert(distribute(req, fees, external, &result) == expected);
    assert(memcmp(&before, &ledger, sizeof(ledger)) == 0);
    assert(memcmp(&saved, &result, sizeof(result)) == 0);
    checks++;
}

static void test_separate_sources_replay_and_reverse(void)
{
    const uint64_t weights[3] = {5, 3, 2};
    setup(weights, 101, 59);
    pay_posting_req_t req = request(), saved = req;
    pay_dividend_result_t result;
    uint64_t seq = ledger.seq;
    assert(distribute(&req, 101, 59, &result) == PAY_OK);
    assert(memcmp(&req, &saved, sizeof(req)) == 0);
    assert(ledger.seq == seq + 1);
    const uint64_t fee_parts[3] = {50, 30, 20}, external_parts[3] = {29, 17, 11};
    for (unsigned i = 0; i < 3; i++) {
        assert(result.from_fees[i] == fee_parts[i]);
        assert(result.from_external[i] == external_parts[i]);
        assert(ledger.acct[members[i].payout_account].debit == fee_parts[i] + external_parts[i]);
        assert(ledger.acct[members[i].holding_account].debit == weights[i]);
        checks++;
    }
    assert(result.paid_fees == 100 && result.paid_external == 57);
    assert(result.undistributed_fees == 1 && result.undistributed_external == 2);
    assert(ledger.acct[fee_pool].debit == 1 && ledger.acct[external_pool].debit == 2);
    assert(pay_ledger_check(&ledger) && pay_ledger_verify_chain(&ledger));
    memcpy(&before, &ledger, sizeof(ledger));
    assert(distribute(&req, 101, 59, &result) == PAY_DUPLICATE);
    assert(memcmp(&before, &ledger, sizeof(ledger)) == 0);
    unchanged_failure(&req, 102, 59, PAY_ERR_REPLAY);
    pay_posting_req_t reverse = request();
    assert(pay_ledger_reverse(&ledger, &reverse, req.uetr, 0) == PAY_OK);
    assert(ledger.acct[fee_pool].debit == 101 && ledger.acct[external_pool].debit == 59);
    for (unsigned i = 0; i < 3; i++) assert(!ledger.acct[members[i].payout_account].debit);
    assert(pay_ledger_check(&ledger) && pay_ledger_verify_chain(&ledger));
    checks += 4;
}

static void test_opt_in_and_policy_boundaries(void)
{
    const uint64_t weights[3] = {5, 3, 2};
    setup(weights, 101, 59);
    assert(pay_ledger_set_flags(&ledger, members[1].payout_account, 0) == PAY_OK);
    /* Frozen is not an online-session state; the ledger permits receiving. */
    assert(pay_ledger_set_flags(&ledger, members[0].payout_account,
                                PAY_ACCT_DIVIDEND_OPT_IN | PAY_ACCT_FROZEN) == PAY_OK);
    pay_posting_req_t req = request();
    pay_dividend_result_t result;
    assert(distribute(&req, 101, 59, &result) == PAY_OK);
    assert(result.eligible_holdings == 7);
    assert(!result.from_fees[1] && !result.from_external[1]);
    assert(!ledger.acct[members[1].payout_account].debit);
    assert(ledger.acct[members[0].payout_account].debit == 114);
    assert(ledger.acct[members[2].payout_account].debit == 44);
    checks++;

    setup(weights, 101, 59);
    req = request();
    unchanged_failure(&req, 1000, 59, PAY_ERR_FUNDS);
    assert(pay_ledger_set_flags(&ledger, external_pool, PAY_ACCT_FROZEN) == PAY_OK);
    unchanged_failure(&req, 101, 59, PAY_ERR_POLICY);
    assert(pay_ledger_set_flags(&ledger, external_pool, 0) == PAY_OK);
    uint32_t saved = members[1].holding_account;
    members[1].holding_account = members[0].holding_account;
    unchanged_failure(&req, 101, 59, PAY_ERR_ARG);
    members[1].holding_account = saved;
    saved = members[1].payout_account;
    members[1].payout_account = fee_pool;
    unchanged_failure(&req, 101, 59, PAY_ERR_POLICY);
    members[1].payout_account = members[0].payout_account;
    unchanged_failure(&req, 101, 59, PAY_ERR_ARG);
    members[1].payout_account = members[2].holding_account;
    unchanged_failure(&req, 101, 59, PAY_ERR_POLICY);
    members[1].payout_account = saved;
    for (unsigned i = 0; i < 3; i++)
        assert(pay_ledger_set_flags(&ledger, members[i].payout_account, 0) == PAY_OK);
    unchanged_failure(&req, 101, 59, PAY_ERR_POLICY);
    setup(weights, 101, 59);
    req = request();
    unchanged_failure(&req, 1, 1, PAY_ERR_LIMIT);
    unchanged_failure(&req, UINT64_MAX, UINT64_MAX, PAY_ERR_OVERFLOW);
}

static uint64_t state = 0x889f;
static uint64_t rnd(void)
{
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    return state;
}

static void test_random_allocations(void)
{
    for (unsigned trial = 0; trial < 500; trial++) {
        uint64_t weights[3] = {1 + rnd() % 1000, 1 + rnd() % 1000, 1 + rnd() % 1000};
        uint64_t fees = 1000 + rnd() % 10000, external = 1000 + rnd() % 10000;
        setup(weights, fees, external);
        uint64_t sum = 0;
        for (unsigned i = 0; i < 3; i++) {
            if (rnd() & 1)
                sum += weights[i];
            else
                assert(pay_ledger_set_flags(&ledger, members[i].payout_account, 0) == PAY_OK);
        }
        pay_posting_req_t req = request();
        if (!sum) {
            unchanged_failure(&req, fees, external, PAY_ERR_POLICY);
            continue;
        }
        pay_dividend_result_t result;
        assert(distribute(&req, fees, external, &result) == PAY_OK);
        uint64_t paid_fee = 0, paid_external = 0;
        for (unsigned i = 0; i < 3; i++) {
            uint64_t a = 0, b = 0;
            if (ledger.acct[members[i].payout_account].flags & PAY_ACCT_DIVIDEND_OPT_IN) {
                /* Host-only native wide oracle independent of pay_muldiv. */
                a = (uint64_t) ((__uint128_t) fees * weights[i] / sum);
                b = (uint64_t) ((__uint128_t) external * weights[i] / sum);
            }
            assert(result.from_fees[i] == a && result.from_external[i] == b);
            assert(ledger.acct[members[i].payout_account].debit == a + b);
            paid_fee += a;
            paid_external += b;
            checks++;
        }
        assert(ledger.acct[fee_pool].debit + paid_fee == fees);
        assert(ledger.acct[external_pool].debit + paid_external == external);
        assert(pay_ledger_check(&ledger) && pay_ledger_verify_chain(&ledger));
        checks++;
    }
}

static void test_capacity_and_total_overflow(void)
{
    const uint64_t weights[3] = {0, 0, 0};
    setup(weights, 600, 600);
    pay_posting_req_t req = request();
    unchanged_failure(&req, 600, 600, PAY_ERR_POLICY);
    pay_dividend_member_t maximum[PAY_DIVIDEND_MAX_RECIPIENTS];
    for (unsigned i = 0; i < PAY_DIVIDEND_MAX_RECIPIENTS; i++) {
        if (i < 3)
            maximum[i] = members[i];
        else {
            maximum[i].holding_account = open_account(i + 1, shares, 0);
            maximum[i].payout_account = open_account(i + 1, money, PAY_ACCT_DIVIDEND_OPT_IN);
        }
        issue(share_issuer, maximum[i].holding_account, 1);
    }
    req = request();
    pay_dividend_result_t result;
    assert(pay_dividend_distribute(&ledger, &req, shares, fee_pool, 600, external_pool, 600,
                                   maximum, PAY_DIVIDEND_MAX_RECIPIENTS, &result, 0) == PAY_OK);
    const pay_journal_t *journal = pay_ledger_find_uetr(&ledger, req.uetr);
    assert(journal && journal->n_lines == PAY_MAX_LINES);
    for (unsigned i = 0; i < PAY_DIVIDEND_MAX_RECIPIENTS; i++) {
        assert(result.from_fees[i] == 100 && result.from_external[i] == 100);
        assert(ledger.acct[maximum[i].payout_account].debit == 200);
    }
    memcpy(&before, &ledger, sizeof(ledger));
    assert(pay_dividend_distribute(&ledger, &req, shares, fee_pool, 600, external_pool, 600,
                                   maximum, PAY_DIVIDEND_MAX_RECIPIENTS + 1, &result,
                                   0) == PAY_ERR_LIMIT);
    assert(memcmp(&before, &ledger, sizeof(ledger)) == 0);
    checks += 2;

    /* Each balance is valid; their eligible total exceeds the uint64 bound. */
    for (unsigned i = 0; i < PAY_DIVIDEND_MAX_RECIPIENTS; i++) {
        uint32_t issuer = open_account(100 + i, shares, PAY_ACCT_ISSUER);
        uint64_t remaining = PAY_BAL_MAX - 2;
        while (remaining) {
            uint64_t chunk = remaining > ((uint64_t) 1 << 59) ? ((uint64_t) 1 << 59) : remaining;
            issue(issuer, maximum[i].holding_account, chunk);
            remaining -= chunk;
        }
    }
    assert(pay_ledger_check(&ledger));
    memcpy(&before, &ledger, sizeof(ledger));
    req = request();
    assert(pay_dividend_distribute(&ledger, &req, shares, fee_pool, 600, external_pool, 600,
                                   maximum, PAY_DIVIDEND_MAX_RECIPIENTS, &result,
                                   0) == PAY_ERR_OVERFLOW);
    assert(memcmp(&before, &ledger, sizeof(ledger)) == 0);
    checks++;
}

int main(void)
{
    test_separate_sources_replay_and_reverse();
    test_opt_in_and_policy_boundaries();
    test_random_allocations();
    test_capacity_and_total_overflow();
    printf("dividend: %u allocation, conservation and failure scenarios passed\n", checks);
    return 0;
}
