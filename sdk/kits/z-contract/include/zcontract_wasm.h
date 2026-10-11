/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zcontract_wasm.h — enforce a z-contract on a Z-Wasm plugin export
 * (kit API 0.2). Needs sdk/kits/z-wasm (kit API 0.2, the call-check hook).
 *
 *   zc_wasm_attach(plugin, "fee", contract, "fee", log, &guard);
 *   st = zw_plugin_call(plugin, "fee", args, n, &res, &has, &fuel);
 *   if (st == ZW_E_CHECK) zc_wasm_last_violation(guard, ...);
 *
 * After attach, EVERY zw_plugin_call of that export is checked, whoever
 * makes it:
 *   - the arguments, before anything runs. A violation refuses the call
 *     (ZW_E_CHECK); no fuel is used and the plugin stays READY, because
 *     bad input is the caller's fault. One evidence record, tag
 *     false/neutral/potential (an ordinary refusal of work not started).
 *   - the result and the proposals the call staged, after it returns and
 *     before any proposal reaches the S0 queue. A violation stops the
 *     plugin exactly like a trap: its proposals are discarded, it becomes
 *     DEAD, the call returns ZW_E_CHECK. One evidence record, tag
 *     false/forbidden/manifest (the kernel's action for a forbidden tag is
 *     rollback and quarantine).
 *   - if the evidence log refuses a record (full, or its sink refused), the
 *     call is refused as above AND the guard refuses every later call
 *     (fail closed: no unrecorded violations).
 *
 * How contract types map onto Wasm i32/i64 arguments:
 *   int, truth, tag   one argument (truth 0..3, tag 0..63 zxv_hex_* codes)
 *   rat               two arguments: numerator, then denominator
 *   bytes             not mappable (Wasm exports take no buffers): ZC_E_MAPPING
 * The result (one i32/i64) may be int, truth or tag; a rat or bytes result
 * is ZC_E_MAPPING: return a rational through `proposes rat ...` instead.
 * Proposals are always exact rationals (`proposes int` requires den 1). */
#ifndef ZCONTRACT_WASM_H
#define ZCONTRACT_WASM_H

#include "zcontract.h"
#include "zwasm.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct zc_wasm_guard zc_wasm_guard;

/* Attach function `fn` of `contract` to `export_name` of `plugin`. The
 * contract, the log and the plugin must outlive the guard; the guard keeps
 * pointers (no copies). Attaching to an export that already has a check
 * replaces it. ZXV_E_NOT_FOUND (no such fn), ZC_E_MAPPING (see above),
 * ZW_E_EXPORT (no such export), ZXV_E_CAPACITY (16 checked exports). */
int32_t zc_wasm_attach(zw_plugin *plugin, const char *export_name, const zc_contract *contract,
                       const char *fn, zc_evlog *log, zc_wasm_guard **out_guard);
/* Remove the export's check and free the guard. Call it BEFORE
 * zw_plugin_destroy (like libzxv R6: children first). NULL is ignored. */
void zc_wasm_detach(zc_wasm_guard *guard);

/* Flatten typed values into the export's arguments (rat -> num, den).
 * The values are not checked here (the guard does that on the call).
 * ZC_E_MAPPING for a bytes value, ZXV_E_BUFFER if `cap` is too small
 * (*out_n set). */
int32_t zc_wasm_args(const zc_vals *vals, int64_t *args, uint32_t cap, uint32_t *out_n);

/* The guard's last violation (ZXV_E_NOT_FOUND if none yet): the part
 * (ZC_PART_*), the index, the ZC_V_* reason, the evidence record's seq
 * (0 if the log refused it) and the message (R4). */
int32_t zc_wasm_last_violation(const zc_wasm_guard *guard, int32_t *out_part,
                               uint32_t *out_index, int32_t *out_reason, uint64_t *out_seq,
                               char *msg, size_t msg_cap, size_t *out_msg_len);
/* Counters: calls checked, violations found, and 1 if the log refused a
 * record (the guard now refuses every call). */
int32_t zc_wasm_stats(const zc_wasm_guard *guard, uint64_t *out_calls, uint64_t *out_violations,
                      int32_t *out_log_failed);

#ifdef __cplusplus
}
#endif

#endif /* ZCONTRACT_WASM_H */
