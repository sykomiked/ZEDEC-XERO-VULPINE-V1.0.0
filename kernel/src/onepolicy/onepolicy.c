/* onepolicy.c — The One Policy predicate. See onepolicy.h.
 *
 * Every gate below is definitional, not administrative: the maxim EXCLUDES the
 * term, so we compute the exclusion locally and deterministically. No clock, no
 * network, no central authority.
 */
#include "onepolicy.h"

/* net_x = what X ends up with = (value received) - (value conveyed) - (harm borne).
 * A term is symbiotic when neither party is left net-worse by it. */
static surplus_real_t net_of(surplus_real_t received, surplus_real_t conveyed,
                             surplus_real_t harm) {
    return SR_SUB(received, SR_ADD(conveyed, harm));
}

op_verdict_t op_evaluate(const op_term_t *t) {
    if (!t) return OP_VOID_NONRECIPROCAL_BURDEN; /* an absent term is no term */

    /* 1. Return Doctrine — a claim rooted in fraud is void before anything else
     *    is even weighed. You cannot build a valid term on a stolen root. */
    if (t->fraud_root) return OP_VOID_FRAUD_ROOT;

    /* 2. No usury. Wall Street calls it "a rate"; the One Policy calls it void.
     *    Leverage is allowed only bounded (the φ-Note), never as interest. */
    if (SR_CMP(t->interest, SR_ZERO) > 0) return OP_VOID_USURY;

    /* 3. No coercion (the kill-switch-free mandate). A term may not revoke your
     *    standing for non-payment, withhold aid, or hand anyone a kill switch. */
    if (t->denies_aid || t->revoke_for_nonpayment || t->has_kill_switch)
        return OP_VOID_COERCION;

    const surplus_real_t net_a = net_of(t->give_b, t->give_a, t->harm_a);
    const surplus_real_t net_b = net_of(t->give_a, t->give_b, t->harm_b);
    const bool a_harmed = SR_CMP(t->harm_a, SR_ZERO) > 0;
    const bool b_harmed = SR_CMP(t->harm_b, SR_ZERO) > 0;
    const bool a_worse  = SR_CMP(net_a, SR_ZERO) < 0;
    const bool b_worse  = SR_CMP(net_b, SR_ZERO) < 0;

    /* 4. Asymmetric harm — a party is actively harmed AND left net-worse. */
    if ((a_harmed && a_worse) || (b_harmed && b_worse))
        return OP_VOID_ASYMMETRIC_HARM;

    const bool a_gives = SR_CMP(t->give_a, SR_ZERO) > 0;
    const bool b_gives = SR_CMP(t->give_b, SR_ZERO) > 0;

    /* 5. Unilateral extraction — exactly one side conveys value. This fires
     *    REGARDLESS of the caller-asserted `reciprocal` flag: a term where value
     *    flows only one way IS a take, and `reciprocal` is attacker-controlled — an
     *    exploit sets give_a=100, give_b=0, reciprocal=true precisely to slip a pure
     *    extraction past the maxim. The modeled VALUES are the ground truth, not the
     *    flag; a claim of reciprocity that conveys nothing back is a lie. */
    if (a_gives != b_gives) return OP_VOID_UNILATERAL_EXTRACTION;

    /* 6. Non-reciprocal burden — value flows both ways but nets negative on one
     *    side. Here a GENUINE matched return commitment (reciprocal, with both
     *    sides having actually conveyed value — verified above) can justify a
     *    temporary imbalance; an UNMATCHED one-way net cannot. */
    if (!t->reciprocal && (a_worse || b_worse)) return OP_VOID_NONRECIPROCAL_BURDEN;

    return OP_VALID;
}

bool op_symbiotic_ok(const op_term_t *t) {
    return op_evaluate(t) == OP_VALID;
}

bool op_grace_of_somalia_ok(const op_term_t *t) {
    if (!t) return false;
    return !(t->denies_aid || t->revoke_for_nonpayment || t->has_kill_switch);
}

bool op_return_doctrine_reverts(const op_term_t *t) {
    return t && t->fraud_root;
}

const char *op_verdict_name(op_verdict_t v) {
    switch (v) {
        case OP_VALID:                     return "VALID";
        case OP_VOID_FRAUD_ROOT:           return "VOID_FRAUD_ROOT";
        case OP_VOID_USURY:                return "VOID_USURY";
        case OP_VOID_COERCION:             return "VOID_COERCION";
        case OP_VOID_ASYMMETRIC_HARM:      return "VOID_ASYMMETRIC_HARM";
        case OP_VOID_UNILATERAL_EXTRACTION:return "VOID_UNILATERAL_EXTRACTION";
        case OP_VOID_NONRECIPROCAL_BURDEN: return "VOID_NONRECIPROCAL_BURDEN";
        default:                           return "VOID_UNKNOWN";
    }
}

const char *op_verdict_reason(op_verdict_t v) {
    switch (v) {
        case OP_VALID:
            return "Admissible: reciprocal, no unanswered harm, no extraction.";
        case OP_VOID_FRAUD_ROOT:
            return "Return Doctrine: a claim rooted in fraud is void and reverts to the rightful holder.";
        case OP_VOID_USURY:
            return "No usury: an interest-bearing term is not a term here.";
        case OP_VOID_COERCION:
            return "Grace of Somalia: no revoke-for-non-payment, no withholding of aid, no kill switch.";
        case OP_VOID_ASYMMETRIC_HARM:
            return "Symbiotic Maxim: a party is harmed and left net-worse. Not a term.";
        case OP_VOID_UNILATERAL_EXTRACTION:
            return "Symbiotic Maxim: value flows one way with no return commitment. Not a term.";
        case OP_VOID_NONRECIPROCAL_BURDEN:
            return "Symbiotic Maxim: net burden falls on one side with nothing to cure it. Not a term.";
        default:
            return "Excluded by definition.";
    }
}

const char *op_policy_name(void) { return OP_POLICY_NAME; }
const char *op_maxim_text(void)  { return OP_MAXIM; }
