/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* devprof.h — the ZXV device line as data the operating system can read.
 *
 * The hardware specifications in hardware/docs/oem (the briefcase, the six
 * servers, the Signet phone, Folio tablet and Courier laptop) each say what a
 * machine carries. This module turns those sheets into one table the OS uses:
 *
 *   D1  PROFILES.  One dp_device_t per model and board: host processor and
 *       its boot path, the arbiter chip, the neural modules, memory, storage
 *       tiers and their erasure code, displays, network speeds, radios, the
 *       power envelope, the default device-mesh role and the swarm tier pack.
 *       Only OS-facing facts are here; nothing a manufacturer would treat as
 *       a design secret (no board layout, no supplier, no price).
 *   D2  COMPONENTS.  One dp_component_t per piece of OS support a device can
 *       need (a boot path, a driver, a service), with its state in this tree
 *       and the file that proves it. The state is one of four:
 *         BUILT    in the tree and tested (host tests or QEMU). Nothing here
 *                  has run on a physical board yet; BUILT never means more.
 *         PARTIAL  in the tree but not complete for the spec (for example the
 *                  802.11 protocol layers without a Wi-Fi silicon driver).
 *         HOSTED   not on bare metal; the device runs zxvd hosted on its
 *                  vendor's OS for this part (the spec's accepted fallback).
 *         MISSING  not in the tree.
 *   D3  NEEDS.  Each device lists the components its spec requires.
 *       dp_assess() counts them by state, so the gap matrix
 *       (docs/DEVICE_COMPAT_MATRIX.md, printed by tools/devprof_matrix.c) is
 *       generated from the same table the OS reads and cannot drift from it.
 *   D4  SELECTION.  dp_find() looks a device up by its slug ("hearth",
 *       "signet"); dp_find_model() by the DMI/device-tree model string a
 *       board reports. A device with no match runs as a generic machine:
 *       the OS never guesses a profile.
 *
 * Freestanding C11: no libc, no malloc, no floating point. All data is const.
 */
#ifndef ZXV_DEVPROF_H
#define ZXV_DEVPROF_H

#include <stdbool.h>
#include <stdint.h>

/* ---- product lines ---- */
typedef enum {
    DP_LINE_BRIEFCASE = 1, /* the folding briefcase workstation */
    DP_LINE_SERVER = 2,    /* sealed climate-shell servers */
    DP_LINE_HANDHELD = 3,  /* Signet, Folio, Courier */
} dp_line_t;

typedef enum {
    DP_ISA_X86_64 = 1,
    DP_ISA_ARM64 = 2,
    DP_ISA_RV64 = 3,
} dp_isa_t;

typedef enum {
    DP_BOOT_UEFI = 1,      /* UEFI firmware loads \EFI\BOOT\BOOT{X64,AA64}.EFI */
    DP_BOOT_UBOOT_EFI = 2, /* U-Boot's EFI loader (RK3588 class boards) */
    DP_BOOT_SBI = 3,       /* RISC-V: first-stage loader + OpenSBI, kernel in S-mode */
    DP_BOOT_VENDOR = 4,    /* vendor-signed chain: ZXV runs hosted (zxvd) */
} dp_boot_t;

/* ---- what a device's spec needs from the OS ---- */
typedef enum {
    /* boot and processors */
    DPC_X86_UEFI = 0, /* x86_64 UEFI disc boot, GOP console, 16550 log */
    DPC_X86_SMP,      /* bring up every core (APs), per-core scheduling */
    DPC_X86_BIGMEM,   /* use all of a large ECC memory (hundreds of GB) */
    DPC_ARM64_UEFI,   /* arm64 EFI boot (QEMU virt today) */
    DPC_ARM64_SMP,    /* PSCI CPU_ON, per-core scheduling on arm64 */
    DPC_BOARD_RK3588, /* Rockchip RK3588/RK3588S board profile */
    DPC_RK3588_SOC,   /* RK3588 clocks, power domains, pinmux, PCIe, USB */
    DPC_BOARD_ZYNQMP, /* Zynq UltraScale+ arbiter: GIC-400 + Cadence UART */
    DPC_BOARD_VERSAL, /* Versal arbiter: GICv3 + PL011 */
    DPC_BOARD_MPFS,   /* PolarFire SoC arbiter: MMUART, PLIC, CLINT, 1 MHz timebase */
    DPC_BOARD_K3,     /* SpacemiT K3 RISC-V laptop board */
    DPC_HOSTED_LINUX, /* zxvd hosted on Linux (x86_64 or arm64, incl. Jetson Linux) */
    /* storage */
    DPC_NVME,
    DPC_AHCI,
    DPC_OPAL,           /* TCG Opal SED unlock with an arbiter-released key */
    DPC_SAS_HBA,        /* SAS HBA for hard-drive shelves (Broadcom 9500 class) */
    DPC_STRIPE,         /* erasure-coded storage service across drives */
    DPC_STRIPE_CLUSTER, /* the same, across servers */
    DPC_MCA_ECC,        /* machine-check and ECC error reporting */
    /* input, USB, display, audio */
    DPC_XHCI_HID,
    DPC_TOUCH,      /* touch panel and pen (I2C-HID / USI) */
    DPC_GOP_FB,     /* one framebuffer console through UEFI GOP */
    DPC_ARBITER_FB, /* three canopy panels drawn through the arbiter's BAR */
    DPC_AMD_DCN,    /* AMD display engine (Pioneer wing panels) */
    DPC_RK_VOP2,    /* RK3588 display controller + DSI/eDP */
    DPC_DLP_1080P,  /* rear projector, 1080p DMD profile */
    DPC_HDA,
    DPC_HANDHELD_AUDIO, /* handheld audio codec (not HD Audio) */
    DPC_CAMERA_ISP,
    /* wired network */
    DPC_NIC_1G,   /* Intel e1000/e1000e/igb class */
    DPC_NIC_2G5,  /* Intel I226 (igc) or Realtek RTL8125 */
    DPC_NIC_10G,  /* Intel X710 class 10GBASE-T */
    DPC_NIC_25G,  /* Intel E810 class 25/100 GbE */
    DPC_NIC_400G, /* NVIDIA ConnectX-7 class 200/400 GbE */
    /* radios */
    DPC_WIFI7,   /* Wi-Fi 7 silicon (MediaTek MT7925 / Intel BE200 class) */
    DPC_BT,      /* Bluetooth HCI */
    DPC_WWAN,    /* cellular module (MBIM over USB, or PCIe) */
    DPC_SAT_NTN, /* satellite IoT-NTN messaging module */
    DPC_IRIDIUM, /* Iridium Certus L-band terminal */
    DPC_LORA,    /* LoRa radio (LR1121) as a mesh bearer */
    DPC_UWB,     /* UWB ranging (DW3000) */
    DPC_GNSS,    /* GNSS receiver with 1PPS */
    DPC_SDR,     /* AD9361 software-defined radio, transmit off by default */
    /* power, thermal, platform */
    DPC_ACPI_TABLES,   /* MADT/FADT/MCFG parsing */
    DPC_ACPI_AML,      /* AML: battery, lid, thermal, sleep */
    DPC_EC_BATTERY,    /* embedded controller, SMBus smart battery */
    DPC_POWER_TELEM,   /* per-rail power, stalls and temperatures into ct_budget */
    DPC_RAPL,          /* x86 RAPL energy counters */
    DPC_THERMAL_MODES, /* quiet / balanced / performance / scram, limp-home */
    DPC_CLIMATE,       /* server shell: cooling regimes, start interlock, door lock */
    /* trust */
    DPC_ARBITER_MBOX, /* the arbiter mailbox: attest, telemetry, kills, tamper, seal */
    DPC_KILL_SWITCH,  /* hardware kill switches reported to the OS */
    DPC_TAMPER,       /* tamper events and key zeroisation */
    DPC_SEAL,         /* seal display + seal button + secure element signing */
    DPC_PQ_BOOT,      /* arbiter-side ML-DSA-87 + SLH-DSA check of the boot image */
    DPC_KEY_SPLIT,    /* Shamir split of a vault key across arbiters and tokens */
    /* mesh and accelerators */
    DPC_DEVMESH,      /* device mesh roles, pairing, routing */
    DPC_ROUTE_POLICY, /* per-task "automatic / always here / always home / pinned" */
    DPC_ANSWER_CHECK, /* a handheld re-runs a sample of a server's answer */
    DPC_COTIER,       /* untrusted accelerator, results reconciled on the integer core */
    DPC_GPU_CUDA,     /* CUDA vb_backend (Thor) */
    DPC_GPU_VULKAN,   /* Vulkan vb_backend (Radeon, Mali, Adreno) */
    DPC__COUNT
} dp_comp_t;

typedef enum {
    DP_ST_MISSING = 0,
    DP_ST_PARTIAL = 1,
    DP_ST_HOSTED = 2,
    DP_ST_BUILT = 3,
} dp_state_t;

typedef struct {
    dp_comp_t id;
    const char *slug; /* short name used in the matrix */
    const char *what; /* one line */
    dp_state_t state;
    const char *evidence; /* the file that proves the state, or what is missing */
} dp_component_t;

/* ---- network speeds and radios (bit sets) ---- */
#define DP_NET_1G   0x01u
#define DP_NET_2G5  0x02u
#define DP_NET_10G  0x04u
#define DP_NET_25G  0x08u
#define DP_NET_100G 0x10u
#define DP_NET_400G 0x20u

#define DP_RF_WIFI7   0x001u
#define DP_RF_BT      0x002u
#define DP_RF_5G      0x004u
#define DP_RF_SAT_NTN 0x008u
#define DP_RF_IRIDIUM 0x010u
#define DP_RF_LORA    0x020u
#define DP_RF_UWB     0x040u
#define DP_RF_GNSS    0x080u
#define DP_RF_SDR     0x100u

/* device-mesh role, same numbers as dm_role_t (devmesh.h) */
#define DP_ROLE_HOME       1u
#define DP_ROLE_THIN       2u
#define DP_ROLE_STANDALONE 3u

#define DP_MAX_NEEDS 48

typedef struct {
    const char *slug;  /* "hearth" */
    const char *name;  /* "Hearth" */
    const char *model; /* DMI product / device-tree model string the board reports */
    dp_line_t line;
    bool institutional;
    /* host */
    dp_isa_t isa;
    dp_boot_t boot;
    const char *cpu;
    uint16_t cores; /* per host node */
    uint16_t nodes; /* host nodes in the system (servers: storage + compute) */
    /* arbiter */
    dp_isa_t arbiter_isa;
    const char *arbiter;
    /* neural (accelerator modules, outside the trust boundary) */
    uint8_t neural_count;
    uint16_t neural_gb_each;
    /* memory and storage */
    uint32_t ram_gb; /* per host node, ECC unless the line says otherwise */
    bool ecc;
    uint32_t usable_tb;     /* capacity tier, after redundancy */
    uint32_t hot_tb_x100;   /* hot tier usable, in hundredths of a TB */
    uint8_t code_k, code_m; /* erasure code of the capacity tier (0,0 = none / mirror) */
    bool code_spans_nodes;  /* pieces placed across servers */
    /* I/O */
    uint8_t displays;
    uint32_t net;    /* DP_NET_* */
    uint32_t radios; /* DP_RF_* */
    /* power, in watts */
    uint32_t sustained_w;
    uint32_t peak_w;
    uint32_t battery_wh;
    /* the OS defaults */
    uint8_t mesh_role;     /* DP_ROLE_* */
    bool held;             /* may approve money (seal) */
    const char *tier_pack; /* swarm tier pack (SWARM_MODEL_LINEUP) */
    /* what the spec needs */
    uint8_t n_needs;
    dp_comp_t needs[DP_MAX_NEEDS];
} dp_device_t;

typedef struct {
    uint32_t built, hosted, partial, missing;
} dp_tally_t;

/* Catalogue access. Return NULL past the end. */
const dp_device_t *dp_device(uint32_t i);
uint32_t dp_device_count(void);
const dp_component_t *dp_component(dp_comp_t id);

/* D4: lookups. NULL when nothing matches (run as a generic machine). */
const dp_device_t *dp_find(const char *slug);
const dp_device_t *dp_find_model(const char *model);

/* D3: count one device's needs by state. */
void dp_assess(const dp_device_t *d, dp_tally_t *t);
/* True when every need of the device is BUILT or HOSTED. */
bool dp_ready(const dp_device_t *d);

const char *dp_state_name(dp_state_t s);
const char *dp_isa_name(dp_isa_t i);
const char *dp_boot_name(dp_boot_t b);

#endif /* ZXV_DEVPROF_H */
