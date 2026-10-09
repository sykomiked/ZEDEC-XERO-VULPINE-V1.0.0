/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv_api.h — the stable, handle-based C ABI of the ZXV platform core.
 *
 * This header is the ONE place where managed bindings (the .NET SDK in
 * bindings/dotnet, and anything else that speaks the C calling convention)
 * meet the freestanding kernel modules. The kernel headers behind it
 * (kernel/src/iso20022, finance, vino_stores, cbank, cardnet, pay) are free
 * to change; this surface is not. Its rules:
 *
 *   R1  OPAQUE HANDLES. Callers only ever hold pointers to incomplete types
 *       (zxv_ctx, zxv_msg, zxv_netting). No kernel struct crosses the ABI,
 *       so no binding depends on a struct layout.
 *   R2  SCALARS ONLY. Every parameter is a fixed-width integer, a size_t,
 *       a NUL-terminated UTF-8 string, a caller-owned byte buffer, or a
 *       pointer to one of those (an out parameter). No floats: amounts are
 *       exact int64 minor units (cents, kobo, pesewas, ...).
 *   R3  STATUS CODES. Every function except the destroy functions and
 *       zxv_status_name returns int32_t: ZXV_OK (0) or a negative ZXV_E_*.
 *       Out parameters are written only on ZXV_OK, except *out_len on
 *       ZXV_E_BUFFER (see R4).
 *   R4  CALLER-OWNED BUFFERS, TWO-CALL PATTERN. A function that returns
 *       text takes (char *buf, size_t cap, size_t *out_len). *out_len always
 *       receives the length the full result needs, EXCLUDING the NUL. If buf
 *       is NULL or cap <= that length, the call returns ZXV_E_BUFFER and
 *       writes nothing to buf; allocate *out_len + 1 bytes and call again.
 *       On ZXV_OK the result is NUL-terminated.
 *   R5  UTF-8. All strings in and out are UTF-8. Input that is not valid
 *       UTF-8 is refused with ZXV_E_UTF8. ISO 20022 fields are XML-escaped
 *       by the serializer; callers pass plain text.
 *   R6  THREADING. A zxv_ctx is internally serialized by a mutex: any number
 *       of threads may call functions on the same context. A zxv_msg or
 *       zxv_netting handle is NOT thread-safe; use it from one thread at a
 *       time. Each handle must be destroyed exactly once, after every other
 *       call on it has returned. A child handle (msg, netting) must be
 *       destroyed before the zxv_ctx it was created from.
 *   R7  LAST-ERROR DETAIL. When a call fails, a human-readable reason is kept
 *       in thread-local storage; zxv_last_error copies it out. It is only
 *       diagnostic text: branch on the status code, never on the message.
 *   R8  VERSIONING. ZXV_ABI_VERSION_MAJOR changes only on a breaking change.
 *       New functions, new enum values and new ZXV_E_* codes are additive
 *       (MINOR). Bindings must treat an unknown negative status as an error.
 *   R9  FEATURES. Modules still being built in the kernel are present in the
 *       ABI as stubs that return ZXV_E_NOTIMPL. zxv_features reports which
 *       groups are live in THIS build, so a binding can light up features
 *       without recompiling.
 *
 * CURRENCIES. ISO 20022 wire messages carry ISO 4217 ALPHA codes ("NGN",
 * "GHS", "XOF") in Ccy attributes. The Vino rail numerics DEBIT 846,
 * CREDIT 810 and EQUITY 888 are platform-internal: none is an active ISO 4217
 * currency (810 is the withdrawn RUR code), so this ABI refuses them, and any
 * private platform unit such as VFV, as a message currency (ZXV_E_CURRENCY).
 * They appear only in ledger rail balances and proprietary fields.
 *
 * HONEST LIMITS. This library serializes messages and keeps an in-process
 * ledger. It has no network transport: no PAPSS, SWIFT, CIPS or RTGS
 * connectivity is included, and nothing here is certified by any scheme.
 */
#ifndef ZXV_API_H
#define ZXV_API_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
#    if defined(ZXV_BUILD_SHARED)
#        define ZXV_API __declspec(dllexport)
#    else
#        define ZXV_API __declspec(dllimport)
#    endif
#elif defined(__GNUC__) && defined(ZXV_BUILD_SHARED)
#    define ZXV_API __attribute__((visibility("default")))
#else
#    define ZXV_API
#endif

/* ===== Version ===== */
#define ZXV_ABI_VERSION_MAJOR 1
#define ZXV_ABI_VERSION_MINOR 0
#define ZXV_ABI_VERSION_PATCH 0

/* ===== Status codes (stable; never renumbered) ===== */
#define ZXV_OK            0
#define ZXV_E_ARG         (-1)  /* NULL handle/pointer or invalid argument value  */
#define ZXV_E_BUFFER      (-2)  /* output buffer too small; *out_len = needed     */
#define ZXV_E_NOTIMPL     (-3)  /* feature not built into this library yet        */
#define ZXV_E_NOT_FOUND   (-4)  /* unknown account, card, currency or index       */
#define ZXV_E_CAPACITY    (-5)  /* a fixed-size table is full                     */
#define ZXV_E_FUNDS       (-6)  /* holder account would go below zero             */
#define ZXV_E_CURRENCY    (-7)  /* unknown, disabled, non-ISO or mismatched ccy   */
#define ZXV_E_RANGE       (-8)  /* amount <= 0 or beyond the exact range          */
#define ZXV_E_STATE       (-9)  /* operation not valid in the current state       */
#define ZXV_E_INVARIANT   (-10) /* an internal accounting invariant failed        */
#define ZXV_E_UTF8        (-11) /* input string is not valid UTF-8                */
#define ZXV_E_TOO_LONG    (-12) /* input string exceeds the field's maximum       */
#define ZXV_E_DUPLICATE   (-13) /* reference/id already used                      */
#define ZXV_E_NOMEM       (-14) /* allocation failed                              */
#define ZXV_E_DECLINED    (-15) /* card authorization declined (not a fault)      */
#define ZXV_E_FIELD       (-16) /* field not valid for this message kind / unset  */
#define ZXV_E_INTERNAL    (-99) /* unexpected kernel status; see zxv_last_error   */

/* ===== Feature bits (zxv_features) ===== */
#define ZXV_FEAT_LEDGER      0x00000001u /* triple-ledger accounts/postings     */
#define ZXV_FEAT_MX_PACS008  0x00000002u
#define ZXV_FEAT_MX_CAMT053  0x00000004u
#define ZXV_FEAT_ISO4217     0x00000008u /* full ISO 4217 table lookup          */
#define ZXV_FEAT_MX_PACS002  0x00000010u
#define ZXV_FEAT_MX_PACS004  0x00000020u
#define ZXV_FEAT_MX_PACS009  0x00000040u
#define ZXV_FEAT_MX_CAMT056  0x00000080u
#define ZXV_FEAT_MX_CAMT029  0x00000100u
#define ZXV_FEAT_NETTING     0x00000200u /* multilateral netting cycles         */
#define ZXV_FEAT_CARDS       0x00000400u /* card issue/reissue/authorize        */
#define ZXV_FEAT_CARD_CHECK  0x00000800u /* PAN check digits (Luhn/Damm/Verh.)  */
#define ZXV_FEAT_VSS         0x00001000u /* VSS conformance checks              */

/* ===== Opaque handles ===== */
typedef struct zxv_ctx zxv_ctx;         /* one platform instance (ledger, config) */
typedef struct zxv_msg zxv_msg;         /* one ISO 20022 message under construction */
typedef struct zxv_netting zxv_netting; /* one netting cycle                     */

/* ===== Library ===== */

/* ABI version of the loaded library. Any out pointer may be NULL. */
ZXV_API int32_t zxv_version(uint32_t *major, uint32_t *minor, uint32_t *patch);

/* Bitmask of ZXV_FEAT_* live in this build. */
ZXV_API int32_t zxv_features(uint32_t *out_mask);

/* Static, never-NULL symbolic name for a status ("ZXV_E_FUNDS"). Not owned
 * by the caller; never free it. Unknown codes give "ZXV_E_UNKNOWN". */
ZXV_API const char *zxv_status_name(int32_t status);

/* Copy the calling thread's last error detail (R7). "" if none. */
ZXV_API int32_t zxv_last_error(char *buf, size_t cap, size_t *out_len);

/* ===== Context ===== */

/* Create a platform context: an empty ledger with no currencies enabled. */
ZXV_API int32_t zxv_ctx_create(zxv_ctx **out_ctx);

/* Destroy a context. NULL is ignored. */
ZXV_API void zxv_ctx_destroy(zxv_ctx *ctx);

/* ===== ISO 4217 reference data (process-wide, read-only) ===== */

#define ZXV_CCYF_FUND     0x01u /* fund code (e.g. CLF, USN)                    */
#define ZXV_CCYF_MINOR_NA 0x02u /* minor unit N.A. (metals, XDR, XTS, XXX)      */
#define ZXV_CCYF_NOCTRY   0x04u /* not the currency of any ISO 3166 country     */
#define ZXV_CCYF_PAYABLE  0x08u /* usable for a payment amount                  */
#define ZXV_CCYF_PRIVATE  0x10u /* platform-private unit registered in a ctx;   */
                                /* never valid in an ISO 20022 Ccy attribute    */
#define ZXV_MINOR_NA 255u

/* Look up an ISO 4217 currency by alpha-3 ("NGN"). Out pointers may be NULL.
 * ZXV_E_NOT_FOUND for anything not in ISO 4217 (including VFV). */
ZXV_API int32_t zxv_iso4217_by_alpha(const char *alpha, uint16_t *out_numeric,
                                     uint8_t *out_minor_units, uint32_t *out_flags);

/* Look up by ISO 4217 numeric (566 -> "NGN"). 846/810/888 -> ZXV_E_NOT_FOUND. */
ZXV_API int32_t zxv_iso4217_by_numeric(uint16_t numeric, char *alpha_buf, size_t cap,
                                       size_t *out_len, uint8_t *out_minor_units,
                                       uint32_t *out_flags);

/* English name of an ISO 4217 currency ("Naira"). */
ZXV_API int32_t zxv_iso4217_name(const char *alpha, char *buf, size_t cap, size_t *out_len);

/* Number of entries in the ISO 4217 table, and enumeration by index. */
ZXV_API int32_t zxv_iso4217_count(uint32_t *out_count);
ZXV_API int32_t zxv_iso4217_at(uint32_t index, char *alpha_buf, size_t cap, size_t *out_len);

/* Publication date of the ISO 4217 list compiled in ("2026-01-01"). */
ZXV_API int32_t zxv_iso4217_published(char *buf, size_t cap, size_t *out_len);

/* Interop caveat for a numeric code (e.g. 810 = withdrawn RUR), "" if none. */
ZXV_API int32_t zxv_ccy_caveat(uint16_t numeric, char *buf, size_t cap, size_t *out_len);

/* True (1) if ISO 3166 alpha-2 country is an African Union member. */
ZXV_API int32_t zxv_au_is_member(const char *country_a2, int32_t *out_is_member);

/* ===== Vino rails ===== */
typedef enum {
    ZXV_RAIL_DEBIT = 0, /* 846: asset / backing      */
    ZXV_RAIL_CREDIT = 1, /* 810: claim / liability   */
    ZXV_RAIL_EQUITY = 2  /* 888: debit - credit      */
} zxv_rail;

/* Rail -> internal numeric (846/810/888). Proprietary fields only. */
ZXV_API int32_t zxv_rail_numeric(int32_t rail, uint16_t *out_numeric);

/* ===== Currency configuration (per context) ===== */

/* Enable an ISO 4217 currency for ledger accounts in this context. Minor
 * units come from the ISO 4217 table. ZXV_E_CURRENCY if not payable. */
ZXV_API int32_t zxv_ccy_enable(zxv_ctx *ctx, const char *alpha);

/* Enable an ISO 4217 currency with operator-supplied minor units: only for
 * builds without the ISO 4217 table, or to assert the expected value (a
 * mismatch with the table is ZXV_E_CURRENCY). */
ZXV_API int32_t zxv_ccy_enable_with_minor(zxv_ctx *ctx, const char *alpha, uint8_t minor_units);

/* Register a platform-private unit (e.g. "VFV", 2 minor units). It can be
 * held in ledger accounts but is flagged ZXV_CCYF_PRIVATE and refused as an
 * ISO 20022 currency. `code` is 3..8 upper-case letters/digits and must not
 * collide with an ISO 4217 alpha. */
ZXV_API int32_t zxv_ccy_register_private(zxv_ctx *ctx, const char *code, uint8_t minor_units);

/* Is `code` enabled in this context? Gives its minor units and flags. */
ZXV_API int32_t zxv_ccy_get(zxv_ctx *ctx, const char *code, uint8_t *out_minor_units,
                            uint32_t *out_flags);

/* Enumerate enabled currencies. */
ZXV_API int32_t zxv_ccy_enabled_count(zxv_ctx *ctx, uint32_t *out_count);
ZXV_API int32_t zxv_ccy_enabled_at(zxv_ctx *ctx, uint32_t index, char *buf, size_t cap,
                                   size_t *out_len);

/* ===== Ledger (finance/triple_ledger + exact shadow) ===== */

typedef enum {
    ZXV_ACCT_HOLDER = 0, /* may never go below zero (no debt)                */
    ZXV_ACCT_ISSUER = 1  /* may carry a CREDIT (810) balance: issuance/nostro */
} zxv_account_kind;

/* Largest amount accepted in one posting and largest |balance| (2^53 - 1),
 * so that every value is exact in every kernel number representation. */
#define ZXV_AMOUNT_MAX ((int64_t) 9007199254740991LL)

/* Open an account in an enabled currency. `name` <= 63 bytes. */
ZXV_API int32_t zxv_ledger_open_account(zxv_ctx *ctx, const char *name, const char *ccy,
                                        int32_t kind, uint32_t *out_account_id);

/* Post a balanced transfer of `amount_minor` (> 0) from `from_account` to
 * `to_account`, both in `ccy`. Posts the financial, provenance and
 * externality axes of the triple ledger atomically: it is fully validated
 * (funds, currency, range, uniqueness) and every resource is reserved before
 * any balance is written. `reference` (<= 35 bytes, e.g. the EndToEndId)
 * must be unique in the context. *out_entry_id receives the platform entry
 * id (monotonic from 1, shared by both sides). Journals are unbounded: the
 * adapter chains fixed-size kernel triple-ledger segments transparently. */
ZXV_API int32_t zxv_ledger_post(zxv_ctx *ctx, uint32_t from_account, uint32_t to_account,
                                int64_t amount_minor, const char *ccy, const char *reference,
                                uint64_t *out_entry_id);

/* Rail balances of an account in minor units: DEBIT 846 (sum received),
 * CREDIT 810 (sum sent), EQUITY 888 (= debit - credit, the spendable
 * position). Any out pointer may be NULL. */
ZXV_API int32_t zxv_ledger_balance(zxv_ctx *ctx, uint32_t account, int64_t *out_debit_minor,
                                   int64_t *out_credit_minor, int64_t *out_equity_minor);

/* Account metadata. */
ZXV_API int32_t zxv_ledger_account_ccy(zxv_ctx *ctx, uint32_t account, char *buf, size_t cap,
                                       size_t *out_len);
ZXV_API int32_t zxv_ledger_account_name(zxv_ctx *ctx, uint32_t account, char *buf, size_t cap,
                                        size_t *out_len);
ZXV_API int32_t zxv_ledger_account_kind(zxv_ctx *ctx, uint32_t account, int32_t *out_kind);
ZXV_API int32_t zxv_ledger_account_count(zxv_ctx *ctx, uint32_t *out_count);

/* Journal of one account, oldest first. Each line: entry id, signed amount
 * (+ received / - sent), counterparty account, reference. */
ZXV_API int32_t zxv_ledger_entry_count(zxv_ctx *ctx, uint32_t account, uint32_t *out_count);
ZXV_API int32_t zxv_ledger_entry_at(zxv_ctx *ctx, uint32_t account, uint32_t index,
                                    uint64_t *out_entry_id, int64_t *out_signed_minor,
                                    uint32_t *out_counterparty, char *ref_buf, size_t ref_cap,
                                    size_t *out_ref_len);

/* Verify the ledger: per currency sum(debit) == sum(credit), every holder
 * >= 0, exact shadow == triple-ledger financial axis. ZXV_E_INVARIANT with
 * detail in zxv_last_error on failure. */
ZXV_API int32_t zxv_ledger_check(zxv_ctx *ctx);

/* ===== ISO 20022 messages ===== */

typedef enum {
    ZXV_MX_PACS008 = 1, /* FIToFICstmrCdtTrf      pacs.008.001.09 */
    ZXV_MX_CAMT053 = 2, /* BkToCstmrStmt          camt.053.001.08 */
    ZXV_MX_PACS002 = 3, /* FIToFIPmtStsRpt        (cbank, pending) */
    ZXV_MX_PACS004 = 4, /* PmtRtr                 (cbank, pending) */
    ZXV_MX_PACS009 = 5, /* FICdtTrf               (cbank, pending) */
    ZXV_MX_CAMT056 = 6, /* FIToFIPmtCxlReq        (cbank, pending) */
    ZXV_MX_CAMT029 = 7  /* RsltnOfInvstgtn        (cbank, pending) */
} zxv_mx_kind;

/* Text fields. Each kind accepts a subset; others give ZXV_E_FIELD. */
typedef enum {
    ZXV_FLD_MSG_ID = 1,        /* GrpHdr/MsgId                 <= 35  all     */
    ZXV_FLD_CRE_DT_TM = 2,     /* GrpHdr/CreDtTm (ISO 8601)    <= 32  all     */
    ZXV_FLD_DEBTOR_NAME = 3,   /* Dbtr/Nm                      <= 70  pacs008 */
    ZXV_FLD_DEBTOR_ACCT = 4,   /* DbtrAcct/Id                  <= 35  pacs008 */
    ZXV_FLD_CREDITOR_NAME = 5, /* Cdtr/Nm                      <= 70  pacs008 */
    ZXV_FLD_CREDITOR_ACCT = 6, /* CdtrAcct/Id                  <= 35  pacs008 */
    ZXV_FLD_END_TO_END_ID = 7, /* PmtId/EndToEndId             <= 35  pacs008 */
    ZXV_FLD_CCY = 8,           /* ISO 4217 alpha-3             == 3   all     */
    ZXV_FLD_ACCT_ID = 9        /* Stmt/Acct/Id/Othr/Id         <= 35  camt053 */
} zxv_mx_field;

/* Amount fields (exact minor units + decimal places). */
typedef enum {
    ZXV_AMT_SETTLEMENT = 1, /* pacs008 IntrBkSttlmAmt / InstdAmt */
    ZXV_AMT_OPENING = 2,    /* camt053 OPBD                      */
    ZXV_AMT_CLOSING = 3     /* camt053 CLBD                      */
} zxv_mx_amount;

typedef enum { ZXV_CRDT = 0, ZXV_DBIT = 1 } zxv_cdtdbt;
typedef enum { ZXV_STS_BOOK = 0, ZXV_STS_PDNG = 1, ZXV_STS_INFO = 2 } zxv_entry_status;

/* Create a message of `kind`. The context validates currencies (enabled is
 * not required, but ISO 4217 membership and minor units are checked).
 * ZXV_E_NOTIMPL for a kind whose kernel builder is not available. */
ZXV_API int32_t zxv_msg_create(zxv_ctx *ctx, int32_t kind, zxv_msg **out_msg);
ZXV_API void zxv_msg_destroy(zxv_msg *msg);

ZXV_API int32_t zxv_msg_set_text(zxv_msg *msg, int32_t field, const char *utf8);

/* `frac_digits` must equal the currency's ISO 4217 minor units once the
 * currency is set (checked again at render). Negative balances are allowed
 * for camt053 opening/closing (rendered with DBIT). */
ZXV_API int32_t zxv_msg_set_amount(zxv_msg *msg, int32_t field, int64_t units,
                                   uint8_t frac_digits);

/* Number of transactions (pacs008 GrpHdr/NbOfTxs). Default 1. */
ZXV_API int32_t zxv_msg_set_tx_count(zxv_msg *msg, uint32_t nb_of_txs);

/* Append one camt053 statement entry (non-negative magnitude + direction). */
ZXV_API int32_t zxv_msg_add_entry(zxv_msg *msg, int64_t units, uint8_t frac_digits,
                                  int32_t cdt_dbt, int32_t status);

/* Serialize to XML (R4 two-call pattern). Validates that every mandatory
 * field is set (ZXV_E_FIELD) and the currency (ZXV_E_CURRENCY). */
ZXV_API int32_t zxv_msg_render(zxv_msg *msg, char *buf, size_t cap, size_t *out_len);

/* ===== Netting cycles (kernel/src/cbank — pending) ===== */

/* Open a multilateral netting cycle in one settlement currency. */
ZXV_API int32_t zxv_netting_open(zxv_ctx *ctx, const char *cycle_id, const char *ccy,
                                 zxv_netting **out_cycle);
ZXV_API void zxv_netting_destroy(zxv_netting *cycle);

/* Add a participant (e.g. a BIC or PAPSS participant code). */
ZXV_API int32_t zxv_netting_add_participant(zxv_netting *cycle, const char *participant);

/* Submit one obligation debtor -> creditor, idempotent on `payment_id`. */
ZXV_API int32_t zxv_netting_submit(zxv_netting *cycle, const char *payment_id,
                                   const char *debtor, const char *creditor,
                                   int64_t amount_minor);

/* Close the cycle and compute net positions. */
ZXV_API int32_t zxv_netting_close(zxv_netting *cycle);

/* Net positions after close: + receives, - pays. Sum is always zero. */
ZXV_API int32_t zxv_netting_position_count(zxv_netting *cycle, uint32_t *out_count);
ZXV_API int32_t zxv_netting_position_at(zxv_netting *cycle, uint32_t index, char *buf,
                                        size_t cap, size_t *out_len, int64_t *out_net_minor);

/* ===== Cards (kernel/src/cardnet — pending except check digits) ===== */

typedef enum {
    ZXV_NET_PHOENIX = 1,     /* Damm check digit     */
    ZXV_NET_DRAGON = 2,      /* Luhn check digit     */
    ZXV_NET_THUNDERBIRD = 3  /* Verhoeff check digit */
} zxv_card_network;

/* Validate the check digit of a decimal PAN for a network (1 = valid). */
ZXV_API int32_t zxv_card_check_digit(int32_t network, const char *pan, int32_t *out_valid);

/* Issue a charge card bound to a ledger account. Writes a masked PAN. */
ZXV_API int32_t zxv_card_issue(zxv_ctx *ctx, int32_t network, uint32_t account,
                               int64_t limit_minor, uint64_t *out_card_id, char *masked_pan,
                               size_t cap, size_t *out_len);

/* Reissue (lost/expired): the old card is blocked, a new id is returned. */
ZXV_API int32_t zxv_card_reissue(zxv_ctx *ctx, uint64_t card_id, uint64_t *out_new_card_id);

/* Authorize an amount. ZXV_OK with an approval code, or ZXV_E_DECLINED with
 * an ISO 8583 response code in *out_response (e.g. 51 insufficient funds). */
ZXV_API int32_t zxv_card_authorize(zxv_ctx *ctx, uint64_t card_id, int64_t amount_minor,
                                   const char *ccy, int32_t *out_response, char *approval_buf,
                                   size_t cap, size_t *out_len);

/* ===== VSS conformance (kernel/src/cbank — pending) ===== */

typedef enum {
    ZXV_VSS_C1_BALANCE = 1,   /* every cycle nets to zero                 */
    ZXV_VSS_C2_RECEIPTS = 2,  /* every payment has a receipt              */
    ZXV_VSS_C3_ROUNDTRIP = 3, /* MX build -> parse -> build is identical  */
    ZXV_VSS_C4_BACKING = 4    /* settlement backed by funded positions    */
} zxv_vss_check;

typedef enum { ZXV_VSS_NONE = 0, ZXV_VSS_BRONZE = 1, ZXV_VSS_SILVER = 2, ZXV_VSS_GOLD = 3 } zxv_vss_level;

/* Run one check over the context. *out_passed = 1/0; detail text (R4). */
ZXV_API int32_t zxv_vss_run(zxv_ctx *ctx, int32_t check, int32_t *out_passed, char *detail,
                            size_t cap, size_t *out_len);

/* Overall conformance level (zxv_vss_level). */
ZXV_API int32_t zxv_vss_level_get(zxv_ctx *ctx, int32_t *out_level);

#ifdef __cplusplus
}
#endif

#endif /* ZXV_API_H */
