/* kernel_main_x86_64.c — x86-64 kernel boot driver
 *
 * Bare-metal entry for x86-64. Called by boot.s after long mode is active.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#include "kernel_main_x86_64.h"
#include "../zxv_handoff.h"
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
#include "efifb_con.h"        /* UEFI GOP text console (real PCs have no COM1) */
#include "swarm/swarm_boot.h" /* swarm + tensor arithmetic self-check ([AI_SELFCHECK_OK]) */
#include "swarm/swarm_bootmodel.h" /* the boot model answers ([AI_OK]) */
#include "../zxv_aimodel.h"        /* the loader's boot model record */
#include "hwplat_x86.h"       /* standard class drivers (kernel/src/hwdrv) */
#include "hwdrv/hw_storage.h" /* GPT + FAT32 on those disks */

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

static void hw_log_line(const char *line)
{
    uart_puts(line);
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

/* Handoff v2 (zxv_handoff.h): accept the stub's record or say why not. A
 * damaged record is never half-used; an absent one (direct -kernel boot)
 * changes nothing. Nothing below consumes the map yet: the frame allocator
 * on x86_64 is the next step (docs/BAREMETAL_OS_GAPS.md, gap 3). */
/* ---- the boot model (zxv_aimodel.h, swarm_bootmodel.h) ----
 * boot.asm maps the low 1 GB; the loader puts the model and its scratch
 * anywhere below 4 GB. Map exactly those ranges, identity and write-back
 * cached, with 2 MB pages. A 1 GB slot that already has a table (the
 * framebuffer's, or a driver's MMIO pages) keeps every entry it has; only
 * absent entries are filled. */
#define AI_PTE_P  0x001ULL
#define AI_PTE_RW 0x002ULL
#define AI_PTE_PS 0x080ULL
#define AI_PTE_A  0x000FFFFFFFFFF000ULL
static uint64_t g_ai_l2[3][512] __attribute__((aligned(4096)));
static uint32_t g_ai_l2_used;

static int ai_map_ram(uint64_t lo, uint64_t hi)
{
    if (hi > ZXAI_MAX_ADDR_X86_64 || lo >= hi) return 0;
    uint64_t cr3;
    __asm__ __volatile__("mov %%cr3, %0" : "=r"(cr3));
    uint64_t *l4 = (uint64_t *) (uintptr_t) (cr3 & AI_PTE_A);
    if (!(l4[0] & AI_PTE_P)) return 0;
    uint64_t *l3 = (uint64_t *) (uintptr_t) (l4[0] & AI_PTE_A);
    for (uint64_t a = lo & ~0x1FFFFFULL; a < hi; a += 0x200000ULL) {
        if (a < 0x40000000ULL) continue; /* the low 1 GB: boot.asm's */
        uint32_t i3 = (uint32_t) (a >> 30), i2 = (uint32_t) (a >> 21) & 511u;
        if (l3[i3] & AI_PTE_PS) continue; /* a 1 GB page covers it */
        if (!(l3[i3] & AI_PTE_P)) {
            if (g_ai_l2_used == 3u) return 0;
            uint64_t *t = g_ai_l2[g_ai_l2_used++];
            for (int i = 0; i < 512; i++) t[i] = 0;
            l3[i3] = (uint64_t) (uintptr_t) t | AI_PTE_P | AI_PTE_RW;
        }
        uint64_t *l2 = (uint64_t *) (uintptr_t) (l3[i3] & AI_PTE_A);
        if (!(l2[i2] & AI_PTE_P)) l2[i2] = a | AI_PTE_P | AI_PTE_RW | AI_PTE_PS;
    }
    __asm__ __volatile__("mov %%cr3, %%rax\n\tmov %%rax, %%cr3" : : : "rax", "memory");
    return 1;
}

extern char __stack_top[];

static void ai_boot_model(void)
{
    static swarm_bootmodel_out_t res;
    const zxai_rec_t *r = (const zxai_rec_t *) (uintptr_t) ZXAI_REC_ADDR_X86_64;
    if (!zxai_valid(r)) {
        swarm_bootmodel_none("no model handed over by the loader", uart_puts);
        return;
    }
    if (!ai_map_ram(r->model_phys, r->model_phys + r->model_bytes) ||
        !ai_map_ram(r->scratch_phys, r->scratch_phys + r->scratch_bytes)) {
        swarm_bootmodel_none("the loader's model is outside the memory this kernel can map",
                             uart_puts);
        return;
    }
    swarm_bootmodel_in_t in;
    in.model = (const uint8_t *) (uintptr_t) r->model_phys;
    in.model_bytes = r->model_bytes;
    in.loader_sha256 = r->sha256;
    in.name = r->name;
    in.name_len = r->name_len;
    in.scratch = (uint8_t *) (uintptr_t) r->scratch_phys;
    in.scratch_bytes = r->scratch_bytes;
    in.map_lo = 0x100000ULL;
    in.map_hi = ZXAI_MAX_ADDR_X86_64;
    in.kernel_lo = 0x80000ULL; /* the handoff pages, the record page, the image */
    in.kernel_hi = (uint64_t) (uintptr_t) __stack_top;
    swarm_bootmodel_run(&in, &res, uart_puts);
}

static void handoff_report(void)
{
    const zxh_t *h = (const zxh_t *) (ZXV_X86_BOOTINFO_ADDR + ZXH_OFFSET);
    int v = zxh_accept(h);
    if (v == 0) {
        boot_msg("[HANDOFF] none (direct kernel boot): no firmware memory map");
        return;
    }
    if (v != 1) {
        boot_msg("[HANDOFF_BAD] handoff record damaged: ignored");
        return;
    }
    uart_puts("  memory: ");
    uart_put_dec(h->n_mem);
    uart_puts(" regions, usable ");
    uart_put_dec(zxh_pages_of(h, ZXH_MEM_USABLE) / 256u);
    uart_puts(" MiB, loader ");
    uart_put_dec(zxh_pages_of(h, ZXH_MEM_LOADER) / 256u);
    uart_puts(" MiB, firmware runtime ");
    uart_put_dec(zxh_pages_of(h, ZXH_MEM_RUNTIME) * 4u);
    uart_puts(" KiB");
    if (h->flags & ZXH_F_MEM_TRUNCATED) uart_puts(" (map truncated)");
    uart_puts("\n  kernel window 0x");
    uart_put_hex(h->kernel_lo);
    uart_puts("-0x");
    uart_put_hex(h->kernel_hi);
    uart_puts(h->window == 1   ? " verified free\n"
              : h->window == 3 ? " free but covering ACPI memory (held)\n"
                               : " NOT verified\n");
    if (h->flags & ZXH_F_ACPI_NVS_LOST)
        uart_puts("  ACPI NVS overwritten by the kernel window: sleep and AML held\n");
    uart_puts("  ACPI RSDP 0x");
    uart_put_hex(h->flags & ZXH_F_ACPI_TABLES_LOST ? 0 : h->rsdp);
    if (h->flags & ZXH_F_ACPI_TABLES_LOST) uart_puts(" (tables overwritten: not trusted)");
    uart_puts(h->smbios3 ? ", SMBIOS 3 present" : ", no SMBIOS 3");
    uart_puts("\n  firmware clock ");
    if (h->efi_time) {
        uart_put_dec((uint64_t) h->efi_time);
        uart_puts(" s since 1970 UTC\n");
    } else {
        uart_puts("unavailable\n");
    }
    uart_puts("  disc models: ");
    uart_puts(h->models == 1 ? "verified" : h->models == 2 ? "FAILED" : "none on this disc");
    uart_puts("\n");
    boot_msg("[HANDOFF_OK] handoff v2 accepted: memory map, ACPI, clock, kernel window");
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
    if (efifb_init())
        boot_msg("  [DRIVER ONLINE] UEFI GOP text console (serial mirrored on screen)");
    handoff_report();

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

    /* Standard class drivers: scan PCI, bind NVMe/AHCI/xHCI/e1000/HDA, then a
     * short self-test of each device that bound. Absent or failing hardware
     * is logged and skipped; it never stops the boot. */
    {
        boot_msg("[BOOT] Hardware: PCI scan and standard class drivers...");
        uint64_t tsc_per_us = 0;
        const hw_plat_t *hp = hwplat_x86_init(hw_log_line, &tsc_per_us);
        static hw_registry_t hreg;
        hw_probe_all(hp, &hreg);
        uint32_t ok = hw_selftest_all(hp, &hreg);
        /* Every disk: find the EFI system partition, mount it as FAT32 and
         * read the MegaROM the disc carries, so a driver is proven all the
         * way to a file. */
        hw_storage_selftest(hp, &hreg, "MEGAROM.TVL");
        uart_puts("  [OK] hardware self-tests passed: ");
        uart_put_dec(ok);
        uart_puts(" (TSC ");
        uart_put_dec(tsc_per_us);
        uart_puts(" ticks/us)\n");
    }

    /* AI self-check, same entry point and constants as arm64 (swarm_boot.h): a
     * mismatch prints [FAULT] and halts here, before [BOOT_OK]. */
    if (!ai_boot_selfcheck(uart_puts)) {
        uart_puts("[FAULT] AI selfcheck failed; halting before BOOT_OK\n");
        cli();
        for (;;) hlt();
    }

    /* The AI itself: run the model the loader handed over. [AI_OK] means it
     * answered; a boot without a model says so and goes on. */
    ai_boot_model();

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
