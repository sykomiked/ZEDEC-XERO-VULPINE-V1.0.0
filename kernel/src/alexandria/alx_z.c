/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* alx_z.c — booklet compression: an LZ4 block codec and the ALXZ block
 * (alx_z.h). */
#include "alx_z.h"
#include "alx_int.h"
#include "../robin_debanks/sha256.h"

#define MINMATCH  4u
#define LASTLIT   5u      /* the last 5 bytes are always literals */
#define MFLIMIT   12u     /* the last match starts at least 12 bytes before the end */
#define MAXOFF    65535u
#define HBITS     16u
#define DEPTH     64u     /* candidates tried per position */
#define WIN       65536u

static uint32_t hash4(const uint8_t *p)
{
    uint32_t v = (uint32_t) p[0] | (uint32_t) p[1] << 8 | (uint32_t) p[2] << 16 |
                 (uint32_t) p[3] << 24;
    return (v * 2654435761u) >> (32u - HBITS);
}

uint32_t alx_z_bound(uint32_t n)
{
    return n + n / 255u + 16u;
}

typedef struct {
    const uint8_t *in;
    uint32_t n, ins;  /* every position below ins is in the chains */
    int32_t *head, *chain;
} hc_t;

static void insert_to(hc_t *h, uint32_t pos)
{
    for (; h->ins < pos; h->ins++) {
        if (h->ins + 4u > h->n) continue;
        uint32_t k = hash4(h->in + h->ins);
        h->chain[h->ins & (WIN - 1u)] = h->head[k];
        h->head[k] = (int32_t) h->ins;
    }
}

/* the longest match for pos among earlier positions (ties: the nearest) */
static uint32_t find(hc_t *h, uint32_t pos, uint32_t limit, uint32_t *off)
{
    insert_to(h, pos);
    uint32_t best = 0, depth = DEPTH;
    int32_t c = h->head[hash4(h->in + pos)];
    while (c >= 0 && depth--) {
        uint32_t cand = (uint32_t) c;
        if (pos - cand > MAXOFF) break;
        if (h->in[cand + best] == h->in[pos + best]) {
            uint32_t l = 0;
            while (pos + l < limit && h->in[cand + l] == h->in[pos + l]) l++;
            if (l > best) {
                best = l;
                *off = pos - cand;
                if (pos + l >= limit) break;
            }
        }
        int32_t nx = h->chain[cand & (WIN - 1u)];
        if (nx >= c) break; /* a slot reused by a later position */
        c = nx;
    }
    return best >= MINMATCH ? best : 0;
}

typedef struct {
    uint8_t *out;
    uint32_t cap, at;
    bool full;
} wr_t;

static void put(wr_t *w, uint8_t b)
{
    if (w->at < w->cap) w->out[w->at++] = b;
    else w->full = true;
}

static void put_len(wr_t *w, uint32_t l)
{
    for (; l >= 255u; l -= 255u) put(w, 255);
    put(w, (uint8_t) l);
}

static void sequence(wr_t *w, const uint8_t *lit, uint32_t nl, uint32_t off, uint32_t ml)
{
    uint32_t m = ml ? ml - MINMATCH : 0;
    put(w, (uint8_t) ((nl >= 15u ? 15u : nl) << 4 | (m >= 15u ? 15u : m)));
    if (nl >= 15u) put_len(w, nl - 15u);
    for (uint32_t i = 0; i < nl && !w->full; i++) put(w, lit[i]);
    if (!ml) return;
    put(w, (uint8_t) (off & 255u));
    put(w, (uint8_t) (off >> 8));
    if (m >= 15u) put_len(w, m - 15u);
}

uint32_t alx_z_lz4(const uint8_t *in, uint32_t n, uint8_t *out, uint32_t cap, int32_t *work)
{
    if ((!in && n) || !out || !work || n > ALX_Z_MAXPLAIN) return 0;
    wr_t w = {out, cap, 0, false};
    uint32_t anchor = 0;
    if (n > MFLIMIT) {
        hc_t h = {in, n, 0, work, work + 65536};
        for (uint32_t i = 0; i < 65536u; i++) work[i] = -1;
        uint32_t limit = n - LASTLIT, ip = 0;
        while (ip + MFLIMIT <= n && !w.full) {
            uint32_t off = 0, len = find(&h, ip, limit, &off);
            if (!len) {
                ip++;
                continue;
            }
            /* one step of lazy matching: a longer match one byte on wins */
            if (ip + 1u + MFLIMIT <= n) {
                uint32_t off2 = 0, len2 = find(&h, ip + 1u, limit, &off2);
                if (len2 > len) {
                    ip++;
                    len = len2;
                    off = off2;
                }
            }
            sequence(&w, in + anchor, ip - anchor, off, len);
            ip += len;
            anchor = ip;
        }
    }
    sequence(&w, in + anchor, n - anchor, 0, 0);
    return w.full ? 0 : w.at;
}

int32_t alx_z_unlz4(const uint8_t *in, uint32_t n, uint8_t *out, uint32_t cap)
{
    if (!in || (!out && cap)) return -1;
    uint32_t ip = 0, op = 0;
    for (;;) {
        if (ip >= n) return -1;
        uint32_t t = in[ip++], lit = t >> 4, b;
        if (lit == 15u) do {
                if (ip >= n) return -1;
                b = in[ip++];
                lit += b;
                if (lit > cap) return -1;
            } while (b == 255u);
        if (lit > n - ip || lit > cap - op) return -1;
        for (uint32_t i = 0; i < lit; i++) out[op++] = in[ip++];
        if (ip == n) return (int32_t) op; /* the last sequence: literals only */
        if (n - ip < 2u) return -1;
        uint32_t off = (uint32_t) in[ip] | (uint32_t) in[ip + 1] << 8, ml = t & 15u;
        ip += 2;
        if (off == 0 || off > op) return -1;
        if (ml == 15u) do {
                if (ip >= n) return -1;
                b = in[ip++];
                ml += b;
                if (ml > cap) return -1;
            } while (b == 255u);
        ml += MINMATCH;
        if (ml > cap - op) return -1;
        for (uint32_t i = 0; i < ml; i++, op++) out[op] = out[op - off]; /* may overlap */
    }
}

static void be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t) (v >> 24), p[1] = (uint8_t) (v >> 16), p[2] = (uint8_t) (v >> 8),
    p[3] = (uint8_t) v;
}

uint32_t alx_z_store(const uint8_t *b, uint32_t len, uint8_t *out, uint32_t cap, int32_t *work)
{
    if (!b || !out || !work || len > ALX_Z_MAXPLAIN || cap < len + ALX_Z_HEAD) return 0;
    /* an ALXZ block only when it is smaller than the booklet */
    uint32_t z = len > ALX_Z_HEAD + 1u ? alx_z_lz4(b, len, out + ALX_Z_HEAD, len - ALX_Z_HEAD - 1u, work) : 0;
    if (!z) {
        for (uint32_t i = 0; i < len; i++) out[i] = b[i];
        return len;
    }
    out[0] = 'A', out[1] = 'L', out[2] = 'X', out[3] = 'Z';
    out[4] = 1, out[5] = ALX_Z_LZ4, out[6] = 0, out[7] = 0;
    be32(out + 8, len);
    be32(out + 12, z);
    out[16] = 0x01, out[17] = 0x55, out[18] = 0x12, out[19] = 0x20;
    sha256(b, len, out + 20);
    for (uint32_t i = 52; i < ALX_Z_HEAD; i++) out[i] = 0;
    return ALX_Z_HEAD + z;
}

static bool zhead(const uint8_t *b, uint32_t len)
{
    return b && len >= ALX_Z_HEAD && b[0] == 'A' && b[1] == 'L' && b[2] == 'X' && b[3] == 'Z';
}

bool alx_z_is(const uint8_t *b, uint32_t len)
{
    return zhead(b, len);
}

uint32_t alx_z_plain_len(const uint8_t *b, uint32_t len)
{
    return zhead(b, len) ? alx_be32(b + 8) : 0;
}

int32_t alx_z_open(const uint8_t *b, uint32_t len, uint8_t *out, uint32_t cap)
{
    if (!zhead(b, len) || b[4] != 1 || b[5] != ALX_Z_LZ4 || b[6] || b[7]) return ALX_E_FORM;
    uint32_t plain = alx_be32(b + 8), z = alx_be32(b + 12);
    if (plain > ALX_Z_MAXPLAIN || z != len - ALX_Z_HEAD || z >= plain || b[16] != 0x01 ||
        b[17] != 0x55 || b[18] != 0x12 || b[19] != 0x20 || !alx_zero(b + 52, ALX_Z_HEAD - 52u))
        return ALX_E_FORM;
    if (!out || cap < plain) return ALX_E_LIMIT;
    if (alx_z_unlz4(b + ALX_Z_HEAD, z, out, plain) != (int32_t) plain) return ALX_E_FORM;
    uint8_t h[32];
    sha256(out, plain, h);
    return alx_same(h, b + 20, 32) ? (int32_t) plain : ALX_E_HASH;
}
