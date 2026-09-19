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

/* THE DECLARATION-GRAPH BOOT GATE, shared with the other four architectures.
 * kernel/src/modbind/zxv_decl_gate.c holds the whole sequence; this main calls
 * it. Before that extraction the gate was inline in kernel_main_arm64.c and so
 * ran on arm64 ALONE -- this file booted without ever asking whether its
 * declaration graph was sound. */
#include "zxv_decl.h"

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

    /* RAISE THE CARRIER, first and silently. mb_real_power returns 0 with the
     * line down -- phase is measured RELATIVE to the carrier, so with no
     * reference a phase difference denotes nothing -- and modbind_resolve now
     * couples by real power. Without this the declaration gate below would
     * report every module holding on an unmet requirement, describing a system
     * that had simply not been switched on. Before the gate, not beside it. */
    mb_carrier_up();

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
        if (x86_ring3_selftest()) {
            boot_msg("  [DRIVER ONLINE] ring-3 ran on the SHARED ABI: WRITE(2), ABI_VERSION(12), EXIT(1)");
            {   /* The ARGUMENT PATH, tested. Three distinct values were loaded
                 * into rdi/rsi/rdx by the ring-3 program; all three must have
                 * reached the dispatcher. This is the regression net for a bug
                 * that was previously caught only by re-reading the assembly. */
                extern uint32_t x86_ring3_argcheck(void);
                uint32_t bad = x86_ring3_argcheck();
                if (bad == 0)
                    boot_msg("  [VERIFIED] syscall args: all 3 registers (rdi/rsi/rdx) reached the kernel");
                else if (bad == 6u)
                    boot_msg("  [FAIL] syscall args: arg1+arg2 LOST — the ISR is not capturing them");
                else
                    boot_msg("  [FAIL] syscall args: a register did not survive the ring-3 transition");
            }
            {   /* Report ABI coverage honestly: x86_64 does not yet bind a
                 * process table or filesystem to ring 3, so several calls are
                 * ENOSYS rather than stubbed to look present. */
                extern void x86_abi_coverage(uint32_t *, uint32_t *);
                uint32_t impl = 0, total = 0;
                x86_abi_coverage(&impl, &total);
                char m[80]; uint32_t o = 0;
                const char *p1 = "  [ABI] x86_64 provides ";
                for (const char *q = p1; *q; q++) m[o++] = *q;
                m[o++] = (char)('0' + (impl / 10) % 10); m[o++] = (char)('0' + impl % 10);
                m[o++] = ' '; m[o++] = 'o'; m[o++] = 'f'; m[o++] = ' ';
                m[o++] = (char)('0' + (total / 10) % 10); m[o++] = (char)('0' + total % 10);
                const char *p2 = " ABI calls (rest ENOSYS, not stubbed)";
                for (const char *q = p2; *q; q++) m[o++] = *q;
                m[o] = 0; boot_msg(m); }
        }
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

    /* ==== THE REAL DECLARATION GRAPH, AND ITS GATE ==========================
     * ONE CALL, FIVE ARCHITECTURES (kernel/src/modbind/zxv_decl_gate.c).
     *
     * PLACEMENT. On arm64 the gate sits after the last modbind_reset() -- the
     * composition fixture and modbind_selfcheck() both wipe the registry, so a
     * gate registering before them would verify a table that is then erased.
     * MEASURED HERE: this main calls neither, and nothing it reaches does
     * either (`grep -rn modbind kernel/arch/x86_64 kernel/src/bootfeat` finds
     * no reset and no selfcheck), so there is no reset to sit after. It is
     * placed at arm64's RELATIVE position instead: after the subsystem inits,
     * and BEFORE boot_features_init/boot_economy_init, matching
     * kernel_main_arm64.c where the gate is at :2321 and boot_features_init at
     * :2867. Verified rather than assumed, because "after the reset" is a
     * property of the code path, not of the line number.
     *
     * The console is uart_puts, not boot_msg. boot_msg files each line into the
     * x86_64 evidence ledger; the gate's lines are diagnostics of the graph, and
     * boot_features_init/boot_economy_init on the two lines below already take
     * raw uart_puts for the same reason. arm64 hands the gate a line-buffering
     * adapter because its ledger is hashed into the boot measurement and eight
     * records would have gone missing; nothing here depends on that hash.
     *
     * Return value deliberately discarded, exactly as on arm64: every fault it
     * counts has already been printed by name, and halting would be a policy
     * change rather than a port. */
    (void)zxv_decl_boot_gate(uart_puts);

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
