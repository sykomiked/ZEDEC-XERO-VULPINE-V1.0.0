/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* climate_arb.c — arbiter telemetry into climate inputs (climate_arb.h). */
#include "climate_arb.h"

uint32_t cl_arb_inputs(const arb_host_t *h, cl_input_t *in, uint32_t *bad)
{
    uint32_t got = 0, refused = 0;
    int32_t v, w;

    int have_cpu = arb_host_sensor(h, ARB_S_T_CPU, &v);
    int have_npu = arb_host_sensor(h, ARB_S_T_NEURAL, &w);
    if (have_cpu || have_npu) {
        in->die_mc = (have_cpu && have_npu) ? (v > w ? v : w) : (have_cpu ? v : w);
        got |= CL_ARB_DIE;
    }
    if (arb_host_sensor(h, ARB_S_T_INTAKE, &v)) {
        in->intake_mc = v;
        got |= CL_ARB_INTAKE;
    }
    if (arb_host_sensor(h, ARB_S_T_EXHAUST, &v)) {
        in->exhaust_mc = v;
        got |= CL_ARB_EXHAUST;
    }
    int have_pc = arb_host_sensor(h, ARB_S_PWR_CPU, &v);
    int have_pn = arb_host_sensor(h, ARB_S_PWR_NEURAL, &w);
    if (have_pc || have_pn) {
        uint32_t sum = 0; /* two non-negative int32 readings always fit */
        int ok = 1;
        if (have_pc) {
            if (v < 0)
                ok = 0;
            else
                sum += (uint32_t) v;
        }
        if (have_pn) {
            if (w < 0)
                ok = 0;
            else
                sum += (uint32_t) w;
        }
        if (ok) {
            in->power_mw = sum;
            got |= CL_ARB_POWER;
        } else {
            refused |= CL_ARB_POWER;
        }
    }
    if (arb_host_sensor(h, ARB_S_T_CELL, &v)) {
        in->cell_mc = v;
        got |= CL_ARB_CELL;
    }
    if (bad) *bad = refused;
    return got;
}
