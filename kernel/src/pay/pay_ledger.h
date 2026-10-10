/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* pay_ledger.h — exact-integer, three-rail, nine-capital payment ledger.
 *
 * THE THREE RAILS (the same codes as vino_stores.h and iso20022.h)
 * ----------------------------------------------------------------
 *   DEBIT  555  NCR  what an account holds (asset / backing). VFV's default
 *                    numeric code is also 555 (platform-internal, not ISO
 *                    4217), so VFV's platform jurisdiction is NCR.
 *   CREDIT 777  NRE  what an account owes (claim / liability). Only ISSUER
 *                    accounts may carry a CREDIT balance: no debt for holders.
 *   EQUITY 888  PNS  debit - credit, posted on every line ("everything has
 *                    equity"). For a share asset (pay_equity) a holder's
 *                    EQUITY balance is its share count.
 *   555, 777 and 888 are all unassigned in ISO 4217: internal rail numerics,
 *   never written into an ISO 20022 Ccy attribute.
 *   Each rail carries its platform jurisdiction code (NCR New California
 *   Republic, NRE Neo Roman Empire, PNS Principality of New Sicily). None is
 *   an ISO 3166 country: they travel only as /ZXV/... remittance data and are
 *   never emitted in a Ctry field (pay_iso_country_ok guards all three).
 *
 * NO USURY. Nothing in kernel/src/pay bears interest: there is no interest
 * rate, accrual, late-payment charge or balance-based fee anywhere. A CREDIT
 * balance is an issuer's claim (store credit outstanding), redeemed at par
 * and never grown by time. pay_usury_check refuses any repayment schedule or
 * fee that would take back more than was given, beyond a flat or
 * transaction-proportional fee agreed up front.
 *   Every posting line carries all three rails: (d_debit, d_credit) are
 *   given and d_equity = d_debit - d_credit is derived and posted.
 *
 * INVARIANTS (checked by pay_ledger_check after every operation)
 * --------------------------------------------------------------
 *   L1  Per posting and per (asset, capital form): sum d_debit == sum
 *       d_credit, hence sum d_equity == 0. A transfer moves DEBIT between
 *       holders; an issue raises a holder's DEBIT and the issuer's CREDIT.
 *   L2  Per (asset, capital form), over all accounts: sum DEBIT == sum CREDIT
 *       and sum EQUITY == 0 (the balance invariant, per rail and per form).
 *   L3  Per account: EQUITY == DEBIT - CREDIT, DEBIT >= 0, CREDIT >= 0, and
 *       CREDIT == 0 unless the account is an ISSUER. Balances stay < 2^62.
 *   L4  A posting is all-or-nothing: it is validated completely before any
 *       balance changes, so a refused posting leaves no partial state.
 *   L5  The four Crown capital forms (Social, Natural, Cultural, Spiritual;
 *       zcapital.h) are inalienable: a posting touching a Crown-form account
 *       may only move value between accounts of the SAME owner.
 *
 * THE TRIPLE LEDGER (finance/triple_ledger.h axes)
 * ------------------------------------------------
 *   Financial (rational):    the balanced rail lines above.
 *   Provenance (logical):    every posting appends a record to a SHA3-256
 *                            hash chain: prev hash, request digest, UETR,
 *                            end-to-end id, attestor, tick, lines.
 *   Externality (imaginary): every posting may carry a signed externality
 *                            value for one capital form, summed per form.
 *   finance/triple_ledger itself uses surplus_real_t (a double on TEST_HOST
 *   builds) and is not linked; an operator can mirror each record into it
 *   through the `mirror` callback.
 *
 * IDENTIFIERS AND REPLAY
 * ----------------------
 *   R1  Every posting has a UETR (UUIDv4 text, from caller randomness via
 *       pay_uetr_from_random) and an end-to-end id; both must be unique
 *       within the journal window.
 *   R2  Idempotency: a 32-byte key. The same key with the same request
 *       returns the original receipt and posts nothing; the same key with a
 *       different request is refused (PAY_ERR_REPLAY).
 *   R3  Replay protection beyond the journal window: an optional per-
 *       initiator nonce that must strictly increase.
 *
 * ASSETS
 * ------
 *   ISO 4217 fiat (alpha, numeric, minor units supplied by the caller), crypto with
 *   an ISO 24165 Digital Token Identifier where one is supplied, VFV (a
 *   platform unit flagged non-ISO and store credit), share assets
 *   (pay_equity), and plain measurement units. Amounts are uint64 minor
 *   units with a per-asset decimal-place count; no float anywhere.
 *   DTIs: a DTI is a 9-character code assigned by the DTI Foundation
 *   (dtif.org) under ISO 24165. pay_ledger does NOT ship a DTI table and
 *   never invents one: the operator supplies the DTI when registering a
 *   crypto asset (checked only for 9 characters of [0-9B-DF-HJ-NP-TV-Z]).
 *   The test uses 4H95J0R2X (Bitcoin) as published in the DTIF registry;
 *   verify every DTI against the registry before production use.
 *
 * HONEST LIMITS. Schema validity is not certification; there is no SWIFT or
 * CIPS connectivity; operating this ledger for other people's money (as a
 * bank, self-bank or money transmitter) needs licences; whether VFV is store
 * credit is a legal question for counsel. Freestanding: no libc, no
 * allocation, no floating point, no 64-bit division.
 */
#ifndef ZXV_PAY_LEDGER_H
#define ZXV_PAY_LEDGER_H

#include <stdint.h>
#include <stdbool.h>
#include "pay_util.h"
#include "pay_rails.h"
#include "../zcapital/zcap_forms.h"

/* ===== Rails ===== */
/* Numerics and jurisdictions come from the canonical pay_rails.h. */
#define PAY_RAIL_DEBIT_CODE   ZXV_RAIL_DEBIT        /* 555, unassigned in ISO 4217 */
#define PAY_RAIL_CREDIT_CODE  ZXV_RAIL_CREDIT       /* 777, unassigned in ISO 4217 */
#define PAY_RAIL_EQUITY_CODE  ZXV_RAIL_EQUITY       /* 888, unassigned in ISO 4217 */
#define PAY_RAIL_DEBIT_JURIS  ZXV_RAIL_DEBIT_JURIS  /* "NCR" (not ISO 3166) */
#define PAY_RAIL_CREDIT_JURIS ZXV_RAIL_CREDIT_JURIS /* "NRE" (not ISO 3166) */
#define PAY_RAIL_EQUITY_JURIS ZXV_RAIL_EQUITY_JURIS /* "PNS" (not ISO 3166) */
typedef enum {
    PAY_RAIL_DEBIT = 0,
    PAY_RAIL_CREDIT = 1,
    PAY_RAIL_EQUITY = 2,
    PAY_RAIL_COUNT
} pay_rail_t;
uint16_t pay_rail_code(pay_rail_t r);
/* Rail -> its platform jurisdiction code ("NCR" / "NRE" / "PNS"), "" for an
 * unknown rail. Never NULL; never an ISO 3166 country. */
const char *pay_rail_juris(pay_rail_t r);

/* ===== Nine capitals: aliases of zcap_form_t (zcapital/zcap_forms.h) ===== */
typedef enum {
    PAY_CAP_FINANCIAL = ZCAP_FINANCIAL,
    PAY_CAP_MANUFACTURED = ZCAP_MANUFACTURED,
    PAY_CAP_INTELLECTUAL = ZCAP_INTELLECTUAL,
    PAY_CAP_HUMAN = ZCAP_HUMAN,
    PAY_CAP_SOCIAL = ZCAP_SOCIAL,       /* Crown */
    PAY_CAP_NATURAL = ZCAP_NATURAL,     /* Crown */
    PAY_CAP_CULTURAL = ZCAP_CULTURAL,   /* Crown */
    PAY_CAP_SPIRITUAL = ZCAP_SPIRITUAL, /* Crown */
    PAY_CAP_SYSTEM = ZCAP_SYSTEM,
    PAY_CAP_COUNT = ZCAP_FORM_COUNT
} pay_cap_t;
_Static_assert((int) PAY_CAP_FINANCIAL == (int) ZCAP_FINANCIAL, "pay FINANCIAL == ZCAP_FINANCIAL");
_Static_assert((int) PAY_CAP_MANUFACTURED == (int) ZCAP_MANUFACTURED,
               "pay MANUFACTURED == ZCAP_MANUFACTURED");
_Static_assert((int) PAY_CAP_INTELLECTUAL == (int) ZCAP_INTELLECTUAL,
               "pay INTELLECTUAL == ZCAP_INTELLECTUAL");
_Static_assert((int) PAY_CAP_HUMAN == (int) ZCAP_HUMAN, "pay HUMAN == ZCAP_HUMAN");
_Static_assert((int) PAY_CAP_SOCIAL == (int) ZCAP_SOCIAL, "pay SOCIAL == ZCAP_SOCIAL");
_Static_assert((int) PAY_CAP_NATURAL == (int) ZCAP_NATURAL, "pay NATURAL == ZCAP_NATURAL");
_Static_assert((int) PAY_CAP_CULTURAL == (int) ZCAP_CULTURAL, "pay CULTURAL == ZCAP_CULTURAL");
_Static_assert((int) PAY_CAP_SPIRITUAL == (int) ZCAP_SPIRITUAL, "pay SPIRITUAL == ZCAP_SPIRITUAL");
_Static_assert((int) PAY_CAP_SYSTEM == (int) ZCAP_SYSTEM, "pay SYSTEM == ZCAP_SYSTEM");
_Static_assert((int) PAY_CAP_COUNT == ZCAP_FORM_COUNT, "pay count == ZCAP_FORM_COUNT");
bool pay_cap_is_crown(pay_cap_t c);

/* ===== Status ===== */
typedef enum {
    PAY_OK = 0,
    PAY_DUPLICATE = 1, /* R2: idempotent replay of an identical request */
    PAY_ERR_ARG = -1,
    PAY_ERR_FULL = -2,
    PAY_ERR_NO_ACCOUNT = -3,
    PAY_ERR_NO_ASSET = -4,
    PAY_ERR_UNBALANCED = -5, /* L1 */
    PAY_ERR_FUNDS = -6,      /* DEBIT would go below 0 */
    PAY_ERR_CREDIT = -7,     /* L3: CREDIT on a non-issuer, or below 0 */
    PAY_ERR_OVERFLOW = -8,
    PAY_ERR_REPLAY = -9,    /* R2 key reused for a different request, or R3 */
    PAY_ERR_DUP_UETR = -10, /* R1 */
    PAY_ERR_UETR = -11,     /* malformed UETR */
    PAY_ERR_CROWN = -12,    /* L5 */
    PAY_ERR_INVARIANT = -13,
    PAY_ERR_NOT_FOUND = -14,
    PAY_ERR_STATE = -15,
    PAY_ERR_LIMIT = -16,
    PAY_ERR_POLICY = -17,
    PAY_ERR_USURY = -18 /* interest or an interest-like charge */
} pay_status_t;

/* NO USURY guard: given `principal` handed over and `repaid` asked back, plus
 * an up-front `fee` agreed at transaction time, refuses (PAY_ERR_USURY) when
 * repaid > principal (anything returned above principal is interest), or
 * when `time_based` is set (a charge that grows with time or balance is
 * interest by another name, including late fees). */
pay_status_t pay_usury_check(uint64_t principal, uint64_t repaid, bool time_based);

/* ===== Platform defaults (operator-overridable) ===== */
typedef struct {
    char vfv_alpha[8];     /* "VFV" — platform-internal code, NOT ISO 4217 */
    uint16_t vfv_numeric;  /* 555 = PAY_RAIL_DEBIT_CODE (not ISO 4217)     */
    uint8_t vfv_minor;     /* 2                                            */
    char jurisdiction[8];  /* "NCR" — VFV's rail (DEBIT) jurisdiction, NOT
                              ISO 3166; emitted as /ZXV/JURIS/<code>      */
    bool jurisdiction_iso; /* false: never emitted as a country code       */
    /* Per-rail jurisdiction codes, indexed by pay_rail_t: "NCR", "NRE",
     * "PNS". Never ISO 3166 and never emitted as a country code. */
    char rail_juris[PAY_RAIL_COUNT][8];
} pay_platform_t;
void pay_platform_default(pay_platform_t *p);

/* ===== Assets ===== */
typedef enum {
    PAY_ASSET_FIAT = 0,     /* ISO 4217                         */
    PAY_ASSET_CRYPTO = 1,   /* ISO 24165 DTI when supplied       */
    PAY_ASSET_PLATFORM = 2, /* VFV                               */
    PAY_ASSET_SHARE = 3,    /* pay_equity shares                 */
    PAY_ASSET_UNIT = 4      /* token-cycles, bytes, byte-hours    */
} pay_asset_kind_t;

#define PAY_CODE_MAX 12u
typedef struct {
    char code[PAY_CODE_MAX + 1];
    uint16_t numeric; /* ISO 4217 numeric, VFV platform numeric, or 0 */
    uint8_t minor;    /* decimal places                              */
    uint8_t kind;     /* pay_asset_kind_t                            */
    char dti[10];     /* ISO 24165 DTI or ""                         */
    bool iso4217;     /* may appear in an ISO 20022 Ccy attribute     */
    bool store_credit;
    bool active;
} pay_asset_t;

/* ===== Accounts ===== */
#define PAY_ACCT_ISSUER   0x01u /* may carry CREDIT (an issuance account) */
#define PAY_ACCT_COMMONS  0x02u /* network commons pool                   */
#define PAY_ACCT_EXTERNAL 0x04u /* mirror of an external (nostro) balance  */
#define PAY_ACCT_FROZEN   0x08u /* compliance hold: refuses outgoing debit */

typedef struct {
    uint32_t owner;
    uint16_t asset;
    uint8_t cap;
    uint8_t flags;
    uint64_t debit;
    uint64_t credit;
    int64_t equity;
    bool active;
} pay_account_t;

/* ===== Postings ===== */
#define PAY_MAX_LINES 8u
#define PAY_E2E_MAX   35u
#define PAY_MEMO_MAX  35u

typedef struct {
    uint32_t account;
    int64_t d_debit;
    int64_t d_credit;
} pay_line_t;

typedef enum {
    PAY_KIND_TRANSFER = 0,
    PAY_KIND_ISSUE = 1,
    PAY_KIND_REDEEM = 2,
    PAY_KIND_TITHE = 3,
    PAY_KIND_RETURN = 4,
    PAY_KIND_TRADE = 5,
    PAY_KIND_ADJUST = 6
} pay_kind_t;

typedef struct {
    uint8_t idem_key[32];
    char uetr[PAY_UETR_LEN + 1];
    char e2e[PAY_E2E_MAX + 1];
    char memo[PAY_MEMO_MAX + 1];
    uint32_t initiator; /* owner id of the principal asking            */
    uint64_t nonce;     /* R3: 0 = none; else must exceed the last one */
    uint64_t tick;      /* supplied time (phase tick), never wall clock */
    uint32_t attestor;  /* provenance: who vouches for this posting    */
    uint8_t kind;       /* pay_kind_t                                   */
    uint8_t ext_cap;    /* externality capital form                     */
    int64_t ext_value;  /* externality value (0 = none)                 */
    pay_line_t lines[PAY_MAX_LINES];
    uint32_t n_lines;
} pay_posting_req_t;

typedef struct {
    uint64_t seq;
    uint8_t digest[PAY_HASH_LEN]; /* request digest                 */
    uint8_t hash[PAY_HASH_LEN];   /* provenance chain hash          */
    pay_status_t status;
} pay_receipt_t;

typedef struct {
    bool used;
    bool reversed;
    uint64_t seq;
    uint64_t tick;
    uint8_t kind;
    uint8_t idem_key[32];
    uint8_t digest[PAY_HASH_LEN];
    uint8_t prev[PAY_HASH_LEN]; /* chain hash before this record */
    uint8_t hash[PAY_HASH_LEN];
    char uetr[PAY_UETR_LEN + 1];
    char e2e[PAY_E2E_MAX + 1];
    uint32_t attestor;
    pay_line_t lines[PAY_MAX_LINES];
    uint32_t n_lines;
} pay_journal_t;

#define PAY_MAX_ASSETS     48u
#define PAY_MAX_ACCOUNTS   512u
#define PAY_JOURNAL_MAX    1024u
#define PAY_MAX_PRINCIPALS 256u
#define PAY_BAL_MAX        ((uint64_t) 1 << 62)

typedef void (*pay_mirror_fn)(void *ctx, const pay_journal_t *rec);

typedef struct {
    pay_platform_t platform;
    pay_asset_t assets[PAY_MAX_ASSETS];
    uint32_t n_assets;
    pay_account_t acct[PAY_MAX_ACCOUNTS];
    uint32_t n_accounts;
    pay_journal_t journal[PAY_JOURNAL_MAX]; /* ring */
    uint64_t seq;                           /* postings ever made */
    uint8_t chain_head[PAY_HASH_LEN];
    int64_t externality[PAY_CAP_COUNT];
    uint32_t nonce_owner[PAY_MAX_PRINCIPALS];
    uint64_t nonce_last[PAY_MAX_PRINCIPALS];
    uint32_t n_nonce;
    pay_mirror_fn mirror;
    void *mirror_ctx;
    uint16_t vfv_asset; /* index of the VFV asset */
} pay_ledger_t;

/* Initialise with platform defaults (NULL = pay_platform_default). Registers
 * VFV as asset 0. */
void pay_ledger_init(pay_ledger_t *L, const pay_platform_t *platform);

/* Register an ISO 4217 currency. The caller supplies alpha, numeric and minor
 * units from its ISO 4217 table (kernel/src/cbank owns the platform's ISO 4217
 * table; pay deliberately carries none). Checked only for [A-Z]{3}, a
 * numeric in 1..999 that is not one of the three rail numerics, and minor
 * units <= 4. VFV can never be registered this way. */
pay_status_t pay_ledger_add_fiat(pay_ledger_t *L, const char *alpha, uint16_t numeric,
                                 uint8_t minor, uint16_t *out_asset);
/* Register a crypto asset. `dti` may be NULL / "" (none known). */
pay_status_t pay_ledger_add_crypto(pay_ledger_t *L, const char *code, uint8_t minor,
                                   const char *dti, uint16_t *out_asset);
/* Register any other asset (share, unit). Never iso4217. */
pay_status_t pay_ledger_add_asset(pay_ledger_t *L, const char *code, uint8_t minor,
                                  pay_asset_kind_t kind, uint16_t *out_asset);
const pay_asset_t *pay_ledger_asset(const pay_ledger_t *L, uint16_t asset);
bool pay_dti_format_ok(const char *dti);

pay_status_t pay_ledger_open(pay_ledger_t *L, uint32_t owner, uint16_t asset, pay_cap_t cap,
                             uint8_t flags, uint32_t *out_acct);
const pay_account_t *pay_ledger_account(const pay_ledger_t *L, uint32_t acct);
pay_status_t pay_ledger_set_flags(pay_ledger_t *L, uint32_t acct, uint8_t flags);

/* The one choke point: validate (L1, L3, L5, R1-R3), then apply atomically
 * (L4), record provenance and externality, and re-check L2/L3. */
pay_status_t pay_ledger_post(pay_ledger_t *L, const pay_posting_req_t *req, pay_receipt_t *rc);

/* Helpers that fill a request's lines and call pay_ledger_post. `req`
 * supplies the identifiers and metadata; its lines are overwritten. */
pay_status_t pay_ledger_transfer(pay_ledger_t *L, pay_posting_req_t *req, uint32_t from,
                                 uint32_t to, uint64_t amount, pay_receipt_t *rc);
pay_status_t pay_ledger_issue(pay_ledger_t *L, pay_posting_req_t *req, uint32_t issuer, uint32_t to,
                              uint64_t amount, pay_receipt_t *rc);
pay_status_t pay_ledger_redeem(pay_ledger_t *L, pay_posting_req_t *req, uint32_t holder,
                               uint32_t issuer, uint64_t amount, pay_receipt_t *rc);
/* Post the exact negation of the journal entry with UETR `orig_uetr`
 * (pacs.004 return / camt.056 cancellation). Marks it reversed. */
pay_status_t pay_ledger_reverse(pay_ledger_t *L, pay_posting_req_t *req, const char *orig_uetr,
                                pay_receipt_t *rc);

/* A payment with the tithe: from -> to `amount`, plus `contribution` from
 * `from` to `commons` (same asset), plus `vfv_credit` VFV issued by
 * `vfv_issuer` to `vfv_to`, all in ONE atomic posting. */
pay_status_t pay_ledger_pay_tithed(pay_ledger_t *L, pay_posting_req_t *req, uint32_t from,
                                   uint32_t to, uint64_t amount, uint32_t commons,
                                   uint64_t contribution, uint32_t vfv_issuer, uint32_t vfv_to,
                                   uint64_t vfv_credit, pay_receipt_t *rc);

/* L2 + L3 over the whole ledger. */
bool pay_ledger_check(const pay_ledger_t *L);
/* Rail totals for one (asset, cap): sums of DEBIT, CREDIT and EQUITY. */
void pay_ledger_totals(const pay_ledger_t *L, uint16_t asset, pay_cap_t cap, uint64_t *debit,
                       uint64_t *credit, int64_t *equity);
/* Recompute the provenance chain over the journal window and compare. */
bool pay_ledger_verify_chain(const pay_ledger_t *L);
const pay_journal_t *pay_ledger_find_uetr(const pay_ledger_t *L, const char *uetr);

#endif /* ZXV_PAY_LEDGER_H */
