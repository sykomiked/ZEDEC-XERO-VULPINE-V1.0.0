/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* vna_cid.c — IPFS CIDv1 raw-block identifiers. See vna_cid.h. */
#include "vna_cid.h"
#include "../robin_debanks/sha256.h"

static const char B32[] = "abcdefghijklmnopqrstuvwxyz234567";

void vna_cid_raw(const uint8_t *data, uint32_t len, uint8_t out[VNA_CID_RAW_LEN])
{
    static const uint8_t none[1] = {0};
    out[0] = 0x01; /* CIDv1 */
    out[1] = 0x55; /* raw */
    out[2] = 0x12; /* sha2-256 */
    out[3] = 0x20; /* 32 bytes */
    sha256(data ? data : none, len, out + 4);
}

bool vna_cid_is_raw(const uint8_t cid[VNA_CID_RAW_LEN])
{
    return cid[0] == 0x01 && cid[1] == 0x55 && cid[2] == 0x12 && cid[3] == 0x20;
}

int32_t vna_cid_to_string(const uint8_t cid[VNA_CID_RAW_LEN], char *out, uint32_t cap)
{
    uint32_t nchars = (VNA_CID_RAW_LEN * 8u + 4u) / 5u; /* 58 */
    if (cap < nchars + 2u) return -1;
    uint32_t o = 0, acc = 0, bits = 0;
    out[o++] = 'b';
    for (uint32_t i = 0; i < VNA_CID_RAW_LEN; i++) {
        acc = (acc << 8) | cid[i];
        bits += 8;
        while (bits >= 5) {
            out[o++] = B32[(acc >> (bits - 5)) & 31u];
            bits -= 5;
        }
    }
    if (bits) out[o++] = B32[(acc << (5 - bits)) & 31u];
    out[o] = 0;
    return (int32_t) o;
}

vna_status_t vna_cid_from_string(const char *s, uint32_t len, uint8_t out[VNA_CID_RAW_LEN])
{
    if (!s || len != 59u || s[0] != 'b') return VNA_ERR_PARSE;
    uint32_t acc = 0, bits = 0, o = 0;
    for (uint32_t i = 1; i < len; i++) {
        char c = s[i];
        uint32_t v;
        if (c >= 'a' && c <= 'z')
            v = (uint32_t) (c - 'a');
        else if (c >= '2' && c <= '7')
            v = 26u + (uint32_t) (c - '2');
        else
            return VNA_ERR_PARSE;
        acc = ((acc << 5) | v) & 0xFFFFu;
        bits += 5;
        if (bits >= 8) {
            if (o >= VNA_CID_RAW_LEN) return VNA_ERR_PARSE;
            out[o++] = (uint8_t) (acc >> (bits - 8));
            bits -= 8;
        }
    }
    if (o != VNA_CID_RAW_LEN || (acc & ((1u << bits) - 1u)) != 0) return VNA_ERR_PARSE;
    return vna_cid_is_raw(out) ? VNA_OK : VNA_ERR_PARSE;
}
