/* kernel_main_arm32.c — ARM32 kernel entry point for ZEDEC XERO pqOS
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#include "arm32_arch.h"

extern void uart_init(void);
extern void uart_puts(const char *s);
extern void uart_hex(uint32_t val);
extern void uart_dec(uint32_t val);

/* License */
extern void license_print_all(void);

/* M5 subsystems (same as x86) */
extern void m5_kernel_init(void);
extern void phase_coordinator_init(void *tick, int profile);
extern void isf_init(void);
extern void edp_risk_init(void);
extern void predictive_model_init(void);
extern void situation_model_init(void);
extern void triple_ledger_init(void);
extern void financial_instruments_init(void);
extern void identity_system_init(void);
extern void payment_rails_init(void);
extern void crypto_bridge_init(void);
extern void quantum_device_init(void);
extern void rtl_device_init(void);
extern void jdr_piratenet_init(void);
extern void epu_device_init(void);
extern void synthesis_engine_init(void *engine, uint32_t id, const char *name);
extern void audio_init(void *dev, int type, const char *name);
extern void video_init(void *dev, const char *name);
extern void wifi_init(void *dev, const char *name);
extern void bt_init(void *dev, const char *name);
extern void browser_init(void *browser, const char *name);
extern void desktop_init(void *desk, uint32_t w, uint32_t h, uint32_t bpp);
extern void event_clock_init(void *clk, int mode);

void kernel_main_arm32(void) {
    uart_init();

    uart_puts("\n\n");
    uart_puts("========================================\n");
    uart_puts("  ZEDEC XERO pqOS v0.0.0\n");
    uart_puts("  ARM32 (AArch32) Edition\n");
    uart_puts("  36N9 Genetics, LLC\n");
    uart_puts("  Michael Laurence Curzi\n");
    uart_puts("========================================\n\n");

    /* Print all licenses */
    uart_puts("[BOOT] Printing licenses...\n");
    license_print_all();

    /* Initialize M5 kernel core */
    uart_puts("[BOOT] Initializing M5 kernel core...\n");
    m5_kernel_init();

    /* Event-driven clock (external sync only) */
    uart_puts("[BOOT] Initializing event-driven clock...\n");
    event_clock_init(0, 1); /* CLOCK_EXTERNAL_SYNC */

    /* M5 subsystems */
    uart_puts("[BOOT] Initializing ISF...\n");
    isf_init();
    uart_puts("[BOOT] Initializing EDP risk...\n");
    edp_risk_init();
    uart_puts("[BOOT] Initializing predictive model...\n");
    predictive_model_init();
    uart_puts("[BOOT] Initializing situation model...\n");
    situation_model_init();

    /* Financial */
    uart_puts("[BOOT] Initializing triple ledger...\n");
    triple_ledger_init();
    uart_puts("[BOOT] Initializing financial instruments...\n");
    financial_instruments_init();
    uart_puts("[BOOT] Initializing payment rails...\n");
    payment_rails_init();
    uart_puts("[BOOT] Initializing crypto bridge...\n");
    crypto_bridge_init();

    /* Identity */
    uart_puts("[BOOT] Initializing identity system...\n");
    identity_system_init();

    /* Quantum & hardware */
    uart_puts("[BOOT] Initializing quantum device...\n");
    quantum_device_init();
    uart_puts("[BOOT] Initializing RTL device...\n");
    rtl_device_init();

    /* Network */
    uart_puts("[BOOT] Initializing JDR PirateNet...\n");
    jdr_piratenet_init();

    /* EPU */
    uart_puts("[BOOT] Initializing EPU...\n");
    epu_device_init();

    /* Synthesis engine */
    uart_puts("[BOOT] Initializing synthesis engine...\n");
    synthesis_engine_init(0, 0, "ZEDEC-Synth");

    /* Drivers */
    uart_puts("[BOOT] Initializing audio driver...\n");
    audio_init(0, 1, "ARM32-Audio"); /* HDA */
    uart_puts("[BOOT] Initializing video driver...\n");
    video_init(0, "ARM32-Video");
    uart_puts("[BOOT] Initializing Wi-Fi driver...\n");
    wifi_init(0, "ARM32-WiFi");
    uart_puts("[BOOT] Initializing Bluetooth driver...\n");
    bt_init(0, "ARM32-Bluetooth");

    /* Desktop */
    uart_puts("[BOOT] Initializing desktop environment...\n");
    desktop_init(0, 1024, 768, 32);

    /* Browser */
    uart_puts("[BOOT] Initializing web browser...\n");
    browser_init(0, "ZEDEC-Browser");

    uart_puts("\n[BOOT] ZEDEC XERO pqOS v0.0.0 ARM32 boot complete!\n");
    uart_puts("[BOOT] All subsystems initialized.\n");
    uart_puts("[BOOT] Event-driven clock active (external sync mode).\n\n");

    /* Event loop */
    uart_puts("[KERNEL] Entering event loop...\n");
    uint32_t event_count = 0;
    while (1) {
        arm32_disable_irq();
        /* Wait for events (WFI = Wait For Interrupt) */
        __asm__ volatile ("wfi");
        arm32_enable_irq();
        event_count++;
        if (event_count % 1000 == 0) {
            uart_puts("[KERNEL] Event count: ");
            uart_dec(event_count);
            uart_puts("\n");
        }
    }
}
