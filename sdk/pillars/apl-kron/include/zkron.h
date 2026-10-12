/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zkron.h — APL and K -> KRON, the fourth ZXV legacy-language pillar
 * (pillar API 0.1).
 *
 * The array languages' treaty is the expression: a line of APL or K read
 * right to left. KRON is that expression language evaluated exactly, on
 * vectors and matrices of int64/int64 rationals and of four-valued truth:
 *
 *     APL glyphs    +/ (⍳5) × 1r2         x[⍋x←3 1 2]       2 3⍴⍳6
 *     K (ASCII)     +/ (!5) * 1r2         x@<x:3 1 2        2 3#!6
 *     canonical     (reduce add (mul (iota 5) 1r2))  ...
 *
 *   1. PARSE. kr_parse reads APL glyph text (UTF-8), K-style ASCII text
 *      or canonical KRON into an immutable kr_program. Both spellings
 *      parse to one canonical form: S-expressions with one word per
 *      primitive (kr_program_text). kr_program_digest is the SHA3-256 of
 *      that text, so two spellings of the same program have the same
 *      digest, and equivalence is a digest comparison.
 *
 *   2. EVALUATE. kr_eval runs a program right to left, like APL and K,
 *      on values bound by the caller. Every arithmetic step is a libzxv
 *      zxv_rat_* call: results are exact and reduced. A result that does
 *      not fit int64/int64 is refused with ZXV_E_RANGE, never rounded;
 *      a division by an exact zero is KR_E_DIVZERO. Comparisons give
 *      four-valued truth (ZXV_LP_*), which is never collapsed to a
 *      boolean: arithmetic on truth is KR_E_DOMAIN.
 *
 *   3. POST. kr_post turns an integer result vector into libzxv ledger
 *      postings through zxv_ledger_post_cap, with a capability the caller
 *      passes. Whether a posting is allowed is the ledger's decision.
 *
 * Honest limits: no floats, no big integers, no nested arrays, rank <= 2,
 * no user-defined functions. Each element operation is a libzxv call
 * with gcd reductions: far slower than an APL or K interpreter on
 * doubles. README.md, "Honest limits".
 *
 * PILLAR API STABILITY. 0.x: it may change before 1.0. It calls only the
 * libzxv ABI 1.3 (zxv_api.h). Opaque handles, int32_t status returns,
 * fixed-width integers, no floats (R11).
 */
#ifndef ZKRON_H
#define ZKRON_H

#include <stddef.h>
#include <stdint.h>

#include "zxv_api.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KR_VERSION_MAJOR 0
#define KR_VERSION_MINOR 1
#define KR_VERSION_PATCH 0

/* ===== Status codes =====
 * 0 and -1 .. -99 are libzxv's, passed through unchanged: ZXV_E_RANGE (an
 * exact result or a literal does not fit int64/int64), ZXV_E_ARG (a NULL
 * pointer, a denominator of 0 in a binding), ZXV_E_UTF8, ZXV_E_BUFFER,
 * ZXV_E_NOMEM, and the ledger's refusals from kr_post. The
 * legacy-language pillars own -2001 and below; apl-kron uses
 * -2301 .. -2399. */
#define KR_OK         0
#define KR_E_SYNTAX   (-2301) /* the text does not parse, or uses a primitive  */
                              /* KRON does not have (monadic * in APL, ...)    */
#define KR_E_NAME     (-2302) /* an unbound or reserved name; a bad binding    */
#define KR_E_RANK     (-2303) /* an argument of the wrong rank, or a result    */
                              /* above rank 2                                  */
#define KR_E_LENGTH   (-2304) /* extents that do not agree                     */
#define KR_E_DOMAIN   (-2305) /* a value outside the function's domain: truth  */
                              /* in arithmetic, a fraction where an integer    */
                              /* is needed, a negative count, an empty max/min */
#define KR_E_INDEX    (-2306) /* an index outside its array                    */
#define KR_E_DIVZERO  (-2307) /* division by an exact zero (÷, %, recip,     */
                              /* 0 * negative)                                 */
#define KR_E_LIMIT    (-2308) /* the work limit, or a size limit below         */

/* Static, never-NULL name ("KR_E_DOMAIN"); other codes give libzxv's name. */
const char *kr_status_name(int32_t status);
/* The ZXV_CLASS_* of a status: KR_E_* codes are USAGE, except KR_E_LIMIT,
 * which is REFUSED (a larger limit may succeed). */
int32_t kr_status_class(int32_t status);
/* The calling thread's last error detail (R4 two-call buffer). */
int32_t kr_last_error(char *buf, size_t cap, size_t *out_len);

/* ===== Limits ===== */
#define KR_MAX_TEXT     (1024u * 1024u)  /* bytes of program text                */
#define KR_MAX_NAME     31u              /* bytes in a name                      */
#define KR_MAX_NODES    65536u           /* syntax nodes in one program          */
#define KR_MAX_LITERALS (1u << 20)       /* literal elements in one program      */
#define KR_MAX_DEPTH    512u             /* syntax tree depth (functions in one  */
                                         /* right-to-left chain, parentheses)    */
#define KR_MAX_RANK     2u               /* scalars, vectors and matrices        */
#define KR_MAX_ELEMENTS (1u << 22)       /* elements in one array                */
#define KR_DEFAULT_WORK (UINT64_C(1) << 32) /* operations in one evaluation      */

/* ===== Spellings ===== */
#define KR_SPELL_AUTO  0 /* CANON if the text starts with the word "kron";    */
                         /* else APL if it has any non-ASCII byte; else K     */
#define KR_SPELL_APL   1 /* APL glyphs (UTF-8), ⎕IO 0                         */
#define KR_SPELL_K     2 /* K-style ASCII                                      */
#define KR_SPELL_CANON 3 /* canonical KRON (kr_program_text)                   */

/* ===== Programs ===== */
typedef struct kr_program kr_program;

/* Where a parse or an evaluation was refused. line and col are 1-based
 * (col counts bytes), offset is 0-based. msg is one line,
 * NUL-terminated, truncated to fit; it starts "line L, column C: ". A
 * refusal with no place in the text (a binding) has line 0. */
typedef struct {
    uint32_t line, col;
    uint64_t offset;
    char msg[256];
} kr_diag;

/* Parse KRON text in `spelling`. `text` need not be NUL-terminated;
 * `len` bytes are read (at most KR_MAX_TEXT, valid UTF-8). Returns ZXV_OK
 * with *out_program, or KR_E_SYNTAX / KR_E_NAME / KR_E_LIMIT /
 * ZXV_E_RANGE (a literal that does not fit) / ZXV_E_UTF8 with *diag
 * filled, or ZXV_E_ARG / ZXV_E_NOMEM. `diag` may be NULL. */
int32_t kr_parse(const char *text, size_t len, int32_t spelling, kr_program **out_program,
                 kr_diag *diag);
void kr_program_destroy(kr_program *program);

/* The canonical text (R4): "kron", then one statement per line as an
 * S-expression with one word per primitive and exact literals ("1r3").
 * kr_parse of it gives the same text again. */
int32_t kr_program_text(const kr_program *program, char *buf, size_t cap, size_t *out_len);
/* SHA3-256 of the canonical text: the program's identity. */
int32_t kr_program_digest(const kr_program *program, uint8_t out[32]);
/* The spelling the program was parsed from (KR_SPELL_APL, _K or _CANON). */
int32_t kr_program_spelling(const kr_program *program, int32_t *out_spelling);
/* The free names (read before any assignment), in order of first use. */
int32_t kr_free_count(const kr_program *program, uint32_t *out_count);
int32_t kr_free_at(const kr_program *program, uint32_t index, char *buf, size_t cap,
                   size_t *out_len);

/* ===== Values ===== */
#define KR_KIND_NUM   1 /* exact rationals                               */
#define KR_KIND_TRUTH 2 /* four-valued truth, ZXV_LP_* codes             */

typedef struct kr_value kr_value;

/* Kind, rank (0, 1, 2), extents (unused ones 0) and element count. Any out
 * pointer may be NULL. */
int32_t kr_value_info(const kr_value *v, int32_t *out_kind, uint32_t *out_rank,
                      int64_t out_shape[2], uint64_t *out_count);
/* Copy the elements in row-major (ravel) order. `cap` is the room in the
 * arrays, in elements; ZXV_E_BUFFER when it is less than the count, and
 * KR_E_DOMAIN for the wrong kind. */
int32_t kr_value_rats(const kr_value *v, int64_t *num, int64_t *den, uint64_t cap);
int32_t kr_value_truths(const kr_value *v, int32_t *truth, uint64_t cap);
/* The value as canonical KRON (R4): "5", "-1r3", "(vec 1 2 3)",
 * "(mat 2 2 1 0 0 1)", "(tvec true both)". Evaluating that text gives the
 * value again. */
int32_t kr_value_text(const kr_value *v, char *buf, size_t cap, size_t *out_len);
/* The value in the APL/K display style: numbers separated by spaces,
 * negatives with '-', fractions as "1r3", matrix rows on their own lines. */
int32_t kr_value_display(const kr_value *v, char *buf, size_t cap, size_t *out_len);
void kr_value_destroy(kr_value *v);

/* Make a value from caller arrays (row-major). kind KR_KIND_NUM uses
 * num/den (den != 0; normalized), KR_KIND_TRUTH uses truth (ZXV_LP_*).
 * rank 0 has one element. */
int32_t kr_value_make(int32_t kind, uint32_t rank, const int64_t shape[2], const int64_t *num,
                      const int64_t *den, const int32_t *truth, kr_value **out_value);

/* ===== Evaluation ===== */
typedef struct {
    const char *name;      /* NUL-terminated; a free name of the program */
    const kr_value *value; /* not modified, not retained after the call  */
} kr_binding;

/* Evaluate `program` with `count` bindings for its free names (unused
 * bindings are KR_E_NAME, so a typo is not silently ignored).
 * `work_limit` caps the operations (0: KR_DEFAULT_WORK); an operation is
 * one rational or truth step or one element produced.
 *
 * Returns ZXV_OK with *out_value, the value of the last statement; or a
 * refusal with *diag filled and *out_value NULL: KR_E_* above, ZXV_E_RANGE
 * (an exact result does not fit int64/int64: never rounded),
 * ZXV_E_NOMEM. `diag` may be NULL. The program is not modified, so one
 * program may be evaluated on many threads at once. */
int32_t kr_eval(const kr_program *program, const kr_binding *bindings, uint32_t count,
                uint64_t work_limit, kr_value **out_value, kr_diag *diag);

/* The operations the last kr_eval on this thread performed. */
int32_t kr_last_work(uint64_t *out_work);

/* One call for language bindings: parse `text` (spelling as kr_parse),
 * evaluate with no bindings and the default work limit, and write the
 * result as `what` into buf (R4 two-call buffer):
 *   KR_TEXT_VALUE    canonical value text      KR_TEXT_DISPLAY  display text
 *   KR_TEXT_PROGRAM  canonical program text    KR_TEXT_DIGEST   64 hex digits
 *   KR_TEXT_ND       "(shape;num;den)" for K: integers only, e.g.
 *                    "(2 2;1 2 3 4;1 1 1 3)"; truth as ZXV_LP_* codes in
 *                    num with den 0
 * KR_TEXT_PROGRAM and KR_TEXT_DIGEST do not evaluate. On a refusal buf
 * gets the diagnostic and the refusal's status is returned (or
 * ZXV_E_BUFFER, with *out_len, when the diagnostic does not fit). */
#define KR_TEXT_VALUE   0
#define KR_TEXT_DISPLAY 1
#define KR_TEXT_PROGRAM 2
#define KR_TEXT_DIGEST  3
#define KR_TEXT_ND      4
int32_t kr_run_text(const char *text, size_t len, int32_t spelling, int32_t what, char *buf,
                    size_t cap, size_t *out_len);

/* ===== Postings =====
 * Post every element of `amounts` (an integer vector, or a scalar, in
 * minor units: 1234 = 12.34 USD) from account `from` to account `to` in
 * currency `ccy`, with references "<ref_prefix>-<i>" (i from 0; at most
 * 35 bytes). Every element is checked first (a whole number, > 0); then
 * each posting goes through zxv_ledger_post_cap(cap, ...), which checks
 * the cap (LEDGER_POST) on every call. The ledger has no batch
 * transaction: a refusal at element i is returned as is (ZXV_E_CAP,
 * ZXV_E_FUNDS, ZXV_E_DUPLICATE, ZXV_E_POISONED, ...) with *out_posted = i
 * postings made. `entry_ids` (may be NULL) gets up to `ids_cap` ids. */
int32_t kr_post(const zxv_cap *cap, const kr_value *amounts, uint32_t from, uint32_t to,
                const char *ccy, const char *ref_prefix, uint64_t *entry_ids, uint64_t ids_cap,
                uint64_t *out_posted);

#ifdef __cplusplus
}
#endif

#endif /* ZKRON_H */
