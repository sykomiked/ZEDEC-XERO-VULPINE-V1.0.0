/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* interspace.c — the res-communis commons. Owner is pinned; seizure is refused. */

#include "interspace.h"

void interstitial_open(interstitial_region_t *r,
                       const uint8_t region_id[ZXV_HASH_LEN],
                       commons_kind_t kind) {
    if (!r) return;
    for (uint32_t i = 0; i < ZXV_HASH_LEN; i++)
        r->region_id[i] = region_id ? region_id[i] : 0;
    r->kind  = kind;
    r->owner = ZXV_NODE_NONE;   /* the pin — non-negotiable */
}

bool interstitial_is_commons(const interstitial_region_t *r) {
    return r && r->kind == ZXV_RES_COMMUNIS;
}

zxv_status_t interstitial_claim(interstitial_region_t *r, zxv_node_id_t claimant) {
    (void)claimant;   /* it does not matter who asks — nobody may seize the sea */
    if (!r) return ZXV_EPERM;
    /* Belt-and-braces: the pin should already hold, but if anyone ever wedged an
     * owner in, refuse to honour it and re-pin. */
    r->owner = ZXV_NODE_NONE;
    if (r->kind == ZXV_RES_COMMUNIS)
        return ZXV_EIMMUNE;   /* common to all, seizable by none */
    return ZXV_EPERM;         /* res nullius: still not yours to unilaterally take */
}
