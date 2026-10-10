#include <stdio.h>
#include "dharma.h"
#include "bodhi.h"
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
    dharma_set_t d;
    sched_init(&sched);
    oseq_init(&oseq);
    dharma_init(&d, &sched, &oseq);
    rmag_init(4096);

    printf("=== Bridge: trit_t <-> l13_phase_t (phases 7-13) ===\n");
    CHECK(dharma_trit_to_phase(TRIT_TRUE) == SEPH_MALKUTH,
          "TRIT_TRUE bridges to Malkuth (running)");
    CHECK(dharma_trit_to_phase(TRIT_FALSE) == VEIL_AIN,
          "TRIT_FALSE bridges to Ain (terminated/no-thing)");
    CHECK(dharma_phase_to_trit(SEPH_MALKUTH) == TRIT_TRUE, "Malkuth bridges back to TRIT_TRUE");
    CHECK(dharma_phase_to_trit(SEPH_NETZACH) == TRIT_FALSE,
          "Netzach (OS-native sleep) has no kernel-trit equivalent -> TRIT_FALSE fallback");

    printf("\n=== Dharma is a literal set: |D| ===\n");
    CHECK(dharma_set_size(&d) == 0, "fresh Dharma set has cardinality 0");
    /* Populate tasks directly rather than via sched_create_task: that
     * function's real-stack pointer setup does (uint32_t)&ptr, which
     * truncates a 64-bit host pointer -- safe on the actual 32-bit
     * kernel target, but not host-testable without -m32. Unrelated to
     * Dharma/Karma/Bodhi, so sidestepped here rather than fixed. */
    sched.tasks[0].id = 1;
    sched.tasks[0].state = TASK_READY;
    sched.tasks[1].id = 2;
    sched.tasks[1].state = TASK_READY;
    sched.num_tasks = 2;
    uint32_t t1 = 1, t2 = 2;
    dharma_on_spawn(&d, t1, 1);
    CHECK(dharma_set_size(&d) == 1, "|D| == 1 after one spawn karma emitted");
    dharma_on_spawn(&d, t2, 1);
    CHECK(dharma_set_size(&d) == 2, "|D| == 2 after a second spawn karma emitted");

    printf("\n=== LOCAL karma: toroidal feedback (reacting does not drain the set) ===\n");
    rmag_set_quota((ordinal_t) t1, (rational_t){10, 1});
    lpres_set_presence((ordinal_t) t1, TRIT_TRUE);
    uint32_t size_before = dharma_set_size(&d);
    dharma_advance(&d);
    /* Below ring capacity, the set GROWS each advance: old resolved
     * karma remains as history, and each reaction's feedback is
     * APPENDED as a new karma. It only stabilizes at fixed capacity
     * (evicting oldest per new arrival) once |D| == DHARMA_RING_SIZE. */
    CHECK(dharma_set_size(&d) == size_before * 2,
          "advancing re-emits one feedback karma per reacted karma -- |D| grows until ring "
          "capacity, nothing is drained");
    CHECK(dharma_task_phase(&d, t1) == SEPH_MALKUTH,
          "t1 (high RMAG quota + TRIT_TRUE presence) collapses to Malkuth (running)");
    task_t *task1 = sched_get_task(&sched, t1);
    CHECK(task1 && task1->state == TASK_RUNNING,
          "Dharma's Malkuth decision propagates to sched.c's task_state_t");

    printf("\n=== NON_LOCAL karma: Bell/EPR-style correlation without ordinal precedence ===\n");
    {
        dharma_set_t d2;
        dharma_init(&d2, &sched, &oseq);
        rmag_set_quota((ordinal_t) t2, (rational_t){10, 1});
        lpres_set_presence((ordinal_t) t2, TRIT_TRUE);
        karma_event_t partner_seed =
            karma_cause_nonlocal(t2, t1, oseq.current_cycle, SEPH_YESOD, (rational_t){1, 1});
        dharma_emit(&d2, partner_seed);
        karma_event_t main_seed =
            karma_cause_nonlocal(t1, t2, oseq.current_cycle, SEPH_YESOD, (rational_t){1, 1});
        dharma_emit(&d2, main_seed);
        dharma_advance(&d2);
        CHECK(dharma_task_phase(&d2, t1) == dharma_task_phase(&d2, t2),
              "correlated NON_LOCAL partners collapse to the SAME phase via fixed correlation, not "
              "independent RMAG reads");
    }

    printf("\n=== Entanglement degree ===\n");
    {
        karma_event_t a = karma_cause_nonlocal(t1, t2, 1, SEPH_YESOD, (rational_t){3, 1});
        karma_event_t b = karma_cause_nonlocal(t2, t1, 1, SEPH_YESOD, (rational_t){3, 1});
        rational_t deg = karma_entanglement_degree(&a, &b);
        CHECK(deg.num == deg.den && deg.num != 0,
              "equal weights -> maximal entanglement degree (exactly 1)");
        CHECK(karma_is_entangled(&a, &b),
              "karma_is_entangled reports true for a mutually-correlated pair");

        karma_event_t c = karma_cause_nonlocal(t2, t1, 1, SEPH_YESOD, (rational_t){9, 1});
        rational_t deg2 = karma_entanglement_degree(&a, &c);
        CHECK(rational_cmp(deg2, (rational_t){0, 1}) > 0 && rational_cmp(deg2, (rational_t){1, 1}) < 0,
              "diverging weights (3 vs 9) -> partial entanglement degree strictly between 0 and 1");

        karma_event_t local = karma_cause(t1, 1, SEPH_YESOD, (rational_t){3, 1});
        rational_t deg3 = karma_entanglement_degree(&local, &b);
        CHECK(deg3.num == 0,
              "a LOCAL karma is never entangled with anything -- exact rational zero, not null");
    }

    printf("\n=== MULTI_LOCAL: fixed set of localities reacted simultaneously ===\n");
    {
        dharma_set_t d3;
        dharma_init(&d3, &sched, &oseq);
        uint32_t ids[2] = {t1, t2};
        karma_event_t multi =
            karma_cause_multilocal(ids, 2, oseq.current_cycle, SEPH_YESOD, (rational_t){1, 1});
        dharma_emit(&d3, multi);
        dharma_advance(&d3);
        CHECK(dharma_task_phase(&d3, t1) != 0 && dharma_task_phase(&d3, t2) != 0,
              "both localities in a MULTI_LOCAL karma receive a resolved effect phase");
    }

    printf("\n=== POLYLOCAL: growing locality mask across advances ===\n");
    {
        dharma_set_t d4;
        dharma_init(&d4, &sched, &oseq);
        karma_event_t poly =
            karma_cause_polylocal(1ULL << t1, oseq.current_cycle, SEPH_YESOD, (rational_t){1, 1});
        dharma_emit(&d4, poly);
        dharma_advance(&d4);
        uint32_t idx = (d4.head) % DHARMA_RING_SIZE;
        uint64_t mask_after = d4.ring[idx].locality_mask;
        uint32_t bits_before = 1; /* just t1 */
        int bits_after = 0;
        for (int b = 0; b < 64; b++)
            if ((mask_after >> b) & 1ULL) bits_after++;
        CHECK((uint32_t) bits_after >= bits_before,
              "POLYLOCAL mask does not shrink, and typically grows, across an advance");
    }

    printf("\n=== Bodhi: transcendent observation, vacuum is presence not null ===\n");
    {
        dharma_set_t empty;
        dharma_init(&empty, &sched, &oseq);
        bodhi_state_t vac = bodhi_observe(&empty);
        CHECK(vac.coherence.num != 0, "the VACUUM (|D|==0) has a nonzero, structurally-grounded "
                                      "coherence -- never a null/zero struct");
        CHECK(vac.transcendent == true,
              "the vacuum is transcendent by construction (pure source, not accumulated)");

        bodhi_state_t observed = bodhi_observe(&d);
        CHECK(dharma_set_size(&d) > 0, "sanity: the main set is non-empty for this observation");
        (void) observed;
        printf("  (non-empty-set bodhi observation ran without crashing: coherence=%lld/%lld "
               "dispersion=%lld/%lld transcendent=%d)\n",
               (long long) observed.coherence.num, (long long) observed.coherence.den,
               (long long) observed.dispersion.num, (long long) observed.dispersion.den,
               observed.transcendent);
    }

    if (failures == 0)
        printf("\n=== ALL DHARMA/KARMA/BODHI TESTS PASSED ===\n");
    else
        printf("\n=== %d FAILURE(S) ===\n", failures);
    return failures;
}
