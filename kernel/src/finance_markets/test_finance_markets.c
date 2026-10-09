/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_finance_markets.c — assert the NUMBERS, not just the control flow.
 * Host-only (TEST_HOST): surplus_real_t is double here, so we can hand-check. */
#include <stdio.h>
#include <math.h>
#include "finance_markets.h"

static int failures = 0;
static int checks = 0;

#define CHECK(cond, msg)                                                                           \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(cond)) {                                                                             \
            failures++;                                                                            \
            printf("  FAIL: %s\n", msg);                                                           \
        }                                                                                          \
    } while (0)

/* surplus_real_t is double under TEST_HOST — compare with a tolerance. */
static int close_to(surplus_real_t a, double b)
{
    double d = (double) a - b;
    if (d < 0) d = -d;
    return d < 1e-6;
}
#define CHECK_NUM(got, want, msg)                                                                  \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!close_to((got), (want))) {                                                            \
            failures++;                                                                            \
            printf("  FAIL: %s (got %.10f want %.10f)\n", msg, (double) (got), (double) (want));   \
        }                                                                                          \
    } while (0)

/* One big book — never on the stack (portfolio + ledger are large). */
static fm_book_t book;

/* ---- Anchor 1: the REUSED financial pricers hit known regression values ---- */
static void test_pricer_regression(void)
{
    printf("[1] pricer regression (real financial_price_* pricers)\n");
    fm_book_init(&book);

    /* FUTURE: F = S(1+(r-q)T) - conv*T, coverage neutralized to 1.
     * S=100, r=0.05, q=0, T=1 -> 100*1.05 = 105 */
    int32_t fut = fm_track_instrument(&book, INST_FUTURE, "ESZ4");
    CHECK(fut >= 0, "track future");
    financial_instrument_t *f = fm_instrument(&book, (uint32_t) fut);
    f->reg_price = SR_FROM_INT(100);
    f->reg_rate = SR_FROM_FLOAT(0.05);
    f->reg_dividend = SR_ZERO;
    f->reg_time = SR_ONE;
    f->reg_conv_yield = SR_ZERO;
    f->coverage_ratio = SR_ONE; /* neutralize the M5 coverage multiplier */
    CHECK_NUM(financial_price_future(f), 105.0, "future price == 105");

    /* BOND: par bond. coupon=5, face=100, y=0.05, n=10 -> price = 100 */
    int32_t bnd = fm_track_instrument(&book, INST_BOND, "UST10");
    CHECK(bnd >= 0, "track bond");
    financial_instrument_t *b = fm_instrument(&book, (uint32_t) bnd);
    b->reg_coupon = SR_FROM_INT(5);
    b->reg_face_value = SR_FROM_INT(100);
    b->reg_yield = SR_FROM_FLOAT(0.05);
    b->reg_maturity_years = 10;
    b->coverage_ratio = SR_ONE;
    CHECK_NUM(financial_price_bond(b), 100.0, "bond price == 100 (par)");

    /* OPTION (call): S=K=100, sigma=0.2, r=0.05, T=1.
     * d1=0.35 -> N_d1=0.5875, N_d2=0.5375 -> 58.75-53.75 = 5.0 */
    int32_t opt = fm_track_instrument(&book, INST_OPTION, "SPX-C");
    CHECK(opt >= 0, "track option");
    financial_instrument_t *o = fm_instrument(&book, (uint32_t) opt);
    o->option_type = OPTION_CALL;
    o->reg_price = SR_FROM_INT(100);
    o->reg_strike = SR_FROM_INT(100);
    o->reg_volatility = SR_FROM_FLOAT(0.2);
    o->reg_rate = SR_FROM_FLOAT(0.05);
    o->reg_time = SR_ONE;
    o->coverage_ratio = SR_ONE;
    CHECK_NUM(financial_price_option(o), 5.0, "call option price == 5");
}

/* ---- Anchor 2: node fee == notional*bps/10000 at 10 and 30 bps ---- */
static void test_fee_math(void)
{
    printf("[2] node fee basis-point math\n");
    surplus_real_t notional = SR_FROM_INT(1000000);
    CHECK_NUM(fm_node_fee(notional, 10), 1000.0, "10 bps of 1,000,000 == 1000");
    CHECK_NUM(fm_node_fee(notional, 30), 3000.0, "30 bps of 1,000,000 == 3000");
    /* and a non-round notional to be sure it is real math, not a table lookup */
    CHECK_NUM(fm_node_fee(SR_FROM_INT(2500000), 20), 5000.0, "20 bps of 2,500,000 == 5000");
}

/* ---- Anchor 3: a settlement leaves the triple ledger balanced ---- */
static void test_settlement_balanced(void)
{
    printf("[3] settlement leaves ledger balanced + fee band enforced\n");
    fm_book_init(&book);
    uint32_t alice = fm_open_account(&book, 1001, "alice");
    uint32_t bob = fm_open_account(&book, 1002, "bob");
    CHECK(alice != 0xFFFFFFFFu && bob != 0xFFFFFFFFu, "accounts opened");

    /* out-of-band fees are refused (no hollow settlement) */
    CHECK(fm_settle(&book, alice, bob, SR_FROM_INT(1000), 5) == FM_ERR_FEE_BAND,
          "5 bps rejected (below band)");
    CHECK(fm_settle(&book, alice, bob, SR_FROM_INT(1000), 40) == FM_ERR_FEE_BAND,
          "40 bps rejected (above band)");

    int32_t rc = fm_settle(&book, alice, bob, SR_FROM_INT(1000000), 15);
    CHECK(rc == FM_OK, "settle 1,000,000 @ 15 bps ok");

    conventional_report_t rep;
    triple_ledger_export_conventional(&book.ledger, &rep);
    /* trial_balance = total_debits - total_credits must be flat */
    CHECK_NUM(rep.trial_balance, 0.0, "ledger trial balance == 0");

    /* The fee was actually ROUTED, not merely internally balanced: the node-fee
     * account holds exactly 15 bps of 1,000,000 = 1500, the payer is debited the
     * principal + fee (1,001,500), and the payee is credited the principal. */
    surplus_real_t node_bal = book.ledger.accounts[book.node_fee_acct].balance[CAP_FINANCIAL];
    surplus_real_t alice_bal = book.ledger.accounts[alice].balance[CAP_FINANCIAL];
    surplus_real_t bob_bal = book.ledger.accounts[bob].balance[CAP_FINANCIAL];
    CHECK_NUM(node_bal, 1500.0, "node-fee account received exactly 1500 (15 bps of 1,000,000)");
    CHECK_NUM(alice_bal, -1001500.0, "payer debited principal + fee (1,001,500)");
    CHECK_NUM(bob_bal, 1000000.0, "payee credited the principal (1,000,000)");
}

/* ---- Anchor 4: PnL with NO quote reports 'no price', invents nothing ---- */
static void test_pnl_no_price(void)
{
    printf("[4] PnL with no posted quote == 'no price' (not fabricated)\n");
    fm_book_init(&book);
    uint32_t acct = fm_open_account(&book, 2001, "trader");
    int32_t eq = fm_track_instrument(&book, INST_EQUITY, "VULP");
    CHECK(eq >= 0, "track equity");
    /* position open, but NO quote posted */
    CHECK(fm_open_position(&book, acct, (uint32_t) eq, SR_FROM_INT(10), SR_FROM_INT(50)) == FM_OK,
          "open position");

    fm_pnl_t pnl;
    int32_t rc = fm_pnl_report(&book, acct, &pnl);
    CHECK(rc == FM_NO_PRICE, "status == FM_NO_PRICE");
    CHECK(pnl.has_price == false, "has_price false");
    CHECK_NUM(pnl.unrealized_pnl, 0.0, "no fabricated pnl (== 0)");
    CHECK_NUM(pnl.market_value, 0.0, "no fabricated market value (== 0)");
    CHECK(!fm_has_quote(&book, (uint32_t) eq), "fm_has_quote false");

    /* Now post a quote and the report becomes a real mark. */
    CHECK(fm_post_quote(&book, (uint32_t) eq, SR_FROM_INT(60), 100) == FM_OK,
          "post quote 60 @ tick 100");
    rc = fm_pnl_report(&book, acct, &pnl);
    CHECK(rc == FM_OK, "status ok once quoted");
    CHECK(pnl.has_price == true, "has_price true");
    /* 10 units, mark 60, cost 50 -> mv 600, cost 500, upnl 100 */
    CHECK_NUM(pnl.market_value, 600.0, "market value == 600");
    CHECK_NUM(pnl.cost_value, 500.0, "cost value == 500");
    CHECK_NUM(pnl.unrealized_pnl, 100.0, "unrealized pnl == 100");
}

/* ---- Anchor 5: quotes order by phase_tick; later tick supersedes ---- */
static void test_quote_ordering(void)
{
    printf("[5] phase-tick ordering; later tick supersedes, stale rejected\n");
    fm_book_init(&book);
    int32_t id = fm_track_instrument(&book, INST_COMMODITY, "GC");
    CHECK(id >= 0, "track commodity");
    uint32_t inst = (uint32_t) id;

    CHECK(fm_post_quote(&book, inst, SR_FROM_INT(100), 10) == FM_OK, "post 100 @ tick 10");
    CHECK_NUM(fm_value_position(&book, inst, SR_ONE), 100.0, "value == 100");

    /* later tick supersedes */
    CHECK(fm_post_quote(&book, inst, SR_FROM_INT(110), 20) == FM_OK,
          "post 110 @ tick 20 supersedes");
    CHECK_NUM(fm_value_position(&book, inst, SR_ONE), 110.0, "value == 110");

    /* earlier tick is stale, must NOT override */
    CHECK(fm_post_quote(&book, inst, SR_FROM_INT(999), 5) == FM_ERR_STALE, "stale tick 5 rejected");
    CHECK_NUM(fm_value_position(&book, inst, SR_ONE), 110.0, "value still 110 after stale reject");

    /* equal tick is also out of order (not strictly later) */
    CHECK(fm_post_quote(&book, inst, SR_FROM_INT(999), 20) == FM_ERR_STALE,
          "equal tick 20 rejected");
    CHECK_NUM(fm_value_position(&book, inst, SR_FROM_INT(3)), 330.0, "value 3 units == 330");
}

/* ---- Bonus: fixing table round-trips, unfixed reports honest absence ---- */
static void test_fixings(void)
{
    printf("[6] posted-rate fixing table round-trip\n");
    fm_book_init(&book);
    surplus_real_t r;
    CHECK(fm_fixing_get(&book, 7, &r) == FM_NO_PRICE, "unfixed pair == no price");
    CHECK(fm_fixing_set(&book, 7, SR_FROM_FLOAT(1.0825)) == FM_OK, "set fixing");
    CHECK(fm_fixing_get(&book, 7, &r) == FM_OK, "get fixing ok");
    CHECK_NUM(r, 1.0825, "fixing round-trips");
    CHECK(fm_fixing_set(&book, FM_MAX_FIXINGS, r) == FM_ERR_RANGE, "out-of-range pair rejected");
}

int main(void)
{
    printf("== test_finance_markets ==\n");
    test_pricer_regression();
    test_fee_math();
    test_settlement_balanced();
    test_pnl_no_price();
    test_quote_ordering();
    test_fixings();

    printf("\n%d/%d checks passed\n", checks - failures, checks);
    if (failures) {
        printf("RESULT: FAIL (%d)\n", failures);
        return 1;
    }
    printf("RESULT: PASS\n");
    return 0;
}
