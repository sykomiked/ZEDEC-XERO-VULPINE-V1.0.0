/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* prov_util.c — bounded byte helpers, checked arithmetic and the SHA3-256
 * hash builder for kernel/src/provider. Wide arithmetic is pay_muldiv
 * (kernel/src/pay/pay_util.c, 128/64 long division over zt_udiv64); SHA3 is
 * kernel/src/mlkem/keccak.c. No libc. */
#include "prov.h"
#include "../pay/pay_util.h"
#include "../mlkem/keccak.h"

void prov_memset(void *p, uint8_t v, size_t n)
{
    volatile uint8_t *d = (volatile uint8_t *) p;
    for (size_t i = 0; i < n; i++) d[i] = v;
}

void prov_memcpy(void *d, const void *s, size_t n)
{
    volatile uint8_t *dd = (volatile uint8_t *) d;
    const uint8_t *ss = (const uint8_t *) s;
    for (size_t i = 0; i < n; i++) dd[i] = ss[i];
}

bool prov_memeq(const void *a, const void *b, size_t n)
{
    const uint8_t *x = (const uint8_t *) a, *y = (const uint8_t *) b;
    for (size_t i = 0; i < n; i++)
        if (x[i] != y[i]) return false;
    return true;
}

bool prov_ct_eq(const void *a, const void *b, size_t n)
{
    const volatile uint8_t *x = (const volatile uint8_t *) a;
    const volatile uint8_t *y = (const volatile uint8_t *) b;
    uint8_t acc = 0;
    for (size_t i = 0; i < n; i++) acc |= (uint8_t) (x[i] ^ y[i]);
    return acc == 0;
}

size_t prov_strnlen(const char *s, size_t max)
{
    size_t i = 0;
    if (!s) return 0;
    while (i < max && s[i]) i++;
    return i;
}

bool prov_streq(const char *a, const char *b)
{
    if (!a || !b) return false;
    while (*a && *a == *b) a++, b++;
    return *a == *b;
}

bool prov_strlcpy(char *dst, const char *src, size_t cap)
{
    size_t i = 0;
    if (!dst || cap == 0) return false;
    if (!src) src = "";
    for (; i + 1 < cap && src[i]; i++) dst[i] = src[i];
    bool fit = src[i] == 0;
    for (size_t k = i; k < cap; k++) dst[k] = 0;
    return fit;
}

void prov_le32_put(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++) p[i] = (uint8_t) (v >> (8 * i));
}

void prov_le64_put(uint8_t *p, uint64_t v)
{
    for (int i = 0; i < 8; i++) p[i] = (uint8_t) (v >> (8 * i));
}

uint32_t prov_le32_get(const uint8_t *p)
{
    uint32_t v = 0;
    for (int i = 3; i >= 0; i--) v = (v << 8) | p[i];
    return v;
}

uint64_t prov_le64_get(const uint8_t *p)
{
    uint64_t v = 0;
    for (int i = 7; i >= 0; i--) v = (v << 8) | p[i];
    return v;
}

bool prov_mul_ok(uint64_t a, uint64_t b, uint64_t *out)
{
    pay_u128 p = pay_mul64(a, b);
    if (p.hi) return false;
    *out = p.lo;
    return true;
}

bool prov_muldiv(uint64_t a, uint64_t b, uint64_t c, uint64_t *out)
{
    return pay_muldiv(a, b, c, out, 0);
}

void prov_sha3(const uint8_t *p, size_t n, uint8_t out[PROV_HASH_LEN])
{
    sha3_256(p, n, out);
}

void prov_hb_init(prov_hb_t *h, const char *domain)
{
    h->len = 0;
    h->err = false;
    prov_hb_str(h, domain, 64);
}

void prov_hb_put(prov_hb_t *h, const void *p, uint32_t n)
{
    if (h->err || n > PROV_HB_CAP - h->len) {
        h->err = true;
        return;
    }
    prov_memcpy(h->buf + h->len, p, n);
    h->len += n;
}

void prov_hb_u8(prov_hb_t *h, uint8_t v)
{
    prov_hb_put(h, &v, 1);
}

void prov_hb_u32(prov_hb_t *h, uint32_t v)
{
    uint8_t b[4];
    prov_le32_put(b, v);
    prov_hb_put(h, b, 4);
}

void prov_hb_u64(prov_hb_t *h, uint64_t v)
{
    uint8_t b[8];
    prov_le64_put(b, v);
    prov_hb_put(h, b, 8);
}

void prov_hb_str(prov_hb_t *h, const char *s, uint32_t max)
{
    uint32_t n = (uint32_t) prov_strnlen(s, max);
    prov_hb_u32(h, n);
    if (n) prov_hb_put(h, s, n);
}

bool prov_hb_final(prov_hb_t *h, uint8_t out[PROV_HASH_LEN])
{
    if (h->err) {
        prov_memset(out, 0, PROV_HASH_LEN);
        return false;
    }
    sha3_256(h->buf, h->len, out);
    return true;
}
