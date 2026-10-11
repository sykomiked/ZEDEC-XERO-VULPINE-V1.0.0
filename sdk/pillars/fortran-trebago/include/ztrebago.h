/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* ztrebago.h — Fortran -> Trebago, the second ZXV legacy-language pillar
 * (pillar API 0.1).
 *
 * Fortran's treaty is the array: 1-based, column-major, with extents the
 * caller knows. Trebago is a short line syntax for the exact numeric
 * kernels a Fortran program would otherwise write with REAL arrays (sums,
 * dot products, scaling, prefix sums, matrix-vector products) over exact
 * rationals instead:
 *
 *     trebago dot
 *       in  x(n), y(n)
 *       out s
 *       s = sum(i, x(i) * y(i))
 *     end
 *
 *   1. PARSE. tb_parse reads Trebago text (README.md, "Trebago") into an
 *      immutable tb_program. Its canonical form is Trebago text
 *      (tb_program_text) and tb_program_digest is the SHA3-256 of that
 *      text, so two spellings of the same kernel have the same digest.
 *
 *   2. EXECUTE. tb_exec runs a program on caller arrays of exact
 *      rationals, each value a numerator and a denominator (int64). Every
 *      operation goes through libzxv's zxv_rat_*: results are exact and
 *      reduced. A result that does not fit int64/int64 is refused with
 *      ZXV_E_RANGE, never rounded. Outputs are written only when the whole
 *      program succeeded.
 *
 *   3. FORTRAN. tb_array is interoperable (BIND(C)): the Fortran module
 *      `trebago` (fortran/trebago.f90) passes INTEGER(INT64) numerator and
 *      denominator arrays straight to tb_exec, in Fortran's own column-major
 *      order.
 *
 * Honest limits: not a replacement for floating-point numerical codes.
 * Every operation is a libzxv call with gcd reductions, far slower than
 * REAL arithmetic, and denominators grow until int64 refuses them. No
 * GPU, no SIMD, no threads inside one run. README.md, "Honest limits".
 *
 * PILLAR API STABILITY. 0.x: it may change before 1.0. It calls only the
 * libzxv ABI 1.3 (zxv_api.h). Opaque handles, int32_t status returns,
 * fixed-width integers, no floats (R11).
 */
#ifndef ZTREBAGO_H
#define ZTREBAGO_H

#include <stddef.h>
#include <stdint.h>

#include "zxv_api.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TB_VERSION_MAJOR 0
#define TB_VERSION_MINOR 1
#define TB_VERSION_PATCH 0

/* ===== Status codes =====
 * 0 and -1 .. -99 are libzxv's, passed through unchanged: ZXV_E_RANGE (an
 * exact result does not fit int64/int64), ZXV_E_ARG (a denominator of 0
 * in an input, a NULL pointer), ZXV_E_UTF8, ZXV_E_NOMEM. The
 * legacy-language pillars own -2001 and below; fortran-trebago uses
 * -2101 .. -2199. */
#define TB_OK         0
#define TB_E_SYNTAX   (-2101) /* the text does not parse                          */
#define TB_E_NAME     (-2102) /* unknown, duplicate or misused name; a binding   */
                              /* that names no parameter, or a missing one       */
#define TB_E_SHAPE    (-2103) /* rank or extent mismatch; a dimension that       */
                              /* nothing binds; an index whose range is unknown  */
#define TB_E_BOUNDS   (-2104) /* a subscript outside its array at run time       */
#define TB_E_DIVZERO  (-2105) /* division by an exact zero, or 0 ** -k           */
#define TB_E_LIMIT    (-2106) /* the work limit, or a size limit below, is hit   */

/* Static, never-NULL name ("TB_E_SHAPE"); other codes give libzxv's name. */
const char *tb_status_name(int32_t status);
/* The ZXV_CLASS_* of a status: the TB_E_* codes are USAGE, except
 * TB_E_LIMIT, which is REFUSED (a larger limit may succeed). */
int32_t tb_status_class(int32_t status);
/* The calling thread's last error detail (R4 two-call buffer). */
int32_t tb_last_error(char *buf, size_t cap, size_t *out_len);

/* ===== Limits ===== */
#define TB_MAX_TEXT     (1024u * 1024u) /* bytes of Trebago text                    */
#define TB_MAX_NAME     31u             /* bytes in a name                          */
#define TB_MAX_SYMBOLS  256u            /* parameters, variables and constants      */
#define TB_MAX_NODES    65536u          /* expression nodes in one program          */
#define TB_MAX_DEPTH    200u            /* expression tree depth                    */
#define TB_MAX_RANK     2u              /* scalars, vectors and matrices            */
#define TB_MAX_ELEMENTS (1u << 26)      /* elements in one array                    */
#define TB_DEFAULT_WORK (UINT64_C(1) << 32) /* rational operations in one run      */

/* ===== Programs ===== */
typedef struct tb_program tb_program;

/* Where a parse or a run was refused. line and col are 1-based (col
 * counts bytes), offset is 0-based. msg is one line, NUL-terminated,
 * truncated to fit; it starts "line L, column C: ". A run-time refusal
 * points at the operation that was refused and names the loop indices
 * ("at i=7"). A refusal that has no place in the text (a binding) has
 * line 0. */
typedef struct {
    uint32_t line, col;
    uint64_t offset;
    char msg[256];
} tb_diag;

/* Parse Trebago text. `text` need not be NUL-terminated; `len` bytes are
 * read (at most TB_MAX_TEXT, valid UTF-8). Returns ZXV_OK with
 * *out_program, or TB_E_SYNTAX / TB_E_NAME / TB_E_SHAPE / TB_E_LIMIT /
 * ZXV_E_RANGE (a constant that does not fit) / ZXV_E_UTF8 with *diag
 * filled, or ZXV_E_ARG / ZXV_E_NOMEM. `diag` may be NULL. */
int32_t tb_parse(const char *text, size_t len, tb_program **out_program, tb_diag *diag);
void tb_program_destroy(tb_program *program);

/* The canonical Trebago text (R4): lower-case names, one declaration per
 * line, constants as their exact value, minimal parentheses, two-space
 * indentation, no comments. tb_parse of it gives the same text again. */
int32_t tb_program_text(const tb_program *program, char *buf, size_t cap, size_t *out_len);
/* SHA3-256 of the canonical text: the program's identity. */
int32_t tb_program_digest(const tb_program *program, uint8_t out[32]);
/* The program's name (R4). */
int32_t tb_program_name(const tb_program *program, char *buf, size_t cap, size_t *out_len);

/* ===== Parameters: what a caller binds ===== */
#define TB_ROLE_IN    1 /* read only                                         */
#define TB_ROLE_OUT   2 /* written; starts at 0                              */
#define TB_ROLE_INOUT 3 /* read, then written                                */

typedef struct {
    char name[TB_MAX_NAME + 1];
    int32_t role;                      /* TB_ROLE_*                            */
    uint32_t rank;                     /* 0 scalar, 1 vector, 2 matrix         */
    char dim[TB_MAX_RANK][TB_MAX_NAME + 1]; /* extent names; "" when fixed    */
    int64_t fixed[TB_MAX_RANK];        /* a fixed extent, or 0                 */
} tb_param;

/* in, out and inout declarations, in source order. */
int32_t tb_param_count(const tb_program *program, uint32_t *out_count);
int32_t tb_param_at(const tb_program *program, uint32_t index, tb_param *out_param);

/* ===== Execution =====
 * One binding per parameter. An array of rank r holds extent[0] *
 * extent[1] * ... values in COLUMN-MAJOR order (Fortran's): a(i,j) is
 * element (i-1) + (j-1)*extent[0]. num[k]/den[k] is one exact value; den
 * may be negative (it is normalized) but not 0. A scalar is rank 0 with
 * one value. Unused extents are 0.
 *
 * The layout is BIND(C)-interoperable: Fortran TYPE(C_PTR) x 3,
 * INTEGER(C_INT32_T) x 2, INTEGER(C_INT64_T) extent(2). */
typedef struct {
    const char *name;  /* NUL-terminated, any letter case            */
    int64_t *num;      /* written for out and inout, read for in     */
    int64_t *den;
    uint32_t rank;
    uint32_t reserved; /* 0                                           */
    int64_t extent[TB_MAX_RANK];
} tb_array;

/* Run `program` on `count` bindings. Every in, out and inout parameter
 * must be bound exactly once, with its rank; extents bind the program's
 * dimension names and must agree wherever the same name appears.
 * `work_limit` caps the rational operations (0: TB_DEFAULT_WORK).
 *
 * Returns ZXV_OK, after writing every out and inout array; or a refusal
 * with *diag filled and NOTHING written: TB_E_NAME, TB_E_SHAPE (bindings),
 * ZXV_E_ARG (an input denominator of 0, or INT64_MIN), ZXV_E_RANGE (an
 * exact result does not fit int64/int64: never rounded), TB_E_BOUNDS,
 * TB_E_DIVZERO, TB_E_LIMIT, ZXV_E_NOMEM. `diag` may be NULL. The program
 * is not modified, so one program may run on many threads at once. */
int32_t tb_exec(const tb_program *program, const tb_array *arrays, uint32_t count,
                uint64_t work_limit, tb_diag *diag);

/* The rational operations the last successful or refused tb_exec on this
 * thread performed (for cost measurements). */
int32_t tb_last_work(uint64_t *out_work);

#ifdef __cplusplus
}
#endif

#endif /* ZTREBAGO_H */
