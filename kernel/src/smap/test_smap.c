/* test_smap.c — S-Map: digests must be SHA-256 and reassembly must check bytes.
 *
 * Regression for the old module, which used FNV-1a as a "BLAKE3 substitute"
 * for every digest, and whose smap_reassemble() never wrote the output: it
 * returned the payload size "as proof of reassembly capability", so a tampered
 * shard could never be noticed. Each check below fails on that code.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#include <stdio.h>
#include <string.h>
#include "smap.h"
#include "../robin_debanks/sha256.h"

static int fails = 0;
#define CK(c, m)                                                                                   \
    do {                                                                                           \
        if (c)                                                                                     \
            printf("  ok   %s\n", m);                                                              \
        else {                                                                                     \
            printf("  FAIL %s\n", m);                                                              \
            fails++;                                                                               \
        }                                                                                          \
    } while (0)

static smap_store_t st;
static uint8_t data[10000], shards[10000], out[10000];

static const uint8_t SHA256_ABC[32] = {
    0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
    0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad};

static void node(const uint8_t *l, const uint8_t *r, uint8_t *o)
{
    uint8_t b[65];
    b[0] = 1;
    memcpy(b + 1, l, 32);
    memcpy(b + 33, r, 32);
    sha256(b, 65, o);
}

static void leaf(const uint8_t *d, uint8_t *o)
{
    uint8_t b[33];
    b[0] = 0;
    memcpy(b + 1, d, 32);
    sha256(b, 33, o);
}

int main(void)
{
    printf("=== smap: SHA-256 manifests and verified reassembly ===\n");
    uint8_t h[32];
    smap_compute_cid((const uint8_t *) "abc", 3, h);
    CK(memcmp(h, SHA256_ABC, 32) == 0, "root ID is SHA-256 (FIPS 180-2 KAT)");

    /* Merkle shape for three leaves: H1(H1(L0,L1), L2). */
    uint8_t lv[3][32], a[32], b[32], c[32], ab[32], want[32], got[32];
    for (int i = 0; i < 3; i++) memset(lv[i], 'a' + i, 32);
    leaf(lv[0], a);
    leaf(lv[1], b);
    leaf(lv[2], c);
    node(a, b, ab);
    node(ab, c, want);
    smap_compute_merkle((const uint8_t(*)[32]) lv, 3, got);
    CK(memcmp(got, want, 32) == 0, "Merkle root matches RFC 6962 shape");

    for (uint32_t i = 0; i < sizeof data; i++) data[i] = (uint8_t) ((i * 31u + 7u) ^ (i >> 8));
    smap_store_init(&st);
    int32_t idx = smap_ingest(&st, data, sizeof data, "doc", SMAP_KEY_K2);
    smap_t *sm = &st.smaps[0];
    CK(idx == 0 && sm->num_chunks == 3 && sm->verified, "10000 bytes -> 3 chunks, verified");
    sha256(data, 4096, h);
    CK(memcmp(sm->chunks[0].chunk_hash, h, 32) == 0, "chunk digest is SHA-256 of the chunk");
    CK(smap_seal(sm) == 0, "sealed");

    memcpy(shards, data, sizeof data);
    memset(out, 0, sizeof out);
    int n = smap_reassemble(&st, 0, shards, sizeof shards, out, sizeof out);
    CK(n == (int) sizeof data && memcmp(out, data, sizeof data) == 0,
       "reassembly writes the original bytes");

    shards[5000] ^= 1;
    n = smap_reassemble(&st, 0, shards, sizeof shards, out, sizeof out);
    CK(n == SMAP_ESHARD, "tampered shard rejected");
    shards[5000] ^= 1;

    /* Forge a manifest: swap in the digest of different chunk bytes. Without
     * the manifest hash this would pass; it must not. */
    smap_chunk_t saved = sm->chunks[1];
    sha256(shards + 1, 4096, sm->chunks[1].chunk_hash);
    CK(!smap_verify(sm), "edited chunk digest breaks the manifest hash");
    n = smap_reassemble(&st, 0, shards, sizeof shards, out, sizeof out);
    CK(n == SMAP_ECORRUPT, "reassembly refuses a corrupt manifest");
    sm->chunks[1] = saved;
    sm->chunks[2].original_offset += 1;
    CK(!smap_verify(sm), "edited layout rejected");
    sm->chunks[2].original_offset -= 1;
    CK(smap_verify(sm), "restored manifest verifies again");

    /* Phase shift is a relabel: root unchanged, manifest rehashed and valid. */
    uint8_t root[32];
    memcpy(root, sm->root_cid, 32);
    CK(smap_phase_shift(&st, 0, SMAP_KEY_K4) == 0 && smap_verify(sm) &&
           memcmp(root, sm->root_cid, 32) == 0 && sm->chunks[0].key_index == SMAP_KEY_K4,
       "phase shift relabels without changing the root");
    CK(smap_file_find(&st, root) != 0, "file found by root ID");

    printf("=== %s (%d failure(s)) ===\n", fails ? "FAILED" : "ALL PASSED", fails);
    return fails ? 1 : 0;
}
