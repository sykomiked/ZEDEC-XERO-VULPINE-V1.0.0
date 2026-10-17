/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* smic_manifest.h — SMIC (Sicilian Military Industrial Complex): the
 * sovereign hardware-provisioning and supply-verification layer's fixed
 * frame. A hardware asset manifest is one 168-byte UBH-168 envelope
 * (schema "SMIC"): the node's identity digest, its hardware-profile
 * digest, capability bitmask, and placement facts. No heap anywhere —
 * the frame is the record.
 *
 * The signed form of a manifest is an attestation datagram:
 *   [168B envelope][2592B ML-DSA-87 pk][4627B ML-DSA-87 sig]
 * (7387 bytes, still bounded) — the signature covers the envelope bytes
 * under the domain "ZXV/SMIC/1". smic_verify.c packs, unpacks, signs and
 * verifies; smic_core.c turns verified manifests into ring admission.
 */
#ifndef SMIC_MANIFEST_H
#define SMIC_MANIFEST_H

#include <stdint.h>
#include "../lpres/lpres_hex.h"

#define SMIC_ENV_BYTES 168u
#define SMIC_FRAME     21u
#define SMIC_PAYLOAD   126u

#define SMIC_SCHEMA    0x534D4943u /* "SMIC" */

/* target_type values (u16) */
#define SMIC_T_MANIFEST  0x0C01u /* hardware profile + provisioning facts */
#define SMIC_T_TELEMETRY 0x0C02u /* live health/probe report             */
#define SMIC_T_RECEIPT   0x0C03u /* provisioning verification receipt    */

/* attestation datagram: envelope + ML-DSA-87 pk + sig */
#define SMIC_ATTEST_BYTES (SMIC_ENV_BYTES + 2592u + 4627u)

/* capability bitmask (u64, payload 64..71) */
#define SMIC_CAP_VMXNET3     (1ull << 0)  /* vmxnet3 NIC (fleet-standard)     */
#define SMIC_CAP_VIRTIO_PCI  (1ull << 1)
#define SMIC_CAP_VIRTIO_MMIO (1ull << 2)
#define SMIC_CAP_GPU         (1ull << 3)  /* compute-class accelerator      */
#define SMIC_CAP_TPM         (1ull << 4)  /* measured-boot root             */
#define SMIC_CAP_AIRGAP      (1ull << 5)  /* provisioning was off-network   */
#define SMIC_CAP_VERIFIED    (1ull << 6)  /* boot chain attested            */
#define SMIC_CAP_DISK_900G   (1ull << 7)  /* >= 900 GB (mainframe capacity) */
#define SMIC_CAP_NET_1G      (1ull << 8)  /* >= 1 Gbps uplink               */
#define SMIC_CAP_IPFS_PIN    (1ull << 9)  /* full release pinning duty      */
#define SMIC_CAP_INDEX       (1ull << 10) /* CISEX index serving            */

/* placement */
#define SMIC_REGIONS 9u /* 8 armada probe regions + SIN mainframe (slot 8) */
#define SMIC_REGION_MAINFRAME 8u

enum {
    SMIC_ROLE_RELAY = 0,    /* index/relay node                */
    SMIC_ROLE_MAINFRAME = 1, /* protocol anchor, global index   */
    SMIC_ROLE_WAREHOUSE = 2, /* bulk pinning / artifact host    */
};

enum {
    SMIC_ARCH_NONE = 0,
    SMIC_ARCH_X86_64 = 1,
    SMIC_ARCH_ARM64 = 2,
    SMIC_ARCH_RISCV = 3,
};

/* status codes */
enum {
    SMIC_OK = 0,
    SMIC_ERR_ARG = -1,
    SMIC_ERR_MALFORMED = -2, /* bad frame, magic, schema, integrity      */
    SMIC_ERR_SIG = -3,       /* attestation signature failed             */
    SMIC_ERR_FULL = -4,      /* registry has no free slot                */
    SMIC_ERR_NOTFOUND = -5,
    SMIC_ERR_SPACE = -6,
};

/* A decoded hardware asset manifest (payload 0..125, big-endian multibyte):
 *   0..31   node_id     SHA-256 of the machine identity (MAC-derived, like
 *                        the Vinea node id — stable across reinstalls)
 *   32..63  hw_digest   SHA-256 of the hardware-profile description
 *   64..71  caps        capability bitmask (SMIC_CAP_*)
 *   72..79  provisioned_at  epoch seconds of this provisioning
 *   80..87  disk_mb     usable disk
 *   88..89  net_mbps    uplink
 *   90      region      0..8 (8 = SIN mainframe)
 *   91      role        SMIC_ROLE_*
 *   92      octane      serving grade (swarm_denom scale)
 *   93      lpx         operator-declared health tag (audit only)
 *   94      arch        SMIC_ARCH_*
 *   95      flags       provisioning flags
 *   96..99  fw_crc      firmware/build identity crc32
 *   100..125 reserved   zero
 */
typedef struct {
    uint8_t node_id[32];
    uint8_t hw_digest[32];
    uint64_t caps;
    uint64_t provisioned_at;
    uint64_t disk_mb;
    uint16_t net_mbps;
    uint8_t region;
    uint8_t role;
    uint8_t octane;
    lpx_t lpx;
    uint8_t arch;
    uint8_t flags;
    uint32_t fw_crc;
} smic_hw_t;

/* Pack/unpack the 168-byte envelope. SMIC_OK or SMIC_ERR_*; unpack
 * validates the header (magic, version, class, schema, payload length)
 * AND the integrity trailer's 3-byte digest over the frame. */
int smic_manifest_pack(uint16_t type, const smic_hw_t *hw, uint8_t out[SMIC_ENV_BYTES]);
int smic_manifest_unpack(const uint8_t in[SMIC_ENV_BYTES], uint16_t *type, smic_hw_t *hw);

#endif /* SMIC_MANIFEST_H */
