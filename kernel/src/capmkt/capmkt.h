/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* capmkt.h — the capacity market: buy extra compute, model inference,
 * storage or bandwidth from other people's machines, at a price set by
 * supply and demand.
 *
 * WHY A NEW MODULE. Nothing in the tree discovers a price for resources:
 * carracho/carr_econ.h prices by a demand multiplier on a posted base and
 * negotiates bilaterally; swarm_market.h is a proportional-share market for
 * internal tokens; financial_fabric's ff_order_book matches one best bid
 * with one best ask at their midpoint, per capital form, with no escrow;
 * finance_markets.h and battering_ram.h say outright that they are not
 * price discovery; pay/pay_equity.h has a continuous price-time order book
 * for equity shares, not for capacity with delivery; pay/pay_farm.h mints
 * VFV for verified work at operator-posted rates (or the swarm price), it
 * does not match buyers with providers. capmkt is the missing piece: a
 * buyer pays an existing provider existing VFV at a discovered price. A
 * proof-of-service verifier can reuse pay_farm's beacon spot checks
 * (pay_farm_sampled) and capmkt's post hook can mirror every movement into
 * pay_ledger. capmkt reuses kernel/src/pay/pay_util.h for wide integer
 * arithmetic and pay_assure.h for the 0.08889% assurance fee.
 *
 * MARKETS. One order book per (resource, region, tenor). Units:
 *   CM_RES_COMPUTE    1 unit = 1 normalised compute-hour (host-defined
 *                     benchmark, e.g. 1 hour of an 8 GB GPU slot)
 *   CM_RES_INFERENCE  1 unit = 1000 tokens from a large model
 *   CM_RES_STORAGE    1 unit = 1 GB stored for the tenor
 *   CM_RES_BANDWIDTH  1 unit = 1 GB relayed
 * region: a host-chosen 16-bit code (0 = anywhere). tenor: the delivery
 * window (hour, day, week, 30 days). Orders only meet inside one market.
 *
 * ORDERS. A provider posts an ASK: quantity and the lowest price per unit it
 * accepts, in VFV minor units: "a small fee of their choosing". A buyer
 * posts a BID: quantity and the highest price per unit it will pay; the bid
 * locks quantity x limit of the buyer's deposit (no credit, no debt).
 *
 * CLEARING (cm_clear): a periodic uniform-price double auction.
 *   1. Asks sorted by price then arrival, bids by price (descending) then
 *      arrival. Walk both curves and trade while ask <= bid: this maximises
 *      the traded volume (where supply meets demand).
 *   2. Every trade in the round is at ONE price p*, the midpoint (rounded
 *      down) of the competitive interval [L, U]:
 *        L = max(last traded ask, best bid left unfilled)
 *        U = min(last traded bid, best ask left unfilled)
 *      so no buyer pays above its limit, no provider gets below its ask,
 *      and no unfilled order would have wanted to trade at p*. A provider
 *      who asks less than p* still receives p*; a buyer who bid more still
 *      pays p*. Bidding your true value is the sensible strategy.
 *   3. ANTI-MONOPOLY. The volume and the price come from the full supply
 *      and demand curves, so a cap never makes buyers go without when
 *      capacity exists. What the cap decides is WHO supplies that volume:
 *      with k distinct providers willing to sell at p*, each provider's
 *      first claim is limited to max(1 unit, floor(share x volume)), where
 *      share = 8/21 when k >= 3 (the repository's Fibonacci cap, as in
 *      swarm_market M4 and carr_econ G3), 1/2 when k = 2, and the whole
 *      volume when k = 1 (the only supplier; cm_quote flags the market as
 *      concentrated). Claims are filled cheapest first, then arrival.
 *      Only if the providers under the cap cannot cover the volume does a
 *      capped provider supply the rest (again cheapest first). So a large
 *      provider cannot crowd smaller ones out by undercutting them by one
 *      unit, and a small provider's offer at or below p* is served before
 *      a dominant provider's units above its share.
 *   All arithmetic is exact integer arithmetic; ties are broken by arrival
 *   order only, never by identity or size.
 *
 * ESCROW AND PAY ON DELIVERY. Each trade becomes a contract. At clearing,
 * qty x p* moves from the bid's lock into escrow and qty x (limit - p*)
 * returns to the buyer at once. The provider is paid only per delivered
 * unit, against a proof of service (cm_deliver): the proof chain (sequence
 * and hash of the previous proof) is checked here, and its evidence (a
 * buyer-signed receipt, a storage challenge answer) by the host's verifier
 * hook. On each payment the assurance fee goes to the fee pool and the rest to the
 * provider. When the window ends, undelivered escrow returns to the buyer
 * in full (cm_expire). Nothing is ever charged for time: there is no
 * interest, no late fee, no penalty rate anywhere in this module, and the
 * refund does not depend on when cm_expire runs.
 *
 * FEE. The 0.08889% assurance fee, fee(a) = floor(a * 8889 / 10^7), exact
 * (kernel/src/pay/pay_assure.h F1; it replaced the former phi-percent tithe).
 * It is a hook (cm_params_t.fee) so a deployment can route it elsewhere (the
 * provider bridge routes it to prov_fee); the default is pay_assure_fee().
 * Each fee is charged once per delivery payment and partitioned exactly into
 * the four bucket counters fee_bucket[] (pay_assure_split: 50% reserve floor
 * + remainder, 25% V-Bill dividend pool, 15% infrastructure/node bounties,
 * 10% regenerative capital). The no-carry form is used: a payment's fee must
 * be recomputable by every peer from that payment alone.
 *
 * PEER TO PEER. Clearing is a pure function of the round's order set:
 * every participant that holds the same signed orders computes the same
 * fills. cm_round_digest() is a SHA3-256 over the canonical order set, so
 * a buyer and a provider can check they cleared the same round before
 * accepting a contract. Signing and gossiping orders (Carracho records,
 * devmesh sessions) is the caller's job; this module trusts the order set
 * it is given.
 *
 * WHAT IT DOES NOT DO: hold real money (deposits are a mirror of what the
 * buyer locked in its own wallet ledger: cm_params_t.post can mirror every
 * movement into pay_ledger); measure service (the verifier does); stop two
 * identities of one provider from splitting their supply to dodge the cap
 * (Sybil resistance needs identity cost, e.g. carracho proof of work);
 * guarantee a round's order set is the same everywhere without a consensus
 * step.
 *
 * Freestanding integer C11: no libc, no float, no allocation, no 64-bit
 * division (pay_udiv64 / zt_udiv64), no __int128.
 */
#ifndef ZXV_CAPMKT_H
#define ZXV_CAPMKT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "../pay/pay_assure.h"

#define CM_ID_BYTES      16u
#define CM_MAX_ACCOUNTS  64u
#define CM_MAX_ORDERS    256u
#define CM_MAX_CONTRACTS 256u
#define CM_MAX_FILLS     512u
#define CM_AMOUNT_MAX    ((1ull << 62) - 1ull)

#define CM_SHARE_NUM 8u  /* F(6) */
#define CM_SHARE_DEN 21u /* F(8) */

typedef enum {
    CM_RES_COMPUTE = 1,
    CM_RES_INFERENCE = 2,
    CM_RES_STORAGE = 3,
    CM_RES_BANDWIDTH = 4,
} cm_resource_t;

typedef enum {
    CM_TENOR_HOUR = 1,
    CM_TENOR_DAY = 2,
    CM_TENOR_WEEK = 3,
    CM_TENOR_MONTH = 4, /* 30 days */
} cm_tenor_t;

typedef enum {
    CM_OK = 0,
    CM_ERR_ARG = -1,
    CM_ERR_FULL = -2,
    CM_ERR_FUNDS = -3,    /* not enough deposit for the bid lock */
    CM_ERR_OVERFLOW = -4, /* an amount above CM_AMOUNT_MAX */
    CM_ERR_UNKNOWN = -5,  /* no such order / contract / account */
    CM_ERR_STATE = -6,
    CM_ERR_PROOF = -7, /* proof chain or evidence refused */
    CM_ERR_LATE = -8,  /* delivery after the window: not paid, no penalty */
    CM_ERR_SELF = -10, /* bid and ask from the same account */
} cm_status_t;

typedef struct {
    uint8_t resource; /* cm_resource_t */
    uint8_t tenor;    /* cm_tenor_t */
    uint16_t region;
} cm_key_t;

#define CM_ASK 1u
#define CM_BID 2u

typedef struct {
    uint8_t used, side;
    cm_key_t key;
    uint32_t id;
    uint8_t owner[CM_ID_BYTES];
    uint64_t qty, filled;
    uint64_t price;   /* per unit, VFV minor units: ask = minimum, bid = maximum */
    uint64_t seq;     /* arrival order */
    uint64_t expires; /* ms; the order leaves the book after this */
    uint64_t locked;  /* bid: deposit still locked for the unfilled part */
} cm_order_t;

typedef struct {
    uint8_t used;
    uint8_t id[CM_ID_BYTES];
    uint64_t available; /* deposit not locked */
    uint64_t locked;    /* held by open bids */
    uint64_t escrow;    /* held by this buyer's open contracts */
    uint64_t earned;    /* statistic: paid to this provider (after fee),
                         * already included in available */
    uint64_t delivered; /* units delivered as provider */
    uint64_t missed;    /* units sold but not delivered in the window */
} cm_account_t;

#define CM_C_OPEN   1u
#define CM_C_CLOSED 2u

typedef struct {
    uint8_t used, state;
    cm_key_t key;
    uint32_t id, bid_id, ask_id;
    uint8_t buyer[CM_ID_BYTES];
    uint8_t provider[CM_ID_BYTES];
    uint64_t qty, delivered, price;
    uint64_t escrow; /* still held */
    uint64_t paid;   /* to the provider, after the fee */
    uint64_t fee;    /* assurance fee charged on this contract */
    uint64_t start_ms, end_ms;
    uint64_t round;
    uint8_t round_digest[32];
    uint32_t proof_seq;
    uint8_t proof_tip[32]; /* hash of the last accepted proof */
} cm_contract_t;

typedef struct {
    uint32_t contract_id;
    uint32_t seq; /* proof_seq + 1 */
    uint64_t units;
    uint64_t at_ms;
    uint8_t prev[32];     /* proof_tip of the contract */
    uint8_t evidence[64]; /* host-defined (receipt hash, challenge answer) */
} cm_proof_t;

typedef struct {
    /* assurance fee on a payment amount; NULL = cm_fee_assure */
    uint64_t (*fee)(void *ctx, uint64_t amount);
    /* check the evidence of a proof; NULL = refuse every proof */
    bool (*verify)(void *ctx, const cm_contract_t *c, const cm_proof_t *p);
    /* mirror a movement into a ledger (optional). kind: CM_POST_* */
    void (*post)(void *ctx, uint8_t kind, const uint8_t from[CM_ID_BYTES],
                 const uint8_t to[CM_ID_BYTES], uint64_t amount);
    void *ctx;
    uint32_t share_num, share_den; /* default 8/21 */
} cm_params_t;

#define CM_POST_DEPOSIT  1u
#define CM_POST_WITHDRAW 2u
#define CM_POST_ESCROW   3u /* bid lock -> escrow at clearing */
#define CM_POST_REFUND   4u /* to the buyer: limit difference or undelivered */
#define CM_POST_PAY      5u /* escrow -> provider */
#define CM_POST_FEE      6u /* escrow -> fee pool (the four buckets) */

typedef struct {
    uint32_t bid_id, ask_id;
    uint64_t qty;
} cm_fill_t;

typedef struct {
    cm_key_t key;
    uint64_t round;
    uint64_t price;     /* p*, 0 when nothing traded */
    uint64_t volume;    /* units traded */
    uint64_t lo, hi;    /* the competitive interval [L, U] */
    uint32_t providers; /* k: distinct providers able to trade */
    uint64_t cap;       /* first-claim cap per provider (= volume when k = 1) */
    uint64_t overflow;  /* units supplied beyond the cap (no one else could) */
    uint32_t nfills;
    cm_fill_t fills[CM_MAX_FILLS];
    uint8_t digest[32];
    uint64_t unmet_demand; /* units bid at >= p* left unfilled */
} cm_result_t;

typedef struct {
    uint64_t last_price, last_volume, last_round;
    uint64_t best_bid, best_ask; /* 0 when none */
    uint64_t bid_units, ask_units;
    uint32_t providers;
    bool concentrated; /* one provider only, or one holding > share of supply */
} cm_quote_t;

#define CM_MAX_MARKETS 32u
typedef struct {
    cm_key_t key;
    uint8_t used;
    uint64_t last_price, last_volume, last_round;
} cm_book_stat_t;

typedef struct {
    cm_params_t p;
    cm_account_t acct[CM_MAX_ACCOUNTS];
    cm_order_t ord[CM_MAX_ORDERS];
    cm_contract_t con[CM_MAX_CONTRACTS];
    cm_book_stat_t stat[CM_MAX_MARKETS];
    uint32_t next_order, next_contract;
    uint64_t next_seq, round;
    uint64_t commons;                        /* assurance fees collected (sum of fee_bucket) */
    uint64_t fee_bucket[PAY_ASSURE_BUCKETS]; /* pay_assure_bucket_t */
    uint64_t deposits;                       /* total deposited - withdrawn */
    /* scratch for clearing */
    uint16_t ai[CM_MAX_ORDERS], bi[CM_MAX_ORDERS], rep[CM_MAX_ORDERS];
    uint64_t sold[CM_MAX_ORDERS], alloc[CM_MAX_ORDERS], balloc[CM_MAX_ORDERS];
} cm_market_t;

/* ===== setup ===== */
void cm_params_default(cm_params_t *p);
void cm_init(cm_market_t *m, const cm_params_t *p);

/* the 0.08889% assurance fee, floor(a * 8889 / 10^7) (= pay_assure_fee) */
uint64_t cm_fee_assure(uint64_t a);

/* ===== accounts ===== */
cm_status_t cm_deposit(cm_market_t *m, const uint8_t id[CM_ID_BYTES], uint64_t amount);
cm_status_t cm_withdraw(cm_market_t *m, const uint8_t id[CM_ID_BYTES], uint64_t amount);
const cm_account_t *cm_account(const cm_market_t *m, const uint8_t id[CM_ID_BYTES]);

/* ===== orders ===== */
cm_status_t cm_ask(cm_market_t *m, const uint8_t provider[CM_ID_BYTES], cm_key_t key, uint64_t qty,
                   uint64_t price, uint64_t expires_ms, uint32_t *order_id);
cm_status_t cm_bid(cm_market_t *m, const uint8_t buyer[CM_ID_BYTES], cm_key_t key, uint64_t qty,
                   uint64_t limit, uint64_t expires_ms, uint32_t *order_id);
cm_status_t cm_cancel(cm_market_t *m, const uint8_t owner[CM_ID_BYTES], uint32_t order_id);
const cm_order_t *cm_order(const cm_market_t *m, uint32_t order_id);

/* ===== clearing ===== */
/* SHA3-256 over the canonical order set of market `key` at time now. */
void cm_round_digest(const cm_market_t *m, cm_key_t key, uint64_t now_ms, uint8_t out[32]);

/* Clear one market: auction, escrow, contracts (window [now, now+tenor)).
 * Expired orders are removed first (their locks returned). */
cm_status_t cm_clear(cm_market_t *m, cm_key_t key, uint64_t now_ms, cm_result_t *out);

/* The pure auction (no state change) for tests and previews. */
cm_status_t cm_auction(cm_market_t *m, cm_key_t key, uint64_t now_ms, cm_result_t *out);

void cm_quote(const cm_market_t *m, cm_key_t key, uint64_t now_ms, cm_quote_t *q);

/* ===== delivery ===== */
cm_status_t cm_deliver(cm_market_t *m, const cm_proof_t *proof);
/* Hash a proof (what the next proof's prev must equal). */
void cm_proof_hash(const cm_proof_t *p, uint8_t out[32]);
/* Close every contract whose window has ended: refund undelivered escrow. */
uint32_t cm_expire(cm_market_t *m, uint64_t now_ms);
const cm_contract_t *cm_contract(const cm_market_t *m, uint32_t id);

uint64_t cm_tenor_ms(uint8_t tenor);

/* Money conservation: sum over accounts of available + locked + escrow,
 * plus commons, equals deposits; every account's locked equals its open
 * bids' locks and its escrow equals its open contracts' escrow. */
bool cm_conserved(const cm_market_t *m);

#endif /* ZXV_CAPMKT_H */
