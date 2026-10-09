/* test_pterm_mux.c — phase-tick multi-terminal (ISF dispatch) tests
 *
 * Proves the mux is event-driven and ISF-weighted, not clock-driven:
 *   - g(u) = 1 + (N-1)u exactly (ISF Thm 2.1 effective count)
 *   - a lone terminal has weight 1 (g(0)=1) regardless of u
 *   - orthogonal (u=1) work earns proportionally more dispatch
 *   - redundant (u=0) subs still make progress (no starvation)
 *   - dispatch is DETERMINISTIC for a given tick sequence (replayable)
 *   - pause/resume and close re-weight the rotation correctly
 *
 *   gcc -std=c11 -Wall -Wextra -Isrc/pterm src/pterm/test_pterm_mux.c \
 *       src/pterm/pterm_mux.c -o /tmp/test_pterm_mux && /tmp/test_pterm_mux
 */
#include <stdio.h>
#include <stdint.h>
#include "pterm_mux.h"

static int failures = 0;
#define CHECK(c, m)                                                                                \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            printf("[FAIL] %s\n", m);                                                              \
            failures++;                                                                            \
        } else                                                                                     \
            printf("[PASS] %s\n", m);                                                              \
    } while (0)

static double W(uint32_t q)
{
    return (double) q / (double) PMUX_ONE;
}

int main(void)
{
    pmux_t m;
    printf("=== P-TERM phase-tick multiplexer (ISF dispatch) ===\n");

    pmux_init(&m);
    CHECK(m.initialized && m.num_subs == 0, "init: empty mux");

    /* one sub: g(0)=1 — a lone terminal is exactly one terminal */
    int32_t a = pmux_spawn(&m, "build", 1, 100);
    CHECK(a == 0, "spawn first sub");
    CHECK(m.num_active == 1, "one active sub");
    CHECK(m.sub[0].weight_q16 == PMUX_ONE, "lone sub weight g=1 even at u=1 (ISF g(0)=1 boundary)");

    /* add two more: N=3, weights must be 1+(N-1)u */
    int32_t b = pmux_spawn(&m, "net", 2, 0);    /* fully redundant */
    int32_t c = pmux_spawn(&m, "audio", 3, 50); /* half-orthogonal */
    CHECK(b == 1 && c == 2, "spawn two more subs");
    CHECK(m.num_active == 3, "three active subs");
    CHECK(m.sub[0].weight_q16 == 3 * PMUX_ONE, "u=1.00, N=3 -> g = 1+(3-1)*1.00 = 3");
    CHECK(m.sub[1].weight_q16 == PMUX_ONE, "u=0.00, N=3 -> g = 1 (redundant contributes one)");
    CHECK(m.sub[2].weight_q16 == 2 * PMUX_ONE, "u=0.50, N=3 -> g = 1+(3-1)*0.5 = 2");
    printf("       weights: build=%.2f net=%.2f audio=%.2f\n", W(m.sub[0].weight_q16),
           W(m.sub[1].weight_q16), W(m.sub[2].weight_q16));

    /* run 600 phase ticks; dispatch should track the weights 3:1:2 */
    for (int i = 0; i < 600; i++) (void) pmux_phase_tick(&m);
    uint64_t d0 = m.sub[0].dispatches, d1 = m.sub[1].dispatches, d2 = m.sub[2].dispatches;
    printf("       dispatches after 600 ticks: build=%llu net=%llu audio=%llu\n",
           (unsigned long long) d0, (unsigned long long) d1, (unsigned long long) d2);
    CHECK(d0 > d2 && d2 > d1, "orthogonal work earns more turns than redundant");
    CHECK(d1 > 0, "redundant sub still progresses (no starvation)");
    /* ratio check: build:net should be about 3:1 (allow +/-10%) */
    {
        double r = (double) d0 / (double) (d1 ? d1 : 1);
        CHECK(r > 2.7 && r < 3.3, "build:net dispatch ratio ~= 3:1 (matches g)");
    }

    /* determinism: an identical mux fed the same tick count must match */
    {
        pmux_t m2;
        pmux_init(&m2);
        pmux_spawn(&m2, "build", 1, 100);
        pmux_spawn(&m2, "net", 2, 0);
        pmux_spawn(&m2, "audio", 3, 50);
        for (int i = 0; i < 600; i++) (void) pmux_phase_tick(&m2);
        CHECK(m2.sub[0].dispatches == d0 && m2.sub[1].dispatches == d1 &&
                  m2.sub[2].dispatches == d2,
              "dispatch is deterministic for the same tick sequence (replayable)");
    }

    /* pause re-weights: N drops to 2 */
    CHECK(pmux_set_state(&m, 0, PMUX_SLOT_PAUSED), "pause sub 0");
    CHECK(m.num_active == 2, "two active after pause");
    CHECK(m.sub[2].weight_q16 == PMUX_ONE + (PMUX_ONE / 2),
          "u=0.50, N=2 -> g = 1+(2-1)*0.5 = 1.5 (re-weighted)");
    CHECK(m.sub[0].weight_q16 == 0, "paused sub has no weight");

    /* paused sub receives no dispatch */
    uint64_t before = m.sub[0].dispatches;
    for (int i = 0; i < 100; i++) (void) pmux_phase_tick(&m);
    CHECK(m.sub[0].dispatches == before, "paused sub is not dispatched");

    /* resume + close */
    CHECK(pmux_set_state(&m, 0, PMUX_SLOT_ACTIVE), "resume sub 0");
    CHECK(m.num_active == 3, "three active after resume");
    CHECK(pmux_close(&m, 1), "close sub 1");
    CHECK(m.num_active == 2 && m.num_subs == 2, "counts after close");

    /* focus */
    CHECK(pmux_set_focus(&m, 2) && m.focus == 2, "set focus to sub 2");
    CHECK(!pmux_set_focus(&m, 1), "cannot focus a closed slot");

    /* capacity */
    {
        pmux_t f;
        pmux_init(&f);
        int ok = 1;
        for (int i = 0; i < PMUX_MAX_SUBS; i++)
            if (pmux_spawn(&f, "x", 0, 50) < 0) ok = 0;
        CHECK(ok, "can fill all PMUX_MAX_SUBS slots");
        CHECK(pmux_spawn(&f, "overflow", 0, 50) < 0, "spawn past capacity rejected");
    }

    printf("\n%s: %d failure(s)\n", failures ? "*** FAILED ***" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
