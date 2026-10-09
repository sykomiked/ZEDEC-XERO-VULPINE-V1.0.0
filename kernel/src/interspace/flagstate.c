/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* flagstate.c — two laws at once: the port's, and the vessel's own flag. */

#include "flagstate.h"

coj_verdict_t cojurisdiction_eval(const cojurisdiction_t *c,
                                  const void *content, uint32_t len,
                                  zxv_node_id_t actor) {
    (void)actor;   /* actor governs identity queries, not this action gate */
    if (!c) return COJ_DENY_HOST;

    /* The sovereign (older) law first: if the content no longer hashes to the
     * flag, the vessel was tampered with or a seizure was attempted. */
    if (!vessel_flag_verify(&c->sovereign, content, len))
        return COJ_DENY_SOVEREIGN;

    /* Then the port's law: the host term must be admissible under the One Policy.
     * An absent term is not a silent yes — a port with no lawful term denies. */
    if (!c->host_flag_term || !op_symbiotic_ok(c->host_flag_term))
        return COJ_DENY_HOST;

    return COJ_ALLOW;
}

bool cojurisdiction_identity_ok(const cojurisdiction_t *c, zxv_node_id_t actor) {
    if (!c) return false;
    /* Identity/immunity bypasses the host flag entirely — a vessel flags OUT of
     * a port, it is not owned by it. */
    return vessel_immune_from(&c->sovereign, actor);
}
