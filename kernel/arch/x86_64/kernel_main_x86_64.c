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
#include "bootmem_x86.h"
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
#include "../zxv_aimodel.h"        /* zxai_scratch_bytes: the boot model's scratch */
#include "hwplat_x86.h"       /* standard class drivers (kernel/src/hwdrv) */
#include "acpi_x86.h"         /* ACPI tables, AML interpreter, power management */
#include "hwdrv/hw_storage.h" /* GPT + FAT32 on those disks */
#include "hwdrv/hw_models.h"  /* golden disc models, checked after boot */
#include "swarmnet/swarmnet.h"  /* missing swarm discs over the network */
#include "layers/layers_boot.h" /* the release lineage L0 -> L1 -> L2, before BOOT_OK */
#include "manifest_hashes.h"    /* L0 and the swarm discs this kernel was built after */
#include "x86_irq.h"            /* IDT, LAPIC, timer (gap 6) */
#include "x86_userspace.h"      /* ring-3 processes and the scheduler (gap 6) */
#include "msr_safe.h"           /* fault-safe RDMSR/WRMSR + RAPL helpers */
#include "mca_x86.h"            /* machine check: banks, #MC, poll */
#include "x86_console.h"        /* PS/2 + COM1 console: login, create, ask */

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
 * changes nothing. bootmem_x86.c takes the kernel's large buffers from the
 * map. */

/* ---- the boot model (swarm_bootmodel.h) ----
 * The core disc's smallest boot-partition model is the boot model. The
 * kernel reads it itself (hw_models_load: the bytes hashed are the bytes
 * kept) and runs it with a scratch region. Both come from RAM above the
 * 1 GB boot.asm maps, out of the handoff map, so the low memory bootmem_x86
 * hands out stays for drivers; each is mapped here, identity and
 * write-back, with 2 MB pages. Without such RAM (no handoff, or too little
 * there) both come from bootmem_x86 below 1 GB. */
#define AI_PTE_P  0x001ULL
#define AI_PTE_RW 0x002ULL
#define AI_PTE_PS 0x080ULL
#define AI_PTE_A  0x000FFFFFFFFFF000ULL
#define AI_2M     0x200000ULL
#define AI_1G     0x40000000ULL
#define AI_TOP    0x8000000000ULL /* what PML4 entry 0 covers: 512 GB */
#define AI_L2S    6u              /* 2 MB tables: two regions of up to 2 GB */
static uint64_t g_ai_l2[AI_L2S][512] __attribute__((aligned(4096)));
static uint32_t g_ai_l2_used;

/* Map [lo, hi), 2 MB aligned, between 1 GB and AI_TOP. A 1 GB slot that
 * already has a table (a driver's MMIO pages) keeps every entry it has; only
 * absent entries are filled. */
static int ai_map_ram(uint64_t lo, uint64_t hi)
{
    if (hi > AI_TOP || lo >= hi || lo < AI_1G) return 0;
    uint64_t cr3;
    __asm__ __volatile__("mov %%cr3, %0" : "=r"(cr3));
    uint64_t *l4 = (uint64_t *) (uintptr_t) (cr3 & AI_PTE_A);
    if (!(l4[0] & AI_PTE_P)) return 0;
    uint64_t *l3 = (uint64_t *) (uintptr_t) (l4[0] & AI_PTE_A);
    for (uint64_t a = lo; a < hi; a += AI_2M) {
        uint32_t i3 = (uint32_t) (a >> 30), i2 = (uint32_t) (a >> 21) & 511u;
        if (l3[i3] & AI_PTE_PS) continue; /* a 1 GB page covers it */
        if (!(l3[i3] & AI_PTE_P)) {
            if (g_ai_l2_used == AI_L2S) return 0;
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

/* need bytes (a multiple of 2 MB) of usable RAM between 1 GB and AI_TOP,
 * whole 2 MB pages, outside [skip_lo, skip_hi); 0 when there is none.
 * Nothing else in the kernel takes RAM there. */
static uint64_t ai_high_ram(uint64_t need, uint64_t skip_lo, uint64_t skip_hi)
{
    const zxh_t *h = zxv_handoff();
    if (!h || !need) return 0;
    for (uint32_t i = 0; i < h->n_mem && i < ZXH_MAX_MEM; i++) {
        if (h->mem[i].kind != ZXH_MEM_USABLE) continue;
        uint64_t lo = h->mem[i].base, hi = lo + h->mem[i].pages * 4096u;
        if (lo < AI_1G) lo = AI_1G;
        if (hi > AI_TOP) hi = AI_TOP;
        lo = (lo + AI_2M - 1) & ~(AI_2M - 1);
        hi &= ~(AI_2M - 1);
        if (lo < skip_hi && lo + need > skip_lo) lo = skip_hi; /* past the other region */
        if (hi > lo && hi - lo >= need && (lo + need <= skip_lo || lo >= skip_hi)) return lo;
    }
    return 0;
}

extern char __stack_top[];

static void ai_boot_model(hw_models_t *ms, const hw_plat_t *hp)
{
    static swarm_bootmodel_out_t res;
    int b = -1;
    for (uint32_t i = 0; i < ms->n; i++)
        if (ms->m[i].vol == 0 && ms->m[i].found && ms->m[i].verdict != HW_F &&
            (b < 0 || ms->m[i].bytes < ms->m[b].bytes))
            b = (int) i;
    if (b < 0) {
        swarm_bootmodel_none("no model on the boot partition checks out", uart_puts);
        return;
    }
    hw_model_t *m = &ms->m[b];
    uint64_t mbytes = ((uint64_t) m->bytes + AI_2M - 1) & ~(AI_2M - 1);
    uint64_t sbytes = (zxai_scratch_bytes(m->bytes) + AI_2M - 1) & ~(AI_2M - 1);
    uint8_t *model = 0, *scratch = 0;
    uint64_t mh = ai_high_ram(mbytes, 0, 0);
    uint64_t sh = mh ? ai_high_ram(sbytes, mh, mh + mbytes) : 0;
    if (sh && ai_map_ram(mh, mh + mbytes) && ai_map_ram(sh, sh + sbytes)) {
        model = (uint8_t *) (uintptr_t) mh;
        scratch = (uint8_t *) (uintptr_t) sh;
    } else {
        model = bootmem_x86_take(mbytes, "the boot model");
        scratch = model ? bootmem_x86_take(sbytes, "the boot model's scratch") : 0;
    }
    if (!scratch) {
        swarm_bootmodel_none("no memory for the boot model and its scratch", uart_puts);
        return;
    }
    uart_puts("[AI_LOAD] ");
    uart_puts(m->name);
    uart_puts(": reading ");
    uart_put_dec(m->bytes);
    uart_puts(" bytes from the boot partition\n");
    if (hw_models_load(ms, hp, (uint32_t) b, model, mbytes) != HW_T) {
        swarm_bootmodel_none("the boot model on the disc is not the one MODELS.SHA lists",
                             uart_puts);
        return;
    }
    uint32_t nlen = 0;
    while (m->name[nlen]) nlen++;
    swarm_bootmodel_in_t in;
    in.model = model;
    in.model_bytes = m->bytes;
    in.loader_sha256 = m->sha; /* MODELS.SHA's digest, matched as it was read */
    in.name = m->name;
    in.name_len = nlen;
    in.scratch = scratch;
    in.scratch_bytes = sbytes;
    in.map_lo = 0x100000ULL;
    in.map_hi = AI_TOP;
    in.kernel_lo = 0x80000ULL; /* the handoff page, then the image */
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
    zxh_report(h, uart_puts, uart_put_dec, uart_put_hex);
    boot_msg("[HANDOFF_OK] handoff v2 accepted: memory map, ACPI, clock, kernel window");
}

/* The ACPI RSDP from an accepted handoff whose tables survived, else 0 (the
 * interrupt layer then takes the LAPIC base from the MSR). */
static uint64_t handoff_rsdp(void)
{
    const zxh_t *h = zxv_handoff();
    if (!h || (h->flags & ZXH_F_ACPI_TABLES_LOST)) return 0;
    return h->rsdp;
}

/* Milliseconds and the heartbeat for the swarm-disc fetch (swarmnet.h), which
 * polls instead of halting. */
static uint64_t g_heartbeat_next;

static uint64_t x86_ms(void *ctx)
{
    (void) ctx;
    return x86_ticks() * 1000u / X86_HZ;
}

/* Set once someone types at the console: the heartbeat has shown the timer
 * works, and a line every ten seconds would cut into what they type. */
static int g_console_typed;

static void x86_heartbeat(void *ctx)
{
    (void) ctx;
    uint64_t now = x86_ticks();
    if (now < g_heartbeat_next || g_console_typed) return;
    g_heartbeat_next = now + 10u * X86_HZ;
    uart_puts("[BOOT_OK] heartbeat: ticks=");
    uart_put_dec(now);
    uart_puts("\n");
}

/* A key for the seal's question: the serial console, then any USB keyboard. */
static int x86_key(void *ctx)
{
    hw_registry_t *r = (hw_registry_t *) ctx;
    int c = uart_try_getc();
    if (c > 0) return c;
    for (uint32_t i = 0; i < r->n_input; i++) {
        hw_key_t ev;
        while (r->input[i]->poll(r->input[i], &ev))
            if (ev.pressed && ev.ascii) return ev.ascii;
    }
    return 0;
}

/* linker64.ld: the hydration code the seal overwrites */
extern uint8_t __zxv_hydrate_start[], __zxv_hydrate_end[];

static int x86_has_rdrand(void)
{
    uint32_t a = 1, b, c = 0, d;
    __asm__ volatile("cpuid" : "+a"(a), "=b"(b), "+c"(c), "=d"(d));
    return (int) ((c >> 30) & 1u);
}

/* RDRAND, retried as Intel advises; 0 after ten underflows. */
static uint32_t x86_rdrand32(void)
{
    for (int i = 0; i < 10; i++) {
        uint32_t v;
        uint8_t ok;
        __asm__ volatile("rdrand %0; setc %1" : "=r"(v), "=qm"(ok)::"cc");
        if (ok) return v;
    }
    return 0;
}

void kernel_main_x86_64(uint32_t mb2_magic, uint64_t mb2_info)
{

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
    bootmem_x86_init(mb2_magic, mb2_info);

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

    /* Interrupts (gap 6): the full IDT, LAPIC and masked PIC/IOAPIC replace
     * the ring-3 self-test's exception-only IDT (x86_irq.c). */
    boot_msg("[BOOT] Interrupts: IDT, local APIC, legacy PIC and IOAPIC masked...");
    if (x86_irq_boot(handoff_rsdp()) == 0 && x86_timer_boot() == 0) {
        /* Processes (gap 6): preemptive round robin over ring-3 processes
         * with their own page tables; prints [SCHED_OK] (x86_userspace.c). */
        boot_msg("[BOOT] Ring-3 processes: preemptive scheduler self-test...");
        x86_sched_selftest();
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
    /* The two ledgers are 62 MB together: they come from firmware-usable
     * memory past the kernel (bootmem_x86.c), not from a fixed BSS window. */
    triple_ledger_t *tl = bootmem_x86_take(sizeof *tl, "the triple ledger");
    vino_ledger_t *vino = bootmem_x86_take(sizeof *vino, "the vino ledger");
    if (!tl || !vino) {
        boot_msg("[BOOT_REFUSED] no firmware memory map with room for the ledgers");
        for (;;) __asm__ __volatile__("cli; hlt");
    }
    triple_ledger_init(tl);
    triple_ledger_create_account(tl, 1, CAP_FINANCIAL, "Genesis:Financial");
    static portfolio_t pf; portfolio_init(&pf);
    static rail_system_t rs; rail_system_init(&rs);
    static bridge_registry_t br; bridge_registry_init(&br);
    static identity_registry_t idr; identity_registry_init(&idr);
    static quantum_system_t qs; quantum_system_init(&qs);
    vino_init(vino, 1);
    vino_create_account(vino, "ZEDEC:node:x86_64:0001", "Genesis Account");
    static vena_runtime_t vena;
    vena_init(&vena, vino);
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

    /* Fault-safe MSR access (msr_safe.h). A valid MSR must read; an MSR access
     * the CPU rejects must #GP, be recovered through the exception-fixup
     * table, come back as X86_MSR_EFAULT -- and the kernel must keep going.
     *
     * MEASURED ON QEMU 8.2 TCG (the CI machine): no RDMSR at CPL0 ever raises
     * #GP. Unknown MSRs read as 0 (probed: 0xDEADBEEF, 0x1FF, 0x2FF, 0x802 in
     * xAPIC mode, 0x606, 0xC0010299, ...). The one MSR fault TCG does raise
     * here is WRMSR of reserved bits into IA32_PKRS (0x6E1) -- which also #GPs
     * on every real CPU (bits 63:32 are reserved where PKS exists; the MSR
     * does not exist where it does not), so writing it changes no state
     * anywhere. So: the read probe reports what the CPU did, the write probe
     * MUST fault and recover, and both table entries must be registered. */
    {
        extern const char x86_rdmsr_insn[], x86_wrmsr_insn[];
        uint64_t apic = 0, tsc = 0, junk = 0x5A5Au;
        int ra = x86_rdmsr_safe(MSR_IA32_APIC_BASE, &apic);
        int rt = x86_rdmsr_safe(MSR_IA32_TSC, &tsc);
        /* x2APIC registers #GP by definition while x2APIC mode is off. */
        uint32_t bad = (ra == X86_MSR_OK && !(apic & (1u << 10))) ? MSR_X2APIC_APICID : 0xDEADBEEFu;
        int rb = x86_rdmsr_safe(bad, &junk);
        int rw = x86_wrmsr_safe(MSR_IA32_PKRS, 0xFFFFFFFF00000000ull);
        int reg = x86_extable_fixup((uint64_t) (uintptr_t) x86_rdmsr_insn) != 0 &&
                  x86_extable_fixup((uint64_t) (uintptr_t) x86_wrmsr_insn) != 0;
        /* APIC_BASE is never 0 on a CPU with an APIC (0xFEE00900 under QEMU).
         * The TSC MSR's VALUE is not checked: QEMU TCG reads it back as 0. */
        int valid_ok = ra == X86_MSR_OK && rt == X86_MSR_OK && apic != 0;
        if (valid_ok && rb == X86_MSR_EFAULT && junk == 0x5A5Au)
            boot_msg("  [MSR OK] safe rdmsr: valid read ok, invalid MSR faulted and recovered");
        else if (valid_ok && rb == X86_MSR_OK)
            boot_msg(
                "  [MSR OK] safe rdmsr: valid read ok; invalid MSR read did not #GP on this CPU "
                "(emulator reads unknown MSRs as 0)");
        else {
            uart_puts("  [MSR FAIL] safe rdmsr: apic_base rc=-");
            uart_put_dec((uint64_t) (-ra));
            uart_puts(" tsc rc=-");
            uart_put_dec((uint64_t) (-rt));
            uart_puts(" invalid rc=-");
            uart_put_dec((uint64_t) (-rb));
            uart_puts("\n");
        }
        if (rw == X86_MSR_EFAULT)
            boot_msg("  [MSR OK] safe wrmsr: invalid MSR write faulted and recovered");
        else
            boot_msg("  [MSR FAIL] safe wrmsr: reserved-bit write to IA32_PKRS did not fault");
        if (reg)
            boot_msg("  [MSR OK] fixup table: rdmsr and wrmsr registered with the #GP handler");
        else
            boot_msg("  [MSR FAIL] fixup table: rdmsr/wrmsr entry missing");
        uint32_t pu = 0, eu = 0, tu = 0;
        int rr = x86_rapl_units(&pu, &eu, &tu);
        if (rr == X86_MSR_OK) {
            uart_puts("  [MSR] RAPL present (");
            uart_puts(x86_rapl_kind() == X86_RAPL_AMD ? "AMD" : "Intel");
            uart_puts("): power 1/2^");
            uart_put_dec(pu);
            uart_puts(" W, energy 1/2^");
            uart_put_dec(eu);
            uart_puts(" J, time 1/2^");
            uart_put_dec(tu);
            uart_puts(" s\n");
        } else {
            uart_puts(rr == X86_MSR_ENODEV
                          ? "  [MSR] RAPL absent (CPUID: no RAPL interface)\n"
                          : "  [MSR] RAPL absent (CPUID claims it; unit MSR faulted)\n");
        }
    }

    /* Standard class drivers: scan PCI, bind NVMe/AHCI/xHCI/e1000/HDA, then a
     * short self-test of each device that bound. Absent or failing hardware
     * is logged and skipped; it never stops the boot. */
    static hw_models_t models;
    static zxv_layers_boot_t layers;
    static const uint8_t l0_cid[ZXV_LAYER_CID_BYTES] = ZXV_TABLE_L0_CID_BIN;
    static hw_registry_t hreg;
    const hw_plat_t *hp;
    int models_left;
    {
        boot_msg("[BOOT] Hardware: PCI scan and standard class drivers...");
        uint64_t tsc_per_us = 0;
        hp = hwplat_x86_init(hw_log_line, &tsc_per_us);
        hw_probe_all(hp, &hreg);
        /* Self-encrypting NVMe disks: unlocked only with a key from an
         * attested arbiter; any other disk gets one line and is left. */
        hw_sed_unlock_all(hp, &hreg, hw_arbiter());
        uint32_t ok = hw_selftest_all(hp, &hreg);
        /* Every disk: find the EFI system partition, mount it as FAT32 and
         * read the MegaROM the disc carries, so a driver is proven all the
         * way to a file. */
        hw_storage_selftest(hp, &hreg, "MEGAROM.TVL");
        /* A golden disc lists its AI models in /ZXV/MODELS.SHA, some on the
         * ESP and the swarm on its own partition. Only the list is read now;
         * the models are hashed in the idle loop below (hw_models.h). */
        models_left = hw_models_index(&models, hp, &hreg) > 0;
        /* The release's layers: the disc's L0, L1 and L2 must name each
         * other, and the boot file and the models must be the ones they
         * list, before BOOT_OK hands the machine over. A model that does
         * not match is refused, never handed out (layers_boot.h). */
        (void) zxv_layers_boot(&layers, &models, hp, "x86_64", "BOOTX64.EFI", l0_cid);
        uart_puts("  [OK] hardware self-tests passed: ");
        uart_put_dec(ok);
        uart_puts(" (TSC ");
        uart_put_dec(tsc_per_us);
        uart_puts(" ticks/us)\n");

        /* ACPI: tables, the AML namespace (DSDT + SSDTs), sleep states, power
         * button, batteries, lid, thermal zones, processors, backlight. Only
         * reports; a failure is logged and the boot goes on. */
        boot_msg("[BOOT] ACPI: tables and AML namespace...");
        acpi_x86_boot(hp, tsc_per_us, hw_log_line);
    }
    x86_mca_init(hw_log_line); /* after the IDT; one [OK] machine check line */

    /* AI self-check, same entry point and constants as arm64 (swarm_boot.h): a
     * mismatch prints [FAULT] and halts here, before [BOOT_OK]. */
    if (!ai_boot_selfcheck(uart_puts)) {
        uart_puts("[FAULT] AI selfcheck failed; halting before BOOT_OK\n");
        cli();
        for (;;) hlt();
    }

    /* The AI itself: run the model the loader handed over. [AI_OK] means it
     * answered; a boot without a model says so and goes on. */
    ai_boot_model(&models, hp);

    boot_msg("\n[BOOT] ZEDEC pqOS [x86-64] — All systems online.");
    boot_msg("[BOOT_OK] Phase E0082 complete; kernel_main reached; Stage-1 kernel loop ready\n");
    boot_msg("[BOOT] Entering event cycle...\n");

    boot_evidence_final();

    /* Swarm discs the core disc lists but nobody attached: fetch them onto a
     * blank drive over the network (swarmnet.h). The core is up already, so
     * the fetch runs in steps from the idle loop below, each disc mounted as
     * it commits; d stops it for discs attached by hand. Each disc is fetched
     * by the CID in the table this kernel was built with (manifest_hashes.h),
     * never by what a gateway or bucket says. No link, no lease
     * or no gateway prints the air-gap lines and the boot carries on with
     * what is attached. */
    static sn_link_t link;
    static sn_fetch_t fetch;
    static const sn_baked_t baked[] = ZXV_TABLE_SWARM; /* the swarm discs, by CID */
    static const sn_clock_t clk = {x86_ms, x86_heartbeat, 0};
    int hydrating = 0;
    {
        uint32_t missing = 0;
        for (uint32_t i = 0; i < models.nwant; i++) missing += !models.want[i].attached;
        if (missing && models.cid[0]) {
            if (x86_has_rdrand()) {
                net_set_entropy(x86_rdrand32);
            } else {
                uart_puts("  [NET] no RDRAND: DHCP and TCP ids are not unpredictable; every "
                          "block is still checked against its CID\n");
            }
            if (sn_sealed()) {
                uart_puts("  [SEAL] the network was sealed; attach the missing discs\n");
            } else if (sn_link_up(&link, hp, &hreg, &clk, 0)) {
                fetch.p = hp;
                fetch.table = baked;
                fetch.ntable = ZXV_TABLE_SWARM_N;
                if (sn_fetch_gateways(&fetch, &link, &models)) {
                    sn_fetch_begin(&fetch);
                    hydrating = 1;
                    uart_puts("[SWARM] The core is up; the swarm discs fill in the background. "
                              "Press d at any time to attach them by hand instead.\n");
                } else {
                    uart_puts("  [SWARM_FETCH] no http:// gateway in /ZXV/UPSTREAM.CID\n");
                }
            } else {
                sn_log_offline(hp, &models);
            }
        }
    }

    /* The LAPIC timer (X86_HZ) wakes each hlt, so the heartbeat counts timer
     * ticks: one line every ten seconds. Counting wake-ups instead (10M of
     * them) printed once a day at 100 Hz, which read as no timer at all.
     * While golden-disc models are unchecked the loop hashes 1 MiB (256
     * clusters of 4 KiB) per pass instead of halting, and while discs are
     * fetched it takes one step of the fetch per pass; the timer keeps
     * interrupting both, so the heartbeat goes on. */
    /* The console (x86_console.h): typed lines from the PS/2 keyboard, COM1
     * and any USB keyboard, for login, create and ask. While swarm discs are
     * fetched the keys belong to the fetch (d), so the console opens when it
     * ends. Each pass polls it; after a key the loop stays awake (pause, not
     * hlt) for a second, so a burst of typing is never left waiting on the
     * next timer tick. */
    int console = 0;
    uint64_t awake_until = 0;
    if (!g_heartbeat_next) g_heartbeat_next = x86_ticks() + 10u * X86_HZ;
    while (1) {
        x86_mca_poll(); /* corrected errors raise no interrupt */
        if (hydrating) {
            int k = x86_key(&hreg);
            if (k == 'd' || k == 'D') {
                sn_fetch_stop(&fetch, &models);
                hydrating = 0;
            } else if (!sn_fetch_step(&fetch, &models, &hreg)) {
                hydrating = 0;
                /* Every disc there: sever the channel that brought them. */
                uint32_t still = 0;
                for (uint32_t i = 0; i < models.nwant; i++) still += !models.want[i].attached;
                if (!still && fetch.committed) {
                    sn_seal_io_t io = {x86_key, &hreg, __zxv_hydrate_start,
                                       (uint32_t) (__zxv_hydrate_end - __zxv_hydrate_start)};
                    int mode = sn_seal(&link, &fetch, &models, &io);
                    if (mode >= 0) zxv_layers_sealed(&layers, mode == HW_SEAL_POWERDOWN_NIC);
                }
            }
            /* a disc committed: mount it, check it against its layer, and
             * its models are hashed next */
            if (fetch.fresh) {
                models_left = hw_models_index(&models, hp, &hreg) > 0;
                (void) zxv_layers_boot(&layers, &models, hp, "x86_64", "BOOTX64.EFI", l0_cid);
            }
        }
        if (!hydrating) {
            if (!console) {
                x86_console_init(uart_puts);
                console = 1;
            }
            uint32_t typed = x86_console_poll();
            for (uint32_t i = 0; i < hreg.n_input; i++) {
                hw_key_t ev;
                while (hreg.input[i]->poll(hreg.input[i], &ev))
                    if (ev.pressed && ev.ascii) {
                        x86_console_feed(ev.ascii);
                        typed++;
                    }
            }
            if (typed) {
                awake_until = x86_ticks() + X86_HZ;
                g_console_typed = 1;
            }
        }
        if (models_left)
            models_left = hw_models_step(&models, hp, 256);
        else if (x86_ticks() < awake_until)
            __asm__ __volatile__("pause");
        else if (!hydrating || x86_ms(0) < fetch.next_try) /* idle, or waiting for peers */
            hlt();
        x86_heartbeat(0);
    }
}
