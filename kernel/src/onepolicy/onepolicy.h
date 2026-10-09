/* onepolicy.h — The One Policy: the foundation everything else is built on.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 *
 * WHAT THIS IS
 * -----------
 * One policy, foundational to the whole platform, that anyone can build on top
 * of. It is not a wall of prose — it is a single DECIDABLE predicate. Every
 * exchange, contract term, allocation, and credential action in ZXV passes
 * through it, and it answers one question: is this term admissible, or is it —
 * by definition — not a term at all?
 *
 * THE SYMBIOTIC MAXIM (NS §11/1111, "the Grace of Somalia")
 *   "Any term that produces asymmetric harm, unilateral extraction, or
 *    non-reciprocal burden is, by definitional exclusion, not a term."
 *
 * So the platform does not "ban" bad deals — it does not RECOGNISE them as deals
 * in the first place. On top of that definitional core sit three concrete rules
 * the proposals make explicit, encoded here as hard gates:
 *   - NO USURY: an interest-bearing term is void (leverage is bounded elsewhere
 *     by the φ-Note, never by interest).
 *   - NO COERCION (the kill-switch-free mandate): no revoking a credential for
 *     non-payment, no withholding of aid, no central kill switch.
 *   - THE RETURN DOCTRINE: a claim whose root of title is fraud is void and
 *     reverts to the rightful holder.
 *
 * It is a pure predicate: freestanding, integer-only (surplus_real_t), no state,
 * no allocation, deterministic, and locally auditable — no central authority is
 * consulted, because the exclusion is definitional, not administrative.
 */
#ifndef ZXV_ONEPOLICY_H
#define ZXV_ONEPOLICY_H

#include <stdint.h>
#include <stdbool.h>
#include "surplus.h"

#define OP_POLICY_NAME  "The One Policy"
#define OP_GRACE_CLAUSE "NS \xC2\xA711/1111 \xE2\x80\x94 Grace of Somalia"
#define OP_MAXIM \
    "Any term that produces asymmetric harm, unilateral extraction, or " \
    "non-reciprocal burden is, by definitional exclusion, not a term."

/* The verdict. OP_VALID means admissible; anything else means the term is, by
 * the maxim, not a term — with the reason it was excluded. */
typedef enum {
    OP_VALID = 0,
    OP_VOID_FRAUD_ROOT,             /* Return Doctrine: root of title is fraud   */
    OP_VOID_USURY,                  /* interest-bearing term                      */
    OP_VOID_COERCION,               /* revoke-for-nonpayment / deny aid / kill sw */
    OP_VOID_ASYMMETRIC_HARM,        /* Maxim (1): a party harmed, left net-worse  */
    OP_VOID_UNILATERAL_EXTRACTION,  /* Maxim (2): value one way, no return        */
    OP_VOID_NONRECIPROCAL_BURDEN,   /* Maxim (3): net burden on one side, no match */
} op_verdict_t;

/* A term described in measurable form. give_* is value conveyed each way; harm_*
 * is harm the term imposes on each party (>= 0); `reciprocal` is true iff a
 * matched return commitment binds both sides. Amounts are surplus_real_t (double
 * on host, Q32.32 on target) — use the SR_ macros, never raw arithmetic. */
typedef struct {
    surplus_real_t give_a;   /* value party A conveys to B          */
    surplus_real_t give_b;   /* value party B conveys to A          */
    surplus_real_t harm_a;   /* harm imposed on A (>= 0)            */
    surplus_real_t harm_b;   /* harm imposed on B (>= 0)            */
    surplus_real_t interest; /* interest charged on the term (== 0) */
    bool reciprocal;              /* a matched return commitment binds both sides */
    bool denies_aid;              /* the term withholds humanitarian aid / grace  */
    bool revoke_for_nonpayment;   /* revokes a credential/right for non-payment   */
    bool has_kill_switch;         /* a party retains a unilateral kill switch     */
    bool fraud_root;              /* the claim's root of title is fraudulent      */
} op_term_t;

/* The One Policy. Returns OP_VALID, or the FIRST rule the term violates (checked
 * in a fixed order so the verdict is deterministic). */
op_verdict_t op_evaluate(const op_term_t *t);

/* Convenience: true iff op_evaluate(t) == OP_VALID. This is the predicate other
 * modules (battering_ram, zmarket, crown revocation) compose. */
bool op_symbiotic_ok(const op_term_t *t);

/* Just the Grace-of-Somalia gate: no coercion, no denial of aid, no kill switch. */
bool op_grace_of_somalia_ok(const op_term_t *t);

/* Return Doctrine: true iff this claim's fraudulent root voids it and it reverts
 * to the rightful holder. */
bool op_return_doctrine_reverts(const op_term_t *t);

const char *op_verdict_name(op_verdict_t v);
const char *op_verdict_reason(op_verdict_t v);
const char *op_policy_name(void);
const char *op_maxim_text(void);

#endif /* ZXV_ONEPOLICY_H */
