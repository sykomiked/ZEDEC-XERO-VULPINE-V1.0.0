/* ipfsn_multiformats.c — unsigned varint, multihash, CIDv0/v1, base32, base58.
 *
 * Every parser is strict and fails closed: non-minimal varints, unknown
 * multihash functions, wrong digest lengths, uppercase or padded base32,
 * stray trailing bits and CIDv0 bytes inside a multibase string are all
 * rejected rather than "fixed". Two spellings of one CID would let one piece
 * of content carry two names, which is exactly what content addressing exists
 * to prevent.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#include "ipfs_node.h"
#include "ipfsn_util.h"
#include "../robin_debanks/sha256.h"

/* ---- varint -------------------------------------------------------------- */

uint32_t ipfsn_varint_put(uint64_t v, uint8_t *out, uint32_t cap)
{
    uint32_t n = 0;
    if (!out || (v >> 63)) return 0;
    do {
        if (n >= cap) return 0;
        uint8_t b = (uint8_t) (v & 0x7Fu);
        v >>= 7;
        if (v) b |= 0x80u;
        out[n++] = b;
    } while (v);
    return n;
}

int ipfsn_varint_get(const uint8_t *in, uint32_t len, uint64_t *v)
{
    uint64_t acc = 0;
    if (!in || !v) return IPFSN_ERR_ARG;
    for (uint32_t i = 0; i < IPFSN_VARINT_MAX; i++) {
        if (i >= len) return IPFSN_ERR_MALFORMED; /* truncated */
        uint8_t b = in[i];
        acc |= (uint64_t) (b & 0x7Fu) << (7u * i);
        if (!(b & 0x80u)) {
            if (i > 0 && b == 0) return IPFSN_ERR_MALFORMED; /* non-minimal */
            *v = acc;
            return (int) (i + 1);
        }
    }
    return IPFSN_ERR_MALFORMED; /* a 9th byte with the continuation bit */
}

/* ---- CID construction ---------------------------------------------------- */

int ipfsn_cid_sha256(uint32_t codec, const uint8_t *data, uint32_t len, ipfsn_cid_t *cid)
{
    if (!cid || (!data && len)) return IPFSN_ERR_ARG;
    ipfsn__set(cid, 0, (uint32_t) sizeof(*cid));
    cid->version = 1;
    cid->codec = codec;
    cid->mh_code = IPFSN_MH_SHA2_256;
    cid->digest_len = SHA256_DIGEST_LEN;
    sha256(len ? data : (const uint8_t *) "", len, cid->digest);
    return IPFSN_OK;
}

int ipfsn_cid_identity(uint32_t codec, const uint8_t *data, uint32_t len, ipfsn_cid_t *cid)
{
    if (!cid || (!data && len)) return IPFSN_ERR_ARG;
    if (len > IPFSN_DIGEST_MAX) return IPFSN_ERR_SPACE;
    ipfsn__set(cid, 0, (uint32_t) sizeof(*cid));
    cid->version = 1;
    cid->codec = codec;
    cid->mh_code = IPFSN_MH_IDENTITY;
    cid->digest_len = (uint8_t) len;
    if (len) ipfsn__cpy(cid->digest, data, len);
    return IPFSN_OK;
}

int ipfsn_cid_verify(const ipfsn_cid_t *cid, const uint8_t *data, uint32_t len)
{
    if (!cid || (!data && len)) return IPFSN_ERR_ARG;
    if (cid->mh_code == IPFSN_MH_SHA2_256) {
        uint8_t d[SHA256_DIGEST_LEN];
        if (cid->digest_len != SHA256_DIGEST_LEN) return IPFSN_ERR_MALFORMED;
        sha256(len ? data : (const uint8_t *) "", len, d);
        return ipfsn__eq(d, cid->digest, SHA256_DIGEST_LEN) ? IPFSN_OK : IPFSN_ERR_HASH;
    }
    if (cid->mh_code == IPFSN_MH_IDENTITY) {
        if (len != cid->digest_len) return IPFSN_ERR_HASH;
        return ipfsn__eq(data, cid->digest, len) ? IPFSN_OK : IPFSN_ERR_HASH;
    }
    return IPFSN_ERR_UNSUPP;
}

bool ipfsn_cid_equal(const ipfsn_cid_t *a, const ipfsn_cid_t *b)
{
    if (!a || !b) return false;
    return a->version == b->version && a->codec == b->codec && a->mh_code == b->mh_code &&
           a->digest_len == b->digest_len && ipfsn__eq(a->digest, b->digest, a->digest_len);
}

void ipfsn_cid_to_v1(const ipfsn_cid_t *in, ipfsn_cid_t *out)
{
    if (!in || !out) return;
    if (out != in) ipfsn__cid_copy(out, in);
    if (out->version == 0) {
        out->version = 1;
        out->codec = IPFSN_MC_DAG_PB;
    }
}

/* ---- binary CIDs --------------------------------------------------------- */

int ipfsn_cid_encode(const ipfsn_cid_t *cid, uint8_t *out, uint32_t cap)
{
    uint32_t n = 0, k;
    if (!cid || !out) return IPFSN_ERR_ARG;
    if (cid->digest_len > IPFSN_DIGEST_MAX) return IPFSN_ERR_ARG;
    if (cid->mh_code == IPFSN_MH_SHA2_256 && cid->digest_len != SHA256_DIGEST_LEN)
        return IPFSN_ERR_ARG;
    if (cid->mh_code != IPFSN_MH_SHA2_256 && cid->mh_code != IPFSN_MH_IDENTITY)
        return IPFSN_ERR_UNSUPP;
    if (cid->version == 0) {
        if (cid->codec != IPFSN_MC_DAG_PB || cid->mh_code != IPFSN_MH_SHA2_256)
            return IPFSN_ERR_ARG;
    } else if (cid->version == 1) {
        if (!(k = ipfsn_varint_put(1, out + n, cap - n))) return IPFSN_ERR_SPACE;
        n += k;
        if (!(k = ipfsn_varint_put(cid->codec, out + n, cap - n))) return IPFSN_ERR_SPACE;
        n += k;
    } else {
        return IPFSN_ERR_ARG;
    }
    if (!(k = ipfsn_varint_put(cid->mh_code, out + n, cap - n))) return IPFSN_ERR_SPACE;
    n += k;
    if (!(k = ipfsn_varint_put(cid->digest_len, out + n, cap - n))) return IPFSN_ERR_SPACE;
    n += k;
    if (cap - n < cid->digest_len) return IPFSN_ERR_SPACE;
    ipfsn__cpy(out + n, cid->digest, cid->digest_len);
    return (int) (n + cid->digest_len);
}

int ipfsn_cid_decode(const uint8_t *in, uint32_t len, ipfsn_cid_t *cid)
{
    uint32_t n = 0;
    uint64_t v, code, dlen;
    int k;
    if (!in || !cid) return IPFSN_ERR_ARG;
    ipfsn__set(cid, 0, (uint32_t) sizeof(*cid));
    if (len >= 2 && in[0] == 0x12u && in[1] == 0x20u) { /* CIDv0: bare sha2-256 multihash */
        if (len < 34) return IPFSN_ERR_MALFORMED;
        cid->version = 0;
        cid->codec = IPFSN_MC_DAG_PB;
        cid->mh_code = IPFSN_MH_SHA2_256;
        cid->digest_len = 32;
        ipfsn__cpy(cid->digest, in + 2, 32);
        return 34;
    }
    if ((k = ipfsn_varint_get(in, len, &v)) < 0) return k;
    n += (uint32_t) k;
    if (v != 1) return IPFSN_ERR_MALFORMED; /* only CIDv1 has a version varint */
    if ((k = ipfsn_varint_get(in + n, len - n, &v)) < 0) return k;
    n += (uint32_t) k;
    if (v > 0xFFFFFFFFu) return IPFSN_ERR_UNSUPP;
    cid->codec = (uint32_t) v;
    if ((k = ipfsn_varint_get(in + n, len - n, &code)) < 0) return k;
    n += (uint32_t) k;
    if ((k = ipfsn_varint_get(in + n, len - n, &dlen)) < 0) return k;
    n += (uint32_t) k;
    if (code == IPFSN_MH_SHA2_256) {
        if (dlen != 32) return IPFSN_ERR_MALFORMED;
    } else if (code == IPFSN_MH_IDENTITY) {
        if (dlen > IPFSN_DIGEST_MAX) return IPFSN_ERR_UNSUPP;
    } else {
        return IPFSN_ERR_UNSUPP;
    }
    if (len - n < dlen) return IPFSN_ERR_MALFORMED;
    cid->version = 1;
    cid->mh_code = (uint32_t) code;
    cid->digest_len = (uint8_t) dlen;
    ipfsn__cpy(cid->digest, in + n, (uint32_t) dlen);
    return (int) (n + (uint32_t) dlen);
}

int ipfsn_cid_decode_exact(const uint8_t *in, uint32_t len, ipfsn_cid_t *cid)
{
    int k = ipfsn_cid_decode(in, len, cid);
    if (k < 0) return k;
    return ((uint32_t) k == len) ? IPFSN_OK : IPFSN_ERR_MALFORMED;
}

/* ---- base32 (RFC 4648, lowercase, no padding) ---------------------------- */

static const char B32[] = "abcdefghijklmnopqrstuvwxyz234567";

int ipfsn_base32_encode(const uint8_t *in, uint32_t len, char *out, uint32_t cap)
{
    uint32_t acc = 0, bits = 0, n = 0;
    if ((!in && len) || !out) return IPFSN_ERR_ARG;
    for (uint32_t i = 0; i < len; i++) {
        acc = (acc << 8) | in[i];
        bits += 8;
        while (bits >= 5) {
            if (n + 1 >= cap) return IPFSN_ERR_SPACE;
            out[n++] = B32[(acc >> (bits - 5)) & 31u];
            bits -= 5;
        }
        acc &= (1u << bits) - 1u;
    }
    if (bits) {
        if (n + 1 >= cap) return IPFSN_ERR_SPACE;
        out[n++] = B32[(acc << (5 - bits)) & 31u];
    }
    if (n >= cap) return IPFSN_ERR_SPACE;
    out[n] = 0;
    return (int) n;
}

int ipfsn_base32_decode(const char *in, uint32_t len, uint8_t *out, uint32_t cap)
{
    uint32_t acc = 0, bits = 0, n = 0, r = len & 7u;
    if ((!in && len) || !out) return IPFSN_ERR_ARG;
    if (r == 1 || r == 3 || r == 6) return IPFSN_ERR_MALFORMED; /* impossible lengths */
    for (uint32_t i = 0; i < len; i++) {
        char c = in[i];
        uint32_t v;
        if (c >= 'a' && c <= 'z')
            v = (uint32_t) (c - 'a');
        else if (c >= '2' && c <= '7')
            v = (uint32_t) (c - '2') + 26u;
        else
            return IPFSN_ERR_MALFORMED;
        acc = (acc << 5) | v;
        bits += 5;
        if (bits >= 8) {
            if (n >= cap) return IPFSN_ERR_SPACE;
            out[n++] = (uint8_t) (acc >> (bits - 8));
            bits -= 8;
            acc &= (1u << bits) - 1u;
        }
    }
    if (acc != 0) return IPFSN_ERR_MALFORMED; /* non-zero leftover bits */
    return (int) n;
}

/* ---- base58btc ------------------------------------------------------------ */

static const char B58[] = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
#define B58_MAX_IN 96u

int ipfsn_base58_encode(const uint8_t *in, uint32_t len, char *out, uint32_t cap)
{
    uint8_t dig[(B58_MAX_IN * 138u) / 100u + 2u];
    uint32_t nd = 0, zeros = 0, n = 0;
    if ((!in && len) || !out) return IPFSN_ERR_ARG;
    if (len > B58_MAX_IN) return IPFSN_ERR_SPACE;
    while (zeros < len && in[zeros] == 0) zeros++;
    for (uint32_t i = zeros; i < len; i++) {
        uint32_t carry = in[i];
        for (uint32_t j = 0; j < nd; j++) {
            carry += (uint32_t) dig[j] << 8;
            dig[j] = (uint8_t) (carry % 58u);
            carry /= 58u;
        }
        while (carry) {
            dig[nd++] = (uint8_t) (carry % 58u);
            carry /= 58u;
        }
    }
    if (zeros + nd + 1 > cap) return IPFSN_ERR_SPACE;
    for (uint32_t i = 0; i < zeros; i++) out[n++] = '1';
    for (uint32_t i = 0; i < nd; i++) out[n++] = B58[dig[nd - 1 - i]];
    out[n] = 0;
    return (int) n;
}

int ipfsn_base58_decode(const char *in, uint32_t len, uint8_t *out, uint32_t cap)
{
    uint8_t b[B58_MAX_IN];
    uint32_t nb = 0, zeros = 0;
    if ((!in && len) || !out) return IPFSN_ERR_ARG;
    if (len > (B58_MAX_IN * 138u) / 100u) return IPFSN_ERR_SPACE;
    while (zeros < len && in[zeros] == '1') zeros++;
    for (uint32_t i = zeros; i < len; i++) {
        uint32_t v = 58;
        for (uint32_t k = 0; k < 58; k++)
            if (B58[k] == in[i]) v = k;
        if (v == 58) return IPFSN_ERR_MALFORMED;
        uint32_t carry = v;
        for (uint32_t j = 0; j < nb; j++) {
            carry += (uint32_t) b[j] * 58u;
            b[j] = (uint8_t) carry;
            carry >>= 8;
        }
        while (carry) {
            if (nb >= B58_MAX_IN) return IPFSN_ERR_SPACE;
            b[nb++] = (uint8_t) carry;
            carry >>= 8;
        }
    }
    /* leading '1's count toward the bound too: the decoder used to return up
     * to 96 + zeros bytes, which ipfsn_base58_encode then refused, so a
     * decoded value could not be re-encoded (found by fuzz_ipfs_cid) */
    if (zeros + nb > B58_MAX_IN) return IPFSN_ERR_SPACE;
    if (zeros + nb > cap) return IPFSN_ERR_SPACE;
    for (uint32_t i = 0; i < zeros; i++) out[i] = 0;
    for (uint32_t i = 0; i < nb; i++) out[zeros + i] = b[nb - 1 - i];
    return (int) (zeros + nb);
}

/* ---- CID strings ---------------------------------------------------------- */

int ipfsn_cid_to_string(const ipfsn_cid_t *cid, char *out, uint32_t cap)
{
    uint8_t bin[IPFSN_CID_BIN_MAX];
    int n, k;
    if (!cid || !out || cap < 2) return IPFSN_ERR_ARG;
    if ((n = ipfsn_cid_encode(cid, bin, sizeof bin)) < 0) return n;
    if (cid->version == 0) return ipfsn_base58_encode(bin, (uint32_t) n, out, cap);
    out[0] = 'b';
    if ((k = ipfsn_base32_encode(bin, (uint32_t) n, out + 1, cap - 1)) < 0) return k;
    return k + 1;
}

int ipfsn_cid_parse(const char *s, uint32_t len, ipfsn_cid_t *cid)
{
    uint8_t bin[IPFSN_CID_BIN_MAX];
    int n, r;
    if (!s || !cid) return IPFSN_ERR_ARG;
    if (len == 46 && s[0] == 'Q' && s[1] == 'm') {
        if ((n = ipfsn_base58_decode(s, len, bin, sizeof bin)) < 0) return n;
        if (n != 34 || bin[0] != 0x12u || bin[1] != 0x20u) return IPFSN_ERR_MALFORMED;
        return ipfsn_cid_decode_exact(bin, 34, cid);
    }
    if (len < 2 || s[0] != 'b') return IPFSN_ERR_MALFORMED;
    if ((n = ipfsn_base32_decode(s + 1, len - 1, bin, sizeof bin)) < 0) return n;
    if ((r = ipfsn_cid_decode_exact(bin, (uint32_t) n, cid)) < 0) return r;
    if (cid->version != 1) return IPFSN_ERR_MALFORMED; /* CIDv0 has no multibase form */
    return IPFSN_OK;
}
