/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* juris_cp.h — the jurisdiction gateway key and the compliance plug-ins
 * (capctl, taxhook, kycwrap) as juris.h plug-ins.
 *
 * juris.h J3 needs a verify hook for the deployment's jurisdiction gateway
 * key, and capctl.h, taxhook.h and kycwrap.h each said "a later adapter maps
 * them to the juris plug-in model". This file is that adapter. Nothing here
 * changes how capctl, taxhook or kycwrap decide; it only translates their
 * answers into 64-state tags (lpres_hex.h) and their inputs from
 * juris_transfer_t.
 *
 *   JC1 GATEWAY KEY. juris_gw_t holds one ML-DSA-87 public key
 *       (PQM_MLDSA87_PK_BYTES). juris_gw_verify is a juris_verify_fn: it
 *       accepts a signature only when a key is set, the signature is exactly
 *       PQM_MLDSA87_SIG_BYTES long and it verifies under the ML-DSA context
 *       string JURIS_GW_CTX ("ZXV-JURIS-GATEWAY"). That context is not
 *       cpk.h's "ZXV-COMPLIANCE", so a compliance-manifest signature never
 *       passes as a plug-in manifest signature, or the other way round, even
 *       from the same key. No key set: every manifest is refused. The key id
 *       is SHA-256 of the public key (juris_gw_key_id).
 *   JC2 MANIFEST. juris_cp_manifest fills a juris_manifest_t from a filled
 *       plug-in (id, country, hook mask), a version and the plug-in "image".
 *       For these plug-ins the image is the signed compliance manifest
 *       (cp_common.h M1) the plug-in serves: its SHA-256 goes in the J3
 *       message, so the gateway signs exactly that table.
 *   JC3 CAPCTL (transfer + commit hooks). juris_cp_capctl_plugin makes a
 *       plug-in with id "capctl/CC" serving country CC and requires a CC
 *       profile in the capctl instance (capctl_require, floor 0), so a
 *       cross-border transfer touching CC with no valid profile is DENY
 *       (PROFILE_MISSING or PROFILE_EXPIRED): fail closed. The transfer hook
 *       answers: capctl disabled -> NEITHER (the plug-in does not answer, so
 *       the host waits); ALLOW -> TRUE; ALLOW_WITH_REPORT -> TRUE, with the
 *       capctl_result_t (and its reports) kept for the caller
 *       (juris_cp_capctl_report); DENY -> FALSE. The commit hook calls
 *       capctl_commit for the same mapped transfer.
 *   JC4 MAPPING. from_country and to_country must be two capital letters;
 *       either "" (unknown) is NEITHER: a cross-border rule cannot be judged
 *       without both countries. ccy (ISO 4217 alpha) goes to capctl's ISO
 *       4217 numeric through the host's juris_ccy_fn; no mapper, or a code
 *       it does not know (a private unit, VFV), is FALSE (a mapper that
 *       answers 0 is refused by capctl itself: DENY, so FALSE too). The ledger accounts
 *       sender and receiver are capctl's person ids. juris_transfer_t has no
 *       purpose code and no base amount, so those go as none (capctl C5 then
 *       denies when a profile needs them). `now` comes from the host's clock.
 *   JC5 TAXHOOK (tax_rate hook). juris_cp_tax_plugin makes a plug-in with
 *       id "taxhook/CC/REGION/K" for one installed table (country, region,
 *       taxhook kind). It answers juris JURIS_TAX_VAT only, and only from a
 *       national (region "") VAT or GST table: the STANDARD rate in force on
 *       today's date (UTC, from the clock) as taxhook X3 computes it,
 *       rate_ppm / 10^6, TRUE. Every other question (income, withholding, a
 *       sales-tax or regional table) is NEITHER: this table does not know.
 *       taxhook disabled is NEITHER. A table that is missing, outside its
 *       validity window, or has no STANDARD rate in force on that date is
 *       FALSE: the plug-in owns the answer and refuses to guess one.
 *   JC6 KYC GATE (transfer hook). juris_kyc_gate_t records, per ledger
 *       account, that it presented a valid kycwrap proof of one predicate
 *       (juris_kyc_admit runs kyc_verify and keeps only the token's expiry
 *       day). Off by default (juris_kyc_enable): while off the hook answers
 *       NEITHER. When on, the sender (and the receiver too when
 *       need_receiver) must have an admission whose expiry day is not past:
 *       TRUE, else FALSE. Room for a new account is checked before the proof
 *       is verified, so a full gate never spends a token.
 *   JC7 TAGS. Every answer is lpx_make(base, NEUTRAL, POTENTIAL); a NULL
 *       context or transfer is NEITHER. Hooks never write the ledger.
 *
 * HONEST LIMITS. A ledger account is not a person: capctl's per-person
 * tallies and the KYC gate count accounts, so one person with two accounts
 * has two tallies. capctl tallies live in caller memory; nothing here saves
 * them. The juris tax question has no category, region or date, so only the
 * national standard VAT/GST rate is reachable through juris; anything finer
 * (reduced rates, US sales tax, invoices) stays taxhook's own API. The KYC
 * gate links an admitted token to a ledger account inside this verifier,
 * which kycwrap's minimal-disclosure design otherwise avoids; it proves a
 * predicate was shown once, not who the account holder is today. kycwrap's
 * ZK backend is still a slot (KYC_E_NOT_AVAILABLE). The plug-in image is the
 * compliance manifest bytes, not code: these plug-ins are compiled in, and
 * the gateway's signature binds the table they serve, not the code that
 * reads it. None of this is legal or tax advice. Freestanding C11: no
 * allocation, no float, no libc.
 */
#ifndef ZXV_JURIS_CP_H
#define ZXV_JURIS_CP_H

#include "juris.h"
#include "../capctl/capctl.h"
#include "../capctl/cpk.h"
#include "../taxhook/taxhook.h"
#include "../kycwrap/kycwrap.h"
#include "../pqsec/pq_matrix_algs.h"

/* the longest plug-in id, "taxhook/CC/RRRRRR/K", without its NUL */
#define JURIS_CP_ID_MAX (7u + 3u + 1u + CP_REGION_LEN + 2u)
_Static_assert(JURIS_CP_ID_MAX < JURIS_ID_LEN, "juris_cp plug-in ids fit with their NUL");

/* ---- JC1 ---- */
#define JURIS_GW_CTX     "ZXV-JURIS-GATEWAY"
#define JURIS_GW_CTX_LEN 17u
#define JURIS_GW_PK_LEN  PQM_MLDSA87_PK_BYTES
#define JURIS_GW_SK_LEN  PQM_MLDSA87_SK_BYTES
#define JURIS_GW_SIG_LEN PQM_MLDSA87_SIG_BYTES

typedef struct {
    bool set;
    uint8_t pk[JURIS_GW_PK_LEN];
} juris_gw_t;

void juris_gw_init(juris_gw_t *g);
/* false (and g unchanged) unless len == JURIS_GW_PK_LEN */
bool juris_gw_set(juris_gw_t *g, const uint8_t *pk, size_t len);
void juris_gw_key_id(const uint8_t pk[JURIS_GW_PK_LEN], uint8_t out[32]);
/* juris_verify_fn: ctx is a juris_gw_t */
bool juris_gw_verify(void *ctx, const uint8_t *msg, uint32_t len, const uint8_t *sig,
                     uint32_t sig_len);
/* The gateway's side (tools and tests): rnd 32 fresh bytes or NULL. */
void juris_gw_sign(const uint8_t sk[JURIS_GW_SK_LEN], const uint8_t *msg, size_t len,
                   const uint8_t *rnd, uint8_t sig[JURIS_GW_SIG_LEN]);

/* ---- JC2 ---- */
bool juris_cp_manifest(const juris_plugin_t *p, uint64_t version, const uint8_t *image,
                       size_t image_len, const uint8_t *sig, uint32_t sig_len,
                       juris_manifest_t *out);

/* ---- JC3 / JC4 ---- */
/* the unix time in seconds */
typedef uint64_t (*juris_clock_fn)(void *ctx);
/* ISO 4217 alpha ("KES") -> numeric (404); false when not a known code */
typedef bool (*juris_ccy_fn)(const char *alpha, uint16_t *num);

typedef struct {
    capctl_t *cc;
    juris_clock_fn clock;
    void *cctx;
    juris_ccy_fn ccy;
    bool have; /* last holds the last evaluation */
    capctl_result_t last;
} juris_cp_capctl_t;

/* false on a bad argument or when capctl_require refuses (p left zero) */
bool juris_cp_capctl_plugin(juris_plugin_t *p, juris_cp_capctl_t *a, capctl_t *cc,
                            const char country[2], juris_clock_fn clock, void *cctx,
                            juris_ccy_fn ccy);
/* JC3: true when the last transfer hook answered ALLOW_WITH_REPORT; *out
 * then holds that result with its reports. */
bool juris_cp_capctl_report(const juris_cp_capctl_t *a, capctl_result_t *out);

/* ---- JC5 ---- */
typedef struct {
    taxhook_t *th;
    char country[2];
    char region[CP_REGION_LEN];
    uint8_t kind; /* taxhook_kind_t */
    juris_clock_fn clock;
    void *cctx;
} juris_cp_tax_t;

bool juris_cp_tax_plugin(juris_plugin_t *p, juris_cp_tax_t *a, taxhook_t *th, const char country[2],
                         const char region[CP_REGION_LEN], uint8_t kind, juris_clock_fn clock,
                         void *cctx);
/* YYYYMMDD (UTC) of a unix time; 0 past year 9999 */
uint32_t juris_cp_ymd(uint64_t unix_s);

/* ---- JC6 ---- */
typedef struct {
    uint32_t account;
    uint32_t expiry_day;
} juris_kyc_entry_t;

typedef struct {
    bool enabled;
    bool need_receiver;
    kyc_verifier_t *v;
    kyc_pred_t need; /* the predicate every admission must prove */
    char country[2];
    juris_clock_fn clock;
    void *cctx;
    juris_kyc_entry_t *e;
    uint32_t cap, n;
} juris_kyc_gate_t;

/* Off until juris_kyc_enable. store: caller memory for cap accounts. */
bool juris_kyc_init(juris_kyc_gate_t *g, kyc_verifier_t *v, const kyc_pred_t *need,
                    const char country[2], juris_clock_fn clock, void *cctx,
                    juris_kyc_entry_t *store, uint32_t cap);
void juris_kyc_enable(juris_kyc_gate_t *g, bool on, bool need_receiver);
/* Verify a presentation (kyc_verify) for req and admit `account` until the
 * token's expiry day. KYC_E_PREDICATE when req asks another predicate,
 * KYC_E_FULL when the gate has no room for a new account. */
kyc_status_t juris_kyc_admit(juris_kyc_gate_t *g, const kyc_request_t *req, const uint8_t *proof,
                             size_t len, uint64_t now, uint32_t account);
/* p: a transfer plug-in, id "kycgate/CC" */
bool juris_kyc_plugin(juris_plugin_t *p, juris_kyc_gate_t *g);

#endif /* ZXV_JURIS_CP_H */
