/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zl_nada.c — Śikṣā classes and earcons (zl_nada.h).
 *
 *   test_zl_nada [TABLE.MD]   with TABLE.MD, also writes the Śikṣā table */
#include <stdio.h>
#include <string.h>
#include "zl.h"
#include "zl_kpy.h"
#include "zl_nada.h"
#include "zl_sutra.h"

static unsigned checks, fails;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            fails++;                                                                               \
            printf("[FAIL] " __VA_ARGS__);                                                         \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

static const char *const STHANA[6] = {"kaṇṭhya (throat)", "tālavya (palate)",
                                      "mūrdhanya (roof)", "dantya (teeth)",
                                      "oṣṭhya (lips)",    "dantoṣṭhya (teeth and lips)"};
static const char *const PRAYATNA[8] = {
    "unvoiced", "unvoiced, aspirated", "voiced",   "voiced, aspirated",
    "nasal",    "semivowel",           "sibilant", "voiced breath"};

int main(int argc, char **argv)
{
    /* every consonant: its digit is Kaṭapayādi's, it matches itself, the
     * 5x5 stops sit in their row and column */
    uint32_t seen = 0;
    for (uint32_t i = 0; i < ZL_NADA_CONSONANTS; i++) {
        const zl_varna_t *v = zl_nada_consonant(i);
        char syl[16];
        snprintf(syl, sizeof syl, "%sa", v->iast);
        uint64_t k = 99;
        uint32_t n = 0;
        CHECK(zl_kpy_value(syl, &k) == 1 && k == v->digit, "%s: digit %u, Kaṭapayādi %llu", v->iast,
              v->digit, (unsigned long long) k);
        CHECK(zl_nada_match(syl, &n) == v && n == strlen(v->iast), "%s matches itself", v->iast);
        if (i < 25)
            CHECK(v->sthana == i / 5 && v->prayatna == i % 5, "%s is row %u column %u", v->iast,
                  i / 5, i % 5);
        else
            CHECK(v->prayatna >= 5, "%s is not a stop", v->iast);
        seen |= 1u << v->sthana;
    }
    CHECK(seen == 63u && !zl_nada_consonant(ZL_NADA_CONSONANTS) && !zl_nada_match("a", 0) &&
              !zl_nada_match(0, 0),
          "six places; no consonant past the end or in a vowel");

    /* every op's earcon, for every evidence tag */
    static const uint32_t CARRIER[6] = {144, 162, 180, 192, 216, 240};
    for (uint32_t op = 0; op < ZL_OP__COUNT; op++)
        for (uint32_t b = 0; b < 4; b++) {
            zl_tone_t t;
            CHECK(zl_nada_tone(zl_sutra_cell(op, b), &t) == 0 && t.phase_deg == 90u * b &&
                      t.envelope < 8u,
                  "op %u tag %u", op, b);
            uint32_t ok = 0;
            for (uint32_t s = 0; s < 6; s++)
                ok |= (t.carrier_hz == CARRIER[s]) | (t.formant_hz == 2u * CARRIER[s]) << 1;
            CHECK(ok == 3u, "op %u: %u Hz and %u Hz", op, t.carrier_hz, t.formant_hz);
        }
    {
        zl_tone_t t;
        /* JMP is √gam: g (throat, voiced) to m (lips) */
        CHECK(zl_nada_tone(zl_sutra_cell(ZL_JMP, 1), &t) == 0 && t.carrier_hz == 144 &&
                  t.formant_hz == 432 && t.envelope == ZL_GHOSA && t.phase_deg == 90,
              "JMP's tone");
        CHECK(zl_nada_tone(zl_sutra_cell(ZL_JMP, 1) | 1ull << 49, &t) < 0 &&
                  zl_nada_tone(0, &t) < 0 && zl_nada_tone(zl_sutra_cell(ZL_JMP, 1), 0) < 0,
              "no tone for a cell that is not an op's");
    }

    if (argc > 1) {
        FILE *f = fopen(argv[1], "w");
        fprintf(f, "| Consonant | Sthāna | Prayatna | Kaṭapayādi |\n|---|---|---|---|\n");
        for (uint32_t i = 0; i < ZL_NADA_CONSONANTS; i++) {
            const zl_varna_t *v = zl_nada_consonant(i);
            fprintf(f, "| %s | %s | %s | %u |\n", v->iast, STHANA[v->sthana], PRAYATNA[v->prayatna],
                    v->digit);
        }
        fclose(f);
    }
    printf("%u checks, %u failures\n", checks, fails);
    return fails != 0;
}
