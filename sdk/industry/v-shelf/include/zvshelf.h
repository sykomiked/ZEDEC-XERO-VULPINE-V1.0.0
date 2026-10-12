/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zvshelf.h — v-shelf: an exact inventory ledger for retail point of sale.
 *
 * Stock is kept per (SKU, location) as an exact rational: whole units for
 * counted items ("each") and exact fractions for measured ones (3/4 kg,
 * 0.125 l). Every movement — receive, sell, transfer, shrink — is an
 * ML-DSA-87 signed, SHA3-256 chained record with a unique reference:
 *
 *   - stock never goes negative (ZIND_E_NEGATIVE, nothing changes);
 *   - a reference is used once (a re-sent POS sale is ZIND_E_REPLAY);
 *   - arithmetic beyond the exact range is ZXV_E_RANGE, never rounded;
 *   - a cycle count equal to the book confirms it (TRUE); a count that
 *     differs is a variance: logged to the quarantine log, the position
 *     held as BOTH (no movement in or out) until a registered approver
 *     signs either "accept the count" (an exact adjustment is posted) or
 *     "keep the book" (a recount is ordered).
 *
 * zvs_reconcile replays the whole signed journal and checks that every
 * balance and every hold equals the live state exactly.
 *
 * The kit records what it is told; it cannot see shelves. A count is only
 * as good as the person counting. Integer code only (R11). Kit API 0.1
 * (preview). */
#ifndef ZVSHELF_H
#define ZVSHELF_H

#include "zind_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ZVS_VERSION_MAJOR 0
#define ZVS_VERSION_MINOR 1

/* Status codes (-3300..-3399). */
#define ZVS_E_UNIT     (-3301) /* a fractional quantity for an item counted in whole units */
#define ZVS_E_SKU      (-3302) /* unknown SKU                                              */
#define ZVS_E_LOCATION (-3303) /* unknown location                                         */
#define ZVS_E_VARIANCE (-3304) /* the count differs from the book: logged and held         */
#define ZVS_E_APPROVER (-3305) /* the key is not a registered approver                     */

const char *zvs_status_name(int32_t status);

#define ZVS_UNIT_EACH    1 /* whole units only */
#define ZVS_UNIT_MEASURE 2 /* exact rationals  */

#define ZVS_SHRINK_THEFT  1
#define ZVS_SHRINK_DAMAGE 2
#define ZVS_SHRINK_EXPIRY 3
#define ZVS_SHRINK_OTHER  4

/* Variance decisions. */
#define ZVS_ACCEPT_COUNT 1 /* post an adjustment: the book becomes the count */
#define ZVS_KEEP_BOOK    2 /* no adjustment: release the hold for a recount  */

/* Record kinds (chain domain "v-shelf"). */
#define ZVS_REC_LOCATION 1
#define ZVS_REC_ITEM     2
#define ZVS_REC_RECEIVE  3
#define ZVS_REC_SELL     4
#define ZVS_REC_TRANSFER 5
#define ZVS_REC_SHRINK   6
#define ZVS_REC_COUNT_OK 7
#define ZVS_REC_VARIANCE 8
#define ZVS_REC_ADJUST   9
#define ZVS_REC_RECOUNT  10

#define ZVS_MAX_LOCATIONS 16u
#define ZVS_MAX_ITEMS     4096u
#define ZVS_ID_MAX        32u /* SKUs and references: [A-Za-z0-9._:/-] */
#define ZVS_LOC_MAX       16u
#define ZVS_MAX_REFS      (1u << 18) /* references (movements + counts) per store */
#define ZVS_VARIANCE_CTX  "v-shelf/variance"

typedef struct zvs_store zvs_store;

/* The cap needs CRYPTO_SIGN (and ATTEST with a quarantine log, which may be
 * NULL). Signer and log are borrowed and must outlive the store. */
int32_t zvs_store_create(const zxv_cap *cap, const zind_signer *signer, zxv_hexlog *qlog,
                         zvs_store **out);
void zvs_store_destroy(zvs_store *s);
int32_t zvs_location_add(zvs_store *s, const char *code, uint32_t *out_loc);
/* unit_name: 1..8 of [A-Za-z0-9._:/-], e.g. "ea", "kg", "l". */
int32_t zvs_item_add(zvs_store *s, const char *sku, int32_t unit_kind, const char *unit_name);
int32_t zvs_approver_add(zvs_store *s, const uint8_t pk[ZIND_PK_BYTES]);

/* Movements: qty > 0; each needs a reference never used before. */
int32_t zvs_receive(zvs_store *s, const char *ref, const char *sku, uint32_t loc, zind_qty qty);
int32_t zvs_sell(zvs_store *s, const char *ref, const char *sku, uint32_t loc, zind_qty qty);
int32_t zvs_transfer(zvs_store *s, const char *ref, const char *sku, uint32_t from, uint32_t to,
                     zind_qty qty);
int32_t zvs_shrink(zvs_store *s, const char *ref, const char *sku, uint32_t loc, zind_qty qty,
                   int32_t reason);

/* A cycle count (counted >= 0). Equal to the book: ZXV_OK, the position is
 * confirmed (TRUE). Different: ZVS_E_VARIANCE, *out_variance = counted -
 * book exactly, the variance is quarantined and the position held (BOTH). */
int32_t zvs_count(zvs_store *s, const char *ref, const char *sku, uint32_t loc, zind_qty counted,
                  zind_qty *out_variance);
/* The bytes an approver signs (context ZVS_VARIANCE_CTX) for a held
 * position: the SKU, the location, the hash of the variance record and the
 * decision. */
int32_t zvs_variance_message(const zvs_store *s, const char *sku, uint32_t loc, int32_t decision,
                             uint8_t *buf, size_t cap, size_t *out_len);
/* Apply a registered approver's signed decision (cap needs ATTEST). */
int32_t zvs_variance_resolve(zvs_store *s, const char *sku, uint32_t loc,
                             const uint8_t approver_pk[ZIND_PK_BYTES], int32_t decision,
                             const uint8_t sig[ZIND_SIG_BYTES]);

int32_t zvs_on_hand(const zvs_store *s, const char *sku, uint32_t loc, zind_qty *out);
/* Sum over every location. */
int32_t zvs_total(const zvs_store *s, const char *sku, zind_qty *out);
/* held: 1 while a variance waits for an approver. tag: the position's tag. */
int32_t zvs_position_state(const zvs_store *s, const char *sku, uint32_t loc, int32_t *out_held,
                           uint8_t *out_tag);
/* Lifetime totals for one SKU: received, sold, shrunk and the net of
 * accepted adjustments. received - sold - shrunk + adjusted == total. */
int32_t zvs_flows(const zvs_store *s, const char *sku, zind_qty *received, zind_qty *sold,
                  zind_qty *shrunk, zind_qty *adjusted);

/* Verify the signed journal with the store's key and replay it: every
 * balance and hold must equal the live state exactly, and no balance may go
 * negative along the way. ZXV_OK, ZIND_E_CHAIN (records broken) or
 * ZXV_E_INVARIANT (the replay disagrees; *out_bad = record index). */
int32_t zvs_reconcile(const zvs_store *s, uint32_t *out_bad);
const zind_chain *zvs_records(const zvs_store *s);

#ifdef __cplusplus
}
#endif

#endif /* ZVSHELF_H */
