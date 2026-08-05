/* triple_ledger.c — Nine-Capital Triple-Ledger System implementation
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#include "triple_ledger.h"

static const char *cap_names[] = {
    "Financial", "Physical", "Land", "Human",
    "Social", "Intellectual", "Cultural", "Spiritual", "Ecological"
};

static const char *ledger_names[] = {
    "Financial", "Provenance", "Externality"
};

const char *capital_type_name(capital_type_t c) {
    if (c < CAP_MAX) return cap_names[c];
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
    for (i = 0; i < 64 && name[i]; i++)
        a->name[i] = name[i];
    a->name[i] = 0;
    for (i = 0; i < LEDGER_MAX; i++)
        a->balance[i] = SR_ZERO;
    return tl->num_accounts++;
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
    account_t *a = &tl->accounts[account_id];
    if (a->num_entries >= 256) return -1;
    
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
    for (i = 0; i < 64 && description && description[i]; i++)
        e->description[i] = description[i];
    e->description[i] = 0;
    
    /* Update balances */
    a->balance[ledger] = SR_ADD(a->balance[ledger], SR_SUB(debit, credit));
    a->conventional_balance = SR_ADD(a->conventional_balance, SR_SUB(debit, credit));
    
    /* Update coverage */
    a->coverage_ratio = SR_DIV(SR_MUL(r, ell),
                                SR_DIV(SR_FROM_INT(18), SR_FROM_INT(10)));
    a->externality_phase = phi;
    
    a->num_entries++;
    
    /* Update system totals */
    tl->total_assets = SR_ADD(tl->total_assets, debit);
    tl->total_liabilities = SR_ADD(tl->total_liabilities, credit);
    tl->total_equity = SR_SUB(tl->total_assets, tl->total_liabilities);
    
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
    (void)cap;
    (void)description;
    if (from_id >= tl->num_accounts || to_id >= tl->num_accounts) return -1;
    
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
    for (j = 0; j < 64 && purpose && purpose[j]; j++)
        v->purpose[j] = purpose[j];
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
                v->redeemed = true;
                /* Credit the redeeming account with merit value */
                triple_ledger_post(tl, account_id, LEDGER_FINANCIAL,
                                   v->merit_value, v->coverage_ratio, v->phase,
                                   v->merit_value, SR_ZERO,
                                   v->issuer_id, "Voucher redemption");
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
        total = SR_ADD(total, tl->accounts[i].coverage_ratio);
        total_ell = SR_ADD(total_ell, tl->accounts[i].externality_phase);
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
    report->balance_sheet_assets = tl->total_assets;
    report->balance_sheet_liabilities = tl->total_liabilities;
    report->balance_sheet_equity = tl->total_equity;
    report->income_statement_revenue = SR_ZERO;
    report->income_statement_expenses = SR_ZERO;
    report->net_income = SR_ZERO;
    
    /* Aggregate from accounts */
    uint32_t i;
    for (i = 0; i < tl->num_accounts; i++) {
        surplus_real_t bal = tl->accounts[i].conventional_balance;
        if (SR_CMP(bal, SR_ZERO) >= 0) {
            report->balance_sheet_assets = SR_ADD(report->balance_sheet_assets, bal);
        } else {
            report->balance_sheet_liabilities = SR_ADD(report->balance_sheet_liabilities, -bal);
        }
    }
    report->balance_sheet_equity = SR_SUB(report->balance_sheet_assets,
                                           report->balance_sheet_liabilities);
}
