/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* x86_irq.c: the x86_64 IDT, legacy PIC, local APIC, IOAPIC mask and the
 * trap dispatcher. See x86_irq.h for the layout and the vector map.
 *
 * Replaces ring3.c's bring-up IDT once the ring-3 self-test has run: that IDT
 * had exceptions and int 0x80 only, so no hardware interrupt could ever be
 * taken. The GDT and TSS stay ring3.c's; this file adds two IST stacks to
 * that TSS (x86_tss_set_ist) so an interrupt taken in ring 0 never lands on
 * the interrupted code's stack. The kernel is built without -mno-red-zone, so
 * without IST a timer tick in ring 0 would overwrite the 128 bytes below a
 * leaf function's stack pointer. */
#include "x86_irq.h"
#include "hwplat_x86.h"
#include "uart_16550.h"
#include "msr_safe.h" /* x86_extable_fixup: #GP recovery for safe MSR access */
#include "zxv_syscall_abi.h"

extern char x86_trap_stubs[];
extern void x86_tss_set_ist(unsigned n, uint64_t stack_top); /* ring3.c */

/* Hooks the process runtime provides (x86_userspace.c). Weak, so the
 * interrupt layer links and boots on its own; with no runtime a trap from
 * ring 3 is reported as fatal. */
__attribute__((weak)) void x86_user_trap(x86_frame_t *f);
__attribute__((weak)) void x86_sched_kernel_tick(void);

/* ---- small CPU helpers ---------------------------------------------------- */
static inline void outb(uint16_t port, uint8_t v)
{
    __asm__ __volatile__("outb %0, %1" : : "a"(v), "Nd"(port));
}
static inline uint8_t inb(uint16_t port)
{
    uint8_t v;
    __asm__ __volatile__("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}
static inline void io_wait(void)
{
    outb(0x80, 0);
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
static inline void cpuid(uint32_t leaf, uint32_t sub, uint32_t *a, uint32_t *b, uint32_t *c,
                         uint32_t *d)
{
    __asm__ __volatile__("cpuid" : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d) : "a"(leaf), "c"(sub));
}
static inline uint64_t read_cr2(void)
{
    uint64_t v;
    __asm__ __volatile__("mov %%cr2, %0" : "=r"(v));
    return v;
}

static x86_irq_info_t g_info;

const x86_irq_info_t *x86_irq_info(void)
{
    return &g_info;
}

/* ---- IDT ------------------------------------------------------------------ */
struct idt_gate {
    uint16_t off_lo;
    uint16_t sel;
    uint8_t ist;
    uint8_t type_attr;
    uint16_t off_mid;
    uint32_t off_hi;
    uint32_t zero;
} __attribute__((packed));

struct desc_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static struct idt_gate g_idt[256] __attribute__((aligned(16)));

/* IST1: every hardware interrupt (taken in ring 0 only by the idle loop and
 * by kernel code running with IF=1). IST2: #DF, NMI and #MC, which must have
 * a known-good stack whatever state the kernel stack is in. */
static uint8_t g_ist_irq[16384] __attribute__((aligned(16)));
static uint8_t g_ist_nmi[8192] __attribute__((aligned(16)));

static void idt_set(unsigned vec, uint8_t ist, uint8_t dpl)
{
    uint64_t a = (uint64_t) (uintptr_t) (x86_trap_stubs + 16u * vec);
    g_idt[vec].off_lo = (uint16_t) (a & 0xFFFF);
    g_idt[vec].sel = 0x08; /* kernel code, as boot and ring3.c */
    g_idt[vec].ist = ist;
    g_idt[vec].type_attr = (uint8_t) (0x8E | (dpl << 5)); /* present, interrupt gate */
    g_idt[vec].off_mid = (uint16_t) ((a >> 16) & 0xFFFF);
    g_idt[vec].off_hi = (uint32_t) (a >> 32);
    g_idt[vec].zero = 0;
}

static void idt_init(void)
{
    x86_tss_set_ist(1, (uint64_t) (uintptr_t) (g_ist_irq + sizeof g_ist_irq));
    x86_tss_set_ist(2, (uint64_t) (uintptr_t) (g_ist_nmi + sizeof g_ist_nmi));
    for (unsigned v = 0; v < 256; v++) {
        uint8_t ist = v >= 32 ? 1 : 0;
        if (v == 2 || v == 8 || v == 18) ist = 2; /* NMI, #DF, #MC */
        idt_set(v, ist, 0);
    }
    /* The syscall gate: callable from ring 3, and taken on TSS.RSP0 like the
     * exceptions a process can raise, never on the IRQ stack. */
    idt_set(X86_VEC_SYSCALL, 0, 3);
    struct desc_ptr ip = {sizeof g_idt - 1, (uint64_t) (uintptr_t) g_idt};
    __asm__ __volatile__("lidt %0" : : "m"(ip));
}

int x86_irq_idt_live(void)
{
    struct desc_ptr cur = {0, 0};
    __asm__ __volatile__("sidt %0" : "=m"(cur));
    return cur.base == (uint64_t) (uintptr_t) g_idt && cur.limit == sizeof g_idt - 1 &&
           (g_idt[13].type_attr & 0x80) != 0;
}

/* ---- legacy 8259 PIC ------------------------------------------------------ */
/* Remap to 0x20/0x28 before masking: masked lines still raise a spurious
 * IRQ 7 or 15, and at the power-on mapping (0x08-0x0F) that would read as a
 * CPU exception. */
static void pic_disable(void)
{
    outb(0x20, 0x11);
    io_wait();
    outb(0xA0, 0x11);
    io_wait();
    outb(0x21, X86_VEC_PIC_BASE);
    io_wait();
    outb(0xA1, X86_VEC_PIC_BASE + 8);
    io_wait();
    outb(0x21, 0x04);
    io_wait();
    outb(0xA1, 0x02);
    io_wait();
    outb(0x21, 0x01);
    io_wait();
    outb(0xA1, 0x01);
    io_wait();
    outb(0x21, 0xFF);
    outb(0xA1, 0xFF);
}

/* ---- ACPI: find the MADT -------------------------------------------------- */
/* Physical memory below 1 GB is identity-mapped by boot; above it, map it
 * uncached through the class-driver MMIO mapper. */
static const uint8_t *phys_ptr(uint64_t pa, uint64_t len)
{
    if (pa + len <= 0x40000000ULL) return (const uint8_t *) (uintptr_t) pa;
    return (const uint8_t *) hwplat_x86_map_mmio(pa, len);
}

static int sum_ok(const uint8_t *p, uint32_t len)
{
    uint8_t s = 0;
    for (uint32_t i = 0; i < len; i++) s = (uint8_t) (s + p[i]);
    return s == 0;
}

/* The table with signature sig from the RSDT/XSDT, checksum verified, or 0. */
static const uint8_t *acpi_find(uint64_t rsdp, const char *sig, uint32_t *len_out)
{
    const uint8_t *r = phys_ptr(rsdp, 36);
    if (!r || r[0] != 'R' || r[1] != 'S' || r[2] != 'D' || r[3] != ' ' || !sum_ok(r, 20)) return 0;
    uint64_t root;
    uint32_t esz;
    if (r[15] >= 2 && sum_ok(r, 36) && (x86_rd32le(r + 24) | x86_rd32le(r + 28))) {
        root = (uint64_t) x86_rd32le(r + 24) | ((uint64_t) x86_rd32le(r + 28) << 32);
        esz = 8;
    } else {
        root = x86_rd32le(r + 16);
        esz = 4;
    }
    const uint8_t *h = phys_ptr(root, 36);
    if (!h) return 0;
    uint32_t rlen = x86_rd32le(h + 4);
    if (rlen < 36 || rlen > 0x10000) return 0;
    h = phys_ptr(root, rlen);
    if (!h || !sum_ok(h, rlen)) return 0;
    for (uint32_t off = 36; off + esz <= rlen; off += esz) {
        uint64_t pa = x86_rd32le(h + off);
        if (esz == 8) pa |= (uint64_t) x86_rd32le(h + off + 4) << 32;
        const uint8_t *t = phys_ptr(pa, 36);
        if (!t || t[0] != (uint8_t) sig[0] || t[1] != (uint8_t) sig[1] ||
            t[2] != (uint8_t) sig[2] || t[3] != (uint8_t) sig[3])
            continue;
        uint32_t tlen = x86_rd32le(t + 4);
        if (tlen < 36 || tlen > 0x10000) return 0;
        t = phys_ptr(pa, tlen);
        if (!t || !sum_ok(t, tlen)) return 0;
        *len_out = tlen;
        return t;
    }
    return 0;
}

/* ---- local APIC ----------------------------------------------------------- */
#define MSR_APIC_BASE 0x1Bu
#define APIC_BASE_EN  (1ull << 11)
#define APIC_BASE_X2  (1ull << 10)

#define LAPIC_ID      0x020u
#define LAPIC_TPR     0x080u
#define LAPIC_EOI     0x0B0u
#define LAPIC_SVR     0x0F0u
#define LAPIC_ESR     0x280u
#define LAPIC_LVT_TMR 0x320u
#define LAPIC_LVT_L0  0x350u
#define LAPIC_LVT_ERR 0x370u
#define LAPIC_TMR_INI 0x380u
#define LAPIC_TMR_CUR 0x390u
#define LAPIC_TMR_DIV 0x3E0u
#define LVT_MASKED    (1u << 16)
#define LVT_PERIODIC  (1u << 17)

static volatile uint32_t *g_lapic; /* xAPIC MMIO, 0 in x2APIC mode */

static uint32_t lapic_rd(uint32_t reg)
{
    if (g_info.x2apic) return (uint32_t) rdmsr(0x800u + (reg >> 4));
    return g_lapic[reg >> 2];
}

static void lapic_wr(uint32_t reg, uint32_t v)
{
    if (g_info.x2apic)
        wrmsr(0x800u + (reg >> 4), v);
    else
        g_lapic[reg >> 2] = v;
}

static void lapic_eoi(void)
{
    lapic_wr(LAPIC_EOI, 0);
}

static int lapic_init(uint64_t rsdp)
{
    uint32_t a, b, c, d;
    cpuid(1, 0, &a, &b, &c, &d);
    if (!(d & (1u << 9))) return -1; /* no APIC at all */
    int has_x2 = (c >> 21) & 1;

    uint64_t msr = rdmsr(MSR_APIC_BASE);
    uint64_t msr_base = msr & 0x000FFFFFFFFFF000ull;
    uint64_t base = msr_base;

    if (rsdp) {
        uint32_t len = 0;
        const uint8_t *madt = acpi_find(rsdp, "APIC", &len);
        x86_madt_t m;
        if (madt && x86_madt_scan(madt, len, &m) == 0) {
            g_info.madt_cpus = m.cpus;
            g_info.ioapic_base = m.ioapic_base;
            if (m.lapic_base) {
                base = m.lapic_base;
                g_info.from_madt = 1;
            }
        }
    }
    if (base != msr_base) {
        /* The address the CPU decodes is the MSR's; say so and use it. */
        uart_puts("  [LAPIC] MADT base 0x");
        uart_put_hex(base);
        uart_puts(" differs from IA32_APIC_BASE 0x");
        uart_put_hex(msr_base);
        uart_puts(": using the MSR\n");
        base = msr_base;
    }
    g_info.lapic_base = base;

    if (has_x2) {
        /* Disabled -> x2APIC directly is an invalid transition (#GP): go
         * through xAPIC-enabled first. x2APIC -> xAPIC is invalid too, so a
         * firmware that already chose x2APIC is left in it. */
        if (!(msr & APIC_BASE_X2)) {
            wrmsr(MSR_APIC_BASE, msr | APIC_BASE_EN);
            wrmsr(MSR_APIC_BASE, msr | APIC_BASE_EN | APIC_BASE_X2);
        }
        g_info.x2apic = 1;
        g_info.lapic_id = lapic_rd(LAPIC_ID);
    } else {
        if (msr & APIC_BASE_X2) return -1; /* firmware left x2APIC on a CPU without it */
        wrmsr(MSR_APIC_BASE, msr | APIC_BASE_EN);
        g_lapic = (volatile uint32_t *) hwplat_x86_map_mmio(base, 0x1000);
        if (!g_lapic) return -1;
        g_info.lapic_id = lapic_rd(LAPIC_ID) >> 24;
    }

    lapic_wr(LAPIC_TPR, 0);
    lapic_wr(LAPIC_LVT_TMR, LVT_MASKED | X86_VEC_TIMER);
    lapic_wr(LAPIC_LVT_L0, LVT_MASKED); /* ExtINT from the (masked) PIC: off */
    lapic_wr(LAPIC_LVT_ERR, X86_VEC_APIC_ERR);
    lapic_wr(LAPIC_ESR, 0);
    lapic_wr(LAPIC_ESR, 0);
    lapic_wr(LAPIC_SVR, 0x100u | X86_VEC_SPURIOUS); /* software enable */
    lapic_eoi();
    return 0;
}

/* ---- IOAPIC: mask every line ---------------------------------------------- */
static void ioapic_mask_all(void)
{
    if (!g_info.ioapic_base) return;
    volatile uint32_t *io = (volatile uint32_t *) hwplat_x86_map_mmio(g_info.ioapic_base, 0x20);
    if (!io) return;
    io[0] = 1; /* IOAPICVER */
    uint32_t pins = ((io[4] >> 16) & 0xFFu) + 1u;
    for (uint32_t i = 0; i < pins; i++) {
        io[0] = 0x10u + 2u * i;
        io[4] = io[4] | LVT_MASKED;
    }
    g_info.ioapic_pins = pins;
}

int x86_irq_init(uint64_t rsdp)
{
    __asm__ __volatile__("cli");
    pic_disable();
    idt_init();
    if (lapic_init(rsdp) != 0) return -1;
    ioapic_mask_all();
    return 0;
}

/* ---- LAPIC timer ---------------------------------------------------------- */
static inline uint64_t rdtsc(void)
{
    uint32_t lo, hi;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t) hi << 32) | lo;
}

static volatile uint64_t g_ticks;

uint64_t x86_ticks(void)
{
    return g_ticks;
}

#define CAL_WINDOW_US 10000u /* PIT channel 2: 11932 ticks of 1.193182 MHz */

/* Count LAPIC timer decrements (divide by 16) and TSC cycles over one PIT
 * gate, the way hwplat_x86 calibrates its delay. 0 when the PIT never
 * answers (some boards gate it off). */
static uint64_t calibrate(uint64_t *tsc_out)
{
    lapic_wr(LAPIC_TMR_DIV, 0x3); /* divide by 16 */
    lapic_wr(LAPIC_LVT_TMR, LVT_MASKED | X86_VEC_TIMER);

    uint8_t g = inb(0x61);
    outb(0x61, (uint8_t) ((g & ~0x02) | 0x01)); /* gate on, speaker off */
    outb(0x43, 0xB0);                           /* channel 2, mode 0, binary */
    outb(0x42, 11932 & 0xFF);
    outb(0x42, 11932 >> 8);
    g = inb(0x61);
    outb(0x61, (uint8_t) (g & ~0x01)); /* gate low: hold the count */

    lapic_wr(LAPIC_TMR_INI, 0xFFFFFFFFu);
    uint64_t t0 = rdtsc();
    outb(0x61, (uint8_t) (g | 0x01)); /* gate high: count starts */
    uint32_t guard = 0;
    while (!(inb(0x61) & 0x20)) {
        if (++guard > 50000000u) {
            lapic_wr(LAPIC_TMR_INI, 0);
            return 0;
        }
    }
    uint64_t t1 = rdtsc();
    uint32_t cur = lapic_rd(LAPIC_TMR_CUR);
    lapic_wr(LAPIC_TMR_INI, 0);
    *tsc_out = t1 - t0;
    return 0xFFFFFFFFull - cur;
}

int x86_timer_start(void)
{
    uint64_t tsc = 0;
    uint64_t counted = calibrate(&tsc);
    uint32_t count = x86_lapic_period_count(counted, CAL_WINDOW_US, 1000000u / X86_HZ);
    if (count == 0) return -1; /* TODO: HPET or ACPI PM timer when there is no PIT */
    g_info.timer_count = count;
    g_info.lapic_hz = x86_rate_hz(counted, CAL_WINDOW_US);
    g_info.tsc_per_tick = x86_rate_hz(tsc, CAL_WINDOW_US) / X86_HZ;
    g_ticks = 0;
    lapic_wr(LAPIC_TMR_DIV, 0x3);
    lapic_wr(LAPIC_LVT_TMR, LVT_PERIODIC | X86_VEC_TIMER);
    lapic_wr(LAPIC_TMR_INI, count);
    return 0;
}

/* ---- the trap dispatcher -------------------------------------------------- */
static uint64_t g_spurious, g_apic_errors, g_unexpected;

static void put_hex16(uint64_t v)
{
    static const char d[] = "0123456789abcdef";
    char s[17];
    for (int i = 0; i < 16; i++) s[i] = d[(v >> ((15 - i) * 4)) & 0xF];
    s[16] = 0;
    uart_puts(s);
}

/* A fault in ring 0 is a kernel bug: report it with the marker CI's boot
 * checks look for and stop. */
__attribute__((noreturn)) static void kernel_fault(const x86_frame_t *f)
{
    uart_puts("\n[x86] CPU exception vector=");
    put_hex16(f->vector);
    uart_puts(" RIP=0x");
    put_hex16(f->rip);
    uart_puts("\n[x86] kernel mode, error=0x");
    put_hex16(f->error);
    uart_puts(" CR2=0x");
    put_hex16(read_cr2());
    uart_puts(" RSP=0x");
    put_hex16(f->rsp);
    uart_puts(": halting\n");
    for (;;) __asm__ __volatile__("cli; hlt");
}

void x86_trap(x86_frame_t *f)
{
    uint64_t v = f->vector;

    if (v == X86_VEC_SPURIOUS) { /* no EOI for the spurious vector */
        g_spurious++;
        return;
    }
    if (v >= X86_VEC_PIC_BASE && v < X86_VEC_PIC_BASE + 16u) { /* masked 8259 */
        g_spurious++;
        return;
    }
    if (v == X86_VEC_APIC_ERR) {
        lapic_wr(LAPIC_ESR, 0);
        g_apic_errors++;
        lapic_eoi();
        return;
    }
    if (v == X86_VEC_TIMER) { /* acknowledge first: a switch below may not return here */
        g_ticks++;
        lapic_eoi();
    }

    if (f->cs & 3u) { /* from ring 3: the process runtime owns it */
        if (x86_user_trap) {
            x86_user_trap(f);
            return;
        }
        kernel_fault(f);
    }

    if (v == 13u) { /* #GP at a registered instruction (msr_safe.h): resume at its fixup */
        uint64_t fix = x86_extable_fixup(f->rip);
        if (fix) {
            f->rip = fix;
            return;
        }
    }
    if (v < 32u) kernel_fault(f);
    if (v == X86_VEC_TIMER) { /* a tick in ring 0: idle loop or kernel code */
        if (x86_sched_kernel_tick) x86_sched_kernel_tick();
        return;
    }
    if (v == X86_VEC_SYSCALL) { /* int 0x80 from ring 0 is a bug, not a call */
        f->rax = (uint64_t) (int64_t) ZXV_ENOSYS;
        return;
    }
    g_unexpected++;
    lapic_eoi();
}

/* ---- boot log ------------------------------------------------------------- */
int x86_irq_boot(uint64_t rsdp)
{
    if (x86_irq_init(rsdp) != 0) {
        uart_puts("  [IRQ] no usable local APIC: interrupts stay off\n");
        return -1;
    }
    uart_puts("  [IRQ] IDT 256 vectors (IST stacks for IRQs and #DF/NMI/#MC), 8259 PIC "
              "remapped+masked\n");
    if (g_info.x2apic) {
        uart_puts("  [IRQ] LAPIC x2APIC mode, id ");
        uart_put_dec(g_info.lapic_id);
    } else {
        uart_puts("  [IRQ] LAPIC xAPIC at 0x");
        uart_put_hex(g_info.lapic_base);
        uart_puts(", id ");
        uart_put_dec(g_info.lapic_id);
    }
    uart_puts(g_info.from_madt ? ", base from the ACPI MADT"
                               : ", base from IA32_APIC_BASE (no MADT)");
    if (g_info.madt_cpus) {
        uart_puts(", ");
        uart_put_dec(g_info.madt_cpus);
        uart_puts(" CPU(s) listed");
    }
    uart_puts("\n  [IRQ] IOAPIC ");
    if (g_info.ioapic_pins) {
        uart_puts("at 0x");
        uart_put_hex(g_info.ioapic_base);
        uart_puts(": ");
        uart_put_dec(g_info.ioapic_pins);
        uart_puts(" lines masked (routing: TODO)\n");
    } else {
        uart_puts("not described: left as firmware set it\n");
    }
    return 0;
}

int x86_timer_boot(void)
{
    if (x86_timer_start() != 0) {
        uart_puts("  [TIMER] no PIT to calibrate against: LAPIC timer not started\n");
        return -1;
    }
    /* Interrupts on from here: every vector has a handler and every IRQ runs
     * on IST1. Wait for a few real ticks, bounded by the TSC. */
    __asm__ __volatile__("sti");
    uint64_t t0 = rdtsc(), limit = g_info.tsc_per_tick * 50u;
    while (g_ticks < 5u && rdtsc() - t0 < limit) __asm__ __volatile__("hlt");
    uint64_t seen = g_ticks;
    uart_puts("  [TIMER] LAPIC timer ");
    uart_put_dec(g_info.lapic_hz / 1000u);
    uart_puts(" kHz (bus/16, PIT-calibrated), periodic ");
    uart_put_dec(X86_HZ);
    uart_puts(" Hz, count ");
    uart_put_dec(g_info.timer_count);
    uart_puts(", TSC ");
    uart_put_dec(g_info.tsc_per_tick * X86_HZ / 1000000u);
    uart_puts(" MHz; ");
    uart_put_dec(seen);
    uart_puts(seen >= 5u ? " ticks taken\n" : " ticks taken (expected 5)\n");
    return seen >= 5u ? 0 : -1;
}
