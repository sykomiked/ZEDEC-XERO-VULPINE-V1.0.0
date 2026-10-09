/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* web4_util.c — see web4_util.h. */
#include "web4_util.h"
#include "../mlkem/keccak.h"
#include "../robin_debanks/sha256.h"

/* ===== Bytes and strings ===== */
void w4_memset(void *p, uint8_t v, size_t n)
{
    volatile uint8_t *d = (volatile uint8_t *) p;
    for (size_t i = 0; i < n; i++) d[i] = v;
}

void w4_memcpy(void *dst, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *) dst;
    const uint8_t *s = (const uint8_t *) src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
}

void w4_memmove(void *dst, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *) dst;
    const uint8_t *s = (const uint8_t *) src;
    if (d == s || n == 0) return;
    if (d < s) {
        for (size_t i = 0; i < n; i++) d[i] = s[i];
    } else {
        for (size_t i = n; i > 0; i--) d[i - 1] = s[i - 1];
    }
}

bool w4_memeq(const void *a, const void *b, size_t n)
{
    const uint8_t *x = (const uint8_t *) a;
    const uint8_t *y = (const uint8_t *) b;
    for (size_t i = 0; i < n; i++)
        if (x[i] != y[i]) return false;
    return true;
}

bool w4_ct_eq(const void *a, const void *b, size_t n)
{
    const volatile uint8_t *x = (const volatile uint8_t *) a;
    const volatile uint8_t *y = (const volatile uint8_t *) b;
    uint8_t acc = 0;
    for (size_t i = 0; i < n; i++) acc |= (uint8_t) (x[i] ^ y[i]);
    return acc == 0;
}

size_t w4_strnlen(const char *s, size_t max)
{
    size_t n = 0;
    if (!s) return 0;
    while (n < max && s[n]) n++;
    return n;
}

bool w4_streq(const char *a, const char *b)
{
    if (!a || !b) return false;
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

bool w4_strneq(const char *a, size_t an, const char *b)
{
    if (!a || !b) return false;
    for (size_t i = 0; i < an; i++)
        if (b[i] == 0 || a[i] != b[i]) return false;
    return b[an] == 0;
}

char w4_lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char) (c + 32) : c;
}

bool w4_casecmp_eq(const char *a, size_t an, const char *b, size_t bn)
{
    if (an != bn) return false;
    for (size_t i = 0; i < an; i++)
        if (w4_lower(a[i]) != w4_lower(b[i])) return false;
    return true;
}

bool w4_strlcpy(char *dst, const char *src, size_t cap)
{
    if (!dst || cap == 0) return false;
    size_t i = 0;
    if (src)
        for (; i + 1 < cap && src[i]; i++) dst[i] = src[i];
    dst[i] = 0;
    return !src || src[i] == 0;
}

bool w4_strlcpyn(char *dst, size_t cap, const char *src, size_t n)
{
    if (!dst || cap == 0) return false;
    if (n + 1 > cap) {
        dst[0] = 0;
        return false;
    }
    for (size_t i = 0; i < n; i++) dst[i] = src[i];
    dst[n] = 0;
    return true;
}

bool w4_is_digit(char c)
{
    return c >= '0' && c <= '9';
}

bool w4_is_alpha(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

int w4_hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool w4_is_hex(char c)
{
    return w4_hexval(c) >= 0;
}

void w4_be32_put(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++) p[i] = (uint8_t) (v >> (24 - 8 * i));
}

void w4_be64_put(uint8_t *p, uint64_t v)
{
    for (int i = 0; i < 8; i++) p[i] = (uint8_t) (v >> (56 - 8 * i));
}

uint32_t w4_be32_get(const uint8_t *p)
{
    return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8) | p[3];
}

uint64_t w4_be64_get(const uint8_t *p)
{
    return ((uint64_t) w4_be32_get(p) << 32) | w4_be32_get(p + 4);
}

void w4_le16_put(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t) v;
    p[1] = (uint8_t) (v >> 8);
}

void w4_le32_put(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++) p[i] = (uint8_t) (v >> (8 * i));
}

void w4_le64_put(uint8_t *p, uint64_t v)
{
    for (int i = 0; i < 8; i++) p[i] = (uint8_t) (v >> (8 * i));
}

uint16_t w4_le16_get(const uint8_t *p)
{
    return (uint16_t) (p[0] | (p[1] << 8));
}

uint32_t w4_le32_get(const uint8_t *p)
{
    return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) |
           ((uint32_t) p[3] << 24);
}

uint64_t w4_le64_get(const uint8_t *p)
{
    return (uint64_t) w4_le32_get(p) | ((uint64_t) w4_le32_get(p + 4) << 32);
}

uint64_t w4_udiv64(uint64_t n, uint64_t d, uint64_t *rem)
{
    if (d == 0) {
        if (rem) *rem = n;
        return 0;
    }
    uint64_t q = 0, r = 0;
    for (int i = 63; i >= 0; i--) {
        uint64_t top = r >> 63;
        r = (r << 1) | ((n >> i) & 1u);
        if (top || r >= d) {
            r -= d;
            q |= (uint64_t) 1 << i;
        }
    }
    if (rem) *rem = r;
    return q;
}

/* ===== Writer ===== */
void w4_w_init(w4_w *w, void *buf, uint32_t cap)
{
    w->buf = (uint8_t *) buf;
    w->cap = cap;
    w->len = 0;
    w->err = (buf == NULL && cap != 0);
}

void w4_w_byte(w4_w *w, uint8_t b)
{
    if (w->err) return;
    if (w->len >= w->cap) {
        w->err = true;
        return;
    }
    w->buf[w->len++] = b;
}

void w4_w_bytes(w4_w *w, const void *p, uint32_t n)
{
    const uint8_t *b = (const uint8_t *) p;
    if (w->err) return;
    if (n > w->cap - w->len) {
        w->err = true;
        return;
    }
    for (uint32_t i = 0; i < n; i++) w->buf[w->len++] = b[i];
}

void w4_w_str(w4_w *w, const char *s)
{
    if (!s) return;
    while (*s) w4_w_byte(w, (uint8_t) *s++);
}

void w4_w_strn(w4_w *w, const char *s, uint32_t n)
{
    w4_w_bytes(w, s, n);
}

void w4_w_u64(w4_w *w, uint64_t v)
{
    char tmp[20];
    int n = 0;
    do {
        uint64_t r;
        v = w4_udiv64(v, 10, &r);
        tmp[n++] = (char) ('0' + (int) r);
    } while (v);
    while (n) w4_w_byte(w, (uint8_t) tmp[--n]);
}

void w4_w_i64(w4_w *w, int64_t v)
{
    if (v < 0) {
        w4_w_byte(w, '-');
        w4_w_u64(w, (uint64_t) 0 - (uint64_t) v);
    } else {
        w4_w_u64(w, (uint64_t) v);
    }
}

static const char W4_HEX[] = "0123456789abcdef";

void w4_w_hex(w4_w *w, const uint8_t *p, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        w4_w_byte(w, (uint8_t) W4_HEX[p[i] >> 4]);
        w4_w_byte(w, (uint8_t) W4_HEX[p[i] & 15]);
    }
}

int32_t w4_w_cstr(w4_w *w)
{
    if (w->err || w->len >= w->cap) {
        if (w->cap) w->buf[w->len < w->cap ? w->len : w->cap - 1] = 0;
        w->err = true;
        return W4_ERR_SPACE;
    }
    w->buf[w->len] = 0;
    return (int32_t) w->len;
}

int32_t w4_w_done(const w4_w *w)
{
    return w->err ? W4_ERR_SPACE : (int32_t) w->len;
}

/* ===== Hex ===== */
int32_t w4_hex_encode(const uint8_t *in, uint32_t n, char *out, uint32_t cap)
{
    if (cap < 2u * n + 1u) return W4_ERR_SPACE;
    for (uint32_t i = 0; i < n; i++) {
        out[2 * i] = W4_HEX[in[i] >> 4];
        out[2 * i + 1] = W4_HEX[in[i] & 15];
    }
    out[2 * n] = 0;
    return (int32_t) (2 * n);
}

int32_t w4_hex_decode(const char *in, uint32_t len, uint8_t *out, uint32_t cap)
{
    if (len & 1u) return W4_ERR_PARSE;
    if (len / 2 > cap) return W4_ERR_SPACE;
    for (uint32_t i = 0; i < len; i += 2) {
        int hi = w4_hexval(in[i]), lo = w4_hexval(in[i + 1]);
        if (hi < 0 || lo < 0) return W4_ERR_PARSE;
        out[i / 2] = (uint8_t) ((hi << 4) | lo);
    }
    return (int32_t) (len / 2);
}

/* ===== Base64 ===== */
static const char W4_B64URL[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
static const char W4_B64STD[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static int32_t b64_encode(const char *alpha, bool pad, const uint8_t *in, uint32_t n, char *out,
                          uint32_t cap)
{
    uint32_t full = n / 3, rem = n - full * 3;
    uint32_t need = full * 4 + (rem ? (pad ? 4 : rem + 1) : 0);
    if (cap < need + 1) return W4_ERR_SPACE;
    uint32_t o = 0, i = 0;
    for (uint32_t k = 0; k < full; k++, i += 3) {
        uint32_t v = ((uint32_t) in[i] << 16) | ((uint32_t) in[i + 1] << 8) | in[i + 2];
        out[o++] = alpha[(v >> 18) & 63];
        out[o++] = alpha[(v >> 12) & 63];
        out[o++] = alpha[(v >> 6) & 63];
        out[o++] = alpha[v & 63];
    }
    if (rem) {
        uint32_t v = (uint32_t) in[i] << 16;
        if (rem == 2) v |= (uint32_t) in[i + 1] << 8;
        out[o++] = alpha[(v >> 18) & 63];
        out[o++] = alpha[(v >> 12) & 63];
        if (rem == 2) out[o++] = alpha[(v >> 6) & 63];
        if (pad) {
            if (rem == 1) out[o++] = '=';
            out[o++] = '=';
        }
    }
    out[o] = 0;
    return (int32_t) o;
}

int32_t w4_b64url_encode(const uint8_t *in, uint32_t n, char *out, uint32_t cap)
{
    return b64_encode(W4_B64URL, false, in, n, out, cap);
}

int32_t w4_b64_encode(const uint8_t *in, uint32_t n, char *out, uint32_t cap)
{
    return b64_encode(W4_B64STD, true, in, n, out, cap);
}

static int b64url_val(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '-') return 62;
    if (c == '_') return 63;
    return -1;
}

int32_t w4_b64url_decode(const char *in, uint32_t len, uint8_t *out, uint32_t cap)
{
    uint32_t rem = len & 3u;
    if (rem == 1) return W4_ERR_PARSE;
    uint32_t outlen = (len / 4) * 3 + (rem ? rem - 1 : 0);
    if (outlen > cap) return W4_ERR_SPACE;
    uint32_t o = 0, acc = 0, bits = 0;
    for (uint32_t i = 0; i < len; i++) {
        int v = b64url_val(in[i]);
        if (v < 0) return W4_ERR_PARSE;
        acc = (acc << 6) | (uint32_t) v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out[o++] = (uint8_t) (acc >> bits);
            acc &= (1u << bits) - 1u;
        }
    }
    if (acc != 0) return W4_ERR_PARSE; /* non-zero unused bits: not canonical */
    return (int32_t) o;
}

/* ===== Hashes ===== */
void w4_sha256(const uint8_t *data, size_t len, uint8_t out[W4_HASH_LEN])
{
    sha256(data, len, out);
}

void w4_sha3_256(const uint8_t *data, size_t len, uint8_t out[W4_HASH_LEN])
{
    sha3_256(data, len, out);
}

void w4_hmac_sha256(const uint8_t *key, uint32_t key_len, const uint8_t *msg, uint32_t msg_len,
                    uint8_t out[W4_HASH_LEN])
{
    const uint8_t *parts[1] = {msg};
    uint32_t lens[1] = {msg_len};
    w4_hmac_sha256_parts(key, key_len, parts, lens, 1, out);
}

void w4_hmac_sha256_parts(const uint8_t *key, uint32_t key_len, const uint8_t *const *parts,
                          const uint32_t *lens, uint32_t n, uint8_t out[W4_HASH_LEN])
{
    uint8_t k[64], pad[64], inner[W4_HASH_LEN];
    sha256_ctx_t c;
    w4_memset(k, 0, sizeof k);
    if (key_len > 64)
        sha256(key, key_len, k);
    else
        w4_memcpy(k, key, key_len);
    for (int i = 0; i < 64; i++) pad[i] = (uint8_t) (k[i] ^ 0x36);
    sha256_init(&c);
    sha256_update(&c, pad, 64);
    for (uint32_t i = 0; i < n; i++)
        if (lens[i]) sha256_update(&c, parts[i], lens[i]);
    sha256_final(&c, inner);
    for (int i = 0; i < 64; i++) pad[i] = (uint8_t) (k[i] ^ 0x5c);
    sha256_init(&c);
    sha256_update(&c, pad, 64);
    sha256_update(&c, inner, W4_HASH_LEN);
    sha256_final(&c, out);
    w4_memset(k, 0, sizeof k);
    w4_memset(pad, 0, sizeof pad);
}

/* keccak.h exposes no bare permutation. Absorbing exactly one 168-byte block
 * of zeros into a SHAKE128 context XORs nothing and runs Keccak-f[1600] once
 * on the state we load, so it is the permutation. (Same technique as
 * kernel/src/pay/pay_util.c, kept local so web4 does not depend on pay.) */
static void keccak_permute(uint64_t st[25])
{
    static const uint8_t zero[168] = {0};
    shake128_ctx_t ctx;
    shake128_init(&ctx);
    for (int i = 0; i < 25; i++) ctx.state[i] = st[i];
    shake128_absorb(&ctx, zero, sizeof zero);
    for (int i = 0; i < 25; i++) st[i] = ctx.state[i];
}

static void st_xor(uint64_t st[25], size_t i, uint8_t b)
{
    st[i >> 3] ^= (uint64_t) b << (8u * (i & 7u));
}

void w4_keccak256(const uint8_t *data, size_t len, uint8_t out[W4_HASH_LEN])
{
    const size_t rate = 136;
    uint64_t st[25];
    size_t off = 0;
    for (int i = 0; i < 25; i++) st[i] = 0;
    while (len - off >= rate) {
        for (size_t i = 0; i < rate; i++) st_xor(st, i, data[off + i]);
        keccak_permute(st);
        off += rate;
    }
    size_t rem = len - off;
    for (size_t i = 0; i < rem; i++) st_xor(st, i, data[off + i]);
    st_xor(st, rem, 0x01);
    st_xor(st, rate - 1, 0x80);
    keccak_permute(st);
    for (size_t i = 0; i < W4_HASH_LEN; i++) out[i] = (uint8_t) (st[i >> 3] >> (8u * (i & 7u)));
}

void w4_hb_init(w4_hb *h)
{
    h->len = 0;
    h->overflow = false;
}

void w4_hb_put(w4_hb *h, const void *p, uint32_t n)
{
    if (h->overflow) return;
    if (n > sizeof h->buf - h->len) {
        h->overflow = true;
        return;
    }
    w4_memcpy(h->buf + h->len, p, n);
    h->len += n;
}

void w4_hb_u8(w4_hb *h, uint8_t v)
{
    w4_hb_put(h, &v, 1);
}

void w4_hb_u32(w4_hb *h, uint32_t v)
{
    uint8_t b[4];
    w4_le32_put(b, v);
    w4_hb_put(h, b, 4);
}

void w4_hb_u64(w4_hb *h, uint64_t v)
{
    uint8_t b[8];
    w4_le64_put(b, v);
    w4_hb_put(h, b, 8);
}

void w4_hb_str(w4_hb *h, const char *s, uint32_t max)
{
    uint32_t n = (uint32_t) w4_strnlen(s, max);
    w4_hb_u32(h, n);
    w4_hb_put(h, s, n);
}

bool w4_hb_sha3(w4_hb *h, uint8_t out[W4_HASH_LEN])
{
    if (h->overflow) {
        w4_memset(out, 0, W4_HASH_LEN);
        return false;
    }
    sha3_256(h->buf, h->len, out);
    return true;
}

/* ===== u256 ===== */
void w4_u256_zero(w4_u256 *a)
{
    for (int i = 0; i < 8; i++) a->w[i] = 0;
}

void w4_u256_from_u64(w4_u256 *a, uint64_t v)
{
    w4_u256_zero(a);
    a->w[0] = (uint32_t) v;
    a->w[1] = (uint32_t) (v >> 32);
}

bool w4_u256_is_zero(const w4_u256 *a)
{
    uint32_t acc = 0;
    for (int i = 0; i < 8; i++) acc |= a->w[i];
    return acc == 0;
}

bool w4_u256_to_u64(const w4_u256 *a, uint64_t *v)
{
    for (int i = 2; i < 8; i++)
        if (a->w[i]) return false;
    *v = ((uint64_t) a->w[1] << 32) | a->w[0];
    return true;
}

int w4_u256_cmp(const w4_u256 *a, const w4_u256 *b)
{
    for (int i = 7; i >= 0; i--) {
        if (a->w[i] < b->w[i]) return -1;
        if (a->w[i] > b->w[i]) return 1;
    }
    return 0;
}

bool w4_u256_add(w4_u256 *r, const w4_u256 *a, const w4_u256 *b)
{
    uint64_t c = 0;
    for (int i = 0; i < 8; i++) {
        c += (uint64_t) a->w[i] + b->w[i];
        r->w[i] = (uint32_t) c;
        c >>= 32;
    }
    return c == 0;
}

bool w4_u256_sub(w4_u256 *r, const w4_u256 *a, const w4_u256 *b)
{
    uint64_t br = 0;
    w4_u256 t;
    for (int i = 0; i < 8; i++) {
        uint64_t d = (uint64_t) a->w[i] - b->w[i] - br;
        t.w[i] = (uint32_t) d;
        br = (d >> 63) & 1u;
    }
    if (br) return false;
    *r = t;
    return true;
}

bool w4_u256_mul_u32(w4_u256 *r, const w4_u256 *a, uint32_t m)
{
    uint64_t c = 0;
    w4_u256 t;
    for (int i = 0; i < 8; i++) {
        c += (uint64_t) a->w[i] * m;
        t.w[i] = (uint32_t) c;
        c >>= 32;
    }
    if (c) return false;
    *r = t;
    return true;
}

bool w4_u256_divmod_u32(w4_u256 *r, const w4_u256 *a, uint32_t d, uint32_t *rem)
{
    if (d == 0) return false;
    w4_u256 q;
    uint64_t rr = 0;
    w4_u256_zero(&q);
    for (int i = 255; i >= 0; i--) {
        rr = (rr << 1) | ((a->w[i >> 5] >> (i & 31)) & 1u);
        if (rr >= d) {
            rr -= d;
            q.w[i >> 5] |= 1u << (i & 31);
        }
    }
    if (r) *r = q;
    if (rem) *rem = (uint32_t) rr;
    return true;
}

bool w4_u256_from_be(w4_u256 *a, const uint8_t *p, uint32_t len)
{
    if (len > 32) return false;
    w4_u256_zero(a);
    for (uint32_t i = 0; i < len; i++) {
        uint32_t bit = (len - 1 - i) * 8; /* bit position of this byte */
        a->w[bit >> 5] |= (uint32_t) p[i] << (bit & 31);
    }
    return true;
}

void w4_u256_to_be(const w4_u256 *a, uint8_t out[32])
{
    for (int i = 0; i < 8; i++) w4_be32_put(out + 4 * (7 - i), a->w[i]);
}

uint32_t w4_u256_to_min_be(const w4_u256 *a, uint8_t out[32])
{
    uint8_t full[32];
    uint32_t s = 0;
    w4_u256_to_be(a, full);
    while (s < 32 && full[s] == 0) s++;
    for (uint32_t i = s; i < 32; i++) out[i - s] = full[i];
    return 32 - s;
}

bool w4_u256_from_dec(w4_u256 *a, const char *s, uint32_t len)
{
    if (len == 0 || len > 78) return false;
    if (s[0] == '0' && len > 1) return false;
    w4_u256 t, d;
    w4_u256_zero(&t);
    for (uint32_t i = 0; i < len; i++) {
        if (!w4_is_digit(s[i])) return false;
        if (!w4_u256_mul_u32(&t, &t, 10)) return false;
        w4_u256_from_u64(&d, (uint64_t) (s[i] - '0'));
        if (!w4_u256_add(&t, &t, &d)) return false;
    }
    *a = t;
    return true;
}

void w4_w_u256_dec(w4_w *w, const w4_u256 *a)
{
    char tmp[80];
    int n = 0;
    w4_u256 t = *a;
    do {
        uint32_t r;
        w4_u256_divmod_u32(&t, &t, 10, &r);
        tmp[n++] = (char) ('0' + (int) r);
    } while (!w4_u256_is_zero(&t));
    while (n) w4_w_byte(w, (uint8_t) tmp[--n]);
}

void w4_w_u256_qty(w4_w *w, const w4_u256 *a)
{
    uint8_t be[32];
    w4_u256_to_be(a, be);
    w4_w_str(w, "0x");
    bool started = false;
    for (int i = 0; i < 64; i++) {
        uint8_t nib = (uint8_t) ((i & 1) ? (be[i >> 1] & 15) : (be[i >> 1] >> 4));
        if (!started && nib == 0 && i != 63) continue;
        started = true;
        w4_w_byte(w, (uint8_t) W4_HEX[nib]);
    }
}

bool w4_u256_from_qty(w4_u256 *a, const char *s, uint32_t len)
{
    if (len < 3 || s[0] != '0' || (s[1] != 'x' && s[1] != 'X')) return false;
    s += 2;
    len -= 2;
    if (len > 64) return false;
    if (s[0] == '0' && len > 1) return false;
    w4_u256 t;
    w4_u256_zero(&t);
    for (uint32_t i = 0; i < len; i++) {
        int v = w4_hexval(s[i]);
        if (v < 0) return false;
        uint32_t bit = (len - 1 - i) * 4;
        t.w[bit >> 5] |= (uint32_t) v << (bit & 31);
    }
    *a = t;
    return true;
}

int w4_u256_rescale(w4_u256 *r, const w4_u256 *a, uint8_t from_dec, uint8_t to_dec)
{
    w4_u256 t = *a;
    if (from_dec > 77 || to_dec > 77) return W4_ERR_RANGE;
    if (to_dec >= from_dec) {
        for (uint32_t i = from_dec; i < to_dec; i++)
            if (!w4_u256_mul_u32(&t, &t, 10)) return W4_ERR_RANGE;
    } else {
        for (uint32_t i = to_dec; i < from_dec; i++) {
            uint32_t rem;
            w4_u256_divmod_u32(&t, &t, 10, &rem);
            if (rem) return W4_ERR_RANGE; /* would lose digits: refuse, never round */
        }
    }
    *r = t;
    return W4_OK;
}
