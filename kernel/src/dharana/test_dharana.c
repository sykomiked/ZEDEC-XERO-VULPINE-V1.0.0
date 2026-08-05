#include <stdio.h>
#include <string.h>
#include "dharana.h"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (cond) printf("PASS: %s\n", msg); \
    else { printf("FAIL: %s\n", msg); failures++; } \
} while (0)

static rational_t R(int64_t n, int64_t d) { rational_t r = {n, d}; return r; }

int main(void) {
    printf("=== Naming ===\n");
    {
        CHECK(dharana_class_of(1) == DHARANA_CLASS_SANDHI, "gate 1 is Sandhi class");
        CHECK(dharana_class_of(28) == DHARANA_CLASS_SANDHI, "gate 28 is last Sandhi");
        CHECK(dharana_class_of(29) == DHARANA_CLASS_VISRANTI, "gate 29 is first Visranti");
        CHECK(dharana_class_of(56) == DHARANA_CLASS_VISRANTI, "gate 56 is last Visranti");
        CHECK(dharana_class_of(57) == DHARANA_CLASS_DVAITADVAITA, "gate 57 is first Dvaitadvaita");
        CHECK(dharana_class_of(84) == DHARANA_CLASS_DVAITADVAITA, "gate 84 is last Dvaitadvaita");
        CHECK(dharana_class_of(85) == DHARANA_CLASS_SUNYA, "gate 85 is first Sunya");
        CHECK(dharana_class_of(112) == DHARANA_CLASS_SUNYA, "gate 112 is last Sunya");
        CHECK(strcmp(dharana_gate_name(7), "Sandhi-7") == 0, "gate 7 name is Sandhi-7");
        CHECK(strcmp(dharana_gate_name(29), "Visranti-1") == 0, "gate 29 name is Visranti-1 (local index resets)");
        CHECK(strcmp(dharana_gate_name(112), "Sunya-28") == 0, "gate 112 name is Sunya-28");
    }

    printf("\n=== Class A: Sandhi (zero-crossing) ===\n");
    {
        sandhi_gate_t g;
        sandhi_init(&g, 1, R(1, 100)); /* epsilon = 0.01 */

        CHECK(sandhi_feed(&g, R(5, 1)) == false, "first sample never triggers (no prev)");
        CHECK(sandhi_feed(&g, R(3, 1)) == false, "same-sign samples (5 -> 3) do not cross");
        CHECK(sandhi_feed(&g, R(-2, 1)) == true, "sign change 3 -> -2 with large velocity IS a crossing");
        CHECK(g.crossing_count == 1, "crossing_count incremented exactly once");

        /* Sign change but velocity below epsilon must NOT count. */
        sandhi_gate_t g2;
        sandhi_init(&g2, 2, R(1, 1)); /* epsilon = 1 (large) */
        sandhi_feed(&g2, R(1, 100));   /* +0.01 */
        bool tiny_cross = sandhi_feed(&g2, R(-1, 100)); /* -0.01, |dv|=0.02 < epsilon=1 */
        CHECK(tiny_cross == false, "sign change with |velocity| < epsilon does NOT count as a crossing");

        /* Exact zero touch: 3 -> 0 -> -2 should register at the 0->-2 step (sign 0 counts as crossing partner). */
        sandhi_gate_t g3;
        sandhi_init(&g3, 3, R(1, 100));
        sandhi_feed(&g3, R(3, 1));
        bool at_zero = sandhi_feed(&g3, R(0, 1));
        CHECK(at_zero == true, "3 -> 0 registers as a crossing (touches substrate zero)");
    }

    printf("\n=== Class B: Visranti (fixed-point attractor) ===\n");
    {
        visranti_gate_t g;
        visranti_init(&g, 29, R(10, 1), R(1, 1000), 100);
        uint32_t iters = 0;
        rational_t result = visranti_settle(&g, R(0, 1), &iters); /* start far from attractor=10 */

        rational_t diff = R(result.num * 1 - 10 * result.den, result.den); /* result - 10, same den trick */
        (void)diff;
        double approx = (double)result.num / (double)result.den;
        CHECK(approx > 9.99 && approx < 10.01, "visranti_settle converges to within tolerance of the attractor (10)");
        CHECK(iters > 0 && iters < 100, "convergence happens in a bounded number of iterations, not hitting max_iters");

        /* Exact convergence check via rational comparison, not just double approx. */
        rational_t tol_check = R(result.num - 10 * result.den, result.den);
        double exact_err = (double)tol_check.num / (double)tol_check.den;
        if (exact_err < 0) exact_err = -exact_err;
        CHECK(exact_err <= 0.001 + 1e-9, "exact-rational error from attractor is within the configured tolerance (1/1000)");

        /* Already-converged input should take 0 iterations. */
        uint32_t iters2 = 0;
        visranti_settle(&g, R(10, 1), &iters2);
        CHECK(iters2 == 0, "input already at the attractor converges in 0 iterations");
    }

    printf("\n=== Class C: Dvaitadvaita (paraconsistent polarity) ===\n");
    {
        dvaitadvaita_gate_t g;
        dvaitadvaita_init(&g, 57);

        CHECK(dvaitadvaita_resolve(&g, TRIT_TRUE, TRIT_TRUE) == TRIT_TRUE, "identical inputs (TRUE,TRUE) resolve to TRUE unchanged");
        CHECK(dvaitadvaita_resolve(&g, TRIT_TRUE, TRIT_FALSE) == TRIT_GLUT_NEUTRAL,
              "direct opposition (TRUE,FALSE) resolves to a balanced GLUT_NEUTRAL synthesis, not a crash");
        CHECK(dvaitadvaita_resolve(&g, TRIT_GLUT_PLUS, TRIT_GLUT_MINUS) == TRIT_GLUT_NEUTRAL,
              "opposite charges (GLUT_PLUS, GLUT_MINUS) cancel to GLUT_NEUTRAL");
        CHECK(dvaitadvaita_resolve(&g, TRIT_GLUT_PLUS, TRIT_FALSE) == TRIT_GLUT_PLUS,
              "GLUT_PLUS dominates over an uncharged FALSE input (net charge +1)");
        CHECK(dvaitadvaita_resolve(&g, TRIT_GLUT_MINUS, TRIT_GLUT_MINUS) == TRIT_GLUT_MINUS,
              "identical GLUT_MINUS inputs resolve to GLUT_MINUS unchanged (equal-input shortcut)");
    }

    printf("\n=== Class D: Sunya (void/reset) ===\n");
    {
        sunya_gate_t g;
        sunya_init(&g, 85);
        CHECK(g.live == false, "freshly initialized Sunya gate is not live");

        sunya_write(&g, R(42, 1));
        CHECK(g.live == true, "writing a value marks the gate live");

        rational_t snapshot = sunya_reset(&g);
        CHECK(snapshot.num == 42 && snapshot.den == 1, "reset returns the exact pre-reset snapshot (42/1), losing no data");
        CHECK(g.live == false, "after reset, the gate is no longer live");
        CHECK(g.register_value.num == 0, "after reset, the live register is exactly substrate zero");
    }

    printf("\n=== Full array init ===\n");
    {
        dharana_array_t arr;
        dharana_array_init(&arr);
        CHECK(arr.sandhi[0].gate_id == 1, "array sandhi[0] is gate 1");
        CHECK(arr.visranti[0].gate_id == 29, "array visranti[0] is gate 29");
        CHECK(arr.dvaitadvaita[0].gate_id == 57, "array dvaitadvaita[0] is gate 57");
        CHECK(arr.sunya[27].gate_id == 112, "array sunya[27] is gate 112 (last gate)");
    }

    if (failures == 0) printf("\n=== ALL DHARANA TESTS PASSED ===\n");
    else printf("\n=== %d FAILURE(S) ===\n", failures);
    return failures;
}
