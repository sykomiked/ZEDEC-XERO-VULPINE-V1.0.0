/* arch/cpu.h — Architecture-independent CPU control
 *
 * Author: 36N9 Genetics, LLC
 * License: SEL-3.3
 */
#ifndef ARCH_CPU_H
#define ARCH_CPU_H

#if defined(__x86_64__) || defined(__i386__)
#include <stdint.h>

#define arch_interrupts_enable() __asm__ __volatile__("sti")
#define arch_interrupts_disable() __asm__ __volatile__("cli")
#define arch_halt() __asm__ __volatile__("hlt")

static inline void arch_outb(uint16_t port, uint8_t val) {
    __asm__ __volatile__("outb %0, %1" :: "a"(val), "d"(port));
}

static inline uint8_t arch_inb(uint16_t port) {
    uint8_t v;
    __asm__ __volatile__("inb %1, %0" : "=a"(v) : "d"(port));
    return v;
}

#elif defined(__aarch64__)
#include <stdint.h>

#define arch_interrupts_enable() __asm__ __volatile__("msr daifclr, #2")
#define arch_interrupts_disable() __asm__ __volatile__("msr daifset, #2")
#define arch_halt() __asm__ __volatile__("wfi")

/* On ARM64, ports are MMIO; stub these. Real drivers use devicetree/ACPI. */
static inline void arch_outb(uint16_t port, uint8_t val) { (void)port; (void)val; }
static inline uint8_t arch_inb(uint16_t port) { (void)port; return 0; }

#else
#error "Unsupported architecture"
#endif

#endif
