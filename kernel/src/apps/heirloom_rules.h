/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* heirloom_rules.h — Heirloom's rules in C, the reference its Sutra,
 * Trebago and Eve blocks are checked against (blocks/heirloom). Pure
 * integer functions, no state: the model (heirloom_model.c) and the
 * language test (tests/heirloom_zxapp.inc) both include this, so the
 * languages are tested against the very C the model runs.
 *
 *   HR1 THE SHARE GAP. Two shares a = an / ad and b = bn / bd, each a part
 *       of a whole (0 <= an <= ad, 0 <= bn <= bd, ad and bd 1 ..
 *       HEIRLOOM_NINE, Sutra's 9(9)), and how far a + b is from one
 *       whole, cross-multiplied over ad * bd and never divided:
 *       an * bd + bn * ad - ad * bd, worked as bn * ad - (ad - an) * bd so
 *       no step leaves 18 digits. 0 exactly when a + b = 1, below 0 when
 *       short of a whole, above 0 when past it. Anything else is refused
 *       (HEIRLOOM_REFUSED).
 *   HR2 A MANIFEST SUMS TO ONE. n shares (1 .. HEIRLOOM_HEIRS, zxvd's
 *       EST_MAX_HEIRS): the first n - 1 are added exactly with Tally's rule
 *       (tally_ratio, TALLY_ADD, reduced), and the last is set against
 *       that sum with HR1. A manifest is whole exactly when that gap is 0.
 *       A partial sum whose reduced denominator passes HEIRLOOM_NINE, or a
 *       share outside HR1's domain, is refused: the manifest is never
 *       taken as whole by default.
 *   HR3 RELEASE DUE. A death report's release is due exactly when a signed
 *       attestation is filed (reported = 1), the guardians' quorum k is
 *       1 .. HEIRLOOM_GUARDIANS (estate.setup's 1..5 guardians), the
 *       approvals beyond k (spare = approvals - k) are 0 .. 4, the waiting
 *       period is over (over = now - (reported_at + wait), 0 ..
 *       HEIRLOOM_EIGHTEEN) and the living owner has not vetoed since the
 *       report (vetoed = 0). Time or silence alone never makes it due: a
 *       report and k guardian signatures must be there. HR3N is the same
 *       rule as a number to count with, 1 due and 0 not.
 *
 * The split of an amount by the manifest is Crown's (crown_allot, CW2:
 * largest remainder, ties to the earlier heir), exactly as zxvd's
 * est_distribute splits it (estate.h E2). */
#ifndef HEIRLOOM_RULES_H
#define HEIRLOOM_RULES_H

#include <stdbool.h>
#include <stdint.h>

#include "tally_rules.h"

#define HEIRLOOM_REFUSED   INT64_MIN
#define HEIRLOOM_NINE      INT64_C(999999999)          /* Sutra's 9(9) */
#define HEIRLOOM_EIGHTEEN  INT64_C(999999999999999999) /* Sutra's s9(18) */
#define HEIRLOOM_HEIRS     16u                         /* zxvd estate.h EST_MAX_HEIRS */
#define HEIRLOOM_GUARDIANS 5                           /* zxvd estate.h EST_MAX_GUARDIANS */

/* HR1: the gap of an / ad + bn / bd from 1, cross-multiplied, or
 * HEIRLOOM_REFUSED. */
static inline int64_t heirloom_share_gap(int64_t an, int64_t ad, int64_t bn, int64_t bd)
{
    if (ad < 1 || ad > HEIRLOOM_NINE || bd < 1 || bd > HEIRLOOM_NINE) return HEIRLOOM_REFUSED;
    if (an < 0 || an > ad || bn < 0 || bn > bd) return HEIRLOOM_REFUSED;
    return bn * ad - (ad - an) * bd; /* each product below 10^18: fits */
}

/* HR2: *gap of n shares from one whole (0 when whole); 0, or
 * HEIRLOOM_REFUSED with *gap unchanged. */
static inline int64_t heirloom_manifest_gap(const int64_t *num, const int64_t *den, uint32_t n,
                                            int64_t *gap)
{
    if (!num || !den || !gap || n == 0 || n > HEIRLOOM_HEIRS) return HEIRLOOM_REFUSED;
    rat_t sum = rat_make(0, 1);
    for (uint32_t i = 0; i + 1 < n; i++) {
        if (heirloom_share_gap(0, 1, num[i], den[i]) == HEIRLOOM_REFUSED) return HEIRLOOM_REFUSED;
        if (!tally_ratio(TALLY_ADD, sum.num, sum.den, num[i], den[i], &sum))
            return HEIRLOOM_REFUSED;
    }
    int64_t g = heirloom_share_gap(sum.num, sum.den, num[n - 1], den[n - 1]);
    if (g == HEIRLOOM_REFUSED) return HEIRLOOM_REFUSED;
    *gap = g;
    return 0;
}

/* HR3: the release is due. */
static inline bool heirloom_release_due(int64_t reported, int64_t k, int64_t spare, int64_t over,
                                        int64_t vetoed)
{
    return reported == 1 && k >= 1 && k <= HEIRLOOM_GUARDIANS && spare >= 0 && spare <= 4 &&
           over >= 0 && over <= HEIRLOOM_EIGHTEEN && vetoed == 0;
}

/* HR3N: HR3 as 1 or 0. */
static inline int64_t heirloom_release_due_n(int64_t reported, int64_t k, int64_t spare,
                                             int64_t over, int64_t vetoed)
{
    return heirloom_release_due(reported, k, spare, over, vetoed) ? 1 : 0;
}

#endif
