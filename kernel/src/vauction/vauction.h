/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* vauction.h — V-Bill issuance by descending (Dutch) uniform-price auction
 * epochs, "backed only", on pay_ledger.
 *
 * OWNER DECISIONS implemented here: "V-Bill issuance runs as periodic
 * descending (Dutch) auction epochs with ONE uniform clearing price.
 * Issuance is 'backed only': each epoch issues only what keeps the reserve at
 * or above its floor, under a per-epoch cap. Collateral comes in as
 * custodian-signed warehouse receipts (ML-DSA signature through the verify
 * hook) valued from a signed median price feed with a staleness limit, minus
 * a per-custodian haircut; a disputed receipt goes to quarantine and stops
 * counting. Auction fiat goes to the reserve and is NEVER burned (refunds go
 * back to bidders). Closed clearing is the default market mode; this module
 * is primary issuance only. No usury. Tiers / Player Cards give NO auction
 * priority: the bid struct has no tier field and ties break only by bid
 * sequence."
 *
 *   A1 RESERVE VALUE. R = the DEBIT balance of the reserve fiat account plus,
 *      over every live receipt (not quarantined, now < expiry), floor(v *
 *      (10000 - haircut_bps) / 10000), v = floor(qty * median / price_scale).
 *      The median is the LOWER median (index (n-1)/2 of the ascending sort)
 *      of the fresh quotes of the receipt's commodity; a quote is fresh when
 *      tick <= now and now - tick <= max_age. Each quote is signed by a
 *      registered oracle over (commodity, price, tick) (vauc_quote_msg) and
 *      must be newer than that oracle's last one. Fewer than min_quotes
 *      fresh quotes, or a value that does not fit 64 bits, counts 0 (fail
 *      closed). A receipt is signed by its custodian over vauc_receipt_msg;
 *      unknown custodian or commodity, a bad signature, an id already held
 *      and an already expired receipt are refused. The operator sets each
 *      custodian's haircut, 0..10000 bps. vauc_receipt_dispute quarantines a
 *      receipt for good: it stops counting and is listed by
 *      vauc_quarantine_list. An expired slot may be reused (a replay of it is
 *      expired and so refused); a quarantined slot never is.
 *   A2 OUTSTANDING. N = the CREDIT balance of the V-Bill issuer account.
 *   A3 FLOOR. R * floor_den >= floor_num * backing * N, backing = fiat minor
 *      units per V-Bill unit (vauc_floor_ok; exact 128-bit compare).
 *   A4 SUPPLY. With k = floor_num * backing (must fit 64 bits) and c = p *
 *      floor_den, Q(p) is the largest q with (R + q*p) * den >= k * (N + q):
 *        R already below the floor -> Q = 0 (checked first);
 *        c >= k -> every q passes -> Q unbounded (UINT64_MAX);
 *        else Q = floor((R*den - k*N) / (k - c)).
 *      S(p) = min(cap, Q(p)), cap the per-epoch issuance cap. Q never falls
 *      as p rises.
 *   A5 BIDS. While the epoch is OPEN a bidder submits (fiat account, V-Bill
 *      account, qty, limit). The bid LOCKS qty*limit by a ledger transfer
 *      bidder -> escrow; if the ledger refuses (funds), nothing is recorded.
 *      At most VAUC_MAX_BIDS per epoch; limit >= p_min; the epoch's total
 *      locked stays below PAY_BAL_MAX, so every sum of quantities fits.
 *   A6 CLEARING. The ladder runs p_start, p_start - step, ... and ends on
 *      p_min (a last step shorter than `step` lands exactly on p_min); at
 *      most VAUC_MAX_RUNGS rungs. D(p) = sum of qty over bids with limit >=
 *      p. The price is the first rung with S(p) > 0 and D(p) >= S(p); the
 *      quantity is S(p). Allocation at price p with previous rung P (none on
 *      the first rung): if D(P) <= S(p), bids with limit >= P fill in full
 *      and the marginal bids (p <= limit < P) share S(p) - D(P); if D(P) >
 *      S(p) (supply fell between the rungs) the bids with limit >= P share
 *      S(p) and the marginal bids get nothing. Sharing is pro rata by qty,
 *      each floor(A * qty / T), and the leftover units go one each to the
 *      shared bids in submission order. Since S falls as p falls, the clock
 *      stops at the first rung with S = 0. If no rung clears, the price is
 *      the lowest rung p* with S > 0 and every bid with limit >= p* fills in
 *      full (D(p*) < S(p*)); that is p_min with min(D(p_min), S(p_min))
 *      whenever S(p_min) > 0. No rung with S > 0: nothing issues (price
 *      recorded as p_min). Everyone pays the same price.
 *   A7 SETTLEMENT. Per winning bid ONE atomic posting: escrow -locked,
 *      reserve +q_i*p, bidder +(locked - q_i*p) (the refund, omitted when 0),
 *      V-Bill issuer CREDIT +q_i, bidder's V-Bill account +q_i. A losing bid
 *      gets one refund posting of its whole lock. So the reserve gains
 *      exactly p * total issued, the escrow returns to where it started and
 *      no fiat leaves the system. Before posting, settlement re-values R and
 *      N at `now` and refuses (VAUC_ERR_FLOOR) unless the quantity still to
 *      issue keeps the floor (nothing to issue: refunds only, no check); the
 *      operator may then vauc_cancel. A refused posting leaves the epoch
 *      CLEARED and settlement resumes where it stopped. States: IDLE ->
 *      OPEN -> CLEARED -> SETTLED -> OPEN ...; vauc_cancel (OPEN, or CLEARED
 *      before any settlement posting) closes the epoch to bids and clearing,
 *      refunds every lock (resuming likewise) and ends it SETTLED with
 *      nothing issued. Anything out of order is VAUC_ERR_STATE.
 *   A8 AUDIT. Each finished epoch leaves a record (epoch, price, qty,
 *      winners, R and N before clearing and after settlement, floor check
 *      after) in a ring of VAUC_HISTORY.
 *
 * NO USURY. Nothing here grows with time or balance: no interest, no fee.
 *
 * HONEST LIMITS. V-Bills are likely securities in many jurisdictions; this
 * is a mechanism, not an offering, and real use needs legal review. A
 * custodian's signature proves who signed a receipt, not that the goods
 * exist or are unencumbered; prices are only as good as the oracles, and a
 * median resists a minority of bad quotes, not a majority. The verify hook is
 * the caller's (ML-DSA in production). Freestanding C11: no allocation, no
 * float.
 */
#ifndef ZXV_VAUCTION_H
#define ZXV_VAUCTION_H

#include <stdbool.h>
#include <stdint.h>
#include "../pay/pay_ledger.h"

#define VAUC_MAX_BIDS       32u
#define VAUC_MAX_QUOTES     8u /* oracles, and quotes per feed */
#define VAUC_MAX_FEEDS      4u
#define VAUC_MAX_CUSTODIANS 8u
#define VAUC_MAX_RECEIPTS   32u
#define VAUC_MAX_RUNGS      4096u
#define VAUC_HISTORY        8u
#define VAUC_BPS            10000u
#define VAUC_UNBOUNDED      UINT64_MAX

typedef enum {
    VAUC_OK = 0,
    VAUC_ERR_ARG = -1,
    VAUC_ERR_FULL = -2,
    VAUC_ERR_SIG = -3,      /* A1: bad signature */
    VAUC_ERR_STATE = -4,    /* A7: out of order */
    VAUC_ERR_UNKNOWN = -5,  /* unknown custodian, oracle, feed or receipt */
    VAUC_ERR_DUP = -6,      /* A1: receipt id already held */
    VAUC_ERR_STALE = -7,    /* A1: quote not newer than the oracle's last */
    VAUC_ERR_EXPIRED = -8,  /* A1: receipt already expired */
    VAUC_ERR_QUOTES = -9,   /* A1: fewer than min_quotes fresh quotes */
    VAUC_ERR_LEDGER = -10,  /* the ledger refused (see last_ledger) */
    VAUC_ERR_FLOOR = -11,   /* A7: settling would break the floor */
    VAUC_ERR_OVERFLOW = -12 /* A1: R does not fit 64 bits */
} vauc_status_t;

typedef enum { VAUC_IDLE = 0, VAUC_OPEN = 1, VAUC_CLEARED = 2, VAUC_SETTLED = 3 } vauc_state_t;

typedef struct {
    uint64_t floor_num, floor_den;   /* A3: f = num/den, both > 0 */
    uint64_t backing;                /* fiat minor units per V-Bill, > 0 */
    uint64_t cap;                    /* per-epoch issuance cap, 0 < cap < PAY_BAL_MAX */
    uint64_t p_start, p_step, p_min; /* A6: p_start >= p_min >= 1, step >= 1 */
    uint64_t max_age;                /* A1: quote staleness limit in ticks */
    uint32_t min_quotes;             /* A1: 1..VAUC_MAX_QUOTES */
} vauc_config_t;

/* A5: no tier field, by design */
typedef struct {
    uint32_t fiat, vbill; /* the bidder's ledger accounts */
    uint64_t qty, limit;
    uint64_t locked; /* qty * limit */
    uint64_t seq;    /* submission sequence (ties) */
    uint64_t fill;   /* A6 */
    bool settled;    /* A7: its posting went through */
} vauc_bid_t;

typedef struct {
    bool used;
    uint32_t id;
    uint32_t haircut_bps;
} vauc_custodian_t;

typedef struct {
    bool used;
    uint32_t commodity;
    uint64_t scale;
    bool has[VAUC_MAX_QUOTES]; /* indexed like vauc_t.oracle */
    uint64_t price[VAUC_MAX_QUOTES];
    uint64_t tick[VAUC_MAX_QUOTES];
} vauc_feed_t;

typedef struct {
    bool used, quarantined;
    uint8_t id[32];
    uint32_t custodian, commodity;
    uint64_t qty, expiry;
} vauc_receipt_t;

typedef struct {
    uint64_t epoch, price, qty;
    uint32_t winners;
    bool cancelled;
    uint64_t r_before, r_after, n_before, n_after;
    bool floor_ok; /* vauc_floor_ok right after settlement */
} vauc_record_t;

/* verify(ctx, signer, msg, len, sig, sig_len): true when sig is signer's */
typedef bool (*vauc_verify_fn)(void *ctx, uint32_t signer, const uint8_t *msg, uint32_t len,
                               const uint8_t *sig, uint32_t sig_len);

typedef struct {
    pay_ledger_t *L;
    uint32_t reserve, escrow, issuer; /* ledger accounts */
    uint16_t fiat, vbill;             /* assets (from the accounts) */
    vauc_config_t cfg;
    vauc_verify_fn verify;
    void *vctx;
    uint8_t seed[32];
    uint64_t seq, bid_seq;
    vauc_state_t state;
    uint8_t mode; /* 0, 1 settling, 2 cancelling */
    uint64_t epoch;
    vauc_bid_t bid[VAUC_MAX_BIDS];
    uint32_t n_bids;
    uint64_t locked; /* this epoch's escrow, A5 */
    vauc_record_t cur;
    vauc_custodian_t cust[VAUC_MAX_CUSTODIANS];
    uint32_t oracle[VAUC_MAX_QUOTES];
    uint32_t n_oracles;
    vauc_feed_t feed[VAUC_MAX_FEEDS];
    vauc_receipt_t rcpt[VAUC_MAX_RECEIPTS];
    vauc_record_t hist[VAUC_HISTORY];
    pay_status_t last_ledger;
} vauc_t;

/* Bind to the ledger. reserve and escrow: open, non-issuer accounts of one
 * fiat asset; issuer: an ISSUER account of another (the V-Bill) asset. */
vauc_status_t vauc_init(vauc_t *a, pay_ledger_t *L, const vauc_config_t *cfg, uint32_t reserve,
                        uint32_t escrow, uint32_t issuer, vauc_verify_fn verify, void *vctx,
                        const uint8_t seed[32]);
/* Only between epochs (IDLE or SETTLED). */
vauc_status_t vauc_set_config(vauc_t *a, const vauc_config_t *cfg);

/* ---- A1 collateral ---- */
vauc_status_t vauc_custodian_set(vauc_t *a, uint32_t custodian, uint32_t haircut_bps);
vauc_status_t vauc_oracle_add(vauc_t *a, uint32_t oracle);
vauc_status_t vauc_feed_add(vauc_t *a, uint32_t commodity, uint64_t price_scale);
/* "zxv/vauc-quote/v1" || commodity || price || tick, little-endian */
#define VAUC_QUOTE_MSG (17u + 4u + 8u + 8u)
void vauc_quote_msg(uint32_t commodity, uint64_t price, uint64_t tick, uint8_t out[VAUC_QUOTE_MSG]);
vauc_status_t vauc_quote(vauc_t *a, uint32_t oracle, uint32_t commodity, uint64_t price,
                         uint64_t tick, const uint8_t *sig, uint32_t sig_len);
vauc_status_t vauc_median(const vauc_t *a, uint32_t commodity, uint64_t now, uint64_t *price);
/* "zxv/vauc-rcpt/v1" || custodian || id || commodity || qty || expiry */
#define VAUC_RCPT_MSG (16u + 4u + 32u + 4u + 8u + 8u)
void vauc_receipt_msg(uint32_t custodian, const uint8_t id[32], uint32_t commodity, uint64_t qty,
                      uint64_t expiry, uint8_t out[VAUC_RCPT_MSG]);
vauc_status_t vauc_receipt_add(vauc_t *a, uint32_t custodian, const uint8_t id[32],
                               uint32_t commodity, uint64_t qty, uint64_t expiry, uint64_t now,
                               const uint8_t *sig, uint32_t sig_len);
vauc_status_t vauc_receipt_dispute(vauc_t *a, const uint8_t id[32]);
/* What the receipt adds to R at `now` (0 if unknown, quarantined, expired or
 * unpriced). */
uint64_t vauc_receipt_counted(const vauc_t *a, const uint8_t id[32], uint64_t now);
/* Copies up to max quarantined ids; returns how many are quarantined. */
uint32_t vauc_quarantine_list(const vauc_t *a, uint8_t (*out)[32], uint32_t max);

/* ---- A1-A4 ---- */
vauc_status_t vauc_reserve(const vauc_t *a, uint64_t now, uint64_t *R);
uint64_t vauc_outstanding(const vauc_t *a);
bool vauc_floor_ok(const vauc_t *a, uint64_t now); /* false on any error */
uint64_t vauc_dilution_q(const vauc_t *a, uint64_t R, uint64_t N, uint64_t p);
uint64_t vauc_supply(const vauc_t *a, uint64_t R, uint64_t N, uint64_t p);
uint64_t vauc_demand(const vauc_t *a, uint64_t p);

/* ---- A5-A8 epochs ---- */
vauc_status_t vauc_open(vauc_t *a);
vauc_status_t vauc_bid(vauc_t *a, uint32_t fiat, uint32_t vbill, uint64_t qty, uint64_t limit,
                       uint64_t tick, uint32_t *index);
vauc_status_t vauc_clear(vauc_t *a, uint64_t now);
vauc_status_t vauc_settle(vauc_t *a, uint64_t now);
vauc_status_t vauc_cancel(vauc_t *a, uint64_t now);
/* The audit record of a finished epoch still in the ring, else NULL. */
const vauc_record_t *vauc_record(const vauc_t *a, uint64_t epoch);

#endif /* ZXV_VAUCTION_H */
