/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* hwr.h — hardware updates delivered as software: signed reconfiguration
 * images for reconfigurable regions.
 *
 * A REGION is a piece of hardware whose function is set by an image: FPGA
 * fabric (a bitstream, full or partial), a microcoded unit (microcode), a
 * soft core (its firmware). Loading a new image changes what the hardware
 * IS, so an image is treated with the care of a kernel update and more:
 *
 *   R1 SIGNED. An image is header || payload || signature. The signature
 *      covers the header, and the header carries the payload's SHA-256, so
 *      it covers every byte. The default suite is HWR_ALG_DUAL: an
 *      ML-DSA-87 (FIPS 204) AND an SLH-DSA-SHAKE-256s (FIPS 205) signature,
 *      both required, so a break of either scheme alone is not enough.
 *      Both use the domain context HWR_SIG_CONTEXT. Only keys the region's
 *      owner trusted verify; there is no other way in, and nothing
 *      unsigned is ever loaded.
 *   R2 FOR THIS HARDWARE ONLY. The header names a target class and,
 *      optionally, a target digest (the exact part/geometry). A region
 *      refuses an image for anything else.
 *   R3 FORWARD ONLY. Versions strictly increase and the security version
 *      (svn) never drops below the region's floor, so a signed but old,
 *      vulnerable image cannot be replayed.
 *   R4 TWO SLOTS. The image is loaded into the INACTIVE slot while the
 *      active one keeps working, then read back: the region's own digest
 *      of what it holds must equal the signed payload digest.
 *   R5 PROVEN ON PROBATION. hwr_trial switches to the new slot and runs the
 *      function's conformance vectors through the emulation matrix
 *      (hwm_attest). While it switches, the matrix sees the resource as
 *      RECONFIGURING and serves the function from another provider (an
 *      emulator), so callers do not stop. Only a TRUE result enters
 *      probation; hwr_commit then makes it permanent and raises the floor.
 *   R6 ROLLBACK AND S0 QUARANTINE. Any failure after the signature checked
 *      out (read-back mismatch, activation error, failed conformance,
 *      probation expiring uncommitted) switches back to the previous image
 *      and re-proves it. If there is none, the region's provider drops to
 *      S0 (NEITHER: unknown never acts) and the emulators keep serving.
 *      The image is recorded with a forbidden hexagram tag and, unless the
 *      failure was only an expired probation, its payload digest is
 *      blocked, so a known-bad image is never loaded again. Bad signatures
 *      and replay attempts are recorded too.
 *   R7 EVERY STEP RECORDED. Every stage, test, activation, rollback,
 *      rejection and quarantine is appended to the region's hash-chained
 *      audit log (hwa.h) and reported to the optional event callback. A
 *      full log halts the region (fail closed): a change that cannot be
 *      recorded is not made, and an activation that cannot be recorded is
 *      rolled back first.
 *
 * The device itself is reached only through hwr_ops_t (load, readback,
 * activate), so this file knows nothing about any vendor's parts.
 * Freestanding C11: no libc, no allocation, no float.
 */
#ifndef HWR_H
#define HWR_H

#include <stdbool.h>
#include <stdint.h>
#include "hwm.h"
#include "hwa.h"
#include "lpres_hex.h"

#define HWR_HEADER      96u
#define HWR_MAX_KEYS    4u
#define HWR_MAX_BAD     16u
#define HWR_MAX_PAYLOAD (64u << 20)
#define HWR_NO_SLOT     0xFFFFFFFFu
#define HWR_SIG_CONTEXT "zxv-hwreconf-v1"

/* Signature suites (the header's alg field). */
#define HWR_ALG_MLDSA87 1u /* 2592-byte keys, 4627-byte signatures */
#define HWR_ALG_MLDSA65 2u /* 1952-byte keys, 3309-byte signatures */
#define HWR_ALG_DUAL    3u /* ML-DSA-87 + SLH-DSA-SHAKE-256s, both required */
#define HWR_MLDSA87_PK  2592u
#define HWR_MLDSA87_SIG 4627u
#define HWR_MLDSA65_PK  1952u
#define HWR_MLDSA65_SIG 3309u
#define HWR_SLH256S_PK  64u
#define HWR_SLH256S_SIG 29792u
/* A dual key is mldsa_pk || slh_pk; a dual signature is mldsa || slh. */
#define HWR_DUAL_PK  (HWR_MLDSA87_PK + HWR_SLH256S_PK)
#define HWR_DUAL_SIG (HWR_MLDSA87_SIG + HWR_SLH256S_SIG)

/* Image kinds. */
#define HWR_KIND_BITSTREAM 1u
#define HWR_KIND_PARTIAL   2u /* partial reconfiguration of one region */
#define HWR_KIND_MICROCODE 3u
#define HWR_KIND_FIRMWARE  4u
#define HWR_KIND_VECTORS                                                                           \
    5u /* a conformance contract: never loaded into a                                              \
          region; applied to the matrix (hwr_vectors_*) */

typedef enum {
    HWR_OK = 0,
    HWR_E_ARG = -1,
    HWR_E_FORMAT = -2,      /* not a ZXHR image */
    HWR_E_TARGET = -3,      /* R2: not for this region */
    HWR_E_VERIFY = -4,      /* R1: no trusted key signed it (logged) */
    HWR_E_ROLLBACK = -5,    /* R3: not newer / below the floor (logged) */
    HWR_E_BLOCKED = -6,     /* R6: payload already quarantined */
    HWR_E_READBACK = -7,    /* R4: the hardware holds other bytes (quarantined) */
    HWR_E_ACTIVATE = -8,    /* the device refused to switch (quarantined) */
    HWR_E_CONFORMANCE = -9, /* R5: vectors failed (quarantined) */
    HWR_E_STATE = -10,      /* wrong step for the region's state */
    HWR_E_FULL = -11,
    HWR_E_FUNCTION = -12, /* the image's function is not in the matrix */
    HWR_E_HALTED = -13,   /* the quarantine log is full: fail closed */
    HWR_E_DEVICE = -14,   /* load failed before anything changed */
    HWR_E_KIND = -15      /* the image kind does not belong here */
} hwr_status_t;

/* Quarantine reason codes (lpx_qrec_t.reason). */
typedef enum {
    HWR_Q_SIGNATURE = 1,
    HWR_Q_ROLLBACK = 2,
    HWR_Q_READBACK = 3,
    HWR_Q_ACTIVATE = 4,
    HWR_Q_CONFORMANCE = 5,
    HWR_Q_EXPIRED = 6
} hwr_qreason_t;

typedef enum {
    HWR_ST_EMPTY = 0,     /* nothing proven loaded */
    HWR_ST_ACTIVE = 1,    /* a committed image serves */
    HWR_ST_STAGED = 2,    /* a verified image waits in the inactive slot */
    HWR_ST_PROBATION = 3, /* the new image serves, not yet committed */
    HWR_ST_HALTED = 4
} hwr_state_t;

/* The parsed header. Pointers point into the caller's image bytes. */
typedef struct {
    uint16_t format, alg;
    uint32_t kind, target_class, fn_id, version, svn, payload_len;
    uint8_t target_digest[32]; /* zeros: any part of the class */
    uint8_t payload_sha256[32];
    const uint8_t *header, *payload, *sig;
    uint32_t sig_len;
} hwr_image_t;

/* R1: true iff `sig` is a valid signature by `pk` over msg, with the
 * HWR_SIG_CONTEXT domain context. */
typedef bool (*hwr_verify_fn)(void *ctx, uint16_t alg, const uint8_t *pk, uint32_t pk_len,
                              const uint8_t *msg, uint32_t msg_len, const uint8_t *sig,
                              uint32_t sig_len);

/* R7: called after each recorded step with the hwa_event_t, the reason
 * (hwr_qreason_t or 0) and the image version. It must not call back into
 * the region. */
typedef void (*hwr_event_fn)(void *ctx, uint32_t region, uint32_t event, uint32_t reason,
                             uint32_t version);

/* The device. Each returns 0 or a negative error. readback writes the
 * SHA-256 of what the slot really holds, computed by the device side. */
typedef struct {
    int (*load)(void *dev, uint32_t slot, const uint8_t *payload, uint32_t len);
    int (*readback)(void *dev, uint32_t slot, uint8_t digest[32]);
    int (*activate)(void *dev, uint32_t slot);
} hwr_ops_t;

typedef struct {
    bool valid;
    uint32_t version, svn, fn_id;
    uint8_t digest[32];
} hwr_slot_t;

typedef struct {
    uint32_t id;
    uint32_t target_class;
    uint8_t target_digest[32];
    uint16_t alg;
    const uint8_t *keys[HWR_MAX_KEYS]; /* caller-owned public keys */
    uint32_t nkeys;
    hwr_verify_fn verify;
    void *verify_ctx;
    const hwr_ops_t *ops;
    void *dev;
    hwm_t *m; /* optional link to the matrix */
    uint32_t resource, provider;
    hwa_log_t *log;
    hwr_event_fn event;
    void *event_ctx;

    hwr_slot_t slot[2];
    uint32_t active, staged; /* slot index or HWR_NO_SLOT */
    hwr_state_t state;
    uint32_t floor;
    uint32_t vfloor; /* R3: highest version ever committed (valid if vset) */
    bool vset;       /*     survives a drop to S0, unlike the active slot */
    uint32_t probation_left;
    uint8_t bad[HWR_MAX_BAD][32];
    uint32_t nbad;
    uint8_t staged_header[HWR_HEADER];
    uint32_t n_staged, n_committed, n_rolled_back, n_quarantined;
} hwr_region_t;

int hwr_parse(const uint8_t *img, uint32_t len, hwr_image_t *out);
/* The bytes hwr_build_header writes, for tools and tests that make images:
 * fill `im` (payload_sha256 included) and get the 96-byte header to sign. */
void hwr_build_header(const hwr_image_t *im, uint8_t out[HWR_HEADER]);
uint32_t hwr_sig_len(uint16_t alg);
uint32_t hwr_pk_len(uint16_t alg);

int hwr_region_init(hwr_region_t *r, uint32_t id, uint32_t target_class,
                    const uint8_t target_digest[32], uint16_t alg, hwr_verify_fn verify,
                    void *verify_ctx, const hwr_ops_t *ops, void *dev, hwa_log_t *log);
void hwr_set_event(hwr_region_t *r, hwr_event_fn fn, void *ctx);
int hwr_trust_key(hwr_region_t *r, const uint8_t *pk);
/* Link the region to its FABRIC or MICRO provider in the matrix (R5, R6). A region
 * that is never linked skips the conformance proof: signature, target,
 * version, read-back and activation are still checked. */
int hwr_link(hwr_region_t *r, hwm_t *m, uint32_t res_index, uint32_t pv_index);

/* R1-R4: verify and load into the inactive slot. */
int hwr_stage(hwr_region_t *r, const uint8_t *img, uint32_t len);
/* R5: switch to the staged slot and prove it. probation_ticks > 0. */
int hwr_trial(hwr_region_t *r, uint32_t probation_ticks);
int hwr_commit(hwr_region_t *r);
/* Operator rollback of a staged or probationary image (not quarantined). */
int hwr_rollback(hwr_region_t *r);
/* R6: one health tick; probation expiring uncommitted rolls back. */
int hwr_tick(hwr_region_t *r);

const char *hwr_state_name(hwr_state_t s);

/* ---- contract packages (HWR_KIND_VECTORS) ----
 * A signed image whose payload is a new conformance contract for function
 * fn_id: version is the contract version, svn its anti-rollback floor. The
 * payload is a sequence of vectors, each
 *   op (u32 LE) || in_len (u32 LE) || in || out_len (u32 LE) || out
 * with out_len <= 512 and at most HWM_MAX_VECTORS vectors.
 *
 * hwr_package_verify checks format, signature (any of the keys) and the
 * payload digest; the caller then checks version/floor, parses with
 * hwr_vectors_next, and installs with hwm_contract_begin + hwm_vector_add,
 * copying the bytes (the matrix does not own them). */
int hwr_package_verify(const uint8_t *img, uint32_t len, uint16_t alg, hwr_verify_fn verify,
                       void *verify_ctx, const uint8_t *const *keys, uint32_t nkeys,
                       hwr_image_t *out);
/* Iterate vectors: *off starts at 0. HWR_OK with a vector, 1 at the end,
 * HWR_E_FORMAT on a malformed payload. */
int hwr_vectors_next(const uint8_t *payload, uint32_t len, uint32_t *off, uint32_t *op,
                     const uint8_t **in, uint32_t *in_len, const uint8_t **out, uint32_t *out_len);

#endif /* HWR_H */
