/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* test_swarm_life.c — known-answer tests for the compute governor and the
 * digital DNA. Expected values are worked by hand from the headers. */
#include <stdio.h>
#include "swarm_governor.h"
#include "swarm_dna.h"

static int failures = 0;
#define CHECK(cond, msg)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("  FAIL: %s\n", msg);                                                           \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static void test_governor(void)
{
    /* G1: 8 cores, 64 GB, 24 GB GPU, remote: 99% -> 7920, 64880, 24330.
     * 4 GB agents: 4096 + 8192 + 12288 + 20480 = 45056 fits, +32768 does not. */
    swarm_hw_scan_t r = {true, 8, 0, 65536, 65536, 24576};
    swarm_compute_budget_t g = swarm_governor_budget(&r, 4096, 1000);
    CHECK(g.cores_milli == 7920 && g.mem_mb == 64880 && g.gpu_mem_mb == 24330, "G1 99% remote");
    CHECK(g.num_levels == 4 && g.num_agents == 11, "G4 4 levels, 11 agents");
    CHECK(g.tokens_per_cycle == 7920, "G5 rate");

    /* G2 + G3: 8 cores with 3 busy, 16 GB with 10 GB free, local.
     * Cores 5000 * 13/21 = 3095. Memory (10240 - 2048) * 13/21 = 5071.
     * 1 GB agents: 1024 + 2048 fits, + 3072 does not. */
    swarm_hw_scan_t l = {false, 8, 3000, 16384, 10240, 0};
    g = swarm_governor_budget(&l, 1024, 500);
    CHECK(g.cores_milli == 3095 && g.mem_mb == 5071, "G2 13/21 of what is free");
    CHECK(g.num_levels == 2 && g.num_agents == 3, "G4 two levels");
    CHECK(g.tokens_per_cycle == 1547, "G5 3.095 cores x 500");

    /* A busy machine: nothing free, the swarm waits. */
    swarm_hw_scan_t busy = {false, 8, 8000, 16384, 10240, 0};
    g = swarm_governor_budget(&busy, 1024, 500);
    CHECK(g.cores_milli == 0 && g.tokens_per_cycle == 0, "G2 never takes what others use");

    /* G3: free memory below the 1/8 floor leaves nothing to use. */
    swarm_hw_scan_t tight = {false, 8, 0, 16384, 1000, 0};
    g = swarm_governor_budget(&tight, 1024, 500);
    CHECK(g.mem_mb == 0 && g.num_levels == 0, "G3 memory floor");

    /* Even an idle local machine is capped at 13/21 of itself. */
    swarm_hw_scan_t idle = {false, 21, 0, 21000, 21000, 2100};
    g = swarm_governor_budget(&idle, 1, 1000);
    CHECK(g.cores_milli == 13000, "G2 cap 13/21 of cores");
    CHECK(g.mem_mb == 11375, "G2 + G3 (21000 - 2625) * 13/21");
    CHECK(g.gpu_mem_mb == 1300, "G2 GPU 13/21");
    CHECK(swarm_governor_budget(0, 1, 1).num_levels == 0, "NULL scan");
}

static void test_dna(void)
{
    swarm_dna_t a = swarm_dna_from_seed(42), a2 = swarm_dna_from_seed(42),
                b = swarm_dna_from_seed(7);
    int same = 1, diff = 0;
    for (unsigned g = 0; g < SWARM_DNA_GENES; g++) {
        same &= a.gene[g] == a2.gene[g];
        diff |= a.gene[g] != b.gene[g];
    }
    CHECK(same, "D1 same seed, same genome");
    CHECK(diff, "D1 different seeds differ");
    CHECK(a.generation == 0 && a.parent_a == 0, "D5 founders have no parents");

    swarm_traits_t t = swarm_dna_express(&a, 5);
    CHECK((unsigned) t.home.emotion < SWARM_EMO_COUNT &&
              t.home.intensity <= SWARM_EMO_MAX_INTENSITY,
          "D2 home feeling in range");
    CHECK(t.curiosity_permille <= 1000 && t.caution_permille <= 1000 && t.specialty < 5,
          "D2 traits in range");

    swarm_dna_t c = swarm_dna_combine(&a, &b, 1), c2 = swarm_dna_combine(&a, &b, 1);
    int inherited = 1, repeat = 1;
    for (unsigned g = 0; g < SWARM_DNA_GENES; g++) {
        int da = (int) c.gene[g] - a.gene[g], db = (int) c.gene[g] - b.gene[g];
        if (da < 0) da = -da;
        if (db < 0) db = -db;
        inherited &= (da <= 3 || db <= 3);
        repeat &= c.gene[g] == c2.gene[g];
    }
    CHECK(inherited, "D3 every gene within 3 of a parent's");
    CHECK(repeat && c.seed == c2.seed, "D3 deterministic");
    CHECK(c.parent_a == 42 && c.parent_b == 7 && c.generation == 1, "D5 lineage");
    swarm_dna_t gc = swarm_dna_combine(&c, &a, 9);
    CHECK(gc.generation == 2, "D5 generations count up");

    uint64_t f[4] = {5, 9, 1, 7};
    uint32_t pa, pb, w;
    CHECK(swarm_dna_select(f, 4, &pa, &pb, &w) && pa == 1 && pb == 3 && w == 2,
          "D4 fittest breed, weakest replaced");
    uint64_t tie[3] = {3, 3, 3};
    CHECK(swarm_dna_select(tie, 3, &pa, &pb, &w) && pa == 0 && pb == 1 && w == 2, "D4 ties");
    CHECK(!swarm_dna_select(f, 2, &pa, &pb, &w), "D4 needs three");
    CHECK(swarm_dna_fitness(30, 5) == 35, "D4 fitness = value + Social");
}

int main(void)
{
    printf("=== test_swarm_life ===\n");
    test_governor();
    test_dna();
    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("all checks passed\n");
    return 0;
}
