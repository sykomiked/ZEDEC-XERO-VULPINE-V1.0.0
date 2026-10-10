/* trispace.h — the Tri-Space artifact triad: ZXV's native file format
 *
 * WHAT THIS IS
 * ------------
 * ZXV does not ship a program as one file. Every module is a TRIAD — three
 * bound artifacts that together describe what the code does, what it must
 * never do (and how to undo it), and what is not yet decided:
 *
 *     positive  S+   .n9n63   ->  .zxvc     what the program DOES
 *     negative  S-   .9n63    ->  .cedez    its inverse / prohibitions / undo
 *     neutral   S0   .0n0     ->  .cedec    the unresolved remainder
 *
 * A conventional build ships the S+ half and leaves the other two implicit
 * (in a comment, in a runbook, in someone's head). ZXV makes them first-class
 * FILES that are cryptographically bound to each other, so a release cannot
 * quietly lose its own undo path or hide what it left unresolved.
 *
 * THE BINDING (why substitution is impossible)
 * --------------------------------------------
 * Before release, every member binds to the OTHER members' digests. The triad
 * seal therefore covers all three, so a member lifted from a different build —
 * a stale S-, a friendlier S0 — changes the seal and is rejected. You cannot
 * ship today's behaviour with yesterday's undo.
 *
 * FIVE HARD REQUIREMENTS, ENFORCED HERE (not documented and hoped for)
 * -------------------------------------------------------------------
 *  1. All three members must be present and bound, or the triad is QUARANTINED
 *     into S0 — never silently released as "just the positive half".
 *  2. S- MUST NOT hold more capabilities than S+. The undo path is not a back
 *     door: a prohibition cannot grant itself powers the action never had.
 *  3. S0 MUST hold NO production-effect capability until a signed policy
 *     resolves it. The unresolved remainder cannot act on the world.
 *  4. An irreversible effect MUST declare compensating, constraining, or
 *     observational semantics — claiming `exact` inverse for something that
 *     cannot be undone is rejected.
 *  5. GENERATED S- material must be labelled generated and MUST NOT be
 *     presented as a proven inverse. An auto-derived undo is a draft, not a proof.
 *
 * Freestanding: integer only, no libc, no allocation. Digests via the kernel's
 * own SHA-256.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)  (ZXV Tri-Space artifact slice)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#ifndef ZXV_TRISPACE_H
#define ZXV_TRISPACE_H

#include <stdint.h>
#include <stdbool.h>

#define TRI_DIGEST_LEN   32u
#define TRI_ID_LEN       32u
#define TRI_SCHEMA_VER   2u        /* ZXV_TRI_SPACE_PROGRAMMING_SPEC_v2 */

/* The three spaces. */
typedef enum { TRI_POSITIVE = 0, TRI_NEGATIVE = 1, TRI_NEUTRAL = 2 } tri_role_t;

/* How S- relates to S+ — the honest kind of inverse this effect can have. */
typedef enum {
    TRI_INV_EXACT = 0,      /* mathematically reversible, inputs retained     */
    TRI_INV_COMPENSATING,   /* cannot erase; counteract (a correcting entry)  */
    TRI_INV_RESTORING,      /* restore a prior checkpoint                     */
    TRI_INV_CONSTRAINING,   /* no inverse exists; define invariants + safe fail */
    TRI_INV_OBSERVATIONAL   /* verify/audit/detect without changing state     */
} tri_inverse_kind_t;

/* Why a triad is sitting in S0 rather than released. */
typedef enum {
    TRI_Q_NONE = 0,
    TRI_Q_MISSING_MEMBER,       /* a triad member was not supplied            */
    TRI_Q_SOURCE_MISMATCH,      /* members come from different source graphs  */
    TRI_Q_NEG_OVER_CAPABLE,     /* S- claims capabilities S+ never had        */
    TRI_Q_NEUTRAL_HAS_EFFECT,   /* S0 holds production-effect capability      */
    TRI_Q_BAD_INVERSE_CLAIM,    /* irreversible effect claims an exact inverse*/
    TRI_Q_UNPROVEN_INVERSE,     /* generated S- presented as a proven inverse */
    TRI_Q_SEAL_MISMATCH,        /* a member was substituted or altered        */
    TRI_Q_CAP_MISDECLARED       /* code carries capability it never declared   */
} tri_quarantine_t;

typedef struct {
    tri_role_t role;
    bool       present;
    uint8_t    content_digest[TRI_DIGEST_LEN];  /* digest of this artifact    */
    uint32_t   capability_set;                  /* what it is permitted to do */
    bool       generated;                       /* auto-derived, not authored */
    bool       claims_proven_inverse;           /* asserted as a PROVEN undo  */
} tri_member_t;

typedef struct {
    uint8_t  triad_id[TRI_ID_LEN];
    uint8_t  source_graph_digest[TRI_DIGEST_LEN]; /* the ONE source all share */
    uint32_t schema_version;
    tri_inverse_kind_t inverse_kind;
    bool     effect_is_irreversible;              /* declared by the author   */
    tri_member_t member[3];                       /* indexed by tri_role_t    */

    /* filled by tri_bind() */
    bool     bound;
    uint8_t  seal[TRI_DIGEST_LEN];                /* covers ALL three members */
    tri_quarantine_t quarantine;
} tri_triad_t;

/* Zero a triad and set its identity. */
void tri_init(tri_triad_t *t, const uint8_t triad_id[TRI_ID_LEN],
              const uint8_t source_graph_digest[TRI_DIGEST_LEN]);

/* Supply one member. Returns false on a bad role. */
bool tri_set_member(tri_triad_t *t, tri_role_t role,
                    const uint8_t content_digest[TRI_DIGEST_LEN],
                    uint32_t capability_set, bool generated,
                    bool claims_proven_inverse);

/* Enforce every hard requirement and, if they all hold, compute the seal that
 * binds all three members together. On failure the triad is QUARANTINED (S0)
 * with a reason and `bound` stays false. Returns true only on a clean bind. */
bool tri_bind(tri_triad_t *t);

/* Recompute the seal and compare. Detects any member substituted or altered
 * after binding. */
bool tri_verify(const tri_triad_t *t);

/* May this triad be released (act on the world)? Only a cleanly bound,
 * unquarantined triad may. */
bool tri_may_release(const tri_triad_t *t);

/* Canonical file extension for a role — source and compiled forms. */
const char *tri_source_extension(tri_role_t role);    /* .n9n63 / .9n63 / .0n0 */
const char *tri_compiled_extension(tri_role_t role);  /* .zxvc / .cedez / .cedec */
const char *tri_role_name(tri_role_t role);
const char *tri_quarantine_reason(tri_quarantine_t q);

#endif /* ZXV_TRISPACE_H */
