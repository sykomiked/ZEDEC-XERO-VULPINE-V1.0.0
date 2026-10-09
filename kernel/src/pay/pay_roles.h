/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* pay_roles.h — who operates a payment endpoint, and how it is configured.
 *
 * ROLES
 *   INDIVIDUAL    a person paying and being paid.
 *   SELF_BANK     a self-banking operator: its own pay_ledger, limits, fee
 *                 rule, tithe policy and compliance profile.
 *   INSTITUTION   a financial institution: LEI (ISO 17442, ISO 7064 mod 97-10
 *                 check digits), BIC (ISO 9362 format), optional settlement
 *                 IBAN (ISO 13616 mod-97 and registry length), and its own
 *                 gateway endpoints.
 *
 * GATEWAYS are callbacks only: SWIFT, CIPS, SEPA, FedNow and a crypto chain.
 * pay_role_dispatch hands a finished message to the callback the operator
 * configured. There is no networking in kernel/src/pay; what the callback
 * does with the bytes (an HSM, a SWIFT Alliance Lite2 box at a member bank,
 * a CIPS participant's front end, a wallet) is entirely outside this code.
 *
 * COMPLIANCE HOOKS. KYC/AML screening is a callback that returns PASS,
 * REVIEW or BLOCK; travel-rule fields (FATF Recommendation 16: originator
 * and beneficiary name, account or wallet, and one of address / national
 * id / customer id / date and place of birth) are carried for crypto
 * transfers at or above the operator's threshold. These are HOOKS: they
 * carry and gate on data the operator supplies; they do not perform
 * screening and they do not make anyone compliant with anything.
 *
 * FEES AND NO USURY. A fee is either flat or proportional to the payment,
 * fixed when the payment is made. There is no interest, accrual, late fee or
 * balance-based fee kind; the INTEREST and LATE kinds exist only so that a
 * configuration asking for them is refused with PAY_ERR_USURY.
 *
 * EQUITY GATE. pay_equity is disabled per role or jurisdiction here: equity
 * offerings fall under securities regulation in most jurisdictions.
 *
 * HONEST LIMITS. Schema validity is not certification; there is no SWIFT or
 * CIPS connectivity (both need membership or a sponsoring partner bank);
 * operating as a bank, self-bank or money transmitter needs licences; the
 * store-credit classification of VFV is a legal question for counsel;
 * format-valid LEI/BIC/IBAN values are not proof that the entity or account
 * exists (that needs GLEIF, SWIFTRef or the bank). Freestanding.
 */
#ifndef ZXV_PAY_ROLES_H
#define ZXV_PAY_ROLES_H

#include <stdint.h>
#include <stdbool.h>
#include "pay_util.h"
#include "pay_ledger.h"
#include "pay_tithe.h"

/* ===== Identifier validation ===== */
/* ISO 7064 MOD 97-10 remainder of an alphanumeric string (A=10 .. Z=35).
 * Returns -1 on a character outside [0-9A-Z]. */
int32_t pay_mod97(const char *s, size_t n);
/* ISO 17442: 18 alphanumerics + 2 check digits, mod 97 == 1. */
bool pay_lei_valid(const char *lei);
/* ISO 9362: 4 alnum party prefix, 2-letter ISO 3166 country, 2 alnum
 * location, optional 3 alnum branch (8 or 11). */
bool pay_bic_valid(const char *bic);
/* ISO 13616: country, 2 check digits, BBAN; registry length; mod 97 == 1.
 * Spaces are not accepted (electronic format). Countries not in the
 * built-in length table are refused. */
bool pay_iban_valid(const char *iban);
uint32_t pay_iban_length(const char *country); /* 0 if unknown */

/* ===== Roles ===== */
typedef enum {
    PAY_ROLE_INDIVIDUAL = 0,
    PAY_ROLE_SELF_BANK = 1,
    PAY_ROLE_INSTITUTION = 2,
    PAY_ROLE_COUNT
} pay_role_t;

typedef enum {
    PAY_GW_SWIFT = 0,
    PAY_GW_CIPS = 1,
    PAY_GW_SEPA = 2,
    PAY_GW_FEDNOW = 3,
    PAY_GW_CHAIN = 4,
    PAY_GW_COUNT
} pay_gateway_kind_t;

typedef struct {
    pay_gateway_kind_t kind;
    const char *msg_def; /* e.g. "pacs.008.001.08"                  */
    const char *bah;     /* head.001.001.02 AppHdr XML, or NULL     */
    const char *doc;     /* the message Document XML                */
    uint32_t doc_len;
    const char *uetr;
} pay_gw_msg_t;

/* Return 0 when the operator's gateway accepted the bytes. */
typedef int32_t (*pay_gateway_submit_fn)(void *ctx, const pay_gw_msg_t *m);

typedef struct {
    bool enabled;
    pay_gateway_submit_fn submit;
    void *ctx;
    char endpoint[64]; /* operator's label for its endpoint; never dialled here */
} pay_gateway_t;

typedef enum { PAY_SCREEN_PASS = 0, PAY_SCREEN_REVIEW = 1, PAY_SCREEN_BLOCK = 2 } pay_screen_t;

/* FATF R.16 data for one party. */
typedef struct {
    char name[71];
    char account[65];     /* IBAN, account number or wallet address */
    char address[141];    /* structured address rendered as one line */
    char national_id[36]; /* or customer id                          */
    char birth[36];       /* date and place of birth                 */
    char vasp_lei[21];    /* the party's VASP / FI, if any           */
} pay_travel_party_t;

typedef struct {
    bool present;
    pay_travel_party_t originator;
    pay_travel_party_t beneficiary;
} pay_travel_rule_t;

typedef struct {
    uint32_t role_id;
    const char *counterparty_name;
    const char *counterparty_country; /* ISO 3166 alpha-2 or "" */
    uint64_t amount;
    const char *asset_code;
    bool crypto;
    const pay_travel_rule_t *travel;
} pay_screen_req_t;

typedef pay_screen_t (*pay_screen_fn)(void *ctx, const pay_screen_req_t *req);

typedef enum {
    PAY_FEE_NONE = 0,
    PAY_FEE_FLAT = 1,
    PAY_FEE_PROPORTIONAL = 2,
    PAY_FEE_INTEREST = 3, /* refused: usury */
    PAY_FEE_LATE = 4      /* refused: usury */
} pay_fee_kind_t;

typedef struct {
    pay_fee_kind_t kind;
    uint64_t flat;
    pay_rat_t prop; /* fraction of the payment, e.g. 1/1000 */
} pay_fee_rule_t;

/* Fee for one payment of `amount`. PAY_ERR_USURY for INTEREST / LATE. */
pay_status_t pay_fee_compute(const pay_fee_rule_t *f, uint64_t amount, uint64_t *fee);

typedef struct {
    char profile[32];          /* operator label, e.g. "retail-basic"       */
    uint8_t kyc_tier_required; /* the caller's KYC tier must reach this     */
    bool screening_required;   /* call `screen` before every payment         */
    bool travel_rule_required; /* for crypto at or above the threshold       */
    uint64_t travel_rule_threshold;
} pay_compliance_t;

typedef struct {
    uint64_t per_tx_max;     /* 0 = no limit */
    uint64_t per_period_max; /* 0 = no limit */
    uint64_t period_ticks;   /* period length in supplied ticks */
} pay_limits_t;

typedef struct {
    pay_role_t role;
    uint32_t id;
    char name[71];
    char jurisdiction[8]; /* platform jurisdiction label, e.g. "NCR" (non-ISO) */
    char country[3];      /* ISO 3166 alpha-2 of the legal seat, or ""          */
    pay_limits_t limits;
    pay_fee_rule_t fee;
    pay_tithe_policy_t tithe;
    pay_compliance_t compliance;
    pay_ledger_t *ledger; /* SELF_BANK: its own ledger */
    char lei[21];
    char bic[12];
    char iban[35];
    pay_gateway_t gw[PAY_GW_COUNT];
    pay_screen_fn screen;
    void *screen_ctx;
    bool equity_enabled;
    uint64_t period_start;
    uint64_t period_used;
} pay_role_cfg_t;

/* Defaults: no limits, no fee, the default tithe policy, no screening, no
 * gateways, equity disabled, jurisdiction from the platform default ("NCR"),
 * no settlement asset (an institution never defaults to VFV). */
pay_status_t pay_role_init(pay_role_cfg_t *c, pay_role_t role, uint32_t id, const char *name);
/* Check the configuration for its role (LEI/BIC/IBAN, ledger, fee). */
pay_status_t pay_role_validate(const pay_role_cfg_t *c);
pay_status_t pay_role_set_gateway(pay_role_cfg_t *c, pay_gateway_kind_t k, pay_gateway_submit_fn fn,
                                  void *ctx, const char *endpoint);

/* Gate one outgoing payment: limits (per tx, per period by supplied tick),
 * KYC tier, screening hook, travel-rule presence. On PAY_OK the period usage
 * is advanced. *screen gets the hook verdict (PASS when no hook ran). */
pay_status_t pay_role_authorize(pay_role_cfg_t *c, const pay_screen_req_t *req, uint8_t kyc_tier,
                                uint64_t tick, pay_screen_t *screen);

/* Hand a finished message to the configured gateway. PAY_ERR_STATE when the
 * gateway is not configured; PAY_ERR_POLICY when its callback refused. */
pay_status_t pay_role_dispatch(const pay_role_cfg_t *c, const pay_gw_msg_t *m);

/* True iff the travel-rule record has the R.16 minimum for both parties. */
bool pay_travel_rule_complete(const pay_travel_rule_t *t);

#endif /* ZXV_PAY_ROLES_H */
