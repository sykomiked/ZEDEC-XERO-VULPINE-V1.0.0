/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* kr_internal.h — the program and value representations shared by the
 * parser, the printer and the evaluator. Integer code only (R11). */
#ifndef KR_INTERNAL_H
#define KR_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include "zkron.h"

/* ===== Primitive functions: one canonical word each ===== */
enum {
    F_NONE = 0,
    /* monadic */
    F_ID, F_NEG, F_SGN, F_RECIP, F_CEIL, F_FLOOR, F_ABS, F_NOT, F_IOTA, F_SHAPE, F_TALLY,
    F_REV, F_REVF, F_FLIP, F_RAVEL, F_GUP, F_GDOWN, F_SORT,
    /* dyadic: the scalar ones first (F_ADD .. F_OR) */
    F_ADD, F_SUB, F_MUL, F_DIV, F_MAX, F_MIN, F_RES, F_POW, F_LT, F_LE, F_EQ, F_GE, F_GT,
    F_NE, F_AND, F_OR,
    F_MATCH, F_RESHAPE, F_CAT, F_CATF, F_TAKE, F_DROP, F_ROTATE, F_FIND, F_INDEX,
    F_COUNT_
};
#define F_FIRST_MONADIC F_ID
#define F_LAST_MONADIC  F_SORT
#define F_FIRST_SCALAR2 F_ADD
#define F_LAST_SCALAR2  F_OR
#define F_IS_MONADIC(f) ((f) >= F_FIRST_MONADIC && (f) <= F_LAST_MONADIC)
#define F_IS_DYADIC(f)  ((f) > F_LAST_MONADIC && (f) < F_COUNT_)
#define F_IS_SCALAR2(f) ((f) >= F_FIRST_SCALAR2 && (f) <= F_LAST_SCALAR2)
/* add, mul, max, min, and, or: the order of a fold does not change an
 * exact result that fits */
#define F_IS_ASSOC(f) ((f) == F_ADD || (f) == F_MUL || (f) == F_MAX || (f) == F_MIN || \
                       (f) == F_AND || (f) == F_OR)

extern const char *const kr_fn_word[F_COUNT_];

/* ===== Syntax nodes ===== */
enum {
    N_NUM = 1, /* scalar number: lit index                          */
    N_VEC,     /* number vector: lit, cnt                           */
    N_MAT,     /* number matrix: lit, cnt, rows = a, cols = b      */
    N_TRUTH,   /* scalar truth: lit                                 */
    N_TVEC,    /* truth vector                                      */
    N_TMAT,    /* truth matrix: rows = a, cols = b                  */
    N_NAME,    /* sym                                               */
    N_LET,     /* sym := a                                          */
    N_MON,     /* fn a                                              */
    N_DYA,     /* a fn b (b evaluated first)                        */
    N_RED,     /* variant fn a                                      */
    N_SCAN,    /* variant fn a                                      */
    N_OUTER,   /* a (outer fn) b                                    */
    N_INNER,   /* a (inner fn fn2) b                                */
    N_INDEX2   /* a[b;c], b or c -1 when elided                     */
};

/* Reduction and scan variants (and their canonical words). */
#define V_LAST  0 /* reduce / scan:   APL f/ f\, last axis, right fold     */
#define V_FIRST 1 /* reducef / scanf: APL f⌿ f⍀, first axis, right fold    */
#define V_LEFT  2 /* foldl / scanl:   K f/ f\, first axis, left fold       */

typedef struct {
    uint8_t kind;
    uint8_t fn, fn2;
    uint8_t variant;
    int8_t rank;      /* static rank after analysis, -1 unknown */
    uint16_t depth;   /* 1 + the deepest child                  */
    int32_t a, b, c;  /* children, or -1 */
    int32_t sym;      /* name */
    uint32_t lit, cnt;
    uint32_t rows, cols; /* N_MAT, N_TMAT */
    uint32_t line, col;
    uint64_t offset;
} kr_node;

typedef struct {
    char name[KR_MAX_NAME + 1];
} kr_sym;

struct kr_program {
    int32_t spelling;
    kr_node *nodes;
    uint32_t nnodes, cap_nodes;
    int32_t *stmts;
    uint32_t nstmts, cap_stmts;
    kr_sym *syms;
    uint32_t nsyms, cap_syms;
    int64_t *lnum, *lden; /* literal pool; truth literals in lnum */
    uint32_t nlit, cap_lit;
    int32_t *free_syms;   /* free names, in order of first use */
    uint32_t nfree;
    char *text;           /* canonical text */
    size_t text_len;
    uint8_t digest[32];
};

/* ===== Values ===== */
struct kr_value {
    int32_t refs;
    uint8_t kind;  /* KR_KIND_* */
    uint8_t rank;
    int64_t shape[2];
    uint64_t n;
    int64_t *num, *den; /* KR_KIND_NUM  */
    int8_t *t;          /* KR_KIND_TRUTH */
};

kr_value *kr_val_new(int kind, uint32_t rank, int64_t s0, int64_t s1);
kr_value *kr_val_ref(kr_value *v);
void kr_val_unref(kr_value *v);

/* ===== Text buffers ===== */
typedef struct {
    char *b;
    size_t n, cap;
    int oom;
} kr_buf;
void kr_buf_put(kr_buf *b, const char *s, size_t n);
void kr_buf_puts(kr_buf *b, const char *s);
void kr_buf_putc(kr_buf *b, char c);
void kr_buf_i64(kr_buf *b, int64_t v);
void kr_buf_u64(kr_buf *b, uint64_t v);
/* "-1r3", "5" */
void kr_buf_rat(kr_buf *b, int64_t num, int64_t den);

/* ===== kr_util.c ===== */
void kr_set_error(const char *msg);
void kr_set_errorf(const char *fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 1, 2)))
#endif
    ;
int32_t kr_diag_at(kr_diag *d, int32_t status, uint32_t line, uint32_t col, uint64_t offset,
                   const char *fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 6, 7)))
#endif
    ;
int32_t kr_out_text(const char *s, size_t n, char *buf, size_t cap, size_t *out_len);
size_t kr_utf8_check(const uint8_t *s, size_t n);
void kr_set_work(uint64_t work);
/* Exact text of a rational for messages, in a rotating static buffer. */
const char *kr_rat_str(int64_t num, int64_t den);

/* ===== kr_print.c ===== */
int32_t kr_print_program(const kr_program *p, char **out_text, size_t *out_len);
void kr_print_value(const kr_value *v, kr_buf *b);

#endif /* KR_INTERNAL_H */
