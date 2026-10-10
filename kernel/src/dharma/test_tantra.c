#include <stdio.h>
#include <string.h>
#include "tantra.h"
#include "rmag_core.h"
#include "lpres_core.h"

static int failures = 0;
#define CHECK(cond, msg)                                                                           \
    do {                                                                                           \
        if (cond)                                                                                  \
            printf("PASS: %s\n", msg);                                                             \
        else {                                                                                     \
            printf("FAIL: %s\n", msg);                                                             \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

int main(void)
{
    scheduler_t sched;
    oseq_state_t oseq;
    tantra_engine_t engine;
    sched_init(&sched);
    oseq_init(&oseq);
    rmag_init(4096);
    tantra_init(&engine, &sched, &oseq);
    sched.tasks[0].id = 1;
    sched.tasks[0].state = TASK_READY;
    sched.num_tasks = 1;

    printf("=== Mantra: signal encoding ===\n");
    const uint8_t seed_a[] = "hello-mantra-seed";
    const uint8_t seed_b[] = "hello-mantra-seed"; /* identical seed */
    const uint8_t seed_c[] = "completely-different-payload-here";
    cyc13_t va = mantra_encode(seed_a, sizeof(seed_a) - 1);
    cyc13_t vb = mantra_encode(seed_b, sizeof(seed_b) - 1);
    cyc13_t vc = mantra_encode(seed_c, sizeof(seed_c) - 1);
    CHECK(cyc13_equal(va, vb),
          "identical seeds encode to identical mantras (resonance is deterministic)");
    CHECK(!cyc13_equal(va, vc), "different seeds encode to different mantras");
    rational_t amp_a = mantra_amplitude(va);
    CHECK(rational_cmp(amp_a, (rational_t){0, 1}) > 0, "a non-empty seed has strictly positive amplitude");
    cyc13_t empty_v = mantra_encode(seed_a, 0);
    rational_t amp_empty = mantra_amplitude(empty_v);
    CHECK(amp_empty.num == 0, "a zero-length seed has exactly zero amplitude (definite, not null)");

    printf("\n=== Yantra: hardware topology containment ===\n");
    yantra_topology_t y1 = yantra_contain(va, L13_DIM_12);
    yantra_topology_t y6 = yantra_contain(va, L13_DIM_6);
    CHECK(y1.dim == L13_DIM_12 && y6.dim == L13_DIM_6,
          "yantra_contain records the requested dimension level");
    rational_t bindu1 = yantra_bindu(&y1); /* L13_DIM_12: |H|=1 */
    rational_t bindu6 = yantra_bindu(&y6); /* L13_DIM_6:  |H|=2 */
    rational_t twice_bindu1 = rmag_mul_quotas(bindu1, (rational_t){2, 1});
    CHECK(bindu6.num == twice_bindu1.num && bindu6.den == twice_bindu1.den,
          "the Bindu scales by the dimension's Galois subgroup order (|H|=2 for DIM_6 vs |H|=1 for "
          "DIM_12), not invariant but exactly predictable");

    printf("\n=== Tantra: weaving Mantra through Yantra into Dharma ===\n");
    uint32_t before = dharma_set_size(&engine.dharma);
    tantra_weave(&engine, 1, seed_a, sizeof(seed_a) - 1, L13_DIM_12, EVENT_LOCAL);
    CHECK(dharma_set_size(&engine.dharma) == before + 1,
          "tantra_weave emits exactly one karma into the Dharma set");

    rmag_set_quota((ordinal_t) 1, (rational_t){10, 1});
    lpres_set_presence((ordinal_t) 1, TRIT_TRUE);
    tantra_result_t r = tantra_run(&engine);
    CHECK(
        r == TANTRA_CLEAR,
        "a well-resolved run (task with strong RMAG+LPRES presence) is TANTRA_CLEAR, not trapped");
    CHECK(dharma_task_phase(&engine.dharma, 1) == SEPH_MALKUTH,
          "the woven task reaches Malkuth after tantra_run");

    printf("\n=== Paradox Trap (dual-rail 1-1-0) ===\n");
    {
        bodhi_state_t contradiction;
        contradiction.coherence = (rational_t){1, 1};  /* rail A asserted */
        contradiction.dispersion = (rational_t){1, 1}; /* rail B asserted */
        contradiction.transcendent = false;            /* neither dominates -- genuine (1,1) */
        CHECK(tantra_paradox_trap(&contradiction) == TANTRA_PARADOX_TRAPPED,
              "both rails asserted with no dominant collapse triggers the Paradox Trap");

        bodhi_state_t clean;
        clean.coherence = (rational_t){1, 1};
        clean.dispersion = (rational_t){0, 1}; /* rail B never asserted */
        clean.transcendent = true;
        CHECK(tantra_paradox_trap(&clean) == TANTRA_CLEAR,
              "only one rail asserted is CLEAR, not a paradox");

        bodhi_state_t dominated;
        dominated.coherence = (rational_t){100, 1};
        dominated.dispersion = (rational_t){1, 1};
        dominated.transcendent = true; /* coherence dominates dispersion */
        CHECK(tantra_paradox_trap(&dominated) == TANTRA_CLEAR,
              "both rails asserted BUT one dominates (transcendent) is CLEAR, not trapped");
    }

    if (failures == 0)
        printf("\n=== ALL MANTRA/YANTRA/TANTRA TESTS PASSED ===\n");
    else
        printf("\n=== %d FAILURE(S) ===\n", failures);
    return failures;
}
