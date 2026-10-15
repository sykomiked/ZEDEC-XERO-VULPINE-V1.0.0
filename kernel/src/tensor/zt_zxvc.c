/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zt_zxvc.c — the .zxvc package and its exact weight blocks (zt_zxvc.h). */
#include "zt_zxvc.h"
#include "zt_gguf.h"
#include "../robin_debanks/sha256.h"

/* ---- little-endian helpers ---- */

static uint64_t rd_le(const uint8_t *p, uint32_t n)
{
    uint64_t v = 0;
    for (uint32_t i = 0; i < n; i++) v |= (uint64_t) p[i] << (8u * i);
    return v;
}

static void wr_le(uint8_t *p, uint64_t v, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) p[i] = (uint8_t) (v >> (8u * i));
}

static void copy_bytes(uint8_t *d, const uint8_t *s, uint64_t n)
{
    for (uint64_t i = 0; i < n; i++) d[i] = s[i];
}

static uint32_t bitlen(uint64_t v)
{
    uint32_t b = 0;
    while (v) {
        b++;
        v >>= 1;
    }
    return b;
}

/* ---- float formats, read and written as integers ---- */

typedef struct {
    uint32_t mbits, ebits, bias, bytes;
} fmt_t;

static bool get_fmt(uint32_t type, fmt_t *f)
{
    switch (type) {
    case ZT_GGML_F32:
        *f = (fmt_t){23u, 8u, 127u, 4u};
        return true;
    case ZT_GGML_F16:
        *f = (fmt_t){10u, 5u, 15u, 2u};
        return true;
    case ZT_GGML_BF16:
        *f = (fmt_t){7u, 8u, 127u, 2u};
        return true;
    default:
        return false;
    }
}

uint32_t zxvc_src_size(uint32_t type)
{
    fmt_t f;
    return get_fmt(type, &f) ? f.bytes : 0u;
}

/* bits -> sign, odd significand (0 for zero) and exponent: value = sig * 2^e.
 * False for NaN and infinity. */
static bool split(const fmt_t *f, uint32_t bits, bool *neg, uint64_t *sig, int32_t *e)
{
    uint32_t emax = (1u << f->ebits) - 1u;
    uint32_t ex = (bits >> f->mbits) & emax;
    uint64_t fr = bits & ((1u << f->mbits) - 1u);
    *neg = (bits >> (f->mbits + f->ebits)) & 1u;
    if (ex == emax) return false;
    int32_t lo = 1 - (int32_t) f->bias - (int32_t) f->mbits; /* exponent of a subnormal's LSB */
    if (ex == 0) {
        *sig = fr;
        *e = lo;
    } else {
        *sig = fr | (1ull << f->mbits);
        *e = lo + (int32_t) ex - 1;
    }
    if (*sig == 0) {
        *e = 0;
        return true;
    }
    while ((*sig & 1u) == 0) {
        *sig >>= 1;
        (*e)++;
    }
    return true;
}

/* sign, magnitude, exponent -> bits, only when exactly representable. */
static bool compose(const fmt_t *f, bool neg, uint64_t mag, int32_t e, uint32_t *bits)
{
    uint32_t sign = neg ? (1u << (f->mbits + f->ebits)) : 0u;
    int32_t b = (int32_t) bitlen(mag);
    int32_t msb = e + b - 1;
    int32_t min_norm = 1 - (int32_t) f->bias;
    int32_t max_norm = (int32_t) ((1u << f->ebits) - 2u) - (int32_t) f->bias;
    int32_t lo = min_norm - (int32_t) f->mbits;
    if (msb > max_norm) return false;
    if (msb >= min_norm) {
        int32_t keep = (int32_t) f->mbits + 1;
        uint64_t s;
        if (b > keep) {
            uint32_t drop = (uint32_t) (b - keep);
            if (mag & ((1ull << drop) - 1u)) return false;
            s = mag >> drop;
        } else {
            s = mag << (uint32_t) (keep - b);
        }
        uint32_t biased = (uint32_t) (msb + (int32_t) f->bias);
        *bits = sign | (biased << f->mbits) | (uint32_t) (s & ((1ull << f->mbits) - 1u));
        return true;
    }
    int32_t sh = e - lo;
    uint64_t fr;
    if (sh >= 0) {
        fr = mag << (uint32_t) sh;
    } else {
        if ((uint32_t) -sh >= 64u || (mag & ((1ull << (uint32_t) -sh) - 1u))) return false;
        fr = mag >> (uint32_t) -sh;
    }
    *bits = sign | (uint32_t) fr;
    return true;
}

/* ---- exact blocks ----
 * byte 0: bits 0-2 integer width code (0..4: 1, 2, 3, 4 or 8 bytes), bit 3
 *         all values zero (no integers follow), bit 4 a negative-zero mask
 *         follows
 * bytes 1-2: the shared exponent, int16
 * [4 bytes: negative-zero mask, bit i for value i]
 * n integers, two's complement */

#define BM_WIDTH   7u
#define BM_ZERO    8u
#define BM_NEGZERO 16u

static const uint8_t width_bytes[5] = {1u, 2u, 3u, 4u, 8u};

int32_t zxvc_block_encode(uint32_t type, const uint8_t *src, uint32_t n, uint8_t *out)
{
    fmt_t f;
    if (!get_fmt(type, &f)) return ZXVC_ETYPE;
    if (!src || !out || n == 0 || n > ZXVC_BLK) return ZXVC_EARG;
    bool neg[ZXVC_BLK];
    uint64_t sig[ZXVC_BLK];
    int32_t ex[ZXVC_BLK];
    uint32_t negzero = 0;
    bool any = false;
    int32_t emin = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t bits = (uint32_t) rd_le(src + i * f.bytes, f.bytes);
        if (!split(&f, bits, &neg[i], &sig[i], &ex[i])) return ZXVC_ENOTEXACT;
        if (sig[i] == 0) {
            if (neg[i]) negzero |= 1u << i;
            continue;
        }
        if (!any || ex[i] < emin) emin = ex[i];
        any = true;
    }
    uint8_t mode = negzero ? BM_NEGZERO : 0u;
    uint32_t w = 0, maxb = 0;
    if (!any) {
        mode |= BM_ZERO;
        emin = 0;
    } else {
        for (uint32_t i = 0; i < n; i++) {
            if (sig[i] == 0) continue;
            uint32_t b = bitlen(sig[i]) + (uint32_t) (ex[i] - emin);
            if (b > 63u) return ZXVC_ENOTEXACT;
            if (b > maxb) maxb = b;
        }
        uint32_t need = maxb + 1u; /* bits including the sign */
        w = need <= 8u ? 0u : need <= 16u ? 1u : need <= 24u ? 2u : need <= 32u ? 3u : 4u;
        mode |= (uint8_t) w;
    }
    out[0] = mode;
    wr_le(out + 1, (uint64_t) (uint16_t) (int16_t) emin, 2);
    uint32_t pos = 3;
    if (negzero) {
        wr_le(out + pos, negzero, 4);
        pos += 4;
    }
    if (!any) return (int32_t) pos;
    uint32_t wb = width_bytes[w];
    for (uint32_t i = 0; i < n; i++) {
        uint64_t mag = sig[i] ? sig[i] << (uint32_t) (ex[i] - emin) : 0u;
        uint64_t v = neg[i] ? (uint64_t) 0 - mag : mag;
        wr_le(out + pos, v, wb);
        pos += wb;
    }
    return (int32_t) pos;
}

/* Read a block header and its i-th integer as sign + magnitude. */
typedef struct {
    uint8_t mode;
    int32_t e;
    uint32_t negzero, wb, hdr;
} blk_t;

static int32_t blk_open(const uint8_t *in, uint64_t avail, uint32_t n, blk_t *b)
{
    if (!in || n == 0 || n > ZXVC_BLK) return ZXVC_EARG;
    if (avail < 3) return ZXVC_ETRUNC;
    b->mode = in[0];
    if (b->mode & ~(BM_WIDTH | BM_ZERO | BM_NEGZERO)) return ZXVC_ECORRUPT;
    b->e = (int16_t) (uint16_t) rd_le(in + 1, 2);
    b->hdr = 3;
    b->negzero = 0;
    if (b->mode & BM_NEGZERO) {
        if (avail < 7) return ZXVC_ETRUNC;
        b->negzero = (uint32_t) rd_le(in + 3, 4);
        b->hdr = 7;
        if (n < 32u && (b->negzero >> n) != 0) return ZXVC_ECORRUPT;
    }
    if ((b->mode & BM_WIDTH) > 4u) return ZXVC_ECORRUPT;
    if ((b->mode & BM_ZERO) && (b->mode & BM_WIDTH)) return ZXVC_ECORRUPT;
    b->wb = (b->mode & BM_ZERO) ? 0u : width_bytes[b->mode & BM_WIDTH];
    if ((uint64_t) b->hdr + (uint64_t) b->wb * n > avail) return ZXVC_ETRUNC;
    return ZXVC_OK;
}

static void blk_get(const uint8_t *in, const blk_t *b, uint32_t i, bool *neg, uint64_t *mag)
{
    if (!b->wb) {
        *neg = false;
        *mag = 0;
        return;
    }
    uint64_t v = rd_le(in + b->hdr + i * b->wb, b->wb);
    uint32_t bits = 8u * b->wb;
    bool s = bits < 64u ? (v >> (bits - 1u)) & 1u : v >> 63;
    if (s) {
        if (bits < 64u) v |= ~0ull << bits; /* sign-extend */
        *neg = true;
        *mag = (uint64_t) 0 - v;
    } else {
        *neg = false;
        *mag = v;
    }
}

int32_t zxvc_block_decode(uint32_t type, const uint8_t *in, uint64_t avail, uint32_t n,
                          uint8_t *src_out)
{
    fmt_t f;
    if (!get_fmt(type, &f)) return ZXVC_ETYPE;
    if (!src_out) return ZXVC_EARG;
    blk_t b;
    int32_t rc = blk_open(in, avail, n, &b);
    if (rc) return rc;
    for (uint32_t i = 0; i < n; i++) {
        bool neg;
        uint64_t mag;
        blk_get(in, &b, i, &neg, &mag);
        uint32_t bits;
        if (mag == 0) {
            if (neg) return ZXVC_ECORRUPT; /* -0 as an integer: not written by the encoder */
            bits = (b.negzero >> i) & 1u ? 1u << (f.mbits + f.ebits) : 0u;
        } else {
            if ((b.negzero >> i) & 1u) return ZXVC_ECORRUPT;
            if (mag >> 63) return ZXVC_ECORRUPT;
            if (!compose(&f, neg, mag, b.e, &bits)) return ZXVC_ECORRUPT;
        }
        wr_le(src_out + i * f.bytes, bits, f.bytes);
    }
    return (int32_t) (b.hdr + b.wb * n);
}

static zt_fx sat_q16(bool neg, uint64_t mag)
{
    if (mag > 0x7FFFFFFFull) mag = neg ? 0x80000000ull : 0x7FFFFFFFull;
    return neg ? (zt_fx) - (int64_t) mag : (zt_fx) mag;
}

int32_t zxvc_block_q16(const uint8_t *in, uint64_t avail, uint32_t n, zt_fx *out)
{
    if (!out) return ZXVC_EARG;
    blk_t b;
    int32_t rc = blk_open(in, avail, n, &b);
    if (rc) return rc;
    int32_t sh = b.e + 16;
    for (uint32_t i = 0; i < n; i++) {
        bool neg;
        uint64_t mag;
        blk_get(in, &b, i, &neg, &mag);
        if (mag == 0) {
            out[i] = 0;
        } else if (sh >= 0) {
            out[i] = (sh >= 63 || (mag >> (63 - sh)) != 0) ? sat_q16(neg, ~0ull)
                                                           : sat_q16(neg, mag << sh);
        } else if (-sh > 63) {
            out[i] = 0;
        } else {
            uint32_t r = (uint32_t) -sh;
            out[i] = sat_q16(neg, (mag + (1ull << (r - 1))) >> r);
        }
    }
    return (int32_t) (b.hdr + b.wb * n);
}

/* ---- the package ---- */

static bool in_buf(uint64_t size, uint64_t off, uint64_t len)
{
    return off <= size && len <= size - off;
}

int32_t zxvc_open(zxvc_t *z, const uint8_t *buf, uint64_t size)
{
    if (!z || !buf) return ZXVC_EARG;
    if (size < ZXVC_HEADER_BYTES) return ZXVC_ETRUNC;
    if ((uint32_t) rd_le(buf, 4) != ZXVC_MAGIC) return ZXVC_EMAGIC;
    z->buf = buf;
    z->size = size;
    z->version = (uint32_t) rd_le(buf + 4, 4);
    if (z->version != ZXVC_VERSION) return ZXVC_EVERSION;
    z->n_parts = (uint32_t) rd_le(buf + 8, 4);
    z->n_tensors = (uint32_t) rd_le(buf + 12, 4);
    z->flags = (uint32_t) rd_le(buf + 16, 4);
    z->parts_off = rd_le(buf + 24, 8);
    z->tensors_off = rd_le(buf + 32, 8);
    z->data_off = rd_le(buf + 40, 8);
    z->file_size = rd_le(buf + 48, 8);
    z->sig_off = rd_le(buf + 56, 8);
    z->sig_len = rd_le(buf + 64, 8);
    if (z->file_size != size) return ZXVC_ETRUNC;
    if (!in_buf(size, z->parts_off, (uint64_t) z->n_parts * ZXVC_PART_BYTES) ||
        !in_buf(size, z->tensors_off, (uint64_t) z->n_tensors * ZXVC_TENSOR_BYTES) ||
        !in_buf(size, z->data_off, 0) || !in_buf(size, z->sig_off, z->sig_len))
        return ZXVC_ETRUNC;
    if (z->parts_off < ZXVC_HEADER_BYTES || z->tensors_off < ZXVC_HEADER_BYTES) return ZXVC_ETRUNC;
    return ZXVC_OK;
}

static void get_name(char *dst, const uint8_t *src, uint32_t cap)
{
    uint32_t i = 0;
    for (; i < cap && src[i]; i++) dst[i] = (char) src[i];
    dst[i] = 0;
}

int32_t zxvc_part(const zxvc_t *z, uint32_t i, zxvc_part_t *p)
{
    if (!z || !p) return ZXVC_EARG;
    if (i >= z->n_parts) return ZXVC_ENOTFOUND;
    const uint8_t *e = z->buf + z->parts_off + (uint64_t) i * ZXVC_PART_BYTES;
    get_name(p->name, e, ZXVC_PART_NAME);
    p->off = rd_le(e + 32, 8);
    p->len = rd_le(e + 40, 8);
    copy_bytes(p->sha, e + 48, ZXVC_HASH);
    if (!in_buf(z->size, p->off, p->len)) return ZXVC_ETRUNC;
    p->data = z->buf + p->off;
    return ZXVC_OK;
}

static bool name_eq(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

int32_t zxvc_find_part(const zxvc_t *z, const char *name, zxvc_part_t *p)
{
    if (!z || !name || !p) return ZXVC_EARG;
    for (uint32_t i = 0; i < z->n_parts; i++) {
        int32_t rc = zxvc_part(z, i, p);
        if (rc) return rc;
        if (name_eq(p->name, name)) return ZXVC_OK;
    }
    return ZXVC_ENOTFOUND;
}

int32_t zxvc_tensor(const zxvc_t *z, uint32_t i, zxvc_tensor_t *t)
{
    if (!z || !t) return ZXVC_EARG;
    if (i >= z->n_tensors) return ZXVC_ENOTFOUND;
    const uint8_t *e = z->buf + z->tensors_off + (uint64_t) i * ZXVC_TENSOR_BYTES;
    get_name(t->name, e, ZXVC_TENSOR_NAME);
    t->enc = (uint32_t) rd_le(e + 96, 4);
    t->src_type = (uint32_t) rd_le(e + 100, 4);
    t->n_dims = (uint32_t) rd_le(e + 104, 4);
    t->src_file = (uint32_t) rd_le(e + 108, 4);
    if (t->n_dims == 0 || t->n_dims > ZXVC_MAX_DIMS) return ZXVC_ECORRUPT;
    uint64_t n = 1;
    for (uint32_t d = 0; d < ZXVC_MAX_DIMS; d++) {
        t->dims[d] = rd_le(e + 112 + 8u * d, 8);
        if (d < t->n_dims) {
            /* at most 2^56 elements, checked without a 64-bit division */
            if (t->dims[d] == 0 || bitlen(n) + bitlen(t->dims[d]) > 57u) return ZXVC_ECORRUPT;
            n *= t->dims[d];
        }
    }
    t->n_elems = rd_le(e + 144, 8);
    if (t->n_elems != n) return ZXVC_ECORRUPT;
    t->off = rd_le(e + 152, 8);
    t->len = rd_le(e + 160, 8);
    t->src_off = rd_le(e + 168, 8);
    t->src_len = rd_le(e + 176, 8);
    copy_bytes(t->sha, e + 184, ZXVC_HASH);
    copy_bytes(t->src_sha, e + 216, ZXVC_HASH);
    if (t->enc != ZXVC_ENC_NATIVE && t->enc != ZXVC_ENC_EXACT) return ZXVC_ECORRUPT;
    if (t->enc == ZXVC_ENC_EXACT) {
        uint32_t sz = zxvc_src_size(t->src_type);
        if (!sz) return ZXVC_ETYPE;
        if (t->src_len != n * sz) return ZXVC_ECORRUPT;
    } else if (t->len != t->src_len) {
        return ZXVC_ECORRUPT;
    }
    if (!in_buf(z->size, t->off, t->len)) return ZXVC_ETRUNC;
    t->data = z->buf + t->off;
    return ZXVC_OK;
}

int32_t zxvc_find_tensor(const zxvc_t *z, const char *name, zxvc_tensor_t *t)
{
    if (!z || !name || !t) return ZXVC_EARG;
    for (uint32_t i = 0; i < z->n_tensors; i++) {
        int32_t rc = zxvc_tensor(z, i, t);
        if (rc) return rc;
        if (name_eq(t->name, name)) return ZXVC_OK;
    }
    return ZXVC_ENOTFOUND;
}

void zxvc_digest(const zxvc_t *z, uint8_t out[ZXVC_HASH])
{
    static const char ctx[] = ZXVC_SIG_CTX;
    uint8_t h[ZXVC_HEADER_BYTES];
    copy_bytes(h, z->buf, ZXVC_HEADER_BYTES);
    for (uint32_t i = 56; i < 72; i++) h[i] = 0;
    sha256_ctx_t c;
    sha256_init(&c);
    sha256_update(&c, (const uint8_t *) ctx, sizeof ctx - 1);
    sha256_update(&c, h, ZXVC_HEADER_BYTES);
    sha256_update(&c, z->buf + z->parts_off, (size_t) z->n_parts * ZXVC_PART_BYTES);
    sha256_update(&c, z->buf + z->tensors_off, (size_t) z->n_tensors * ZXVC_TENSOR_BYTES);
    sha256_final(&c, out);
}

static bool hash_eq(const uint8_t *data, uint64_t len, const uint8_t want[ZXVC_HASH])
{
    uint8_t got[ZXVC_HASH];
    sha256(data, (size_t) len, got);
    uint8_t d = 0;
    for (uint32_t i = 0; i < ZXVC_HASH; i++) d |= (uint8_t) (got[i] ^ want[i]);
    return d == 0;
}

int32_t zxvc_verify_hashes(const zxvc_t *z, uint32_t *bad)
{
    if (!z) return ZXVC_EARG;
    for (uint32_t i = 0; i < z->n_parts; i++) {
        zxvc_part_t p;
        int32_t rc = zxvc_part(z, i, &p);
        if (rc || !hash_eq(p.data, p.len, p.sha)) {
            if (bad) *bad = i;
            return rc ? rc : ZXVC_EHASH;
        }
    }
    for (uint32_t i = 0; i < z->n_tensors; i++) {
        zxvc_tensor_t t;
        int32_t rc = zxvc_tensor(z, i, &t);
        if (rc || !hash_eq(t.data, t.len, t.sha)) {
            if (bad) *bad = z->n_parts + i;
            return rc ? rc : ZXVC_EHASH;
        }
    }
    return ZXVC_OK;
}

int32_t zxvc_tensor_source(const zxvc_tensor_t *t, uint8_t *out, uint64_t out_len)
{
    if (!t || !out) return ZXVC_EARG;
    if (out_len < t->src_len) return ZXVC_ESPACE;
    if (t->enc == ZXVC_ENC_NATIVE) {
        copy_bytes(out, t->data, t->len);
        return ZXVC_OK;
    }
    uint32_t sz = zxvc_src_size(t->src_type);
    uint64_t pos = 0;
    for (uint64_t i = 0; i < t->n_elems; i += ZXVC_BLK) {
        uint32_t n = t->n_elems - i < ZXVC_BLK ? (uint32_t) (t->n_elems - i) : ZXVC_BLK;
        int32_t rc = zxvc_block_decode(t->src_type, t->data + pos, t->len - pos, n, out + i * sz);
        if (rc < 0) return rc;
        pos += (uint64_t) rc;
    }
    return pos == t->len ? ZXVC_OK : ZXVC_ECORRUPT;
}

int32_t zxvc_tensor_q16(const zxvc_tensor_t *t, zt_fx *out, uint64_t n_out)
{
    if (!t || !out) return ZXVC_EARG;
    if (t->enc != ZXVC_ENC_EXACT) return ZXVC_ETYPE;
    if (n_out < t->n_elems) return ZXVC_ESPACE;
    uint64_t pos = 0;
    for (uint64_t i = 0; i < t->n_elems; i += ZXVC_BLK) {
        uint32_t n = t->n_elems - i < ZXVC_BLK ? (uint32_t) (t->n_elems - i) : ZXVC_BLK;
        int32_t rc = zxvc_block_q16(t->data + pos, t->len - pos, n, out + i);
        if (rc < 0) return rc;
        pos += (uint64_t) rc;
    }
    return pos == t->len ? ZXVC_OK : ZXVC_ECORRUPT;
}

/* ---- writing ---- */

void zxvc_put_header(uint8_t h[ZXVC_HEADER_BYTES], const zxvc_t *z)
{
    for (uint32_t i = 0; i < ZXVC_HEADER_BYTES; i++) h[i] = 0;
    wr_le(h, ZXVC_MAGIC, 4);
    wr_le(h + 4, ZXVC_VERSION, 4);
    wr_le(h + 8, z->n_parts, 4);
    wr_le(h + 12, z->n_tensors, 4);
    wr_le(h + 16, z->flags, 4);
    wr_le(h + 24, z->parts_off, 8);
    wr_le(h + 32, z->tensors_off, 8);
    wr_le(h + 40, z->data_off, 8);
    wr_le(h + 48, z->file_size, 8);
    wr_le(h + 56, z->sig_off, 8);
    wr_le(h + 64, z->sig_len, 8);
}

static void put_name(uint8_t *dst, const char *src, uint32_t cap)
{
    uint32_t i = 0;
    for (; i < cap && src[i]; i++) dst[i] = (uint8_t) src[i];
    for (; i < cap; i++) dst[i] = 0;
}

void zxvc_put_part(uint8_t e[ZXVC_PART_BYTES], const zxvc_part_t *p)
{
    put_name(e, p->name, ZXVC_PART_NAME);
    wr_le(e + 32, p->off, 8);
    wr_le(e + 40, p->len, 8);
    copy_bytes(e + 48, p->sha, ZXVC_HASH);
}

void zxvc_put_tensor(uint8_t e[ZXVC_TENSOR_BYTES], const zxvc_tensor_t *t)
{
    put_name(e, t->name, ZXVC_TENSOR_NAME);
    wr_le(e + 96, t->enc, 4);
    wr_le(e + 100, t->src_type, 4);
    wr_le(e + 104, t->n_dims, 4);
    wr_le(e + 108, t->src_file, 4);
    for (uint32_t d = 0; d < ZXVC_MAX_DIMS; d++)
        wr_le(e + 112 + 8u * d, d < t->n_dims ? t->dims[d] : 0u, 8);
    wr_le(e + 144, t->n_elems, 8);
    wr_le(e + 152, t->off, 8);
    wr_le(e + 160, t->len, 8);
    wr_le(e + 168, t->src_off, 8);
    wr_le(e + 176, t->src_len, 8);
    copy_bytes(e + 184, t->sha, ZXVC_HASH);
    copy_bytes(e + 216, t->src_sha, ZXVC_HASH);
}

const char *zxvc_err_name(int32_t code)
{
    switch (code) {
    case ZXVC_OK:
        return "ok";
    case ZXVC_EARG:
        return "bad argument";
    case ZXVC_EMAGIC:
        return "not a .zxvc package";
    case ZXVC_EVERSION:
        return "unsupported .zxvc version";
    case ZXVC_ETRUNC:
        return "truncated or out of bounds";
    case ZXVC_ETYPE:
        return "type not supported for exact encoding";
    case ZXVC_ENOTEXACT:
        return "value cannot be stored exactly (NaN, infinity or block spread over 63 bits)";
    case ZXVC_EHASH:
        return "SHA-256 mismatch";
    case ZXVC_ECORRUPT:
        return "corrupt block or table entry";
    case ZXVC_ENOTFOUND:
        return "not found";
    case ZXVC_ESPACE:
        return "buffer too small";
    default:
        return "unknown error";
    }
}
