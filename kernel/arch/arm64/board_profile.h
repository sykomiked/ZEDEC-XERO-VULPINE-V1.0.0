/* board_profile.h — ARM64 Board/SoC Profile Selection
 *
 * "Hardware is code, code is hardware": the CPU architecture (AArch64,
 * GICv3, ARM generic timer) is shared across every target below, so
 * a board is DATA plugged into one boot/driver path, not a forked
 * copy of it -- the same discipline kernel/src/hardware/rtl_device.h
 * uses for synthesizable peripherals.
 *
 * addr_confidence_t exists so this file cannot silently overstate
 * what's actually known. An address is ADDR_VERIFIED only if it has
 * been cross-checked against either (a) a public TRM, or (b) a
 * same-silicon GPL-published device tree / kernel source, cited
 * inline. MediaTek publishes no public TRM for any chip below, and
 * no same-silicon GPL source has been located yet for the exact
 * Dimensity 9400e / Dimensity 8200 / Helio G99 parts these boards
 * ship -- so every MediaTek profile here is ADDR_UNVERIFIED at best,
 * built from cross-generation MediaTek device-tree patterns (mt6779,
 * mt6797) that are cited where used, and MUST be confirmed against
 * real hardware (serial bring-up / JTAG) before being trusted.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#ifndef BOARD_PROFILE_H
#define BOARD_PROFILE_H

#include <stdint.h>
#include <stdbool.h>

#define BOARD_QEMU_VIRT   0   /* QEMU `virt` machine. Verified: this session built and booted it
                                 * through 2000+ live event cycles over a real GICv3 timer IRQ. */
#define BOARD_TANK5       1   /* Unihertz 8849 Tank 5 -- MediaTek Dimensity 9400e */
#define BOARD_TANK5_PRO   2   /* Unihertz 8849 Tank 5 Pro -- chipset UNCONFIRMED (pre-MWC2026 as of last research pass) */
#define BOARD_TANKPAD     3   /* Unihertz 8849 Tank Pad -- MediaTek Dimensity 8200 (MT6896Z) */
#define BOARD_TANKPAD_E   4   /* Unihertz 8849 Tank Pad E -- MediaTek Helio G99 (MT6789) */
#define BOARD_MAX         5

#ifndef TARGET_BOARD
#define TARGET_BOARD BOARD_QEMU_VIRT
#endif

typedef enum {
    ADDR_VERIFIED,    /* cross-checked against a public TRM or same-silicon GPL source */
    ADDR_UNVERIFIED,  /* best-effort from a same-vendor/same-IP-family pattern; NOT confirmed on this exact chip */
    ADDR_UNKNOWN      /* chipset itself not public, or no pattern source found -- sentinel value only, must not be trusted */
} addr_confidence_t;

typedef struct {
    const char *board_name;
    const char *soc_name;
    addr_confidence_t confidence;
    const char *confidence_note;    /* one-line "why", for anyone reading this before flashing hardware */

    uint64_t ram_base;
    uint64_t kernel_load_offset;    /* kernel loads at ram_base + this (Linux Image-header convention) */

    uint64_t uart_base;
    uint32_t uart_reg_shift;        /* register stride: 0 = byte-packed (PL011), 2 = 4-byte-strided (dw-apb-uart/16550-class) */
    bool     uart_is_pl011;         /* true = PL011 register set (DR/FR/IBRD/FBRD/LCRH/CR); false = 16550-class (THR/RBR/DLL/DLH/LCR/LSR/FCR) */
    uint32_t uart_clock_hz;
    uint32_t uart_irq_spi;          /* GIC SPI number; INTID = 32 + this */

    uint64_t gicd_base;
    uint64_t gicr_base;
    uint32_t gic_num_cpus;

    /* virtio-mmio transport window. On QEMU virt this is 32 device
     * slots of 0x200 bytes each starting at 0x0a000000 (found by
     * scanning for the 'virt' magic + a matching device-id). 0 means
     * the board exposes no virtio-mmio transport (real SoCs use their
     * own storage controllers instead). */
    uint64_t virtio_mmio_base;
    uint32_t virtio_mmio_count;     /* number of 0x200-byte device slots */

    uint32_t timer_irq_ppi;         /* PPI number for CNTPNSIRQ; INTID = 16 + this. Architected by
                                      * ARMv8 (always PPI 14 / INTID 30) -- this is the one timer
                                      * field that IS trustworthy on every profile regardless of
                                      * vendor, since it comes from the ARM architecture, not the SoC. */
} board_profile_t;

static inline const board_profile_t *board_get_profile(void) {
    static const board_profile_t profiles[BOARD_MAX] = {
        [BOARD_QEMU_VIRT] = {
            .board_name = "QEMU virt", .soc_name = "Generic AArch64 (-cpu cortex-a53)",
            .confidence = ADDR_VERIFIED,
            .confidence_note = "Built + booted in this session: full boot sequence, then a "
                                "live tick stream through 2000+ event cycles over a real "
                                "GICv3-routed generic-timer IRQ.",
            .ram_base = 0x40000000, .kernel_load_offset = 0x80000,
            .uart_base = 0x09000000, .uart_reg_shift = 0, .uart_is_pl011 = true,
            .uart_clock_hz = 24000000, .uart_irq_spi = 1, /* INTID 33 */
            .gicd_base = 0x08000000, .gicr_base = 0x080A0000, .gic_num_cpus = 1,
            .timer_irq_ppi = 14, /* INTID 30 */
            /* QEMU virt: 32 virtio-mmio slots at 0x0a000000, stride 0x200.
             * Verified against qemu hw/arm/virt.c (VIRT_MMIO). */
            .virtio_mmio_base = 0x0a000000, .virtio_mmio_count = 32,
        },
        [BOARD_TANK5] = {
            .board_name = "Unihertz 8849 Tank 5", .soc_name = "MediaTek Dimensity 9400e",
            .confidence = ADDR_UNVERIFIED,
            .confidence_note = "UART0/GIC addresses reused from mainline mt6779.dtsi (MediaTek "
                                "keeps the infracfg/uart0 base at 0x11002000 and gic-v3 at "
                                "0x0c000000 across mt6779 AND mt6797 device trees, both "
                                "GPL-published) as the closest same-vendor pattern found. NOT "
                                "confirmed against Dimensity 9400e / MT6896-class silicon -- "
                                "GIC base is already known to vary between mt6779 (0x0c000000) "
                                "and mt6797 (0x19000000), so treat this as a starting point for "
                                "real hardware bring-up, not a trusted value.",
            .ram_base = 0x40000000, .kernel_load_offset = 0x80000,
            .uart_base = 0x11002000, .uart_reg_shift = 2, .uart_is_pl011 = false,
            .uart_clock_hz = 26000000, .uart_irq_spi = 115,
            .gicd_base = 0x0C000000, .gicr_base = 0x0C040000, .gic_num_cpus = 8,
            .timer_irq_ppi = 14,
        },
        [BOARD_TANK5_PRO] = {
            .board_name = "Unihertz 8849 Tank 5 Pro", .soc_name = "UNCONFIRMED",
            .confidence = ADDR_UNKNOWN,
            .confidence_note = "The chipset itself was unconfirmed (pre-MWC2026 rumor stage) as "
                                "of the last research pass. Every field below is a structural "
                                "placeholder copied from BOARD_TANK5 purely so this profile "
                                "compiles and the board-select enum has somewhere to point -- it "
                                "carries zero evidentiary weight. Replace this entire struct once "
                                "the chipset is officially announced.",
            .ram_base = 0x40000000, .kernel_load_offset = 0x80000,
            .uart_base = 0x11002000, .uart_reg_shift = 2, .uart_is_pl011 = false,
            .uart_clock_hz = 26000000, .uart_irq_spi = 115,
            .gicd_base = 0x0C000000, .gicr_base = 0x0C040000, .gic_num_cpus = 8,
            .timer_irq_ppi = 14,
        },
        [BOARD_TANKPAD] = {
            .board_name = "Unihertz 8849 Tank Pad", .soc_name = "MediaTek Dimensity 8200 (MT6896Z)",
            .confidence = ADDR_UNVERIFIED,
            .confidence_note = "Same mt6779/mt6797-pattern basis as BOARD_TANK5 (see its note). "
                                "Not confirmed against MT6896Z silicon specifically.",
            .ram_base = 0x40000000, .kernel_load_offset = 0x80000,
            .uart_base = 0x11002000, .uart_reg_shift = 2, .uart_is_pl011 = false,
            .uart_clock_hz = 26000000, .uart_irq_spi = 115,
            .gicd_base = 0x0C000000, .gicr_base = 0x0C040000, .gic_num_cpus = 8,
            .timer_irq_ppi = 14,
        },
        [BOARD_TANKPAD_E] = {
            .board_name = "Unihertz 8849 Tank Pad E", .soc_name = "MediaTek Helio G99 (MT6789)",
            .confidence = ADDR_UNVERIFIED,
            .confidence_note = "Same mt6779/mt6797-pattern basis as BOARD_TANK5 (see its note). "
                                "MT6789 GPL kernel sources exist for OTHER vendors' phones on "
                                "this exact chip (e.g. Transsion/Infinix device trees) but were "
                                "not pulled into this repo to extract confirmed addresses yet -- "
                                "that is the single highest-value next step for THIS profile "
                                "specifically, since it could move straight to ADDR_VERIFIED.",
            .ram_base = 0x40000000, .kernel_load_offset = 0x80000,
            .uart_base = 0x11002000, .uart_reg_shift = 2, .uart_is_pl011 = false,
            .uart_clock_hz = 26000000, .uart_irq_spi = 115,
            .gicd_base = 0x0C000000, .gicr_base = 0x0C040000, .gic_num_cpus = 8,
            .timer_irq_ppi = 14,
        },
    };
    return &profiles[TARGET_BOARD];
}

#endif /* BOARD_PROFILE_H */
