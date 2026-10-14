/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv_api.h — the stable, handle-based C ABI of the ZXV platform core.
 *
 * This header is the ONE place where every SDK (sdk/java, sdk/dotnet,
 * sdk/python, sdk/rust, sdk/go, sdk/typescript, sdk/swift, sdk/cpp, ...)
 * meets the freestanding kernel modules. The kernel headers behind it
 * (kernel/src/iso20022, finance, rational, lpres, rmag, phase_coord,
 * harmonic, swarm, pqsec, tensor, ...) are free to change; this surface is
 * not. sdk/docs/ABI.md explains every rule below with examples. Its rules:
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
 *       ZXV_E_BUFFER (see R4) and *out_granted of zxv_swarm_consume on
 *       ZXV_E_BUDGET (it reports the partial grant).
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
 *       destroyed before the zxv_ctx it was created from. Destroy order
 *       for the ABI 1.1+ handles, children first:
 *         zxv_session  before the zxv_model it was created from, and
 *                      before (or unbound from) any zxv_rmag it is bound to;
 *         zxv_msg made by zxv_msg_create_cap  before that zxv_cap;
 *         an attenuated zxv_cap  before its parent zxv_cap;
 *         zxv_cap, zxv_rmag, zxv_phase, zxv_lpres, zxv_swarm  before the
 *                      zxv_ctx they belong to.
 *         zxv_hexlog  before the zxv_ctx it belongs to (ABI 1.3).
 *         zxv_token_wallet, zxv_treaty, zxv_treaty_result  before the
 *                      zxv_ctx they belong to, where they hold one (ABI 1.5).
 *       A zxv_model holds no context and may outlive the cap that opened
 *       it. The destroy functions themselves never touch the context.
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
 * FAIL-CLOSED (ABI 1.1, completed in 1.2). Once a context is poisoned
 * (zxv_ctx_halt, or a broken ledger invariant) EVERY call on it or on any
 * handle created from it (cap, msg, rmag, phase, lpres, swarm, and the stub
 * groups netting / cards / VSS) returns ZXV_E_POISONED (class FATAL),
 * before any argument validation. The only exceptions: the destroy
 * functions, zxv_ctx_is_poisoned, and zxv_cap_revoke (which can only take
 * authority away). zxv_generate charging a budget of a poisoned context is
 * refused the same way.
 *
 * CURRENCIES. ISO 20022 wire messages carry ISO 4217 ALPHA codes ("NGN",
 * "GHS", "XOF") in Ccy attributes. The Vino rail numerics DEBIT 555 (NRE),
 * CREDIT 777 (NCR) and EQUITY 888 (PNS) are platform-internal: all three are
 * unassigned in ISO 4217, so this ABI refuses them, and any
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
#define ZXV_ABI_VERSION_MINOR 5
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
/* ABI 1.1 */
#define ZXV_E_CAP         (-17) /* the zxv_cap handle lacks the capability, is    */
                                /* revoked, or its attestation is not TRUE        */
#define ZXV_E_POISONED    (-18) /* the context halted after a broken invariant;   */
                                /* terminal: every later call fails the same way  */
#define ZXV_E_BUDGET      (-19) /* an rmag or swarm budget is exhausted (denied)  */
#define ZXV_E_RMAG_INVALID (-20) /* RMAG_RESULT_INVALID: malformed or overflowing */
                                /* rational amount; never retry the same value    */
#define ZXV_E_VERIFY      (-21) /* signature / tag did not verify (not a fault)   */
#define ZXV_E_FORMAT      (-22) /* malformed input (number text, GGUF, frame)     */
#define ZXV_E_IO          (-23) /* a file could not be opened or read             */
#define ZXV_E_FLOAT       (-24) /* NaN or infinity refused at the float boundary  */
#define ZXV_E_INTERNAL    (-99) /* unexpected kernel status; see zxv_last_error   */

/* Status classes (zxv_status_class). Every SDK maps them the same way:
 *   USAGE   a caller bug: fix the arguments; retrying the same call fails again
 *   REFUSED a well-formed request the platform declined (funds, capability,
 *           budget, signature): handle it; a retry may succeed after the
 *           situation changes (a budget is replenished, a capability granted)
 *   FATAL   ZXV_E_INVARIANT / ZXV_E_POISONED: the context is halted and rolled
 *           back; never retry; SDKs raise their terminal error type
 *   SYSTEM  out of memory, I/O, internal: environmental */
#define ZXV_CLASS_OK      0
#define ZXV_CLASS_USAGE   1
#define ZXV_CLASS_REFUSED 2
#define ZXV_CLASS_FATAL   3
#define ZXV_CLASS_SYSTEM  4

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
/* ABI 1.1 */
#define ZXV_FEAT_RATIONAL    0x00002000u /* exact rationals (rational.h rat_t)  */
#define ZXV_FEAT_LPRES       0x00004000u /* four-valued logic + attestations    */
#define ZXV_FEAT_CAPS        0x00008000u /* capability handles (zxv_cap)        */
#define ZXV_FEAT_RMAG        0x00010000u /* rational resource budgets (K2)      */
#define ZXV_FEAT_PHASE       0x00020000u /* phase coordinator ticks (K6)        */
#define ZXV_FEAT_WIRE        0x00040000u /* UBH-168 harmonic wire frames        */
#define ZXV_FEAT_SWARM       0x00080000u /* Fibonacci + emotional token economy */
#define ZXV_FEAT_HASH        0x00100000u /* SHA3-256, SHAKE256, Keccak-256      */
#define ZXV_FEAT_MLKEM1024   0x00200000u /* FIPS 203 ML-KEM-1024                */
#define ZXV_FEAT_MLDSA87     0x00400000u /* FIPS 204 ML-DSA-87                  */
#define ZXV_FEAT_ED25519     0x00800000u /* RFC 8032 Ed25519                    */
#define ZXV_FEAT_TENSOR      0x01000000u /* integer GGUF inference engine       */
#define ZXV_FEAT_LORA        0x02000000u /* LoRA adapters (NOT built yet)       */
#define ZXV_FEAT_EVM         0x04000000u /* EVM state-channel encoding          */
/* ABI 1.3 */
#define ZXV_FEAT_HEX         0x08000000u /* 64-state hexagram tags + quarantine */

/* ===== Opaque handles ===== */
typedef struct zxv_ctx zxv_ctx;         /* one platform instance (ledger, config) */
typedef struct zxv_msg zxv_msg;         /* one ISO 20022 message under construction */
typedef struct zxv_netting zxv_netting; /* one netting cycle                     */
typedef struct zxv_cap zxv_cap;         /* a capability (ABI 1.1, see R10)       */

/* ===== Library ===== */

/* ABI version of the loaded library. Any out pointer may be NULL. */
ZXV_API int32_t zxv_version(uint32_t *major, uint32_t *minor, uint32_t *patch);

/* Bitmask of ZXV_FEAT_* live in this build. */
ZXV_API int32_t zxv_features(uint32_t *out_mask);

/* Static, never-NULL symbolic name for a status ("ZXV_E_FUNDS"). Not owned
 * by the caller; never free it. Unknown codes give "ZXV_E_UNKNOWN". */
ZXV_API const char *zxv_status_name(int32_t status);

/* ZXV_CLASS_* of a status (ABI 1.1). Unknown negative codes are SYSTEM. */
ZXV_API int32_t zxv_status_class(int32_t status);

/* Copy the calling thread's last error detail (R7). "" if none. */
ZXV_API int32_t zxv_last_error(char *buf, size_t cap, size_t *out_len);

/* ===== Context ===== */

/* Create a platform context: an empty ledger with no currencies enabled. */
ZXV_API int32_t zxv_ctx_create(zxv_ctx **out_ctx);

/* Destroy a context. NULL is ignored. */
ZXV_API void zxv_ctx_destroy(zxv_ctx *ctx);

/* 1 if the context halted after a broken invariant (ABI 1.1). A poisoned
 * context refuses every further call with ZXV_E_POISONED; it can only be
 * destroyed. This is the C form of "fail closed": a host may not retry a
 * mathematically corrupted transaction on the same state. */
ZXV_API int32_t zxv_ctx_is_poisoned(zxv_ctx *ctx, int32_t *out_poisoned);

/* Halt a context on purpose (an operator kill switch; also how SDK tests
 * exercise the fatal path). Afterwards it behaves exactly like a context
 * that broke an invariant. reason <= 150 bytes, may be NULL. */
ZXV_API int32_t zxv_ctx_halt(zxv_ctx *ctx, const char *reason);

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

/* Look up by ISO 4217 numeric (566 -> "NGN"). 555/777/888 -> ZXV_E_NOT_FOUND. */
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

/* Interop caveat for a numeric code (e.g. 777 = Vino CREDIT rail), "" if none. */
ZXV_API int32_t zxv_ccy_caveat(uint16_t numeric, char *buf, size_t cap, size_t *out_len);

/* True (1) if ISO 3166 alpha-2 country is an African Union member. */
ZXV_API int32_t zxv_au_is_member(const char *country_a2, int32_t *out_is_member);

/* ===== Vino rails ===== */
typedef enum {
    ZXV_RAIL_DEBIT = 0,  /* 555 NRE: asset / backing  */
    ZXV_RAIL_CREDIT = 1, /* 777 NCR: claim / liability */
    ZXV_RAIL_EQUITY = 2  /* 888 PNS: debit - credit   */
} zxv_rail;

/* Rail -> internal numeric (555/777/888). Proprietary fields only. */
ZXV_API int32_t zxv_rail_numeric(int32_t rail, uint16_t *out_numeric);

/* ===== Currency configuration (per context) =====
 * DEPRECATED since ABI 1.2: zxv_ccy_enable, zxv_ccy_enable_with_minor and
 * zxv_ccy_register_private take no capability (R10). They keep working
 * unchanged; new code calls the zxv_*_cap forms below (LEDGER_POST). */

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

/* ===== Ledger (finance/triple_ledger + exact shadow) =====
 * DEPRECATED since ABI 1.2: the zxv_ctx forms below take no capability
 * (R10). They keep working unchanged for 1.0 bindings; new code calls the
 * zxv_ledger_*_cap forms (section "Capabilities"): opening an account needs
 * LEDGER_POST, reading accounts and journals needs LEDGER_READ. */

typedef enum {
    ZXV_ACCT_HOLDER = 0, /* may never go below zero (no debt)                */
    ZXV_ACCT_ISSUER = 1  /* may carry a CREDIT (777) balance: issuance/nostro */
} zxv_account_kind;

/* Largest amount accepted in one posting and largest |balance| (2^53 - 1),
 * so that every value is exact in every kernel number representation.
 * The hosted library has this range. A firmware build of libzxv on the
 * kernel's Q32.32 numbers (no TEST_HOST) caps amounts, rail totals and the
 * context's total of all debits at 2^31 - 1 instead; beyond that a posting
 * is ZXV_E_RANGE (sdk/embedded/README.md, "Floats"). */
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

/* Rail balances of an account in minor units: DEBIT 555 (sum received),
 * CREDIT 777 (sum sent), EQUITY 888 (= debit - credit, the spendable
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
 * ZXV_E_NOTIMPL for a kind whose kernel builder is not available.
 * DEPRECATED since ABI 1.2 (no capability, R10): use zxv_msg_create_cap.
 * Every zxv_msg_* call on a message of a poisoned context returns
 * ZXV_E_POISONED. */
ZXV_API int32_t zxv_msg_create(zxv_ctx *ctx, int32_t kind, zxv_msg **out_msg);
/* ABI 1.2: create through a capability (needs LEDGER_READ). The message
 * remembers `cap` and every later zxv_msg_set_* / add_entry / render call
 * re-checks it (R10), so revoking the cap or clearing its attestation stops
 * the message. The cap must outlive the message (destroy the msg first). */
ZXV_API int32_t zxv_msg_create_cap(const zxv_cap *cap, int32_t kind, zxv_msg **out_msg);
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
 * field is set (ZXV_E_FIELD) and the currency (ZXV_E_CURRENCY). A camt053
 * whose OPBD + booked entries != CLBD is caller data, not a broken ledger:
 * ZXV_E_FIELD (USAGE), and the context is not halted (ABI 1.1.1). */
ZXV_API int32_t zxv_msg_render(zxv_msg *msg, char *buf, size_t cap, size_t *out_len);

/* ===== Netting cycles (kernel/src/cbank cb_net, ZXV_FEAT_NETTING) =====
 * A cycle nets the obligations submitted to it: no liquidity limit, no
 * prefunding, no sanctions screening (the caller screens), nothing
 * persisted and no settlement instruction sent anywhere. Ids, participant
 * codes and payment ids are 1..35 bytes of UTF-8 (Max35Text); at most 64
 * participants and 512 obligations per cycle (ZXV_E_CAPACITY); an amount
 * is 1..10^15 minor units (ZXV_E_RANGE). After close the cycle takes no
 * more participants or obligations (ZXV_E_STATE), and positions are read
 * only after close (ZXV_E_STATE before). */

/* Open a multilateral netting cycle in one ISO 4217 settlement currency
 * (ZXV_E_CURRENCY for a private unit such as VFV). */
ZXV_API int32_t zxv_netting_open(zxv_ctx *ctx, const char *cycle_id, const char *ccy,
                                 zxv_netting **out_cycle);
ZXV_API void zxv_netting_destroy(zxv_netting *cycle);

/* Add a participant (e.g. a BIC or PAPSS participant code); a code once
 * per cycle (ZXV_E_DUPLICATE). */
ZXV_API int32_t zxv_netting_add_participant(zxv_netting *cycle, const char *participant);

/* Submit one obligation debtor -> creditor, idempotent on `payment_id`:
 * the same obligation again is ZXV_OK and books nothing; the same id with
 * other parties or another amount is ZXV_E_DUPLICATE. Unknown debtor or
 * creditor: ZXV_E_NOT_FOUND; debtor == creditor: ZXV_E_ARG. */
ZXV_API int32_t zxv_netting_submit(zxv_netting *cycle, const char *payment_id,
                                   const char *debtor, const char *creditor,
                                   int64_t amount_minor);

/* Close the cycle and compute net positions. */
ZXV_API int32_t zxv_netting_close(zxv_netting *cycle);

/* Net positions after close, one per participant in the order added:
 * + receives, - pays. Sum is always zero (a cycle that did not net to zero
 * would poison the context: ZXV_E_INVARIANT). */
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

/* ===================================================================== */
/* ABI 1.1: the AI swarm, exactness, logic, security and engine groups.  */
/*                                                                       */
/* Rules R1-R9 above hold unchanged. Two more:                           */
/*   R10 CAPABILITIES. Every call that touches state, keys or the engine */
/*       takes a `const zxv_cap *` as its first argument and verifies it */
/*       on EVERY call (sdk/docs/ABI.md, "Capabilities"). Pure functions */
/*       (rationals, logic tables, hashes, wire framing) take none.      */
/*       ABI 1.2: a cap passed with a handle (rmag, phase, lpres, swarm, */
/*       a session's bound budget) must belong to the same context as    */
/*       that handle, else ZXV_E_CAP. Handles also have *_create_cap     */
/*       forms; the zxv_ctx *_create forms are deprecated.               */
/*   R11 NO FLOATS. No parameter is float or double. Q16 values are      */
/*       int32 fixed point (value = q / 65536). A host float can enter   */
/*       only through zxv_rat_from_ieee754, which takes the raw IEEE-754 */
/*       BITS and returns the exact rational they denote.                */
/* ===================================================================== */

/* ===== Exact rationals (kernel/src/rational: rat_t, the orbital_compat
 * IR numeric normal form) =====
 * A rational crosses the ABI as two int64: numerator and denominator.
 * Results are always reduced with den > 0. Overflow is detected, never
 * silent: ZXV_E_RANGE. A zero denominator is ZXV_E_ARG. INT64_MIN is
 * refused as a numerator or denominator (its negation is not an int64). */

ZXV_API int32_t zxv_rat_normalize(int64_t num, int64_t den, int64_t *out_num, int64_t *out_den);
ZXV_API int32_t zxv_rat_add(int64_t an, int64_t ad, int64_t bn, int64_t bd, int64_t *out_num,
                            int64_t *out_den);
ZXV_API int32_t zxv_rat_sub(int64_t an, int64_t ad, int64_t bn, int64_t bd, int64_t *out_num,
                            int64_t *out_den);
ZXV_API int32_t zxv_rat_mul(int64_t an, int64_t ad, int64_t bn, int64_t bd, int64_t *out_num,
                            int64_t *out_den);
/* Division by zero (bn == 0) is ZXV_E_ARG. */
ZXV_API int32_t zxv_rat_div(int64_t an, int64_t ad, int64_t bn, int64_t bd, int64_t *out_num,
                            int64_t *out_den);
/* *out_cmp = -1, 0 or +1. Exact; no epsilon. */
ZXV_API int32_t zxv_rat_cmp(int64_t an, int64_t ad, int64_t bn, int64_t bd, int32_t *out_cmp);

/* Parse "12.75", "-0.1", "3/4" or "42" exactly. ZXV_E_FORMAT on anything
 * else (exponents, spaces, "NaN"), ZXV_E_RANGE if it does not fit. */
ZXV_API int32_t zxv_rat_parse(const char *text, int64_t *out_num, int64_t *out_den);

/* Render exactly (R4): a terminating decimal when one exists ("12.75"),
 * otherwise "n/d" ("1/3"). Never rounds. */
ZXV_API int32_t zxv_rat_format(int64_t num, int64_t den, char *buf, size_t cap, size_t *out_len);

/* THE FLOAT BOUNDARY. `bits` is the raw IEEE-754 binary64 encoding of a
 * host double (Java Double.doubleToRawLongBits, C# BitConverter.
 * DoubleToInt64Bits, Python struct.pack('<d'), ...). Every finite double
 * IS an exact rational m * 2^e; this returns that rational, so 0.1 maps
 * to 3602879701896397/36028797018963968, never to 1/10: the drift is made
 * visible instead of hidden. NaN and infinities: ZXV_E_FLOAT. A value whose
 * exact form needs more than 63 bits (e.g. 2^-80): ZXV_E_RANGE.
 * SDKs never call this implicitly; it backs an explicit, named API. */
ZXV_API int32_t zxv_rat_from_ieee754(uint64_t bits, int64_t *out_num, int64_t *out_den);

/* Round a rational to Q16.16 fixed point (round half away from zero).
 * ZXV_E_RANGE outside int32. Used for engine parameters (temperature). */
ZXV_API int32_t zxv_rat_to_q16(int64_t num, int64_t den, int32_t *out_q16);

/* ===== Paraconsistent four-valued logic (kernel/src/lpres, K3) =====
 * Values are the kernel's lpres_state_t, fixed forever. "Both" is a held
 * contradiction, not an error; SDKs must never collapse it to a boolean. */
#define ZXV_LP_NEITHER 0 /* S0: unknown, unattested, pending */
#define ZXV_LP_TRUE    1 /* S+: attested present and valid   */
#define ZXV_LP_FALSE   2 /* S-: attested absent or invalid   */
#define ZXV_LP_BOTH    3 /* contradictory evidence, held      */

ZXV_API int32_t zxv_lp_not(int32_t a, int32_t *out);
ZXV_API int32_t zxv_lp_and(int32_t a, int32_t b, int32_t *out);
ZXV_API int32_t zxv_lp_or(int32_t a, int32_t b, int32_t *out);
/* From evidence counts: pos > 0 and neg > 0 -> BOTH, only pos -> TRUE,
 * only neg -> FALSE, none -> NEITHER. */
ZXV_API int32_t zxv_lp_from_evidence(uint32_t positive, uint32_t negative, int32_t *out);
/* Static name ("BOTH"); "INVALID" for an unknown value. */
ZXV_API const char *zxv_lp_name(int32_t value);

/* ===== Capabilities (the sb_lang_context_t of kernel/src/sdk_bridge) =====
 * A zxv_cap carries what the kernel's language context carries: a process
 * id, a 21-octet (168-bit) identity, a capability set and an attestation
 * state. Gated calls verify, every time: the handle is live (not revoked,
 * nor any ancestor), the needed bit is set, and the attestation is TRUE.
 * A cap can only be narrowed (zxv_cap_attenuate), never widened.
 * HONEST LIMIT: inside one process this is least-privilege discipline,
 * not a sandbox; code in the same address space can create its own cap.
 * The process boundary (zxvd over IPC) is where it becomes enforcement.
 * (zxv_cap is declared with the other opaque handles above.) */

#define ZXV_CAP_LEDGER_READ   (UINT64_C(1) << 0)  /* read balances, journals    */
#define ZXV_CAP_LEDGER_POST   (UINT64_C(1) << 1)  /* SB_CAP_CAPITAL_TRANSFER     */
#define ZXV_CAP_CRYPTO_SIGN   (UINT64_C(1) << 2)  /* SB_CAP_CRYPTO_SIGN          */
#define ZXV_CAP_CRYPTO_KEM    (UINT64_C(1) << 3)  /* SB_CAP_CRYPTO_KEM           */
#define ZXV_CAP_AI_INFERENCE  (UINT64_C(1) << 4)  /* SB_CAP_AI_INFERENCE         */
#define ZXV_CAP_BUDGET        (UINT64_C(1) << 5)  /* SB_CAP_COMPUTE_SCHEDULE     */
#define ZXV_CAP_PHASE         (UINT64_C(1) << 6)  /* admit / defer / advance     */
#define ZXV_CAP_ATTEST        (UINT64_C(1) << 7)  /* SB_CAP_IDENTITY_CREDENTIAL  */
#define ZXV_CAP_SWARM         (UINT64_C(1) << 8)  /* swarm economy               */
#define ZXV_CAP_EVM           (UINT64_C(1) << 9)  /* state-channel encoding      */
#define ZXV_CAP_ALL           ((UINT64_C(1) << 10) - 1)

#define ZXV_IDENTITY_BYTES 21

/* Create a root capability for a context. identity may be NULL (zeros).
 * Unknown bits in `caps` are ZXV_E_ARG. Attestation starts TRUE. */
ZXV_API int32_t zxv_cap_create(zxv_ctx *ctx, uint32_t process_id, const uint8_t *identity,
                               uint64_t caps, zxv_cap **out_cap);
/* A child holding (parent caps & keep). The parent must outlive it.
 * Revocation propagates: revoking the parent (or any ancestor) kills the
 * child. Attestation does NOT propagate: the child starts with a COPY of
 * the parent's attestation at attenuation time, and a later
 * zxv_cap_set_attestation on the parent does not change existing children
 * (set each one, or revoke the parent to stop them all). */
ZXV_API int32_t zxv_cap_attenuate(const zxv_cap *parent, uint64_t keep, zxv_cap **out_cap);
/* Permanently revoke this cap and, through it, every descendant. */
ZXV_API int32_t zxv_cap_revoke(zxv_cap *cap);
/* Set the attestation (ZXV_LP_*). Anything but TRUE blocks gated calls;
 * BOTH reports "contradictory attestation". */
ZXV_API int32_t zxv_cap_set_attestation(zxv_cap *cap, int32_t lp_state);
ZXV_API int32_t zxv_cap_info(const zxv_cap *cap, uint64_t *out_caps, uint32_t *out_process_id,
                             int32_t *out_attestation, int32_t *out_revoked);
/* ZXV_OK if `cap` could make a call needing `needed`, else ZXV_E_CAP. */
ZXV_API int32_t zxv_cap_check(const zxv_cap *cap, uint64_t needed);
ZXV_API void zxv_cap_destroy(zxv_cap *cap);

/* Capability-checked ledger calls: the same semantics as zxv_ledger_post /
 * zxv_ledger_balance, with the context taken from the cap. SDKs use these. */
ZXV_API int32_t zxv_ledger_post_cap(const zxv_cap *cap, uint32_t from_account, uint32_t to_account,
                                    int64_t amount_minor, const char *ccy, const char *reference,
                                    uint64_t *out_entry_id);
ZXV_API int32_t zxv_ledger_balance_cap(const zxv_cap *cap, uint32_t account,
                                       int64_t *out_debit_minor, int64_t *out_credit_minor,
                                       int64_t *out_equity_minor);
/* zxv_ledger_check through a cap. On ZXV_E_INVARIANT the context is
 * poisoned (see zxv_ctx_is_poisoned). */
ZXV_API int32_t zxv_ledger_check_cap(const zxv_cap *cap);

/* ABI 1.2: the rest of the context surface through a capability, with the
 * semantics of the zxv_ctx forms (which are now deprecated). */
/* LEDGER_POST: */
ZXV_API int32_t zxv_ccy_enable_cap(const zxv_cap *cap, const char *alpha);
ZXV_API int32_t zxv_ccy_enable_with_minor_cap(const zxv_cap *cap, const char *alpha,
                                              uint8_t minor_units);
ZXV_API int32_t zxv_ccy_register_private_cap(const zxv_cap *cap, const char *code,
                                             uint8_t minor_units);
ZXV_API int32_t zxv_ledger_open_account_cap(const zxv_cap *cap, const char *name, const char *ccy,
                                            int32_t kind, uint32_t *out_account_id);
/* LEDGER_READ: */
ZXV_API int32_t zxv_ledger_account_ccy_cap(const zxv_cap *cap, uint32_t account, char *buf,
                                           size_t cap_len, size_t *out_len);
ZXV_API int32_t zxv_ledger_account_name_cap(const zxv_cap *cap, uint32_t account, char *buf,
                                            size_t cap_len, size_t *out_len);
ZXV_API int32_t zxv_ledger_account_kind_cap(const zxv_cap *cap, uint32_t account,
                                            int32_t *out_kind);
ZXV_API int32_t zxv_ledger_account_count_cap(const zxv_cap *cap, uint32_t *out_count);
ZXV_API int32_t zxv_ledger_entry_count_cap(const zxv_cap *cap, uint32_t account,
                                           uint32_t *out_count);
ZXV_API int32_t zxv_ledger_entry_at_cap(const zxv_cap *cap, uint32_t account, uint32_t index,
                                        uint64_t *out_entry_id, int64_t *out_signed_minor,
                                        uint32_t *out_counterparty, char *ref_buf, size_t ref_cap,
                                        size_t *out_ref_len);

/* ===== Attestation registry (kernel/src/lpres) ===== */
typedef struct zxv_lpres zxv_lpres;

/* zxv_lpres_create is DEPRECATED since ABI 1.2 (no capability). */
ZXV_API int32_t zxv_lpres_create(zxv_ctx *ctx, zxv_lpres **out_reg);
/* ABI 1.2: create through a cap (needs ATTEST); the registry belongs to
 * the cap's context. */
ZXV_API int32_t zxv_lpres_create_cap(const zxv_cap *cap, zxv_lpres **out_reg);
ZXV_API void zxv_lpres_destroy(zxv_lpres *reg);
/* name, attester <= 31 bytes; digest <= 32 bytes (may be NULL/0). subject is
 * the kernel lpres_subject_type_t (0 SOURCE .. 14 FIRMWARE). Needs ATTEST. */
ZXV_API int32_t zxv_lpres_attest(const zxv_cap *cap, zxv_lpres *reg, const char *name,
                                 int32_t subject, int32_t lp_state, const char *attester,
                                 const uint8_t *digest, uint32_t digest_len, uint32_t *out_index);
/* Add one piece of evidence; the state follows zxv_lp_from_evidence. */
ZXV_API int32_t zxv_lpres_evidence(const zxv_cap *cap, zxv_lpres *reg, uint32_t index,
                                   int32_t positive);
ZXV_API int32_t zxv_lpres_find(zxv_lpres *reg, const char *name, uint32_t *out_index);
ZXV_API int32_t zxv_lpres_state(zxv_lpres *reg, uint32_t index, int32_t *out_lp_state,
                                uint32_t *out_positive, uint32_t *out_negative);
/* 1 if every attestation is certain (TRUE or FALSE): no BOTH, no NEITHER.
 * Actuator authority requires a clear gate. */
ZXV_API int32_t zxv_lpres_gate(zxv_lpres *reg, int32_t *out_clear);

/* ===== Rational resource budgets (kernel/src/rmag, K2) ===== */
typedef struct zxv_rmag zxv_rmag;

/* rmag_resource_t, fixed. */
#define ZXV_RES_COMPUTE_CYCLES 0
#define ZXV_RES_MEMORY         1
#define ZXV_RES_BANDWIDTH      2
#define ZXV_RES_ENERGY         3
#define ZXV_RES_STORAGE        4
#define ZXV_RES_TOKENS         12 /* RMAG_RES_EVENT_COUNT: one model token = one event */

/* Listener events, delivered synchronously on the thread whose call caused
 * them, after the budget's lock is released (so the listener may call
 * back into the library). The calling thread's zxv_last_error detail is
 * saved around the listener and restored (ABI 1.2), so a listener that
 * calls the library cannot change the detail the outer call reports. */
#define ZXV_RMAG_EV_DENIED      1 /* a consume was refused (ZXV_E_BUDGET)     */
#define ZXV_RMAG_EV_INVALID     2 /* a consume/release was malformed         */
#define ZXV_RMAG_EV_REPLENISHED 3 /* a release or new limit freed budget     */
typedef void (*zxv_rmag_listener)(void *user, uint32_t budget, int32_t event);

/* zxv_rmag_create is DEPRECATED since ABI 1.2 (no capability). */
ZXV_API int32_t zxv_rmag_create(zxv_ctx *ctx, zxv_rmag **out_rmag);
/* ABI 1.2: create through a cap (needs BUDGET). */
ZXV_API int32_t zxv_rmag_create_cap(const zxv_cap *cap, zxv_rmag **out_rmag);
ZXV_API void zxv_rmag_destroy(zxv_rmag *rmag);
/* A budget with an exact rational limit (>= 0). enforce = 0 only counts. */
ZXV_API int32_t zxv_rmag_budget(const zxv_cap *cap, zxv_rmag *rmag, const char *name,
                                int32_t resource, int64_t limit_num, int64_t limit_den,
                                int32_t enforce, uint32_t *out_budget);
/* Consume an amount (> 0). ZXV_E_BUDGET if it would pass an enforced limit
 * (nothing is consumed); ZXV_E_RMAG_INVALID for a negative or overflowing
 * amount. */
ZXV_API int32_t zxv_rmag_consume(const zxv_cap *cap, zxv_rmag *rmag, uint32_t budget,
                                 int64_t num, int64_t den);
/* Give back (clamped at zero consumed). Fires REPLENISHED. */
ZXV_API int32_t zxv_rmag_release(const zxv_cap *cap, zxv_rmag *rmag, uint32_t budget,
                                 int64_t num, int64_t den);
/* Replace the limit. Fires REPLENISHED when it grows. */
ZXV_API int32_t zxv_rmag_set_limit(const zxv_cap *cap, zxv_rmag *rmag, uint32_t budget,
                                   int64_t num, int64_t den);
/* Remaining = limit - consumed (exact), and consumed. Any out may be NULL.
 * *out_epoch counts REPLENISHED events, so an async waiter can tell that
 * something changed since it last looked. */
ZXV_API int32_t zxv_rmag_status(zxv_rmag *rmag, uint32_t budget, int64_t *out_rem_num,
                                int64_t *out_rem_den, int64_t *out_used_num,
                                int64_t *out_used_den, uint64_t *out_epoch);
/* One listener per zxv_rmag (NULL removes it). When this returns (ABI
 * 1.2), no call of the PREVIOUS listener is running on any thread or will
 * start, so the host may free the old `user` at once: it waits for
 * deliveries already in flight on other threads. Called from inside a
 * listener of the same zxv_rmag it cannot wait for itself; it then only
 * swaps the listener (other threads' in-flight calls may still finish). */
ZXV_API int32_t zxv_rmag_listen(zxv_rmag *rmag, zxv_rmag_listener fn, void *user);

/* ===== Phase coordinator ticks (kernel/src/phase_coord, K6) =====
 * ZXV time is a logical tick count, not wall-clock time. One harmonic
 * fundamental is ZXV_PHASE_CYCLE ticks (27,720 = lcm(1..11) * 1: every
 * harmonic 1..11 lands on a whole tick). A host event loop advances the
 * clock (manually, or from a timer at a chosen tick rate) and awaits
 * deferred S0 events, which clear when their tick is reached. */
typedef struct zxv_phase zxv_phase;

#define ZXV_PHASE_CYCLE 27720u

#define ZXV_ADMIT    0 /* PC_DECISION_ADMIT: token issued      */
#define ZXV_VETO     1 /* PC_DECISION_VETO: rejected           */
#define ZXV_DEFER    2 /* PC_DECISION_DEFER: parked in S0      */
#define ZXV_RETRY    3 /* PC_DECISION_RETRY                    */

#define ZXV_STEP_REQUIRES_HEALTH 0x1u
#define ZXV_STEP_HARDWARE        0x2u
#define ZXV_STEP_S0_PENDING      0x4u

#define ZXV_EV_PENDING   0 /* S0: waiting for its tick */
#define ZXV_EV_CLEARED   1
#define ZXV_EV_CANCELLED 2

#define ZXV_HEALTH_UNKNOWN   0
#define ZXV_HEALTH_HEALTHY   1
#define ZXV_HEALTH_DEGRADED  2
#define ZXV_HEALTH_UNHEALTHY 3

/* zxv_phase_create is DEPRECATED since ABI 1.2 (no capability). */
ZXV_API int32_t zxv_phase_create(zxv_ctx *ctx, zxv_phase **out_phase);
/* ABI 1.2: create through a cap (needs PHASE). */
ZXV_API int32_t zxv_phase_create_cap(const zxv_cap *cap, zxv_phase **out_phase);
ZXV_API void zxv_phase_destroy(zxv_phase *phase);
ZXV_API int32_t zxv_phase_now(zxv_phase *phase, uint64_t *out_tick);
/* Advance by `delta` ticks; deferred events due at or before the new tick
 * become CLEARED. *out_cleared (may be NULL) = how many cleared now. */
ZXV_API int32_t zxv_phase_advance(const zxv_cap *cap, zxv_phase *phase, uint64_t delta,
                                  uint64_t *out_now, uint32_t *out_cleared);
/* Ask the K6 coordinator to admit a step of phase `phase_id` (1..13).
 * *out_token is non-zero only on ZXV_ADMIT. */
ZXV_API int32_t zxv_phase_admit(const zxv_cap *cap, zxv_phase *phase, uint32_t phase_id,
                                uint32_t step_id, uint32_t flags, int32_t *out_decision,
                                uint64_t *out_token);
ZXV_API int32_t zxv_phase_set_health(const zxv_cap *cap, zxv_phase *phase, uint32_t phase_id,
                                     int32_t health);
/* Park an S0 event until absolute tick `due_tick` (> now). Event ids come
 * from a 64-bit counter that never repeats within a zxv_phase, so a stale
 * id can never name or cancel a newer event. A finished event's table slot
 * is recycled once 4096 newer ids exist; its id then gives
 * ZXV_E_NOT_FOUND. */
ZXV_API int32_t zxv_phase_defer(const zxv_cap *cap, zxv_phase *phase, uint64_t due_tick,
                                uint64_t *out_event);
ZXV_API int32_t zxv_phase_event(zxv_phase *phase, uint64_t event, int32_t *out_state,
                                uint64_t *out_due_tick);
ZXV_API int32_t zxv_phase_cancel(const zxv_cap *cap, zxv_phase *phase, uint64_t event);
/* Earliest due tick of a PENDING event; ZXV_E_NOT_FOUND if none. */
ZXV_API int32_t zxv_phase_next_due(zxv_phase *phase, uint64_t *out_tick);

/* ===== UBH-168 harmonic wire (kernel/src/harmonic/zt_harmonic_wire.h) =====
 * 21 octets: tag, then five 32-bit words in fixed L-B-L-B-L byte order:
 * W0 little-endian (S+), W1 big-endian (S-), W2 LE (S+), W3 BE (S-),
 * W4 LE (S+). The octets are identical on every host. */
#define ZXV_WIRE_FRAME_BYTES 21u

#define ZXV_TRUTH_UNKNOWN 0
#define ZXV_TRUTH_TRUE    1
#define ZXV_TRUTH_FALSE   2
#define ZXV_TRUTH_NEUTRAL 3
#define ZXV_TRUTH_GLUT    4 /* contradiction held */
#define ZXV_TRUTH_PARADOX 5 /* persistent GLUT    */

/* tag = 100*sync + 10*shell + trunk; trunk, shell 0..9, sync 0..1, so a
 * valid tag is 0..199. zxv_wire_pack refuses a tag above 199 with
 * ZXV_E_ARG (ABI 1.2; 1.1 framed it anyway). */
ZXV_API int32_t zxv_wire_tag(uint32_t trunk, uint32_t shell, uint32_t sync, uint8_t *out_tag);
ZXV_API int32_t zxv_wire_pack(uint8_t tag, const uint32_t s_plus[3], const uint32_t s_minus[2],
                              uint8_t out_frame[21]);
ZXV_API int32_t zxv_wire_unpack(const uint8_t frame[21], uint8_t *out_tag, uint32_t out_s_plus[3],
                                uint32_t out_s_minus[2]);
/* The W5 correlation resolver (words read as signed Q16.16). */
ZXV_API int32_t zxv_wire_resolve(const uint8_t frame[21], int32_t *out_truth);
/* The idle pilot frame of a trunk (never all zeros). */
ZXV_API int32_t zxv_wire_idle(uint32_t trunk, uint8_t out_frame[21]);

/* ===== Swarm token economy (kernel/src/swarm: budget + emotion) =====
 * Each cycle T tokens are split across the swarm's Fibonacci levels (the
 * real axis) and, by the mood's charge, an imaginary (emotional) share is
 * split by each model's own feeling. Feelings are emoji code points from
 * the kernel palette with intensity 0..5 (charge 0,1,2,3,5,8). All exact
 * integers. */
typedef struct zxv_swarm zxv_swarm;

/* zxv_swarm_create is DEPRECATED since ABI 1.2 (no capability). */
ZXV_API int32_t zxv_swarm_create(zxv_ctx *ctx, uint32_t num_levels, uint64_t tokens_per_cycle,
                                 zxv_swarm **out_swarm);
/* ABI 1.2: create through a cap (needs SWARM). */
ZXV_API int32_t zxv_swarm_create_cap(const zxv_cap *cap, uint32_t num_levels,
                                     uint64_t tokens_per_cycle, zxv_swarm **out_swarm);
ZXV_API void zxv_swarm_destroy(zxv_swarm *swarm);
/* Level d (0-based) holds at most F(d+2) models. ZXV_E_CAPACITY when full. */
ZXV_API int32_t zxv_swarm_register(const zxv_cap *cap, zxv_swarm *swarm, uint32_t model_id,
                                   uint32_t level);
ZXV_API int32_t zxv_swarm_set_active(const zxv_cap *cap, zxv_swarm *swarm, uint32_t model_id,
                                     int32_t active);
ZXV_API int32_t zxv_swarm_set_rate(const zxv_cap *cap, zxv_swarm *swarm,
                                   uint64_t tokens_per_cycle);
/* codepoint: an emoji of the palette (e.g. 0x1F914); ZXV_E_NOT_FOUND else. */
ZXV_API int32_t zxv_swarm_set_mood(const zxv_cap *cap, zxv_swarm *swarm, uint32_t codepoint,
                                   uint32_t intensity);
ZXV_API int32_t zxv_swarm_set_feeling(const zxv_cap *cap, zxv_swarm *swarm, uint32_t model_id,
                                      uint32_t codepoint, uint32_t intensity);
ZXV_API int32_t zxv_swarm_begin_cycle(const zxv_cap *cap, zxv_swarm *swarm, uint64_t *out_cycle);
/* Grant up to `requested` tokens. ZXV_OK with *out_granted = requested,
 * or ZXV_E_BUDGET with *out_granted = what was left and granted. This out
 * parameter is written on ZXV_E_BUDGET too (an exception to R3): the
 * partial grant IS consumed, so read it before treating BUDGET as a
 * failure. On any other status it is not written. */
ZXV_API int32_t zxv_swarm_consume(const zxv_cap *cap, zxv_swarm *swarm, uint32_t model_id,
                                  uint64_t requested, uint64_t *out_granted);
/* This cycle: allotted (real + imaginary), its imaginary part, used. */
ZXV_API int32_t zxv_swarm_allotment(zxv_swarm *swarm, uint32_t model_id, uint64_t *out_allotted,
                                    uint64_t *out_imaginary, uint64_t *out_used);
/* Close the cycle; *out_expired = unused tokens (they never carry over). */
ZXV_API int32_t zxv_swarm_end_cycle(const zxv_cap *cap, zxv_swarm *swarm, uint64_t *out_expired);
/* Pure: capacity of level d, and an emotion's behaviour profile. */
ZXV_API int32_t zxv_swarm_level_capacity(uint32_t level, uint32_t *out_capacity);
ZXV_API int32_t zxv_swarm_emotion_profile(uint32_t codepoint, int32_t *out_valence,
                                          uint32_t *out_temperature_milli,
                                          uint32_t *out_explore_permille,
                                          uint32_t *out_verify_passes, uint32_t *out_priority);

/* ===== Hashes and secure memory ===== */
ZXV_API int32_t zxv_sha3_256(const uint8_t *data, size_t len, uint8_t out[32]);
ZXV_API int32_t zxv_shake256(const uint8_t *data, size_t len, uint8_t *out, size_t out_len);
/* Ethereum's Keccak-256 (pre-FIPS padding 0x01), for the EVM adapter. */
ZXV_API int32_t zxv_keccak256(const uint8_t *data, size_t len, uint8_t out[32]);
/* Fill with bytes from the OS CSPRNG: getrandom (Linux), arc4random_buf
 * (macOS, BSD), BCryptGenRandom (Windows; link bcrypt), getentropy (WASI,
 * over random_get). ZXV_E_NOTIMPL on a platform with none. */
ZXV_API int32_t zxv_random(uint8_t *buf, size_t len);
/* Zero memory in a way the compiler may not remove. SDKs call it on every
 * buffer that held a secret (their off-heap key storage). */
ZXV_API int32_t zxv_secure_zero(void *buf, size_t len);

/* ===== Post-quantum and classical signatures / KEM (kernel/src/pqsec,
 * kernel/third_party/ed25519) =====
 * Secret keys go in caller buffers; every internal copy is wiped before
 * return. Calls are serialized internally (the vendored ML-DSA keygen is
 * not re-entrant). NOT FIPS 140-3 validated; see sdk/docs/SECURITY.md. */
#define ZXV_MLKEM1024_EK_BYTES 1568u
#define ZXV_MLKEM1024_DK_BYTES 3168u
#define ZXV_MLKEM1024_CT_BYTES 1568u
#define ZXV_MLKEM_SS_BYTES     32u
#define ZXV_MLDSA87_PK_BYTES   2592u
#define ZXV_MLDSA87_SK_BYTES   4896u
#define ZXV_MLDSA87_SIG_BYTES  4627u
#define ZXV_MLDSA_CTX_MAX      255u
#define ZXV_ED25519_PK_BYTES   32u
#define ZXV_ED25519_SK_BYTES   32u /* the RFC 8032 private key (seed) */
#define ZXV_ED25519_SIG_BYTES  64u

ZXV_API int32_t zxv_mlkem1024_keygen(const zxv_cap *cap, uint8_t ek[1568], uint8_t dk[3168]);
/* Deterministic from d and z (FIPS 203 ML-KEM.KeyGen_internal), for KATs. */
ZXV_API int32_t zxv_mlkem1024_keygen_derand(const zxv_cap *cap, const uint8_t d[32],
                                            const uint8_t z[32], uint8_t ek[1568],
                                            uint8_t dk[3168]);
/* ZXV_E_FORMAT if ek fails the FIPS 203 modulus check. */
ZXV_API int32_t zxv_mlkem1024_encaps(const zxv_cap *cap, const uint8_t ek[1568], uint8_t ct[1568],
                                     uint8_t ss[32]);
ZXV_API int32_t zxv_mlkem1024_encaps_derand(const zxv_cap *cap, const uint8_t ek[1568],
                                            const uint8_t m[32], uint8_t ct[1568],
                                            uint8_t ss[32]);
ZXV_API int32_t zxv_mlkem1024_decaps(const zxv_cap *cap, const uint8_t dk[3168],
                                     const uint8_t ct[1568], uint8_t ss[32]);

ZXV_API int32_t zxv_mldsa87_keygen(const zxv_cap *cap, uint8_t pk[2592], uint8_t sk[4896]);
ZXV_API int32_t zxv_mldsa87_keygen_seed(const zxv_cap *cap, const uint8_t seed[32],
                                        uint8_t pk[2592], uint8_t sk[4896]);
/* Hedged signing (fresh randomness). ctx: FIPS 204 context string, may be
 * NULL/0, at most 255 bytes. */
ZXV_API int32_t zxv_mldsa87_sign(const zxv_cap *cap, const uint8_t sk[4896], const uint8_t *msg,
                                 size_t msg_len, const uint8_t *ctx, size_t ctx_len,
                                 uint8_t sig[4627]);
/* Deterministic signing (same input, same signature), for KATs. */
ZXV_API int32_t zxv_mldsa87_sign_deterministic(const zxv_cap *cap, const uint8_t sk[4896],
                                               const uint8_t *msg, size_t msg_len,
                                               const uint8_t *ctx, size_t ctx_len,
                                               uint8_t sig[4627]);
/* ZXV_OK if valid, ZXV_E_VERIFY if not. */
ZXV_API int32_t zxv_mldsa87_verify(const zxv_cap *cap, const uint8_t pk[2592], const uint8_t *msg,
                                   size_t msg_len, const uint8_t *ctx, size_t ctx_len,
                                   const uint8_t sig[4627]);

ZXV_API int32_t zxv_ed25519_keygen(const zxv_cap *cap, uint8_t pk[32], uint8_t sk[32]);
ZXV_API int32_t zxv_ed25519_public(const zxv_cap *cap, const uint8_t sk[32], uint8_t pk[32]);
ZXV_API int32_t zxv_ed25519_sign(const zxv_cap *cap, const uint8_t sk[32], const uint8_t *msg,
                                 size_t msg_len, uint8_t sig[64]);
ZXV_API int32_t zxv_ed25519_verify(const zxv_cap *cap, const uint8_t pk[32], const uint8_t *msg,
                                   size_t msg_len, const uint8_t sig[64]);

/* ===== Integer inference engine (kernel/src/tensor) =====
 * GGUF models (qwen2, qwen3, llama; F32/F16/BF16/Q8_0/Q4_0/Q5_x/Q4_K/Q6_K)
 * run in integer arithmetic only, bit-identical on every machine. A model
 * is read-only after loading and may be shared by sessions on any threads;
 * a session (KV cache) is used by one thread at a time. */
typedef struct zxv_model zxv_model;
typedef struct zxv_session zxv_session;
typedef struct zxv_lora zxv_lora;

#define ZXV_KV_Q16 0 /* exact KV cache, 4 bytes per value       */
#define ZXV_KV_Q8  1 /* 8-bit KV cache, ~1.16 bytes per value   */

#define ZXV_MODEL_ARCH        1 /* 1 qwen2, 2 qwen3, 3 llama */
#define ZXV_MODEL_N_VOCAB     2
#define ZXV_MODEL_N_CTX_TRAIN 3
#define ZXV_MODEL_N_LAYER     4
#define ZXV_MODEL_N_EMBD      5
#define ZXV_MODEL_N_HEAD      6
#define ZXV_MODEL_N_HEAD_KV   7
#define ZXV_MODEL_EOS         8

#define ZXV_STOP_EOS      1 /* end-of-sequence token          */
#define ZXV_STOP_MAX      2 /* max_tokens reached             */
#define ZXV_STOP_STR      3 /* reserved                       */
#define ZXV_STOP_CALLBACK 4 /* the token callback returned 0  */
#define ZXV_STOP_CTX      5 /* the context window is full     */
#define ZXV_STOP_BUDGET   6 /* the bound rmag budget ran out  */

/* Called once per generated token with its bytes (UTF-8, possibly a
 * partial character). Return non-zero to continue, 0 to stop. Runs on the
 * thread that called zxv_generate. */
typedef int32_t (*zxv_token_fn)(void *user, int32_t token, const uint8_t *piece, size_t len);

/* Open a GGUF file. tokenizer_path NULL = the model file's own tokenizer. */
ZXV_API int32_t zxv_model_open(const zxv_cap *cap, const char *model_path,
                               const char *tokenizer_path, zxv_model **out_model);
/* The same from memory; the bytes are copied, so the caller may free them. */
ZXV_API int32_t zxv_model_open_memory(const zxv_cap *cap, const uint8_t *model, size_t model_len,
                                      const uint8_t *tokenizer, size_t tokenizer_len,
                                      zxv_model **out_model);
ZXV_API void zxv_model_destroy(zxv_model *model);
ZXV_API int32_t zxv_model_info(const zxv_model *model, int32_t key, int64_t *out_value);

/* Token ids of UTF-8 text, two-call on *out_count. Unlike R4 text, `ids`
 * is a plain array with NO terminator: *out_count is always the exact
 * number of ids, and cap >= *out_count suffices (no +1). A NULL ids or
 * cap < count gives ZXV_E_BUFFER with *out_count set. */
ZXV_API int32_t zxv_tokenize(const zxv_model *model, const uint8_t *text, size_t len,
                             int32_t parse_special, int32_t *ids, size_t cap, size_t *out_count);
ZXV_API int32_t zxv_detokenize(const zxv_model *model, const int32_t *ids, size_t count,
                               char *buf, size_t cap, size_t *out_len);

/* n_ctx 0 = min(n_ctx_train, 4096). */
ZXV_API int32_t zxv_session_create(const zxv_model *model, uint32_t n_ctx, int32_t kv_type,
                                   zxv_session **out_session);
ZXV_API void zxv_session_destroy(zxv_session *session);
/* Forget the conversation (empty KV cache). */
ZXV_API int32_t zxv_session_reset(zxv_session *session);
/* Tokens held in the KV cache. After zxv_generate on a fresh (or reset)
 * session with P prompt tokens and N >= 1 generated tokens this is
 * P + N - 1: the last generated token is returned but not yet fed back
 * into the cache (the next zxv_generate call starts after it). */
ZXV_API int32_t zxv_session_position(zxv_session *session, uint32_t *out_pos);
/* Charge this budget one unit per prompt and generated token (rmag NULL
 * unbinds). Prompt tokens are charged up front: if they do not fit,
 * zxv_generate returns ZXV_E_BUDGET and runs nothing.
 * LIFETIME: the session keeps the `rmag` POINTER (no copy, no reference
 * count). The zxv_rmag, and its zxv_ctx, must stay alive until the
 * session is unbound (rmag NULL), rebound or destroyed; destroying a bound
 * rmag first is a use-after-free on the next zxv_generate. zxv_generate
 * refuses a budget of another context than its cap (ZXV_E_CAP). */
ZXV_API int32_t zxv_session_bind_budget(zxv_session *session, zxv_rmag *rmag, uint32_t budget);

/* Generate. Sampling parameters are Q16.16 (temperature_q16 0 = greedy;
 * top_p_q16 0 = off; repeat_penalty_q16 0 = off). The same seed and inputs
 * give the same tokens on every machine. Appends to the session, so a chat
 * continues turn after turn. Any out pointer may be NULL. */
ZXV_API int32_t zxv_generate(const zxv_cap *cap, zxv_session *session, const uint8_t *prompt,
                             size_t prompt_len, uint32_t max_tokens, int32_t temperature_q16,
                             uint32_t top_k, int32_t top_p_q16, int32_t repeat_penalty_q16,
                             uint32_t repeat_last_n, uint64_t seed, zxv_token_fn on_token,
                             void *user, uint32_t *out_n_prompt, uint32_t *out_n_generated,
                             int32_t *out_stop);

/* LoRA adapters: declared for the ABI, NOT implemented by the engine yet
 * (ZXV_FEAT_LORA is never set; these return ZXV_E_NOTIMPL). */
ZXV_API int32_t zxv_lora_load(const zxv_cap *cap, zxv_model *model, const char *path,
                              int32_t scale_q16, zxv_lora **out_lora);
ZXV_API int32_t zxv_lora_apply(const zxv_cap *cap, zxv_session *session, zxv_lora *lora);
ZXV_API void zxv_lora_destroy(zxv_lora *lora);

/* ===== EVM state channels =====
 * Buffers: two-call on *out_len, but BYTES, not text (R4 differs): there
 * is no NUL, *out_len is exactly (6 + 2 * count) * 32, and cap_bytes >=
 * *out_len suffices (no +1). NULL buf or a smaller cap_bytes gives
 * ZXV_E_BUFFER with *out_len set and nothing else written; `digest` is
 * written only on ZXV_OK. Size it once: (6 + 2n) * 32 is known up front.
 *
 * The ledger's exact integer balances, encoded for a Solidity state-channel
 * contract (sdk/solidity/ZxvStateChannel.sol) as
 *   abi.encode(bytes32 channelId, uint256 nonce, address[] parties,
 *              int256[] balances)
 * and digest = keccak256(encoding). The parties sign the digest with their
 * own Ethereum keys (EIP-191) in the host wallet; ZXV has no secp256k1. */
ZXV_API int32_t zxv_evm_encode_state(const zxv_cap *cap, const uint8_t channel_id[32],
                                     uint64_t nonce, const uint8_t *addresses,
                                     const int64_t *balances, uint32_t count, uint8_t *buf,
                                     size_t cap_bytes, size_t *out_len, uint8_t digest[32]);
/* The same, reading each party's EQUITY (888) balance from ledger accounts
 * (all in `ccy`). Needs EVM and LEDGER_READ. */
ZXV_API int32_t zxv_evm_ledger_state(const zxv_cap *cap, const uint8_t channel_id[32],
                                     uint64_t nonce, const char *ccy, const uint32_t *accounts,
                                     const uint8_t *addresses, uint32_t count, uint8_t *buf,
                                     size_t cap_bytes, size_t *out_len, uint8_t digest[32]);

/* ===================================================================== */
/* ABI 1.3: the 64-state hexagram tag (kernel/src/lpres/lpres_hex.h)     */
/* ===================================================================== */
/* A tag is one byte 0..63 holding three 2-bit fields, read like an I Ching
 * hexagram from the top line down:
 *
 *   bits 5-4  BASE      the four-valued state of the work
 *   bits 3-2  POLARITY  how its evidence is moving
 *   bits 1-0  PHASE     the work's lifecycle, a Gray cycle
 *
 * tag = (base << 4) | (polarity << 2) | phase.
 *
 * THE BASE IS NOT NUMBERED LIKE ZXV_LP_*. The hexagram codes are the line
 * patterns True 11, False 00, Both 10, Neither 01, so
 *
 *   ZXV_HEX_BASE_FALSE 0 = ZXV_LP_FALSE 2      ZXV_HEX_BASE_BOTH 2 = ZXV_LP_BOTH 3
 *   ZXV_HEX_BASE_NEITHER 1 = ZXV_LP_NEITHER 0  ZXV_HEX_BASE_TRUE 3 = ZXV_LP_TRUE 1
 *
 * Convert only with zxv_hex_from_lp / zxv_hex_to_lp; never cast.
 *
 * Rules (the kernel's X1-X6):
 *   ONE TABLE. zxv_hex_action is one lookup in a fixed 64-entry table:
 *     polarity FORBIDDEN -> ROLLBACK (and quarantine), whatever else;
 *     TRUE -> PROCEED, or WAIT when entropic (a weakening truth);
 *     FALSE -> REFUSE; BOTH -> WAIT when negentropic, else ESCALATE;
 *     NEITHER -> WAIT. The phase never changes the action.
 *   ONE STEP AT A TIME. The phase moves potential -> emerging -> manifest
 *     -> dissolving -> potential (one bit flip per step). zxv_hex_step
 *     refuses a jump with ZXV_E_STATE.
 *   FORBIDDEN IS ABSORBING. No call takes a forbidden tag out of the
 *     forbidden polarity (zxv_hex_set gives ZXV_E_STATE); only a fresh tag
 *     (new work) starts clean.
 *   COMBINING. and / or / not use the ZXV_LP_* tables (zxv_lp_and,
 *     zxv_lp_or, zxv_lp_not) on the base, the worst polarity (forbidden >
 *     entropic > neutral > negentropic) and the least advanced phase
 *     (potential < emerging < manifest < dissolving). not keeps polarity
 *     and phase.
 * All zxv_hex_* functions are pure (no capability, R10) and refuse a tag
 * above 63, or a field above 3, with ZXV_E_ARG. */
#define ZXV_HEX_STATES 64u

#define ZXV_HEX_BASE_FALSE   0 /* 00 */
#define ZXV_HEX_BASE_NEITHER 1 /* 01 */
#define ZXV_HEX_BASE_BOTH    2 /* 10 */
#define ZXV_HEX_BASE_TRUE    3 /* 11 */

#define ZXV_HEX_POL_ENTROPIC    0 /* 00 diverging or stale evidence */
#define ZXV_HEX_POL_NEUTRAL     1 /* 01 steady                      */
#define ZXV_HEX_POL_FORBIDDEN   2 /* 10 a wall: absorbing           */
#define ZXV_HEX_POL_NEGENTROPIC 3 /* 11 converging evidence         */

#define ZXV_HEX_PHASE_POTENTIAL  0 /* 00 */
#define ZXV_HEX_PHASE_EMERGING   1 /* 01 */
#define ZXV_HEX_PHASE_DISSOLVING 2 /* 10 */
#define ZXV_HEX_PHASE_MANIFEST   3 /* 11 */

#define ZXV_HEX_ACT_PROCEED  0
#define ZXV_HEX_ACT_WAIT     1
#define ZXV_HEX_ACT_REFUSE   2
#define ZXV_HEX_ACT_ESCALATE 3
#define ZXV_HEX_ACT_ROLLBACK 4 /* and quarantine (zxv_hexlog_append) */

/* Build a tag from its fields (each 0..3). */
ZXV_API int32_t zxv_hex_pack(uint8_t base, uint8_t polarity, uint8_t phase, uint8_t *out_tag);
/* Split a tag. Any out pointer may be NULL. */
ZXV_API int32_t zxv_hex_unpack(uint8_t tag, uint8_t *out_base, uint8_t *out_polarity,
                               uint8_t *out_phase);
/* What happens next: a ZXV_HEX_ACT_* value. */
ZXV_API int32_t zxv_hex_action(uint8_t tag, int32_t *out_action);
/* The phase after `phase` on the Gray cycle. */
ZXV_API int32_t zxv_hex_phase_next(uint8_t phase, uint8_t *out_phase);
/* Move `tag` to `phase`: the same phase or the next one only, else
 * ZXV_E_STATE. Base and polarity are kept (a forbidden tag stays forbidden
 * but may still move through its lifecycle). */
ZXV_API int32_t zxv_hex_step(uint8_t tag, uint8_t phase, uint8_t *out_tag);
/* Replace base and polarity, keeping the phase. A forbidden tag stays
 * forbidden: asking it for another polarity is ZXV_E_STATE; passing
 * ZXV_HEX_POL_FORBIDDEN updates its base (for the audit record). */
ZXV_API int32_t zxv_hex_set(uint8_t tag, uint8_t base, uint8_t polarity, uint8_t *out_tag);
/* The same tag with polarity FORBIDDEN. */
ZXV_API int32_t zxv_hex_forbid(uint8_t tag, uint8_t *out_tag);
ZXV_API int32_t zxv_hex_and(uint8_t a, uint8_t b, uint8_t *out_tag);
ZXV_API int32_t zxv_hex_or(uint8_t a, uint8_t b, uint8_t *out_tag);
ZXV_API int32_t zxv_hex_not(uint8_t a, uint8_t *out_tag);
/* Polarity from evidence counts before (pos0, neg0) and after (pos1, neg1)
 * and whether the newest evidence is stale (the caller's freshness limit;
 * non-zero = stale). Stale -> ENTROPIC; else the margin |pos - neg|
 * growing -> NEGENTROPIC, shrinking -> ENTROPIC, unchanged -> NEUTRAL.
 * Never FORBIDDEN. */
ZXV_API int32_t zxv_hex_polarity(uint32_t pos0, uint32_t neg0, uint32_t pos1, uint32_t neg1,
                                 int32_t stale, uint8_t *out_polarity);
/* The four-valued bridge: a ZXV_LP_* value <-> a ZXV_HEX_BASE_* code (see
 * the table above). Anything else is ZXV_E_ARG. */
ZXV_API int32_t zxv_hex_from_lp(int32_t lp_state, uint8_t *out_base);
ZXV_API int32_t zxv_hex_to_lp(uint8_t base, int32_t *out_lp_state);
/* Static, never-NULL text "base/polarity/phase", lower case, e.g.
 * "true/negentropic/manifest"; "INVALID" for a tag above 63. */
ZXV_API const char *zxv_hex_name(uint8_t tag);
/* Static name of a ZXV_HEX_ACT_* value ("rollback"); "INVALID" else. */
ZXV_API const char *zxv_hex_action_name(int32_t action);

/* ===== The quarantine log (lpres_hex X6) =====
 * An append-only, hash-chained log of forbidden work: the tag, who
 * submitted it, a 32-byte digest of the work and a caller reason code. It
 * is read for audit, never replayed. Record k's hash is
 *   SHA-256("zxv/lpx-qlog/v1" || seq (u64 LE) || tag || reason ||
 *           submitter (u32 LE) || subject[32] || prev[32])
 * with prev = the previous record's hash (32 zeros for the first), and the
 * head is the last record's hash (32 zeros when empty), so a host can
 * recompute the chain from zxv_hexlog_at. The hash is SHA-256 (FIPS 180-4),
 * as in the kernel, not SHA3-256. Signing the head is the caller's job.
 * Creating and appending need ZXV_CAP_ATTEST: a quarantine record is an
 * attestation about refused work (any work, not only ledger postings).
 * Reading needs no capability. Every call fails closed (ZXV_E_POISONED)
 * once the context halted. A zxv_hexlog is internally serialized. */
typedef struct zxv_hexlog zxv_hexlog;

#define ZXV_HEXLOG_HASH_BYTES    32u
#define ZXV_HEXLOG_SUBJECT_BYTES 32u
#define ZXV_HEXLOG_MAX_RECORDS   65536u

/* A log of at most `capacity` records (1..ZXV_HEXLOG_MAX_RECORDS). Needs
 * ATTEST; the log belongs to the cap's context. */
ZXV_API int32_t zxv_hexlog_create(const zxv_cap *cap, uint32_t capacity, zxv_hexlog **out_log);
ZXV_API void zxv_hexlog_destroy(zxv_hexlog *log);
/* Quarantine one record. Needs ATTEST (and the cap's context must be the
 * log's, else ZXV_E_CAP). Only forbidden tags are quarantined: any other
 * tag is ZXV_E_ARG. A full log is ZXV_E_CAPACITY and records nothing: the
 * caller must then halt (zxv_ctx_halt), because forbidden work it cannot
 * record must not continue. *out_seq (may be NULL) is the record's index. */
ZXV_API int32_t zxv_hexlog_append(const zxv_cap *cap, zxv_hexlog *log, uint8_t tag, uint8_t reason,
                                  uint32_t submitter, const uint8_t subject[32], uint64_t *out_seq);
ZXV_API int32_t zxv_hexlog_count(zxv_hexlog *log, uint32_t *out_count);
/* The chain head (32 zeros when empty). */
ZXV_API int32_t zxv_hexlog_head(zxv_hexlog *log, uint8_t out_head[32]);
/* One record, oldest first. Any out pointer may be NULL. */
ZXV_API int32_t zxv_hexlog_at(zxv_hexlog *log, uint32_t index, uint64_t *out_seq, uint8_t *out_tag,
                              uint8_t *out_reason, uint32_t *out_submitter,
                              uint8_t out_subject[32], uint8_t out_prev[32], uint8_t out_hash[32]);
/* Recompute every link. ZXV_OK, or ZXV_E_INVARIANT if any record or the
 * head no longer matches; that poisons the context (fail closed), since
 * only a memory fault inside this process can alter the log. */
ZXV_API int32_t zxv_hexlog_verify(zxv_hexlog *log);

/* ===================================================================== */
/* ABI 1.4: hardware emulation and hardware updates (kernel/src/hwemu).  */
/*                                                                       */
/* Documented in sdk/docs/HW_EMULATION.md and, for the profile format,   */
/* sdk/docs/HW_PROFILE.md:                                               */
/*   PROFILES  ZHP-1, the "hardware ROM": a canonical description of a  */
/*             machine as a tree of abstract components with integer     */
/*             capability descriptors, takeover state and an opaque      */
/*             private extension. zxv_hw_profile_probe_host describes    */
/*             the machine you are on; zxv_hw_profile_reference gives    */
/*             the public VULPINE-class reference machine R1 that ZXV    */
/*             software targets; zxv_hw_profile_present fills R1 from a  */
/*             host.                                                     */
/*   MATRIX    functions (engine contracts) served by interchangeable    */
/*             implementations: HARD silicon, FABRIC or MICRO regions,   */
/*             a PEER component, a SOFT emulator or a REMOTE machine.    */
/*             An implementation serves only after it reproduces the     */
/*             function's versioned golden vectors bit for bit. Every    */
/*             matrix starts with the standard engines (ZXV_HW_FN_*),    */
/*             each with a SOFT implementation, so every call works on   */
/*             any device. Apps state placement constraints, never an    */
/*             implementation; key-bearing work is always local.         */
/*   REGIONS   hardware updates shipped as software: images signed with  */
/*             ML-DSA-87 AND SLH-DSA-SHAKE-256s (both required), staged  */
/*             in an inactive slot, read back, tested, activated or      */
/*             rolled back. Every step is an event and a record in a     */
/*             hash-chained audit log; failures are quarantined (S0).    */
/* Profiles are plain data and take no capability. Matrix calls need     */
/* ZXV_CAP_HW_MATRIX; region calls and contract packages need            */
/* ZXV_CAP_HW_RECONF; building images and keys needs                     */
/* ZXV_CAP_CRYPTO_SIGN. A matrix or region handle may be used from       */
/* several threads (calls are serialized) and a provider callback may    */
/* call zxv_hw_call on the same matrix for the functions it declared as  */
/* dependencies.                                                         */
/* ===================================================================== */

#define ZXV_FEAT_HW (0x10000000u) /* emulation matrix, ZHP-1, hardware updates */

#define ZXV_CAP_HW_MATRIX (UINT64_C(1) << 10) /* build and call the emulation matrix   */
#define ZXV_CAP_HW_RECONF (UINT64_C(1) << 11) /* load hardware images and contracts    */
/* ZXV_CAP_ALL keeps its 1.1 value forever; this is every bit as of 1.4. */
#define ZXV_CAP_ALL_1_4 ((UINT64_C(1) << 12) - 1)

#define ZXV_E_NOPROVIDER  (-25) /* no proven implementation may serve the call */
#define ZXV_E_DISPUTE     (-26) /* a shadow check found two implementations disagree */
#define ZXV_E_QUARANTINED (-27) /* image failed in hardware: rolled back, logged */
#define ZXV_E_ROLLBACK    (-28) /* image or contract not newer / below the floor */
#define ZXV_E_BLOCKED     (-29) /* image already quarantined; never reloaded   */
#define ZXV_E_TARGET      (-30) /* image is for other hardware                 */
#define ZXV_E_POLICY      (-31) /* refused by the function's placement policy  */

typedef struct zxv_hw_matrix zxv_hw_matrix;   /* functions x implementations */
typedef struct zxv_hw_profile zxv_hw_profile; /* one ZHP-1 machine profile   */
typedef struct zxv_hw_region zxv_hw_region;   /* one reconfigurable region   */

/* ----- Profiles (ZHP-1) ----- */

/* Component classes (kernel/src/hwemu/hwp.h; names in comments). Classes
 * from ZXV_HW_CLASS_PRIVATE up are an implementer's own. */
#define ZXV_HW_CLASS_MACHINE        1  /* device              */
#define ZXV_HW_CLASS_CPU_CLUSTER    2  /* cluster             */
#define ZXV_HW_CLASS_CPU_CORE       3  /* core                */
#define ZXV_HW_CLASS_CACHE          4  /* mem.cache           */
#define ZXV_HW_CLASS_MEMORY         5  /* mem                 */
#define ZXV_HW_CLASS_INTERCONNECT   6  /* interconnect        */
#define ZXV_HW_CLASS_MATRIX_UNIT    7  /* engine.tensor.int   */
#define ZXV_HW_CLASS_VECTOR_UNIT    8  /* engine.vector       */
#define ZXV_HW_CLASS_TERNARY_UNIT   9  /* engine.ternary      */
#define ZXV_HW_CLASS_HASH_UNIT      10 /* engine.hash         */
#define ZXV_HW_CLASS_CRYPTO_UNIT    11 /* engine.crypto       */
#define ZXV_HW_CLASS_CODEC_UNIT     12 /* engine.codec        */
#define ZXV_HW_CLASS_RECONF_FABRIC  13 /* fabric.reconfig     */
#define ZXV_HW_CLASS_STORAGE        14 /* io.storage          */
#define ZXV_HW_CLASS_NETWORK        15 /* io.network          */
#define ZXV_HW_CLASS_RADIO          16 /* radio               */
#define ZXV_HW_CLASS_SENSOR         17 /* sensor              */
#define ZXV_HW_CLASS_ACTUATOR       18 /* actuator            */
#define ZXV_HW_CLASS_CLOCK          19 /* clock               */
#define ZXV_HW_CLASS_POWER          20 /* power               */
#define ZXV_HW_CLASS_SECURE_ELEMENT 21 /* secure.element      */
#define ZXV_HW_CLASS_DISPLAY        22 /* io.display          */
#define ZXV_HW_CLASS_AUDIO          23 /* io.audio            */
#define ZXV_HW_CLASS_WIRE           24 /* io.wire             */
#define ZXV_HW_CLASS_GPU            25 /* engine.gpu          */
#define ZXV_HW_CLASS_PACKAGE        26 /* package             */
#define ZXV_HW_CLASS_LANE           27 /* lane                */
#define ZXV_HW_CLASS_LOGIC_UNIT     28 /* engine.logic64      */
#define ZXV_HW_CLASS_NTT_UNIT       29 /* engine.ntt          */
#define ZXV_HW_CLASS_FRAME_UNIT     30 /* engine.frame.ubh168 */
#define ZXV_HW_CLASS_GCD_UNIT       31 /* engine.gcd          */
#define ZXV_HW_CLASS_MEM_CHANNEL    32 /* mem.channel         */
#define ZXV_HW_CLASS_SECURE_ISLAND  33 /* secure.island       */
#define ZXV_HW_CLASS_IO             34 /* io                  */
#define ZXV_HW_CLASS_PRIVATE        0x80000000u

/* Capability keys: bit 15 critical, bits 14-13 comparison (MIN, EXACT,
 * MASK, INFO), bits 12-0 id; ids 0x1000-0x1FFF are private. The values
 * are the kernel's HWP_CAP_* and never change. */
#define ZXV_HW_CAP_WORD_BITS      0x0001
#define ZXV_HW_CAP_COUNT          0x0002
#define ZXV_HW_CAP_FREQ_KHZ       0x6003
#define ZXV_HW_CAP_BYTES          0x0004
#define ZXV_HW_CAP_LINE_BYTES     0x6005
#define ZXV_HW_CAP_ISA            0x2006
#define ZXV_HW_CAP_INT_WIDTHS     0x4007
#define ZXV_HW_CAP_RADIX          0x2008
#define ZXV_HW_CAP_MACS_PER_CYCLE 0x0009
#define ZXV_HW_CAP_SIMD           0x400A
#define ZXV_HW_CAP_TICK_NS        0x600B
#define ZXV_HW_CAP_REGIONS        0x000C
#define ZXV_HW_CAP_BANDWIDTH_MBPS 0x000D
#define ZXV_HW_CAP_LATENCY_NS     0x600E
#define ZXV_HW_CAP_PQ_ALGS        0x400F
#define ZXV_HW_CAP_HASH_ALGS      0x4010
#define ZXV_HW_CAP_WIRE_BITS      0x2011
#define ZXV_HW_CAP_PEAK_OPS_MHZ   0x6012
#define ZXV_HW_CAP_CONTRACT_ID    0x2013 /* the engine contract (ZXV_HW_FN_*) */
#define ZXV_HW_CAP_CONTRACT_VER   0x0014 /* its golden-vector version (MIN)   */
#define ZXV_HW_CAP_KIND           0x6015 /* ZXV_HW_KIND_* serving the node    */
#define ZXV_HW_CAP_CAPACITY_CLASS 0x0016 /* coarse, 0..15                     */
#define ZXV_HW_CAP_PERF_BUCKET    0x0017 /* coarse, 0..15                     */
#define ZXV_HW_CAP_POWER_BUCKET   0x6018 /* ZXV_HW_POWER_* scale              */

#define ZXV_HW_ISA_X86_64  1
#define ZXV_HW_ISA_ARM64   2
#define ZXV_HW_ISA_RISCV64 3
#define ZXV_HW_ISA_WASM32  4

#define ZXV_HW_ROOT 0xFFFF /* parent id of a root node */
#define ZXV_HW_EXT_MAX 256 /* private extension bytes per node */

/* How a target component is served on a host (zxv_hw_profile_pair). */
#define ZXV_HW_NATIVE  0 /* a host component satisfies it               */
#define ZXV_HW_EMULATE 1 /* nothing does: the matrix emulates it         */
#define ZXV_HW_GROUP   2 /* a device or package node: nothing to run     */

ZXV_API int32_t zxv_hw_profile_create(zxv_hw_profile **out);
ZXV_API void zxv_hw_profile_destroy(zxv_hw_profile *p);
/* Describe this machine: CPU cluster (ISA, cores, SIMD), memory, cache
 * line. Only what the OS reports reliably; nothing is guessed, so units
 * that cannot be detected portably (matrix engines, secure enclaves) are
 * left out and will be emulated. */
ZXV_API int32_t zxv_hw_profile_probe_host(zxv_hw_profile **out);
/* A copy of the public VULPINE-class reference machine R1. */
ZXV_API int32_t zxv_hw_profile_reference(zxv_hw_profile **out);
/* The target as served on the host: a copy of target in which every node
 * that runs something carries ZXV_HW_CAP_KIND (ZXV_HW_KIND_HARD where the
 * host has it, ZXV_HW_KIND_SOFT where it is emulated). */
ZXV_API int32_t zxv_hw_profile_present(const zxv_hw_profile *host, const zxv_hw_profile *target,
                                       zxv_hw_profile **out);
/* Parse a canonical ZHP-1 encoding (anything non-canonical: ZXV_E_FORMAT). */
ZXV_API int32_t zxv_hw_profile_decode(const uint8_t *buf, size_t len, zxv_hw_profile **out);
/* R4 buffer rules: ZXV_E_BUFFER with *out_len = needed. */
ZXV_API int32_t zxv_hw_profile_encode(const zxv_hw_profile *p, uint8_t *buf, size_t cap,
                                      size_t *out_len);
/* SHA-256 of the canonical encoding: the profile's identity. */
ZXV_API int32_t zxv_hw_profile_digest(const zxv_hw_profile *p, uint8_t out[32]);
/* Add a node: ids strictly increase, parent first (or ZXV_HW_ROOT). */
ZXV_API int32_t zxv_hw_profile_node_add(zxv_hw_profile *p, uint32_t id, uint32_t parent,
                                        uint32_t cls);
ZXV_API int32_t zxv_hw_profile_cap_set(zxv_hw_profile *p, uint32_t node_id, uint32_t key,
                                       uint64_t value);
/* ZXV_E_NOT_FOUND if the node lacks the cap. */
ZXV_API int32_t zxv_hw_profile_cap_get(const zxv_hw_profile *p, uint32_t node_id, uint32_t key,
                                       uint64_t *out_value);
/* The node's private extension: opaque bytes (at most ZXV_HW_EXT_MAX)
 * that ZXV carries, encodes and digests but never interprets. get: R4
 * buffer rules. */
ZXV_API int32_t zxv_hw_profile_ext_set(zxv_hw_profile *p, uint32_t node_id, const uint8_t *ext,
                                       size_t len);
ZXV_API int32_t zxv_hw_profile_ext_get(const zxv_hw_profile *p, uint32_t node_id, uint8_t *buf,
                                       size_t cap, size_t *out_len);
ZXV_API int32_t zxv_hw_profile_count(const zxv_hw_profile *p, uint32_t *out_nodes);
ZXV_API int32_t zxv_hw_profile_node(const zxv_hw_profile *p, uint32_t index, uint32_t *out_id,
                                    uint32_t *out_parent, uint32_t *out_cls, uint32_t *out_ncaps);
/* Match a target against a host: how many target components run natively
 * and how many must be emulated. */
ZXV_API int32_t zxv_hw_profile_match(const zxv_hw_profile *host, const zxv_hw_profile *target,
                                     uint32_t *out_native, uint32_t *out_emulated);
/* The pairing of target node `index` (in target order): ZXV_HW_NATIVE with
 * the host node id, ZXV_HW_EMULATE with the first unmet cap key (0 if the
 * host has no component of that class), or ZXV_HW_GROUP. */
ZXV_API int32_t zxv_hw_profile_pair(const zxv_hw_profile *host, const zxv_hw_profile *target,
                                    uint32_t index, uint32_t *out_need_id, int32_t *out_kind,
                                    uint32_t *out_have_id, uint32_t *out_unmet_key);

/* ----- The emulation matrix ----- */

/* Implementation kinds, in the order the matrix prefers them at equal
 * cost. */
#define ZXV_HW_KIND_HARD   0 /* silicon built for the function       */
#define ZXV_HW_KIND_FABRIC 1 /* a reconfigurable fabric region       */
#define ZXV_HW_KIND_MICRO  2 /* a microcoded unit or soft core       */
#define ZXV_HW_KIND_PEER   3 /* another local component standing in  */
#define ZXV_HW_KIND_SOFT   4 /* a software emulator                  */
#define ZXV_HW_KIND_REMOTE 5 /* another machine                      */

#define ZXV_HW_RES_OK            0
#define ZXV_HW_RES_FAULTED       1
#define ZXV_HW_RES_RECONFIGURING 2
#define ZXV_HW_RES_OFFLINE       3

/* The resource every matrix starts with: this machine's CPU, where the
 * standard engines' SOFT implementations run. */
#define ZXV_HW_RES_HOST 0xFFFFFFF0u

/* The standard engines: contracts every matrix starts with, each with a
 * SOFT implementation and version-1 golden vectors (formats in
 * HW_EMULATION.md). Integers are little-endian. */
#define ZXV_HW_FN_SHA256  0x5A450001u /* op 0: SHA-256                         */
#define ZXV_HW_FN_KECCAK  0x5A450002u /* op 0/1 SHA3-256/512, op 2/3 SHAKE     */
#define ZXV_HW_FN_MAC     0x5A450003u /* op 0: HMAC-SHA256 (key-bearing)       */
#define ZXV_HW_FN_GCD     0x5A450004u /* op 0: n pairs of u64 -> n gcds        */
#define ZXV_HW_FN_LOGIC64 0x5A450005u /* 64-lane four-valued AND / OR / NOT    */
#define ZXV_HW_FN_TENSOR  0x5A450006u /* op 0: one Q8_0 row dot (zt_dot_raw)   */

/* Placement policy flags (zxv_hw_policy_set). */
#define ZXV_HW_POLICY_KEY_BEARING 0x1u /* always local, never memoized; sticky */
#define ZXV_HW_POLICY_LOCAL_ONLY  0x2u /* no REMOTE implementation            */

/* Implementation status flags (zxv_hw_provider_info). */
#define ZXV_HW_PF_FAULT    0x01u /* its resource is faulted or offline       */
#define ZXV_HW_PF_DISPUTED 0x02u /* a shadow check found it disagreeing      */
#define ZXV_HW_PF_FAILED   0x04u /* it failed the golden vectors             */
#define ZXV_HW_PF_UNPROVEN 0x08u /* not proven against the current contract  */
#define ZXV_HW_PF_DISABLED 0x10u
#define ZXV_HW_PF_POLICY   0x20u /* the function's policy excludes it        */
#define ZXV_HW_PF_SERVING  0x40u /* serving the function now                 */

/* Coarse power classes (ZXV_HW_CAP_POWER_BUCKET, zxv_hw_provider_info). */
#define ZXV_HW_POWER_UNKNOWN 0 /* not declared  */
#define ZXV_HW_POWER_10MW    1 /* <= 10 mW      */
#define ZXV_HW_POWER_100MW   2 /* <= 100 mW     */
#define ZXV_HW_POWER_1W      3 /* <= 1 W        */
#define ZXV_HW_POWER_10W     4 /* <= 10 W       */
#define ZXV_HW_POWER_100W    5 /* <= 100 W      */
#define ZXV_HW_POWER_MORE    6 /* above         */

/* An implementation: write at most out_cap bytes to `out`, the length to
 * *out_len, and return ZXV_OK (or any negative status, which the caller
 * of zxv_hw_call receives). */
typedef int32_t (*zxv_hw_call_fn)(void *user, uint32_t op, const uint8_t *in, size_t in_len,
                                  uint8_t *out, size_t out_cap, size_t *out_len);

/* A new matrix holds the ZXV_HW_RES_HOST resource and the standard engines
 * this build has, each served by its proven SOFT implementation. */
ZXV_API int32_t zxv_hw_matrix_create_cap(const zxv_cap *cap, zxv_hw_matrix **out);
ZXV_API void zxv_hw_matrix_destroy(zxv_hw_matrix *mx);
/* name: UTF-8, at most 23 bytes, may be NULL. */
ZXV_API int32_t zxv_hw_function_add(const zxv_cap *cap, zxv_hw_matrix *mx, uint32_t fn_id,
                                    const char *name);
/* A golden vector of the function's current contract (copied; output at
 * most 512 bytes). Resets every implementation of the function to
 * unproven. */
ZXV_API int32_t zxv_hw_vector_add(const zxv_cap *cap, zxv_hw_matrix *mx, uint32_t fn_id,
                                  uint32_t op, const uint8_t *in, size_t in_len,
                                  const uint8_t *expect, size_t expect_len);
/* Start contract `version` (strictly newer, else ZXV_E_ROLLBACK): the
 * vectors are cleared and every implementation must prove itself again. */
ZXV_API int32_t zxv_hw_function_contract(const zxv_cap *cap, zxv_hw_matrix *mx, uint32_t fn_id,
                                         uint32_t version);
/* Placement: ZXV_HW_POLICY_* flags, a latency ceiling and a power budget
 * (0: none). An implementation qualifies only if it declared values within
 * them (zxv_hw_provider_traits); undeclared never qualifies. Clearing
 * KEY_BEARING once set: ZXV_E_POLICY. */
ZXV_API int32_t zxv_hw_policy_set(const zxv_cap *cap, zxv_hw_matrix *mx, uint32_t fn_id,
                                  uint32_t flags, uint32_t max_latency_ns,
                                  uint32_t power_budget_mw);
ZXV_API int32_t zxv_hw_function_info(const zxv_cap *cap, zxv_hw_matrix *mx, uint32_t fn_id,
                                     uint32_t *out_contract, uint32_t *out_policy,
                                     uint32_t *out_vectors);
ZXV_API int32_t zxv_hw_resource_add(const zxv_cap *cap, zxv_hw_matrix *mx, uint32_t res_id);
ZXV_API int32_t zxv_hw_resource_set(const zxv_cap *cap, zxv_hw_matrix *mx, uint32_t res_id,
                                    int32_t state);
/* *out_provider is the implementation's index, used by the calls below. */
ZXV_API int32_t zxv_hw_provider_add(const zxv_cap *cap, zxv_hw_matrix *mx, uint32_t fn_id,
                                    uint32_t res_id, int32_t kind, uint32_t cost,
                                    zxv_hw_call_fn call, void *user, uint32_t *out_provider);
/* Declared traits used by placement and discovery: typical latency per
 * call, power while serving, throughput class 0 (slowest) .. 15. */
ZXV_API int32_t zxv_hw_provider_traits(const zxv_cap *cap, zxv_hw_matrix *mx, uint32_t provider,
                                       uint32_t latency_ns, uint32_t power_mw,
                                       uint32_t throughput_class);
ZXV_API int32_t zxv_hw_provider_dep(const zxv_cap *cap, zxv_hw_matrix *mx, uint32_t provider,
                                    uint32_t dep_fn_id);
ZXV_API int32_t zxv_hw_provider_enable(const zxv_cap *cap, zxv_hw_matrix *mx, uint32_t provider,
                                       int32_t on);
/* Run the golden vectors; *out_lp is ZXV_LP_TRUE (serves), ZXV_LP_FALSE
 * (a vector failed) or ZXV_LP_NEITHER (no vectors). */
ZXV_API int32_t zxv_hw_attest(const zxv_cap *cap, zxv_hw_matrix *mx, uint32_t provider,
                              int32_t *out_lp);
/* Discovery: implementations are indexed 0 .. count-1. */
ZXV_API int32_t zxv_hw_provider_count(const zxv_cap *cap, zxv_hw_matrix *mx, uint32_t *out_count);
/* out_contract: the contract version it last proved; out_flags:
 * ZXV_HW_PF_*; out_power_class: ZXV_HW_POWER_*. Any out pointer may be
 * NULL. */
ZXV_API int32_t zxv_hw_provider_info(const zxv_cap *cap, zxv_hw_matrix *mx, uint32_t provider,
                                     uint32_t *out_fn_id, int32_t *out_kind, int32_t *out_lp,
                                     uint32_t *out_contract, uint32_t *out_flags,
                                     uint32_t *out_throughput_class, uint32_t *out_power_class,
                                     uint64_t *out_calls, uint64_t *out_cost_spent);
/* The implementation that serves the function now (ZXV_E_NOPROVIDER if
 * none). */
ZXV_API int32_t zxv_hw_bound(const zxv_cap *cap, zxv_hw_matrix *mx, uint32_t fn_id,
                             uint32_t *out_provider);
ZXV_API int32_t zxv_hw_call(const zxv_cap *cap, zxv_hw_matrix *mx, uint32_t fn_id, uint32_t op,
                            const uint8_t *in, size_t in_len, uint8_t *out, size_t out_cap,
                            size_t *out_len);
/* zxv_hw_call that also runs the next-best implementation and compares.
 * On a mismatch both stop serving: ZXV_E_DISPUTE. */
ZXV_API int32_t zxv_hw_shadow(const zxv_cap *cap, zxv_hw_matrix *mx, uint32_t fn_id, uint32_t op,
                              const uint8_t *in, size_t in_len, uint8_t *out, size_t out_cap,
                              size_t *out_len, int32_t *out_checked);
/* Declare the function pure and give it a memo of `entries` (a power of
 * two, at most 65536; 0 removes it). Repeated inputs (<= 64 bytes) skip
 * the work. Key-bearing functions: ZXV_E_POLICY. */
ZXV_API int32_t zxv_hw_memo(const zxv_cap *cap, zxv_hw_matrix *mx, uint32_t fn_id,
                            uint32_t entries, uint64_t *out_hits, uint64_t *out_misses);

/* ----- Signed packages: keys, images, contracts ----- */

/* Image and package kinds (the header's kind). */
#define ZXV_HW_IMG_BITSTREAM 1
#define ZXV_HW_IMG_PARTIAL   2
#define ZXV_HW_IMG_MICROCODE 3
#define ZXV_HW_IMG_FIRMWARE  4
#define ZXV_HW_IMG_VECTORS   5 /* a new golden-vector contract for a function */

/* Every package is signed with ML-DSA-87 AND SLH-DSA-SHAKE-256s; both
 * must verify. A key is the ML-DSA public key followed by the SLH-DSA
 * one. */
#define ZXV_HW_PK_BYTES        2656 /* 2592 + 64                       */
#define ZXV_HW_MLDSA_SK_BYTES  4896
#define ZXV_HW_SLHDSA_SK_BYTES 128
#define ZXV_HW_SIG_BYTES       34419 /* 4627 + 29792                    */
#define ZXV_HW_HEADER_BYTES    96

/* A fresh release key pair (needs CRYPTO_SIGN). */
ZXV_API int32_t zxv_hw_keygen(const zxv_cap *cap, uint8_t pk[2656], uint8_t mldsa_sk[4896],
                              uint8_t slhdsa_sk[128]);
/* Build and sign a package: header || payload || signatures (needs
 * CRYPTO_SIGN). For an image, fn_id is the function the region will serve;
 * for ZXV_HW_IMG_VECTORS, fn_id is the function, version the contract
 * version, and the payload is a vector list (HW_EMULATION.md). svn is the
 * anti-rollback floor. target_digest may be NULL. SLH-DSA signing takes
 * seconds: it is a release step. R4 buffer rules. */
ZXV_API int32_t zxv_hw_image_build(const zxv_cap *cap, uint32_t kind, uint32_t target_class,
                                   const uint8_t *target_digest, uint32_t fn_id,
                                   uint32_t version, uint32_t svn, const uint8_t *payload,
                                   size_t payload_len, const uint8_t mldsa_sk[4896],
                                   const uint8_t slhdsa_sk[128], uint8_t *buf, size_t cap_bytes,
                                   size_t *out_len);

/* Contract packages for a matrix: trust a release key (copied; at most 4),
 * then apply ZXV_HW_IMG_VECTORS packages. Apply verifies both signatures,
 * requires a newer version and svn >= the function's floor
 * (ZXV_E_ROLLBACK), installs the vectors, proves every implementation
 * again and records the change in the matrix's audit log. *out_proven:
 * how many implementations prove the new contract. Needs CAP_HW_RECONF. */
ZXV_API int32_t zxv_hw_matrix_trust_key(const zxv_cap *cap, zxv_hw_matrix *mx,
                                        const uint8_t pk[2656]);
ZXV_API int32_t zxv_hw_contract_apply(const zxv_cap *cap, zxv_hw_matrix *mx, const uint8_t *pkg,
                                      size_t len, uint32_t *out_proven);

/* ----- Reconfigurable regions: hardware updates as signed images ----- */

#define ZXV_HW_REGION_EMPTY     0
#define ZXV_HW_REGION_ACTIVE    1
#define ZXV_HW_REGION_STAGED    2
#define ZXV_HW_REGION_PROBATION 3
#define ZXV_HW_REGION_HALTED    4

/* Events and audit records. */
#define ZXV_HW_EV_STAGED      1 /* verified and loaded into the inactive slot */
#define ZXV_HW_EV_TESTED      2 /* switched to and proven: probation begins   */
#define ZXV_HW_EV_ACTIVATED   3 /* committed: permanent, floor raised         */
#define ZXV_HW_EV_ROLLED_BACK 4 /* back to the previous image (or to S0)      */
#define ZXV_HW_EV_REJECTED    5 /* refused before loading (signature, replay) */
#define ZXV_HW_EV_QUARANTINED 6 /* failed in hardware; payload blocked        */
#define ZXV_HW_EV_CONTRACT    7 /* a function's golden vectors replaced       */

/* The device side of a region. Each returns ZXV_OK or a negative status.
 * readback writes the SHA-256 of what the slot really holds. */
typedef int32_t (*zxv_hw_load_fn)(void *user, uint32_t slot, const uint8_t *payload, size_t len);
typedef int32_t (*zxv_hw_readback_fn)(void *user, uint32_t slot, uint8_t digest[32]);
typedef int32_t (*zxv_hw_activate_fn)(void *user, uint32_t slot);
/* Called after each recorded step, once the region call has released its
 * locks, in order: event (ZXV_HW_EV_*), reason (the failure code, 0 for
 * none) and image version. It may call into the region again. */
typedef void (*zxv_hw_event_fn)(void *user, uint32_t region_id, int32_t event, uint32_t reason,
                                uint32_t version);

/* mx/provider: the region's FABRIC or MICRO implementation in a matrix,
 * whose golden vectors test each new image; mx may be NULL (no test).
 * target_digest may be NULL (zeros). log_cap: audit log size; when it
 * fills, the region halts and the context is poisoned (fail closed): a
 * change that cannot be recorded is not made. */
ZXV_API int32_t zxv_hw_region_create_cap(const zxv_cap *cap, zxv_hw_matrix *mx,
                                         uint32_t provider, uint32_t region_id,
                                         uint32_t target_class, const uint8_t *target_digest,
                                         zxv_hw_load_fn load, zxv_hw_readback_fn readback,
                                         zxv_hw_activate_fn activate, void *user,
                                         uint32_t log_cap, zxv_hw_region **out);
ZXV_API void zxv_hw_region_destroy(zxv_hw_region *rg);
ZXV_API int32_t zxv_hw_region_events(const zxv_cap *cap, zxv_hw_region *rg, zxv_hw_event_fn fn,
                                     void *user);
/* Trust a release key (copied; at most 4 per region). */
ZXV_API int32_t zxv_hw_region_trust_key(const zxv_cap *cap, zxv_hw_region *rg,
                                        const uint8_t pk[2656]);
/* Stage: verify and load into the inactive slot. Rejections (ZXV_E_VERIFY,
 * ZXV_E_ROLLBACK) are recorded; ZXV_E_QUARANTINED (read-back mismatch) is
 * recorded and the image blocked. */
ZXV_API int32_t zxv_hw_region_stage(const zxv_cap *cap, zxv_hw_region *rg, const uint8_t *image,
                                    size_t len);
/* Test: switch to the staged image and prove it against the golden
 * vectors; ZXV_E_QUARANTINED if it fails (the previous image, or S0, is
 * back in place). Passing starts a probation of `ticks`. */
ZXV_API int32_t zxv_hw_region_trial(const zxv_cap *cap, zxv_hw_region *rg, uint32_t ticks);
/* Activate: make the tested image permanent and raise the floor. */
ZXV_API int32_t zxv_hw_region_commit(const zxv_cap *cap, zxv_hw_region *rg);
ZXV_API int32_t zxv_hw_region_rollback(const zxv_cap *cap, zxv_hw_region *rg);
/* One health tick; an expiring probation rolls back (ZXV_E_QUARANTINED). */
ZXV_API int32_t zxv_hw_region_tick(const zxv_cap *cap, zxv_hw_region *rg);
ZXV_API int32_t zxv_hw_region_info(const zxv_cap *cap, zxv_hw_region *rg, int32_t *out_state,
                                   uint32_t *out_version, uint32_t *out_floor,
                                   uint32_t *out_quarantined);

/* Audit logs (regions and matrices). log: the head hash and length;
 * ZXV_E_INVARIANT (and the context poisoned) if the chain does not
 * verify. log_entry: record `index` (0 = oldest): event, reason, the
 * region id (function id for ZXV_HW_EV_CONTRACT), version, hexagram tag
 * (0..63; forbidden for rejections and quarantines), the SHA-256 of the
 * package header, and the record's chain hash. */
ZXV_API int32_t zxv_hw_region_log(const zxv_cap *cap, zxv_hw_region *rg, uint8_t head[32],
                                  uint32_t *out_count);
ZXV_API int32_t zxv_hw_region_log_entry(const zxv_cap *cap, zxv_hw_region *rg, uint32_t index,
                                        int32_t *out_event, uint32_t *out_reason,
                                        uint32_t *out_id, uint32_t *out_version,
                                        uint32_t *out_tag, uint8_t subject[32],
                                        uint8_t hash[32]);
ZXV_API int32_t zxv_hw_matrix_log(const zxv_cap *cap, zxv_hw_matrix *mx, uint8_t head[32],
                                  uint32_t *out_count);
ZXV_API int32_t zxv_hw_matrix_log_entry(const zxv_cap *cap, zxv_hw_matrix *mx, uint32_t index,
                                        int32_t *out_event, uint32_t *out_reason,
                                        uint32_t *out_id, uint32_t *out_version,
                                        uint32_t *out_tag, uint8_t subject[32],
                                        uint8_t hash[32]);

/* ===================================================================== */
/* ABI 1.5 (add-only): SDK access to the newer kernel features.          */
/* The 32 bits of zxv_features are nearly used up, so 1.5 groups report  */
/* through a second mask, zxv_features2. Each group below is its own     */
/* section; a group not built into this library returns ZXV_E_NOTIMPL    */
/* from every call and leaves its ZXV_FEAT2_* bit clear (rule R9).       */
/* ===================================================================== */

ZXV_API int32_t zxv_features2(uint32_t *out_mask);

#define ZXV_FEAT2_TREATY  0x00000001u /* treaties: copybook records, Sutra   */
#define ZXV_FEAT2_AIPI    0x00000002u /* content-addressed functions, CIDs   */
#define ZXV_FEAT2_COIN    0x00000004u /* Fibonacci coins and token octane    */
#define ZXV_FEAT2_FEE     0x00000008u /* assurance fee and its split         */
#define ZXV_FEAT2_VHUB    0x00000010u /* V-Bill hub pegs, quotes and swaps   */
#define ZXV_FEAT2_FREIGHT 0x00000020u /* erasure-coded freight packets       */
#define ZXV_FEAT2_WEB4    0x00000040u /* agent manifests, directory, consent */
#define ZXV_FEAT2_PARADOX 0x00000080u /* paradox engine                      */

#define ZXV_CAP_AIPI    (UINT64_C(1) << 12) /* publish and execute AIPI functions */
#define ZXV_CAP_MINT    (UINT64_C(1) << 13) /* mint coins, change token octane    */
#define ZXV_CAP_PARADOX (UINT64_C(1) << 14) /* paradox operators and policies     */
/* ZXV_CAP_ALL keeps its 1.1 value forever; this is every bit as of 1.5. */
#define ZXV_CAP_ALL_1_5 ((UINT64_C(1) << 15) - 1)

#define ZXV_E_DENOM   (-32) /* not a Fibonacci ladder face (coins, denominations) */
#define ZXV_E_TREATY  (-33) /* treaty fails validation; detail in zxv_last_error */
#define ZXV_E_CID     (-34) /* content does not match its CID, or bad CID     */
#define ZXV_E_CONSENT (-35) /* no valid consent for this agent action          */
#define ZXV_E_ERASURE (-36) /* too few freight rows to rebuild the packet      */
#define ZXV_E_PEG     (-37) /* no valid signed peg for this currency pair      */
#define ZXV_E_LIMIT   (-38) /* refused by a hub limit rule (not a fault)       */
/* -39 .. -45 are reserved for the paradox group. */

/* ===== treaty (ABI 1.5): COBOL copybook treaties and Sutra text (kernel/src/treaty, PR #24) ===== */

/* A treaty is one batch of settlement work: who takes part, what each party
 * deposits and may claim back, how the batch's dividend is shared (each
 * party's weight num/den), and who owes whom. It has two exact spellings
 * that hold the same treaty:
 *
 *   RECORD  the fixed ZXV_TREATY_REC_LEN-byte COBOL copybook record
 *           (kernel/src/treaty/copybooks/ZXVTRTY.cpy), all DISPLAY text, so
 *           it is plain ASCII on every machine.
 *   SUTRA   compact text, e.g.
 *             TREATY CORRIDOR-7 USD SETL\n
 *             PARTY ACME 5000 4000 1/2\n
 *             PARTY BETA 3000 3000 1/2\n
 *             OWE ACME BETA 1200\n
 *             END\n
 *           (single spaces, '\n' line ends, no leading zeros, fractions in
 *           lowest terms, nothing after "END\n"; kernel/src/treaty/README.md
 *           has the grammar).
 *
 * Each spelling is canonical: a parse that succeeds followed by an emit
 * gives the input back byte for byte, and the record and Sutra forms of one
 * treaty compare equal (zxv_treaty_equal). Every parse and emit checks:
 *   R1  weights: 1 <= den, num <= den, lowest terms, both at most
 *       ZXV_TREATY_WEIGHT_MAX, and they add up to exactly 1;
 *   R2  claim <= deposit for every party;
 *   R3  every amount fits PIC S9(18) (|v| <= ZXV_TREATY_PIC18_MAX),
 *       deposits and claims >= 0, obligations > 0;
 *   R4  1..16 parties with distinct ids, 0..64 obligations, each between
 *       two different known parties.
 * A refusal is ZXV_E_TREATY with a ZXV_TREATY_FAULT_* reason and, for a
 * parse, the byte offset of the first fault. These two out parameters are
 * an exception to R3: they are written on ZXV_E_TREATY as well as on ZXV_OK
 * (where they are 0 and ZXV_TREATY_FAULT_NONE); either may be NULL.
 *
 * Everything in this group is pure: no context, no capability. Running a
 * treaty (zxv_treaty_run) works in a private scratch engine that is freed
 * before the call returns; it changes no ledger or state any caller holds,
 * so it needs no capability either. Its numbers are a deterministic report,
 * not a payment.
 *
 * HONEST LIMITS. A treaty is a batch description, not a contract in law:
 * signatures, consent and authority live elsewhere. One asset per treaty.
 * The dividend shares are reported only; nothing is paid out of the bucket.
 */

#define ZXV_TREATY_REC_LEN     2542u /* ZXVTRTY.cpy treaty record            */
#define ZXV_TREATY_RESULT_LEN  2422u /* ZXVRSLT.cpy result record            */
#define ZXV_TREATY_SUTRA_MAX   3843u /* longest canonical Sutra text (bytes) */
#define ZXV_TREATY_MAX_PARTIES 16u
#define ZXV_TREATY_MAX_OWES    64u
#define ZXV_TREATY_NAME_MAX    30u   /* A-Z 0-9 '-', starts with a letter   */
#define ZXV_TREATY_ID_MAX      8u    /* party ids, same alphabet             */
#define ZXV_TREATY_PIC18_MAX   INT64_C(999999999999999999) /* PIC S9(18)     */
#define ZXV_TREATY_WEIGHT_MAX  999999999u                  /* PIC 9(9)       */

/* Operation (zxv_treaty_header). */
#define ZXV_TREATY_OP_NET  1 /* net the obligations only                     */
#define ZXV_TREATY_OP_SETL 2 /* net, post the legs with the fee, pay claims  */

/* Why a treaty was refused (the *out_fault of the calls below). */
#define ZXV_TREATY_FAULT_NONE   0
#define ZXV_TREATY_FAULT_MAGIC  2  /* record does not start with "ZXT1"       */
#define ZXV_TREATY_FAULT_SYNTAX 3  /* Sutra text outside the grammar          */
#define ZXV_TREATY_FAULT_NAME   4  /* treaty name not 1..30 of A-Z 0-9 '-'    */
#define ZXV_TREATY_FAULT_ASSET  5  /* asset not three capital letters         */
#define ZXV_TREATY_FAULT_OP     6  /* operation not NET or SETL               */
#define ZXV_TREATY_FAULT_COUNT  7  /* party or obligation count out of range  */
#define ZXV_TREATY_FAULT_NUMBER 8  /* malformed number or outside PIC S9(18)  */
#define ZXV_TREATY_FAULT_PARTY  9  /* bad, duplicate or unknown party id      */
#define ZXV_TREATY_FAULT_WEIGHT 10 /* R1                                      */
#define ZXV_TREATY_FAULT_CLAIM  11 /* R2                                      */
#define ZXV_TREATY_FAULT_OWE    12 /* obligation to self, or not above zero   */
#define ZXV_TREATY_FAULT_PAD    13 /* unused record slot or field not blank   */

/* One treaty. NOT thread-safe: use it from one thread at a time (concurrent
 * calls that only read it, i.e. take `const zxv_treaty *`, are fine).
 * Holds no context; destroy it exactly once. */
typedef struct zxv_treaty zxv_treaty;
/* What running a treaty returned. Immutable once made, so any number of
 * threads may read it; destroy it exactly once. Independent of the treaty
 * it came from (either may be destroyed first). */
typedef struct zxv_treaty_result zxv_treaty_result;

/* A new, empty treaty (no parties: it fails validation until a parse
 * fills it). ZXV_E_ARG (out NULL), ZXV_E_NOMEM. */
ZXV_API int32_t zxv_treaty_create(zxv_treaty **out);
/* Free it. NULL is ignored. */
ZXV_API void zxv_treaty_destroy(zxv_treaty *t);
/* Empty it again, as zxv_treaty_create left it. ZXV_E_ARG (t NULL). */
ZXV_API int32_t zxv_treaty_clear(zxv_treaty *t);

/* Parse NUL-terminated Sutra text into t. On any failure t is unchanged.
 * ZXV_OK; ZXV_E_TREATY with *out_fault and *out_fault_off (the byte offset
 * of the first fault in `text`); ZXV_E_ARG (t or text NULL); ZXV_E_UTF8;
 * ZXV_E_TOO_LONG (more than ZXV_TREATY_SUTRA_MAX bytes). Valid UTF-8 that
 * is not ASCII is never ZXV_E_UTF8: it is ZXV_E_TREATY, with the fault the
 * grammar meets at that point, like any other byte outside it. Where a field
 * is read whole (it starts there, or is fixed width) that is the field's own
 * fault at the field's first byte: "PARTY \xc3\x89 ..." gives
 * ZXV_TREATY_FAULT_PARTY at the party id, "TREATY N US\xc3\x89 ..." gives
 * ZXV_TREATY_FAULT_ASSET at the asset, a non-ASCII byte that starts a
 * treaty name gives ZXV_TREATY_FAULT_NAME and one that starts a number
 * ZXV_TREATY_FAULT_NUMBER. Directly after a complete token ("TREATY
 * N\xc3\x89 ...") or outside any field it is ZXV_TREATY_FAULT_SYNTAX at the
 * non-ASCII byte itself. */
ZXV_API int32_t zxv_treaty_parse_sutra(zxv_treaty *t, const char *text, uint32_t *out_fault_off,
                                       int32_t *out_fault);
/* Parse a copybook record of exactly ZXV_TREATY_REC_LEN bytes into t. Unused
 * party and obligation slots must be exactly what COBOL's INITIALIZE leaves
 * (spaces and "+000..."), and "-0" is refused. On any failure t is
 * unchanged. ZXV_OK; ZXV_E_TREATY with *out_fault and *out_fault_off (byte
 * offset in the record); ZXV_E_ARG (t or rec NULL, or len is not
 * ZXV_TREATY_REC_LEN). */
ZXV_API int32_t zxv_treaty_parse_record(zxv_treaty *t, const uint8_t *rec, size_t len,
                                        uint32_t *out_fault_off, int32_t *out_fault);
/* The canonical Sutra text (R4 text rules: NUL-terminated, at most
 * ZXV_TREATY_SUTRA_MAX bytes before the NUL). ZXV_E_TREATY if t does not
 * validate (an empty treaty, say); ZXV_E_ARG; ZXV_E_BUFFER. */
ZXV_API int32_t zxv_treaty_emit_sutra(const zxv_treaty *t, char *buf, size_t cap,
                                      size_t *out_len);
/* The canonical copybook record. BYTES, not text: no NUL; *out_len is
 * always ZXV_TREATY_REC_LEN and cap >= ZXV_TREATY_REC_LEN suffices. NULL
 * buf or a smaller cap gives ZXV_E_BUFFER with *out_len set and nothing
 * written. ZXV_E_TREATY if t does not validate (checked first, *out_len
 * then unwritten); ZXV_E_ARG (t or out_len NULL). */
ZXV_API int32_t zxv_treaty_emit_record(const zxv_treaty *t, uint8_t *buf, size_t cap,
                                       size_t *out_len);
/* Check R1-R4 and the spelling rules. ZXV_OK (*out_fault = NONE) or
 * ZXV_E_TREATY with *out_fault; out_fault may be NULL. ZXV_E_ARG (t NULL).
 * A parsed treaty always validates; an empty one fails (its first fault
 * is the empty name, ZXV_TREATY_FAULT_NAME). */
ZXV_API int32_t zxv_treaty_validate(const zxv_treaty *t, int32_t *out_fault);
/* *out_equal = 1 if a and b hold the same treaty (every field, every slot),
 * else 0. Two empty treaties are equal. ZXV_E_ARG on any NULL. */
ZXV_API int32_t zxv_treaty_equal(const zxv_treaty *a, const zxv_treaty *b, int32_t *out_equal);

/* ---- what a treaty holds (R4 text rules for the strings) ---- */
/* The treaty name ("" when empty). */
ZXV_API int32_t zxv_treaty_name(const zxv_treaty *t, char *buf, size_t cap, size_t *out_len);
/* The asset: three capital letters ("" when empty). */
ZXV_API int32_t zxv_treaty_asset(const zxv_treaty *t, char *buf, size_t cap, size_t *out_len);
/* The operation (ZXV_TREATY_OP_*, 0 when empty) and the party and
 * obligation counts. ZXV_E_ARG on any NULL. */
ZXV_API int32_t zxv_treaty_header(const zxv_treaty *t, int32_t *out_op, uint32_t *out_parties,
                                  uint32_t *out_owes);
/* Party `index` (0-based, in treaty order): its id. ZXV_E_NOT_FOUND if
 * index >= the party count. */
ZXV_API int32_t zxv_treaty_party_id(const zxv_treaty *t, uint32_t index, char *buf, size_t cap,
                                    size_t *out_len);
/* Party `index`: deposit and claim (minor units of the asset) and dividend
 * weight num/den. ZXV_E_ARG on any NULL; ZXV_E_NOT_FOUND. */
ZXV_API int32_t zxv_treaty_party(const zxv_treaty *t, uint32_t index, int64_t *out_deposit,
                                 int64_t *out_claim, uint32_t *out_w_num, uint32_t *out_w_den);
/* Obligation `index` (0-based): party `from` owes party `to` `amount`
 * (party indices, 0-based). ZXV_E_ARG on any NULL; ZXV_E_NOT_FOUND. */
ZXV_API int32_t zxv_treaty_owe(const zxv_treaty *t, uint32_t index, uint32_t *out_from,
                               uint32_t *out_to, int64_t *out_amount);

/* ---- PIC S9(18) arithmetic, as COBOL does it with ON SIZE ERROR ----
 * Exact, on integers in [-ZXV_TREATY_PIC18_MAX, ZXV_TREATY_PIC18_MAX].
 * ZXV_E_RANGE (a size error; nothing written) when an operand or the exact
 * result is outside that range, or on division by zero. Division truncates
 * toward zero and the remainder has the dividend's sign (DIVIDE a BY b
 * GIVING q REMAINDER r): -7 / 2 = -3 rem -1, 7 / -2 = -3 rem 1.
 * ZXV_E_ARG on a NULL out pointer. */
ZXV_API int32_t zxv_treaty_pic_add(int64_t a, int64_t b, int64_t *out);
ZXV_API int32_t zxv_treaty_pic_sub(int64_t a, int64_t b, int64_t *out);
ZXV_API int32_t zxv_treaty_pic_mul(int64_t a, int64_t b, int64_t *out);
ZXV_API int32_t zxv_treaty_pic_div(int64_t a, int64_t b, int64_t *out_q, int64_t *out_rem);

/* ---- running a treaty ----
 * Runs t in a fresh private engine for its asset: funds each deposit, nets
 * the obligations multilaterally (at most parties - 1 legs), and for SETL
 * posts the legs with the 0.08889% assurance fee (all or nothing), pays
 * each claim back out, and reports each party's weight share of what the
 * fee put in the V-Bill dividend bucket (floor shares, then leftover units
 * to the largest remainders, ties to the lowest index; reported, not paid).
 * The engine is freed before the call returns: nothing persists and no
 * capability is needed. Deterministic: the same treaty gives the same
 * result, identical to what CALL 'ZXV_ENGINE' returns for its record.
 * Takes about 500 KB of heap for the call.
 * ZXV_OK; ZXV_E_TREATY (t does not validate); ZXV_E_FUNDS (a payer cannot
 * cover its legs plus the fee, or a claim exceeds what is left after
 * settlement); ZXV_E_RANGE (a net position leaves PIC S9(18), or an amount
 * exceeds the engine's per-line limit of 2^59); ZXV_E_CAPACITY;
 * ZXV_E_NOMEM; ZXV_E_ARG; ZXV_E_INTERNAL (the scratch engine broke an
 * invariant, which is a bug). On failure *out is not written. */
ZXV_API int32_t zxv_treaty_run(const zxv_treaty *t, zxv_treaty_result **out);
/* Free it. NULL is ignored. */
ZXV_API void zxv_treaty_result_destroy(zxv_treaty_result *r);
/* The party and leg counts and the total fee charged (0 for NET). */
ZXV_API int32_t zxv_treaty_result_summary(const zxv_treaty_result *r, uint32_t *out_parties,
                                          uint32_t *out_legs, int64_t *out_fees);
/* Party `index` (treaty order): net = what it is owed minus what it owes
 * (gross); balance = deposit for NET, else what it holds after the legs,
 * the fee and its claim; dividend = its reported dividend share (0 for
 * NET). ZXV_E_ARG on any NULL; ZXV_E_NOT_FOUND. */
ZXV_API int32_t zxv_treaty_result_party(const zxv_treaty_result *r, uint32_t index,
                                        int64_t *out_net, int64_t *out_balance,
                                        int64_t *out_dividend);
/* Netted leg `index`: party `from` pays party `to` `amount` (0-based party
 * indices). ZXV_E_ARG on any NULL; ZXV_E_NOT_FOUND. */
ZXV_API int32_t zxv_treaty_result_leg(const zxv_treaty_result *r, uint32_t index,
                                      uint32_t *out_from, uint32_t *out_to, int64_t *out_amount);
/* The ZXVRSLT.cpy result record (ZXV_TREATY_RESULT_LEN bytes, the same
 * byte rules as zxv_treaty_emit_record; party numbers in it are 1-based as
 * the copybook says). */
ZXV_API int32_t zxv_treaty_result_record(const zxv_treaty_result *r, uint8_t *buf, size_t cap,
                                         size_t *out_len);

/* ===== end treaty ===== */

/* ===== aipi (ABI 1.5): AIPI content-addressed functions and IPFS CIDs (kernel/src/aipi, kernel/src/ipfs, PR #22) ===== */
/* AIPI names data and functions by WHAT they are (a hash) instead of WHERE
 * they live. Design and limits: docs/AIPI.md. Everything here is the
 * kernel's code (kernel/src/aipi and its IPFS node, kernel/src/ipfs_node);
 * this group validates arguments, maps statuses and owns the handle.
 *
 * CIDs AS BYTES. A CID crosses this ABI in its BINARY form (1 to
 * ZXV_CID_MAX bytes: version, codec, multihash), never as a struct. Every
 * function that takes a CID refuses bytes that are not exactly one
 * canonical CIDv0 or CIDv1 (sha2-256 or identity multihash) with ZXV_E_CID.
 * A CID out is written to a caller array of ZXV_CID_MAX bytes and its
 * length to *out_cid_len. The CIDs are byte-for-byte the ones Kubo makes:
 * the tests check them against strings produced by Kubo 0.32.1.
 *
 * R4 BYTES. A record or blob out takes (buf, cap, *out_len) as R4 text
 * does, without the NUL: *out_len always receives the length; if
 * cap < *out_len (or buf is NULL and the result is not empty) the call is
 * ZXV_E_BUFFER and writes nothing to buf.
 *
 * KUBO IDENTITY. zxv_cid_compute == `ipfs block put` (CIDv1, sha2-256 of
 * the bytes as ONE block, any codec); zxv_cid_unixfs == `ipfs add
 * --cid-version=1` (256 KiB chunks, raw leaves, balanced DAG of 174 links);
 * the two agree for files of at most 262144 bytes. zxv_cid_to_text with
 * ZXV_CID_BASE_DEFAULT prints what Kubo prints (CIDv1 "b..." base32,
 * CIDv0 "Qm..."); ZXV_CID_BASE_58BTC prints "z..." (the AIPI form, also
 * `ipfs cid format -b base58btc`).
 *
 * THE FOUR AIPI IDENTIFIERS (all CIDv1, full sha2-256; codes 0x3A10xx are
 * multicodec PRIVATE-USE values, not registered):
 *   data      codec raw 0x55 (or dag-pb 0x70): blobs, modules, contracts
 *   function  codec ZXV_AIPI_MC_FN + intent, hash of a function record
 *   call      codec ZXV_AIPI_MC_CALL, hash of a call record
 *   claim     not a CID: an executor's answer (zxv_aipi_claim_encode)
 * Records are the wire bytes of docs/AIPI.md section 11, decoded strictly
 * (exact lengths, no trailing bytes; a function record must be the
 * canonical encoding, so one function has exactly one CID).
 *
 * HONEST LIMITS.
 *   - No network transport in libzxv: no Vinea mesh, no IPFS peers, no
 *     HTTP gateway, no libp2p. A zxv_aipi handle is an OFFLINE, in-process
 *     node: GET finds only what was put into this handle.
 *   - No EXECUTE: running a function needs the Z-Wasm sandbox
 *     (sdk/kits/aipi), which is not part of libzxv. zxv_aipi_plan reports
 *     the route a call would take here; it runs nothing.
 *   - No money moves in this group. AIPI bounties are escrowed by host hooks
 *     (reserve / pay / refund) that the kernel calls only from EXECUTE and
 *     claim submission, neither of which is exposed; kernel/src/aipi never
 *     posts to the ledger itself.
 *   - A CID proves which bytes are meant. It does not prove that a claimed
 *     OUTPUT of a function is right (see docs/AIPI.md section 6), and it
 *     does not make bytes available: a CID resolves only while someone
 *     holds them.
 *   - Claims are the bytes an executor signs (e.g. zxv ML-DSA); signing and
 *     checking the signature are the caller's job, not this group's.
 *   - kernel/src/ipfs's bare SHA-256 "cid:<64hex>" names are NOT CIDs in
 *     this sense and are not exposed; use these real CIDs.
 */

#define ZXV_CID_MAX      80u  /* longest binary CID accepted or produced     */
#define ZXV_CID_TEXT_MAX 139u /* longest CID text (excluding the NUL)       */
#define ZXV_CID_DIGEST_MAX 64u /* longest multihash digest (identity)       */

/* Multicodec and multihash codes (zxv_cid_info, zxv_cid_compute). */
#define ZXV_CID_CODEC_RAW    0x55u
#define ZXV_CID_CODEC_DAG_PB 0x70u
#define ZXV_CID_HASH_IDENTITY 0x00u
#define ZXV_CID_HASH_SHA2_256 0x12u

/* zxv_cid_to_text bases. */
#define ZXV_CID_BASE_DEFAULT 0u /* Kubo's form: v1 "b" base32, v0 "Qm" base58 */
#define ZXV_CID_BASE_58BTC   1u /* "z" + base58btc; CIDv1 only (AIPI form)    */

/* AIPI codecs (private-use multicodec range, unregistered). */
#define ZXV_AIPI_MC_FN            0x3A1000u /* zxv-fn            (intent 0) */
#define ZXV_AIPI_MC_TENSOR_INFER  0x3A1001u /* zxv-tensor-infer  (intent 1) */
#define ZXV_AIPI_MC_SETTLE_LEDGER 0x3A1002u /* zxv-settle-ledger (intent 2) */
#define ZXV_AIPI_MC_SWARM_ROUTE   0x3A1003u /* zxv-swarm-route   (intent 3) */
#define ZXV_AIPI_MC_CALL          0x3A10C0u /* zxv-aipi-call                */

/* Function record fields. */
#define ZXV_AIPI_INTENT_FN            0u
#define ZXV_AIPI_INTENT_TENSOR_INFER  1u
#define ZXV_AIPI_INTENT_SETTLE_LEDGER 2u
#define ZXV_AIPI_INTENT_SWARM_ROUTE   3u
#define ZXV_AIPI_VERIFY_CHECKER 1u /* a checker export judges (input, output)  */
#define ZXV_AIPI_VERIFY_CERT    2u /* as CHECKER, with a certificate           */
#define ZXV_AIPI_VERIFY_QUORUM  3u /* k of n executors agree byte for byte     */
#define ZXV_AIPI_VERIFY_LOCAL   4u /* never accept a remote result             */
#define ZXV_AIPI_PRIV_PUBLIC 0u
#define ZXV_AIPI_PRIV_LOCAL  1u /* private or key-bearing: never leaves the node */
#define ZXV_AIPI_DET_NONDETERMINISTIC 0u
#define ZXV_AIPI_DET_DETERMINISTIC    1u

/* Limits (bytes). */
#define ZXV_AIPI_NAME_MAX   63u   /* export names: [A-Za-z0-9_]{1,63}        */
#define ZXV_AIPI_IO_MAX     1024u /* call input, claim output                 */
#define ZXV_AIPI_CERT_MAX   1024u /* claim certificate                        */
#define ZXV_AIPI_QUORUM_MAX 8u    /* QUORUM n                                 */
#define ZXV_AIPI_FN_MAX     373u  /* longest function record                  */
#define ZXV_AIPI_CALL_MAX   1112u /* longest call record                      */
#define ZXV_AIPI_CLAIM_MAX  2170u /* longest claim record                     */
#define ZXV_AIPI_BLOCK_MAX  2097152u /* largest blob a handle stores (2 MiB)  */

/* ---- CIDs (pure: no capability, no context) ---- */

/* The CIDv1 of `data` as one block: sha2-256 under multicodec `codec`
 * (ZXV_CID_CODEC_RAW for plain bytes; Kubo `ipfs block put`). data may be
 * NULL when len is 0. ZXV_E_ARG (NULL), ZXV_E_TOO_LONG (len > 2^32 - 1). */
ZXV_API int32_t zxv_cid_compute(uint32_t codec, const uint8_t *data, size_t len,
                                uint8_t out_cid[ZXV_CID_MAX], size_t *out_cid_len);
/* The root CID Kubo gives for `data` as a file (`ipfs add --cid-version=1`
 * with default settings): a raw CID up to 262144 bytes, a dag-pb root
 * above. The blocks themselves are not kept. ZXV_E_ARG, ZXV_E_NOMEM. */
ZXV_API int32_t zxv_cid_unixfs(const uint8_t *data, size_t len, uint8_t out_cid[ZXV_CID_MAX],
                               size_t *out_cid_len);
/* ZXV_OK if `data` is exactly the block `cid` names (re-hashed; identity
 * CIDs compare the bytes), else ZXV_E_CID. A UnixFS root (dag-pb) names its
 * root BLOCK, not the file: verify a file with zxv_cid_unixfs and compare.
 * ZXV_E_ARG (NULL), ZXV_E_CID (bad CID bytes or mismatch). */
ZXV_API int32_t zxv_cid_verify(const uint8_t *cid, size_t cid_len, const uint8_t *data,
                               size_t len);
/* CID text (R4), base ZXV_CID_BASE_*. ZXV_E_ARG (unknown base, NULL),
 * ZXV_E_CID (bad CID; a CIDv0 in base 58btc, which has no multibase form),
 * ZXV_E_BUFFER. */
ZXV_API int32_t zxv_cid_to_text(const uint8_t *cid, size_t cid_len, uint32_t base, char *buf,
                                size_t cap, size_t *out_len);
/* Parse "b..." (CIDv1 base32 lower), "Qm..." (CIDv0) or "z..." (CIDv1
 * base58btc). Only the canonical text is accepted: no upper case, padding,
 * other multibases or extra leading zeros. ZXV_E_ARG (NULL or ""), ZXV_E_UTF8,
 * ZXV_E_TOO_LONG (over ZXV_CID_TEXT_MAX), ZXV_E_CID (anything else). */
ZXV_API int32_t zxv_cid_from_text(const char *text, uint8_t out_cid[ZXV_CID_MAX],
                                  size_t *out_cid_len);
/* The parts of a CID. Any out pointer may be NULL; out_digest needs
 * ZXV_CID_DIGEST_MAX bytes. ZXV_E_ARG, ZXV_E_CID. */
ZXV_API int32_t zxv_cid_info(const uint8_t *cid, size_t cid_len, uint32_t *out_version,
                             uint32_t *out_codec, uint32_t *out_hash, uint8_t *out_digest,
                             size_t *out_digest_len);
/* CIDv0 to CIDv1 (dag-pb, same hash; Kubo `ipfs cid base32`). A CIDv1 is
 * returned unchanged. ZXV_E_ARG, ZXV_E_CID. */
ZXV_API int32_t zxv_cid_to_v1(const uint8_t *cid, size_t cid_len, uint8_t out_cid[ZXV_CID_MAX],
                              size_t *out_cid_len);
/* The Kademlia key under which providers of `cid` are announced on Vinea:
 * SHA-256("zxv/aipi/v1/provider" || binary CID). ZXV_E_ARG, ZXV_E_CID. */
ZXV_API int32_t zxv_aipi_provider_key(const uint8_t *cid, size_t cid_len, uint8_t out_key[32]);

/* ---- Function records (pure) ---- */

/* The canonical function record (R4 bytes). Fields, all checked, none masked:
 *   intent       ZXV_AIPI_INTENT_*; it picks the function CID's codec
 *   verify       ZXV_AIPI_VERIFY_*
 *   privacy      ZXV_AIPI_PRIV_*;  determinism ZXV_AIPI_DET_*
 *   quorum_n/k   QUORUM only (2 <= n <= 8, n/2 < k <= n, DETERMINISTIC);
 *                both 0 otherwise
 *   fuel_max     > 0: the most fuel one run may use
 *   module_cid   CIDv1 raw sha2-256 of the Z-Wasm module bytes
 *   contract_cid NULL (len 0) for none, else CIDv1 raw sha2-256 of the
 *                z-contract text
 *   entry        the export computing the result
 *   checker      CHECKER/CERT only (differs from entry); NULL or "" else
 *   certifier    CERT only (differs from entry and checker); NULL or "" else
 * ZXV_E_ARG (NULL, a field out of range or breaking a rule above, a name
 * not [A-Za-z0-9_]), ZXV_E_UTF8, ZXV_E_TOO_LONG (name over 63), ZXV_E_CID
 * (a CID that does not parse or is not CIDv1 raw sha2-256), ZXV_E_BUFFER. */
ZXV_API int32_t zxv_aipi_fn_encode(uint32_t intent, uint32_t verify, uint32_t privacy,
                                   uint32_t determinism, uint32_t quorum_n, uint32_t quorum_k,
                                   uint64_t fuel_max, const uint8_t *module_cid,
                                   size_t module_cid_len, const uint8_t *contract_cid,
                                   size_t contract_cid_len, const char *entry,
                                   const char *checker, const char *certifier, uint8_t *buf,
                                   size_t cap, size_t *out_len);
/* Check a function record strictly and read its scalar fields. Any out
 * pointer may be NULL. ZXV_E_ARG (rec NULL), ZXV_E_FORMAT (not a valid,
 * canonical function record). */
ZXV_API int32_t zxv_aipi_fn_decode(const uint8_t *rec, size_t len, uint32_t *out_intent,
                                   uint32_t *out_verify, uint32_t *out_privacy,
                                   uint32_t *out_determinism, uint32_t *out_quorum_n,
                                   uint32_t *out_quorum_k, uint64_t *out_fuel_max);
#define ZXV_AIPI_FIELD_MODULE   0u /* zxv_aipi_fn_get_cid */
#define ZXV_AIPI_FIELD_CONTRACT 1u
#define ZXV_AIPI_FIELD_ENTRY     0u /* zxv_aipi_fn_get_name */
#define ZXV_AIPI_FIELD_CHECKER   1u
#define ZXV_AIPI_FIELD_CERTIFIER 2u
/* A CID field of a function record. ZXV_E_NOT_FOUND for CONTRACT when the
 * record has none; ZXV_E_ARG (NULL, unknown field), ZXV_E_FORMAT. */
ZXV_API int32_t zxv_aipi_fn_get_cid(const uint8_t *rec, size_t len, uint32_t field,
                                    uint8_t out_cid[ZXV_CID_MAX], size_t *out_cid_len);
/* A name field of a function record (R4 text; "" when the mode has none).
 * ZXV_E_ARG, ZXV_E_FORMAT, ZXV_E_BUFFER. */
ZXV_API int32_t zxv_aipi_fn_get_name(const uint8_t *rec, size_t len, uint32_t field, char *buf,
                                     size_t cap, size_t *out_len);
/* The function CID of a record: codec ZXV_AIPI_MC_FN + intent, sha2-256 of
 * the record bytes. ZXV_E_ARG, ZXV_E_FORMAT. */
ZXV_API int32_t zxv_aipi_fn_cid(const uint8_t *rec, size_t len, uint8_t out_cid[ZXV_CID_MAX],
                                size_t *out_cid_len);

/* ---- Call and claim records (pure) ---- */

/* The call record naming one invocation: function CID + input (at most
 * ZXV_AIPI_IO_MAX bytes; input may be NULL when in_len is 0). R4 bytes.
 * ZXV_E_ARG, ZXV_E_CID (not a CIDv1 sha2-256 with a function codec),
 * ZXV_E_TOO_LONG, ZXV_E_BUFFER. */
ZXV_API int32_t zxv_aipi_call_encode(const uint8_t *fn_cid, size_t fn_cid_len,
                                     const uint8_t *input, size_t in_len, uint8_t *buf,
                                     size_t cap, size_t *out_len);
/* Check a call record strictly and locate its parts as offsets into `rec`
 * (the bytes stay the caller's). Any out pointer may be NULL.
 * ZXV_E_ARG, ZXV_E_FORMAT. */
ZXV_API int32_t zxv_aipi_call_decode(const uint8_t *rec, size_t len, size_t *out_fn_cid_off,
                                     size_t *out_fn_cid_len, size_t *out_input_off,
                                     size_t *out_input_len);
/* The call CID (codec ZXV_AIPI_MC_CALL, sha2-256 of the call record) of
 * (function CID, input); same errors as zxv_aipi_call_encode. */
ZXV_API int32_t zxv_aipi_call_cid(const uint8_t *fn_cid, size_t fn_cid_len, const uint8_t *input,
                                  size_t in_len, uint8_t out_cid[ZXV_CID_MAX],
                                  size_t *out_cid_len);
/* The claim record an executor signs: its answer `output` (and, for CERT
 * functions, `cert`) to the call `call_cid`, with its 32-byte node id.
 * output / cert may be NULL when their length is 0. R4 bytes. ZXV_E_ARG,
 * ZXV_E_CID (call_cid is not a CIDv1 sha2-256 call CID), ZXV_E_TOO_LONG
 * (output or cert over 1024), ZXV_E_BUFFER. */
ZXV_API int32_t zxv_aipi_claim_encode(const uint8_t *call_cid, size_t call_cid_len,
                                      const uint8_t executor[32], const uint8_t *output,
                                      size_t output_len, const uint8_t *cert, size_t cert_len,
                                      uint8_t *buf, size_t cap, size_t *out_len);
/* Check a claim record strictly and locate its parts as offsets into
 * `rec` (the executor id is 32 bytes at *out_executor_off). Any out pointer
 * may be NULL. ZXV_E_ARG, ZXV_E_FORMAT. */
ZXV_API int32_t zxv_aipi_claim_decode(const uint8_t *rec, size_t len, size_t *out_call_off,
                                      size_t *out_call_len, size_t *out_executor_off,
                                      size_t *out_output_off, size_t *out_output_len,
                                      size_t *out_cert_off, size_t *out_cert_len);

/* ---- The AIPI node handle ----
 * A zxv_aipi is an offline, in-memory IPFS node (block store + pins) with
 * the AIPI context over it. It belongs to the context of the cap that
 * created it and fails closed with it (ZXV_E_POISONED). Calls that store
 * need ZXV_CAP_AIPI from a cap of that context; reads take only the handle.
 * THREADING: internally serialized by its own mutex; any thread may call.
 * Destroy it before its zxv_ctx, after every other call on it returned. */
typedef struct zxv_aipi zxv_aipi;

#define ZXV_AIPI_STORE_MIN  4096u
#define ZXV_AIPI_STORE_MAX  268435456u /* 256 MiB */
#define ZXV_AIPI_BLOCKS_MAX 65536u

/* Routes (zxv_aipi_plan). This library is offline, so it reports only
 * LOCAL (the code is held here) or UNAVAILABLE; the others are the
 * kernel's routes for a networked host, listed so the values are fixed. */
#define ZXV_AIPI_ROUTE_CACHE       1
#define ZXV_AIPI_ROUTE_LOCAL       2
#define ZXV_AIPI_ROUTE_FETCH_LOCAL 3
#define ZXV_AIPI_ROUTE_MESH        4
#define ZXV_AIPI_ROUTE_UNAVAILABLE 5

/* A node holding at most `max_blocks` blocks (1..ZXV_AIPI_BLOCKS_MAX) in
 * `store_bytes` of memory (ZXV_AIPI_STORE_MIN..ZXV_AIPI_STORE_MAX; each
 * block costs its length plus about 100 bytes). node_key (32 bytes, copied;
 * may be NULL) seals private blocks with ChaCha20-Poly1305; without it,
 * private puts are ZXV_E_STATE. Needs ZXV_CAP_AIPI. ZXV_E_ARG, ZXV_E_CAP,
 * ZXV_E_POISONED, ZXV_E_NOMEM. */
ZXV_API int32_t zxv_aipi_create(const zxv_cap *cap, size_t store_bytes, uint32_t max_blocks,
                                const uint8_t *node_key, zxv_aipi **out);
/* NULL is ignored. Wipes the key. */
ZXV_API void zxv_aipi_destroy(zxv_aipi *a);
/* Store and pin `data` (at most ZXV_AIPI_BLOCK_MAX bytes) as one raw block;
 * its CID out. is_private != 0 stores it sealed and never announceable.
 * Putting the same bytes again is a no-op; the same bytes public after
 * private (or the reverse) is ZXV_E_STATE and writes nothing: making
 * something public is a deliberate act, not a side effect, so publishing a
 * block this handle holds private (a public put of its bytes) is always
 * ZXV_E_STATE; there is no call that unseals one. Needs ZXV_CAP_AIPI. ZXV_E_ARG,
 * ZXV_E_TOO_LONG, ZXV_E_STATE (private without a node key, or the
 * visibility conflict), ZXV_E_CAPACITY (store or table full). */
ZXV_API int32_t zxv_aipi_put(const zxv_cap *cap, zxv_aipi *a, const uint8_t *data, size_t len,
                             int32_t is_private, uint8_t out_cid[ZXV_CID_MAX],
                             size_t *out_cid_len);
/* AIPI_GET: the bytes `cid` names, re-hashed before they are returned (R4
 * bytes). The block is fetched and verified BEFORE the caller's buffer is
 * looked at, so a sizing call (buf NULL or cap too small, ZXV_E_BUFFER with
 * *out_len set) is a full verified GET: it counts toward gets_verified in
 * zxv_aipi_info (and a block whose bytes fail their hash is dropped and
 * counted as forbidden on that call). A sizing call followed by the real
 * call counts twice. ZXV_E_NOT_FOUND (not held here: there is no network),
 * ZXV_E_CID (bad CID, an identity CID, or stored bytes that no longer
 * match: those are dropped and counted as forbidden), ZXV_E_STATE (a
 * private block and no node key), ZXV_E_BUFFER, ZXV_E_ARG, ZXV_E_POISONED. */
ZXV_API int32_t zxv_aipi_get(zxv_aipi *a, const uint8_t *cid, size_t cid_len, uint8_t *buf,
                             size_t cap, size_t *out_len);
/* Store a function record (checked strictly; sealed when its privacy is
 * LOCAL) and return its function CID. The module and contract need not be
 * held yet. Needs ZXV_CAP_AIPI. ZXV_E_FORMAT (bad record), ZXV_E_STATE
 * (private without a node key, or visibility conflict), ZXV_E_CAPACITY,
 * ZXV_E_ARG, ZXV_E_CAP, ZXV_E_POISONED. */
ZXV_API int32_t zxv_aipi_publish_fn(const zxv_cap *cap, zxv_aipi *a, const uint8_t *rec,
                                    size_t len, uint8_t out_cid[ZXV_CID_MAX],
                                    size_t *out_cid_len);
/* GET a function record by function CID, verified by hash, decoded
 * strictly, and its codec checked against its intent; the record bytes out
 * (R4). ZXV_E_CID (not a function codec, codec/intent mismatch, bad CID),
 * ZXV_E_NOT_FOUND, ZXV_E_FORMAT (the CID names bytes that are not a
 * function record), ZXV_E_STATE, ZXV_E_BUFFER, ZXV_E_ARG, ZXV_E_POISONED. */
ZXV_API int32_t zxv_aipi_load_fn(zxv_aipi *a, const uint8_t *fn_cid, size_t fn_cid_len,
                                 uint8_t *buf, size_t cap, size_t *out_len);
/* The route a call of fn_cid on `input` would take now (pure: runs and
 * changes nothing). input_private != 0 marks the input private. In this
 * offline library: ZXV_AIPI_ROUTE_LOCAL when the module (and contract) are
 * held, else ZXV_AIPI_ROUTE_UNAVAILABLE. Errors as zxv_aipi_load_fn, plus
 * ZXV_E_TOO_LONG (input over ZXV_AIPI_IO_MAX). */
ZXV_API int32_t zxv_aipi_plan(zxv_aipi *a, const uint8_t *fn_cid, size_t fn_cid_len,
                              const uint8_t *input, size_t in_len, int32_t input_private,
                              int32_t *out_route);
/* Audit counters. Any out pointer may be NULL: blocks indexed, store bytes
 * used, verified GETs, and forbidden GETs (bytes that failed their hash).
 * *out_blocks counts BLOCK-STORE INDEX ENTRIES, not distinct puts: a public
 * block is one entry, a private block two (its plaintext CID and the CID of
 * its sealed ciphertext). Three distinct puts of which one is private (or a
 * private zxv_aipi_publish_fn) therefore report 4. A repeated put adds
 * nothing. *out_gets_verified counts every verified GET, including the ones
 * zxv_aipi_load_fn and zxv_aipi_plan make and zxv_aipi_get sizing calls. */
ZXV_API int32_t zxv_aipi_info(zxv_aipi *a, uint32_t *out_blocks, uint64_t *out_store_used,
                              uint32_t *out_gets_verified, uint32_t *out_forbidden);

/* ===== end aipi ===== */

/* ===== coin (ABI 1.5): Fibonacci coins: VFV ladder faces and Zeckendorf splits (kernel/src/vino_stores, PR #29) and swarm token denominations/octane (kernel/src/swarm/swarm_denom) ===== */

/*
 * Two Fibonacci ladders: the VFV coin ladder (Vino) and the swarm's AI
 * token tiers. Both use the same faces 1, 2, 3, 5, 8, 13, 21, ... and the
 * same unique fewest-pieces (Zeckendorf) form, and both are integer-only.
 *
 * ----- VFV coins (kernel/src/vino_stores) -----
 * FIBONACCI-ONLY (owner decision 2026-10-11): a VFV coin's face is a rung of
 * the ladder, rung r (1..ZXV_COIN_RUNGS) has face F(r+1):
 *   rung  1  2  3  4  5   6   7   8 ...  45
 *   face  1  2  3  5  8  13  21  34 ...  1,836,311,903 (= F(46), the top face)
 * Rung 0 is the ceremonial V0 note: face 0, worth nothing. Any other face
 * (4, 6, 7, 100, ...) is not a coin: ZXV_E_DENOM. To pay an arbitrary
 * amount, split it with zxv_coin_split.
 *
 * MINTING IS NOT IN THIS ABI (yet). The kernel's vino_mint registers coins
 * in a 256-voucher store that is either unbacked (no ledger) or must open
 * its own account on a triple ledger and post through vino_ledger_act's
 * 1.8x coverage gate; its amounts are surplus_real_t, which is a double in
 * the host build libzxv uses. Wrapping it safely needs a coin-store handle
 * bound to the context's ledger and an integer mint path in the kernel, so
 * ZXV_CAP_MINT is used here only by the token wallet below.
 */
#define ZXV_COIN_RUNGS    45u                    /* rungs 1..45 carry value     */
#define ZXV_COIN_TOP_FACE UINT64_C(1836311903)   /* face of rung 45, F(46)      */

/* Face of ladder rung `rung` (0..ZXV_COIN_RUNGS) into *out_face; rung 0 is
 * the V0 note, face 0. ZXV_E_ARG (out_face NULL), ZXV_E_RANGE (rung > 45). */
ZXV_API int32_t zxv_coin_face(uint32_t rung, uint64_t *out_face);

/* The rung (1..ZXV_COIN_RUNGS) whose face is exactly `face` (face 1 is
 * rung 1: the ladder has one 1-coin, not two). ZXV_E_ARG
 * (out_rung NULL), ZXV_E_DENOM (0 or any face not on the ladder, including
 * Fibonacci numbers above the top face). */
ZXV_API int32_t zxv_coin_rung(uint64_t face, uint32_t *out_rung);

/* The coins for a whole `amount`: its Zeckendorf split into rungs, largest
 * first. Amounts at or above the top face take floor(amount / top) coins of
 * rung 45 first; the rest uses each lower rung at most once and never two
 * neighbouring rungs, which makes the split unique and the fewest coins
 * possible. Two-call on *out_count, like zxv_tokenize: `rungs` is a plain
 * array (no terminator); *out_count is always the exact coin count; NULL
 * rungs (unless the split is empty) or cap < count gives ZXV_E_BUFFER with
 * *out_count set and nothing written. amount 0 is an empty split (ZXV_OK,
 * *out_count = 0). ZXV_E_ARG (out_count NULL), ZXV_E_RANGE (the split would
 * need more than 4,294,967,294 coins: amounts of about 7.9e18 and up). */
ZXV_API int32_t zxv_coin_split(uint64_t amount, uint32_t *rungs, size_t cap, size_t *out_count);

/*
 * ----- Swarm AI token denominations and octane (kernel/src/swarm/swarm_denom) -----
 * Tier k (0..ZXV_TOKEN_TIERS-1) has face F(k+2): 1, 2, 3, 5, 8, ... F(65).
 * Tier 0 is the plain token; tiers ZXV_TOKEN_SUPER_TIER (face 144) and up are
 * super tokens. A holding is an array counts[ZXV_TOKEN_TIERS] (tokens held
 * per tier); its value is sum counts[k] * face(k). OCTANE is the densest
 * tier a holding may use; the named grades are like a fuel pump's, any
 * octane 0..63 is allowed. The CANONICAL form of a value at octane o puts
 * floor(value / face(o)) tokens on tier o and the remainder in Zeckendorf
 * form below it (each lower tier 0 or 1 token, no two neighbours): every
 * value has exactly one canonical form per octane.
 *
 * NO PREMIUM. A dense token is worth exactly its face in plain tokens.
 * Density earns no interest, costs no fee, and buys no extra allotment or
 * budget priority; changing octane re-expresses the same value and never
 * changes it. What density does buy is fewer pieces (1,000,000 tokens are
 * 1,000,000 pieces at PLAIN, 17 at RACING): it helps wherever tokens are
 * handled one piece at a time (receipts, signatures, ledger rows), and it
 * does not make a model faster or an amount shorter on the wire.
 *
 * The pure functions below take no capability. Out-of-range tiers and
 * octanes are ZXV_E_RANGE; a value that would pass 64 bits is ZXV_E_RANGE
 * and changes nothing.
 */
#define ZXV_TOKEN_TIERS      64u
#define ZXV_TOKEN_SUPER_TIER 10u
#define ZXV_OCTANE_PLAIN     0u  /* face 1 only            */
#define ZXV_OCTANE_REGULAR   4u  /* up to face 8           */
#define ZXV_OCTANE_MIDGRADE  9u  /* up to face 89          */
#define ZXV_OCTANE_PREMIUM   14u /* up to face 987         */
#define ZXV_OCTANE_RACING    23u /* up to face 75025       */
#define ZXV_OCTANE_MAX       63u /* up to face F(65)       */
/* Longest Fibonacci code of an amount: 92 Zeckendorf bits + the closing 1. */
#define ZXV_TOKEN_CODE_MAX_BITS 93u
/* Largest buffer the code functions take (bit positions are uint32_t). */
#define ZXV_TOKEN_CODE_MAX_BYTES 536870911u

/* Face of `tier` and whether it is a super token (1) or not (0). Either out
 * may be NULL. ZXV_E_RANGE (tier >= 64). */
ZXV_API int32_t zxv_token_face(uint32_t tier, uint64_t *out_face, int32_t *out_is_super);

/* The tier whose face is exactly `face`. ZXV_E_ARG (out_tier NULL),
 * ZXV_E_DENOM (not one of the 64 faces). */
ZXV_API int32_t zxv_token_tier(uint64_t face, uint32_t *out_tier);

/* Value of a holding, and its number of pieces (sum of counts); either out
 * may be NULL. ZXV_E_ARG (counts NULL), ZXV_E_RANGE (value past 64 bits). */
ZXV_API int32_t zxv_token_value(const uint64_t counts[64], uint64_t *out_value,
                                uint64_t *out_pieces);

/* The canonical form of `value` at `octane` into out_counts[64]. ZXV_E_ARG
 * (out_counts NULL), ZXV_E_RANGE (octane >= 64). */
ZXV_API int32_t zxv_token_canonical(uint64_t value, uint32_t octane, uint64_t out_counts[64]);

/* *out_canonical = 1 if counts is the canonical form of its value at
 * `octane`, else 0 (also 0 when a tier above the octane is used).
 * ZXV_E_ARG (a pointer NULL), ZXV_E_RANGE (octane >= 64, value past 64
 * bits). */
ZXV_API int32_t zxv_token_is_canonical(const uint64_t counts[64], uint32_t octane,
                                       int32_t *out_canonical);

/* Rewrite counts (in place) into the canonical form of its value at
 * `octane`; the value never changes. ZXV_E_ARG, ZXV_E_RANGE (octane, or the
 * value past 64 bits); counts is unchanged on any failure. */
ZXV_API int32_t zxv_token_compress(uint64_t counts[64], uint32_t octane);

/* Fuse (in place): one tier-k and one tier-(k+1) token become one
 * tier-(k+2) token (F(k+2) + F(k+3) = F(k+4)). ZXV_E_ARG (counts NULL),
 * ZXV_E_RANGE (octane >= 64, tier > 61, or the tier-(k+2) count is full),
 * ZXV_E_STATE (tier k+2 is denser than the octane allows), ZXV_E_FUNDS
 * (no tier-k or no tier-(k+1) token). counts is unchanged on failure. */
ZXV_API int32_t zxv_token_fuse(uint64_t counts[64], uint32_t octane, uint32_t tier);

/* Split (in place): one tier-k token becomes one tier-(k-1) and one
 * tier-(k-2) token; tier 1 becomes two plain tokens. Tier 0 is atomic.
 * ZXV_E_ARG (counts NULL), ZXV_E_RANGE (tier 0 or >= 64, or a lower count
 * is full), ZXV_E_FUNDS (no tier-k token). counts is unchanged on failure.
 * Splitting never needs an octane: it only makes a holding plainer. */
ZXV_API int32_t zxv_token_split(uint64_t counts[64], uint32_t tier);

/* Number of bits in the Fibonacci code of `value` (2..93). ZXV_E_ARG
 * (out_bits NULL), ZXV_E_RANGE (value is UINT64_MAX: not encodable). */
ZXV_API int32_t zxv_token_code_bits(uint64_t value, uint32_t *out_bits);

/* Append the Fibonacci code of `value` (Zeckendorf bits of value+1, lowest
 * first, then a closing 1, so every code ends in "11" and needs no length
 * field) at bit *inout_bit of buf (bit i is bit i%8 of byte i/8). Only the
 * code's bits are written; *inout_bit advances past them. ZXV_E_ARG (a
 * pointer NULL), ZXV_E_TOO_LONG (buf_len > ZXV_TOKEN_CODE_MAX_BYTES),
 * ZXV_E_RANGE (value is UINT64_MAX, or *inout_bit > buf_len * 8),
 * ZXV_E_BUFFER (the code does not fit; nothing written, see
 * zxv_token_code_bits). */
ZXV_API int32_t zxv_token_code_put(uint8_t *buf, size_t buf_len, uint32_t *inout_bit,
                                   uint64_t value);

/* Read one Fibonacci code from bit *inout_bit of buf; *inout_bit advances
 * past it. ZXV_E_ARG, ZXV_E_TOO_LONG, ZXV_E_RANGE (*inout_bit past the
 * end), ZXV_E_FORMAT (no closing "11" before the end, or a code whose value
 * passes 64 bits). Nothing is written on failure. */
ZXV_API int32_t zxv_token_code_get(const uint8_t *buf, size_t buf_len, uint32_t *inout_bit,
                                   uint64_t *out_value);

/* ----- A token wallet: a holding kept canonical at its own octane -----
 * The wallet always holds its value in canonical form at its octane.
 * Every change is gated on ZXV_CAP_MINT, not ZXV_CAP_SWARM: the wallet is
 * not tied to a zxv_swarm budget (the kernel's budget lockstep, payout and
 * consume, is not in this ABI), so tokens enter it only by
 * zxv_token_wallet_mint, which creates them, and ZXV_CAP_SWARM's budget
 * authority would not cover that. Reads need no capability. Every call
 * fails with ZXV_E_POISONED once the wallet's context halts. A wallet is
 * internally serialized (any thread may call it); destroy it before the
 * zxv_ctx it was created from (R6). */
typedef struct zxv_token_wallet zxv_token_wallet;

/* An empty wallet at `octane`, labelled `model_id` (a free label, e.g. the
 * swarm model that holds it). Needs MINT. ZXV_E_CAP, ZXV_E_POISONED,
 * ZXV_E_ARG (cap or out_wallet NULL), ZXV_E_RANGE (octane
 * >= 64), ZXV_E_NOMEM. */
ZXV_API int32_t zxv_token_wallet_create(const zxv_cap *cap, uint32_t model_id, uint32_t octane,
                                        zxv_token_wallet **out_wallet);
ZXV_API void zxv_token_wallet_destroy(zxv_token_wallet *wallet);

/* Create `amount` new tokens in the wallet. Needs MINT (from the wallet's
 * context). ZXV_E_ARG, ZXV_E_CAP, ZXV_E_POISONED, ZXV_E_RANGE (the value
 * would pass 64 bits; nothing changes). */
ZXV_API int32_t zxv_token_wallet_mint(const zxv_cap *cap, zxv_token_wallet *wallet,
                                      uint64_t amount);

/* Spend (burn) `amount`; the change stays canonical. Needs MINT.
 * ZXV_E_ARG, ZXV_E_CAP, ZXV_E_POISONED, ZXV_E_FUNDS (the wallet holds less;
 * nothing changes). */
ZXV_API int32_t zxv_token_wallet_spend(const zxv_cap *cap, zxv_token_wallet *wallet,
                                       uint64_t amount);

/* Move `amount` from one wallet to another. The payer keeps its change at
 * its octane; the receiver gets the payment at ITS octane. The total is
 * conserved exactly. Needs MINT from the context both wallets belong to;
 * from == to only checks the funds. ZXV_E_ARG, ZXV_E_CAP (cap or a wallet
 * from another context), ZXV_E_POISONED, ZXV_E_FUNDS (`from` holds less),
 * ZXV_E_RANGE (`to` would pass 64 bits). Neither wallet changes on
 * failure. */
ZXV_API int32_t zxv_token_wallet_transfer(const zxv_cap *cap, zxv_token_wallet *from,
                                          zxv_token_wallet *to, uint64_t amount);

/* Change the octane (up = denser, down = plainer) and re-express the same
 * value canonically. Needs MINT. ZXV_E_ARG, ZXV_E_CAP, ZXV_E_POISONED,
 * ZXV_E_RANGE (octane >= 64). */
ZXV_API int32_t zxv_token_wallet_set_octane(const zxv_cap *cap, zxv_token_wallet *wallet,
                                            uint32_t octane);

/* The wallet's label, octane, value and piece count; any out may be NULL.
 * ZXV_E_ARG (wallet NULL), ZXV_E_POISONED. */
ZXV_API int32_t zxv_token_wallet_info(zxv_token_wallet *wallet, uint32_t *out_model_id,
                                      uint32_t *out_octane, uint64_t *out_value,
                                      uint64_t *out_pieces);

/* The wallet's counts per tier into out_counts[64]. ZXV_E_ARG,
 * ZXV_E_POISONED. */
ZXV_API int32_t zxv_token_wallet_counts(zxv_token_wallet *wallet, uint64_t out_counts[64]);

/* ===== end coin ===== */

/* ===== fee (ABI 1.5): 0.08889% assurance fee and its 50/25/15/10 split (kernel/src/pay/pay_assure) ===== */

/*
 * The assurance fee, exact and integer-only (kernel/src/pay/pay_assure.h).
 *   fee(g) = floor(g * 8889 / 10,000,000)        (0.08889% of the gross)
 * g is in the unit's own minor measure (money minor units, compute
 * token-cycles, bandwidth bytes, storage byte-hours). The product is formed
 * in 128 bits, so every g up to UINT64_MAX is exact; fee(1124) = 0 and
 * fee(1125) = 1. The fee is charged ONCE per transaction, on its gross:
 * never on a fee, never compounded.
 *
 * CARRY. The sub-unit remainder of an account (0 <= r < 10^7) is carried
 * to its next charge: fee = floor((g * 8889 + r) / 10^7), r' = the rest.
 * Starting from r = 0, many small charges then pay exactly what one charge
 * of their sum pays. The caller stores r per account.
 *
 * THE FOUR BUCKETS. A fee is split 50% reserve floor (backs Vino), 25%
 * V-Bill dividend pool, 15% infrastructure / node bounties, 10%
 * regenerative capital. Buckets 1-3 get floor(fee * pct / 100); the reserve
 * floor gets the rest, so every remainder unit goes to the reserve floor
 * and the four parts always sum to the fee exactly.
 *
 * All functions are pure: no context, no capability.
 * HONEST LIMITS: these are the arithmetic rules only. How a fee, a reserve
 * or a dividend pool is treated for tax, securities or consumer law is a
 * question for counsel; nothing here is a licence or a certification.
 */
#define ZXV_FEE_NUM 8889u     /* 0.08889% = 8889 / 10^7 */
#define ZXV_FEE_DEN 10000000u /* 10^7; a carry is always below this */

#define ZXV_FEE_RESERVE_FLOOR   0 /* 50% + every remainder unit */
#define ZXV_FEE_VBILL_DIVIDEND  1 /* 25%                        */
#define ZXV_FEE_INFRA_BOUNTY    2 /* 15%                        */
#define ZXV_FEE_REGEN_CAPITAL   3 /* 10%                        */
#define ZXV_FEE_BUCKETS         4

#define ZXV_FEE_UNIT_MONEY     0 /* minor units of a ledger asset */
#define ZXV_FEE_UNIT_COMPUTE   1 /* token-cycles                  */
#define ZXV_FEE_UNIT_BANDWIDTH 2 /* bytes                         */
#define ZXV_FEE_UNIT_STORAGE   3 /* byte-hours                    */
#define ZXV_FEE_UNITS          4

/* fee(gross) with no carry. ZXV_E_ARG (out_fee NULL). */
ZXV_API int32_t zxv_fee_assure(uint64_t gross, uint64_t *out_fee);

/* The fee on `gross` with the account's incoming remainder `rem_in`;
 * *out_rem is the remainder to store for its next charge. Either out may
 * be NULL. ZXV_E_RANGE (rem_in >= ZXV_FEE_DEN: a corrupt carry; nothing
 * written). */
ZXV_API int32_t zxv_fee_assure_carry(uint64_t rem_in, uint64_t gross, uint64_t *out_fee,
                                     uint64_t *out_rem);

/* Split `fee` into out[ZXV_FEE_*] (four parts summing to fee exactly; the
 * reserve floor takes every remainder unit). ZXV_E_ARG (out NULL). The
 * partition is re-checked before it is handed out; ZXV_E_INTERNAL would
 * mean the kernel broke it (nothing written). */
ZXV_API int32_t zxv_fee_split(uint64_t fee, uint64_t out[4]);

/* The default policy, any out may be NULL:
 *   out_pct[4]          the bucket shares in percent: 50, 25, 15, 10;
 *   *out_allow_below    0: a contribution below the fee is raised to it;
 *   out_rate_num/den[4] the posted VFV store-credit rate per unit kind
 *                       (ZXV_FEE_UNIT_*) for a voluntary contribution above
 *                       the fee: money 1/1, the others 0/1 (unset: the
 *                       excess earns no credit until a rate is posted);
 *   *out_mult_num/den   the operator's credit multiplier, 1/1;
 *   *out_commons_num/den the no-monopoly cap on one recipient's share of
 *                       the non-money commons pool, 8/21.
 * Always ZXV_OK. */
ZXV_API int32_t zxv_fee_policy_default(uint32_t out_pct[4], int32_t *out_allow_below,
                                       uint64_t out_rate_num[4], uint64_t out_rate_den[4],
                                       uint64_t *out_mult_num, uint64_t *out_mult_den,
                                       uint64_t *out_commons_num, uint64_t *out_commons_den);

/* ===== end fee ===== */

/* ===== vhub (ABI 1.5): V-Bill hub: test currencies, signed exact pegs, quotes, swaps and limit rules (kernel/src/vhub) ===== */

/* A demonstration of cross-currency clearing through V-Bills: each test
 * currency has ONE exact peg against the V-Bill, set only by its issuer
 * with an ML-DSA-87 signature, and a swap from currency A to currency B
 * goes A -> V-Bill -> B as ONE balanced, all-or-nothing posting on the
 * hub's own in-process pay_ledger (kernel/src/vhub, kernel/src/pay):
 *
 *   sender -a, A pool +a;  A V-Bill float -v, B V-Bill float +v;
 *   B pool -b, receiver +b;   v = a*denA/numA,  b = v*numB/denB.
 *
 * EXACT. A peg is the rational num/den: num minor units of the currency
 * per den V-Bills. Both divisions must be exact or the swap is refused
 * (ZXV_E_RANGE); nothing is rounded and there is no float rate anywhere.
 * zxv_vhub_quote gives the largest exact amount not above a requested one.
 * The A -> B rate is implied by the two pegs, so the hub never quotes two
 * prices for one swap.
 *
 * BACKING. Each currency's V-Bill float holds what was deposited against
 * its peg and its pool holds what its issuer provided; a swap that would
 * take either below zero is refused (ZXV_E_FUNDS). An exact peg removes
 * rounding, not exchange-rate risk.
 *
 * RULES. Every rule of the sending AND the receiving currency must allow a
 * swap. A currency with no rule refuses every swap through it (fail closed,
 * ZXV_E_STATE), so add a rule to each currency before swapping. Only the
 * built-in limit rule is exposed: at most `limit` minor units out of a
 * currency per sender per rule epoch (ZXV_E_LIMIT when it refuses). The
 * kernel's arbitrary C rule plug-ins are NOT exposed: a host callback would
 * run foreign code in the middle of a swap across twelve language runtimes
 * (exceptions unwinding through C, re-entry into the hub, collected user
 * pointers), which is not clearly safe; they stay a kernel-level feature.
 *
 * ACCOUNTS. The pay ledger's rails: DEBIT is what an account holds (A),
 * CREDIT what it owes (L, issuers only), EQUITY = DEBIT - CREDIT (E), so
 * A = L + E on every account and, summed over each asset, DEBIT == CREDIT.
 * zxv_vhub_check verifies this after the fact; every posting re-checks it.
 *
 * CAPABILITIES. Creating a hub, adding currencies and rules, setting pegs,
 * opening and funding accounts and swapping need ZXV_CAP_LEDGER_POST;
 * quotes, balances, currency info and the check need ZXV_CAP_LEDGER_READ.
 * The cap must belong to the hub's context (else ZXV_E_CAP), and a
 * poisoned context refuses every call with ZXV_E_POISONED.
 *
 * THREADING (R6). A zxv_vhub may be used from several threads; its calls
 * are serialized by an internal lock. Destroy it once, after every other
 * call on it has returned, and before the zxv_ctx it was created from.
 *
 * HONEST LIMITS. A demo hub with test currencies only (ISO 4217 codes are
 * refused) and no real money: its ledger is in process memory, separate
 * from the context's ledger, and is lost on destroy. No network: it is
 * designed to support settlement rails such as PAPSS but is connected to
 * none, nor to any central bank or issuer. Peg signatures are verified
 * with ML-DSA-87 (not FIPS 140-3 validated). V-Bills are likely securities
 * in many jurisdictions; real use needs legal review and a participating
 * issuer. A rule reports what it decided; it is not itself a legal check. */

typedef struct zxv_vhub zxv_vhub; /* one demo hub and its ledger */

#define ZXV_VHUB_MAX_CCY       8u  /* currency slots 0..7                   */
#define ZXV_VHUB_MAX_RULES     4u  /* limit rules per currency              */
#define ZXV_VHUB_LIMIT_SENDERS 16u /* senders one limit tracks per epoch    */
#define ZXV_VHUB_PEG_MSG_BYTES 40u /* zxv_vhub_peg_msg output size          */
#define ZXV_VHUB_CODE_MAX      12u /* test currency code length             */
#define ZXV_VHUB_AMOUNT_MAX    (UINT64_C(1) << 59) /* any one amount; balances */
                                                    /* stay below 2^62          */
/* The V-Bill issuer's account: always account 0 of a hub's ledger. */
#define ZXV_VHUB_VBILL_ISSUER  0u

/* Create a hub on the cap's context (needs LEDGER_POST). Its ledger starts
 * with the V-Bill asset ("VBL", 0 minor units) and the V-Bill issuer's
 * account (ZXV_VHUB_VBILL_ISSUER). seed: 32 bytes from which every
 * posting's idempotency key and UETR are derived (SHA-256(seed || tag ||
 * n)); NULL draws a fresh random seed. A rule epoch starts at 0.
 * Returns ZXV_E_ARG (out NULL), ZXV_E_CAP, ZXV_E_POISONED, ZXV_E_NOMEM. */
ZXV_API int32_t zxv_vhub_create(const zxv_cap *cap, const uint8_t *seed, zxv_vhub **out);
/* Destroy a hub (R6: before its context). NULL is ignored. */
ZXV_API void zxv_vhub_destroy(zxv_vhub *hub);

/* Register test currency `slot` (0..7): its asset `code` (3..12 bytes of
 * A-Z and 0-9; not an ISO 4217 code, not "VBL" or "VFV"), its minor units
 * (0..4) and its issuer's ML-DSA-87 public key (copied; it alone may set
 * the peg). Opens three accounts: the issuer's, the hub's pool of the
 * currency and the currency's V-Bill float, then funds the pool with
 * `pool_funding` (issued by the currency's issuer) and the float with
 * `vbill_backing` V-Bills (issued by the V-Bill issuer); either may be 0.
 * The peg starts unset: no quote or swap until zxv_vhub_set_peg.
 * Returns ZXV_E_ARG (NULL hub, code or key; empty code), ZXV_E_UTF8, ZXV_E_FORMAT (code
 * characters), ZXV_E_TOO_LONG / ZXV_E_RANGE (code length, slot >= 8,
 * minor units > 4, an amount > ZXV_VHUB_AMOUNT_MAX), ZXV_E_CURRENCY (an
 * ISO 4217 code), ZXV_E_DUPLICATE (slot or code taken), ZXV_E_CAPACITY
 * (ledger full), ZXV_E_CAP, ZXV_E_POISONED. A failure registers nothing. */
ZXV_API int32_t zxv_vhub_add_ccy(const zxv_cap *cap, zxv_vhub *hub, uint8_t slot, const char *code,
                                 uint8_t minor_units, const uint8_t issuer_pk[2592],
                                 uint64_t pool_funding, uint64_t vbill_backing);
/* A currency's accounts and current peg (needs LEDGER_READ). Any out
 * pointer may be NULL. An unset peg reads num = den = epoch = 0.
 * Returns ZXV_E_ARG, ZXV_E_NOT_FOUND (slot not registered), ZXV_E_CAP,
 * ZXV_E_POISONED. */
ZXV_API int32_t zxv_vhub_ccy_info(const zxv_cap *cap, zxv_vhub *hub, uint8_t slot,
                                  uint32_t *out_issuer_account, uint32_t *out_pool_account,
                                  uint32_t *out_vfloat_account, uint64_t *out_peg_num,
                                  uint64_t *out_peg_den, uint64_t *out_peg_epoch);

/* Pure: the exact 40 bytes an issuer signs to set a peg: the ASCII tag
 * "zxv/vhub-peg/v1" (15 bytes) || slot (1 byte) || num || den || epoch
 * (each 8 bytes little-endian). Sign them with zxv_mldsa87_sign and an
 * empty FIPS 204 context. Returns ZXV_E_ARG if out is NULL. */
ZXV_API int32_t zxv_vhub_peg_msg(uint8_t slot, uint64_t num, uint64_t den, uint64_t epoch,
                                 uint8_t out[40]);
/* Set a currency's peg to num/den (num minor units per den V-Bills) at
 * `epoch`, which must be above the current one, given the issuer's
 * ML-DSA-87 signature (empty context) over zxv_vhub_peg_msg(slot, num, den,
 * epoch). The new peg applies to the next quote and swap. Needs
 * LEDGER_POST. Returns ZXV_E_ARG (NULL hub or sig), ZXV_E_NOT_FOUND,
 * ZXV_E_RANGE (num or den 0), ZXV_E_PEG (epoch not above the current one:
 * a stale or replayed peg), ZXV_E_VERIFY (not the issuer's signature over
 * these numbers), ZXV_E_NOTIMPL (built with ZXV_NO_PQC), ZXV_E_CAP,
 * ZXV_E_POISONED. A refused peg changes nothing. */
ZXV_API int32_t zxv_vhub_set_peg(const zxv_cap *cap, zxv_vhub *hub, uint8_t slot, uint64_t num,
                                 uint64_t den, uint64_t epoch, const uint8_t sig[4627]);

/* Open a holder account in currency `slot` for `owner` (an id of the
 * caller's choosing) (needs LEDGER_POST). Returns ZXV_E_ARG,
 * ZXV_E_NOT_FOUND, ZXV_E_CAPACITY (the ledger holds 512 accounts),
 * ZXV_E_CAP, ZXV_E_POISONED. */
ZXV_API int32_t zxv_vhub_open_account(const zxv_cap *cap, zxv_vhub *hub, uint8_t slot,
                                      uint32_t owner, uint32_t *out_account);
/* Test faucet (needs LEDGER_POST): issue `amount` to a holder account or a
 * currency's pool (by that currency's issuer), or V-Bills to a currency's
 * V-Bill float (by the V-Bill issuer). The issuer's CREDIT rises by the
 * same amount. Returns ZXV_E_ARG (NULL hub, or an issuer account),
 * ZXV_E_NOT_FOUND (no such account), ZXV_E_RANGE (amount 0 or above
 * ZXV_VHUB_AMOUNT_MAX, or a balance would reach 2^62), ZXV_E_CAP,
 * ZXV_E_POISONED. */
ZXV_API int32_t zxv_vhub_issue(const zxv_cap *cap, zxv_vhub *hub, uint32_t account,
                               uint64_t amount);
/* One account's rails (needs LEDGER_READ): DEBIT (held), CREDIT (owed)
 * and EQUITY (DEBIT - CREDIT). Any out pointer may be NULL. Returns
 * ZXV_E_ARG, ZXV_E_NOT_FOUND, ZXV_E_CAP, ZXV_E_POISONED. */
ZXV_API int32_t zxv_vhub_balance(const zxv_cap *cap, zxv_vhub *hub, uint32_t account,
                                 uint64_t *out_debit, uint64_t *out_credit, int64_t *out_equity);

/* Add the built-in limit rule to currency `slot` (needs LEDGER_POST): at
 * most `limit` minor units of the currency out per sender per rule epoch;
 * inflows are not counted. One limit tracks 16 distinct senders per epoch
 * and refuses a 17th (fail closed). Several limits on one currency all
 * apply. Returns ZXV_E_ARG, ZXV_E_NOT_FOUND, ZXV_E_CAPACITY (4 rules
 * already), ZXV_E_CAP, ZXV_E_POISONED. */
ZXV_API int32_t zxv_vhub_add_limit(const zxv_cap *cap, zxv_vhub *hub, uint8_t slot,
                                   uint64_t limit);
/* Start the next rule epoch: every limit starts every sender at zero
 * (needs LEDGER_POST). *out_epoch (may be NULL) gets the new epoch.
 * Returns ZXV_E_ARG, ZXV_E_RANGE (epoch at UINT64_MAX), ZXV_E_CAP,
 * ZXV_E_POISONED. */
ZXV_API int32_t zxv_vhub_next_epoch(const zxv_cap *cap, zxv_vhub *hub, uint64_t *out_epoch);

/* Pure read (needs LEDGER_READ): the largest exact amount of `from_slot`
 * not above `want` (0 if none), and the V-Bills and `to_slot` units it
 * would carry. Does not run rules or check balances, so a quoted swap may
 * still be refused. Any out pointer may be NULL. Returns ZXV_E_ARG (NULL
 * hub, from == to), ZXV_E_NOT_FOUND, ZXV_E_PEG (either peg unset),
 * ZXV_E_RANGE (want > ZXV_VHUB_AMOUNT_MAX), ZXV_E_CAP, ZXV_E_POISONED. */
ZXV_API int32_t zxv_vhub_quote(const zxv_cap *cap, zxv_vhub *hub, uint8_t from_slot,
                               uint8_t to_slot, uint64_t want, uint64_t *out_amount,
                               uint64_t *out_vbills, uint64_t *out_received);
/* Swap `amount` minor units of `from_slot` from holder account `sender` to
 * holder account `receiver` in `to_slot`, as one six-line posting at
 * `tick` (a caller-supplied time, never the wall clock). Needs
 * LEDGER_POST. On ZXV_OK *out_vbills and *out_received (either may be
 * NULL) get the V-Bills moved and the units received. Returns ZXV_E_ARG
 * (NULL hub, from == to, or a hub-internal account), ZXV_E_NOT_FOUND
 * (slot or account), ZXV_E_CURRENCY (an account of another currency),
 * ZXV_E_PEG (either peg unset), ZXV_E_RANGE (amount 0, above
 * ZXV_VHUB_AMOUNT_MAX, not exact under the pegs, or a V-Bill amount or
 * payout above ZXV_VHUB_AMOUNT_MAX), ZXV_E_STATE (a
 * currency has no rule), ZXV_E_LIMIT (a limit rule refused), ZXV_E_FUNDS
 * (the sender, A's V-Bill float or B's pool would go below zero),
 * ZXV_E_CAP, ZXV_E_POISONED, ZXV_E_INVARIANT (the ledger broke an
 * invariant; the context is poisoned). A refused swap changes nothing. */
ZXV_API int32_t zxv_vhub_swap(const zxv_cap *cap, zxv_vhub *hub, uint8_t from_slot,
                              uint8_t to_slot, uint32_t sender, uint32_t receiver,
                              uint64_t amount, uint64_t tick, uint64_t *out_vbills,
                              uint64_t *out_received);
/* Verify the whole hub ledger (needs LEDGER_READ): per account A = L + E
 * with no negative rail and no CREDIT outside an issuer, per asset sum
 * DEBIT == sum CREDIT, and the posting hash chain. *out_postings (may be
 * NULL) gets the postings made so far (issues and swaps). Returns
 * ZXV_E_INVARIANT (and poisons the context) on a failure, ZXV_E_ARG,
 * ZXV_E_CAP, ZXV_E_POISONED. */
ZXV_API int32_t zxv_vhub_check(const zxv_cap *cap, zxv_vhub *hub, uint64_t *out_postings);

/* ===== end vhub ===== */

/* ===== freight (ABI 1.5): Freight packets: 168 erasure-coded 21-byte rows, split/join, op streams (kernel/src/freight) ===== */

/* A FREIGHT carries up to 3360 payload bytes as 168 smart packets of 21
 * bytes (one UBH-168 frame each): byte 0 is the row index 0..167, bytes
 * 1..20 are row data. Rows 0..k-1 hold the payload verbatim (zero padded);
 * rows k..167 are parity of a systematic Cauchy Reed-Solomon code over
 * GF(2^8) (polynomial 0x11d), coded column by column. ANY k distinct rows
 * rebuild the payload, so up to 168 - k rows may be lost. Choosing k is the
 * trade: payload per freight is k * 20 bytes, loss tolerance is 168 - k rows.
 *
 * The 64-byte FREIGHT HEADER travels separately (in the outer message):
 *   "ZXFH" version k flags 0 | payload_len seq seq_count 0 0 (u16 LE)
 *   | freight_id (u64 LE) | expand_limit (u64 LE) | SHA-256 of the payload
 * It always crosses this ABI in that serialized form. A payload larger than
 * one freight is split into a SET of up to ZXV_FREIGHT_MAX_SET freights that
 * share freight_id; member `seq` carries bytes [seq * k * 20, ...).
 *
 * The payload may be an OP STREAM ("ZXOP" header, then LITERAL, COPY (an
 * LZ77-style back reference, overlap allowed), CIDREF (bytes the receiver
 * resolves by content id through a callback) and SEED (a built-in
 * deterministic generator) ops) that the receiver expands. Expansion checks
 * the declared total against the caller's limit before writing a byte and
 * refuses any op that would pass it, so a stream cannot expand past what the
 * receiver allowed.
 *
 * HONEST LIMITS. Erasure coding only: a lost row is recovered, a CORRUPTED
 * row is detected (the payload SHA-256 fails, ZXV_E_VERIFY) but not located
 * or repaired; drop rows you have reason to distrust and decode again. The
 * header itself is not protected by the code and is not authenticated:
 * sign it (or the outer message) if the sender matters. Parity and row
 * indices make a freight larger than its payload (3528 wire bytes for at
 * most 3360). The op stream gives no size guarantee: random data encodes
 * slightly LARGER than itself, COPY helps only data that actually repeats,
 * CIDREF only names bytes the receiver must already hold or fetch, and SEED
 * only reproduces output of the built-in generators (no encoder searches
 * for seeds). No information is created: an expanded stream holds what its
 * ops carry plus what the receiver supplies.
 *
 * BYTES OUT ("R4 for bytes" below): *out_len always receives the length
 * the result needs; if that is nonzero and buf is NULL or cap is below it,
 * the call returns ZXV_E_BUFFER and writes nothing to buf. No NUL is added.
 *
 * Every function here is pure (no capability, no context); the join and
 * writer handles hold only their own memory. */
#define ZXV_FREIGHT_PACKET_BYTES 21u
#define ZXV_FREIGHT_ROW_PAYLOAD  20u
#define ZXV_FREIGHT_ROWS         168u
#define ZXV_FREIGHT_BYTES        3528u /* 168 * 21: one encoded freight     */
#define ZXV_FREIGHT_MAX_PAYLOAD  3360u /* at k = 168 (no loss tolerated)    */
#define ZXV_FREIGHT_HEADER_BYTES 64u
#define ZXV_FREIGHT_VERSION      1u
#define ZXV_FREIGHT_MAX_SET      16384u /* freights in one set              */
/* Largest set payload: ZXV_FREIGHT_MAX_SET * ZXV_FREIGHT_MAX_PAYLOAD. */
#define ZXV_FREIGHT_SET_MAX_BYTES UINT64_C(55050240)
#define ZXV_FREIGHT_F_OPSTREAM   0x01u /* the (set's) payload is an op stream */

/* Build the header of a single freight (seq 0 of 1) carrying payload[0..len)
 * with k data rows (1..168): it hashes the payload. flags: 0 or
 * ZXV_FREIGHT_F_OPSTREAM; expand_limit must be 0 without that flag.
 * ZXV_E_ARG: NULL out_header, NULL payload with len > 0, k out of range,
 * unknown flags, or expand_limit without OPSTREAM. ZXV_E_TOO_LONG: len is
 * over k * 20. */
ZXV_API int32_t zxv_freight_header_init(uint64_t freight_id, uint8_t k, uint8_t flags,
                                        uint64_t expand_limit, const uint8_t *payload, size_t len,
                                        uint8_t out_header[64]);
/* Serialize a header from its fields (for a set member, or to re-create a
 * header received field by field). Same rules as zxv_freight_header_check;
 * any violation is ZXV_E_ARG. The version is always ZXV_FREIGHT_VERSION. */
ZXV_API int32_t zxv_freight_header_serialize(uint8_t k, uint8_t flags, uint16_t payload_len,
                                             uint16_t seq, uint16_t seq_count, uint64_t freight_id,
                                             uint64_t expand_limit, const uint8_t payload_sha256[32],
                                             uint8_t out_header[64]);
/* Parse and check a serialized header. Any out pointer may be NULL; all are
 * written only on ZXV_OK. ZXV_E_ARG: header NULL. ZXV_E_FORMAT: bad magic,
 * nonzero reserved bytes, or a field that fails the check. */
ZXV_API int32_t zxv_freight_header_parse(const uint8_t header[64], uint8_t *out_version,
                                         uint8_t *out_k, uint8_t *out_flags,
                                         uint16_t *out_payload_len, uint16_t *out_seq,
                                         uint16_t *out_seq_count, uint64_t *out_freight_id,
                                         uint64_t *out_expand_limit,
                                         uint8_t out_payload_sha256[32]);
/* ZXV_OK if the header is well formed: magic, version 1, k 1..168, known
 * flags, payload_len <= k * 20, seq_count 1..16384, seq < seq_count, and
 * expand_limit 0 unless OPSTREAM. Else ZXV_E_FORMAT (ZXV_E_ARG if NULL). */
ZXV_API int32_t zxv_freight_header_check(const uint8_t header[64]);

/* Encode the 168 smart packets (3528 bytes, rows in order) of the freight
 * described by `header`. payload/len must be exactly what the header was
 * built for: a length mismatch is ZXV_E_ARG, a hash mismatch ZXV_E_VERIFY.
 * ZXV_E_FORMAT: the header fails the check. */
ZXV_API int32_t zxv_freight_encode(const uint8_t header[64], const uint8_t *payload, size_t len,
                                   uint8_t out_packets[3528]);
/* Rebuild the payload from any packets that arrived: `packets` holds
 * packets_len / 21 packets of 21 bytes each, in any order; duplicates and
 * row indices above 167 are ignored, and the first k distinct rows are
 * used. Bytes out (R4 for bytes): *out_len receives the payload length; if
 * buf is NULL or cap is below it, ZXV_E_BUFFER and nothing is decoded.
 * ZXV_E_ERASURE: fewer than k distinct rows (nothing written to buf).
 * ZXV_E_VERIFY: the rebuilt payload fails the header's SHA-256 (a corrupted
 * row or a header from another freight; buf contents are then unspecified).
 * ZXV_E_ARG: packets_len not a multiple of 21, or NULL packets with
 * packets_len > 0. ZXV_E_FORMAT: bad header. ZXV_E_NOMEM. */
ZXV_API int32_t zxv_freight_decode(const uint8_t header[64], const uint8_t *packets,
                                   size_t packets_len, uint8_t *buf, size_t cap,
                                   size_t *out_len);

/* Freights needed to carry `len` bytes at k (0 bytes still needs one).
 * ZXV_E_ARG: k out of range or NULL out. ZXV_E_TOO_LONG: more than
 * ZXV_FREIGHT_MAX_SET freights would be needed. */
ZXV_API int32_t zxv_freight_set_count(uint64_t len, uint8_t k, uint32_t *out_count);
/* Build member `seq` of the set carrying data[0..len): its header and its
 * 168 packets. ZXV_E_ARG: NULL pointer, bad k / flags / expand_limit.
 * ZXV_E_TOO_LONG: the set would exceed ZXV_FREIGHT_MAX_SET freights.
 * ZXV_E_RANGE: seq is not below the set count. */
ZXV_API int32_t zxv_freight_split(uint64_t freight_id, uint8_t k, uint8_t flags,
                                  uint64_t expand_limit, const uint8_t *data, size_t len,
                                  uint32_t seq, uint8_t out_header[64],
                                  uint8_t out_packets[3528]);

/* A JOIN reassembles one set, member by member, in any order, into a buffer
 * of `capacity` bytes it allocates (0..ZXV_FREIGHT_SET_MAX_BYTES). The first
 * member fixes the set (freight_id, k, flags, count, expand_limit). Create:
 * ZXV_E_RANGE for a larger capacity, ZXV_E_ARG, ZXV_E_NOMEM. Not
 * thread-safe: use a join from one thread at a time. */
typedef struct zxv_freight_join zxv_freight_join;
ZXV_API int32_t zxv_freight_join_create(uint64_t capacity, zxv_freight_join **out_join);
ZXV_API void zxv_freight_join_destroy(zxv_freight_join *join);
/* Decode one member (packets as for zxv_freight_decode) into its place. A
 * member already received returns ZXV_OK and changes nothing.
 * ZXV_E_STATE: the member belongs to another set (freight_id, k, flags,
 * count or expand_limit differ) or a non-last member is not full.
 * ZXV_E_CAPACITY: the whole set cannot fit the join's capacity (checked on
 * the first member, before anything is accepted). ZXV_E_ERASURE,
 * ZXV_E_VERIFY, ZXV_E_FORMAT, ZXV_E_ARG as for zxv_freight_decode. A failed
 * member is not recorded; add it again when more rows arrive. */
ZXV_API int32_t zxv_freight_join_add(zxv_freight_join *join, const uint8_t header[64],
                                     const uint8_t *packets, size_t packets_len);
/* Progress. Any out pointer may be NULL. *out_count is 0 until the first
 * member is accepted; *out_total is the joined length once complete, else 0. */
ZXV_API int32_t zxv_freight_join_status(const zxv_freight_join *join, uint32_t *out_received,
                                        uint32_t *out_count, int32_t *out_complete,
                                        uint64_t *out_total);
/* The joined bytes (R4 for bytes). ZXV_E_STATE until every member arrived. */
ZXV_API int32_t zxv_freight_join_result(const zxv_freight_join *join, uint8_t *buf, size_t cap,
                                        size_t *out_len);

/* ----- op streams -----
 * Format: "ZXOP" version(1) flags total(u64 LE) [SHA-256 of the output when
 * flags bit 0], then ops until the end. Integers are minimal LEB128 varints;
 * every op produces at least one byte:
 *   0x01 LITERAL  n, n bytes
 *   0x02 COPY     distance (1..bytes produced so far), length
 *   0x03 CIDREF   cid_len (1..128), cid bytes, length
 *   0x04 SEED     generator (u8), param_len (u8, <= 32), params, length */
#define ZXV_FREIGHT_CID_MAX       128u
#define ZXV_FREIGHT_GEN_PARAM_MAX 32u
/* SEED generators and their params: */
#define ZXV_FREIGHT_GEN_XORSHIFT   1 /* 8 bytes: u64 LE seed, nonzero; xorshift64* bytes LE */
#define ZXV_FREIGHT_GEN_REPEAT     2 /* 1..32 bytes: the pattern, repeated                  */
#define ZXV_FREIGHT_GEN_FIBWORD    3 /* 2 distinct bytes a, b: Fibonacci word abaababaab... */
#define ZXV_FREIGHT_GEN_SIERPINSKI 4 /* 3 bytes: log2 width w (1..15), on, off: byte i is  */
                                     /* `on` iff (x & y) == 0, x = i mod 2^w,              */
                                     /* y = (i >> w) mod 2^w                                */
/* Largest op stream or expansion a writer/encoder/expander handles here. */
#define ZXV_FREIGHT_OPS_MAX UINT64_C(67108864) /* 64 MiB */

/* Read a stream's declared total and whether it carries an output SHA-256,
 * without expanding. Either out may be NULL. ZXV_E_FORMAT: bad header. */
ZXV_API int32_t zxv_freight_ops_peek(const uint8_t *ops, size_t ops_len, uint64_t *out_total,
                                     int32_t *out_has_sha);

/* CIDREF resolver: write exactly `len` bytes for content id cid[0..cid_len)
 * to `out` and return ZXV_OK, or return any nonzero value if it cannot.
 * It must not call back into this expansion. */
typedef int32_t (*zxv_freight_resolve_fn)(void *user, const uint8_t *cid, size_t cid_len,
                                          uint8_t *out, uint64_t len);

/* Expand an op stream. `limit` is the most bytes the caller accepts
 * (<= ZXV_FREIGHT_OPS_MAX, else ZXV_E_RANGE): a declared total above it is
 * ZXV_E_TOO_LONG, before anything is written. Then R4 for bytes: *out_len receives the
 * declared total; buf NULL or cap below it is ZXV_E_BUFFER. resolve may be
 * NULL (a CIDREF then fails). ZXV_E_FORMAT: malformed stream (bad varint,
 * distance, generator or params, an op past the total, trailing bytes, or
 * fewer bytes than declared). ZXV_E_NOT_FOUND: a CIDREF was not resolved.
 * ZXV_E_VERIFY: the output fails the stream's SHA-256. On any error buf
 * may hold partial output; do not use it. */
ZXV_API int32_t zxv_freight_ops_expand(const uint8_t *ops, size_t ops_len, uint64_t limit,
                                       zxv_freight_resolve_fn resolve, void *user, uint8_t *buf,
                                       size_t cap, size_t *out_len);

/* Run one SEED generator directly: out[0..len). ZXV_E_ARG: NULL pointer with
 * a nonzero length, params over 32 bytes, or an unknown generator / params
 * the generator refuses (see the list above). */
ZXV_API int32_t zxv_freight_gen_run(uint8_t gen, const uint8_t *params, size_t params_len,
                                    uint8_t *out, size_t len);

/* Greedy LZ77 encoder (hash chains over 4-byte prefixes, window 65536,
 * deterministic): in -> a LITERAL/COPY op stream, with the output SHA-256
 * when with_sha != 0. R4 for bytes: *out_len receives the exact stream
 * length (computed by encoding). ZXV_E_TOO_LONG: n over ZXV_FREIGHT_OPS_MAX.
 * ZXV_E_NOMEM. The result may be LARGER than the input; see HONEST LIMITS. */
ZXV_API int32_t zxv_freight_ops_encode(const uint8_t *in, size_t n, int32_t with_sha,
                                       uint8_t *buf, size_t cap, size_t *out_len);
/* Worst-case encoded size of n input bytes (all literals). */
ZXV_API int32_t zxv_freight_ops_bound(uint64_t n, int32_t with_sha, uint64_t *out_bound);

/* A WRITER builds a stream op by op into a buffer of `max_bytes` it
 * allocates (14..ZXV_FREIGHT_OPS_MAX). declared_total is what the ops must
 * produce in all; sha256 (may be NULL) is the SHA-256 of that output, which
 * expansion will verify. Each put returns its own status and the first
 * error is sticky: every later put and finish return it again.
 * Not thread-safe: use a writer from one thread at a time. */
typedef struct zxv_freight_ops_writer zxv_freight_ops_writer;
/* ZXV_E_ARG: NULL out, or max_bytes too small for the stream header (14,
 * or 46 with sha256). ZXV_E_RANGE: max_bytes over ZXV_FREIGHT_OPS_MAX.
 * ZXV_E_NOMEM. */
ZXV_API int32_t zxv_freight_ops_writer_create(uint64_t declared_total, const uint8_t *sha256,
                                              uint64_t max_bytes,
                                              zxv_freight_ops_writer **out_writer);
ZXV_API void zxv_freight_ops_writer_destroy(zxv_freight_ops_writer *w);
/* Each put: ZXV_E_ARG for a length of 0 or more than is left of the
 * declared total, a NULL pointer, a COPY distance of 0 or beyond what was
 * produced, a cid of 0 or more than 128 bytes, or SEED params the generator
 * refuses. ZXV_E_CAPACITY: the writer's buffer is full. */
ZXV_API int32_t zxv_freight_ops_put_literal(zxv_freight_ops_writer *w, const uint8_t *bytes,
                                            size_t n);
ZXV_API int32_t zxv_freight_ops_put_copy(zxv_freight_ops_writer *w, uint64_t distance,
                                         uint64_t len);
ZXV_API int32_t zxv_freight_ops_put_cidref(zxv_freight_ops_writer *w, const uint8_t *cid,
                                           size_t cid_len, uint64_t len);
ZXV_API int32_t zxv_freight_ops_put_seed(zxv_freight_ops_writer *w, uint8_t gen,
                                         const uint8_t *params, size_t params_len, uint64_t len);
/* The finished stream (R4 for bytes). ZXV_E_STATE: the ops produce fewer
 * bytes than declared; or the sticky error of an earlier put. The writer
 * stays usable (finish again after more puts). */
ZXV_API int32_t zxv_freight_ops_writer_finish(const zxv_freight_ops_writer *w, uint8_t *buf,
                                              size_t cap, size_t *out_len);
/* ===== end freight ===== */

/* ===== web4 (ABI 1.5): Web 4 agent layer: signed tool manifests, agent directory, consent (kernel/src/web4) ===== */

/* The Web 4 agent layer (kernel/src/web4/web4_agent): agents with
 * ML-DSA-65 keys (FIPS 204), their signed tool manifests, a directory of
 * verified agent cards, human consent tokens, and signed envelopes. The
 * kernel's rules, which this ABI keeps:
 *   IDENTITY  agent_id = SHA3-256(agent public key). An agent is bound to
 *             the node (peer) hosting it by a binding record signed by the
 *             node's key, peer_id = SHA3-256(node public key); a self-bound
 *             agent signs its own binding and has peer_id == agent_id.
 *   CONTEXTS  every signature carries a FIPS 204 context string naming its
 *             purpose ("web4/v1/manifest", "web4/v1/consent", ...), so a
 *             signature for one purpose cannot be replayed as another.
 *   CONSENT   an agent never moves money or shares personal data without a
 *             token signed by the HUMAN's key over {human_id, agent_id,
 *             action digest, scope, max amount, validity, nonce}; the action
 *             digest binds the exact tool, parties, request id and payload.
 *             Tokens are single-use (a bounded log; when it is full, new
 *             consent is refused until expired entries are pruned).
 *
 * SECRETS. A private key never crosses this ABI: it is generated inside a
 * zxv_w4_agent handle (also used for a human's key: human_id is the handle's
 * agent_id), used there for signing, and wiped by zxv_w4_agent_destroy.
 * There is no call that returns it, in bytes or in text.
 *
 * CAPABILITIES. Creating a key handle and anything it signs (bindings,
 * manifests, envelopes) needs ZXV_CAP_CRYPTO_SIGN; issuing a consent token
 * needs ZXV_CAP_ATTEST. The cap must belong to the context of the key
 * handle it signs with (ZXV_E_CAP otherwise), and every call on a key handle
 * fails with ZXV_E_POISONED once that context halted. Parsing, verifying,
 * digests, manifests, the directory and the consent log are pure or hold
 * only caller state: no capability.
 *
 * TIME. There is no clock in here: every call that needs the time takes
 * now_ms (Unix milliseconds) from the caller.
 *
 * ENCODINGS. Manifests and envelopes use the kernel's wire formats; a
 * consent token is the kernel's in-envelope encoding ("W4CS", little
 * endian, ZXV_W4_CONSENT_BYTES). An agent CARD (what a peer needs to verify
 * an agent) has no kernel wire format; libzxv defines one:
 *   "W4CD" | 1 | flags (bit 0: self-bound) | name_len | name | bound_ms
 *   (u64 LE) | public key | node public key (only when not self-bound)
 *   | binding signature
 * The ids are not carried: a reader derives them from the keys.
 * Byte results use "R4 for bytes" as described in the freight section.
 *
 * HONEST LIMITS. Envelopes are signed, not encrypted. A consent token proves
 * that a key holder signed; that the holder is the right human and
 * understood what they signed depends on the device and UI that showed the
 * action, which this library cannot see. The receiving runtime (replay
 * window, rate limits), carriage over freight, and the Web 2 (OIDC, OAuth,
 * HTTP) and Web 3 (EVM, secp256k1, payments, identity links) bridges are
 * NOT in this ABI. Interoperability is between ZXV instances only; no other
 * agent protocol is implemented or claimed. Built without PQC
 * (ZXV_NO_PQC=1), every call returns ZXV_E_NOTIMPL. */
#define ZXV_W4_PK_BYTES          1952u /* ML-DSA-65 public key            */
#define ZXV_W4_SIG_BYTES         3309u /* ML-DSA-65 signature             */
#define ZXV_W4_ID_BYTES          32u   /* agent_id, peer_id, human_id     */
#define ZXV_W4_NAME_MAX          31u   /* agent and tool names: 1..31 of  */
                                       /* [a-z0-9._-]                     */
#define ZXV_W4_MAX_TOOLS         16u   /* tools in one manifest (hard)   */
#define ZXV_W4_MSG_MAX_PAYLOAD   2048u
#define ZXV_W4_CONSENT_BYTES     3453u /* an encoded consent token        */
#define ZXV_W4_CARD_MAX          7259u /* a node-bound card; self-bound is 1952 shorter */
#define ZXV_W4_MANIFEST_MAX      4349u
#define ZXV_W4_ENV_MAX           8970u
#define ZXV_W4_DIR_MAX           4096u  /* directory entries              */
#define ZXV_W4_CONSENT_LOG_MAX   65536u /* consent log entries            */

#define ZXV_W4_TOOL_MONEY  0x01u /* the tool moves money                  */
#define ZXV_W4_TOOL_PII    0x02u /* the tool reveals personal data        */
#define ZXV_W4_TOOL_STREAM 0x04u /* answers with STREAM chunks            */
#define ZXV_W4_CONSENT_MONEY 0x01u
#define ZXV_W4_CONSENT_PII   0x02u

#define ZXV_W4_MSG_REQUEST  1
#define ZXV_W4_MSG_RESPONSE 2
#define ZXV_W4_MSG_STREAM   3
#define ZXV_W4_MSG_CANCEL   4
#define ZXV_W4_MSG_ERROR    5
#define ZXV_W4_MF_FINAL     0x01u /* last STREAM chunk / final response   */
#define ZXV_W4_MF_CONSENT   0x02u /* a consent token is attached (set by  */
                                  /* zxv_w4_env_seal, never by the caller) */

/* ----- key handles (agents and humans) -----
 * A zxv_w4_agent holds an ML-DSA-65 key pair and the agent's signed card.
 * It belongs to the context of the cap that created it and must be
 * destroyed before that context. A hosted agent keeps only its node's
 * PUBLIC key, so the node handle may be destroyed first. Not thread-safe:
 * use a handle from one thread at a time (sealing advances its envelope
 * sequence). */
typedef struct zxv_w4_agent zxv_w4_agent;

/* Create a key handle. seed: 32 bytes for a reproducible key, or NULL for a
 * fresh one from zxv_random. name: 1..31 characters of [a-z0-9._-]. node:
 * the handle of the node hosting this agent (it signs the binding), or NULL
 * for a self-bound agent; it must belong to the same context. now_ms is the
 * binding time. Needs CRYPTO_SIGN. ZXV_E_ARG: NULL name or out, bad name
 * characters. ZXV_E_TOO_LONG: name over 31. ZXV_E_UTF8. ZXV_E_NOMEM.
 * ZXV_E_IO: no OS randomness. */
ZXV_API int32_t zxv_w4_agent_create(const zxv_cap *cap, const uint8_t *seed, const char *name,
                                    const zxv_w4_agent *node, uint64_t now_ms,
                                    zxv_w4_agent **out_agent);
/* Wipes the private key, then frees. NULL is a no-op. */
ZXV_API void zxv_w4_agent_destroy(zxv_w4_agent *agent);
/* The handle's public facts. Any out pointer may be NULL. */
ZXV_API int32_t zxv_w4_agent_info(const zxv_w4_agent *agent, uint8_t out_agent_id[32],
                                  uint8_t out_peer_id[32], uint8_t out_public_key[1952]);
/* The agent's card (R4 for bytes; at most ZXV_W4_CARD_MAX). */
ZXV_API int32_t zxv_w4_agent_card(const zxv_w4_agent *agent, uint8_t *buf, size_t cap,
                                  size_t *out_len);

/* Verify a card: well formed and its binding signature valid under the
 * node key (or the agent's own key when self-bound). Pure. Any out pointer
 * may be NULL; out_name receives the NUL-terminated name.
 * ZXV_E_FORMAT: malformed card. ZXV_E_VERIFY: the signature fails. */
ZXV_API int32_t zxv_w4_card_verify(const uint8_t *card, size_t len, uint8_t out_agent_id[32],
                                   uint8_t out_peer_id[32], char out_name[32],
                                   uint64_t *out_bound_ms);

/* ----- directory of verified peers -----
 * Holds up to `capacity` cards (1..ZXV_W4_DIR_MAX, else ZXV_E_RANGE),
 * keyed by agent_id. Not thread-safe: use it from one thread at a time. */
typedef struct zxv_w4_dir zxv_w4_dir;
ZXV_API int32_t zxv_w4_dir_create(uint32_t capacity, zxv_w4_dir **out_dir);
ZXV_API void zxv_w4_dir_destroy(zxv_w4_dir *dir);
/* Verify a card, then add it or replace the entry for the same agent.
 * ZXV_E_FORMAT, ZXV_E_VERIFY as zxv_w4_card_verify. ZXV_E_STATE: the card's
 * binding is older than the one held (no rollback). ZXV_E_CAPACITY: full. */
ZXV_API int32_t zxv_w4_dir_add(zxv_w4_dir *dir, const uint8_t *card, size_t len);
/* The card held for agent_id (R4 for bytes). ZXV_E_NOT_FOUND if none. */
ZXV_API int32_t zxv_w4_dir_find(const zxv_w4_dir *dir, const uint8_t agent_id[32], uint8_t *buf,
                                size_t cap, size_t *out_len);
ZXV_API int32_t zxv_w4_dir_count(const zxv_w4_dir *dir, uint32_t *out_count);

/* ----- tool manifests -----
 * A manifest lists up to 16 tools: name, price in VFV minor units (2
 * digits), a rate limit (calls per minute, burst) and flags. Signing fixes
 * the agent and peer ids; adding a tool afterwards drops the signature.
 * Not thread-safe: use a manifest from one thread at a time. */
typedef struct zxv_w4_manifest zxv_w4_manifest;
/* An empty, unsigned manifest valid in [issued_ms, expires_ms).
 * ZXV_E_ARG: NULL out, or expires_ms <= issued_ms. */
ZXV_API int32_t zxv_w4_manifest_create(uint32_t version, uint64_t issued_ms, uint64_t expires_ms,
                                       zxv_w4_manifest **out_manifest);
ZXV_API void zxv_w4_manifest_destroy(zxv_w4_manifest *m);
/* Add a tool. name: 1..31 of [a-z0-9._-]; rate_per_min and burst >= 1;
 * flags: ZXV_W4_TOOL_*; max_input <= ZXV_W4_MSG_MAX_PAYLOAD. ZXV_E_ARG,
 * ZXV_E_TOO_LONG (name), ZXV_E_DUPLICATE (name already listed),
 * ZXV_E_CAPACITY: the manifest already lists ZXV_W4_MAX_TOOLS (16) tools;
 * a manifest never holds more, and nothing is changed. */
ZXV_API int32_t zxv_w4_manifest_add_tool(zxv_w4_manifest *m, const char *name, uint64_t price_vfv,
                                         uint32_t rate_per_min, uint32_t burst, uint32_t flags,
                                         uint32_t max_input);
/* Sign with the agent's key (CRYPTO_SIGN; cap in the agent's context). */
ZXV_API int32_t zxv_w4_manifest_sign(const zxv_cap *cap, const zxv_w4_agent *agent,
                                     zxv_w4_manifest *m);
/* ZXV_OK if signed by `public_key`, that key is the manifest's agent, and
 * issued_ms <= now_ms < expires_ms. ZXV_E_VERIFY: wrong key or bad (or no)
 * signature. ZXV_E_STATE: valid signature but not valid at now_ms. */
ZXV_API int32_t zxv_w4_manifest_verify(const zxv_w4_manifest *m, const uint8_t public_key[1952],
                                       uint64_t now_ms);
/* The signed wire form (R4 for bytes, <= ZXV_W4_MANIFEST_MAX). ZXV_E_STATE
 * if not signed. */
ZXV_API int32_t zxv_w4_manifest_encode(const zxv_w4_manifest *m, uint8_t *buf, size_t cap,
                                       size_t *out_len);
/* Parse the wire form into a new handle. The signature is kept but NOT
 * checked: call zxv_w4_manifest_verify. ZXV_E_FORMAT: malformed (including
 * trailing bytes or an invalid tool). */
ZXV_API int32_t zxv_w4_manifest_decode(const uint8_t *bytes, size_t len,
                                       zxv_w4_manifest **out_manifest);
/* The Web 2 discovery face (e.g. /.well-known/web4-agent.json) as JSON
 * text (R4). Prices stay integers in minor units. */
ZXV_API int32_t zxv_w4_manifest_to_json(const zxv_w4_manifest *m, char *buf, size_t cap,
                                        size_t *out_len);
/* Header fields. Any out pointer may be NULL; ids are zero until signed. */
ZXV_API int32_t zxv_w4_manifest_info(const zxv_w4_manifest *m, uint8_t out_agent_id[32],
                                     uint8_t out_peer_id[32], uint32_t *out_version,
                                     uint64_t *out_issued_ms, uint64_t *out_expires_ms,
                                     uint32_t *out_tool_count, int32_t *out_signed);
/* Tool `index` (0-based). Any out pointer may be NULL. ZXV_E_NOT_FOUND
 * past the last tool. */
ZXV_API int32_t zxv_w4_manifest_tool(const zxv_w4_manifest *m, uint32_t index, char out_name[32],
                                     uint64_t *out_price_vfv, uint32_t *out_rate_per_min,
                                     uint32_t *out_burst, uint32_t *out_flags,
                                     uint32_t *out_max_input);
/* Index of a tool by name; ZXV_E_NOT_FOUND if not listed. */
ZXV_API int32_t zxv_w4_manifest_find(const zxv_w4_manifest *m, const char *name,
                                     uint32_t *out_index);

/* ----- consent ----- */

/* The digest a consent token for one request must carry:
 * SHA3-256 over "web4/v1/action", tool, from, to, req_id and the payload
 * (each length-prefixed, see web4_agent.c). Pure. tool: a valid tool name;
 * payload up to ZXV_W4_MSG_MAX_PAYLOAD (ZXV_E_TOO_LONG beyond). */
ZXV_API int32_t zxv_w4_action_digest(const char *tool, const uint8_t from[32],
                                     const uint8_t to[32], uint64_t req_id,
                                     const uint8_t *payload, size_t len, uint8_t out_digest[32]);

/* Issue a consent token on the HUMAN's side, after showing them the action:
 * signed by `human` (a key handle; human_id = its agent_id) for agent_id to
 * do `action` within `scope` (ZXV_W4_CONSENT_*, nonzero), money up to
 * max_vfv minor units, valid in [issued_ms, expires_ms). nonce: 16 bytes,
 * or NULL for a random one; it makes the token single-use. Needs ATTEST
 * (cap in the human handle's context). ZXV_E_ARG: NULL pointer, bad scope,
 * expires_ms <= issued_ms. */
ZXV_API int32_t zxv_w4_consent_issue(const zxv_cap *cap, const zxv_w4_agent *human,
                                     const uint8_t agent_id[32], const uint8_t action[32],
                                     uint32_t scope, uint64_t max_vfv, uint64_t issued_ms,
                                     uint64_t expires_ms, const uint8_t *nonce,
                                     uint8_t out_token[3453]);
/* Read a token's fields (no signature check). Any out may be NULL.
 * ZXV_E_FORMAT: malformed. */
ZXV_API int32_t zxv_w4_consent_info(const uint8_t token[3453], uint8_t out_human_id[32],
                                    uint8_t out_agent_id[32], uint8_t out_action[32],
                                    uint32_t *out_scope, uint64_t *out_max_vfv,
                                    uint64_t *out_issued_ms, uint64_t *out_expires_ms,
                                    uint8_t out_nonce[16]);

/* A single-use log of consumed and revoked tokens, `capacity` entries
 * (1..ZXV_W4_CONSENT_LOG_MAX, else ZXV_E_RANGE). Not thread-safe: one
 * thread at a time. */
typedef struct zxv_w4_consent_log zxv_w4_consent_log;
ZXV_API int32_t zxv_w4_consent_log_create(uint32_t capacity, zxv_w4_consent_log **out_log);
ZXV_API void zxv_w4_consent_log_destroy(zxv_w4_consent_log *log);
/* Check that `token` lets agent_id do `action` with scope `need` (nonzero
 * ZXV_W4_CONSENT_* bits) for `amount` VFV minor units (checked against
 * max_vfv when need has MONEY), signed by the human whose key is
 * human_public_key, valid at now_ms; then record it as used. ZXV_OK means
 * consent was given and is now spent. ZXV_E_CONSENT: no token (NULL), a
 * mismatch (human, agent, action, scope, amount), expired or not yet
 * valid, a signature that fails, or a token already used or revoked; the
 * reason is in zxv_last_error. ZXV_E_FORMAT: malformed token.
 * ZXV_E_CAPACITY: the log is full even after pruning expired entries
 * (fail closed: nothing is allowed). ZXV_E_ARG: other NULLs, bad need. */
ZXV_API int32_t zxv_w4_consent_check(zxv_w4_consent_log *log, const uint8_t *token,
                                     const uint8_t human_public_key[1952],
                                     const uint8_t agent_id[32], const uint8_t action[32],
                                     uint32_t need, uint64_t amount, uint64_t now_ms);
/* Revoke a token before use: later checks of it fail with ZXV_E_CONSENT.
 * Needs no capability (it only takes authority away) and no signature check.
 * Revoking twice is ZXV_OK. ZXV_E_FORMAT, ZXV_E_CAPACITY as above. */
ZXV_API int32_t zxv_w4_consent_revoke(zxv_w4_consent_log *log, const uint8_t token[3453],
                                      uint64_t now_ms);
/* Drop entries whose token expired at or before now_ms (they can no longer
 * be presented). *out_remaining (may be NULL) receives the number of
 * entries STILL HELD after the prune, not the number dropped (callers that
 * want the dropped count compare with a prune before). ZXV_E_ARG (log NULL
 * or not a consent log). */
ZXV_API int32_t zxv_w4_consent_log_prune(zxv_w4_consent_log *log, uint64_t now_ms,
                                         uint32_t *out_remaining);

/* ----- envelopes -----
 * One signed shape for REQUEST, RESPONSE, STREAM, CANCEL and ERROR: kind,
 * flags, from, to, a strictly increasing per-sender sequence number, a
 * timestamp, request id, chunk index, tool, payload (<= 2048 bytes), an
 * optional consent token, and the sender's ML-DSA-65 signature over it all.
 * Sealing assigns the next sequence number of the agent handle. */

/* Build, sign and encode an envelope from `agent` to `to` (R4 for bytes,
 * <= ZXV_W4_ENV_MAX; on ZXV_E_BUFFER no sequence number is used). kind:
 * ZXV_W4_MSG_*; flags: 0 or ZXV_W4_MF_FINAL; tool: a valid tool name;
 * consent_token: NULL or a token to attach. Needs CRYPTO_SIGN (cap in the
 * agent's context). ZXV_E_ARG: bad kind, flags, tool, NULL pointer.
 * ZXV_E_TOO_LONG: payload over 2048. ZXV_E_FORMAT: malformed token. */
ZXV_API int32_t zxv_w4_env_seal(const zxv_cap *cap, zxv_w4_agent *agent, int32_t kind,
                                const uint8_t to[32], uint64_t req_id, uint32_t chunk,
                                const char *tool, uint32_t flags, const uint8_t *payload,
                                size_t len, const uint8_t *consent_token, uint64_t now_ms,
                                uint8_t *buf, size_t buf_cap, size_t *out_len);
/* Seal a reply to a REQUEST envelope addressed to `agent`: to/from swapped,
 * same request id and tool. kind: RESPONSE, STREAM or ERROR. The request's
 * signature is not checked here (zxv_w4_env_verify). ZXV_E_FORMAT:
 * malformed request. ZXV_E_ARG: not a REQUEST, not addressed to this agent,
 * or a bad kind / flags. Otherwise as zxv_w4_env_seal. */
ZXV_API int32_t zxv_w4_env_reply(const zxv_cap *cap, zxv_w4_agent *agent, const uint8_t *request,
                                 size_t request_len, int32_t kind, uint32_t chunk, uint32_t flags,
                                 const uint8_t *payload, size_t len, uint64_t now_ms,
                                 uint8_t *buf, size_t buf_cap, size_t *out_len);
/* Parse an envelope (no signature check). Any out may be NULL; out_tool
 * receives the NUL-terminated tool name. ZXV_E_FORMAT: malformed. */
ZXV_API int32_t zxv_w4_env_decode(const uint8_t *env, size_t len, int32_t *out_kind,
                                  uint32_t *out_flags, uint8_t out_from[32], uint8_t out_to[32],
                                  uint64_t *out_seq, uint64_t *out_ts_ms, uint64_t *out_req_id,
                                  uint32_t *out_chunk, char out_tool[32],
                                  uint32_t *out_payload_len);
/* The payload (R4 for bytes). ZXV_E_FORMAT: malformed envelope. */
ZXV_API int32_t zxv_w4_env_payload(const uint8_t *env, size_t len, uint8_t *buf, size_t cap,
                                   size_t *out_len);
/* The attached consent token. ZXV_E_NOT_FOUND: none attached. */
ZXV_API int32_t zxv_w4_env_consent(const uint8_t *env, size_t len, uint8_t out_token[3453]);
/* ZXV_OK if `sender_public_key` is the envelope's sender (from ==
 * SHA3-256 of it) and its signature verifies. ZXV_E_VERIFY otherwise.
 * Replay, timestamp and rate checks are the receiver's job. */
ZXV_API int32_t zxv_w4_env_verify(const uint8_t *env, size_t len,
                                  const uint8_t sender_public_key[1952]);
/* ===== end web4 ===== */

/* ===== paradox (ABI 1.5): Paradox engine: four kinds, operators, complex-paradox graphs (kernel/src/paradox; reserved until it reaches main) ===== */

/* A PARADOX here is work whose four-valued state is Both (the evidence
 * points both ways) or Neither (no evidence). This group does not decide
 * which side is "really" true. It reads the work's 64-state hexagram tag
 * (the zxv_hex_* tags of ABI 1.3; there is no second tag type), gives the
 * paradox one of four KINDS, and runs a fixed, logged operator for that
 * kind. docs/PARADOX_ENGINE.md explains the design.
 *
 *   KIND          WHEN                                 WHAT THE OPERATOR DOES
 *   CONSTRUCTIVE  Both, evidence margin growing        a bounded parameter
 *     (catalyst)  (polarity NEGENTROPIC)               update (shares, weights,
 *                                                      timings), applied only
 *                                                      inside published bounds
 *   DESTRUCTIVE   Both, margin shrinking or stale      contain: quarantine,
 *     (immune)    (polarity ENTROPIC)                  roll back from a
 *                                                      checkpoint, RepairProof
 *   NEUTRAL       Both and steady past the potential   hold: keep every
 *                 phase, or Neither                    candidate branch
 *   SPECIAL       Both, steady, potential phase, and   frontier: a permanent
 *     (frontier)  no registry signature                append-only record and a
 *                                                      draft classification
 *                                                      for review
 *
 * A signature found in the registry (zxv_paradox_signature of a canonical
 * description of the anomaly) overrides the trend. True and False are not
 * paradoxes (ZXV_PDX_KIND_NONE).
 *
 * THE CLASS PAIR. Every operator takes the pair (kind, tag) that
 * zxv_paradox_classify returned: the tag carries the kind in its polarity
 * bits (constructive NEGENTROPIC, destructive ENTROPIC, neutral and special
 * NEUTRAL; base Both or Neither; special only at the potential phase; kind
 * NONE only with base True or False). A pair that breaks these rules is
 * ZXV_E_ARG. FORBIDDEN STAYS SEPARATE: a tag with ZXV_HEX_POL_FORBIDDEN
 * means a real check found a broken invariant. It is accepted with any
 * kind, and every operator refuses it and rolls back (ROLLED_BACK), whatever
 * the kind; zxv_paradox_destructive quarantines it.
 *
 * THE BOOK (zxv_paradox). One handle holds what the operators write: the
 * operator log, the frontier log (special records, never edited), the
 * quarantine log (contained work, the lpres_hex X6 log) and the signature
 * registry. All three logs are append-only and SHA-256 hash-chained; their
 * records can be read out as bytes and their chains recomputed by a host.
 *
 * OUTCOMES. An operator that ran returns ZXV_OK, has appended one record
 * to the operator log, and reports its decision in *out_outcome (a
 * ZXV_PDX_OUT_* value; REFUSED and PROPOSAL_FOR_OWNER are decisions, not
 * errors). A negative status means it did not run: nothing was logged and
 * nothing changed. Two exceptions: zxv_paradox_destructive restores the
 * state even when the logs are full (rollback is always safe), and
 * zxv_paradox_graph_pass returns ZXV_E_PARADOX_STEP after logging the pass
 * that reached the step limit (the graph goes to the owner).
 * A full log is ZXV_E_CAPACITY: halt (zxv_ctx_halt). Work that cannot be
 * recorded must not continue.
 *
 * CAPABILITIES (R10). Everything that writes a log or the registry, applies
 * a policy update, or creates a handle needs ZXV_CAP_PARADOX, from a cap of
 * the book's context (else ZXV_E_CAP). Reading, classifying, adding
 * branches, nodes and updates to a handle you hold, and the pure functions
 * need none. Nothing here signs: the engine produces bytes and hashes, and
 * the caller signs them with zxv_ed25519_sign or zxv_mldsa87_sign (which
 * need ZXV_CAP_CRYPTO_SIGN). Every call on a handle fails closed
 * (ZXV_E_POISONED) once its context halted.
 *
 * THREADING (R6). A zxv_paradox book is internally serialized. A policy,
 * hold or graph handle is NOT thread-safe: use it from one thread at a
 * time. Destroy policy, hold and graph handles and the book before the
 * zxv_ctx they belong to; a handle and a book used together must belong to
 * the same context (else ZXV_E_ARG).
 *
 * HONEST LIMITS, in plain words.
 *   - Automatic changes happen only inside published bounds. A constructive
 *     update with any value outside its [lo, hi] changes nothing and becomes
 *     a proposal for the owner. The operator has no balance input: it
 *     cannot move money or change a balance or a ledger invariant.
 *   - Policies that touch money or devices need the owner. A policy created
 *     with ZXV_PDX_FLAG_MONEY or ZXV_PDX_FLAG_DEVICE is never changed
 *     automatically; registry entries and drafts with those flags need
 *     owner approval. "owner_ok" is the caller's assertion: inside one
 *     process this library cannot check who the owner is.
 *   - Detection is by these checks and not instant: the step limit, the
 *     budget, a repeated state fingerprint, the caller's invariant result
 *     and the registry lookup, each run when the caller calls. What those
 *     checks do not look at goes undetected.
 *   - Destructive work is contained, never answered: there is no way to
 *     reply to, or act against, a sender.
 *   - This is contradiction-tolerant bookkeeping. It contains, holds and
 *     routes contradictions; it never makes both sides true.
 *   - Peers agree because they apply the same fixed order to the same
 *     signed inputs, not through any shared clock or instant network-wide
 *     change. "Subquanta" and similar words in the design notes are a
 *     metaphor, not physics.
 *   - Integers only; no floating point anywhere. */

#define ZXV_E_PARADOX_STEP  (-39) /* a paradox graph reached its step limit:  */
                                  /* escalated to the owner (class REFUSED)    */
#define ZXV_E_PARADOX_OWNER (-40) /* needs the owner's approval (money or      */
                                  /* device policy); class REFUSED             */

#define ZXV_PDX_KIND_CONSTRUCTIVE 0
#define ZXV_PDX_KIND_DESTRUCTIVE  1
#define ZXV_PDX_KIND_NEUTRAL      2
#define ZXV_PDX_KIND_SPECIAL      3 /* unclassified: the frontier */
#define ZXV_PDX_KIND_NONE         4 /* True or False: not a paradox */

/* What to do with a classified paradox (zxv_paradox_action). */
#define ZXV_PDX_DO_NONE       0 /* not a paradox: use zxv_hex_action        */
#define ZXV_PDX_DO_SYNTHESIZE 1 /* constructive: zxv_paradox_constructive   */
#define ZXV_PDX_DO_CONTAIN    2 /* destructive: zxv_paradox_destructive     */
#define ZXV_PDX_DO_HOLD       3 /* neutral: zxv_paradox_neutral             */
#define ZXV_PDX_DO_RECORD     4 /* special: zxv_paradox_special (sandbox)   */
#define ZXV_PDX_DO_ROLLBACK   5 /* forbidden: refuse, roll back, quarantine */

/* Operator outcomes (the kernel's pdx_out_t). */
#define ZXV_PDX_OUT_APPLIED            0  /* constructive update applied            */
#define ZXV_PDX_OUT_KEPT               1  /* an applied update survived evaluation  */
#define ZXV_PDX_OUT_PROPOSAL_FOR_OWNER 2  /* out of bounds or unapproved: no change */
#define ZXV_PDX_OUT_ROLLED_BACK        3  /* tension grew, or forbidden: restored   */
#define ZXV_PDX_OUT_CONTAINED          4  /* destructive: quarantined, rolled back  */
#define ZXV_PDX_OUT_DEFERRED           5  /* neutral: still holding every branch    */
#define ZXV_PDX_OUT_SETTLED            6  /* neutral: one branch chosen             */
#define ZXV_PDX_OUT_RECORDED           7  /* special: frontier record written       */
#define ZXV_PDX_OUT_ACCEPTED           8  /* reviewed draft joined the registry     */
#define ZXV_PDX_OUT_PENDING            9  /* graph: more passes needed              */
#define ZXV_PDX_OUT_RESOLVED           10 /* graph: every node is final             */
#define ZXV_PDX_OUT_ESCALATE_TO_OWNER  11 /* graph: step limit (ZXV_E_PARADOX_STEP) */
#define ZXV_PDX_OUT_REFUSED            12 /* wrong kind: nothing changed            */
#define ZXV_PDX_OUT_LOG_FULL           13 /* never reported: ZXV_E_CAPACITY instead */

/* Graph node states. Final: CLEAR, CONTAINED, ROLLED_BACK, TAINTED, APPLIED. */
#define ZXV_PDX_NS_OPEN        0 /* not resolved yet                          */
#define ZXV_PDX_NS_CLEAR       1 /* kind NONE: not a paradox                  */
#define ZXV_PDX_NS_CONTAINED   2 /* destructive: contained (sticky)           */
#define ZXV_PDX_NS_ROLLED_BACK 3 /* forbidden: refused and rolled back (sticky)*/
#define ZXV_PDX_NS_TAINTED     4 /* depends on contained work (sticky)        */
#define ZXV_PDX_NS_SANDBOXED   5 /* special: recorded, awaiting review        */
#define ZXV_PDX_NS_WAITING     6 /* depends on a sandboxed node               */
#define ZXV_PDX_NS_HELD        7 /* neutral: branches kept                    */
#define ZXV_PDX_NS_BLOCKED     8 /* constructive reached by held/waiting work */
#define ZXV_PDX_NS_APPLIED     9 /* constructive: applied                     */

/* Flags of a policy, a registry entry or a draft. */
#define ZXV_PDX_FLAG_MONEY  1u /* touches money: the owner decides   */
#define ZXV_PDX_FLAG_DEVICE 2u /* touches devices: the owner decides */

/* Which checks fired (a RepairProof's trips; the quarantine reason). */
#define ZXV_PDX_TRIP_STEPS     1u  /* steps >= step_limit (limit 0: none) */
#define ZXV_PDX_TRIP_BUDGET    2u  /* spent >= budget (budget 0: none)    */
#define ZXV_PDX_TRIP_CYCLE     4u  /* a state fingerprint repeated        */
#define ZXV_PDX_TRIP_INVARIANT 8u  /* the caller's invariant result was 0 */
#define ZXV_PDX_TRIP_FORBIDDEN 16u /* the tag was already forbidden       */

/* The book's three logs. */
#define ZXV_PDX_LOG_OPS        0 /* operator log                     */
#define ZXV_PDX_LOG_FRONTIER   1 /* special records, never edited    */
#define ZXV_PDX_LOG_QUARANTINE 2 /* contained work (lpres_hex X6)    */

#define ZXV_PDX_HASH_BYTES     32u
#define ZXV_PDX_REC_BYTES      144u /* an operator or frontier log record */
#define ZXV_PDX_QREC_BYTES     110u /* a quarantine log record            */
#define ZXV_PDX_REPAIR_BYTES   160u /* a RepairProof                      */
#define ZXV_PDX_DRAFT_BYTES    71u  /* a draft classification             */
#define ZXV_PDX_PROPOSAL_BYTES(n) (12u + 32u * (uint32_t) (n)) /* a policy proposal */
#define ZXV_PDX_LOG_MAX        65536u /* records per log                   */
#define ZXV_PDX_REGISTRY_MAX   65536u /* registry entries                  */
#define ZXV_PDX_POLICY_MAX     64u    /* parameters per policy             */
#define ZXV_PDX_HOLD_MAX       64u    /* branches per hold                 */
#define ZXV_PDX_GRAPH_MAX      64u    /* nodes per graph                   */
#define ZXV_PDX_MAX_DEPS       4u     /* dependencies per node             */
#define ZXV_PDX_UPDATES_MAX    256u   /* queued node updates per pass      */
#define ZXV_PDX_TRACE_MAX      4096u  /* state fingerprints per detection  */
#define ZXV_PDX_SRC_SEED       UINT64_MAX /* registry entry not learned from the frontier */

typedef struct zxv_paradox zxv_paradox;               /* the book: logs + registry */
typedef struct zxv_paradox_policy zxv_paradox_policy; /* constructive parameters   */
typedef struct zxv_paradox_hold zxv_paradox_hold;     /* neutral branches          */
typedef struct zxv_paradox_graph zxv_paradox_graph;   /* a complex paradox         */

/* ---- pure: no capability, no context ---- */

/* The signature of an anomaly: SHA-256("zxv/pdx-sig/v1" || data), from a
 * canonical description the caller builds. data may be NULL when len is 0.
 * ZXV_E_ARG (out NULL, or data NULL with len > 0). */
ZXV_API int32_t zxv_paradox_signature(const uint8_t *data, size_t len, uint8_t out[32]);
/* The kind a tag reads as on its own: Both by polarity (NEGENTROPIC
 * constructive, ENTROPIC destructive, NEUTRAL neutral, or special at the
 * potential phase), Neither neutral, True/False NONE. A forbidden tag reads
 * as NONE: forbidden is not a kind. ZXV_E_ARG (tag > 63, out NULL). */
ZXV_API int32_t zxv_paradox_kind_of_tag(uint8_t tag, int32_t *out_kind);
/* What to do with a classified pair (kind, tag): a ZXV_PDX_DO_* value.
 * A forbidden tag gives ROLLBACK whatever the kind. ZXV_E_ARG (an invalid
 * pair, see THE CLASS PAIR; out NULL). */
ZXV_API int32_t zxv_paradox_action(int32_t kind, uint8_t tag, int32_t *out_do);
/* The ZXV_HEX_ACT_* an outcome amounts to (APPLIED -> PROCEED, DEFERRED ->
 * WAIT, PROPOSAL_FOR_OWNER -> ESCALATE, CONTAINED -> ROLLBACK, ...).
 * ZXV_E_ARG (outcome not 0..13, out NULL). */
ZXV_API int32_t zxv_paradox_outcome_action(int32_t outcome, int32_t *out_action);
/* Recompute a RepairProof's binding hash: bytes 120..151 must equal
 * SHA-256("zxv/pdx-repair/v1" || bytes 0..119). ZXV_OK or ZXV_E_VERIFY;
 * ZXV_E_ARG (NULL). Checks integrity only; a signature over the proof is
 * the caller's to verify. */
ZXV_API int32_t zxv_paradox_repair_verify(const uint8_t proof[160]);
/* Static, never-NULL names ("constructive", "proposal-for-owner",
 * "sandboxed"); "INVALID" for an unknown value. */
ZXV_API const char *zxv_paradox_kind_name(int32_t kind);
ZXV_API const char *zxv_paradox_outcome_name(int32_t outcome);
ZXV_API const char *zxv_paradox_node_state_name(int32_t state);

/* ---- the book ---- */

/* A new book with three empty logs of `log_capacity` records each
 * (1..ZXV_PDX_LOG_MAX) and an empty registry of `registry_capacity`
 * entries (1..ZXV_PDX_REGISTRY_MAX). Needs PARADOX; the book belongs to the
 * cap's context. ZXV_E_ARG (out NULL or a capacity out of range),
 * ZXV_E_NOMEM, ZXV_E_CAP, ZXV_E_POISONED. */
ZXV_API int32_t zxv_paradox_create(const zxv_cap *cap, uint32_t log_capacity,
                                   uint32_t registry_capacity, zxv_paradox **out);
/* Free it. NULL is ignored. */
ZXV_API void zxv_paradox_destroy(zxv_paradox *book);

/* Classify a tag. book may be NULL (no registry lookup); sig may be NULL
 * (no signature). evidence may be NULL (no trend: polarity NEUTRAL), else
 * it is {pos0, neg0, pos1, neg1}, the evidence counts before and after,
 * and `stale` (non-zero) says the newest evidence is past the caller's
 * freshness limit; stale is ignored without evidence. A known signature
 * overrides the trend for Both and Neither. Writes the kind, the tag with
 * the kind's polarity (base and phase kept; a forbidden tag stays
 * forbidden) and the registry index that decided it (-1 if none). Any out
 * pointer may be NULL. Reads only: no capability. ZXV_E_ARG (tag > 63, or
 * book not a book), ZXV_E_POISONED. */
ZXV_API int32_t zxv_paradox_classify(const zxv_paradox *book, uint8_t tag,
                                     const uint32_t evidence[4], int32_t stale,
                                     const uint8_t sig[32], int32_t *out_kind, uint8_t *out_tag,
                                     int32_t *out_reg_index);

/* ---- the signature registry (append-only: an entry is never edited) ---- */

/* Add a reviewed signature (a seed, or one a peer shared from its
 * RepairProof) as kind CONSTRUCTIVE, DESTRUCTIVE or NEUTRAL, with the
 * caller's reason code and ZXV_PDX_FLAG_* flags. An entry flagged MONEY or
 * DEVICE needs owner_ok (non-zero), else ZXV_E_PARADOX_OWNER. Logs one
 * operator record (op 6, outcome ACCEPTED, aux = the index, a = 32 zeros,
 * b = sig). Its src is ZXV_PDX_SRC_SEED. *out_index (may be NULL) is the
 * entry's index. Needs PARADOX. ZXV_E_ARG (NULL sig, kind SPECIAL or NONE
 * or out of range, unknown flag bits), ZXV_E_DUPLICATE (already
 * registered), ZXV_E_CAPACITY (registry or operator log full). */
ZXV_API int32_t zxv_paradox_registry_add(const zxv_cap *cap, zxv_paradox *book,
                                         const uint8_t sig[32], int32_t kind, uint8_t reason,
                                         uint8_t flags, int32_t owner_ok, uint32_t *out_index);
/* The index of a signature. ZXV_E_NOT_FOUND if absent; ZXV_E_ARG. */
ZXV_API int32_t zxv_paradox_registry_find(const zxv_paradox *book, const uint8_t sig[32],
                                          uint32_t *out_index);
ZXV_API int32_t zxv_paradox_registry_count(const zxv_paradox *book, uint32_t *out_count);
/* Entry `index`. src is the frontier record it was learned from, or
 * ZXV_PDX_SRC_SEED. Any out pointer may be NULL. ZXV_E_NOT_FOUND. */
ZXV_API int32_t zxv_paradox_registry_at(const zxv_paradox *book, uint32_t index,
                                        uint8_t out_sig[32], int32_t *out_kind,
                                        uint8_t *out_reason, uint8_t *out_flags,
                                        uint64_t *out_src);

/* ---- the logs ---- */

/* Record count of log `which` (ZXV_PDX_LOG_*). ZXV_E_ARG. */
ZXV_API int32_t zxv_paradox_log_count(const zxv_paradox *book, int32_t which,
                                      uint32_t *out_count);
/* The chain head: the last record's hash (32 zeros when empty). */
ZXV_API int32_t zxv_paradox_log_head(const zxv_paradox *book, int32_t which, uint8_t out_head[32]);
/* Record `index` as BYTES (no NUL; R4 two-call: *out_len is always set to
 * the record size, ZXV_E_BUFFER when buf is NULL or cap is smaller).
 * Operator and frontier records, ZXV_PDX_REC_BYTES, little-endian:
 *   [0,8) seq  [8] op  [9] kind  [10] tag  [11] outcome  [12,16) aux
 *   [16,48) a  [48,80) b  [80,112) prev  [112,144) hash
 *   hash = SHA-256("zxv/pdx-log/v1" || bytes[0,112)).
 *   op: 1 constructive (a = SHA-256 of the live values, b = of the
 *   proposed ones, both "zxv/pdx-params/v1" || n u32 || values i64),
 *   2 destructive (aux steps, a = proof hash, b = signature), 3 neutral
 *   (aux settled index or branch count, a = branch-set hash, b = settled
 *   id), 4 special (frontier: a = sig, b = subject; operator log: aux =
 *   frontier index, b = frontier record hash), 5 draft, 6 review or
 *   registry add, 7 graph pass (aux pass, a = pass digest, b = chain).
 * Quarantine records, ZXV_PDX_QREC_BYTES:
 *   [0,8) seq  [8] tag  [9] reason (the trips)  [10,14) submitter
 *   [14,46) subject (the RepairProof hash)  [46,78) prev  [78,110) hash
 *   hash = SHA-256("zxv/lpx-qlog/v1" || bytes[0,78)).
 * ZXV_E_NOT_FOUND (index >= count); ZXV_E_ARG. */
ZXV_API int32_t zxv_paradox_log_record(const zxv_paradox *book, int32_t which, uint32_t index,
                                       uint8_t *buf, size_t cap, size_t *out_len);
/* Recompute every link of log `which`. ZXV_OK, or ZXV_E_INVARIANT if any
 * record or the head no longer matches; that poisons the context (fail
 * closed), since only a memory fault in this process can alter a log. */
ZXV_API int32_t zxv_paradox_log_verify(zxv_paradox *book, int32_t which);

/* ---- constructive: a bounded policy update ---- */

/* A policy: n parameters (1..ZXV_PDX_POLICY_MAX) with published bounds
 * lo[i] <= hi[i] and initial values inside them. The bounds never change.
 * flags: ZXV_PDX_FLAG_MONEY / DEVICE make every update a proposal for the
 * owner (never applied automatically). The values are swarm parameters
 * (budget shares, routing weights, escrow timing), never balances. Needs
 * PARADOX. ZXV_E_ARG (NULL, n out of range, unknown flags), ZXV_E_RANGE
 * (lo > hi, or an initial value outside its bounds), ZXV_E_NOMEM. */
ZXV_API int32_t zxv_paradox_policy_create(const zxv_cap *cap, uint32_t n, const int64_t *lo,
                                          const int64_t *hi, const int64_t *initial,
                                          uint32_t flags, zxv_paradox_policy **out);
ZXV_API void zxv_paradox_policy_destroy(zxv_paradox_policy *policy);
/* One evaluation, then (if synth is not NULL) one proposed update of n
 * values (n must equal the policy's; it is ignored when synth is NULL).
 * The synthesis (the proposed values) is the caller's; this checks and
 * applies it:
 *   forbidden tag              -> an armed update is restored; ROLLED_BACK
 *   kind not CONSTRUCTIVE      -> REFUSED
 *   armed, tension > tension at apply -> previous values; ROLLED_BACK
 *   synth NULL                 -> KEPT (an armed update is disarmed)
 *   flagged policy (MONEY/DEVICE), or any synth value outside its
 *   [lo, hi]                   -> nothing changes; PROPOSAL_FOR_OWNER
 *   else                       -> applied and armed; APPLIED
 * `tension` is the caller's integer measure of the paradox (higher is
 * worse). Needs PARADOX. ZXV_E_ARG (NULL, a bad pair, n mismatch),
 * ZXV_E_CAPACITY (operator log full: nothing changed). */
ZXV_API int32_t zxv_paradox_constructive(const zxv_cap *cap, zxv_paradox *book,
                                         zxv_paradox_policy *policy, int32_t kind, uint8_t tag,
                                         const int64_t *synth, uint32_t n, uint32_t tension,
                                         int32_t *out_outcome);
/* The live values and the values before the last applied update (n
 * entries each; n must equal the policy's; either array may be NULL),
 * whether an applied update awaits evaluation, and the tension it was
 * applied at. Any out pointer may be NULL. */
ZXV_API int32_t zxv_paradox_policy_values(const zxv_paradox_policy *policy, int64_t *out_val,
                                          int64_t *out_prev, uint32_t n, int32_t *out_armed,
                                          uint32_t *out_tension_at);
/* The bounds and flags. Any out pointer may be NULL. */
ZXV_API int32_t zxv_paradox_policy_bounds(const zxv_paradox_policy *policy, int64_t *out_lo,
                                          int64_t *out_hi, uint32_t n, uint32_t *out_flags);
/* A proposed update as BYTES for the owner or for signing (R4 two-call,
 * *out_len = ZXV_PDX_PROPOSAL_BYTES(n)), little-endian:
 *   [0,4) n  [4,8) how many values are outside their bounds  [8,12) flags
 *   then n entries of 32 bytes: lo, hi, current value, proposed value (i64).
 * Reads only. ZXV_E_ARG (NULL synth or out_len, n mismatch). */
ZXV_API int32_t zxv_paradox_policy_proposal(const zxv_paradox_policy *policy,
                                            const int64_t *synth, uint32_t n, uint8_t *buf,
                                            size_t cap, size_t *out_len);

/* ---- destructive: contain, quarantine, roll back, prove ---- */

/* Detect and contain. The checks: steps >= step_limit (0: no limit),
 * spent >= budget (0: no budget), a repeated value in trace[0..trace_n)
 * (state fingerprints in visit order; trace may be NULL when trace_n is
 * 0), invariant_ok == 0 (the caller ran its invariant check), and a
 * forbidden tag. When the kind is DESTRUCTIVE or any check fires: the work
 * goes into the quarantine log (tag forbidden, reason = trips, submitter,
 * subject = the proof hash), state[0..len) is restored from checkpoint,
 * and out_proof receives the RepairProof (ZXV_PDX_REPAIR_BYTES,
 * little-endian):
 *   [0,4) trips  [4,8) steps  [8,12) spent  [12,16) trace_n
 *   [16,20) submitter  [20,24) len  [24,56) SHA-256 of the state as found
 *   [56,88) SHA-256 of the state after rollback (each "zxv/pdx-state/v1"
 *   || len u32 || state)  [88,120) the signature to share with peers
 *   (zxv_paradox_signature of `pattern`, the canonical description of the
 *   trap)  [120,152) the proof hash, SHA-256("zxv/pdx-repair/v1" ||
 *   bytes[0,120))  [152,160) the quarantine record's index.
 * Outcome CONTAINED. Otherwise outcome REFUSED and nothing changes (the
 * refusal is logged; out_proof is not written). Peers add the signature
 * to their registries (zxv_paradox_registry_add, kind DESTRUCTIVE) to
 * recognise the trap. It contains: it has no way to reply to or act
 * against the submitter. Signing the proof is the caller's job. Needs
 * PARADOX. ZXV_E_ARG (NULL, a bad pair, state/checkpoint NULL with len >
 * 0, pattern NULL with pattern_len > 0), ZXV_E_RANGE (trace_n >
 * ZXV_PDX_TRACE_MAX), ZXV_E_CAPACITY (a log is full: halt; the state may
 * already be restored, nothing is logged). */
ZXV_API int32_t zxv_paradox_destructive(const zxv_cap *cap, zxv_paradox *book, int32_t kind,
                                        uint8_t tag, uint32_t steps, uint32_t step_limit,
                                        uint32_t spent, uint32_t budget, const uint64_t *trace,
                                        uint32_t trace_n, int32_t invariant_ok, uint8_t *state,
                                        const uint8_t *checkpoint, uint32_t len,
                                        const uint8_t *pattern, uint32_t pattern_len,
                                        uint32_t submitter, int32_t *out_outcome,
                                        uint8_t out_proof[160]);

/* ---- neutral: hold every branch ---- */

/* A hold of up to `capacity` candidate branches (1..ZXV_PDX_HOLD_MAX). A
 * branch settles when its net evidence (for minus against) leads every
 * other by at least `decisive` (0 means 1), or alone reaches it, or the
 * owner picks it. Needs PARADOX. ZXV_E_ARG, ZXV_E_NOMEM. */
ZXV_API int32_t zxv_paradox_hold_create(const zxv_cap *cap, uint32_t capacity, uint32_t decisive,
                                        zxv_paradox_hold **out);
ZXV_API void zxv_paradox_hold_destroy(zxv_paradox_hold *hold);
/* Add a candidate (32-byte digest, e.g. one peer's timestamp record). The
 * same id again is the same candidate (ZXV_OK). ZXV_E_STATE (settled),
 * ZXV_E_CAPACITY (full), ZXV_E_ARG. */
ZXV_API int32_t zxv_paradox_hold_add(zxv_paradox_hold *hold, const uint8_t id[32]);
/* One piece of evidence for (positive non-zero) or against a candidate.
 * ZXV_E_NOT_FOUND (unknown id), ZXV_E_STATE (settled), ZXV_E_ARG. */
ZXV_API int32_t zxv_paradox_hold_evidence(zxv_paradox_hold *hold, const uint8_t id[32],
                                          int32_t positive);
/* The branch count, the settled branch (-1 while deferred) and the branch
 * that currently has decisive evidence (-1 if none). Any out may be NULL. */
ZXV_API int32_t zxv_paradox_hold_info(const zxv_paradox_hold *hold, uint32_t *out_count,
                                      int32_t *out_settled, int32_t *out_decisive);
/* Branch `index`: id and evidence counts. Any out may be NULL. */
ZXV_API int32_t zxv_paradox_hold_branch(const zxv_paradox_hold *hold, uint32_t index,
                                        uint8_t out_id[32], uint32_t *out_pos, uint32_t *out_neg);
/* Settle on decisive evidence or on owner_pick (a branch index, -1 for
 * none); otherwise keep every branch. Outcomes: SETTLED, DEFERRED,
 * ROLLED_BACK (forbidden: nothing settles), REFUSED (kind not NEUTRAL).
 * *out_settled (may be NULL) is the settled branch or -1. Needs PARADOX.
 * ZXV_E_ARG (NULL, a bad pair, owner_pick < -1), ZXV_E_NOT_FOUND
 * (owner_pick >= the branch count), ZXV_E_CAPACITY (log full). */
ZXV_API int32_t zxv_paradox_neutral(const zxv_cap *cap, zxv_paradox *book, zxv_paradox_hold *hold,
                                    int32_t kind, uint8_t tag, int32_t owner_pick,
                                    int32_t *out_outcome, int32_t *out_settled);

/* ---- special: the frontier ---- */

/* Record an unclassified paradox: a permanent frontier record (a = sig,
 * b = subject, the work's digest), never edited or erased, plus an
 * operator record. Outcomes: RECORDED (*out_rec = the frontier index),
 * REFUSED (kind not SPECIAL), ROLLED_BACK (forbidden); for those two
 * *out_rec = UINT32_MAX. out_rec may be NULL. The work stays sandboxed
 * (not run) until a reviewed classification exists. Needs PARADOX.
 * ZXV_E_ARG, ZXV_E_CAPACITY (a log is full: nothing written). */
ZXV_API int32_t zxv_paradox_special(const zxv_cap *cap, zxv_paradox *book, int32_t kind,
                                    uint8_t tag, const uint8_t sig[32], const uint8_t subject[32],
                                    int32_t *out_outcome, uint32_t *out_rec);
/* A draft classification of frontier record `rec`, for review: kind
 * CONSTRUCTIVE, DESTRUCTIVE or NEUTRAL, the caller's reason code and
 * ZXV_PDX_FLAG_* flags. Logged (op 5). out_draft receives
 * ZXV_PDX_DRAFT_BYTES: [0,4) rec  [4,36) the record's hash  [36,68) its
 * signature  [68] kind  [69] reason  [70] flags. Needs PARADOX. ZXV_E_ARG
 * (NULL, kind, unknown flags), ZXV_E_NOT_FOUND (no such record),
 * ZXV_E_CAPACITY (log full). */
ZXV_API int32_t zxv_paradox_draft(const zxv_cap *cap, zxv_paradox *book, uint32_t rec,
                                  int32_t kind, uint8_t reason, uint8_t flags,
                                  uint8_t out_draft[71]);
/* Review a draft (one of ours, or one a peer sent: it must match this
 * book's frontier record). A MONEY or DEVICE draft needs owner_ok; others
 * owner_ok or consensus_ok (the caller's quorum result). Outcomes:
 * ACCEPTED (a new registry entry, src = rec), PROPOSAL_FOR_OWNER (not
 * approved; nothing added), REFUSED (the signature is already registered,
 * or the registry is full). Logged (op 6). Needs PARADOX. ZXV_E_ARG,
 * ZXV_E_FORMAT (bad kind or flags in the bytes), ZXV_E_NOT_FOUND (no such
 * record), ZXV_E_VERIFY (hash or signature does not match the record),
 * ZXV_E_INVARIANT (the frontier chain is broken: context poisoned),
 * ZXV_E_CAPACITY (log full). */
ZXV_API int32_t zxv_paradox_review(const zxv_cap *cap, zxv_paradox *book,
                                   const uint8_t draft[71], int32_t owner_ok,
                                   int32_t consensus_ok, int32_t *out_outcome);

/* ---- complex paradoxes: a dependency graph ---- */

/* A graph of up to ZXV_PDX_GRAPH_MAX nodes that escalates to the owner
 * after `step_limit` passes (>= 1) without resolving. Needs PARADOX.
 * ZXV_E_ARG, ZXV_E_NOMEM. */
ZXV_API int32_t zxv_paradox_graph_create(const zxv_cap *cap, uint32_t step_limit,
                                         zxv_paradox_graph **out);
ZXV_API void zxv_paradox_graph_destroy(zxv_paradox_graph *g);
/* Add a node before the first pass. id = the hash of its signed input
 * (the sort key: insertion order never matters); (kind, tag) a classified
 * pair; tension magnitude `mag` and direction `dir` (-1, 0 or +1).
 * ZXV_E_DUPLICATE (id present), ZXV_E_CAPACITY (full), ZXV_E_STATE (after
 * the first pass), ZXV_E_ARG. */
ZXV_API int32_t zxv_paradox_graph_add_node(zxv_paradox_graph *g, const uint8_t id[32],
                                           int32_t kind, uint8_t tag, uint32_t mag, int32_t dir);
/* Node `id` depends on node `dep` (which may be added later; it must exist
 * by the first pass). Adding the same edge again does nothing. ZXV_E_ARG
 * (dep == id, NULL), ZXV_E_NOT_FOUND (no node `id`), ZXV_E_CAPACITY (more
 * than ZXV_PDX_MAX_DEPS), ZXV_E_STATE (after the first pass). */
ZXV_API int32_t zxv_paradox_graph_add_dep(zxv_paradox_graph *g, const uint8_t id[32],
                                          const uint8_t dep[32]);
/* Queue new evidence for a node, applied at the start of the next pass.
 * Several for one node in a batch merge "worst wins" (forbidden >
 * destructive > special > neutral > constructive > none; tension: larger
 * magnitude, then larger direction), so a batch gives the same result in
 * any order. Contained, rolled-back and tainted nodes stay so.
 * ZXV_E_NOT_FOUND, ZXV_E_CAPACITY (ZXV_PDX_UPDATES_MAX queued), ZXV_E_ARG. */
ZXV_API int32_t zxv_paradox_graph_update(zxv_paradox_graph *g, const uint8_t id[32],
                                         int32_t kind, uint8_t tag, uint32_t mag, int32_t dir);
/* One pass, in the fixed order: destructive and forbidden nodes contained
 * (and every node depending on them, transitively, tainted), then special
 * sandboxed (dependants wait), then neutral held, then constructive
 * applied where nothing contained, sandboxed, waiting or held reaches.
 * Nodes are taken in id order, so every peer with the same signed inputs
 * reaches the same states and the same chain. Logs one record (op 7).
 * ZXV_OK with RESOLVED (every node final) or PENDING; ZXV_E_PARADOX_STEP
 * when the pass count reaches the step limit unresolved (logged; the graph
 * stays readable and goes to the owner). Needs PARADOX. ZXV_E_NOT_FOUND
 * (a dependency names no node; first pass only, nothing logged),
 * ZXV_E_CAPACITY (log full: nothing changed), ZXV_E_ARG. */
ZXV_API int32_t zxv_paradox_graph_pass(const zxv_cap *cap, zxv_paradox *book,
                                       zxv_paradox_graph *g, int32_t *out_outcome);
/* A node's state (ZXV_PDX_NS_*; OPEN before the first pass).
 * ZXV_E_NOT_FOUND, ZXV_E_ARG. */
ZXV_API int32_t zxv_paradox_graph_state(const zxv_paradox_graph *g, const uint8_t id[32],
                                        int32_t *out_state);
/* Node `index` in id order (after the first pass, else ZXV_E_STATE): its
 * id, current kind and state. Any out may be NULL. ZXV_E_NOT_FOUND. */
ZXV_API int32_t zxv_paradox_graph_node_at(const zxv_paradox_graph *g, uint32_t index,
                                          uint8_t out_id[32], int32_t *out_kind,
                                          int32_t *out_state);
/* Node count, passes run, the net tension of the open nodes after the last
 * pass (magnitude and direction -1/0/+1), whether it escalated, and the
 * SHA-256 chain over every pass (zeros before the first). Any out may be
 * NULL. */
ZXV_API int32_t zxv_paradox_graph_info(const zxv_paradox_graph *g, uint32_t *out_nodes,
                                       uint32_t *out_pass, uint32_t *out_total_mag,
                                       int32_t *out_total_dir, int32_t *out_escalated,
                                       uint8_t out_chain[32]);

/* ===== end paradox ===== */

#ifdef __cplusplus
}
#endif

#endif /* ZXV_API_H */
