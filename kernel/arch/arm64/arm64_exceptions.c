/* arm64_exceptions.c — Synchronous/IRQ exception dispatch for ARM64
 *
 * boot.s's vector_table calls irq_handler_c()/exception_handler() by
 * symbol name for every IRQ / synchronous exception taken at EL1
 * with SP_ELx (see irq_handler_curr_elx / sync_handler_curr_elx).
 * kernel/compat/compat_layer.c defines both as weak empty stubs for
 * architectures that don't need them; ARM64 does, and nothing
 * previously provided the strong override -- every enabled IRQ
 * (including the generic timer PPI) silently did nothing: no
 * gic_handle_irq() (so no IAR read, no dispatch to the registered
 * handler, no EOI), and any real synchronous fault would eret
 * straight back into the faulting instruction forever with zero
 * diagnostic output.
 *
 * F3 fix: Added el0_sync_handler_c and el0_irq_handler_c for
 * lower-EL (EL0) exception dispatch — SVC syscall gate and
 * timer-driven preemptive scheduling.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * License: SEL-3.3
 */
#include "arm64_arch.h"

extern void gic_handle_irq(void);
extern void uart_puts(const char *s);
extern void uart_put_hex(uint64_t val);
extern void uart_put_dec(uint64_t val);

/* EL0 scheduler — defined in el0_userspace.c */
#include "el0_userspace.h"

/* Global pointer to the EL0 process scheduler.
 * Set by kernel_main_arm64.c before entering EL0. */
static proc_scheduler_t *g_el0_sched = 0;

void el0_set_scheduler(proc_scheduler_t *ps) {
    g_el0_sched = ps;
}

/* Accessor so the shell (kernel_shell_exec, running in the SVC handler
 * at EL1) can create a new EL0 process for `run <file>` and let the
 * existing preemptive scheduler pick it up on the next tick. */
proc_scheduler_t *el0_get_scheduler(void) {
    return g_el0_sched;
}

/* Kernel event-cycle hook — the external-clock bridge for the unified
 * runtime.  When EL0 user space is active, kernel_main's event loop is
 * never reached (proc_enter_el0 ERETs away), so the generic-timer IRQ
 * itself drives one kernel event cycle per tick through this hook.
 * This keeps the event-space/self-audit/subsystem fabric live while
 * user processes run — exactly the boundary described in
 * ARCHITECTURE_EXTERNAL_CLOCK_BRIDGE.md: the ISR is the only consumer
 * of the raw clock; everything else sees event cycles.
 * Set by kernel_main_arm64.c only in the EL0+event-loop configuration;
 * NULL otherwise (the classic kernel event loop then runs unchanged). */
static void (*g_event_cycle_hook)(void) = 0;

void el0_set_event_cycle_hook(void (*fn)(void)) {
    g_event_cycle_hook = fn;
}

/* Build a cpu_context_t from the saved register frame on stack.
 * The SAVE_REGS macro stores x0-x30 at sp[0..240], then we read
 * ELR_EL1 (PC) and SPSR_EL1 (PSTATE) and SP_EL0. */
static void frame_to_context(void *frame, cpu_context_t *ctx) {
    uint64_t *regs = (uint64_t *)frame;
    for (int i = 0; i < 31; i++)
        ctx->x[i] = regs[i];
    __asm__ volatile("mrs %0, ELR_EL1"  : "=r"(ctx->pc));
    __asm__ volatile("mrs %0, SPSR_EL1" : "=r"(ctx->pstate));
    __asm__ volatile("mrs %0, SP_EL0"   : "=r"(ctx->sp));
}

/* IRQ path: read-acknowledge (IAR), dispatch to the registered
 * gic_register_handler() callback, end-of-interrupt (EOIR). frame
 * (the SAVE_REGS block) is unused today; kept so a future
 * per-task/per-IRQ context inspection doesn't need an ABI change. */
void irq_handler_c(void *frame) {
    (void)frame;
    gic_handle_irq();
    /* Unified runtime: if the event-cycle hook is registered (EL0 mode),
     * drive one kernel event cycle per EL1-taken tick as well — this
     * covers the windows where the CPU is at EL1 (boot tail, WFI idle
     * after all user processes exit). */
    if (g_event_cycle_hook)
        g_event_cycle_hook();
    /* Keep EL0 scheduler time flowing while the CPU idles at EL1
     * (e.g. WFI inside the SYS_EXIT idle loop).  ticks normally
     * advance in proc_sched_tick on the EL0 IRQ path; when the timer
     * fires at EL1 instead, advance them here so SLEEPING processes
     * still reach their wake tick and become READY. */
    if (g_el0_sched && g_el0_sched->initialized) {
        g_el0_sched->ticks++;
        proc_wake_eligible(g_el0_sched);
    }
}

static const char *ec_name(uint32_t ec) {
    switch (ec) {
        case 0x00: return "Unknown reason";
        case 0x0E: return "Illegal Execution State";
        case 0x15: return "SVC instruction (AArch64)";
        case 0x18: return "MSR/MRS/system instruction trap";
        case 0x20: return "Instruction Abort (lower EL)";
        case 0x21: return "Instruction Abort (same EL)";
        case 0x22: return "PC alignment fault";
        case 0x24: return "Data Abort (lower EL)";
        case 0x25: return "Data Abort (same EL)";
        case 0x26: return "SP alignment fault";
        case 0x2C: return "FP/SIMD trap";
        default:   return "Other/unhandled EC";
    }
}

/* Synchronous exception path. boot.s passes (sp, type, ESR_EL1) in
 * x0/x1/x2 per AAPCS64 -- matched here exactly (the weak stub this
 * overrides took zero parameters, so ESR_EL1 was silently discarded
 * and there was no way to diagnose a fault from the serial console).
 * There is deliberately no recovery path: a synchronous fault this
 * kernel doesn't explicitly handle elsewhere is unrecoverable, so we
 * report it and halt with interrupts masked (the default PSTATE on
 * synchronous-exception entry) rather than silently spin. */
void exception_handler(void *frame, uint64_t type, uint64_t esr) {
    (void)frame;
    (void)type;
    uint32_t ec = (uint32_t)((esr >> 26) & 0x3F);
    uint64_t iss = esr & 0x1FFFFFFULL;

    uint64_t far, elr, spsr, sp_el0, ttbr0, ttbr1, tcr, mair, sctlr;
    __asm__ volatile("mrs %0, FAR_EL1"     : "=r"(far));
    __asm__ volatile("mrs %0, ELR_EL1"     : "=r"(elr));
    __asm__ volatile("mrs %0, SPSR_EL1"    : "=r"(spsr));
    __asm__ volatile("mrs %0, SP_EL0"      : "=r"(sp_el0));
    __asm__ volatile("mrs %0, TTBR0_EL1"   : "=r"(ttbr0));
    __asm__ volatile("mrs %0, TTBR1_EL1"   : "=r"(ttbr1));
    __asm__ volatile("mrs %0, TCR_EL1"     : "=r"(tcr));
    __asm__ volatile("mrs %0, MAIR_EL1"    : "=r"(mair));
    __asm__ volatile("mrs %0, SCTLR_EL1"   : "=r"(sctlr));

    uart_puts("\n[FAULT] Synchronous exception -- ");
    uart_puts(ec_name(ec));
    uart_puts("\n  ESR_EL1=0x");
    uart_put_hex(esr);
    uart_puts("  EC=0x");
    uart_put_hex((uint64_t)ec);
    uart_puts("  ISS=0x");
    uart_put_hex(iss);
    uart_puts("\n  FAR_EL1=0x");
    uart_put_hex(far);
    uart_puts("\n  ELR_EL1=0x");
    uart_put_hex(elr);
    uart_puts("  SPSR_EL1=0x");
    uart_put_hex(spsr);
    uart_puts("\n  SP_EL0=0x");
    uart_put_hex(sp_el0);
    uart_puts("  TTBR0_EL1=0x");
    uart_put_hex(ttbr0);
    uart_puts("  TTBR1_EL1=0x");
    uart_put_hex(ttbr1);
    uart_puts("\n  TCR_EL1=0x");
    uart_put_hex(tcr);
    uart_puts("  MAIR_EL1=0x");
    uart_put_hex(mair);
    uart_puts("  SCTLR_EL1=0x");
    uart_put_hex(sctlr);
    uart_puts("\n[FAULT] Halting.\n");

    while (1) {
        halt();
    }
}

/* ---- EL0 exception handlers ---- */

/* Synchronous exception from EL0 (SVC, data abort, instruction abort, etc.)
 * frame = saved x0-x30 on stack
 * type  = exception type (0 = sync)
 * esr   = ESR_EL1
 * elr   = ELR_EL1 (faulting PC)
 * pstate = SPSR_EL1
 */
void el0_sync_handler_c(void *frame, uint64_t type, uint64_t esr,
                         uint64_t elr, uint64_t pstate) {
    (void)type;
    uint32_t ec = (uint32_t)((esr >> 26) & 0x3F);

    if (ec == 0x15) {
        /* SVC instruction from EL0 — syscall gate */
        if (!g_el0_sched) {
            uart_puts("[EL0] SVC but no scheduler — halting\n");
            while (1) halt();
        }

        cpu_context_t ctx;
        frame_to_context(frame, &ctx);

        /* syscall number in x8, args in x0-x5 */
        uint64_t syscall_num = ctx.x[8];
        uint64_t args[6] = { ctx.x[0], ctx.x[1], ctx.x[2],
                             ctx.x[3], ctx.x[4], ctx.x[5] };

        /* For SVC, ELR_EL1 already points to the instruction after SVC */
        ctx.pc = elr;

        proc_handle_svc(g_el0_sched, syscall_num, args, &ctx);

        /* Write the (possibly modified) context back for ERET */
        __asm__ volatile("msr ELR_EL1,  %0" :: "r"(ctx.pc));
        __asm__ volatile("msr SPSR_EL1, %0" :: "r"(pstate));
        __asm__ volatile("msr SP_EL0,   %0" :: "r"(ctx.sp));

        /* Write x0 (return value) and x8 back into the stack frame
         * so RESTORE_REGS loads the updated values */
        uint64_t *regs = (uint64_t *)frame;
        regs[0] = ctx.x[0];
        regs[8] = ctx.x[8];
        return;
    }

    /* Any other synchronous exception from EL0 is a fault.
     * If we have a scheduler, terminate the offending process. */
    if (g_el0_sched) {
        user_proc_t *curr = proc_current(g_el0_sched);
        if (curr) {
            uint64_t far, sp_el0, ttbr0, ttbr1, tcr, sctlr;
            __asm__ volatile("mrs %0, FAR_EL1"   : "=r"(far));
            __asm__ volatile("mrs %0, SP_EL0"    : "=r"(sp_el0));
            __asm__ volatile("mrs %0, TTBR0_EL1" : "=r"(ttbr0));
            __asm__ volatile("mrs %0, TTBR1_EL1" : "=r"(ttbr1));
            __asm__ volatile("mrs %0, TCR_EL1"   : "=r"(tcr));
            __asm__ volatile("mrs %0, SCTLR_EL1" : "=r"(sctlr));

            uart_puts("\n[EL0 FAULT] PID=");
            uart_put_dec((uint64_t)curr->pid);
            uart_puts(" EC=");
            uart_puts(ec_name(ec));
            uart_puts("\n  ESR_EL1=0x");
            uart_put_hex(esr);
            uart_puts(" EC=0x");
            uart_put_hex((uint64_t)ec);
            uart_puts(" ISS=0x");
            uart_put_hex(esr & 0x1ffffffULL);
            uart_puts("\n  ELR_EL1=0x");
            uart_put_hex(elr);
            uart_puts("  SPSR_EL1=0x");
            uart_put_hex(pstate);
            uart_puts("\n  FAR_EL1=0x");
            uart_put_hex(far);
            uart_puts("  SP_EL0=0x");
            uart_put_hex(sp_el0);
            uart_puts("\n  TTBR0=0x");
            uart_put_hex(ttbr0);
            uart_puts("  TTBR1=0x");
            uart_put_hex(ttbr1);
            uart_puts("\n  TCR=0x");
            uart_put_hex(tcr);
            uart_puts("  SCTLR=0x");
            uart_put_hex(sctlr);
            uart_puts("\n  terminating process\n");
            proc_terminate(g_el0_sched, curr->pid, -1);

            /* Pick next ready process and ERET to it */
            for (uint32_t i = 1; i <= MAX_USER_PROCS; i++) {
                uint32_t idx = (g_el0_sched->current_pid + i) % MAX_USER_PROCS;
                if (g_el0_sched->procs[idx].state == PROC_READY) {
                    g_el0_sched->current_pid = idx;
                    g_el0_sched->procs[idx].state = PROC_RUNNING;
                    g_el0_sched->procs[idx].quantum_ticks =
                        g_el0_sched->procs[idx].quantum_default;
                    proc_switch_address_space(&g_el0_sched->procs[idx]);
                    __asm__ volatile("msr ELR_EL1,  %0" :: "r"(g_el0_sched->procs[idx].ctx.pc));
                    __asm__ volatile("msr SPSR_EL1, %0" :: "r"(0ULL));
                    __asm__ volatile("msr SP_EL0,   %0" :: "r"(g_el0_sched->procs[idx].ctx.sp));
                    uint64_t *regs = (uint64_t *)frame;
                    for (int j = 0; j < 31; j++)
                        regs[j] = g_el0_sched->procs[idx].ctx.x[j];
                    return;
                }
            }
            /* No ready processes — return to kernel idle */
            uart_puts("[EL0] No ready processes after fault — kernel idle\n");
        }
    }

    /* No scheduler or no current process — treat as fatal */
    uart_puts("\n[EL0 FAULT] ");
    uart_puts(ec_name(ec));
    uart_puts(" ESR=0x");
    uart_put_hex(esr);
    uart_puts(" PC=0x");
    uart_put_hex(elr);
    uart_puts(" SPSR=0x");
    uart_put_hex(pstate);
    uart_puts("\n[EL0 FAULT] Halting.\n");
    while (1) halt();
}

/* Timer IRQ from EL0 — preemptive scheduling.
 * frame = saved x0-x30 on stack (the EL0 context at IRQ entry) */
void el0_irq_handler_c(void *frame) {
    /* Acknowledge the GIC interrupt */
    gic_handle_irq();

    /* Unified runtime: drive one kernel event cycle per timer tick
     * while user space runs.  Must happen BEFORE proc_sched_tick —
     * the scheduler may ERET directly to the next process and never
     * return to this handler. */
    if (g_event_cycle_hook)
        g_event_cycle_hook();

    if (!g_el0_sched) {
        /* No EL0 scheduler — this shouldn't happen, but handle gracefully */
        return;
    }

    /* Build a cpu_context_t from the saved frame + ELR/SPSR/SP_EL0 */
    cpu_context_t ctx;
    frame_to_context(frame, &ctx);

    /* Call the preemptive scheduler tick — this may switch processes */
    proc_sched_tick(g_el0_sched, &ctx);

    /* proc_sched_tick has already called proc_restore_el0 if it switched,
     * which does an ERET. If it returned, we need to ERET back to the
     * same or next process. The scheduler has already set up TTBR0,
     * ELR, SPSR, SP_EL0. We just need to write x0-x30 back to the frame. */
    user_proc_t *curr = proc_current(g_el0_sched);
    if (curr) {
        uint64_t *regs = (uint64_t *)frame;
        for (int j = 0; j < 31; j++)
            regs[j] = curr->ctx.x[j];
        __asm__ volatile("msr ELR_EL1,  %0" :: "r"(curr->ctx.pc));
        __asm__ volatile("msr SPSR_EL1, %0" :: "r"(0ULL));
        __asm__ volatile("msr SP_EL0,   %0" :: "r"(curr->ctx.sp));
    }
}
