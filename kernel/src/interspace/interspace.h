/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* interspace.h — the interstitial commons BETWEEN sovereign nodes.
 *
 * "We are inside a law older than yours."
 *
 * Cyberspace, in ZXV, is not a territory any node owns. The space BETWEEN
 * nodes — the wire, the swarm, the phase-tick medium — is res communis: owned
 * by no node, governable only by a maritime-flavoured commons law (Lex Rhodia
 * de Iactu, ~800 BC) that predates the sovereigns floating on it. This umbrella
 * header gathers the family:
 *
 *   interspace.h   — the res-communis commons + the shared status vocabulary
 *   lex_rhodia.h   — general average, salvage, sovereign immunity, safe passage
 *   flagstate.h    — flag-state co-jurisdiction (host port + vessel sovereign)
 *   federation.h   — treaty-federation: microstates, councils, writ recognition
 *   minister.h     — the Minister of Interstitial Affairs (computes, cannot seize)
 *
 * We REUSE, we do not reinvent: onepolicy (the Symbiotic Maxim; general average
 * is its numeric ANTIDOTE to asymmetric harm), ipfs (SHA-256 content-addressing:
 * "your hash is your key" IS the sovereign immunity of the flagged vessel),
 * constellation (cc_coordinator_t / cc_node_t — the fabric between nodes),
 * surplus (the numeric type), and the Royal Writ (LICENSE_ROYAL_WRIT: declaratory
 * mutual recognition — we cite it, we claim no new authority of our own).
 *
 * Freestanding: integer only, no libc, no allocation, no floating point on the
 * target. surplus_real_t is double on host, Q32.32 on the target — SR_ macros
 * only, never raw arithmetic.
 */
#ifndef ZXV_INTERSPACE_H
#define ZXV_INTERSPACE_H

#include <stdint.h>
#include <stdbool.h>
#include "surplus.h"

/* ===== Shared vocabulary (defined BEFORE the family re-includes) ===== */

/* A node's stable handle within the interstitial fabric. By convention a
 * zxv_node_id_t is the node's SLOT in the shared constellation registry, so the
 * Minister can ask the constellation whether that node has actually joined. */
typedef uint32_t zxv_node_id_t;
#define ZXV_NODE_NONE  0xFFFFFFFFu   /* "no node" — the commons has no owner */

/* Every hash we speak is one SHA-256 digest — the same 32 bytes ipfs calls a
 * CID. We alias the length rather than mint a rival constant. */
#define ZXV_HASH_LEN   32u

/* One status vocabulary for the whole commons. Negative == refusal, with the
 * reason; ZXV_OK == 0 == admitted. */
typedef enum {
    ZXV_OK       =  0,   /* admitted / carried / conserved                    */
    ZXV_EIMMUNE  = -1,   /* the commons (or a flagged vessel) is immune        */
    ZXV_EPERM    = -2,   /* actor lacks standing (never granted, not "not yet")*/
    ZXV_EBADSIG  = -3,   /* a writ / attestation binding does not verify       */
    ZXV_ECHARTER = -4,   /* a charter / identity CID does not recompute        */
    ZXV_EQUORUM  = -5,   /* a vote failed to carry                             */
    ZXV_EDEGEN   = -6    /* degenerate input (e.g. zero total stake)           */
} zxv_status_t;

/* ===== The interstitial commons (res communis / res nullius) ===== */

/* Roman law of things owned by no one:
 *   res communis — the sea, the air: common to all, seizable by none.
 *   res nullius  — currently unowned, but in principle claimable.
 * The interstitial medium of ZXV is res communis. */
typedef enum {
    ZXV_RES_NULLIUS  = 0,   /* unowned, in principle claimable   */
    ZXV_RES_COMMUNIS = 1    /* common to all, seizable by NONE    */
} commons_kind_t;

typedef struct {
    uint8_t        region_id[ZXV_HASH_LEN];  /* content-address of the region  */
    commons_kind_t kind;
    zxv_node_id_t  owner;   /* INVARIANT: pinned to ZXV_NODE_NONE — a commons
                             * with an owner is not a commons. */
} interstitial_region_t;

/* Open a region of the interstitial medium. `owner` is ALWAYS forced to
 * ZXV_NODE_NONE regardless of anything the caller wishes — the pin is the point. */
void interstitial_open(interstitial_region_t *r,
                       const uint8_t region_id[ZXV_HASH_LEN],
                       commons_kind_t kind);

/* True iff this region is res communis (common to all, seizable by none). */
bool interstitial_is_commons(const interstitial_region_t *r);

/* True iff this region is valid (active commons with valid region_id). */
static inline bool interstitial_region_valid(const interstitial_region_t *r) {
    return r && r->kind == ZXV_RES_COMMUNIS && r->owner == ZXV_NODE_NONE;
}

/* Attempt to claim the region for a node. For res communis this ALWAYS returns
 * ZXV_EIMMUNE — a commons any node can seize is a hollow commons. For res
 * nullius it returns ZXV_EPERM: no node may unilaterally seize the interstice;
 * the owner pin never moves off ZXV_NODE_NONE. There is no success path here on
 * purpose. */
zxv_status_t interstitial_claim(interstitial_region_t *r, zxv_node_id_t claimant);

/* ===== The family =====
 * Re-included ONLY at top level. When a family header is itself the compilation
 * entry point it includes this umbrella for the shared vocabulary above, but it
 * defines ZXV_IN_FAMILY_HEADER first so this block does not recurse back into a
 * half-defined sibling (a sub-header's guard is set before its own types exist).
 * Each sub-header pulls the specific siblings it actually depends on. */
#ifndef ZXV_IN_FAMILY_HEADER
#include "lex_rhodia.h"
#include "flagstate.h"
#include "federation.h"
#include "minister.h"
#endif

#endif /* ZXV_INTERSPACE_H */
