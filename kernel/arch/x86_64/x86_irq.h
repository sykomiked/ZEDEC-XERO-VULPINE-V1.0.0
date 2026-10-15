/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* x86_irq.h: x86_64 interrupts: one IDT, the local APIC and its timer.
 *
 *   IDT       all 256 vectors go through x86_trap_asm.S into x86_trap() with a
 *             full register frame. CPU exceptions 0-31, the legacy PIC range
 *             0x20-0x2F (masked; only its spurious IRQ 7/15 can arrive), the
 *             LAPIC timer, error and spurious vectors, and int 0x80 (DPL 3).
 *   PIC       remapped to 0x20/0x28 and fully masked, so an 8259 spurious IRQ
 *             can never land on an exception vector.
 *   LAPIC     x2APIC (MSRs) when the CPU has it, else xAPIC (MMIO, mapped
 *             uncached). The base comes from the ACPI MADT that the boot
 *             handoff points at, else from IA32_APIC_BASE.
 *   IOAPIC    every redirection entry masked. Routing a device line is not
 *             done here (TODO: route COM1 and PCI INTx when a driver needs one).
 *   timer     the LAPIC timer, calibrated against PIT channel 2 over a 10 ms
 *             gate, periodic at X86_HZ. The TSC is measured in the same window.
 *
 * Plain convention throughout (docs/BAREMETAL_OS_GAPS.md section 3): no
 * verdict tags on interrupts. Integer only.
 *
 * The pure helpers at the bottom (calibration arithmetic, MADT walk) are
 * static inline so the host test compiles them without the kernel. */
#ifndef ZXV_X86_IRQ_H
#define ZXV_X86_IRQ_H

#include <stdint.h>

/* Vectors. 0x20-0x2F stay the masked legacy PIC's; the APIC's own vectors sit
 * above them. 0x80 is the shared-ABI syscall gate (ring3.c used it first). */
#define X86_VEC_PIC_BASE  0x20u
#define X86_VEC_TIMER     0x40u
#define X86_VEC_SYSCALL   0x80u
#define X86_VEC_APIC_ERR  0xFEu
#define X86_VEC_SPURIOUS  0xFFu

#define X86_HZ 100u /* scheduler tick */

/* The register frame every trap stub builds, lowest address first. The CPU
 * pushes rip..ss (always, in 64-bit mode); the stub pushes vector, error (0
 * when the CPU pushes none) and the fifteen general registers. 22 quadwords,
 * so the stack stays 16-byte aligned for the C call. */
typedef struct {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t vector, error;
    uint64_t rip, cs, rflags, rsp, ss;
} x86_frame_t;

typedef char x86_frame_size_ok[(sizeof(x86_frame_t) == 22u * 8u) ? 1 : -1];

/* What the interrupt layer found, for the boot log and the self-test. */
typedef struct {
    uint64_t lapic_base;   /* physical; 0 in x2APIC mode means "MSRs" */
    uint64_t ioapic_base;  /* 0 when the MADT named none */
    uint32_t x2apic;       /* 1 when the LAPIC runs in x2APIC mode */
    uint32_t lapic_id;
    uint32_t madt_cpus;    /* enabled processors the MADT lists (0 = no MADT) */
    uint32_t ioapic_pins;  /* redirection entries masked */
    uint32_t from_madt;    /* 1 when the LAPIC base came from the MADT */
    uint32_t timer_count;  /* LAPIC initial count per tick (divide by 16) */
    uint64_t lapic_hz;     /* LAPIC timer input clock after the divider */
    uint64_t tsc_per_tick; /* TSC cycles per scheduler tick, 0 = unknown */
} x86_irq_info_t;

/* Build and load the IDT, remap and mask the PIC, enable the LAPIC, mask the
 * IOAPIC. rsdp is the handoff's ACPI RSDP (0 when there is none or it is not
 * trusted). Leaves interrupts disabled. Returns 0, or -1 when no LAPIC. */
int x86_irq_init(uint64_t rsdp);

/* x86_irq_init plus the boot log lines. */
int x86_irq_boot(uint64_t rsdp);

/* Calibrate and start the periodic LAPIC timer at X86_HZ. Interrupts stay
 * as they were. Returns 0, or -1 when calibration failed (no PIT). */
int x86_timer_start(void);

const x86_irq_info_t *x86_irq_info(void);

/* Timer interrupts taken since x86_timer_start. */
uint64_t x86_ticks(void);

/* C entry from every trap stub (x86_trap_asm.S). */
void x86_trap(x86_frame_t *f);

/* x86_trap_asm.S: load registers from f and IRETQ. Never returns. Interrupts
 * must be off; f may be any memory (it is used as the stack for the pops). */
__attribute__((noreturn)) void x86_frame_resume(const x86_frame_t *f);

/* A saved kernel context (callee-saved registers, stack, resume address):
 * setjmp/longjmp for the scheduler's way back into kernel_main. */
typedef struct {
    uint64_t rbx, rbp, r12, r13, r14, r15, rsp, rip;
} x86_kctx_t;

/* Returns 0 when saving, 1 when resumed by x86_kctx_restore. */
__attribute__((returns_twice)) int x86_kctx_save(x86_kctx_t *k);
__attribute__((noreturn)) void x86_kctx_restore(const x86_kctx_t *k);

/* ---- pure helpers (host-tested in test_x86_irq.c) ---------------------- */

/* LAPIC initial count for one period, from `counted` timer decrements seen
 * in a `window_us` calibration window. 0 when any input is 0 (no
 * calibration); otherwise at least 1 and at most 0xFFFFFFFF. counted is at
 * most 2^32 and period_us at most 10^6 here, so the product fits in 64 bits. */
static inline uint32_t x86_lapic_period_count(uint64_t counted, uint32_t window_us,
                                              uint32_t period_us)
{
    if (counted == 0 || window_us == 0 || period_us == 0) return 0;
    uint64_t c = counted * (uint64_t) period_us / window_us;
    if (c == 0) c = 1;
    if (c > 0xFFFFFFFFull) c = 0xFFFFFFFFull;
    return (uint32_t) c;
}

/* Clock rate in Hz from `counted` events in `window_us`. */
static inline uint64_t x86_rate_hz(uint64_t counted, uint32_t window_us)
{
    if (window_us == 0) return 0;
    return counted * 1000000ull / window_us;
}

/* What the MADT says. */
typedef struct {
    uint64_t lapic_base;    /* 32-bit field, or the type-5 64-bit override */
    uint64_t ioapic_base;   /* the first IOAPIC (type 1); 0 = none */
    uint32_t ioapic_gsi;
    uint32_t cpus;          /* enabled (or online-capable) LAPIC entries, types 0 and 9 */
    uint32_t bad;           /* 1 when an entry ran past the table */
} x86_madt_t;

static inline uint32_t x86_rd32le(const uint8_t *p)
{
    return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) |
           ((uint32_t) p[3] << 24);
}

/* Walk a MADT ("APIC" table, header included) of `len` bytes. Returns 0, or
 * -1 when the signature or length is wrong; out->bad marks a damaged entry
 * list (entries up to it are kept). Checksum is the caller's job. */
static inline int x86_madt_scan(const uint8_t *t, uint32_t len, x86_madt_t *out)
{
    out->lapic_base = 0;
    out->ioapic_base = 0;
    out->ioapic_gsi = 0;
    out->cpus = 0;
    out->bad = 0;
    if (len < 44 || t[0] != 'A' || t[1] != 'P' || t[2] != 'I' || t[3] != 'C') return -1;
    if (x86_rd32le(t + 4) != len) return -1;
    out->lapic_base = x86_rd32le(t + 36);
    uint32_t off = 44;
    while (off + 2 <= len) {
        uint8_t type = t[off], elen = t[off + 1];
        if (elen < 2 || off + elen > len) {
            out->bad = 1;
            break;
        }
        const uint8_t *e = t + off;
        if (type == 0 && elen >= 8) { /* processor local APIC */
            uint32_t fl = x86_rd32le(e + 4);
            if (fl & 3u) out->cpus++;
        } else if (type == 9 && elen >= 16) { /* processor local x2APIC */
            uint32_t fl = x86_rd32le(e + 8);
            if (fl & 3u) out->cpus++;
        } else if (type == 1 && elen >= 12) { /* IOAPIC */
            if (out->ioapic_base == 0) {
                out->ioapic_base = x86_rd32le(e + 4);
                out->ioapic_gsi = x86_rd32le(e + 8);
            }
        } else if (type == 5 && elen >= 12) { /* LAPIC address override */
            out->lapic_base = (uint64_t) x86_rd32le(e + 4) | ((uint64_t) x86_rd32le(e + 8) << 32);
        }
        off += elen;
    }
    return 0;
}

#endif /* ZXV_X86_IRQ_H */
