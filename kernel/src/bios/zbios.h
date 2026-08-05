/* zbios.h — ZXV staged measured boot ("the ten-key BIOS")
 *
 * WHAT THIS IS
 * ------------
 * A ten-stage chain-of-trust boot. Each stage verifies the next stage's
 * signature before handing control to it, and extends a running
 * measurement over what it just authorised. This is the standard,
 * well-understood construction behind verified + measured boot (ARM
 * TF-A's BL1/BL2/BL31 chain, TPM PCR extension), built natively here and
 * organised into the ten-key structure this system uses.
 *
 * THE "BLOCKCHAIN" PART, STATED PRECISELY
 * ---------------------------------------
 * The measurement is a HASH CHAIN:
 *
 *     M[0]   = SHA256( anchor_key )
 *     M[k+1] = SHA256( M[k] || stage_digest[k+1] || key_index[k+1] )
 *
 * Each link commits to every link before it, so altering ANY stage — or
 * running stages out of order, or skipping one — changes the final
 * digest. That is exactly the useful property people reach for when they
 * say "blockchain": tamper-evidence through hash linking. It is NOT
 * consensus, and we do not claim it is; there is one machine booting, so
 * there is nothing to reach agreement about. What it gives you is an
 * unforgeable statement of what actually ran.
 *
 * WHY TEN STAGES (and why that is not numerology)
 * -----------------------------------------------
 * Stages exist to separate TRUST DOMAINS. A stage is only justified if
 * something meaningfully different is trusted before and after it. Each
 * of the ten below has a distinct job and a distinct thing it authorises,
 * arranged as three nested triads plus a sealing key:
 *
 *   Triad I  — SILICON      keys 0-2: anchor, silicon init, measure root
 *   Triad II — STORAGE      keys 3-5: medium, slot select, payload verify
 *   Triad III— SOVEREIGNTY  keys 6-8: cell fabric, policy, runtime handoff
 *   Key 9    — SEAL         finalise the chain and publish the attestation
 *
 * If a stage cannot name what it uniquely authorises, it should be
 * removed: extra stages cost boot time and add attack surface. Every
 * stage here can.
 *
 * FAIL-CLOSED
 * A stage whose signature does not verify, whose version is older than
 * the recorded minimum (rollback), or which is reached out of order,
 * HALTS the chain. There is no "continue anyway" path.
 *
 * Freestanding: integer only, no libc, no allocation.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)  (ZXV zBIOS slice)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#ifndef ZXV_ZBIOS_H
#define ZXV_ZBIOS_H

#include <stdint.h>
#include <stdbool.h>

#define ZB_KEYS        10          /* ten vector-number keys = ten stages */
#define ZB_DIGEST_LEN  32          /* SHA-256 */
#define ZB_NAME_LEN    24

/* The ten keys, in mandatory execution order. */
typedef enum {
    ZB_K0_ANCHOR = 0,     /* immutable root of trust; verifies K1 */
    ZB_K1_SILICON,        /* clocks, DRAM, minimal MMIO */
    ZB_K2_MEASURE,        /* start the measurement chain */
    ZB_K3_MEDIUM,         /* bring up the boot medium (virtio-blk) */
    ZB_K4_SLOT,           /* A/B slot select + rollback counter check */
    ZB_K5_PAYLOAD,        /* Ed25519-verify the kernel image */
    ZB_K6_FABRIC,         /* admit the multikernel cell contracts */
    ZB_K7_POLICY,         /* load the capability/security policy */
    ZB_K8_HANDOFF,        /* MMU, vectors, exception-level transition */
    ZB_K9_SEAL            /* finalise + publish the attestation quote */
} zb_key_t;

typedef enum {
    ZB_TRIAD_SILICON = 0,
    ZB_TRIAD_STORAGE,
    ZB_TRIAD_SOVEREIGNTY,
    ZB_TRIAD_SEAL
} zb_triad_t;

typedef enum {
    ZB_OK = 0,
    ZB_ERR_ORDER      = -1,   /* stage reached out of sequence */
    ZB_ERR_SIGNATURE  = -2,   /* stage image failed verification */
    ZB_ERR_ROLLBACK   = -3,   /* stage version below the recorded minimum */
    ZB_ERR_HALTED     = -4,   /* the chain already failed; it is fail-closed */
    ZB_ERR_ARG        = -5
} zb_result_t;

typedef struct {
    char     name[ZB_NAME_LEN];
    uint8_t  digest[ZB_DIGEST_LEN];  /* measurement of this stage's image */
    uint32_t version;                /* monotonic; rollback protection */
    bool     executed;
    bool     verified;
} zb_stage_t;

typedef struct {
    zb_stage_t stage[ZB_KEYS];
    uint8_t    chain[ZB_DIGEST_LEN]; /* the running hash chain M[k] */
    uint32_t   next_key;             /* which key must run next */
    bool       halted;
    zb_result_t fault;               /* why it halted */

    /* rollback floor: a stage's version may never go below this */
    uint32_t   min_version[ZB_KEYS];

    /* stats */
    uint32_t   stages_run;
    bool       sealed;
} zbios_t;

/* Initialize with the anchor key (the immutable root, e.g. fused in
 * silicon). The measurement chain starts as SHA256(anchor_key). */
void zb_init(zbios_t *b, const uint8_t *anchor_key, uint32_t anchor_len);

/* Execute one stage. `digest` is the measurement of that stage's image;
 * `verified` is the result of the caller's signature check (the caller
 * owns the Ed25519 verify so this module stays transport-agnostic).
 *
 * Enforces: correct order, signature validity, rollback floor. On any
 * failure the chain HALTS and every later call returns ZB_ERR_HALTED. */
zb_result_t zb_stage(zbios_t *b, zb_key_t key, const char *name,
                     const uint8_t digest[ZB_DIGEST_LEN],
                     uint32_t version, bool verified);

/* Finalise. Valid only after all ten keys have run. Writes the
 * attestation quote (the final chain value) to `out`. */
zb_result_t zb_seal(zbios_t *b, uint8_t out[ZB_DIGEST_LEN]);

/* Which triad a key belongs to. */
zb_triad_t  zb_triad_of(zb_key_t key);
const char *zb_key_name(zb_key_t key);
const char *zb_triad_name(zb_triad_t t);
const char *zb_strerror(zb_result_t r);

/* Set the rollback floor for a key (from persistent, authenticated state). */
void zb_set_min_version(zbios_t *b, zb_key_t key, uint32_t min_version);

#endif /* ZXV_ZBIOS_H */
