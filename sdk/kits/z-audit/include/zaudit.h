/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zaudit.h — z-audit, a static checker for ZXV plugins, run in CI before a
 * plugin is published (kit API 0.1).
 *
 * z-audit reads a plugin (.wasm) and its contract (.zc) and runs these
 * checks, in this order:
 *
 *   module.validate     the Z-Wasm profile validator (zw_validate) with the
 *                       host's grants and allow-list: the same filter a
 *                       Z-Wasm host applies at load.
 *   module.float-simd   every float or SIMD type and instruction, listed
 *                       (the validator stops at the first one).
 *   module.imports      every import against the allow-list: module, name
 *                       and signature; only function imports are allowed.
 *   contract.parse      the contract parses (z-contract).
 *   exports.declared    every exported function has a contract declaration
 *                       that can be bound to it (the rules zc_wasm_attach
 *                       applies at run time: parameter count after rat ->
 *                       2 arguments, no bytes, result present or not).
 *   contract.bounds     the declared bounds are well formed for the export:
 *                       int ranges an i32 parameter can carry, rat ranges
 *                       with a den rule that some value satisfies, result
 *                       ranges an i32 result can reach.
 *   exports.analysis    BOUNDED checks on straight-line export bodies: an
 *                       interval interpretation over i32/i64 constants,
 *                       locals and the contract's parameter ranges. It can
 *                       show that a result always stays in its range, may
 *                       leave it, or always leaves it, and checks constant
 *                       proposals and the number of propose calls. A body
 *                       with any branch, loop or instruction outside the
 *                       small supported subset is skipped and says so.
 *
 * WHAT THIS IS NOT. These are bounded, static checks. They are not a proof
 * that no Forbidden state is reachable, and a clean report does not show
 * that a plugin is correct. The runtime guards (Z-Wasm's validator and
 * fuel, z-contract's call checks) still run on every call. See README.md.
 *
 * KIT API STABILITY. 0.x: it may change before 1.0. It calls libzxv ABI
 * 1.3, Z-Wasm kit API 0.2 (and its internal decoder, built from the same
 * tree) and z-contract kit API 0.2. Opaque handles, int32_t status
 * returns, no floats.
 */
#ifndef ZAUDIT_H
#define ZAUDIT_H

#include <stddef.h>
#include <stdint.h>

#include "zxv_api.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ZAU_VERSION_MAJOR 0
#define ZAU_VERSION_MINOR 1
#define ZAU_VERSION_PATCH 0

/* Status codes: libzxv's 0 / -1..-99 pass through; z-audit owns -1401... An
 * audit that FINDS problems is not an error: zau_audit returns ZXV_OK and
 * the report says FAIL. */
#define ZAU_E_INPUT (-1401) /* the audit could not run on these inputs     */

const char *zau_status_name(int32_t status);
int32_t zau_last_error(char *buf, size_t cap, size_t *out_len);

/* ===== Options ===== */
typedef struct zau_options zau_options;

/* Flags (zau_options_set_flags). */
#define ZAU_F_STRICT      0x1u /* warnings fail the audit too              */
#define ZAU_F_NO_ANALYSIS 0x2u /* skip exports.analysis                     */

int32_t zau_options_create(zau_options **out_options);
void zau_options_destroy(zau_options *options);
/* Allow the host functions of Z-Wasm grant groups (ZW_GRANT_RATIONAL,
 * ZW_GRANT_LOGIC, ZW_GRANT_PROPOSE, OR-ed): exactly the imports a Z-Wasm
 * host with those grants allows. ZXV_E_ARG for unknown bits. */
int32_t zau_options_grant(zau_options *options, uint32_t grants);
/* Allow one more function import (wasm3 signature form, e.g. "i(IIi)"),
 * as zw_policy_allow_import. */
int32_t zau_options_allow_import(zau_options *options, const char *module, const char *name,
                                 const char *signature);
/* A Z-Wasm policy limit (ZW_LIM_*), as zw_policy_set_limit. */
int32_t zau_options_set_limit(zau_options *options, int32_t which, uint64_t value);
int32_t zau_options_set_flags(zau_options *options, uint32_t flags);

/* ===== Running an audit ===== */
typedef struct zau_report zau_report;

/* Audit `wasm` against `contract` (z-contract text, `contract_len` bytes;
 * NULL means "no contract": exports.declared then fails for every
 * exported function). The inputs are not kept. Returns ZXV_OK with a
 * report whatever it found, ZXV_E_ARG, or ZXV_E_NOMEM. */
int32_t zau_audit(const zau_options *options, const uint8_t *wasm, size_t wasm_len,
                  const char *contract, size_t contract_len, zau_report **out_report);
void zau_report_destroy(zau_report *report);

/* *out_pass 1 if the audit passed (no error; with ZAU_F_STRICT also no
 * warning). Any out pointer may be NULL. */
int32_t zau_report_verdict(const zau_report *report, int32_t *out_pass, uint32_t *out_errors,
                           uint32_t *out_warnings);

/* Check status. */
#define ZAU_PASS 0 /* nothing to report but information          */
#define ZAU_WARN 1 /* warnings only                              */
#define ZAU_FAIL 2 /* at least one error                         */
#define ZAU_SKIP 3 /* not run (an earlier problem, or a flag)    */

/* Finding severity. */
#define ZAU_INFO    0
#define ZAU_WARNING 1
#define ZAU_ERROR   2

/* Checks, in report order. Strings are static to the report. */
int32_t zau_report_check_count(const zau_report *report, uint32_t *out_count);
int32_t zau_report_check(const zau_report *report, uint32_t index, const char **out_id,
                         int32_t *out_status, const char **out_summary);
/* Findings, in report order. *out_check is the index of its check;
 * *out_export the export it is about ("" if none); *out_func the
 * function index or -1; *out_offset the byte offset in the module or -1.
 * Any out pointer may be NULL. */
int32_t zau_report_finding_count(const zau_report *report, uint32_t *out_count);
int32_t zau_report_finding(const zau_report *report, uint32_t index, uint32_t *out_check,
                           int32_t *out_severity, const char **out_code,
                           const char **out_message, const char **out_export,
                           int64_t *out_func, int64_t *out_offset);

/* The report as JSON (machine-readable) or as text (for people), R4
 * two-call buffers: a NULL or small buffer gives ZXV_E_BUFFER with
 * *out_len set to the length without the NUL. `module_label` and
 * `contract_label` (e.g. file names; may be NULL) are only printed. The
 * JSON layout is described in README.md, "Report format". */
int32_t zau_report_json(const zau_report *report, const char *module_label,
                        const char *contract_label, char *buf, size_t cap, size_t *out_len);
int32_t zau_report_text(const zau_report *report, const char *module_label,
                        const char *contract_label, char *buf, size_t cap, size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif /* ZAUDIT_H */
