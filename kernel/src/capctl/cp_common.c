/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* cp_common.c — see cp_common.h (M1-M3). Freestanding: no libc. */
#include "cp_common.h"
#include "../robin_debanks/sha256.h"

void cp_put(cp_w_t *w, const void *src, size_t n)
{
    if (w->bad || n > w->cap - w->len) {
        w->bad = true;
        return;
    }
    cp_copy(w->p + w->len, src, n);
    w->len += n;
}

void cp_put8(cp_w_t *w, uint8_t v)
{
    cp_put(w, &v, 1);
}

void cp_put16(cp_w_t *w, uint16_t v)
{
    uint8_t b[2] = {(uint8_t) v, (uint8_t) (v >> 8)};
    cp_put(w, b, 2);
}

void cp_put32(cp_w_t *w, uint32_t v)
{
    uint8_t b[4];
    for (unsigned i = 0; i < 4; i++) b[i] = (uint8_t) (v >> (8 * i));
    cp_put(w, b, 4);
}

void cp_put64(cp_w_t *w, uint64_t v)
{
    uint8_t b[8];
    for (unsigned i = 0; i < 8; i++) b[i] = (uint8_t) (v >> (8 * i));
    cp_put(w, b, 8);
}

void cp_get(cp_r_t *r, void *dst, size_t n)
{
    if (r->bad || n > r->len - r->off) {
        r->bad = true;
        cp_zero(dst, n);
        return;
    }
    cp_copy(dst, r->p + r->off, n);
    r->off += n;
}

uint8_t cp_get8(cp_r_t *r)
{
    uint8_t v;
    cp_get(r, &v, 1);
    return v;
}

uint16_t cp_get16(cp_r_t *r)
{
    uint8_t b[2];
    cp_get(r, b, 2);
    return (uint16_t) (b[0] | (b[1] << 8));
}

uint32_t cp_get32(cp_r_t *r)
{
    uint8_t b[4];
    cp_get(r, b, 4);
    uint32_t v = 0;
    for (unsigned i = 0; i < 4; i++) v |= (uint32_t) b[i] << (8 * i);
    return v;
}

uint64_t cp_get64(cp_r_t *r)
{
    uint8_t b[8];
    cp_get(r, b, 8);
    uint64_t v = 0;
    for (unsigned i = 0; i < 8; i++) v |= (uint64_t) b[i] << (8 * i);
    return v;
}

uint64_t cp_udivmod64(uint64_t n, uint64_t d, uint64_t *rem)
{
    if (d == 0) {
        if (rem) *rem = 0;
        return 0;
    }
    uint64_t q = 0, r = 0;
    for (int i = 63; i >= 0; i--) {
        /* Before the shift r <= the top 63 - i bits of n, so it is below
         * 2^63 and the shift cannot overflow. */
        r = (r << 1) | ((n >> i) & 1u);
        if (r >= d) {
            r -= d;
            q |= (uint64_t) 1 << i;
        }
    }
    if (rem) *rem = r;
    return q;
}

bool cp_cc_ok(const char cc[2], bool wild)
{
    if (wild && cc[0] == '*' && cc[1] == '*') return true;
    return cc[0] >= 'A' && cc[0] <= 'Z' && cc[1] >= 'A' && cc[1] <= 'Z';
}

static bool region_ok(const char r[CP_REGION_LEN])
{
    bool end = false;
    for (unsigned i = 0; i < CP_REGION_LEN; i++) {
        char c = r[i];
        if (c == 0) {
            end = true;
            continue;
        }
        if (end) return false; /* a character after the zero padding */
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) return false;
    }
    return true;
}

void cp_hdr_put(cp_w_t *w, const cp_hdr_t *h)
{
    cp_put32(w, CP_MAGIC);
    cp_put16(w, CP_FORMAT);
    cp_put16(w, h->kind);
    cp_put(w, h->country, 2);
    cp_put(w, h->region, CP_REGION_LEN);
    cp_put32(w, h->version);
    cp_put64(w, h->not_before);
    cp_put64(w, h->not_after);
    cp_put(w, h->key_id, CP_KEYID_LEN);
}

bool cp_hdr_get(cp_r_t *r, cp_hdr_t *h, bool wild)
{
    uint32_t magic = cp_get32(r);
    uint16_t fmt = cp_get16(r);
    h->kind = cp_get16(r);
    cp_get(r, h->country, 2);
    cp_get(r, h->region, CP_REGION_LEN);
    h->version = cp_get32(r);
    h->not_before = cp_get64(r);
    h->not_after = cp_get64(r);
    cp_get(r, h->key_id, CP_KEYID_LEN);
    if (r->bad || magic != CP_MAGIC || fmt != CP_FORMAT) return false;
    if (!cp_cc_ok(h->country, wild) || !region_ok(h->region)) return false;
    if (h->not_after < h->not_before || h->version == 0) return false;
    return true;
}

void cp_manifest_msg(const uint8_t *manifest, size_t len, uint8_t out[CP_MSG_LEN])
{
    cp_copy(out, CP_DOMAIN, CP_DOMAIN_LEN);
    sha256(manifest, len, out + CP_DOMAIN_LEN);
}

bool cp_manifest_verify(cp_verify_fn verify, void *vctx, const cp_hdr_t *h, const uint8_t *manifest,
                        size_t len, const uint8_t *sig, size_t sig_len)
{
    if (!verify || !sig || sig_len == 0) return false;
    uint8_t msg[CP_MSG_LEN];
    cp_manifest_msg(manifest, len, msg);
    return verify(vctx, h, msg, sizeof msg, sig, sig_len);
}
