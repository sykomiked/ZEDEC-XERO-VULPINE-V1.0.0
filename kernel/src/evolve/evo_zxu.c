/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* evo_zxu.c — binds lineage trust to zx_upcheck's per-bucket release keys:
 * a publisher is trusted for lineages iff the user trusts its ML-DSA-65 key
 * in some enabled update bucket. The key id is SHA3-256 of the raw 1952-byte
 * key, so a lineage author signs with that same raw key (verify hook:
 * pq_mldsa65_verify) for the ids to match. */
#include "evo.h"
#include "zx_upcheck.h"

bool evo_trust_zxu(const uint8_t key_id[32], void *zxu_config)
{
    const zxu_config_t *c = (const zxu_config_t *) zxu_config;
    if (!c || !key_id) return false;
    for (uint32_t b = 0; b < ZXU_MAX_BUCKETS; b++) {
        const zxu_bucket_t *k = &c->b[b];
        if (!k->used || !k->enabled) continue;
        for (uint32_t i = 0; i < k->nkeys && i < ZXU_MAX_KEYS; i++) {
            uint8_t id[32];
            evo_key_id(k->keys[i], ZXU_PK_BYTES, id);
            uint8_t diff = 0;
            for (uint32_t j = 0; j < 32; j++) diff |= (uint8_t) (id[j] ^ key_id[j]);
            if (!diff) return true;
        }
    }
    return false;
}
