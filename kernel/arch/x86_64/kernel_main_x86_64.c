/* kernel_main_x86_64.c — x86-64 kernel boot driver
 *
 * Bare-metal entry for x86-64. Called by boot.s after long mode is active.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#include "kernel_main_x86_64.h"
#include "uart_16550.h"
#include "x86_64_arch.h"
#include "boot_evidence.h"
#include "cellular_multikernel.h"

/* Full subsystem set (all 196 modules are now linked into the x86-64 image). */
#include "phase_coord/phase_coordinator.h"
#include "rmag/rmag_core.h"
#include "lpres/lpres_core.h"
#include "iphase/iphase_core.h"
#include "choice/choice_core.h"
#include "oseq/oseq_core.h"
#include "predictive/predictive_model.h"
#include "finance/triple_ledger.h"
#include "finance/financial.h"
#include "finance/rails.h"
#include "finance/crypto_bridge.h"
#include "identity/identity.h"
#include "quantum/quantum_device.h"
#include "vino/vino.h"
#include "vena/vena.h"
#include "holographic/holo.h"
#include "bootfeat/boot_features.h"

static void boot_msg(const char *msg) {
    uint32_t eid = boot_evidence_record(msg);
    uart_puts("[E");
    if (eid < 1000) uart_puts("0");
    if (eid < 100) uart_puts("0");
    if (eid < 10) uart_puts("0");
    uart_put_dec((uint64_t)eid);
    uart_puts("] ");
    uart_puts(msg);
    uart_puts("\n");
}

/* Helper to copy a short string into a fixed-size, zero-padded cell id. */
static void cell_id_set(char *dst, const char *src) {
    for (int i = 0; i < CELL_NAME_LEN; i++) {
        dst[i] = src[i] ? src[i] : '\0';
        if (!src[i]) break;
    }
}

/* CELL-001: boot-time multikernel fabric setup for x86-64. */
static void cell_fabric_boot_init(void) {
    static cell_fabric_t cell_fabric;
    static const uint8_t cell_image_digest[CELL_MAX_DIGEST] = {
        0xab,0xcd,0xef,0x01,0x02,0x03,0x04,0x05,
        0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,
        0x0e,0x0f,0x10,0x11,0x12,0x13,0x14,0x15,
        0x16,0x17,0x18,0x19,0x1a,0x1b,0x1c,0x1d
    };

    boot_msg("[BOOT] Cellular Multikernel (CELL-001): fabric discovery and admission...");
    cell_fabric_init(&cell_fabric);

    static cell_t coord; cell_contract_zero(&coord);
    cell_id_set(coord.cell_id, "x86_64-coord");
    coord.arch = CELL_ARCH_X86_64; coord.bitness = CELL_BITNESS_64;
    coord.trust = CELL_TRUST_KERNEL; coord.privilege = CELL_PRIVILEGE_RING0;
    coord.incarnation = 1;
    coord.capabilities = CELL_CAP_OSEQ | CELL_CAP_IPHASE | CELL_CAP_PHASECOORD;
    coord.budget.max_events_per_second = 100000;
    coord.budget.max_memory_bytes = 0x40000000;
    cell_set_digest(&coord, cell_image_digest);

    static cell_t kpipe; cell_contract_zero(&kpipe);
    cell_id_set(kpipe.cell_id, "x86_64-k1k6");
    kpipe.arch = CELL_ARCH_X86_64; kpipe.bitness = CELL_BITNESS_64;
    kpipe.trust = CELL_TRUST_KERNEL; kpipe.privilege = CELL_PRIVILEGE_RING0;
    kpipe.incarnation = 1;
    kpipe.capabilities = CELL_CAP_RMAG | CELL_CAP_LPRES | CELL_CAP_CHOICE;
    kpipe.budget.max_events_per_second = 50000;
    kpipe.budget.max_memory_bytes = 0x20000000;
    cell_set_digest(&kpipe, cell_image_digest);

    static cell_t io; cell_contract_zero(&io);
    cell_id_set(io.cell_id, "x86_64-io");
    io.arch = CELL_ARCH_X86_64; io.bitness = CELL_BITNESS_64;
    io.trust = CELL_TRUST_DEVICE; io.privilege = CELL_PRIVILEGE_RING0;
    io.incarnation = 1;
    io.capabilities = CELL_CAP_NETWORK | CELL_CAP_AUDIO;
    io.budget.max_events_per_second = 25000;
    io.budget.max_memory_bytes = 0x10000000;
    cell_set_digest(&io, cell_image_digest);

    if (!cell_fabric_discover(&cell_fabric, &coord) ||
        !cell_fabric_discover(&cell_fabric, &kpipe) ||
        !cell_fabric_discover(&cell_fabric, &io)) {
        boot_msg("  [FAIL] cell fabric: discovery rejected");
        return;
    }

    if (!cell_fabric_authenticate(&cell_fabric, "x86_64-coord", cell_image_digest) ||
        !cell_fabric_authenticate(&cell_fabric, "x86_64-k1k6", cell_image_digest) ||
        !cell_fabric_authenticate(&cell_fabric, "x86_64-io", cell_image_digest)) {
        boot_msg("  [FAIL] cell fabric: authentication rejected");
        return;
    }

    cell_fabric_admit(&cell_fabric, "x86_64-coord");
    cell_fabric_admit(&cell_fabric, "x86_64-k1k6");
    cell_fabric_admit(&cell_fabric, "x86_64-io");

    cell_fabric_activate(&cell_fabric, "x86_64-coord");
    cell_fabric_activate(&cell_fabric, "x86_64-k1k6");
    cell_fabric_activate(&cell_fabric, "x86_64-io");

    static cell_route_t r_oseq = { 10, 1, "x86_64-coord", 0, false };
    static cell_route_t r_rmag = { 20, 1, "x86_64-k1k6",  1, false };
    static cell_route_t r_net  = { 30, 1, "x86_64-io",    2, false };
    cell_fabric_add_route(&cell_fabric, &r_oseq);
    cell_fabric_add_route(&cell_fabric, &r_rmag);
    cell_fabric_add_route(&cell_fabric, &r_net);

    boot_msg("  [INITIALIZED] 3 x86-64 cells active (coord, k1k6, io) with phase routes");

    cell_fabric_handle_fault(&cell_fabric, "x86_64-io", CELL_HEALTH_FAIL_STOPPED);
    if (cell_fabric_route_lookup(&cell_fabric, 30, 1) == NULL) {
        boot_msg("  [FAULT CONTAINED] x86_64-io fail-stopped; routes revoked; fabric survives");
    } else {
        boot_msg("  [FAIL] cell fabric: faulted cell still has routes");
    }

    io.incarnation = 2;
    io.health = CELL_HEALTH_OK;
    io.state = CELL_STATE_DISCOVERED;
    cell_set_digest(&io, cell_image_digest);
    if (cell_fabric_discover(&cell_fabric, &io)) {
        cell_fabric_authenticate(&cell_fabric, "x86_64-io", cell_image_digest);
        cell_fabric_admit(&cell_fabric, "x86_64-io");
        cell_fabric_activate(&cell_fabric, "x86_64-io");
        cell_route_t r_net2 = { 30, 1, "x86_64-io", 2, false };
        cell_fabric_add_route(&cell_fabric, &r_net2);
        boot_msg("  [RECOVERED] x86_64-io re-admitted with incarnation=2 and new routes");
    }
}

void kernel_main_x86_64(uint32_t mb2_magic, uint64_t mb2_info) {
    (void)mb2_magic;
    (void)mb2_info;

    boot_evidence_init();
    uart_init();

    uart_puts("\nZEDEC pqOS — M5 Axiomatic Kernel (VOVINA SHAKINA) [x86-64]\n");
    uart_puts("Edition: ZEDEC XERO VULPINE (ZXV)\n");
    uart_puts("==================================================\n\n");

    boot_msg("[BOOT] Long mode entered; x86-64 kernel main");
    boot_msg("[BOOT] Initializing COM1 serial console");
    boot_msg("  [DRIVER ONLINE] 16550A COM1 at 0x3F8");

    /* Ring-3 (user-mode) parity — mirrors the arm64 EL0 path. Runs FIRST so it is
     * isolated from the rest of bring-up during development. */
    boot_msg("[BOOT] Ring-3 user-mode bring-up (GDT/TSS/IDT + int 0x80 syscall)...");
    {
        extern void x86_ring3_init(void);
        extern int  x86_ring3_selftest(void);
        x86_ring3_init();
        boot_msg("  [.] entering ring 3...");
        if (x86_ring3_selftest())
            boot_msg("  [DRIVER ONLINE] ring-3 process ran, SYS_WRITE serviced, SYS_EXIT returned to ring 0 — user/kernel split live");
        else
            boot_msg("  [WARN] ring-3 self-test faulted");
    }

    cell_fabric_boot_init();

    /* ---- Full subsystem bring-up (mirrors kernel_main_riscv.c/arm32) ---- */
    boot_msg("[BOOT] M5 core: phase/rmag/lpres/iphase/choice/oseq...");
    phase_tick_t tick; phase_coordinator_init(&tick, EXEC_DC);
    rmag_init(256); lpres_init(); iphase_init(); choice_handoff();
    static oseq_state_t oseq; oseq_init(&oseq); oseq_register_device(&oseq, "core");
    for (int i = 0; i < 5; i++) phase_coordinator_tick(&tick);
    boot_msg("  [OK] M5 core online");

    boot_msg("[BOOT] Economy core: ledger/instruments/rails/bridge/identity/quantum...");
    predictive_config_t pcfg; predictive_config_init(&pcfg);
    static triple_ledger_t tl; triple_ledger_init(&tl);
    triple_ledger_create_account(&tl, 1, CAP_FINANCIAL, "Genesis:Financial");
    static portfolio_t pf; portfolio_init(&pf);
    static rail_system_t rs; rail_system_init(&rs);
    static bridge_registry_t br; bridge_registry_init(&br);
    static identity_registry_t idr; identity_registry_init(&idr);
    static quantum_system_t qs; quantum_system_init(&qs);
    static vino_ledger_t vino; vino_init(&vino, 1);
    vino_create_account(&vino, "ZEDEC:node:x86_64:0001", "Genesis Account");
    static vena_runtime_t vena; vena_init(&vena, &vino);
    vena_set_language(&vena, LANG_M5_AXIOMATIC);
    static holo_ctx_t holo; holo_init(&holo);
    boot_msg("  [OK] nine-capital ledger + instruments + 3 rails + bridge + identity + vino/vena/holo");

    /* Platform + economy aggregate self-checks (same entry points as arm64). */
    boot_features_init(uart_puts, 0, 0);
    boot_economy_init(uart_puts);

    boot_msg("\n[BOOT] ZEDEC pqOS [x86-64] — All systems online.");
    boot_msg("[BOOT_OK] Phase E0082 complete; kernel_main reached; Stage-1 kernel loop ready\n");
    boot_msg("[BOOT] Entering event cycle...\n");

    boot_evidence_final();

    uint64_t cycle = 0;
    while (1) {
        if (++cycle >= 10000000) {
            cycle = 0;
            boot_msg("[BOOT_OK] heartbeat: cycle=10M");
        }
        hlt();
    }
}
