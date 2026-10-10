#include "compat_layer.h"
#include "m5_types.h"

long project_to_legacy(const phase_tick_t *pt) {
    /* omega * r truncated toward zero, in integers (was a double product). */
    if (pt->r.den == 0) return 0;
    long scalar = (long) fx_sdiv64((int64_t) pt->omega * pt->r.num, pt->r.den);
    return scalar;
}

m8_process_t lift_to_m8(const phase_tick_t *pt) {
    m8_process_t p8 = {0};
    p8.base = *pt;
    p8.axis6 = 0;
    p8.axis7 = 0;
    p8.axis8 = 0;
    return p8;
}
