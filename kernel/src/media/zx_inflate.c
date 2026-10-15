/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zx_inflate.c — DEFLATE and zlib decoding. See zx_inflate.h. */
#include "zx_inflate.h"
#include "zui_capture.h" /* zui_adler32 */

#define MAXBITS  15
#define MAXLCODE 286
#define MAXDCODE 30
#define FIXLCODE 288
#define FASTBITS 9

typedef struct {
    uint16_t count[MAXBITS + 1];  /* codes of each length                */
    uint16_t symbol[FIXLCODE];    /* symbols ordered by code             */
    uint16_t fast[1 << FASTBITS]; /* (sym << 4) | len, 0 = use the slow path */
} huff_t;

typedef struct {
    const uint8_t *in;
    uint32_t inlen, pos;
    uint64_t bits;
    uint32_t nbits;
    uint8_t *out;
    uint32_t cap, n;
    int err; /* sticky */
} st_t;

/* Make at least `need` bits available when the input allows; returns how
 * many there are. */
static uint32_t fill(st_t *s, uint32_t need)
{
    while (s->nbits < need && s->pos < s->inlen) {
        s->bits |= (uint64_t) s->in[s->pos++] << s->nbits;
        s->nbits += 8;
    }
    return s->nbits;
}

static uint32_t getbits(st_t *s, uint32_t n) /* n <= 16 */
{
    if (fill(s, n) < n) {
        if (!s->err) s->err = ZX_INF_E_TRUNC;
        return 0;
    }
    uint32_t v = (uint32_t) (s->bits & ((1u << n) - 1u));
    s->bits >>= n;
    s->nbits -= n;
    return v;
}

/* Canonical code from lengths (puff's construction). Returns 0 complete,
 * > 0 incomplete (that many codes left), < 0 over-subscribed. */
static int build(huff_t *h, const uint8_t *len, uint32_t n)
{
    uint16_t offs[MAXBITS + 1];
    for (uint32_t i = 0; i <= MAXBITS; i++) h->count[i] = 0;
    for (uint32_t i = 0; i < n; i++) h->count[len[i]]++;
    for (uint32_t i = 0; i < (1u << FASTBITS); i++) h->fast[i] = 0;
    if (h->count[0] == n) return 0; /* no codes: complete, but decodes nothing */
    int left = 1;
    for (uint32_t i = 1; i <= MAXBITS; i++) {
        left <<= 1;
        left -= h->count[i];
        if (left < 0) return left;
    }
    offs[1] = 0;
    for (uint32_t i = 1; i < MAXBITS; i++) offs[i + 1] = (uint16_t) (offs[i] + h->count[i]);
    for (uint32_t i = 0; i < n; i++)
        if (len[i]) h->symbol[offs[len[i]]++] = (uint16_t) i;
    /* the fast table: walk the canonical codes in order */
    uint32_t code = 0, k = 0;
    for (uint32_t l = 1; l <= FASTBITS; l++) {
        for (uint32_t c = 0; c < h->count[l]; c++, k++, code++) {
            uint32_t rev = 0;
            for (uint32_t b = 0; b < l; b++) rev |= ((code >> b) & 1u) << (l - 1 - b);
            for (uint32_t j = rev; j < (1u << FASTBITS); j += 1u << l)
                h->fast[j] = (uint16_t) ((h->symbol[k] << 4) | l);
        }
        code <<= 1;
    }
    return left;
}

static int decode(st_t *s, const huff_t *h)
{
    if (fill(s, FASTBITS) >= 1) {
        uint32_t e = h->fast[s->bits & ((1u << FASTBITS) - 1u)];
        if (e && (e & 15u) <= s->nbits) {
            s->bits >>= e & 15u;
            s->nbits -= e & 15u;
            return (int) (e >> 4);
        }
    }
    int code = 0, first = 0, index = 0;
    for (uint32_t len = 1; len <= MAXBITS; len++) {
        code |= (int) getbits(s, 1);
        if (s->err) return -1;
        int count = h->count[len];
        if (code - count < first) return h->symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    if (!s->err) s->err = ZX_INF_E_DATA;
    return -1;
}

static const uint16_t LBASE[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                   31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
static const uint8_t LEXT[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
static const uint16_t DBASE[30] = {1,    2,    3,    4,    5,    7,    9,    13,    17,    25,
                                   33,   49,   65,   97,   129,  193,  257,  385,   513,   769,
                                   1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
static const uint8_t DEXT[30] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

static int codes(st_t *s, const huff_t *lc, const huff_t *dc)
{
    for (;;) {
        int sym = decode(s, lc);
        if (s->err) return s->err;
        if (sym < 256) {
            if (s->n >= s->cap) return ZX_INF_E_SPACE;
            s->out[s->n++] = (uint8_t) sym;
        } else if (sym == 256) {
            return ZX_INF_OK;
        } else {
            sym -= 257;
            if (sym >= 29) return ZX_INF_E_DATA;
            uint32_t len = LBASE[sym] + getbits(s, LEXT[sym]);
            int ds = decode(s, dc);
            if (s->err) return s->err;
            if (ds < 0 || ds >= 30) return ZX_INF_E_DATA;
            uint32_t dist = DBASE[ds] + getbits(s, DEXT[ds]);
            if (s->err) return s->err;
            if (dist > s->n) return ZX_INF_E_DATA;
            if (len > s->cap - s->n) return ZX_INF_E_SPACE;
            uint8_t *o = s->out + s->n;
            for (uint32_t i = 0; i < len; i++) o[i] = o[(int32_t) i - (int32_t) dist];
            s->n += len;
        }
    }
}

static int stored(st_t *s)
{
    /* drop to the byte boundary, then give back whole buffered bytes */
    s->bits >>= s->nbits & 7u;
    s->nbits -= s->nbits & 7u;
    s->pos -= s->nbits / 8u;
    s->bits = 0;
    s->nbits = 0;
    if (s->inlen - s->pos < 4) return ZX_INF_E_TRUNC;
    uint32_t len = s->in[s->pos] | (uint32_t) s->in[s->pos + 1] << 8;
    uint32_t nlen = s->in[s->pos + 2] | (uint32_t) s->in[s->pos + 3] << 8;
    s->pos += 4;
    if (len != (~nlen & 0xFFFFu)) return ZX_INF_E_DATA;
    if (s->inlen - s->pos < len) return ZX_INF_E_TRUNC;
    if (len > s->cap - s->n) return ZX_INF_E_SPACE;
    for (uint32_t i = 0; i < len; i++) s->out[s->n + i] = s->in[s->pos + i];
    s->n += len;
    s->pos += len;
    return ZX_INF_OK;
}

static int fixed(st_t *s)
{
    huff_t lc, dc;
    uint8_t len[FIXLCODE];
    uint32_t i = 0;
    for (; i < 144; i++) len[i] = 8;
    for (; i < 256; i++) len[i] = 9;
    for (; i < 280; i++) len[i] = 7;
    for (; i < FIXLCODE; i++) len[i] = 8;
    build(&lc, len, FIXLCODE);
    for (i = 0; i < 32; i++) len[i] = 5;
    build(&dc, len, 32); /* 30 and 31 decode, then are refused as reserved */
    return codes(s, &lc, &dc);
}

static int dynamic(st_t *s)
{
    static const uint8_t ORDER[19] = {16, 17, 18, 0, 8,  7, 9,  6, 10, 5,
                                      11, 4,  12, 3, 13, 2, 14, 1, 15};
    huff_t lc, dc;
    uint8_t len[MAXLCODE + MAXDCODE];
    uint32_t nlen = getbits(s, 5) + 257, ndist = getbits(s, 5) + 1, ncode = getbits(s, 4) + 4;
    if (s->err) return s->err;
    if (nlen > MAXLCODE || ndist > MAXDCODE) return ZX_INF_E_DATA;
    uint32_t i = 0;
    for (; i < ncode; i++) len[ORDER[i]] = (uint8_t) getbits(s, 3);
    for (; i < 19; i++) len[ORDER[i]] = 0;
    if (s->err) return s->err;
    if (build(&lc, len, 19) != 0) return ZX_INF_E_DATA; /* must be complete */
    for (i = 0; i < nlen + ndist;) {
        int sym = decode(s, &lc);
        if (s->err) return s->err;
        if (sym < 16) {
            len[i++] = (uint8_t) sym;
            continue;
        }
        uint8_t v = 0;
        uint32_t rep;
        if (sym == 16) {
            if (i == 0) return ZX_INF_E_DATA;
            v = len[i - 1];
            rep = 3 + getbits(s, 2);
        } else if (sym == 17) {
            rep = 3 + getbits(s, 3);
        } else {
            rep = 11 + getbits(s, 7);
        }
        if (s->err) return s->err;
        if (i + rep > nlen + ndist) return ZX_INF_E_DATA;
        while (rep--) len[i++] = v;
    }
    if (len[256] == 0) return ZX_INF_E_DATA; /* no end-of-block code */
    int e = build(&lc, len, nlen);
    if (e < 0 || (e > 0 && nlen - lc.count[0] != 1)) return ZX_INF_E_DATA;
    e = build(&dc, len + nlen, ndist);
    if (e < 0 || (e > 0 && ndist - dc.count[0] != 1)) return ZX_INF_E_DATA;
    return codes(s, &lc, &dc);
}

static int run(st_t *s)
{
    uint32_t last;
    do {
        last = getbits(s, 1);
        uint32_t type = getbits(s, 2);
        if (s->err) return s->err;
        int r = type == 0   ? stored(s)
                : type == 1 ? fixed(s)
                : type == 2 ? dynamic(s)
                            : ZX_INF_E_DATA;
        if (r) return r;
    } while (!last);
    return ZX_INF_OK;
}

int zx_inflate_raw(const uint8_t *in, uint32_t inlen, uint8_t *out, uint32_t cap, uint32_t *outlen,
                   uint32_t *used)
{
    if (!in || !outlen || (!out && cap)) return ZX_INF_E_ARG;
    st_t s = {in, inlen, 0, 0, 0, out, cap, 0, 0};
    int r = run(&s);
    *outlen = s.n;
    if (used) *used = s.pos - s.nbits / 8u;
    return r;
}

int zx_inflate_zlib(const uint8_t *in, uint32_t inlen, uint8_t *out, uint32_t cap, uint32_t *outlen)
{
    if (!in || !outlen || (!out && cap)) return ZX_INF_E_ARG;
    *outlen = 0;
    if (inlen < 2) return ZX_INF_E_TRUNC;
    uint32_t cmf = in[0], flg = in[1];
    if ((cmf & 15u) != 8 || (cmf >> 4) > 7 || ((cmf << 8) | flg) % 31u != 0) return ZX_INF_E_CHECK;
    if (flg & 0x20u) return ZX_INF_E_DATA; /* preset dictionary: refused */
    uint32_t used = 0;
    int r = zx_inflate_raw(in + 2, inlen - 2, out, cap, outlen, &used);
    if (r) return r;
    used += 2;
    if (inlen - used < 4) return ZX_INF_E_TRUNC;
    uint32_t want = (uint32_t) in[used] << 24 | (uint32_t) in[used + 1] << 16 |
                    (uint32_t) in[used + 2] << 8 | in[used + 3];
    return zui_adler32(1, out, *outlen) == want ? ZX_INF_OK : ZX_INF_E_CHECK;
}
