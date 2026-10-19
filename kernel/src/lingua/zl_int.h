/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zl_int.h — pieces the Lingua front-ends share: the code emitter, the
 * string table, the text writer for twins and a few byte helpers that stand
 * in for libc (the module links against nothing). */
#ifndef ZL_INT_H
#define ZL_INT_H

#include "zl.h"

/* Zero or copy bytes without letting the compiler turn the loop into a
 * memset/memcpy call (the module must stay free of external symbols). */
void zl_bzero(void *p, size_t n);
void zl_bcopy(void *dst, const void *src, size_t n);

static inline int zl_upper(int c)
{
    return (c >= 'a' && c <= 'z') ? c - 32 : c;
}
static inline int zl_isdigit(int c)
{
    return c >= '0' && c <= '9';
}
static inline int zl_isalpha(int c)
{
    c = zl_upper(c);
    return c >= 'A' && c <= 'Z';
}
static inline int zl_isalnum(int c)
{
    return zl_isalpha(c) || zl_isdigit(c);
}
static inline int zl_isspace(int c)
{
    return c == ' ' || c == '\t' || c == '\r';
}

/* Compiler context: where code goes and where the first error is kept. */
typedef struct {
    zl_prog_t *p;
    zl_diag_t *d;
    uint32_t line, col;
    int failed;
} zl_cc_t;

void zl_cc_init(zl_cc_t *c, zl_prog_t *p, zl_diag_t *d, int lang, uint32_t bits);
/* Record the first failure; returns status. */
int zl_fail(zl_cc_t *c, int status, const char *msg);
/* Append an instruction; returns its index, or -1 after recording LIMIT. */
int32_t zl_emit(zl_cc_t *c, int op, int64_t a, int32_t b);
/* Point an emitted jump at a target. */
void zl_patch(zl_cc_t *c, int32_t at, int64_t target);
/* Intern a string literal; returns its index or -1. */
int32_t zl_addstr(zl_cc_t *c, const char *s, size_t n);
/* Reserve n memory slots; returns the first or -1. */
int32_t zl_slots(zl_cc_t *c, uint32_t n);
/* Is v representable in the program's integer width? */
int zl_fits(const zl_cc_t *c, int64_t v);

/* Text writer for twins. Overflow is sticky and reported as ZL_E_LIMIT. */
typedef struct {
    char *p; /* destination, or NULL in compare mode */
    size_t cap, len;
    int over;
    const char *cmp; /* compare mode: the text this output must equal */
    size_t cmp_len;
    int diff;       /* compare mode: a byte differed or ran past cmp */
    size_t diff_at; /* offset of the first difference */
} zl_out_t;

void zl_put(zl_out_t *o, const char *s, size_t n);
void zl_puts(zl_out_t *o, const char *s);
void zl_putc(zl_out_t *o, char ch);
void zl_puti(zl_out_t *o, int64_t v);

/* Decimal digits of v into buf (at least 21 bytes); returns the length. */
size_t zl_itoa(int64_t v, char *buf);

/* Case-insensitive compare of n bytes of a against the upper-case word w. */
int zl_wordeq(const char *a, size_t n, const char *w);

/* Front-ends (one per language). Each compiles into p, and, when tw is not
 * NULL, also writes the canonical twin text into it. */
int zl_fortran_compile(const char *src, size_t len, zl_cc_t *c, zl_out_t *tw, void *work);
int zl_basic_compile(const char *src, size_t len, zl_cc_t *c, zl_out_t *tw, void *work);
int zl_forth_compile(const char *src, size_t len, zl_cc_t *c, zl_out_t *tw, void *work);

#define ZL_STATIC_ASSERT(cond, name) typedef char zl_static_##name[(cond) ? 1 : -1]

#endif /* ZL_INT_H */
