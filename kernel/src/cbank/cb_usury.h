/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* cb_usury.h — the no-usury guard. Any module may call it to refuse an
 * instrument or a fee schedule that charges for the passage of time.
 *
 * The rule, stated once: a charge is usury when what is owed grows with
 * elapsed time on an outstanding amount, whatever it is called (interest,
 * coupon, discount accretion, late-payment accrual, overdraft rate, negative
 * rate, "time value", compounding, rollover fee per period). Allowed:
 *   - a flat fee per service event (per payment, per message);
 *   - an ad valorem fee per event, fixed in basis points of that payment and
 *     independent of how long anything is outstanding;
 *   - a one-off, fixed late-payment penalty that does NOT grow with time
 *     (the guard still requires it to be marked as donated to charity or
 *     cost-recovery, as AAOIFI practice expects, by setting `charity`);
 *   - profit and loss sharing on realised results (musharakah, mudarabah)
 *     on the EQUITY rail 888, where the provider's return is a share of an
 *     actual outcome and losses are shared too;
 *   - sale-based or lease-based pricing fixed once at contract time
 *     (murabahah mark-up, ijarah rent) as long as the price never re-accrues
 *     because time passed after a due date.
 *
 * HONEST LIMITS. The guard judges the structured description it is given. It
 * cannot see a usurious economic substance hidden behind a flat-fee label
 * (for example a "flat fee" that is in fact recomputed every month), so the
 * caller must describe instruments faithfully, and Shari'ah or legal review
 * of real products remains necessary. It is a policy filter, not a fatwa and
 * not legal advice.
 */
#ifndef ZXV_CB_USURY_H
#define ZXV_CB_USURY_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef enum {
    CB_FEE_FLAT = 1,         /* fixed amount per event */
    CB_FEE_AD_VALOREM = 2,   /* fixed bps of the event amount */
    CB_FEE_PENALTY_ONCE = 3, /* fixed, one-off, non-accruing */
    CB_FEE_TIME_ACCRUAL = 4, /* per day/month/year on an outstanding amount */
    CB_FEE_COMPOUNDING = 5,  /* accrual on accrual */
    CB_FEE_LATE_ACCRUAL = 6, /* grows with days past due */
    CB_FEE_ROLLOVER = 7,     /* charged again each period a balance rolls */
    CB_FEE_PROFIT_SHARE = 8  /* share of realised profit, losses shared */
} cb_fee_kind;

typedef struct {
    uint8_t kind;        /* cb_fee_kind */
    uint64_t amount;     /* minor units for FLAT / PENALTY_ONCE */
    uint32_t bps;        /* for AD_VALOREM or PROFIT_SHARE ratio */
    uint32_t period_s;   /* > 0 means the charge recurs with time */
    bool on_outstanding; /* computed on an outstanding balance */
    bool shares_losses;  /* PROFIT_SHARE: provider bears losses too */
    bool charity;        /* PENALTY_ONCE: donated / cost recovery */
    uint16_t rail;       /* rail the charge posts to (888 for profit share) */
} cb_fee;

typedef struct {
    uint32_t interest_rate_bps; /* any nonzero rate */
    bool coupon;                /* periodic coupon */
    bool discount_accretes;     /* zero-coupon style accretion to par */
    bool late_accrual;          /* grows after due date */
    bool negative_rate;         /* time-based charge on deposits */
    bool profit_loss_sharing;   /* PLS instrument (allowed) */
    bool guaranteed_return;     /* a "profit share" with capital and return guaranteed */
    uint32_t n_fees;
    const cb_fee *fees;
} cb_instrument;

#define CB_USURY_OK           0
#define CB_USURY_NULL         (-1)
#define CB_USURY_INTEREST     (-2) /* explicit interest rate */
#define CB_USURY_TIME_BASED   (-3) /* charge recurs or accrues with time */
#define CB_USURY_COMPOUND     (-4)
#define CB_USURY_ON_BALANCE   (-5) /* percentage of an outstanding balance */
#define CB_USURY_LATE         (-6) /* accruing late charge */
#define CB_USURY_GUARANTEED   (-7) /* fixed guaranteed return dressed as profit share */
#define CB_USURY_BAD_RAIL     (-8) /* profit share not on the equity rail */
#define CB_USURY_PENALTY_KEPT (-9) /* one-off penalty kept as income */
#define CB_USURY_BAD_FEE      (-10)

/* Check one fee item. CB_USURY_OK if allowed. */
int cb_usury_check_fee(const cb_fee *f);
/* Check a whole fee schedule; *bad (optional) gets the offending index. */
int cb_usury_check_schedule(const cb_fee *fees, uint32_t n, uint32_t *bad);
/* Check an instrument and all its fees. */
int cb_usury_check_instrument(const cb_instrument *in);
const char *cb_usury_reason(int code);

#endif /* ZXV_CB_USURY_H */
