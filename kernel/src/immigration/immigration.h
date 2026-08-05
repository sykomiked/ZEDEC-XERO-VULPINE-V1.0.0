/* immigration.h — Immigration Enforcement / Undocumented Daemons
 *
 * Native ZXV daemon admission control subsystem. Every process/daemon
 * that attempts to run on the kernel must pass through "Immigration"
 * — verifying its identity, signature, and authorization before being
 * granted a visa to execute.
 *
 * Key design principles (post-quantum, native ZXV):
 *   - All daemon identities are 168-bit critical words (peer node IDs)
 *   - Daemon packages must be signed; verification uses a pluggable
 *     signature scheme (Ed25519 now, ML-DSA-44 when available)
 *   - Porter House gates network-facing daemons
 *   - Three visa tiers: RESIDENT (trusted system daemon), WORKER
 *     (user application), TRANSIT (temporary/sandboxed)
 *   - Undocumented daemons (no valid signature) are REJECTED and
 *     logged for audit
 *   - Deportation (process termination) for daemons that violate
 *     their visa terms
 *
 * CRYPTO INTEGRATION INSTRUCTIONS:
 * The current implementation uses a HMAC-SHA256 verification signature
 * verifier (verifies HMAC-SHA256 against kernel authority key). To wire real post-quantum
 * crypto:
 *
 * 1. Implement `immig_verify_sig()` in `immigration.c` to call your
 *    chosen signature library (Ed25519 for now, ML-DSA-44 for PQ).
 * 2. Replace the `IMMIG_SIG_LEN` constant with the actual signature
 *    length for your chosen scheme (64 for Ed25519, 3456 for ML-DSA-44).
 * 3. Replace `IMMIG_PUBKEY_LEN` with the actual public key length
 *    (32 for Ed25519, 1312 for ML-DSA-44).
 * 4. The `immig_daemon_t` struct already carries pubkey + signature
 *    fields — no struct changes needed.
 * 5. For ML-DSA-44 (NIST FIPS 204), use the reference implementation
 *    from pq-crystals or a hardware-accelerated variant.
 *
 * Author: Michael Laurence Curzi (c)
 * 36N9 Genetics, LLC — All Rights Reserved
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#ifndef IMMIGRATION_H
#define IMMIGRATION_H

#include <stdint.h>
#include <stdbool.h>
#include "m5_types.h"
#include "surplus.h"
#include "edp_risk.h"
#include "porter_house.h"

/* ===== Constants ===== */

#define IMMIG_MAX_DAEMONS       64
#define IMMIG_MAX_NAME_LEN      48
#define IMMIG_SIG_LEN           64    /* Ed25519 sig (pluggable → ML-DSA-44) */
#define IMMIG_PUBKEY_LEN        32    /* Ed25519 pubkey (pluggable) */
#define IMMIG_CONTENT_HASH_LEN  21    /* 168-bit content hash */
#define IMMIG_MAX_VIOLATIONS    3     /* strikes before deportation */

/* ===== Visa Types ===== */

typedef enum {
    IMMIG_VISA_NONE      = 0,
    IMMIG_VISA_RESIDENT  = 1,  /* trusted system daemon (kernel-level) */
    IMMIG_VISA_WORKER    = 2,  /* user application daemon */
    IMMIG_VISA_TRANSIT   = 3   /* temporary/sandboxed daemon */
} immig_visa_t;

/* ===== Daemon States ===== */

typedef enum {
    IMMIG_DAEMON_UNUSED     = 0,
    IMMIG_DAEMON_PENDING    = 1,  /* awaiting immigration review */
    IMMIG_DAEMON_GRANTED    = 2,  /* visa granted, running */
    IMMIG_DAEMON_REJECTED   = 3,  /* visa denied (undocumented) */
    IMMIG_DAEMON_DEPORTED   = 4,  /* deported due to violations */
    IMMIG_DAEMON_EXPIRED    = 5   /* transit visa expired */
} immig_daemon_state_t;

/* ===== Daemon Record ===== */

typedef struct immig_daemon {
    uint32_t id;
    char name[IMMIG_MAX_NAME_LEN];
    immig_visa_t visa;
    immig_daemon_state_t state;

    word168_t daemon_id;            /* 168-bit daemon identity */
    uint8_t pubkey[IMMIG_PUBKEY_LEN];
    uint8_t content_hash[IMMIG_CONTENT_HASH_LEN];
    uint8_t signature[IMMIG_SIG_LEN];
    bool sig_verified;

    uint32_t violations;            /* policy violation count */
    uint64_t created_cycle;
    uint64_t last_active_cycle;
    uint32_t network_ports;         /* bitmask of ports this daemon uses */

    bool active;
} immig_daemon_t;

/* ===== Immigration Enforcement Engine ===== */

typedef struct immigration_engine {
    uint32_t device_id;

    immig_daemon_t daemons[IMMIG_MAX_DAEMONS];
    uint32_t num_daemons;
    uint32_t next_id;

    porter_house_t *porter;         /* for network-facing daemon admission */

    /* Stats */
    uint32_t total_granted;
    uint32_t total_rejected;
    uint32_t total_deported;
    uint32_t total_expired;
    uint32_t resident_count;
    uint32_t worker_count;
    uint32_t transit_count;

    /* M5 coordinates */
    m5_coords_t m5;
    surplus_real_t coverage_ratio;

    /* Pluggable signature verification (mirrors count_house_t.verify_sig).
     * NULL = the built-in Ed25519 ASYMMETRIC verification against the
     * embedded IMMIGRATION public key, whose private key is held offline.
     * Override to supply a different trust root, or in tests that cannot
     * mint a real Ed25519 signature. */
    bool (*verify_sig)(const struct immig_daemon *d);
} immigration_t;

/* ===== API ===== */

void immig_init(immigration_t *im, uint32_t device_id, porter_house_t *porter);

/* Apply for a visa. Returns daemon ID on success, -1 if full,
 * -2 if signature verification fails (undocumented daemon). */
int32_t immig_apply_visa(immigration_t *im, const char *name,
                          immig_visa_t visa,
                          const word168_t *daemon_id,
                          const uint8_t pubkey[IMMIG_PUBKEY_LEN],
                          const uint8_t content_hash[IMMIG_CONTENT_HASH_LEN],
                          const uint8_t signature[IMMIG_SIG_LEN],
                          uint32_t network_ports,
                          uint64_t current_cycle);

/* Verify a daemon's signature. Returns true if valid. */
bool immig_verify_daemon(immigration_t *im, uint32_t daemon_id);

/* Record a policy violation. Returns violation count, or -1 if not found.
 * If violations >= IMMIG_MAX_VIOLATIONS, daemon is deported. */
int32_t immig_record_violation(immigration_t *im, uint32_t daemon_id);

/* Deport a daemon (terminate and revoke visa). */
int32_t immig_deport(immigration_t *im, uint32_t daemon_id);

/* Check for expired transit visas. Returns count of expired. */
uint32_t immig_check_expired(immigration_t *im, uint64_t current_cycle,
                              uint64_t transit_timeout);

/* Touch a daemon (update last active cycle). */
int32_t immig_touch(immigration_t *im, uint32_t daemon_id, uint64_t current_cycle);

/* Get daemon by ID. */
immig_daemon_t *immig_get_daemon(immigration_t *im, uint32_t daemon_id);

/* Update M5 coverage. */
surplus_real_t immig_update_coverage(immigration_t *im);

#endif /* IMMIGRATION_H */
