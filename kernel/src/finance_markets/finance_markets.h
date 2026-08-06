/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* finance_markets.h — the spyglass on the horizon.
 *
 * HONEST financial TRACKING for equities / futures / commodities. It values
 * instruments (by REUSING the src/finance pricers — no new pricer here),
 * records positions and quotes FED FROM OUTSIDE, keeps a posted-rate fixing
 * table, and does node-fee (10-30 bps) settlement accounting through the
 * triple ledger.
 *
 * It is a TRACKER, not a live market. Price data is an ops boundary: with no
 * feed bound it reports absent/stale, it never paints a sail that isn't there.
 * NOT price discovery. NOT a matching engine. NOT venue execution.
 */
#ifndef FINANCE_MARKETS_H
#define FINANCE_MARKETS_H

#include <stdint.h>
#include <stdbool.h>
#include "surplus.h"
#include "financial.h"      /* portfolio_t, financial_* pricers, instrument_type_t */
#include "triple_ledger.h"  /* triple_ledger_t, settlement posting */

#define FM_MAX_QUOTES     256   /* one per instrument slot in the portfolio */
#define FM_MAX_FIXINGS     64   /* posted-rate fixing table (pair_id indexed) */
#define FM_MAX_POSITIONS   64   /* recorded (acct, instrument) holdings */

#define FM_FEE_BPS_MIN     10   /* node fee floor */
#define FM_FEE_BPS_MAX     30   /* node fee ceiling */

/* Status codes. Positive FM_NO_PRICE is an honest 'absent', not an error:
 * the report ran, there is simply no quote to mark against. */
typedef enum {
    FM_OK             =  0,
    FM_NO_PRICE       =  1,   /* no posted quote — absence, never fabricated */
    FM_ERR_NULL       = -1,
    FM_ERR_RANGE      = -2,   /* id out of range */
    FM_ERR_FULL       = -3,   /* table full */
    FM_ERR_STALE      = -4,   /* quote tick <= current tick (out of order) */
    FM_ERR_FEE_BAND   = -5,   /* fee_bps outside [10,30] */
    FM_ERR_NO_POSITION = -6,  /* no recorded position for this account */
    FM_ERR_LEDGER     = -7,   /* triple-ledger rejected the posting */
} fm_status_t;

/* A quote is an EXTERNAL feed input, ordered by supplied phase-tick ordinal. */
typedef struct {
    bool           has_quote;
    surplus_real_t price;
    uint32_t       phase_tick;   /* phase-tick ordinal, NEVER wall-clock */
} fm_quote_t;

typedef struct {
    bool           set;
    surplus_real_t rate;
} fm_fixing_t;

typedef struct {
    bool           active;
    uint32_t       acct;
    uint32_t       inst_id;
    surplus_real_t qty;
    surplus_real_t cost_basis;   /* entry price per unit (supplied) */
} fm_position_t;

/* Mark-to-posted-quote PnL report. If has_price is false the numeric fields
 * are all zero: the tracker refuses to invent a mark. */
typedef struct {
    bool           has_price;
    uint32_t       inst_id;
    surplus_real_t qty;
    surplus_real_t mark_price;      /* the posted quote used */
    uint32_t       mark_tick;       /* phase-tick of that quote */
    surplus_real_t market_value;    /* qty * mark_price */
    surplus_real_t cost_value;      /* qty * cost_basis */
    surplus_real_t unrealized_pnl;  /* market_value - cost_value */
} fm_pnl_t;

typedef struct {
    portfolio_t    portfolio;              /* tracked instruments (REUSED) */
    triple_ledger_t ledger;                /* settlement accounting (REUSED) */
    fm_quote_t     quotes[FM_MAX_QUOTES];
    fm_fixing_t    fixings[FM_MAX_FIXINGS];
    fm_position_t  positions[FM_MAX_POSITIONS];
    uint32_t       num_positions;
    uint32_t       node_fee_acct;          /* account that collects node fees */
    bool           inited;
} fm_book_t;

/* ---- Lifecycle ---- */
void fm_book_init(fm_book_t *book);

/* Open a ledger account (CAP_FINANCIAL) for settlement. Returns account id or
 * 0xFFFFFFFF on failure. Thin wrapper over triple_ledger_create_account. */
uint32_t fm_open_account(fm_book_t *book, uint32_t entity_id, const char *name);

/* ---- Instrument tracking (wraps financial_create_instrument) ---- */
int32_t fm_track_instrument(fm_book_t *book, instrument_type_t type,
                            const char *ticker);

/* Mutable access to a tracked instrument (to set pricing registers / drive the
 * REUSED financial pricers). Returns NULL if out of range. */
financial_instrument_t *fm_instrument(fm_book_t *book, uint32_t inst_id);

/* ---- Quotes (EXTERNAL feed, phase-tick ordered) ---- */
/* price is an external feed input. Rejects out-of-order ticks (FM_ERR_STALE);
 * a strictly later tick supersedes an earlier one. */
int32_t fm_post_quote(fm_book_t *book, uint32_t inst_id,
                      surplus_real_t price, uint32_t phase_tick);

/* True iff a live quote exists for the instrument. Callers should check this
 * before trusting fm_value_position — no quote means no basis to value. */
bool fm_has_quote(const fm_book_t *book, uint32_t inst_id);

/* qty * latest posted quote. Returns SR_ZERO when no quote is bound — it does
 * NOT fabricate a price; guard with fm_has_quote. */
surplus_real_t fm_value_position(const fm_book_t *book, uint32_t inst_id,
                                 surplus_real_t qty);

/* ---- Posted-rate fixing table ---- */
int32_t fm_fixing_set(fm_book_t *book, uint32_t pair_id, surplus_real_t rate);
int32_t fm_fixing_get(const fm_book_t *book, uint32_t pair_id,
                      surplus_real_t *out_rate);

/* ---- Positions (FED FROM OUTSIDE) ---- */
int32_t fm_open_position(fm_book_t *book, uint32_t acct, uint32_t inst_id,
                         surplus_real_t qty, surplus_real_t cost_basis);

/* ---- Node fee + settlement ---- */
/* Pure basis-point math: notional * bps / 10000. Exposed for auditing. */
surplus_real_t fm_node_fee(surplus_real_t notional, uint16_t fee_bps);

/* Settle `amount` from->to and route the node fee (10-30 bps) to the node fee
 * account, all through the triple ledger. Leaves the ledger balanced. */
int32_t fm_settle(fm_book_t *book, uint32_t from_acct, uint32_t to_acct,
                  surplus_real_t amount, uint16_t fee_bps);

/* ---- PnL (mark-to-posted-quote; honest absence) ---- */
int32_t fm_pnl_report(const fm_book_t *book, uint32_t acct, fm_pnl_t *out);

#endif /* FINANCE_MARKETS_H */
