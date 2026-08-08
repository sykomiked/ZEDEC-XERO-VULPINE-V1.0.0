/* el0_userspace.c — ARM64 EL0 User Space Implementation
 *
 * Implements per-process address spaces via TTBR0_EL1, privilege
 * boundary via EL1→EL0 ERET, and SVC-based syscall gate.
 *
 * The kernel runs at EL1 with identity-mapped memory (TTBR1_EL1).
 * User processes run at EL0 with per-process page tables (TTBR0_EL1).
 * The timer IRQ forces preemption: saves EL0 context, picks the next
 * process, switches TTBR0, and ERETs to EL0.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#include "el0_userspace.h"
#include "arm64_arch.h"
#include "../src/loader/elf.h"

/* Syscall numbers are defined in el0_userspace.h */

/* Forward UART diagnostic helpers from uart_pl011.c for debug prints. */
extern void uart_puts(const char *s);
extern void uart_putc(char c);
extern void uart_put_hex(uint64_t val);
extern void uart_put_dec(uint64_t val);

/* Import the kernel's identity L2 template so per-process page tables can
 * include the kernel mapping while still overlaying 4KB user pages. */
extern void arm64_copy_pt_template(uint64_t *l2_dst, uint64_t *l1_block_1gb);
extern uint64_t arm64_mmu_kernel_l1(int idx);  /* kernel L1 entry, to keep the kernel mapped in user tables */

/* Static page table storage for user processes.
 * Each process gets one L0 table (512 entries × 8 bytes = 4KB).
 * We also need L1/L2/L3 tables for actual mappings.
 * For simplicity, each process gets a pre-allocated set:
 *   1 L0 table (512 × 8 = 4KB)
 *   1 L1 table (512 × 8 = 4KB)
 *   1 L2 table (512 × 8 = 4KB)
 *   2 L3 tables (512 × 8 = 4KB each) — one for code, one for stack
 *
 * Total per process: 20KB. With MAX_USER_PROCS=32, that's 640KB.
 * Allocated as static BSS to avoid dynamic allocation complexity. */

/* Page table storage: aligned to 4KB */
static uint64_t pt_storage[MAX_USER_PROCS][5][TABLE_ENTRIES]
    __attribute__((aligned(4096)));

/* Track which process uses which pt_storage slot */
static int pt_slot_used[MAX_USER_PROCS];

/* Simple bitmap allocator for 4KB pages from a static pool.
 * We carve out a 2MB region from BSS for user code/stack pages. */
#define USER_PAGE_POOL_SIZE  (2 * 1024 * 1024)  /* 2MB */
static uint8_t user_page_pool[USER_PAGE_POOL_SIZE]
    __attribute__((aligned(4096)));
static uint32_t user_page_pool_used;  /* bytes used */

/* Allocate a 4KB page from the static pool */
static uint64_t alloc_user_page_phys(void) {
    if (user_page_pool_used + PAGE_SIZE_4K > USER_PAGE_POOL_SIZE)
        return 0;  /* out of memory */
    uint64_t addr = (uint64_t)&user_page_pool[user_page_pool_used];
    user_page_pool_used += PAGE_SIZE_4K;
    return addr;
}

/* Allocate a page table from per-process storage */
static uint64_t *alloc_pt_for_proc(uint32_t proc_idx) {
    if (proc_idx >= MAX_USER_PROCS) return 0;
    pt_slot_used[proc_idx] = 1;
    /* Zero the tables */
    for (int j = 0; j < 5; j++)
        for (int k = 0; k < TABLE_ENTRIES; k++)
            pt_storage[proc_idx][j][k] = 0;
    return pt_storage[proc_idx][0];  /* return L0 table */
}

/* Get the 5 page tables for a given L0 base */
static uint64_t *get_l0(uint64_t *base) { return base; }
static uint64_t *get_l1(uint64_t *base) { return base + TABLE_ENTRIES; }
static uint64_t *get_l2(uint64_t *base) { return base + 2 * TABLE_ENTRIES; }
static uint64_t *get_l3_code(uint64_t *base) { return base + 3 * TABLE_ENTRIES; }

/* Initialize ARM64-specific page table storage.
 * Called by proc_sched_init in el0_sched.c. */
void proc_pt_init(void) {
    for (int i = 0; i < MAX_USER_PROCS; i++)
        pt_slot_used[i] = 0;

    user_page_pool_used = 0;
}

/* Configure TCR_EL1 for split EL1/EL0 translation:
 *   T0SZ=25 (user VA: 39 bits, 0 – 0x7FFFFFFFFF)
 *   T1SZ=25 (kernel VA: 39 bits, identity-mapped)
 *   TG0=4KB, TG1=4KB
 *   AS=8-bit ASID (for TLB tagging per process)
 *   EPD0=0 (enable TTBR0 walks)
 *   EPD1=0 (enable TTBR1 walks)
 */
void proc_configure_tcr_el1(void) {
    uint64_t tcr;
    __asm__ __volatile__("mrs %0, tcr_el1" : "=r"(tcr));
    /* Clear T0SZ and T1SZ fields */
    tcr &= ~((uint64_t)0x3F << 0);   /* T0SZ */
    tcr &= ~((uint64_t)0x3F << 16);  /* T1SZ */
    /* T0SZ=24 => 4-level user page tables start at L0 (40-bit VA).
     * T1SZ=0  => the tiny kernel high-half (bits all 1) uses TTBR1;
     *            all low addresses, including the identity-mapped 1GB
     *            blocks at 0x40000000, are walked with TTBR0. */
    tcr |= (24ULL << 0);
    tcr |= (0ULL << 16);
    /* TG0=4KB (0b00 at bits 14:15), TG1=4KB (0b00 at bits 30:31) */
    tcr &= ~((uint64_t)3 << 14);
    tcr &= ~((uint64_t)3 << 30);
    /* ASID=8 bit (bit 36) */
    tcr |= (1ULL << 36);
    __asm__ __volatile__("msr tcr_el1, %0" :: "r"(tcr));
    __asm__ __volatile__("isb");
}

/* Set up TTBR1_EL1 for kernel space — uses the existing identity
 * mapping from arm64_mmu_init(). We just need to ensure TTBR1
 * points to the kernel's L0 table. */
void proc_setup_ttbr1(void) {
    /* The MMU init already set TTBR1 to the identity-mapped L0 table.
     * We just need to make sure TCR_EL1.EPD1=0 (walks enabled for TTBR1),
     * which is the default. Nothing to do here unless we need to
     * reconfigure. */
}

/* Map a 4KB page in the process's page table.
 * We use a simple fixed layout:
 *   L0[0] → L1 table
 *   L1[0] → L2 table
 *   L2[n] → L3 table (for the specific 2MB region)
 *   L3[n] → physical page
 *
 * The L2 table is pre-filled with the kernel's identity 2MB block
 * template before this is called, so we always overwrite the user
 * code/stack 2MB slot with an L3 table pointer.
 */
bool proc_map_page(user_proc_t *proc, uint64_t va, uint64_t pa,
                    bool writable, bool executable) {
    if (!proc || !proc->l0_table) return false;

    uint64_t *l0 = get_l0(proc->l0_table);
    uint64_t *l1 = get_l1(proc->l0_table);
    uint64_t *l2 = get_l2(proc->l0_table);

    /* L0[0] → L1 table (maps VA 0 – 512GB) */
    if (!(l0[0] & PTE_VALID)) {
        l0[0] = ((uint64_t)l1) | PTE_TABLE | PTE_VALID;
    }

    /* L1[0] → L2 table (maps VA 0 – 1GB) */
    if (!(l1[0] & PTE_VALID)) {
        l1[0] = ((uint64_t)l2) | PTE_TABLE | PTE_VALID;
    }

    /* Keep the KERNEL mapped in this user table (VA 1-4GB = L1[1..3]). Without
     * this, switching TTBR0 here unmaps the kernel and the next kernel access
     * (e.g. proc_enter_el0 restoring the register context) faults. Idempotent:
     * shares the kernel's own L2 tables, which are EL1-only so EL0 gains nothing. */
    if (!(l1[1] & PTE_VALID)) {
        l1[1] = arm64_mmu_kernel_l1(1);
        l1[2] = arm64_mmu_kernel_l1(2);
        l1[3] = arm64_mmu_kernel_l1(3);
    }

    /* L2 index: which 2MB block within the first 1GB */
    uint64_t l2_idx = (va >> 21) & 0x1FF;

    /* For the MVP layout, both code and stack live in the first 2MB
     * block (VA 0x0000_0000–0x001F_FFFF). We use one L3 table. */
    if (l2_idx != (USER_CODE_BASE >> 21)) {
        return false;
    }
    uint64_t *l3 = get_l3_code(proc->l0_table);

    /* L2[l2_idx] → L3 table.  Always overwrite any 2MB block template
     * that may have been copied from the kernel's L2 table. */
    l2[l2_idx] = ((uint64_t)l3) | PTE_TABLE | PTE_VALID;

    /* L3 index: which 4KB page within the 2MB block */
    uint64_t l3_idx = (va >> 12) & 0x1FF;

    /* Set L3 entry.  User pages are always non-executable at EL1
     * (PXN=1).  Code is executable at EL0 (UXN=0); data is not. */
    uint64_t pte = pa | PTE_AF | PTE_SH_INNER | PTE_ATTR_NORMAL
                    | PTE_PAGE | PTE_VALID | PTE_AP_RW_EL0 | PTE_PXN;
    if (!writable)
        pte |= PTE_AP_RO_EL0;
    if (!executable)
        pte |= PTE_UXN;

    l3[l3_idx] = pte;
    return true;
}

/* Switch TTBR0_EL1 to the process's page table */
void proc_switch_address_space(user_proc_t *proc) {
    if (!proc || !proc->l0_table) return;
    /* Set TTBR0_EL1 to the process's L0 table.
     * ASID=0 for now (we flush TLB on every switch — simple but correct). */
    __asm__ __volatile__("msr ttbr0_el1, %0" :: "r"((uint64_t)proc->l0_table));
    /* Invalidate TLB for EL0/EL1 regime */
    __asm__ __volatile__("tlbi vmalle1is");
    __asm__ __volatile__("dsb ish");
    __asm__ __volatile__("isb");
}

/* Enter EL0: set up SPSR_EL1 and ELR_EL1, then ERET */
void proc_enter_el0(user_proc_t *proc) {
    if (!proc) return;

    /* Set SP_EL0 to the process's stack pointer */
    __asm__ __volatile__("msr sp_el0, %0" :: "r"(proc->ctx.sp));

    /* Set ELR_EL1 to the process's PC */
    __asm__ __volatile__("msr elr_el1, %0" :: "r"(proc->ctx.pc));

    /* Set SPSR_EL1 for EL0:
     *   EL=0 (EL0h), IRQ unmasked, AArch64
     *   SPSR_EL1[3:0] = 0b0000 (EL0t)
     *   SPSR_EL1[6] = 0 (IRQ not masked)
     *   SPSR_EL1[7] = 0 (FIH not masked)
     * SPSR value: 0x00000000 for EL0t with all interrupts unmasked */
    __asm__ __volatile__("msr spsr_el1, %0" :: "r"(0ULL));

    /* Switch to user address space */
    proc_switch_address_space(proc);

    /* Load x0-x30 from the saved context.
     * Use x0 as the table base and load x0 last, so the ldr into the
     * base register happens only when there are no further uses of the
     * base.  The user x0 value is at offset 0 of the context array. */
    register uint64_t *base __asm__("x0") = &proc->ctx.x[0];
    __asm__ __volatile__(
        /* Reset SP_EL1 to the top of the kernel stack before ERET — identical to
         * proc_restore_el0. This function's prologue pushes a frame it NEVER pops
         * (it ERETs away), so without this reset SP_EL1 drifts DOWN by ~96 bytes
         * on every context switch through here and eventually overflows the 1MB
         * kernel stack into .rodata/.text, corrupting code/data and producing
         * wild control-flow faults — the proc_enter_el0 +0x4c wild-jump the Game
         * Master caught (which the earlier MMU change did NOT fix). x1 is scratch,
         * immediately reloaded with its EL0 value below. */
        "adrp x1, _stack_top\n"
        "add  x1, x1, :lo12:_stack_top\n"
        "mov  sp, x1\n"
        "ldr x1, [x0, #8]\n"
        "ldr x2, [x0, #16]\n"
        "ldr x3, [x0, #24]\n"
        "ldr x4, [x0, #32]\n"
        "ldr x5, [x0, #40]\n"
        "ldr x6, [x0, #48]\n"
        "ldr x7, [x0, #56]\n"
        "ldr x8, [x0, #64]\n"
        "ldr x9, [x0, #72]\n"
        "ldr x10, [x0, #80]\n"
        "ldr x11, [x0, #88]\n"
        "ldr x12, [x0, #96]\n"
        "ldr x13, [x0, #104]\n"
        "ldr x14, [x0, #112]\n"
        "ldr x15, [x0, #120]\n"
        "ldr x16, [x0, #128]\n"
        "ldr x17, [x0, #136]\n"
        "ldr x18, [x0, #144]\n"
        "ldr x19, [x0, #152]\n"
        "ldr x20, [x0, #160]\n"
        "ldr x21, [x0, #168]\n"
        "ldr x22, [x0, #176]\n"
        "ldr x23, [x0, #184]\n"
        "ldr x24, [x0, #192]\n"
        "ldr x25, [x0, #200]\n"
        "ldr x26, [x0, #208]\n"
        "ldr x27, [x0, #216]\n"
        "ldr x28, [x0, #224]\n"
        "ldr x29, [x0, #232]\n"
        "ldr x30, [x0, #240]\n"
        "ldr x0, [x0, #0]\n"
        : "+r"(base) : : "x1", "x2", "x3", "x4", "x5", "x6",
           "x7", "x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15",
           "x16", "x17", "x18", "x19", "x20", "x21", "x22", "x23", "x24",
           "x25", "x26", "x27", "x28", "x29", "x30", "memory"
    );

    /* ERET to EL0 */
    __asm__ __volatile__("eret");
}

/* Save current EL0 context (called from timer IRQ handler).
 * The IRQ handler in boot.s already saved x0-x30 on the kernel stack.
 * We copy them into the process's context struct. */
void proc_save_context(proc_scheduler_t *ps, cpu_context_t *ctx) {
    if (!ps || !ctx) return;
    user_proc_t *curr = proc_current(ps);
    if (!curr) return;

    /* Copy saved context into the process's PCB */
    for (int i = 0; i < 31; i++)
        curr->ctx.x[i] = ctx->x[i];
    curr->ctx.sp = ctx->sp;
    curr->ctx.pc = ctx->pc;
    curr->ctx.pstate = ctx->pstate;
}

/* Restore EL0 context and ERET (called after context switch) */
void proc_restore_el0(cpu_context_t *ctx) {
    /* This is equivalent to proc_enter_el0 but uses the provided context */
    __asm__ __volatile__("msr sp_el0, %0" :: "r"(ctx->sp));
    __asm__ __volatile__("msr elr_el1, %0" :: "r"(ctx->pc));
    __asm__ __volatile__("msr spsr_el1, %0" :: "r"(0ULL));

    /* Load x0-x30 and ERET.  Use x0 as the table base and load x0 last
     * so the base is not clobbered before the remaining loads. */
    register uint64_t *base __asm__("x0") = &ctx->x[0];
    __asm__ __volatile__(
        /* Reset SP_EL1 to the top of the kernel stack BEFORE returning to EL0.
         * This ERET abandons the ENTIRE EL1 exception call chain (the vector's
         * SAVE_REGS frame + every handler frame down to here) and never returns
         * through the vector's RESTORE_REGS. Without this reset, SP_EL1 drifts
         * DOWN by one exception frame on every context switch and — after tens
         * of seconds at 100Hz — overflows the 1MB kernel stack into .rodata
         * (silently poisoning cursor()'s sprite table -> random data aborts) and
         * .text. x1 is scratch here, then immediately reloaded with its EL0 value. */
        "adrp x1, _stack_top\n"
        "add  x1, x1, :lo12:_stack_top\n"
        "mov  sp, x1\n"
        "ldr x1, [x0, #8]\n"
        "ldr x2, [x0, #16]\n"
        "ldr x3, [x0, #24]\n"
        "ldr x4, [x0, #32]\n"
        "ldr x5, [x0, #40]\n"
        "ldr x6, [x0, #48]\n"
        "ldr x7, [x0, #56]\n"
        "ldr x8, [x0, #64]\n"
        "ldr x9, [x0, #72]\n"
        "ldr x10, [x0, #80]\n"
        "ldr x11, [x0, #88]\n"
        "ldr x12, [x0, #96]\n"
        "ldr x13, [x0, #104]\n"
        "ldr x14, [x0, #112]\n"
        "ldr x15, [x0, #120]\n"
        "ldr x16, [x0, #128]\n"
        "ldr x17, [x0, #136]\n"
        "ldr x18, [x0, #144]\n"
        "ldr x19, [x0, #152]\n"
        "ldr x20, [x0, #160]\n"
        "ldr x21, [x0, #168]\n"
        "ldr x22, [x0, #176]\n"
        "ldr x23, [x0, #184]\n"
        "ldr x24, [x0, #192]\n"
        "ldr x25, [x0, #200]\n"
        "ldr x26, [x0, #208]\n"
        "ldr x27, [x0, #216]\n"
        "ldr x28, [x0, #224]\n"
        "ldr x29, [x0, #232]\n"
        "ldr x30, [x0, #240]\n"
        "ldr x0, [x0, #0]\n"
        : "+r"(base) : : "x1", "x2", "x3", "x4", "x5", "x6",
           "x7", "x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15",
           "x16", "x17", "x18", "x19", "x20", "x21", "x22", "x23", "x24",
           "x25", "x26", "x27", "x28", "x29", "x30", "memory"
    );

    __asm__ __volatile__("eret");
}

int32_t proc_create(proc_scheduler_t *ps, const char *name,
                     void (*entry_point)(void), uint32_t priority) {
    if (!ps || ps->num_procs >= MAX_USER_PROCS) return -1;

    /* Find a free slot */
    uint32_t slot = 0;
    for (uint32_t i = 0; i < MAX_USER_PROCS; i++) {
        if (ps->procs[i].state == PROC_UNUSED) {
            slot = i;
            break;
        }
    }

    user_proc_t *proc = &ps->procs[slot];

    /* Allocate page tables */
    proc->l0_table = alloc_pt_for_proc(slot);
    if (!proc->l0_table) return -1;

    uint64_t *l0 = get_l0(proc->l0_table);
    uint64_t *l1 = get_l1(proc->l0_table);
    uint64_t *l2 = get_l2(proc->l0_table);

    /* L0[0] -> L1[0] -> L2; the L2 table is populated from the kernel's
     * identity mapping so the kernel remains accessible when we switch
     * TTBR0 to this process's table. */
    l0[0] = ((uint64_t)l1) | PTE_TABLE | PTE_VALID;
    l1[0] = ((uint64_t)l2) | PTE_TABLE | PTE_VALID;
    arm64_copy_pt_template(l2, &l1[1]);

    /* Allocate user code pages (64KB = 16 pages).
     * W^X: code is mapped READ-ONLY + EXECUTABLE at EL0 (writable=false).
     * The loader below writes the code through the kernel's identity
     * mapping of the physical page, never through this user VA, so the
     * user mapping never needs to be writable.  A user-mode write to
     * its own code page now faults (see the ENABLE_WX_TEST process). */
    uint64_t first_code_pa = 0;
    for (uint32_t i = 0; i < USER_CODE_SIZE / PAGE_SIZE_4K; i++) {
        uint64_t va = USER_CODE_BASE + i * PAGE_SIZE_4K;
        uint64_t pa = alloc_user_page_phys();
        if (!pa) return -1;
        if (!proc_map_page(proc, va, pa, false, true)) return -1;
        if (i == 0) first_code_pa = pa;
    }

    /* Allocate user stack pages (64KB = 16 pages) */
    for (uint32_t i = 0; i < USER_STACK_SIZE / PAGE_SIZE_4K; i++) {
        uint64_t va = USER_STACK_BASE + i * PAGE_SIZE_4K;
        uint64_t pa = alloc_user_page_phys();
        if (!pa) return -1;
        if (!proc_map_page(proc, va, pa, true, false)) return -1;
    }

    /* Copy entry point code to user space */
    /* For a real kernel, this would load from a binary. For now,
     * we copy the function's code to the user-mapped physical page.
     * The function must be position-independent or linked for the
     * user VA. For testing, we use a simple user-mode function. */
    /* We write to the physical page directly using the identity
     * mapping. The first code page's PA is tracked above. */

    /* Copy the entry function's code into the first code page.
     * The source function is compiled for EL1 but is position-
     * independent SVC loops; the user page will be mapped at EL0
     * with execute permission.  We copy only the first 4KB to avoid
     * over-reading past the end of the function. */
    if (first_code_pa == 0) return -1;
    uint8_t *code_ptr = (uint8_t *)first_code_pa;
    uint8_t *src = (uint8_t *)entry_point;
    for (uint32_t i = 0; i < PAGE_SIZE_4K; i++)
        code_ptr[i] = src[i];

    /* Clean data cache and invalidate instruction cache so the copied
     * code is visible to the instruction fetch path at EL0. */
    uint8_t *flush = code_ptr;
    for (uint32_t i = 0; i < PAGE_SIZE_4K; i += 64) {
        __asm__ __volatile__("dc cvac, %0" :: "r"(flush));
        __asm__ __volatile__("ic ivau, %0" :: "r"(flush));
        flush += 64;
    }
    __asm__ __volatile__("dsb ish");
    __asm__ __volatile__("isb");

    /* Optional bring-up diagnostic — off by default (audit: keep the
     * boot log clean). Build with -DEL0_PROC_DEBUG=1 to trace the copied
     * entry VA and the first instruction word of the user code page. */
#if defined(EL0_PROC_DEBUG) && EL0_PROC_DEBUG
    uart_puts("[EL0] proc_create slot=");
    uart_put_dec(slot);
    uart_puts(" pid=");
    uart_put_dec(ps->next_pid);
    uart_puts(" ctx.pc=0x");
    uart_put_hex(USER_CODE_BASE);
    uart_puts(" first=0x");
    uart_put_hex(((uint64_t)code_ptr[0]) |
                 ((uint64_t)code_ptr[1] << 8) |
                 ((uint64_t)code_ptr[2] << 16) |
                 ((uint64_t)code_ptr[3] << 24));
    uart_puts("\n");
#endif

    /* Initialize process state */
    proc->pid = ps->next_pid++;
    /* Copy name */
    for (int i = 0; i < 31 && name[i]; i++)
        proc->name[i] = name[i];
    proc->name[31] = 0;
    proc->state = PROC_READY;
    /* Kernel-created processes (shell, test procs) are trusted. */
    proc->capabilities = CAP_TRUSTED_ALL;
    proc->priority = priority;
    proc->cpu_time_ticks = 0;
    proc->quantum_ticks = 10;  /* 10 tick time slice */
    proc->quantum_default = 10;
    proc->exit_code = 0;
    proc->syscall_result = 0;

    /* Initialize IPC queue */
    proc->ipc_queue.head = 0;
    proc->ipc_queue.tail = 0;
    proc->ipc_queue.count = 0;
    proc->ipc_queue.wait_sender = 0;
    proc->blocked_on_pid = 0;
    proc->sleep_until_tick = 0;

    /* Set initial context: PC at USER_CODE_BASE, SP at USER_STACK_TOP */
    for (int i = 0; i < 31; i++)
        proc->ctx.x[i] = 0;
    proc->ctx.pc = USER_CODE_BASE;
    proc->ctx.sp = USER_STACK_TOP;
    proc->ctx.pstate = 0;  /* EL0t, interrupts unmasked */

    ps->num_procs++;
    return (int32_t)proc->pid;
}

/* ---- User-pointer validation (copy_from_user safety) ----
 *
 * Walks a process's own page table to confirm that every page in
 * [va, va+len) is mapped and EL0-accessible (AP[1]=1). This is the
 * check that must gate any syscall which dereferences a user-supplied
 * pointer, so a hostile EL0 program cannot make EL1 read or write
 * arbitrary kernel addresses (audit finding P0-1). Returns true if the
 * whole range is safe to touch. */
bool proc_user_range_check(user_proc_t *proc, uint64_t va, uint64_t len,
                           bool need_write) {
    if (!proc || !proc->l0_table) return false;
    if (len == 0) return true;
    if (va + len < va) return false;                 /* overflow */
    /* The EL0 user window is the first 2MB block (see proc_map_page). */
    if (va < PAGE_SIZE_4K || va + len > 0x200000ULL) return false;

    uint64_t *l3 = get_l3_code(proc->l0_table);
    uint64_t first = va & ~(PAGE_SIZE_4K - 1);
    uint64_t last  = (va + len - 1) & ~(PAGE_SIZE_4K - 1);
    for (uint64_t p = first; p <= last; p += PAGE_SIZE_4K) {
        uint64_t idx = (p >> 12) & 0x1FF;
        uint64_t pte = l3[idx];
        if (!(pte & PTE_VALID)) return false;
        /* AP[1] (bit 6) must be set = EL0-accessible. */
        if (!(pte & (1ULL << 6))) return false;
        /* When EL1 will WRITE the buffer, AP[2] (bit 7) must be clear =
         * writable. Without this, a read-only RECV buffer faulted at EL1
         * and (via the fail-closed abort handler) halted the kernel — a
         * user-triggered DoS (red-team). Reject it as EFAULT instead. */
        if (need_write && (pte & (1ULL << 7))) return false;
    }
    return true;
}

bool proc_user_range_ok(user_proc_t *proc, uint64_t va, uint64_t len) {
    return proc_user_range_check(proc, va, len, false);
}

/* Copy `len` bytes from a validated user VA into a kernel buffer.
 * Returns 0 on success, -1 if the range is not safe. Because the user
 * pages are identity-mapped-equivalent in this MVP (TTBR0 == TTBR1
 * cover the same PA for the first block via the shared L2 template),
 * the VA is directly readable at EL1 once validated. */
int copy_from_user(user_proc_t *proc, void *dst, uint64_t user_va,
                   uint64_t len) {
    if (!proc_user_range_ok(proc, user_va, len)) return -1;
    const uint8_t *src = (const uint8_t *)user_va;
    uint8_t *d = (uint8_t *)dst;
    for (uint64_t i = 0; i < len; i++) d[i] = src[i];
    return 0;
}

/* ---- ELF loader integration ---- */

/* Per-load mapper context. */
typedef struct {
    user_proc_t *proc;
} elf_map_ctx_t;

/* Map one PT_LOAD segment into the target process: allocate pages for
 * [vaddr, vaddr+memsz), map them with the segment's W^X-respecting
 * permissions, copy the file bytes through the kernel identity map, and
 * zero the BSS tail. Executable pages get cache maintenance so the new
 * code is visible to the EL0 instruction fetch. Returns 0 on success. */
static int elf_seg_map(void *vctx, const elf_segment_t *seg) {
    elf_map_ctx_t *ctx = (elf_map_ctx_t *)vctx;
    user_proc_t *proc = ctx->proc;

    uint64_t start = seg->vaddr & ~(PAGE_SIZE_4K - 1);
    uint64_t end   = seg->vaddr + seg->mem_size;

    for (uint64_t page = start; page < end; page += PAGE_SIZE_4K) {
        uint64_t pa = alloc_user_page_phys();
        if (!pa) return 1;
        /* code = RO+X, data = RW+NX. proc_map_page already forces PXN. */
        if (!proc_map_page(proc, page, pa, seg->writable, seg->executable))
            return 1;

        uint8_t *dstp = (uint8_t *)pa;   /* kernel identity view of the page */
        for (uint32_t i = 0; i < PAGE_SIZE_4K; i++) dstp[i] = 0;  /* BSS/zero */

        /* copy the overlap of this page with the file-backed region */
        uint64_t file_end = seg->vaddr + seg->file_size;
        uint64_t copy_start = (page > seg->vaddr) ? page : seg->vaddr;
        uint64_t copy_end = (page + PAGE_SIZE_4K < file_end)
                                ? (page + PAGE_SIZE_4K) : file_end;
        if (copy_end > copy_start) {
            uint64_t off_in_page = copy_start - page;
            uint64_t off_in_file = copy_start - seg->vaddr;
            uint64_t n = copy_end - copy_start;
            for (uint64_t i = 0; i < n; i++)
                dstp[off_in_page + i] = seg->data[off_in_file + i];
        }

        if (seg->executable) {
            uint8_t *flush = dstp;
            for (uint32_t i = 0; i < PAGE_SIZE_4K; i += 64) {
                __asm__ __volatile__("dc cvac, %0" :: "r"(flush));
                __asm__ __volatile__("ic ivau, %0" :: "r"(flush));
                flush += 64;
            }
        }
    }
    __asm__ __volatile__("dsb ish");
    __asm__ __volatile__("isb");
    return 0;
}

/* Create an EL0 process from a validated static AArch64 ELF64 image.
 * Returns PID on success, or a negative elf_result_t / -100 on error. */
int32_t proc_create_from_elf(proc_scheduler_t *ps, const char *name,
                             const uint8_t *image, uint32_t len) {
    if (!ps || ps->num_procs >= MAX_USER_PROCS) return -100;

    uint32_t slot = 0;
    bool found = false;
    for (uint32_t i = 0; i < MAX_USER_PROCS; i++) {
        if (ps->procs[i].state == PROC_UNUSED) { slot = i; found = true; break; }
    }
    if (!found) return -100;

    user_proc_t *proc = &ps->procs[slot];
    proc->l0_table = alloc_pt_for_proc(slot);
    if (!proc->l0_table) return -100;

    uint64_t *l0 = get_l0(proc->l0_table);
    uint64_t *l1 = get_l1(proc->l0_table);
    uint64_t *l2 = get_l2(proc->l0_table);
    l0[0] = ((uint64_t)l1) | PTE_TABLE | PTE_VALID;
    l1[0] = ((uint64_t)l2) | PTE_TABLE | PTE_VALID;
    arm64_copy_pt_template(l2, &l1[1]);

    /* Stack: RW + NX, same fixed region as proc_create. */
    for (uint32_t i = 0; i < USER_STACK_SIZE / PAGE_SIZE_4K; i++) {
        uint64_t va = USER_STACK_BASE + i * PAGE_SIZE_4K;
        uint64_t pa = alloc_user_page_phys();
        if (!pa) return -100;
        if (!proc_map_page(proc, va, pa, true, false)) return -100;
    }

    /* Load the ELF segments. */
    elf_map_ctx_t mctx = { proc };
    uint64_t entry = 0;
    elf_result_t r = elf_load(image, len, elf_seg_map, &mctx, &entry);
    if (r != ELF_OK) {
        uart_puts("[EL0] ELF load rejected: ");
        uart_puts(elf_strerror(r));
        uart_puts("\n");
        proc->state = PROC_UNUSED;
        return (int32_t)r;   /* negative */
    }

    /* Init process state. */
    proc->pid = ps->next_pid++;
    for (int i = 0; i < 31 && name[i]; i++) proc->name[i] = name[i];
    proc->name[31] = 0;
    proc->state = PROC_READY;
    /* Loaded applications run at LEAST PRIVILEGE: console + self-control
     * only. They cannot run shell commands (CAP_EXEC), do IPC (CAP_IPC),
     * or touch the filesystem (CAP_FS) unless explicitly granted. */
    proc->capabilities = CAP_APP_DEFAULT;
    proc->priority = 1;
    proc->cpu_time_ticks = 0;
    proc->quantum_ticks = 10;
    proc->quantum_default = 10;
    proc->exit_code = 0;
    proc->syscall_result = 0;
    proc->ipc_queue.head = proc->ipc_queue.tail = proc->ipc_queue.count = 0;
    proc->ipc_queue.wait_sender = 0;
    proc->blocked_on_pid = 0;
    proc->sleep_until_tick = 0;
    for (int i = 0; i < 31; i++) proc->ctx.x[i] = 0;
    proc->ctx.pc = entry;
    proc->ctx.sp = USER_STACK_TOP;
    proc->ctx.pstate = 0;

    ps->num_procs++;
    return (int32_t)proc->pid;
}

/* Preemptive scheduler tick — called from timer IRQ.
 * ctx contains the saved EL0 context from the IRQ handler.
 * If the current process's quantum is exhausted, switch to the next. */
void proc_sched_tick(proc_scheduler_t *ps, cpu_context_t *ctx) {
    if (!ps || !ctx) return;
    ps->ticks++;

    /* Wake any sleeping processes whose timer has expired */
    proc_wake_eligible(ps);

    user_proc_t *curr = proc_current(ps);
    if (!curr) {
        /* No process running — find a ready one */
        for (uint32_t i = 0; i < MAX_USER_PROCS; i++) {
            if (ps->procs[i].state == PROC_READY) {
                ps->current_pid = i;
                ps->procs[i].state = PROC_RUNNING;
                ps->procs[i].quantum_ticks = ps->procs[i].quantum_default;
                ps->total_context_switches++;
                proc_switch_address_space(&ps->procs[i]);
                proc_restore_el0(&ps->procs[i].ctx);
                return;
            }
        }
        return;  /* no ready processes */
    }

    /* Save context of current process */
    proc_save_context(ps, ctx);
    curr->cpu_time_ticks++;
    curr->quantum_ticks--;

    /* Check if quantum expired */
    if (curr->quantum_ticks == 0) {
        curr->state = PROC_READY;
        curr->quantum_ticks = curr->quantum_default;
        ps->total_preemptions++;

        /* Find next ready process (round-robin) */
        uint32_t next = ps->current_pid;
        for (uint32_t i = 1; i <= MAX_USER_PROCS; i++) {
            uint32_t idx = (ps->current_pid + i) % MAX_USER_PROCS;
            if (ps->procs[idx].state == PROC_READY) {
                next = idx;
                break;
            }
        }

        if (next != ps->current_pid) {
            ps->current_pid = next;
            ps->procs[next].state = PROC_RUNNING;
            ps->total_context_switches++;
            proc_switch_address_space(&ps->procs[next]);
            proc_restore_el0(&ps->procs[next].ctx);
        } else {
            /* Same process continues */
            curr->state = PROC_RUNNING;
            proc_restore_el0(&curr->ctx);
        }
    } else {
        /* Quantum not expired — continue current process */
        proc_restore_el0(&curr->ctx);
    }
}

/* ---- IPC Message Passing ---- */

/* Send a message to a process. Copies payload into the receiver's
 * message queue. Returns 0 on success, -1 if queue full. */
int32_t ipc_send(proc_scheduler_t *ps, uint32_t dest_pid,
                  const uint8_t *payload, uint32_t length) {
    if (!ps || !payload || length > IPC_MAX_PAYLOAD) return -1;

    user_proc_t *dest = proc_get(ps, dest_pid);
    if (!dest) return -1;

    ipc_queue_t *q = &dest->ipc_queue;
    if (q->count >= IPC_QUEUE_DEPTH) return -1;

    ipc_message_t *msg = &q->messages[q->head];
    msg->sender_pid = proc_current(ps) ? proc_current(ps)->pid : 0;
    msg->length = length;
    for (uint32_t i = 0; i < length; i++)
        msg->payload[i] = payload[i];

    q->head = (q->head + 1) % IPC_QUEUE_DEPTH;
    q->count++;

    /* Wake the receiver if it was blocked waiting for a message.
     * blocked_on_pid==0 means "any sender". */
    if (dest->state == PROC_BLOCKED &&
        (dest->blocked_on_pid == 0 || dest->blocked_on_pid == msg->sender_pid)) {
        dest->state = PROC_READY;
        dest->blocked_on_pid = 0;
    }

    return 0;
}

/* Receive a message. If no message is available, the process blocks.
 * Returns message length on success, -1 on error. */
int32_t ipc_recv(proc_scheduler_t *ps, uint32_t sender_pid,
                  uint8_t *buffer, uint32_t max_len) {
    if (!ps || !buffer) return -1;

    user_proc_t *curr = proc_current(ps);
    if (!curr) return -1;

    ipc_queue_t *q = &curr->ipc_queue;

    /* Check if there's a message from the specified sender (or any if 0) */
    if (q->count == 0) {
        /* Block until a message arrives */
        curr->state = PROC_BLOCKED;
        curr->blocked_on_pid = sender_pid;
        return 0;  /* caller should yield */
    }

    /* Find a message from the sender (or any) */
    uint32_t idx = q->tail;
    for (uint32_t i = 0; i < q->count; i++) {
        ipc_message_t *msg = &q->messages[idx];
        if (sender_pid == 0 || msg->sender_pid == sender_pid) {
            uint32_t copy_len = msg->length;
            if (copy_len > max_len) copy_len = max_len;
            for (uint32_t j = 0; j < copy_len; j++)
                buffer[j] = msg->payload[j];

            /* Remove the message at idx by shifting the messages after it
             * back one slot. Bound the shift by the true COUNT, not by head:
             * when the queue is full head==tail, so head is not a usable end
             * sentinel and the old loop duplicated one message and dropped
             * another (red-team). Invariant kept: head == (tail+count)%DEPTH. */
            uint32_t off = (idx - q->tail + IPC_QUEUE_DEPTH) % IPC_QUEUE_DEPTH;
            for (uint32_t k = off; k + 1 < q->count; k++) {
                uint32_t a = (q->tail + k) % IPC_QUEUE_DEPTH;
                uint32_t b = (q->tail + k + 1) % IPC_QUEUE_DEPTH;
                q->messages[a] = q->messages[b];
            }
            q->count--;
            q->head = (q->tail + q->count) % IPC_QUEUE_DEPTH;
            return (int32_t)copy_len;
        }
        idx = (idx + 1) % IPC_QUEUE_DEPTH;
    }

    /* No matching message — block */
    curr->state = PROC_BLOCKED;
    curr->blocked_on_pid = sender_pid;
    return 0;
}

/* Wake processes whose sleep has expired or whose blocked condition is met */
void proc_wake_eligible(proc_scheduler_t *ps) {
    if (!ps) return;
    for (uint32_t i = 0; i < MAX_USER_PROCS; i++) {
        user_proc_t *p = &ps->procs[i];
        if (p->state == PROC_SLEEPING && ps->ticks >= p->sleep_until_tick) {
            p->state = PROC_READY;
        }
    }
}

/* SVC handler — called from synchronous exception when EL0 issues SVC.
 * EC=0x15 (SVC instruction in AArch64).
 * The syscall number is in x8, args in x0-x5.
 * Results returned in x0. */
void proc_handle_svc(proc_scheduler_t *ps, uint64_t syscall_num,
                      uint64_t *args, cpu_context_t *ctx) {
    if (!ps || !ctx) return;
    ps->total_syscalls++;

    user_proc_t *curr = proc_current(ps);
    if (!curr) return;

    /* Capability gate: map each syscall to the capability it needs, and
     * deny (-EPERM) if the calling process lacks it. EXIT/GETPID/YIELD/
     * SLEEP act only on the caller itself and need CAP_PROC. */
    uint32_t need = 0;
    switch (syscall_num) {
        case SYS_WRITE:              need = CAP_WRITE; break;
        case SYS_EXIT: case SYS_GETPID:
        case SYS_YIELD: case SYS_SLEEP: need = CAP_PROC; break;
        case SYS_EXEC:               need = CAP_EXEC; break;
        case SYS_SEND: case SYS_RECV: need = CAP_IPC; break;
        case SYS_OPEN: case SYS_READ:
        case SYS_CLOSE:              need = CAP_FS; break;
        default:                     need = 0; break;
    }
    if (need && !(curr->capabilities & need)) {
        uart_puts("[EL0][EPERM] pid=");
        uart_put_dec((uint64_t)curr->pid);
        uart_puts(" denied syscall ");
        uart_put_dec(syscall_num);
        uart_puts(" (missing capability)\n");
        ctx->x[0] = (uint64_t)-3;   /* -EPERM */
        return;
    }

    switch (syscall_num) {
        case SYS_EXIT:
            proc_terminate(ps, curr->pid, (int32_t)(args[0]));
            /* Won't return — pick next process.  A process may be
             * SLEEPING rather than READY right now, so idle at EL1
             * until one wakes (irq_handler_c advances ps->ticks and
             * runs proc_wake_eligible while we WFI here) instead of
             * halting forever the moment no process is READY.
             *
             * CRITICAL: synchronous-exception entry masked IRQs
             * (PSTATE.I=1).  With IRQs masked, WFI wakes on a PENDING
             * interrupt but never TAKES it — irq_handler_c would never
             * run, ticks would never advance, and no sleeper could
             * ever wake.  Unmask IRQs here; we have already abandoned
             * the exited process's context, and the dispatch below
             * rebuilds state via proc_restore_el0. */
            __asm__ __volatile__("msr daifclr, #2");
            for (;;) {
                bool any_live = false;
                for (uint32_t i = 1; i <= MAX_USER_PROCS; i++) {
                    uint32_t idx = (ps->current_pid + i) % MAX_USER_PROCS;
                    proc_state_t st = ps->procs[idx].state;
                    if (st == PROC_READY) {
                        ps->current_pid = idx;
                        ps->procs[idx].state = PROC_RUNNING;
                        ps->procs[idx].quantum_ticks =
                            ps->procs[idx].quantum_default;
                        proc_switch_address_space(&ps->procs[idx]);
                        proc_restore_el0(&ps->procs[idx].ctx);
                        return; /* not reached — proc_restore_el0 ERETs */
                    }
                    if (st == PROC_SLEEPING || st == PROC_BLOCKED ||
                        st == PROC_RUNNING) {
                        any_live = true;
                    }
                }
                if (!any_live)
                    break;
                __asm__ __volatile__("wfi");
            }
            /* No live processes remain — all user processes exited. */
            uart_puts("[EL0] all user processes exited — EL1 idle\n");
            while (1) { __asm__ __volatile__("wfi"); }
            return;

        case SYS_GETPID:
            ctx->x[0] = curr->pid;
            return;

        case SYS_YIELD:
            /* Save the current EL0 context before we might switch away. */
            proc_save_context(ps, ctx);
            curr->state = PROC_READY;
            curr->quantum_ticks = 0;  /* force reschedule */
            /* Find a *different* ready process (don't yield to ourself). */
            for (uint32_t i = 1; i < MAX_USER_PROCS; i++) {
                uint32_t idx = (ps->current_pid + i) % MAX_USER_PROCS;
                if (ps->procs[idx].state == PROC_READY) {
                    ps->current_pid = idx;
                    ps->procs[idx].state = PROC_RUNNING;
                    proc_switch_address_space(&ps->procs[idx]);
                    proc_restore_el0(&ps->procs[idx].ctx);
                    return;
                }
            }
            /* No other process — continue current process */
            curr->state = PROC_RUNNING;
            return;

        case SYS_WRITE:
            /* Write to UART — args[0] = char value */
            {
                extern void uart_putc(char c);
                char c = (char)(args[0]);
                uart_putc(c);
                ctx->x[0] = 1;  /* wrote 1 byte */
            }
            return;

        case SYS_READ:
            /* Read one char from UART (NON-blocking) — returns the char
             * in x0, or 0 if no input is pending.  Never busy-wait here:
             * SVC entry runs at EL1 with IRQs masked, so a blocking read
             * would freeze the timer tick, preemption, and the whole
             * kernel event cycle until a key arrived.  User code polls
             * and SYS_SLEEPs between polls instead. */
            {
                extern char uart_getc(void);
                extern bool uart_rx_ready(void);
                ctx->x[0] = uart_rx_ready()
                    ? (uint64_t)(uint8_t)uart_getc()
                    : 0;
            }
            return;

        case SYS_EXEC:
            /* Execute a shell command line via the kernel P-TERM engine.
             * args[0] = user VA of the command string (in the calling
             * process's stack/data pages, mapped EL0+EL1 RW), args[1] =
             * length (bounded).  The string is copied into a kernel
             * buffer before use so the shell executor never depends on
             * user memory staying mapped. */
            {
                extern void kernel_shell_exec(const char *line);
                char kbuf[IPC_MSG_SIZE];
                uint64_t uva = args[0];
                uint64_t len = args[1];
                if (uva == 0 || len >= sizeof(kbuf)) {
                    ctx->x[0] = (uint64_t)-1;
                    return;
                }
                /* P0-1: validate + copy the user pointer instead of
                 * dereferencing it raw. A bad/kernel address fails here. */
                if (copy_from_user(curr, kbuf, uva, len) != 0) {
                    ctx->x[0] = (uint64_t)-2;   /* EFAULT */
                    return;
                }
                kbuf[len] = '\0';
                kernel_shell_exec(kbuf);
                ctx->x[0] = 0;
            }
            return;

        case SYS_SLEEP:
            /* Sleep for args[0] milliseconds */
            {
                ctx->x[0] = 0;
                /* Save the full context FIRST (directly — proc_current()
                 * returns NULL once the state leaves RUNNING) so a later
                 * wake dispatches from exactly after this SVC, not from
                 * a stale context saved ticks ago. */
                curr->ctx = *ctx;

                /* Find another ready process to run while we sleep */
                uint32_t next = ps->current_pid;
                for (uint32_t i = 1; i <= MAX_USER_PROCS; i++) {
                    uint32_t idx = (ps->current_pid + i) % MAX_USER_PROCS;
                    if (ps->procs[idx].state == PROC_READY) {
                        next = idx;
                        break;
                    }
                }
                if (next != ps->current_pid) {
                    curr->state = PROC_SLEEPING;
                    curr->sleep_until_tick = ps->ticks + (args[0] / 10);
                    ps->current_pid = next;
                    ps->procs[next].state = PROC_RUNNING;
                    proc_switch_address_space(&ps->procs[next]);
                    proc_restore_el0(&ps->procs[next].ctx);
                }
                /* No other runnable process: degrade the sleep to a
                 * no-op and keep this process RUNNING.  Marking it
                 * SLEEPING while it keeps executing would rewind it to
                 * the saved context on the next tick (losing any UART
                 * input consumed in between).  This also preserves the
                 * invariant that at least one process is always RUNNING. */
            }
            return;

        case SYS_SEND:
            /* args[0]=dest_pid, args[1]=msg_ptr, args[2]=length */
            {
                /* P0-1: the message buffer is a user pointer — validate the
                 * whole [ptr, ptr+len) range is mapped and EL0-accessible in
                 * the caller before EL1 reads it. */
                if (args[2] > IPC_MAX_PAYLOAD ||
                    !proc_user_range_ok(curr, args[1], args[2])) {
                    ctx->x[0] = (uint64_t)-2;   /* EFAULT / too big */
                    return;
                }
                int32_t ret = ipc_send(ps, (uint32_t)args[0],
                                        (const uint8_t *)args[1],
                                        (uint32_t)args[2]);
                ctx->x[0] = (uint64_t)ret;
            }
            return;

        case SYS_RECV:
            /* args[0]=sender_pid(0=any), args[1]=buf_ptr, args[2]=max_len */
            {
                /* P0-1: the receive buffer is a user pointer EL1 will WRITE —
                 * validate the range is mapped, EL0-accessible, AND WRITABLE. */
                if (args[2] > IPC_MAX_PAYLOAD ||
                    !proc_user_range_check(curr, args[1], args[2], true)) {
                    ctx->x[0] = (uint64_t)-2;   /* EFAULT / too big / read-only */
                    return;
                }
                int32_t ret = ipc_recv(ps, (uint32_t)args[0],
                                        (uint8_t *)args[1],
                                        (uint32_t)args[2]);
                ctx->x[0] = (uint64_t)ret;
                /* If blocked, yield */
                if (curr->state == PROC_BLOCKED) {
                    uint32_t next = ps->current_pid;
                    for (uint32_t i = 1; i <= MAX_USER_PROCS; i++) {
                        uint32_t idx = (ps->current_pid + i) % MAX_USER_PROCS;
                        if (ps->procs[idx].state == PROC_READY) {
                            next = idx;
                            break;
                        }
                    }
                    if (next != ps->current_pid) {
                        ps->current_pid = next;
                        ps->procs[next].state = PROC_RUNNING;
                        proc_switch_address_space(&ps->procs[next]);
                        proc_restore_el0(&ps->procs[next].ctx);
                    }
                }
            }
            return;

        default:
            ctx->x[0] = (uint64_t)-1;  /* unknown syscall */
            return;
    }
}

/* Free page table slot for a terminated process.
 * Called by proc_terminate in el0_sched.c. */
void proc_pt_free(uint64_t *l0_table) {
    for (int j = 0; j < MAX_USER_PROCS; j++) {
        if (pt_slot_used[j] && pt_storage[j][0] == l0_table) {
            pt_slot_used[j] = 0;
            break;
        }
    }
}

uint64_t *proc_alloc_page_table(void) {
    /* Used externally if needed */
    return 0;
}
