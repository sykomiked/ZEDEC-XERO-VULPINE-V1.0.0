/* kernel_main_arm.c — ARM kernel boot driver for VOVINA SHAKINA
 * Called by boot.s after QEMU virt machine loads the kernel.
 * Same M5 axiomatic boot sequence as x86, using ARM hardware drivers.
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#include "../include/m5_types.h"
#include "arch.h"
#include "uart.h"
#include "gic.h"
#include "timer_arm.h"
#include "registers.h"

/* M5 subsystem headers (shared with x86) */
#include "../src/oseq/oseq_core.h"
#include "../src/rmag/rmag_core.h"
#include "../src/lpres/lpres_core.h"
#include "../src/iphase/iphase_core.h"
#include "../src/choice/choice_core.h"
#include "../src/phase_coord/phase_coordinator.h"
#include "../src/telemetry/telemetry_core.h"
#include "../src/axiom_matrix/axiom_matrix_core.h"
#include "../src/crit168/crit_168_word.h"

/* Shared subsystem headers (architecture-independent) */
#include "../src/mm/mm.h"
#include "../src/sched/sched.h"
#include "../src/vfs/vfs.h"
#include "../src/net/net.h"
#include "../src/net/m5route.h"
#include "../src/net/dtmf.h"
#include "../src/net/radio.h"
#include "../src/vino/vino.h"
#include "../src/vena/vena.h"
#include "../src/holographic/holo.h"

/* Stub for syscall — ARM uses SVC, not int 0x80 */
#include "../src/syscall/syscall.h"

static int strlen_local(const char *s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

static void itoa_local(uint64_t val, char *buf) {
    if (val == 0) { buf[0] = '0'; buf[1] = 0; return; }
    char tmp[20];
    int i = 0;
    while (val > 0) { tmp[i++] = '0' + (val % 10); val /= 10; }
    for (int j = 0; j < i; j++) buf[j] = tmp[i - 1 - j];
    buf[i] = 0;
}

static void boot_msg(const char *msg) {
    uart_puts(msg);
    uart_puts("\n");
}

/* Stub keyboard/mouse state for shared code compatibility */
typedef struct {
    uint8_t buffer[256];
    uint32_t buf_head, buf_tail, buf_count;
    int shift, ctrl, alt;
} arm_keyboard_state_t;

static arm_keyboard_state_t kb_state;
static mouse_state_t mouse_state_dummy;

arm_keyboard_state_t *keyboard_get_state_arm(void) { return &kb_state; }

/* Weak stubs for GUI/init — not available on ARM yet */
void gui_init(void *a, void *b) { (void)a; (void)b; }
void gui_render(void *a) { (void)a; }
void gui_handle_mouse(void *a, void *b) { (void)a; (void)b; }
void gui_handle_keyboard(void *a, void *b) { (void)a; (void)b; }

void kernel_main(uint32_t magic, void *mbi) {
    (void)magic;
    (void)mbi;

    /* Phase 1: UART output (replaces VGA framebuffer) */
    uart_init();
    uart_puts("ZEDEC pqOS — M5 Axiomatic Kernel (VOVINA SHAKINA) [ARM]\n");
    uart_puts("==================================================\n\n");

    /* Phase 2: M5 Kernel subsystems (identical to x86) */
    uart_puts("[BOOT] M5 Kernel subsystems...\n");
    phase_tick_t tick;
    phase_coordinator_init(&tick, EXEC_DC);
    uart_puts("  [OK] Phase Coordinator (EXEC_DC)\n");
    rmag_init(256);
    uart_puts("  [OK] RMAG (256 slots)\n");
    lpres_init();
    uart_puts("  [OK] LPRES\n");
    iphase_init();
    uart_puts("  [OK] IPHASE\n");
    choice_handoff();
    uart_puts("  [OK] CHOICE\n");
    oseq_state_t oseq;
    oseq_init(&oseq);
    oseq_register_device(&oseq, "core");
    uart_puts("  [OK] OSEQ\n");

    for (int i = 0; i < 5; i++) {
        phase_coordinator_tick(&tick);
        uart_puts("  cycle ");
        char buf[20];
        itoa_local((uint64_t)(i + 1), buf);
        uart_puts(buf);
        uart_puts(": omega=");
        itoa_local(tick.omega, buf);
        uart_puts(buf);
        uart_puts("\n");
    }

    /* Phase 3: ARM hardware drivers */
    uart_puts("\n[BOOT] ARM hardware drivers...\n");

    /* Initialize GIC (replaces x86 PIC) */
    gic_init();
    uart_puts("  [OK] GIC (Generic Interrupt Controller v2)\n");

    /* Initialize ARM generic timer (replaces x86 PIT) */
    timer_init(TIMER_DEFAULT_HZ);
    arm_irq_register(IRQ_TIMER, timer_interrupt_handler);
    uart_puts("  [OK] ARM Generic Timer (100 Hz)\n");

    /* Enable interrupts (replaces x86 sti) */
    arch_enable_interrupts();
    uart_puts("  [OK] Interrupts enabled\n");

    /* Phase 5: Memory Management (shared code) */
    uart_puts("\n[BOOT] Memory management...\n");
    static mm_state_t mm;
    mm_init(&mm);
    uart_puts("  [OK] Paging + frame allocator + heap\n");

    /* Phase 6: Process Scheduler (shared code, M5-weighted) */
    uart_puts("[BOOT] Process scheduler...\n");
    static scheduler_t sched;
    sched_init(&sched);
    uart_puts("  [OK] Round-robin scheduler (M5 weighted)\n");

    /* Phase 7: Syscall Interface (stub — ARM uses SVC) */
    uart_puts("[BOOT] Syscall interface...\n");
    static syscall_table_t syscall_table;
    syscall_init(&syscall_table);
    uart_puts("  [OK] SVC dispatch table\n");

    /* Phase 8: VFS (shared code) */
    uart_puts("[BOOT] Virtual filesystem...\n");
    static vfs_state_t vfs;
    vfs_init(&vfs);
    uart_puts("  [OK] VFS layer over FAT32\n");

    /* Phase 9: Network Stack + M5 Omni-Router (shared code) */
    uart_puts("[BOOT] Network stack + M5 Omni-Router...\n");
    static net_state_t net;
    net_init(&net);
    static m5_router_t router;
    m5_address_t local_addr;
    local_addr.proto = M5_PROTO_NATIVE;
    local_addr.addr[0] = 'Z'; local_addr.addr[1] = 'E'; local_addr.addr[2] = 'D';
    local_addr.addr[3] = 'E'; local_addr.addr[4] = 'C'; local_addr.addr[5] = 0;
    m5_router_init(&router, &local_addr);
    uart_puts("  [OK] Ethernet/ARP/IP/ICMP/TCP/UDP\n");
    uart_puts("  [OK] M5 Omni-Router (20 protocol adapters)\n");

    /* Register protocol adapters (shared code) */
    static cell_modem_t cell;
    cell_init(&cell, CELL_GEN_5G);
    m5_adapter_register_cellular(&router, &cell);

    static satellite_link_t sat_geo;
    sat_init(&sat_geo, SAT_TYPE_GEO, 1);
    m5_adapter_register_satellite(&router, &sat_geo);

    static starlink_terminal_t starlink;
    starlink_init(&starlink);
    m5_adapter_register_starlink(&router, &starlink);

    static radio_interface_t radio_am, radio_fm;
    radio_init(&radio_am, RADIO_AM, 530000);
    radio_init(&radio_fm, RADIO_FM, 91700000);
    m5_adapter_register_radio(&router, &radio_am);
    m5_adapter_register_radio(&router, &radio_fm);

    static quantum_link_t quantum;
    quantum_init(&quantum);
    m5_adapter_register_quantum(&router, &quantum);

    static laser_link_t laser;
    laser_init(&laser, 1550);
    m5_adapter_register_laser(&router, &laser);

    static neutrino_link_t neutrino;
    neutrino_init(&neutrino);
    m5_adapter_register_neutrino(&router, &neutrino);

    dtmf_init();
    uart_puts("  [OK] DTMF + Morse + RTTY + SSTV + PSK31 + AX.25/APRS\n");
    uart_puts("  [OK] Cellular(5G) + Satellite(GEO/LEO) + AM/FM Radio\n");
    uart_puts("  [OK] Quantum + Laser + Neutrino (futuristic)\n");

    /* Phase 10: Vino Decentralized Bank Node (shared code) */
    uart_puts("[BOOT] Vino decentralized bank node...\n");
    static vino_ledger_t vino;
    vino_init(&vino, 1);
    vino_create_account(&vino, "ZEDEC:node:0001", "Genesis Account");
    vino_set_validator(&vino, true, 1000000);
    uart_puts("  [OK] Triple ledger (primary + balance + audit)\n");
    uart_puts("  [OK] Nine forms of capital\n");
    uart_puts("  [OK] ISO 20022 + CAMT.053 + SWIFT MT103/MX\n");
    uart_puts("  [OK] CIPS + SPFS + Visa + MasterCard + Hormung + EVC\n");
    uart_puts("  [OK] Blockchain adapters (BTC/ETH/all families)\n");
    uart_puts("  [OK] Asset classes: equities, commodities, options, futures, bonds, forex\n");

    /* Phase 11: Vena Application Runtime (shared code) */
    uart_puts("[BOOT] Vena application runtime...\n");
    static vena_runtime_t vena;
    vena_init(&vena, &vino);
    vena_set_language(&vena, LANG_M5_AXIOMATIC);
    uart_puts("  [OK] M5 Axiomatic app loader/executor\n");
    uart_puts("  [OK] 31 languages (including obscure/constructed)\n");
    uart_puts("  [OK] Smart contracts + oracles\n");

    /* Phase 13: Holographic Data System (shared code) */
    uart_puts("[BOOT] Holographic data system...\n");
    static holo_ctx_t holo;
    holo_init(&holo);
    static holo_positive_t holo_pos;
    static holo_negative_t holo_neg;
    static holo_dataset_t holo_ds;
    static holo_renderer_t holo_rend;
    static holo_container_t holo_cont;
    holo_positive_create(&holo_pos, 1, 1, 255, 44000, HOLO_ENC_INTERFERENCE, HOLO_DIM_3D);
    holo_negative_create(&holo_neg, 1, 1, 255, 180, HOLO_ENC_INTERFERENCE, HOLO_DIM_3D);
    holo_dataset_create(&holo_ds, 1, "ZEDEC:holo:genesis");
    holo_dataset_add_pair(&holo_ds, 1, 1, 0);
    holo_renderer_create(&holo_rend, 1, HOLO_RENDER_HOLOGRAM, HOLO_BLEND_HOLOGRAPHIC, 1920, 1080, 1);
    holo_container_create(&holo_cont, 1, "ZEDEC:holo:boot", "ZEDEC:node:0001");
    holo_container_add_36n9(&holo_cont, "genesis.36n9", 0, 0);
    holo_container_add_9n63(&holo_cont, "genesis.9n63", 0, 0);
    holo_container_add_36m9(&holo_cont, "genesis.36m9", 0, 0);
    holo_container_add_zedei(&holo_cont, "genesis.zedei", 0, 0);
    uart_puts("  [OK] .36n9 positive space + .9n63 negative space\n");
    uart_puts("  [OK] .36m9 dataset + .zedei renderer + .zedec container\n");
    uart_puts("  [OK] 5 holographic file types registered\n");

    /* Phase 14: Main event loop (same M5 tick as x86) */
    uart_puts("\n[BOOT] ZEDEC pqOS — All systems online.\n");
    uart_puts("[BOOT] Entering event cycle...\n\n");

    uint32_t cycle = 0;
    while (1) {
        arch_halt();  /* WFI — wait for interrupt (replaces x86 hlt) */

        /* M5 Phase Coordinator: drive the axiomatic tick every cycle.
         * This updates omega (ordinal), r (rational), ell (trit),
         * iphi (phase), chi (collapse) — feeding all subsystems. */
        phase_coordinator_tick(&tick);

        /* Poll network adapters via M5 omni-router */
        net_poll(&net);
        m5_router_poll(&router, 0);

        /* Scheduler tick — uses RMAG + LPRES coverage from coordinator */
        sched_tick(&sched);

        /* Vino ledger maintenance — RMAG quotas updated by coordinator */
        if (cycle % 100 == 0 && vino.is_validator)
            vino_propose_block(&vino);

        /* Telemetry self-observation — feeds back into CHOICE */
        if (cycle % 50 == 0) {
            static axiom_matrix_t axiom_mat;
            telemetry_t tel = emit_and_observe(&axiom_mat, tick.omega);
            choice_resolve_from_telemetry(&tel, 0);
        }

        /* Periodic status output */
        if (cycle % 200 == 0) {
            uart_puts("[TICK] cycle=");
            char buf[20];
            itoa_local(cycle, buf);
            uart_puts(buf);
            uart_puts(" omega=");
            itoa_local(tick.omega, buf);
            uart_puts(buf);
            uart_puts(" mm_used=");
            rational_t used = mm_get_used_rational();
            itoa_local((uint64_t)used.num, buf);
            uart_puts(buf);
            uart_puts("/");
            itoa_local((uint64_t)used.den, buf);
            uart_puts(buf);
            uart_puts("\n");
        }

        cycle++;
    }
}
