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
#include "swarm/swarm_boot.h" /* swarm + tensor boot self-check ([AI_OK]) */
#include "hwplat_x86.h"       /* standard class drivers (kernel/src/hwdrv) */
#include "acpi_x86.h"         /* ACPI tables, AML interpreter, power management */
#include "hwdrv/hw_storage.h" /* GPT + FAT32 on those disks */
#include "zxvfs/zxvfs.h"      /* the node's persistent copy-on-write FS */
#include "loader/abupdate.h"  /* signed A/B slots on top of it */
#include "hwdrv/hw_models.h"  /* golden disc models, checked after boot */
#include "swarmnet/swarmnet.h"  /* missing swarm discs over the network */
#include "robin_debanks/sha256.h"
#include "vinea/vna_common.h"

/* The player-card seat: a card id that the vino ledger holds, with the pin
 * derived from the node's stable owner secret (the same seed that makes its
 * identity), so nothing is stored and the check is the same on every boot
 * of this machine. The pin is the first eight bytes of
 * SHA-256("zxv/card/pin/v1" || owner || id), sent as 16 hex digits. */
struct card_ctx_t {
    vino_ledger_t *vino;
    const uint8_t *mac;
};

static int card_seat(void *ctx, const uint8_t *id, uint32_t id_len, const uint8_t *pin,
                     uint32_t pin_len)
{
    struct card_ctx_t *cc = (struct card_ctx_t *) ctx;
    vino_ledger_t *v = cc->vino;
    const uint8_t *mac = cc->mac;
    sha256_ctx_t c;
    uint8_t owner[32], expect[32];
    char acc[65];
    static const char hex[] = "0123456789abcdef";
    uint32_t i;
    if (!v || !mac || !id || !pin || id_len == 0 || id_len > 63 || pin_len != 16) return 1;
    vna_copy((uint8_t *) acc, id, id_len);
    acc[id_len] = 0;
    if (!vino_get_account(v, acc)) return 1;
    sha256_init(&c);
    sha256_update(&c, (const uint8_t *) "zxv/vinea/node/v1", 17);
    sha256_update(&c, mac, NET_MAC_LEN);
    sha256_final(&c, owner);
    sha256_init(&c);
    sha256_update(&c, (const uint8_t *) "zxv/card/pin/v1", 15);
    sha256_update(&c, owner, 32);
    sha256_update(&c, id, id_len);
    sha256_final(&c, expect);
    for (i = 0; i < 8; i++) {
        if (pin[2 * i] != (uint8_t) hex[expect[i] >> 4]) return 1;
        if (pin[2 * i + 1] != (uint8_t) hex[expect[i] & 15]) return 1;
    }
    return 0;
}
#include "vna_host.h"           /* the Vinea (Web4) node, over the same link */
#include "layers/layers_boot.h" /* the release lineage L0 -> L1 -> L2, before BOOT_OK */
#include "manifest_hashes.h"    /* L0 and the swarm discs this kernel was built after */
#include "x86_irq.h"            /* IDT, LAPIC, timer (gap 6) */
#include "x86_userspace.h"      /* ring-3 processes and the scheduler (gap 6) */
#include "msr_safe.h"           /* fault-safe RDMSR/WRMSR + RAPL helpers */
#include "mca_x86.h"            /* machine check: banks, #MC, poll */

/* THE DECLARATION-GRAPH BOOT GATE, shared with the other four architectures.
 * kernel/src/modbind/zxv_decl_gate.c holds the whole sequence; this main calls
 * it. Before that extraction the gate was inline in kernel_main_arm64.c and so
 * ran on arm64 ALONE -- this file booted without ever asking whether its
 * declaration graph was sound. */
#include "zxv_decl.h"

/* The https base the seed's door names at /boot.src: the first https://
 * gateway line in UPSTREAM.CID (hydration skips https; the browser needs
 * it — it is where boot.json and boot.pack live). NULL when none is set.
 * A gateway line is a bare host (80 chars cannot hold host + /ipfs/<cid>),
 * while its objects live under /ipfs/<bucket cid> — the door names the
 * composed root so the page's own boot.json resolves. */
static const char *door_src_of(const hw_models_t *m)
{
    static const char https[] = "https://";
    static const char ipfs[] = "/ipfs/";
    static char buf[HW_GATEWAY_LEN + sizeof ipfs - 1 + sizeof((hw_models_t *)0)->cid];
    for (uint32_t i = 0; i < m->ngateway; i++) {
        const char *g = m->gateway[i];
        uint32_t j, n = 0;
        for (j = 0; j < sizeof https - 1; j++)
            if (g[j] != https[j]) break;
        if (j != sizeof https - 1) continue;
        if (!m->cid[0]) return g;
        while (n + 1 < sizeof buf && g[n]) {
            buf[n] = g[n];
            n++;
        }
        while (n && buf[n - 1] == '/') n--;
        for (j = 0; j < sizeof ipfs - 1; j++) buf[n++] = ipfs[j];
        for (j = 0; m->cid[j] && n + 1 < sizeof buf; j++) buf[n++] = m->cid[j];
        buf[n] = 0;
        return buf;
    }
    return 0;
}

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
static zxvfs_t *g_zxvfs; /* set once the ZXV_DATA partition mounts */
static ab_state_t *g_ab; /* the A/B slot state on it (NULL: unreadable) */

static uint64_t x86_ms(void *ctx)
{
    (void) ctx;
    return x86_ticks() * 1000u / X86_HZ;
}

static void x86_heartbeat(void *ctx)
{
    (void) ctx;
    uint64_t now = x86_ticks();
    if (now < g_heartbeat_next) return;
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
    /* $YHVH: the floating voucher this node's Vinea book earns. total_supply
     * is the counter's own bound (u32); issuance happens only as earned
     * float arrives from the book's held_in, never ahead of it. */
    vino_register_asset(vino, "$YHVH", "YHVH floating voucher", ASSET_TOKEN, 0, 0xFFFFFFFFu);
    /* Generation-0 beneficiary policy: this seed fleet's earned vouchers
     * settle 50/50 into the two Black Swan player cards issued through the
     * Crown tool (BLACK_SWAN_CARDS.json). The accounts start empty here;
     * their authoritative balances live on the issuance ledger, and what a
     * node posts is its own verifiably earned contribution. */
    vino_create_account(vino, "BSC-0-1870892d", "Black Swan #0: Michael Laurence Curzi");
    vino_create_account(vino, "BSC-1-8b58e9d4", "Black Swan #1: Angela");
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

        /* The node's persistent store: a ZXV_DATA-typed partition on any
         * registered disk becomes ZXVFS (blank is formatted, same as arm64),
         * and the signed A/B slots live on it — a probation tick runs now so
         * an update that never confirms rolls itself back. */
        {
            static zxvfs_t zfs;
            static hw_bdev_t zdev;
            static ab_state_t zab;
            static uint8_t zscratch[4096];
            int mounted = 0;
            for (uint32_t i = 0; i < HW_MAX_BLK && !mounted; i++) {
                hw_blk_t *b = hreg.blk[i];
                hw_part_t p;
                if (!b) continue;
                if (hw_gpt_find_type(b, HW_GPT_ZXV_DATA, &p, zscratch) == HW_GPT_OK &&
                    hw_bdev_init(&zdev, b, p.first, p.last - p.first + 1u) == 0) {
                    int mrc = zxvfs_mount(&zfs, &zdev.bd);
                    if (mrc != 0 && zxvfs_probe_blank(&zdev.bd) > 0 && zxvfs_format(&zdev.bd) == 0)
                        mrc = zxvfs_mount(&zfs, &zdev.bd);
                    mounted = (mrc == 0);
                }
            }
            if (mounted) {
                bool rolled = false;
                g_zxvfs = &zfs;
                if (ab_init(&zfs, &zab) == AB_OK) {
                    g_ab = &zab;
                    (void) ab_boot_tick(&zfs, &zab, &rolled);
                    uart_puts(rolled ? "  [AB] probation expired; rolled back\n"
                                     : "  [AB] ZXVFS up; A/B state loaded\n");
                } else
                    uart_puts("  [AB] ZXVFS up; A/B state unreadable\n");
            } else
                uart_puts("  [AB] no ZXV_DATA partition: running without persistence\n");
        }
    }
    x86_mca_init(hw_log_line); /* after the IDT; one [OK] machine check line */

    /* AI self-check, same entry point and constants as arm64 (swarm_boot.h): a
     * mismatch prints [FAULT] and halts here, before [BOOT_OK]. */
    if (!ai_boot_selfcheck(uart_puts)) {
        uart_puts("[FAULT] AI selfcheck failed; halting before BOOT_OK\n");
        cli();
        for (;;) hlt();
    }

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
    static vna_host_t vna;
    static const sn_baked_t baked[] = ZXV_TABLE_SWARM; /* the swarm discs, by CID */
    static const sn_clock_t clk = {x86_ms, x86_heartbeat, 0};
    int hydrating = 0;
    uint64_t vna_report_next = 0;
    {
        uint32_t missing = 0;
        for (uint32_t i = 0; i < models.nwant; i++) missing += !models.want[i].attached;
        /* the link comes up for the Web4 node (peer seeds) or for hydration
         * (missing swarm discs + a bucket to fetch them from); either asks
         * the NIC for a lease, so one attempt serves both */
        if (models.npeer || (missing && models.cid[0])) {
            if (x86_has_rdrand()) {
                net_set_entropy(x86_rdrand32);
            } else {
                uart_puts("  [NET] no RDRAND: DHCP and TCP ids are not unpredictable; every "
                          "block is still checked against its CID\n");
            }
            if (sn_sealed()) {
                uart_puts("  [SEAL] the network was sealed; attach the missing discs\n");
            } else if (sn_link_up(&link, hp, &hreg, &clk, 0)) {
                if (models.npeer) {
                    static uint8_t ident_seed[32];
                    uint8_t *iseed = 0;
                    if (g_zxvfs) {
                        int r = zxvfs_read(g_zxvfs, "identity.seed", ident_seed, 32);
                        if (r == 32) {
                            iseed = ident_seed;
                        } else if (x86_has_rdrand()) {
                            /* first boot: generate the identity secret from
                             * hardware entropy and persist it (the audit's
                             * finding 1: the MAC alone must never key the
                             * node's signatures) */
                            uint32_t e[8];
                            for (uint32_t ei = 0; ei < 8; ei++) e[ei] = x86_rdrand32();
                            for (uint32_t ei = 0; ei < 8; ei++) {
                                ident_seed[ei * 4 + 0] = (uint8_t) (e[ei] >> 0);
                                ident_seed[ei * 4 + 1] = (uint8_t) (e[ei] >> 8);
                                ident_seed[ei * 4 + 2] = (uint8_t) (e[ei] >> 16);
                                ident_seed[ei * 4 + 3] = (uint8_t) (e[ei] >> 24);
                            }
                            if (zxvfs_write(g_zxvfs, "identity.seed", ident_seed, 32) == 0)
                                iseed = ident_seed;
                        }
                    }
                    if (vna_host_start(&vna, &link, models.peer, models.npeer, door_src_of(&models),
                                       x86_has_rdrand() ? x86_rdrand32 : 0, iseed)) {
                        vna_report_next = x86_ms(0) + 60000u;
                        vna.card_check = card_seat;
                        {
                            static struct card_ctx_t card_ctx;
                            card_ctx.vino = vino;
                            card_ctx.mac = link.nic->mac;
                            vna.card_ctx = &card_ctx;
                        }
                    }
                }
                if (missing && models.cid[0]) {
                    fetch.p = hp;
                    fetch.table = baked;
                    fetch.ntable = ZXV_TABLE_SWARM_N;
                    if (sn_fetch_gateways(&fetch, &link, &models)) {
                        sn_fetch_begin(&fetch);
                        hydrating = 1;
                        uart_puts("[SWARM] The core is up; the swarm discs fill in the "
                                  "background. Press d at any time to attach them by hand "
                                  "instead.\n");
                    } else {
                        uart_puts("  [SWARM_FETCH] no http:// gateway in /ZXV/UPSTREAM.CID\n");
                    }
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
                    if (mode == HW_SEAL_POWERDOWN_NIC) vna.up = 0;
                }
            }
            /* a disc committed: mount it, check it against its layer, and
             * its models are hashed next */
            if (fetch.fresh) {
                models_left = hw_models_index(&models, hp, &hreg) > 0;
                (void) zxv_layers_boot(&layers, &models, hp, "x86_64", "BOOTX64.EFI", l0_cid);
            }
        }
        if (vna.up) {
            static uint64_t owed0, owed1; /* $YHVH earned, not yet posted   */
            vna_host_pump(&vna, x86_ms(0));
            {
                /* The voucher bridge: whatever the node's book earned since
                 * the last pass splits 50/50 into the two card accounts.
                 * The odd unit goes to card 0. A refused issue (a card at
                 * the u32 counter bound) stays owed and is retried, so the
                 * book's earned total and the ledger never diverge. */
                uint64_t d = vna_host_take_earned(&vna);
                if (d) {
                    owed0 += d / 2 + (d & 1u);
                    owed1 += d / 2;
                }
                if (owed0 && vino_issue(vino, "BSC-0-1870892d", ASSET_TOKEN, owed0,
                                        "$YHVH seed float gen0") >= 0)
                    owed0 = 0;
                if (owed1 && vino_issue(vino, "BSC-1-8b58e9d4", ASSET_TOKEN, owed1,
                                        "$YHVH seed float gen0") >= 0)
                    owed1 = 0;
            }
            if (x86_ms(0) >= vna_report_next) {
                vna_report_next = x86_ms(0) + 60000u;
                vna_host_report(&vna);
                if (vino_chain_verify(vino, 0) != VINO_CHAIN_OK)
                    uart_puts("  [VINO] chain verify FAILED\n");
            }
        }
        if (models_left)
            models_left = hw_models_step(&models, hp, 256);
        else if (!hydrating || x86_ms(0) < fetch.next_try) /* idle, or waiting for peers */
            hlt();
        x86_heartbeat(0);
    }
}
