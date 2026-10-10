/* app_template.c — Minimal M5 Application Template
 *
 * Copy this file and modify for your application.
 * Build with: make -f Makefile.app APP=myapp
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#include "m5_api.h"

/* ===== Application Metadata ===== */
#define APP_NAME "myapp"
#define APP_VERSION "1.0.0"
#define APP_AUTHOR "Your Name"
#define APP_DESCRIPTION "A minimal M5 application"

/* ===== Application State ===== */
typedef struct {
    m5_app_base_t base;
    /* Add your app-specific state here */
    uint32_t counter;
    char buffer[256];
} myapp_t;

static myapp_t g_app;

/* ===== Forward Declarations ===== */
static void app_init(void);
static void app_handle_key(char ch);
static void app_handle_special(uint8_t scancode);
static void app_render(void);
static void app_tick(void);

/* ===== Entry Point ===== */
int main(void) {
    app_init();
    /* Wire these to your input and display path; referenced here so the
     * template builds warning-free (-Werror) before you do. */
    (void) app_handle_key;
    (void) app_handle_special;
    (void) app_render;

    /* Register with app launcher */
    m5_app_launcher_open(M5_APP_CUSTOM);
    
    /* Main event loop */
    while (g_app.base.active) {
        app_tick();
        
        /* Yield to scheduler */
        m5_syscall(M5_SYS_YIELD, 0, 0, 0, 0, 0, 0);
    }
    
    return 0;
}

/* ===== Initialization ===== */
static void app_init(void) {
    g_app.base.type = M5_APP_CUSTOM;
    g_app.base.state = M5_APP_STATE_RUNNING;
    g_app.base.window_id = 0;
    g_app.base.active = true;
    
    /* Copy app name */
    const char *name = APP_NAME;
    for (int i = 0; i < 31 && name[i]; i++) g_app.base.name[i] = name[i];
    g_app.base.name[31] = 0;
    
    g_app.counter = 0;
    g_app.buffer[0] = 0;
    
    /* Initialize window */
    m5_gui_clear(g_app.base.window_id);
    m5_gui_write_attr(g_app.base.window_id, "=== " APP_NAME " v" APP_VERSION " ===\n", M5_ATTR_BRIGHT);
    m5_gui_write(g_app.base.window_id, "Press 'q' to quit, 'h' for help\n\n");
}

/* ===== Key Handler ===== */
static void app_handle_key(char ch) {
    switch (ch) {
        case 'q':
        case 'Q':
            g_app.base.active = false;
            break;
            
        case 'h':
        case 'H':
            m5_gui_write(g_app.base.window_id, "\n=== Help ===\n");
            m5_gui_write(g_app.base.window_id, "  q/Q  - Quit application\n");
            m5_gui_write(g_app.base.window_id, "  h/H  - Show this help\n");
            m5_gui_write(g_app.base.window_id, "  c/C  - Increment counter\n");
            m5_gui_write(g_app.base.window_id, "  r/R  - Reset counter\n");
            m5_gui_write(g_app.base.window_id, "  l/L  - Show ledger info\n");
            m5_gui_write(g_app.base.window_id, "  v/V  - Show Vena contracts\n");
            m5_gui_newline(g_app.base.window_id);
            break;
            
        case 'c':
        case 'C':
            g_app.counter++;
            m5_gui_write(g_app.base.window_id, "Counter: ");
            char num[32];
            uint32_t len = 0;
            uint32_t val = g_app.counter;
            if (val == 0) { num[len++] = '0'; }
            else {
                char tmp[32]; int ti = 0;
                while (val > 0) { tmp[ti++] = '0' + (val % 10); val /= 10; }
                while (ti > 0) num[len++] = tmp[--ti];
            }
            num[len] = 0;
            m5_gui_write(g_app.base.window_id, num);
            m5_gui_newline(g_app.base.window_id);
            break;
            
        case 'r':
        case 'R':
            g_app.counter = 0;
            m5_gui_write(g_app.base.window_id, "Counter reset\n");
            break;
            
        case 'l':
        case 'L': {
            m5_gui_write(g_app.base.window_id, "\n=== Ledger Info ===\n");
            m5_rat_t coverage = m5_ledger_account_coverage(0);
            char cov_str[64];
            bool exact;
            m5_rat_to_fixed(coverage, 4, cov_str, 64, &exact);
            m5_gui_write(g_app.base.window_id, "Account 0 coverage: ");
            m5_gui_write(g_app.base.window_id, cov_str);
            m5_gui_write(g_app.base.window_id, exact ? " (exact)" : " (approx)");
            m5_gui_newline(g_app.base.window_id);
            break;
        }
            
        case 'v':
        case 'V': {
            m5_gui_write(g_app.base.window_id, "\n=== Vena Contracts ===\n");
            uint32_t ids[32];
            int32_t count = m5_vena_list_apps(ids, 32);
            if (count <= 0) {
                m5_gui_write(g_app.base.window_id, "No contracts loaded\n");
            } else {
                for (int i = 0; i < count; i++) {
                    m5_gui_write(g_app.base.window_id, "  Contract ");
                    char id_str[16];
                    uint32_t len = 0;
                    uint32_t val = ids[i];
                    if (val == 0) { id_str[len++] = '0'; }
                    else {
                        char tmp[16]; int ti = 0;
                        while (val > 0) { tmp[ti++] = '0' + (val % 10); val /= 10; }
                        while (ti > 0) id_str[len++] = tmp[--ti];
                    }
                    id_str[len] = 0;
                    m5_gui_write(g_app.base.window_id, id_str);
                    m5_gui_newline(g_app.base.window_id);
                }
            }
            break;
        }
            
        default:
            break;
    }
}

/* ===== Special Key Handler ===== */
static void app_handle_special(uint8_t scancode) {
    /* Handle arrow keys, function keys, etc. */
    (void)scancode;
}

/* ===== Render ===== */
static void app_render(void) {
    /* Called periodically to refresh display */
    (void)0;
}

/* ===== Tick ===== */
static void app_tick(void) {
    /* Periodic work */
    (void)0;
}
