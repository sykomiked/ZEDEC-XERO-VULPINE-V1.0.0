/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* flagstate.h — flag-state co-jurisdiction.
 *
 * A vessel in a foreign port answers to TWO laws at once: the PORT'S law (the
 * host node's flag term — must pass the One Policy) and its OWN flag-state law
 * (its sovereign content-hash immunity). An action is allowed only when BOTH
 * agree. But IDENTITY and IMMUNITY questions bypass the host entirely: a vessel
 * flags OUT of a port, it is not owned by it, so who-you-are is settled by your
 * flag, never by whose water you happen to be floating in.
 */
#ifndef ZXV_FLAGSTATE_H
#define ZXV_FLAGSTATE_H

#define ZXV_IN_FAMILY_HEADER
#include "interspace.h"     /* shared vocabulary (core, family re-include suppressed) */
#undef ZXV_IN_FAMILY_HEADER
#include "lex_rhodia.h"     /* vessel_flag_t / vessel_flag_verify / vessel_immune_from */
#include "onepolicy.h"      /* op_term_t / op_symbiotic_ok — reused, not rebuilt */

typedef struct {
    const op_term_t *host_flag_term;  /* the port's terms for this action        */
    vessel_flag_t    sovereign;       /* the vessel's own flag                    */
} cojurisdiction_t;

typedef enum {
    COJ_ALLOW = 0,          /* host term is symbiotic AND sovereign intact       */
    COJ_DENY_HOST,          /* the port's term fails the One Policy              */
    COJ_DENY_SOVEREIGN      /* the vessel content does not match its flag        */
} coj_verdict_t;

/* Evaluate an action on `content` by `actor`. Checks the SOVEREIGN first (the
 * older law): if the content no longer hashes to the flag, the vessel has been
 * tampered with or a seizure was attempted -> COJ_DENY_SOVEREIGN. Otherwise the
 * host port's term must be op_symbiotic_ok; if it is not (or is absent) ->
 * COJ_DENY_HOST. Only when both hold -> COJ_ALLOW. */
coj_verdict_t cojurisdiction_eval(const cojurisdiction_t *c,
                                  const void *content, uint32_t len,
                                  zxv_node_id_t actor);

/* Identity/immunity query — BYPASSES the host flag entirely. True iff the
 * vessel's immunity vests in `actor` (i.e. actor is the keyholder). The port
 * gets no say in who the vessel is. */
bool cojurisdiction_identity_ok(const cojurisdiction_t *c, zxv_node_id_t actor);

#endif /* ZXV_FLAGSTATE_H */
