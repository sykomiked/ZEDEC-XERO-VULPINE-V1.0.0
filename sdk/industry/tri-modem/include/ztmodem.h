/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* ztmodem.h — tri-modem: a neighbourhood mesh fallback planner for home
 * routers.
 *
 * When a home's WAN link is down, the planner chooses neighbour routers to
 * relay through, from the adverts those neighbours sign (Ed25519 or
 * ML-DSA-87), and shares each neighbour's bandwidth fairly and exactly:
 *
 *   adverts      a neighbour's signed, sequenced, time-limited statement of
 *                its own WAN, the bandwidth it offers (an exact rational in
 *                kbit/s), its latency and which destinations it can reach.
 *                A bad signature is refused and logged; a replayed or older
 *                advert, and one that has expired or comes from the future,
 *                is refused.
 *   evidence     per neighbour and destination, the advert's claim and the
 *                local probes are joined four-valued. A contradiction (one
 *                advert claiming a destination both ways, or a fresh probe
 *                that contradicts the advert) is held as BOTH: logged, and
 *                that neighbour is not used for that destination until the
 *                household's owner signs a decision.
 *   agreements   a neighbour is used only after the owner records an
 *                agreement with it, which also caps what this home may draw.
 *   the plan     each flow goes to one eligible neighbour (the one that
 *                would give it the largest fair share); each neighbour's
 *                budget, min(offer, agreement), is split max-min fairly
 *                among its flows in exact rationals and charged to a libzxv
 *                rmag bandwidth budget, which refuses any overdraw.
 *
 * A library plus simulated topologies. It configures no router, no radio
 * and no route; the integrator applies (or ignores) a plan. Amateur-radio
 * relays are never selected (see README "Limits"). Integer and rational code
 * only (R11). Kit API 0.1 (preview). */
#ifndef ZTMODEM_H
#define ZTMODEM_H

#include "zind_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ZTM_VERSION_MAJOR 0
#define ZTM_VERSION_MINOR 1

/* Status codes (-3900..-3999). */
#define ZTM_E_NEIGHBOUR (-3901) /* unknown neighbour                                    */
#define ZTM_E_EXPIRED   (-3902) /* expired, or issued too far in the future            */
#define ZTM_E_AGREEMENT (-3903) /* no owner agreement with this neighbour               */
#define ZTM_E_MEDIUM    (-3904) /* an amateur-radio relay: never selected               */
#define ZTM_E_NO_RELAY  (-3905) /* the WAN is down and no neighbour is eligible         */
#define ZTM_E_WAN_UP    (-3906) /* the local WAN is up: no fallback plan is needed      */
#define ZTM_E_OWNER     (-3907) /* the owner's signature does not verify                */

const char *ztm_status_name(int32_t status);

#define ZTM_ALG_ED25519 1
#define ZTM_ALG_MLDSA87 2

#define ZTM_MEDIUM_ETHERNET  1
#define ZTM_MEDIUM_WIFI      2
#define ZTM_MEDIUM_POWERLINE 3
#define ZTM_MEDIUM_FIBRE     4
#define ZTM_MEDIUM_AMATEUR   9 /* amateur radio: refused (FCC Part 97 and equivalents) */

#define ZTM_ID_MAX        16u  /* neighbour and flow ids: [A-Za-z0-9._:/-] */
#define ZTM_DEST_MAX      32u  /* destination names, same alphabet          */
#define ZTM_MAX_DESTS     8u   /* per advert and per neighbour              */
#define ZTM_MAX_NEIGHBOURS 32u
#define ZTM_MAX_FLOWS     64u
#define ZTM_SKEW_S        120u /* how far an advert may be dated ahead      */
#define ZTM_LIFETIME_MAX  86400u
#define ZTM_PROBE_TTL_S   300u /* a probe older than this is no evidence    */
#define ZTM_ADVERT_CTX    "tri-modem/advert"
#define ZTM_ADVERT_MAX    (512u + ZXV_MLDSA87_SIG_BYTES)

typedef struct {
    char name[ZTM_DEST_MAX + 1];
    int32_t lp; /* ZXV_LP_TRUE (reachable) or ZXV_LP_FALSE (not) */
} ztm_claim;

typedef struct {
    char neighbour[ZTM_ID_MAX + 1];
    uint64_t seq;     /* strictly increasing per neighbour      */
    uint64_t issued;  /* unix seconds                           */
    uint64_t expires; /* issued < expires <= issued + 86400     */
    uint8_t wan_up;   /* the neighbour's own WAN: 1 up, 0 down   */
    uint8_t medium;   /* ZTM_MEDIUM_*                           */
    zind_qty offer_kbps;
    uint32_t latency_ms;
    uint8_t n_claims;
    ztm_claim claims[ZTM_MAX_DESTS];
} ztm_advert;

/* Canonical signed bytes: "ZTMA" u8 1 | u8 alg | u16 body_len | body |
 * u16 sig_len | sig, all little-endian (body: zind_wr encoding of the fields).
 * The signature covers "ztm/advert/v1" || body (ML-DSA-87 under the context
 * ZTM_ADVERT_CTX). These are what a neighbour router publishes. */
int32_t ztm_advert_seal_ed25519(const zxv_cap *cap, const ztm_advert *a, const uint8_t sk[32],
                                uint8_t *out, size_t cap_bytes, size_t *out_len);
int32_t ztm_advert_seal_mldsa(const zxv_cap *cap, const ztm_advert *a, const zind_signer *s,
                              uint8_t *out, size_t cap_bytes, size_t *out_len);
/* Parse without verifying (for display); ZXV_E_FORMAT if malformed. */
int32_t ztm_advert_parse(const uint8_t *buf, size_t len, ztm_advert *out, int32_t *out_alg);

typedef struct ztm_planner ztm_planner;

/* self_id: this home router. owner_pk: the household owner's Ed25519 key,
 * which signs hold resolutions. The cap needs CRYPTO_SIGN and
 * BUDGET (and ATTEST when qlog, which may be NULL, is given). */
int32_t ztm_planner_create(const zxv_cap *cap, const char *self_id, const uint8_t owner_pk[32],
                           zxv_hexlog *qlog, ztm_planner **out);
void ztm_planner_destroy(ztm_planner *p);
/* pk: 32 bytes (Ed25519) or ZIND_PK_BYTES (ML-DSA-87). */
int32_t ztm_neighbour_add(ztm_planner *p, const char *id, int32_t alg, const uint8_t *pk,
                          size_t pk_len);
/* The owner's agreement with a neighbour: the most this home may draw, in
 * kbit/s (> 0). Zero withdraws the agreement. */
int32_t ztm_agreement_set(ztm_planner *p, const char *id, zind_qty cap_kbps);

/* Verify and take a signed advert, at local time `now`. Returns ZXV_OK,
 * ZIND_E_HELD (taken, but a destination is contradictory: held as BOTH and
 * logged), ZIND_E_REPLAY (seq not newer: refused), ZTM_E_EXPIRED,
 * ZTM_E_MEDIUM (an amateur-radio relay: refused), ZXV_E_VERIFY (refused and
 * logged), ZTM_E_NEIGHBOUR or ZXV_E_FORMAT. */
int32_t ztm_advert_accept(ztm_planner *p, const uint8_t *buf, size_t len, uint64_t now);
/* A local probe through `neighbour` to `dest`: ZXV_LP_TRUE or ZXV_LP_FALSE.
 * ZIND_E_HELD when it contradicts the advert (held as BOTH, logged). */
int32_t ztm_probe_record(ztm_planner *p, const char *neighbour, const char *dest, int32_t lp,
                         uint64_t now);

/* The evidence for one neighbour and destination at `now`: the joined
 * ZXV_LP_* value, whether it is held, and that neighbour's budget
 * min(offer, agreement) (zero when it has none). */
typedef struct {
    int32_t lp;
    int32_t held;
    int32_t eligible;  /* would be used for dest in a plan at `now` */
    int32_t why;       /* ZXV_OK, or the reason it is not eligible   */
    zind_qty budget_kbps;
    uint64_t advert_seq;
} ztm_view;
int32_t ztm_neighbour_view(const ztm_planner *p, const char *id, const char *dest, uint64_t now,
                           ztm_view *out);

/* Resolving a hold: the owner signs (Ed25519) the canonical message, which
 * binds this router, the neighbour, the destination, the advert sequence,
 * the probe's time and value, and the decision. USE treats the destination
 * as reachable through that neighbour, AVOID as unreachable, until the
 * neighbour's next advert. */
#define ZTM_USE   1
#define ZTM_AVOID 2
int32_t ztm_owner_message(const ztm_planner *p, const char *neighbour, const char *dest,
                          int32_t decision, uint8_t *buf, size_t cap, size_t *out_len);
int32_t ztm_hold_resolve(ztm_planner *p, const char *neighbour, const char *dest,
                         int32_t decision, const uint8_t sig[64]);

typedef struct {
    char flow[ZTM_ID_MAX + 1];
    zind_qty demand_kbps; /* > 0 */
} ztm_demand;

typedef struct {
    char flow[ZTM_ID_MAX + 1];
    char relay[ZTM_ID_MAX + 1];
    zind_qty alloc_kbps;
} ztm_assign;

typedef struct {
    uint32_t n;              /* flows planned (all of them on ZXV_OK)     */
    ztm_assign a[ZTM_MAX_FLOWS];
    uint32_t eligible;       /* neighbours used or usable                  */
    uint32_t satisfied;      /* flows that got their whole demand          */
    uint8_t tag;             /* TRUE all satisfied, TRUE/entropic when some
                                flows are limited, FALSE when no relay      */
} ztm_plan;

/* Plan `dest` at `now`. local_wan_up != 0 gives ZTM_E_WAN_UP and an empty
 * plan. ZTM_E_NO_RELAY when nothing is eligible. Every allocation is
 * charged to the neighbour's rmag bandwidth budget (the previous plan's
 * charges are released first). */
int32_t ztm_plan_make(ztm_planner *p, const char *dest, uint64_t now, int32_t local_wan_up,
                      const ztm_demand *demands, uint32_t n, ztm_plan *out);
/* What remains of a neighbour's budget after the last plan (exact). */
int32_t ztm_budget_remaining(const ztm_planner *p, const char *id, zind_qty *out);

#ifdef __cplusplus
}
#endif

#endif /* ZTMODEM_H */
