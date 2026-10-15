/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* climate_ct.h — the climate controller's power caps and thermal headroom in
 * the form ct_budget consumes (src/cotier/ct_budget.h, B2). The controller
 * itself (climate.h) sits in the kernel stratum; this adapter includes the
 * AI-stratum budget header, so it is pinned to that stratum on its own.
 *
 *   cl_ct_metrics  a ct_metrics_fn: ctx is a const cl_ctl_t * that has run at
 *                  least one cl_step. Reports CT_M_WATTS (the measured
 *                  compute power of the last step) and CT_M_TEMP (headroom
 *                  in milli-degrees below the device's throttle point: the
 *                  briefcase scram trigger minus the hottest die, the
 *                  server supply-air limit minus the supply air, the
 *                  handheld operating maximum minus the cell). Returns false
 *                  before the first step, as ct_budget expects of a sensor
 *                  that cannot be read.
 *   cl_ct_refs     sets cfg->watts_ref_mw to the current compute cap (so
 *                  P / P_ref = 1 at the cap) and cfg->headroom_ref_mc to
 *                  CL_CT_HEADROOM_REF_MC. The weights stay the caller's
 *                  calibration; nothing else in cfg is touched.
 */
#ifndef ZXV_CLIMATE_CT_H
#define ZXV_CLIMATE_CT_H

#include "climate.h"
#include "ct_budget.h"

/* Headroom at which the thermal cost term reaches zero: 10 K [default]. */
#define CL_CT_HEADROOM_REF_MC 10000

bool cl_ct_metrics(void *ctx, ct_metrics_t *m);
bool cl_ct_refs(const cl_ctl_t *c, ct_budget_cfg_t *cfg);

#endif /* ZXV_CLIMATE_CT_H */
