/* kernel_main_riscv.c — RISC-V kernel boot driver for VOVINA SHAKINA
 *
 * Bare-metal entry point SHARED BY BOTH RISC-V WIDTHS. Entered from boot.s on
 * rv64 (lp64d) and boot_rv32.s on rv32 (ilp32d), after stack setup, BSS zeroing,
 * sstatus.FS enable, and stvec configuration. Nothing in this file may assume a
 * register width; use `unsigned long` for anything CSR- or pointer-shaped.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#include "m5_types.h"
#include "riscv_arch.h"

/* Supervisor paging bring-up (riscv_mmu.c) — enables Sv39 (rv64) / Sv32 (rv32)
 * and PROVIDES(mm_ready). Called early so the identity map is live before any
 * later subsystem, and long before the declaration gate reads satp back. */
extern void riscv_mmu_init(void);

/* UART driver */
extern void uart_init(void);
extern void uart_puts(const char *s);
extern void uart_put_hex(uint64_t val);
extern void uart_put_dec(uint64_t val);

/* PLIC driver */
extern void plic_init(void);
extern void plic_register_handler(uint32_t irq, void (*handler)(void));
extern void plic_handle_irq(void);

/* Timer driver */
extern void riscv_timer_init(void);
extern void riscv_timer_set_callback(void (*cb)(void));
extern void riscv_timer_handler(void);
extern uint64_t riscv_timer_get_ticks(void);

/* Firmware handoff, published by _start in boot.s / boot_rv32.s BEFORE any C
 * runs and AFTER the BSS clear. riscv_dtb_addr is the device tree OpenSBI gave
 * us in a1 — the machine describing itself, which is what a later board probe
 * must read instead of compiling in an address. */
unsigned long riscv_boot_hart;
unsigned long riscv_dtb_addr;

/* Fatal, unhandled synchronous exception.
 * WHY THIS EXISTS: sret returns to sepc, and sepc points AT the faulting
 * instruction. A handler that ignores an exception therefore re-executes it
 * forever — an infinite trap loop with zero output, which is what made every
 * RISC-V fault present as "it just stopped printing". Report and park instead:
 * a hang you can read is worth more than a hang you cannot. */
static void riscv_trap_fatal(unsigned long scause, unsigned long code) {
    uart_puts("\n[FAULT] unhandled S-mode exception\n");
    uart_puts("  scause = 0x"); uart_put_hex((uint64_t)scause);
    uart_puts("  (code "); uart_put_dec((uint64_t)code); uart_puts(")\n");
    uart_puts("  sepc   = 0x"); uart_put_hex((uint64_t)csr_read_sepc());   uart_puts("\n");
    uart_puts("  stval  = 0x"); uart_put_hex((uint64_t)csr_read_stval());  uart_puts("\n");
    uart_puts("  hart   = ");   uart_put_dec((uint64_t)riscv_boot_hart);   uart_puts("\n");
    uart_puts("[FAULT] halted.\n");
    disable_irq();
    for (;;) halt();
}

/* Trap handler (called from boot.s / boot_rv32.s with scause in a0).
 * Take the cause XLEN-wide: the interrupt flag is the top bit of the register
 * (bit 63 on rv64, bit 31 on rv32), so a hardcoded 0x8000...ULL mask would
 * never match on rv32. */
void riscv_trap_handler(unsigned long scause) {
    unsigned long int_bit = 1UL << (__riscv_xlen - 1);
    int is_interrupt = (scause & int_bit) != 0;
    unsigned long code = scause & ~int_bit;

    if (is_interrupt) {
        if (code == 5) {
            /* Supervisor timer interrupt (S-mode under OpenSBI) */
            riscv_timer_handler();
        } else if (code == 9) {
            /* Supervisor external interrupt (PLIC) */
            plic_handle_irq();
        }
        /* An interrupt we do not service is harmless to return from: the source
         * stays pending, it does not re-fault on an instruction. */
        return;
    }

    /* Synchronous exception. Nothing here recovers yet, so do not pretend to. */
    riscv_trap_fatal(scause, code);
}

/* M5 subsystems */
#include "../src/oseq/oseq_core.h"
#include "../src/rmag/rmag_core.h"
#include "../src/lpres/lpres_core.h"
#include "../src/iphase/iphase_core.h"
#include "../src/choice/choice_core.h"
#include "../src/phase_coord/phase_coordinator.h"
#include "../src/telemetry/telemetry_core.h"
#include "../src/axiom_matrix/axiom_matrix_core.h"
#include "../src/crit168/crit_168_word.h"
#include "../src/vfs/vfs.h"
#include "../src/net/net.h"
#include "../src/net/m5route.h"
#include "../src/vino/vino.h"
#include "../src/vena/vena.h"
#include "../src/holographic/holo.h"
#include "license.h"
#include "../src/surplus/surplus.h"
#include "../src/edp_risk/edp_risk.h"
#include "../src/predictive/predictive_model.h"
#include "../src/situation/situation_model.h"
#include "../src/finance/triple_ledger.h"
#include "../src/finance/financial.h"
#include "../src/finance/rails.h"
#include "../src/finance/crypto_bridge.h"
#include "../src/identity/identity.h"
#include "../src/quantum/quantum_device.h"
#include "../src/hardware/rtl_device.h"
#include "../src/net/jdr_piratenet.h"

/* THE DECLARATION-GRAPH BOOT GATE, shared with the other four architectures.
 * kernel/src/modbind/zxv_decl_gate.c holds the whole sequence; this main calls
 * it. Before that extraction the gate was inline in kernel_main_arm64.c and so
 * ran on arm64 ALONE -- BOTH RISC-V WIDTHS booted through this file without
 * ever asking whether their declaration graph was sound. Nothing in the gate is
 * XLEN-dependent: it formats every decimal with uint32_t arithmetic precisely so
 * rv32 (ilp32d, LIBGCC empty) never needs __udivdi3. */
#include "zxv_decl.h"

static void boot_msg(const char *msg) {
    uart_puts(msg);
    uart_puts("\n");
}

void kernel_main_riscv(void) {
    /* Phase 1: UART output */
    uart_init();

    /* RAISE THE CARRIER, first and silently -- and this one line covers TWO
     * architectures, rv64 and rv32, which share this file. mb_real_power
     * returns 0 with the line down (phase is measured RELATIVE to the carrier,
     * so with no reference a phase difference denotes nothing), and
     * modbind_resolve now couples by real power. Without this the declaration
     * gate below would report every module holding on an unmet requirement --
     * a system that had simply not been switched on. */
    mb_carrier_up();

    uart_puts("\nZEDEC pqOS — M5 Axiomatic Kernel (VOVINA SHAKINA) [" ARCH_NAME "]\n");
    uart_puts("==================================================\n\n");

    /* Report what the FIRMWARE said, not what we assumed. This is the evidence
     * that _start captured the handoff and that the BSS clear did not eat it. */
    uart_puts("[BOOT] SBI handoff: hart=");
    uart_put_dec((uint64_t)riscv_boot_hart);
    uart_puts(" dtb=0x");
    uart_put_hex((uint64_t)riscv_dtb_addr);
    uart_puts(" xlen=");
    uart_put_dec((uint64_t)ARCH_XLEN);
    uart_puts("\n\n");

    /* Phase 0: License banner */
    uart_puts("License: Apache-2.0\n");
    uart_puts("Author: H.M. Michael-Laurence: Curzi (c)\n");
    uart_puts("36N9 Genetics, LLC — Irrevocable, Interdimensional\n\n");

    /* Phase 1.5: Supervisor paging. Turn the MMU ON (Sv39 on rv64, Sv32 on
     * rv32) with a flat identity map, then let the declaration gate below read
     * satp back to confirm it — mm_ready is PROVIDED by riscv_mmu.c and goes
     * READY only if this actually took. Identity-mapped, so execution continues
     * uninterrupted across the switch. */
    boot_msg("[BOOT] Supervisor paging (identity map)...");
    riscv_mmu_init();
    boot_msg("  [OK] MMU enabled (satp written); mm_ready to be verified by gate");

    /* Phase 2: PLIC + Timer */
    boot_msg("[BOOT] PLIC interrupt controller...");
    plic_init();
    boot_msg("  [OK] PLIC initialized for hart 0");

    boot_msg("[BOOT] CLINT timer...");
    riscv_timer_init();
    boot_msg("  [OK] Timer at 100 Hz");

    /* Phase 3: M5 Kernel subsystems */
    boot_msg("[BOOT] M5 Kernel subsystems...");
    phase_tick_t tick;
    phase_coordinator_init(&tick, EXEC_DC);
    boot_msg("  [OK] Phase Coordinator (EXEC_DC)");
    rmag_init(256);
    boot_msg("  [OK] RMAG (256 slots)");
    lpres_init();
    boot_msg("  [OK] LPRES");
    iphase_init();
    boot_msg("  [OK] IPHASE");
    choice_handoff();
    boot_msg("  [OK] CHOICE");
    oseq_state_t oseq;
    oseq_init(&oseq);
    oseq_register_device(&oseq, "core");
    boot_msg("  [OK] OSEQ");

    int i;
    for (i = 0; i < 5; i++) {
        phase_coordinator_tick(&tick);
        uart_puts("  cycle ");
        uart_put_dec((uint64_t)(i + 1));
        uart_puts(": omega=");
        uart_put_dec(tick.omega);
        uart_puts("\n");
    }

    /* Phase 4: EDP Risk + ISF + Predictive */
    boot_msg("[BOOT] EDP risk calculus + ISF + predictive model...");
    predictive_config_t pred_cfg;
    predictive_config_init(&pred_cfg);
    boot_msg("  [OK] EDP operators + Fibonacci algebra + ISF surplus");

    /* Phase 5: Situation Modeler */
    boot_msg("[BOOT] Tactical/strategic situation modeler...");
    boot_msg("  [OK] 15 domains + cross-domain contagion");

    /* Phase 6: Triple Ledger */
    boot_msg("[BOOT] Nine-capital triple ledger system...");
    static triple_ledger_t tl;
    triple_ledger_init(&tl);
    triple_ledger_create_account(&tl, 1, CAP_FINANCIAL, "Genesis:Financial");
    triple_ledger_create_account(&tl, 1, CAP_HUMAN, "Genesis:Human");
    triple_ledger_create_account(&tl, 1, CAP_ECOLOGICAL, "Genesis:Ecological");
    boot_msg("  [OK] 9 capital types + floating vouchers + conventional compat");

    /* Phase 7: Financial Instruments */
    boot_msg("[BOOT] Financial instruments suite...");
    static portfolio_t portfolio;
    portfolio_init(&portfolio);
    boot_msg("  [OK] 20 instrument types + M5 valuation + Greeks");

    /* Phase 8: Payment Rails */
    boot_msg("[BOOT] Dragon/Phoenix/Thunderbird payment rails...");
    static rail_system_t rail_sys;
    rail_system_init(&rail_sys);
    boot_msg("  [OK] 3 rails + conventional network compat");

    /* Phase 9: Crypto Bridge */
    boot_msg("[BOOT] Web2-Web3 cryptocurrency bridge...");
    static bridge_registry_t bridge_reg;
    bridge_registry_init(&bridge_reg);
    boot_msg("  [OK] 35 chains + 11 smart contract languages + dual-directional");

    /* Phase 10: Identity System */
    boot_msg("[BOOT] Universal national identity system...");
    static identity_registry_t id_reg;
    identity_registry_init(&id_reg);
    boot_msg("  [OK] All 193 UN member states + biometric attestation");

    /* Phase 11: JDR PirateNet */
    boot_msg("[BOOT] JDR PirateNet harmonic hum carrier...");
    static jdr_network_t jdr_net;
    jdr_network_init(&jdr_net);
    jdr_register_node(&jdr_net, 1);
    jdr_transceiver_create(&jdr_net, 145000000, 12500,
        JDR_BAND_VHF, JDR_MOD_FM, JDR_EXEC_AC, "JDR-RV-001");
    boot_msg("  [OK] 22 frequency bands + harmonic hum + FHSS");

    /* Phase 12: Quantum Devices */
    boot_msg("[BOOT] Quantum + exotic matter devices...");
    static quantum_system_t qsys;
    quantum_system_init(&qsys);
    boot_msg("  [OK] Casimir + ZPE + wormhole + exotic matter + harmonic rendering");

    /* Phase 13: RTL Device Framework */
    boot_msg("[BOOT] Hardware-as-code RTL device framework...");
    static rtl_registry_t rtl_reg;
    rtl_registry_init(&rtl_reg);
    boot_msg("  [OK] Chisel/SystemVerilog/VHDL + AXI4/APB/AHB + second quantization");

    /* Phase 14: Vino Bank */
    boot_msg("[BOOT] Vino decentralized bank node...");
    static vino_ledger_t vino;
    vino_init(&vino, 1);
    vino_create_account(&vino, "ZEDEC:node:riscv:0001", "Genesis Account");
    vino_set_validator(&vino, true, 1000000);
    boot_msg("  [OK] Triple ledger + nine capital + ISO 20022");

    /* Phase 15: Vena Runtime */
    boot_msg("[BOOT] Vena application runtime...");
    static vena_runtime_t vena;
    vena_init(&vena, &vino);
    vena_set_language(&vena, LANG_M5_AXIOMATIC);
    boot_msg("  [OK] M5 Axiomatic app loader + 31 languages");

    /* Phase 16: Holographic Data System */
    boot_msg("[BOOT] Holographic data system...");
    static holo_ctx_t holo;
    holo_init(&holo);
    boot_msg("  [OK] .36n9 + .9n63 + .36m9 + .zedei + .zedec file types");

    /* ==== THE REAL DECLARATION GRAPH, AND ITS GATE ==========================
     * ONE CALL, FIVE ARCHITECTURES (kernel/src/modbind/zxv_decl_gate.c), and
     * this call covers TWO of them -- rv64 and rv32 share this file.
     *
     * PLACEMENT. On arm64 the gate must sit after the last modbind_reset(),
     * because the composition fixture and modbind_selfcheck() each wipe the
     * registry and a gate registering before them would verify a table that is
     * then erased. MEASURED HERE: this main calls neither, and neither does
     * anything it reaches (`grep -rn modbind kernel/arch/riscv` is empty), so
     * there is no reset to sit after. It goes at arm64's RELATIVE position --
     * after the subsystem inits, before the "all systems online" milestone.
     * Checked rather than assumed: "after the reset" is a property of the code
     * path, not of a line number.
     *
     * uart_puts is the console. The gate needs no put_dec -- it formats its own
     * decimals, which is what lets one function serve arm32 as well.
     *
     * The return value is discarded exactly as on arm64: every fault it counts
     * has already been printed by name, and halting the boot here would be a
     * policy change rather than a port. */
    (void)zxv_decl_boot_gate(uart_puts);

    /* ==== TOL VOVINA UPAAH LOT — the MegaROM container registers at boot =====
     * On x86_64/arm64 boot_features_init() runs tvl_bringup()->
     * tvl_rom_register_boot(); that init is NOT on the RISC-V boot path (only
     * kernel_main_x86_64.c and kernel_main_arm64.c call boot_features_init),
     * so the MegaROM would never take a console slot here. Register it directly:
     * tvl_rom_register_boot() builds the canonical TVUL container with the SAME
     * build_probe() the host tool uses, validates it through tvl_rom_parse(),
     * and takes a real MR_KIND_GAME megarom slot. The identical bytes ship on
     * the attached virtio-blk drive as MEGAROM.TVL (build_system/mk_megarom.c);
     * the on-drive READ path is not compiled for this arch, so what registers is
     * the EMBEDDED twin -- stated, not hidden. Forward-declared (not #included)
     * so no header path is assumed; both symbols are already linked in from
     * kernel/src/tolvovina/tvl_rom.c and kernel/src/emu/megarom.c. */
    {
        extern int tvl_rom_register_boot(void);
        extern int megarom_count(void);
        int mr_slot = tvl_rom_register_boot();
        if (mr_slot >= 0) {
            uart_puts("[MEGAROM] slot ");
            uart_put_dec((uint64_t)mr_slot);
            uart_puts(": 'TOL VOVINA UPAAH LOT' — MR_KIND_GAME registered "
                      "(container embedded; identical bytes on attached "
                      "virtio-blk drive as MEGAROM.TVL); registry count=");
            uart_put_dec((uint64_t)megarom_count());
            uart_puts("\n");
        } else {
            uart_puts("[MEGAROM] tvl_rom_register_boot FAILED\n");
        }
    }

    /* Phase 16b: ramfb display scanout — draw the ZEDEC desktop.
     * The full compositor chain (prism_break -> display negotiation -> ramfb
     * over fw_cfg -> vbe -> zxv_shell + lattice_dim) was linked into this build
     * but never CALLED here; arm64 alone reached it. zxv_render_boot() is that
     * missing call site, shared with arm32. Geometry is negotiated from real
     * capability limits (scanout budget + compositor capacity), never hardcoded.
     * Absent a `-device ramfb`, ramfb_init returns <0 and we stay on serial --
     * an honest headless boot, not a failure. Covers rv64 and rv32 (one file). */
    {
        extern int zxv_render_boot(void (*log)(const char *));
        (void)zxv_render_boot(boot_msg);
    }

    /* Phase 17: Enable interrupts and enter event loop */
    boot_msg("\n[BOOT] ZEDEC pqOS [" ARCH_NAME "] — All systems online.");
    boot_msg("[BOOT] Entering event cycle...\n");

    enable_irq();

    /* The milestone every other arch already emits. Its ABSENCE here is why the
     * matrix has read "riscv runs but never prints BOOT_OK" — that was a missing
     * probe, not a failed boot. Wording matches kernel_main_arm64.c:2781 and
     * kernel_main_x86_64.c:240 so ONE grep works across all five arches. */
    boot_msg("[BOOT_OK] Phase E0082 complete; kernel_main reached; S-mode event loop ready\n");

    uint32_t cycle = 0;
    while (1) {
        halt();

        phase_coordinator_tick(&tick);

        if (cycle % 100 == 0) {
            uart_puts("tick: omega=");
            uart_put_dec(tick.omega);
            uart_puts(" cycle=");
            uart_put_dec((uint64_t)cycle);
            uart_puts("\n");
        }

        if (cycle % 1000 == 0 && vino.is_validator) {
            vino_propose_block(&vino);
        }

        cycle++;
    }
}
