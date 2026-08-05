/* arm64_mmu.c — MMU/Paging setup for ARM64
 *
 * Sets up identity mapping with 4KB granule, 3-level page tables,
 * multi-board via board_profile.h: which 2MB L2 blocks get marked
 * Device (vs. Normal/cacheable) memory is computed from the active
 * board's UART/GIC base addresses, not a QEMU-virt-only hardcoded
 * range -- MediaTek Tank boards put their UART (~0x11002000) and GIC
 * (~0x0C000000) at addresses outside QEMU virt's 0x08000000-0x0A000000
 * window, so that window alone would silently leave Tank's MMIO
 * mapped Normal/cacheable, corrupting every register access.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#include "arm64_arch.h"
#include "board_profile.h"

/* Page table constants */
#define PAGE_SIZE       4096
#define PAGE_SHIFT      12
#define TABLE_ENTRIES   512
#define PAGE_MASK       (PAGE_SIZE - 1)

/* Page table entry bits */
#define PTE_VALID       (1 << 0)
#define PTE_BLOCK       (0 << 1)
#define PTE_TABLE       (1 << 1)
#define PTE_PAGE        (1 << 1)
#define PTE_AF          (1 << 10)   /* Access flag */
#define PTE_SH_INNER    (3 << 8)    /* Inner shareable */
#define PTE_SH_OUTER    (2 << 8)    /* Outer shareable */
#define PTE_ATTR_NORMAL (0 << 2)    /* Normal memory */
#define PTE_ATTR_DEVICE (1 << 2)    /* Device memory */
#define PTE_AP_RW       (0 << 6)    /* Read/Write */
#define PTE_AP_RO       (1 << 7)    /* Read-only */
#define PTE_NS          (1 << 5)    /* Non-secure */

/* MAIR (Memory Attribute Indirection Register) values */
#define MAIR_NORMAL     0xFF        /* Normal, WB, inner+outer */
#define MAIR_DEVICE     0x00        /* Device-nGnRnE */

/* Page table storage (16KB aligned) */
static uint64_t l0_table[TABLE_ENTRIES] __attribute__((aligned(4096)));
static uint64_t l1_table[TABLE_ENTRIES] __attribute__((aligned(4096)));
static uint64_t l2_table[TABLE_ENTRIES] __attribute__((aligned(4096)));

/* Copy the kernel's identity L2 table and the 1GB block at 0x40000000
 * so a per-process page table can include the kernel mapping while
 * still overriding specific 2MB/L3 regions for user code and stack. */
void arm64_copy_pt_template(uint64_t *l2_dst, uint64_t *l1_block_1gb) {
    for (int i = 0; i < TABLE_ENTRIES; i++)
        l2_dst[i] = l2_table[i];
    if (l1_block_1gb)
        *l1_block_1gb = l1_table[1];  /* 0x40000000-0x7FFFFFFF 1GB normal */
}

/* True if the 2MB block [block_start, block_start+2MB) overlaps the
 * peripheral window [periph_base - pad, periph_base + pad). Generously
 * oversized on purpose: marking extra headroom around a peripheral as
 * Device is always safe (non-cacheable access to ordinary RAM just
 * costs a little performance); under-marking a real peripheral as
 * Normal/cacheable is a correctness bug (stale/reordered MMIO
 * accesses), so this errs toward "too much Device", never "too little". */
static bool block_overlaps_periph(uint64_t block_start, uint64_t periph_base, uint64_t pad) {
    uint64_t block_end = block_start + (2 * 1024 * 1024);
    uint64_t win_start = (periph_base > pad) ? (periph_base - pad) : 0;
    uint64_t win_end = periph_base + pad;
    return block_start < win_end && block_end > win_start;
}

void arm64_mmu_init(void) {
    const board_profile_t *bp = board_get_profile();

    /* Set MAIR_EL1 */
    uint64_t mair = ((uint64_t)MAIR_NORMAL << 0) | ((uint64_t)MAIR_DEVICE << 8);
    __asm__ __volatile__("msr mair_el1, %0" :: "r"(mair));

    /* Clear page tables */
    int i;
    for (i = 0; i < TABLE_ENTRIES; i++) {
        l0_table[i] = 0;
        l1_table[i] = 0;
        l2_table[i] = 0;
    }

    /* L0[0] → L1 table */
    l0_table[0] = ((uint64_t)l1_table) | PTE_TABLE | PTE_VALID;

    /* L1[0] → L2 table (maps 0x00000000-0x3FFFFFFF = 1GB, fine-grained
     * so the board's GIC/UART MMIO windows can be marked Device while
     * the rest of that GB is Normal). Every board profile so far
     * (QEMU virt, all Tank profiles) keeps its peripherals in this
     * first GB and RAM starting at the next GB boundary -- see the
     * ram_base-driven loop below, which would need restructuring
     * (not just re-pointing) if a future board broke that assumption. */
    l1_table[0] = ((uint64_t)l2_table) | PTE_TABLE | PTE_VALID;

    /* L1[ram_base/1GB .. +4] → direct 1GB block mappings, Normal
     * memory. The ARM64 Image boot convention loads the kernel at
     * ram_base+0x80000 (board_profile_t.kernel_load_offset) -- without
     * this, the kernel's own code is unmapped the instant the MMU is
     * enabled, causing an immediate Level-1 instruction-abort loop. */
    uint64_t ram_base_gb = bp->ram_base / (1024ULL * 1024 * 1024);
    for (i = (int)ram_base_gb; i < (int)ram_base_gb + 4 && i < TABLE_ENTRIES; i++) {
        uint64_t base = (uint64_t)i * (1024 * 1024 * 1024);
        l1_table[i] = base | PTE_ATTR_NORMAL | PTE_AF | PTE_SH_INNER
                            | PTE_BLOCK | PTE_VALID | PTE_AP_RW;
    }

    /* L2: 512 entries × 2MB = 1GB identity mapping. Device windows are
     * generously padded (see block_overlaps_periph) around the active
     * board's real UART/GICD/GICR bases instead of a fixed QEMU-virt
     * range. */
    for (i = 0; i < TABLE_ENTRIES; i++) {
        uint64_t addr = (uint64_t)i * (2 * 1024 * 1024);
        uint64_t attr;

        /* 16MB pad: chosen so the QEMU-virt profile (uart_base
         * 0x09000000, exactly mid-window) reproduces the original
         * hardcoded 0x08000000-0x0A000000 Device range as a subset,
         * confirmed by inspection (0x09000000 +/- 0x1000000 =
         * exactly [0x08000000, 0x0A000000]) -- this refactor cannot
         * regress the one board profile actually boot-tested so far. */
        bool is_device =
            block_overlaps_periph(addr, bp->uart_base, 0x1000000) ||
            block_overlaps_periph(addr, bp->gicd_base, 0x1000000) ||
            block_overlaps_periph(addr, bp->gicr_base, 0x1000000) ||
            (bp->virtio_mmio_base &&
             block_overlaps_periph(addr, bp->virtio_mmio_base, 0x1000000));

        if (is_device) {
            attr = PTE_ATTR_DEVICE | PTE_AF | PTE_SH_OUTER;
        } else {
            attr = PTE_ATTR_NORMAL | PTE_AF | PTE_SH_INNER;
        }

        l2_table[i] = addr | attr | PTE_BLOCK | PTE_VALID | PTE_AP_RW;
    }

    /* TCR_EL1: Translation Control Register
     * IPS=0 (32-bit PA), TG0=4KB, T0SZ=24 → start-at-level-0 walk
     * (T0SZ 16-24 = 4 levels from L0; T0SZ 25-33 shifts the start to L1,
     * which would silently reinterpret l0/l1/l2 as L1/L2/L3 and turn the
     * intended L2 block entries into bogus L3 table pointers). */
    uint64_t tcr = ((uint64_t)0 << 32) |   /* IPS: 32-bit PA -- was a plain
                                             * `int` shifted by 32 (UB, and
                                             * GCC now warns: "shift count
                                             * >= width of type"); harmless
                                             * with a literal 0 operand on
                                             * this compiler/opt level, but
                                             * real UB nonetheless. */
                   (0 << 14) |   /* TG0: 4KB */
                   (24 << 0);    /* T0SZ: start walk at L0 */
    __asm__ __volatile__("msr tcr_el1, %0" :: "r"(tcr));

    /* Set TTBR0_EL1 (user space) — not used yet */
    __asm__ __volatile__("msr ttbr0_el1, %0" :: "r"((uint64_t)l0_table));

    /* Set TTBR1_EL1 (kernel space) — same for identity mapping */
    __asm__ __volatile__("msr ttbr1_el1, %0" :: "r"((uint64_t)l0_table));

    /* Invalidate TLB */
    tlb_invalidate_all();

    /* Enable MMU (SCTLR_EL1.M = bit 0) */
    uint64_t sctlr;
    __asm__ __volatile__("mrs %0, sctlr_el1" : "=r"(sctlr));
    sctlr |= 1;  /* M: Enable MMU */
    __asm__ __volatile__("msr sctlr_el1, %0" :: "r"(sctlr));
    __asm__ __volatile__("isb");
}

void arm64_mmu_disable(void) {
    uint64_t sctlr;
    __asm__ __volatile__("mrs %0, sctlr_el1" : "=r"(sctlr));
    sctlr &= ~1;
    __asm__ __volatile__("msr sctlr_el1, %0" :: "r"(sctlr));
    tlb_invalidate_all();
}
