/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* layers_boot.h — check the release's lineage at boot, before BOOT_OK hands
 * the machine to the chiglet (docs/LAYERS.md).
 *
 * The core disc carries the manifests of the three layers it stands on, in
 * /ZXV on its ESP: LAYER0.JSN (L0, the hardware envelope), LAYER1.JSN (L1,
 * the core boot engine) and LAYER2.JSN (L2, the swarm). The check, after
 * hw_models_index has read the discs:
 *   1. the three parse (layers.h), L1 names the CID of L0 and L2 that of L1;
 *   2. L0 lists a UEFI machine for this architecture with 4096-byte pages and
 *      512-byte sectors, what this kernel is built for;
 *   3. /EFI/BOOT/<boot file> on the ESP has the size and SHA-256 L1 gives it
 *      (the file the firmware loaded; this kernel is inside it);
 *   4. every core model in MODELS.SHA is the one L1 lists (name, size,
 *      SHA-256) and every model L1 lists is on the core disc; every model of
 *      an attached swarm disc ZXV_SWARM_<G> is one L2 lists in group G.
 *      Swarm discs not attached yet are not missing from the lineage: they
 *      are fetched or attached later, and checked when they mount.
 * A model that fails 4 is refused (hw_models_refuse: [MODEL_BAD], never
 * handed out). If 1 to 3 fail, nothing the layers vouch for can be trusted,
 * so every model is refused. Logs one [LAYER] line per layer, then
 *   [LAYERS_OK] L0 -> L1 -> L2: ...      or
 *   [LAYERS_FAIL] <what failed>          or
 *   [LAYERS] none ...                    (a disc without layer manifests)
 * Run it again after every hw_models_index (a disc that commits mid-boot is
 * indexed afresh). */
#ifndef ZXV_LAYERS_BOOT_H
#define ZXV_LAYERS_BOOT_H

#include "layers.h"
#include "../hwdrv/hw_models.h"

#define ZXV_LAYERS_BOOT_N     3u
#define ZXV_LAYERS_BOOT_BYTES 65536u /* the most of each manifest read at boot */

#define ZXV_LAYERS_NONE 0
#define ZXV_LAYERS_OK   1
#define ZXV_LAYERS_FAIL (-1)

typedef struct zxv_layers_boot {
    struct zxv_layer_node node[ZXV_LAYERS_BOOT_N]; /* L0, L1, L2 as read */
    uint32_t n;                                    /* nodes filled */
    int verdict;                                   /* ZXV_LAYERS_* */
    uint32_t refused;                              /* models refused */
} zxv_layers_boot_t;

/* arch: "x86_64" or "arm64"; efi: its boot file, "BOOTX64.EFI". Returns
 * b->verdict. */
int zxv_layers_boot(zxv_layers_boot_t *b, hw_models_t *s, const hw_plat_t *p, const char *arch,
                    const char *efi);

/* After the post-hydration seal: flag every node SEALED, and AIRGAPPED when
 * the NIC was powered down. */
void zxv_layers_sealed(zxv_layers_boot_t *b, int airgapped);

#endif
