/* zsp.h — ZXV Signed Package verification (Ed25519 + SHA-256)
 *
 * A .zsp binds a payload (an ELF) to a signature over its SHA-256 under
 * an offline root key. The kernel loads code only if BOTH the hash
 * (integrity) and the signature (authenticity, against a compiled-in
 * root public key) verify. This is the on-target trust chain the audit
 * requires (P0-3): a root key gates what executes.
 *
 * Layout (little-endian), then payload:
 *   magic[4]="ZSP1" | payload_len:u32 | sha256[32] | sig[64] | payload
 * The signed preimage is (magic || payload_len || sha256) = 40 bytes.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)  (ZXV signed-package slice)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#ifndef ZXV_ZSP_H
#define ZXV_ZSP_H

#include <stdint.h>
#include <stdbool.h>

#define ZSP_MAGIC0 'Z'
#define ZSP_MAGIC1 'S'
#define ZSP_MAGIC2 'P'
#define ZSP_MAGIC3 '1'
#define ZSP_PREIMAGE_LEN 40   /* v1: magic(4)+len(4)+sha256(32) */
#define ZSP_HEADER_LEN   104  /* v1: preimage(40)+sig(64) */

/* ---- ZSP v2 (audit P0-6): the signed preimage now covers version, arch, ABI,
 * capabilities, key-id and a package identity — so none of those can be swapped
 * without breaking the signature — and the loader enforces a monotonic version
 * floor so an old (validly-signed) package cannot be rolled back in.
 *
 * Layout (little-endian), then payload:
 *   [0]  magic[4]="ZSP2"  [4] hdr_len:u16  [6] abi:u16   [8]  version:u32
 *   [12] arch:u16         [14] rsvd:u16     [16] caps:u32 [20] payload_len:u32
 *   [24] key_id[8]        [32] identity[32] [64] sha256[32]
 *   --- signed preimage = bytes [0,96) ---  [96] sig[64]  [160] payload
 */
#define ZSP2_MAGIC3      '2'
#define ZSP2_PREIMAGE_LEN 96
#define ZSP2_HEADER_LEN   160

/* Architecture ids (must match sign_package.py --arch). 0 = "don't care". */
#define ZSP_ARCH_ANY     0
#define ZSP_ARCH_ARM64   1
#define ZSP_ARCH_X86_64  2
#define ZSP_ARCH_RISCV64 3
#define ZSP_ARCH_RISCV32 4
#define ZSP_ARCH_ARM32   5

typedef enum {
    ZSP_OK = 0,
    ZSP_ERR_SHORT    = -1,  /* buffer smaller than header / declared payload */
    ZSP_ERR_MAGIC    = -2,  /* bad magic */
    ZSP_ERR_HASH     = -3,  /* SHA-256 mismatch (payload tampered) */
    ZSP_ERR_SIG      = -4,  /* Ed25519 signature invalid (not from root key) */
    ZSP_ERR_ROLLBACK = -5,  /* v2: version below the monotonic rollback floor */
    ZSP_ERR_ARCH     = -6,  /* v2: package built for a different architecture */
    ZSP_ERR_KEYID    = -7,  /* v2: key-id does not match the root public key */
} zsp_result_t;

/* The authenticated metadata carried by a ZSP v2 package. */
typedef struct {
    uint16_t abi;
    uint16_t arch;
    uint32_t version;
    uint32_t caps;
    uint32_t payload_len;
    uint8_t  key_id[8];
    uint8_t  identity[32];
} zsp_meta_t;

/* Verify a .zsp buffer against `root_pubkey` (32 bytes). Accepts BOTH ZSP1 and
 * ZSP2 (integrity + signature only — no version/arch enforcement). On success
 * sets *payload / *payload_len to point inside `buf` and returns ZSP_OK. */
zsp_result_t zsp_verify(const uint8_t *buf, uint32_t len,
                        const uint8_t root_pubkey[32],
                        const uint8_t **payload, uint32_t *payload_len);

/* Verify a ZSP v2 package with FULL enforcement: integrity, signature over the
 * rich preimage, key-id == SHA-256(root_pubkey)[0:8], arch == expected_arch
 * (unless expected_arch is ZSP_ARCH_ANY), and version >= min_version (the
 * monotonic anti-rollback floor). Fills *meta on success. A ZSP1 package is
 * rejected here (ZSP_ERR_MAGIC) — v2 is required for enforced execution. */
zsp_result_t zsp_verify2(const uint8_t *buf, uint32_t len,
                         const uint8_t root_pubkey[32],
                         uint32_t min_version, uint16_t expected_arch,
                         zsp_meta_t *meta,
                         const uint8_t **payload, uint32_t *payload_len);

const char *zsp_strerror(zsp_result_t r);

#endif /* ZXV_ZSP_H */
