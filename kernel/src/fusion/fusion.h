/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* fusion.h — program fusion over the Tri-Space file types.
 *
 * THE IDEA
 * --------
 * Positive space plugs into negative space; the socket finds the plug, and the
 * deck grows a new room. Every program is a Tri-Space artifact (a tri_triad_t):
 *
 *     S+  (TRI_POSITIVE)  capability_set = what it PROVIDES  (the plug)
 *     S-  (TRI_NEGATIVE)  capability_set = what it NEEDS     (the socket)
 *     S0  (TRI_NEUTRAL)   capability_set = glue / metadata   (neutral remainder)
 *
 * Fusion connects one program's S+ (provides) to another's S- (needs). Take two
 * people's programs and FUSE them like modular components: the provider's plug
 * fills the consumer's socket, and the ecosystem grows more diverse.
 *
 *   provides = a.provides | b.provides        (the union — the new plug)
 *   needs    = (a.needs | b.needs)            (everything still wanted...)
 *              & ~(a.provides | b.provides)   (...minus everything now supplied)
 *
 * That needs-formula is associative: fuse(fuse(A,B),C) has the same contract as
 * the three fused together, so a deck can be grown one room at a time.
 *
 * WHAT THIS MODULE DOES — AND DOES NOT — DO
 * -----------------------------------------
 * It composes the CAPABILITY CONTRACTS (the Tri-Space provides/needs) and
 * content-addresses the result. It PROVES two programs can fuse and states what
 * the fused contract is. It does NOT emit machine code: actually LINKING the
 * fused programs' code is the loader/linker's job — an ops boundary. This is the
 * contract algebra, not the linker.
 *
 * Freestanding: integer only, no libc, no allocation, no float. Digests via the
 * kernel's own SHA-256; the artifact model is reused verbatim from trispace.
 */
#ifndef ZXV_FUSION_H
#define ZXV_FUSION_H

#include <stdint.h>
#include <stdbool.h>
#include "trispace.h"   /* tri_triad_t / tri_member_t — the S+/S-/S0 artifact  */

#define FUSE_CID_LEN 32u

typedef enum {
    FUSE_OK        = 0,
    FUSE_ERR_NULL  = -1,   /* a NULL argument                                   */
    FUSE_ERR_UNMET = -2    /* the socket found no plug: a provides NONE of b's  */
                           /* needs — no connection is made, nothing fabricated */
} fuse_result_t;

/* A program is a Tri-Space artifact plus its content address. */
typedef struct {
    tri_triad_t triad;              /* S+ provides / S- needs / S0 glue          */
    uint8_t     cid[FUSE_CID_LEN];  /* content address of THIS program           */
    bool        has_cid;            /* true once a CID has been computed         */
} fuse_program_t;

/* ---- constructing a program ---- */

/* Zero a program and set its triad identity (both ids may be NULL -> zeroed). */
void fuse_program_init(fuse_program_t *p, const uint8_t triad_id[TRI_ID_LEN],
                       const uint8_t source_graph_digest[TRI_DIGEST_LEN]);

/* Declare the S+ plug: what this program PROVIDES. */
bool fuse_set_provides(fuse_program_t *p, uint32_t caps,
                       const uint8_t content_digest[TRI_DIGEST_LEN]);
/* Declare the S- socket: what this program NEEDS. */
bool fuse_set_needs(fuse_program_t *p, uint32_t caps,
                    const uint8_t content_digest[TRI_DIGEST_LEN]);
/* Declare the S0 glue / metadata (neutral; no production effect by contract). */
bool fuse_set_neutral(fuse_program_t *p, uint32_t caps,
                      const uint8_t content_digest[TRI_DIGEST_LEN]);

/* The plug (S+ provides) and the socket (S- needs) as capability bitmasks. */
uint32_t fuse_provides(const fuse_program_t *p);
uint32_t fuse_needs(const fuse_program_t *p);

/* ---- the fusion algebra ---- */

/* True iff the provider's S+ plug SATISFIES (is a superset of) the consumer's
 * S- socket — provider provides EVERY capability consumer needs, leaving no
 * unmet need. A consumer that needs nothing composes with anyone. */
bool fuse_can_compose(const fuse_program_t *provider, const fuse_program_t *consumer);

/* Fuse a (provider/plug) into b (consumer/socket), writing the fused artifact
 * to *out:
 *     out.provides = a.provides | b.provides
 *     out.needs    = (a.needs | b.needs) & ~(a.provides | b.provides)
 *     out.neutral  = a.neutral | b.neutral
 * and a DETERMINISTIC content digest that is a function of both inputs.
 *
 * Refused with FUSE_ERR_UNMET when b actually has needs but a provides NONE of
 * them — the socket found no plug, so no artifact is fabricated. A PARTIAL match
 * (a supplies some of b's needs) is honoured: the fused artifact records the
 * remaining needs in its S- (exactly b's needs minus a's provides). Returns
 * FUSE_OK on success, FUSE_ERR_NULL on a NULL argument. */
int32_t fuse_compose(const fuse_program_t *a, const fuse_program_t *b,
                     fuse_program_t *out);

/* Content-address a program: out_cid = SHA-256 over its canonical contract
 * (provides || needs || neutral || the three member content digests). Same
 * contract -> same CID; change any capability or member digest -> different CID.
 * Returns 0 on success, -1 on a NULL argument. */
int32_t fuse_cid(const fuse_program_t *p, uint8_t out_cid[FUSE_CID_LEN]);

#endif /* ZXV_FUSION_H */
