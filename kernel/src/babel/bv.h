/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* bv.h — the Babel value: one typed value every runtime meets in
 * (docs/BABEL.md section 1).
 *
 * Kinds: nothing, integer (int64), ratio (num/den, den > 1, lowest terms),
 * truth (Belnap NEITHER/TRUE/FALSE/BOTH), bytes, text (valid UTF-8), symbol
 * (non-empty UTF-8), list (ordered values), pair (a head and a tail that is
 * not a list: LISP's dotted pair; a pair whose tail is a list is that longer
 * list) and record (ordered fields with distinct symbol names). No floats:
 * a decimal is read as an exact ratio.
 *
 * Values live in a caller-owned arena with fixed limits; every builder
 * returns a handle (>= 0) or a status (< 0) and never truncates. Nesting is
 * bounded (BV_MAX_DEPTH) when a value is built, so every walk is bounded.
 *
 * Canonical encoding (bv_encode): a kind byte, then
 *   integer   8 bytes, big-endian two's complement
 *   ratio     num, den as two such integers
 *   truth     1 byte (0..3)
 *   bytes, text, symbol   4-byte big-endian length, then the bytes
 *   list      4-byte count, then each value
 *   record    4-byte count, then each field as a symbol value and a value
 *   pair      the head, then the tail
 *   nothing   no more bytes
 * There is exactly one encoding per value: bv_decode refuses anything else
 * (a ratio not in lowest terms or with den 1, bad UTF-8, a repeated field
 * name, a pair whose tail is a list, trailing bytes), so two values are equal exactly when their
 * encodings are equal.
 *
 * Freestanding: no libc, integers only, no allocation. */
#ifndef ZXV_BV_H
#define ZXV_BV_H

#include <stdbool.h>
#include <stdint.h>

#define BV_MAX_NODES 4096u
#define BV_MAX_KIDS  8192u
#define BV_MAX_BYTES 65536u
#define BV_MAX_DEPTH 64u

typedef enum {
    BV_NOTHING = 0,
    BV_INTEGER = 1,
    BV_RATIO = 2,
    BV_TRUTH = 3,
    BV_BYTES = 4,
    BV_TEXT = 5,
    BV_SYMBOL = 6,
    BV_LIST = 7,
    BV_RECORD = 8,
    BV_PAIR = 9
} bv_kind_t;

enum { BV_NEITHER = 0, BV_TRUE = 1, BV_FALSE = 2, BV_BOTH = 3 };

enum {
    BV_OK = 0,
    BV_E_LIMIT = -1,    /* an arena limit, the output buffer, or the depth  */
    BV_E_DOMAIN = -2,   /* a zero denominator, a truth outside 0..3         */
    BV_E_TEXT = -3,     /* text or a symbol that is not valid UTF-8         */
    BV_E_NAME = -4,     /* an empty or repeated field name, not a symbol    */
    BV_E_FORM = -5,     /* an encoding or decimal that is not canonical     */
    BV_E_OVERFLOW = -6, /* the exact value does not fit 64 bits             */
    BV_E_HANDLE = -7    /* not a value of this arena, or the wrong kind     */
};

typedef struct {
    uint8_t kind, depth;
    uint32_t len;
    int64_t a, b;
} bv_node_t;

typedef struct {
    bv_node_t node[BV_MAX_NODES];
    uint32_t kid[BV_MAX_KIDS];
    uint8_t byte[BV_MAX_BYTES];
    uint32_t nn, nk, nb;
} bv_arena_t;

void bv_reset(bv_arena_t *a);

/* Builders: a handle (>= 0) or a BV_E_* status. */
int32_t bv_nothing(bv_arena_t *a);
int32_t bv_int(bv_arena_t *a, int64_t v);
int32_t bv_ratio(bv_arena_t *a, int64_t num, int64_t den); /* an integer when exact */
/* "-12.50" -> -25/2: an optional sign, digits, an optional point and digits;
 * the digits without the point must fit 64 bits unsigned (OVERFLOW). */
int32_t bv_decimal(bv_arena_t *a, const char *s, uint32_t n);
int32_t bv_truth(bv_arena_t *a, int64_t t);
int32_t bv_bytes(bv_arena_t *a, const uint8_t *p, uint32_t n);
int32_t bv_text(bv_arena_t *a, const char *s, uint32_t n);
int32_t bv_symbol(bv_arena_t *a, const char *s, uint32_t n);
int32_t bv_list(bv_arena_t *a, const int32_t *v, uint32_t n);
/* names[i] are symbol handles, distinct */
int32_t bv_record(bv_arena_t *a, const int32_t *names, const int32_t *v, uint32_t n);
/* (head . tail); a list tail gives the list with head in front */
int32_t bv_pair(bv_arena_t *a, int32_t head, int32_t tail);

/* Readers. A bad handle reads as nothing, length 0. */
bv_kind_t bv_kind(const bv_arena_t *a, int32_t v);
uint32_t bv_len(const bv_arena_t *a, int32_t v);             /* bytes, or elements, or fields */
int64_t bv_num(const bv_arena_t *a, int32_t v);              /* integer, ratio numerator, truth */
int64_t bv_den(const bv_arena_t *a, int32_t v);              /* 1 for an integer */
const uint8_t *bv_data(const bv_arena_t *a, int32_t v);      /* bytes, text, symbol */
int32_t bv_at(const bv_arena_t *a, int32_t v, uint32_t i);   /* element, field value, head/tail */
int32_t bv_name(const bv_arena_t *a, int32_t v, uint32_t i); /* field name */
int32_t bv_get(const bv_arena_t *a, int32_t v, const char *name, uint32_t n);

/* Canonical bytes: the length, or BV_E_LIMIT when cap is too small. */
int32_t bv_encode(const bv_arena_t *a, int32_t v, uint8_t *out, uint32_t cap);
/* Exactly one value, all n bytes, canonical: a handle or a status. */
int32_t bv_decode(bv_arena_t *a, const uint8_t *in, uint32_t n);

bool bv_equal(const bv_arena_t *a, int32_t x, const bv_arena_t *b, int32_t y);
/* Both sides of a bridge: TRUE when equal, BOTH when not, NEITHER when a
 * side has no value. */
int bv_witness(const bv_arena_t *a, int32_t x, const bv_arena_t *b, int32_t y);

const char *bv_status_name(int32_t s);

#endif
