/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* faraid.h — ADVISORY Islamic inheritance (faraid) shares for a small set
 * of heirs, as exact fractions, and the estate manifest built from them.
 * The output is never a ruling: estate.h requires a signed qualified ruling
 * from the configured jurisdiction gateway before anything is distributed.
 *
 *   F1 HEIRS. Husband, wives (count, 1..4), father, mother, sons (count),
 *      daughters (count). Siblings, grandparents, grandchildren and every
 *      other relative are NOT modelled: a caller who flags any of them
 *      (`others`) is refused with EST_ERR_UNSUPPORTED.
 *   F2 FIXED SHARES. Husband 1/2 without children, 1/4 with. Wives together
 *      1/4 without children, 1/8 with. Mother 1/6 with children, else 1/3,
 *      except the Umariyyatan cases (a spouse, the father and the mother
 *      only): the mother gets 1/3 of what remains after the spouse. Father
 *      1/6 with sons. Daughters without sons: 1/2 for one, 2/3 shared by two
 *      or more.
 *   F3 RESIDUE. With sons, sons and daughters take the residue 2:1 (a son
 *      takes twice a daughter). Otherwise the father takes the residue:
 *      1/6 plus residue with daughters only, the residue alone with no
 *      children.
 *   F4 AWL. When the fixed shares exceed 1, every fixed share is scaled down
 *      in proportion (the denominator becomes the sum of the numerators) and
 *      no residuary takes anything.
 *   F5 RADD. When there is no residuary heir and the fixed shares are below
 *      1, the surplus returns to the fixed-share heirs OTHER THAN the spouse,
 *      in proportion to their shares. When the spouse is the only fixed
 *      heir the surplus is left unallocated (`rest`; schools differ on
 *      whether it returns to the spouse or the public treasury), and
 *      est_set_faraid refuses such an estate with EST_ERR_UNSUPPORTED.
 *   F6 EXACT. All arithmetic is in 24ths (the least common denominator of
 *      1/2, 1/3, 1/4, 1/6, 1/8, 2/3), scaled exactly for awl, radd and the
 *      2:1 split; every output fraction is in lowest terms and the class
 *      shares sum to exactly 1. Counts are bounded (EST_MAX_HEIRS), so
 *      nothing can overflow.
 *
 * Textbook checks (test_estate.c): Minbariyya (wife, father, mother, two
 * daughters: 1/9, 4/27, 4/27, 16/27), both Umariyyatan, radd with mother and
 * one daughter (1/4, 3/4), wife and one daughter (1/8, 7/8), son and
 * daughter (2/3, 1/3), husband and son (1/4, 3/4), husband, father and one
 * daughter (1/4, 1/4, 1/2).
 *
 * HONEST LIMITS. Not legal or religious advice. The four Sunni schools and
 * Shia jurisprudence differ (radd to a spouse, the grandfather with
 * siblings, and much more); this models one common majority treatment of a
 * few heirs only. A qualified scholar or court decides. Freestanding C11.
 */
#ifndef ZXV_EST_FARAID_H
#define ZXV_EST_FARAID_H

#include "estate.h"

#define EST_FARAID_MAX_WIVES 4u

enum {
    EST_FA_HUSBAND = 0,
    EST_FA_WIVES = 1, /* all wives together */
    EST_FA_FATHER = 2,
    EST_FA_MOTHER = 3,
    EST_FA_SONS = 4,      /* all sons together */
    EST_FA_DAUGHTERS = 5, /* all daughters together */
    EST_FA_REST = 6,      /* unallocated (F5) */
    EST_FA_N = 7
};

typedef struct {
    bool husband;
    uint32_t wives;
    bool father, mother;
    uint32_t sons, daughters;
    bool others; /* any other relative: refused */
} est_faraid_heirs_t;

typedef struct {
    est_frac_t share[EST_FA_N]; /* lowest terms; absent classes 0/1 */
    bool awl, radd;
    bool advisory; /* always true */
} est_faraid_t;

est_status_t est_faraid(const est_faraid_heirs_t *h, est_faraid_t *out);
/* The manifest, one heir per person, ids in this order: husband or each
 * wife, father, mother, each son, each daughter. */
est_status_t est_set_faraid(est_t *e, const est_faraid_heirs_t *h, const uint32_t id[]);

#endif /* ZXV_EST_FARAID_H */
