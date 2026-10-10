/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zt_harmonic_logic.c — truth-enum maps. See zt_harmonic_logic.h. */
#include "zt_harmonic_logic.h"

lpres_state_t zt_truth_to_lpres(zt_truth_state_t s)
{
    switch (s) {
    case ZT_TRUTH_TRUE:
        return LPRES_STATE_TRUE;
    case ZT_TRUTH_FALSE:
        return LPRES_STATE_FALSE;
    case ZT_TRUTH_GLUT:
    case ZT_TRUTH_PARADOX:
        return LPRES_STATE_BOTH;
    case ZT_TRUTH_UNKNOWN:
    case ZT_TRUTH_NEUTRAL:
        break;
    }
    return LPRES_STATE_NEITHER;
}

zt_truth_state_t zt_truth_from_lpres(lpres_state_t s)
{
    switch (s) {
    case LPRES_STATE_TRUE:
        return ZT_TRUTH_TRUE;
    case LPRES_STATE_FALSE:
        return ZT_TRUTH_FALSE;
    case LPRES_STATE_BOTH:
        return ZT_TRUTH_GLUT;
    case LPRES_STATE_NEITHER:
        break;
    }
    return ZT_TRUTH_UNKNOWN;
}

swarm_hk_truth_t zt_truth_to_hk(zt_truth_state_t s)
{
    switch (s) {
    case ZT_TRUTH_TRUE:
        return SWARM_HK_TRUE;
    case ZT_TRUTH_FALSE:
        return SWARM_HK_FALSE;
    case ZT_TRUTH_NEUTRAL:
        return SWARM_HK_NEUTRAL;
    case ZT_TRUTH_GLUT:
        return SWARM_HK_GLUT;
    case ZT_TRUTH_PARADOX:
        return SWARM_HK_PARADOX;
    case ZT_TRUTH_UNKNOWN:
        break;
    }
    return SWARM_HK_UNKNOWN;
}

zt_truth_state_t zt_truth_from_hk(swarm_hk_truth_t s)
{
    switch (s) {
    case SWARM_HK_TRUE:
        return ZT_TRUTH_TRUE;
    case SWARM_HK_FALSE:
        return ZT_TRUTH_FALSE;
    case SWARM_HK_GLUT:
        return ZT_TRUTH_GLUT;
    case SWARM_HK_NEUTRAL:
        return ZT_TRUTH_NEUTRAL;
    case SWARM_HK_PARADOX:
        return ZT_TRUTH_PARADOX;
    case SWARM_HK_UNKNOWN:
        break;
    }
    return ZT_TRUTH_UNKNOWN;
}
