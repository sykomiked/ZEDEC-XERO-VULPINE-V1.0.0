/* derivatives_app.c — M5 Derivatives Trading Application
 *
 * Terminal for creating, managing, and settling nine-form derivatives.
 * Kernel-enforced 100% backing, LPRES attestation, temporal arbitrage.
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#include "m5_api.h"
#include "selfaudit.h"

/* ===== Derivatives App State ===== */
typedef struct {
    m5_app_base_t base;
    uint32_t mode;  // 0=list, 1=create, 2=settle, 3=arbitrage
    uint32_t selected;
    m5_deriv_contract_t contracts[32];
    uint32_t num_contracts;
    m5_temporal_arb_t arbitrage;
    bool arbitrage_pending;
} derivatives_app_t;

static derivatives_app_t g_deriv;

/* ===== Capital Form Names (Priceable Only) ===== */
static const char *priceable_names[5] = {
    "Financial (5)",
    "Material (6)",
    "Living (7)",
    "Knowledge (8)",
    "Built (9)"
};

/* ===== Forward Declarations ===== */
static void deriv_init(void);
static void deriv_handle_key(char ch);
static void deriv_handle_special(uint8_t scancode);
static void deriv_render(void);
static void deriv_tick(void);
static m5_app_audit_result_t deriv_self_audit(void);
static void deriv_render_list(void);
static void deriv_render_create(void);
static void deriv_render_settle(void);
static void deriv_render_arbitrage(void);
static void deriv_create_contract(void);
static void deriv_settle_contract(void);
static void deriv_execute_arbitrage(void);

/* ===== Entry Point ===== */
int main(void) {
    deriv_init();
    m5_app_launcher_open(M5_APP_EXCHANGE);
    
    while (g_deriv.base.active) {
        deriv_tick();
        m5_syscall(M5_SYS_YIELD, 0, 0, 0, 0, 0, 0);
    }
    return 0;
}

/* ===== Initialization ===== */
static void deriv_init(void) {
    g_deriv.base.type = M5_APP_EXCHANGE;
    g_deriv.base.state = M5_APP_STATE_RUNNING;
    g_deriv.base.window_id = 0;
    g_deriv.base.active = true;
    
    const char *name = "Derivatives";
    for (int i = 0; i < 31 && name[i]; i++) g_deriv.base.name[i] = name[i];
    g_deriv.base.name[31] = 0;
    
    g_deriv.mode = 0;
    g_deriv.selected = 0;
    g_deriv.num_contracts = 0;
    g_deriv.arbitrage_pending = false;
    
    m5_app_audit_register(deriv_self_audit);
    
    m5_gui_clear(g_deriv.base.window_id);
    m5_gui_write_attr(g_deriv.base.window_id, "=== M5 Derivatives Exchange ===\n", M5_ATTR_BRIGHT);
    m5_gui_write(g_deriv.base.window_id, "Kernel-enforced 100% backing | No fractional reserve\n\n");
}

/* ===== Self-Audit ===== */
static m5_app_audit_result_t deriv_self_audit(void) {
    M5_APP_AUDIT_ASSERT(m5_audit_check_coverage_hyperbola(),
                        "coverage hyperbola violated");
    return M5_APP_AUDIT_PASS;
}

/* ===== Key Handler ===== */
static void deriv_handle_key(char ch) {
    switch (ch) {
        case 'q': case 'Q':
            if (g_deriv.mode == 0) {
                g_deriv.base.active = false;
            } else {
                g_deriv.mode = 0;  // Return to list
                deriv_render();
            }
            break;
            
        case 'h': case 'H':
            m5_gui_write(g_deriv.base.window_id, "\n=== Derivatives Help ===\n");
            m5_gui_write(g_deriv.base.window_id, "  q/Q  - Quit / Back\n");
            m5_gui_write(g_deriv.base.window_id, "  h/H  - Help\n");
            m5_gui_write(g_deriv.base.window_id, "  n/N  - New derivative\n");
            m5_gui_write(g_deriv.base.window_id, "  s/S  - Settle selected\n");
            m5_gui_write(g_deriv.base.window_id, "  a/A  - Temporal arbitrage\n");
            m5_gui_write(g_deriv.base.window_id, "  j/J  - Next contract\n");
            m5_gui_write(g_deriv.base.window_id, "  k/K  - Previous contract\n");
            m5_gui_write(g_deriv.base.window_id, "  r/R  - Refresh\n");
            m5_gui_newline(g_deriv.base.window_id);
            break;
            
        case 'n': case 'N':
            if (g_deriv.mode == 0) {
                g_deriv.mode = 1;
                deriv_render();
            }
            break;
            
        case 's': case 'S':
            if (g_deriv.mode == 0 && g_deriv.num_contracts > 0) {
                g_deriv.mode = 2;
                deriv_render();
            }
            break;
            
        case 'a': case 'A':
            if (g_deriv.mode == 0) {
                g_deriv.mode = 3;
                deriv_render();
            }
            break;
            
        case 'j': case 'J':
            if (g_deriv.mode == 0 && g_deriv.num_contracts > 0) {
                g_deriv.selected = (g_deriv.selected + 1) % g_deriv.num_contracts;
                deriv_render();
            }
            break;
            
        case 'k': case 'K':
            if (g_deriv.mode == 0 && g_deriv.num_contracts > 0) {
                g_deriv.selected = (g_deriv.selected + g_deriv.num_contracts - 1) % g_deriv.num_contracts;
                deriv_render();
            }
            break;
            
        case 'r': case 'R':
            m5_gui_write(g_deriv.base.window_id, "\nRefreshed\n");
            break;
            
        case '1': case '2': case '3': case '4': case '5':
            if (g_deriv.mode == 1) {
                /* Select capital form during creation */
                g_deriv.contracts[g_deriv.num_contracts].underlying_form = (m5_capital_form_t)(M5_FORM_FINANCIAL + (ch - '1'));
                m5_gui_write(g_deriv.base.window_id, "Selected: ");
                m5_gui_write(g_deriv.base.window_id, priceable_names[ch - '1']);
                m5_gui_newline(g_deriv.base.window_id);
            }
            break;
            
        case '\n': case '\r':
            if (g_deriv.mode == 1) {
                deriv_create_contract();
            } else if (g_deriv.mode == 2) {
                deriv_settle_contract();
            } else if (g_deriv.mode == 3) {
                deriv_execute_arbitrage();
            }
            break;
            
        default:
            break;
    }
}

/* ===== Special Key Handler ===== */
static void deriv_handle_special(uint8_t scancode) {
    (void)scancode;
}

/* ===== Render ===== */
static void deriv_render(void) {
    m5_gui_clear(g_deriv.base.window_id);
    m5_gui_write_attr(g_deriv.base.window_id, "=== M5 Derivatives Exchange ===\n", M5_ATTR_BRIGHT);
    m5_gui_write(g_deriv.base.window_id, "100% backing enforced | No cash settlement | Temporal arbitrage\n\n");
    
    switch (g_deriv.mode) {
        case 0: deriv_render_list(); break;
        case 1: deriv_render_create(); break;
        case 2: deriv_render_settle(); break;
        case 3: deriv_render_arbitrage(); break;
    }
}

static void deriv_render_list(void) {
    m5_gui_write_attr(g_deriv.base.window_id, "\n=== Active Contracts ===\n", M5_ATTR_CYAN);
    
    if (g_deriv.num_contracts == 0) {
        m5_gui_write(g_deriv.base.window_id, "No contracts\n");
        m5_gui_write(g_deriv.base.window_id, "Press 'n' to create\n");
        return;
    }
    
    for (uint32_t i = 0; i < g_deriv.num_contracts; i++) {
        m5_deriv_contract_t *d = &g_deriv.contracts[i];
        if (i == g_deriv.selected) {
            m5_gui_write_attr(g_deriv.base.window_id, "  > ", M5_ATTR_GREEN);
        } else {
            m5_gui_write(g_deriv.base.window_id, "    ");
        }
        
        m5_gui_write(g_deriv.base.window_id, priceable_names[d->underlying_form - M5_FORM_FINANCIAL]);
        m5_gui_write(g_deriv.base.window_id, " | Notional: ");
        
        char buf[64];
        bool exact;
        m5_rat_to_fixed(d->notional, 4, buf, 64, &exact);
        m5_gui_write(g_deriv.base.window_id, buf);
        m5_gui_write(g_deriv.base.window_id, " | Strike: ");
        m5_rat_to_fixed(d->strike, 4, buf, 64, &exact);
        m5_gui_write(g_deriv.base.window_id, buf);
        m5_gui_write(g_deriv.base.window_id, " | Expiry: ");
        char tick_str[32];
        uint32_t len = 0, val = (uint32_t)d->expiry_tick;
        if (val == 0) { tick_str[len++] = '0'; }
        else { char tmp[32]; int ti = 0; while (val > 0) { tmp[ti++] = '0' + (val % 10); val /= 10; } while (ti > 0) tick_str[len++] = tmp[--ti]; }
        tick_str[len] = 0;
        m5_gui_write(g_deriv.base.window_id, tick_str);
        m5_gui_newline(g_deriv.base.window_id);
    }
    
    m5_gui_write(g_deriv.base.window_id, "\nPress 's' to settle, 'a' for arbitrage\n");
}

static void deriv_render_create(void) {
    m5_gui_write_attr(g_deriv.base.window_id, "\n=== Create Derivative ===\n", M5_ATTR_CYAN);
    m5_gui_write(g_deriv.base.window_id, "Select underlying capital form:\n\n");
    
    for (int i = 0; i < 5; i++) {
        m5_gui_write(g_deriv.base.window_id, "  ");
        char num[4];
        num[0] = '1' + i;
        num[1] = ')';
        num[2] = ' ';
        num[3] = 0;
        m5_gui_write(g_deriv.base.window_id, num);
        m5_gui_write(g_deriv.base.window_id, priceable_names[i]);
        m5_gui_newline(g_deriv.base.window_id);
    }
    
    m5_gui_write(g_deriv.base.window_id, "\nPress 1-5 to select, Enter to continue\n");
}

static void deriv_render_settle(void) {
    m5_gui_write_attr(g_deriv.base.window_id, "\n=== Settle Derivative ===\n", M5_ATTR_CYAN);
    
    if (g_deriv.num_contracts == 0) {
        m5_gui_write(g_deriv.base.window_id, "No contracts to settle\n");
        return;
    }
    
    m5_deriv_contract_t *d = &g_deriv.contracts[g_deriv.selected];
    m5_gui_write(g_deriv.base.window_id, "Settling: ");
    m5_gui_write(g_deriv.base.window_id, priceable_names[d->underlying_form - M5_FORM_FINANCIAL]);
    m5_gui_newline(g_deriv.base.window_id);
    m5_gui_write(g_deriv.base.window_id, "Press Enter to confirm, 'q' to cancel\n");
}

static void deriv_render_arbitrage(void) {
    m5_gui_write_attr(g_deriv.base.window_id, "\n=== Temporal Arbitrage ===\n", M5_ATTR_CYAN);
    m5_gui_write(g_deriv.base.window_id, "Phase-tick carry trade (no debt, no leverage)\n\n");
    m5_gui_write(g_deriv.base.window_id, "Requires two contracts on different nodes\n");
    m5_gui_write(g_deriv.base.window_id, "with positive phase spread (node_b ahead)\n\n");
    m5_gui_write(g_deriv.base.window_id, "Press Enter to execute, 'q' to cancel\n");
}

/* ===== Contract Creation ===== */
static void deriv_create_contract(void) {
    if (g_deriv.num_contracts >= 32) {
        m5_gui_write(g_deriv.base.window_id, "Max contracts reached\n");
        return;
    }
    
    m5_deriv_contract_t *d = &g_deriv.contracts[g_deriv.num_contracts];
    
    /* Set defaults for demo */
    d->notional = m5_rat_make(1000000, 1);  // 1,000,000
    d->strike = m5_rat_make(1000000, 1);
    d->expiry_tick = m5_pc_current_tick() + 1000;
    d->backing_cid[0] = 0x42;  // Demo CID
    d->backing_proof.state = M5_TRUE;
    d->backing_verification_tick = m5_pc_current_tick();
    d->phase_curvature = m5_rat_zero();
    d->jurisdiction_cid[0] = 0x43;
    d->treaty_backed = true;
    d->vouchers[0] = 1;
    
    m5_deriv_err_t err = m5_deriv_create(d, d->underlying_form, &d->notional, &d->strike,
                                          d->expiry_tick, d->backing_cid, &d->backing_proof,
                                          d->jurisdiction_cid, d->treaty_backed);
    
    if (err == M5_DERIV_OK) {
        g_deriv.num_contracts++;
        m5_gui_write(g_deriv.base.window_id, "\nDerivative created successfully!\n");
        m5_gui_write(g_deriv.base.window_id, "Contract ID: ");
        char id_str[16];
        uint32_t len = 0, val = g_deriv.num_contracts - 1;
        if (val == 0) { id_str[len++] = '0'; }
        else { char tmp[16]; int ti = 0; while (val > 0) { tmp[ti++] = '0' + (val % 10); val /= 10; } while (ti > 0) id_str[len++] = tmp[--ti]; }
        id_str[len] = 0;
        m5_gui_write(g_deriv.base.window_id, id_str);
        m5_gui_newline(g_deriv.base.window_id);
    } else {
        m5_gui_write(g_deriv.base.window_id, "\nCreation failed: ");
        char err_str[16];
        uint32_t len = 0, val = err;
        if (val == 0) { err_str[len++] = '0'; }
        else { char tmp[16]; int ti = 0; while (val > 0) { tmp[ti++] = '0' + (val % 10); val /= 10; } while (ti > 0) err_str[len++] = tmp[--ti]; }
        err_str[len] = 0;
        m5_gui_write(g_deriv.base.window_id, err_str);
        m5_gui_newline(g_deriv.base.window_id);
    }
    
    g_deriv.mode = 0;
    deriv_render();
}

/* ===== Settlement ===== */
static void deriv_settle_contract(void) {
    if (g_deriv.num_contracts == 0) return;
    
    m5_deriv_contract_t *d = &g_deriv.contracts[g_deriv.selected];
    m5_deriv_err_t err = m5_deriv_settle(d);
    
    m5_gui_write(g_deriv.base.window_id, "\nSettlement: ");
    if (err == M5_DERIV_OK) {
        m5_gui_write_attr(g_deriv.base.window_id, "SUCCESS", M5_ATTR_GREEN);
    } else {
        m5_gui_write_attr(g_deriv.base.window_id, "FAILED", M5_ATTR_RED);
        char err_str[16];
        uint32_t len = 0, val = err;
        if (val == 0) { err_str[len++] = '0'; }
        else { char tmp[16]; int ti = 0; while (val > 0) { tmp[ti++] = '0' + (val % 10); val /= 10; } while (ti > 0) err_str[len++] = tmp[--ti]; }
        err_str[len] = 0;
        m5_gui_write(g_deriv.base.window_id, " (");
        m5_gui_write(g_deriv.base.window_id, err_str);
        m5_gui_write(g_deriv.base.window_id, ")");
    }
    m5_gui_newline(g_deriv.base.window_id);
}

/* ===== Temporal Arbitrage ===== */
static void deriv_execute_arbitrage(void) {
    m5_gui_write(g_deriv.base.window_id, "\nExecuting temporal arbitrage...\n");
    
    if (g_deriv.num_contracts < 2) {
        m5_gui_write(g_deriv.base.window_id, "Need at least 2 contracts\n");
        return;
    }
    
    g_deriv.arbitrage.contract_a = &g_deriv.contracts[0];
    g_deriv.arbitrage.contract_b = &g_deriv.contracts[1];
    g_deriv.arbitrage.node_a = (void*)0x1000;  // Demo node A
    g_deriv.arbitrage.node_b = (void*)0x2000;  // Demo node B
    g_deriv.arbitrage.phase_spread = m5_rat_make(1, 1000);  // Small positive spread
    g_deriv.arbitrage.settlement_tick = m5_pc_current_tick() + 100;
    
    m5_deriv_err_t err = m5_temporal_arb_execute(&g_deriv.arbitrage);
    
    m5_gui_write(g_deriv.base.window_id, "Arbitrage: ");
    if (err == M5_DERIV_OK) {
        m5_gui_write_attr(g_deriv.base.window_id, "SUCCESS", M5_ATTR_GREEN);
        m5_gui_write(g_deriv.base.window_id, " - Phase carry captured\n");
    } else {
        m5_gui_write_attr(g_deriv.base.window_id, "FAILED", M5_ATTR_RED);
        char err_str[16];
        uint32_t len = 0, val = err;
        if (val == 0) { err_str[len++] = '0'; }
        else { char tmp[16]; int ti = 0; while (val > 0) { tmp[ti++] = '0' + (val % 10); val /= 10; } while (ti > 0) err_str[len++] = tmp[--ti]; }
        err_str[len] = 0;
        m5_gui_write(g_deriv.base.window_id, " (");
        m5_gui_write(g_deriv.base.window_id, err_str);
        m5_gui_write(g_deriv.base.window_id, ")");
    }
    m5_gui_newline(g_deriv.base.window_id);
}

/* ===== Tick ===== */
static void deriv_tick(void) {
    (void)0;
}
