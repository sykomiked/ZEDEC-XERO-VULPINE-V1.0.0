/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* market.h — peer-to-peer commerce: storefronts, listings, carts, orders,
 * escrow, disputes, returns, reviews, discovery, agent shopping and B2B.
 *
 * WHAT THIS IS
 * ------------
 * The commerce state machine that runs on each node. Storefronts and
 * listings are SIGNED objects (the owner's key signs a SHA3-256 digest of a
 * canonical encoding, so they can travel over any transport and be checked
 * anywhere). Orders move through one table-driven state machine; buyer money
 * sits in an ESCROW book per order until delivery proof plus a timer, buyer
 * confirmation, or a mediator ruling releases it.
 *
 *   PENDING_PAYMENT --pay--> PAID --fulfil--> FULFILLED --confirm/timer--> COMPLETED
 *        |                    |                   |                          |
 *     cancel/expire        cancel/timeout      dispute --ruling--> RESOLVED  return
 *        v                    v                   v                          v
 *    CANCELLED            REFUNDED            DISPUTED                  RETURN_OPEN
 *
 * MONEY RULES (exact integers, minor units of one currency per order)
 *   M1  Every price is an integer count of the currency's minor units. The
 *       operator registers currencies (ISO 4217 alpha, numeric, minor units
 *       from its own ISO 4217 table, e.g. kernel/src/cbank) and VFV (555, 2)
 *       is registered by default. No floating point anywhere.
 *   M2  Escrow conservation, per order and globally:
 *         in  = captured + seller_funded + commons_returned + tax_returned
 *         out = to_seller + to_commons + to_tax + to_buyer
 *         held = in - out >= 0, and held == 0 in every terminal state.
 *   M3  The 0.08889% assurance fee is pay_assure_fee() from kernel/src/pay
 *       (one source of truth, not a copy; it replaced the phi-percent tithe)
 *       applied ONCE per sale to the seller's net revenue excluding tax: after
 *       every settlement or refund
 *         fee_charged == to_commons - commons_returned
 *                     == pay_assure_fee(released_base - returned_base).
 *       Charging fee(cumulative base) - fee already charged is the per-order
 *       sub-unit carry of pay_assure.h F2: partial releases pay exactly what
 *       one release of their sum would pay. A sale refunded before release
 *       pays no fee. to_commons is the order's fee pool; the operator's settle
 *       hook moves it to the four fee buckets (pay_assure_split) on its
 *       ledger.
 *   M4  Refund exactness: a line's charge splits into base (seller revenue)
 *       and tax. After r of n units are refunded the cumulative refund is
 *         floor(base * r / n) + floor(tax * r / n),
 *       so refunding every unit, in any batches, returns exactly what was
 *       charged, tax included, to the minor unit, and each batch's base and
 *       tax parts are never negative.
 *   M5  Tax: the OPERATOR supplies rates (region, tax class, num/den,
 *       exclusive or inclusive, rounding). This module only does arithmetic;
 *       a missing rate yields zero tax and sets order.tax_unconfigured. It
 *       gives no tax advice.
 *   M6  No interest anywhere. B2B invoices may carry only FLAT, disclosed
 *       fees, fixed when the purchase order is accepted, applied at most once,
 *       capped by policy. There is no rate field to misuse.
 *
 * DISCOVERY. Results are ranked by the Interaction Surplus Framework of
 * kernel/src/concord (complementarity of buyer and seller, via the `isf`
 * hook; see mk_concord.h) weighted by review quality with a neutral prior so
 * new sellers are not buried, with a per-store cap per page (anti-monopoly).
 * There is no ad-spend input of any kind.
 *
 * AGENTS. An assistant may shop under a user's mandate (limits, categories,
 * expiry) but every order it builds needs the user's confirmation token: a
 * signature by the user over that exact order digest. Without it the order
 * cannot be paid; a token for one order confirms no other.
 *
 * HONEST LIMITS. This is a state machine, not a payment processor: money
 * moves only through the `settle` hook (wired to kernel/src/pay, cardnet or a
 * bank), which must apply each settlement atomically. Signed objects
 * (storefronts, listings, POs, rulings, confirmation tokens) are verified
 * through the `verify` hook (mk_pq.h wires ML-DSA-65); other calls take an
 * `actor` the local node has already authenticated. Delivery proof for
 * physical goods is only as good as the `verify_delivery` hook (a carrier or
 * courier attestation); without one the release timer is doubled. Disputes
 * are decided by people, not by this code. Juror selection is the operator's
 * hook. Capacities are fixed (MK_MAX_*). Marketplace, consumer-protection,
 * tax, sanctions and licensing law differ by jurisdiction and are for the
 * operator and counsel. Freestanding C11: no libc, no allocation, no float,
 * no 64-bit division (pay_udiv64 / pay_muldiv).
 */
#ifndef ZXV_MARKET_H
#define ZXV_MARKET_H

#include <stdint.h>
#include <stdbool.h>
#include "pay_util.h"

#define MK_MAX_CCY       16u
#define MK_MAX_STORES    32u
#define MK_MAX_LISTINGS  128u
#define MK_MAX_ORDERS    128u
#define MK_ORDER_LINES   8u
#define MK_MAX_CARTS     16u
#define MK_CART_LINES    16u
#define MK_MAX_REVIEWS   256u
#define MK_MAX_EVIDENCE  8u
#define MK_MAX_JURORS    5u
#define MK_MAX_SLOTS     8u
#define MK_MAX_SUBS      32u
#define MK_MAX_MANDATES  16u
#define MK_MAX_POS       32u
#define MK_MAX_INVOICES  32u
#define MK_MAX_TAX_RATES 32u
#define MK_INVOICE_FEES  4u
#define MK_CID_MAX       64u /* textual CID, NUL-terminated */
#define MK_NAME_MAX      48u
#define MK_REGION_MAX    8u
#define MK_UNLIMITED     0xFFFFFFFFu
#define MK_Q16           65536u
#define MK_NONE          0u /* invalid handle; handles are index + 1 */

typedef struct {
    uint8_t b[32];
} mk_id_t;

typedef enum {
    MK_OK = 0,
    MK_ERR_ARG = -1,
    MK_ERR_FULL = -2,
    MK_ERR_NOT_FOUND = -3,
    MK_ERR_STATE = -4,    /* transition not allowed from this state     */
    MK_ERR_AUTH = -5,     /* wrong actor or bad signature                */
    MK_ERR_STOCK = -6,    /* not enough inventory / slot capacity        */
    MK_ERR_CCY = -7,      /* unknown or mismatched currency              */
    MK_ERR_OVERFLOW = -8, /* arithmetic would overflow                   */
    MK_ERR_SETTLE = -9,   /* the settle hook refused; nothing changed    */
    MK_ERR_HOOK = -10,    /* a required hook is missing                  */
    MK_ERR_CONFIRM = -11, /* agent order lacks the user's confirmation   */
    MK_ERR_MANDATE = -12, /* outside the agent's mandate                 */
    MK_ERR_WINDOW = -13,  /* outside a time window                       */
    MK_ERR_DUPLICATE = -14,
    MK_ERR_POLICY = -15
} mk_status_t;

/* ===== Currencies ===== */
typedef struct {
    bool used;
    bool is_vfv;
    char alpha[4];
    uint16_t numeric;
    uint8_t minor;
} mk_ccy_t;

/* ===== Listings ===== */
typedef enum {
    MK_KIND_PHYSICAL = 0,
    MK_KIND_DIGITAL = 1, /* delivered by CID */
    MK_KIND_SERVICE = 2,
    MK_KIND_BOOKING = 3,
    MK_KIND_SUBSCRIPTION = 4,
    MK_KIND_COUNT = 5
} mk_kind_t;

typedef struct {
    uint64_t start;    /* tick (seconds)               */
    uint32_t duration; /* seconds                      */
    uint32_t capacity;
    uint32_t booked;
} mk_slot_t;

typedef struct {
    char name[MK_NAME_MAX];
    mk_id_t owner; /* key fingerprint of the seller          */
    char region[MK_REGION_MAX];
    uint32_t concord_id; /* the seller's person id in concord      */
    bool institution;    /* individual or institution (display)    */
    bool has_arbiter;    /* a chosen arbiter, else community panel */
    mk_id_t arbiter;
    uint32_t fulfil_within; /* seconds after payment to fulfil        */
    uint32_t version;       /* strictly increasing per update         */
} mk_store_spec_t;

typedef struct {
    bool used;
    bool active;
    mk_store_spec_t spec;
    uint8_t digest[32];
    uint64_t rating_wsum; /* sum of (rating-1) * weight_q16            */
    uint64_t weight_sum;  /* sum of weight_q16                         */
    uint32_t n_reviews;
    uint32_t seller_defaults; /* returns not honoured, etc.          */
} mk_store_t;

typedef struct {
    uint16_t store; /* handle */
    mk_kind_t kind;
    char title[64];
    uint16_t category; /* 0..63 */
    uint8_t ccy;       /* handle */
    uint64_t price;    /* minor units per unit                       */
    uint64_t shipping; /* flat per order line, minor units           */
    uint32_t stock;    /* MK_UNLIMITED for digital / services        */
    uint16_t tax_class;
    char cid[MK_CID_MAX];   /* digital goods: the content CID       */
    uint32_t return_window; /* seconds after fulfilment, 0 = none  */
    uint32_t auto_release;  /* seconds after fulfilment            */
    uint64_t cancel_fee;    /* bookings: flat, disclosed            */
    uint32_t cancel_cutoff; /* bookings: free cancel until start-x  */
    uint32_t period;        /* subscriptions: seconds per period    */
    mk_slot_t slot[MK_MAX_SLOTS];
    uint8_t n_slots;
    uint32_t version;
} mk_listing_spec_t;

typedef struct {
    bool used;
    bool active;
    mk_listing_spec_t spec;
    uint32_t reserved; /* units held by unpaid orders */
    uint8_t digest[32];
} mk_listing_t;

/* ===== Tax (operator-supplied) ===== */
typedef enum { MK_ROUND_HALF_UP = 0, MK_ROUND_FLOOR = 1, MK_ROUND_CEIL = 2 } mk_round_t;

typedef struct {
    bool used;
    char region[MK_REGION_MAX];
    uint16_t tax_class;
    uint32_t num, den; /* rate = num/den, e.g. 20/100           */
    bool inclusive;    /* prices already include the tax        */
    bool tax_shipping; /* shipping is part of the taxable base  */
    mk_round_t rounding;
} mk_tax_rate_t;

/* ===== Orders ===== */
typedef enum {
    MK_ORD_FREE = 0,
    MK_ORD_PENDING_PAYMENT,
    MK_ORD_PAID,
    MK_ORD_FULFILLED,
    MK_ORD_COMPLETED,
    MK_ORD_DISPUTED,
    MK_ORD_RESOLVED,
    MK_ORD_RETURN_OPEN,
    MK_ORD_CANCELLED,
    MK_ORD_REFUNDED,
    MK_ORD_EXPIRED,
    MK_ORD_STATE_COUNT
} mk_order_state_t;

typedef enum {
    MK_EV_PAY = 0,
    MK_EV_CANCEL,
    MK_EV_EXPIRE,
    MK_EV_FULFIL,
    MK_EV_CONFIRM,
    MK_EV_AUTO_RELEASE,
    MK_EV_FULFIL_TIMEOUT,
    MK_EV_DISPUTE,
    MK_EV_RULING,
    MK_EV_REQUEST_RETURN,
    MK_EV_RETURN_RECEIVED,
    MK_EV_RETURN_DECLINED,
    MK_EV_REFUND_ALL, /* every unit refunded */
    MK_EV_COUNT
} mk_event_t;

typedef struct {
    uint16_t listing;
    uint32_t qty;
    uint8_t slot; /* bookings */
    uint64_t unit_price;
    uint64_t shipping;
    uint64_t line_total; /* what the buyer pays for this line        */
    uint64_t tax;        /* tax contained in line_total              */
    uint32_t refunded_qty;
    uint64_t refunded_total; /* cumulative, exact (M4)           */
    uint64_t refunded_tax;
} mk_line_t;

typedef struct {
    mk_id_t from;
    char cid[MK_CID_MAX];
    uint8_t digest[32];
    uint64_t at;
} mk_evidence_t;

typedef struct {
    bool open;
    bool community;
    mk_id_t opened_by;
    mk_id_t arbiter;
    mk_id_t juror[MK_MAX_JURORS];
    uint8_t n_jurors;
    bool voted[MK_MAX_JURORS];
    uint64_t vote[MK_MAX_JURORS]; /* buyer's share of held escrow */
    mk_evidence_t ev[MK_MAX_EVIDENCE];
    uint8_t n_ev;
    uint64_t opened_at;
    uint64_t deadline;
    uint64_t buyer_award; /* final */
    mk_order_state_t from_state;
} mk_dispute_t;

typedef struct {
    bool open;
    uint8_t line;
    uint32_t qty;
    uint64_t requested_at;
    bool shipped_back;
    uint64_t shipped_at;
    mk_order_state_t from_state;
} mk_return_t;

typedef struct {
    bool used;
    mk_order_state_t state;
    uint16_t store;
    uint8_t ccy;
    mk_id_t buyer;
    mk_id_t seller;
    mk_line_t line[MK_ORDER_LINES];
    uint8_t n_lines;
    char region[MK_REGION_MAX];
    bool tax_unconfigured;
    uint64_t total; /* sum of line_total */
    uint64_t tax;   /* sum of line tax   */
    /* times */
    uint64_t created_at, pay_by, paid_at, fulfil_by, fulfilled_at, release_at, completed_at;
    bool proof_verified;
    uint8_t proof_digest[32];
    /* escrow book (M2) */
    uint64_t captured, seller_funded, commons_returned, tax_returned;
    uint64_t to_seller, to_commons, to_tax, to_buyer;
    uint64_t held_base, held_tax; /* held = held_base + held_tax */
    /* assurance fee (M3) */
    uint64_t released_base, returned_base, fee_charged;
    uint64_t cancel_fee; /* booking late-cancel fee kept by the seller */
    bool settled;        /* the seller was paid at least once: review-eligible */
    bool reviewed;
    /* arbitration choice made at checkout */
    bool community_mediation;
    mk_dispute_t dispute;
    mk_return_t ret;
    /* agents */
    bool needs_confirm;
    bool confirmed;
    mk_id_t agent;
    uint8_t mandate; /* handle */
    uint64_t confirm_by;
    uint64_t nonce;
    uint8_t digest[32];
    uint8_t sub; /* subscription handle, 0 none */
} mk_order_t;

/* ===== Settlement instruction for the `settle` hook (atomic) ===== */
typedef enum {
    MK_SETTLE_CAPTURE = 0, /* from_buyer into escrow                    */
    MK_SETTLE_RELEASE,     /* escrow to seller / commons / tax         */
    MK_SETTLE_REFUND,      /* escrow (and returns) to buyer            */
    MK_SETTLE_RULING,      /* dispute split                             */
    MK_SETTLE_INVOICE      /* B2B invoice payment                       */
} mk_settle_kind_t;

typedef struct {
    mk_settle_kind_t kind;
    uint32_t ref; /* order or invoice handle */
    uint8_t ccy;
    const mk_id_t *buyer;
    const mk_id_t *seller;
    uint64_t from_buyer, from_seller, from_commons, from_tax;
    uint64_t to_seller, to_commons, to_tax, to_buyer;
} mk_settle_t;

/* ===== Reviews ===== */
typedef struct {
    bool used;
    uint16_t order, store, listing;
    mk_id_t reviewer;
    uint8_t rating; /* 1..5 */
    char cid[MK_CID_MAX];
    uint32_t weight_q16;
    uint64_t at;
} mk_review_t;

/* ===== Carts ===== */
typedef struct {
    uint16_t listing;
    uint32_t qty;
    uint8_t slot;
} mk_cart_line_t;

typedef struct {
    bool used;
    mk_id_t owner;
    mk_cart_line_t line[MK_CART_LINES];
    uint8_t n;
} mk_cart_t;

/* ===== Subscriptions (no auto-renew traps) ===== */
typedef struct {
    bool used;
    mk_id_t buyer;
    uint16_t listing;
    char region[MK_REGION_MAX];
    bool auto_renew;     /* off unless the buyer opts in             */
    bool cancelled;      /* one call, any time, no fee               */
    uint64_t price_lock; /* renewal never above this without consent */
    uint64_t period_end;
    bool reminded;
    uint64_t reminded_at;
    uint32_t renewals;
    uint32_t max_renewals;
    uint16_t current_order;
} mk_sub_t;

/* ===== Agent mandates ===== */
typedef struct {
    bool used;
    bool revoked;
    mk_id_t user;
    mk_id_t agent;
    uint8_t ccy;
    uint64_t max_per_order;
    uint64_t max_total;
    uint64_t committed;  /* confirmed orders */
    uint64_t categories; /* bitmask of allowed categories          */
    uint64_t expires;
} mk_mandate_t;

/* ===== B2B ===== */
typedef enum {
    MK_PO_FREE = 0,
    MK_PO_DRAFT,
    MK_PO_SUBMITTED,
    MK_PO_ACCEPTED,
    MK_PO_REJECTED,
    MK_PO_INVOICED,
    MK_PO_CLOSED,
    MK_PO_CANCELLED
} mk_po_state_t;

typedef struct {
    uint16_t listing;
    uint32_t qty;
    uint64_t unit_price; /* agreed price */
} mk_po_line_t;

typedef struct {
    char po_number[24];
    mk_id_t buyer_org;
    uint16_t store;
    uint8_t ccy;
    char region[MK_REGION_MAX];
    mk_po_line_t line[MK_ORDER_LINES];
    uint8_t n_lines;
    uint32_t net_days;    /* payment terms                           */
    uint64_t late_fee;    /* FLAT, disclosed, once; 0 = none         */
    uint64_t service_fee; /* FLAT, disclosed; 0 = none               */
} mk_po_spec_t;

typedef struct {
    bool used;
    mk_po_state_t state;
    mk_po_spec_t spec;
    uint8_t digest[32];
    uint16_t invoice;
} mk_po_t;

typedef enum {
    MK_INV_FREE = 0,
    MK_INV_ISSUED,
    MK_INV_INITIATED, /* pain.001 handed to the debtor's bank */
    MK_INV_OVERDUE,
    MK_INV_PAID,
    MK_INV_VOID
} mk_inv_state_t;

typedef struct {
    char label[24];
    uint64_t amount;
    bool late; /* applied only once the invoice is overdue */
    bool applied;
} mk_fee_t;

typedef struct {
    bool used;
    mk_inv_state_t state;
    uint16_t po;
    uint16_t store;
    uint8_t ccy;
    mk_id_t buyer;
    mk_id_t seller;
    char number[24];
    uint64_t base; /* net of tax */
    uint64_t tax;
    uint64_t fees; /* applied flat fees */
    mk_fee_t fee[MK_INVOICE_FEES];
    uint8_t n_fees;
    uint64_t issued_at, due_at, paid_at;
    uint64_t assure_fee; /* the 0.08889% assurance fee charged at settlement */
    bool tax_unconfigured;
} mk_invoice_t;

/* pain.001 request handed to the operator's ISO 20022 builder (pay_iso). */
typedef struct {
    const char *invoice_number;
    const char *end_to_end_id; /* "ZXV-INV-<number>"            */
    const mk_id_t *debtor;
    const mk_id_t *creditor;
    const char *ccy_alpha;
    uint8_t minor;
    uint64_t amount;
    uint64_t due_at;
    const char *remittance; /* "/ZXV/INV/<number>"            */
} mk_pain001_req_t;

/* ===== Notifications ===== */
typedef enum {
    MK_NOTE_RENEWAL_REMINDER = 0,
    MK_NOTE_RENEWAL_PRICE_CHANGED,
    MK_NOTE_RENEWED,
    MK_NOTE_AUTO_RELEASED,
    MK_NOTE_AUTO_CANCELLED,
    MK_NOTE_INVOICE_OVERDUE,
    MK_NOTE_CONFIRM_NEEDED,
    MK_NOTE_COMPLETED /* a trade delivered as promised (quest, reputation) */
} mk_note_t;

/* ===== Hooks (narrow interfaces to other modules) ===== */
typedef struct {
    void *ctx;
    /* Signature check by the key whose fingerprint is `signer` (mk_pq.h). */
    bool (*verify)(void *ctx, const mk_id_t *signer, const uint8_t *msg, uint32_t len,
                   const uint8_t *sig, uint32_t sig_len);
    /* Apply a settlement atomically (pay ledger, cardnet, bank). */
    bool (*settle)(void *ctx, const mk_settle_t *s);
    /* Physical delivery proof (carrier / courier attestation). Optional. */
    bool (*verify_delivery)(void *ctx, uint32_t order, const uint8_t *proof, uint32_t len);
    /* ISF complementarity, Q16 in [0, 65536]. Optional (neutral). */
    uint32_t (*isf)(void *ctx, uint32_t buyer_concord, uint32_t seller_concord);
    /* Mutual concord divides hide both sides. Optional. */
    bool (*can_see)(void *ctx, uint32_t buyer_concord, uint32_t seller_concord);
    /* Sybil-resistant review weight, Q16 in [0, 65536]. Optional (1.0). */
    uint32_t (*review_weight)(void *ctx, const mk_id_t *reviewer, uint16_t store);
    /* Community panel: write up to `max` juror ids; return the count. */
    uint32_t (*select_jurors)(void *ctx, uint32_t order, mk_id_t *out, uint32_t max);
    /* Build pain.001 bytes; return length or < 0. */
    int32_t (*pain001)(void *ctx, const mk_pain001_req_t *req, char *out, uint32_t cap);
    void (*notify)(void *ctx, mk_note_t kind, uint32_t ref);
    /* Optional operator gate at checkout (kernel/src/ident opt-in KYC tier,
     * sanctions screening). NULL = open market. */
    bool (*party_ok)(void *ctx, const mk_id_t *buyer, const mk_id_t *seller, uint8_t ccy,
                     uint64_t total);
} mk_hooks_t;

typedef struct {
    uint32_t payment_window;   /* unpaid orders expire (s)            */
    uint32_t default_fulfil;   /* when the store sets 0 (s)           */
    uint32_t min_auto_release; /* clamps listing auto_release         */
    uint32_t max_auto_release;
    uint32_t dispute_window;     /* ruling deadline after opening       */
    uint32_t return_accept;      /* seller must act after return ship   */
    uint32_t confirm_window;     /* agent orders: user must confirm     */
    uint32_t renewal_notice;     /* reminder lead time                  */
    uint32_t booking_grace;      /* after slot end                      */
    uint64_t max_flat_fee;       /* cap on any single B2B flat fee      */
    bool facilitator_remits_tax; /* tax goes to the tax account        */
    uint8_t per_store_cap;       /* discovery: max hits per store/page  */
    uint8_t jurors;              /* community panel size (odd)          */
} mk_policy_t;

typedef struct {
    mk_hooks_t hooks;
    mk_policy_t policy;
    uint64_t now;
    uint64_t nonce;
    mk_ccy_t ccy[MK_MAX_CCY];
    mk_store_t store[MK_MAX_STORES];
    mk_listing_t listing[MK_MAX_LISTINGS];
    mk_tax_rate_t tax[MK_MAX_TAX_RATES];
    mk_order_t order[MK_MAX_ORDERS];
    mk_cart_t cart[MK_MAX_CARTS];
    mk_review_t review[MK_MAX_REVIEWS];
    mk_sub_t sub[MK_MAX_SUBS];
    mk_mandate_t mandate[MK_MAX_MANDATES];
    mk_po_t po[MK_MAX_POS];
    mk_invoice_t invoice[MK_MAX_INVOICES];
} mk_market_t;

/* ===== Setup ===== */
void mk_policy_default(mk_policy_t *p);
/* Zero the market, set hooks/policy (NULL policy = defaults), register VFV. */
void mk_init(mk_market_t *m, const mk_hooks_t *hooks, const mk_policy_t *policy);
/* Register a currency; minor units come from the operator's ISO 4217 table.
 * Returns the handle (>= 1) or a negative mk_status_t. */
int32_t mk_ccy_register(mk_market_t *m, const char *alpha, uint16_t numeric, uint8_t minor,
                        bool is_vfv);
int32_t mk_ccy_find(const mk_market_t *m, const char *alpha);
int32_t mk_tax_set(mk_market_t *m, const mk_tax_rate_t *rate);
/* Advance the clock and run every timer (expiry, auto-release, fulfilment
 * timeouts, return timeouts, dispute deadlines, renewals, overdue). */
void mk_tick(mk_market_t *m, uint64_t now);

/* ===== Signed storefronts and listings ===== */
/* Canonical SHA3-256 digests that the owner signs. */
void mk_store_digest(const mk_store_spec_t *s, uint8_t out[32]);
void mk_listing_digest(const mk_market_t *m, const mk_listing_spec_t *l, uint8_t out[32]);
/* Open (or update, by passing the existing handle in *handle) a storefront.
 * The signature is by spec->owner over the digest. Updates need a higher
 * version and the same owner. */
mk_status_t mk_store_publish(mk_market_t *m, const mk_store_spec_t *spec, const uint8_t *sig,
                             uint32_t sig_len, uint16_t *handle);
mk_status_t mk_store_close(mk_market_t *m, const mk_id_t *actor, uint16_t store);
mk_status_t mk_listing_publish(mk_market_t *m, const mk_listing_spec_t *spec, const uint8_t *sig,
                               uint32_t sig_len, uint16_t *handle);
mk_status_t mk_listing_withdraw(mk_market_t *m, const mk_id_t *actor, uint16_t listing);
/* Inventory: add units (seller). */
mk_status_t mk_restock(mk_market_t *m, const mk_id_t *actor, uint16_t listing, uint32_t qty);
/* Units that can still be ordered (stock - reserved), MK_UNLIMITED if so. */
uint32_t mk_available(const mk_market_t *m, uint16_t listing);

/* ===== Carts and checkout ===== */
int32_t mk_cart_open(mk_market_t *m, const mk_id_t *owner);
mk_status_t mk_cart_add(mk_market_t *m, uint16_t cart, uint16_t listing, uint32_t qty,
                        uint8_t slot);
mk_status_t mk_cart_remove(mk_market_t *m, uint16_t cart, uint16_t listing);
void mk_cart_clear(mk_market_t *m, uint16_t cart);

typedef struct {
    char region[MK_REGION_MAX]; /* destination tax region            */
    bool community_mediation;   /* else the store's chosen arbiter   */
    const mk_id_t *agent;       /* non-NULL: built by an assistant   */
    uint16_t mandate;           /* required when agent != NULL       */
} mk_checkout_opts_t;

/* Split the cart into one order per (store, currency), reserve inventory
 * for all of it or none, compute tax, and clear the cart. Writes order
 * handles to out[]; returns the count or a negative mk_status_t. */
int32_t mk_checkout(mk_market_t *m, uint16_t cart, const mk_checkout_opts_t *opts, uint16_t *out,
                    uint32_t max);

/* ===== The order state machine ===== */
const mk_order_t *mk_order(const mk_market_t *m, uint16_t order);
/* Pure transition table: the state an event leads to, or MK_ORD_FREE if the
 * event is not allowed in `from`. MK_ORD_STATE_COUNT means "back to the state
 * the return was opened from" (RETURN_OPEN only). Guards (actors, windows,
 * amounts) are applied on top by the calls below. */
mk_order_state_t mk_order_next(mk_order_state_t from, mk_event_t ev);
const char *mk_order_state_name(mk_order_state_t s);

mk_status_t mk_order_pay(mk_market_t *m, uint16_t order, const mk_id_t *actor);
mk_status_t mk_order_cancel(mk_market_t *m, uint16_t order, const mk_id_t *actor);
/* Seller: delivery proof. PHYSICAL: carrier attestation for verify_delivery.
 * DIGITAL: the CID text, which must equal every digital line's listing CID.
 * SERVICE / BOOKING / SUBSCRIPTION: seller attestation (any bytes). */
mk_status_t mk_order_fulfil(mk_market_t *m, uint16_t order, const mk_id_t *actor,
                            const uint8_t *proof, uint32_t proof_len);
/* Buyer confirms receipt: escrow releases now. */
mk_status_t mk_order_confirm(mk_market_t *m, uint16_t order, const mk_id_t *actor);
/* Seller refunds k units of a line (before or after release; exact, M4). */
mk_status_t mk_order_refund_line(mk_market_t *m, uint16_t order, const mk_id_t *actor, uint8_t line,
                                 uint32_t qty);

/* Disputes. Either party opens while escrow is held. */
mk_status_t mk_dispute_open(mk_market_t *m, uint16_t order, const mk_id_t *actor);
mk_status_t mk_dispute_evidence(mk_market_t *m, uint16_t order, const mk_id_t *actor,
                                const char *cid, const uint8_t digest[32]);
/* Digest a mediator signs: order digest, dispute open time, buyer award. */
void mk_ruling_digest(const mk_market_t *m, uint16_t order, uint64_t buyer_award, uint8_t out[32]);
/* Chosen arbiter: one signed ruling. Community: each juror votes; the
 * ruling is the median vote once all have voted or at the deadline with a
 * majority. */
mk_status_t mk_dispute_rule(mk_market_t *m, uint16_t order, const mk_id_t *mediator,
                            uint64_t buyer_award, const uint8_t *sig, uint32_t sig_len);

/* Returns (physical; within the listing's return window). */
mk_status_t mk_return_request(mk_market_t *m, uint16_t order, const mk_id_t *actor, uint8_t line,
                              uint32_t qty);
mk_status_t mk_return_shipped(mk_market_t *m, uint16_t order, const mk_id_t *actor);
mk_status_t mk_return_received(mk_market_t *m, uint16_t order, const mk_id_t *actor, bool restock);
mk_status_t mk_return_decline(mk_market_t *m, uint16_t order, const mk_id_t *actor);

/* Conservation check (M2, M3) for one order, and for the whole market. */
bool mk_order_conserved(const mk_market_t *m, uint16_t order);
bool mk_market_conserved(const mk_market_t *m);

/* ===== Reviews and discovery ===== */
mk_status_t mk_review(mk_market_t *m, uint16_t order, const mk_id_t *actor, uint8_t rating,
                      const char *cid);
/* Store quality in Q16 [0, 65536] with a neutral prior of 1/2 worth two
 * reviews. */
uint32_t mk_store_quality(const mk_market_t *m, uint16_t store);

typedef struct {
    const mk_id_t *buyer;
    uint32_t buyer_concord;
    int32_t category;      /* -1 any */
    int32_t kind;          /* -1 any */
    int32_t ccy;           /* -1 any */
    uint8_t per_store_cap; /* 0 = policy default */
} mk_query_t;

typedef struct {
    uint16_t listing;
    uint32_t score_q16;
    uint32_t isf_q16;
    uint32_t quality_q16;
} mk_hit_t;

uint32_t mk_discover(const mk_market_t *m, const mk_query_t *q, mk_hit_t *out, uint32_t max);

/* ===== Subscriptions ===== */
/* After a subscription order is paid: record it. auto_renew is false. */
int32_t mk_sub_start(mk_market_t *m, uint16_t order);
/* Buyer opts in to renewal at the current price (locked) for at most
 * max_renewals periods. */
mk_status_t mk_sub_opt_in(mk_market_t *m, uint16_t sub, const mk_id_t *actor,
                          uint32_t max_renewals);
/* One call, any time, no fee; access runs to period_end. */
mk_status_t mk_sub_cancel(mk_market_t *m, uint16_t sub, const mk_id_t *actor);

/* ===== Agents ===== */
int32_t mk_mandate_grant(mk_market_t *m, const mk_id_t *user, const mk_id_t *agent, uint8_t ccy,
                         uint64_t max_per_order, uint64_t max_total, uint64_t categories,
                         uint64_t expires);
mk_status_t mk_mandate_revoke(mk_market_t *m, uint16_t mandate, const mk_id_t *user);
/* The message the user signs to confirm an agent-built order. */
void mk_confirm_message(const mk_market_t *m, uint16_t order, uint8_t out[32]);
mk_status_t mk_order_user_confirm(mk_market_t *m, uint16_t order, const uint8_t *token,
                                  uint32_t token_len);

/* ===== B2B ===== */
void mk_po_digest(const mk_po_spec_t *po, uint8_t out[32]);
int32_t mk_po_draft(mk_market_t *m, const mk_po_spec_t *spec);
/* Buyer signs the PO digest. */
mk_status_t mk_po_submit(mk_market_t *m, uint16_t po, const uint8_t *sig, uint32_t sig_len);
mk_status_t mk_po_accept(mk_market_t *m, uint16_t po, const mk_id_t *actor);
mk_status_t mk_po_reject(mk_market_t *m, uint16_t po, const mk_id_t *actor);
mk_status_t mk_po_cancel(mk_market_t *m, uint16_t po, const mk_id_t *actor);
/* Seller ships and invoices: tax, flat fees from the PO, due date. */
int32_t mk_invoice_issue(mk_market_t *m, uint16_t po, const mk_id_t *actor, const char *number);
uint64_t mk_invoice_amount_due(const mk_market_t *m, uint16_t inv);
/* Fiat invoices: build pain.001 through the hook. VFV invoices are refused
 * (they settle internally with mk_invoice_settle). */
int32_t mk_invoice_pain001(mk_market_t *m, uint16_t inv, char *out, uint32_t cap);
/* Payment arrived (camt.054 or an internal VFV transfer): settle with the
 * assurance fee once. `internal` true moves buyer money through settle; false
 * means the bank already paid the seller, who remits the fee. */
mk_status_t mk_invoice_settle(mk_market_t *m, uint16_t inv, uint64_t amount, bool internal);
mk_status_t mk_invoice_void(mk_market_t *m, uint16_t inv, const mk_id_t *actor);

#endif /* ZXV_MARKET_H */
