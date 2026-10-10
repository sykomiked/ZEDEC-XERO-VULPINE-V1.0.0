/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* web4_rlp.c — Recursive Length Prefix encoding (Ethereum Yellow Paper,
 * appendix B), with a strict canonical decoder. */
#include "web4_web3.h"

void w4_rlp_init(w4_rlp_enc_t *e, uint8_t *buf, uint32_t cap)
{
    e->buf = buf;
    e->cap = cap;
    e->len = 0;
    e->depth = 0;
    e->err = (buf == NULL);
}

static uint32_t be_len(uint32_t v, uint8_t out[4])
{
    uint32_t n = 0;
    uint8_t t[4];
    w4_be32_put(t, v);
    uint32_t s = 0;
    while (s < 3 && t[s] == 0) s++;
    for (uint32_t i = s; i < 4; i++) out[n++] = t[i];
    return n;
}

static void put(w4_rlp_enc_t *e, const uint8_t *p, uint32_t n)
{
    if (e->err) return;
    if (n > e->cap - e->len) {
        e->err = true;
        return;
    }
    for (uint32_t i = 0; i < n; i++) e->buf[e->len++] = p[i];
}

static void header(w4_rlp_enc_t *e, uint8_t short_base, uint8_t long_base, uint32_t n)
{
    uint8_t h[5];
    if (n < 56) {
        h[0] = (uint8_t) (short_base + n);
        put(e, h, 1);
    } else {
        uint32_t k = be_len(n, h + 1);
        h[0] = (uint8_t) (long_base + k);
        put(e, h, k + 1);
    }
}

void w4_rlp_bytes(w4_rlp_enc_t *e, const uint8_t *p, uint32_t n)
{
    if (n == 1 && p[0] < 0x80) {
        put(e, p, 1);
        return;
    }
    header(e, 0x80, 0xb7, n);
    put(e, p, n);
}

void w4_rlp_str(w4_rlp_enc_t *e, const char *s)
{
    w4_rlp_bytes(e, (const uint8_t *) s, (uint32_t) w4_strnlen(s, 1u << 20));
}

void w4_rlp_u64(w4_rlp_enc_t *e, uint64_t v)
{
    uint8_t b[8];
    w4_be64_put(b, v);
    uint32_t s = 0;
    while (s < 8 && b[s] == 0) s++;
    w4_rlp_bytes(e, b + s, 8 - s);
}

void w4_rlp_u256(w4_rlp_enc_t *e, const w4_u256 *v)
{
    uint8_t b[32];
    uint32_t n = w4_u256_to_min_be(v, b);
    w4_rlp_bytes(e, b, n);
}

void w4_rlp_list_begin(w4_rlp_enc_t *e)
{
    if (e->depth >= W4_RLP_DEPTH) {
        e->err = true;
        return;
    }
    e->stack[e->depth++] = e->len;
}

void w4_rlp_list_end(w4_rlp_enc_t *e)
{
    if (e->depth == 0) {
        e->err = true;
        return;
    }
    if (e->err) {
        e->depth--;
        return;
    }
    uint32_t s = e->stack[--e->depth];
    uint32_t n = e->len - s;
    uint8_t h[5];
    uint32_t hl;
    if (n < 56) {
        h[0] = (uint8_t) (0xc0 + n);
        hl = 1;
    } else {
        uint32_t k = be_len(n, h + 1);
        h[0] = (uint8_t) (0xf7 + k);
        hl = k + 1;
    }
    if (hl > e->cap - e->len) {
        e->err = true;
        return;
    }
    w4_memmove(e->buf + s + hl, e->buf + s, n);
    for (uint32_t i = 0; i < hl; i++) e->buf[s + i] = h[i];
    e->len += hl;
}

int32_t w4_rlp_finish(const w4_rlp_enc_t *e)
{
    if (e->err) return W4_ERR_SPACE;
    if (e->depth) return W4_ERR_STATE;
    return (int32_t) e->len;
}

int w4_rlp_decode(const uint8_t *buf, uint32_t len, w4_rlp_item_t *it)
{
    if (!buf || len == 0) return W4_ERR_PARSE;
    uint8_t b = buf[0];
    uint32_t hl, pl;
    bool list = false;
    if (b < 0x80) {
        it->is_list = false;
        it->p = buf;
        it->len = 1;
        it->total = 1;
        return W4_OK;
    }
    if (b <= 0xb7) {
        hl = 1;
        pl = b - 0x80u;
        if (pl == 1 && (len < 2 || buf[1] < 0x80)) return W4_ERR_PARSE; /* must be bare byte */
    } else if (b <= 0xbf || (b >= 0xf8)) {
        uint32_t k = (b <= 0xbf) ? b - 0xb7u : b - 0xf7u;
        list = b >= 0xf8;
        if (k > 4 || len < 1 + k || buf[1] == 0) return W4_ERR_PARSE; /* no leading zero */
        pl = 0;
        for (uint32_t i = 0; i < k; i++) pl = (pl << 8) | buf[1 + i];
        if (pl < 56) return W4_ERR_PARSE; /* should have used the short form */
        hl = 1 + k;
    } else {
        hl = 1;
        pl = b - 0xc0u;
        list = true;
    }
    if (pl > len - hl) return W4_ERR_PARSE;
    it->is_list = list;
    it->p = buf + hl;
    it->len = pl;
    it->total = hl + pl;
    return W4_OK;
}

int w4_rlp_next(const w4_rlp_item_t *list, uint32_t *off, w4_rlp_item_t *it)
{
    if (!list->is_list) return W4_ERR_PARSE;
    if (*off >= list->len) return W4_ERR_NOTFOUND;
    int r = w4_rlp_decode(list->p + *off, list->len - *off, it);
    if (r) return r;
    *off += it->total;
    return W4_OK;
}

int w4_rlp_as_u64(const w4_rlp_item_t *it, uint64_t *v)
{
    if (it->is_list || it->len > 8 || (it->len > 0 && it->p[0] == 0)) return W4_ERR_PARSE;
    uint64_t r = 0;
    for (uint32_t i = 0; i < it->len; i++) r = (r << 8) | it->p[i];
    *v = r;
    return W4_OK;
}

int w4_rlp_as_u256(const w4_rlp_item_t *it, w4_u256 *v)
{
    if (it->is_list || it->len > 32 || (it->len > 0 && it->p[0] == 0)) return W4_ERR_PARSE;
    return w4_u256_from_be(v, it->p, it->len) ? W4_OK : W4_ERR_PARSE;
}
