/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* fxepoch.h — FX by uniform-price batch epochs, on pay_ledger.
 *
 * AGREED DESIGN implemented here (docs/ARCHITECTURE.md): "FX and markets:
 * uniform-price batch epochs; multilateral non-USD netting; cooling epochs
 * instead of cliff liquidation; leverage off by default". And: 'FX "liquidity
 * pump": the fee still applies; savings come from netting'. Closed clearing is
 * the default market mode; this open batch-auction market is a per-deployment
 * / per-jurisdiction switch, so it is OFF unless cfg.enabled (fail closed).
 *
 *   F1 OFF BY DEFAULT, NO LEVERAGE. fx_config_default gives enabled = false,
 *      leverage = 0, band_bps = FX_BAND_DEFAULT (500, a defaulted owner
 *      decision), max_orders = FX_MAX_ORDERS. While !enabled, fx_open and
 *      fx_order return FX_ERR_DISABLED (settle and cancel still run, so an
 *      epoch can always be unwound). Any leverage != 0 is refused
 *      (FX_ERR_LEVERAGE) by fx_init and fx_set_config: there is no margin,
 *      borrowing or short-sale path anywhere in this module, and no
 *      liquidation function. Config changes only between epochs.
 *   F2 BOOKS. A market holds up to FX_MAX_BOOKS books, one per currency pair
 *      (base asset, quote asset): any two distinct ledger assets, no USD (or
 *      any) vehicle currency, so a non-USD pair trades directly. A book is
 *      bound to three ledger accounts: a base escrow and a quote escrow (one
 *      per asset, never shared with another book) and a fee account in the
 *      quote asset (may be shared by books, never an escrow). None may be an
 *      ISSUER. A pair is listed once: (B, Q) and (Q, B) are the same pair.
 *      Books are added only between epochs; the escrows' and fee account's
 *      balances at that moment are the baselines F13 audits against.
 *   F3 PRICES. A price p is an integer: quote minor units per `scale` base
 *      minor units, scale > 0 fixed per book (e.g. 10^8). A quantity is in
 *      base minor units. The quote value of x at p is floor(x * p / scale)
 *      (128-bit product, exact).
 *   F4 ORDERS. While the epoch is OPEN a participant submits a SELL (base
 *      qty, min price `limit`) or a BUY (base qty, max price `limit`), qty > 0
 *      and limit > 0, naming a base account and a quote account of the same
 *      owner, in the book's assets, neither of them an escrow or fee account
 *      of any book. The owner signs fx_order_msg ("zxv/fxepoch/v1" ||
 *      market id || epoch || book || side || base acct || quote acct || qty
 *      || limit || nonce), checked through the verify hook; the epoch in the
 *      message stops cross-epoch replay and an (owner, nonce) pair already
 *      used in this epoch is refused (FX_ERR_DUP). Orders are FULLY FUNDED:
 *      a SELL locks qty base, a BUY locks ceil(qty * limit / scale) quote, by
 *      one ledger transfer to the book's escrow; if the ledger refuses
 *      (funds, frozen), nothing is recorded. Per book, the sell quantities
 *      (= base locked), the buy quantities and the quote locked each stay
 *      below PAY_BAL_MAX, so every sum below fits 64 bits. At most
 *      cfg.max_orders orders per epoch over the market. Orders keep their
 *      submission sequence; there is NO tier or priority field.
 *   F5 CLEARING (uniform price, per book). The candidates are the distinct
 *      limit prices in the book. For a candidate p:
 *          S(p) = sum qty of SELLs with limit <= p,
 *          D(p) = sum qty of BUYs  with limit >= p,   V(p) = min(S, D).
 *      The market price p* maximises V; ties go to the least |S - D|; then
 *      to the price closest to the reference r (the book's last clearing
 *      price, or, if it has none, the lower midpoint lo + (hi - lo) / 2 of
 *      the prices still tied); then to the lowest p. If the book is empty or
 *      max V == 0 nothing trades (no clamp, no cooling, reference unchanged;
 *      the record's price is the reference, or 0 without one).
 *   F6 COOLING EPOCHS (no cliff, no liquidation). With band_bps > 0 and a
 *      reference r, the clearing price is p* clamped into
 *          [ceil(r * (10000 - band) / 10000), floor(r * (10000 + band) / 10000)]
 *      (the upper bound is UINT64_MAX when it does not fit). If the clamp
 *      moved the price the epoch is COOLING: S, D and V are recomputed at the
 *      clamped price (V may then be 0) and the clamped price becomes the next
 *      reference even when nothing traded, so the price walks toward the
 *      market at most band_bps per epoch and never jumps. A traded epoch sets
 *      the reference to its price. band_bps < 10000; 0 disables the band.
 *   F7 ALLOCATION. At the clearing price p the eligible SELLs (limit <= p,
 *      total S) and BUYs (limit >= p, total D) trade V = min(S, D). Each side
 *      shares its traded amount pro rata by qty: fill = floor(A * qty / T)
 *      (A = V, T = that side's total), then the leftover units go one each to
 *      that side's eligible orders in submission order (as vauction A6). The
 *      short side has A == T and so fills in full; with S == D both do.
 *   F8 MATCHING AND ROUNDING. Fills are paired by walking the filled SELLs
 *      and the filled BUYs of the book in submission order (two pointers);
 *      each pair (s, b, x) is one fill with quote value q = floor(x * p /
 *      scale) (F3). The seller is credited q and the buyer pays exactly q, so
 *      quote is conserved per fill. A buyer pays sum q <= floor(fill * p /
 *      scale) <= its lock (p <= limit); everything else of its lock,
 *      including the rounding dust below one quote unit, is refunded to the
 *      buyer. A seller receives no quote for the dust either: dust never
 *      leaves the buyer. At most n_orders - 1 fills per book.
 *   F9 FEE. On each fill the 0.08889% assurance fee is charged on the quote
 *      gross q, with the BOOK's sub-unit carry (pay_assure_fee_carry, F2 of
 *      pay_assure.h), in fill order. It is paid by the seller out of q and
 *      posted to the book's fee account (the four-bucket split of
 *      pay_assure_split is the fee account owner's step, not this module's).
 *      fee <= q always. Over the book's life, starting from carry 0:
 *          10^7 * fee_total + carry == 8889 * gross_total     (exact, F13)
 *      i.e. fee_total == floor(gross_total * 8889 / 10^7): many fills pay
 *      what one fill of their sum would. The carry, totals and reference are
 *      committed only when the epoch settles; a cancelled epoch charges none.
 *   F10 SETTLEMENT (DvP inside the epoch). Per order ONE atomic posting:
 *      SELL: base escrow -> seller base refund (lock - fill), quote escrow ->
 *            seller quote (sum q - fee) and fee account (fee);
 *      BUY:  quote escrow -> buyer quote refund (lock - paid), base escrow ->
 *            buyer base (fill).
 *      Zero lines are omitted; an order with nothing to move posts nothing.
 *      After the last posting both escrows are back at their baselines and
 *      no obligation is left unsettled. A refused posting leaves the epoch
 *      CLEARED; fx_settle resumes where it stopped.
 *   F11 STATES. IDLE -> OPEN -> CLEARED -> SETTLED -> OPEN ...; fx_cancel
 *      (OPEN, or CLEARED before any settlement posting) closes the epoch,
 *      drops every fill, refunds every lock in full (resumable likewise) and
 *      ends SETTLED with nothing traded. fx_clear refused part-way (counter
 *      overflow, corrupt carry) leaves the epoch OPEN with no fills. Anything
 *      out of order is FX_ERR_STATE.
 *   F12 MULTILATERAL NETTING REPORT. fx_net computes, over every book's fills
 *      in the epoch (CLEARED or SETTLED), each participant's (account owner's)
 *      net position per asset: + receipts - payments (seller: -x base,
 *      +(q - fee) quote; buyer: +x base, -q quote; fee account owner: +fee
 *      quote), and checks that each asset sums to 0 (`balanced`). gross_legs
 *      counts the bilateral transfers the fills imply (per fill: the base
 *      leg, the seller's quote leg when q - fee > 0, the fee leg when fee >
 *      0); net_legs = sum over assets of (k - 1), k the asset's non-zero
 *      positions, the most transfers a net settlement of those positions
 *      needs; saved = gross_legs - net_legs (0 when not positive). This is the
 *      FX epoch's own report ("savings come from netting"); kernel/src/cbank
 *      cb_net does multilateral netting for payments and is not duplicated.
 *      The settlement itself stays gross per order through the escrows (F10).
 *   F13 AUDIT. A ring of FX_HISTORY records, one per book per finished epoch
 *      (market price p*, clearing price, reference before, S, D, volume,
 *      quote gross, fee, fills, cooling, cancelled). fx_check verifies:
 *      pay_ledger_check; each escrow's DEBIT == baseline + open locks -
 *      what settled postings released (== baseline once SETTLED), CREDIT 0;
 *      each fee account's DEBIT == its baseline + the fees of its books
 *      (committed, plus settled sells of a CLEARED epoch) (these sums are
 *      taken mod 2^64: every true value is a ledger balance, so equality
 *      mod 2^64 is exact); the F9 identity
 *      and carry < 10^7; every lock as F4 computes it; every fill well
 *      formed (sides, book, x > 0, q == floor(x * p / scale)); per order the
 *      fills sum to its fill, quote and fee; per book they sum to the
 *      record's volume, gross and fee; and fx_net's per-asset sums are 0.
 *
 * NO USURY. Nothing here accrues with time or balance: no interest, no
 * margin, no funding rate, no late fee. The only charge is the assurance fee
 * on the quote gross of a fill (transaction-proportional, agreed up front).
 *
 * HONEST LIMITS. FX dealing and operating an exchange or batch auction are
 * regulated in most jurisdictions (licences, market-conduct, AML/KYC rules);
 * this module is OFF by default and needs legal review before any use. A
 * uniform clearing price is only as good as the order flow that forms it; a
 * thin or one-sided book clears badly or not at all, and the band slows
 * price discovery as much as it damps shocks. No claim of best execution or
 * of any legal or regulatory status is made. The netting report is
 * arithmetic over this epoch's fills, not a settlement-finality claim. The
 * verify hook is the caller's (ML-DSA in production). Freestanding C11: no
 * allocation, no floating point, no libc.
 */
#ifndef ZXV_FXEPOCH_H
#define ZXV_FXEPOCH_H

#include <stdbool.h>
#include <stdint.h>
#include "../pay/pay_ledger.h"

#define FX_MAX_BOOKS    4u
#define FX_MAX_ORDERS   48u
#define FX_MAX_FILLS    FX_MAX_ORDERS /* F8: at most n - 1 per book */
#define FX_HISTORY      16u
#define FX_BPS          10000u
#define FX_BAND_DEFAULT 500u /* F1: 5% per epoch, a defaulted owner decision */
#define FX_NET_MAX      (2u * FX_MAX_ORDERS + FX_MAX_BOOKS)

typedef enum {
    FX_OK = 0,
    FX_ERR_ARG = -1,
    FX_ERR_DISABLED = -2, /* F1: market switched off */
    FX_ERR_STATE = -3,    /* F11: out of order */
    FX_ERR_FULL = -4,
    FX_ERR_SIG = -5,      /* F4: bad signature */
    FX_ERR_DUP = -6,      /* F4: (owner, nonce) already used this epoch */
    FX_ERR_LEDGER = -7,   /* the ledger refused (see last_ledger) */
    FX_ERR_OVERFLOW = -8, /* a lifetime counter would overflow */
    FX_ERR_LEVERAGE = -9  /* F1: leverage is not offered */
} fx_status_t;

typedef enum { FX_IDLE = 0, FX_OPEN = 1, FX_CLEARED = 2, FX_SETTLED = 3 } fx_state_t;
typedef enum { FX_SELL = 0, FX_BUY = 1 } fx_side_t;

typedef struct {
    bool enabled;        /* F1: per deployment / jurisdiction, default false */
    uint32_t leverage;   /* F1: must be 0                                   */
    uint32_t band_bps;   /* F6: < 10000, 0 = no band                       */
    uint32_t max_orders; /* F4: 1..FX_MAX_ORDERS per epoch                 */
} fx_config_t;

/* F13: one per book per finished epoch */
typedef struct {
    uint64_t epoch;
    uint32_t book;
    uint64_t market;     /* F5: p* (0 when nothing crossed) */
    uint64_t price;      /* F6: the clearing price           */
    uint64_t ref_before; /* 0 when the book had no reference */
    uint64_t supply, demand, volume;
    uint64_t gross, fee; /* quote value traded, fee on it */
    uint32_t fills;
    bool cooling, cancelled;
} fx_record_t;

typedef struct {
    uint16_t base, quote;               /* assets (from the escrows) */
    uint32_t escrow_base, escrow_quote; /* F2 */
    uint32_t fee_acct;
    uint64_t scale;
    uint64_t base_start, quote_start, fee_start; /* F13 baselines */
    bool has_ref;
    uint64_t ref;
    uint64_t carry; /* F9, committed at settlement */
    uint64_t gross_total, fee_total, volume_total;
    uint64_t sell_qty, buy_qty, quote_lock; /* F4, this epoch */
    /* this epoch's clearing, committed by fx_settle */
    fx_record_t cur;
    uint64_t carry_next, ref_next;
    bool has_ref_next;
} fx_book_t;

/* F4: no tier field, by design */
typedef struct {
    uint32_t book;
    uint8_t side; /* fx_side_t */
    uint32_t base_acct, quote_acct, owner;
    uint64_t qty, limit, nonce;
    uint64_t locked; /* F4 */
    uint64_t seq;    /* submission sequence (ties) */
    uint64_t fill;   /* F7 */
    uint64_t quote;  /* F8: SELL gross proceeds, BUY amount paid */
    uint64_t fee;    /* F9: SELL only */
    bool settled;    /* F10 / F11: its posting went through */
} fx_order_t;

typedef struct {
    uint32_t book, sell, buy; /* order indices */
    uint64_t qty, quote, fee;
} fx_fill_t;

typedef struct {
    uint32_t owner;
    uint16_t asset;
    int64_t net;
} fx_pos_t;

typedef struct {
    fx_pos_t pos[FX_NET_MAX];
    uint32_t n_pos;      /* (owner, asset) pairs touched */
    uint32_t nonzero;    /* of them, non-zero            */
    uint32_t gross_legs; /* F12 */
    uint32_t net_legs;
    uint32_t saved;
    bool balanced; /* every asset sums to 0 */
} fx_net_t;

/* verify(ctx, signer, msg, len, sig, sig_len): true when sig is signer's */
typedef bool (*fx_verify_fn)(void *ctx, uint32_t signer, const uint8_t *msg, uint32_t len,
                             const uint8_t *sig, uint32_t sig_len);

typedef struct {
    pay_ledger_t *L;
    fx_config_t cfg;
    uint32_t market_id;
    fx_verify_fn verify;
    void *vctx;
    uint8_t seed[32];
    uint64_t seq, order_seq;
    fx_state_t state;
    uint8_t mode; /* 0, 1 settling, 2 cancelling */
    uint64_t epoch;
    fx_book_t book[FX_MAX_BOOKS];
    uint32_t n_books;
    fx_order_t order[FX_MAX_ORDERS];
    uint32_t n_orders;
    fx_fill_t fill[FX_MAX_FILLS];
    uint32_t n_fills;
    fx_record_t hist[FX_HISTORY];
    uint64_t n_hist;
    pay_status_t last_ledger;
    bool ready;
} fx_t;

/* F1: enabled false, leverage 0, band FX_BAND_DEFAULT, max_orders FX_MAX_ORDERS. */
void fx_config_default(fx_config_t *c);
fx_status_t fx_init(fx_t *m, pay_ledger_t *L, const fx_config_t *cfg, uint32_t market_id,
                    fx_verify_fn verify, void *vctx, const uint8_t seed[32]);
/* Only between epochs (IDLE or SETTLED). */
fx_status_t fx_set_config(fx_t *m, const fx_config_t *cfg);
/* F2. Only between epochs. *index (optional) gets the book number. */
fx_status_t fx_add_book(fx_t *m, uint32_t escrow_base, uint32_t escrow_quote, uint32_t fee_acct,
                        uint64_t scale, uint32_t *index);

/* F3 (pure): floor(x * p / scale). False when scale == 0 or it does not fit. */
bool fx_quote_value(uint64_t x, uint64_t p, uint64_t scale, uint64_t *out);
/* F4 (pure): ceil(qty * limit / scale). False when scale == 0 or it does not fit. */
bool fx_buy_lock(uint64_t qty, uint64_t limit, uint64_t scale, uint64_t *out);
/* F6 (pure): the band around ref. False when ref == 0 or band_bps >= 10000. */
bool fx_band(uint64_t ref, uint32_t band_bps, uint64_t *lo, uint64_t *hi);
/* F5 over the current orders of a book (0 for an unknown book). */
uint64_t fx_supply(const fx_t *m, uint32_t book, uint64_t p);
uint64_t fx_demand(const fx_t *m, uint32_t book, uint64_t p);

/* "zxv/fxepoch/v1" || market || epoch || book || side || base || quote ||
 * qty || limit || nonce, little-endian */
#define FX_ORDER_MSG (14u + 4u + 8u + 4u + 1u + 4u + 4u + 8u + 8u + 8u)
void fx_order_msg(uint32_t market_id, uint64_t epoch, uint32_t book, uint8_t side,
                  uint32_t base_acct, uint32_t quote_acct, uint64_t qty, uint64_t limit,
                  uint64_t nonce, uint8_t out[FX_ORDER_MSG]);

/* ---- F4-F11 epochs ---- */
fx_status_t fx_open(fx_t *m);
fx_status_t fx_order(fx_t *m, uint32_t book, uint8_t side, uint32_t base_acct, uint32_t quote_acct,
                     uint64_t qty, uint64_t limit, uint64_t nonce, uint64_t tick,
                     const uint8_t *sig, uint32_t sig_len, uint32_t *index);
fx_status_t fx_clear(fx_t *m);
fx_status_t fx_settle(fx_t *m, uint64_t now);
fx_status_t fx_cancel(fx_t *m, uint64_t now);

/* F12. CLEARED or SETTLED only. */
fx_status_t fx_net(const fx_t *m, fx_net_t *out);
/* F13. The record of (epoch, book) still in the ring, else NULL. */
const fx_record_t *fx_record(const fx_t *m, uint64_t epoch, uint32_t book);
bool fx_check(const fx_t *m);

#endif /* ZXV_FXEPOCH_H */
