/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* prov_swarm.h — a remote provider as one of the swarm's "models".
 *
 * The swarm (kernel/src/swarm/swarm_budget.h) splits a fixed number of
 * tokens per cycle across its models under the Fibonacci rule. A remote
 * inference offer is attached as an ordinary model slot at a chosen level:
 * it receives its share of the cycle's token budget exactly like a local
 * model (R1-R6 unchanged), and every job sent to it is first charged against
 * that allotment (swarm_budget_consume). The granted tokens become the job's
 * quantity, so a remote model can never spend more of the swarm's budget
 * than a local model at the same level. The money side (price ceiling,
 * budget cap, filters) is the ordinary prov_job_t. */
#ifndef ZXV_PROV_SWARM_H
#define ZXV_PROV_SWARM_H

#include "prov.h"
#include "../swarm/swarm_budget.h"

typedef struct {
    uint32_t model_id; /* swarm model id of this remote slot */
    uint32_t user;     /* prov user handle that pays          */
    prov_job_t tmpl;   /* filters, ceiling, asset, privacy    */
} prov_swarm_link_t;

/* Register the remote model in the swarm at `level` and remember the job
 * template (rclass INFERENCE, unit TOKEN are enforced). */
int prov_swarm_attach(swarm_budget_t *b, prov_swarm_link_t *l, uint32_t model_id, uint32_t level,
                      uint32_t user, const prov_job_t *tmpl);
/* Charge up to `tokens` against the model's allotment for this cycle and
 * post a bid for the granted amount. *granted gets the tokens charged.
 * PROV_ERR_BUDGET when the allotment is exhausted (no bid is posted). */
int prov_swarm_request(prov_net_t *n, swarm_budget_t *b, const prov_swarm_link_t *l,
                       uint64_t tokens, const uint8_t request_hash[PROV_HASH_LEN],
                       uint64_t *granted, uint32_t *bid);

#endif /* ZXV_PROV_SWARM_H */
