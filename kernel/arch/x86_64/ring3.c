/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* ring3.c — x86-64 ring-3 (user-mode) parity, mirroring the arm64 EL0 path.
 *
 * Builds a GDT with ring-3 code/data segments + a TSS (so int 0x80 from ring 3
 * lands on a kernel interrupt stack), an IDT (int 0x80 = the syscall gate at
 * DPL 3; CPU exceptions at DPL 0), maps a user code + user stack page with the
 * page-table U/S bit set (kernel pages stay U/S=0 → not reachable from ring 3),
 * then IRETQs to ring 3 to run a tiny user program that makes SYS_WRITE and
 * SYS_EXIT syscalls the kernel services. Proves the ring-0/ring-3 boundary. */
#include "../../src/syscall/syscall.h"
#include <stdint.h>
#include "ring3.h"

extern void uart_puts(const char *s);

/* asm entry points (ring3_asm.S) */
extern void x86_ring3_enter(uint64_t user_rip, uint64_t user_rsp);
extern void x86_syscall_isr(void);
extern volatile int64_t x86_last_exc;

/* ---- COM1 byte output (independent of the driver, for the syscall) ---- */
static inline void com1_putc(char c) {
    __asm__ __volatile__("outb %0, %1" : : "a"((uint8_t)c), "Nd"((uint16_t)0x3F8));
}

/* ================= GDT + TSS ================= */
struct tss64 {
    uint32_t reserved0;
    uint64_t rsp0, rsp1, rsp2;
    uint64_t reserved1;
    uint64_t ist[7];
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed));

/* All the ring-3 machinery lives in .bss.user (linked < 2 MB, in the mapped 4 KB
 * region). In particular TSS.RSP0 must point at reliably-mapped memory: the CPU
 * pushes the interrupt frame there on every ring3->ring0 transition, and a bad
 * RSP0 double-faults (#DF). */
#define LOWBSS __attribute__((section(".bss.user")))
static uint64_t     g_gdt[7]                    LOWBSS __attribute__((aligned(16)));
static struct tss64 g_tss                       LOWBSS;
static uint8_t      g_irq_stack[16384]          LOWBSS __attribute__((aligned(16)));

struct desc_ptr { uint16_t limit; uint64_t base; } __attribute__((packed));

static void gdt_tss_init(void) {
    g_gdt[0] = 0;
    g_gdt[1] = 0x00209A0000000000ULL;   /* kernel code (ring 0), L=1  (== boot) */
    g_gdt[2] = 0x0000920000000000ULL;   /* kernel data (ring 0)                 */
    g_gdt[3] = 0x0020FA0000000000ULL;   /* user code   (ring 3, DPL=3), L=1     */
    g_gdt[4] = 0x0000F20000000000ULL;   /* user data   (ring 3, DPL=3)          */

    /* TSS 16-byte system descriptor at gdt[5..6] */
    for (unsigned i = 0; i < sizeof g_tss; i++) ((uint8_t *)&g_tss)[i] = 0;
    g_tss.rsp0 = (uint64_t)(g_irq_stack + sizeof g_irq_stack);
    g_tss.iomap_base = sizeof g_tss;
    uint64_t base = (uint64_t)&g_tss, limit = sizeof g_tss - 1;
    g_gdt[5] = (limit & 0xFFFF) | ((base & 0xFFFF) << 16)
             | (((base >> 16) & 0xFF) << 32) | (0x89ULL << 40)   /* present, 64-bit avail TSS */
             | (((limit >> 16) & 0xF) << 48) | (((base >> 24) & 0xFF) << 56);
    g_gdt[6] = (base >> 32) & 0xFFFFFFFF;

    struct desc_ptr gp = { sizeof g_gdt - 1, (uint64_t)g_gdt };
    __asm__ __volatile__("lgdt %0" : : "m"(gp));
    /* kcode stays 0x08 (identical to boot) so CS is still valid; reload data + TR */
    __asm__ __volatile__(
        "mov $0x10, %%ax\n\t mov %%ax, %%ds\n\t mov %%ax, %%es\n\t"
        "mov %%ax, %%ss\n\t mov %%ax, %%fs\n\t mov %%ax, %%gs\n\t"
        "mov $0x28, %%ax\n\t ltr %%ax\n\t" : : : "ax");
}

/* ================= IDT ================= */
struct idt_gate {
    uint16_t off_lo; uint16_t sel; uint8_t ist; uint8_t type_attr;
    uint16_t off_mid; uint32_t off_hi; uint32_t zero;
} __attribute__((packed));

static struct idt_gate g_idt[256];

static void idt_set(int vec, void (*handler)(void), uint8_t dpl) {
    uint64_t a = (uint64_t)handler;
    g_idt[vec].off_lo   = a & 0xFFFF;
    g_idt[vec].sel      = 0x08;              /* kernel code */
    g_idt[vec].ist      = 0;
    g_idt[vec].type_attr= 0x8E | (dpl << 5); /* present, interrupt gate, DPL */
    g_idt[vec].off_mid  = (a >> 16) & 0xFFFF;
    g_idt[vec].off_hi   = (a >> 32) & 0xFFFFFFFF;
    g_idt[vec].zero     = 0;
}

extern void (*x86_exc_table[32])(void);
static void idt_init(void) {
    for (int i = 0; i < 32; i++) idt_set(i, x86_exc_table[i], 0);   /* CPU exceptions */
    idt_set(0x80, x86_syscall_isr, 3);                              /* syscall gate, callable from ring 3 */
    struct desc_ptr ip = { sizeof g_idt - 1, (uint64_t)g_idt };
    __asm__ __volatile__("lidt %0" : : "m"(ip));
}

/* ================= User-page U/S mapping ================= */
/* The boot identity map (first 2 MB, 4 KB pages) is all supervisor. To reach a
 * page from ring 3, U/S (bit 2) must be set at every level. We set it on the
 * shared upper entries and on ONLY the target leaf PTEs, so kernel pages (whose
 * leaf U/S stays 0) remain unreachable from ring 3. */
static uint64_t *phys(uint64_t e) { return (uint64_t *)(e & ~0xFFFULL); }

/* Pool of fresh L1 tables for splitting 2 MB huge pages (the boot map uses huge
 * pages beyond the first 2 MB; a user page landing there must be split so only
 * its 4 KB sub-page becomes user-accessible). */
static uint64_t g_split_l1[4][512] __attribute__((aligned(4096)));
static int      g_split_next = 0;

static void make_user_page(uint64_t vaddr) {
    uint64_t cr3; __asm__ __volatile__("mov %%cr3, %0" : "=r"(cr3));
    uint64_t *l4 = (uint64_t *)(cr3 & ~0xFFFULL);
    l4[(vaddr >> 39) & 0x1FF] |= 0x4;
    uint64_t *l3 = phys(l4[(vaddr >> 39) & 0x1FF]);
    l3[(vaddr >> 30) & 0x1FF] |= 0x4;
    uint64_t *l2 = phys(l3[(vaddr >> 30) & 0x1FF]);
    unsigned i2 = (vaddr >> 21) & 0x1FF;
    if ((l2[i2] & 0x80) && g_split_next < 4) {          /* 2 MB huge page: split it */
        uint64_t base = l2[i2] & ~0x1FFFFFULL;
        uint64_t *nl1 = g_split_l1[g_split_next++];
        for (int i = 0; i < 512; i++) nl1[i] = (base + (uint64_t)i * 4096) | 0x03;
        l2[i2] = ((uint64_t)nl1) | 0x07;                /* -> L1, P|RW|U/S */
    }
    l2[i2] |= 0x4;
    uint64_t *l1 = phys(l2[i2]);
    l1[(vaddr >> 12) & 0x1FF] |= 0x4;                   /* leaf: U/S=1 */
}

/* ================= syscall dispatch (called from the ISR) ================= */
/* THE SHARED ABI, not a local numbering. Before this, x86_64 used WRITE=1 and
 * EXIT=2 -- exactly INVERTED from arm64, so the same program calling syscall 1
 * wrote a byte on one architecture and terminated on the other. That is the
 * drift the ABI table exists to prevent, and it was live. */
typedef char x86_abi_pinned[(ZXV_SYS_EXIT == 1 && ZXV_SYS_WRITE == 2) ? 1 : -1];

/* What a ring-3 program on x86_64 is currently permitted to hold. There is no
 * process table or filesystem bound to ring 3 here yet, so this is deliberately
 * smaller than arm64's -- and the calls that need those are reported ENOSYS
 * rather than stubbed to look successful. */
#define X86_RING3_CAPS (ZSC_CAP_WRITE | ZSC_CAP_PROC)

static uint32_t g_x86_pid = 1;

/* Which ABI calls this architecture actually provides. Kept as data so the
 * coverage can be REPORTED rather than asserted -- a gap you can measure gets
 * closed, a gap described in prose does not. */
static bool x86_provides(uint32_t nr) {
    switch (nr) {
        case ZXV_SYS_EXIT: case ZXV_SYS_WRITE: case ZXV_SYS_GETPID:
        case ZXV_SYS_YIELD: case ZXV_SYS_SLEEP: case ZXV_SYS_ABI_VERSION:
            return true;
        default: return false;
    }
}

/* Count implemented vs total (excluding the reserved hole). */
void x86_abi_coverage(uint32_t *impl, uint32_t *total) {
    uint32_t i = 0, t = 0;
    zxv_syscall_info_t info;
    for (uint32_t n = 0; n < zxv_syscall_count(); n++) {
        if (!zxv_syscall_info(n, &info) || info.reserved) continue;
        t++;
        if (x86_provides(n)) i++;
    }
    if (impl) *impl = i;
    if (total) *total = t;
}

/* What the last syscall actually received. Recorded BEFORE the permission
 * check so it observes the REGISTER FORWARDING itself rather than the outcome
 * of a call -- the argument path is what broke, so the argument path is what
 * has to be watched. */
uint64_t x86_sc_last[4];
/* The WRITE call's arguments, kept separately: a later syscall (ABI_VERSION,
 * then EXIT) would overwrite x86_sc_last before the selftest could read it. */
static uint64_t g_argsnap[4];

int64_t x86_syscall_dispatch(uint64_t num, uint64_t a0, uint64_t a1, uint64_t a2) {
    x86_sc_last[0] = num; x86_sc_last[1] = a0;
    x86_sc_last[2] = a1;  x86_sc_last[3] = a2;
    if (num == ZXV_SYS_WRITE) {
        g_argsnap[0] = num; g_argsnap[1] = a0;
        g_argsnap[2] = a1;  g_argsnap[3] = a2;
    }

    /* Permission first, from the SAME table arm64 consults. An unknown or
     * reserved number is ENOSYS before any capability question. */
    int permit = zxv_syscall_permit((uint32_t)num, X86_RING3_CAPS);
    if (permit != ZXV_OK) return permit;

    switch (num) {
        case ZXV_SYS_WRITE:
            /* arg0 = byte. (The pointer+length form arrives with L2 handles;
             * a byte-at-a-time write is what this ring-3 path can honestly do
             * without a user-copy primitive, and user copying is exactly the
             * thing not to improvise.) */
            com1_putc((char)a0);
            return 1;
        case ZXV_SYS_GETPID:
            return (int64_t)g_x86_pid;
        case ZXV_SYS_YIELD:
            return ZXV_OK;              /* single ring-3 task: nothing to yield to */
        case ZXV_SYS_SLEEP:
            return ZXV_OK;              /* no timer bound to ring 3 yet */
        case ZXV_SYS_ABI_VERSION:
            return (int64_t)ZXV_ABI_VERSION;
        case ZXV_SYS_EXIT:
            return ZXV_OK;              /* handled in asm; never reaches here */
        default:
            /* In the ABI and permitted, but this architecture does not provide
             * it yet. ENOSYS is the honest answer -- a stub returning success
             * would make a missing feature look present. */
            return ZXV_ENOSYS;
    }
}

/* Set while the ring-3 self-test is running: a fault then recovers to the kernel
 * (the test reports failure); a fault elsewhere reports + halts. */
long g_ring3_active = 0;

/* Diagnostic for a CPU exception outside ring 3 — print a hex word and return
 * (the asm stub then halts). */
static void put_hex(uint64_t v) {
    char hex[17]; const char *d = "0123456789abcdef";
    for (int i = 0; i < 16; i++) hex[i] = d[(v >> ((15 - i) * 4)) & 0xF];
    hex[16] = 0; uart_puts(hex);
}
void x86_exc_report(uint64_t vector, uint64_t rip) {
    uart_puts("\n[x86] CPU exception vector="); put_hex(vector);
    uart_puts(" RIP=0x"); put_hex(rip); uart_puts("\n");
}

/* ================= the ring-3 user program ================= */
/* Hand-assembled: write 'U' then exit.
 * Three DISTINCT argument registers, so a forwarding bug cannot hide: an
 * earlier version of the ISR copied arg0 into every slot, and a test using the
 * same value everywhere would have passed anyway.
 *   BF 55 00 00 00   mov edi, 'U'      (arg0)
 *   BE AA AA 00 00   mov esi, 0xAAAA   (arg1)
 *   BA BB BB 00 00   mov edx, 0xBBBB   (arg2)
 *   B8 02 00 00 00   mov eax, ZXV_SYS_WRITE  (2 in the shared ABI)
 *   CD 80            int 0x80
 *   B8 0C 00 00 00   mov eax, ZXV_SYS_ABI_VERSION (12)
 *   CD 80            int 0x80
 *   B8 01 00 00 00   mov eax, ZXV_SYS_EXIT   (1 in the shared ABI)
 *   CD 80            int 0x80
 *   EB FE            jmp .                (safety) */
#define X86_ARG0 0x55u
#define X86_ARG1 0xAAAAu
#define X86_ARG2 0xBBBBu
static const uint8_t USER_PROG[] = {
    0xBF,0x55,0x00,0x00,0x00,           /* mov edi, 'U'    */
    0xBE,0xAA,0xAA,0x00,0x00,           /* mov esi, 0xAAAA */
    0xBA,0xBB,0xBB,0x00,0x00,           /* mov edx, 0xBBBB */
    0xB8,0x02,0x00,0x00,0x00, 0xCD,0x80,
    0xB8,0x0C,0x00,0x00,0x00, 0xCD,0x80,
    0xB8,0x01,0x00,0x00,0x00, 0xCD,0x80, 0xEB,0xFE
};

/* Placed in .bss.user, which the linker puts at the very start of .bss (< 2 MB) so
 * these live in the 4 KB-page region where make_user_page can set U/S per page. */
static uint8_t g_user_code[4096]  __attribute__((aligned(4096), section(".bss.user")));
static uint8_t g_user_stack[4096] __attribute__((aligned(4096), section(".bss.user")));

int x86_ring3_selftest(void) {
    x86_last_exc = 0;
    for (unsigned i = 0; i < sizeof USER_PROG; i++) g_user_code[i] = USER_PROG[i];

    make_user_page((uint64_t)g_user_code);
    make_user_page((uint64_t)g_user_stack);
    __asm__ __volatile__("mov %%cr3, %%rax\n\t mov %%rax, %%cr3" : : : "rax");  /* flush TLB */

    g_ring3_active = 1;
    x86_ring3_enter((uint64_t)g_user_code,
                    (uint64_t)(g_user_stack + sizeof g_user_stack));
    g_ring3_active = 0;
    /* returns here after the user's SYS_EXIT */
    return x86_last_exc == 0;   /* 0 = clean ring-3 run, no exception */
}

/* Did the arguments the user program loaded actually REACH the dispatcher?
 * x86_sc_last was captured on the final call (EXIT, which carries no args), so
 * the WRITE call's arguments are checked from the recording made at that time --
 * see x86_ring3_argcheck_run below, which inspects immediately after the WRITE.
 *
 * Returns a bitmask of what MISMATCHED (0 = all three arrived intact):
 *   bit0 arg0   bit1 arg1   bit2 arg2
 * A mask rather than a bool because WHICH argument was lost identifies the bug:
 * losing only arg1/arg2 means the ISR never captured them; all three matching
 * arg0 means they were overwritten with a copy of arg0, which is the exact
 * failure an earlier version of the ISR had. */
uint32_t x86_ring3_argcheck(void) {
    uint32_t bad = 0;
    if (g_argsnap[1] != X86_ARG0) bad |= 1u;
    if (g_argsnap[2] != X86_ARG1) bad |= 2u;
    if (g_argsnap[3] != X86_ARG2) bad |= 4u;
    return bad;
}

static inline void outb8(uint16_t port, uint8_t val) {
    __asm__ __volatile__("outb %0, %1" : : "a"(val), "Nd"(port));
}

void x86_ring3_init(void) {
    gdt_tss_init();
    idt_init();
    /* Mask the legacy 8259 PICs. They map IRQ0..7 to vectors 0x08..0x0F by default,
     * colliding with the CPU exception vectors (IRQ0/timer == vector 8 == #DF), so a
     * hardware IRQ during ring 3 would masquerade as an exception. */
    outb8(0x21, 0xFF);
    outb8(0xA1, 0xFF);
    uart_puts("  [OK] GDT+TSS (ring-3 segments), IDT (int 0x80 syscall gate), PIC masked\n");
}

/* ---- DECLARATION: x86_64_paging PROVIDES mm_ready ---------------------------
 * WHY THIS TU AND NOT boot.asm. The kernel's four-level tables are BUILT in
 * kernel/arch/x86_64/boot.asm (pt_l4 -> pt_l3 -> pt_l2, L2[0] -> pt_l1 4 KB
 * leaves, L2[1..511] 2 MB huge pages, then CR3/CR4.PAE/EFER.LME/CR0.PG). A
 * declaration has to live in a C translation unit, and this file is the one
 * that OWNS those tables afterwards: make_user_page() above reads CR3, walks
 * L4/L3/L2, splits a 2 MB huge page into a fresh L1 and sets U/S on the leaf.
 * That is the real page-table work on this architecture, in C, here.
 *
 * WHY THE BRING-UP IS NOT `return 0`. CR0.PG alone is worthless evidence on
 * x86-64: long mode cannot execute a single 64-bit instruction with paging off,
 * so a bring-up that only checked PG would be true by construction -- it could
 * never fail, and a check that cannot fail is not a check. So this one WALKS
 * THE LIVE TABLES from CR3 and asks whether three addresses the kernel is
 * actually using resolve to a present leaf. That IS falsifiable: boot.asm fills
 * exactly one L3 and one L2, i.e. it maps the low 1 GB and nothing above it,
 * while linker64.ld puts .bss "well past 2MB" and then the 512 KB stack above
 * it. Grow the static tables past 1 GB and __stack_top stops resolving and this
 * bring-up returns -1 -- which is the honest report, because at that point the
 * stack the CPU is pushing onto is unmapped.
 *
 * Same shape as arm64_mmu_bringup (kernel/arch/arm64/arm64_mmu.c), which reads
 * SCTLR_EL1.M back off the CPU rather than trusting that init ran.
 *
 * PA == VA. The walk dereferences table PHYSICAL addresses as pointers. That is
 * legal here for exactly the reason make_user_page() already relies on: the
 * boot map is an identity map. It is the same assumption phys() makes 200 lines
 * up, not a new one introduced by this declaration.
 *
 * REQUIRES NOTHING, and that is a checkable position rather than an omission:
 * paging is up before the first C instruction runs, so there is no earlier
 * module for it to depend on. */
#include "zxv_decl.h"

extern char __kernel_end[];
extern char __stack_top[];

static inline uint64_t x86_read_cr0(void) {
    uint64_t v; __asm__ __volatile__("mov %%cr0, %0" : "=r"(v)); return v;
}
static inline uint64_t x86_read_cr3(void) {
    uint64_t v; __asm__ __volatile__("mov %%cr3, %0" : "=r"(v)); return v;
}
static inline uint64_t x86_read_cr4(void) {
    uint64_t v; __asm__ __volatile__("mov %%cr4, %0" : "=r"(v)); return v;
}

/* Walk the ACTIVE tables for `va`. 0 = it resolves to a present leaf; -1 at the
 * first level whose entry has P clear. Handles 1 GB (L3.PS) and 2 MB (L2.PS)
 * leaves as well as 4 KB ones, because the boot map uses two of the three. */
static int x86_va_resolves(uint64_t va) {
    const uint64_t *t = (const uint64_t *)(x86_read_cr3() & ~0xFFFULL);
    uint64_t e = t[(va >> 39) & 0x1FF];
    if (!(e & 1u)) return -1;                       /* L4 */
    t = (const uint64_t *)(e & ~0xFFFULL);
    e = t[(va >> 30) & 0x1FF];
    if (!(e & 1u)) return -1;                       /* L3 */
    if (e & 0x80u) return 0;                        /* 1 GB page */
    t = (const uint64_t *)(e & ~0xFFFULL);
    e = t[(va >> 21) & 0x1FF];
    if (!(e & 1u)) return -1;                       /* L2 */
    if (e & 0x80u) return 0;                        /* 2 MB page */
    t = (const uint64_t *)(e & ~0xFFFULL);
    e = t[(va >> 12) & 0x1FF];
    if (!(e & 1u)) return -1;                       /* L1 leaf */
    return 0;
}

static int x86_64_paging_bringup(void) {
    if (!(x86_read_cr0() & (1ULL << 31))) return -1;   /* CR0.PG  */
    if (!(x86_read_cr4() & (1ULL << 5)))  return -1;   /* CR4.PAE */
    /* .text: the code executing right now. */
    if (x86_va_resolves((uint64_t)(uintptr_t)&x86_ring3_init) != 0) return -1;
    /* the last byte of the image (end of .bss, past the 2 MB 4 KB-page window) */
    if (x86_va_resolves((uint64_t)(uintptr_t)__kernel_end - 8u) != 0) return -1;
    /* the kernel stack the CPU is pushing onto */
    if (x86_va_resolves((uint64_t)(uintptr_t)__stack_top - 8u) != 0) return -1;
    return 0;
}

ZXV_DECLARE(x86_64_paging,
    ZXV_PROVIDES(mm_ready),
    ZXV_REQUIRES_NONE,
    ZXV_BRINGUP(x86_64_paging_bringup));
