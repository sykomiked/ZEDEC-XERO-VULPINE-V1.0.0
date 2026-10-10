/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* web4_cid.c — CIDv1 to/from Web 3 and Web 2 forms, on kernel/src/ipfs_node. */
#include "web4_web3.h"

int w4_cid_of(const uint8_t *data, uint32_t len, ipfsn_cid_t *cid)
{
    if ((!data && len) || !cid) return W4_ERR_ARG;
    return ipfsn_cid_sha256(IPFSN_MC_RAW, data, len, cid) == IPFSN_OK ? W4_OK : W4_ERR_ARG;
}

int w4_cid_to_bytes32(const ipfsn_cid_t *cid, uint8_t out[32], uint32_t *codec)
{
    if (!cid || !out) return W4_ERR_ARG;
    if (cid->mh_code != IPFSN_MH_SHA2_256 || cid->digest_len != 32) return W4_ERR_UNSUPP;
    w4_memcpy(out, cid->digest, 32);
    if (codec) *codec = cid->codec;
    return W4_OK;
}

int w4_cid_from_bytes32(const uint8_t d[32], uint32_t codec, ipfsn_cid_t *cid)
{
    if (!d || !cid) return W4_ERR_ARG;
    if (codec != IPFSN_MC_RAW && codec != IPFSN_MC_DAG_PB) return W4_ERR_UNSUPP;
    w4_memset(cid, 0, sizeof *cid);
    cid->version = 1;
    cid->codec = codec;
    cid->mh_code = IPFSN_MH_SHA2_256;
    cid->digest_len = 32;
    w4_memcpy(cid->digest, d, 32);
    return W4_OK;
}

int32_t w4_cid_uri(const ipfsn_cid_t *cid, char *out, uint32_t cap)
{
    char s[IPFSN_CID_STR_MAX];
    if (ipfsn_cid_to_string(cid, s, sizeof s) < 0) return W4_ERR_ARG;
    w4_w w;
    w4_w_init(&w, out, cap);
    w4_w_str(&w, "ipfs://");
    w4_w_str(&w, s);
    return w4_w_cstr(&w);
}

int w4_cid_uri_parse(const char *s, uint32_t len, ipfsn_cid_t *cid)
{
    if (!s || len <= 7 || !w4_memeq(s, "ipfs://", 7)) return W4_ERR_PARSE;
    uint32_t e = 7;
    while (e < len && s[e] != '/' && s[e] != '?' && s[e] != '#') e++;
    return ipfsn_cid_parse(s + 7, e - 7, cid) == IPFSN_OK ? W4_OK : W4_ERR_PARSE;
}

int32_t w4_cid_gateway_url(const ipfsn_cid_t *cid, const char *gateway_host, char *out,
                           uint32_t cap)
{
    if (!gateway_host || !gateway_host[0]) return W4_ERR_ARG;
    for (const char *p = gateway_host; *p; p++)
        if (*p == '/' || *p == '@' || (uint8_t) *p <= 0x20) return W4_ERR_ARG;
    char s[IPFSN_CID_STR_MAX];
    if (ipfsn_cid_to_string(cid, s, sizeof s) < 0) return W4_ERR_ARG;
    w4_w w;
    w4_w_init(&w, out, cap);
    w4_w_str(&w, "https://");
    w4_w_str(&w, gateway_host);
    w4_w_str(&w, "/ipfs/");
    w4_w_str(&w, s);
    return w4_w_cstr(&w);
}
