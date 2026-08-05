#ifndef PHASE_COORDINATOR_H
#define PHASE_COORDINATOR_H

#include "m5_types.h"

void phase_coordinator_init(phase_tick_t *tick, exec_profile_t profile);
int phase_coordinator_tick(phase_tick_t *tick);

#endif
