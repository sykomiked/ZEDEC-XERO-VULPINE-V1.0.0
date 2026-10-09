/* riscv_mmu.c — RISC-V supervisor paging bring-up + mm_ready provider.
 *
 * SHARED BY BOTH RISC-V WIDTHS. rv64 (lp64d) enables Sv39; rv32 (ilp32d)
 * enables Sv32. Both build an IDENTITY map with the coarsest leaf the mode
 * offers — a 1 GiB gigapage on Sv39, a 4 MiB megapage on Sv32 — so VA == PA and
 * the instruction after `csrw satp` is fetched from the same address it lived at
 * with the MMU off. QEMU virt's whole space (kernel at 0x80200000, UART at
 * 0x10000000, PLIC at 0x0C000000, CLINT at 0x02000000) is covered by the single
 * flat table, so nothing this kernel already touches faults the moment paging
 * turns on.
 *
 * WHY THIS EXISTS. The declaration gate reported mm_ready UNPROVIDED on riscv:
 * boot.s/boot_rv32.s set up a stack, cleared BSS and enabled the FPU but never
 * wrote satp, so the hart ran in Bare mode and no module owned the capability
 * that alloc REQUIRES and the shared ramdisk (blockdev_ready) transitively needs.
 * arm64 (SCTLR_EL1.M) and x86_64 (CR3, 4-level paging in boot.asm) both enable
 * translation and report mm_ready READY; riscv running MMU-off was the actual
 * gap, not a design choice, so this file closes it the same way — by turning the
 * MMU ON and then READING BACK satp to prove it.
 *
 * FALSIFIABLE BY CONSTRUCTION (the project's RULE 1). riscv_mmu_bringup reads
 * satp.MODE off the CSR. If riscv_mmu_init was never called, or the write did
 * not take (an implementation with no S-mode paging leaves satp at Bare), MODE
 * reads 0 and the bring-up returns non-zero, so mm_ready stays S0/HELD. A
 * provider that cannot report "not up" is not a provider — see arm64_mmu.c,
 * whose bring-up reads SCTLR_EL1.M for exactly this reason.
 *
 * Freestanding: integer only, no libc, no allocation, no float. Every constant
 * is a compile-time shift; nothing lowers to a libgcc 64-bit divide/shift helper
 * that the 32-bit target has no library to supply.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#include "riscv_arch.h"

/* Page-table entry permission/status bits, identical on Sv32 and Sv39. */
#define PTE_V   (1UL << 0)   /* valid */
#define PTE_R   (1UL << 1)   /* readable  — a leaf has at least one of R/W/X */
#define PTE_W   (1UL << 2)   /* writable */
#define PTE_X   (1UL << 3)   /* executable */
#define PTE_A   (1UL << 6)   /* accessed — set eagerly so a hart that traps on a */
#define PTE_D   (1UL << 7)   /* dirty    — clear A/D never faults us at boot     */

#define PTE_LEAF (PTE_V | PTE_R | PTE_W | PTE_X | PTE_A | PTE_D)

#if __riscv_xlen == 32
/* Sv32: 2 levels, 1024 entries per table, level-1 leaf = 4 MiB megapage.
 * 1024 * 4 MiB = 4 GiB — the entire rv32 physical space, mapped flat. Each PTE
 * is 32-bit. Table size 1024*4 = 4096 bytes = one page, 4 KiB-aligned. */
#define RV_PT_ENTRIES   1024u
typedef uint32_t rv_pte_t;
#else
/* Sv39: 3 levels, 512 entries per table, level-2 leaf = 1 GiB gigapage.
 * 512 * 1 GiB = 512 GiB covers everything QEMU virt exposes. Each PTE is 64-bit.
 * Table size 512*8 = 4096 bytes = one page, 4 KiB-aligned. */
#define RV_PT_ENTRIES   512u
typedef uint64_t rv_pte_t;
#endif

/* The single root table. In .bss (zeroed by boot.s clear_bss before any C runs),
 * page-aligned so its physical address >>12 is the exact PPN satp wants. */
static rv_pte_t rv_root_pt[RV_PT_ENTRIES] __attribute__((aligned(4096)));

/* Build the identity map and switch paging on. Idempotent-safe: re-running it
 * rebuilds the same table and rewrites the same satp. */
void riscv_mmu_init(void) {
    unsigned long i;

    for (i = 0; i < RV_PT_ENTRIES; i++) {
        /* Physical base of leaf i:
         *   Sv32: i << 22 (4 MiB stride);  Sv39: i << 30 (1 GiB stride).
         * PTE encodes PPN = PA >> 12 in bits [.. :10], i.e. PTE = (PA >> 2)|flags.
         * Composed as (i << (shift-2)) so it is a single constant shift with no
         * intermediate that overflows the PTE width on either ABI. */
#if __riscv_xlen == 32
        rv_root_pt[i] = (rv_pte_t)((i << 20) | PTE_LEAF);   /* (i<<22)>>2 */
#else
        rv_root_pt[i] = (rv_pte_t)(((uint64_t)i << 28) | PTE_LEAF); /* (i<<30)>>2 */
#endif
    }

    /* satp = MODE | PPN(root). PPN = &rv_root_pt >> 12 (identity => PA == VA). */
    unsigned long root_ppn = ((unsigned long)(uintptr_t)rv_root_pt) >> 12;
#if __riscv_xlen == 32
    unsigned long satp = (1UL << 31) | root_ppn;            /* MODE=1 (Sv32) */
#else
    unsigned long satp = (8UL << 60) | root_ppn;            /* MODE=8 (Sv39) */
#endif

    /* Order matters: fence the old (empty) translations, install the table,
     * fence again so the very next fetch uses it. sfence.vma with no operands
     * flushes everything, which is correct for a one-shot global remap. */
    __asm__ __volatile__("sfence.vma" ::: "memory");
    __asm__ __volatile__("csrw satp, %0" :: "r"(satp) : "memory");
    __asm__ __volatile__("sfence.vma" ::: "memory");
}

/* ---- DECLARATION -----------------------------------------------------------
 * PROVIDES(mm_ready). The kernel/src consumer `alloc` REQUIRES it, and the
 * shared ramdisk (blockdev_ready) REQUIRES it transitively; before this file no
 * riscv module provided it and the gate counted it MB_ERR_UNPROVIDED on both
 * widths. Now it has an owner, and the fixpoint flips its dependents to READY
 * the instant this bring-up first succeeds.
 *
 * REQUIRES_NONE, exactly as arm64_mmu declares: paging stands on the CPU and the
 * firmware handoff, neither of which is a modbind capability. Declaring a
 * requirement nothing provides is the MB_ERR_UNPROVIDED this gate exists to stop.
 *
 * The bring-up reads satp.MODE back off the hart — the only honest answer to "is
 * translation on", since riscv_mmu_init returns void and a table built wrong is
 * otherwise indistinguishable from here. MODE==0 (Bare) => return -1 => HELD. */
#include "zxv_decl.h"

static int riscv_mmu_bringup(void) {
    unsigned long satp;
    __asm__ __volatile__("csrr %0, satp" : "=r"(satp));
#if __riscv_xlen == 32
    unsigned long mode = (satp >> 31) & 0x1UL;   /* Sv32: 1 == on, 0 == Bare */
#else
    unsigned long mode = (satp >> 60) & 0xFUL;   /* Sv39/48/57 nonzero == on */
#endif
    return mode ? 0 : -1;                          /* falsifiable: Bare => HELD */
}

ZXV_DECLARE(riscv_mmu,
    ZXV_PROVIDES(mm_ready),
    ZXV_REQUIRES_NONE,
    ZXV_BRINGUP(riscv_mmu_bringup));
