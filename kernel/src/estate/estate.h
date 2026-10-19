/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* estate.h — estate succession control: mortality phases, an exact-fraction
 * inheritance manifest, contested-share escrow, royalty streaming to heirs,
 * a time-delayed key release gate, and a forced-heirship plug-in table.
 * Faraid (Islamic inheritance) shares are computed in faraid.h.
 *
 * Owner spec: "mortality phases; jurisdiction enclaves (common law, civil-law
 * / OHADA forced heirship, faraid); contested-share escrow; exact-fraction
 * inheritance manifest; royalty streaming to heirs; time-delayed key
 * release; living veto." Agreed adjustments: nothing changes on silence,
 * only a filed attestation starts anything; a living veto (the owner's own
 * signature) cancels and REFUNDS the executor bond, which is forfeited only
 * on a guardian or court fraud finding; contested shares go to an estate
 * escrow (not the solvency vault) and earn only the V-Bill dividend, the
 * principal passes to heirs; sealed papers are a separate data key split
 * among guardians and released after a delay (here: only the release gate).
 *
 *   E1 PHASES. ALIVE -> REPORTED (est_report: a death attestation signed by a
 *      configured attestor, with the executor bond amount) -> CONFIRMED
 *      (est_confirm: >= k guardian approvals AND tick >= reported +
 *      wait_ticks) -> RULED (faraid and forced-heirship enclaves only: the
 *      configured jurisdiction gateway signs the manifest digest; common law
 *      needs only the confirmation) -> DISTRIBUTING (est_begin) -> CLOSED
 *      (est_close, which refunds a bond still held). A living veto (owner
 *      signature) in REPORTED or CONFIRMED returns to ALIVE, clears the
 *      approvals, bumps the epoch (old signatures stop working) and marks a
 *      held bond REFUNDED. est_fraud_finding (a guardian or the court) marks
 *      a held bond FORFEITED; a forfeited bond stays forfeited after a veto.
 *      Every signature is checked by the caller's verify hook (ML-DSA in
 *      production) over est_msg, which binds the owner key, the epoch and the
 *      manifest digest. Nothing moves on time or silence alone.
 *   E2 MANIFEST. Up to EST_MAX_HEIRS heirs with exact fractions over one
 *      common denominator (the least one) summing to exactly 1.
 *      est_distribute(amount) gives each heir floor(amount * num / den); the
 *      leftover units (fewer than the number of heirs) go one each by largest
 *      remainder, ties to the lower heir index, so the parts sum to amount
 *      exactly. A contested heir's part goes to the escrow instead (E2a).
 *      est_stream(amount) for royalties keeps the cumulative total T and what
 *      each heir has been paid, and pays each heir floor(T * num / den) minus
 *      what it already had: cumulative payouts equal floor(T * share)
 *      exactly. The units this floor holds back (fewer than the number of
 *      heirs, est_stream_held) stay in the stream until later payments carry
 *      them over; they are never paid twice and never lost.
 *   E2a ESCROW. A guardian or the court files est_contest on an heir; from
 *      then on that heir's parts are held in escrow (per heir, principal
 *      only: no interest, nothing grows here; the V-Bill dividend is the
 *      V-Bill module's). est_resolve (court signature) releases the
 *      principal to that heir and ends the contest.
 *   E3 KEY RELEASE. est_key_release_ok(tick) is true only in DISTRIBUTING or
 *      CLOSED and when tick >= confirmed + release_ticks.
 *   E4 COMMON LAW. The will's shares as given (est_set_manifest with
 *      EST_COMMON), validated to sum to 1. The will is fixed while the owner
 *      is ALIVE.
 *   E5 FORCED HEIRSHIP. A plug-in table of rows: the reserved portion by
 *      number of children. One built-in row, France (Code civil art. 913:
 *      1 child 1/2, 2 children 2/3, 3 or more 3/4), split equally among the
 *      children; the rest is the freely disposable quota, which goes by the
 *      will. Will gifts to non-children are reduced proportionally (exactly)
 *      to fit the quota; disposable quota the will leaves unused goes to the
 *      children equally. Other rows are registered by the operator (a signed
 *      manifest in production; the caller checks it). Output is ADVISORY.
 *   E6 ADVISORY. Faraid and forced-heirship manifests can be set until the
 *      ruling and need it before distribution: they are advisory, never a
 *      ruling (est_advisory).
 *
 * HONEST LIMITS. This is not legal advice. Forced-heirship rules differ by
 * country and by facts (the France row ignores, e.g., the surviving spouse's
 * rights, lifetime gifts and representation); schools of fiqh differ (see
 * faraid.h). A qualified person or court decides; this module only refuses
 * to distribute before a signed ruling, and is designed to support that
 * process. The bond, escrow and payouts are amounts this module reports;
 * the ledger moves them. Freestanding C11: no allocation, no float.
 */
#ifndef ZXV_EST_ESTATE_H
#define ZXV_EST_ESTATE_H

#include <stdbool.h>
#include <stdint.h>

#define EST_KEY           32u
#define EST_MAX_HEIRS     16u
#define EST_MAX_GUARDIANS 5u
#define EST_MAX_ATTESTORS 4u
#define EST_COURT         0xFFu /* signer index: the court key */
#define EST_FORCED_STEPS  4u
#define EST_MAX_JUR       8u
#define EST_JUR(a, b)     ((uint16_t) (((uint16_t) (uint8_t) (a) << 8) | (uint8_t) (b)))

typedef enum {
    EST_OK = 0,
    EST_DUP = 1, /* duplicate approval / contest: nothing changed */
    EST_ERR_ARG = -1,
    EST_ERR_SIG = -2,
    EST_ERR_STATE = -3,       /* wrong phase / bond / contest state */
    EST_ERR_QUORUM = -4,      /* E1: fewer than k approvals */
    EST_ERR_TIME = -5,        /* E1: waiting period not over */
    EST_ERR_SUM = -6,         /* E2/E5: fractions do not sum to 1 */
    EST_ERR_OVERFLOW = -7,    /* an exact value does not fit 64 bits */
    EST_ERR_UNSUPPORTED = -8, /* faraid: relatives not modelled */
    EST_ERR_NOMANIFEST = -9,
    EST_ERR_FULL = -10,
    EST_ERR_UNKNOWN = -11 /* unknown jurisdiction */
} est_status_t;

typedef enum {
    EST_ALIVE = 0,
    EST_REPORTED = 1,
    EST_CONFIRMED = 2,
    EST_RULED = 3,
    EST_DISTRIBUTING = 4,
    EST_CLOSED = 5
} est_phase_t;

typedef enum { EST_NONE = 0, EST_COMMON = 1, EST_FORCED = 2, EST_FARAID = 3 } est_enclave_t;

typedef enum {
    EST_BOND_NONE = 0,
    EST_BOND_HELD = 1,
    EST_BOND_REFUNDED = 2,
    EST_BOND_FORFEITED = 3
} est_bond_t;

enum {
    EST_MSG_REPORT = 1,
    EST_MSG_APPROVE = 2,
    EST_MSG_VETO = 3,
    EST_MSG_RULING = 4,
    EST_MSG_FRAUD = 5,
    EST_MSG_CONTEST = 6,
    EST_MSG_RESOLVE = 7
};

typedef struct {
    uint64_t num, den;
} est_frac_t;

/* verify(ctx, signer, msg, len, sig, sig_len): true when sig is signer's. */
typedef bool (*est_verify_fn)(void *ctx, const uint8_t signer[EST_KEY], const uint8_t *msg,
                              uint32_t len, const uint8_t *sig, uint32_t sig_len);

typedef struct {
    uint8_t owner[EST_KEY]; /* living veto */
    uint8_t guardian[EST_MAX_GUARDIANS][EST_KEY];
    uint32_t n_guardians, k; /* 1 <= k <= n_guardians */
    uint8_t attestor[EST_MAX_ATTESTORS][EST_KEY];
    uint32_t n_attestors;
    uint8_t court[EST_KEY];   /* fraud findings, contest resolution */
    uint8_t gateway[EST_KEY]; /* the jurisdiction gateway: qualified rulings */
    uint64_t wait_ticks;      /* E1 */
    uint64_t release_ticks;   /* E3 */
} est_cfg_t;

typedef struct {
    est_cfg_t cfg;
    est_verify_fn verify;
    void *vctx;
    est_phase_t phase;
    uint64_t epoch;
    uint64_t bond;
    est_bond_t bond_state;
    uint64_t reported, confirmed;
    uint32_t approved; /* bit i: guardian i */
    /* E2 manifest */
    est_enclave_t enclave; /* EST_NONE: no manifest */
    uint32_t n;
    uint32_t id[EST_MAX_HEIRS];
    uint64_t num[EST_MAX_HEIRS];
    uint64_t den;
    uint8_t digest[32];
    /* E2a */
    bool contested[EST_MAX_HEIRS];
    uint64_t ccount[EST_MAX_HEIRS];
    uint64_t escrow[EST_MAX_HEIRS];
    uint64_t escrow_total;
    /* E2 stream */
    uint64_t stream_total;
    uint64_t paid[EST_MAX_HEIRS];
} est_t;

est_status_t est_init(est_t *e, const est_cfg_t *cfg, est_verify_fn verify, void *vctx);

/* "zxv/est-msg/v1" || kind || owner || epoch (LE) || value (LE) || digest */
#define EST_MSG (14u + 1u + EST_KEY + 8u + 8u + 32u)
void est_msg(const est_t *e, uint8_t kind, uint64_t value, uint8_t out[EST_MSG]);
/* The value signed by est_contest / est_resolve for heir h. */
uint64_t est_contest_value(const est_t *e, uint32_t heir);

/* E1. Signed messages: REPORT (value = bond), APPROVE, VETO, RULING, FRAUD
 * (value 0). */
est_status_t est_report(est_t *e, uint32_t attestor, uint64_t bond, uint64_t tick,
                        const uint8_t *sig, uint32_t sig_len);
est_status_t est_approve(est_t *e, uint32_t guardian, const uint8_t *sig, uint32_t sig_len);
est_status_t est_confirm(est_t *e, uint64_t tick);
est_status_t est_rule(est_t *e, const uint8_t *sig, uint32_t sig_len);
est_status_t est_begin(est_t *e);
est_status_t est_close(est_t *e);
est_status_t est_veto(est_t *e, const uint8_t *sig, uint32_t sig_len);
/* who: a guardian index or EST_COURT */
est_status_t est_fraud_finding(est_t *e, uint32_t who, const uint8_t *sig, uint32_t sig_len);

/* E2/E4. Per-heir fractions num[i]/den[i] (any denominators); stored over
 * the least common denominator. EST_COMMON only while ALIVE; EST_FORCED and
 * EST_FARAID until RULED. Clears contests. */
est_status_t est_set_manifest(est_t *e, est_enclave_t enc, uint32_t n, const uint32_t id[],
                              const uint64_t num[], const uint64_t den[]);
bool est_advisory(const est_t *e);

/* E2. out[0..n) gets each heir's part (0 for a contested heir); *to_escrow
 * (optional) what went to escrow this call. Distribute: DISTRIBUTING only.
 * Stream: DISTRIBUTING or CLOSED (royalties keep coming). */
est_status_t est_distribute(est_t *e, uint64_t amount, uint64_t out[EST_MAX_HEIRS],
                            uint64_t *to_escrow);
est_status_t est_stream(est_t *e, uint64_t amount, uint64_t out[EST_MAX_HEIRS],
                        uint64_t *to_escrow);
uint64_t est_stream_held(const est_t *e);

/* E2a. who: a guardian index or EST_COURT; sig over est_msg(CONTEST,
 * est_contest_value). est_resolve: the court over est_msg(RESOLVE, ...);
 * *released gets the heir's escrowed principal. */
est_status_t est_contest(est_t *e, uint32_t heir, uint32_t who, const uint8_t *sig,
                         uint32_t sig_len);
est_status_t est_resolve(est_t *e, uint32_t heir, const uint8_t *sig, uint32_t sig_len,
                         uint64_t *released);

/* E3 */
bool est_key_release_ok(const est_t *e, uint64_t tick);

/* E5. Row: reserve[i] applies to i + 1 children; n_steps or more children
 * use reserve[n_steps - 1]. No children: no reserve. */
typedef struct {
    uint16_t jur; /* EST_JUR('F', 'R') */
    uint32_t n_steps;
    est_frac_t reserve[EST_FORCED_STEPS];
} est_forced_row_t;

typedef struct {
    est_forced_row_t row[EST_MAX_JUR];
    uint32_t n;
} est_forced_table_t;

typedef struct {
    uint32_t n;                  /* children first, then the gifts */
    uint64_t num[EST_MAX_HEIRS]; /* over den, lowest terms */
    uint64_t den;
    bool reduced;  /* the gifts were cut to the disposable quota */
    bool advisory; /* always true */
} est_forced_out_t;

void est_forced_init(est_forced_table_t *t); /* France only */
est_status_t est_forced_register(est_forced_table_t *t, const est_forced_row_t *row);
const est_forced_row_t *est_forced_find(const est_forced_table_t *t, uint16_t jur);
/* gift_num[j] / will_den: the will's gift j (of the whole estate) to a
 * non-child. With no children the gifts must sum to exactly 1. */
est_status_t est_forced_shares(const est_forced_row_t *row, uint32_t n_children, uint32_t n_gifts,
                               const uint64_t gift_num[], uint64_t will_den, est_forced_out_t *out);
est_status_t est_set_forced(est_t *e, const est_forced_table_t *t, uint16_t jur,
                            uint32_t n_children, const uint32_t child_id[], uint32_t n_gifts,
                            const uint32_t gift_id[], const uint64_t gift_num[], uint64_t will_den);

#endif /* ZXV_EST_ESTATE_H */
