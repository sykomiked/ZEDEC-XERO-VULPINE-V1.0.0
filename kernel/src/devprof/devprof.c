/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* devprof.c — the device catalogue and the OS support table (devprof.h).
 *
 * Sources: hardware/docs/oem/{OEM_CONSUMER,OEM_INSTITUTIONAL,PLATFORM_COMMON,
 * OS_HARDWARE_INTERFACE}.md, servers/{SERVER_COMMON,SERVER_CIVILIAN,
 * SERVER_INSTITUTIONAL}.md, mobile/{MOBILE_COMMON,MOBILE_DEVICES}.md, PORTS.md.
 * Where a spec line is [CONFIRM] the number here is the spec's number; the
 * OS uses these values as defaults and envelopes, never as measurements. */
#include "devprof.h"

/* ---- D2: what the OS has, with the file that shows it ---- */
static const dp_component_t g_comp[DPC__COUNT] = {
    {DPC_X86_UEFI, "x86-uefi", "x86_64 UEFI disc boot, GOP console, COM1 log", DP_ST_BUILT,
     "kernel/arch/x86_64/efi_stub.c; build_system/uefi_boot_check.sh (QEMU OVMF)"},
    {DPC_X86_SMP, "x86-smp", "bring up every core", DP_ST_MISSING,
     "one core only; docs/BAREMETAL_OS_GAPS.md row 24 (Bare-metal OS thread)"},
    {DPC_X86_BIGMEM, "x86-bigmem", "use all of a large ECC memory", DP_ST_PARTIAL,
     "frame allocator kernel/src/pmm + handoff map kernel/arch/zxv_handoff.h; no "
     "page tables for memory beyond the boot map yet"},
    {DPC_ARM64_UEFI, "arm64-uefi", "arm64 EFI boot", DP_ST_BUILT,
     "kernel/arch/arm64/efi_stub.c; build_system/arm64_session_check.py (QEMU virt)"},
    {DPC_ARM64_SMP, "arm64-smp", "PSCI CPU_ON, every core scheduled", DP_ST_MISSING,
     "boot CPU only (kernel/arch/arm64/kernel_main_arm64.c)"},
    {DPC_BOARD_RK3588, "rk3588-board", "RK3588/RK3588S board profile", DP_ST_PARTIAL,
     "kernel/arch/arm64/board_profile.h BOARD_RK3588 (uart2, GIC-600, mainline DT addresses); "
     "builds, but QEMU has no RK3588 model, so it has never booted"},
    {DPC_RK3588_SOC, "rk3588-soc", "RK3588 clocks, power domains, pinmux, PCIe, USB", DP_ST_MISSING,
     "none; mainline Linux drivers are the reference"},
    {DPC_BOARD_ZYNQMP, "zynqmp-board", "Zynq UltraScale+ arbiter (GIC-400, Cadence UART)",
     DP_ST_BUILT,
     "board_profile.h BOARD_ZYNQMP + Cadence UART in uart_pl011.c; boots to "
     "[AI_OK] in QEMU xlnx-zcu102 (build_system/boards_boot_check.sh), not yet on silicon"},
    {DPC_BOARD_VERSAL, "versal-board", "Versal arbiter (GICv3, PL011)", DP_ST_BUILT,
     "board_profile.h BOARD_VERSAL; boots to [AI_OK] in QEMU xlnx-versal-virt "
     "(build_system/boards_boot_check.sh), not yet on silicon"},
    {DPC_BOARD_MPFS, "mpfs-board", "PolarFire SoC arbiter (MMUART, PLIC, CLINT)", DP_ST_BUILT,
     "kernel/arch/riscv/riscv_board.h RV_BOARD_MPFS; boots to [BOOT_OK] on a U54 hart in QEMU "
     "microchip-icicle-kit (build_system/boards_boot_check.sh), not yet on silicon"},
    {DPC_BOARD_K3, "k3-board", "SpacemiT K3 laptop board", DP_ST_MISSING, "no profile"},
    {DPC_HOSTED_LINUX, "hosted-linux", "zxvd hosted on Linux (x86_64, arm64, Jetson Linux)",
     DP_ST_BUILT, "kernel/arch/hosted/build_zxvd.sh; docs/DESKTOP_LINUX_WINDOWS.md"},
    {DPC_NVME, "nvme", "NVMe", DP_ST_BUILT, "kernel/src/hwdrv/nvme.c (QEMU boot check)"},
    {DPC_AHCI, "ahci", "AHCI / SATA", DP_ST_BUILT, "kernel/src/hwdrv/ahci.c (QEMU boot check)"},
    {DPC_OPAL, "opal", "TCG Opal SED unlock", DP_ST_MISSING, "no Security Send/Receive path"},
    {DPC_SAS_HBA, "sas-hba", "SAS HBA for hard-drive shelves", DP_ST_MISSING,
     "none (Broadcom 9500 class needs vendor firmware co-operation)"},
    {DPC_STRIPE, "stripe", "erasure-coded storage service", DP_ST_BUILT,
     "kernel/src/stripe: k + m members over hw_blk_t, SHA-256 per piece, header written last, "
     "scrub and rebuild, the six server layouts (make -C kernel test-stripe)"},
    {DPC_STRIPE_CLUSTER, "stripe-cluster", "stripes across servers", DP_ST_MISSING,
     "placement protocol is R&D (servers/SERVER_COMMON.md s.8)"},
    {DPC_MCA_ECC, "mca-ecc", "machine-check and ECC reporting", DP_ST_PARTIAL,
     "kernel/src/mca (decode, SMCA UMC DRAM ECC, log ring, thresholds; test_mca.c) + "
     "kernel/arch/x86_64/mca_x86.c (banks, #MC gate, poll), run in QEMU TCG with monitor-injected "
     "errors on the legacy MSR map; SMCA only in host tests (QEMU has none), not yet on EPYC "
     "silicon, no CMCI, UMC addresses not translated to system PA"},
    {DPC_XHCI_HID, "xhci-hid", "USB keyboard and storage", DP_ST_BUILT,
     "kernel/src/hwdrv/xhci.c, usbkbd.c, usbmsc.c (QEMU)"},
    {DPC_TOUCH, "touch", "touch panel and pen", DP_ST_MISSING, "no I2C-HID or USI driver"},
    {DPC_GOP_FB, "gop-fb", "one framebuffer through UEFI GOP", DP_ST_BUILT,
     "kernel/arch/x86_64/efifb_con.c"},
    {DPC_ARBITER_FB, "arbiter-fb", "three canopy panels via the arbiter", DP_ST_PARTIAL,
     "kernel/src/arbiter: three framebuffers in the mailbox BAR, arb_host_fb() and flips "
     "(test_arbiter.c); the desktop compositor does not draw into them yet"},
    {DPC_AMD_DCN, "amd-dcn", "AMD display engine (wing panels)", DP_ST_MISSING,
     "only the GOP panel works on bare metal"},
    {DPC_RK_VOP2, "rk-vop2", "RK3588 display controller + DSI/eDP", DP_ST_MISSING, "none"},
    {DPC_DLP_1080P, "dlp-1080p", "rear projector, 1080p DMD", DP_ST_PARTIAL,
     "kernel/src/hardware/dlp_projector.c: DLP230NP/DLPC3436 1080p profile (960x540 DMD shown "
     "as 1080p) [default chipset]; no DLPC I2C/video-port driver on silicon"},
    {DPC_HDA, "hda", "Intel HD Audio", DP_ST_BUILT, "kernel/src/hwdrv/hda.c (QEMU)"},
    {DPC_HANDHELD_AUDIO, "hh-audio", "handheld audio codec", DP_ST_MISSING, "none"},
    {DPC_CAMERA_ISP, "camera-isp", "camera sensors and ISP", DP_ST_MISSING, "none"},
    {DPC_NIC_1G, "nic-1g", "Intel e1000/e1000e/igb", DP_ST_BUILT,
     "kernel/src/hwdrv/e1000.c, igb.c (QEMU)"},
    {DPC_NIC_2G5, "nic-2g5", "Intel I226 (igc) / Realtek RTL8125", DP_ST_PARTIAL,
     "kernel/src/hwdrv/igc.c (I225/I226) and RTL8125 in r8169.c, tested against register "
     "fakes (test-hwdrv); QEMU models neither, not yet run on silicon"},
    {DPC_NIC_10G, "nic-10g", "Intel X710 class", DP_ST_MISSING, "none"},
    {DPC_NIC_25G, "nic-25g", "Intel E810 class 25/100 GbE", DP_ST_MISSING, "none"},
    {DPC_NIC_400G, "nic-400g", "NVIDIA ConnectX-7 class", DP_ST_MISSING,
     "none; needs vendor firmware co-operation"},
    {DPC_WIFI7, "wifi7", "Wi-Fi 7 silicon", DP_ST_PARTIAL,
     "802.11 protocol layers kernel/src/wifi; no silicon driver"},
    {DPC_BT, "bt", "Bluetooth HCI", DP_ST_PARTIAL,
     "protocol logic kernel/src/bluetooth; HCI over USB queued (Hardware drivers thread)"},
    {DPC_WWAN, "wwan", "cellular module", DP_ST_PARTIAL,
     "AT modem bearer kernel/src/bearer/br_modem.c (hosted serial); no MBIM"},
    {DPC_SAT_NTN, "sat-ntn", "satellite IoT-NTN messaging", DP_ST_MISSING, "none"},
    {DPC_IRIDIUM, "iridium", "Iridium Certus terminal", DP_ST_MISSING, "none"},
    {DPC_LORA, "lora", "LoRa bearer", DP_ST_PARTIAL,
     "framing kernel/src/bearer/br_lora.c; no LR1121 radio driver"},
    {DPC_UWB, "uwb", "UWB ranging", DP_ST_MISSING, "none"},
    {DPC_GNSS, "gnss", "GNSS + 1PPS", DP_ST_MISSING, "no receiver driver or NMEA parser"},
    {DPC_SDR, "sdr", "AD9361 SDR", DP_ST_MISSING, "none"},
    {DPC_ACPI_TABLES, "acpi-tables", "ACPI table parsing", DP_ST_BUILT, "kernel/src/acpi/acpi.c"},
    {DPC_ACPI_AML, "acpi-aml", "ACPI AML (battery, thermal, sleep)", DP_ST_PARTIAL,
     "draft PR #140 (Hardware drivers thread), not on this base"},
    {DPC_EC_BATTERY, "battery", "smart battery and embedded controller", DP_ST_MISSING, "none"},
    {DPC_POWER_TELEM, "power-telem", "power, stalls, temperatures into ct_budget", DP_ST_PARTIAL,
     "arbiter telemetry -> kernel/src/climate/climate_arb.c -> cl_step -> climate_ct.c -> "
     "ct_budget (test-climate); stall counters per core not read on silicon yet"},
    {DPC_RAPL, "rapl", "x86 RAPL energy", DP_ST_BUILT, "kernel/src/power/power_rapl.c"},
    {DPC_THERMAL_MODES, "thermal-modes", "quiet/balanced/performance/scram", DP_ST_BUILT,
     "kernel/src/climate: briefcase modes, scram, limp-home, handheld windows (test-climate)"},
    {DPC_CLIMATE, "climate", "server shell climate and start interlock", DP_ST_BUILT,
     "kernel/src/climate: cooling regimes, heaters, dew point, door rule, start interlock "
     "(test-climate)"},
    {DPC_ARBITER_MBOX, "arbiter-mbox", "arbiter mailbox", DP_ST_BUILT,
     "kernel/src/arbiter (protocol, both ends) + kernel/src/hwdrv/arbmbox.c (PCI bind); "
     "make -C kernel test-arbiter test-hwdrv"},
    {DPC_KILL_SWITCH, "kill-switch", "hardware kill switches reported", DP_ST_BUILT,
     "ARB_M_KILL_STATE over the mailbox; arb_host_powered() (kernel/src/arbiter)"},
    {DPC_TAMPER, "tamper", "tamper events and zeroisation", DP_ST_BUILT,
     "arb_dev_tamper(): keys cleared before the host is told, latched; the host wipes its "
     "copies (kernel/src/arbiter, test_arbiter.c)"},
    {DPC_SEAL, "seal", "seal display, button, secure element", DP_ST_PARTIAL,
     "seal request, display callback, button, expiry (kernel/src/arbiter); the SE051 "
     "secure-element signing is not in the tree"},
    {DPC_PQ_BOOT, "pq-boot", "arbiter-side PQ check of the boot image", DP_ST_PARTIAL,
     "MATRIX dual signature exists (kernel/src/hwemu/hwr.c); no arbiter boot gate"},
    {DPC_KEY_SPLIT, "key-split", "Shamir split of the vault key", DP_ST_BUILT,
     "kernel/src/ident (Shamir recovery)"},
    {DPC_DEVMESH, "devmesh", "device mesh roles, pairing, routing", DP_ST_BUILT,
     "kernel/src/devmesh (test_devmesh)"},
    {DPC_ROUTE_POLICY, "route-policy", "per-task where-it-runs setting", DP_ST_BUILT,
     "kernel/src/devmesh/dm_remote.c: automatic / here / home / pinned per device and task "
     "kind, wallet tasks never leave the device (test_devmesh)"},
    {DPC_ANSWER_CHECK, "answer-check", "handheld re-runs a sample of a remote answer",
     DP_ST_MISSING, "R&D (mobile/MOBILE_COMMON.md s.3.4)"},
    {DPC_COTIER, "cotier", "untrusted accelerator reconciled on the integer core", DP_ST_BUILT,
     "kernel/src/cotier"},
    {DPC_GPU_CUDA, "gpu-cuda", "CUDA backend (Thor)", DP_ST_MISSING,
     "planned after Vulkan (docs/GPU_STRATEGY.md); CPU path meanwhile"},
    {DPC_GPU_VULKAN, "gpu-vulkan", "Vulkan compute backend", DP_ST_MISSING,
     "next in docs/GPU_STRATEGY.md s.4; Vulkan is UI-only today; CPU path meanwhile"},
};

/* ---- D1/D3: the devices ---- */
#define NEEDS(...)                                                                                 \
    .n_needs = (uint8_t) (sizeof((dp_comp_t[]){__VA_ARGS__}) / sizeof(dp_comp_t)),                 \
    .needs = {__VA_ARGS__}

/* What every Pioneer needs; the Pro adds the WWAN slot and the projector. */
#define PIONEER_NEEDS                                                                              \
    DPC_X86_UEFI, DPC_X86_SMP, DPC_GOP_FB, DPC_AMD_DCN, DPC_NVME, DPC_XHCI_HID, DPC_HDA,           \
        DPC_NIC_2G5, DPC_WIFI7, DPC_BT, DPC_ACPI_TABLES, DPC_ACPI_AML, DPC_EC_BATTERY, DPC_RAPL,   \
        DPC_POWER_TELEM, DPC_THERMAL_MODES, DPC_DEVMESH, DPC_ROUTE_POLICY, DPC_GPU_VULKAN

/* Institutional briefcase common set. */
#define BRIEFCASE_INST_NEEDS                                                                       \
    DPC_X86_UEFI, DPC_X86_SMP, DPC_X86_BIGMEM, DPC_MCA_ECC, DPC_NVME, DPC_OPAL, DPC_XHCI_HID,      \
        DPC_ARBITER_FB, DPC_ARBITER_MBOX, DPC_HDA, DPC_NIC_10G, DPC_WWAN, DPC_WIFI7, DPC_BT,       \
        DPC_ACPI_TABLES, DPC_EC_BATTERY, DPC_POWER_TELEM, DPC_THERMAL_MODES, DPC_KILL_SWITCH,      \
        DPC_TAMPER, DPC_PQ_BOOT, DPC_COTIER, DPC_HOSTED_LINUX, DPC_GPU_CUDA, DPC_DLP_1080P,        \
        DPC_DEVMESH

/* Server common set. */
#define SERVER_NEEDS                                                                               \
    DPC_X86_UEFI, DPC_X86_SMP, DPC_X86_BIGMEM, DPC_MCA_ECC, DPC_NVME, DPC_OPAL, DPC_STRIPE,        \
        DPC_XHCI_HID, DPC_GOP_FB, DPC_ACPI_TABLES, DPC_RAPL, DPC_ARBITER_MBOX, DPC_BOARD_MPFS,     \
        DPC_CLIMATE, DPC_POWER_TELEM, DPC_PQ_BOOT, DPC_DEVMESH

/* Handheld common set (arbiter is a PolarFire SoC on every handheld). */
#define HANDHELD_NEEDS                                                                             \
    DPC_NVME, DPC_XHCI_HID, DPC_TOUCH, DPC_HANDHELD_AUDIO, DPC_WIFI7, DPC_BT, DPC_WWAN,            \
        DPC_SAT_NTN, DPC_LORA, DPC_UWB, DPC_GNSS, DPC_ARBITER_MBOX, DPC_BOARD_MPFS,                \
        DPC_KILL_SWITCH, DPC_SEAL, DPC_PQ_BOOT, DPC_THERMAL_MODES, DPC_POWER_TELEM,                \
        DPC_EC_BATTERY, DPC_DEVMESH, DPC_ROUTE_POLICY, DPC_ANSWER_CHECK, DPC_GPU_VULKAN

#define RK3588_NEEDS DPC_ARM64_UEFI, DPC_ARM64_SMP, DPC_BOARD_RK3588, DPC_RK3588_SOC, DPC_RK_VOP2

static const dp_device_t g_dev[] = {
    /* ===== briefcase, consumer (OEM_CONSUMER.md s.6.4 SKU ladder) ===== */
    {.slug = "pioneer-64",
     .name = "Pioneer 64",
     .model = "ZXV Pioneer 64",
     .line = DP_LINE_BRIEFCASE,
     .isa = DP_ISA_X86_64,
     .boot = DP_BOOT_UEFI,
     .cpu = "Ryzen AI Max 385 class",
     .cores = 8,
     .nodes = 1,
     .ram_gb = 64,
     .ecc = false,
     .hot_tb_x100 = 200,
     .displays = 4,
     .net = DP_NET_2G5,
     .radios = DP_RF_WIFI7 | DP_RF_BT,
     .sustained_w = 85,
     .peak_w = 120,
     .battery_wh = 98,
     .mesh_role = DP_ROLE_HOME,
     .tier_pack = "personal-2",
     NEEDS(PIONEER_NEEDS)},
    {.slug = "pioneer-128",
     .name = "Pioneer 128",
     .model = "ZXV Pioneer 128",
     .line = DP_LINE_BRIEFCASE,
     .isa = DP_ISA_X86_64,
     .boot = DP_BOOT_UEFI,
     .cpu = "Ryzen AI Max+ 395",
     .cores = 16,
     .nodes = 1,
     .ram_gb = 128,
     .ecc = false,
     .hot_tb_x100 = 400,
     .displays = 4,
     .net = DP_NET_2G5,
     .radios = DP_RF_WIFI7 | DP_RF_BT,
     .sustained_w = 85,
     .peak_w = 120,
     .battery_wh = 98,
     .mesh_role = DP_ROLE_HOME,
     .tier_pack = "personal-3",
     NEEDS(PIONEER_NEEDS)},
    {.slug = "pioneer-128-pro",
     .name = "Pioneer 128 Pro",
     .model = "ZXV Pioneer 128 Pro",
     .line = DP_LINE_BRIEFCASE,
     .isa = DP_ISA_X86_64,
     .boot = DP_BOOT_UEFI,
     .cpu = "Ryzen AI Max+ 395",
     .cores = 16,
     .nodes = 1,
     .ram_gb = 128,
     .ecc = false,
     .hot_tb_x100 = 800,
     .displays = 4,
     .net = DP_NET_2G5,
     .radios = DP_RF_WIFI7 | DP_RF_BT | DP_RF_5G,
     .sustained_w = 85,
     .peak_w = 120,
     .battery_wh = 197,
     .mesh_role = DP_ROLE_HOME,
     .tier_pack = "personal-3",
     NEEDS(PIONEER_NEEDS, DPC_WWAN, DPC_DLP_1080P)},
    /* ===== briefcase, institutional (OEM_INSTITUTIONAL.md s.3) ===== */
    {.slug = "vanguard",
     .name = "Vanguard",
     .model = "ZXV Vanguard",
     .line = DP_LINE_BRIEFCASE,
     .institutional = true,
     .isa = DP_ISA_X86_64,
     .boot = DP_BOOT_UEFI,
     .cpu = "EPYC Embedded 8224P",
     .cores = 24,
     .nodes = 1,
     .arbiter_isa = DP_ISA_RV64,
     .arbiter = "PolarFire SoC MPFS250TS",
     .neural_count = 1,
     .neural_gb_each = 64,
     .ram_gb = 192,
     .ecc = true,
     .usable_tb = 30,
     .displays = 3,
     .net = DP_NET_10G,
     .radios = DP_RF_WIFI7 | DP_RF_BT | DP_RF_5G,
     .sustained_w = 250,
     .peak_w = 340,
     .battery_wh = 197,
     .mesh_role = DP_ROLE_HOME,
     .tier_pack = "vanguard",
     NEEDS(BRIEFCASE_INST_NEEDS, DPC_BOARD_MPFS)},
    {.slug = "sovereign",
     .name = "Sovereign",
     .model = "ZXV Sovereign",
     .line = DP_LINE_BRIEFCASE,
     .institutional = true,
     .isa = DP_ISA_X86_64,
     .boot = DP_BOOT_UEFI,
     .cpu = "EPYC Embedded 8434P",
     .cores = 48,
     .nodes = 1,
     .arbiter_isa = DP_ISA_ARM64,
     .arbiter = "Zynq UltraScale+ ZU7EV",
     .neural_count = 1,
     .neural_gb_each = 128,
     .ram_gb = 384,
     .ecc = true,
     .usable_tb = 61,
     .displays = 3,
     .net = DP_NET_10G | DP_NET_25G,
     .radios = DP_RF_WIFI7 | DP_RF_BT | DP_RF_5G | DP_RF_SDR,
     .sustained_w = 330,
     .peak_w = 450,
     .battery_wh = 197,
     .mesh_role = DP_ROLE_HOME,
     .tier_pack = "sovereign",
     NEEDS(BRIEFCASE_INST_NEEDS, DPC_BOARD_ZYNQMP, DPC_NIC_25G, DPC_SDR)},
    {.slug = "citadel",
     .name = "Citadel",
     .model = "ZXV Citadel",
     .line = DP_LINE_BRIEFCASE,
     .institutional = true,
     .isa = DP_ISA_X86_64,
     .boot = DP_BOOT_UEFI,
     .cpu = "EPYC Embedded 8534P",
     .cores = 64,
     .nodes = 1,
     .arbiter_isa = DP_ISA_ARM64,
     .arbiter = "Versal Prime VM1802",
     .neural_count = 2,
     .neural_gb_each = 128,
     .ram_gb = 1152,
     .ecc = true,
     .usable_tb = 122,
     .code_k = 1,
     .code_m = 1,
     .displays = 3,
     .net = DP_NET_10G | DP_NET_25G,
     .radios = DP_RF_WIFI7 | DP_RF_BT | DP_RF_5G | DP_RF_SDR | DP_RF_IRIDIUM,
     .sustained_w = 500,
     .peak_w = 650,
     .battery_wh = 197,
     .mesh_role = DP_ROLE_HOME,
     .tier_pack = "citadel",
     NEEDS(BRIEFCASE_INST_NEEDS, DPC_BOARD_VERSAL, DPC_NIC_25G, DPC_SDR, DPC_IRIDIUM, DPC_STRIPE)},
    /* ===== servers, civilian (SERVER_CIVILIAN.md) ===== */
    {.slug = "hearth",
     .name = "Hearth",
     .model = "ZXV Hearth",
     .line = DP_LINE_SERVER,
     .isa = DP_ISA_X86_64,
     .boot = DP_BOOT_UEFI,
     .cpu = "EPYC 4565P",
     .cores = 16,
     .nodes = 1,
     .arbiter_isa = DP_ISA_RV64,
     .arbiter = "PolarFire SoC MPFS250T",
     .ram_gb = 96,
     .ecc = true,
     .usable_tb = 128,
     .hot_tb_x100 = 768,
     .code_k = 4,
     .code_m = 2,
     .displays = 1,
     .net = DP_NET_2G5 | DP_NET_25G,
     .sustained_w = 260,
     .peak_w = 360,
     .battery_wh = 1000,
     .mesh_role = DP_ROLE_HOME,
     .tier_pack = "personal-server-1",
     NEEDS(SERVER_NEEDS, DPC_AHCI, DPC_NIC_2G5, DPC_NIC_25G)},
    {.slug = "homestead",
     .name = "Homestead",
     .model = "ZXV Homestead",
     .line = DP_LINE_SERVER,
     .isa = DP_ISA_X86_64,
     .boot = DP_BOOT_UEFI,
     .cpu = "EPYC 8535P",
     .cores = 64,
     .nodes = 1,
     .arbiter_isa = DP_ISA_RV64,
     .arbiter = "PolarFire SoC MPFS250T",
     .ram_gb = 384,
     .ecc = true,
     .usable_tb = 512,
     .hot_tb_x100 = 3072,
     .code_k = 16,
     .code_m = 4,
     .displays = 1,
     .net = DP_NET_25G,
     .sustained_w = 450,
     .peak_w = 650,
     .battery_wh = 2500,
     .mesh_role = DP_ROLE_HOME,
     .tier_pack = "personal-server-2",
     NEEDS(SERVER_NEEDS, DPC_SAS_HBA, DPC_NIC_25G)},
    {.slug = "guildhall",
     .name = "Guildhall",
     .model = "ZXV Guildhall",
     .line = DP_LINE_SERVER,
     .isa = DP_ISA_X86_64,
     .boot = DP_BOOT_UEFI,
     .cpu = "EPYC 8635P",
     .cores = 84,
     .nodes = 1,
     .arbiter_isa = DP_ISA_RV64,
     .arbiter = "PolarFire SoC MPFS250T",
     .ram_gb = 576,
     .ecc = true,
     .usable_tb = 1020,
     .hot_tb_x100 = 6144,
     .code_k = 16,
     .code_m = 4,
     .displays = 1,
     .net = DP_NET_25G | DP_NET_100G,
     .sustained_w = 800,
     .peak_w = 1050,
     .battery_wh = 5000,
     .mesh_role = DP_ROLE_HOME,
     .tier_pack = "personal-server-3",
     NEEDS(SERVER_NEEDS, DPC_SAS_HBA, DPC_NIC_25G)},
    /* ===== servers, institutional (SERVER_INSTITUTIONAL.md) ===== */
    {.slug = "strongbox",
     .name = "Strongbox",
     .model = "ZXV Strongbox",
     .line = DP_LINE_SERVER,
     .institutional = true,
     .isa = DP_ISA_X86_64,
     .boot = DP_BOOT_UEFI,
     .cpu = "EPYC 9555",
     .cores = 64,
     .nodes = 1,
     .arbiter_isa = DP_ISA_RV64,
     .arbiter = "PolarFire SoC MPFS250TS",
     .ram_gb = 768,
     .ecc = true,
     .usable_tb = 492,
     .hot_tb_x100 = 3072,
     .code_k = 8,
     .code_m = 4,
     .displays = 1,
     .net = DP_NET_400G,
     .sustained_w = 750,
     .peak_w = 1100,
     .battery_wh = 5000,
     .mesh_role = DP_ROLE_HOME,
     .tier_pack = "institutional-server-1",
     NEEDS(SERVER_NEEDS, DPC_NIC_400G, DPC_TAMPER, DPC_COTIER)},
    {.slug = "count-house",
     .name = "Count House",
     .model = "ZXV Count House",
     .line = DP_LINE_SERVER,
     .institutional = true,
     .isa = DP_ISA_X86_64,
     .boot = DP_BOOT_UEFI,
     .cpu = "EPYC 9755",
     .cores = 128,
     .nodes = 3,
     .arbiter_isa = DP_ISA_RV64,
     .arbiter = "PolarFire SoC MPFS250TS",
     .ram_gb = 1152,
     .ecc = true,
     .usable_tb = 1970,
     .hot_tb_x100 = 9216,
     .code_k = 16,
     .code_m = 8,
     .code_spans_nodes = true,
     .displays = 1,
     .net = DP_NET_400G,
     .sustained_w = 2600,
     .peak_w = 3700,
     .battery_wh = 10000,
     .mesh_role = DP_ROLE_HOME,
     .tier_pack = "institutional-server-2",
     NEEDS(SERVER_NEEDS, DPC_NIC_400G, DPC_TAMPER, DPC_COTIER, DPC_STRIPE_CLUSTER, DPC_KEY_SPLIT)},
    {.slug = "hoard",
     .name = "Hoard",
     .model = "ZXV Hoard",
     .line = DP_LINE_SERVER,
     .institutional = true,
     .isa = DP_ISA_X86_64,
     .boot = DP_BOOT_UEFI,
     .cpu = "EPYC 9755 (storage) + 2 x EPYC 9965 (compute)",
     .cores = 128,
     .nodes = 6,
     .arbiter_isa = DP_ISA_RV64,
     .arbiter = "2 x PolarFire SoC MPFS250TS",
     .ram_gb = 1152,
     .ecc = true,
     .usable_tb = 2950,
     .hot_tb_x100 = 12288,
     .code_k = 12,
     .code_m = 8,
     .code_spans_nodes = true,
     .displays = 1,
     .net = DP_NET_400G,
     .sustained_w = 5500,
     .peak_w = 7300,
     .battery_wh = 30000,
     .mesh_role = DP_ROLE_HOME,
     .tier_pack = "institutional-server-3",
     NEEDS(SERVER_NEEDS, DPC_NIC_400G, DPC_TAMPER, DPC_COTIER, DPC_STRIPE_CLUSTER, DPC_KEY_SPLIT)},
    /* ===== handhelds (MOBILE_DEVICES.md); institutional builds add tamper ===== */
    {.slug = "signet",
     .name = "Signet",
     .model = "ZXV Signet",
     .line = DP_LINE_HANDHELD,
     .isa = DP_ISA_ARM64,
     .boot = DP_BOOT_UBOOT_EFI,
     .cpu = "Rockchip RK3588S",
     .cores = 8,
     .nodes = 1,
     .arbiter_isa = DP_ISA_RV64,
     .arbiter = "PolarFire SoC",
     .ram_gb = 16,
     .hot_tb_x100 = 100,
     .displays = 1,
     .radios =
         DP_RF_WIFI7 | DP_RF_BT | DP_RF_5G | DP_RF_SAT_NTN | DP_RF_LORA | DP_RF_UWB | DP_RF_GNSS,
     .sustained_w = 6,
     .peak_w = 6,
     .battery_wh = 27,
     .mesh_role = DP_ROLE_THIN,
     .held = true,
     .tier_pack = "personal-1",
     NEEDS(HANDHELD_NEEDS, RK3588_NEEDS, DPC_CAMERA_ISP)},
    {.slug = "folio",
     .name = "Folio",
     .model = "ZXV Folio",
     .line = DP_LINE_HANDHELD,
     .isa = DP_ISA_ARM64,
     .boot = DP_BOOT_UBOOT_EFI,
     .cpu = "Rockchip RK3588",
     .cores = 8,
     .nodes = 1,
     .arbiter_isa = DP_ISA_RV64,
     .arbiter = "PolarFire SoC",
     .ram_gb = 32,
     .hot_tb_x100 = 200,
     .displays = 1,
     .radios =
         DP_RF_WIFI7 | DP_RF_BT | DP_RF_5G | DP_RF_SAT_NTN | DP_RF_LORA | DP_RF_UWB | DP_RF_GNSS,
     .sustained_w = 12,
     .peak_w = 12,
     .battery_wh = 45,
     .mesh_role = DP_ROLE_THIN,
     .tier_pack = "personal-2",
     NEEDS(HANDHELD_NEEDS, RK3588_NEEDS)},
    {.slug = "courier-open",
     .name = "Courier (open board)",
     .model = "ZXV Courier RK3588",
     .line = DP_LINE_HANDHELD,
     .isa = DP_ISA_ARM64,
     .boot = DP_BOOT_UBOOT_EFI,
     .cpu = "Rockchip RK3588",
     .cores = 8,
     .nodes = 1,
     .arbiter_isa = DP_ISA_RV64,
     .arbiter = "PolarFire SoC",
     .ram_gb = 32,
     .hot_tb_x100 = 1600,
     .displays = 1,
     .net = DP_NET_2G5 | DP_NET_10G,
     .radios =
         DP_RF_WIFI7 | DP_RF_BT | DP_RF_5G | DP_RF_SAT_NTN | DP_RF_LORA | DP_RF_UWB | DP_RF_GNSS,
     .sustained_w = 15,
     .peak_w = 15,
     .battery_wh = 99,
     .mesh_role = DP_ROLE_THIN,
     .tier_pack = "personal-2",
     NEEDS(HANDHELD_NEEDS, RK3588_NEEDS, DPC_NIC_2G5, DPC_NIC_10G)},
    {.slug = "courier-riscv",
     .name = "Courier (RISC-V board)",
     .model = "ZXV Courier K3",
     .line = DP_LINE_HANDHELD,
     .isa = DP_ISA_RV64,
     .boot = DP_BOOT_SBI,
     .cpu = "SpacemiT K3",
     .cores = 8,
     .nodes = 1,
     .arbiter_isa = DP_ISA_RV64,
     .arbiter = "PolarFire SoC",
     .ram_gb = 32,
     .hot_tb_x100 = 1600,
     .displays = 1,
     .net = DP_NET_2G5 | DP_NET_10G,
     .radios =
         DP_RF_WIFI7 | DP_RF_BT | DP_RF_5G | DP_RF_SAT_NTN | DP_RF_LORA | DP_RF_UWB | DP_RF_GNSS,
     .sustained_w = 15,
     .peak_w = 15,
     .battery_wh = 99,
     .mesh_role = DP_ROLE_THIN,
     .tier_pack = "personal-2",
     NEEDS(HANDHELD_NEEDS, DPC_BOARD_K3, DPC_NIC_2G5, DPC_NIC_10G)},
    {.slug = "courier-performance",
     .name = "Courier (performance board)",
     .model = "ZXV Courier X2",
     .line = DP_LINE_HANDHELD,
     .isa = DP_ISA_ARM64,
     .boot = DP_BOOT_VENDOR,
     .cpu = "Snapdragon X2 Elite",
     .cores = 18,
     .nodes = 1,
     .arbiter_isa = DP_ISA_RV64,
     .arbiter = "PolarFire SoC",
     .ram_gb = 128,
     .hot_tb_x100 = 1600,
     .displays = 1,
     .net = DP_NET_2G5 | DP_NET_10G,
     .radios =
         DP_RF_WIFI7 | DP_RF_BT | DP_RF_5G | DP_RF_SAT_NTN | DP_RF_LORA | DP_RF_UWB | DP_RF_GNSS,
     .sustained_w = 45,
     .peak_w = 45,
     .battery_wh = 99,
     .mesh_role = DP_ROLE_THIN,
     .tier_pack = "personal-3",
     NEEDS(DPC_HOSTED_LINUX, DPC_ARBITER_MBOX, DPC_BOARD_MPFS, DPC_KILL_SWITCH, DPC_SEAL,
           DPC_PQ_BOOT, DPC_DEVMESH, DPC_ROUTE_POLICY, DPC_ANSWER_CHECK, DPC_GPU_VULKAN)},
};

#define DP_N_DEV ((uint32_t) (sizeof(g_dev) / sizeof(g_dev[0])))

const dp_device_t *dp_device(uint32_t i)
{
    return i < DP_N_DEV ? &g_dev[i] : 0;
}

uint32_t dp_device_count(void)
{
    return DP_N_DEV;
}

const dp_component_t *dp_component(dp_comp_t id)
{
    if ((uint32_t) id >= (uint32_t) DPC__COUNT) return 0;
    return &g_comp[id];
}

static bool dp_streq(const char *a, const char *b)
{
    if (!a || !b) return false;
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

const dp_device_t *dp_find(const char *slug)
{
    for (uint32_t i = 0; i < DP_N_DEV; i++)
        if (dp_streq(g_dev[i].slug, slug)) return &g_dev[i];
    return 0;
}

const dp_device_t *dp_find_model(const char *model)
{
    for (uint32_t i = 0; i < DP_N_DEV; i++)
        if (dp_streq(g_dev[i].model, model)) return &g_dev[i];
    return 0;
}

void dp_assess(const dp_device_t *d, dp_tally_t *t)
{
    t->built = t->hosted = t->partial = t->missing = 0;
    if (!d) return;
    for (uint32_t i = 0; i < d->n_needs; i++) {
        const dp_component_t *c = dp_component(d->needs[i]);
        dp_state_t s = c ? c->state : DP_ST_MISSING;
        if (s == DP_ST_BUILT)
            t->built++;
        else if (s == DP_ST_HOSTED)
            t->hosted++;
        else if (s == DP_ST_PARTIAL)
            t->partial++;
        else
            t->missing++;
    }
}

bool dp_ready(const dp_device_t *d)
{
    dp_tally_t t;
    dp_assess(d, &t);
    return d && t.partial == 0 && t.missing == 0;
}

const char *dp_state_name(dp_state_t s)
{
    switch (s) {
    case DP_ST_BUILT:
        return "built";
    case DP_ST_HOSTED:
        return "hosted";
    case DP_ST_PARTIAL:
        return "partial";
    default:
        return "missing";
    }
}

const char *dp_isa_name(dp_isa_t i)
{
    switch (i) {
    case DP_ISA_X86_64:
        return "x86_64";
    case DP_ISA_ARM64:
        return "arm64";
    case DP_ISA_RV64:
        return "riscv64";
    default:
        return "-";
    }
}

const char *dp_boot_name(dp_boot_t b)
{
    switch (b) {
    case DP_BOOT_UEFI:
        return "UEFI";
    case DP_BOOT_UBOOT_EFI:
        return "U-Boot EFI";
    case DP_BOOT_SBI:
        return "OpenSBI";
    case DP_BOOT_VENDOR:
        return "vendor-signed (hosted)";
    default:
        return "-";
    }
}
