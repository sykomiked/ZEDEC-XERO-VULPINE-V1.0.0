/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zwasm.h — Z-Wasm, the ZXV plugin sandbox kit (kit API 0.2).
 *
 * A ZXV plugin is a WebAssembly module. Z-Wasm does three things with it:
 *
 *   1. VALIDATE (zw_validate). A load-time filter, written without a single
 *      float in its own code (R11), parses the binary (core spec 2.0
 *      sections) and refuses any module that uses a float or SIMD type or
 *      instruction anywhere, declares memory without a maximum (or above
 *      the policy's page limit), declares a table without a maximum,
 *      imports anything but the functions the host allowed, has a start
 *      function, or passes a size limit. A refusal names the reason, the
 *      section, the function index and the byte offset.
 *   2. RUN (zw_plugin_load / zw_plugin_call). The module is metered (a fuel
 *      counter is injected at every basic block) and run in the vendored
 *      wasm3 interpreter, built with its float support compiled out. Fuel
 *      for each call is taken from a libzxv rmag budget and charged back to
 *      it, exactly, after the call. A trap or running out of fuel stops the
 *      plugin and is reported; the host's zxv_ctx is never touched by it.
 *   3. PROPOSE (Tri-Isolate starter). A plugin cannot reach the ledger. It
 *      can only call the host functions the host granted; the propose host
 *      function puts an exact rational into a host-side queue in state S0
 *      (ZXV_LP_NEITHER, neutral). Nothing happens until the host accepts a
 *      proposal with its own capability.
 *
 * HONEST LIMITS. Isolation comes from the Wasm runtime (memory safety of
 * the interpreter) and from capabilities (least privilege). It is NOT a
 * proof that a plugin is correct, and the validator is a profile filter
 * plus a structural decoder, not a full Wasm type checker (wasm3 type
 * checks at load). See sdk/kits/z-wasm/README.md, "Threat model".
 *
 * KIT API STABILITY. This kit API is 0.x: it may change before 1.0. The
 * libzxv ABI it calls (zxv_api.h, ABI 1.2) is frozen as usual. The kit
 * follows the libzxv shape anyway: opaque handles, fixed-width integers,
 * int32_t status returns, no floats.
 */
#ifndef ZWASM_H
#define ZWASM_H

#include <stddef.h>
#include <stdint.h>

#include "zxv_api.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ZW_VERSION_MAJOR 0
#define ZW_VERSION_MINOR 2
#define ZW_VERSION_PATCH 0

/* ===== Status codes =====
 * 0 and -1 .. -99 are libzxv's ZXV_OK / ZXV_E_* (passed through unchanged,
 * e.g. ZXV_E_BUDGET from the rmag budget, ZXV_E_CAP from a capability).
 * Kit-specific codes start at -1001. */
#define ZW_OK          0
#define ZW_E_REJECTED  (-1001) /* the validator refused the module (see zw_validate) */
#define ZW_E_LINK      (-1002) /* the runtime refused to parse, link or compile it  */
#define ZW_E_TRAP      (-1003) /* the plugin trapped; the plugin is now DEAD        */
#define ZW_E_FUEL      (-1004) /* the call ran out of fuel; the plugin is now DEAD  */
#define ZW_E_EXPORT    (-1005) /* no such exported function, or not an i32/i64 one */
#define ZW_E_DEAD      (-1006) /* the plugin stopped earlier; destroy and reload it */
#define ZW_E_CHECK     (-1007) /* an attached call check refused the call (0.2)     */

/* Static, never-NULL name ("ZW_E_FUEL"); ZXV codes give libzxv's name. */
const char *zw_status_name(int32_t status);

/* The calling thread's last error detail (libzxv R4 two-call buffer: if
 * buf is NULL or cap <= *out_len, ZXV_E_BUFFER and nothing written). */
int32_t zw_last_error(char *buf, size_t cap, size_t *out_len);

/* ===== Validator reasons (zw_validate *out_reason) ===== */
#define ZW_R_OK             0
#define ZW_R_MALFORMED      1  /* not a well-formed Wasm 2.0 binary             */
#define ZW_R_TOO_LARGE      2  /* module bytes over ZW_LIM_MODULE_BYTES         */
#define ZW_R_FLOAT_TYPE     3  /* f32/f64 value type (types, locals, globals,   */
                               /* imports, block types, select)                 */
#define ZW_R_FLOAT_OP       4  /* a float instruction (incl. trunc_sat 0xFC 0-7)*/
#define ZW_R_SIMD           5  /* v128 type or any 0xFD-prefixed instruction    */
#define ZW_R_MEMORY_NO_MAX  6  /* a memory without a declared maximum           */
#define ZW_R_MEMORY_LIMIT   7  /* memory maximum above ZW_LIM_MEMORY_PAGES      */
#define ZW_R_TABLE_LIMIT    8  /* table without maximum, above the limit, or >1 */
#define ZW_R_IMPORT_DENIED  9  /* a function import not on the allow-list       */
#define ZW_R_IMPORT_KIND    10 /* a table, memory or global import              */
#define ZW_R_IMPORT_SIG     11 /* allowed import name, different signature      */
#define ZW_R_START          12 /* a start function (refused: it would run       */
                               /* before the host sets the fuel)                */
#define ZW_R_LIMIT          13 /* a count limit (functions, locals, body size,  */
                               /* nesting, data bytes, ...)                     */
#define ZW_R_RESERVED_NAME  14 /* an export named "__zw_..." (kit-reserved)     */
#define ZW_R_UNSUPPORTED    15 /* valid elsewhere, outside this profile: shared */
                               /* or 64-bit memory, threads, exceptions, tail   */
                               /* calls, GC, bulk memory when not enabled       */
#define ZW_R_INDEX          16 /* an index out of range (type, func, local,     */
                               /* global, table, memory, data, elem, label)     */

/* Static, never-NULL name of a reason ("FLOAT_OP"). */
const char *zw_reason_name(int32_t reason);

/* zw_validate *out_func when the problem is not inside a function body. */
#define ZW_NO_FUNC UINT32_C(0xFFFFFFFF)
/* zw_validate *out_section before any section (the header) / custom. */
#define ZW_SEC_HEADER UINT32_C(0xFFFFFFFF)

/* ===== Policy: limits and the import allow-list ===== */
typedef struct zw_policy zw_policy;

#define ZW_LIM_MODULE_BYTES  1  /* default 1 MiB                                 */
#define ZW_LIM_MEMORY_PAGES  2  /* max declared memory pages (64 KiB); dflt 16   */
#define ZW_LIM_TABLE_ELEMS   3  /* max declared table size; default 1024         */
#define ZW_LIM_FUNCTIONS     4  /* imported + defined; default 4096              */
#define ZW_LIM_BODY_BYTES    5  /* one function body; default 128 KiB            */
#define ZW_LIM_LOCALS        6  /* locals of one function; default 1024          */
#define ZW_LIM_GLOBALS       7  /* default 256                                   */
#define ZW_LIM_TYPES         8  /* default 1024                                  */
#define ZW_LIM_IMPORTS       9  /* default 64                                    */
#define ZW_LIM_EXPORTS       10 /* default 256                                   */
#define ZW_LIM_DATA_BYTES    11 /* all data segments together; default 1 MiB     */
#define ZW_LIM_NESTING       12 /* block nesting depth; default 256              */
#define ZW_LIM_PARAMS        13 /* params or results of one type; default 16     */
#define ZW_LIM_BULK_MEMORY   14 /* 1 = allow 0xFC 8..17; default 0 (see README)  */
#define ZW_LIM_ELEM_SEGMENTS 15 /* elements in all element segments; dflt 4096  */

int32_t zw_policy_create(zw_policy **out_policy);
void zw_policy_destroy(zw_policy *policy);
/* ZXV_E_ARG for an unknown `which` or a value the kit cannot honour
 * (e.g. MEMORY_PAGES above 65536, MODULE_BYTES above 64 MiB). */
int32_t zw_policy_set_limit(zw_policy *policy, int32_t which, uint64_t value);
int32_t zw_policy_get_limit(const zw_policy *policy, int32_t which, uint64_t *out_value);
/* Allow one function import. `signature` is wasm3's form: return type then
 * the parameters in parentheses, 'i' i32, 'I' i64, 'v' void, e.g. "i(IIi)".
 * Only i/I/v are accepted. Names are UTF-8, at most 63 bytes. A module may
 * import an allowed function more than once; it is refused if the
 * signature differs (ZW_R_IMPORT_SIG). ZXV_E_DUPLICATE for the same
 * module/name twice, ZXV_E_CAPACITY past 64 entries. */
int32_t zw_policy_allow_import(zw_policy *policy, const char *module, const char *name,
                               const char *signature);

/* Validate a module against a policy. Returns ZW_OK or ZW_E_REJECTED (or
 * ZXV_E_ARG for NULL arguments). Unlike libzxv R3, the out parameters are
 * written on ZW_OK AND on ZW_E_REJECTED, since the reason is the result:
 *   *out_reason    ZW_R_*
 *   *out_section   Wasm section id (0 custom .. 12 data count), or
 *                  ZW_SEC_HEADER
 *   *out_func      function index in the module's function index space
 *                  (imports first), or ZW_NO_FUNC
 *   *out_offset    byte offset into `wasm` of the offending byte
 * `msg` (may be NULL) receives a one-line message, TRUNCATED to fit cap
 * (always NUL-terminated when cap > 0); *out_msg_len gets its full length.
 * Any out pointer may be NULL. Pure: no allocation, no global state. */
int32_t zw_validate(const zw_policy *policy, const uint8_t *wasm, size_t len,
                    int32_t *out_reason, uint32_t *out_section, uint32_t *out_func,
                    uint64_t *out_offset, char *msg, size_t msg_cap, size_t *out_msg_len);

/* ===== Host: the capabilities and budget plugins run under ===== */
typedef struct zw_host zw_host;
typedef struct zw_plugin zw_plugin;

/* Host function groups a host may grant (zw_host_grant). Each adds its
 * imports (module "zxv") to the host's allow-list; nothing else can be
 * imported. Signatures are listed in README.md, "Host functions". */
#define ZW_GRANT_RATIONAL 0x1u /* zxv.rat_add/sub/mul/div/cmp/normalize (pure)    */
#define ZW_GRANT_LOGIC    0x2u /* zxv.lp_not/and/or/from_evidence (pure)          */
#define ZW_GRANT_PROPOSE  0x4u /* zxv.propose: put a value in the S0 queue        */

/* Fuel: one unit per Wasm instruction (charged per basic block, before it
 * runs) plus ZW_HOST_CALL_FUEL per host-function call. */
#define ZW_HOST_CALL_FUEL 16

/* Create a host. `budget_cap` must hold ZXV_CAP_BUDGET and belong to the
 * same context as `rmag`; `budget` is a budget id in `rmag` (an ENFORCED
 * budget, typically ZXV_RES_COMPUTE_CYCLES). The host keeps both pointers
 * (no copy): the cap and the rmag must outlive the host and its plugins.
 * The host starts with no grants and the default policy limits. */
int32_t zw_host_create(const zxv_cap *budget_cap, zxv_rmag *rmag, uint32_t budget,
                       zw_host **out_host);
/* Destroys the host's queue. Every plugin of the host must be destroyed
 * first (ZXV_E_STATE is not possible here, so this is a documented rule,
 * like libzxv R6). NULL is ignored. */
void zw_host_destroy(zw_host *host);
/* Grant host-function groups (ZW_GRANT_*, OR-ed). Only affects plugins
 * loaded afterwards. Unknown bits are ZXV_E_ARG. */
int32_t zw_host_grant(zw_host *host, uint32_t grants);
/* The host's policy (owned by the host; change limits before loading). */
int32_t zw_host_policy(zw_host *host, zw_policy **out_policy);
/* Most fuel one call may use, whatever the budget holds. Default 10^7. */
int32_t zw_host_set_call_fuel(zw_host *host, uint64_t max_fuel);
/* The capability bits a capability must hold to accept or reject a
 * proposal. Default ZXV_CAP_LEDGER_POST. 0 or unknown bits: ZXV_E_ARG. */
int32_t zw_host_set_accept_caps(zw_host *host, uint64_t needed);
/* wasm3 value-stack bytes per plugin (default 64 KiB, 4 KiB .. 1 MiB). */
int32_t zw_host_set_stack_bytes(zw_host *host, uint32_t bytes);

/* ===== Plugins ===== */
#define ZW_PLUGIN_READY 0
#define ZW_PLUGIN_DEAD  1 /* trapped or ran out of fuel; only destroy is useful */

/* Validate (the host's policy plus its grants), meter, parse, link and
 * compile a module. ZW_E_REJECTED with the validator's message in
 * zw_last_error; ZW_E_LINK if wasm3 refuses it. The bytes are copied.
 * `name` (<= 31 bytes, may be NULL) labels its proposals. */
int32_t zw_plugin_load(zw_host *host, const uint8_t *wasm, size_t len, const char *name,
                       zw_plugin **out_plugin);
void zw_plugin_destroy(zw_plugin *plugin);
int32_t zw_plugin_state(const zw_plugin *plugin, int32_t *out_state);

/* Call an exported function whose parameters and result are i32 or i64
 * (at most 8 parameters, at most 1 result). Each argument is an int64; an
 * i32 parameter must be in [INT32_MIN, UINT32_MAX] (ZXV_E_RANGE otherwise)
 * and is passed as its low 32 bits. An i32 result is sign-extended.
 *
 * Fuel = min(the host's call fuel, floor(remaining of the rmag budget)).
 * No fuel left: ZXV_E_BUDGET and nothing runs. After the call the fuel
 * used is consumed from the budget (zxv_rmag_consume, exact integer).
 * Proposals made during the call reach the queue only if the call returns
 * normally; on ZW_E_TRAP / ZW_E_FUEL they are discarded and the plugin
 * becomes DEAD. *out_fuel_used is written on ZW_OK, ZW_E_TRAP and
 * ZW_E_FUEL. Any out pointer may be NULL. */
int32_t zw_plugin_call(zw_plugin *plugin, const char *export_name, const int64_t *args,
                       uint32_t nargs, int64_t *out_result, int32_t *out_has_result,
                       uint64_t *out_fuel_used);

/* ===== Call checks (kit API 0.2) =====
 * An integration hook, used by sdk/kits/z-contract to enforce a contract on
 * an export. A host attaches at most one check to each export of a plugin
 * (up to ZW_CHECKS_MAX exports). Every zw_plugin_call of that export then
 * runs the check twice:
 *
 *   ZW_CHECK_BEFORE  after the arguments are checked against the export's
 *                    i32/i64 types, before any fuel is set or code runs.
 *                    `result`/`has_result` are 0. A non-zero return refuses
 *                    the call: ZW_E_CHECK, nothing runs, no fuel is used,
 *                    the plugin stays READY (bad input is the caller's
 *                    fault, not the plugin's).
 *   ZW_CHECK_AFTER   after the call returned normally and its fuel was
 *                    charged, before its staged proposals reach the S0
 *                    queue. zw_plugin_staged_count / zw_plugin_staged_get
 *                    read those proposals (only during this stage). A
 *                    non-zero return is handled like a trap: the staged
 *                    proposals are discarded, the plugin becomes DEAD and
 *                    the call returns ZW_E_CHECK. *out_result is not
 *                    written.
 *
 * A check must not call zw_plugin_call, zw_plugin_set_check or
 * zw_plugin_destroy on the same plugin (ZXV_E_STATE for the first two).
 * The check keeps its own record of why it refused; zw_last_error only
 * names the export and the stage. `user` is passed through untouched and
 * must outlive the attachment. */
#define ZW_CHECK_BEFORE 0
#define ZW_CHECK_AFTER  1
#define ZW_CHECKS_MAX   16

typedef int32_t (*zw_check_fn)(void *user, zw_plugin *plugin, int32_t stage,
                               const char *export_name, const int64_t *args, uint32_t nargs,
                               int64_t result, int32_t has_result);

/* Attach (fn != NULL) or remove (fn == NULL) the check of one export.
 * ZW_E_EXPORT if the plugin exports no such function, ZXV_E_CAPACITY past
 * ZW_CHECKS_MAX exports, ZXV_E_STATE from inside a check. Replacing an
 * export's check is allowed. A DEAD plugin accepts the call (it changes
 * nothing that can still run). */
int32_t zw_plugin_set_check(zw_plugin *plugin, const char *export_name, zw_check_fn fn,
                            void *user);
/* During ZW_CHECK_AFTER only (ZXV_E_STATE otherwise): the proposals the
 * call staged, in order. The label follows the R4 two-call rule of
 * zw_proposal_get. */
int32_t zw_plugin_staged_count(const zw_plugin *plugin, uint32_t *out_count);
int32_t zw_plugin_staged_get(const zw_plugin *plugin, uint32_t index, uint32_t *out_kind,
                             int64_t *out_num, int64_t *out_den, uint8_t *label,
                             size_t label_cap, size_t *out_label_len);

/* ===== The S0 proposal queue (host side) =====
 * Proposal states are libzxv's four-valued logic values:
 *   ZXV_LP_NEITHER  S0: proposed, neutral, waiting for the host
 *   ZXV_LP_TRUE     accepted by the host
 *   ZXV_LP_FALSE    rejected by the host
 * Ids start at 1 and never repeat within a host. At most 1024 proposals
 * are pending; past that, propose returns ZXV_E_CAPACITY to the plugin. */
int32_t zw_queue_pending(zw_host *host, uint32_t *out_count);
/* Oldest pending proposal id; ZXV_E_NOT_FOUND if none. */
int32_t zw_queue_next(zw_host *host, uint64_t *out_id);
/* Read a proposal. The label is the plugin's bytes, at most 64 (R4 two-call
 * on label: NULL buf or cap <= len gives ZXV_E_BUFFER with *out_label_len
 * set and nothing else written). `plugin_name` gets the plugin's name the
 * same way into its own buffer of 32 bytes (always fits). */
int32_t zw_proposal_get(zw_host *host, uint64_t id, int32_t *out_state, uint32_t *out_kind,
                        int64_t *out_num, int64_t *out_den, char plugin_name[32],
                        uint8_t *label, size_t label_cap, size_t *out_label_len);
/* Accept / reject a pending proposal. `cap` is the HOST's capability; it
 * must pass zxv_cap_check for the accept caps (ZXV_E_CAP otherwise; the
 * proposal stays S0). A decided proposal gives ZXV_E_STATE. Accepting
 * changes nothing in libzxv: the host then acts with its own cap. */
int32_t zw_proposal_accept(zw_host *host, const zxv_cap *cap, uint64_t id);
int32_t zw_proposal_reject(zw_host *host, const zxv_cap *cap, uint64_t id);
/* Accept and post in one step: the proposal's value must be a positive
 * integer (den == 1); it is posted as amount_minor with
 * zxv_ledger_post_cap(cap, from, to, value, ccy, reference, out_entry_id).
 * The proposal becomes TRUE only if the post succeeded; otherwise it stays
 * S0 and the libzxv status is returned (ZXV_E_FUNDS, ZXV_E_CAP, ...). */
int32_t zw_proposal_accept_post(zw_host *host, const zxv_cap *cap, uint64_t id,
                                uint32_t from_account, uint32_t to_account, const char *ccy,
                                const char *reference, uint64_t *out_entry_id);

#ifdef __cplusplus
}
#endif

#endif /* ZWASM_H */
