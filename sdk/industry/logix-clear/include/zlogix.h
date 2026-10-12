/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zlogix.h — Logix-Clear (Logix-Transit): freight and customs release.
 *
 * A desk tracks containers through the 64-state lifecycle phase:
 *
 *   booked      -> Potential   (zlc_book; lines may be added)
 *   in transit  -> Emerging    (zlc_depart; lines are locked)
 *   released    -> Manifest    (zlc_release or a broker's decision)
 *   closed      -> Dissolving  (zlc_close)
 *
 * Release is decided by a small rule table per jurisdiction (HS-code
 * prefix and origin -> release / refuse / inspect, with an ad-valorem duty
 * rate) joined with rulings recorded by the operator. Each jurisdiction on
 * the route gets a four-valued ruling; if one releases while another
 * refuses (or one jurisdiction's own evidence conflicts) the shipment is
 * held as BOTH and frozen: it is never released automatically, only by a
 * registered human broker's signed decision. Duties are exact rationals,
 * converted to minor units under an explicit rounding rule (by default
 * none: an inexact duty is refused) and posted to the libzxv ledger with
 * the desk's capability. Every lifecycle step is an ML-DSA-87 signed,
 * SHA3-256 chained record.
 *
 * NOT CONNECTED TO ANY CUSTOMS AUTHORITY. Rules and rulings are whatever
 * the operator enters; the kit does not file declarations, does not know
 * any real tariff schedule and makes no customs or legal determination. It
 * recommends; licensed brokers and the authorities decide.
 *
 * Integer code only (R11). Kit API 0.1 (preview). */
#ifndef ZLOGIX_H
#define ZLOGIX_H

#include "zind_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ZLC_VERSION_MAJOR 0
#define ZLC_VERSION_MINOR 1

/* Status codes (-3100..-3199). */
#define ZLC_E_REFUSED   (-3101) /* a jurisdiction refused release              */
#define ZLC_E_PENDING   (-3102) /* not every jurisdiction has ruled: waiting   */
#define ZLC_E_CONTAINER (-3103) /* not a valid ISO 6346 container number       */
#define ZLC_E_BROKER    (-3104) /* the key is not a registered broker          */
#define ZLC_E_JURIS     (-3105) /* unknown or malformed jurisdiction code      */

const char *zlc_status_name(int32_t status);

/* Rule actions. */
#define ZLC_RULE_RELEASE 1 /* TRUE: may be released (duty applies)   */
#define ZLC_RULE_REFUSE  2 /* FALSE: may not enter                   */
#define ZLC_RULE_INSPECT 3 /* NEITHER: held for inspection (waiting) */

typedef struct {
    const char *jurisdiction; /* a registered code                          */
    const char *hs_prefix;    /* 0..10 digits; "" matches every line         */
    const char *origin;       /* ISO 3166 alpha-2 (two upper-case letters) or "*" */
    int32_t action;           /* ZLC_RULE_*                                  */
    zind_qty duty_rate;       /* ad valorem, 0 <= rate <= 10; RELEASE only    */
} zlc_rule;

/* Shipment states. */
#define ZLC_ST_BOOKED     1
#define ZLC_ST_IN_TRANSIT 2
#define ZLC_ST_HELD       3 /* BOTH: frozen for a human broker */
#define ZLC_ST_REFUSED    4
#define ZLC_ST_RELEASED   5
#define ZLC_ST_CLOSED     6

/* Record kinds in the desk's chain (domain "logix-clear"). */
#define ZLC_REC_BOOK    1
#define ZLC_REC_DEPART  2
#define ZLC_REC_RULING  3
#define ZLC_REC_HOLD    4
#define ZLC_REC_REFUSE  5
#define ZLC_REC_RELEASE 6
#define ZLC_REC_BROKER  7
#define ZLC_REC_CLOSE   8

#define ZLC_MAX_JURIS   32u
#define ZLC_MAX_RULES   512u
#define ZLC_MAX_ROUTE   8u
#define ZLC_MAX_LINES   32u
#define ZLC_MAX_RULINGS 16u
#define ZLC_MAX_SHIPMENTS 65536u

typedef struct zlc_desk zlc_desk;

/* ISO 6346: three-letter owner code, category U, J or Z, six digits and the
 * check digit. ZXV_OK or ZLC_E_CONTAINER. */
int32_t zlc_container_check(const char *id);
/* The check digit for the first ten characters (0..9), or -1. */
int32_t zlc_container_digit(const char *first10);

/* A desk in one ISO 4217 currency (enabled through the cap if needed). The
 * cap needs LEDGER_POST, LEDGER_READ and CRYPTO_SIGN; with a quarantine log
 * (may be NULL) it also needs ATTEST. The signer and the log are borrowed
 * and must outlive the desk. */
int32_t zlc_desk_create(const zxv_cap *cap, const char *ccy, const zind_signer *desk_signer,
                        zxv_hexlog *qlog, zlc_desk **out);
void zlc_desk_destroy(zlc_desk *d);
/* ZIND_ROUND_* for duties (default ZIND_ROUND_EXACT). */
int32_t zlc_set_rounding(zlc_desk *d, int32_t mode);

/* A jurisdiction: 2..8 of [A-Z0-9]. Opens its duty account "duty:<code>". */
int32_t zlc_jurisdiction_add(zlc_desk *d, const char *code, uint32_t *out_duty_account);
int32_t zlc_rule_add(zlc_desk *d, const zlc_rule *rule);
int32_t zlc_broker_add(zlc_desk *d, const uint8_t pk[ZIND_PK_BYTES]);

/* Book a container on a route of 1..8 registered jurisdictions (no repeats).
 * payer_account: a ledger account in the desk's currency that pays duties. */
int32_t zlc_book(zlc_desk *d, const char *container, const char *const *route, size_t n_route,
                 uint32_t payer_account, uint32_t *out_shipment);
/* A declared line while booked: HS code (6..10 digits), origin (ISO 3166
 * alpha-2), customs value (> 0, in the desk currency). */
int32_t zlc_line_add(zlc_desk *d, uint32_t shipment, const char *hs_code, const char *origin,
                     zind_qty customs_value);
/* Booked -> in transit (Potential -> Emerging). */
int32_t zlc_depart(zlc_desk *d, uint32_t shipment);
/* Record a ruling the operator received for one jurisdiction on the route
 * (ZXV_LP_TRUE release, ZXV_LP_FALSE refuse), in transit or held. */
int32_t zlc_ruling_record(zlc_desk *d, uint32_t shipment, const char *jurisdiction, int32_t lp);
/* The current four-valued decision without changing anything. per_juris
 * (may be NULL) receives one ZXV_LP_* per route entry. */
int32_t zlc_evaluate(const zlc_desk *d, uint32_t shipment, int32_t *out_lp, int32_t *per_juris,
                     size_t per_juris_cap);

typedef struct {
    int32_t lp;            /* the decision that released it (TRUE)  */
    int64_t total_minor;   /* duties posted, in minor units         */
    uint32_t duty_count;   /* jurisdictions with a non-zero duty    */
    uint64_t record_seq;   /* the signed release record             */
} zlc_release_info;

/* Decide and act:
 *   TRUE    duties posted, signed release record, Manifest; ZXV_OK
 *   BOTH    held and frozen, logged to the quarantine log; ZIND_E_HELD
 *   FALSE   refused (signed record); ZLC_E_REFUSED
 *   NEITHER nothing changes; ZLC_E_PENDING
 * Again on a released shipment: ZIND_E_REPLAY. On a held one: ZIND_E_HELD.
 * An inexact duty under ZIND_ROUND_EXACT is ZIND_E_INEXACT and a payer
 * short of funds ZXV_E_FUNDS; both leave the shipment unchanged. */
int32_t zlc_release(zlc_desk *d, uint32_t shipment, zlc_release_info *out);

/* The canonical bytes a broker signs (context "logix-clear/broker") to
 * resolve a held shipment: the shipment, its container, the hash of its
 * hold record and the decision (ZXV_LP_TRUE release / ZXV_LP_FALSE refuse). */
int32_t zlc_broker_message(const zlc_desk *d, uint32_t shipment, int32_t decision, uint8_t *buf,
                           size_t cap, size_t *out_len);
/* Apply a registered broker's signed decision to a held shipment. The
 * desk's cap needs ATTEST. TRUE releases (posting duties like
 * zlc_release), FALSE refuses. */
int32_t zlc_broker_resolve(zlc_desk *d, uint32_t shipment, const uint8_t broker_pk[ZIND_PK_BYTES],
                           int32_t decision, const uint8_t sig[ZIND_SIG_BYTES],
                           zlc_release_info *out);
/* Released -> closed (Manifest -> Dissolving). */
int32_t zlc_close(zlc_desk *d, uint32_t shipment);

int32_t zlc_shipment_state(const zlc_desk *d, uint32_t shipment, int32_t *out_state,
                           uint8_t *out_tag);
/* The exact duty for one jurisdiction (computed from the rule table), and
 * after release the minor units posted and the exact residual. */
int32_t zlc_duty(const zlc_desk *d, uint32_t shipment, const char *jurisdiction,
                 zind_qty *out_exact, int64_t *out_posted_minor, zind_qty *out_residual);
/* The desk's signed record chain (borrowed). */
const zind_chain *zlc_records(const zlc_desk *d);

#ifdef __cplusplus
}
#endif

#endif /* ZLOGIX_H */
