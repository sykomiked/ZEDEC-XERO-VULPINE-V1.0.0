/* x86_userspace.c: x86-64 ring-3 processes (page tables, FXSAVE, the trap
 * hook, syscalls, the idle loop and the boot self-test)
 *
 * The decisions (slots, ticks, who runs next) are x86_sched.c's; this file
 * acts on them. Every trap from ring 3 arrives in x86_user_trap() with the
 * full frame on the kernel stack (TSS.RSP0 for syscalls and faults, IST1 for
 * the timer). The running process's FP/SIMD state is saved with FXSAVE and
 * its frame copied into its control block; the handler changes process
 * state; then the process that runs next has its frame copied over the one
 * on the stack, its CR3 loaded and its FP state restored, and the trap path
 * IRETQs into it. There are no per-process kernel stacks: the kernel never
 * blocks inside a trap, it only switches which frame it returns to.
 *
 * Memory: frames come from the four-state allocator (src/pmm), over a range
 * of usable RAM from the handoff memory map when there is one (the UEFI
 * boot), else over a static pool (direct -kernel boot, no map). The range is
 * kept below 1 GB, where boot's identity map lets the kernel write a frame
 * through its physical address.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#include "x86_userspace.h"
#include "../zxv_handoff.h"
#include "efifb_con.h"
#include "uart_16550.h"
#include "../../src/syscall/syscall.h"
#include "../../src/loader/elf.h"
#include "../../src/pmm/pmm.h"

/* The ABI's capability bits and the per-process bits are the same numbers
 * (as on arm64): the table decides with the ABI names, the PCB stores these. */
typedef char
    x86_cap_agree[(CAP_WRITE == ZSC_CAP_WRITE && CAP_PROC == ZSC_CAP_PROC &&
                   CAP_EXEC == ZSC_CAP_EXEC && CAP_IPC == ZSC_CAP_IPC && CAP_FS == ZSC_CAP_FS)
                      ? 1
                      : -1];

#define SEL_UCODE 0x1Bu /* ring3.c GDT: user code | RPL 3 */
#define SEL_UDATA 0x23u /* user data | RPL 3 */

/* ---- CPU helpers ---------------------------------------------------------- */
static inline uint64_t read_cr3(void)
{
    uint64_t v;
    __asm__ __volatile__("mov %%cr3, %0" : "=r"(v));
    return v;
}
static inline void write_cr3(uint64_t v)
{
    __asm__ __volatile__("mov %0, %%cr3" : : "r"(v) : "memory");
}
static inline uint64_t read_cr2(void)
{
    uint64_t v;
    __asm__ __volatile__("mov %%cr2, %0" : "=r"(v));
    return v;
}
static inline uint64_t rdmsr(uint32_t msr)
{
    uint32_t lo, hi;
    __asm__ __volatile__("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t) hi << 32) | lo;
}
static inline void wrmsr(uint32_t msr, uint64_t v)
{
    __asm__ __volatile__("wrmsr" : : "c"(msr), "a"((uint32_t) v), "d"((uint32_t) (v >> 32)));
}
static inline void cpuid(uint32_t leaf, uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d)
{
    __asm__ __volatile__("cpuid" : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d) : "a"(leaf), "c"(0));
}

/* FXSAVE/FXRSTOR move the user's x87/SSE state; the kernel never touches
 * those registers (-mgeneral-regs-only), so they hold the user's values
 * from the trap until the restore. */
typedef struct {
    uint8_t b[512];
} fx_area_t;
static inline void fx_save(user_proc_t *p)
{
    __asm__ __volatile__("fxsave64 %0" : "=m"(*(fx_area_t *) p->fx));
}
static inline void fx_restore(const user_proc_t *p)
{
    __asm__ __volatile__("fxrstor64 %0" : : "m"(*(const fx_area_t *) p->fx));
}

/* The state FNINIT plus a default MXCSR would give: control word 0x037F
 * (all x87 exceptions masked), MXCSR 0x1F80 (all SSE exceptions masked),
 * every register empty and zero. */
static void fx_init(user_proc_t *p)
{
    for (uint32_t i = 0; i < 512; i++) p->fx[i] = 0;
    p->fx[0] = 0x7F;
    p->fx[1] = 0x03;
    p->fx[24] = 0x80;
    p->fx[25] = 0x1F;
}

static void copy_frame(x86_frame_t *dst, const x86_frame_t *src)
{
    uint64_t *d = (uint64_t *) dst;
    const uint64_t *s = (const uint64_t *) src;
    for (uint32_t i = 0; i < sizeof(x86_frame_t) / 8u; i++) d[i] = s[i];
}

/* ---- the frame pool ------------------------------------------------------- */
#define POOL_MAX_FRAMES 512u          /* at most 2 MB from the map */
#define FALLBACK_FRAMES 128u          /* 512 KB when there is no map */
#define IDENTITY_LIMIT  0x40000000ULL /* boot maps the low 1 GB */

static pmm_t g_upmm;
static uint8_t g_upmm_bits[PMM_BITS_BYTES(POOL_MAX_FRAMES)];
static uint8_t g_fallback_pool[FALLBACK_FRAMES * PMM_FRAME] __attribute__((aligned(4096)));
static uint32_t g_frames_fenced;
static int g_pool_ready, g_pool_from_map;

extern char __stack_top[];

/* The largest usable range of the handoff map that lies wholly above the
 * kernel (image, BSS and its stack) and below the identity limit. 0 when
 * there is no accepted handoff. */
static uint64_t pool_from_handoff(uint32_t *nframes)
{
    const zxh_t *h = (const zxh_t *) (ZXV_X86_BOOTINFO_ADDR + ZXH_OFFSET);
    if (zxh_accept(h) != 1) return 0;
    uint64_t floor = (uint64_t) (uintptr_t) __stack_top;
    if (h->kernel_hi > floor) floor = h->kernel_hi;
    floor = (floor + PMM_FRAME - 1) & ~(uint64_t) (PMM_FRAME - 1);
    uint64_t best = 0, best_n = 0;
    for (uint32_t i = 0; i < h->n_mem; i++) {
        const zxh_mem_t *m = &h->mem[i];
        if (m->kind != ZXH_MEM_USABLE) continue;
        uint64_t lo = m->base, hi = m->base + m->pages * PMM_FRAME;
        if (lo < floor) lo = floor;
        if (hi > IDENTITY_LIMIT) hi = IDENTITY_LIMIT;
        if (hi <= lo) continue;
        uint64_t n = (hi - lo) / PMM_FRAME;
        if (n > best_n) {
            best = lo;
            best_n = n;
        }
    }
    if (best_n > POOL_MAX_FRAMES) best_n = POOL_MAX_FRAMES;
    *nframes = (uint32_t) best_n;
    return best_n ? best : 0;
}

static void pool_init(void)
{
    if (g_pool_ready) return;
    uint32_t n = 0;
    uint64_t base = pool_from_handoff(&n);
    if (base && n >= 64u) {
        g_pool_from_map = 1;
    } else {
        base = (uint64_t) (uintptr_t) g_fallback_pool;
        n = FALLBACK_FRAMES;
        g_pool_from_map = 0;
    }
    pmm_init(&g_upmm, base, n, g_upmm_bits);
    g_pool_ready = 1;
}

uint32_t proc_user_pages_free(void)
{
    return g_upmm.n_free;
}

/* A zeroed frame, or 0. A new process never sees what an earlier one left. */
static uint64_t frame_alloc(void)
{
    uint64_t pa = 0;
    lpres_state_t v = pmm_alloc(&g_upmm, &pa);
    if (v != LPRES_STATE_TRUE) {
        if (v == LPRES_STATE_BOTH)
            uart_puts("  [PROC] frame pool counters disagree with its bitmap\n");
        return 0;
    }
    uint64_t *p = (uint64_t *) (uintptr_t) pa;
    for (uint32_t i = 0; i < PMM_FRAME / 8u; i++) p[i] = 0;
    return pa;
}

static void frame_free(uint64_t pa)
{
    if (!pmm_owns_range(&g_upmm, pa)) return;
    if (pmm_free(&g_upmm, pa) == LPRES_STATE_BOTH) {
        g_frames_fenced++;
        uart_puts("  [PROC] frame freed twice: fenced\n");
    }
}

/* ---- address spaces ------------------------------------------------------- */
static uint64_t g_kernel_cr3;
static int g_nx;

static uint64_t *table_of(uint64_t e)
{
    return (uint64_t *) (uintptr_t) (e & PTE_ADDR);
}

/* The next-level table under *e, made (zeroed, user-reachable) if absent. */
static uint64_t *next_table(uint64_t *e)
{
    if (!(*e & PTE_P)) {
        uint64_t f = frame_alloc();
        if (!f) return 0;
        *e = f | PTE_P | PTE_W | PTE_U;
    }
    if (*e & PTE_PS) return 0;
    return table_of(*e);
}

bool proc_map_page(user_proc_t *proc, uint64_t va, uint64_t pa, bool writable, bool executable)
{
    if (!proc || !proc->pml4 || (va & 0xFFF) || (pa & 0xFFF)) return false;
    if (va >= (1ULL << 47) || ((va >> 39) & 511u) != X86_USER_L4_SLOT) return false;
    uint64_t *l3 = next_table(&proc->pml4[X86_USER_L4_SLOT]);
    if (!l3) return false;
    uint64_t *l2 = next_table(&l3[(va >> 30) & 511u]);
    if (!l2) return false;
    uint64_t *l1 = next_table(&l2[(va >> 21) & 511u]);
    if (!l1) return false;
    uint64_t *e = &l1[(va >> 12) & 511u];
    if (*e & PTE_P) return false; /* never map over a page */
    *e = pa | PTE_P | PTE_U | (writable ? PTE_W : 0) | ((!executable && g_nx) ? PTE_NX : 0);
    return true;
}

/* The frame behind a user page, or 0. */
static uint64_t user_pa(const user_proc_t *p, uint64_t va)
{
    uint64_t e = p->pml4[(va >> 39) & 511u];
    if (!(e & PTE_P) || ((va >> 39) & 511u) != X86_USER_L4_SLOT) return 0;
    e = table_of(e)[(va >> 30) & 511u];
    if (!(e & PTE_P)) return 0;
    e = table_of(e)[(va >> 21) & 511u];
    if (!(e & PTE_P)) return 0;
    e = table_of(e)[(va >> 12) & 511u];
    if (!(e & PTE_P)) return 0;
    return e & PTE_ADDR;
}

void proc_switch_address_space(user_proc_t *proc)
{
    uint64_t want = proc && proc->pml4 ? (uint64_t) (uintptr_t) proc->pml4 : g_kernel_cr3;
    if (read_cr3() != want) write_cr3(want);
}

/* x86_sched.c calls this when a process ends or a half-built one is
 * abandoned: every frame of the user tree, then the PML4, back to the pool.
 * The kernel leaves this address space first if it is still live (the
 * process's own exit or fault), so nothing runs on freed tables. */
void x86_proc_release_space(user_proc_t *p)
{
    if (!p->pml4) return;
    if (read_cr3() == (uint64_t) (uintptr_t) p->pml4) write_cr3(g_kernel_cr3);
    uint64_t e4 = p->pml4[X86_USER_L4_SLOT];
    if (e4 & PTE_P) {
        uint64_t *l3 = table_of(e4);
        for (uint32_t i3 = 0; i3 < TABLE_ENTRIES; i3++) {
            if (!(l3[i3] & PTE_P)) continue;
            uint64_t *l2 = table_of(l3[i3]);
            for (uint32_t i2 = 0; i2 < TABLE_ENTRIES; i2++) {
                if (!(l2[i2] & PTE_P)) continue;
                uint64_t *l1 = table_of(l2[i2]);
                for (uint32_t i1 = 0; i1 < TABLE_ENTRIES; i1++)
                    if (l1[i1] & PTE_P) frame_free(l1[i1] & PTE_ADDR);
                frame_free((uint64_t) (uintptr_t) l1);
            }
            frame_free((uint64_t) (uintptr_t) l2);
        }
        frame_free((uint64_t) (uintptr_t) l3);
    }
    frame_free((uint64_t) (uintptr_t) p->pml4);
    p->pml4 = 0;
}

/* ---- loading -------------------------------------------------------------- */
static int seg_map(void *ctx, const elf_segment_t *seg)
{
    user_proc_t *p = (user_proc_t *) ctx;
    uint64_t start = seg->vaddr & ~0xFFFULL, end = seg->vaddr + seg->mem_size;
    for (uint64_t va = start; va < end; va += PAGE_SIZE_4K) {
        uint64_t pa = frame_alloc();
        if (!pa) return 1;
        if (!proc_map_page(p, va, pa, seg->writable, seg->executable)) {
            frame_free(pa);
            return 1;
        }
    }
    /* Copy through the kernel's identity view of each frame; the user
     * mapping of a code page is never writable. The rest stays zero (BSS). */
    for (uint64_t i = 0; i < seg->file_size; i++) {
        uint64_t va = seg->vaddr + i;
        uint8_t *dst = (uint8_t *) (uintptr_t) (user_pa(p, va) + (va & 0xFFF));
        *dst = seg->data[i];
    }
    return 0;
}

int32_t proc_create_from_elf(proc_scheduler_t *ps, const char *name, const uint8_t *image,
                             uint32_t len)
{
    if (!ps || !g_pool_ready) return -100;
    uint32_t slot = proc_take_slot(ps);
    if (slot >= MAX_USER_PROCS) return -100;
    user_proc_t *p = &ps->procs[slot];

    uint64_t l4 = frame_alloc();
    if (!l4) return -100;
    p->pml4 = (uint64_t *) (uintptr_t) l4;
    /* The kernel half: every entry of the kernel's PML4, shared by pointer,
     * so MMIO the kernel maps later under slot 0 is visible here too. Its
     * leaves are supervisor-only. */
    const uint64_t *k4 = (const uint64_t *) (uintptr_t) (g_kernel_cr3 & PTE_ADDR);
    if (k4[X86_USER_L4_SLOT] & PTE_P) { /* the kernel mapped something in user space */
        proc_abandon(p, slot);
        return -100;
    }
    for (uint32_t i = 0; i < TABLE_ENTRIES; i++) p->pml4[i] = k4[i];

    for (uint64_t va = USER_STACK_BASE; va < USER_STACK_TOP; va += PAGE_SIZE_4K) {
        uint64_t pa = frame_alloc();
        if (!pa || !proc_map_page(p, va, pa, true, false)) {
            if (pa) frame_free(pa);
            proc_abandon(p, slot);
            return -100;
        }
    }

    static const elf_target_t x86 = {ELF_EM_X86_64, USER_CODE_BASE,
                                     USER_CODE_BASE + USER_CODE_SIZE};
    uint64_t entry = 0;
    elf_result_t r = elf_load_target(&x86, image, len, seg_map, p, &entry);
    if (r != ELF_OK) {
        uart_puts("  [PROC] ELF load refused: ");
        uart_puts(elf_strerror(r));
        uart_puts("\n");
        proc_abandon(p, slot);
        return (int32_t) r;
    }

    uint64_t *c = (uint64_t *) &p->ctx;
    for (uint32_t i = 0; i < sizeof(cpu_context_t) / 8u; i++) c[i] = 0;
    p->ctx.rip = entry;
    p->ctx.cs = SEL_UCODE;
    p->ctx.ss = SEL_UDATA;
    p->ctx.rflags = 0x202;            /* IF on, reserved bit 1 */
    p->ctx.rsp = USER_STACK_TOP - 8u; /* as if _start had been called */
    fx_init(p);

    p->pid = ps->next_pid++;
    uint32_t i = 0;
    for (; i < 31u && name[i]; i++) p->name[i] = name[i];
    p->name[i] = 0;
    p->capabilities = CAP_APP_DEFAULT;
    p->priority = 1;
    p->cpu_time_ticks = 0;
    p->quantum_default = 2;
    p->quantum_ticks = 2;
    p->exit_code = 0;
    p->exit_verdict = LPRES_STATE_NEITHER;
    p->fault_vector = 0;
    p->fault_rip = p->fault_addr = 0;
    p->sleep_until_tick = 0;
    p->preempted = 0;
    p->syscalls = 0;
    p->state = PROC_READY;
    ps->num_procs++;
    return (int32_t) p->pid;
}

/* ---- the trap hook -------------------------------------------------------- */
static proc_scheduler_t *g_ps;
static int g_running;
static x86_kctx_t g_kctx;

static const char *exc_name(uint64_t v)
{
    switch (v) {
    case 0:
        return "#DE divide error";
    case 1:
        return "#DB debug";
    case 3:
        return "#BP breakpoint";
    case 4:
        return "#OF overflow";
    case 5:
        return "#BR bound range";
    case 6:
        return "#UD invalid opcode";
    case 7:
        return "#NM no FPU";
    case 10:
        return "#TS invalid TSS";
    case 11:
        return "#NP segment not present";
    case 12:
        return "#SS stack fault";
    case 13:
        return "#GP general protection";
    case 14:
        return "#PF page fault";
    case 16:
        return "#MF x87 error";
    case 17:
        return "#AC alignment check";
    case 19:
        return "#XM SIMD error";
    default:
        return "exception";
    }
}

/* A fault in ring 3 ends that process and nothing else. */
static void user_fault(proc_scheduler_t *ps, user_proc_t *p, const x86_frame_t *f)
{
    p->fault_vector = (uint32_t) f->vector;
    p->fault_rip = f->rip;
    p->fault_addr = f->vector == 14 ? read_cr2() : 0;
    ps->total_faults++;
    uart_puts("  [PROC] pid ");
    uart_put_dec(p->pid);
    uart_puts(" '");
    uart_puts(p->name);
    uart_puts("' ended: ");
    uart_puts(exc_name(f->vector));
    uart_puts(" in ring 3 at rip 0x");
    uart_put_hex(f->rip);
    if (f->vector == 14) {
        uart_puts(" addr 0x");
        uart_put_hex(p->fault_addr);
    }
    uart_puts(" err 0x");
    uart_put_hex(f->error);
    uart_puts("; only this process ends\n");
    proc_end(ps, p, -1, LPRES_STATE_FALSE);
}

static void handle_syscall(proc_scheduler_t *ps, user_proc_t *p)
{
    cpu_context_t *c = &p->ctx;
    ps->total_syscalls++;
    p->syscalls++;
    uint64_t nr = c->rax, a0 = c->rdi;
    /* Permission first, from the table every architecture consults. A number
     * past 32 bits must not truncate into a valid one. */
    int permit =
        nr > 0xFFFFFFFFull ? ZXV_ENOSYS : zxv_syscall_permit((uint32_t) nr, p->capabilities);
    if (permit != ZXV_OK) {
        c->rax = (uint64_t) (int64_t) permit;
        return;
    }
    switch (nr) {
    case SYS_EXIT:
        proc_end(ps, p, (int32_t) a0, LPRES_STATE_TRUE);
        return;
    case SYS_WRITE: /* one byte, as on arm64 (pointer+length is ABI 1.1, gap 7) */
        uart_putc((char) a0);
        c->rax = 1;
        return;
    case SYS_GETPID:
        c->rax = p->pid;
        return;
    case SYS_YIELD:
        c->rax = ZXV_OK;
        p->state = PROC_READY;
        p->quantum_ticks = p->quantum_default;
        return;
    case SYS_SLEEP: /* milliseconds, as on arm64; at most an hour */
        c->rax = ZXV_OK;
        proc_sleep(ps, p, proc_ms_to_ticks(a0 > 3600000u ? 3600000u : a0));
        return;
    case ZXV_SYS_ABI_VERSION:
        c->rax = ZXV_ABI_VERSION;
        return;
    default: /* in the ABI and permitted, but not provided here yet */
        c->rax = (uint64_t) (int64_t) ZXV_ENOSYS;
        return;
    }
}

/* x86_irq.c calls this for every trap from ring 3 (timer, int 0x80, CPU
 * exceptions). The timer's EOI has already been sent. */
void x86_user_trap(x86_frame_t *f)
{
    proc_scheduler_t *ps = g_ps;
    user_proc_t *curr = g_running ? proc_current(ps) : 0;
    if (!curr) { /* ring 3 with no process to own it: nothing sane to return to */
        uart_puts("\n[x86] CPU exception vector=0x");
        uart_put_hex(f->vector);
        uart_puts(" from ring 3 with no running process: halting\n");
        for (;;) __asm__ __volatile__("cli; hlt");
    }
    fx_save(curr);
    copy_frame(&curr->ctx, f);
    uint32_t prev = ps->current_pid;

    if (f->vector == X86_VEC_TIMER)
        proc_on_tick(ps);
    else if (f->vector == X86_VEC_SYSCALL)
        handle_syscall(ps, curr);
    else if (f->vector < 32u && f->vector != 2u) /* an NMI is not the process's doing */
        user_fault(ps, curr, f);
    /* any other vector: nothing is routed, the process just continues */

    uint32_t idx = proc_pick_next(ps);
    /* Nothing runnable: exit, sleep or fault, never the timer (a preempted
     * process is still READY). So this runs on TSS.RSP0, never on IST1,
     * which the idle loop's own ticks reuse. */
    if (idx >= MAX_USER_PROCS) proc_run_next_or_idle(ps);
    user_proc_t *next = &ps->procs[idx];
    if (idx != prev) {
        if (f->vector == X86_VEC_TIMER) {
            ps->total_preemptions++;
            ps->procs[prev].preempted++;
        }
        proc_switch_address_space(next);
    }
    copy_frame(f, &next->ctx);
    fx_restore(next);
}

/* x86_irq.c: a tick taken in ring 0 (the idle loop, or kernel code after
 * the scheduler stopped). It wakes sleepers; it never switches. */
void x86_sched_kernel_tick(void)
{
    if (g_running) proc_on_tick(g_ps);
}

void proc_run_next_or_idle(proc_scheduler_t *ps)
{
    for (;;) {
        __asm__ __volatile__("cli");
        uint32_t idx = proc_pick_next(ps);
        if (idx < MAX_USER_PROCS) {
            user_proc_t *n = &ps->procs[idx];
            proc_switch_address_space(n);
            fx_restore(n);
            x86_frame_resume(&n->ctx);
        }
        if (!proc_any_live(ps)) break;
        /* sti's one-instruction shadow keeps a tick from landing between
         * the check above and the hlt. */
        __asm__ __volatile__("sti; hlt; cli");
    }
    write_cr3(g_kernel_cr3);
    g_running = 0;
    x86_kctx_restore(&g_kctx); /* back into x86_sched_run */
}

void x86_sched_run(proc_scheduler_t *ps)
{
    __asm__ __volatile__("cli");
    g_ps = ps;
    g_running = 1;
    if (x86_kctx_save(&g_kctx) == 0) proc_run_next_or_idle(ps);
    /* every process has ended; interrupts are off */
}

/* ---- CPU features the runtime depends on --------------------------------- */
static void cpu_setup(void)
{
    uint32_t a, b, c, d;
    g_kernel_cr3 = read_cr3();
    /* NX for stack and data pages. */
    cpuid(0x80000000u, &a, &b, &c, &d);
    if (a >= 0x80000001u) {
        cpuid(0x80000001u, &a, &b, &c, &d);
        if (d & (1u << 20)) {
            wrmsr(0xC0000080u, rdmsr(0xC0000080u) | (1ull << 11)); /* EFER.NXE */
            g_nx = 1;
        }
    }
    uint64_t cr0, cr4;
    __asm__ __volatile__("mov %%cr0, %0" : "=r"(cr0));
    __asm__ __volatile__("mov %%cr4, %0" : "=r"(cr4));
    cr0 &= ~(1ull << 3);               /* TS: FXSAVE must not trap */
    cr4 &= ~(1ull << 2);               /* TSD: user rdtsc is allowed */
    cr4 |= (1ull << 9) | (1ull << 10); /* OSFXSR, OSXMMEXCPT (boot sets them) */
    __asm__ __volatile__("mov %0, %%cr0" : : "r"(cr0));
    __asm__ __volatile__("mov %0, %%cr4" : : "r"(cr4));
    /* FXSAVE covers x87 and SSE only. If firmware left XSAVE on with AVX or
     * more enabled, turn those off so no user state escapes the save. Saving
     * AVX state with XSAVE is a TODO. */
    if (cr4 & (1ull << 18)) {
        uint32_t lo, hi;
        __asm__ __volatile__("xgetbv" : "=a"(lo), "=d"(hi) : "c"(0));
        if (lo != 3u || hi != 0u) __asm__ __volatile__("xsetbv" : : "a"(3u), "d"(0u), "c"(0));
    }
}

/* ---- boot self-test ------------------------------------------------------- */
extern const uint8_t x86_selftest_elf[];
extern const uint8_t x86_selftest_elf_end[];

/* Roles the user program takes from rdi (kernel/userapp/x86_selftest.c). */
enum {
    ROLE_YIELD = 1,
    ROLE_SPIN = 2,
    ROLE_SLEEP = 3,
    ROLE_PF = 10,
    ROLE_GP = 11,
    ROLE_UD = 12,
    ROLE_DE = 13
};

typedef struct {
    const char *name;
    uint64_t role, a1, a2;
    lpres_state_t want; /* TRUE: exits 0 by itself; FALSE: ended by its fault */
    uint32_t vector;    /* the fault expected when want is FALSE */
    int32_t pid;
} selftest_proc_t;

#define SELFTEST_MIN_PREEMPTIONS 4u

static proc_scheduler_t g_sched;

int x86_sched_selftest(void)
{
    if (x86_irq_info()->tsc_per_tick == 0) {
        uart_puts("  [PROC] no calibrated timer: scheduler self-test skipped\n");
        return -1;
    }
    cpu_setup();
    pool_init();
    proc_sched_init(&g_sched);
    uint32_t free0 = proc_user_pages_free();
    uart_puts("  [PROC] frame pool ");
    uart_put_dec(g_upmm.nframes);
    uart_puts(" frames at 0x");
    uart_put_hex(g_upmm.base);
    uart_puts(g_pool_from_map ? " (usable RAM from the handoff memory map)"
                              : " (static pool: no handoff map)");
    uart_puts(g_nx ? ", NX on\n" : ", no NX\n");

    /* Spinners burn 15 ticks of TSC time each, checking their xmm registers
     * all along: if FP state leaked between them the check would fail. */
    uint64_t spin = x86_irq_info()->tsc_per_tick * 15u;
    selftest_proc_t t[] = {
        {"yield", ROLE_YIELD, 8, 0, LPRES_STATE_TRUE, 0, 0},
        {"spin-a", ROLE_SPIN, spin, 0x5A5A0001A5A5F00Dull, LPRES_STATE_TRUE, 0, 0},
        {"spin-b", ROLE_SPIN, spin, 0xC0DE0002BEEF7777ull, LPRES_STATE_TRUE, 0, 0},
        {"sleep", ROLE_SLEEP, 3, 20, LPRES_STATE_TRUE, 0, 0},
        {"fault-pf", ROLE_PF, 0, 0, LPRES_STATE_FALSE, 14, 0},
        {"fault-gp", ROLE_GP, 0, 0, LPRES_STATE_FALSE, 13, 0},
        {"fault-ud", ROLE_UD, 0, 0, LPRES_STATE_FALSE, 6, 0},
        {"fault-de", ROLE_DE, 0, 0, LPRES_STATE_FALSE, 0, 0},
    };
    const uint32_t n = sizeof t / sizeof t[0];
    uint32_t len = (uint32_t) (x86_selftest_elf_end - x86_selftest_elf);
    for (uint32_t i = 0; i < n; i++) {
        t[i].pid = proc_create_from_elf(&g_sched, t[i].name, x86_selftest_elf, len);
        user_proc_t *p = t[i].pid > 0 ? proc_get(&g_sched, (uint32_t) t[i].pid) : 0;
        if (!p) {
            uart_puts("  [PROC] could not create ");
            uart_puts(t[i].name);
            uart_puts("\n");
            continue;
        }
        p->ctx.rdi = t[i].role;
        p->ctx.rsi = t[i].a1;
        p->ctx.rdx = t[i].a2;
    }
    uart_puts("  [PROC] ");
    uart_put_dec(g_sched.num_procs);
    uart_puts(" ring-3 processes from one static ELF64 (");
    uart_put_dec(len);
    uart_puts(" bytes), own PML4 each; user output follows\n");

    uint64_t t0 = x86_ticks();
    x86_sched_run(&g_sched);
    uint64_t took = x86_ticks() - t0;
    __asm__ __volatile__("sti");

    /* The decision: every process ended the way it was meant to (a check per
     * process, TRUE when it matched), enough timer preemptions, and every
     * frame back in the pool. All must hold, so they are conjoined: one
     * FALSE refuses [SCHED_OK], one still-live process (NEITHER) holds it. */
    lpres_state_t ok = LPRES_STATE_TRUE;
    uart_puts("\n");
    for (uint32_t i = 0; i < n; i++) {
        user_proc_t *p = t[i].pid > 0 ? proc_get(&g_sched, (uint32_t) t[i].pid) : 0;
        lpres_state_t c;
        if (!p)
            c = LPRES_STATE_FALSE;
        else if (p->state != PROC_TERMINATED)
            c = LPRES_STATE_NEITHER;
        else if (p->exit_verdict != t[i].want)
            c = LPRES_STATE_FALSE;
        else if (t[i].want == LPRES_STATE_TRUE)
            c = p->exit_code == 0 ? LPRES_STATE_TRUE : LPRES_STATE_FALSE;
        else
            c = p->fault_vector == t[i].vector ? LPRES_STATE_TRUE : LPRES_STATE_FALSE;
        ok = lpres_conjoin(ok, c);
        uart_puts("  [PROC] pid ");
        uart_put_dec(p ? p->pid : 0);
        uart_puts(" ");
        uart_puts(t[i].name);
        if (p && p->exit_verdict == LPRES_STATE_TRUE) {
            uart_puts(": exited ");
            uart_put_dec((uint64_t) (uint32_t) p->exit_code);
        } else if (p && p->exit_verdict == LPRES_STATE_FALSE) {
            uart_puts(": ended by vector ");
            uart_put_dec(p->fault_vector);
        } else {
            uart_puts(": did not end");
        }
        if (p) {
            uart_puts(", ");
            uart_put_dec(p->syscalls);
            uart_puts(" syscalls, preempted ");
            uart_put_dec(p->preempted);
            uart_puts("x");
        }
        uart_puts(c == LPRES_STATE_TRUE ? " (as expected)\n" : " (NOT as expected)\n");
    }
    lpres_state_t pre = g_sched.total_preemptions >= SELFTEST_MIN_PREEMPTIONS ? LPRES_STATE_TRUE
                                                                              : LPRES_STATE_FALSE;
    lpres_state_t mem = proc_user_pages_free() == free0 && g_frames_fenced == 0 ? LPRES_STATE_TRUE
                                                                                : LPRES_STATE_FALSE;
    ok = lpres_conjoin(lpres_conjoin(ok, pre), mem);

    uart_puts("  [PROC] ");
    uart_put_dec(g_sched.total_preemptions);
    uart_puts(" timer preemptions, ");
    uart_put_dec(g_sched.total_context_switches);
    uart_puts(" context switches, ");
    uart_put_dec(g_sched.total_syscalls);
    uart_puts(" syscalls, ");
    uart_put_dec(g_sched.total_faults);
    uart_puts(" user faults contained, ");
    uart_put_dec(took);
    uart_puts(" ticks; frames ");
    uart_put_dec(proc_user_pages_free());
    uart_puts("/");
    uart_put_dec(free0);
    uart_puts(" free after\n");
    if (ok == LPRES_STATE_TRUE) {
        uart_puts("[SCHED_OK] x86_64 preemptive scheduler: ring-3 processes, FXSAVE, per-process "
                  "page tables, user faults end only their process\n");
        return 0;
    }
    uart_puts("[SCHED_FAIL] x86_64 scheduler self-test did not hold (see the lines above)\n");
    return -1;
}
