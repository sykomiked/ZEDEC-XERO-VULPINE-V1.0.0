/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* climate_arb.h — the arbiter's telemetry as climate controller inputs.
 *
 * The arbiter reports power and temperature sensors over the mailbox
 * (src/arbiter, ARB_S_*; OS_HARDWARE_INTERFACE.md section 5) in the same
 * units cl_input_t uses: milliwatts and milli-degrees C. This copies the
 * readings the arbiter has reported into an input sample; climate_ct then
 * hands the controller's caps and headroom to ct_budget, so the chain is
 * arbiter sensor -> cl_step -> ct_budget.
 *
 *   die_mc      the hotter of ARB_S_T_CPU and ARB_S_T_NEURAL
 *   intake_mc   ARB_S_T_INTAKE
 *   exhaust_mc  ARB_S_T_EXHAUST
 *   power_mw    ARB_S_PWR_CPU + ARB_S_PWR_NEURAL, the compute power the
 *               controller caps [default: DRAM, storage, projector and bus
 *               rails are not compute and are left out]
 *   cell_mc     ARB_S_T_CELL
 *
 * A field whose sensors the arbiter has never reported is left as the
 * caller set it (state N: no reading, nothing invented). A reading that
 * cannot be true (negative power) is not copied and is reported in *bad
 * (state B). Server air, humidity and drive
 * temperatures come from the climate shell's own sensors, not the arbiter.
 */
#ifndef ZXV_CLIMATE_ARB_H
#define ZXV_CLIMATE_ARB_H

#include "climate.h"
#include "../arbiter/arb_host.h"

#define CL_ARB_DIE     0x01u
#define CL_ARB_INTAKE  0x02u
#define CL_ARB_EXHAUST 0x04u
#define CL_ARB_POWER   0x08u
#define CL_ARB_CELL    0x10u

/* Fill `in` from the arbiter's latest telemetry. Returns the CL_ARB_* fields
 * written; *bad (may be null) gets the fields refused as impossible. */
uint32_t cl_arb_inputs(const arb_host_t *h, cl_input_t *in, uint32_t *bad);

#endif /* ZXV_CLIMATE_ARB_H */
