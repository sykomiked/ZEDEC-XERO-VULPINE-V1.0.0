/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* pay_util.h — shared freestanding helpers for kernel/src/pay.
 *
 * Wide integer arithmetic built from 64-bit limbs (no __int128, no 64-bit
 * division: 64/64 division goes through zt_udiv64 from kernel/src/tensor/zt.c),
 * bounded byte/string helpers (no libc), a bounded text writer, a decimal
 * amount formatter, and SHA3-256 / Keccak-256 hashing built on the public
 * API of kernel/src/mlkem/keccak.h.
 *
 * HONEST LIMITS. This module is arithmetic and text plumbing. Nothing here
 * certifies anything: schema validity is not certification, there is no
 * SWIFT or CIPS connectivity anywhere in kernel/src/pay, operating as a bank
 * or money transmitter needs licences, and whether the Vino Floating Voucher
 * is "store credit" in a given jurisdiction is a legal question for counsel.
 */
#ifndef ZXV_PAY_UTIL_H
#define ZXV_PAY_UTIL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ===== 128-bit unsigned integers from two 64-bit limbs ===== */
typedef struct {
    uint64_t hi, lo;
} pay_u128;

pay_u128 pay_u128_from(uint64_t v);
pay_u128 pay_mul64(uint64_t a, uint64_t b); /* full 64x64 -> 128 product */
pay_u128 pay_u128_add(pay_u128 a, pay_u128 b);
pay_u128 pay_u128_add64(pay_u128 a, uint64_t b);
pay_u128 pay_u128_sub(pay_u128 a, pay_u128 b); /* a >= b required */
int pay_u128_cmp(pay_u128 a, pay_u128 b);      /* -1, 0, 1 */
/* Quotient of n / d (d != 0). *rem gets the remainder. Bitwise long division. */
pay_u128 pay_udiv128_64(pay_u128 n, uint64_t d, uint64_t *rem);
/* floor(sqrt(v)) of a 128-bit value; the result always fits in 64 bits. */
uint64_t pay_isqrt128(pay_u128 v);

/* 64/64 division without libgcc (wraps zt_udiv64). d == 0 returns 0. */
uint64_t pay_udiv64(uint64_t n, uint64_t d, uint64_t *rem);

/* floor(a * b / c). Returns false (and *out = 0) when c == 0 or the quotient
 * does not fit in 64 bits. *rem (optional) gets (a*b) mod c. */
bool pay_muldiv(uint64_t a, uint64_t b, uint64_t c, uint64_t *out, uint64_t *rem);

/* Checked add / sub of uint64 (false on overflow / underflow). */
bool pay_add_ok(uint64_t a, uint64_t b, uint64_t *out);
bool pay_sub_ok(uint64_t a, uint64_t b, uint64_t *out);
/* Checked signed add of int64 (false on overflow). */
bool pay_sadd_ok(int64_t a, int64_t b, int64_t *out);

/* ===== Bytes and strings (no libc) ===== */
void pay_memset(void *p, uint8_t v, size_t n);
void pay_memcpy(void *dst, const void *src, size_t n);
bool pay_memeq(const void *a, const void *b, size_t n);
size_t pay_strnlen(const char *s, size_t max);
bool pay_streq(const char *a, const char *b);
/* Copy at most cap-1 bytes and NUL-terminate. Returns false if `src` did not
 * fit (dst then holds a truncated, still terminated, copy). */
bool pay_strlcpy(char *dst, const char *src, size_t cap);
bool pay_is_digit(char c);
bool pay_is_upper(char c);
bool pay_is_lower(char c);
bool pay_is_alnum_upper(char c); /* [A-Z0-9] */
char pay_to_lower(char c);

/* ===== Bounded text writer ===== */
typedef struct {
    char *buf;
    uint32_t cap;
    uint32_t len;
    bool trunc;
} pay_w;

void pay_w_init(pay_w *w, char *buf, uint32_t cap);
void pay_w_c(pay_w *w, char c);
void pay_w_s(pay_w *w, const char *s);
void pay_w_esc(pay_w *w, const char *s); /* XML-escapes & < > " ' */
void pay_w_u64(pay_w *w, uint64_t v);
void pay_w_hex(pay_w *w, const uint8_t *p, size_t n);
/* NUL-terminates; returns bytes written or -1 when truncated. */
int32_t pay_w_finish(pay_w *w);

/* Fixed-point decimal: `units` minor units with `frac` decimal places
 * (frac <= 19). 12345, 2 -> "123.45"; 5, 2 -> "0.05"; 7, 0 -> "7". */
void pay_w_amount(pay_w *w, uint64_t units, uint8_t frac);
/* Number of decimal digits of v (1 for 0). */
uint32_t pay_dec_digits(uint64_t v);
/* 10^e for e <= 19 (0 otherwise). */
uint64_t pay_pow10(uint32_t e);

/* Parse a non-negative decimal "123.45" into minor units with exactly `frac`
 * decimal places. The text may carry fewer fraction digits than `frac` (they
 * are padded) but never more. Returns false on any syntax error or overflow. */
bool pay_parse_amount(const char *s, size_t n, uint8_t frac, uint64_t *units);
/* The number of fraction digits present in a decimal string, or -1 if bad. */
int32_t pay_amount_frac_digits(const char *s, size_t n);

/* ===== Hashing (on the public mlkem/keccak.h API) ===== */
#define PAY_HASH_LEN 32u
void pay_sha3_256(const uint8_t *data, size_t len, uint8_t out[PAY_HASH_LEN]);
/* Original Keccak-256 (padding 0x01, as Ethereum uses), NOT SHA3-256. The
 * Keccak-f[1600] permutation is reached through shake128_absorb (see .c). */
void pay_keccak256(const uint8_t *data, size_t len, uint8_t out[PAY_HASH_LEN]);

/* Incremental SHA3-256 over several pieces (fixed 1 KiB scratch buffer). */
typedef struct {
    uint8_t buf[1024];
    uint32_t len;
    bool overflow;
} pay_hbuf;
void pay_hbuf_init(pay_hbuf *h);
void pay_hbuf_put(pay_hbuf *h, const void *p, size_t n);
void pay_hbuf_u64(pay_hbuf *h, uint64_t v);
void pay_hbuf_str(pay_hbuf *h, const char *s);
/* false if the pieces overflowed the scratch buffer */
bool pay_hbuf_final(pay_hbuf *h, uint8_t out[PAY_HASH_LEN]);

/* ===== UUIDv4 / UETR ===== */
#define PAY_UETR_LEN 36u
/* Format 16 caller-supplied random bytes as a lowercase RFC 4122 version-4
 * UUID (sets the version nibble to 4 and the variant bits to 10xx). The
 * randomness is the caller's responsibility; this module has no RNG. */
void pay_uetr_from_random(const uint8_t rnd[16], char out[PAY_UETR_LEN + 1]);
/* True iff s is exactly the CBPR+ UETR pattern
 * [a-f0-9]{8}-[a-f0-9]{4}-4[a-f0-9]{3}-[89ab][a-f0-9]{3}-[a-f0-9]{12}. */
bool pay_uetr_valid(const char *s);

#endif /* ZXV_PAY_UTIL_H */
