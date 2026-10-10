/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* minister.h — the Minister of Interstitial Affairs.
 *
 * The Minister COMPUTES settlements over the commons — a general-average
 * apportionment, a salvage award — but holds NO sovereignty and CANNOT seize.
 * An adjudication binds ONLY nodes that have actually consented by JOINING the
 * constellation (a node ACTIVE in the shared cc_coordinator fabric). A party
 * that has not joined still receives its fair number, but the Minister has no
 * power to compel it — there is deliberately no seizure path over a
 * non-consenting node.
 *
 * We COMPOSE the constellation; we do not reinvent a node registry.
 */
#ifndef ZXV_MINISTER_H
#define ZXV_MINISTER_H

#define ZXV_IN_FAMILY_HEADER
#include "interspace.h"   /* shared vocabulary (core, family re-include suppressed) */
#undef ZXV_IN_FAMILY_HEADER
#include "lex_rhodia.h"                  /* ga_party_t — composed, not reinvented   */
#include "constellation_coordinator.h"   /* cc_coordinator_t / cc_node_t — reused   */

typedef struct {
    const cc_coordinator_t *fabric;   /* the consent boundary; NULL == no consent */
} minister_t;

void minister_init(minister_t *m, const cc_coordinator_t *fabric);

/* Has `node` consented, i.e. joined the constellation and gone ACTIVE? By the
 * interspace convention a zxv_node_id_t is the node's slot in the fabric's
 * registry; ZXV_NODE_NONE and out-of-range slots never consent. */
bool minister_node_consents(const minister_t *m, zxv_node_id_t node);

/* Adjudicate a general-average apportionment (composing lex_rhodia). The fair
 * contribution is computed for EVERY party (that is the maritime rule), but
 * *bound_mask_out gets bit i set only for parties whose node has consented —
 * the enforceable subset. Non-consenting parties are computed, never compelled.
 * Propagates ZXV_EDEGEN from the underlying apportionment. n is capped at 32
 * (the mask width); bound_mask_out may be NULL if the caller only wants numbers. */
zxv_status_t minister_adjudicate_average(const minister_t *m,
                                         surplus_real_t sacrifice,
                                         const ga_party_t *parties, uint32_t n,
                                         surplus_real_t *contrib_out,
                                         uint32_t *bound_mask_out);

/* The Minister cannot seize — not a commons region, not a node, not ever. This
 * is a function so the honesty is testable, not just a comment: it ALWAYS
 * returns false. */
bool minister_may_seize(const minister_t *m, const interstitial_region_t *r);

#endif /* ZXV_MINISTER_H */
