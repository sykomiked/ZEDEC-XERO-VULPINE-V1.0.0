/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* ministry.c — the treasury organ.
 *
 * ANTI-CAPTURE (audited): this translation unit does NOT include crown.h and
 * names no sign/ISC/credential symbol anywhere. Grep it. The Ministry touches
 * money and issues NOTHING; it cannot revoke a right it was never handed the
 * pen to grant. Money here, papers elsewhere.
 */
#include "ministry.h"
#include "onepolicy.h"   /* compose the no-usury gate; DO NOT include crown.h */

/* The coverage floor the whole house agrees on: 1.8x, as 18/10. Kept identical
 * to the constant baked into triple_ledger so our pre-flight matches its verify. */
static surplus_real_t coverage_floor(void) {
    return SR_DIV(SR_FROM_INT(18), SR_FROM_INT(10));
}

/* The eleven. 11% of `amount`, computed the one true way (matches zcap_gratuity). */
static surplus_real_t eleven_percent(surplus_real_t amount) {
    return SR_DIV(SR_MUL(amount, SR_FROM_INT(11)), SR_FROM_INT(100));
}

void ministry_init(ministry_t *m, triple_ledger_t *ledger) {
    if (!m) return;
    m->ledger = ledger;
    for (uint32_t f = 0; f < ZCAP_FORM_COUNT; f++) {
        m->measured.bal[f] = SR_ZERO;
        for (uint32_t t = 0; t < ZCAP_FORM_COUNT; t++) {
            m->ratio[f][t] = SR_ONE;      /* documented default: 1:1 until supplied */
            m->ratio_set[f][t] = false;
        }
    }
}

ministry_result_t ministry_measure(ministry_t *m, zcap_form_t form,
                                   surplus_real_t amount) {
    if (!m) return MIN_NOT_MEASURABLE;
    if ((uint32_t)form >= ZCAP_FORM_COUNT) return MIN_NOT_MEASURABLE;
    /* Only the four Ministry forms are quantified. The Crown recognises its own
     * four forms; the Ministry flatly refuses to put a number on them. */
    if (zcap_who_owns_this(form) != ZCAP_AUTH_MINISTRY) return MIN_NOT_MEASURABLE;
    m->measured.bal[form] = SR_ADD(m->measured.bal[form], amount);
    return MIN_OK;
}

surplus_real_t ministry_tribute(surplus_real_t amount) {
    return eleven_percent(amount);   /* the mandatory eleven */
}

surplus_real_t ministry_gratuity(surplus_real_t amount) {
    /* Delegate to the canonical gratuity so there is exactly one definition of
     * "the voluntary eleven" in the whole platform. Returned SEPARATELY; the
     * caller decides whether to add it — it never leaves the principal. */
    return zcap_gratuity(amount);
}

void ministry_set_exchange_ratio(ministry_t *m, zcap_form_t from,
                                 zcap_form_t to, surplus_real_t ratio) {
    if (!m) return;
    if ((uint32_t)from >= ZCAP_FORM_COUNT || (uint32_t)to >= ZCAP_FORM_COUNT) return;
    m->ratio[from][to] = ratio;
    m->ratio_set[from][to] = true;
}

surplus_real_t ministry_exchange_ratio(const ministry_t *m, zcap_form_t from,
                                       zcap_form_t to) {
    if (!m) return SR_ZERO;
    if ((uint32_t)from >= ZCAP_FORM_COUNT || (uint32_t)to >= ZCAP_FORM_COUNT)
        return SR_ZERO;
    /* You cannot price the inalienable. No ratio into a Crown form, ever. */
    if (!zcap_is_priceable(from) || !zcap_is_priceable(to)) return SR_ZERO;
    return m->ratio[from][to];   /* set value, or the 1:1 default from init */
}

ministry_result_t ministry_settle(ministry_t *m, uint32_t from_acct,
                                  uint32_t to_acct, surplus_real_t amount,
                                  surplus_real_t interest) {
    if (!m || !m->ledger) return MIN_UNDERCOVERED;
    if (from_acct >= m->ledger->num_accounts ||
        to_acct   >= m->ledger->num_accounts) return MIN_UNDERCOVERED;

    /* THE MAXIM, checked FIRST and before a single byte is posted: interest is
     * usury and usury is not a term. We compose The One Policy rather than
     * hand-rolling the rule. */
    op_term_t term = {0};
    term.give_a = amount;
    term.give_b = amount;
    term.harm_a = SR_ZERO;
    term.harm_b = SR_ZERO;
    term.interest = interest;
    term.reciprocal = true;   /* a settlement is its own matched return */
    if (!op_symbiotic_ok(&term)) return MIN_VOID_MAXIM;

    /* Ministry attaches full attestation to the money it moves. */
    surplus_real_t ell = SR_ONE;
    surplus_real_t phi = SR_ZERO;

    /* FAIL-CLOSED coverage pre-flight: replicate exactly the derived
     * coverage_ratio the ledger would store — (r*ell)/floor — and require it to
     * clear the same 1.8x floor triple_ledger_verify_coverage compares against.
     * If it would be undercovered we refuse HERE, so the books never see a
     * half-done settlement. */
    surplus_real_t floor = coverage_floor();
    surplus_real_t projected_cov = SR_DIV(SR_MUL(amount, ell), floor);
    if (SR_CMP(projected_cov, floor) < 0) return MIN_UNDERCOVERED;

    /* Equity must not go negative. A balanced double entry preserves equity, so
     * a non-negative starting equity stays non-negative — but we gate on it
     * anyway, fail-closed, before posting. */
    if (SR_CMP(m->ledger->total_equity, SR_ZERO) < 0) return MIN_UNDERCOVERED;

    /* Post the balanced transfer through the treasury's double-entry engine.
     * We do NOT reimplement debits and credits — we compose the one that exists. */
    int32_t rc = triple_ledger_transfer(m->ledger, from_acct, to_acct,
                                        CAP_FINANCIAL, amount, ell, phi,
                                        "Ministry settlement");
    if (rc < 0) return MIN_UNDERCOVERED;

    /* Post-condition (the ledger's own verify), belt and suspenders. */
    if (!triple_ledger_verify_coverage(m->ledger, from_acct) ||
        !triple_ledger_verify_coverage(m->ledger, to_acct))
        return MIN_UNDERCOVERED;

    return MIN_OK;
}
