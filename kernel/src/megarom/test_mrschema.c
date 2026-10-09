/* test_mrschema.c — MegaROM schema extraction tests.
 *
 * The headline tests are the two that keep the corpus honest:
 *   1. AGENCY is only reported when it was actually measured (runs under
 *      DIFFERENT input storms). One storm yields UNMEASURED, never 0 — because
 *      "we did not look" and "it ignores the player" are different claims.
 *   2. NO TITLE CONTENT reaches the schema. The emitted text is searched for
 *      bytes that were present in the source data; finding any would mean the
 *      pipeline is carrying someone else's expression forward.
 *
 * Build/run:
 *   gcc -std=c11 -Wall -Wextra -Werror -Isrc/megarom \
 *       src/megarom/test_mrschema.c src/megarom/mrschema.c -o /tmp/test_mrs && /tmp/test_mrs
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <string.h>
#include "mrschema.h"

static int failures = 0;
#define CHECK(cond, msg)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("  [FAIL] %s\n", msg);                                                          \
            failures++;                                                                            \
        } else {                                                                                   \
            printf("  [PASS] %s\n", msg);                                                          \
        }                                                                                          \
    } while (0)

static char sbuf[4096];

static void mkid(uint8_t id[MRS_ID_LEN], uint8_t seed)
{
    for (uint32_t i = 0; i < MRS_ID_LEN; i++) id[i] = (uint8_t) (seed * 17u + i);
}

int main(void)
{
    printf("MegaROM schema extraction\n");

    /* ---- agency: measured only when input actually differed ---- */
    printf("agency is measured, not assumed:\n");
    uint8_t id[MRS_ID_LEN];
    mkid(id, 1);
    mrs_profile_t responsive, inert, single;

    /* a responsive title: different storms -> different traces */
    {
        mrs_run_t runs[3] = {{120000, 4000, 20000, 90, 32768, 0xAAAA, true},
                             {40000, 1200, 6000, 20, 32768, 0xBBBB, true},
                             {180000, 7000, 33000, 140, 32768, 0xCCCC, true}};
        CHECK(mrs_observe(id, runs, 3, &responsive) == 0, "  observe responsive title");
        CHECK(responsive.agency_measured, "differing storms => agency MEASURED");
        CHECK(responsive.agency > 100, "a responsive title shows real divergence");
    }

    /* an inert title: identical traces whatever is pressed */
    {
        mrs_run_t runs[3] = {{100000, 3000, 15000, 60, 32768, 0x1111, true},
                             {100000, 3000, 15000, 60, 32768, 0x2222, true},
                             {100000, 3000, 15000, 60, 32768, 0x3333, true}};
        mkid(id, 2);
        CHECK(mrs_observe(id, runs, 3, &inert) == 0, "  observe inert title");
        CHECK(inert.agency_measured && inert.agency == 0,
              "identical traces under DIFFERENT storms => agency 0 (it ignores the player)");
    }

    /* one storm only: nothing to compare */
    {
        mrs_run_t runs[1] = {{100000, 3000, 15000, 60, 32768, 0x1111, true}};
        mkid(id, 3);
        CHECK(mrs_observe(id, runs, 1, &single) == 0, "  observe with one storm");
        CHECK(!single.agency_measured && single.agency == 0,
              "ONE storm => UNMEASURED, not a fabricated 0");
    }

    /* runs that share a seed prove nothing either */
    {
        mrs_run_t runs[2] = {{100000, 3000, 15000, 60, 32768, 0x9999, true},
                             {50000, 1000, 5000, 10, 32768, 0x9999, true}};
        mrs_profile_t p;
        mkid(id, 4);
        mrs_observe(id, runs, 2, &p);
        CHECK(!p.agency_measured, "runs sharing an input seed do not count as evidence of agency");
    }

    /* ---- the schema carries dynamics, never content ---- */
    printf("no title content escapes into the schema:\n");
    int n = mrs_emit_sutra(&responsive, sbuf, sizeof(sbuf));
    CHECK(n > 0, "schema emits");
    CHECK(strstr(sbuf, "SUTRA-PROGRAM. MEGAROM-DYNAMICS.") == sbuf, "output is a SUTRA-PROGRAM");
    CHECK(strstr(sbuf, "CONTENT. NONE-CARRIED.") != NULL,
          "schema declares that it carries no title content");
    CHECK(strstr(sbuf, "AGENCY-STATUS     SUTRA-STATUS VALUE MEASURED.") != NULL,
          "a measured agency is labelled MEASURED");
    n = mrs_emit_sutra(&single, sbuf, sizeof(sbuf));
    CHECK(n > 0 && strstr(sbuf, "VALUE UNMEASURED.") != NULL,
          "an unmeasured agency is labelled UNMEASURED in the schema itself");

    /* the emitted text must contain none of the observed BYTES — only numbers.
     * A distinctive marker planted in the id must appear only as the dev
     * reference digest, never as raw content. */
    {
        mrs_profile_t p;
        uint8_t marker[MRS_ID_LEN];
        for (uint32_t i = 0; i < MRS_ID_LEN; i++) marker[i] = 0x7F;
        mrs_run_t runs[2] = {{1000, 10, 100, 1, 4096, 1, true}, {2000, 20, 200, 2, 4096, 2, true}};
        mrs_observe(marker, runs, 2, &p);
        mrs_emit_sutra(&p, sbuf, sizeof(sbuf));
        int raw = 0;
        for (uint32_t i = 0; sbuf[i]; i++)
            if ((uint8_t) sbuf[i] == 0x7F) raw = 1;
        CHECK(!raw, "no raw source byte appears in the schema (digest is hex-encoded)");
        CHECK(strstr(sbuf, "7f7f7f7f") != NULL,
              "the corpus reference is a hex digest — a dev reference, not a name");
    }

    /* ---- the cross-corpus matrix ---- */
    printf("cross-corpus matrix:\n");
    {
        mrs_profile_t set[4];
        set[0] = responsive;
        set[1] = inert;
        set[2] = single;
        set[3] = responsive; /* a deliberate duplicate */

        mrs_relation_t rel[16];
        uint32_t distinct = 0;
        CHECK(mrs_matrix(set, 4, 950, rel, &distinct) == 0, "matrix builds");
        CHECK(rel[0 * 4 + 0].affinity == 1000, "a title is maximally similar to itself");
        CHECK(rel[0 * 4 + 3].affinity == 1000, "the duplicate is detected as identical");
        CHECK(distinct == 3, "dedup counts DESIGNS (3), not files (4)");

        mrs_relation_t r01 = mrs_relate(&set[0], &set[1]);
        CHECK(r01.affinity + r01.complementarity == 1000,
              "affinity and complementarity are complements");

        uint32_t a = 0, b = 0;
        CHECK(mrs_widest_axis(set, 4, &a, &b) == 0 && a != b,
              "the widest design axis in the corpus is identified");
    }

    printf("\n%s mrschema: %d failure(s)\n", failures ? "FAILED" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
