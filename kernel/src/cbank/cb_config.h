/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* cb_config.h — currency and operator configuration for a central-bank style
 * deployment of the multilateral payment engine (cb_net.h).
 *
 * A cb_config holds, for one operator:
 *   - its role profile (central bank, commercial bank participant,
 *     self-banking operator, institution, individual) with permissions;
 *   - up to CB_MAX_CCY currency profiles: issuing authority, legal-tender
 *     flag, ISO 4217 minor units, rounding rule, settlement window, cut-off,
 *     weekend mask and holiday slots, prefunding/reserve rules and
 *     capital-flow-management switches;
 *   - per-corridor limits (currency pair, per payment and per cycle);
 *   - FX rate sources (Ed25519 public key, staleness limit) and the signed
 *     rate record format they publish;
 *   - a sanctions/AML screening hook (interface only: no list is shipped).
 *
 * Every amount is an exact integer count of ISO 4217 minor units. There is
 * no floating point and no interest anywhere: the only liquidity tools are
 * prefunding and an optional interest-free intraday net debit cap.
 *
 * A central-bank profile never defaults to the Vino Floating Voucher: the
 * currencies are whatever ISO 4217 codes the operator adds, and VFV (whose
 * rails 555/777/888 travel only in SplmtryData) stays off unless enabled.
 * "VFV" and the rail numerics are refused as currency profiles.
 *
 * HONEST LIMITS. Configuration is not authorisation: a profile saying a
 * currency is legal tender or that a source is trusted is the operator's
 * assertion, not something this code can verify. The screening hook is an
 * interface only; no sanctions list, PEP list or AML model is included, and
 * with screening required and no hook installed every payment is refused.
 * The reason and status codes used follow the ISO 20022 external code sets
 * as known when this was written; check them against the current release.
 * Nothing here implies PAPSS membership, RTGS connectivity, SWIFT/CIPS
 * connectivity, certification or regulatory approval.
 */
#ifndef ZXV_CB_CONFIG_H
#define ZXV_CB_CONFIG_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "cb_usury.h"

#define CB_MAX_CCY        16u
#define CB_MAX_HOLIDAYS   64u
#define CB_MAX_CORRIDORS  64u
#define CB_MAX_FX_SOURCES 8u
#define CB_MAX_FEES       8u
#define CB_NAME_LEN       71u /* Max70Text + NUL */
#define CB_BIC_LEN        12u /* 11 + NUL */

/* ===== Result codes ===== */
#define CB_OK               0
#define CB_E_NULL           (-1)
#define CB_E_FULL           (-2)
#define CB_E_BAD_CCY        (-3) /* not an ISO 4217 payable currency */
#define CB_E_VFV_AS_CCY     (-4) /* VFV or a rail numeric offered as currency */
#define CB_E_DUP            (-5)
#define CB_E_UNKNOWN_CCY    (-6) /* currency not configured */
#define CB_E_ROUNDING       (-7)
#define CB_E_CALENDAR       (-8)
#define CB_E_BIC            (-9)
#define CB_E_COUNTRY        (-10)
#define CB_E_ISSUER_COUNTRY (-11) /* legal tender not used in issuer country */
#define CB_E_RESERVE        (-12)
#define CB_E_NO_CCY         (-13)
#define CB_E_UNIT           (-14) /* settlement unit not configured / payable */
#define CB_E_NO_VERIFY      (-15) /* FX sources but no signature verifier */
#define CB_E_NO_SCREEN      (-16) /* screening required, no hook */
#define CB_E_ROLE           (-17)
#define CB_E_VFV_DEFAULT    (-18) /* central bank profile with VFV forced on */
#define CB_E_DATE           (-19)
#define CB_E_SOURCE         (-20)
#define CB_E_SIG            (-21)
#define CB_E_STALE          (-22)
#define CB_E_REPLAY         (-23)
#define CB_E_RATE           (-24)
#define CB_E_NAME           (-25)
#define CB_E_USURY          (-26) /* fee schedule fails cb_usury */

/* ===== Roles ===== */
typedef enum {
    CB_ROLE_CENTRAL_BANK = 1,    /* issuing authority / settlement operator */
    CB_ROLE_COMMERCIAL_BANK = 2, /* direct participant holding prefund */
    CB_ROLE_SELF_BANKING = 3,    /* the platform's own self-banking node */
    CB_ROLE_INSTITUTION = 4,     /* non-bank institution, via a sponsor */
    CB_ROLE_INDIVIDUAL = 5       /* a person, via a sponsor */
} cb_role;

#define CB_PERM_OPERATE_CYCLE 0x0001u /* open/close settlement cycles */
#define CB_PERM_ADMIT         0x0002u /* admit/suspend participants */
#define CB_PERM_SEND_CUSTOMER 0x0004u /* pacs.008 customer credit transfers */
#define CB_PERM_SEND_FI       0x0008u /* pacs.009 FI-to-FI transfers */
#define CB_PERM_HOLD_PREFUND  0x0010u /* direct participant with a position */
#define CB_PERM_RETURN        0x0020u /* pacs.004 returns */
#define CB_PERM_CANCEL        0x0040u /* camt.056 cancellation requests */
#define CB_PERM_VFV           0x0080u /* may carry VFV rail data (SplmtryData) */

typedef struct {
    uint8_t role;
    uint32_t perms;
    uint64_t per_payment_max; /* payer currency minor units, 0 = none; enforced by cb_net */
    bool screening_required;
    bool vfv_enabled; /* false by default for every role except self-banking */
    bool needs_sponsor;
} cb_role_profile;

/* Defaults for a role. Central bank: no VFV, screening required. */
int cb_role_default(cb_role role, cb_role_profile *out);

/* ===== Currency profile ===== */
typedef enum {
    CB_ROUND_EXACT = 1,         /* refuse any conversion with a remainder */
    CB_ROUND_FLOOR_REPORTED = 2 /* floor; the sub-minor-unit remainder is
                                   reported on the payment, never charged */
} cb_rounding;

#define CB_WEEKEND_SAT_SUN 0x60u /* bit 0 Monday .. bit 6 Sunday */
#define CB_WEEKEND_FRI_SAT 0x30u

typedef struct {
    uint16_t open_min;   /* minutes after local midnight */
    uint16_t cutoff_min; /* last minute payments are accepted */
    uint16_t close_min;  /* end of the settlement day */
    int16_t utc_offset_min;
    uint8_t weekend_mask;
    uint16_t n_holidays;
    uint32_t holidays[CB_MAX_HOLIDAYS]; /* local civil days since 1970-01-01 */
} cb_calendar;

/* Capital-flow-management switches. */
#define CB_CFM_OUTBOUND    0x01u /* payments out of this currency allowed */
#define CB_CFM_INBOUND     0x02u /* payments into this currency allowed */
#define CB_CFM_PURPOSE_REQ 0x04u /* ISO purpose code required */
#define CB_CFM_NONRESIDENT 0x08u /* participants outside issuer country may use it */
#define CB_CFM_HALT        0x10u /* emergency halt: nothing in or out */

typedef struct {
    char alpha[4];
    uint16_t num;
    uint8_t minor; /* always the ISO 4217 minor unit */
    char issuer_name[CB_NAME_LEN];
    char issuer_bic[CB_BIC_LEN];
    char issuer_country[3];
    bool legal_tender;
    uint8_t rounding;
    cb_calendar cal;
    bool prefund_required;
    uint64_t min_prefund;   /* participant must hold this to be active */
    uint64_t net_debit_cap; /* interest-free intraday debit beyond prefund */
    uint16_t reserve_bps;   /* share of prefund held back, 0..10000 */
    uint32_t cfm;
    uint64_t cfm_max_outbound; /* per payment, 0 = no cap */
} cb_ccy_profile;

typedef struct {
    char from[4];
    char to[4];
    bool enabled;
    uint64_t per_payment_max; /* 'from' minor units, 0 = no cap */
    uint64_t per_cycle_max;   /* 'from' minor units, 0 = no cap */
} cb_corridor;

/* ===== FX sources and signed rate records ===== */
typedef bool (*cb_sig_verify_fn)(const uint8_t *msg, size_t len, const uint8_t sig[64],
                                 const uint8_t pk[32]);

typedef struct {
    uint8_t id;
    char name[CB_NAME_LEN];
    uint8_t pubkey[32];
    uint32_t max_age_s; /* staleness limit */
    bool enabled;
} cb_fx_source;

/* "1 major unit of ccy = mant / 10^scale major units of unit". */
typedef struct {
    char ccy[4];
    char unit[4];
    uint64_t mant; /* 1 .. 10^12 */
    uint8_t scale; /* 0 .. 12 */
    uint64_t published_at;
    uint64_t valid_until;
    uint32_t seq; /* strictly increasing per (source, ccy) */
    uint8_t source_id;
    uint8_t sig[64]; /* Ed25519 over cb_rate_canon() */
} cb_rate_rec;

#define CB_RATE_CANON_LEN 48u
#define CB_RATE_MANT_MAX  1000000000000ull
#define CB_RATE_SCALE_MAX 12u
/* Canonical signed bytes: "ZXV-CBRATE-1" ccy unit mant(BE64) scale
 * published(BE64) valid_until(BE64) seq(BE32) source. */
void cb_rate_canon(const cb_rate_rec *r, uint8_t out[CB_RATE_CANON_LEN]);

/* ===== Screening hook (interface only) ===== */
typedef enum {
    CB_SCREEN_CLEAR = 0,
    CB_SCREEN_HIT = 1,
    CB_SCREEN_REVIEW = 2, /* treated as refusal: there is no hold queue */
    CB_SCREEN_ERROR = 3   /* fail closed */
} cb_screen_result;

typedef struct {
    const char *msg_id;
    const char *dbtr_agt_bic, *cdtr_agt_bic;
    const char *dbtr_ctry, *cdtr_ctry;
    const char *dbtr_name, *cdtr_name;
    const char *ccy;
    uint64_t amount;
    const char *purpose;
} cb_screen_req;

typedef int (*cb_screen_fn)(void *ctx, const cb_screen_req *req);

/* ===== Operator configuration ===== */
typedef struct {
    char operator_name[CB_NAME_LEN];
    char operator_bic[CB_BIC_LEN];
    char home_country[3];
    cb_role_profile rp;
    char settle_unit[4]; /* ISO 4217 code valuing cross-currency positions */
    bool corridor_default_deny;
    uint8_t n_ccy;
    cb_ccy_profile ccy[CB_MAX_CCY];
    uint8_t n_corr;
    cb_corridor corr[CB_MAX_CORRIDORS];
    uint8_t n_src;
    cb_fx_source src[CB_MAX_FX_SOURCES];
    cb_sig_verify_fn verify;
    cb_screen_fn screen;
    void *screen_ctx;
    /* Published fee schedule of the operator. The engine does not apply
     * fees; validation only guarantees the schedule is usury-free. */
    uint8_t n_fees;
    cb_fee fees[CB_MAX_FEES];
} cb_config;

/* Zero the config and apply the role defaults. No currency is added. */
int cb_config_init(cb_config *c, cb_role role, const char *operator_name, const char *operator_bic,
                   const char *home_country);
/* Add an ISO 4217 currency profile. Minor units come from ISO 4217, the
 * rounding rule defaults to CB_ROUND_EXACT, the calendar to 08:00-17:00 with
 * a 16:00 cut-off, UTC, Saturday/Sunday closed, no holidays; prefunding is
 * required with no debit cap, all CFM switches open except HALT. */
int cb_config_add_ccy(cb_config *c, const char *alpha, const char *issuer_name,
                      const char *issuer_bic, const char *issuer_country, bool legal_tender);
cb_ccy_profile *cb_config_ccy(cb_config *c, const char *alpha);
const cb_ccy_profile *cb_config_ccy_c(const cb_config *c, const char *alpha);
int cb_config_set_window(cb_config *c, const char *alpha, uint16_t open_min, uint16_t cutoff_min,
                         uint16_t close_min, int16_t utc_offset_min, uint8_t weekend_mask);
int cb_config_add_holiday(cb_config *c, const char *alpha, uint32_t y, uint32_t m, uint32_t d);
int cb_config_add_corridor(cb_config *c, const char *from, const char *to, uint64_t per_payment_max,
                           uint64_t per_cycle_max);
const cb_corridor *cb_config_corridor(const cb_config *c, const char *from, const char *to);
int cb_config_add_source(cb_config *c, uint8_t id, const char *name, const uint8_t pubkey[32],
                         uint32_t max_age_s);
/* Full consistency check; CB_OK or the first error found. */
int cb_config_validate(const cb_config *c);

/* Days since 1970-01-01 for a civil date (1970..9999), or 0xffffffff. */
uint32_t cb_days_from_civil(uint32_t y, uint32_t m, uint32_t d);

typedef enum {
    CB_BIZ_OPEN = 0,
    CB_BIZ_BEFORE_OPEN = 1,
    CB_BIZ_AFTER_CUTOFF = 2,
    CB_BIZ_CLOSED_DAY = 3, /* weekend */
    CB_BIZ_HOLIDAY = 4
} cb_biz;
cb_biz cb_business_status(const cb_ccy_profile *p, uint64_t now_unix);

/* Verify a rate record against the configured sources (signature, unit,
 * currency configured, mantissa/scale bounds, published <= now < valid). */
int cb_rate_check(const cb_config *c, const cb_rate_rec *r, uint64_t now_unix);
/* Fresh at time now: within the source's max age and before valid_until. */
bool cb_rate_fresh(const cb_config *c, const cb_rate_rec *r, uint64_t now_unix);

#endif /* ZXV_CB_CONFIG_H */
