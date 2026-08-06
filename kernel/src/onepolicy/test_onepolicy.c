/* test_onepolicy.c — The One Policy decides deals by definition, not by decree.
 *
 * The external anchor is the Symbiotic Maxim's own three named exclusions
 * (asymmetric harm / unilateral extraction / non-reciprocal burden) plus the
 * three concrete rules the ASCW proposals state (no usury; no coercion — the
 * Grace of Somalia no-revoke/no-withhold rules; the Return Doctrine). Each case
 * is a hand-built term whose verdict is known from the maxim, not from our code.
 */
#include <stdio.h>
#include "onepolicy.h"

static int failures = 0;
#define CHECK(c,m) do{ if(!(c)){printf("[FAIL] %s\n",m);failures++;} \
    else printf("[PASS] %s\n",m);}while(0)

/* a fair, reciprocal, harmless base term: equal value both ways, no harm */
static op_term_t fair(void) {
    op_term_t t = {0};
    t.give_a = SR_FROM_INT(10);
    t.give_b = SR_FROM_INT(10);
    t.reciprocal = true;
    return t;
}

int main(void) {
    printf("=== The One Policy: the Symbiotic Maxim as a decidable predicate ===\n");
    printf("       %s\n", op_maxim_text());

    /* ---- the admissible base case ---- */
    {
        op_term_t t = fair();
        CHECK(op_evaluate(&t) == OP_VALID, "a fair, reciprocal, harmless exchange is VALID");
        CHECK(op_symbiotic_ok(&t), "op_symbiotic_ok agrees it is admissible");
    }

    /* ---- Return Doctrine: a fraud-rooted claim is void and reverts ---- */
    {
        op_term_t t = fair();
        t.fraud_root = true;
        CHECK(op_evaluate(&t) == OP_VOID_FRAUD_ROOT, "a claim rooted in fraud is VOID_FRAUD_ROOT");
        CHECK(op_return_doctrine_reverts(&t), "the Return Doctrine reverts it to the rightful holder");
        CHECK(!op_return_doctrine_reverts(&(op_term_t){0}), "a clean claim does not revert");
    }

    /* ---- no usury: any interest voids the term ---- */
    {
        op_term_t t = fair();
        t.interest = SR_FROM_INT(1);
        CHECK(op_evaluate(&t) == OP_VOID_USURY, "an interest-bearing term is VOID_USURY");
        /* zero interest is fine */
        op_term_t z = fair(); z.interest = SR_ZERO;
        CHECK(op_evaluate(&z) == OP_VALID, "zero interest is admissible (leverage is bounded elsewhere)");
    }

    /* ---- no coercion (the kill-switch-free / Grace-of-Somalia gate) ---- */
    {
        op_term_t a = fair(); a.denies_aid = true;
        op_term_t b = fair(); b.revoke_for_nonpayment = true;
        op_term_t c = fair(); c.has_kill_switch = true;
        CHECK(op_evaluate(&a) == OP_VOID_COERCION, "withholding aid is VOID_COERCION");
        CHECK(op_evaluate(&b) == OP_VOID_COERCION, "revoke-for-non-payment is VOID_COERCION");
        CHECK(op_evaluate(&c) == OP_VOID_COERCION, "a central kill switch is VOID_COERCION");
        CHECK(!op_grace_of_somalia_ok(&a) && !op_grace_of_somalia_ok(&c),
              "op_grace_of_somalia_ok rejects aid-denial and kill switches");
        op_term_t ok = fair();
        CHECK(op_grace_of_somalia_ok(&ok), "a non-coercive term passes the Grace gate");
    }

    /* ---- Maxim (1): asymmetric harm — a party harmed and left net-worse ---- */
    {
        op_term_t t = fair();
        t.harm_a = SR_FROM_INT(50);      /* A conveys 10, receives 10, but eats 50 harm */
        CHECK(op_evaluate(&t) == OP_VOID_ASYMMETRIC_HARM,
              "a party harmed and left net-worse => VOID_ASYMMETRIC_HARM");
        /* harm that is fully compensated is NOT asymmetric: A also receives more */
        op_term_t comp = fair();
        comp.harm_a = SR_FROM_INT(5);
        comp.give_b = SR_FROM_INT(20);   /* A receives 20, conveys 10, eats 5 => net +5 */
        CHECK(op_evaluate(&comp) == OP_VALID, "harm fully answered by value is admissible");
    }

    /* ---- Maxim (2): unilateral extraction — one-way value, no return ---- */
    {
        op_term_t t = {0};
        t.give_a = SR_FROM_INT(10);      /* A gives, B gives nothing, no commitment */
        t.give_b = SR_ZERO;
        t.reciprocal = false;
        CHECK(op_evaluate(&t) == OP_VOID_UNILATERAL_EXTRACTION,
              "one-way value with no return => VOID_UNILATERAL_EXTRACTION");
        /* RED TEAM: setting reciprocal=true does NOT cure a one-way flow — the flag
         * is caller-asserted and cannot override the values. Value flows one way,
         * so it stays extraction. (An attacker used this to slip a pure take past.) */
        op_term_t lie = t; lie.reciprocal = true;
        CHECK(op_evaluate(&lie) == OP_VOID_UNILATERAL_EXTRACTION,
              "a claimed-reciprocal one-way flow is STILL extraction (the flag cannot lie)");
        /* the canonical exploit: A conveys 100 and receives literally nothing, but
         * claims reciprocity. It must be VOID, not VALID. */
        op_term_t exploit = {0};
        exploit.give_a = SR_FROM_INT(100); exploit.give_b = SR_ZERO; exploit.reciprocal = true;
        CHECK(op_evaluate(&exploit) == OP_VOID_UNILATERAL_EXTRACTION,
              "give_a=100, give_b=0, reciprocal=true => VOID (pure extraction closed)");
    }

    /* ---- Maxim (3): non-reciprocal burden — nets negative on one side ---- */
    {
        op_term_t t = {0};
        t.give_a = SR_FROM_INT(10);      /* both convey, but A nets -5 */
        t.give_b = SR_FROM_INT(5);
        t.reciprocal = false;
        CHECK(op_evaluate(&t) == OP_VOID_NONRECIPROCAL_BURDEN,
              "value both ways but net-negative on one side => VOID_NONRECIPROCAL_BURDEN");
        op_term_t cured = t; cured.reciprocal = true;
        CHECK(op_evaluate(&cured) == OP_VALID, "a return commitment cures the burden");
    }

    /* ---- determinism + precedence of gates ---- */
    {
        /* a term that trips several gates at once returns the HIGHEST-precedence
         * reason (fraud before usury before coercion), deterministically */
        op_term_t t = fair();
        t.fraud_root = true; t.interest = SR_FROM_INT(9); t.has_kill_switch = true;
        CHECK(op_evaluate(&t) == OP_VOID_FRAUD_ROOT, "fraud outranks usury and coercion");
        int stable = 1;
        for (int i = 0; i < 1000; i++) if (op_evaluate(&t) != OP_VOID_FRAUD_ROOT) stable = 0;
        CHECK(stable, "verdict is deterministic across 1000 repeated evaluations");
    }

    /* ---- NULL is not a term ---- */
    CHECK(op_evaluate(0) != OP_VALID, "a NULL term is not admissible");

    printf("\n%s: %d failure(s)\n", failures?"*** FAILED ***":"ALL PASS", failures);
    return failures?1:0;
}
