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
 * "GHS", "XOF") in Ccy attributes. The Vino rail numerics DEBIT 555 (NCR),
 * CREDIT 777 (NRE) and EQUITY 888 (PNS) are platform-internal: all three are
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
#define ZXV_ABI_VERSION_MINOR 4
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
    ZXV_RAIL_DEBIT = 0,  /* 555 NCR: asset / backing  */
    ZXV_RAIL_CREDIT = 1, /* 777 NRE: claim / liability */
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

#ifdef __cplusplus
}
#endif

#endif /* ZXV_API_H */
