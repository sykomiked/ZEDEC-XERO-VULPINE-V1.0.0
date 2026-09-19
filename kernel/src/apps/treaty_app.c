/* treaty_app.c — M5 Treaty Tokenization Application
 *
 * Tokenizes conservation easements and tangible assets as treaty-backed
 * Natural/Built capital tokens on Externality rail (999).
 * Never fractionalized. Backing ratio >= 1.0x enforced by kernel.
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#include "m5_api.h"
#include "selfaudit.h"

/* ===== Treaty App State ===== */
typedef struct {
    m5_app_base_t base;
    uint32_t mode;  // 0=list, 1=create, 2=verify
    uint32_t selected;
    m5_treaty_asset_t assets[32];
    uint32_t num_assets;
} treaty_app_t;

static treaty_app_t g_treaty;

/* ===== Asset Form Names ===== */
static const char *asset_forms[2] = {
    "Natural (2) - Conservation Easement",
    "Built (9) - Infrastructure"
};

/* ===== Forward Declarations ===== */
static void treaty_init(void);
static void treaty_handle_key(char ch);
static void treaty_handle_special(uint8_t scancode);
static void treaty_render(void);
static void treaty_tick(void);
static m5_app_audit_result_t treaty_self_audit(void);
static void treaty_render_list(void);
static void treaty_render_create(void);
static void treaty_render_verify(void);
static void treaty_create_asset(void);
static void treaty_verify_asset(void);

/* ===== Entry Point ===== */
int main(void) {
    treaty_init();
    m5_app_launcher_open(M5_APP_CUSTOM);
    
    while (g_treaty.base.active) {
        treaty_tick();
        m5_syscall(M5_SYS_YIELD, 0, 0, 0, 0, 0, 0);
    }
    return 0;
}

/* ===== Initialization ===== */
static void treaty_init(void) {
    g_treaty.base.type = M5_APP_CUSTOM;
    g_treaty.base.state = M5_APP_STATE_RUNNING;
    g_treaty.base.window_id = 0;
    g_treaty.base.active = true;
    
    const char *name = "Treaty Tokenization";
    for (int i = 0; i < 31 && name[i]; i++) g_treaty.base.name[i] = name[i];
    g_treaty.base.name[31] = 0;
    
    g_treaty.mode = 0;
    g_treaty.selected = 0;
    g_treaty.num_assets = 0;
    
    m5_app_audit_register(treaty_self_audit);
    
    m5_gui_clear(g_treaty.base.window_id);
    m5_gui_write_attr(g_treaty.base.window_id, "=== M5 Treaty Tokenization ===\n", M5_ATTR_BRIGHT);
    m5_gui_write(g_treaty.base.window_id, "Conservation easements -> treaty-backed Natural capital tokens\n");
    m5_gui_write(g_treaty.base.window_id, "Never fractionalized | Backing ratio >= 1.0x | Rail 999\n\n");
}

/* ===== Self-Audit ===== */
static m5_app_audit_result_t treaty_self_audit(void) {
    M5_APP_AUDIT_ASSERT(m5_audit_check_coverage_hyperbola(),
                        "coverage hyperbola violated");
    return M5_APP_AUDIT_PASS;
}

/* ===== Key Handler ===== */
static void treaty_handle_key(char ch) {
    switch (ch) {
        case 'q': case 'Q':
            if (g_treaty.mode == 0) {
                g_treaty.base.active = false;
            } else {
                g_treaty.mode = 0;
                treaty_render();
            }
            break;
            
        case 'h': case 'H':
            m5_gui_write(g_treaty.base.window_id, "\n=== Treaty Tokenization Help ===\n");
            m5_gui_write(g_treaty.base.window_id, "  q/Q  - Quit / Back\n");
            m5_gui_write(g_treaty.base.window_id, "  h/H  - Help\n");
            m5_gui_write(g_treaty.base.window_id, "  n/N  - New treaty asset\n");
            m5_gui_write(g_treaty.base.window_id, "  v/V  - Verify asset\n");
            m5_gui_write(g_treaty.base.window_id, "  j/J  - Next asset\n");
            m5_gui_write(g_treaty.base.window_id, "  k/K  - Previous asset\n");
            m5_gui_write(g_treaty.base.window_id, "  r/R  - Refresh\n");
            m5_gui_newline(g_treaty.base.window_id);
            break;
            
        case 'n': case 'N':
            if (g_treaty.mode == 0) {
                g_treaty.mode = 1;
                treaty_render();
            }
            break;
            
        case 'v': case 'V':
            if (g_treaty.mode == 0 && g_treaty.num_assets > 0) {
                g_treaty.mode = 2;
                treaty_render();
            }
            break;
            
        case 'j': case 'J':
            if (g_treaty.mode == 0 && g_treaty.num_assets > 0) {
                g_treaty.selected = (g_treaty.selected + 1) % g_treaty.num_assets;
                treaty_render();
            }
            break;
            
        case 'k': case 'K':
            if (g_treaty.mode == 0 && g_treaty.num_assets > 0) {
                g_treaty.selected = (g_treaty.selected + g_treaty.num_assets - 1) % g_treaty.num_assets;
                treaty_render();
            }
            break;
            
        case 'r': case 'R':
            m5_gui_write(g_treaty.base.window_id, "\nRefreshed\n");
            break;
            
        case '1': case '2':
            if (g_treaty.mode == 1) {
                g_treaty.assets[g_treaty.num_assets].form = (m5_capital_form_t)(M5_FORM_NATURAL + (ch - '1') * 7);
                m5_gui_write(g_treaty.base.window_id, "Form: ");
                m5_gui_write(g_treaty.base.window_id, asset_forms[ch - '1']);
                m5_gui_newline(g_treaty.base.window_id);
            }
            break;
            
        case '\n': case '\r':
            if (g_treaty.mode == 1) {
                treaty_create_asset();
            } else if (g_treaty.mode == 2) {
                treaty_verify_asset();
            }
            break;
            
        default:
            break;
    }
}

/* ===== Special Key Handler ===== */
static void treaty_handle_special(uint8_t scancode) {
    (void)scancode;
}

/* ===== Render ===== */
static void treaty_render(void) {
    m5_gui_clear(g_treaty.base.window_id);
    m5_gui_write_attr(g_treaty.base.window_id, "=== M5 Treaty Tokenization ===\n", M5_ATTR_BRIGHT);
    m5_gui_write(g_treaty.base.window_id, "Rail 999 (Externality) | Conservation easements -> Natural capital\n\n");
    
    switch (g_treaty.mode) {
        case 0: treaty_render_list(); break;
        case 1: treaty_render_create(); break;
        case 2: treaty_render_verify(); break;
    }
}

static void treaty_render_list(void) {
    m5_gui_write_attr(g_treaty.base.window_id, "\n=== Treaty-Backed Assets ===\n", M5_ATTR_CYAN);
    
    if (g_treaty.num_assets == 0) {
        m5_gui_write(g_treaty.base.window_id, "No assets tokenized\n");
        m5_gui_write(g_treaty.base.window_id, "Press 'n' to create\n");
        return;
    }
    
    for (uint32_t i = 0; i < g_treaty.num_assets; i++) {
        m5_treaty_asset_t *a = &g_treaty.assets[i];
        if (i == g_treaty.selected) {
            m5_gui_write_attr(g_treaty.base.window_id, "  > ", M5_ATTR_GREEN);
        } else {
            m5_gui_write(g_treaty.base.window_id, "    ");
        }
        
        m5_gui_write(g_treaty.base.window_id, asset_forms[a->form - M5_FORM_NATURAL]);
        m5_gui_write(g_treaty.base.window_id, " | Value: ");
        
        char buf[64];
        bool exact;
        m5_rat_to_fixed(a->quantified_value, 4, buf, 64, &exact);
        m5_gui_write(g_treaty.base.window_id, buf);
        m5_gui_write(g_treaty.base.window_id, " | Treaty: ");
        m5_gui_write(g_treaty.base.window_id, a->sovereignty_proof.state == M5_TRUE ? "RATIFIED" : "PENDING");
        m5_gui_newline(g_treaty.base.window_id);
    }
    
    m5_gui_write(g_treaty.base.window_id, "\nPress 'v' to verify\n");
}

static void treaty_render_create(void) {
    m5_gui_write_attr(g_treaty.base.window_id, "\n=== Create Treaty Asset ===\n", M5_ATTR_CYAN);
    m5_gui_write(g_treaty.base.window_id, "Select asset form:\n\n");
    
    for (int i = 0; i < 2; i++) {
        m5_gui_write(g_treaty.base.window_id, "  ");
        char num[4];
        num[0] = '1' + i;
        num[1] = ')';
        num[2] = ' ';
        num[3] = 0;
        m5_gui_write(g_treaty.base.window_id, num);
        m5_gui_write(g_treaty.base.window_id, asset_forms[i]);
        m5_gui_newline(g_treaty.base.window_id);
    }
    
    m5_gui_write(g_treaty.base.window_id, "\nPress 1-2 to select, Enter to continue\n");
}

static void treaty_render_verify(void) {
    m5_gui_write_attr(g_treaty.base.window_id, "\n=== Verify Treaty Asset ===\n", M5_ATTR_CYAN);
    
    if (g_treaty.num_assets == 0) {
        m5_gui_write(g_treaty.base.window_id, "No assets to verify\n");
        return;
    }
    
    m5_treaty_asset_t *a = &g_treaty.assets[g_treaty.selected];
    m5_gui_write(g_treaty.base.window_id, "Verifying: ");
    m5_gui_write(g_treaty.base.window_id, asset_forms[a->form - M5_FORM_NATURAL]);
    m5_gui_newline(g_treaty.base.window_id);
    m5_gui_write(g_treaty.base.window_id, "Press Enter to verify, 'q' to cancel\n");
}

/* ===== Asset Creation ===== */
static void treaty_create_asset(void) {
    if (g_treaty.num_assets >= 32) {
        m5_gui_write(g_treaty.base.window_id, "Max assets reached\n");
        return;
    }
    
    m5_treaty_asset_t *a = &g_treaty.assets[g_treaty.num_assets];
    
    /* Set defaults for demo */
    a->treaty_cid[0] = 0x54;  // Demo treaty CID
    a->asset_cid[0] = 0x41;   // Demo asset CID
    a->quantified_value = m5_rat_make(1000000, 1);  // 1,000,000
    a->sovereignty_proof.state = M5_TRUE;
    a->corridor_cid[0] = 0x43;
    a->tokenization_tick = m5_pc_current_tick();
    
    m5_treaty_err_t err = m5_treaty_asset_create(a, a->treaty_cid, a->asset_cid,
                                                  a->form, &a->quantified_value,
                                                  &a->sovereignty_proof,
                                                  a->corridor_cid);
    
    if (err == M5_TREATY_OK) {
        g_treaty.num_assets++;
        m5_gui_write(g_treaty.base.window_id, "\nTreaty asset created successfully!\n");
        m5_gui_write(g_treaty.base.window_id, "Asset ID: ");
        char id_str[16];
        uint32_t len = 0, val = g_treaty.num_assets - 1;
        if (val == 0) { id_str[len++] = '0'; }
        else { char tmp[16]; int ti = 0; while (val > 0) { tmp[ti++] = '0' + (val % 10); val /= 10; } while (ti > 0) id_str[len++] = tmp[--ti]; }
        id_str[len] = 0;
        m5_gui_write(g_treaty.base.window_id, id_str);
        m5_gui_newline(g_treaty.base.window_id);
    } else {
        m5_gui_write(g_treaty.base.window_id, "\nCreation failed: ");
        char err_str[16];
        uint32_t len = 0, val = err;
        if (val == 0) { err_str[len++] = '0'; }
        else { char tmp[16]; int ti = 0; while (val > 0) { tmp[ti++] = '0' + (val % 10); val /= 10; } while (ti > 0) err_str[len++] = tmp[--ti]; }
        err_str[len] = 0;
        m5_gui_write(g_treaty.base.window_id, err_str);
        m5_gui_newline(g_treaty.base.window_id);
    }
    
    g_treaty.mode = 0;
    treaty_render();
}

/* ===== Asset Verification ===== */
static void treaty_verify_asset(void) {
    if (g_treaty.num_assets == 0) return;
    
    m5_treaty_asset_t *a = &g_treaty.assets[g_treaty.selected];
    m5_treaty_err_t err = m5_treaty_asset_verify(a);
    
    m5_gui_write(g_treaty.base.window_id, "\nVerification: ");
    if (err == M5_TREATY_OK) {
        m5_gui_write_attr(g_treaty.base.window_id, "VALID", M5_ATTR_GREEN);
        m5_gui_newline(g_treaty.base.window_id);
        m5_gui_write(g_treaty.base.window_id, "Treaty-backed ");
        m5_gui_write(g_treaty.base.window_id, asset_forms[a->form - M5_FORM_NATURAL]);
        m5_gui_write(g_treaty.base.window_id, " token on Rail 999\n");
    } else {
        m5_gui_write_attr(g_treaty.base.window_id, "INVALID", M5_ATTR_RED);
        char err_str[16];
        uint32_t len = 0, val = err;
        if (val == 0) { err_str[len++] = '0'; }
        else { char tmp[16]; int ti = 0; while (val > 0) { tmp[ti++] = '0' + (val % 10); val /= 10; } while (ti > 0) err_str[len++] = tmp[--ti]; }
        err_str[len] = 0;
        m5_gui_write(g_treaty.base.window_id, " (");
        m5_gui_write(g_treaty.base.window_id, err_str);
        m5_gui_write(g_treaty.base.window_id, ")");
        m5_gui_newline(g_treaty.base.window_id);
    }
}

/* ===== Tick ===== */
static void treaty_tick(void) {
    (void)0;
}
