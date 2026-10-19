/* test_sym_interpret.c — the Symbolic Interpretation Engine, host tests.
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 *
 * Proves the symbolic criteria:
 *   1. archetypal reduction: every number reduces deterministically to a
 *      base-9 archetype, and master numbers 11/22/33 are preserved wherever
 *      they appear in the reduction chain;
 *   2. soul/expression split: vowel weight (intent) and consonant weight
 *      (boundary) map DIRECTLY to lpres's told-true/told-false encoding —
 *      (0,0)=NEITHER, (1,0)=TRUE, (0,1)=FALSE, (1,1)=BOTH — never a
 *      re-derived table;
 *   3. recursive cycle stacking: the nine-period ring wraps, completed
 *      cycles count, and the four-level awareness recursion advances
 *      deterministically without growing memory;
 *   4. narrative formatting: bounded, deterministic, NUL-terminated lines.
 */
#include <stdio.h>
#include <string.h>

#include "sym_interpret.h"
#include "../lpres/lpres.h"

static int checks = 0;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL line %d: %s\n", __LINE__, #cond);                                         \
            return 1;                                                                              \
        }                                                                                          \
        checks++;                                                                                  \
    } while (0)

int main(void)
{
    sym_master_t master;

    /* ---- 1. archetypal reduction ----------------------------------------- */
    CHECK(sym_reduce(0u, &master) == 0);
    CHECK(master == SYM_MASTER_NONE);
    /* 7 -> 7 */
    CHECK(sym_reduce(7u, &master) == SYM_ARCH_PURGE);
    CHECK(master == SYM_MASTER_NONE);
    /* 123 -> 1+2+3 = 6 -> heur */
    CHECK(sym_reduce(123u, &master) == SYM_ARCH_HEUR);
    /* 92 -> 9+2 = 11 -> MASTER 11, archetype 2 */
    CHECK(sym_reduce(92u, &master) == SYM_ARCH_DUAL);
    CHECK(master == SYM_MASTER_11);
    /* 445 -> 4+4+5 = 13 -> 4 -> mem */
    CHECK(sym_reduce(445u, &master) == SYM_ARCH_MEM);
    CHECK(master == SYM_MASTER_NONE);
    /* 55555 -> 25 -> 7 -> purge */
    CHECK(sym_reduce(55555u, &master) == SYM_ARCH_PURGE);
    /* 999... (multiples of 9) -> 9 -> converge */
    CHECK(sym_reduce(999u, &master) == SYM_ARCH_CONVERGE);
    /* 187 -> 1+8+7 = 16 -> 7 */
    CHECK(sym_reduce(187u, &master) == SYM_ARCH_PURGE);
    /* names */
    CHECK(strcmp(sym_archetype_name(SYM_ARCH_INIT), "init") == 0);
    CHECK(strcmp(sym_archetype_name(SYM_ARCH_CONVERGE), "converge") == 0);
    CHECK(strcmp(sym_master_name(SYM_MASTER_22), "milestone-complete") == 0);

    /* ---- 2. soul/expression split ----------------------------------------- */
    {
        sym_split_t sp;
        /* "ka" — one consonant, one vowel: (1,1) = BOTH */
        sym_split("ka", &sp);
        CHECK(sp.vowels == 1u && sp.consonants == 1u && sp.separators == 0u);
        CHECK(sp.valence == LPRES_STATE_BOTH);
        /* "ae" — vowels only: (1,0) = TRUE */
        sym_split("ae", &sp);
        CHECK(sp.vowels == 2u && sp.consonants == 0u);
        CHECK(sp.valence == LPRES_STATE_TRUE);
        /* "xyz" — consonants only: (0,1) = FALSE */
        sym_split("xyz", &sp);
        CHECK(sp.consonants == 3u && sp.vowels == 0u);
        CHECK(sp.valence == LPRES_STATE_FALSE);
        /* "123" — separators only: (0,0) = NEITHER */
        sym_split("123", &sp);
        CHECK(sp.separators == 3u);
        CHECK(sp.valence == LPRES_STATE_NEITHER);
        /* case-insensitive: "AI" same as "ai" (both vowels: TRUE) */
        {
            sym_split_t u;
            sym_split("AI", &u);
            CHECK(u.vowels == 2u && u.consonants == 0u);
            CHECK(u.valence == LPRES_STATE_TRUE);
        }
        /* null is NEITHER */
        sym_split(NULL, &sp);
        CHECK(sp.valence == LPRES_STATE_NEITHER);
    }

    /* ---- 3. recursive cycle stacking ---------------------------------------- */
    {
        sym_context_t c;
        sym_context_init(&c);
        CHECK(c.cycles == 0u && c.period_count == 0u);
        /* push nine periods (a full cycle) */
        for (int i = 0; i < 9; i++) sym_period_push(&c, (sym_archetype_t) (1 + (i % 9)));
        CHECK(c.period_count == 9u);
        CHECK(c.cycles == 1u);
        /* the ring wraps: pushing two more keeps count at 9 */
        sym_period_push(&c, SYM_ARCH_INIT);
        sym_period_push(&c, SYM_ARCH_DUAL);
        CHECK(c.period_count == 9u);
        CHECK(c.period_head == 2u);

        /* awareness recursion: four levels, deterministic, bounded */
        {
            uint64_t a1 = sym_awareness_advance(&c, 144u);
            uint64_t a2 = sym_awareness_advance(&c, 144u);
            CHECK(a1 == a2); /* deterministic               */
            CHECK(c.aware[SYM_AWARE_ENV] != 0u);
            CHECK(c.aware[SYM_AWARE_TWO] == a1);
            /* a different tick changes the recursion */
            CHECK(sym_awareness_advance(&c, 145u) != a1);
        }
    }

    /* ---- 4. narrative formatting -------------------------------------------- */
    {
        sym_narrative_t n;
        char buf[128];
        uint32_t w;
        n.arch = SYM_ARCH_PURGE;
        n.master = SYM_MASTER_NONE;
        n.polarity = -555;
        n.harmonic = 693u;
        w = sym_narrate(&n, buf, sizeof buf);
        CHECK(w > 0u && w < sizeof buf);
        CHECK(strstr(buf, "fault purge") != NULL);
        CHECK(strstr(buf, "-555") != NULL);
        CHECK(strstr(buf, "locked") != NULL);
        /* bounded: a tiny buffer never overflows and is NUL-terminated */
        {
            char small[8];
            uint32_t ws = sym_narrate(&n, small, sizeof small);
            CHECK(ws < sizeof small);
            CHECK(small[ws] == 0);
        }
        /* master appears in the line */
        n.master = SYM_MASTER_33;
        n.harmonic = 0u;
        sym_narrate(&n, buf, sizeof buf);
        CHECK(strstr(buf, "harmonic-balance") != NULL);
        CHECK(strstr(buf, "free") != NULL);
        /* nulls are safe */
        CHECK(sym_narrate(NULL, buf, sizeof buf) == 0u);
        CHECK(sym_narrate(&n, NULL, 0u) == 0u);
    }

    printf("sym_interpret: %d checks passed\n", checks);
    return 0;
}