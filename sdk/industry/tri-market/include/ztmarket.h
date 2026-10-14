/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* ztmarket.h — tri-market: e-commerce escrow on the libzxv ledger.
 *
 *   ztm_order_create   the buyer's payment moves into escrow: Potential
 *   ztm_ship           the seller records a shipment: Emerging
 *   ztm_confirm        the buyer's signed delivery confirmation (Ed25519 or
 *                      ML-DSA-87, made on the buyer's own device) releases
 *                      escrow to the seller minus the assurance fee:
 *                      Manifest. The fee is zxv_fee_assure_carry on the
 *                      released amount (one carry per seller) and is split
 *                      into the four fee accounts by zxv_fee_split; this kit
 *                      computes neither.
 *   ztm_dispute        buyer or seller disagrees: BOTH, frozen
 *   ztm_rule           a registered arbiter's signed ruling splits the
 *                      escrow exactly (seller part + buyer part == price;
 *                      the seller part carries the fee, the buyer part none)
 *   ztm_expire         after the deadline an unconfirmed, undisputed order
 *                      is refunded to the buyer in full
 *
 * Money leaves escrow once: a second confirmation, a replayed one, a ruling
 * on an order that was not disputed, or a refund after release is refused.
 *
 * CROSS-CURRENCY. With a zxv_vhub attached (ztm_hub_attach), an order can be
 * listed in another hub currency: ztm_order_create_quoted asks the hub
 * (zxv_vhub_quote) for the exact amount of the desk currency that the list
 * price is worth through the V-Bill, refuses it if the pegs do not make it
 * exact, and pins the quote (list slot, list price, V-Bills, both peg
 * epochs) in the signed order record. The buyer pays, and the seller is
 * paid, in the desk currency; the kit converts nothing itself.
 *
 * Every step is an ML-DSA-87 signed, SHA3-256 chained record; forged
 * confirmations and rulings go to the quarantine log. The kit settles on
 * the libzxv ledger; it is not a payment processor, and no fiat enters or
 * leaves through it. Clocks are the caller's (see README "Limits").
 *
 * Integer code only (R11). Kit API 0.1 (preview). */
#ifndef ZTMARKET_H
#define ZTMARKET_H

#include "zind_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ZTM_VERSION_MAJOR 0
#define ZTM_VERSION_MINOR 1

/* Status codes (-4400..-4499). */
#define ZTM_E_SPLIT   (-4401) /* a ruling's parts do not add up to the escrow exactly */
#define ZTM_E_ARBITER (-4402) /* the key is not a registered arbiter                  */
#define ZTM_E_EXPIRED (-4403) /* the confirmation is dated after the order's deadline */
#define ZTM_E_EARLY   (-4404) /* a timeout refund asked for before the deadline       */
#define ZTM_E_NOHUB   (-4405) /* a cross-currency order with no hub attached          */

const char *ztm_status_name(int32_t status);

#define ZTM_ALG_ED25519 1
#define ZTM_ALG_MLDSA87 2

/* Order states. */
#define ZTM_ST_ESCROW   1 /* paid into escrow (Potential)                 */
#define ZTM_ST_SHIPPED  2 /* shipment recorded (Emerging)                 */
#define ZTM_ST_RELEASED 3 /* confirmed: seller paid, fee taken (Manifest) */
#define ZTM_ST_DISPUTED 4 /* BOTH: frozen for an arbiter                  */
#define ZTM_ST_RULED    5 /* an arbiter's ruling split the escrow         */
#define ZTM_ST_REFUNDED 6 /* timed out: the buyer refunded in full        */

/* Who disputes. */
#define ZTM_PARTY_BUYER  1
#define ZTM_PARTY_SELLER 2

/* Record kinds (chain domain "tri-market"). */
#define ZTM_REC_CREATE  1
#define ZTM_REC_SHIP    2
#define ZTM_REC_RELEASE 3
#define ZTM_REC_DISPUTE 4
#define ZTM_REC_RULING  5
#define ZTM_REC_REFUND  6
#define ZTM_REC_HUB     7

#define ZTM_ID_MAX       32u /* order ids and shipment refs: [A-Za-z0-9._:/-] */
#define ZTM_MAX_ORDERS   65536u
#define ZTM_MAX_WINDOW   (90ULL * 24 * 3600) /* the longest deadline after creation */
#define ZTM_CONFIRM_CTX  "tri-market/confirm" /* ML-DSA context strings */
#define ZTM_RULING_CTX   "tri-market/ruling"

typedef struct ztm_desk ztm_desk;

/* A desk in one ISO 4217 currency; opens its escrow account and the four
 * fee accounts (ZXV_FEE_* order). The cap needs LEDGER_POST, LEDGER_READ
 * and CRYPTO_SIGN (and ATTEST with a quarantine log, which may be NULL).
 * Signer and log are borrowed. */
int32_t ztm_desk_create(const zxv_cap *cap, const char *ccy, const zind_signer *desk_signer,
                        zxv_hexlog *qlog, ztm_desk **out);
void ztm_desk_destroy(ztm_desk *d);
uint32_t ztm_escrow_account(const ztm_desk *d);
/* The four fee accounts, indexed by ZXV_FEE_RESERVE_FLOOR .. ZXV_FEE_REGEN_CAPITAL. */
int32_t ztm_fee_accounts(const ztm_desk *d, uint32_t out[ZXV_FEE_BUCKETS]);
int32_t ztm_arbiter_add(ztm_desk *d, const uint8_t pk[ZIND_PK_BYTES]);

/* Attach a hub (borrowed; must outlive the desk, on the desk cap's
 * context) whose slot `pay_slot` stands for the desk currency. The kit
 * cannot read a slot's minor units through ABI 1.5: the operator states
 * them in `pay_minor_units`, which must equal the desk currency's. */
int32_t ztm_hub_attach(ztm_desk *d, zxv_vhub *hub, uint8_t pay_slot, uint8_t pay_minor_units);

/* Create an order and move price_minor (> 0) from buyer to escrow.
 * buyer_pk: 32 bytes (ED25519) or ZIND_PK_BYTES (MLDSA87). deadline: unix
 * seconds, after `now` and at most ZTM_MAX_WINDOW after it. An order id is
 * never reused on a desk. */
int32_t ztm_order_create(ztm_desk *d, const char *order_id, uint32_t buyer_account,
                         uint32_t seller_account, int64_t price_minor, int32_t buyer_alg,
                         const uint8_t *buyer_pk, size_t pk_len, uint64_t deadline, uint64_t now,
                         uint32_t *out_order);
/* The same, priced at `list_price` minor units of hub slot `list_slot`:
 * the escrowed amount is the hub's exact quote. ZTM_E_NOHUB without a hub,
 * ZIND_E_INEXACT if the pegs give no exact amount for the list price,
 * ZXV_E_PEG if either peg is unset. */
int32_t ztm_order_create_quoted(ztm_desk *d, const char *order_id, uint32_t buyer_account,
                                uint32_t seller_account, uint8_t list_slot, uint64_t list_price,
                                int32_t buyer_alg, const uint8_t *buyer_pk, size_t pk_len,
                                uint64_t deadline, uint64_t now, uint32_t *out_order);
/* The pinned quote of a quoted order (ZXV_E_STATE for a plain one). */
int32_t ztm_order_quote(const ztm_desk *d, uint32_t order, uint8_t *out_list_slot,
                        uint64_t *out_list_price, uint64_t *out_vbills, uint64_t *out_list_epoch,
                        uint64_t *out_pay_epoch);

/* Escrow -> shipped, with a carrier reference. */
int32_t ztm_ship(ztm_desk *d, uint32_t order, const char *shipment_ref, uint64_t time);

/* The bytes the buyer signs: "ztm/confirm/v1" || u8 len || order id ||
 * order hash[32] (the hash of its CREATE record, ztm_order_hash) || u64le
 * price || u64le time. ztm_confirm_bytes is pure, so a buyer app holding the
 * order hash builds the same bytes itself. */
int32_t ztm_order_hash(const ztm_desk *d, uint32_t order, uint8_t out[32]);
int32_t ztm_confirm_bytes(const char *order_id, const uint8_t order_hash[32], int64_t price,
                          uint64_t time, uint8_t *buf, size_t cap, size_t *out_len);
int32_t ztm_confirm_message(const ztm_desk *d, uint32_t order, uint64_t time, uint8_t *buf,
                            size_t cap, size_t *out_len);
/* Submit the buyer's confirmation; on success escrow goes to the seller
 * minus the fee, and the fee to the four fee accounts. */
int32_t ztm_confirm(ztm_desk *d, uint32_t order, uint64_t time, const uint8_t *sig,
                    size_t sig_len);

int32_t ztm_dispute(ztm_desk *d, uint32_t order, int32_t party, uint32_t reason);
/* The bytes an arbiter signs (context ZTM_RULING_CTX): "ztm/ruling/v1" ||
 * u8 len || order id || the dispute record's hash || u64le to_seller ||
 * u64le to_buyer. */
int32_t ztm_ruling_message(const ztm_desk *d, uint32_t order, int64_t to_seller, int64_t to_buyer,
                           uint8_t *buf, size_t cap, size_t *out_len);
/* Apply a registered arbiter's signed ruling (desk cap needs ATTEST). */
int32_t ztm_rule(ztm_desk *d, uint32_t order, const uint8_t arbiter_pk[ZIND_PK_BYTES],
                 int64_t to_seller, int64_t to_buyer, const uint8_t sig[ZIND_SIG_BYTES]);
/* Refund an unconfirmed, undisputed order whose deadline has passed. */
int32_t ztm_expire(ztm_desk *d, uint32_t order, uint64_t now);

int32_t ztm_order_state(const ztm_desk *d, uint32_t order, int32_t *out_state, uint8_t *out_tag);
/* What the order paid out: the fee, the seller's net and the buyer refund
 * (zeros while it is in escrow). Any out may be NULL. */
int32_t ztm_order_amounts(const ztm_desk *d, uint32_t order, int64_t *out_price,
                          int64_t *out_fee, int64_t *out_to_seller, int64_t *out_to_buyer);
int32_t ztm_order_find(const ztm_desk *d, const char *order_id, uint32_t *out_order);
/* A seller's stored fee carry (zxv_fee_assure_carry's remainder). */
int32_t ztm_fee_carry(const ztm_desk *d, uint32_t seller_account, uint64_t *out_rem);
/* Money currently held for orders in escrow (ESCROW, SHIPPED, DISPUTED). */
int32_t ztm_escrow_owed(const ztm_desk *d, int64_t *out_minor);
const zind_chain *ztm_records(const ztm_desk *d);

#ifdef __cplusplus
}
#endif

#endif /* ZTMARKET_H */
