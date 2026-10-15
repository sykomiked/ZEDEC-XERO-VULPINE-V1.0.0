/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* bv_syntax.h — Babel values in a language's own print syntax
 * (docs/BABEL.md section 2): read what a program printed, and spell a value
 * as a literal that program can take.
 *
 *   NUMBERS  FORTRAN, BASIC, Pascal, ALGOL 60, Forth: integers separated by
 *            blanks (a "-" sign; nothing else).
 *   LISP     as Common Lisp prints: (a b), (a . b), #(a b) read as a list,
 *            "text" with \" and \\, integers, ratios n/d, symbols kept in
 *            the case printed; T is TRUE and NIL the empty list (LISP's
 *            false is the empty list, so FALSE is written NIL).
 *   SCHEME   as Scheme writes: the same, with #t #f #true #false, () the
 *            empty list, and \n \t in strings.
 *   LOGO     as UCBLogo shows: [a b] and {a b} lists, words, integers, true
 *            and false. PRINT drops the outermost brackets, so each line of
 *            printed output is one value: a line of one item is that item,
 *            a line of several is a list of them (print [5] reads as 5; use
 *            show for exact values). An empty line is the empty list.
 *
 * bv_read returns one list holding every top-level value, in order, or a
 * status with *at set to the byte offset where reading stopped. bv_write
 * spells one value; a value the syntax cannot spell (bytes, a record,
 * nothing, BOTH, NEITHER; ratios and pairs in Logo; anything but integers
 * in NUMBERS; text that would not read back) is refused with BV_E_DOMAIN,
 * never approximated. For every value bv_write accepts, bv_read gives the
 * same value back, except LISP's FALSE, which reads back as NIL, the empty
 * list.
 *
 * Freestanding: no libc, integers only, bounded nesting (BV_MAX_DEPTH). */
#ifndef ZXV_BV_SYNTAX_H
#define ZXV_BV_SYNTAX_H

#include "bv.h"

enum { BV_SYNTAX_NUMBERS = 1, BV_SYNTAX_LISP = 2, BV_SYNTAX_SCHEME = 3, BV_SYNTAX_LOGO = 4 };

/* The reader's scratch: the values of every open list. */
typedef struct {
    int32_t stack[BV_MAX_KIDS];
    uint32_t open[BV_MAX_DEPTH + 1u]; /* where each open list's values start */
    uint8_t closer[BV_MAX_DEPTH + 1u];
    uint8_t dot[BV_MAX_DEPTH + 1u]; /* 0, 1: after " . ", 2: after the tail */
} bv_reader_t;

int32_t bv_read(bv_arena_t *a, bv_reader_t *w, int syntax, const char *s, uint32_t n, uint32_t *at);
/* The length written (no NUL), or a status; BV_E_LIMIT when cap is short. */
int32_t bv_write(const bv_arena_t *a, int32_t v, int syntax, char *out, uint32_t cap);

#endif
