/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* vna_common.c — exact helpers for Vinea. See vna_common.h. */
#include "vna_common.h"
#include "../mlkem/keccak.h"

void vna_copy(void *dst, const void *src, uint32_t n)
{
    uint8_t *d = (uint8_t *) dst;
    const uint8_t *s = (const uint8_t *) src;
    if (d == s || n == 0) return;
    if (d < s) {
        for (uint32_t i = 0; i < n; i++) d[i] = s[i];
    } else {
        for (uint32_t i = n; i > 0; i--) d[i - 1] = s[i - 1];
    }
}

void vna_zero(void *dst, uint32_t n)
{
    uint8_t *d = (uint8_t *) dst;
    for (uint32_t i = 0; i < n; i++) d[i] = 0;
}

void vna_wipe(void *dst, uint32_t n)
{
    volatile uint8_t *d = (volatile uint8_t *) dst;
    for (uint32_t i = 0; i < n; i++) d[i] = 0;
}

bool vna_eq(const void *a, const void *b, uint32_t n)
{
    const uint8_t *x = (const uint8_t *) a, *y = (const uint8_t *) b;
    for (uint32_t i = 0; i < n; i++)
        if (x[i] != y[i]) return false;
    return true;
}

bool vna_ct_eq(const void *a, const void *b, uint32_t n)
{
    const uint8_t *x = (const uint8_t *) a, *y = (const uint8_t *) b;
    uint8_t acc = 0;
    for (uint32_t i = 0; i < n; i++) acc |= (uint8_t) (x[i] ^ y[i]);
    return acc == 0;
}

bool vna_id_eq(const vna_id_t *a, const vna_id_t *b)
{
    return vna_eq(a->b, b->b, VNA_ID_LEN);
}

bool vna_id_is_zero(const vna_id_t *a)
{
    for (uint32_t i = 0; i < VNA_ID_LEN; i++)
        if (a->b[i]) return false;
    return true;
}

int vna_id_closer(const vna_id_t *target, const vna_id_t *a, const vna_id_t *b)
{
    for (uint32_t i = 0; i < VNA_ID_LEN; i++) {
        uint8_t da = (uint8_t) (a->b[i] ^ target->b[i]);
        uint8_t db = (uint8_t) (b->b[i] ^ target->b[i]);
        if (da < db) return -1;
        if (da > db) return 1;
    }
    return 0;
}

uint32_t vna_leading_zero_bits(const uint8_t h[32])
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < 32; i++) {
        if (h[i] == 0) {
            n += 8;
            continue;
        }
        uint8_t v = h[i];
        while (!(v & 0x80u)) {
            n++;
            v = (uint8_t) (v << 1);
        }
        break;
    }
    return n;
}

int vna_bucket_index(const vna_id_t *self, const vna_id_t *other)
{
    uint8_t x[32];
    for (uint32_t i = 0; i < 32; i++) x[i] = (uint8_t) (self->b[i] ^ other->b[i]);
    uint32_t lz = vna_leading_zero_bits(x);
    if (lz >= 256) return -1;
    return (int) (255u - lz);
}

void vna_sha3(const uint8_t *data, uint32_t len, uint8_t out[32])
{
    static const uint8_t none[1] = {0};
    sha3_256(data ? data : none, len, out);
}

void vna_h2(const uint8_t a[32], const uint8_t b[32], uint8_t out[32])
{
    uint8_t buf[64];
    vna_copy(buf, a, 32);
    vna_copy(buf + 32, b, 32);
    sha3_256(buf, 64, out);
}

void vna_htag(const char *tag, const uint8_t *data, uint32_t len, uint8_t out[32])
{
    uint32_t tl = 0;
    while (tag[tl]) tl++;
    uint8_t ht[32], hd[32];
    sha3_256((const uint8_t *) tag, tl, ht);
    vna_sha3(data, len, hd);
    vna_h2(ht, hd, out);
}

void vna_drbg_seed(vna_drbg_t *d, const uint8_t *seed, uint32_t len)
{
    vna_htag("vinea/v2/drbg-seed", seed, len, d->key);
    d->ctr = 0;
    d->seeded = true;
}

void vna_drbg_gen(vna_drbg_t *d, uint8_t *out, uint32_t len)
{
    uint8_t in[41];
    uint8_t blk[64];
    uint32_t done = 0;
    if (!d->seeded) { /* fail closed: an unseeded generator yields nothing usable */
        vna_zero(out, len);
        return;
    }
    while (done < len) {
        vna_copy(in, d->key, 32);
        vna_put64(in + 32, d->ctr++);
        in[40] = 0x01; /* output block */
        shake256(in, 41, blk, 64);
        uint32_t take = len - done < 64 ? len - done : 64;
        vna_copy(out + done, blk, take);
        done += take;
    }
    vna_copy(in, d->key, 32);
    vna_put64(in + 32, d->ctr++);
    in[40] = 0x02; /* ratchet: earlier outputs cannot be recomputed from a later state */
    shake256(in, 41, d->key, 32);
    vna_wipe(blk, 64);
    vna_wipe(in, 41);
}

uint64_t vna_drbg_u64(vna_drbg_t *d)
{
    uint8_t b[8];
    vna_drbg_gen(d, b, 8);
    return vna_get64(b);
}

uint64_t vna_sat_add(uint64_t a, uint64_t b)
{
    return (a > UINT64_MAX - b) ? UINT64_MAX : a + b;
}

uint64_t vna_min64(uint64_t a, uint64_t b)
{
    return a < b ? a : b;
}

uint64_t vna_absdiff(uint64_t a, uint64_t b)
{
    return a > b ? a - b : b - a;
}
