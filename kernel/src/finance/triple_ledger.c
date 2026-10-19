/* triple_ledger.c — Nine-Capital Triple-Ledger System implementation
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#include "triple_ledger.h"

/* Indexed by capital_type_t, which is the canonical zcap_form_t order
 * (zcap_forms.h). The old table named the slots in a fifth order
 * (Physical, Land, ... Ecological), so most names were for the wrong form. */
static const char *cap_names[CAP_MAX] = {"Financial", "Manufactured", "Intellectual",
                                         "Human",     "Social",       "Natural",
                                         "Cultural",  "Spiritual",    "System"};

static const char *ledger_names[] = {
    "Financial", "Provenance", "Externality"
};

const char *capital_type_name(capital_type_t c) {
    if ((uint32_t) c < CAP_MAX) return cap_names[c];
    return "Unknown";
}

const char *ledger_type_name(ledger_type_t l) {
    if (l < LEDGER_MAX) return ledger_names[l];
    return "Unknown";
}

void triple_ledger_init(triple_ledger_t *tl) {
    uint32_t i;
    for (i = 0; i < 512; i++) {
        tl->accounts[i].num_entries = 0;
        tl->accounts[i].num_vouchers = 0;
        tl->accounts[i].conventional_balance = SR_ZERO;
        int j;
        for (j = 0; j < LEDGER_MAX; j++)
            tl->accounts[i].balance[j] = SR_ZERO;
    }
    tl->num_accounts = 0;
    tl->next_entry_id = 1;
    tl->next_voucher_id = 1;
    tl->total_coverage = SR_ZERO;
    tl->total_externality = SR_ZERO;
    tl->system_health = SR_ONE;
    tl->total_assets = SR_ZERO;
    tl->total_liabilities = SR_ZERO;
    tl->total_equity = SR_ZERO;
}

uint32_t triple_ledger_create_account(triple_ledger_t *tl,
                                       uint32_t entity_id,
                                       capital_type_t cap,
                                       const char *name) {
    if (tl->num_accounts >= 512) return 0xFFFFFFFF;
    account_t *a = &tl->accounts[tl->num_accounts];
    a->account_id = tl->num_accounts;
    a->entity_id = entity_id;
    a->capital_type = cap;
    a->conventional_balance = SR_ZERO;
    a->coverage_ratio = SR_ZERO;
    a->externality_phase = SR_ZERO;
    a->num_entries = 0;
    a->num_vouchers = 0;
    int i;
    /* Leave room for the terminator: name[] is 64 bytes. */
    for (i = 0; i < 63 && name && name[i]; i++) a->name[i] = name[i];
    a->name[i] = 0;
    for (i = 0; i < LEDGER_MAX; i++)
        a->balance[i] = SR_ZERO;
    return tl->num_accounts++;
}

/* Overflow guards. On the kernel path surplus_real_t is Q32.32 in an int64,
 * and an edge-valued amount (found by fuzz/econ/fuzz_econ_triple.c) made the
 * balance and total sums signed-overflow (undefined behaviour) instead of
 * being refused. A posting that cannot be represented is now refused before
 * anything changes. The TEST_HOST build uses double and needs no guard. */
#ifdef TEST_HOST
static bool tl_add(surplus_real_t a, surplus_real_t b, surplus_real_t *r)
{
    *r = SR_ADD(a, b);
    return true;
}
static bool tl_sub(surplus_real_t a, surplus_real_t b, surplus_real_t *r)
{
    *r = SR_SUB(a, b);
    return true;
}
#else
static bool tl_add(surplus_real_t a, surplus_real_t b, surplus_real_t *r)
{
    return !__builtin_add_overflow(a, b, r);
}
static bool tl_sub(surplus_real_t a, surplus_real_t b, surplus_real_t *r)
{
    return !__builtin_sub_overflow(a, b, r);
}
#endif

/* a + b, clamped to the representable range instead of overflowing */
static surplus_real_t tl_add_sat(surplus_real_t a, surplus_real_t b)
{
    surplus_real_t r;
    if (tl_add(a, b, &r)) return r;
    return SR_CMP(b, SR_ZERO) > 0 ? surplus_real_t_max : -surplus_real_t_max;
}

/* Would a posting of (debit, credit) on `ledger` of account `a` fit, given
 * extra amounts already headed for the system totals (for a transfer, whose
 * other leg lands first)? */
static bool tl_post_fits(const triple_ledger_t *tl, const account_t *a, ledger_type_t ledger,
                         surplus_real_t debit, surplus_real_t credit, surplus_real_t pend_assets,
                         surplus_real_t pend_liab)
{
    surplus_real_t delta, t, assets, liab;
    if (!tl_sub(debit, credit, &delta)) return false;
    if (!tl_add(a->balance[ledger], delta, &t)) return false;
    if (ledger != LEDGER_FINANCIAL) return true;
    if (!tl_add(a->conventional_balance, delta, &t)) return false;
    if (!tl_add(tl->total_assets, pend_assets, &assets) || !tl_add(assets, debit, &assets))
        return false;
    if (!tl_add(tl->total_liabilities, pend_liab, &liab) || !tl_add(liab, credit, &liab))
        return false;
    return tl_sub(assets, liab, &t);
}

int32_t triple_ledger_post(triple_ledger_t *tl,
                            uint32_t account_id,
                            ledger_type_t ledger,
                            surplus_real_t r,
                            surplus_real_t ell,
                            surplus_real_t phi,
                            surplus_real_t debit,
                            surplus_real_t credit,
                            uint32_t counterparty_id,
                            const char *description) {
    if (account_id >= tl->num_accounts) return -1;
    if ((uint32_t) ledger >= LEDGER_MAX) return -1;
    account_t *a = &tl->accounts[account_id];
    if (a->num_entries >= 256) return -1;
    if (!tl_post_fits(tl, a, ledger, debit, credit, SR_ZERO, SR_ZERO)) return -1;

    ledger_entry_t *e = &a->entries[a->num_entries];
    e->id = tl->next_entry_id++;
    e->timestamp = 0; /* Caller should set */
    e->capital_type = a->capital_type;
    e->ledger = ledger;
    e->omega = a->num_entries;
    e->r = r;
    e->ell = ell;
    e->phi = phi;
    e->chi = 0;
    e->debit = debit;
    e->credit = credit;
    e->counterparty_id = counterparty_id;
    e->custodian_id = 0;
    e->coverage_verified = (SR_CMP(SR_MUL(r, ell),
        SR_DIV(SR_FROM_INT(18), SR_FROM_INT(10))) >= 0);
    
    int i;
    for (i = 0; i < 63 && description && description[i]; i++) e->description[i] = description[i];
    e->description[i] = 0;

    /* Update balances. Only the financial ledger is conventional money: the
     * provenance and externality ledgers carry attestation and phase, which
     * must not leak into the conventional balance or the system totals. */
    a->balance[ledger] = SR_ADD(a->balance[ledger], SR_SUB(debit, credit));
    if (ledger == LEDGER_FINANCIAL)
        a->conventional_balance = SR_ADD(a->conventional_balance, SR_SUB(debit, credit));

    /* Update coverage */
    a->coverage_ratio = SR_DIV(SR_MUL(r, ell),
                                SR_DIV(SR_FROM_INT(18), SR_FROM_INT(10)));
    a->externality_phase = phi;
    
    a->num_entries++;

    /* Update system totals (financial ledger only, see above) */
    if (ledger == LEDGER_FINANCIAL) {
        tl->total_assets = SR_ADD(tl->total_assets, debit);
        tl->total_liabilities = SR_ADD(tl->total_liabilities, credit);
        tl->total_equity = SR_SUB(tl->total_assets, tl->total_liabilities);
    }

    return 0;
}

int32_t triple_ledger_transfer(triple_ledger_t *tl,
                                uint32_t from_id,
                                uint32_t to_id,
                                capital_type_t cap,
                                surplus_real_t amount,
                                surplus_real_t ell,
                                surplus_real_t phi,
                                const char *description) {
    (void) cap;
    if (from_id >= tl->num_accounts || to_id >= tl->num_accounts) return -1;
    if (from_id == to_id) return -1;
    /* A transfer moves a positive amount: a negative one would pull money
     * out of the receiver. (Balances may still go negative; callers such as
     * ministry and finance_markets rely on that and gate funding themselves.) */
    if (SR_CMP(amount, SR_ZERO) <= 0) return -1;
    /* All six legs or none: check capacity before the first post. */
    if (tl->accounts[from_id].num_entries + 3u > 256u ||
        tl->accounts[to_id].num_entries + 3u > 256u)
        return -1;
    /* ...and refuse up front any leg whose sums would overflow (the from
     * leg's credit reaches the totals before the to leg's debit). */
    const account_t *fa = &tl->accounts[from_id], *ta = &tl->accounts[to_id];
    if (!tl_post_fits(tl, fa, LEDGER_FINANCIAL, SR_ZERO, amount, SR_ZERO, SR_ZERO) ||
        !tl_post_fits(tl, ta, LEDGER_FINANCIAL, amount, SR_ZERO, SR_ZERO, amount) ||
        !tl_post_fits(tl, fa, LEDGER_PROVENANCE, SR_ZERO, amount, SR_ZERO, SR_ZERO) ||
        !tl_post_fits(tl, ta, LEDGER_PROVENANCE, amount, SR_ZERO, SR_ZERO, SR_ZERO) ||
        !tl_post_fits(tl, fa, LEDGER_EXTERNALITY, SR_ZERO, phi, SR_ZERO, SR_ZERO) ||
        !tl_post_fits(tl, ta, LEDGER_EXTERNALITY, phi, SR_ZERO, SR_ZERO, SR_ZERO))
        return -1;

    /* Credit from account */
    int32_t rc1 = triple_ledger_post(tl, from_id, LEDGER_FINANCIAL,
                                      amount, ell, phi, SR_ZERO, amount,
                                      to_id, description);
    if (rc1 < 0) return -1;
    
    /* Debit to account */
    int32_t rc2 = triple_ledger_post(tl, to_id, LEDGER_FINANCIAL,
                                      amount, ell, phi, amount, SR_ZERO,
                                      from_id, description);
    if (rc2 < 0) return -1;
    
    /* Also post to provenance ledger */
    triple_ledger_post(tl, from_id, LEDGER_PROVENANCE,
                       amount, ell, phi, SR_ZERO, amount, to_id, "Provenance: transfer out");
    triple_ledger_post(tl, to_id, LEDGER_PROVENANCE,
                       amount, ell, phi, amount, SR_ZERO, from_id, "Provenance: transfer in");
    
    /* Post to externality ledger */
    triple_ledger_post(tl, from_id, LEDGER_EXTERNALITY,
                       amount, ell, phi, SR_ZERO, phi, to_id, "Externality: phase loading");
    triple_ledger_post(tl, to_id, LEDGER_EXTERNALITY,
                       amount, ell, phi, phi, SR_ZERO, from_id, "Externality: phase received");
    
    return 0;
}

uint64_t triple_ledger_issue_voucher(triple_ledger_t *tl,
                                      uint32_t issuer_id,
                                      uint32_t holder_id,
                                      capital_type_t cap,
                                      surplus_real_t merit_value,
                                      surplus_real_t coverage_ratio,
                                      const char *purpose) {
    /* Find holder's account with matching capital type */
    uint32_t acct_id = 0xFFFFFFFF;
    uint32_t i;
    for (i = 0; i < tl->num_accounts; i++) {
        if (tl->accounts[i].entity_id == holder_id &&
            tl->accounts[i].capital_type == cap) {
            acct_id = i;
            break;
        }
    }
    if (acct_id == 0xFFFFFFFF) return 0;
    
    account_t *a = &tl->accounts[acct_id];
    if (a->num_vouchers >= 64) return 0;
    
    floating_voucher_t *v = &a->vouchers[a->num_vouchers];
    v->voucher_id = tl->next_voucher_id++;
    v->issuer_id = issuer_id;
    v->holder_id = holder_id;
    v->capital_type = cap;
    v->merit_value = merit_value;
    v->coverage_ratio = coverage_ratio;
    v->phase = SR_ZERO;
    v->issued_tick = 0;
    v->expires_tick = 0; /* Floating — no hard expiry */
    v->transferable = true;
    v->redeemed = false;
    
    int j;
    for (j = 0; j < 63 && purpose && purpose[j]; j++) v->purpose[j] = purpose[j];
    v->purpose[j] = 0;
    
    a->num_vouchers++;
    return v->voucher_id;
}

int32_t triple_ledger_transfer_voucher(triple_ledger_t *tl,
                                        uint64_t voucher_id,
                                        uint32_t new_holder_id) {
    uint32_t i, j;
    for (i = 0; i < tl->num_accounts; i++) {
        for (j = 0; j < tl->accounts[i].num_vouchers; j++) {
            if (tl->accounts[i].vouchers[j].voucher_id == voucher_id) {
                if (!tl->accounts[i].vouchers[j].transferable) return -1;
                if (tl->accounts[i].vouchers[j].redeemed) return -1;
                tl->accounts[i].vouchers[j].holder_id = new_holder_id;
                return 0;
            }
        }
    }
    return -1;
}

int32_t triple_ledger_redeem_voucher(triple_ledger_t *tl,
                                      uint64_t voucher_id,
                                      uint32_t account_id) {
    if (account_id >= tl->num_accounts) return -1;
    uint32_t i, j;
    for (i = 0; i < tl->num_accounts; i++) {
        for (j = 0; j < tl->accounts[i].num_vouchers; j++) {
            if (tl->accounts[i].vouchers[j].voucher_id == voucher_id) {
                floating_voucher_t *v = &tl->accounts[i].vouchers[j];
                if (v->redeemed) return -1;
                /* Only the current holder may redeem. */
                if (tl->accounts[account_id].entity_id != v->holder_id) return -1;
                /* Credit the redeeming account with merit value */
                if (triple_ledger_post(tl, account_id, LEDGER_FINANCIAL, v->merit_value,
                                       v->coverage_ratio, v->phase, v->merit_value, SR_ZERO,
                                       v->issuer_id, "Voucher redemption") < 0)
                    return -1;
                v->redeemed = true;
                return 0;
            }
        }
    }
    return -1;
}

bool triple_ledger_verify_coverage(const triple_ledger_t *tl,
                                    uint32_t account_id) {
    if (account_id >= tl->num_accounts) return false;
    const account_t *a = &tl->accounts[account_id];
    surplus_real_t floor = SR_DIV(SR_FROM_INT(18), SR_FROM_INT(10));
    return SR_CMP(a->coverage_ratio, floor) >= 0;
}

surplus_real_t triple_ledger_account_coverage(const triple_ledger_t *tl,
                                                uint32_t account_id) {
    if (account_id >= tl->num_accounts) return SR_ZERO;
    return tl->accounts[account_id].coverage_ratio;
}

void triple_ledger_update_health(triple_ledger_t *tl) {
    surplus_real_t total = SR_ZERO;
    surplus_real_t total_ell = SR_ZERO;
    uint32_t i;
    for (i = 0; i < tl->num_accounts; i++) {
        /* saturating: a sum of in-range ratios can exceed Q32.32 */
        total = tl_add_sat(total, tl->accounts[i].coverage_ratio);
        total_ell = tl_add_sat(total_ell, tl->accounts[i].externality_phase);
    }
    if (tl->num_accounts > 0) {
        tl->total_coverage = SR_DIV(total, SR_FROM_INT(tl->num_accounts));
        tl->total_externality = SR_DIV(total_ell, SR_FROM_INT(tl->num_accounts));
        tl->system_health = tl->total_coverage;
    }
}

void triple_ledger_export_conventional(const triple_ledger_t *tl,
                                        conventional_report_t *report) {
    report->total_debits = tl->total_assets;
    report->total_credits = tl->total_liabilities;
    report->trial_balance = SR_SUB(tl->total_assets, tl->total_liabilities);
    /* The balance sheet is aggregated from account balances below; starting
     * from the debit/credit totals as well would count every posting twice. */
    report->balance_sheet_assets = SR_ZERO;
    report->balance_sheet_liabilities = SR_ZERO;
    report->balance_sheet_equity = SR_ZERO;
    report->income_statement_revenue = SR_ZERO;
    report->income_statement_expenses = SR_ZERO;
    report->net_income = SR_ZERO;
    
    /* Aggregate from accounts */
    uint32_t i;
    for (i = 0; i < tl->num_accounts; i++) {
        surplus_real_t bal = tl->accounts[i].conventional_balance;
        /* Sums of many in-range balances can still exceed Q32.32: the
         * report saturates rather than overflow (found by fuzz_econ_triple). */
        if (SR_CMP(bal, SR_ZERO) >= 0) {
            report->balance_sheet_assets = tl_add_sat(report->balance_sheet_assets, bal);
        } else {
            surplus_real_t mag;
            if (!tl_sub(SR_ZERO, bal, &mag)) mag = surplus_real_t_max;
            report->balance_sheet_liabilities = tl_add_sat(report->balance_sheet_liabilities, mag);
        }
    }
    report->balance_sheet_equity =
        tl_add_sat(report->balance_sheet_assets, -report->balance_sheet_liabilities);
}
