/* abacus.c — Smaug's Abacus multilateral clearing. See abacus.h. */
#include "abacus.h"

void smaug_init(abacus_t *a) {
    if (!a) return;
    for (uint32_t i = 0; i < AB_MAX_MEMBERS; i++) {
        a->member[i].used = false;
        a->member[i].name[0] = '\0';
    }
    for (uint32_t i = 0; i < AB_MAX_OBLIGATIONS; i++) a->obl[i].active = false;
    a->num_members = 0;
    a->num_obl = 0;
    a->gross_before = rat_zero();
    a->gross_after = rat_zero();
    a->transfers_before = a->transfers_after = 0;
    a->cleared_runs = 0;
}

int32_t smaug_add_member(abacus_t *a, const char *name) {
    if (!a || a->num_members >= AB_MAX_MEMBERS) return -1;
    uint32_t i = a->num_members++;
    a->member[i].used = true;
    uint32_t k = 0;
    if (name) while (k < AB_NAME_LEN - 1 && name[k]) { a->member[i].name[k] = name[k]; k++; }
    a->member[i].name[k] = '\0';
    return (int32_t)i;
}

bool smaug_owe(abacus_t *a, uint32_t from, uint32_t to, rat_t amount,
            uint8_t capital) {
    if (!a || from == to) return false;
    if (from >= a->num_members || to >= a->num_members) return false;
    if (!amount.valid || amount.num < 0) return false;      /* no negative debts */
    if (rat_is_zero(amount)) return true;                   /* nothing to record */
    if (a->num_obl >= AB_MAX_OBLIGATIONS) return false;

    ab_obligation_t *o = &a->obl[a->num_obl++];
    o->from = from; o->to = to; o->amount = amount;
    o->active = true; o->capital = capital;
    return true;
}

rat_t smaug_net(const abacus_t *a, uint32_t member) {
    if (!a || member >= a->num_members) return rat_zero();
    rat_t net = rat_zero();
    for (uint32_t i = 0; i < a->num_obl; i++) {
        const ab_obligation_t *o = &a->obl[i];
        if (!o->active) continue;
        if (o->to == member)   net = rat_add(net, o->amount);   /* owed to them */
        if (o->from == member) net = rat_sub(net, o->amount);   /* they owe */
    }
    return net;
}

rat_t smaug_conservation(const abacus_t *a) {
    if (!a) return rat_zero();
    rat_t total = rat_zero();
    for (uint32_t m = 0; m < a->num_members; m++)
        total = rat_add(total, smaug_net(a, m));
    return total;
}

rat_t smaug_gross(const abacus_t *a) {
    if (!a) return rat_zero();
    rat_t g = rat_zero();
    for (uint32_t i = 0; i < a->num_obl; i++)
        if (a->obl[i].active) g = rat_add(g, a->obl[i].amount);
    return g;
}

uint32_t smaug_transfer_count(const abacus_t *a) {
    if (!a) return 0;
    uint32_t n = 0;
    for (uint32_t i = 0; i < a->num_obl; i++) if (a->obl[i].active) n++;
    return n;
}

/* Multilateral clearing.
 *
 * Method: compute every member's net position, then rebuild the obligation
 * set by repeatedly matching the largest debtor against the largest creditor
 * and transferring the smaller of the two magnitudes. That settles at least
 * one participant per step, so at most (members-1) transfers remain — the
 * minimum possible for a given vector of net positions.
 *
 * Because each step moves exactly min(|debt|, |credit|) and decrements both
 * sides by that amount, every member's net is reproduced exactly and the
 * conservation sum is preserved. All arithmetic is rational, so no minor
 * unit is lost. */
/* An explicit failure value. Clearing returns this when the book cannot be
 * cleared correctly, leaving the obligations exactly as they were. */
static rat_t invalid(void) { return rat_make(1, 0); }

rat_t smaug_clear(abacus_t *a) {
    if (!a || a->num_members == 0) return rat_zero();

    a->clear_failed = false;
    a->gross_before = smaug_gross(a);
    a->transfers_before = smaug_transfer_count(a);

    /* Build the replacement book in scratch and COMMIT ONLY ON SUCCESS.
     * The previous version wiped a->obl first and then netted, so any
     * arithmetic failure mid-run left the book destroyed — obligations
     * silently forgiven, creditor claims erased, and both documented
     * invariants still reporting success because they were measured
     * against the already-corrupted state. */
    ab_obligation_t out[AB_MAX_OBLIGATIONS];
    uint32_t n_out = 0;

    /* Distinct capitals present. Netting across capitals would offset a
     * debt in one unit against a credit in another, which is not a
     * clearing operation at all; each capital clears independently. */
    uint8_t caps[AB_MAX_OBLIGATIONS];
    uint32_t n_caps = 0;
    for (uint32_t i = 0; i < a->num_obl; i++) {
        if (!a->obl[i].active) continue;
        bool seen = false;
        for (uint32_t k = 0; k < n_caps; k++) if (caps[k] == a->obl[i].capital) seen = true;
        if (!seen) caps[n_caps++] = a->obl[i].capital;
    }

    for (uint32_t ci = 0; ci < n_caps; ci++) {
        uint8_t cap = caps[ci];

        /* net position per member, within THIS capital only */
        rat_t net[AB_MAX_MEMBERS];
        for (uint32_t m = 0; m < a->num_members; m++) net[m] = rat_zero();
        for (uint32_t i = 0; i < a->num_obl; i++) {
            const ab_obligation_t *o = &a->obl[i];
            if (!o->active || o->capital != cap) continue;
            if (o->from >= a->num_members || o->to >= a->num_members) { a->clear_failed = true; return invalid(); }
            net[o->from] = rat_sub(net[o->from], o->amount);
            net[o->to]   = rat_add(net[o->to],   o->amount);
        }

        /* Validate the WHOLE vector before acting on any of it. Skipping an
         * invalid entry mid-loop would drop that member's obligations while
         * keeping everyone else's — inventing and erasing debt at once. */
        for (uint32_t m = 0; m < a->num_members; m++)
            if (!net[m].valid) { a->clear_failed = true; return invalid(); }

        for (;;) {
            uint32_t d = 0, c = 0;
            bool have_d = false, have_c = false;
            rat_t worst = rat_zero(), best = rat_zero();

            for (uint32_t m = 0; m < a->num_members; m++) {
                if (net[m].num < 0 && (!have_d || rat_cmp(net[m], worst) < 0)) {
                    worst = net[m]; d = m; have_d = true;
                }
                if (net[m].num > 0 && (!have_c || rat_cmp(net[m], best) > 0)) {
                    best = net[m]; c = m; have_c = true;
                }
            }
            if (!have_d || !have_c) break;      /* this capital is settled */

            rat_t debt = rat_abs(worst);
            if (!debt.valid) { a->clear_failed = true; return invalid(); }
            rat_t amt = (rat_cmp(debt, best) < 0) ? debt : best;
            if (rat_is_zero(amt)) break;

            if (n_out >= AB_MAX_OBLIGATIONS) { a->clear_failed = true; return invalid(); }
            ab_obligation_t *o = &out[n_out++];
            o->from = d; o->to = c; o->amount = amt;
            o->active = true;
            o->capital = cap;                   /* preserve the denomination */

            rat_t nd = rat_add(net[d], amt);    /* debtor owes less */
            rat_t nc = rat_sub(net[c], amt);    /* creditor is owed less */
            /* An overflow here would poison the vector and quietly change
             * who owes what, so it aborts the run with the book intact. */
            if (!nd.valid || !nc.valid) { a->clear_failed = true; return invalid(); }
            net[d] = nd; net[c] = nc;
        }
    }

    /* every capital cleared without fault — now it is safe to commit */
    for (uint32_t i = 0; i < n_out; i++) a->obl[i] = out[i];
    for (uint32_t i = n_out; i < a->num_obl; i++) a->obl[i].active = false;
    a->num_obl = n_out;

    a->gross_after = smaug_gross(a);
    a->transfers_after = smaug_transfer_count(a);
    a->cleared_runs++;
    return rat_sub(a->gross_before, a->gross_after);
}
