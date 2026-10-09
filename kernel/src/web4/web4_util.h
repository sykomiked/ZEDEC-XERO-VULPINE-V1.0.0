/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* web4_util.h — shared freestanding helpers for kernel/src/web4.
 *
 * Status codes, bounded byte/string helpers (no libc), a bounded writer,
 * hex and base64/base64url codecs (strict: non-canonical input is refused),
 * 256-bit unsigned integers from 32-bit limbs (no __int128, no 64-bit
 * division anywhere: 64-bit quotients go through w4_udiv64, a shift-subtract
 * loop), and thin wrappers over the hashes the tree already has:
 *   SHA-256                  kernel/src/robin_debanks/sha256.c (HMAC built here, streamed)
 *   SHA3-256                 kernel/src/mlkem/keccak.c
 *   Keccak-256 (Ethereum)    built here on the public keccak.h API (see .c)
 *
 * HONEST LIMITS. Plumbing only. Nothing in this file is a security boundary
 * by itself; w4_ct_eq is constant-time in its data, the rest is not meant to be.
 */
#ifndef ZXV_WEB4_UTIL_H
#define ZXV_WEB4_UTIL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ===== Status codes (shared by every web4 file) ===== */
enum {
    W4_OK = 0,
    W4_ERR_ARG = -1,       /* bad argument                                    */
    W4_ERR_SPACE = -2,     /* caller buffer too small                         */
    W4_ERR_PARSE = -3,     /* malformed input                                 */
    W4_ERR_SIG = -4,       /* signature or MAC did not verify                 */
    W4_ERR_REPLAY = -5,    /* sequence already seen                           */
    W4_ERR_STALE = -6,     /* timestamp outside the acceptance window         */
    W4_ERR_CONSENT = -7,   /* money / personal-data action without consent    */
    W4_ERR_RATE = -8,      /* rate limit exceeded                             */
    W4_ERR_STATE = -9,     /* call not valid in the current state             */
    W4_ERR_UNSUPP = -10,   /* well-formed but not supported here              */
    W4_ERR_HASH = -11,     /* content does not match its hash / CID           */
    W4_ERR_EXPIRED = -12,  /* token, intent or record has expired             */
    W4_ERR_DENIED = -13,   /* the remote side refused                         */
    W4_ERR_RANGE = -14,    /* value out of range / not exactly representable  */
    W4_ERR_NOTFOUND = -15, /* key, peer or tool not known                     */
    W4_ERR_PENDING = -16,  /* need more input / try again later               */
    W4_ERR_BINDING = -17   /* identity does not bind to the claimed key       */
};

/* ===== Bytes and strings ===== */
void w4_memset(void *p, uint8_t v, size_t n);
void w4_memcpy(void *dst, const void *src, size_t n);
void w4_memmove(void *dst, const void *src, size_t n);
bool w4_memeq(const void *a, const void *b, size_t n); /* not constant-time */
bool w4_ct_eq(const void *a, const void *b, size_t n); /* constant-time in the data */
size_t w4_strnlen(const char *s, size_t max);
bool w4_streq(const char *a, const char *b);
bool w4_strneq(const char *a, size_t an, const char *b); /* a[0..an) == NUL-terminated b */
/* ASCII case-insensitive compare of two counted strings. */
bool w4_casecmp_eq(const char *a, size_t an, const char *b, size_t bn);
/* Copy at most cap-1 bytes, NUL-terminate. False if src did not fit. */
bool w4_strlcpy(char *dst, const char *src, size_t cap);
bool w4_strlcpyn(char *dst, size_t cap, const char *src, size_t n);
char w4_lower(char c);
bool w4_is_digit(char c);
bool w4_is_alpha(char c);
bool w4_is_hex(char c);
int w4_hexval(char c); /* -1 if not hex */

/* Big- / little-endian integer packing. */
void w4_be32_put(uint8_t *p, uint32_t v);
void w4_be64_put(uint8_t *p, uint64_t v);
uint32_t w4_be32_get(const uint8_t *p);
uint64_t w4_be64_get(const uint8_t *p);
void w4_le16_put(uint8_t *p, uint16_t v);
void w4_le32_put(uint8_t *p, uint32_t v);
void w4_le64_put(uint8_t *p, uint64_t v);
uint16_t w4_le16_get(const uint8_t *p);
uint32_t w4_le32_get(const uint8_t *p);
uint64_t w4_le64_get(const uint8_t *p);

/* 64/64 division by shift-subtract (no libgcc). d == 0 returns 0, rem = n. */
uint64_t w4_udiv64(uint64_t n, uint64_t d, uint64_t *rem);

/* ===== Bounded writer (bytes or text) ===== */
typedef struct {
    uint8_t *buf;
    uint32_t cap;
    uint32_t len;
    bool err; /* sticky: something did not fit or was refused */
} w4_w;

void w4_w_init(w4_w *w, void *buf, uint32_t cap);
void w4_w_byte(w4_w *w, uint8_t b);
void w4_w_bytes(w4_w *w, const void *p, uint32_t n);
void w4_w_str(w4_w *w, const char *s);
void w4_w_strn(w4_w *w, const char *s, uint32_t n);
void w4_w_u64(w4_w *w, uint64_t v);                   /* decimal */
void w4_w_i64(w4_w *w, int64_t v);                    /* decimal */
void w4_w_hex(w4_w *w, const uint8_t *p, uint32_t n); /* lowercase */
/* NUL-terminate (not counted). Returns the length or W4_ERR_SPACE. */
int32_t w4_w_cstr(w4_w *w);
/* Returns the length or W4_ERR_SPACE, without a terminator. */
int32_t w4_w_done(const w4_w *w);

/* ===== Hex ===== */
/* Lowercase hex of in[0..n) into out (2n chars + NUL). Returns 2n or W4_ERR_SPACE. */
int32_t w4_hex_encode(const uint8_t *in, uint32_t n, char *out, uint32_t cap);
/* Decode exactly `len` hex chars (even, either case). Returns bytes or negative. */
int32_t w4_hex_decode(const char *in, uint32_t len, uint8_t *out, uint32_t cap);

/* ===== Base64 (RFC 4648) ===== */
/* base64url, no padding (JOSE, PKCE). Returns chars written (NUL added) or negative. */
int32_t w4_b64url_encode(const uint8_t *in, uint32_t n, char *out, uint32_t cap);
/* Strict: alphabet [A-Za-z0-9-_], no padding, no whitespace, unused trailing
 * bits must be zero, length mod 4 != 1. Returns bytes or W4_ERR_PARSE / SPACE. */
int32_t w4_b64url_decode(const char *in, uint32_t len, uint8_t *out, uint32_t cap);
/* Standard base64 with '=' padding (HTTP Basic). */
int32_t w4_b64_encode(const uint8_t *in, uint32_t n, char *out, uint32_t cap);

/* ===== Hashes ===== */
#define W4_HASH_LEN 32u
void w4_sha256(const uint8_t *data, size_t len, uint8_t out[W4_HASH_LEN]);
void w4_sha3_256(const uint8_t *data, size_t len, uint8_t out[W4_HASH_LEN]);
/* Original Keccak-256 (pad 0x01), as Ethereum uses. NOT SHA3-256. */
void w4_keccak256(const uint8_t *data, size_t len, uint8_t out[W4_HASH_LEN]);
void w4_hmac_sha256(const uint8_t *key, uint32_t key_len, const uint8_t *msg, uint32_t msg_len,
                    uint8_t out[W4_HASH_LEN]);
/* HMAC-SHA256 over the concatenation of n parts, streamed (RFC 2104). */
void w4_hmac_sha256_parts(const uint8_t *key, uint32_t key_len, const uint8_t *const *parts,
                          const uint32_t *lens, uint32_t n, uint8_t out[W4_HASH_LEN]);

/* Incremental SHA3-256 over a bounded scratch buffer (pieces of a record). */
typedef struct {
    uint8_t buf[4096];
    uint32_t len;
    bool overflow;
} w4_hb;
void w4_hb_init(w4_hb *h);
void w4_hb_put(w4_hb *h, const void *p, uint32_t n);
void w4_hb_u8(w4_hb *h, uint8_t v);
void w4_hb_u32(w4_hb *h, uint32_t v); /* little-endian */
void w4_hb_u64(w4_hb *h, uint64_t v); /* little-endian */
/* length-prefixed (le32) string, so "ab"+"c" never collides with "a"+"bc" */
void w4_hb_str(w4_hb *h, const char *s, uint32_t max);
bool w4_hb_sha3(w4_hb *h, uint8_t out[W4_HASH_LEN]); /* false on overflow */

/* ===== 256-bit unsigned integers ===== */
typedef struct {
    uint32_t w[8]; /* little-endian limbs */
} w4_u256;

void w4_u256_zero(w4_u256 *a);
void w4_u256_from_u64(w4_u256 *a, uint64_t v);
bool w4_u256_is_zero(const w4_u256 *a);
/* true and *v if a fits in 64 bits */
bool w4_u256_to_u64(const w4_u256 *a, uint64_t *v);
int w4_u256_cmp(const w4_u256 *a, const w4_u256 *b);              /* -1, 0, 1 */
bool w4_u256_add(w4_u256 *r, const w4_u256 *a, const w4_u256 *b); /* false on overflow */
bool w4_u256_sub(w4_u256 *r, const w4_u256 *a, const w4_u256 *b); /* false if a < b */
bool w4_u256_mul_u32(w4_u256 *r, const w4_u256 *a, uint32_t m);   /* false on overflow */
/* r = a / d, *rem = a mod d (d != 0, d <= 0xffff for the 16-bit chunk loop;
 * larger d up to 2^32-1 is handled bitwise). Returns false if d == 0. */
bool w4_u256_divmod_u32(w4_u256 *r, const w4_u256 *a, uint32_t d, uint32_t *rem);
/* Big-endian bytes. from: len <= 32. to: exactly 32 bytes. */
bool w4_u256_from_be(w4_u256 *a, const uint8_t *p, uint32_t len);
void w4_u256_to_be(const w4_u256 *a, uint8_t out[32]);
/* Minimal big-endian bytes (no leading zero; zero -> 0 bytes). Returns length. */
uint32_t w4_u256_to_min_be(const w4_u256 *a, uint8_t out[32]);
/* Strict decimal ("0" or [1-9][0-9]*, no sign, no spaces). */
bool w4_u256_from_dec(w4_u256 *a, const char *s, uint32_t len);
void w4_w_u256_dec(w4_w *w, const w4_u256 *a);
/* "0x" + minimal lowercase hex, "0x0" for zero (Ethereum JSON-RPC QUANTITY). */
void w4_w_u256_qty(w4_w *w, const w4_u256 *a);
/* Parse a QUANTITY: "0x0" or "0x[1-9a-f][0-9a-f]*" (<= 64 digits). */
bool w4_u256_from_qty(w4_u256 *a, const char *s, uint32_t len);
/* Exact rescale between decimal precisions: a * 10^(to - from). Fails
 * (W4_ERR_RANGE) on overflow or, when to < from, when digits would be lost. */
int w4_u256_rescale(w4_u256 *r, const w4_u256 *a, uint8_t from_dec, uint8_t to_dec);

#endif /* ZXV_WEB4_UTIL_H */
