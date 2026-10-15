/* wallet_app.c — M5 Wallet Application
 *
 * Native wallet for managing nine-form capital, vouchers, and derivatives.
 * Demonstrates the complete M5 financial API.
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#include "m5_api.h"
#include "selfaudit.h"

/* ===== Wallet State ===== */
typedef struct {
    m5_app_base_t base;
    uint32_t selected_capital;
    uint32_t scroll_offset;
    m5_rat_t balances[M5_FORM_COUNT];
    m5_voucher_t vouchers[32];
    uint32_t num_vouchers;
    m5_deriv_contract_t derivatives[16];
    uint32_t num_derivatives;
    m5_assurance_contract_t assurances[16];
    uint32_t num_assurances;
} wallet_app_t;

static wallet_app_t g_wallet;

/* ===== Capital Form Names ===== */
/* Indexed by the canonical form index (zcap_forms.h): m5_capital_form_t and
 * m5_capital_type_t share those values. */
static const char *capital_names[M5_FORM_COUNT] = {
    [M5_FORM_FINANCIAL] = "Financial",
    [M5_FORM_MATERIAL] = "Material",
    [M5_FORM_KNOWLEDGE] = "Knowledge",
    [M5_FORM_LIVING] = "Living",
    [M5_FORM_SOCIAL] = "Social (State-Reserved)",
    [M5_FORM_NATURAL] = "Natural (State-Reserved)",
    [M5_FORM_HERITAGE_INTELLECTUAL] = "Heritage/Intellectual (State-Reserved)",
    [M5_FORM_GOVERNANCE_INSTITUTIONAL] = "Governance/Institutional (State-Reserved)",
    [M5_FORM_BUILT] = "Built"};

/* ===== Forward Declarations ===== */
static void wallet_init(void);
static void wallet_handle_key(char ch);
static void wallet_handle_special(uint8_t scancode);
static void wallet_render(void);
static void wallet_tick(void);
static m5_app_audit_result_t wallet_self_audit(void);
static void wallet_refresh_balances(void);
static void wallet_refresh_vouchers(void);
static void wallet_render_balances(void);
static void wallet_render_vouchers(void);
static void wallet_render_derivatives(void);
static void wallet_render_assurances(void);

/* ===== Entry Point ===== */
int main(void) {
    wallet_init();
    m5_app_launcher_open(M5_APP_WALLET);
    
    while (g_wallet.base.active) {
        wallet_tick();
        m5_syscall(M5_SYS_YIELD, 0, 0, 0, 0, 0, 0);
    }
    return 0;
}

/* ===== Initialization ===== */
static void wallet_init(void) {
    g_wallet.base.type = M5_APP_WALLET;
    g_wallet.base.state = M5_APP_STATE_RUNNING;
    g_wallet.base.window_id = 0;
    g_wallet.base.active = true;
    
    const char *name = "Wallet";
    for (int i = 0; i < 31 && name[i]; i++) g_wallet.base.name[i] = name[i];
    g_wallet.base.name[31] = 0;
    
    g_wallet.selected_capital = 0;
    g_wallet.scroll_offset = 0;
    g_wallet.num_vouchers = 0;
    g_wallet.num_derivatives = 0;
    g_wallet.num_assurances = 0;
    
    /* Register self-audit */
    m5_app_audit_register(wallet_self_audit);
    
    /* Initial data load */
    wallet_refresh_balances();
    wallet_refresh_vouchers();
    
    /* UI */
    m5_gui_clear(g_wallet.base.window_id);
    m5_gui_write_attr(g_wallet.base.window_id, "=== M5 Wallet ===\n", M5_ATTR_BRIGHT);
    m5_gui_write(g_wallet.base.window_id, "Managing nine-form capital on triple ledger\n\n");
}

/* ===== Self-Audit ===== */
static m5_app_audit_result_t wallet_self_audit(void) {
    M5_APP_AUDIT_ASSERT(m5_audit_check_rat_normalized(g_wallet.balances, M5_FORM_COUNT),
                        "wallet balances not normalized");
    M5_APP_AUDIT_ASSERT(m5_audit_check_coverage_hyperbola(),
                        "coverage hyperbola violated");
    M5_APP_AUDIT_ASSERT(m5_audit_check_voucher_single_active(),
                        "voucher double-spend possible");
    return M5_APP_AUDIT_PASS;
}

/* ===== Data Refresh ===== */
static void wallet_refresh_balances(void) {
    for (int i = 0; i < M5_FORM_COUNT; i++) {
        m5_rat_t balance = m5_rat_zero();
        /* Query ledger for each capital form */
        m5_ledger_entry_t entry;
        /* In real implementation, query ledger for account 0, capital form i */
        g_wallet.balances[i] = balance;
    }
}

static void wallet_refresh_vouchers(void) {
    /* Query ledger for vouchers */
    g_wallet.num_vouchers = 0;
}

/* ===== Key Handler ===== */
static void wallet_handle_key(char ch) {
    switch (ch) {
        case 'q': case 'Q':
            g_wallet.base.active = false;
            break;
            
        case 'h': case 'H':
            m5_gui_write(g_wallet.base.window_id, "\n=== Wallet Help ===\n");
            m5_gui_write(g_wallet.base.window_id, "  q/Q  - Quit\n");
            m5_gui_write(g_wallet.base.window_id, "  h/H  - Help\n");
            m5_gui_write(g_wallet.base.window_id, "  j/J  - Next capital form\n");
            m5_gui_write(g_wallet.base.window_id, "  k/K  - Previous capital form\n");
            m5_gui_write(g_wallet.base.window_id, "  v/V  - View vouchers\n");
            m5_gui_write(g_wallet.base.window_id, "  d/D  - View derivatives\n");
            m5_gui_write(g_wallet.base.window_id, "  a/A  - View assurances\n");
            m5_gui_write(g_wallet.base.window_id, "  r/R  - Refresh balances\n");
            m5_gui_write(g_wallet.base.window_id, "  n/N  - New derivative\n");
            m5_gui_write(g_wallet.base.window_id, "  s/S  - New assurance\n");
            m5_gui_newline(g_wallet.base.window_id);
            break;
            
        case 'j': case 'J':
            g_wallet.selected_capital = (g_wallet.selected_capital + 1) % M5_FORM_COUNT;
            wallet_render();
            break;
            
        case 'k': case 'K':
            g_wallet.selected_capital = (g_wallet.selected_capital + M5_FORM_COUNT - 1) % M5_FORM_COUNT;
            wallet_render();
            break;
            
        case 'v': case 'V':
            wallet_render_vouchers();
            break;
            
        case 'd': case 'D':
            wallet_render_derivatives();
            break;
            
        case 'a': case 'A':
            wallet_render_assurances();
            break;
            
        case 'r': case 'R':
            wallet_refresh_balances();
            wallet_refresh_vouchers();
            m5_gui_write(g_wallet.base.window_id, "\nBalances refreshed\n");
            break;
            
        case 'n': case 'N':
            /* Create new derivative */
            m5_gui_write(g_wallet.base.window_id, "\n=== New Derivative ===\n");
            m5_gui_write(g_wallet.base.window_id, "Feature: Create derivative contract\n");
            m5_gui_newline(g_wallet.base.window_id);
            break;
            
        case 's': case 'S':
            /* Create new assurance */
            m5_gui_write(g_wallet.base.window_id, "\n=== New Assurance ===\n");
            m5_gui_write(g_wallet.base.window_id, "Feature: Create Pay-It-Forward assurance\n");
            m5_gui_newline(g_wallet.base.window_id);
            break;
            
        default:
            break;
    }
}

/* ===== Special Key Handler ===== */
static void wallet_handle_special(uint8_t scancode) {
    (void)scancode;
}

/* ===== Render ===== */
static void wallet_render(void) {
    m5_gui_clear(g_wallet.base.window_id);
    m5_gui_write_attr(g_wallet.base.window_id, "=== M5 Wallet ===\n", M5_ATTR_BRIGHT);
    m5_gui_write(g_wallet.base.window_id, "Press 'h' for help\n\n");
    
    wallet_render_balances();
}

static void wallet_render_balances(void) {
    m5_gui_write_attr(g_wallet.base.window_id, "\n=== Capital Balances ===\n", M5_ATTR_CYAN);
    
    for (int i = 0; i < M5_FORM_COUNT; i++) {
        if (i == g_wallet.selected_capital) {
            m5_gui_write_attr(g_wallet.base.window_id, "  > ", M5_ATTR_GREEN);
        } else {
            m5_gui_write(g_wallet.base.window_id, "    ");
        }
        
        m5_gui_write(g_wallet.base.window_id, capital_names[i]);
        m5_gui_write(g_wallet.base.window_id, ": ");
        
        char buf[64];
        bool exact;
        m5_rat_to_fixed(g_wallet.balances[i], 4, buf, 64, &exact);
        m5_gui_write(g_wallet.base.window_id, buf);
        m5_gui_write(g_wallet.base.window_id, exact ? " (exact)" : " (approx)");
        m5_gui_newline(g_wallet.base.window_id);
    }
}

static void wallet_render_vouchers(void) {
    m5_gui_clear(g_wallet.base.window_id);
    m5_gui_write_attr(g_wallet.base.window_id, "=== Vouchers ===\n", M5_ATTR_CYAN);
    
    if (g_wallet.num_vouchers == 0) {
        m5_gui_write(g_wallet.base.window_id, "No vouchers held\n");
        m5_gui_write(g_wallet.base.window_id, "Press 'b' to return to balances\n");
        return;
    }
    
    for (uint32_t i = 0; i < g_wallet.num_vouchers; i++) {
        m5_voucher_t *v = &g_wallet.vouchers[i];
        m5_gui_write(g_wallet.base.window_id, "Voucher ");
        char id_str[16];
        uint32_t len = 0, val = v->voucher_id;
        if (val == 0) { id_str[len++] = '0'; }
        else { char tmp[16]; int ti = 0; while (val > 0) { tmp[ti++] = '0' + (val % 10); val /= 10; } while (ti > 0) id_str[len++] = tmp[--ti]; }
        id_str[len] = 0;
        m5_gui_write(g_wallet.base.window_id, id_str);
        m5_gui_write(g_wallet.base.window_id, " - ");
        m5_gui_write(g_wallet.base.window_id, capital_names[v->capital]);
        m5_gui_write(g_wallet.base.window_id, " - ");
        
        char merit[64];
        bool exact;
        m5_rat_to_fixed(v->merit_value, 4, merit, 64, &exact);
        m5_gui_write(g_wallet.base.window_id, merit);
        m5_gui_write(g_wallet.base.window_id, v->redeemed ? " (REDEEMED)" : " (ACTIVE)");
        m5_gui_newline(g_wallet.base.window_id);
    }
    
    m5_gui_write(g_wallet.base.window_id, "\nPress 'b' to return\n");
}

static void wallet_render_derivatives(void) {
    m5_gui_clear(g_wallet.base.window_id);
    m5_gui_write_attr(g_wallet.base.window_id, "=== Derivatives ===\n", M5_ATTR_CYAN);
    
    if (g_wallet.num_derivatives == 0) {
        m5_gui_write(g_wallet.base.window_id, "No derivatives\n");
        m5_gui_write(g_wallet.base.window_id, "Press 'n' to create new derivative\n");
        m5_gui_write(g_wallet.base.window_id, "Press 'b' to return\n");
        return;
    }
    
    for (uint32_t i = 0; i < g_wallet.num_derivatives; i++) {
        m5_deriv_contract_t *d = &g_wallet.derivatives[i];
        m5_gui_write(g_wallet.base.window_id, "Derivative ");
        char id_str[16];
        uint32_t len = 0, val = i;
        if (val == 0) { id_str[len++] = '0'; }
        else { char tmp[16]; int ti = 0; while (val > 0) { tmp[ti++] = '0' + (val % 10); val /= 10; } while (ti > 0) id_str[len++] = tmp[--ti]; }
        id_str[len] = 0;
        m5_gui_write(g_wallet.base.window_id, id_str);
        m5_gui_write(g_wallet.base.window_id, " - ");
        m5_gui_write(g_wallet.base.window_id, capital_names[d->underlying_form]);
        m5_gui_newline(g_wallet.base.window_id);
    }
    
    m5_gui_write(g_wallet.base.window_id, "\nPress 'b' to return\n");
}

static void wallet_render_assurances(void) {
    m5_gui_clear(g_wallet.base.window_id);
    m5_gui_write_attr(g_wallet.base.window_id, "=== Assurances (Pay-It-Forward) ===\n", M5_ATTR_CYAN);
    
    if (g_wallet.num_assurances == 0) {
        m5_gui_write(g_wallet.base.window_id, "No assurances\n");
        m5_gui_write(g_wallet.base.window_id, "Press 's' to create new assurance\n");
        m5_gui_write(g_wallet.base.window_id, "Press 'b' to return\n");
        return;
    }
    
    for (uint32_t i = 0; i < g_wallet.num_assurances; i++) {
        m5_assurance_contract_t *a = &g_wallet.assurances[i];
        m5_gui_write(g_wallet.base.window_id, "Assurance ");
        char id_str[16];
        uint32_t len = 0, val = i;
        if (val == 0) { id_str[len++] = '0'; }
        else { char tmp[16]; int ti = 0; while (val > 0) { tmp[ti++] = '0' + (val % 10); val /= 10; } while (ti > 0) id_str[len++] = tmp[--ti]; }
        id_str[len] = 0;
        m5_gui_write(g_wallet.base.window_id, id_str);
        m5_gui_write(g_wallet.base.window_id, " - Target: ");
        m5_gui_write(g_wallet.base.window_id, capital_names[a->target_form]);
        m5_gui_write(g_wallet.base.window_id, " -> Generates: ");
        m5_gui_write(g_wallet.base.window_id, capital_names[a->generated_form]);
        m5_gui_newline(g_wallet.base.window_id);
    }
    
    m5_gui_write(g_wallet.base.window_id, "\nPress 'b' to return\n");
}

/* ===== Tick ===== */
static void wallet_tick(void) {
    (void)0;
}
