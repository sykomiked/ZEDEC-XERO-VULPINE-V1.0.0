/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* layers.h — read the release's layer manifests and check their lineage
 * (docs/LAYERS.md; written by build_system/mk_layers.py).
 *
 * A manifest is JSON in one fixed layout, so it is read line by line here
 * without a JSON parser; any other layout is refused (it would also be a
 * different CID):
 *
 *   {
 *    "format": "zxv-layer 1",
 *    "layer_depth": 1,
 *    "layer_name": "CORE_BOOT_ENGINE",
 *    "parent_layer_cid": "bafkrei...",      null at L0
 *    "parent_compat": "exact",              or "evo_comm" at L3 and L4
 *    "objects": [
 *     {"kind": "boot", "arch": "x86_64", "name": "BOOTX64.EFI", "bytes": N, "sha256": "..."},
 *     {"kind": "model", ...}
 *    ]
 *   }
 *
 * A layer's CID is the CIDv1 raw sha2-256 of its manifest's bytes; the
 * manifest is at most 1 MiB. A chain is given root first: L0 has no parent,
 * each later layer names the CID of the one before it, depth rises by one
 * (L4 may follow L4). An "evo_comm" layer may instead name a parent the
 * caller admits under the evo_comm compatibility contract; L1 and L2 are
 * always exact.
 *
 * Freestanding C11, no allocation. */
#ifndef ZXV_LAYERS_H
#define ZXV_LAYERS_H

#include <stdint.h>
#include "layer_manifest.h"

#define ZXV_LAYER_BYTES_MAX (1u << 20)

typedef struct zxv_layer {
    uint8_t depth;
    uint8_t compat; /* 1 = evo_comm */
    uint8_t cid[ZXV_LAYER_CID_BYTES];
    uint8_t parent[ZXV_LAYER_CID_BYTES]; /* zero at L0 */
    const char *objs;                    /* the object lines */
    uint32_t objs_len;
    uint32_t nobj;
} zxv_layer_t;

typedef struct zxv_layer_obj {
    char kind[12];
    char arch[12];
    char group[17];
    char name[64];
    uint64_t bytes;
    uint8_t sha[32];
    uint8_t has_bytes, has_sha;
    /* L0 machines */
    char firmware[8];
    uint64_t page_bytes, sector_bytes;
} zxv_layer_obj_t;

/* Parse manifest t[0..n): 0 and *L (its CID computed), else -1 with *why. */
int zxv_layer_parse(const char *t, uint32_t n, zxv_layer_t *L, const char **why);
/* The next object: 1 with *o, 0 at the end. *pos starts at 0. Objects were
 * checked by zxv_layer_parse, so this does not fail. */
int zxv_layer_next(const zxv_layer_t *L, uint32_t *pos, zxv_layer_obj_t *o);

/* Check a chain, root first. admit (may be NULL) says whether an evo_comm
 * layer may stand on `parent` although it is not the CID below. 0 = an
 * unbroken chain; else -1, *at the first layer that fails and *why. */
int zxv_layer_chain(const zxv_layer_t *const *L, uint32_t n,
                    int (*admit)(const uint8_t parent[ZXV_LAYER_CID_BYTES], void *ctx), void *ctx,
                    uint32_t *at, const char **why);

/* "bafkrei..." for a layer CID (59 characters and NUL). */
void zxv_layer_cid_text(const uint8_t cid[ZXV_LAYER_CID_BYTES], char out[61]);

#endif
