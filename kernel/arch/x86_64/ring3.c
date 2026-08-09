/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* ring3.c — x86-64 ring-3 (user-mode) parity, mirroring the arm64 EL0 path.
 *
 * Builds a GDT with ring-3 code/data segments + a TSS (so int 0x80 from ring 3
 * lands on a kernel interrupt stack), an IDT (int 0x80 = the syscall gate at
 * DPL 3; CPU exceptions at DPL 0), maps a user code + user stack page with the
 * page-table U/S bit set (kernel pages stay U/S=0 → not reachable from ring 3),
 * then IRETQs to ring 3 to run a tiny user program that makes SYS_WRITE and
 * SYS_EXIT syscalls the kernel services. Proves the ring-0/ring-3 boundary. */
#include <stdint.h>
#include "ring3.h"

extern void uart_puts(const char *s);

/* asm entry points (ring3_asm.S) */
extern void x86_ring3_enter(uint64_t user_rip, uint64_t user_rsp);
extern void x86_syscall_isr(void);
extern void x86_exc_isr(void);
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

static uint64_t     g_gdt[7];
static struct tss64 g_tss;
static uint8_t      g_irq_stack[16384] __attribute__((aligned(16)));

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

static void idt_init(void) {
    for (int i = 0; i < 32; i++) idt_set(i, x86_exc_isr, 0);   /* CPU exceptions */
    idt_set(0x80, x86_syscall_isr, 3);                          /* syscall gate, callable from ring 3 */
    struct desc_ptr ip = { sizeof g_idt - 1, (uint64_t)g_idt };
    __asm__ __volatile__("lidt %0" : : "m"(ip));
}

/* ================= User-page U/S mapping ================= */
/* The boot identity map (first 2 MB, 4 KB pages) is all supervisor. To reach a
 * page from ring 3, U/S (bit 2) must be set at every level. We set it on the
 * shared upper entries and on ONLY the target leaf PTEs, so kernel pages (whose
 * leaf U/S stays 0) remain unreachable from ring 3. */
static uint64_t *phys(uint64_t e) { return (uint64_t *)(e & ~0xFFFULL); }

static void make_user_page(uint64_t vaddr) {
    uint64_t cr3; __asm__ __volatile__("mov %%cr3, %0" : "=r"(cr3));
    uint64_t *l4 = (uint64_t *)(cr3 & ~0xFFFULL);
    l4[(vaddr >> 39) & 0x1FF] |= 0x4;
    uint64_t *l3 = phys(l4[(vaddr >> 39) & 0x1FF]);
    l3[(vaddr >> 30) & 0x1FF] |= 0x4;
    uint64_t *l2 = phys(l3[(vaddr >> 30) & 0x1FF]);
    l2[(vaddr >> 21) & 0x1FF] |= 0x4;
    uint64_t *l1 = phys(l2[(vaddr >> 21) & 0x1FF]);
    l1[(vaddr >> 12) & 0x1FF] |= 0x4;      /* leaf: U/S=1 (P|RW already set at boot) */
}

/* ================= syscall dispatch (called from the ISR) ================= */
#define SYS_WRITE 1
void x86_syscall_dispatch(uint64_t num, uint64_t arg0) {
    if (num == SYS_WRITE) com1_putc((char)arg0);
    /* SYS_EXIT (2) is handled entirely in asm (returns to the kernel). */
}

/* Set while the ring-3 self-test is running: a fault then recovers to the kernel
 * (the test reports failure); a fault elsewhere reports + halts. */
long g_ring3_active = 0;

/* Diagnostic for a CPU exception outside ring 3 — print a hex word and return
 * (the asm stub then halts). */
void x86_exc_report(uint64_t frame0) {
    uart_puts("\n[x86] CPU exception in kernel — halted. frame0=0x");
    char hex[17]; const char *d = "0123456789abcdef";
    for (int i = 0; i < 16; i++) hex[i] = d[(frame0 >> ((15 - i) * 4)) & 0xF];
    hex[16] = 0; uart_puts(hex); uart_puts("\n");
}

/* ================= the ring-3 user program ================= */
/* Hand-assembled: write 'U' then exit.
 *   BF 55 00 00 00   mov edi, 'U'
 *   B8 01 00 00 00   mov eax, SYS_WRITE
 *   CD 80            int 0x80
 *   B8 02 00 00 00   mov eax, SYS_EXIT
 *   CD 80            int 0x80
 *   EB FE            jmp .                (safety) */
static const uint8_t USER_PROG[] = {
    0xBF,0x55,0x00,0x00,0x00, 0xB8,0x01,0x00,0x00,0x00, 0xCD,0x80,
    0xB8,0x02,0x00,0x00,0x00, 0xCD,0x80, 0xEB,0xFE
};

static uint8_t g_user_code[4096]  __attribute__((aligned(4096)));
static uint8_t g_user_stack[4096] __attribute__((aligned(4096)));

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

void x86_ring3_init(void) {
    gdt_tss_init();
    idt_init();
    uart_puts("  [OK] GDT+TSS (ring-3 segments), IDT (int 0x80 = syscall gate)\n");
}
