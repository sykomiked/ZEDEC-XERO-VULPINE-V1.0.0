/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* vna_cid.h — the ONE place Vinea computes content identifiers.
 *
 * A raw-block CID here is a real IPFS CIDv1:
 *     0x01 (CIDv1) || 0x55 (multicodec raw) || 0x12 (multihash sha2-256) ||
 *     0x20 (digest length 32) || SHA-256(block)
 * and its text form is multibase base32-lower ('b' prefix, RFC 4648 alphabet,
 * no padding), e.g. "hello world" -> bafkreifzjut3te2nhyekklss27nh3k72ysco7y32koao5eei66wof36n5e.
 * The digest is the kernel's SHA-256 (src/robin_debanks/sha256.h) — the same
 * digest src/ipfs/ipfs.h calls a CID (ipfs_cid_from_bytes), wrapped in the
 * multiformat prefix so an IPFS node addresses the same block.
 *
 * Every other Vinea file goes through vna_cid_raw(), so the backing
 * implementation can later be switched to src/ipfs_node without touching any
 * caller.
 */
#ifndef VNA_CID_H
#define VNA_CID_H

#include "vna_common.h"

#define VNA_CID_RAW_LEN 36u /* binary CIDv1 raw sha2-256 */
#define VNA_CID_STR_MAX 64u /* 'b' + 58 base32 chars + NUL fits */

/* CIDv1 / raw / sha2-256 of data[0..len). */
void vna_cid_raw(const uint8_t *data, uint32_t len, uint8_t out[VNA_CID_RAW_LEN]);

/* True iff cid is a well-formed CIDv1 raw sha2-256 (prefix check only). */
bool vna_cid_is_raw(const uint8_t cid[VNA_CID_RAW_LEN]);

/* Multibase base32-lower text form. Returns the string length (excluding the
 * NUL), or -1 if cap is too small. */
int32_t vna_cid_to_string(const uint8_t cid[VNA_CID_RAW_LEN], char *out, uint32_t cap);

/* Parse the text form back. Returns VNA_OK or VNA_ERR_PARSE. */
vna_status_t vna_cid_from_string(const char *s, uint32_t len, uint8_t out[VNA_CID_RAW_LEN]);

#endif /* VNA_CID_H */
