/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zc_internal.h — z-contract internals (not part of the kit API; the
 * tests include it to tamper with evidence records). Integer code only. */
#ifndef ZC_INTERNAL_H
#define ZC_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include "zcontract.h"

/* ZC_DEN_* are public (zcontract.h, kit API 0.2). */

typedef struct {
    int32_t kind;          /* ZC_T_* */
    int has_lo, has_hi;    /* a range was written; "*" leaves one side open */
    int ranged;            /* the [..] was written at all                    */
    int64_t lo_n, lo_d;    /* INT: lo_n; RAT: lo_n/lo_d                      */
    int64_t hi_n, hi_d;
    uint64_t blo, bhi;     /* BYTES length range (bhi valid if has_hi)       */
    int den_mode;          /* ZC_DEN_*                                       */
    int64_t den_bound;
    uint64_t allowed;      /* TRUTH: 4 bits; TAG: 64 bits                    */
    uint32_t nset;         /* entries written in {..}; 0 = no set (all)      */
    uint8_t set[ZC_MAX_SET][3]; /* TRUTH: set[i][0]; TAG: base/pol/phase,    */
                                /* 0xFF = "*"                                */
} zc_type;

typedef struct {
    char name[ZC_MAX_NAME + 1];
    zc_type type;
} zc_param;

typedef struct {
    char name[ZC_MAX_NAME + 1];
    uint32_t nparams;
    zc_param params[ZC_MAX_PARAMS];
    int has_result;
    zc_type result;
    int has_proposes;
    zc_type proposes;
    uint32_t max_proposals;
} zc_fn;

struct zc_contract {
    uint32_t nfns;
    zc_fn *fns;
};

typedef struct {
    int32_t kind;
    int64_t num, den;
    uint8_t *bytes;
    size_t len;
} zc_val;

struct zc_vals {
    uint32_t n;
    zc_val v[ZC_MAX_VALUES];
};

typedef struct {
    uint64_t seq;
    uint32_t origin;
    int32_t part;
    uint32_t index;
    int32_t reason;
    int32_t tag;
    uint8_t contract[32];
    uint8_t subject[32];
    uint32_t text_len;
    char text[ZC_EVLOG_TEXT_MAX + 1];
    uint8_t prev[32];
    uint8_t hash[32];
} zc_rec;

struct zc_evlog {
    zc_rec *rec;
    uint32_t n, cap, alloc;
    uint8_t head[32];
    zc_evlog_sink sink;
    void *sink_user;
};

/* zc_util.c */
void zc_set_error(const char *msg);
void zc_set_errorf(const char *fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 1, 2)))
#endif
    ;
/* Copy `src` (len n) into msg/cap (truncating, NUL-terminated) and set
 * *out_len. */
void zc_out_msg(const char *src, char *msg, size_t cap, size_t *out_len);
/* Offset of the first byte of the first invalid UTF-8 sequence; n if valid. */
size_t zc_utf8_check(const uint8_t *s, size_t n);
/* Length of the longest prefix of s[0..n) that ends on a UTF-8 boundary
 * and is at most max bytes. */
size_t zc_utf8_cut(const char *s, size_t n, size_t max);
/* Decimal text of an int64 into b (>= 21 bytes); returns the length. */
size_t zc_i64_text(int64_t v, char *b);

const zc_fn *zc_find_fn(const zc_contract *c, const char *name);
/* Append the canonical text of a type. */
typedef struct {
    char *b;
    size_t n, cap;
    int oom;
} zc_buf;
void zc_buf_puts(zc_buf *b, const char *s);
void zc_buf_putn(zc_buf *b, const char *s, size_t n);
void zc_buf_i64(zc_buf *b, int64_t v);
void zc_buf_u64(zc_buf *b, uint64_t v);
void zc_buf_rat(zc_buf *b, int64_t n, int64_t d);
void zc_type_format(zc_buf *b, const zc_type *t);
/* Short text of a value for messages ("333/80", "true", "tag 17", "4 bytes"). */
void zc_val_text(const zc_val *v, char *out, size_t cap);

extern const char *const zc_base_names[4];
extern const char *const zc_pol_names[4];
extern const char *const zc_phase_names[4];

#endif /* ZC_INTERNAL_H */
