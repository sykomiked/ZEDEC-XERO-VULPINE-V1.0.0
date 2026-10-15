/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv-cid.c - print the CIDv1 of each file, as `ipfs add --only-hash
 * --cid-version=1` gives it, with no IPFS node and nothing but a C compiler.
 *
 *   cc -O2 -o zxv-cid tools/zxv-cid.c
 *   ./zxv-cid FILE...          one "<cid>  <file>" line per file
 *
 * The parameters are pinned (docs/BUCKET.md); the same bytes give another CID
 * under any other ones, so a verifier must use exactly these:
 *   chunks of 262144 bytes, each a raw leaf (codec 0x55); a balanced DAG of
 *   UnixFS File nodes (dag-pb, 0x70) with at most 174 links each; sha2-256;
 *   CIDv1, multibase base32 lower ("b..."). A file of one chunk is its raw
 *   leaf ("bafkrei..."); a longer one is a dag-pb root ("bafybei...").
 * tools/zxv-verify.sh compiles and calls this to check that each file's name
 * is its CID. Self-contained: its own SHA-256, no libraries, C99. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifndef CHUNK /* overridden only by tools/test_zxv_cid.sh, to reach deep trees with small files */
#define CHUNK 262144u
#define LINKS 174u
#endif
#define LEVELS 12u

/* ---- SHA-256 (FIPS 180-4) ---- */
typedef struct {
    uint32_t h[8];
    uint64_t n;
    uint8_t b[64];
    uint32_t k;
} sha_t;

static const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

#define ROR(x, r) (((x) >> (r)) | ((x) << (32 - (r))))

static void sha_block(sha_t *s, const uint8_t *p)
{
    uint32_t w[64], a[8];
    for (int i = 0; i < 16; i++)
        w[i] = (uint32_t) p[4 * i] << 24 | (uint32_t) p[4 * i + 1] << 16 |
               (uint32_t) p[4 * i + 2] << 8 | p[4 * i + 3];
    for (int i = 16; i < 64; i++)
        w[i] = w[i - 16] + (ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3)) + w[i - 7] +
               (ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10));
    memcpy(a, s->h, sizeof a);
    for (int i = 0; i < 64; i++) {
        uint32_t t1 = a[7] + (ROR(a[4], 6) ^ ROR(a[4], 11) ^ ROR(a[4], 25)) +
                      ((a[4] & a[5]) ^ (~a[4] & a[6])) + K[i] + w[i];
        uint32_t t2 = (ROR(a[0], 2) ^ ROR(a[0], 13) ^ ROR(a[0], 22)) +
                      ((a[0] & a[1]) ^ (a[0] & a[2]) ^ (a[1] & a[2]));
        memmove(a + 1, a, 7 * sizeof a[0]);
        a[4] += t1;
        a[0] = t1 + t2;
    }
    for (int i = 0; i < 8; i++) s->h[i] += a[i];
}

static void sha(const uint8_t *p, size_t n, uint8_t out[32])
{
    static const uint32_t H0[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                   0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    sha_t s;
    memcpy(s.h, H0, sizeof H0);
    s.n = (uint64_t) n * 8u;
    for (; n >= 64; n -= 64, p += 64) sha_block(&s, p);
    memset(s.b, 0, sizeof s.b);
    memcpy(s.b, p, n);
    s.b[n] = 0x80;
    if (n >= 56) {
        sha_block(&s, s.b);
        memset(s.b, 0, sizeof s.b);
    }
    for (int i = 0; i < 8; i++) s.b[56 + i] = (uint8_t) (s.n >> (56 - 8 * i));
    sha_block(&s, s.b);
    for (int i = 0; i < 8; i++)
        for (int j = 0; j < 4; j++) out[4 * i + j] = (uint8_t) (s.h[i] >> (24 - 8 * j));
}

/* ---- the DAG ---- */
typedef struct {
    uint8_t cid[36];
    uint64_t tsize, fsize;
} link_t;

static link_t lv[LEVELS][LINKS];
static uint32_t nlv[LEVELS], top;
static uint8_t chunk[CHUNK], node[LINKS * 64u + LINKS * 11u + 32u];

static size_t varint(uint8_t *o, uint64_t v)
{
    size_t n = 0;
    while (v >= 0x80) {
        o[n++] = (uint8_t) (v | 0x80);
        v >>= 7;
    }
    o[n++] = (uint8_t) v;
    return n;
}

static void cid_of(uint8_t codec, const uint8_t *p, size_t n, uint8_t cid[36])
{
    cid[0] = 0x01;
    cid[1] = codec;
    cid[2] = 0x12;
    cid[3] = 0x20;
    sha(p, n, cid + 4);
}

/* A UnixFS File node over links l[0..n): links first, then its Data. */
static link_t file_node(const link_t *l, uint32_t n)
{
    size_t k = 0, d;
    uint8_t data[16 + LINKS * 10u], ln[64];
    link_t r = {{0}, 0, 0};
    for (uint32_t i = 0; i < n; i++) {
        size_t m = 0;
        ln[m++] = 0x0a;
        ln[m++] = 36;
        memcpy(ln + m, l[i].cid, 36);
        m += 36;
        ln[m++] = 0x12;
        ln[m++] = 0x00;
        ln[m++] = 0x18;
        m += varint(ln + m, l[i].tsize);
        node[k++] = 0x12;
        k += varint(node + k, m);
        memcpy(node + k, ln, m);
        k += m;
        r.fsize += l[i].fsize;
        r.tsize += l[i].tsize;
    }
    d = 0;
    data[d++] = 0x08;
    data[d++] = 0x02;
    data[d++] = 0x18;
    d += varint(data + d, r.fsize);
    for (uint32_t i = 0; i < n; i++) {
        data[d++] = 0x20;
        d += varint(data + d, l[i].fsize);
    }
    node[k++] = 0x0a;
    k += varint(node + k, d);
    memcpy(node + k, data, d);
    k += d;
    cid_of(0x70, node, k, r.cid);
    r.tsize += k;
    return r;
}

static int add(uint32_t at, link_t l)
{
    if (at >= LEVELS) return -1;
    if (at > top) top = at;
    if (nlv[at] == LINKS) {
        link_t up = file_node(lv[at], LINKS);
        nlv[at] = 0;
        if (add(at + 1, up)) return -1;
    }
    lv[at][nlv[at]++] = l;
    return 0;
}

static const char B32[] = "abcdefghijklmnopqrstuvwxyz234567";

static int cid_file(FILE *f, char out[64])
{
    uint32_t leaves = 0;
    link_t root;
    memset(nlv, 0, sizeof nlv);
    top = 0;
    for (;;) {
        size_t n = fread(chunk, 1, CHUNK, f);
        if (ferror(f)) return -1;
        if (n == 0 && leaves) break;
        link_t l;
        cid_of(0x55, chunk, n, l.cid);
        l.tsize = l.fsize = n;
        if (add(0, l)) return -1;
        leaves++;
        if (n < CHUNK) break;
    }
    if (top == 0 && nlv[0] == 1) {
        root = lv[0][0];
    } else {
        for (uint32_t at = 0; at < top; at++) {
            if (!nlv[at]) continue;
            link_t up = file_node(lv[at], nlv[at]);
            nlv[at] = 0;
            if (add(at + 1, up)) return -1;
        }
        root = file_node(lv[top], nlv[top]);
    }
    uint32_t acc = 0, bits = 0, o = 0;
    out[o++] = 'b';
    for (int i = 0; i < 36; i++) {
        acc = (acc << 8) | root.cid[i];
        bits += 8;
        while (bits >= 5) {
            out[o++] = B32[(acc >> (bits - 5)) & 31u];
            bits -= 5;
        }
    }
    if (bits) out[o++] = B32[(acc << (5 - bits)) & 31u];
    out[o] = 0;
    return 0;
}

int main(int argc, char **argv)
{
    int rc = 0;
    if (argc < 2) {
        fprintf(stderr, "usage: zxv-cid FILE...\n");
        return 2;
    }
    for (int i = 1; i < argc; i++) {
        char cid[64];
        FILE *f = fopen(argv[i], "rb");
        if (!f || cid_file(f, cid)) {
            fprintf(stderr, "zxv-cid: %s: cannot read\n", argv[i]);
            rc = 1;
        } else {
            printf("%s  %s\n", cid, argv[i]);
        }
        if (f) fclose(f);
    }
    return rc;
}
