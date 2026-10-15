/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* layer_manifest.h — the kernel's table of release layers (L0 hardware
 * envelope, L1 core boot engine, L2 swarm, L3 the Library of Alexandria, L4
 * community overlays), one node per layer it has read and checked. A layer
 * is named by the CID of its manifest and names its parent's CID; the
 * manifest format and the checks are kernel/src/layers/layers.h, the spec
 * docs/LAYERS.md. */
#ifndef ZXV_LAYER_MANIFEST_H
#define ZXV_LAYER_MANIFEST_H

#include <stdint.h>

#define ZXV_LAYER_CID_BYTES 36u /* CIDv1 raw sha2-256: 01 55 12 20 + digest */
#define ZXV_LAYER_MAX       8u

/* flags */
#define ZXV_LAYER_VERIFIED  0x01u /* lineage and contents checked this boot */
#define ZXV_LAYER_SEALED    0x02u /* the network that brought it is severed */
#define ZXV_LAYER_AIRGAPPED 0x04u /* ... by powering the NIC down */
#define ZXV_LAYER_OVERLAY   0x08u /* L4: a community overlay */
#define ZXV_LAYER_COMPAT    0x10u /* stands on a parent admitted under evo_comm */
#define ZXV_LAYER_BAKED     0x20u /* L0, and the one this kernel's baked table lists */

struct zxv_layer_node {
    uint8_t depth;
    uint8_t layer_cid[ZXV_LAYER_CID_BYTES];
    uint8_t parent_cid[ZXV_LAYER_CID_BYTES]; /* all zero at L0 */
    uint32_t flags;
    uint64_t sector_start; /* the partition the manifest was read from */
    uint64_t sector_count;
};

#endif
