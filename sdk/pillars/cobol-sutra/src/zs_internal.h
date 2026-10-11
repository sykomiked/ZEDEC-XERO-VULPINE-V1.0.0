/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zs_internal.h — libzsutra internals (not part of the pillar API; the
 * fuzzer includes it). Integer code only (R11). */
#ifndef ZS_INTERNAL_H
#define ZS_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include "zsutra.h"

/* Where a field was declared, for error messages raised after parsing. */
typedef struct {
    uint32_t line, col;
    uint64_t offset;
} zs_pos;

typedef struct {
    zs_field f;        /* the public view                                  */
    uint32_t nchild;   /* direct children                                  */
    uint64_t rel;      /* offset inside the parent's first occurrence      */
    int32_t usage_set; /* copybook: a USAGE was written on this item       */
    int32_t is_signed; /* the picture starts with S                        */
    int32_t sign_clause; /* ZS_SIGN_* written in a SIGN clause, 0 if none  */
    int32_t level;     /* copybook level number (01..49); Sutra: indent    */
    zs_pos pos;
} zs_item;

struct zs_layout {
    uint32_t n;
    zs_item *items;
    uint64_t size;
    int32_t overpunch; /* ZS_OVERPUNCH_* */
};

/* ---- errors (zs_util.c) ---- */
void zs_set_error(const char *msg);
void zs_set_errorf(const char *fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 1, 2)))
#endif
    ;
/* Fill *d (may be NULL) with a position and a message, and set the last
 * error to the same text. Returns `status`. */
int32_t zs_diag_set(zs_diag *d, int32_t status, const char *text, size_t len, uint64_t offset,
                    const char *fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 6, 7)))
#endif
    ;
/* Length of the valid UTF-8 prefix of s[0..n). */
size_t zs_utf8_check(const uint8_t *s, size_t n);
/* R4 copy: n bytes of src into buf (NUL-terminated). */
int32_t zs_out_text(const char *src, size_t n, char *buf, size_t cap, size_t *out_len);
int zs_ieq(const char *a, const char *b); /* ASCII case-insensitive equality */

/* ---- layouts (zs_layout.c) ---- */
/* A layout under construction: items are appended in declaration order
 * with their parent already set. */
typedef struct {
    zs_layout *l;
    uint32_t cap;
} zs_builder;

int32_t zs_builder_init(zs_builder *b);
void zs_builder_free(zs_builder *b);
/* Append an item; returns its index in *out or ZXV_E_CAPACITY / NOMEM. */
int32_t zs_builder_add(zs_builder *b, zs_item *item, uint32_t *out);
/* Validate a complete layout and compute sizes and offsets. On failure
 * fills *d with the offending item's position. On success the layout is
 * moved to *out and the builder is empty. */
int32_t zs_builder_finish(zs_builder *b, const char *text, size_t len, zs_diag *d,
                          zs_layout **out);
/* 1 if s[0..n) is a valid data name (1..30 of A-Z a-z 0-9 '-', no '-' at
 * either end, at least one letter). */
int zs_valid_name(const char *s, size_t n);
/* 1 if s[0..n) is a word the copybook parser reserves (PIC, COMP, TO,
 * FILLER, ...), case-insensitive. Sutra refuses these as names too, so
 * both front ends accept exactly the same names. */
int zs_reserved(const char *s, size_t n);

/* ---- pictures (zs_picture.c) ---- */
/* Parse a picture string: X(n), [S]9(i)[V9(d)]. On success fills kind,
 * digits, scale and *out_signed. On failure returns ZS_E_SYNTAX or
 * ZS_E_UNSUPPORTED with a message in msg and the bad character's index in
 * *out_at. */
int32_t zs_picture_parse(const char *s, size_t n, int32_t *out_kind, uint32_t *out_digits,
                         uint32_t *out_scale, int32_t *out_signed, size_t *out_at, char *msg,
                         size_t msg_cap);

/* ---- values (zs_codec.c) ---- */
extern const int64_t zs_pow10[19];
/* Decode / encode one elementary numeric item at rec + off. */
int32_t zs_num_decode(const zs_layout *l, const zs_item *it, const uint8_t *p, int64_t *out);
int32_t zs_num_encode(const zs_layout *l, const zs_item *it, uint8_t *p, int64_t unscaled);
/* Exact decimal text of unscaled / 10^scale with all `scale` decimals. */
size_t zs_decimal_text(int64_t unscaled, uint32_t scale, char *b /* >= 24 */);

#endif /* ZS_INTERNAL_H */
