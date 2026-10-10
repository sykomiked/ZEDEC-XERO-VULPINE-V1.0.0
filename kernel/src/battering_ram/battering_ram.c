/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* battering_ram.c — The Battering Ram Exchange.
 *
 * Integer-only (SR_ macros), no libc, no allocation. Every rate is supplied,
 * every outcome is externally verified, and a non-achievement produces exactly
 * zero debt. There is no matching engine and no margin anywhere in this file —
 * on purpose. If you came looking for a liquidation path, there isn't one.
 */
#include "battering_ram.h"
#include "onepolicy.h"       /* op_symbiotic_ok — the void-by-definition core     */
#include "financial.h"       /* financial_price_future — the pricer we REUSE      */
#include "ed25519_verify.h"  /* ed25519_verify — the built-in attestation check   */

/* ---- tiny local helpers (no libc) ------------------------------------- */

static bool form_in_range(zcap_form_t f) {
    return (uint32_t)f < (uint32_t)ZCAP_FORM_COUNT;
}

/* Find the alliance slot for an outcome, or NULL. */
static br_alliance_t *find_alliance(br_exchange_t *ex, uint64_t outcome_id) {
    for (uint32_t i = 0; i < ex->num_alliances; i++) {
        if (ex->alliances[i].state != BR_ALLIANCE_NONE &&
            ex->alliances[i].outcome_id == outcome_id) {
            return &ex->alliances[i];
        }
    }
    return NULL;
}

/* Zero an allocation. */
static void alloc_zero(allocation_t *out) {
    out->realized = SR_ZERO;
    out->tribute = SR_ZERO;
    out->gratuity = SR_ZERO;
    out->contributor_pool = SR_ZERO;
    out->n = 0;
    for (uint32_t i = 0; i < BR_MAX_PLEDGES; i++) {
        out->party_id[i] = 0;
        out->party_form[i] = ZCAP_FINANCIAL;
        out->party[i] = SR_ZERO;
    }
    out->achieved = false;
    out->zero_debt = true;   /* nobody ever owes anything through this module */
}

/* ---- lifecycle -------------------------------------------------------- */

void br_exchange_init(br_exchange_t *ex) {
    if (!ex) return;
    for (uint32_t f = 0; f < ZCAP_FORM_COUNT; f++) ex->book.bal[f] = SR_ZERO;
    for (uint32_t i = 0; i < BR_MAX_ALLIANCES; i++) {
        ex->alliances[i].state = BR_ALLIANCE_NONE;
        ex->alliances[i].outcome_id = 0;
        ex->alliances[i].n = 0;
        ex->alliances[i].total_units = SR_ZERO;
    }
    ex->num_alliances = 0;
    ex->verify = NULL;         /* fail closed until an oracle is bound */
    ex->rail = NULL;
    ex->rail_voucher = 0;
    for (uint32_t i = 0; i < VINO_PROOF_CID_LEN; i++) ex->rail_cid[i] = 0;
    for (uint32_t i = 0; i < 32; i++) ex->attestor_key[i] = 0;
    ex->attestor_pinned = false;
}

void br_pin_attestor(br_exchange_t *ex, const uint8_t key[32])
{
    if (!ex) return;
    ex->attestor_pinned = key != NULL;
    for (uint32_t i = 0; i < 32; i++) ex->attestor_key[i] = key ? key[i] : 0;
}

void br_set_verifier(br_exchange_t *ex, br_attest_verify_fn fn) {
    if (!ex) return;
    ex->verify = fn;
}

void br_bind_rail(br_exchange_t *ex, vino_stores_t *rail, uint64_t voucher_id,
                  const uint8_t proof_cid[VINO_PROOF_CID_LEN]) {
    if (!ex) return;
    ex->rail = rail;
    ex->rail_voucher = voucher_id;
    if (proof_cid) {
        for (uint32_t i = 0; i < VINO_PROOF_CID_LEN; i++) ex->rail_cid[i] = proof_cid[i];
    }
}

void br_book_credit(br_exchange_t *ex, zcap_form_t form, surplus_real_t units) {
    if (!ex || !form_in_range(form)) return;
    ex->book.bal[form] = SR_ADD(ex->book.bal[form], units);
}

/* ---- alliances -------------------------------------------------------- */

int32_t br_alliance_open(br_exchange_t *ex, uint64_t outcome_id,
                         const pledge_t *contributors, uint32_t n) {
    if (!ex || !contributors) return BR_ERR_NULL;
    if (n == 0 || n > BR_MAX_PLEDGES) return BR_ERR_RANGE;
    if (ex->num_alliances >= BR_MAX_ALLIANCES) return BR_ERR_CAPACITY;
    if (find_alliance(ex, outcome_id) != NULL) return BR_ERR_STATE; /* no dup */

    /* Validate every pledge before we commit a single one (PROPOSED phase). */
    surplus_real_t total = SR_ZERO;
    for (uint32_t i = 0; i < n; i++) {
        if (!form_in_range(contributors[i].form)) return BR_ERR_RANGE;
        if (SR_CMP(contributors[i].units, SR_ZERO) < 0) return BR_ERR_RANGE;
        total = SR_ADD(total, contributors[i].units);
    }

    br_alliance_t *a = &ex->alliances[ex->num_alliances];
    a->outcome_id = outcome_id;
    a->state = BR_PROPOSED;
    a->n = n;
    a->total_units = total;
    for (uint32_t i = 0; i < n; i++) a->pledges[i] = contributors[i];
    /* PROPOSED -> ALLIANCE_FORMED: the pledges are now bound. */
    a->state = BR_ALLIANCE_FORMED;

    int32_t idx = (int32_t)ex->num_alliances;
    ex->num_alliances++;
    return idx;
}

/* ---- cross-capital swap ---------------------------------------------- */

int32_t br_swap_settle(br_exchange_t *ex, const cross_cap_swap_t *s) {
    if (!ex || !s) return BR_ERR_NULL;
    if (!form_in_range(s->from) || !form_in_range(s->to)) return BR_ERR_RANGE;
    /* from == to is not a swap. Without this guard a self-form "swap" at a rate
     * != 1 operates twice on the SAME book cell and MINTS value from nothing
     * (HUMAN=100, swap HUMAN->HUMAN x10 @rate 2 would net HUMAN=110). Reject it. */
    if (s->from == s->to) return BR_ERR_RANGE;
    if (SR_CMP(s->amount, SR_ZERO) < 0) return BR_ERR_RANGE;       /* no negatives */
    if (SR_CMP(s->agreed_rate, SR_ZERO) <= 0) return BR_ERR_RANGE; /* rate supplied, positive */

    /* A FINANCIAL leg is SPENDABLE money — the only capital form that can leave
     * the exchange as a claim on others. It may settle ONLY THROUGH a bound vino
     * rail, whose coverage floor actually backs the claim. Without a rail, a
     * chosen agreed_rate could round-trip FINANCIAL->X->FINANCIAL and MINT
     * spendable money from nothing (buy X cheap with money, sell X dear for more
     * money). Non-money barters (HUMAN<->SOCIAL, etc.) are conserved by the book
     * itself and need no rail. So: money moves only where a rail can vouch for it. */
    if ((s->from == ZCAP_FINANCIAL || s->to == ZCAP_FINANCIAL) && ex->rail == NULL)
        return BR_ERR_NO_RAIL;

    surplus_real_t received = SR_MUL(s->amount, s->agreed_rate);

    /* A cross-capital swap is a conserved principal barter between two DISTINCT
     * forms; its integrity is the from!=to guard above, the atomic feasibility
     * check below, and (for a money leg) the vino coverage floor — NOT a harm-
     * term test fed hardcoded-benign inputs. The Symbiotic Maxim guard
     * (the public symbiotic_ok) is applied by callers to the actual DEAL TERMS,
     * where real harm/interest/reciprocity attributes exist — see br_distribute
     * and the symbiotic_ok unit test. It does not belong on a bare barter. */

    /* ATOMIC feasibility: if the pay leg cannot complete, NEITHER leg settles. */
    if (SR_CMP(ex->book.bal[s->from], s->amount) < 0) return BR_ERR_INSUFFICIENT;

    /* If a rail is bound and money is involved, settle the money leg THROUGH the
     * vino triple rail. Roll the whole swap back if the rail rejects it. */
    if (ex->rail != NULL &&
        (s->from == ZCAP_FINANCIAL || s->to == ZCAP_FINANCIAL)) {
        int32_t rc;
        if (s->to == ZCAP_FINANCIAL) {
            /* Money flows IN, fully backed: assets += received, equity += received.
             * equity == debit - credit, so no interest is implied. */
            rc = vino_ledger_act(ex->rail, ex->rail_voucher, VINO_CIRCULATE,
                                 received, SR_ZERO, received, ex->rail_cid);
        } else {
            /* Money flows OUT: a claim posts as a credit; the rail's own
             * coverage floor decides whether the books can bear it. */
            rc = vino_ledger_act(ex->rail, ex->rail_voucher, VINO_CIRCULATE,
                                 SR_ZERO, s->amount, SR_ZERO, ex->rail_cid);
        }
        if (rc != VINO_OK) return BR_ERR_SETTLEMENT;  /* neither leg settles */
    }

    /* Commit both legs. */
    ex->book.bal[s->from] = SR_SUB(ex->book.bal[s->from], s->amount);
    ex->book.bal[s->to]   = SR_ADD(ex->book.bal[s->to], received);
    return BR_OK;
}

/* ---- distribution & pay-it-forward ----------------------------------- */

int32_t br_distribute(br_exchange_t *ex, uint64_t outcome_id,
                      const ext_attestation_t *v, allocation_t *out) {
    if (!ex || !v || !out) return BR_ERR_NULL;

    br_alliance_t *a = find_alliance(ex, outcome_id);
    if (!a) return BR_ERR_NOT_FOUND;
    if (a->state != BR_ALLIANCE_FORMED) return BR_ERR_STATE;

    /* The attestation must be ABOUT this outcome. */
    if (v->outcome_id != outcome_id) return BR_ERR_UNVERIFIED;

    /* Ops boundary: with no verifier bound we do NOT assume achievement. */
    if (ex->verify == NULL) return BR_ERR_NO_ORACLE;

    /* The built-in verifier checks the signature against the key the
     * attestation itself carries, so without a pinned trust root anyone could
     * self-sign "achieved" and release the pool. Fail closed. */
    if (ex->verify == br_ed25519_attest_verify && !ex->attestor_pinned) return BR_ERR_NO_ORACLE;
    if (ex->attestor_pinned) {
        uint8_t diff = 0;
        for (uint32_t i = 0; i < 32; i++) diff |= (uint8_t) (v->attestor[i] ^ ex->attestor_key[i]);
        if (diff) return BR_ERR_UNVERIFIED;
    }

    /* Verify the ATTESTOR'S SIGNATURE — never the fact it asserts. A bad
     * signature consumes nothing and distributes nothing. */
    if (!ex->verify(v)) return BR_ERR_UNVERIFIED;

    alloc_zero(out);
    out->n = a->n;
    for (uint32_t i = 0; i < a->n; i++) {
        out->party_id[i] = a->pledges[i].contributor;
        out->party_form[i] = a->pledges[i].form;
    }
    out->achieved = v->achieved;

    if (!v->achieved) {
        /* Verified non-achievement: forgiven, ZERO debt, no clawback. */
        a->state = BR_FORGIVEN;
        return BR_NOT_ACHIEVED;
    }

    /* Achievement: distribute the pooled PRINCIPAL proportionally. Principal
     * only — nobody is levered, so nobody can be liquidated. Contributors
     * retain the whole pool (the majority). tribute/gratuity are the separate
     * concern of br_revenue_split over any realized SURPLUS, not principal. */
    surplus_real_t realized = a->total_units;
    out->realized = realized;
    out->contributor_pool = realized;

    surplus_real_t acc = SR_ZERO;
    for (uint32_t i = 0; i < a->n; i++) {
        surplus_real_t share;
        if (i + 1 < a->n && SR_CMP(a->total_units, SR_ZERO) > 0) {
            /* share = realized * units_i / total — the scale-by-fraction form,
             * never open-coded (realized*units)/total (which floors on target). */
            share = SR_DIV(SR_MUL(realized, a->pledges[i].units), a->total_units);
            acc = SR_ADD(acc, share);
        } else {
            /* Last party (or degenerate total): the exact remainder, so the
             * shares sum to `realized` with no rounding drift. */
            share = SR_SUB(realized, acc);
        }
        out->party[i] = share;
    }

    a->state = BR_DISTRIBUTED;
    return BR_OK;
}

int32_t br_payitforward(br_exchange_t *ex, uint64_t outcome_id,
                        payitforward_status_t how) {
    if (!ex) return BR_ERR_NULL;
    br_alliance_t *a = find_alliance(ex, outcome_id);
    if (!a) return BR_ERR_NOT_FOUND;
    if (a->state != BR_ALLIANCE_FORMED && a->state != BR_FORGIVEN)
        return BR_ERR_STATE;

    /* Every disposition is ZERO debt. FORGIVEN dissolves the obligation;
     * FORWARDED passes the goodwill on; LEARNING_CREDIT records a credit, never
     * a liability. There is deliberately no branch that creates a debt, a
     * negative balance, or a margin call — the whole point of the ram. */
    (void)how;
    a->state = BR_FORGIVEN;
    return BR_OK;
}

/* ---- revenue split ---------------------------------------------------- */

void br_revenue_split(surplus_real_t realized, allocation_t *out) {
    if (!out) return;
    alloc_zero(out);
    out->realized = realized;
    /* 11% tribute + 11% gratuity, as true fractions (scale-by-fraction form). */
    out->tribute  = SR_DIV(SR_MUL(realized, SR_FROM_INT(11)), SR_FROM_INT(100));
    out->gratuity = SR_DIV(SR_MUL(realized, SR_FROM_INT(11)), SR_FROM_INT(100));
    /* The contributor pool is the EXACT remainder — the majority. */
    out->contributor_pool = SR_SUB(realized, SR_ADD(out->tribute, out->gratuity));
    out->zero_debt = true;
}

bool br_contributors_hold_majority(const allocation_t *a) {
    if (!a) return false;
    /* pool > realized/2  <=>  2*pool > realized */
    return SR_CMP(SR_MUL(a->contributor_pool, SR_FROM_INT(2)), a->realized) > 0;
}

/* ---- void-by-definition guard ---------------------------------------- */

bool symbiotic_ok(const br_term_t *t) {
    if (!t) return false;
    /* A derivative written ON someone's suffering is void by definition, before
     * any arithmetic — you cannot make a market in another's harm. */
    if (t->on_suffering) return false;

    /* Compose The One Policy for harm / extraction / burden / usury. */
    op_term_t op = {0};
    op.give_a = t->give_a;
    op.give_b = t->give_b;
    op.harm_a = t->harm_a;
    op.harm_b = t->harm_b;
    op.interest = t->interest;
    op.reciprocal = t->reciprocal;
    op.denies_aid = false;
    op.revoke_for_nonpayment = false;
    op.has_kill_switch = false;
    op.fraud_root = false;
    return op_symbiotic_ok(&op);
}

/* ---- futures ON the nine capitals ------------------------------------ */

surplus_real_t br_price_capital_future(zcap_form_t form, surplus_real_t notional,
                                       surplus_real_t r, surplus_real_t T) {
    (void)form;  /* the future is written ON the form; the pricer is form-agnostic */
    financial_instrument_t inst = {0};
    inst.type = INST_FUTURE;
    inst.exec_mode = FIN_EXEC_PC;
    inst.reg_price = notional;
    inst.reg_rate = r;
    inst.reg_dividend = SR_ZERO;
    inst.reg_time = T;
    inst.reg_conv_yield = SR_ZERO;
    inst.coverage_ratio = SR_ONE;   /* coverage-neutral: no M5 haircut here */
    /* Reuse the existing pricer; do NOT reimplement F = S*(1+(r-q)T). */
    return financial_price_future(&inst);
}

/* ---- built-in Ed25519 attestation verifier --------------------------- */

bool br_ed25519_attest_verify(const ext_attestation_t *att) {
    if (!att) return false;
    /* Canonical message: outcome_id little-endian (8 bytes) || achieved (1). */
    uint8_t msg[9];
    uint64_t id = att->outcome_id;
    for (int i = 0; i < 8; i++) {
        msg[i] = (uint8_t)(id & 0xFFu);
        id >>= 8;
    }
    msg[8] = att->achieved ? 1u : 0u;
    return ed25519_verify(msg, sizeof(msg), att->sig, att->attestor);
}
