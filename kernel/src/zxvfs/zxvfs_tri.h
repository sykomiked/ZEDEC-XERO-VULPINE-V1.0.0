/* zxvfs_tri.h — Tri-Space native storage: the TRIAD is the unit, not the file
 *
 * WHAT THIS ADDS
 * --------------
 * ZXVFS stores files. Tri-Space says a module is not a file — it is three bound
 * artifacts (S+ what it does, S- its inverse/undo, S0 the unresolved remainder).
 * Until now that binding lived only in PACKAGING (trispace.c, zxpkg): the seal
 * caught substitution at release time, but the filesystem knew nothing about
 * triads, so nothing stopped an S+ from being persisted while its S- was lost.
 *
 * This layer makes the triad the storage unit, so "you cannot have the action
 * without its undo" becomes a property of the DISK, not a convention.
 *
 * THE COMMIT ORDER IS THE WHOLE GUARANTEE
 * ---------------------------------------
 * A triad is stored as four ZXVFS files:
 *
 *     <name>.zxvc   S+ payload        <name>.cedez  S- payload
 *     <name>.cedec  S0 payload        <name>.tri    the DESCRIPTOR
 *
 * The descriptor is written LAST, and it is the only thing that makes a triad
 * observable. Role payloads are written first and are inert until a descriptor
 * names them with matching digests. Therefore:
 *
 *     crash before the descriptor commits  ->  no bound triad exists
 *     crash during the descriptor commit   ->  ZXVFS's redo journal makes that
 *                                              single-sector write all-or-nothing
 *
 * so there is NO interleaving in which a caller observes a bound S+ whose S-
 * did not land. `test_zxvfs_tri.c` proves this by crashing at EVERY write index
 * of a triad write and asserting the invariant after each remount.
 *
 * REWRITE IS FAIL-CLOSED, NOT ATOMIC (stated plainly)
 * ---------------------------------------------------
 * Overwriting an already-bound triad writes new role payloads under the same
 * names before the new descriptor commits. A crash in that window leaves NEW
 * bytes described by the OLD descriptor — the digests will not match, so
 * `zxvfs_tri_open()` refuses and QUARANTINES the triad. That is fail-closed and
 * detected, but it is not old-or-new atomicity. Achieving that needs role
 * shadowing (write to a shadow name, flip in the descriptor), which is a
 * follow-up once ZXVFS grows an extent allocator.
 *
 * ONE SOURCE OF TRUTH FOR THE RULES
 * ---------------------------------
 * The five hard requirements are NOT reimplemented here. This layer computes
 * the content digests and calls trispace.c (`tri_bind`) to enforce them, so the
 * storage layer and the packaging layer can never drift apart:
 *
 *   1. all three members present            5. generated S- may not claim a
 *   2. S- caps must be a subset of S+ caps      proven inverse
 *   3. S0 must hold no production capability
 *   4. an irreversible effect may not claim an exact inverse
 *
 * QUARANTINE SURVIVES REBOOT
 * --------------------------
 * When `zxvfs_tri_open()` detects tampering (a role edited underneath us, a
 * seal mismatch), it does not merely return an error: it rewrites the
 * descriptor with state=QUARANTINED and the reason, so the triad stays refused
 * across reboots instead of being re-litigated on every mount.
 *
 * HONEST SCOPE
 * ------------
 * The enforcement point is THIS API. A caller holding the underlying
 * `zxvfs_t` can still `zxvfs_read()` a role file directly, exactly as a caller
 * holding a `block_device_t` can read raw sectors beneath ZXVFS. What this
 * layer guarantees is that no BOUND TRIAD is ever observable in a partial or
 * altered state — not that the bytes are unreachable by lower-level means.
 *
 * Also inherited from ZXVFS MVP limits: 64 files total (so ~16 triads), and
 * each role payload capped at ZXVFS_FILE_MAX_BYTES.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)  (ZXV Tri-Space storage slice)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#ifndef ZXVFS_TRI_H
#define ZXVFS_TRI_H

#include <stdint.h>
#include <stdbool.h>
#include "zxvfs.h"
#include "../trispace/trispace.h"
#include "../zab/zab_exec.h"

#define ZXVFS_TRI_MAGIC    0x5A545249u   /* 'ZTRI' */
#define ZXVFS_TRI_VERSION  1u

/* Longest triad base name. The role suffixes (".cedez" = 6 chars) plus a NUL
 * must fit inside ZXVFS_NAME_LEN, so the base is bounded well below it. */
#define ZXVFS_TRI_NAME_MAX (ZXVFS_NAME_LEN - 8)   /* 24 */

/* Observable state of a stored triad. */
typedef enum {
    ZXVFS_TRI_ABSENT      = 0,   /* no descriptor on disk                     */
    ZXVFS_TRI_BOUND       = 1,   /* all three present, bound, seal verified   */
    ZXVFS_TRI_QUARANTINED = 2    /* refused; see `quarantine` for the reason  */
} zxvfs_tri_state_t;

/* Errors. Rule violations are reported as -(100 + tri_quarantine_t) so the
 * caller can recover the exact reason; use ZXVFS_TRI_REASON() to decode. */
#define ZXVFS_TRI_ERR_RULE_BASE 100
#define ZXVFS_TRI_REASON(rc) \
    ((tri_quarantine_t)((-(rc)) - ZXVFS_TRI_ERR_RULE_BASE))
#define ZXVFS_TRI_IS_RULE_ERR(rc) ((rc) <= -ZXVFS_TRI_ERR_RULE_BASE)

/* What the caller supplies to store a triad. All three roles are required —
 * that is requirement 1, and it is checked before anything touches the disk. */
typedef struct {
    const uint8_t *data[3];                 /* indexed by tri_role_t          */
    uint32_t       len[3];
    uint32_t       capability_set[3];       /* what each member may do        */
    bool           generated[3];            /* auto-derived, not authored     */
    bool           claims_proven_inverse[3];
    tri_inverse_kind_t inverse_kind;
    bool           effect_is_irreversible;
    uint8_t        triad_id[TRI_ID_LEN];
    uint8_t        source_graph_digest[TRI_DIGEST_LEN];

    /* PROOF OF INVERSE (required when S- claims a proven EXACT inverse).
     * `proof_state` is the post-state S+ produced. The S- payload must be a ZXI
     * inverse witness for it, and it is VERIFIED BY APPLYING IT at bind time:
     * the undo must land byte-exactly on the recorded prior state. Without
     * this, "proven" is just a boolean the author set. See invproof.h for what
     * a witness does and does not prove. */
    const uint8_t *proof_state;
    uint32_t       proof_state_len;
} zxvfs_tri_spec_t;

/* The on-disk descriptor — written last, and the sole proof a triad exists.
 * Fixed layout, little-endian native, well under one ZXVFS file extent. */
typedef struct {
    uint32_t magic;                         /* ZXVFS_TRI_MAGIC                */
    uint32_t version;
    uint32_t state;                         /* zxvfs_tri_state_t              */
    uint32_t quarantine;                    /* tri_quarantine_t               */
    uint32_t inverse_kind;                  /* tri_inverse_kind_t             */
    uint32_t effect_is_irreversible;
    uint8_t  triad_id[TRI_ID_LEN];
    uint8_t  source_graph_digest[TRI_DIGEST_LEN];
    uint32_t size[3];
    uint8_t  digest[3][TRI_DIGEST_LEN];     /* digest of each role payload    */
    uint32_t capability_set[3];
    uint32_t generated[3];
    uint32_t claims_proven_inverse[3];
    uint8_t  seal[TRI_DIGEST_LEN];          /* trispace seal over all three   */
    uint32_t checksum;                      /* over everything above          */
} zxvfs_tri_desc_t;

/* Store a triad. Enforces the five requirements BEFORE writing anything: on a
 * violation the filesystem is left untouched and the return value carries the
 * reason (see ZXVFS_TRI_REASON). On success, role payloads are written first
 * and the descriptor last, so a crash can never expose a partial triad.
 * Returns 0 on success, <0 on error. */
int zxvfs_tri_write(zxvfs_t *fs, const char *name, const zxvfs_tri_spec_t *spec);

/* Load and FULLY VERIFY a triad: descriptor checksum, every role payload's
 * digest re-derived from what is actually stored, and the trispace seal. Any
 * mismatch quarantines the triad on disk (so it stays refused after reboot)
 * and returns a rule error. `out` may be NULL. Returns 0 if bound and clean. */
int zxvfs_tri_open(zxvfs_t *fs, const char *name, zxvfs_tri_desc_t *out);

/* Read one role's payload — only from a triad that opens clean. Returns bytes
 * read, or <0. This is the gate: a quarantined or partial triad yields nothing. */
int zxvfs_tri_read_role(zxvfs_t *fs, const char *name, tri_role_t role,
                        uint8_t *buf, uint32_t max);

/* Current on-disk state without performing full verification (cheap probe). */
zxvfs_tri_state_t zxvfs_tri_state(zxvfs_t *fs, const char *name);

/* Remove a triad: descriptor FIRST (so it stops being observable), then the
 * role payloads. A crash mid-unlink leaves orphan role files, never a
 * descriptor pointing at bytes that are gone. Returns 0 on success. */
int zxvfs_tri_unlink(zxvfs_t *fs, const char *name);

/* ---- execution -----------------------------------------------------------
 * Running a stored artifact. The triad must open clean (digests + seal), so a
 * quarantined or tampered triad is not executable at all — verification is not
 * a separate step a caller can forget.
 *
 * S0 IS NEVER EXECUTABLE. The unresolved remainder cannot act on the world by
 * definition (requirement 3), so this refuses TRI_NEUTRAL outright rather than
 * relying on its capability set happening to be empty.
 *
 * The effective grant is `granted` INTERSECTED with the capabilities the
 * descriptor recorded for that role — a caller cannot hand an artifact more
 * authority than it was bound with.
 *
 * AND IT CANNOT HAND IT MORE OBJECTS EITHER. A capability names a verb; a
 * `zab_scope_t` names the object that verb may touch. If `host` binds no
 * extents, this DERIVES one: the triad itself (kind ZAB_SCOPE_TRIAD, named by
 * `name`, spanning the role payload's own bytes, carrying exactly the
 * effective verbs). If `host` binds its own table, every entry is clamped by
 * the same effective mask. Either way an instruction must select an extent
 * that carries its verb, so FS_WRITE means "write THIS triad", not "write the
 * filesystem".
 *
 * Extra failures introduced by that clamp, both fail-closed:
 *   -5  the caller bound more extents than can be clamped in one run
 *   -6  the triad's name will not fit a scope name, so no extent can name it
 * Neither can occur on the derived path for a triad whose name obeys
 * ZXVFS_TRI_NAME_MAX. */
int zxvfs_tri_execute(zxvfs_t *fs, const char *name, tri_role_t role,
                      uint32_t granted, const zab_host_t *host,
                      zab_exec_t *out);

/* Run S+ and, if it fails, run S- to undo — the payoff of binding an undo to
 * an action: rollback is always available because it could not have been
 * shipped without one.
 *
 * HONEST LIMIT: S- is the inverse of the COMPLETE effect of S+. Applying it
 * after a PARTIAL failure assumes S- is safe on a partially-applied state,
 * which the author must ensure (and is why TRI_INV_COMPENSATING exists as a
 * kind distinct from TRI_INV_EXACT). `fwd` reports exactly which effects ran
 * before the failure so a caller needing finer recovery can drive it itself.
 * Rollback is REFUSED for inverse kinds that have no undo (OBSERVATIONAL,
 * CONSTRAINING) — there is nothing honest to run.
 *
 * Returns 0 if S+ succeeded (no rollback needed), ZXVFS_TRI_ROLLED_BACK if S+
 * failed and S- completed, or <0 if S+ failed and the undo did not. */
#define ZXVFS_TRI_ROLLED_BACK 1
int zxvfs_tri_execute_with_undo(zxvfs_t *fs, const char *name,
                                uint32_t granted, const zab_host_t *host,
                                zab_exec_t *fwd, zab_exec_t *undo);

#endif /* ZXVFS_TRI_H */
