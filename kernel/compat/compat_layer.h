#ifndef COMPAT_LAYER_H
#define COMPAT_LAYER_H

#include "m5_types.h"

typedef struct m8_process {
    phase_tick_t base;
    double axis6;
    double axis7;
    double axis8;
} m8_process_t;

long project_to_legacy(const phase_tick_t *pt);
m8_process_t lift_to_m8(const phase_tick_t *pt);

#endif
