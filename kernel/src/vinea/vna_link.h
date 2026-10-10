/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* vna_link.h — the trade loop between two peers' books (vna_econ.h), over
 * receipts carried by the node (vna_node_send_rcpt / vna_rcpt_fn).
 *
 * A trade is one receipt, signed by the seller and then the buyer. Both
 * books must apply the same receipts in the same order (the per-pair chain:
 * pair_seq and prev), so the protocol makes sure neither side applies a
 * receipt the other will refuse:
 *
 *   T1 OFFER    the seller checks the sale (vna_book_check_sale: agreement,
 *               the buyer's tokens for PAY), signs and sends the receipt.
 *   T2 ACCEPT   the buyer accepts only what it asked for (a WANT: resource,
 *               form, units left, a price ceiling) and only at the next link
 *               of the pair chain; it countersigns and sends it back WITHOUT
 *               applying it.
 *   T3 COMMIT   the seller applies the fully signed receipt (the book checks
 *               both signatures and the agreement again) and only then sends
 *               it back as the confirmation.
 *   T4 CONFIRM  the buyer applies the fully signed receipt.
 *
 *   L1 ONE AT A TIME. At most one receipt is outstanding per peer, in either
 *      direction (the pair chain is shared by both directions).
 *   L2 IDEMPOTENT. Messages may be lost or repeated. The side waiting
 *      resends its last message on vna_link_tick; a seller that receives a
 *      receipt it already applied (the hash matches the chain head) sends the
 *      confirmation again; a buyer that receives one applies it once.
 *   L3 FAIL CLOSED. Anything that does not fit (wrong sender, not asked for,
 *      over the ceiling, off the chain, refused by the book) is refused and
 *      counted; nothing is applied half-way.
 *   L4 THE LOOP. vna_link_cycle closes the node's economic cycle: verified
 *      remote compute received this cycle (countersigned receipts) becomes
 *      extra internal rate for the next swarm budget cycle through the owner
 *      gate (vna_gate_import), then the book settles and its DISTRIBUTE
 *      receipts are queued for the peers.
 *
 * HONEST LIMITS. A receipt records that a resource was delivered; this module
 * does not run the work itself (serving compute for a peer comes with the
 * model runtime). A seller can stop answering after the buyer countersigned:
 * the buyer then keeps its countersigned copy and resends it, and nothing is
 * applied on either side until the seller commits. Single-threaded, no
 * allocation.
 */
#ifndef VNA_LINK_H
#define VNA_LINK_H

#include "vna_econ.h"

#define VNA_LINK_WANTS 8u
#define VNA_LINK_PEND  16u
#define VNA_LINK_OUT   8u

typedef enum { VNA_LINK_SELL = 1, VNA_LINK_BUY = 2 } vna_link_role_t;

typedef struct {
    bool used;
    uint8_t resource, form;
    uint64_t units_left; /* still wanted (reserved while a receipt is outstanding) */
    uint64_t max_price;  /* the most this node pays for one receipt */
} vna_link_want_t;

typedef struct {
    bool used;
    uint8_t role; /* vna_link_role_t */
    int32_t want; /* BUY: the want it reserves from */
    vna_id_t peer;
    uint64_t pair_seq, units;
    uint32_t tries;
    uint64_t next_ms;
    uint32_t len;
    uint8_t bytes[VNA_RCPT_MAX]; /* SELL: seller-signed; BUY: countersigned */
} vna_link_pend_t;

typedef struct {
    vna_id_t dst;
    uint32_t len;
    uint8_t bytes[VNA_RCPT_MAX];
} vna_link_msg_t;

typedef struct {
    vna_book_t *book;
    const vna_agreement_t *agr;
    vna_usage_t *us;
    vna_drbg_t rng;
    uint64_t retry_ms;
    uint32_t max_tries;
    vna_link_want_t want[VNA_LINK_WANTS];
    vna_link_pend_t pend[VNA_LINK_PEND];
    vna_link_msg_t out[VNA_LINK_OUT];
    uint32_t out_head, out_n;
    vna_receipt_t scratch;
    uint8_t buf[VNA_RCPT_MAX];
    uint8_t dist[16][VNA_RCPT_MAX]; /* settle's DISTRIBUTE receipts */
    uint32_t dist_len[16];
    /* statistics */
    uint32_t offered, accepted, committed, confirmed, refused, resent, abandoned, out_dropped;
    uint64_t imported; /* internal rate added through the gate, all cycles */
} vna_link_t;

/* `seed` (32 bytes) feeds the signature randomness. agr/us: this node's
 * sharing agreement and usage table (checked when it sells). */
void vna_link_init(vna_link_t *l, vna_book_t *book, const vna_agreement_t *agr, vna_usage_t *us,
                   const uint8_t seed[32], uint64_t retry_ms, uint32_t max_tries);

/* T2: what this node is willing to buy. Returns the want slot or -1. */
int32_t vna_link_want(vna_link_t *l, uint8_t resource, uint8_t form, uint64_t units,
                      uint64_t max_price);

/* T1: offer `units` of `resource` to `buyer` for `price` (PAY: in tokens this
 * node issued, which the buyer holds; CREDIT: on credit). */
vna_status_t vna_link_sell(vna_link_t *l, const vna_id_t *buyer, uint8_t mode, uint8_t form,
                           uint8_t resource, uint64_t units, uint64_t price, const char *hk,
                           uint64_t now);

/* T2-T4 and L2: the node's receipt handler (a vna_rcpt_fn; ctx is the link). */
vna_status_t vna_link_on_rcpt(void *ctx, const vna_id_t *from, const uint8_t *rcpt, uint32_t len,
                              uint64_t now);

/* L2: resend what is still waiting; give up after max_tries (a SELL or a BUY
 * that was never committed: nothing was applied, the want is restored). */
void vna_link_tick(vna_link_t *l, uint64_t now);

/* L4: import up to `import` units of this cycle's verified remote compute
 * into the next budget cycle (sb, base_rate) through the gate, then settle
 * the book and queue its DISTRIBUTE receipts. *imported_out may be NULL. */
vna_status_t vna_link_cycle(vna_link_t *l, vna_gate_t *g, const uint8_t owner_secret[32],
                            swarm_budget_t *sb, uint64_t base_rate, uint64_t now,
                            uint64_t *imported_out);

/* The next receipt to send (dst, bytes, len), or false when none is queued.
 * The bytes stay valid until the next call that changes the link. */
bool vna_link_next_out(vna_link_t *l, vna_id_t *dst, const uint8_t **bytes, uint32_t *len);

/* Receipts still outstanding with `peer` (NULL: with anyone). */
uint32_t vna_link_pending(const vna_link_t *l, const vna_id_t *peer);

#endif /* VNA_LINK_H */
