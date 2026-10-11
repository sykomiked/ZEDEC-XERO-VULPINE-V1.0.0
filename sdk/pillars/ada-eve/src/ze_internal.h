/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* ze_internal.h — libzeve internals (not part of the pillar API; the
 * fuzzer includes it). Integer code only (R11). */
#ifndef ZE_INTERNAL_H
#define ZE_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include "zeve.h"

/* Static expressions are evaluated in 128 bits so that Ada's usual
 * "range -2**63 .. 2**63 - 1" works; results must still fit int64. */
__extension__ typedef __int128 ze_i128;

/* ---- packages (ze_pkg.c) ---- */
typedef struct {
    ze_type t;              /* the public view                                   */
    uint32_t lit0;          /* ENUM: index of the base's first literal          */
    uint32_t comp0;         /* RECORD: index of the first component             */
    ze_dim dim[ZE_MAX_DIMS];/* ARRAY                                             */
    uint32_t width;         /* scalars: 1, 2, 4 or 8 bytes                       */
    int32_t is_signed;      /* scalars: two's complement (else unsigned)         */
    int32_t has_range;      /* a range constraint was written (and differs from  */
                            /* the base's)                                       */
    uint64_t at;            /* text offset of the declaration, for messages      */
} ze_titem;

struct ze_pkg {
    char name[ZE_MAX_NAME + 1];
    uint32_t ntypes, cap_types;
    ze_titem *types;
    uint32_t ncomps, cap_comps;
    ze_component *comps;
    uint32_t nlits, cap_lits;
    char (*lits)[ZE_MAX_NAME + 1];
};

/* A fresh package holding only the predefined Boolean (type 0). */
int32_t ze_pkg_new(ze_pkg **out);
/* Append a type (its kind-specific fields already set, sizes not yet);
 * computes width, signedness and size. *out gets its index. Returns
 * ZXV_OK, ZE_E_LIMIT or ZXV_E_NOMEM (message set, caller adds position). */
int32_t ze_pkg_add_type(ze_pkg *p, ze_titem *it, uint32_t *out);
int32_t ze_pkg_add_literal(ze_pkg *p, const char *name, uint32_t *out);
int32_t ze_pkg_add_component(ze_pkg *p, const char *name, uint32_t type, uint32_t *out);
/* Lay out a record whose components are comps[comp0 .. comp0+count). */
int32_t ze_pkg_finish_record(ze_pkg *p, uint32_t type);
/* A named type, case-insensitive name already lower-cased; UINT32_MAX if none. */
uint32_t ze_pkg_lookup(const ze_pkg *p, const char *lower);
/* 1 if `lower` is a literal of any enumeration (not Boolean's). */
int ze_pkg_is_literal(const ze_pkg *p, const char *lower);
/* The narrowest canonical encoding of [lo, hi]. */
void ze_scalar_width(int64_t lo, int64_t hi, uint32_t *width, int32_t *is_signed);

/* ---- names (ze_util.c) ---- */
/* 1 if s[0..n) is an Ada reserved word (case-insensitive). */
int ze_reserved(const char *s, size_t n);
/* 1 if s[0..n) is a predefined name a package may not declare
 * (boolean, integer, string, float, ...). */
int ze_predefined(const char *s, size_t n);
/* Lower-case copy into dst[ZE_MAX_NAME + 1]; n <= ZE_MAX_NAME. */
void ze_lower(char *dst, const char *src, size_t n);
int ze_ieq(const char *a, const char *b);
/* Ada's "Account_Id" spelling of a lower-case name. */
void ze_ada_case(char *dst, const char *lower);

/* ---- errors (ze_util.c) ---- */
void ze_set_error(const char *msg);
void ze_set_errorf(const char *fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 1, 2)))
#endif
    ;
int32_t ze_diag_set(ze_diag *d, int32_t status, const char *text, size_t len, uint64_t offset,
                    const char *fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 6, 7)))
#endif
    ;
size_t ze_utf8_check(const uint8_t *s, size_t n);
int32_t ze_out_text(const char *src, size_t n, char *buf, size_t cap, size_t *out_len);

/* A growable text buffer for the printers. Appending aborts nothing: on
 * allocation failure `failed` is set and later appends are ignored. */
typedef struct {
    char *p;
    size_t n, cap;
    int failed;
} ze_sb;
void ze_sb_put(ze_sb *s, const char *t, size_t n);
void ze_sb_str(ze_sb *s, const char *t);
void ze_sb_i64(ze_sb *s, int64_t v);
/* units / 10^scale as exact decimal text with all `scale` decimals. */
void ze_sb_dec(ze_sb *s, int64_t units, uint32_t scale);

/* ---- tokens (ze_lex.c) ---- */
enum {
    ZT_EOF = 0,
    ZT_NL,   /* Eve only: end of line                             */
    ZT_ID,   /* identifier                                        */
    ZT_INT,  /* integer literal: v (ov set if beyond 2^100)       */
    ZT_REAL, /* real literal: v * 10^exp (ov set if too long)     */
    ZT_STR,  /* "string literal"                                  */
    ZT_CHAR, /* 'c' character literal                             */
    ZT_SYM,  /* punctuation: sym                                  */
    ZT_ERR   /* a lexical error: status and message in the lexer  */
};
/* two-character symbols */
#define ZS_DOTDOT  256 /* ..  */
#define ZS_POW     257 /* **  */
#define ZS_ASSIGN  258 /* :=  */
#define ZS_ARROW   259 /* =>  */
#define ZS_BOX     260 /* <>  */
#define ZS_NE      261 /* /=  */
#define ZS_LE      262 /* <=  */
#define ZS_GE      263 /* >=  */

typedef struct {
    int kind;
    int sym;     /* ZT_SYM: the character or ZS_*                */
    int ov;      /* literal too large                            */
    int based;   /* ZT_INT written with a base (16#FF#)          */
    int32_t exp; /* ZT_REAL: decimal exponent                    */
    ze_i128 v;   /* ZT_INT value; ZT_REAL mantissa               */
    size_t off, len;
} ze_tok;

#define ZE_LEX_ADA 0
#define ZE_LEX_EVE 1

typedef struct {
    const char *text;
    size_t len;
    ze_diag *d;
    int mode;
    ze_tok *t;
    size_t n, cap, i; /* tokens, and the parser's position          */
    int32_t err_status;
    char err_msg[200];
} ze_lexer;

/* Tokenize the whole text. A lexical error becomes a final ZT_ERR token,
 * reported when the parser reaches it. ZXV_E_NOMEM on allocation failure. */
int32_t ze_lex(ze_lexer *lx, const char *text, size_t len, int mode, ze_diag *d);
void ze_lex_free(ze_lexer *lx);
/* Report the ZT_ERR token (or, for any other token, "expected ..."). */
int32_t ze_lex_error_at(ze_lexer *lx, const ze_tok *t, const char *expected);

/* Static integer expressions, Ada's precedence: unary +/- below * / mod
 * rem, ** binding tightest; only integer literals and parentheses.
 * Evaluated in 128 bits; ZE_E_CONSTRAINT on overflow beyond 2^120,
 * division by zero or a negative exponent. Leaves lx->i after it. */
int32_t ze_expr(ze_lexer *lx, ze_i128 *out);
/* A decimal bound: [+|-] integer-or-real literal, exactly as units of
 * 10^-scale. ZE_E_CONSTRAINT if it is not a multiple. */
int32_t ze_dec_bound(ze_lexer *lx, uint32_t scale, int64_t *out_units);
/* The exact value of a real literal token as num * 10^-*out_scale with
 * the smallest scale (0..ZE_MAX_DIGITS); ZE_E_CONSTRAINT if it needs more. */
int32_t ze_real_exact(ze_lexer *lx, const ze_tok *t, int64_t *out_num, uint32_t *out_scale);

#define ZE_ERR(lx, st, off, ...) ze_diag_set((lx)->d, (st), (lx)->text, (lx)->len, (off), __VA_ARGS__)

/* ---- shared declaration semantics (ze_decl.c) ---- */
/* What a front end hands over for one declaration. Both parsers build
 * the same ze_titem through these, so their checks are identical. */
int32_t ze_check_new_name(ze_lexer *lx, const ze_pkg *p, const ze_tok *name, char *lower);
int32_t ze_decl_int(ze_lexer *lx, ze_pkg *p, const char *name, int64_t lo, int64_t hi, uint64_t at,
                    uint32_t *out);
int32_t ze_decl_dec(ze_lexer *lx, ze_pkg *p, const char *name, uint32_t digits, uint32_t scale,
                    int has_range, int64_t lo, int64_t hi, uint64_t at, uint32_t *out);
int32_t ze_decl_text(ze_lexer *lx, ze_pkg *p, const char *name, int64_t n, uint64_t at,
                     uint32_t *out);
/* A subtype of `base`, with or without a range (lo/hi in the base's units
 * or positions). An unconstrained subtype of a composite is allowed. */
int32_t ze_decl_subtype(ze_lexer *lx, ze_pkg *p, const char *name, uint32_t base, int has_range,
                        int64_t lo, int64_t hi, uint64_t at, uint32_t *out);
/* An array of `elem` over dims[0..ndims). */
int32_t ze_decl_array(ze_lexer *lx, ze_pkg *p, const char *name, const ze_dim *dims,
                      uint32_t ndims, uint32_t elem, uint64_t at, uint32_t *out);
/* Enumerations: note lit0 = p->nlits, add each literal, then the type. */
int32_t ze_decl_literal(ze_lexer *lx, ze_pkg *p, uint32_t lit0, const ze_tok *name);
int32_t ze_decl_enum(ze_lexer *lx, ze_pkg *p, const char *name, uint32_t lit0, uint64_t at,
                     uint32_t *out);
/* Records: note comp0 = p->ncomps, add each component, then the type
 * (after its components, so a record cannot contain itself). */
int32_t ze_decl_component(ze_lexer *lx, ze_pkg *p, uint32_t comp0, const ze_tok *name,
                          uint32_t type);
int32_t ze_decl_record(ze_lexer *lx, ze_pkg *p, const char *name, uint32_t comp0, uint64_t at,
                       uint32_t *out);
/* A range bound naming a literal of an enumeration type `base`: its position. */
int32_t ze_enum_pos(const ze_pkg *p, uint32_t base, const char *lower, uint32_t *out_pos);

/* ---- values (ze_codec.c) ---- */
extern const int64_t ze_pow10[19];
int32_t ze_scalar_read(const ze_titem *it, const uint8_t *p, int64_t *out);
void ze_scalar_write(const ze_titem *it, uint8_t *p, int64_t v);

#endif /* ZE_INTERNAL_H */
