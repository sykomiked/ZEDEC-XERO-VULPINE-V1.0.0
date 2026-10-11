/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zffi.h — z-ffi: a strictly typed boundary for calling C libraries from
 * ZXV code (kit API 0.1).
 *
 * A SPEC declares the C functions a host may call, with the exact types
 * and ranges it accepts:
 *
 *     library libm;
 *     include <math.h>;
 *     fn sqrt(x: f64 from rat) -> f64 as rat;
 *     fn ldexp(x: f64 from rat, e: int[-1074..1024]) -> f64 as rat needs BUDGET;
 *
 * zf-gen turns the spec into a C header and source with one WRAPPER per
 * function (zf_libm_sqrt, zf_libm_ldexp). A wrapper:
 *
 *   1. requires a capability: zxv_cap_check(cap, needs) must pass, where
 *      `needs` is the ZXV_CAP_* bits the spec names (none: the cap must
 *      still be live, unrevoked and attested TRUE);
 *   2. checks every integer argument against its declared range, which
 *      defaults to the C type's range and can only be narrowed (abs on
 *      INT_MIN and toupper on 300 are undefined behaviour in C; a spec
 *      that says int[-INT_MAX..INT_MAX] or int[-1..255] refuses them);
 *   3. takes floating-point ARGUMENTS as exact rationals and converts them
 *      only when the float type holds that value exactly (3/4 yes, 1/10
 *      no: ZF_E_INEXACT, never rounded);
 *   4. refuses floating-point RESULTS unless the spec opts in with
 *      `as rat`, and then converts the returned bits exactly with
 *      zxv_rat_from_ieee754 (NaN and infinities: ZXV_E_FLOAT; a value with
 *      no int64 rational form: ZXV_E_RANGE);
 *   5. checks integer results against their declared range (ZF_E_RESULT);
 *   6. proves the declaration at COMPILE time: each wrapper carries a
 *      _Static_assert(_Generic(&fn, <declared type>: 1, default: 0)), so a
 *      spec that disagrees with the library's own prototype does not
 *      compile.
 *
 * HONEST LIMIT. This is a typed wrapper generator plus runtime checks. It
 * is NOT a sandbox: the C function runs in the caller's process with the
 * caller's privileges, and a bug or a hostile library can do anything the
 * process can. The capability check is least privilege inside a trusted
 * process, the same limit libzxv states for zxv_cap. For untrusted code
 * use Z-Wasm (sdk/kits/z-wasm). See README.md.
 *
 * R11. The runtime below and zf-gen contain no float type. GENERATED code
 * necessarily holds float and double values for the duration of the call;
 * it is generated, never hand-edited, and is outside the no-float check.
 *
 * KIT API STABILITY. 0.x. Calls only libzxv ABI 1.2. Opaque handles,
 * int32_t status returns.
 */
#ifndef ZFFI_H
#define ZFFI_H

#include <stddef.h>
#include <stdint.h>

#include "zxv_api.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ZF_VERSION_MAJOR 0
#define ZF_VERSION_MINOR 1
#define ZF_VERSION_PATCH 0

/* z-ffi status codes start at -1201. libzxv codes are used where they fit:
 * ZXV_E_CAP (the capability check failed), ZXV_E_FLOAT (a NaN or infinite
 * result), ZXV_E_RANGE (a finite result with no int64 rational form),
 * ZXV_E_ARG (a NULL output pointer, a zero denominator). */
#define ZF_E_SPEC    (-1201) /* zf_gen: the spec does not parse or is not valid  */
#define ZF_E_RANGE   (-1202) /* an argument outside its declared range or length */
                             /* (a cstr containing NUL included)                 */
#define ZF_E_INEXACT (-1203) /* a rational the float type cannot hold exactly    */
#define ZF_E_RESULT  (-1204) /* an integer result outside its declared range     */

/* Static, never-NULL ("ZF_E_RANGE"); libzxv codes via zxv_status_name. */
const char *zf_status_name(int32_t status);
/* The calling thread's last error message (R4: NULL buf or small cap gives
 * ZXV_E_BUFFER with *out_len set). Wrappers name the function and argument:
 * "libc.abs: argument 0 'x' = -2147483648 is below -2147483647". */
int32_t zf_last_error(char *buf, size_t cap, size_t *out_len);

/* ===== Exact rational <-> IEEE-754 bits (pure) ===== */
/* The binary64 / binary32 encoding of num/den when that format holds the
 * value EXACTLY: den (reduced) a power of two and the odd part of num at
 * most 53 / 24 bits. Zero gives +0. ZF_E_INEXACT otherwise (1/10, 1/3,
 * 2^53 + 1); ZXV_E_ARG for den 0; ZXV_E_RANGE for INT64_MIN (as libzxv's
 * zxv_rat_normalize). The way back is zxv_rat_from_ieee754. */
int32_t zf_rat_to_f64_bits(int64_t num, int64_t den, uint64_t *out_bits);
int32_t zf_rat_to_f32_bits(int64_t num, int64_t den, uint32_t *out_bits);

/* ===== Runtime used by generated wrappers =====
 * `fn` is "library.function". The wrappers call these in order; a host
 * normally does not call them directly. */
int32_t zf_enter(const zxv_cap *cap, uint64_t needs, const char *fn);
int32_t zf_null_out(const char *fn);
int32_t zf_arg_i64(const char *fn, uint32_t index, const char *name, int64_t value, int64_t lo,
                   int64_t hi);
int32_t zf_arg_u64(const char *fn, uint32_t index, const char *name, uint64_t value, uint64_t lo,
                   uint64_t hi);
int32_t zf_arg_f64(const char *fn, uint32_t index, const char *name, int64_t num, int64_t den,
                   uint64_t *out_bits);
int32_t zf_arg_f32(const char *fn, uint32_t index, const char *name, int64_t num, int64_t den,
                   uint32_t *out_bits);
/* `len` bytes at `s` (s may be NULL only when len is 0), lo <= len <= hi,
 * no NUL among them: *out_copy is a NUL-terminated copy for the C call
 * (free with zf_cstr_free). The callee never reads the caller's buffer. */
int32_t zf_arg_cstr(const char *fn, uint32_t index, const char *name, const char *s, size_t len,
                    size_t lo, size_t hi, char **out_copy);
void zf_cstr_free(char *copy);
int32_t zf_res_i64(const char *fn, int64_t value, int64_t lo, int64_t hi, int64_t *out);
int32_t zf_res_u64(const char *fn, uint64_t value, uint64_t lo, uint64_t hi, uint64_t *out);
/* binary64 bits of the result -> the exact rational (zxv_rat_from_ieee754). */
int32_t zf_res_f64(const char *fn, uint64_t bits, int64_t *out_num, int64_t *out_den);

/* ===== The generator (also built into zf-gen) ===== */
/* Limits of one spec. */
#define ZF_MAX_SPEC   (1024u * 1024u) /* bytes                               */
#define ZF_MAX_FNS    256u
#define ZF_MAX_PARAMS 8u
#define ZF_MAX_NAME   63u             /* identifiers                         */
#define ZF_MAX_INCLUDES 32u

/* Generate the wrappers for a spec. `header_name` is how the source
 * includes the header ("libm_zf.h"). On success *out_header and
 * *out_source are NUL-terminated texts (free with zf_gen_free) and
 * *out_len_* their lengths (either length pointer may be NULL). On
 * ZF_E_SPEC, *out_line / *out_col (1-based) / *out_offset locate the
 * problem and `msg` gets "line L, column C: ..." truncated to fit (always
 * NUL-terminated when msg_cap > 0); the position pointers may be NULL. The
 * header records the spec's SHA3-256, so a wrapper set can be traced to
 * the exact spec that produced it. ZXV_E_NOMEM, ZXV_E_ARG as usual. */
int32_t zf_gen(const char *spec, size_t len, const char *header_name, char **out_header,
               size_t *out_header_len, char **out_source, size_t *out_source_len,
               uint32_t *out_line, uint32_t *out_col, uint64_t *out_offset, char *msg,
               size_t msg_cap);
void zf_gen_free(char *text);

#ifdef __cplusplus
}
#endif

#endif /* ZFFI_H */
