/* kernel_main_arm32.c — ARM32 (AArch32) kernel entry point for ZEDEC XERO pqOS.
 *
 * Rewritten 2026-08-08 to use the CURRENT subsystem APIs (the previous version
 * called an init API — m5_kernel_init/isf_init/*_device_init/desktop_init/… — that
 * no longer exists). This mirrors kernel_main_riscv.c's proven init sequence, with
 * the arm32 arch wrappers (PL011 UART + WFI event loop). Core bring-up profile.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3
 */
#include "arm32_arch.h"

/* Arch UART (kernel/arch/arm32/uart_pl011.c) */
extern void uart_init(void);
extern void uart_puts(const char *s);
extern void uart_dec(uint32_t val);

extern void license_print_all(void);

/* Current M5 + platform subsystem APIs (same set kernel_main_riscv.c links). */
#include "../src/oseq/oseq_core.h"
#include "../src/rmag/rmag_core.h"
#include "../src/lpres/lpres_core.h"
#include "../src/iphase/iphase_core.h"
#include "../src/choice/choice_core.h"
#include "../src/phase_coord/phase_coordinator.h"
#include "../src/predictive/predictive_model.h"
#include "../src/finance/triple_ledger.h"
#include "../src/finance/financial.h"
#include "../src/finance/rails.h"
#include "../src/finance/crypto_bridge.h"
#include "../src/identity/identity.h"
#include "../src/quantum/quantum_device.h"
#include "../src/hardware/rtl_device.h"
#include "../src/net/jdr_piratenet.h"
#include "../src/vino/vino.h"
#include "../src/vena/vena.h"
#include "../src/holographic/holo.h"

static void boot_msg(const char *m) { uart_puts(m); uart_puts("\n"); }

void kernel_main_arm32(void) {
    uart_init();
    uart_puts("\nZEDEC pqOS — M5 Axiomatic Kernel (VOVINA SHAKINA) [ARM32]\n");
    uart_puts("==================================================\n\n");
    uart_puts("License: SEL-3.3 — Streisand Engine License\n");
    uart_puts("Author: H.M. Michael-Laurence: Curzi (c)\n");
    uart_puts("36N9 Genetics, LLC\n\n");
    license_print_all();

    /* M5 core */
    boot_msg("[BOOT] M5 Kernel subsystems...");
    phase_tick_t tick;
    phase_coordinator_init(&tick, EXEC_DC);
    rmag_init(256);
    lpres_init();
    iphase_init();
    choice_handoff();
    oseq_state_t oseq;
    oseq_init(&oseq);
    oseq_register_device(&oseq, "core");
    boot_msg("  [OK] phase/rmag/lpres/iphase/choice/oseq");

    for (int i = 0; i < 5; i++) phase_coordinator_tick(&tick);

    /* Predictive + situation */
    boot_msg("[BOOT] EDP risk + ISF + predictive model...");
    predictive_config_t pred_cfg; predictive_config_init(&pred_cfg);
    boot_msg("  [OK] EDP operators + Fibonacci algebra + ISF surplus");

    /* Nine-capital triple ledger */
    boot_msg("[BOOT] Nine-capital triple ledger...");
    static triple_ledger_t tl; triple_ledger_init(&tl);
    triple_ledger_create_account(&tl, 1, CAP_FINANCIAL,  "Genesis:Financial");
    triple_ledger_create_account(&tl, 1, CAP_HUMAN,      "Genesis:Human");
    triple_ledger_create_account(&tl, 1, CAP_ECOLOGICAL, "Genesis:Ecological");
    boot_msg("  [OK] 9 capital types");

    boot_msg("[BOOT] Financial instruments + payment rails + crypto bridge...");
    static portfolio_t portfolio;   portfolio_init(&portfolio);
    static rail_system_t rail_sys;  rail_system_init(&rail_sys);
    static bridge_registry_t br;    bridge_registry_init(&br);
    boot_msg("  [OK] instruments + 3 rails + bridge");

    boot_msg("[BOOT] Identity + quantum + RTL + JDR PirateNet...");
    static identity_registry_t id_reg; identity_registry_init(&id_reg);
    static quantum_system_t qsys;      quantum_system_init(&qsys);
    static rtl_registry_t rtl_reg;     rtl_registry_init(&rtl_reg);
    static jdr_network_t jdr_net;      jdr_network_init(&jdr_net);
    jdr_register_node(&jdr_net, 1);
    jdr_transceiver_create(&jdr_net, 145000000, 12500,
        JDR_BAND_VHF, JDR_MOD_FM, JDR_EXEC_AC, "JDR-ARM32-001");
    boot_msg("  [OK] identity/quantum/rtl/jdr");

    boot_msg("[BOOT] Vino bank + Vena runtime + holographic data...");
    static vino_ledger_t vino; vino_init(&vino, 1);
    vino_create_account(&vino, "ZEDEC:node:arm32:0001", "Genesis Account");
    vino_set_validator(&vino, true, 1000000);
    static vena_runtime_t vena; vena_init(&vena, &vino);
    vena_set_language(&vena, LANG_M5_AXIOMATIC);
    static holo_ctx_t holo; holo_init(&holo);
    boot_msg("  [OK] vino/vena/holo");

    boot_msg("\n[BOOT_OK] ZEDEC pqOS [ARM32] — core online. Entering event cycle.\n");

    /* Core bring-up event loop. We pace off the generic-timer virtual counter
     * (readable at PL1) rather than a timer IRQ — the GIC is not wired up in this
     * profile, so a bare `wfi` would sleep forever with no wakeup source. */
    uint32_t cycle = 0;
    uint32_t freq = arm32_cntfrq();
    uint32_t period = freq ? (freq / 100u) : 625000u;   /* ~100 Hz; fallback 62.5 MHz/100 */
    uint64_t last = arm32_cntvct();
    while (1) {
        uint64_t now;
        do { now = arm32_cntvct(); } while ((uint32_t)(now - last) < period);
        last += period;
        phase_coordinator_tick(&tick);
        if (cycle % 100 == 0) { uart_puts("tick: omega="); uart_dec(tick.omega); uart_puts("\n"); }
        cycle++;
    }
}
