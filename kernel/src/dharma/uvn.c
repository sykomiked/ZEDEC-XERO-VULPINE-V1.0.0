/* uvn.c — Universal Vector Number System implementation. See uvn.h. */
#include "uvn.h"
#include "rmag_core.h"

uvn_t uvn_scalar(rational_t r) {
    uvn_t u;
    u.v = cyc13_zero();
    u.v.c[0] = r;
    return u;
}

uvn_t uvn_from_int(int64_t n) {
    return uvn_scalar((rational_t){n, 1});
}

rational_t rational_abs_exact(rational_t r) {
    /* r is assumed already normalized (den > 0), matching this
     * codebase's rational_normalize() convention -- so the sign lives
     * entirely in num. */
    if (r.num < 0) return (rational_t){-r.num, r.den};
    return r;
}

rational_t uvn_magnitude(uvn_t u) {
    rational_t total = {0, 1};
    for (int i = 0; i < L13_NUM_PHASES; i++) {
        total = rmag_add_quotas(total, rational_abs_exact(u.v.c[i]));
    }
    return total;
}

rational_t uvn_scalar_value(uvn_t u) {
    return u.v.c[0];
}

uvn_t uvn_add(uvn_t a, uvn_t b) {
    uvn_t r;
    r.v = cyc13_add(a.v, b.v);
    return r;
}

uvn_t uvn_scale(uvn_t a, rational_t s) {
    uvn_t r;
    r.v = cyc13_scale(a.v, s);
    return r;
}

bool uvn_equal(uvn_t a, uvn_t b) {
    return cyc13_equal(a.v, b.v);
}

bool uvn_is_zero(uvn_t a) {
    return cyc13_is_zero(a.v);
}

/* ---- DECLARATION -----------------------------------------------------------

 * REQUIRES measured from uvn.o's `nm -u` = {cyc13_add, cyc13_equal,
 * cyc13_is_zero, cyc13_scale, cyc13_zero, rmag_add_quotas}. mantra depends on
 * this file (U uvn_magnitude), so uvn sits below mantra in the same subtree.
 */
#include "zxv_decl.h"
ZXV_DECLARE(uvn,
    ZXV_PROVIDES(uvn_ready),
    ZXV_REQUIRES(rmag_ready, cyc13_ready),
    ZXV_NO_BRINGUP);
