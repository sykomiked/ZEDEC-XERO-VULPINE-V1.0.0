/* zxpkg.h — the on-disk native package: a compiled artifact as a Tri-Space triad
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 *
 * WHAT THIS IS
 * -----------
 * src/trispace/ enforces the LOGICAL triad — the three spaces, their binding,
 * the five hard requirements — but over digests and metadata. It never touches
 * the actual bytes of a build. This module is the CONTAINER: it turns the real
 * output of a compile (the kernel image, an app, a driver) into the three
 * on-disk files ZXV natively ships, each self-describing and cryptographically
 * bound to the other two:
 *
 *     positive  S+   .zxvc    what the program DOES        (the compiled image)
 *     negative  S-   .cedez   its inverse / undo / limits  (rollback descriptor)
 *     neutral   S0   .cedec   the unresolved remainder     (unsettled policy)
 *
 * This is what "render the program in the native format" means concretely: a
 * conventional build emits one binary; a ZXV build emits a bound triad, and a
 * loader will not release (act on) a triad that is incomplete, substituted, or
 * whose undo path claims powers the action never had.
 *
 * THE FILE
 * --------
 * Each member file is a fixed 148-byte header followed by the payload:
 *     magic "ZXVT", schema, role, flags, inverse-kind, capability set,
 *     payload length, triad id, source-graph digest, the payload's own
 *     content digest, and the TRIAD SEAL (identical across all three members,
 *     because it binds all three). Reading verifies the payload against its
 *     content digest; verifying the triad rebuilds the seal from the three
 *     members and rejects any member lifted from a different build.
 *
 * WHAT THIS DOES NOT DO (stated, not discovered)
 * ----------------------------------------------
 * The triad seal proves INTEGRITY and detects SUBSTITUTION within a triad. It
 * does NOT prove PROVENANCE — a whole triad can be re-issued consistently.
 * Binding the seal to a release identity is the signing layer (Ed25519 / ZSP),
 * which is a separate step and is not performed here. A member's payload is
 * carried verbatim; this module neither compiles nor executes it.
 *
 * Freestanding: integer only, no libc, no allocation. Digests via kernel SHA-256.
 */
#ifndef ZXV_ZXPKG_H
#define ZXV_ZXPKG_H

#include <stdint.h>
#include <stdbool.h>
#include "../trispace/trispace.h"

#define ZXPKG_MAGIC0 'Z'
#define ZXPKG_MAGIC1 'X'
#define ZXPKG_MAGIC2 'V'
#define ZXPKG_MAGIC3 'T'
#define ZXPKG_HDR_LEN 148u
#define ZXPKG_SCHEMA  2u

#define ZXPKG_FLAG_GENERATED     0x01u
#define ZXPKG_FLAG_PROVEN_INV    0x02u
#define ZXPKG_FLAG_IRREVERSIBLE  0x04u

/* Everything needed to build a triad from three payloads. */
typedef struct {
    uint8_t  triad_id[TRI_ID_LEN];
    uint8_t  source_graph_digest[TRI_DIGEST_LEN];
    tri_inverse_kind_t inverse_kind;
    bool     irreversible;
    /* per role (index by tri_role_t) */
    const uint8_t *payload[3];
    uint32_t payload_len[3];
    uint32_t capability[3];
    bool     generated[3];
    bool     claims_proven_inverse[3];
} zxpkg_spec_t;

/* A parsed member file. `payload` points into the caller's buffer. */
typedef struct {
    uint32_t schema;
    tri_role_t role;
    bool     generated, claims_proven_inverse, irreversible;
    tri_inverse_kind_t inverse_kind;
    uint32_t capability_set;
    uint8_t  triad_id[TRI_ID_LEN];
    uint8_t  source_graph_digest[TRI_DIGEST_LEN];
    uint8_t  content_digest[TRI_DIGEST_LEN];
    uint8_t  seal[TRI_DIGEST_LEN];
    const uint8_t *payload;
    uint32_t payload_len;
} zxpkg_member_t;

/* Seal a triad described by `spec`: computes each payload's digest, enforces
 * every Tri-Space requirement, and writes the binding seal. Returns TRI_Q_NONE
 * on a clean bind; any other value is the quarantine reason and `seal_out` is
 * left zero. */
tri_quarantine_t zxpkg_seal(const zxpkg_spec_t *spec, uint8_t seal_out[TRI_DIGEST_LEN]);

/* Serialize one role's member file (header + payload) into `out`. `seal` must
 * be the seal returned by zxpkg_seal for the same spec. Returns bytes written,
 * or 0 on error (bad role, or `out` too small). */
uint32_t zxpkg_write(const zxpkg_spec_t *spec, tri_role_t role,
                     const uint8_t seal[TRI_DIGEST_LEN],
                     uint8_t *out, uint32_t cap);

/* Parse a member file and VERIFY its payload against the content digest in its
 * header. Returns false on a bad magic/header/length, or if the payload does
 * not match its digest (tampering). */
bool zxpkg_read(const uint8_t *buf, uint32_t len, zxpkg_member_t *out);

/* Verify a whole triad from its three serialized member files. Checks every
 * payload digest, that all three carry the same seal and consistent triad
 * identity, that the seal REBUILDS from the three members (no member was
 * lifted from another build), and that the triad may be released. Returns
 * TRI_Q_NONE iff the triad is intact and releasable, else the reason. */
tri_quarantine_t zxpkg_verify_triad(const uint8_t *pos, uint32_t pos_len,
                                    const uint8_t *neg, uint32_t neg_len,
                                    const uint8_t *neu, uint32_t neu_len);

/* Canonical compiled extension for a role: .zxvc / .cedez / .cedec */
const char *zxpkg_extension(tri_role_t role);

#endif /* ZXV_ZXPKG_H */
