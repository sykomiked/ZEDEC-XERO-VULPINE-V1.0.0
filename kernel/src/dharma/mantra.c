/* mantra.c — Mantra: signal encoding implementation. See mantra.h. */
#include "mantra.h"
#include "rmag_core.h"
#include "uvn.h"

cyc13_t mantra_encode(const uint8_t *seed, uint32_t len) {
    cyc13_t v = cyc13_zero();
    for (uint32_t i = 0; i < len; i++) {
        uint32_t idx = i % L13_NUM_PHASES;
        rational_t contribution = {(int64_t)seed[i], 1};
        v.c[idx] = rmag_add_quotas(v.c[idx], contribution);
    }
    return v;
}

rational_t mantra_amplitude(cyc13_t vector) {
    uvn_t u;
    u.v = vector;
    return uvn_magnitude(u); /* exact-rational total magnitude, no double intermediate */
}

/* ---- DECLARATION -----------------------------------------------------------

 * REQUIRES measured from mantra.o's `nm -u` = {cyc13_zero, rmag_add_quotas,
 * uvn_magnitude}: the cyclotomic algebra, quota arithmetic, and its sibling
 * uvn. Rooted through tantra -> naga_raja, not directly.
 */
#include "zxv_decl.h"
ZXV_DECLARE(mantra,
    ZXV_PROVIDES(mantra_ready),
    ZXV_REQUIRES(rmag_ready, cyc13_ready, uvn_ready),
    ZXV_NO_BRINGUP);
