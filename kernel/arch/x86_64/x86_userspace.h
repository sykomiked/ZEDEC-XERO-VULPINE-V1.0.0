/* x86_userspace.h: x86-64 ring-3 processes with per-process address spaces
 *
 * The x86-64 counterpart of arch/arm64/el0_userspace.h, with the same API
 * shape and the same shared ABI (src/syscall/syscall.h, int 0x80 with the
 * number in rax, arguments in rdi/rsi/rdx, the result in rax):
 *
 *   - each process has its own PML4. The kernel's entries are copied in
 *     (supervisor-only leaves, so ring 3 cannot touch them) and user space is
 *     one PML4 slot of its own, X86_USER_L4_SLOT, with U/S set down its tree
 *   - the LAPIC timer preempts round-robin; a syscall or fault saves the
 *     frame into the process, and the next process's frame is restored
 *   - FP/SIMD state is saved with FXSAVE into the process on every entry from
 *     ring 3 and restored with FXRSTOR on the way out. The kernel itself is
 *     integer only (-mgeneral-regs-only); the save area is user state only.
 *   - a fault in ring 3 ends that process alone; the scheduler continues
 *
 * Every page a process uses (tables, code, stack) is a frame from the
 * four-state allocator (src/pmm) and goes back to it when the process ends.
 *
 * Portable logic (slots, ticks, round robin, sleep) is in x86_sched.c and
 * host-tested; page tables, FXSAVE, the trap hook and the boot self-test are
 * in x86_userspace.c.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#ifndef X86_USERSPACE_H
#define X86_USERSPACE_H

#include <stdint.h>
#include <stdbool.h>
#include "x86_irq.h"
#include "zxv_syscall_abi.h"
#include "lpres.h"

/* Maximum user-space processes (as arm64) */
#define MAX_USER_PROCS 32

/* Syscall numbers, the arm64 header's names over the shared table. */
#define SYS_EXIT   ZXV_SYS_EXIT
#define SYS_WRITE  ZXV_SYS_WRITE
#define SYS_GETPID ZXV_SYS_GETPID
#define SYS_YIELD  ZXV_SYS_YIELD
#define SYS_SLEEP  ZXV_SYS_SLEEP
#define SYS_SEND   ZXV_SYS_SEND
#define SYS_RECV   ZXV_SYS_RECV
#define SYS_OPEN   ZXV_SYS_OPEN
#define SYS_CLOSE  ZXV_SYS_CLOSE
#define SYS_READ   ZXV_SYS_READ
#define SYS_EXEC   ZXV_SYS_EXEC

/* User virtual layout. User space is PML4 slot 255 (the top 512 GB of the
 * lower half), never slot 0: the kernel's identity map and every MMIO window
 * hwplat_x86 adds live under slot 0, shared by pointer with every process,
 * and a physical address that high needs 47 address bits, so no BAR lands in
 * slot 255 in practice (a process is refused if the kernel ever maps it).
 * Offsets inside it follow arm64: code at +64 KB, stack below +512 KB + size.
 * The page under the stack is never mapped (guard). */
#define X86_USER_L4_SLOT 255u
#define USER_SPACE_BASE  0x00007F8000000000ULL
#define USER_CODE_BASE   (USER_SPACE_BASE + 0x00010000ULL)
#define USER_CODE_SIZE   0x00070000ULL /* the ELF window: code + data, 448 KB */
#define USER_STACK_BASE  (USER_SPACE_BASE + 0x00080000ULL)
#define USER_STACK_SIZE  0x00004000ULL /* 16 KB */
#define USER_STACK_TOP   (USER_STACK_BASE + USER_STACK_SIZE)

#define PAGE_SIZE_4K  4096ULL
#define TABLE_ENTRIES 512

/* Page table entry bits */
#define PTE_P    (1ULL << 0)
#define PTE_W    (1ULL << 1)
#define PTE_U    (1ULL << 2)
#define PTE_PS   (1ULL << 7)
#define PTE_NX   (1ULL << 63)
#define PTE_ADDR 0x000FFFFFFFFFF000ULL

/* Per-process capabilities; the same bits as arm64 and the ABI table. */
#define CAP_WRITE       (1u << 0)
#define CAP_PROC        (1u << 1)
#define CAP_EXEC        (1u << 2)
#define CAP_IPC         (1u << 3)
#define CAP_FS          (1u << 4)
#define CAP_APP_DEFAULT (CAP_WRITE | CAP_PROC)

/* Process states (the same values as arm64) */
typedef enum {
    PROC_UNUSED = 0,
    PROC_READY = 1,
    PROC_RUNNING = 2,
    PROC_BLOCKED = 3,
    PROC_SLEEPING = 4,
    PROC_TERMINATED = 5
} proc_state_t;

/* Saved ring-3 CPU context: the trap frame itself. */
typedef x86_frame_t cpu_context_t;

/* User process control block. fx first: FXSAVE needs 16-byte alignment. */
typedef struct {
    uint8_t fx[512] __attribute__((aligned(16))); /* FXSAVE area (user FP/SIMD) */
    uint32_t pid;
    char name[32];
    proc_state_t state;
    uint32_t capabilities;

    uint64_t *pml4; /* this process's PML4 (a pmm frame); 0 = none */

    cpu_context_t ctx;

    uint32_t priority;
    uint32_t cpu_time_ticks;
    uint32_t quantum_ticks;
    uint32_t quantum_default;
    int32_t exit_code;
    /* Process exit as a decision (docs/BAREMETAL_OS_GAPS.md section 3): TRUE
     * exited by itself, FALSE ended by the kernel (fault or refused),
     * NEITHER still live or never ran. */
    lpres_state_t exit_verdict;
    uint32_t fault_vector; /* the CPU exception that ended it */
    uint64_t fault_rip, fault_addr;

    uint64_t sleep_until_tick;
    uint32_t preempted; /* times the timer switched away from it */
    uint32_t syscalls;
} __attribute__((aligned(16))) user_proc_t;

/* Scheduler state */
typedef struct {
    user_proc_t procs[MAX_USER_PROCS];
    uint32_t num_procs;   /* live (not UNUSED, not TERMINATED) */
    uint32_t current_pid; /* index into procs[] of the running process */
    uint32_t next_pid;
    uint64_t ticks;
    bool initialized;

    uint32_t total_context_switches;
    uint32_t total_preemptions; /* timer-forced switches to another process */
    uint32_t total_syscalls;
    uint32_t total_faults; /* processes ended by a user fault */
} proc_scheduler_t;

/* ---- portable (x86_sched.c, host-tested) -------------------------------- */

void proc_sched_init(proc_scheduler_t *ps);
user_proc_t *proc_current(proc_scheduler_t *ps);
user_proc_t *proc_get(proc_scheduler_t *ps, uint32_t pid);

/* Pick a process slot: an UNUSED one first, else reap the first TERMINATED
 * one. MAX_USER_PROCS when every slot holds a live process. (arm64 has the
 * same rule, static in el0_userspace.c.) */
uint32_t proc_take_slot(proc_scheduler_t *ps);
/* Undo a half-built process: free what it took and mark the slot unused. */
void proc_abandon(user_proc_t *proc, uint32_t slot);
/* End a live process: TERMINATED, exit code and verdict kept, its address
 * space released. A second call for the same pid changes nothing. */
void proc_terminate(proc_scheduler_t *ps, uint32_t pid, int32_t exit_code);
void proc_end(proc_scheduler_t *ps, user_proc_t *p, int32_t exit_code, lpres_state_t verdict);

/* The next READY slot after current_pid, round robin (current last), or
 * MAX_USER_PROCS. */
uint32_t proc_find_next_ready(proc_scheduler_t *ps);
/* One timer tick: advance the clock, wake sleepers, charge the running
 * process and expire its quantum (RUNNING -> READY). */
void proc_on_tick(proc_scheduler_t *ps);
/* Who runs now: the current process if still RUNNING, else the next READY
 * one (made RUNNING, current_pid moved, quantum refilled). MAX_USER_PROCS
 * when nothing can run. */
uint32_t proc_pick_next(proc_scheduler_t *ps);
/* Is any process READY, RUNNING, SLEEPING or BLOCKED? */
bool proc_any_live(proc_scheduler_t *ps);
void proc_wake_eligible(proc_scheduler_t *ps);
/* Sleep the running process for n ticks (READY at once when n is 0). */
void proc_sleep(proc_scheduler_t *ps, user_proc_t *p, uint64_t n);
/* Milliseconds to ticks at X86_HZ, rounded up. */
uint64_t proc_ms_to_ticks(uint64_t ms);

/* ---- arch (x86_userspace.c) --------------------------------------------- */

/* Create a ring-3 process from a static x86-64 ELF64 image (EM_X86_64,
 * segments inside USER_CODE_BASE .. +USER_CODE_SIZE). Returns the pid, or a
 * negative elf_result_t, or -100 when out of slots or frames. */
int32_t proc_create_from_elf(proc_scheduler_t *ps, const char *name, const uint8_t *image,
                             uint32_t len);

/* Map one 4 KB user page in a process's tables. */
bool proc_map_page(user_proc_t *proc, uint64_t va, uint64_t pa, bool writable, bool executable);

/* Load CR3 with the process's PML4. */
void proc_switch_address_space(user_proc_t *proc);

/* Run the next READY process, or idle with interrupts on until one wakes.
 * Called after the current process exited, slept or faulted. Does not
 * return: with no live process left it resumes the kernel at the point that
 * started the scheduler (x86_sched_run). */
__attribute__((noreturn)) void proc_run_next_or_idle(proc_scheduler_t *ps);

/* Start the scheduler from kernel code; returns when no live process is
 * left. */
void x86_sched_run(proc_scheduler_t *ps);

/* Free frames left in the user frame pool. */
uint32_t proc_user_pages_free(void);

/* Boot self-test: processes that make syscalls, yield, sleep and get
 * preempted, and four that fault (#PF, #GP, #UD, #DE). Prints [SCHED_OK]
 * when every check holds. Returns 0 on success. */
int x86_sched_selftest(void);

#endif /* X86_USERSPACE_H */
