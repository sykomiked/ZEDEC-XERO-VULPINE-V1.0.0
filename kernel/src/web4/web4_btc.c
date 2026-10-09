/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* web4_btc.c — RIPEMD-160, HASH160, base58check (on ipfs_node's base58btc),
 * bech32 / bech32m (BIP-173, BIP-350) and segwit / P2PKH addresses. */
#include "web4_web3.h"

/* ===== RIPEMD-160 (Dobbertin, Bosselaers, Preneel 1996) ===== */
static uint32_t rol(uint32_t x, uint32_t n)
{
    return (x << n) | (x >> (32 - n));
}

static const uint8_t RL[80] = {0, 1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15,
                               7, 4,  13, 1,  10, 6,  15, 3,  12, 0, 9,  5,  2,  14, 11, 8,
                               3, 10, 14, 4,  9,  15, 8,  1,  2,  7, 0,  6,  13, 11, 5,  12,
                               1, 9,  11, 10, 0,  8,  12, 4,  13, 3, 7,  15, 14, 5,  6,  2,
                               4, 0,  5,  9,  7,  12, 2,  10, 14, 1, 3,  8,  11, 6,  15, 13};
static const uint8_t RR[80] = {5,  14, 7,  0, 9, 2,  11, 4,  13, 6,  15, 8,  1,  10, 3,  12,
                               6,  11, 3,  7, 0, 13, 5,  10, 14, 15, 8,  12, 4,  9,  1,  2,
                               15, 5,  1,  3, 7, 14, 6,  9,  11, 8,  12, 2,  10, 0,  4,  13,
                               8,  6,  4,  1, 3, 11, 15, 0,  5,  12, 2,  13, 9,  7,  10, 14,
                               12, 15, 10, 4, 1, 5,  8,  7,  6,  2,  13, 14, 0,  3,  9,  11};
static const uint8_t SL[80] = {11, 14, 15, 12, 5,  8,  7,  9,  11, 13, 14, 15, 6,  7,  9,  8,
                               7,  6,  8,  13, 11, 9,  7,  15, 7,  12, 15, 9,  11, 7,  13, 12,
                               11, 13, 6,  7,  14, 9,  13, 15, 14, 8,  13, 6,  5,  12, 7,  5,
                               11, 12, 14, 15, 14, 15, 9,  8,  9,  14, 5,  6,  8,  6,  5,  12,
                               9,  15, 5,  11, 6,  8,  13, 12, 5,  12, 13, 14, 11, 8,  5,  6};
static const uint8_t SR[80] = {8,  9,  9,  11, 13, 15, 15, 5,  7,  7,  8,  11, 14, 14, 12, 6,
                               9,  13, 15, 7,  12, 8,  9,  11, 7,  7,  12, 7,  6,  15, 13, 11,
                               9,  7,  15, 11, 8,  6,  6,  14, 12, 13, 5,  14, 13, 13, 7,  5,
                               15, 5,  8,  11, 14, 14, 6,  14, 6,  9,  12, 9,  12, 5,  15, 8,
                               8,  5,  12, 9,  12, 5,  14, 6,  8,  13, 6,  5,  15, 13, 11, 11};
static const uint32_t KL[5] = {0x00000000u, 0x5a827999u, 0x6ed9eba1u, 0x8f1bbcdcu, 0xa953fd4eu};
static const uint32_t KR[5] = {0x50a28be6u, 0x5c4dd124u, 0x6d703ef3u, 0x7a6d76e9u, 0x00000000u};

static uint32_t rmd_f(int j, uint32_t x, uint32_t y, uint32_t z)
{
    switch (j >> 4) {
    case 0:
        return x ^ y ^ z;
    case 1:
        return (x & y) | (~x & z);
    case 2:
        return (x | ~y) ^ z;
    case 3:
        return (x & z) | (y & ~z);
    default:
        return x ^ (y | ~z);
    }
}

static void rmd_block(uint32_t h[5], const uint8_t b[64])
{
    uint32_t X[16];
    for (int i = 0; i < 16; i++) X[i] = w4_le32_get(b + 4 * i);
    uint32_t al = h[0], bl = h[1], cl = h[2], dl = h[3], el = h[4];
    uint32_t ar = h[0], br = h[1], cr = h[2], dr = h[3], er = h[4];
    for (int j = 0; j < 80; j++) {
        uint32_t t = rol(al + rmd_f(j, bl, cl, dl) + X[RL[j]] + KL[j >> 4], SL[j]) + el;
        al = el;
        el = dl;
        dl = rol(cl, 10);
        cl = bl;
        bl = t;
        t = rol(ar + rmd_f(79 - j, br, cr, dr) + X[RR[j]] + KR[j >> 4], SR[j]) + er;
        ar = er;
        er = dr;
        dr = rol(cr, 10);
        cr = br;
        br = t;
    }
    uint32_t t = h[1] + cl + dr;
    h[1] = h[2] + dl + er;
    h[2] = h[3] + el + ar;
    h[3] = h[4] + al + br;
    h[4] = h[0] + bl + cr;
    h[0] = t;
}

void w4_ripemd160(const uint8_t *data, uint32_t len, uint8_t out[20])
{
    uint32_t h[5] = {0x67452301u, 0xefcdab89u, 0x98badcfeu, 0x10325476u, 0xc3d2e1f0u};
    uint32_t off = 0;
    while (len - off >= 64) {
        rmd_block(h, data + off);
        off += 64;
    }
    uint8_t blk[128];
    uint32_t rem = len - off;
    w4_memset(blk, 0, sizeof blk);
    w4_memcpy(blk, data + off, rem);
    blk[rem] = 0x80;
    uint32_t total = (rem + 9 <= 64) ? 64 : 128;
    uint64_t bits = (uint64_t) len << 3;
    w4_le64_put(blk + total - 8, bits);
    rmd_block(h, blk);
    if (total == 128) rmd_block(h, blk + 64);
    for (int i = 0; i < 5; i++) w4_le32_put(out + 4 * i, h[i]);
}

void w4_hash160(const uint8_t *data, uint32_t len, uint8_t out[20])
{
    uint8_t h[32];
    w4_sha256(data, len, h);
    w4_ripemd160(h, 32, out);
}

/* ===== base58check ===== */
#define B58C_MAX 92u

int32_t w4_base58check_encode(const uint8_t *payload, uint32_t len, char *out, uint32_t cap)
{
    if (!payload || len == 0 || len > B58C_MAX) return W4_ERR_ARG;
    uint8_t buf[B58C_MAX + 4], h[32];
    w4_memcpy(buf, payload, len);
    w4_sha256(payload, len, h);
    w4_sha256(h, 32, h);
    w4_memcpy(buf + len, h, 4);
    int r = ipfsn_base58_encode(buf, len + 4, out, cap);
    return r < 0 ? W4_ERR_SPACE : r;
}

int32_t w4_base58check_decode(const char *s, uint32_t len, uint8_t *out, uint32_t cap)
{
    uint8_t buf[B58C_MAX + 4], h[32];
    int n = ipfsn_base58_decode(s, len, buf, sizeof buf);
    if (n < 5) return W4_ERR_PARSE;
    uint32_t pl = (uint32_t) n - 4;
    w4_sha256(buf, pl, h);
    w4_sha256(h, 32, h);
    if (!w4_memeq(h, buf + pl, 4)) return W4_ERR_HASH;
    if (pl > cap) return W4_ERR_SPACE;
    w4_memcpy(out, buf, pl);
    return (int32_t) pl;
}

/* ===== bech32 / bech32m ===== */
static const char B32[] = "qpzry9x8gf2tvdw0s3jn54khce6mua7l";
#define BECH32M_CONST 0x2bc830a3u
#define BECH32_MAXLEN 90u

static uint32_t polymod_step(uint32_t chk, uint8_t v)
{
    static const uint32_t GEN[5] = {0x3b6a57b2u, 0x26508e6du, 0x1ea119fau, 0x3d4233ddu,
                                    0x2a1462b3u};
    uint32_t b = chk >> 25;
    chk = ((chk & 0x1ffffffu) << 5) ^ v;
    for (int i = 0; i < 5; i++)
        if ((b >> i) & 1u) chk ^= GEN[i];
    return chk;
}

static uint32_t hrp_polymod(const char *hrp, uint32_t hl)
{
    uint32_t c = 1;
    for (uint32_t i = 0; i < hl; i++) c = polymod_step(c, (uint8_t) ((uint8_t) hrp[i] >> 5));
    c = polymod_step(c, 0);
    for (uint32_t i = 0; i < hl; i++) c = polymod_step(c, (uint8_t) (hrp[i] & 31));
    return c;
}

int32_t w4_bech32_encode(int variant, const char *hrp, const uint8_t *data5, uint32_t n, char *out,
                         uint32_t cap)
{
    if (variant != W4_BECH32 && variant != W4_BECH32M) return W4_ERR_ARG;
    uint32_t hl = (uint32_t) w4_strnlen(hrp, 84);
    if (hl == 0 || hl + 1 + n + 6 > BECH32_MAXLEN) return W4_ERR_ARG;
    for (uint32_t i = 0; i < hl; i++)
        if (hrp[i] < 33 || hrp[i] > 126 || (hrp[i] >= 'A' && hrp[i] <= 'Z')) return W4_ERR_ARG;
    if (cap < hl + 1 + n + 6 + 1) return W4_ERR_SPACE;
    uint32_t c = hrp_polymod(hrp, hl);
    uint32_t o = 0;
    for (uint32_t i = 0; i < hl; i++) out[o++] = hrp[i];
    out[o++] = '1';
    for (uint32_t i = 0; i < n; i++) {
        if (data5[i] >> 5) return W4_ERR_ARG;
        c = polymod_step(c, data5[i]);
        out[o++] = B32[data5[i]];
    }
    for (int i = 0; i < 6; i++) c = polymod_step(c, 0);
    c ^= (variant == W4_BECH32) ? 1u : BECH32M_CONST;
    for (int i = 0; i < 6; i++) out[o++] = B32[(c >> (5 * (5 - i))) & 31u];
    out[o] = 0;
    return (int32_t) o;
}

int w4_bech32_decode(const char *s, uint32_t len, char *hrp, uint32_t hrp_cap, uint8_t *data5,
                     uint32_t *n, uint32_t cap)
{
    if (!s || len < 8 || len > BECH32_MAXLEN) return W4_ERR_PARSE;
    bool lower = false, upper = false;
    uint32_t sep = len;
    for (uint32_t i = 0; i < len; i++) {
        uint8_t c = (uint8_t) s[i];
        if (c < 33 || c > 126) return W4_ERR_PARSE;
        if (c >= 'a' && c <= 'z') lower = true;
        if (c >= 'A' && c <= 'Z') upper = true;
        if (c == '1') sep = i;
    }
    if (lower && upper) return W4_ERR_PARSE;
    if (sep == len || sep == 0 || len - sep - 1 < 6) return W4_ERR_PARSE;
    if (sep + 1 > hrp_cap || len - sep - 1 - 6 > cap) return W4_ERR_SPACE;
    for (uint32_t i = 0; i < sep; i++) hrp[i] = w4_lower(s[i]);
    hrp[sep] = 0;
    uint32_t c = hrp_polymod(hrp, sep);
    uint32_t nd = 0;
    for (uint32_t i = sep + 1; i < len; i++) {
        char ch = w4_lower(s[i]);
        int v = -1;
        for (int k = 0; k < 32; k++)
            if (B32[k] == ch) v = k;
        if (v < 0) return W4_ERR_PARSE;
        c = polymod_step(c, (uint8_t) v);
        if (i < len - 6) data5[nd++] = (uint8_t) v;
    }
    *n = nd;
    if (c == 1u) return W4_BECH32;
    if (c == BECH32M_CONST) return W4_BECH32M;
    return W4_ERR_HASH;
}

/* Regroup bits. pad: allow a final partial group (encode); otherwise the
 * leftover must be < from bits and all zero (decode). */
static int convert_bits(const uint8_t *in, uint32_t n, uint32_t from, uint32_t to, bool pad,
                        uint8_t *out, uint32_t *on, uint32_t cap)
{
    uint32_t acc = 0, bits = 0, o = 0, maxv = (1u << to) - 1u;
    for (uint32_t i = 0; i < n; i++) {
        if (in[i] >> from) return W4_ERR_PARSE;
        acc = (acc << from) | in[i];
        bits += from;
        while (bits >= to) {
            bits -= to;
            if (o >= cap) return W4_ERR_SPACE;
            out[o++] = (uint8_t) ((acc >> bits) & maxv);
        }
        acc &= (1u << bits) - 1u;
    }
    if (pad) {
        if (bits) {
            if (o >= cap) return W4_ERR_SPACE;
            out[o++] = (uint8_t) ((acc << (to - bits)) & maxv);
        }
    } else if (bits >= from || acc) {
        return W4_ERR_PARSE; /* > 4 bits of padding, or non-zero padding */
    }
    *on = o;
    return W4_OK;
}

int32_t w4_segwit_encode(const char *hrp, uint8_t version, const uint8_t *prog, uint32_t plen,
                         char *out, uint32_t cap)
{
    if (version > 16 || plen < 2 || plen > 40) return W4_ERR_ARG;
    if (version == 0 && plen != 20 && plen != 32) return W4_ERR_ARG;
    uint8_t d[1 + 64];
    uint32_t dn = 0;
    d[0] = version;
    if (convert_bits(prog, plen, 8, 5, true, d + 1, &dn, sizeof d - 1)) return W4_ERR_ARG;
    return w4_bech32_encode(version == 0 ? W4_BECH32 : W4_BECH32M, hrp, d, dn + 1, out, cap);
}

int w4_segwit_decode(const char *hrp, const char *addr, uint32_t len, uint8_t *version,
                     uint8_t *prog, uint32_t *plen)
{
    char h[84];
    uint8_t d[90];
    uint32_t dn = 0;
    int var = w4_bech32_decode(addr, len, h, sizeof h, d, &dn, sizeof d);
    if (var < 0) return var;
    if (!w4_streq(h, hrp)) return W4_ERR_PARSE;
    if (dn < 1 || d[0] > 16) return W4_ERR_PARSE;
    if ((d[0] == 0) != (var == W4_BECH32)) return W4_ERR_HASH; /* wrong checksum variant */
    uint32_t pn = 0;
    if (convert_bits(d + 1, dn - 1, 5, 8, false, prog, &pn, 40)) return W4_ERR_PARSE;
    if (pn < 2 || pn > 40) return W4_ERR_PARSE;
    if (d[0] == 0 && pn != 20 && pn != 32) return W4_ERR_PARSE;
    *version = d[0];
    *plen = pn;
    return W4_OK;
}

int32_t w4_btc_p2pkh(uint8_t version, const uint8_t *pubkey, uint32_t plen, char *out, uint32_t cap)
{
    if (!pubkey || (plen != 33 && plen != 65)) return W4_ERR_ARG;
    uint8_t p[21];
    p[0] = version;
    w4_hash160(pubkey, plen, p + 1);
    return w4_base58check_encode(p, 21, out, cap);
}

int32_t w4_btc_p2wpkh(const char *hrp, const uint8_t pub33[33], char *out, uint32_t cap)
{
    if (!pub33 || (pub33[0] != 0x02 && pub33[0] != 0x03)) return W4_ERR_ARG;
    uint8_t h[20];
    w4_hash160(pub33, 33, h);
    return w4_segwit_encode(hrp, 0, h, 20, out, cap);
}
