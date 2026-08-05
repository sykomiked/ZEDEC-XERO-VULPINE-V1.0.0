#include "compat_layer.h"
#include "m5_types.h"
#include <math.h>

long project_to_legacy(const phase_tick_t *pt) {
    long scalar = (long)(pt->omega * rational_mag(pt->r));
    return scalar;
}

m8_process_t lift_to_m8(const phase_tick_t *pt) {
    m8_process_t p8 = {0};
    p8.base = *pt;
    p8.axis6 = 0.0;
    p8.axis7 = 0.0;
    p8.axis8 = 0.0;
    return p8;
}
