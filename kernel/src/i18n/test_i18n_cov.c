/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_i18n_cov.c — which locales kernel/src/font cannot classify.
 *
 * For every locale, walks the code points CLDR says it needs (exemplars,
 * punctuation, digits) and asks font_script_of() about each. A code point it
 * calls SCRIPT_UNKNOWN cannot be routed to any font, so it renders as tofu.
 * Checks that the live count matches the gap count the generator stored (a
 * mismatch means kernel/src/font/script.c changed: rerun gen_i18n_tables.py),
 * then prints a per-script report. Pass -v to list every locale with gaps.
 */
#include <stdio.h>
#include <string.h>
#include "i18n.h"
#include "../font/script.h"

int main(int argc, char **argv)
{
    int verbose = argc > 1 && strcmp(argv[1], "-v") == 0;
    unsigned pass = 0, fail = 0;
    struct {
        char script[8];
        unsigned locales, with_gaps, max_gaps;
        char worst[24];
        uint32_t sample[4];
        unsigned nsample;
    } agg[64];
    unsigned nagg = 0, total_gap_locales = 0;
    memset(agg, 0, sizeof agg);

    for (uint32_t i = 0; i < i18n_locale_count(); i++) {
        const i18n_locale_t *l = i18n_locale_at(i);
        const i18n_range_t *r;
        uint32_t stored, n = i18n_locale_needs(l, &r, &stored), live = 0;
        uint32_t first[4], nf = 0;
        for (uint32_t k = 0; k < n; k++)
            for (uint32_t cp = r[k].lo; cp <= r[k].hi; cp++)
                if (font_script_of(cp) == SCRIPT_UNKNOWN) {
                    if (nf < 4) first[nf++] = cp;
                    live++;
                }
        if (live == stored)
            pass++;
        else {
            fail++;
            printf("[FAIL] %s: %u unknown now, %u when generated (rerun the generator)\n",
                   i18n_locale_tag(l), live, stored);
        }
        const char *sc = i18n_locale_script(l);
        unsigned a = 0;
        while (a < nagg && strcmp(agg[a].script, sc) != 0) a++;
        if (a == nagg && nagg < 64) {
            strncpy(agg[a].script, sc, sizeof agg[a].script - 1);
            nagg++;
        }
        if (a == nagg) continue;
        agg[a].locales++;
        if (live) {
            agg[a].with_gaps++;
            total_gap_locales++;
            if (live > agg[a].max_gaps) {
                agg[a].max_gaps = live;
                strncpy(agg[a].worst, i18n_locale_tag(l), sizeof agg[a].worst - 1);
                for (uint32_t s = 0; s < nf; s++) agg[a].sample[s] = first[s];
                agg[a].nsample = nf;
            }
            if (verbose) {
                printf("  %-14s %-5s %4u unknown:", i18n_locale_tag(l), sc, live);
                for (uint32_t s = 0; s < nf; s++) printf(" U+%04X", (unsigned) first[s]);
                printf("\n");
            }
        }
    }

    printf("script  locales  with-gaps  worst            first unknown code points\n");
    for (unsigned a = 0; a < nagg; a++) {
        if (!agg[a].with_gaps) continue;
        printf("%-7s %7u  %9u  %-10s %4u ", agg[a].script, agg[a].locales, agg[a].with_gaps,
               agg[a].worst, agg[a].max_gaps);
        for (unsigned s = 0; s < agg[a].nsample; s++)
            printf(" U+%04X", (unsigned) agg[a].sample[s]);
        printf("\n");
    }
    printf("%u of %u locales need code points font_script_of() does not know\n", total_gap_locales,
           (unsigned) i18n_locale_count());
    printf("[%s] test_i18n_cov: %u passed, %u failed\n", fail ? "FAIL" : "PASS", pass, fail);
    return fail ? 1 : 0;
}
