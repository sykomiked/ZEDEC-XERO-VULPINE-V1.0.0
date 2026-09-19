/* arm32_mmu.c — ARM32 (AArch32) mm_ready provider for ZEDEC XERO pqOS.
 *
 * WHAT THIS IS, AND WHAT IT IS NOT
 * --------------------------------
 * This is the arm32 owner of the `mm_ready` capability. Before it existed the
 * declaration gate reported [FAIL] graph NOT sound on arm32: every consumer of
 * mm_ready (alloc, the shared ramdisk, virtio_bus, zxvfs) required a capability
 * that NO module on this arch provided, which the gate counts as MB_ERR_UNPROVIDED.
 *
 * It is NOT a fabricated provider. This kernel's arm32 profile is a CORE bring-up
 * that runs with the MMU OFF: boot.s (this directory) enables the VFP and clears
 * BSS, and kernel_main_arm32.c never touches TTBR0/TTBR1, TTBCR, DACR or SCTLR.M
 * (measured: `grep -rn 'ttbr\|SCTLR\|c2, c0\|c3, c0' kernel/arch/arm32` is empty
 * apart from this file). Flat physical addressing works on qemu virt because RAM
 * sits at 0x40000000 and every MMIO block (PL011 at 0x09000000, GIC at
 * 0x08000000) is directly addressable — so there is, today, nothing to translate.
 *
 * THE HONEST OUTCOME (project Rule 2)
 * -----------------------------------
 * The right result where the hardware genuinely runs MMU-off is NEITHER to fake
 * mm_ready (a bring-up that returns 0 unconditionally is the "true by
 * construction" defect this project refuses) NOR to leave it UNPROVIDED (a
 * capability with no owner, which the gate faults). It is to DECLARE a real
 * provider whose bring-up READS SCTLR.M off CP15 and honestly reports
 * held-because-off. That removes the UNPROVIDED gate failure — mm_ready now has
 * an owner — while telling the truth: the bring-up returns non-zero, the module
 * stays S0/HELD, and every consumer that REQUIRES mm_ready waits in S0 with it.
 * HELD is not a fault; UNPROVIDED and CYCLE are.
 *
 * FALSIFIABLE BY CONSTRUCTION (project Rule 1)
 * --------------------------------------------
 * The bring-up reads SCTLR (CP15 c1,c0,0) back off the CPU and returns 0 only
 * when bit 0 (M — the MMU is actually translating) is set. It CAN return
 * non-zero, and on this MMU-off profile it always does. That is exactly the
 * reference shape of kernel/arch/arm64/arm64_mmu.c's bring-up, which reads
 * SCTLR_EL1.M. A provider that cannot fail is not a provider. The instant a
 * future arm32 boot path enables short-descriptor paging and sets SCTLR.M, this
 * same bring-up begins returning 0, mm_ready flips to READY, and the ramdisk /
 * alloc / zxvfs that ride it come up behind it — with no edit here.
 *
 * NO FIXED VALUES, NO libgcc: the read is a single CP15 MRC and a bit test on a
 * uint32_t. No 64-bit divide or shift, no __builtin_bswap — nothing that lowers
 * to a libgcc helper this freestanding 32-bit target does not carry.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#include <stdint.h>

/* Read SCTLR (System Control Register) from CP15: MRC p15, 0, <Rt>, c1, c0, 0.
 * Readable at PL1 with no prior setup, exactly as CNTVCT/CNTFRQ are in
 * arm32_arch.h. Bit 0 is M — set iff the MMU is enabled and translating. */
static inline uint32_t arm32_read_sctlr(void) {
    uint32_t sctlr;
    __asm__ __volatile__("mrc p15, 0, %0, c1, c0, 0" : "=r"(sctlr));
    return sctlr;
}

/* Public predicate, mirroring arm64_mmu's intent: is paging actually on? */
int arm32_mmu_is_enabled(void) {
    return (arm32_read_sctlr() & 1u) ? 1 : 0;   /* SCTLR.M */
}

/* ---- DECLARATION -----------------------------------------------------------
 * PROVIDES(mm_ready) — the arm32 owner of the capability. Alternative provision
 * to arm64_mmu.c and x86_64/ring3.c at the same contract (legal by
 * modbind.h:146-153); those arch files are not linked into the arm32 image, so
 * on arm32 this is the sole provider.
 * REQUIRES_NONE — bring-up asks the CPU directly and declares no dependency, the
 * same deliberate position arm64_mmu.c takes.
 *
 * The bring-up reads SCTLR.M back off the hardware: 0 (ready) when the MMU is
 * translating, non-zero (held, S0) when it is off. On this MMU-off core profile
 * it reports held — the honest state — and flips to ready automatically if a
 * later boot path enables paging. */
#include "zxv_decl.h"

static int arm32_mmu_bringup(void) {
    /* Falsifiable and THREE-VALUED. The read is a live CP15 MRC of SCTLR.M:
     *   M set   -> the MMU is actually translating       -> 0            (UP, S+)
     *   M clear -> this profile runs flat, MMU legitimately off -> HELD  (S0)
     * HELD is not a fault: mm_ready has an owner, and the honest report is that
     * the hardware it fronts is absent, so the module is S0 and named -- never a
     * [FAIL]. The instant a boot path enables paging and sets M, this same read
     * returns 0 and mm_ready flips to READY with no edit here. A genuinely broken
     * read cannot forge M, so it cannot forge either 0 or HELD. */
    return arm32_mmu_is_enabled() ? 0 : MB_BRINGUP_HELD;
}

ZXV_DECLARE(arm32_mmu,
    ZXV_PROVIDES(mm_ready),
    ZXV_REQUIRES_NONE,
    ZXV_BRINGUP(arm32_mmu_bringup));
