/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* prov_swarm.c — remote providers inside the swarm token budget (see
 * prov_swarm.h). */
#include "prov_swarm.h"

int prov_swarm_attach(swarm_budget_t *b, prov_swarm_link_t *l, uint32_t model_id, uint32_t level,
                      uint32_t user, const prov_job_t *tmpl)
{
    if (!b || !l || !tmpl || user == PROV_NONE) return PROV_ERR_ARG;
    if (tmpl->rclass != PROV_RC_INFERENCE || tmpl->unit != PROV_UNIT_TOKEN) return PROV_ERR_ARG;
    swarm_status_t st = swarm_budget_register(b, model_id, level);
    if (st == SWARM_ERR_FULL) return PROV_ERR_FULL;
    if (st == SWARM_ERR_DUPLICATE) return PROV_ERR_DUPLICATE;
    if (st != SWARM_OK) return PROV_ERR_ARG;
    prov_memset(l, 0, sizeof *l);
    l->model_id = model_id;
    l->user = user;
    prov_memcpy(&l->tmpl, tmpl, sizeof l->tmpl);
    return PROV_OK;
}

int prov_swarm_request(prov_net_t *n, swarm_budget_t *b, const prov_swarm_link_t *l,
                       uint64_t tokens, const uint8_t request_hash[PROV_HASH_LEN],
                       uint64_t *granted, uint32_t *bid)
{
    uint64_t g = 0;
    if (granted) *granted = 0;
    if (!n || !b || !l || tokens == 0) return PROV_ERR_ARG;
    swarm_status_t st = swarm_budget_consume(b, l->model_id, tokens, &g);
    if (st == SWARM_ERR_NO_CYCLE) return PROV_ERR_STATE;
    if (st != SWARM_OK) return PROV_ERR_ARG;
    if (g == 0) return PROV_ERR_BUDGET;
    prov_job_t j;
    prov_memcpy(&j, &l->tmpl, sizeof j);
    j.qty = g;
    if (request_hash) prov_memcpy(j.request_hash, request_hash, PROV_HASH_LEN);
    /* j.max_total stays the template's per-request cap; prov_bid_post clamps
     * it to qty * ceiling (0 means exactly that). */
    int rc = prov_bid_post(n, l->user, &j, bid);
    if (rc != PROV_OK) return rc; /* tokens stay charged: R6 expires them at cycle end */
    if (granted) *granted = g;
    return PROV_OK;
}
