/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* finance_markets.c — mark your hold honestly, never paint a sail that isn't there.
 *
 * A TRACKER over the existing src/finance suite. Every number here is either
 * (a) computed by a REUSED financial pricer, (b) a fee from honest bps math, or
 * (c) a mark against an EXTERNALLY posted quote. When the feed is unbound we
 * report absence — we do not invent a tick, a fill, or a fair value.
 */
#include "finance_markets.h"

/* Freestanding: no <stddef.h>. On the target, freestanding.h provides NULL;
 * guard so the host build (which pulls in neither) still has it. */
#ifndef NULL
#define NULL ((void *)0)
#endif

/* Sentinel entity for the node's own fee-collection account. */
#define FM_NODE_ENTITY_ID 0x364E0FEEu  /* "36N o' FEE" — just a marker */

void fm_book_init(fm_book_t *book) {
    if (!book) return;

    portfolio_init(&book->portfolio);
    triple_ledger_init(&book->ledger);

    uint32_t i;
    for (i = 0; i < FM_MAX_QUOTES; i++) {
        book->quotes[i].has_quote  = false;
        book->quotes[i].price      = SR_ZERO;
        book->quotes[i].phase_tick = 0;
    }
    for (i = 0; i < FM_MAX_FIXINGS; i++) {
        book->fixings[i].set  = false;
        book->fixings[i].rate = SR_ZERO;
    }
    for (i = 0; i < FM_MAX_POSITIONS; i++) {
        book->positions[i].active = false;
    }
    book->num_positions = 0;

    /* Stand up the node fee-collection account up front so settlement always
     * has a balanced counterparty for the fee leg. */
    book->node_fee_acct = triple_ledger_create_account(
        &book->ledger, FM_NODE_ENTITY_ID, CAP_FINANCIAL, "node-fee");
    book->inited = true;
}

uint32_t fm_open_account(fm_book_t *book, uint32_t entity_id, const char *name) {
    if (!book || !book->inited) return 0xFFFFFFFFu;
    return triple_ledger_create_account(&book->ledger, entity_id,
                                        CAP_FINANCIAL, name);
}

int32_t fm_track_instrument(fm_book_t *book, instrument_type_t type,
                            const char *ticker) {
    if (!book || !book->inited) return FM_ERR_NULL;
    if (book->portfolio.num_instruments >= FM_MAX_QUOTES) return FM_ERR_FULL;

    /* Reuse the real instrument constructor. Price starts at ZERO precisely
     * because it has no feed yet — the caller must post a quote. Mode DC =
     * spot/settlement, the natural default for a tracker. */
    uint32_t id = financial_create_instrument(&book->portfolio, type,
                                              FIN_EXEC_DC, ticker,
                                              SR_ZERO, SR_ZERO);
    if (id == 0xFFFFFFFFu) return FM_ERR_FULL;
    return (int32_t)id;
}

financial_instrument_t *fm_instrument(fm_book_t *book, uint32_t inst_id) {
    if (!book || !book->inited) return NULL;
    if (inst_id >= book->portfolio.num_instruments) return NULL;
    return &book->portfolio.instruments[inst_id];
}

int32_t fm_post_quote(fm_book_t *book, uint32_t inst_id,
                      surplus_real_t price, uint32_t phase_tick) {
    if (!book || !book->inited) return FM_ERR_NULL;
    if (inst_id >= book->portfolio.num_instruments) return FM_ERR_RANGE;

    fm_quote_t *q = &book->quotes[inst_id];
    /* Phase-tick ordering, NEVER wall-clock. A tick that does not strictly
     * advance is out of order and cannot supersede the standing quote. */
    if (q->has_quote && phase_tick <= q->phase_tick) return FM_ERR_STALE;

    q->has_quote  = true;
    q->price      = price;      /* verbatim external feed value */
    q->phase_tick = phase_tick;

    /* Mirror into the instrument register so the REUSED pricers/valuation see
     * the same spot. This is a copy of the fed value, not a computed one. */
    financial_instrument_t *inst = &book->portfolio.instruments[inst_id];
    financial_set_price(inst, price);
    return FM_OK;
}

bool fm_has_quote(const fm_book_t *book, uint32_t inst_id) {
    if (!book || !book->inited) return false;
    if (inst_id >= book->portfolio.num_instruments) return false;
    return book->quotes[inst_id].has_quote;
}

surplus_real_t fm_value_position(const fm_book_t *book, uint32_t inst_id,
                                 surplus_real_t qty) {
    if (!book || !book->inited) return SR_ZERO;
    if (inst_id >= book->portfolio.num_instruments) return SR_ZERO;
    const fm_quote_t *q = &book->quotes[inst_id];
    if (!q->has_quote) return SR_ZERO;   /* no feed => no fabricated value */
    return SR_MUL(qty, q->price);
}

int32_t fm_fixing_set(fm_book_t *book, uint32_t pair_id, surplus_real_t rate) {
    if (!book || !book->inited) return FM_ERR_NULL;
    if (pair_id >= FM_MAX_FIXINGS) return FM_ERR_RANGE;
    book->fixings[pair_id].set  = true;
    book->fixings[pair_id].rate = rate;
    return FM_OK;
}

int32_t fm_fixing_get(const fm_book_t *book, uint32_t pair_id,
                      surplus_real_t *out_rate) {
    if (!book || !book->inited || !out_rate) return FM_ERR_NULL;
    if (pair_id >= FM_MAX_FIXINGS) return FM_ERR_RANGE;
    if (!book->fixings[pair_id].set) return FM_NO_PRICE;  /* unfixed, honest */
    *out_rate = book->fixings[pair_id].rate;
    return FM_OK;
}

int32_t fm_open_position(fm_book_t *book, uint32_t acct, uint32_t inst_id,
                         surplus_real_t qty, surplus_real_t cost_basis) {
    if (!book || !book->inited) return FM_ERR_NULL;
    if (inst_id >= book->portfolio.num_instruments) return FM_ERR_RANGE;
    if (acct >= book->ledger.num_accounts) return FM_ERR_RANGE;

    /* Overwrite an existing position for this account if present. */
    uint32_t i;
    for (i = 0; i < book->num_positions; i++) {
        if (book->positions[i].active && book->positions[i].acct == acct) {
            book->positions[i].inst_id    = inst_id;
            book->positions[i].qty        = qty;
            book->positions[i].cost_basis = cost_basis;
            return FM_OK;
        }
    }
    if (book->num_positions >= FM_MAX_POSITIONS) return FM_ERR_FULL;
    fm_position_t *p = &book->positions[book->num_positions++];
    p->active     = true;
    p->acct       = acct;
    p->inst_id    = inst_id;
    p->qty        = qty;
    p->cost_basis = cost_basis;
    return FM_OK;
}

surplus_real_t fm_node_fee(surplus_real_t notional, uint16_t fee_bps) {
    /* notional * bps / 10000, done as a scaled fraction so it does NOT floor
     * to zero on the Q32.32 target (never (x*n)/d in one integer step here —
     * SR_MUL/SR_DIV keep the fixed-point scale). */
    return SR_DIV(SR_MUL(notional, SR_FROM_INT(fee_bps)), SR_FROM_INT(10000));
}

int32_t fm_settle(fm_book_t *book, uint32_t from_acct, uint32_t to_acct,
                  surplus_real_t amount, uint16_t fee_bps) {
    if (!book || !book->inited) return FM_ERR_NULL;
    if (fee_bps < FM_FEE_BPS_MIN || fee_bps > FM_FEE_BPS_MAX)
        return FM_ERR_FEE_BAND;
    if (from_acct >= book->ledger.num_accounts ||
        to_acct   >= book->ledger.num_accounts)
        return FM_ERR_RANGE;

    surplus_real_t fee = fm_node_fee(amount, fee_bps);
    triple_ledger_t *tl = &book->ledger;

    /* Snapshot every scalar the two transfers mutate, across the three touched
     * accounts (from, to, node-fee) + the ledger totals, so a failed fee leg
     * ROLLS THE PRINCIPAL LEG BACK. Without this, a fee leg that hits an account's
     * entry-capacity limit leaves the principal committed — a partial settlement. */
    surplus_real_t s_assets = tl->total_assets, s_liab = tl->total_liabilities,
                   s_equity = tl->total_equity;
    uint64_t s_nid = tl->next_entry_id;
    uint32_t idx[3] = { from_acct, to_acct, book->node_fee_acct };
    struct { uint32_t ne; surplus_real_t cbal, cov, ext, bal[LEDGER_MAX]; } snap[3];
    for (int k = 0; k < 3; k++) {
        account_t *a = &tl->accounts[idx[k]];
        snap[k].ne = a->num_entries; snap[k].cbal = a->conventional_balance;
        snap[k].cov = a->coverage_ratio; snap[k].ext = a->externality_phase;
        for (int j = 0; j < LEDGER_MAX; j++) snap[k].bal[j] = a->balance[j];
    }

    /* Principal leg: from -> to. ell=1.0 (fully attested), phi=0 (no phase). */
    int32_t rc = triple_ledger_transfer(tl, from_acct, to_acct,
                                        CAP_FINANCIAL, amount, SR_ONE, SR_ZERO,
                                        "fm settle: principal");
    if (rc < 0) return FM_ERR_LEDGER;

    /* Node fee leg: from -> node fee account. */
    rc = triple_ledger_transfer(tl, from_acct, book->node_fee_acct,
                                CAP_FINANCIAL, fee, SR_ONE, SR_ZERO,
                                "fm settle: node fee");
    if (rc < 0) {
        /* Fee leg failed — reverse the principal leg. Restore the scalars; stale
         * entry rows past the restored num_entries are logically discarded. */
        tl->total_assets = s_assets; tl->total_liabilities = s_liab;
        tl->total_equity = s_equity; tl->next_entry_id = s_nid;
        for (int k = 0; k < 3; k++) {
            account_t *a = &tl->accounts[idx[k]];
            a->num_entries = snap[k].ne; a->conventional_balance = snap[k].cbal;
            a->coverage_ratio = snap[k].cov; a->externality_phase = snap[k].ext;
            for (int j = 0; j < LEDGER_MAX; j++) a->balance[j] = snap[k].bal[j];
        }
        return FM_ERR_LEDGER;
    }

    return FM_OK;
}

int32_t fm_pnl_report(const fm_book_t *book, uint32_t acct, fm_pnl_t *out) {
    if (!book || !book->inited || !out) return FM_ERR_NULL;

    /* Zero the report first so an early return never leaks a stale/invented
     * number into the caller's struct. */
    out->has_price      = false;
    out->inst_id        = 0;
    out->qty            = SR_ZERO;
    out->mark_price     = SR_ZERO;
    out->mark_tick      = 0;
    out->market_value   = SR_ZERO;
    out->cost_value     = SR_ZERO;
    out->unrealized_pnl = SR_ZERO;

    const fm_position_t *pos = NULL;
    uint32_t i;
    for (i = 0; i < book->num_positions; i++) {
        if (book->positions[i].active && book->positions[i].acct == acct) {
            pos = &book->positions[i];
            break;
        }
    }
    if (!pos) return FM_ERR_NO_POSITION;

    out->inst_id = pos->inst_id;
    out->qty     = pos->qty;

    const fm_quote_t *q = &book->quotes[pos->inst_id];
    if (!q->has_quote) {
        /* The spyglass sees no sail: report 'no price', invent nothing. */
        return FM_NO_PRICE;
    }

    out->has_price      = true;
    out->mark_price     = q->price;
    out->mark_tick      = q->phase_tick;
    out->market_value   = SR_MUL(pos->qty, q->price);
    out->cost_value     = SR_MUL(pos->qty, pos->cost_basis);
    out->unrealized_pnl = SR_SUB(out->market_value, out->cost_value);
    return FM_OK;
}
