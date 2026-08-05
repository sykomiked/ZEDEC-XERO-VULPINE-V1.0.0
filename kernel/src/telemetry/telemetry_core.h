#ifndef TELEMETRY_CORE_H
#define TELEMETRY_CORE_H
#include "m5_types.h"

#define TELEMETRY_DEMO_TICKS 20

telemetry_t emit_and_observe(const axiom_matrix_t *A, ordinal_t tick);
ordinal_t choice_resolve_from_telemetry(const telemetry_t *t, uint32_t paradox_level);
uint32_t fib_bound(uint32_t paradox_level);

void run_telemetry_recursion_demo(void);

#endif