/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* climate_ct.c — climate readings for ct_budget. See climate_ct.h. */
#include "climate_ct.h"

bool cl_ct_metrics(void *ctx, ct_metrics_t *m)
{
    const cl_ctl_t *c = (const cl_ctl_t *) ctx;
    if (!c || !m || !c->p || c->ticks == 0) return false;
    m->present |= CT_M_WATTS | CT_M_TEMP;
    m->watts_mw = c->last_power_mw;
    m->headroom_mc = c->last_headroom_mc;
    return true;
}

bool cl_ct_refs(const cl_ctl_t *c, ct_budget_cfg_t *cfg)
{
    if (!c || !cfg || !c->p || c->ticks == 0 || c->last_compute_cap_mw == 0) return false;
    cfg->watts_ref_mw = c->last_compute_cap_mw;
    cfg->headroom_ref_mc = CL_CT_HEADROOM_REF_MC;
    return true;
}
