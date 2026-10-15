/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* talisman_rules.h — Talisman's rules in C, the reference its Sutra,
 * Trebago, Eve and KRON blocks are checked against (blocks/talisman). Pure
 * integer functions, no state: the model (talisman_model.c) and the
 * language test (tests/talisman_zxapp.inc) both include this, so the
 * languages are tested against the very C the model runs.
 *
 *   TL1 REACH. How far a value has come toward a threshold, in hundredths
 *       of a percent, floored and held at 10000 (the threshold met):
 *       min(10000, floor(10000 * have / need)). have 0..999999999 and need
 *       1..999999999 (Sutra's 9(9)); anything else is refused
 *       (TALISMAN_REFUSED).
 *   TL2 EARNED. A tier is earned when every input is at or above its
 *       threshold. Each input comes as its margin, what it has minus what
 *       the tier needs: credibility and bond margins 0..TALISMAN_MARGIN_MAX
 *       (18 digits), the uptime margin 0..TALISMAN_BPS_MAX, since an uptime
 *       past 10000 basis points is no measurement (sicily.h S2).
 *   TL3 EARNED AS A NUMBER: 1 when TL2 holds, else 0, to count with. */
#ifndef TALISMAN_RULES_H
#define TALISMAN_RULES_H

#include <stdbool.h>
#include <stdint.h>

#define TALISMAN_REFUSED    (-1)
#define TALISMAN_REACH_FULL 10000     /* x100: the threshold met */
#define TALISMAN_VALUE_MAX  999999999 /* TL1: Sutra's 9(9) */
#define TALISMAN_MARGIN_MAX 999999999999999999
#define TALISMAN_BPS_MAX    10000

/* TL1 */
static inline int64_t talisman_reach_x100(int64_t have, int64_t need)
{
    if (have < 0 || have > TALISMAN_VALUE_MAX) return TALISMAN_REFUSED;
    if (need < 1 || need > TALISMAN_VALUE_MAX) return TALISMAN_REFUSED;
    int64_t r = have * TALISMAN_REACH_FULL / need;
    return r < TALISMAN_REACH_FULL ? r : TALISMAN_REACH_FULL;
}

/* TL2 */
static inline bool talisman_earned(int64_t cred, int64_t bond, int64_t uptime)
{
    return cred >= 0 && cred <= TALISMAN_MARGIN_MAX && bond >= 0 && bond <= TALISMAN_MARGIN_MAX &&
           uptime >= 0 && uptime <= TALISMAN_BPS_MAX;
}

/* TL3 */
static inline int64_t talisman_earned_n(int64_t cred, int64_t bond, int64_t uptime)
{
    return talisman_earned(cred, bond, uptime) ? 1 : 0;
}

#endif
