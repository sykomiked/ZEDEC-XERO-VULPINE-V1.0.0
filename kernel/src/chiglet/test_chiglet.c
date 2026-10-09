/* test_chiglet.c — ISF-weighted mixture of experts.
 *
 * The load-bearing test is DUPLICATION RESISTANCE. An earlier design
 * claimed "cloning an expert cannot manufacture confidence"; adversarial
 * review falsified it with a concrete counterexample. That counterexample
 * is preserved here as a permanent regression so the claim can never
 * silently become false again.
 *
 *   gcc -std=c11 -Wall -Wextra -DTEST_HOST -Isrc/chiglet -Isrc/surplus \
 *       src/chiglet/test_chiglet.c src/chiglet/chiglet.c src/surplus/surplus.c \
 *       -o /tmp/test_chiglet -lm && /tmp/test_chiglet
 */
#include <stdio.h>
#include <string.h>
#include "chiglet.h"

static int failures = 0;
#define CHECK(c, m)                                                                                \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            printf("[FAIL] %s\n", m);                                                              \
            failures++;                                                                            \
        } else                                                                                     \
            printf("[PASS] %s\n", m);                                                              \
    } while (0)
#define D(x) ((double) (x) / (double) SR_ONE)

static void setvec(surplus_real_t *v, double a, double b, double c, double d)
{
    v[0] = SR_FROM_FLOAT(a);
    v[1] = SR_FROM_FLOAT(b);
    v[2] = SR_FROM_FLOAT(c);
    v[3] = SR_FROM_FLOAT(d);
    for (uint32_t i = 4; i < CHG_DIM; i++) v[i] = SR_ZERO;
}

static void basic_model(chg_model_t *m, double rmin, double marginmin)
{
    memset(m, 0, sizeof(*m));
    m->K = 4;
    m->D = 4;
    m->L = 2;
    m->epoch = 1;
    setvec(m->proto[0], 1, 0, 0, 0);
    setvec(m->proto[1], 0, 1, 0, 0);
    m->R_min = SR_FROM_FLOAT(rmin);
    m->margin_min = SR_FROM_FLOAT(marginmin);
    m->loaded = true;
}

int main(void)
{
    printf("=== Chiglet — ISF-weighted mixture of small experts ===\n");

    /* ---- interaction parameter ---- */
    {
        surplus_real_t a[CHG_DIM], b[CHG_DIM];
        setvec(a, 1, 0, 0, 0);
        setvec(b, 1, 0, 0, 0);
        CHECK(D(chg_interaction(a, b, 4)) < 0.001, "identical evidence -> u = 0 (fully redundant)");
        setvec(b, 0, 1, 0, 0);
        CHECK(D(chg_interaction(a, b, 4)) > 0.999,
              "orthogonal evidence -> u = 1 (fully independent)");
        setvec(b, 5, 0, 0, 0);
        CHECK(D(chg_interaction(a, b, 4)) < 0.001,
              "u is SCALE-INVARIANT (5x the same direction is still u=0)");
        setvec(b, 0, 0, 0, 0);
        CHECK(D(chg_interaction(a, b, 4)) < 0.001,
              "zero vector is treated as redundant, not a division by zero");
    }

    /* ---- R at the two extremes ---- */
    {
        surplus_real_t ev[CHG_MAX_EXPERTS][CHG_DIM];
        uint32_t dis = 0;
        /* four identical experts */
        for (int i = 0; i < 4; i++) setvec(ev[i], 1, 0, 0, 0);
        surplus_real_t R = chg_effective_experts(ev, 4, 4, &dis);
        printf("       4 identical experts -> R=%.3f, distinct=%u\n", D(R), dis);
        CHECK(D(R) < 1.05 && dis == 1, "total collinearity -> R = 1 (eight voices, one opinion)");

        /* four mutually orthogonal experts */
        setvec(ev[0], 1, 0, 0, 0);
        setvec(ev[1], 0, 1, 0, 0);
        setvec(ev[2], 0, 0, 1, 0);
        setvec(ev[3], 0, 0, 0, 1);
        R = chg_effective_experts(ev, 4, 4, &dis);
        printf("       4 orthogonal experts -> R=%.3f, distinct=%u\n", D(R), dis);
        CHECK(D(R) > 3.9 && dis == 4, "mutual orthogonality -> R = K'");
    }

    /* ================= THE REGRESSION THAT MATTERS =================
     * Adversarial review's counterexample. WITHOUT direction-merging:
     *   e1 orthogonal to e2=e3=e4  ->  R = 4/2.5 = 1.600
     *   clone e1 (K'=5)            ->  R = 5/2.6 = 1.923   (+20%!)
     * An attacker could pump R past R_min by duplicating the most
     * independent expert. With merging, cloning changes nothing. */
    {
        surplus_real_t ev4[CHG_MAX_EXPERTS][CHG_DIM];
        setvec(ev4[0], 1, 0, 0, 0); /* independent */
        setvec(ev4[1], 0, 1, 0, 0); /* three collinear */
        setvec(ev4[2], 0, 1, 0, 0);
        setvec(ev4[3], 0, 1, 0, 0);
        uint32_t d4 = 0;
        surplus_real_t R4 = chg_effective_experts(ev4, 4, 4, &d4);

        surplus_real_t ev5[CHG_MAX_EXPERTS][CHG_DIM];
        memcpy(ev5, ev4, sizeof(ev4));
        setvec(ev5[4], 1, 0, 0, 0); /* CLONE of the independent one */
        uint32_t d5 = 0;
        surplus_real_t R5 = chg_effective_experts(ev5, 5, 4, &d5);

        printf("       counterexample: R(4)=%.3f distinct=%u  ->  clone -> R(5)=%.3f distinct=%u\n",
               D(R4), d4, D(R5), d5);
        CHECK(d4 == 2 && d5 == 2,
              "duplicates collapse: 4 and 5 experts both yield 2 DISTINCT directions");
        CHECK(D(R5) <= D(R4) + 0.01,
              "CLONING THE INDEPENDENT EXPERT DOES NOT RAISE R (attack blocked)");

        /* and piling on more clones still cannot raise it */
        surplus_real_t ev8[CHG_MAX_EXPERTS][CHG_DIM];
        memcpy(ev8, ev5, sizeof(ev5));
        setvec(ev8[5], 1, 0, 0, 0);
        setvec(ev8[6], 1, 0, 0, 0);
        setvec(ev8[7], 1, 0, 0, 0);
        uint32_t d8 = 0;
        surplus_real_t R8 = chg_effective_experts(ev8, 8, 4, &d8);
        printf("       piling on: R(8)=%.3f distinct=%u\n", D(R8), d8);
        CHECK(d8 == 2 && D(R8) <= D(R4) + 0.01,
              "eight experts, still 2 distinct directions, R unchanged");
    }

    /* ---- THE PRODUCT CLAIM: redundancy produces UNCERTAIN ---- */
    {
        chiglet_t c;
        chg_init(&c, CHG_CAP_INFER);
        chg_model_t m;
        basic_model(&m, 1.8, 0.01);
        CHECK(chg_load_model(&c, &m) == CHG_OK, "model loads");

        /* eight copies of one expert — a softmax gate would call this
         * eight-fold confidence */
        surplus_real_t ev[CHG_MAX_EXPERTS][CHG_DIM];
        for (int i = 0; i < 8; i++) setvec(ev[i], 1, 0, 0, 0);
        chg_result_t r;
        CHECK(chg_infer(&c, ev, 8, &r) == CHG_OK, "inference runs");
        printf("       8 clones -> state=%s reason=\"%s\" R=%.3f distinct=%u\n",
               chg_state_name(r.state), chg_reason_name(r.reason), D(r.R), r.k_distinct);
        CHECK(r.state == CHG_UNCERTAIN && r.reason == CHG_REASON_REDUNDANT,
              "EIGHT AGREEING CLONES -> UNCERTAIN, not confidence");
        CHECK(r.k_valid == 8 && r.k_distinct == 1,
              "8 valid experts but only 1 independent direction");
    }

    /* ---- genuinely independent evidence DOES decide ---- */
    {
        chiglet_t c;
        chg_init(&c, CHG_CAP_INFER);
        chg_model_t m;
        basic_model(&m, 1.8, 0.01);
        chg_load_model(&c, &m);

        surplus_real_t ev[CHG_MAX_EXPERTS][CHG_DIM];
        setvec(ev[0], 1.0, 0.0, 0, 0);
        setvec(ev[1], 0.9, 0.2, 0, 0);
        setvec(ev[2], 0.0, 0.0, 1, 0);
        setvec(ev[3], 0.0, 0.0, 0, 1);
        chg_result_t r;
        chg_infer(&c, ev, 4, &r);
        printf("       independent evidence -> state=%s label=%u R=%.3f S=%.3f margin=%.4f\n",
               chg_state_name(r.state), r.label, D(r.R), D(r.S), D(r.margin));
        CHECK(r.state == CHG_DECIDED, "independent evidence yields a DECISION");
        CHECK(r.label == 0, "it picks the prototype the evidence points at");
        CHECK(D(r.S) > 0.0, "ensemble surplus S = ln R is positive");
        CHECK(r.k_distinct >= 3, "at least three distinct directions counted");
    }

    /* ---- the low-margin abstention ---- */
    {
        chiglet_t c;
        chg_init(&c, CHG_CAP_INFER);
        chg_model_t m;
        basic_model(&m, 1.5, 0.50); /* demand a big margin */
        chg_load_model(&c, &m);
        surplus_real_t ev[CHG_MAX_EXPERTS][CHG_DIM];
        setvec(ev[0], 1, 0, 0, 0);
        setvec(ev[1], 0, 1, 0, 0); /* points equally at both prototypes */
        chg_result_t r;
        chg_infer(&c, ev, 2, &r);
        printf("       ambiguous -> state=%s reason=\"%s\" margin=%.4f\n", chg_state_name(r.state),
               chg_reason_name(r.reason), D(r.margin));
        CHECK(r.state == CHG_UNCERTAIN && r.reason == CHG_REASON_LOW_MARGIN,
              "an unseparable top-2 abstains rather than guessing");
    }

    /* ---- capability gate ---- */
    {
        chiglet_t c;
        chg_init(&c, 0); /* no CHG_CAP_INFER */
        chg_model_t m;
        basic_model(&m, 1.5, 0.01);
        chg_load_model(&c, &m);
        surplus_real_t ev[CHG_MAX_EXPERTS][CHG_DIM];
        setvec(ev[0], 1, 0, 0, 0);
        chg_result_t r;
        CHECK(chg_infer(&c, ev, 1, &r) == CHG_ERR_DENIED,
              "inference without CHG_CAP_INFER is DENIED");
    }

    /* ---- no model => UNAVAILABLE, never a guess ---- */
    {
        chiglet_t c;
        chg_init(&c, CHG_CAP_INFER);
        surplus_real_t ev[CHG_MAX_EXPERTS][CHG_DIM];
        setvec(ev[0], 1, 0, 0, 0);
        chg_result_t r;
        chg_infer(&c, ev, 1, &r);
        CHECK(r.state == CHG_UNAVAILABLE && r.reason == CHG_REASON_NO_MODEL,
              "with no model the runtime reports UNAVAILABLE, it does not guess");
    }

    /* ---- rollback protection on model load ---- */
    {
        chiglet_t c;
        chg_init(&c, CHG_CAP_INFER);
        chg_model_t m;
        basic_model(&m, 1.5, 0.01);
        m.epoch = 5;
        CHECK(chg_load_model(&c, &m) == CHG_OK, "epoch 5 loads");
        chg_model_t old;
        basic_model(&old, 1.5, 0.01);
        old.epoch = 4;
        CHECK(chg_load_model(&c, &old) == CHG_ERR_ARG,
              "an OLDER model epoch is rejected (rollback protection)");
        chg_model_t newer;
        basic_model(&newer, 1.5, 0.01);
        newer.epoch = 6;
        CHECK(chg_load_model(&c, &newer) == CHG_OK, "a newer epoch is accepted");
    }

    /* ---- malformed models rejected ---- */
    {
        chiglet_t c;
        chg_init(&c, CHG_CAP_INFER);
        chg_model_t m;
        basic_model(&m, 1.5, 0.01);
        m.K = 99;
        CHECK(chg_load_model(&c, &m) == CHG_ERR_ARG, "K over bound rejected");
        basic_model(&m, 1.5, 0.01);
        m.D = 0;
        CHECK(chg_load_model(&c, &m) == CHG_ERR_ARG, "zero dimension rejected");
        basic_model(&m, 1.5, 0.01);
        m.L = 99;
        CHECK(chg_load_model(&c, &m) == CHG_ERR_ARG, "L over bound rejected");
    }

    /* ---- determinism ---- */
    {
        chiglet_t a, b;
        chg_model_t m;
        basic_model(&m, 1.5, 0.01);
        chg_init(&a, CHG_CAP_INFER);
        chg_load_model(&a, &m);
        chg_init(&b, CHG_CAP_INFER);
        chg_load_model(&b, &m);
        surplus_real_t ev[CHG_MAX_EXPERTS][CHG_DIM];
        setvec(ev[0], 1, 0.3, 0, 0);
        setvec(ev[1], 0, 1, 0.2, 0);
        setvec(ev[2], 0, 0, 0, 1);
        chg_result_t r1, r2;
        chg_infer(&a, ev, 3, &r1);
        chg_infer(&b, ev, 3, &r2);
        CHECK(r1.state == r2.state && r1.label == r2.label && r1.R == r2.R,
              "identical input yields a bit-identical decision (replayable)");
    }

    printf("\n%s: %d failure(s)\n", failures ? "*** FAILED ***" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
