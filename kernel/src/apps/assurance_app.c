/* assurance_app.c — M5 Assurance (Pay-It-Forward) Application
 *
 * Manages proactive capital generation through prevention projects.
 * Inverts insurance: contributions flow forward into prevention that generates capital.
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#include "m5_api.h"
#include "selfaudit.h"

/* ===== Assurance App State ===== */
typedef struct {
    m5_app_base_t base;
    uint32_t mode;  // 0=list, 1=create, 2=verify, 3=forward
    uint32_t selected;
    m5_assurance_contract_t assurances[32];
    uint32_t num_assurances;
    m5_assurance_deriv_t derivatives[16];
    uint32_t num_derivatives;
} assurance_app_t;

static assurance_app_t g_assur;

/* ===== Prevention Domain Names ===== */
static const char *prevention_domains[8] = {
    "Pandemic Early Warning",
    "Watershed Restoration",
    "Language Preservation",
    "Governance Integrity",
    "Grid Hardening",
    "Developer Apprenticeship",
    "Open-Source Hardening",
    "Mesh Corridor Hardening"
};

static const char *target_forms[8] = {
    "Social (1)", "Natural (2)", "Heritage/Intellectual (3)",
    "Governance/Institutional (4)", "Material (6)", "Living (7)",
    "Knowledge (8)", "Built (9)"
};

static const char *generated_forms[8] = {
    "Living (7)", "Built (9)", "Knowledge (8)",
    "Social (1)", "Natural (2)", "Knowledge (8)",
    "Material (6)", "Natural (2)"
};

/* ===== Forward Declarations ===== */
static void assur_init(void);
static void assur_handle_key(char ch);
static void assur_handle_special(uint8_t scancode);
static void assur_render(void);
static void assur_tick(void);
static m5_app_audit_result_t assur_self_audit(void);
static void assur_render_list(void);
static void assur_render_create(void);
static void assur_render_verify(void);
static void assur_render_forward(void);
static void assur_create_contract(void);
static void assur_verify_generation(void);
static void assur_forward_capital(void);

/* ===== Entry Point ===== */
int main(void) {
    assur_init();
    m5_app_launcher_open(M5_APP_BANK);
    
    while (g_assur.base.active) {
        assur_tick();
        m5_syscall(M5_SYS_YIELD, 0, 0, 0, 0, 0, 0);
    }
    return 0;
}

/* ===== Initialization ===== */
static void assur_init(void) {
    g_assur.base.type = M5_APP_BANK;
    g_assur.base.state = M5_APP_STATE_RUNNING;
    g_assur.base.window_id = 0;
    g_assur.base.active = true;
    
    const char *name = "Assurance";
    for (int i = 0; i < 31 && name[i]; i++) g_assur.base.name[i] = name[i];
    g_assur.base.name[31] = 0;
    
    g_assur.mode = 0;
    g_assur.selected = 0;
    g_assur.num_assurances = 0;
    g_assur.num_derivatives = 0;
    
    m5_app_audit_register(assur_self_audit);
    
    m5_gui_clear(g_assur.base.window_id);
    m5_gui_write_attr(g_assur.base.window_id, "=== M5 Assurance (Pay-It-Forward) ===\n", M5_ATTR_BRIGHT);
    m5_gui_write(g_assur.base.window_id, "Proactive capital generation | Not insurance | Regenerative\n\n");
}

/* ===== Self-Audit ===== */
static m5_app_audit_result_t assur_self_audit(void) {
    M5_APP_AUDIT_ASSERT(m5_audit_check_coverage_hyperbola(),
                        "coverage hyperbola violated");
    return M5_APP_AUDIT_PASS;
}

/* ===== Key Handler ===== */
static void assur_handle_key(char ch) {
    switch (ch) {
        case 'q': case 'Q':
            if (g_assur.mode == 0) {
                g_assur.base.active = false;
            } else {
                g_assur.mode = 0;
                assur_render();
            }
            break;
            
        case 'h': case 'H':
            m5_gui_write(g_assur.base.window_id, "\n=== Assurance Help ===\n");
            m5_gui_write(g_assur.base.window_id, "  q/Q  - Quit / Back\n");
            m5_gui_write(g_assur.base.window_id, "  h/H  - Help\n");
            m5_gui_write(g_assur.base.window_id, "  n/N  - New assurance\n");
            m5_gui_write(g_assur.base.window_id, "  v/V  - Verify generation\n");
            m5_gui_write(g_assur.base.window_id, "  f/F  - Forward capital\n");
            m5_gui_write(g_assur.base.window_id, "  j/J  - Next assurance\n");
            m5_gui_write(g_assur.base.window_id, "  k/K  - Previous assurance\n");
            m5_gui_write(g_assur.base.window_id, "  r/R  - Refresh\n");
            m5_gui_newline(g_assur.base.window_id);
            break;
            
        case 'n': case 'N':
            if (g_assur.mode == 0) {
                g_assur.mode = 1;
                assur_render();
            }
            break;
            
        case 'v': case 'V':
            if (g_assur.mode == 0 && g_assur.num_assurances > 0) {
                g_assur.mode = 2;
                assur_render();
            }
            break;
            
        case 'f': case 'F':
            if (g_assur.mode == 0 && g_assur.num_assurances > 0) {
                g_assur.mode = 3;
                assur_render();
            }
            break;
            
        case 'j': case 'J':
            if (g_assur.mode == 0 && g_assur.num_assurances > 0) {
                g_assur.selected = (g_assur.selected + 1) % g_assur.num_assurances;
                assur_render();
            }
            break;
            
        case 'k': case 'K':
            if (g_assur.mode == 0 && g_assur.num_assurances > 0) {
                g_assur.selected = (g_assur.selected + g_assur.num_assurances - 1) % g_assur.num_assurances;
                assur_render();
            }
            break;
            
        case 'r': case 'R':
            m5_gui_write(g_assur.base.window_id, "\nRefreshed\n");
            break;
            
        case '1': case '2': case '3': case '4': case '5':
        case '6': case '7': case '8':
            if (g_assur.mode == 1) {
                g_assur.assurances[g_assur.num_assurances].target_form = (m5_capital_form_t)(ch - '1');
                m5_gui_write(g_assur.base.window_id, "Target: ");
                m5_gui_write(g_assur.base.window_id, target_forms[ch - '1']);
                m5_gui_newline(g_assur.base.window_id);
            }
            break;
            
        case '\n': case '\r':
            if (g_assur.mode == 1) {
                assur_create_contract();
            } else if (g_assur.mode == 2) {
                assur_verify_generation();
            } else if (g_assur.mode == 3) {
                assur_forward_capital();
            }
            break;
            
        default:
            break;
    }
}

/* ===== Special Key Handler ===== */
static void assur_handle_special(uint8_t scancode) {
    (void)scancode;
}

/* ===== Render ===== */
static void assur_render(void) {
    m5_gui_clear(g_assur.base.window_id);
    m5_gui_write_attr(g_assur.base.window_id, "=== M5 Assurance (Pay-It-Forward) ===\n", M5_ATTR_BRIGHT);
    m5_gui_write(g_assur.base.window_id, "Prevention -> Generation -> Forward | No premiums | No claims\n\n");
    
    switch (g_assur.mode) {
        case 0: assur_render_list(); break;
        case 1: assur_render_create(); break;
        case 2: assur_render_verify(); break;
        case 3: assur_render_forward(); break;
    }
}

static void assur_render_list(void) {
    m5_gui_write_attr(g_assur.base.window_id, "\n=== Active Assurances ===\n", M5_ATTR_CYAN);
    
    if (g_assur.num_assurances == 0) {
        m5_gui_write(g_assur.base.window_id, "No assurances\n");
        m5_gui_write(g_assur.base.window_id, "Press 'n' to create\n");
        return;
    }
    
    for (uint32_t i = 0; i < g_assur.num_assurances; i++) {
        m5_assurance_contract_t *a = &g_assur.assurances[i];
        if (i == g_assur.selected) {
            m5_gui_write_attr(g_assur.base.window_id, "  > ", M5_ATTR_GREEN);
        } else {
            m5_gui_write(g_assur.base.window_id, "    ");
        }
        
        m5_gui_write(g_assur.base.window_id, prevention_domains[a->target_form]);
        m5_gui_write(g_assur.base.window_id, " -> ");
        m5_gui_write(g_assur.base.window_id, generated_forms[a->generated_form]);
        
        char buf[64];
        bool exact;
        m5_rat_to_fixed(a->generation_ratio, 2, buf, 64, &exact);
        m5_gui_write(g_assur.base.window_id, " | Ratio: ");
        m5_gui_write(g_assur.base.window_id, buf);
        m5_gui_write(g_assur.base.window_id, " | ");
        m5_gui_write(g_assur.base.window_id, a->pay_it_forward ? "FORWARD" : "LOCAL");
        m5_gui_newline(g_assur.base.window_id);
    }
    
    m5_gui_write(g_assur.base.window_id, "\nPress 'v' to verify, 'f' to forward\n");
}

static void assur_render_create(void) {
    m5_gui_write_attr(g_assur.base.window_id, "\n=== Create Assurance ===\n", M5_ATTR_CYAN);
    m5_gui_write(g_assur.base.window_id, "Select prevention domain (target capital form):\n\n");
    
    for (int i = 0; i < 8; i++) {
        m5_gui_write(g_assur.base.window_id, "  ");
        char num[4];
        num[0] = '1' + i;
        num[1] = ')';
        num[2] = ' ';
        num[3] = 0;
        m5_gui_write(g_assur.base.window_id, num);
        m5_gui_write(g_assur.base.window_id, prevention_domains[i]);
        m5_gui_write(g_assur.base.window_id, " (");
        m5_gui_write(g_assur.base.window_id, target_forms[i]);
        m5_gui_write(g_assur.base.window_id, ")");
        m5_gui_newline(g_assur.base.window_id);
    }
    
    m5_gui_write(g_assur.base.window_id, "\nPress 1-8 to select target form\n");
}

static void assur_render_verify(void) {
    m5_gui_write_attr(g_assur.base.window_id, "\n=== Verify Generation ===\n", M5_ATTR_CYAN);
    
    if (g_assur.num_assurances == 0) {
        m5_gui_write(g_assur.base.window_id, "No assurances to verify\n");
        return;
    }
    
    m5_assurance_contract_t *a = &g_assur.assurances[g_assur.selected];
    m5_gui_write(g_assur.base.window_id, "Verifying: ");
    m5_gui_write(g_assur.base.window_id, prevention_domains[a->target_form]);
    m5_gui_write(g_assur.base.window_id, " -> ");
    m5_gui_write(g_assur.base.window_id, generated_forms[a->generated_form]);
    m5_gui_newline(g_assur.base.window_id);
    m5_gui_write(g_assur.base.window_id, "Press Enter to verify, 'q' to cancel\n");
}

static void assur_render_forward(void) {
    m5_gui_write_attr(g_assur.base.window_id, "\n=== Forward Capital ===\n", M5_ATTR_CYAN);
    
    if (g_assur.num_assurances == 0) {
        m5_gui_write(g_assur.base.window_id, "No assurances to forward\n");
        return;
    }
    
    m5_assurance_contract_t *a = &g_assur.assurances[g_assur.selected];
    m5_gui_write(g_assur.base.window_id, "Forwarding: ");
    m5_gui_write(g_assur.base.window_id, prevention_domains[a->target_form]);
    m5_gui_write(g_assur.base.window_id, " -> ");
    m5_gui_write(g_assur.base.window_id, generated_forms[a->generated_form]);
    m5_gui_newline(g_assur.base.window_id);
    m5_gui_write(g_assur.base.window_id, "Press Enter to forward, 'q' to cancel\n");
}

/* ===== Contract Creation ===== */
static void assur_create_contract(void) {
    if (g_assur.num_assurances >= 32) {
        m5_gui_write(g_assur.base.window_id, "Max assurances reached\n");
        return;
    }
    
    m5_assurance_contract_t *a = &g_assur.assurances[g_assur.num_assurances];
    
    /* Set defaults for demo */
    a->contribution = m5_rat_make(100000, 1);  // 100,000
    a->prevention_cid[0] = 0x50;  // Demo CID
    a->efficacy_proof.state = M5_TRUE;
    a->generated_form = (m5_capital_form_t)((a->target_form + 1) % 8 + 1);  // Transmute
    a->generation_ratio = m5_rat_make(150, 100);  // 1.5x generation
    a->generation_tick = 0;
    a->pay_it_forward = true;
    a->forward_cid[0] = 0x51;
    a->forward_phase = m5_rat_zero();
    
    m5_assur_err_t err = m5_assurance_create(a, &a->contribution, a->target_form,
                                              a->prevention_cid, &a->efficacy_proof,
                                              a->generated_form, &a->generation_ratio,
                                              a->pay_it_forward, a->forward_cid,
                                              &a->forward_phase);
    
    if (err == M5_ASSUR_OK) {
        g_assur.num_assurances++;
        m5_gui_write(g_assur.base.window_id, "\nAssurance created successfully!\n");
        m5_gui_write(g_assur.base.window_id, "Assurance ID: ");
        char id_str[16];
        uint32_t len = 0, val = g_assur.num_assurances - 1;
        if (val == 0) { id_str[len++] = '0'; }
        else { char tmp[16]; int ti = 0; while (val > 0) { tmp[ti++] = '0' + (val % 10); val /= 10; } while (ti > 0) id_str[len++] = tmp[--ti]; }
        id_str[len] = 0;
        m5_gui_write(g_assur.base.window_id, id_str);
        m5_gui_newline(g_assur.base.window_id);
    } else {
        m5_gui_write(g_assur.base.window_id, "\nCreation failed: ");
        char err_str[16];
        uint32_t len = 0, val = err;
        if (val == 0) { err_str[len++] = '0'; }
        else { char tmp[16]; int ti = 0; while (val > 0) { tmp[ti++] = '0' + (val % 10); val /= 10; } while (ti > 0) err_str[len++] = tmp[--ti]; }
        err_str[len] = 0;
        m5_gui_write(g_assur.base.window_id, err_str);
        m5_gui_newline(g_assur.base.window_id);
    }
    
    g_assur.mode = 0;
    assur_render();
}

/* ===== Generation Verification ===== */
static void assur_verify_generation(void) {
    if (g_assur.num_assurances == 0) return;
    
    m5_assurance_contract_t *a = &g_assur.assurances[g_assur.selected];
    m5_assur_err_t err = m5_assurance_verify_generation(a);
    
    m5_gui_write(g_assur.base.window_id, "\nVerification: ");
    if (err == M5_ASSUR_OK) {
        m5_gui_write_attr(g_assur.base.window_id, "GENERATION VERIFIED", M5_ATTR_GREEN);
        m5_gui_newline(g_assur.base.window_id);
        m5_gui_write(g_assur.base.window_id, "Capital generated and ");
        m5_gui_write(g_assur.base.window_id, a->pay_it_forward ? "forwarded" : "held locally");
        m5_gui_newline(g_assur.base.window_id);
    } else {
        m5_gui_write_attr(g_assur.base.window_id, "VERIFICATION FAILED", M5_ATTR_RED);
        char err_str[16];
        uint32_t len = 0, val = err;
        if (val == 0) { err_str[len++] = '0'; }
        else { char tmp[16]; int ti = 0; while (val > 0) { tmp[ti++] = '0' + (val % 10); val /= 10; } while (ti > 0) err_str[len++] = tmp[--ti]; }
        err_str[len] = 0;
        m5_gui_write(g_assur.base.window_id, " (");
        m5_gui_write(g_assur.base.window_id, err_str);
        m5_gui_write(g_assur.base.window_id, ")");
        m5_gui_newline(g_assur.base.window_id);
    }
}

/* ===== Forward Capital ===== */
static void assur_forward_capital(void) {
    if (g_assur.num_assurances == 0) return;
    
    m5_assurance_contract_t *a = &g_assur.assurances[g_assur.selected];
    
    if (!a->pay_it_forward) {
        m5_gui_write(g_assur.base.window_id, "\nThis assurance is not configured for pay-it-forward\n");
        return;
    }
    
    m5_assur_err_t err = m5_assurance_verify_generation(a);  // Re-verify then forward
    
    m5_gui_write(g_assur.base.window_id, "\nForwarding: ");
    if (err == M5_ASSUR_OK) {
        m5_gui_write_attr(g_assur.base.window_id, "FORWARDED", M5_ATTR_GREEN);
        m5_gui_newline(g_assur.base.window_id);
    } else {
        m5_gui_write_attr(g_assur.base.window_id, "FAILED", M5_ATTR_RED);
        m5_gui_newline(g_assur.base.window_id);
    }
}

/* ===== Tick ===== */
static void assur_tick(void) {
    (void)0;
}
