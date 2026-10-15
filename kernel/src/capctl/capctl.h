/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* capctl.h — capital-control profiles as signed plug-ins (roadmap R3.4).
 *
 * WHAT IT IS
 * A jurisdiction's capital-control rules, written as a signed manifest
 * (cp_common.h M1-M3, kind CP_KIND_CAPCTL, ML-DSA-87 through cpk.h), and an
 * evaluator that answers one question about a proposed cross-border
 * transfer: ALLOW, ALLOW_WITH_REPORT (allowed, and a report record is
 * produced) or DENY, always with a reason code.
 *
 * OFF BY DEFAULT. capctl_init leaves the feature disabled; while disabled
 * every transfer is ALLOW with reason CAPCTL_R_DISABLED and nothing is
 * checked or recorded. capctl_enable(c, true) turns it on (see
 * docs/retail/COMPLIANCE_PLUGINS.md).
 *
 * RULES
 *   C1 PROFILE. One profile per country (ISO 3166-1 alpha-2; region must be
 *      empty). Its body, little-endian, after the 68-byte header:
 *        ccy u16 (ISO 4217 numeric of every amount in the profile)
 *        flags u8 (CAPCTL_F_OUTBOUND, CAPCTL_F_INBOUND, CAPCTL_F_DEFAULT_ALLOW)
 *        per_tx_limit u64 (0 = none)
 *        n u8, n x {window_days u16 (1..366), limit u64}      rolling limits
 *        report_single u64 (0 = none)
 *        n u8, n x {window_days u16, threshold u64}           aggregate reports
 *        purpose_threshold u64 (0 = never required)
 *        n u8, n x code[4]          allowed purpose codes (0 = any code)
 *        n u8, n x {from[2], to[2], ccy u16 (0 = any), action u8}  corridors
 *      Country fields in corridors may be "**" (any). Decoding is exact: a
 *      count past its maximum, an unknown flag, a bad code or a trailing
 *      byte rejects the whole manifest.
 *   C2 INSTALL (fail closed). capctl_install accepts a profile only when it
 *      decodes, its signature verifies, `now` is inside [not_before,
 *      not_after], its version is strictly above the installed one for that
 *      country and not below that country's floor (capctl_require). A
 *      rejected manifest leaves the installed one untouched.
 *   C3 WHO CHECKS. A transfer from country A to country B (A != B) is
 *      checked by A's profile when it has CAPCTL_F_OUTBOUND (the person is
 *      the sender) and by B's profile when it has CAPCTL_F_INBOUND (the
 *      person is the receiver). A transfer with A == B is ALLOW (DOMESTIC):
 *      these are cross-border controls only.
 *   C4 MISSING PROFILES. For a side whose country is listed with
 *      capctl_require: no profile, a profile outside its validity window at
 *      `now`, or a profile below the floor is DENY (PROFILE_MISSING,
 *      PROFILE_EXPIRED, PROFILE_ROLLBACK). A bad signature or an older
 *      version never installs (C2), so it shows up as the previous state.
 *      For a country not listed: no profile is ALLOW (NO_PROFILE), but an
 *      installed profile that has expired is still DENY (PROFILE_EXPIRED):
 *      once controls exist, they never lapse silently.
 *   C5 ORDER. One side is checked in this order; the first failure is the
 *      reason: currency (a transfer not in the profile's ccy must carry
 *      base_amount, its value in that ccy, else FX_REQUIRED) -> corridor
 *      (any matching block rule = CORRIDOR_BLOCKED; else a matching allow
 *      rule passes; else DEFAULT_ALLOW decides, CORRIDOR_NOT_ALLOWED) ->
 *      per-transaction limit (amount > limit) -> purpose (amount >=
 *      purpose_threshold needs a code, PURPOSE_REQUIRED; when the profile
 *      lists codes it must be one of them, PURPOSE_NOT_ALLOWED) -> rolling
 *      limits (used in the window + amount > limit, PERIOD_LIMIT). The
 *      sending side is checked first; a DENY from either side wins.
 *   C6 ROLLING WINDOWS. Usage is kept per (country, direction, person) in
 *      day buckets (day = unix seconds / 86400, UTC). A window of W days at
 *      day D covers days D-W+1 .. D. Only committed transfers count
 *      (capctl_commit, called once the payment has actually posted). The
 *      caller supplies the tally store; when a person needs a tally and the
 *      store is full, the transfer is DENY (TALLY_FULL).
 *   C7 REPORTS, NOT BLOCKS. amount >= report_single, or used + amount >=
 *      an aggregate threshold, makes the answer ALLOW_WITH_REPORT and fills
 *      a capctl_report_t for that side. Reporting never blocks.
 *   C8 INTEGERS. Amounts are integer minor units (uint64). Every sum is
 *      checked; an overflow is DENY (OVERFLOW), never a wrap.
 *
 * HOW IT FITS THE JURISDICTION PLUG-IN MODEL (R3.3). A later adapter maps
 * ALLOW to PROCEED, ALLOW_WITH_REPORT to PROCEED plus the report, DENY to
 * false and a required-but-missing profile to fail closed, exactly as
 * vhub's limit rule (H5, H6) does today. Nothing here depends on that code.
 *
 * HONEST LIMITS. This evaluates the rules a signer wrote; whether those rules
 * match a country's law is the signer's job (a compliance officer per
 * country). It is "designed to follow" published capital-control rules, not
 * certified for any. No FX: the caller converts to the profile currency.
 * Report records are produced, not filed with any authority. Calendar months
 * and years are approximated by rolling windows of N days. Tested on the
 * host only; nothing here has run on real hardware or with a regulator.
 * Freestanding C11: no allocation, no float, no libc.
 */
#ifndef ZXV_CAPCTL_H
#define ZXV_CAPCTL_H

#include "cp_common.h"

#define CAPCTL_MAX_PERIOD   4u
#define CAPCTL_MAX_AGG      2u
#define CAPCTL_MAX_PURPOSE  16u
#define CAPCTL_MAX_CORR     16u
#define CAPCTL_MAX_PROFILES 16u
#define CAPCTL_MAX_REQ      32u
#define CAPCTL_DAYS         366u /* longest window, and the bucket ring */
#define CAPCTL_MANIFEST_MAX                                                                        \
    (CP_HDR_LEN + 2u + 1u + 8u + 1u + CAPCTL_MAX_PERIOD * 10u + 8u + 1u + CAPCTL_MAX_AGG * 10u +   \
     8u + 1u + CAPCTL_MAX_PURPOSE * 4u + 1u + CAPCTL_MAX_CORR * 7u)

#define CAPCTL_F_OUTBOUND      0x01u
#define CAPCTL_F_INBOUND       0x02u
#define CAPCTL_F_DEFAULT_ALLOW 0x04u
#define CAPCTL_F_ALL           0x07u

#define CAPCTL_CORR_ALLOW 1u
#define CAPCTL_CORR_BLOCK 2u

#define CAPCTL_DIR_OUT 0u
#define CAPCTL_DIR_IN  1u

typedef enum {
    CAPCTL_OK = 0,
    CAPCTL_E_ARG = -1,
    CAPCTL_E_DECODE = -2,  /* C1 */
    CAPCTL_E_SIG = -3,     /* C2: signature or signer scope */
    CAPCTL_E_WINDOW = -4,  /* C2: now outside [not_before, not_after] */
    CAPCTL_E_VERSION = -5, /* C2: not above the installed version */
    CAPCTL_E_FLOOR = -6,   /* C2: below the required floor */
    CAPCTL_E_FULL = -7     /* no room for another profile, floor or tally */
} capctl_status_t;

typedef enum { CAPCTL_ALLOW = 0, CAPCTL_ALLOW_WITH_REPORT = 1, CAPCTL_DENY = 2 } capctl_decision_t;

typedef enum {
    CAPCTL_R_OK = 0,
    CAPCTL_R_DISABLED,
    CAPCTL_R_DOMESTIC,
    CAPCTL_R_NO_PROFILE,
    CAPCTL_R_REPORT,
    CAPCTL_R_BAD_ARG,
    CAPCTL_R_PROFILE_MISSING,
    CAPCTL_R_PROFILE_EXPIRED,
    CAPCTL_R_PROFILE_ROLLBACK,
    CAPCTL_R_FX_REQUIRED,
    CAPCTL_R_CORRIDOR_BLOCKED,
    CAPCTL_R_CORRIDOR_NOT_ALLOWED,
    CAPCTL_R_PER_TX_LIMIT,
    CAPCTL_R_PURPOSE_REQUIRED,
    CAPCTL_R_PURPOSE_NOT_ALLOWED,
    CAPCTL_R_PERIOD_LIMIT,
    CAPCTL_R_TALLY_FULL,
    CAPCTL_R_OVERFLOW,
    CAPCTL_R_COUNT
} capctl_reason_t;

typedef struct {
    uint16_t window_days;
    uint64_t amount;
} capctl_window_t;

typedef struct {
    char from[2], to[2];
    uint16_t ccy;
    uint8_t action;
} capctl_corridor_t;

typedef struct {
    cp_hdr_t hdr; /* kind CP_KIND_CAPCTL, country = the jurisdiction */
    uint16_t ccy;
    uint8_t flags;
    uint64_t per_tx_limit;
    capctl_window_t period[CAPCTL_MAX_PERIOD];
    uint8_t n_period;
    uint64_t report_single;
    capctl_window_t agg[CAPCTL_MAX_AGG];
    uint8_t n_agg;
    uint64_t purpose_threshold;
    char purpose[CAPCTL_MAX_PURPOSE][4];
    uint8_t n_purpose;
    capctl_corridor_t corr[CAPCTL_MAX_CORR];
    uint8_t n_corr;
} capctl_profile_t;

typedef struct {
    char from[2], to[2];       /* ISO 3166-1 alpha-2 */
    uint16_t ccy;              /* ISO 4217 numeric of amount */
    uint64_t amount;           /* minor units of ccy, > 0 */
    uint64_t base_amount;      /* C5: value in a profile's ccy when ccy differs (0 = none) */
    uint64_t sender, receiver; /* the caller's pseudonymous person ids */
    char purpose[4];           /* e.g. ISO 20022 "SUPP"; all zero = none */
} capctl_transfer_t;

#define CAPCTL_REP_SINGLE    0x01u
#define CAPCTL_REP_AGGREGATE 0x02u

typedef struct {
    char country[2];
    uint8_t dir;   /* CAPCTL_DIR_* */
    uint8_t flags; /* CAPCTL_REP_* */
    uint32_t profile_version;
    uint64_t person;
    uint64_t base_amount; /* in profile_ccy */
    uint16_t profile_ccy;
    uint16_t ccy;
    uint64_t amount;
    char from[2], to[2];
    char purpose[4];
    uint64_t time;
} capctl_report_t;

typedef struct {
    capctl_decision_t decision;
    capctl_reason_t reason;
    char country[2]; /* the side that decided a DENY (zero otherwise) */
    uint32_t n_reports;
    capctl_report_t report[2];
} capctl_result_t;

typedef struct {
    bool used;
    char country[2];
    uint8_t dir;
    uint64_t person;
    uint32_t day[CAPCTL_DAYS];
    uint64_t amt[CAPCTL_DAYS];
} capctl_tally_t;

typedef struct {
    char country[2];
    uint32_t min_version;
} capctl_req_t;

typedef struct {
    bool enabled;
    cp_verify_fn verify;
    void *vctx;
    capctl_req_t req[CAPCTL_MAX_REQ];
    uint32_t n_req;
    capctl_profile_t prof[CAPCTL_MAX_PROFILES];
    uint32_t n_prof;
    capctl_tally_t *tally;
    uint32_t tally_cap;
} capctl_t;

/* Disabled until capctl_enable. tally: caller storage for tally_cap persons
 * (may be NULL / 0: then any profile with rolling rules denies TALLY_FULL). */
void capctl_init(capctl_t *c, cp_verify_fn verify, void *vctx, capctl_tally_t *tally,
                 uint32_t tally_cap);
void capctl_enable(capctl_t *c, bool on);
/* C4: country requires a profile, at least min_version (0 = any). Calling it
 * again for the same country raises or lowers the floor. */
capctl_status_t capctl_require(capctl_t *c, const char country[2], uint32_t min_version);

/* C1: encode a profile; returns the length, or 0 if it does not fit or is
 * invalid. The caller signs cp_manifest_msg() of the result. */
size_t capctl_encode(const capctl_profile_t *p, uint8_t *out, size_t cap);
capctl_status_t capctl_decode(const uint8_t *m, size_t len, capctl_profile_t *out);
/* C2. */
capctl_status_t capctl_install(capctl_t *c, const uint8_t *m, size_t len, const uint8_t *sig,
                               size_t sig_len, uint64_t now);
/* The installed profile for a country, or NULL. */
const capctl_profile_t *capctl_profile(const capctl_t *c, const char country[2]);

/* C3-C8. out is always filled. Nothing is recorded: see capctl_commit. */
capctl_decision_t capctl_evaluate(const capctl_t *c, const capctl_transfer_t *t, uint64_t now,
                                  capctl_result_t *out);
/* C6: record a transfer that has posted. CAPCTL_E_FULL if a needed tally
 * has no room (the caller should have been told TALLY_FULL already). */
capctl_status_t capctl_commit(capctl_t *c, const capctl_transfer_t *t, uint64_t now);

/* Usage of one person in one window (for display and tests). */
uint64_t capctl_used(const capctl_t *c, const char country[2], uint8_t dir, uint64_t person,
                     uint16_t window_days, uint64_t now);

const char *capctl_reason_name(capctl_reason_t r);
const char *capctl_decision_name(capctl_decision_t d);

#endif /* ZXV_CAPCTL_H */
