/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* tb_internal.h — the program representation shared by the parser, the
 * printer and the executor. Integer code only (R11). */
#ifndef TB_INTERNAL_H
#define TB_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include "ztrebago.h"

/* Symbol roles: the three bindable ones are TB_ROLE_*. */
#define TB_ROLE_VAR   4
#define TB_ROLE_CONST 5

typedef struct {
    char name[TB_MAX_NAME + 1];
    int32_t role;
    uint32_t rank;
    int32_t dim[TB_MAX_RANK];   /* dimension id, or -1 for a fixed extent */
    int64_t fixed[TB_MAX_RANK]; /* the fixed extent                       */
    int64_t cnum, cden;         /* a constant's value                     */
    uint32_t line, col;
} tb_sym;

typedef struct {
    char name[TB_MAX_NAME + 1];
} tb_dim;

/* Node kinds. */
enum {
    N_NUM = 1, /* literal: num/den; is_int when written as an integer        */
    N_IDX,     /* loop index: a = slot                                       */
    N_DIM,     /* dimension extent: a = dim                                  */
    N_SYM,     /* scalar or constant: a = symbol                             */
    N_ELEM,    /* array element: a = symbol, b, c = subscripts (c -1: rank 1) */
    N_NEG,     /* -a                                                          */
    N_ADD, N_SUB, N_MUL, N_DIV,
    N_POW,     /* a ** b, b an integer expression                            */
    N_SUM,     /* sum over slot a of b; c, d = lo, hi or -1 (inferred range)  */
    N_SCAN,    /* running sum of a over the statement's vector index         */
    N_ABS, N_MIN, N_MAX
};

typedef struct {
    uint8_t kind;
    uint8_t is_int; /* an integer expression: usable as a subscript */
    uint16_t depth;
    int32_t a, b, c, d;
    int64_t num, den;
    uint32_t line, col;
} tb_node;

/* A loop index: a statement's target index or a sum's index. */
typedef struct {
    char name[TB_MAX_NAME + 1];
    int32_t range_sym;  /* the full range is this array's axis ...         */
    int32_t range_axis; /* ... or -1 for an explicit lo:hi range            */
} tb_slot;

/* An index used alone as a subscript of an array over its full range:
 * the array's extent on that axis must equal the index's range. */
typedef struct {
    int32_t slot, sym, axis;
    uint32_t line, col;
} tb_use;

typedef struct {
    int32_t sym;               /* the target                               */
    int32_t idx[TB_MAX_RANK];  /* its index slots (rank many)              */
    int32_t rhs;               /* root node                                */
    uint32_t line, col;
} tb_stmt;

struct tb_program {
    char name[TB_MAX_NAME + 1];
    tb_sym *syms;
    uint32_t nsyms;
    tb_dim *dims;
    uint32_t ndims;
    tb_node *nodes;
    uint32_t nnodes, cap_nodes;
    tb_slot *slots;
    uint32_t nslots, cap_slots;
    tb_use *uses;
    uint32_t nuses, cap_uses;
    tb_stmt *stmts;
    uint32_t nstmts, cap_stmts;
};

/* tb_util.c */
void tb_set_error(const char *msg);
void tb_set_errorf(const char *fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 1, 2)))
#endif
    ;
int32_t tb_diag_at(tb_diag *d, int32_t status, uint32_t line, uint32_t col, uint64_t offset,
                   const char *fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 6, 7)))
#endif
    ;
int32_t tb_out_text(const char *s, size_t n, char *buf, size_t cap, size_t *out_len);
size_t tb_utf8_check(const uint8_t *s, size_t n);
void tb_set_work(uint64_t work);

/* tb_exec.c: evaluate a constant expression (no arrays, no indices). */
int32_t tb_fold(const tb_program *p, int32_t node, int64_t *out_num, int64_t *out_den,
                tb_diag *diag);

/* tb_print.c */
int32_t tb_print_program(const tb_program *p, char **out_text, size_t *out_len);
/* Append node `n` to a growing buffer; returns 0 or ZXV_E_NOMEM. */
typedef struct {
    char *b;
    size_t n, cap;
    int oom;
} tb_buf;
void tb_buf_puts(tb_buf *b, const char *s);
void tb_print_node(const tb_program *p, int32_t n, tb_buf *b);

#endif /* TB_INTERNAL_H */
