/* el0_userspace.h — ARM64 EL0 User Space with Per-Process Address Spaces
 *
 * Implements true privilege separation: user processes run at EL0 with
 * their own page tables (TTBR0_EL1), isolated from kernel space
 * (TTBR1_EL1). The kernel runs at EL1 and transitions to EL0 via ERET.
 *
 * Key design:
 * - Each user process has its own L0 page table for TTBR0_EL1
 * - Kernel space is identity-mapped via TTBR1_EL1 (shared, never switched)
 * - User space is mapped in TTBR0_EL1 (per-process, switched on context change)
 * - TCR_EL1 configured for split EL1/EL0 translation regimes
 * - SVC instruction from EL0 triggers synchronous exception at EL1
 *   for syscall dispatch
 * - Timer IRQ forces preemption: saves EL0 context, switches process
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#ifndef EL0_USERSPACE_H
#define EL0_USERSPACE_H

#include <stdint.h>
#include <stdbool.h>

/* ARM64 arch helpers are needed only by the implementation, not the
 * header. This allows the header (struct/enum/constant definitions)
 * to be included in host-side tests without ARM64 inline asm. */

/* Maximum user-space processes */
#define MAX_USER_PROCS  32

/* IPC message passing */
#define IPC_MSG_SIZE    256     /* max message payload */
#define IPC_QUEUE_DEPTH 8       /* messages per process queue */
#define IPC_MAX_PAYLOAD 248     /* payload after header */

/* Syscall numbers (extended) */
#define SYS_EXIT        1
#define SYS_WRITE       2
#define SYS_GETPID      3
#define SYS_YIELD       4
#define SYS_SLEEP       5
#define SYS_SEND        6
#define SYS_RECV        7
#define SYS_OPEN        8
#define SYS_CLOSE       9
#define SYS_READ        10
#define SYS_EXEC        11      /* execute a shell command line (P-TERM) */

/* User-space virtual address layout:
 *   0x0000000000 – 0x0000FFFFFFFF : user space (TTBR0, per-process)
 *   0xFFFF000000000000+           : kernel space (TTBR1, shared, identity)
 *
 * On ARM64 with TCR_EL1.T0SZ=25, the user VA range is 39 bits
 * (0 – 0x7FFFFFFFFF). We use a simpler 32-bit-compatible subset
 * since our QEMU virt RAM starts at 0x40000000.
 *
 * User code loads at USER_CODE_BASE, stack grows down from USER_STACK_TOP.
 */
#define USER_CODE_BASE   0x00010000ULL    /* 64KB – user code start */
#define USER_CODE_SIZE   0x00010000ULL    /* 64KB max user code */
#define USER_STACK_BASE  0x00080000ULL    /* 512KB – user stack region */
#define USER_STACK_SIZE  0x00010000ULL    /* 64KB stack */
#define USER_STACK_TOP   (USER_STACK_BASE + USER_STACK_SIZE)

/* Page table constants (4KB granule, 48-bit VA) */
#define PAGE_SIZE_4K     4096ULL
#define PAGE_SHIFT       12
#define TABLE_ENTRIES    512  /* 512 entries per table level */

/* Page table entry bits */
#define PTE_VALID        (1ULL << 0)
#define PTE_TABLE        (1ULL << 1)
#define PTE_BLOCK        (0ULL << 1)  /* block mapping (level 1/2) */
#define PTE_PAGE         (1ULL << 1)  /* page mapping (level 3) */
#define PTE_AF           (1ULL << 10)  /* access flag */
#define PTE_SH_INNER     (3ULL << 8)   /* inner shareable */
#define PTE_SH_OUTER     (2ULL << 8)
#define PTE_ATTR_NORMAL  (0ULL << 2)  /* MAIR attr index 0 = normal */
#define PTE_ATTR_DEVICE  (1ULL << 2)  /* MAIR attr index 1 = device */
#define PTE_AP_RW        (0ULL << 6)  /* read-write, EL0+EL1 */
#define PTE_AP_RO        (1ULL << 6)  /* read-only, EL0+EL1 */
#define PTE_AP_RW_EL0    (1ULL << 6)  /* EL0 access (AP[1]=1) */
#define PTE_AP_RO_EL0    (3ULL << 6)  /* EL0 read-only, EL1 read-only */

#define PTE_PXN          (1ULL << 53) /* privileged (EL1) no-execute */
#define PTE_UXN          (1ULL << 54) /* user no-execute */

/* Per-process capabilities — a syscall the process lacks the capability
 * for is denied with -EPERM. Loaded ELF apps get a least-privilege set;
 * trusted kernel-created processes (the shell) get more. This is the
 * minimal form of the audit's "capability-scoped" syscall requirement. */
#define CAP_WRITE   (1u << 0)   /* SYS_WRITE — console output */
#define CAP_PROC    (1u << 1)   /* SYS_GETPID/YIELD/SLEEP/EXIT — self only */
#define CAP_EXEC    (1u << 2)   /* SYS_EXEC — run shell commands */
#define CAP_IPC     (1u << 3)   /* SYS_SEND / SYS_RECV */
#define CAP_FS      (1u << 4)   /* SYS_OPEN / SYS_READ / SYS_CLOSE */

/* Least privilege for an untrusted loaded application. */
#define CAP_APP_DEFAULT   (CAP_WRITE | CAP_PROC)
/* Trusted kernel-created process (e.g. the P-TERM shell). */
#define CAP_TRUSTED_ALL   (CAP_WRITE | CAP_PROC | CAP_EXEC | CAP_IPC | CAP_FS)

/* Process states */
typedef enum {
    PROC_UNUSED = 0,
    PROC_READY = 1,       /* ready to run */
    PROC_RUNNING = 2,     /* currently running at EL0 */
    PROC_BLOCKED = 3,     /* waiting on syscall/I/O */
    PROC_SLEEPING = 4,    /* sleeping (timer) */
    PROC_TERMINATED = 5   /* exited */
} proc_state_t;

/* IPC message */
typedef struct {
    uint32_t sender_pid;           /* who sent it */
    uint32_t length;               /* payload length */
    uint8_t payload[IPC_MAX_PAYLOAD];
} ipc_message_t;

/* Per-process IPC message queue (ring buffer) */
typedef struct {
    ipc_message_t messages[IPC_QUEUE_DEPTH];
    uint32_t head;       /* next write position */
    uint32_t tail;       /* next read position */
    uint32_t count;      /* messages in queue */
    uint32_t wait_sender; /* PID of process waiting to send (0=none) */
} ipc_queue_t;

/* Saved EL0 CPU context for context switching */
typedef struct {
    uint64_t x[31];       /* x0-x30 general registers */
    uint64_t sp;          /* SP_EL0 */
    uint64_t pc;          /* ELR_EL1 (return PC for ERET) */
    uint64_t pstate;      /* SPSR_EL1 (saved PSTATE) */
} cpu_context_t;

/* User process control block */
typedef struct {
    uint32_t pid;
    char name[32];
    proc_state_t state;
    uint32_t capabilities;   /* CAP_* bitmask; gates syscalls */

    /* Per-process page table (TTBR0_EL1) */
    uint64_t *l0_table;   /* L0 page table (4096 bytes, page-aligned) */

    /* Saved CPU context for preemption */
    cpu_context_t ctx;

    /* Process metadata */
    uint32_t priority;
    uint32_t cpu_time_ticks;
    uint32_t quantum_ticks;     /* time slice remaining */
    uint32_t quantum_default;  /* default time slice */
    int32_t exit_code;

    /* Syscall result */
    int64_t syscall_result;

    /* IPC */
    ipc_queue_t ipc_queue;

    /* Sleep */
    uint64_t sleep_until_tick;  /* wake when scheduler ticks >= this */
    uint32_t blocked_on_pid;    /* PID we're waiting for a message from */
} user_proc_t;

/* Scheduler state */
typedef struct {
    user_proc_t procs[MAX_USER_PROCS];
    uint32_t num_procs;
    uint32_t current_pid;   /* index into procs[] of running process */
    uint32_t next_pid;
    uint64_t ticks;
    bool initialized;

    /* Statistics */
    uint32_t total_context_switches;
    uint32_t total_preemptions;
    uint32_t total_syscalls;
} proc_scheduler_t;

/* API */

/* Initialize the process scheduler */
void proc_sched_init(proc_scheduler_t *ps);

/* Create a user process with code at USER_CODE_BASE.
 * entry_point is a virtual address in user space.
 * Returns PID on success, -1 on failure. */
int32_t proc_create(proc_scheduler_t *ps, const char *name,
                     void (*entry_point)(void), uint32_t priority);

/* Create a user process from a static AArch64 ELF64 image loaded from
 * storage. Returns PID on success, or a negative error (elf_result_t
 * or -100 on resource exhaustion). */
int32_t proc_create_from_elf(proc_scheduler_t *ps, const char *name,
                             const uint8_t *image, uint32_t len);

/* copy_from_user safety: validate that [va, va+len) is fully mapped and
 * EL0-accessible in `proc`, and (copy_from_user) copy it into a kernel
 * buffer. Any syscall that dereferences a user pointer must gate on
 * these (audit finding P0-1). */
bool proc_user_range_ok(user_proc_t *proc, uint64_t va, uint64_t len);
/* As above, but when need_write is true also requires each page be writable
 * at EL0 (AP[2] clear) — use for any buffer EL1 will write into. */
bool proc_user_range_check(user_proc_t *proc, uint64_t va, uint64_t len,
                           bool need_write);
int  copy_from_user(user_proc_t *proc, void *dst, uint64_t user_va,
                    uint64_t len);
/* Copy OUT to a validated + WRITABLE user VA (kernel -> user). 0 / -1 (EFAULT). */
int  copy_to_user(user_proc_t *proc, uint64_t user_va, const void *src,
                  uint64_t len);

/* Map a page in a process's page table */
bool proc_map_page(user_proc_t *proc, uint64_t va, uint64_t pa,
                    bool writable, bool executable);

/* Switch TTBR0_EL1 to the process's page table */
void proc_switch_address_space(user_proc_t *proc);

/* Enter EL0: ERET to user space with the process's context */
void proc_enter_el0(user_proc_t *proc);

/* Save current EL0 context (called from timer IRQ handler) */
void proc_save_context(proc_scheduler_t *ps, cpu_context_t *ctx);

/* Restore EL0 context and ERET (called after context switch) */
void proc_restore_el0(cpu_context_t *ctx);

/* Preemptive scheduler tick — called from timer IRQ.
 * Saves current context, selects next process, switches address space. */
void proc_sched_tick(proc_scheduler_t *ps, cpu_context_t *ctx);

/* SVC handler — called from synchronous exception when EL0 issues SVC */
void proc_handle_svc(proc_scheduler_t *ps, uint64_t syscall_num,
                      uint64_t *args, cpu_context_t *ctx);

/* Terminate a process */
void proc_terminate(proc_scheduler_t *ps, uint32_t pid, int32_t exit_code);

/* Get current process */
user_proc_t *proc_current(proc_scheduler_t *ps);

/* Get process by PID */
user_proc_t *proc_get(proc_scheduler_t *ps, uint32_t pid);

/* Allocate a 4KB-aligned page table (returns virtual address) */
uint64_t *proc_alloc_page_table(void);

/* Configure TCR_EL1 for split EL1/EL0 translation */
void proc_configure_tcr_el1(void);

/* Set up TTBR1_EL1 for kernel space (shared) */
void proc_setup_ttbr1(void);

/* IPC: send a message to a process */
int32_t ipc_send(proc_scheduler_t *ps, uint32_t dest_pid,
                  const uint8_t *payload, uint32_t length);

/* IPC: receive a message (blocking if none available) */
int32_t ipc_recv(proc_scheduler_t *ps, uint32_t sender_pid,
                  uint8_t *buffer, uint32_t max_len);

/* Wake sleeping/blocked processes whose conditions are met */
void proc_wake_eligible(proc_scheduler_t *ps);

#endif /* EL0_USERSPACE_H */
