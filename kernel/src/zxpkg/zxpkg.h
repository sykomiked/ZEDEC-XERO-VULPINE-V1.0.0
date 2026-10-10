/* zxpkg.h — the on-disk native package: a compiled artifact as a Tri-Space triad
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
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

/* ===================== release signing (provenance) =====================
 *
 * zxpkg_verify_triad proves a triad is INTACT and internally consistent, but
 * a whole triad can be re-issued. Binding it to a release IDENTITY is the ZSP
 * layer (src/loader/zsp): a release carries a ZSP **v2** envelope whose signed
 * payload IS the triad's 32-byte seal, and whose signed header carries the
 * release version and architecture. Its 32-byte identity field must equal
 *
 *   zxpkg_release_identity = zxp_manifest_digest(ZXP_DOMAIN_ZXPKG,
 *       {PackageID = triad_id, Version, Architecture, no deps, ContentCID = seal})
 *
 * (src/provenance/zx_provenance.h), so the Ed25519 signature covers the
 * complete manifest in the shared canonical encoding. A v1 envelope signs no
 * version and is refused (ZXREL_UNSIGNED). Signing is offline (the kernel only
 * verifies); this is the verify side. build_system/sign_release.sh still emits
 * v1 envelopes and must be moved to this format before it can sign a release.
 */
typedef enum {
    ZXREL_OK = 0,           /* intact AND signed by the root over this seal   */
    ZXREL_TRIAD_BAD,        /* the triad itself does not verify               */
    ZXREL_UNSIGNED,         /* no (v2) ZSP envelope, or it is malformed       */
    ZXREL_BAD_SIG,          /* the signature is not from the root key         */
    ZXREL_SEAL_MISMATCH,    /* signed, but over a DIFFERENT triad's seal      */
    ZXREL_ROLLBACK,         /* signed version below the caller's floor        */
    ZXREL_ARCH,             /* signed for another architecture                */
    ZXREL_MANIFEST_MISMATCH /* identity != canonical (id, version, arch, seal) */
} zxrel_t;

/* The identity a ZSP v2 release envelope must carry (see above). zsp_arch is a
 * ZSP_ARCH_* id. Returns false for an unknown arch id. */
bool zxpkg_release_identity(const uint8_t triad_id[TRI_ID_LEN], uint32_t version, uint16_t zsp_arch,
                            const uint8_t seal[TRI_DIGEST_LEN], uint8_t out[32]);

/* Verify a signed release: the triad must verify; the ZSP v2 envelope must
 * verify against `root_pubkey` with version >= min_version and arch ==
 * expected_arch (ZSP_ARCH_ANY = any); its signed payload must equal the
 * triad's seal; and its identity must equal zxpkg_release_identity. On
 * ZXREL_OK, *version_out (if non-NULL) is the signed release version. */
zxrel_t zxpkg_verify_release(const uint8_t *pos, uint32_t pos_len, const uint8_t *neg,
                             uint32_t neg_len, const uint8_t *neu, uint32_t neu_len,
                             const uint8_t *zsp, uint32_t zsp_len, const uint8_t root_pubkey[32],
                             uint32_t min_version, uint16_t expected_arch, uint32_t *version_out);

const char *zxrel_strerror(zxrel_t r);

#endif /* ZXV_ZXPKG_H */
