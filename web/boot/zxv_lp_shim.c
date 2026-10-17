/* zxv_lp_shim.c — the SDK's four-valued ABI (zxv_lp_*, zxv_status_name)
 * over the kernel's own lpres for the browser's wasm app engine. The
 * kernel's conjoin/disjoin follow its chain ordering; this adapter calls
 * them directly, so the wasm and the node never disagree.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdint.h>

#include "lpres.h"

int32_t zxv_lp_and(int32_t a, int32_t b)
{
    return (int32_t) lpres_conjoin((lpres_state_t) a, (lpres_state_t) b);
}

int32_t zxv_lp_or(int32_t a, int32_t b)
{
    return (int32_t) lpres_disjoin((lpres_state_t) a, (lpres_state_t) b);
}

int32_t zxv_lp_not(int32_t a)
{
    /* the kernel's negation: TRUE <-> FALSE, BOTH and NEITHER fixed */
    switch ((lpres_state_t) a) {
    case LPRES_STATE_TRUE:
        return (int32_t) LPRES_STATE_FALSE;
    case LPRES_STATE_FALSE:
        return (int32_t) LPRES_STATE_TRUE;
    default:
        return a;
    }
}

const char *zxv_status_name(int32_t s)
{
    switch ((lpres_state_t) s) {
    case LPRES_STATE_TRUE:
        return "true";
    case LPRES_STATE_FALSE:
        return "false";
    case LPRES_STATE_BOTH:
        return "both";
    default:
        return "neither";
    }
}
