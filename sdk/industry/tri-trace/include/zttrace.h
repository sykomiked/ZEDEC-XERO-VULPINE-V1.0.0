/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zttrace.h — tri-trace: chain of custody for serialized items (medicines,
 * luxury goods).
 *
 * Every item tag (a serial such as a GS1 SGTIN) has its own hash-chained
 * record of custody events, each signed with the ML-DSA-87 key of the
 * party that scanned it:
 *
 *   COMMISSION  the maker creates the tag; it holds the item   (Potential)
 *   SHIP        the holder sends it to a named party            (Emerging)
 *   RECEIVE     that party, and only that party, takes it
 *   DISPENSE    the holder hands it to the end user             (Manifest)
 *
 * A scan that does not fit the history — the tag commissioned twice, seen
 * at a party it was not shipped to, shipped by someone who does not hold
 * it, scanned again after it was dispensed, or a time earlier than the
 * last event — is a duplicate: the same tag in two places in conflicting
 * order. The scan is recorded as evidence and the item is frozen as BOTH
 * for review, never resolved automatically. A registered reviewer (a
 * person) signs either GENUINE (custody continues with a named holder) or
 * COUNTERFEIT (the tag becomes Forbidden for good). An identical scan sent
 * twice is a replay and is refused without a record.
 *
 * ztt_verify_export checks a whole exported chain offline: every link, every
 * signature, that each event is signed by the party it names, every review
 * signature, and the custody rules, giving TRUE (consistent), BOTH
 * (unresolved conflict) or FALSE (declared counterfeit).
 *
 * A scan proves that a key signed; it cannot prove which physical object
 * was in front of the scanner (see README "Limits"). Integer code only
 * (R11). Kit API 0.1 (preview). */
#ifndef ZTTRACE_H
#define ZTTRACE_H

#include "zind_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ZTT_VERSION_MAJOR 0
#define ZTT_VERSION_MINOR 1

/* Status codes (-3400..-3499). */
#define ZTT_E_PARTY    (-3401) /* unknown party, or the signer is not that party */
#define ZTT_E_ITEM     (-3402) /* unknown item tag, or a chain for another tag   */
#define ZTT_E_REVIEWER (-3403) /* the key is not a registered reviewer           */
#define ZTT_E_CONFLICT (-3404) /* the scan conflicts with the custody history:
                                  recorded as evidence, the item frozen as BOTH */

const char *ztt_status_name(int32_t status);

/* Event kinds (also the record kinds; chain domain "tri-trace"). */
#define ZTT_EV_COMMISSION 1
#define ZTT_EV_SHIP       2
#define ZTT_EV_RECEIVE    3
#define ZTT_EV_DISPENSE   4
#define ZTT_EV_REVIEW     5 /* appended by the registry operator */

/* Item states. */
#define ZTT_ST_HELD        1 /* at its holder                       */
#define ZTT_ST_TRANSIT     2 /* shipped, not yet received           */
#define ZTT_ST_DISPENSED   3 /* with the end user (terminal)        */
#define ZTT_ST_CONFLICT    4 /* BOTH: frozen for review             */
#define ZTT_ST_COUNTERFEIT 5 /* Forbidden: absorbing                */

/* Review outcomes. */
#define ZTT_GENUINE     1
#define ZTT_COUNTERFEIT 2

#define ZTT_TAG_MAX    64u /* item tags: [A-Za-z0-9._:/-] */
#define ZTT_PARTY_MAX  16u
#define ZTT_MAX_PARTIES 64u
#define ZTT_MAX_ITEMS  65536u
#define ZTT_REVIEW_CTX "tri-trace/review"

typedef struct ztt_registry ztt_registry;

/* The cap needs CRYPTO_SIGN (and ATTEST with a quarantine log, which may be
 * NULL). The operator signs review records. Borrowed. */
int32_t ztt_registry_create(const zxv_cap *cap, const zind_signer *operator_key, zxv_hexlog *qlog,
                            ztt_registry **out);
void ztt_registry_destroy(ztt_registry *r);
int32_t ztt_party_add(ztt_registry *r, const char *party_id, const uint8_t pk[ZIND_PK_BYTES]);
int32_t ztt_reviewer_add(ztt_registry *r, const uint8_t pk[ZIND_PK_BYTES]);

/* Record a scan by `scanner`, who must be the registered key of `party`.
 * to_party: the destination for SHIP, else NULL. time: unix seconds from the
 * scanning device. Returns ZXV_OK, ZTT_E_CONFLICT (recorded, frozen),
 * ZIND_E_REPLAY (identical scan, not recorded), ZIND_E_HELD (frozen for
 * review), ZIND_E_FROZEN (counterfeit) or a refusal. */
int32_t ztt_scan(ztt_registry *r, const char *item, const char *party,
                 const zind_signer *scanner, int32_t kind, const char *to_party, uint64_t time);

/* State, tag and current holder (holder_buf >= ZTT_PARTY_MAX + 1, may be
 * NULL; "" in transit). */
int32_t ztt_item_state(const ztt_registry *r, const char *item, int32_t *out_state,
                       uint8_t *out_tag, char *holder_buf, size_t holder_cap);

/* The bytes a reviewer signs (context ZTT_REVIEW_CTX): the tag, the hash of
 * the first conflicting record, the outcome and the holder custody resumes
 * with (a registered party; ignored for COUNTERFEIT, pass ""). */
int32_t ztt_review_message(const ztt_registry *r, const char *item, int32_t outcome,
                           const char *holder, uint8_t *buf, size_t cap, size_t *out_len);
/* Apply a registered reviewer's decision (cap needs ATTEST). The record
 * carries the reviewer's full signature, so it can be checked offline. */
int32_t ztt_review_resolve(ztt_registry *r, const char *item, const uint8_t reviewer_pk[ZIND_PK_BYTES],
                           int32_t outcome, const char *holder, const uint8_t sig[ZIND_SIG_BYTES]);

/* The item's chain, serialized (zind_chain_export). */
int32_t ztt_item_export(const ztt_registry *r, const char *item, uint8_t *buf, size_t cap,
                        size_t *out_len);

/* What an offline verifier trusts. */
typedef struct {
    const char *const *party_ids;
    const uint8_t *const *party_pks;
    size_t n_parties;
    const uint8_t *const *reviewer_pks;
    size_t n_reviewers;
    const uint8_t *operator_pk;
} ztt_trust;

typedef struct {
    int32_t lp;        /* TRUE consistent, BOTH unresolved conflict, FALSE counterfeit */
    int32_t state;     /* ZTT_ST_*                                                    */
    char holder[ZTT_PARTY_MAX + 1];
    uint32_t events;   /* records                                                     */
    uint32_t conflicts;
    uint32_t bad_index; /* the first bad record on failure, else UINT32_MAX            */
} ztt_verdict;

/* Verify an exported chain for `item` with nothing but the trust set.
 * ZXV_OK with a verdict, or ZXV_E_FORMAT, ZIND_E_CHAIN (broken link or
 * signature), ZIND_E_UNTRUSTED (a record signed by a key that is not the
 * party it names, or a review not signed by a trusted reviewer) or
 * ZTT_E_ITEM (a chain for another tag). Needs CRYPTO_SIGN. */
int32_t ztt_verify_export(const zxv_cap *cap, const char *item, const uint8_t *buf, size_t len,
                          const ztt_trust *trust, ztt_verdict *out);

#ifdef __cplusplus
}
#endif

#endif /* ZTTRACE_H */
