/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* cb_util.h — freestanding helpers for kernel/src/cbank.
 *
 * Exact integer arithmetic (128-bit values from two 64-bit limbs, no
 * __int128; 64-bit division only through zt_udiv64 from
 * kernel/src/tensor/zt.c), bounded strings without libc, and a bounded text
 * writer with XML escaping and a fixed-point decimal formatter.
 *
 * cbank keeps its own copy of these helpers instead of including
 * kernel/src/pay/pay_util.h so that the two modules can evolve separately.
 *
 * HONEST LIMITS. This is arithmetic and text plumbing. It certifies nothing.
 */
#ifndef ZXV_CB_UTIL_H
#define ZXV_CB_UTIL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    uint64_t hi, lo;
} cb_u128;

cb_u128 cb_mul64(uint64_t a, uint64_t b);
/* a * b where a is 128-bit; false on overflow past 128 bits. */
bool cb_u128_mul64(cb_u128 a, uint64_t b, cb_u128 *out);
/* n / d, d != 0. False when d == 0 or the quotient exceeds 64 bits. */
bool cb_udiv128(cb_u128 n, uint64_t d, uint64_t *q, uint64_t *rem);
/* floor(a * b * 10^e / d). False on d == 0 or overflow. *rem optional. */
bool cb_muldiv_p10(uint64_t a, uint64_t b, uint32_t e, uint64_t d, uint64_t *q, uint64_t *rem);
/* 64/64 division without libgcc (wraps zt_udiv64). d == 0 returns 0. */
uint64_t cb_udiv64(uint64_t n, uint64_t d, uint64_t *rem);
/* 10^e for e <= 19, otherwise 0. */
uint64_t cb_pow10(uint32_t e);
bool cb_add_ok(uint64_t a, uint64_t b, uint64_t *out);
bool cb_sadd_ok(int64_t a, int64_t b, int64_t *out);

void cb_memset(void *p, uint8_t v, size_t n);
void cb_memcpy(void *dst, const void *src, size_t n);
bool cb_memeq(const void *a, const void *b, size_t n);
size_t cb_strnlen(const char *s, size_t max);
bool cb_streq(const char *a, const char *b);
/* Copy and NUL-terminate; false (and dst emptied) if src does not fit. */
bool cb_strlcpy(char *dst, const char *src, size_t cap);
bool cb_is_upper(char c);
bool cb_is_digit(char c);
/* True if s is exactly n characters of [A-Z]. */
bool cb_is_upper_n(const char *s, size_t n);
/* ISO 9362 BIC (BICFIDec2014 pattern): 4 alnum (party), 2 letters (country), 2 alnum (location),
 * optional 3 alnum (branch). Structure only; not a directory lookup. */
bool cb_bic_valid(const char *s);

/* ===== Bounded text writer ===== */
typedef struct {
    char *buf;
    uint32_t cap;
    uint32_t len;
    bool trunc;
} cb_w;

void cb_w_init(cb_w *w, char *buf, uint32_t cap);
void cb_w_c(cb_w *w, char c);
void cb_w_s(cb_w *w, const char *s);
void cb_w_esc(cb_w *w, const char *s); /* XML-escapes & < > " ' */
void cb_w_u64(cb_w *w, uint64_t v);
void cb_w_u64_pad(cb_w *w, uint64_t v, uint32_t width); /* zero padded */
/* units with frac decimal places: 12345,2 -> "123.45"; 7,0 -> "7". */
void cb_w_amount(cb_w *w, uint64_t units, uint8_t frac);
/* signed variant ("-1.50"). */
void cb_w_samount(cb_w *w, int64_t units, uint8_t frac);
/* ISO 8601 UTC date-time "YYYY-MM-DDThh:mm:ssZ" / date "YYYY-MM-DD" from
 * Unix seconds (proleptic Gregorian, years 1970..9999). */
void cb_w_datetime(cb_w *w, uint64_t unix_s);
void cb_w_date(cb_w *w, uint64_t unix_s);
/* NUL-terminates; returns bytes written or -1 when truncated. */
int32_t cb_w_finish(cb_w *w);

/* Days since 1970-01-01 -> civil date (Howard Hinnant's algorithm). */
void cb_civil_from_days(uint32_t days, uint32_t *y, uint32_t *m, uint32_t *d);
/* 0 = Monday .. 6 = Sunday. */
uint32_t cb_weekday(uint32_t days);

#endif /* ZXV_CB_UTIL_H */
