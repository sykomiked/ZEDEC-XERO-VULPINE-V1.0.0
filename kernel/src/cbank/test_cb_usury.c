/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_cb_usury.c — the no-usury guard. */
#include "cb_usury.h"
#include "cb_test.h"

static cb_fee fee(uint8_t kind)
{
    cb_fee f;
    memset(&f, 0, sizeof f);
    f.kind = kind;
    return f;
}

int main(void)
{
    cb_fee f = fee(CB_FEE_FLAT);
    f.amount = 250;
    CHECK(cb_usury_check_fee(&f) == CB_USURY_OK, "flat fee per payment allowed");
    f.period_s = 30u * 86400u;
    CHECK(cb_usury_check_fee(&f) == CB_USURY_TIME_BASED, "'flat' fee charged monthly refused");

    f = fee(CB_FEE_AD_VALOREM);
    f.bps = 15;
    CHECK(cb_usury_check_fee(&f) == CB_USURY_OK, "fixed 0.15% of the payment allowed");
    f.on_outstanding = true;
    CHECK(cb_usury_check_fee(&f) == CB_USURY_ON_BALANCE,
          "percentage of an outstanding balance refused");
    f.on_outstanding = false;
    f.bps = 10001;
    CHECK(cb_usury_check_fee(&f) == CB_USURY_BAD_FEE, "ad valorem over 100% refused");

    f = fee(CB_FEE_TIME_ACCRUAL);
    CHECK(cb_usury_check_fee(&f) == CB_USURY_TIME_BASED, "per-day accrual refused");
    f = fee(CB_FEE_COMPOUNDING);
    CHECK(cb_usury_check_fee(&f) == CB_USURY_COMPOUND, "compounding refused");
    f = fee(CB_FEE_LATE_ACCRUAL);
    CHECK(cb_usury_check_fee(&f) == CB_USURY_LATE, "late charge growing per day refused");
    f = fee(CB_FEE_ROLLOVER);
    CHECK(cb_usury_check_fee(&f) == CB_USURY_TIME_BASED, "rollover fee refused");

    f = fee(CB_FEE_PENALTY_ONCE);
    f.amount = 1000;
    CHECK(cb_usury_check_fee(&f) == CB_USURY_PENALTY_KEPT,
          "one-off penalty kept as income refused");
    f.charity = true;
    CHECK(cb_usury_check_fee(&f) == CB_USURY_OK, "one-off penalty to charity allowed");
    f.period_s = 86400;
    CHECK(cb_usury_check_fee(&f) == CB_USURY_LATE, "repeating 'one-off' penalty refused");

    f = fee(CB_FEE_PROFIT_SHARE);
    f.bps = 3000;
    f.shares_losses = true;
    f.rail = 888;
    CHECK(cb_usury_check_fee(&f) == CB_USURY_OK,
          "musharakah 30/70 profit share on rail 888 allowed");
    f.rail = 810;
    CHECK(cb_usury_check_fee(&f) == CB_USURY_BAD_RAIL, "profit share on the credit rail refused");
    f.rail = 888;
    f.shares_losses = false;
    CHECK(cb_usury_check_fee(&f) == CB_USURY_GUARANTEED,
          "profit share without loss sharing refused");
    f.shares_losses = true;
    f.on_outstanding = true;
    CHECK(cb_usury_check_fee(&f) == CB_USURY_TIME_BASED, "profit share on a balance refused");

    f = fee(99);
    CHECK(cb_usury_check_fee(&f) == CB_USURY_BAD_FEE, "unknown fee kind refused");
    CHECK(cb_usury_check_fee(0) == CB_USURY_NULL, "NULL fee refused");

    cb_fee sched[3] = {fee(CB_FEE_FLAT), fee(CB_FEE_AD_VALOREM), fee(CB_FEE_TIME_ACCRUAL)};
    uint32_t bad = 99;
    CHECK(cb_usury_check_schedule(sched, 3, &bad) == CB_USURY_TIME_BASED && bad == 2,
          "schedule refused at the accruing item");
    CHECK(cb_usury_check_schedule(sched, 2, &bad) == CB_USURY_OK, "flat + ad valorem schedule ok");

    cb_instrument in;
    memset(&in, 0, sizeof in);
    in.fees = sched;
    in.n_fees = 2;
    CHECK(cb_usury_check_instrument(&in) == CB_USURY_OK, "plain instrument with clean fees ok");
    in.interest_rate_bps = 1;
    CHECK(cb_usury_check_instrument(&in) == CB_USURY_INTEREST, "0.01% interest refused");
    in.interest_rate_bps = 0;
    in.coupon = true;
    CHECK(cb_usury_check_instrument(&in) == CB_USURY_INTEREST, "coupon bond refused");
    in.coupon = false;
    in.discount_accretes = true;
    CHECK(cb_usury_check_instrument(&in) == CB_USURY_TIME_BASED, "zero-coupon accretion refused");
    in.discount_accretes = false;
    in.negative_rate = true;
    CHECK(cb_usury_check_instrument(&in) == CB_USURY_TIME_BASED, "negative deposit rate refused");
    in.negative_rate = false;
    in.profit_loss_sharing = true;
    CHECK(cb_usury_check_instrument(&in) == CB_USURY_OK, "profit-and-loss sharing instrument ok");
    in.guaranteed_return = true;
    CHECK(cb_usury_check_instrument(&in) == CB_USURY_GUARANTEED,
          "guaranteed return disguised as profit sharing refused");
    in.guaranteed_return = false;
    in.n_fees = 3;
    CHECK(cb_usury_check_instrument(&in) == CB_USURY_TIME_BASED,
          "instrument with an accruing fee refused");
    CHECK(cb_usury_reason(CB_USURY_INTEREST)[0] != 0, "reason text available");
    CB_TEST_DONE("test_cb_usury");
}
