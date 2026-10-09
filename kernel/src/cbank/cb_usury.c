/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* cb_usury.c — see cb_usury.h. */
#include "cb_usury.h"

#define RAIL_EQUITY 888u

int cb_usury_check_fee(const cb_fee *f)
{
    if (!f) return CB_USURY_NULL;
    switch (f->kind) {
    case CB_FEE_TIME_ACCRUAL:
        return CB_USURY_TIME_BASED;
    case CB_FEE_COMPOUNDING:
        return CB_USURY_COMPOUND;
    case CB_FEE_LATE_ACCRUAL:
        return CB_USURY_LATE;
    case CB_FEE_ROLLOVER:
        return CB_USURY_TIME_BASED;
    case CB_FEE_FLAT:
    case CB_FEE_AD_VALOREM:
        if (f->period_s) return CB_USURY_TIME_BASED;
        if (f->on_outstanding) return CB_USURY_ON_BALANCE;
        if (f->kind == CB_FEE_AD_VALOREM && f->bps > 10000u) return CB_USURY_BAD_FEE;
        return CB_USURY_OK;
    case CB_FEE_PENALTY_ONCE:
        if (f->period_s) return CB_USURY_LATE;
        if (f->on_outstanding) return CB_USURY_ON_BALANCE;
        if (!f->charity) return CB_USURY_PENALTY_KEPT;
        return CB_USURY_OK;
    case CB_FEE_PROFIT_SHARE:
        if (f->period_s || f->on_outstanding) return CB_USURY_TIME_BASED;
        if (!f->shares_losses) return CB_USURY_GUARANTEED;
        if (f->rail != RAIL_EQUITY) return CB_USURY_BAD_RAIL;
        if (f->bps == 0 || f->bps > 10000u) return CB_USURY_BAD_FEE;
        return CB_USURY_OK;
    default:
        return CB_USURY_BAD_FEE;
    }
}

int cb_usury_check_schedule(const cb_fee *fees, uint32_t n, uint32_t *bad)
{
    if (n && !fees) return CB_USURY_NULL;
    for (uint32_t i = 0; i < n; i++) {
        int r = cb_usury_check_fee(&fees[i]);
        if (r != CB_USURY_OK) {
            if (bad) *bad = i;
            return r;
        }
    }
    return CB_USURY_OK;
}

int cb_usury_check_instrument(const cb_instrument *in)
{
    if (!in) return CB_USURY_NULL;
    if (in->interest_rate_bps || in->coupon) return CB_USURY_INTEREST;
    if (in->discount_accretes || in->negative_rate) return CB_USURY_TIME_BASED;
    if (in->late_accrual) return CB_USURY_LATE;
    if (in->profit_loss_sharing && in->guaranteed_return) return CB_USURY_GUARANTEED;
    return cb_usury_check_schedule(in->fees, in->n_fees, 0);
}

const char *cb_usury_reason(int code)
{
    switch (code) {
    case CB_USURY_OK:
        return "no time-based charge";
    case CB_USURY_NULL:
        return "missing description";
    case CB_USURY_INTEREST:
        return "interest rate or coupon";
    case CB_USURY_TIME_BASED:
        return "charge recurs or accrues with elapsed time";
    case CB_USURY_COMPOUND:
        return "compounding charge";
    case CB_USURY_ON_BALANCE:
        return "charge computed on an outstanding balance";
    case CB_USURY_LATE:
        return "late charge that grows with time";
    case CB_USURY_GUARANTEED:
        return "guaranteed return presented as profit sharing";
    case CB_USURY_BAD_RAIL:
        return "profit share must post to the equity rail 888";
    case CB_USURY_PENALTY_KEPT:
        return "one-off penalty must go to charity or cost recovery";
    default:
        return "malformed fee";
    }
}
