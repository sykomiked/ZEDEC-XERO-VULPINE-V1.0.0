/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* pay_util.c — wide integers, bounded text, hashing. See pay_util.h. */
#include "pay_util.h"
#include "zt.h"
#include "keccak.h"

/* ===== 128-bit arithmetic ===== */

pay_u128 pay_u128_from(uint64_t v)
{
    pay_u128 r = {0, v};
    return r;
}

pay_u128 pay_mul64(uint64_t a, uint64_t b)
{
    uint64_t a0 = a & 0xffffffffu, a1 = a >> 32, b0 = b & 0xffffffffu, b1 = b >> 32;
    uint64_t p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1;
    uint64_t mid = (p00 >> 32) + (p01 & 0xffffffffu) + (p10 & 0xffffffffu);
    pay_u128 r;
    r.lo = (p00 & 0xffffffffu) | (mid << 32);
    r.hi = p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32);
    return r;
}

pay_u128 pay_u128_add(pay_u128 a, pay_u128 b)
{
    pay_u128 r;
    r.lo = a.lo + b.lo;
    r.hi = a.hi + b.hi + (r.lo < a.lo ? 1u : 0u);
    return r;
}

pay_u128 pay_u128_add64(pay_u128 a, uint64_t b)
{
    return pay_u128_add(a, pay_u128_from(b));
}

pay_u128 pay_u128_sub(pay_u128 a, pay_u128 b)
{
    pay_u128 r;
    r.lo = a.lo - b.lo;
    r.hi = a.hi - b.hi - (a.lo < b.lo ? 1u : 0u);
    return r;
}

int pay_u128_cmp(pay_u128 a, pay_u128 b)
{
    if (a.hi != b.hi) return a.hi < b.hi ? -1 : 1;
    if (a.lo != b.lo) return a.lo < b.lo ? -1 : 1;
    return 0;
}

pay_u128 pay_udiv128_64(pay_u128 n, uint64_t d, uint64_t *rem)
{
    pay_u128 q = {0, 0};
    uint64_t r = 0;
    if (d == 0) {
        if (rem) *rem = 0;
        return q;
    }
    for (int i = 127; i >= 0; i--) {
        uint64_t bit = i >= 64 ? (n.hi >> (i - 64)) & 1u : (n.lo >> i) & 1u;
        uint64_t carry = r >> 63;
        r = (r << 1) | bit;
        if (carry || r >= d) {
            r -= d;
            if (i >= 64)
                q.hi |= (uint64_t) 1 << (i - 64);
            else
                q.lo |= (uint64_t) 1 << i;
        }
    }
    if (rem) *rem = r;
    return q;
}

static pay_u128 shr2(pay_u128 v)
{
    pay_u128 r;
    r.lo = (v.lo >> 2) | (v.hi << 62);
    r.hi = v.hi >> 2;
    return r;
}

static pay_u128 shr1(pay_u128 v)
{
    pay_u128 r;
    r.lo = (v.lo >> 1) | (v.hi << 63);
    r.hi = v.hi >> 1;
    return r;
}

/* Digit-by-digit (binary) integer square root on two limbs. */
uint64_t pay_isqrt128(pay_u128 v)
{
    pay_u128 res = {0, 0};
    pay_u128 bit = {(uint64_t) 1 << 62, 0}; /* 2^126 */
    while (pay_u128_cmp(bit, v) > 0) bit = shr2(bit);
    while (bit.hi | bit.lo) {
        pay_u128 t = pay_u128_add(res, bit);
        if (pay_u128_cmp(v, t) >= 0) {
            v = pay_u128_sub(v, t);
            res = pay_u128_add(shr1(res), bit);
        } else {
            res = shr1(res);
        }
        bit = shr2(bit);
    }
    return res.lo;
}

uint64_t pay_udiv64(uint64_t n, uint64_t d, uint64_t *rem)
{
    return zt_udiv64(n, d, rem);
}

bool pay_muldiv(uint64_t a, uint64_t b, uint64_t c, uint64_t *out, uint64_t *rem)
{
    uint64_t r = 0;
    *out = 0;
    if (rem) *rem = 0;
    if (c == 0) return false;
    pay_u128 q = pay_udiv128_64(pay_mul64(a, b), c, &r);
    if (q.hi != 0) return false;
    *out = q.lo;
    if (rem) *rem = r;
    return true;
}

bool pay_add_ok(uint64_t a, uint64_t b, uint64_t *out)
{
    uint64_t s = a + b;
    if (s < a) return false;
    *out = s;
    return true;
}

bool pay_sub_ok(uint64_t a, uint64_t b, uint64_t *out)
{
    if (b > a) return false;
    *out = a - b;
    return true;
}

bool pay_sadd_ok(int64_t a, int64_t b, int64_t *out)
{
    if ((b > 0 && a > INT64_MAX - b) || (b < 0 && a < INT64_MIN - b)) return false;
    *out = a + b;
    return true;
}

/* ===== Bytes and strings ===== */

void pay_memset(void *p, uint8_t v, size_t n)
{
    volatile uint8_t *d = (volatile uint8_t *) p;
    for (size_t i = 0; i < n; i++) d[i] = v;
}

void pay_memcpy(void *dst, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *) dst;
    const uint8_t *s = (const uint8_t *) src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
}

bool pay_memeq(const void *a, const void *b, size_t n)
{
    const uint8_t *x = (const uint8_t *) a, *y = (const uint8_t *) b;
    uint8_t acc = 0;
    for (size_t i = 0; i < n; i++) acc |= (uint8_t) (x[i] ^ y[i]);
    return acc == 0;
}

size_t pay_strnlen(const char *s, size_t max)
{
    size_t n = 0;
    if (!s) return 0;
    while (n < max && s[n]) n++;
    return n;
}

bool pay_streq(const char *a, const char *b)
{
    if (!a || !b) return false;
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

bool pay_strlcpy(char *dst, const char *src, size_t cap)
{
    size_t i = 0;
    if (!dst || cap == 0) return false;
    if (!src) {
        dst[0] = '\0';
        return true;
    }
    while (src[i] && i + 1 < cap) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
    return src[i] == '\0';
}

bool pay_is_digit(char c)
{
    return c >= '0' && c <= '9';
}

bool pay_is_upper(char c)
{
    return c >= 'A' && c <= 'Z';
}

bool pay_is_lower(char c)
{
    return c >= 'a' && c <= 'z';
}

bool pay_is_alnum_upper(char c)
{
    return pay_is_digit(c) || pay_is_upper(c);
}

char pay_to_lower(char c)
{
    return pay_is_upper(c) ? (char) (c - 'A' + 'a') : c;
}

/* ===== Writer ===== */

void pay_w_init(pay_w *w, char *buf, uint32_t cap)
{
    w->buf = buf;
    w->cap = cap;
    w->len = 0;
    w->trunc = (buf == 0 || cap == 0);
}

void pay_w_c(pay_w *w, char c)
{
    if (w->trunc) return;
    if (w->len + 1u >= w->cap) {
        w->trunc = true;
        return;
    }
    w->buf[w->len++] = c;
}

void pay_w_s(pay_w *w, const char *s)
{
    while (s && *s && !w->trunc) pay_w_c(w, *s++);
}

void pay_w_esc(pay_w *w, const char *s)
{
    while (s && *s && !w->trunc) {
        char c = *s++;
        switch (c) {
        case '&':
            pay_w_s(w, "&amp;");
            break;
        case '<':
            pay_w_s(w, "&lt;");
            break;
        case '>':
            pay_w_s(w, "&gt;");
            break;
        case '"':
            pay_w_s(w, "&quot;");
            break;
        case '\'':
            pay_w_s(w, "&apos;");
            break;
        default:
            pay_w_c(w, c);
            break;
        }
    }
}

void pay_w_u64(pay_w *w, uint64_t v)
{
    char t[20];
    int n = 0;
    if (v == 0) {
        pay_w_c(w, '0');
        return;
    }
    while (v && n < 20) {
        uint64_t r;
        v = pay_udiv64(v, 10u, &r);
        t[n++] = (char) ('0' + (int) r);
    }
    while (n > 0) pay_w_c(w, t[--n]);
}

void pay_w_hex(pay_w *w, const uint8_t *p, size_t n)
{
    static const char hx[] = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) {
        pay_w_c(w, hx[p[i] >> 4]);
        pay_w_c(w, hx[p[i] & 15u]);
    }
}

int32_t pay_w_finish(pay_w *w)
{
    if (w->buf && w->cap > 0) w->buf[w->len < w->cap ? w->len : w->cap - 1u] = '\0';
    return w->trunc ? -1 : (int32_t) w->len;
}

uint64_t pay_pow10(uint32_t e)
{
    uint64_t p = 1;
    if (e > 19) return 0;
    for (uint32_t i = 0; i < e; i++) p *= 10u;
    return p;
}

uint32_t pay_dec_digits(uint64_t v)
{
    uint32_t d = 1;
    while (d < 20 && v >= pay_pow10(d)) d++;
    return d;
}

void pay_w_amount(pay_w *w, uint64_t units, uint8_t frac)
{
    if (frac == 0) {
        pay_w_u64(w, units);
        return;
    }
    if (frac > 19) {
        w->trunc = true;
        return;
    }
    uint64_t r, whole = pay_udiv64(units, pay_pow10(frac), &r);
    pay_w_u64(w, whole);
    pay_w_c(w, '.');
    for (int i = (int) frac - 1; i >= 0; i--) {
        uint64_t d;
        pay_udiv64(pay_udiv64(r, pay_pow10((uint32_t) i), 0), 10u, &d);
        pay_w_c(w, (char) ('0' + (int) d));
    }
}

int32_t pay_amount_frac_digits(const char *s, size_t n)
{
    size_t i = 0, digits = 0;
    int32_t frac = -1;
    if (!s || n == 0) return -1;
    for (; i < n; i++) {
        if (s[i] == '.') {
            if (frac >= 0 || digits == 0) return -1;
            frac = 0;
        } else if (pay_is_digit(s[i])) {
            if (frac >= 0)
                frac++;
            else
                digits++;
        } else {
            return -1;
        }
    }
    if (frac == 0) return -1; /* "12." */
    return frac < 0 ? 0 : frac;
}

bool pay_parse_amount(const char *s, size_t n, uint8_t frac, uint64_t *units)
{
    int32_t fd = pay_amount_frac_digits(s, n);
    uint64_t v = 0;
    if (fd < 0 || fd > (int32_t) frac || frac > 19) return false;
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '.') continue;
        pay_u128 t = pay_mul64(v, 10u);
        if (t.hi) return false;
        if (!pay_add_ok(t.lo, (uint64_t) (s[i] - '0'), &v)) return false;
    }
    for (int32_t i = fd; i < (int32_t) frac; i++) {
        pay_u128 t = pay_mul64(v, 10u);
        if (t.hi) return false;
        v = t.lo;
    }
    *units = v;
    return true;
}

/* ===== Hashing ===== */

void pay_sha3_256(const uint8_t *data, size_t len, uint8_t out[PAY_HASH_LEN])
{
    sha3_256(data, len, out);
}

/* Keccak-f[1600] through the public incremental SHAKE128 API: with an empty
 * buffer, shake128_absorb of exactly one 168-byte block of zeros XORs nothing
 * into the state and then applies the permutation once. Setting ctx.state
 * first therefore gives state <- Keccak-f[1600](state). */
static void keccak_permute(uint64_t st[25])
{
    static const uint8_t zero[168] = {0};
    shake128_ctx_t ctx;
    shake128_init(&ctx);
    for (int i = 0; i < 25; i++) ctx.state[i] = st[i];
    shake128_absorb(&ctx, zero, sizeof zero);
    for (int i = 0; i < 25; i++) st[i] = ctx.state[i];
}

static void st_xor_byte(uint64_t st[25], size_t i, uint8_t b)
{
    st[i >> 3] ^= (uint64_t) b << (8u * (i & 7u));
}

static uint8_t st_byte(const uint64_t st[25], size_t i)
{
    return (uint8_t) (st[i >> 3] >> (8u * (i & 7u)));
}

void pay_keccak256(const uint8_t *data, size_t len, uint8_t out[PAY_HASH_LEN])
{
    const size_t rate = 136;
    uint64_t st[25];
    size_t off = 0;
    for (int i = 0; i < 25; i++) st[i] = 0;
    while (len - off >= rate) {
        for (size_t i = 0; i < rate; i++) st_xor_byte(st, i, data[off + i]);
        keccak_permute(st);
        off += rate;
    }
    size_t rem = len - off;
    for (size_t i = 0; i < rem; i++) st_xor_byte(st, i, data[off + i]);
    st_xor_byte(st, rem, 0x01);
    st_xor_byte(st, rate - 1, 0x80);
    keccak_permute(st);
    for (size_t i = 0; i < PAY_HASH_LEN; i++) out[i] = st_byte(st, i);
}

void pay_hbuf_init(pay_hbuf *h)
{
    h->len = 0;
    h->overflow = false;
}

void pay_hbuf_put(pay_hbuf *h, const void *p, size_t n)
{
    const uint8_t *b = (const uint8_t *) p;
    for (size_t i = 0; i < n; i++) {
        if (h->len >= sizeof h->buf) {
            h->overflow = true;
            return;
        }
        h->buf[h->len++] = b[i];
    }
}

void pay_hbuf_u64(pay_hbuf *h, uint64_t v)
{
    uint8_t b[8];
    for (int i = 0; i < 8; i++) b[i] = (uint8_t) (v >> (56 - 8 * i));
    pay_hbuf_put(h, b, 8);
}

void pay_hbuf_str(pay_hbuf *h, const char *s)
{
    size_t n = pay_strnlen(s, 4096);
    pay_hbuf_u64(h, n);
    pay_hbuf_put(h, s, n);
}

bool pay_hbuf_final(pay_hbuf *h, uint8_t out[PAY_HASH_LEN])
{
    pay_sha3_256(h->buf, h->len, out);
    return !h->overflow;
}

/* ===== UETR ===== */

void pay_uetr_from_random(const uint8_t rnd[16], char out[PAY_UETR_LEN + 1])
{
    static const char hx[] = "0123456789abcdef";
    uint8_t b[16];
    size_t o = 0;
    for (int i = 0; i < 16; i++) b[i] = rnd[i];
    b[6] = (uint8_t) ((b[6] & 0x0fu) | 0x40u);
    b[8] = (uint8_t) ((b[8] & 0x3fu) | 0x80u);
    for (int i = 0; i < 16; i++) {
        if (i == 4 || i == 6 || i == 8 || i == 10) out[o++] = '-';
        out[o++] = hx[b[i] >> 4];
        out[o++] = hx[b[i] & 15u];
    }
    out[o] = '\0';
}

static bool lhex(char c)
{
    return pay_is_digit(c) || (c >= 'a' && c <= 'f');
}

bool pay_uetr_valid(const char *s)
{
    if (!s || pay_strnlen(s, PAY_UETR_LEN + 1) != PAY_UETR_LEN) return false;
    for (uint32_t i = 0; i < PAY_UETR_LEN; i++) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (s[i] != '-') return false;
        } else if (!lhex(s[i])) {
            return false;
        }
    }
    if (s[14] != '4') return false;
    return s[19] == '8' || s[19] == '9' || s[19] == 'a' || s[19] == 'b';
}
