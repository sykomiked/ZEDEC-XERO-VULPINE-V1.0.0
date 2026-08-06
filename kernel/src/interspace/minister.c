/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* minister.c — computes settlements, binds only the consenting, seizes nothing. */

#include "minister.h"
#include "lex_rhodia.h"   /* compose general average, do not reinvent it */

void minister_init(minister_t *m, const cc_coordinator_t *fabric) {
    if (!m) return;
    m->fabric = fabric;
}

bool minister_node_consents(const minister_t *m, zxv_node_id_t node) {
    if (!m || !m->fabric) return false;
    if (node == ZXV_NODE_NONE) return false;
    if (node >= m->fabric->num_nodes) return false;   /* not a joined slot */
    const cc_node_t *n = &m->fabric->nodes[node];
    /* Consent == actually joined AND live in the shared fabric. Discovered,
     * quarantined, degraded, failed, or removed nodes have NOT consented to be
     * bound by this adjudication. */
    return n->registered && n->state == CC_NODE_ACTIVE;
}

zxv_status_t minister_adjudicate_average(const minister_t *m,
                                         surplus_real_t sacrifice,
                                         const ga_party_t *parties, uint32_t n,
                                         surplus_real_t *contrib_out,
                                         uint32_t *bound_mask_out) {
    if (!m) return ZXV_EDEGEN;

    /* The maritime rule apportions across EVERY stakeholder — compose it. */
    zxv_status_t st = lex_rhodia_general_average(sacrifice, parties, n, contrib_out);
    if (st != ZXV_OK) return st;

    /* Enforceability is a separate question from fairness: only consenting
     * (constellation-joined) nodes are BOUND. The Minister records what a
     * non-consenting node fairly owes but has no power to compel it. */
    if (bound_mask_out) {
        uint32_t mask = 0;
        uint32_t cap = (n < 32u) ? n : 32u;   /* mask is 32 bits wide */
        for (uint32_t i = 0; i < cap; i++)
            if (minister_node_consents(m, parties[i].node)) mask |= (1u << i);
        *bound_mask_out = mask;
    }
    return ZXV_OK;
}

bool minister_may_seize(const minister_t *m, const interstitial_region_t *r) {
    (void)m; (void)r;
    /* The Minister holds no sovereignty. There is no branch that returns true —
     * and there never will be. */
    return false;
}
