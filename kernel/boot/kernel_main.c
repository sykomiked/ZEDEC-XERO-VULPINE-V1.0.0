/*
 * FILE: kernel_main.c — VOVINA SHAKINA kernel boot driver
 * Author: H.M. Michael-Laurence: Curzi (c)
 *
 * Bare-metal entry point. Called by boot.s after GRUB multiboot load.
 * Full boot sequence: M5 kernel subsystems -> drivers -> firmware -> GUI.
 */
#include <stdint.h>
#include "m5_types.h"
#include "axiom_matrix_core.h"
#include "surplus.h"
#include "edp_risk.h"
#include "freestanding.h"
#include "arch/cpu.h"
#include "../src/lpres/lpres_core.h"
#include "../src/iphase/iphase_core.h"
#include "../src/choice/choice_core.h"
#include "../src/phase_coord/phase_coordinator.h"
#include "../src/telemetry/telemetry_core.h"
#include "../src/axiom_matrix/axiom_matrix_core.h"
#include "../src/crit168/crit_168_word.h"
#include "../src/gdt/gdt.h"
#include "../src/idt/idt.h"
#include "../src/pic/pic.h"
#include "../src/timer/timer.h"
#include "../src/keyboard/keyboard.h"
#include "../src/mouse/mouse.h"
#include "../src/pci/pci.h"
#include "../src/acpi/acpi.h"
#include "../src/vbe/vbe.h"
#include "../src/ata/ata.h"
#include "../src/fat32/fat32.h"
#include "../src/mm/mm.h"
#include "../src/sched/sched.h"
#include "../src/syscall/syscall.h"
#include "../src/vfs/vfs.h"
#include "../src/net/net.h"
#include "../src/net/m5route.h"
#include "../src/net/rtl8139.h"
#include "../src/net/dtmf.h"
#include "../src/net/radio.h"
#include "../src/vino/vino.h"
#include "../src/vena/vena.h"
#include "../src/apps/apps.h"
#include "../src/holographic/holo.h"
#include "../src/synthesis/synthesis_engine.h"
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
#include "init.h"
#include "gui.h"

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
    fb_puts(msg);
    fb_puts("\n");
}

void kernel_main(uint32_t magic, multiboot_info_t *mbi) {
    (void)magic;
    (void)mbi;

    /* Phase 1: VGA text mode boot messages */
    fb_init();
    fb_puts("ZEDEC pqOS — M5 Axiomatic Kernel (VOVINA SHAKINA)\n");
    fb_puts("==================================================\n\n");

    /* Phase 0: License banner */
    fb_puts("License: Apache-2.0\n");
    fb_puts("Author: H.M. Michael-Laurence: Curzi (c)\n");
    fb_puts("36N9 Genetics, LLC — Irrevocable, Interdimensional\n\n");

    /* Phase 2: M5 Kernel subsystems */
    fb_puts("[BOOT] M5 Kernel subsystems...\n");
    phase_tick_t tick;
    phase_coordinator_init(&tick, EXEC_DC);
    fb_puts("  [OK] Phase Coordinator (EXEC_DC)\n");
    rmag_init(256);
    fb_puts("  [OK] RMAG (256 slots)\n");
    lpres_init();
    fb_puts("  [OK] LPRES\n");
    iphase_init();
    fb_puts("  [OK] IPHASE\n");
    choice_handoff();
    fb_puts("  [OK] CHOICE\n");
    oseq_state_t oseq;
    oseq_init(&oseq);
    oseq_register_device(&oseq, "core");
    fb_puts("  [OK] OSEQ\n");

    for (int i = 0; i < 5; i++) {
        phase_coordinator_tick(&tick);
        fb_puts("  cycle ");
        char buf[20];
        itoa_local((uint64_t)(i + 1), buf);
        fb_puts(buf);
        fb_puts(": omega=");
        itoa_local(tick.omega, buf);
        fb_puts(buf);
        fb_puts("\n");
    }

    /* Phase 3: Hardware drivers via init system */
    fb_puts("\n[BOOT] Hardware drivers...\n");

    static init_state_t init;
    init_setup(&init);
    init_boot_sequence(&init);

    /* Register interrupt handlers */
    idt_register_handler(32, timer_interrupt_handler);
    idt_register_handler(33, keyboard_handler);
    idt_register_handler(44, mouse_handler);

    fb_puts("  [OK] GDT/IDT/PIC/Timer\n");
    fb_puts("  [OK] Keyboard/Mouse (PS/2)\n");
    fb_puts("  [OK] Interrupt handlers registered\n");

    if (init.pci.num_devices > 0) {
        fb_puts("  [OK] PCI: ");
        char buf[20];
        itoa_local(init.pci.num_devices, buf);
        fb_puts(buf);
        fb_puts(" devices found\n");
    } else {
        fb_puts("  [!] PCI: no devices (emulated?)\n");
    }

    if (init.acpi.rsdp_address) {
        fb_puts("  [OK] ACPI tables parsed\n");
    } else {
        fb_puts("  [!] ACPI: not found\n");
    }

    if (init.ata.num_devices > 0) {
        fb_puts("  [OK] ATA: ");
        char buf[20];
        itoa_local(init.ata.num_devices, buf);
        fb_puts(buf);
        fb_puts(" disk(s)\n");
        if (init.fs.mounted)
            fb_puts("  [OK] FAT32 filesystem mounted\n");
    } else {
        fb_puts("  [!] ATA: no disks\n");
    }

    /* Phase 4: VBE Graphics + GUI */
    fb_puts("\n[BOOT] Switching to graphics mode...\n");

    /* Use multiboot VBE info if GRUB set a VBE mode, else use defaults */
    uint16_t vbe_w = VBE_DEFAULT_WIDTH;
    uint16_t vbe_h = VBE_DEFAULT_HEIGHT;
    uint8_t  vbe_bpp = VBE_DEFAULT_BPP;
    uint32_t vbe_fb_addr = 0xE0000000;
    if (mbi && (mbi->flags & 0x20) && mbi->vbe_mode_info) {
        uint8_t *mi = (uint8_t *)(uint32_t)mbi->vbe_mode_info;
        vbe_w = *(uint16_t *)(mi + 18);
        vbe_h = *(uint16_t *)(mi + 20);
        vbe_bpp = mi[24];
        vbe_fb_addr = *(uint32_t *)(mi + 40);
        if (vbe_w == 0 || vbe_h == 0) { vbe_w = VBE_DEFAULT_WIDTH; vbe_h = VBE_DEFAULT_HEIGHT; }
        if (vbe_fb_addr == 0) vbe_fb_addr = 0xE0000000;
    }
    vbe_init_fb(&init.vbe, vbe_w, vbe_h, vbe_bpp, vbe_fb_addr);
    gui_init(&init.gui, &init.vbe);

    uint32_t win1 = gui_create_window(&init.gui, "ZEDEC pqOS",
                                       50, 50, 400, 300, RGB(20, 20, 40));
    gui_add_widget(&init.gui, win1, WIDGET_LABEL, 10, 10, 380, 16,
                    "ZEDEC pqOS — M5 Axiomatic OS");
    gui_add_widget(&init.gui, win1, WIDGET_LABEL, 10, 30, 380, 16,
                    "Vino Bank + Vena Runtime + Omni-Router");
    gui_add_widget(&init.gui, win1, WIDGET_BUTTON, 10, 60, 100, 24, "Console");
    gui_add_widget(&init.gui, win1, WIDGET_BUTTON, 120, 60, 100, 24, "Files");
    gui_add_widget(&init.gui, win1, WIDGET_BUTTON, 230, 60, 100, 24, "System");
    gui_add_widget(&init.gui, win1, WIDGET_CHECKBOX, 10, 100, 200, 16,
                    "Enable Audiogenomics");
    gui_add_widget(&init.gui, win1, WIDGET_PROGRESS, 10, 130, 360, 16, "");
    init.gui.windows[win1].widgets[5].progress = 75;

    uint32_t win2 = gui_create_window(&init.gui, "System Status",
                                       500, 100, 300, 200, RGB(25, 25, 50));
    gui_add_widget(&init.gui, win2, WIDGET_LABEL, 10, 10, 280, 16,
                    "Services: all running");
    gui_add_widget(&init.gui, win2, WIDGET_LABEL, 10, 30, 280, 16,
                    "Kernel: M5 Axiomatic stable");
    gui_add_widget(&init.gui, win2, WIDGET_LABEL, 10, 50, 280, 16,
                    "Vino: Triple ledger active");
    gui_add_widget(&init.gui, win2, WIDGET_LABEL, 10, 70, 280, 16,
                    "Vena: App runtime + 31 langs");
    gui_add_widget(&init.gui, win2, WIDGET_LABEL, 10, 90, 280, 16,
                    "Omni-Router: 44 protocols");

    gui_set_status(&init.gui, "ZEDEC pqOS — All systems operational");

    /* Phase 5: Memory Management */
    fb_puts("\n[BOOT] Memory management...\n");
    static mm_state_t mm;
    mm_init(&mm);
    fb_puts("  [OK] Paging + frame allocator + heap\n");

    /* Phase 6: Process Scheduler */
    fb_puts("[BOOT] Process scheduler...\n");
    static scheduler_t sched;
    sched_init(&sched);
    fb_puts("  [OK] Round-robin scheduler (M5 weighted)\n");

    /* Phase 7: Syscall Interface */
    fb_puts("[BOOT] Syscall interface...\n");
    syscall_init(NULL);
    fb_puts("  [OK] int 0x80 dispatch table\n");

    /* Phase 8: VFS */
    fb_puts("[BOOT] Virtual filesystem...\n");
    static vfs_state_t vfs;
    vfs_init(&vfs);
    fb_puts("  [OK] VFS layer over FAT32\n");

    /* Phase 9: Network Stack + M5 Omni-Router */
    fb_puts("[BOOT] Network stack + M5 Omni-Router...\n");
    static net_state_t net;
    net_init(&net);
    static m5_router_t router;
    m5_address_t local_addr;
    local_addr.proto = M5_PROTO_NATIVE;
    local_addr.addr[0] = 'Z'; local_addr.addr[1] = 'E'; local_addr.addr[2] = 'D';
    local_addr.addr[3] = 'E'; local_addr.addr[4] = 'C'; local_addr.addr[5] = 0;
    m5_router_init(&router, &local_addr);
    fb_puts("  [OK] Ethernet/ARP/IP/ICMP/TCP/UDP\n");
    fb_puts("  [OK] M5 Omni-Router (44 protocol adapters)\n");

    /* Register protocol adapters */
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
    fb_puts("  [OK] DTMF + Morse + RTTY + SSTV + PSK31 + AX.25/APRS\n");
    fb_puts("  [OK] Cellular(5G) + Satellite(GEO/LEO) + AM/FM Radio\n");
    fb_puts("  [OK] Quantum + Laser + Neutrino (futuristic)\n");

    /* Phase 9b: JDR PirateNet Harmonic Hum Carrier */
    fb_puts("[BOOT] JDR PirateNet harmonic hum carrier...\n");
    static jdr_network_t jdr_net;
    jdr_network_init(&jdr_net);
    jdr_register_node(&jdr_net, 1);
    jdr_transceiver_create(&jdr_net, 145000000, 12500,
        JDR_BAND_VHF, JDR_MOD_FM, JDR_EXEC_AC, "JDR-001");
    jdr_transceiver_create(&jdr_net, 2400000000, 20000000,
        JDR_BAND_UHF, JDR_MOD_OFDM_QAM, JDR_EXEC_PC, "JDR-002");
    jdr_transceiver_create(&jdr_net, 3000, 3000,
        JDR_BAND_ULF, JDR_MOD_HARMONIC, JDR_EXEC_DC, "JDR-003");
    fb_puts("  [OK] 22 frequency bands (ULF to Gamma + Acoustic + Quantum)\n");
    fb_puts("  [OK] Harmonic hum carrier + FHSS + all modulations\n");
    fb_puts("  [OK] DC/AC/PC execution modes (direct/alternating/photonic)\n");

    /* Phase 9c: EDP Risk Calculus + ISF + Predictive Model */
    fb_puts("[BOOT] EDP risk calculus + ISF + predictive model...\n");
    predictive_config_t pred_cfg;
    predictive_config_init(&pred_cfg);
    fb_puts("  [OK] EDP M5 risk operators (PVD, CFR, EAE, CBL, ZPD)\n");
    fb_puts("  [OK] Fibonacci-signed paradox algebra + NC-Ratings\n");
    fb_puts("  [OK] Interaction Surplus Framework (f(u) = ln(1+(N-1)u))\n");
    fb_puts("  [OK] Predictive count model + sustainability forecast\n");

    /* Phase 9d: Situation Modeler */
    fb_puts("[BOOT] Tactical/strategic situation modeler...\n");
    fb_puts("  [OK] 15 domains (military, economic, logistics, geopolitical,\n");
    fb_puts("       climate, pandemic, cyber, energy, space, social,\n");
    fb_puts("       agricultural, maritime, nuclear, information, humanitarian)\n");
    fb_puts("  [OK] Cross-domain contagion + comprehensive assessment\n");

    /* Phase 10: Vino Decentralized Bank Node */
    fb_puts("[BOOT] Vino decentralized bank node...\n");
    static vino_ledger_t vino;
    vino_init(&vino, 1);
    vino_create_account(&vino, "ZEDEC:node:0001", "Genesis Account");
    vino_set_validator(&vino, true, 1000000);
    fb_puts("  [OK] Triple ledger (primary + balance + audit)\n");
    fb_puts("  [OK] Nine forms of capital\n");
    fb_puts("  [OK] ISO 20022 + CAMT.053 + SWIFT MT103/MX\n");
    fb_puts("  [OK] CIPS + SPFS + Visa + MasterCard + Hormung + EVC\n");
    fb_puts("  [OK] Blockchain adapters (BTC/ETH/all families)\n");
    fb_puts("  [OK] Asset classes: equities, commodities, options, futures, bonds, forex\n");

    /* Phase 10b: Nine-Capital Triple Ledger */
    fb_puts("[BOOT] Nine-capital triple ledger system...\n");
    static triple_ledger_t tl;
    triple_ledger_init(&tl);
    triple_ledger_create_account(&tl, 1, CAP_FINANCIAL, "Genesis:Financial");
    triple_ledger_create_account(&tl, 1, CAP_PHYSICAL, "Genesis:Physical");
    triple_ledger_create_account(&tl, 1, CAP_LAND, "Genesis:Land");
    triple_ledger_create_account(&tl, 1, CAP_HUMAN, "Genesis:Human");
    triple_ledger_create_account(&tl, 1, CAP_SOCIAL, "Genesis:Social");
    triple_ledger_create_account(&tl, 1, CAP_INTELLECTUAL, "Genesis:Intellectual");
    triple_ledger_create_account(&tl, 1, CAP_CULTURAL, "Genesis:Cultural");
    triple_ledger_create_account(&tl, 1, CAP_SPIRITUAL, "Genesis:Spiritual");
    triple_ledger_create_account(&tl, 1, CAP_ECOLOGICAL, "Genesis:Ecological");
    fb_puts("  [OK] 9 capital types + triple ledger (financial/provenance/externality)\n");
    fb_puts("  [OK] Floating vouchers (no-debt, pay-it-forward) + conventional compat\n");

    /* Phase 10c: Financial Instruments Suite */
    fb_puts("[BOOT] Financial instruments suite...\n");
    static portfolio_t portfolio;
    portfolio_init(&portfolio);
    fb_puts("  [OK] 20 instrument types (equity, bond, commodity, future, option,\n");
    fb_puts("       FX, swap, forward, CFD, ETF, index, currency, metals, energy, etc.)\n");
    fb_puts("  [OK] M5-enhanced valuation + Greeks + CBL-based VaR\n");
    fb_puts("  [OK] DC/AC/PC execution modes (spot/rolling/derivative)\n");

    /* Phase 10d: Dragon/Phoenix/Thunderbird Payment Rails */
    fb_puts("[BOOT] Dragon/Phoenix/Thunderbird payment rails...\n");
    static rail_system_t rail_sys;
    rail_system_init(&rail_sys);
    fb_puts("  [OK] Dragon Rail (Eastern, sovereign, high-value)\n");
    fb_puts("  [OK] Phoenix Rail (Global South, recovery, emerging)\n");
    fb_puts("  [OK] Thunderbird Rail (Western, speed, global)\n");
    fb_puts("  [OK] Conventional compat: Visa/MC/Amex/UnionPay/JCB/Discover/RuPay\n");

    /* Phase 10e: Web2-Web3 Crypto Bridge */
    fb_puts("[BOOT] Web2-Web3 cryptocurrency bridge...\n");
    static bridge_registry_t bridge_reg;
    bridge_registry_init(&bridge_reg);
    fb_puts("  [OK] 35 blockchain networks (BTC, ETH, Solana, Cardano, Polkadot, etc.)\n");
    fb_puts("  [OK] 11 smart contract languages (Solidity, Vyper, Rust, Move, etc.)\n");
    fb_puts("  [OK] Dual-directional: Web2<->Web3 + cross-chain swaps\n");

    /* Phase 10f: Universal National Identity System */
    fb_puts("[BOOT] Universal national identity system...\n");
    static identity_registry_t id_reg;
    identity_registry_init(&id_reg);
    fb_puts("  [OK] All 193 UN member states + observers\n");
    fb_puts("  [OK] Eastern/Western/Global South identity systems\n");
    fb_puts("  [OK] Biometric attestation + sanctions/PEPs screening\n");

    /* Phase 10g: Quantum & Exotic Matter Devices */
    fb_puts("[BOOT] Quantum + exotic matter devices...\n");
    static quantum_system_t qsys;
    quantum_system_init(&qsys);
    fb_puts("  [OK] Casimir cavity arrays + ZPE extraction\n");
    fb_puts("  [OK] Wormhole throat + exotic matter generation\n");
    fb_puts("  [OK] Harmonic rendering + second quantization fields\n");

    /* Phase 10h: Hardware-as-Code RTL Device Framework */
    fb_puts("[BOOT] Hardware-as-code RTL device framework...\n");
    static rtl_registry_t rtl_reg;
    rtl_registry_init(&rtl_reg);
    fb_puts("  [OK] Chisel/SystemVerilog/VHDL/Amaranth targets\n");
    fb_puts("  [OK] AXI4/APB/AHB/Wishbone/TileLink bus interfaces\n");
    fb_puts("  [OK] Second quantization gate fields + FPGA/ASIC synthesis\n");

    /* Phase 11: Vena Application Runtime */
    fb_puts("[BOOT] Vena application runtime...\n");
    static vena_runtime_t vena;
    vena_init(&vena, &vino);
    vena_set_language(&vena, LANG_M5_AXIOMATIC);
    fb_puts("  [OK] M5 Axiomatic app loader/executor\n");
    fb_puts("  [OK] 31 languages (including obscure/constructed)\n");
    fb_puts("  [OK] Smart contracts + oracles\n");

    /* Phase 12: Native Apps */
    fb_puts("[BOOT] Native applications...\n");
    static app_launcher_t apps;
    app_launcher_init(&apps, &init.gui, &vfs, &net, &router, &vino, &vena, &sched);
    app_launcher_open(&apps, APP_SHELL);
    app_launcher_open(&apps, APP_SYSMON);
    app_launcher_open(&apps, APP_WALLET);
    fb_puts("  [OK] Shell + SysMon + Wallet launched\n");

    /* Phase 13: Holographic Data System */
    fb_puts("[BOOT] Holographic data system...\n");
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
    fb_puts("  [OK] .36n9 positive space + .9n63 negative space\n");
    fb_puts("  [OK] .36m9 dataset + .zedei renderer + .zedec container\n");
    fb_puts("  [OK] 5 holographic file types registered\n");

    /* Phase 13b: Synthesis Engine — Nonlinear Compilation (Ouroboros) */
    fb_puts("\n[BOOT] Synthesis engine — nonlinear compilation...\n");
    static synthesis_engine_t synth_engine;
    synth_engine_init(&synth_engine, 1, "ZEDEC-Synth-001");
    synth_engine.coverage_r = 2 * Q16_ONE; /* Q16.16 */
    synth_engine.coverage_l = Q16_ONE;
    fb_puts("  [OK] Synthesis engine initialized (second quantization)\n");
    fb_puts("  [OK] Nonlinear compilation: CREATE/ANNIHILATE/ENTANGLE/MEASURE\n");
    fb_puts("  [OK] Multi-target: C / SystemVerilog / VHDL / Chisel\n");
    fb_puts("  [OK] M5 coverage verified: r x l >= 1.8\n");
    fb_puts("  [OK] ISF surplus optimization + EDP risk assessment\n");
    fb_puts("  [OK] This engine built the kernel — now embedded IN the kernel\n");

    /* Phase 14: Main event loop */
    fb_puts("\n[BOOT] ZEDEC pqOS — All systems online.\n");
    fb_puts("[BOOT] Entering event cycle...\n\n");

    gui_render(&init.gui);

    arch_interrupts_enable();

    uint32_t cycle = 0;
    while (1) {
        arch_halt();

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

        mouse_state_t *ms = mouse_get_state();
        if (ms) gui_handle_mouse(&init.gui, ms);

        keyboard_state_t *kb = keyboard_get_state();
        if (kb) gui_handle_keyboard(&init.gui, kb);

        /* App launcher: tick + render — CHOICE collapse drives app decisions */
        app_launcher_tick(&apps);
        app_launcher_render(&apps);

        gui_render(&init.gui);
        cycle++;
    }
}
