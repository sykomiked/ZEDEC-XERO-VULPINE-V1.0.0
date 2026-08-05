/* gic.c — ARM Generic Interrupt Controller (GICv2) implementation
 * Replaces the x86 8259 PIC for QEMU virt machine.
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#include "gic.h"
#include "arch.h"

void gic_init(void) {
    /* Disable distributor and CPU interface during setup */
    mmio_write(GICD_CTLR, 0);
    mmio_write(GICC_CTLR, 0);

    /* Set priority mask to allow all priorities */
    mmio_write(GICC_PMR, 0xFF);

    /* Set all SPI interrupts (32+) to target CPU 0 */
    for (uint32_t i = 32; i < 96; i += 4) {
        mmio_write(GICD_ITARGETSR + i, 0x01010101);
    }

    /* Set all interrupts to priority 0 (highest) */
    for (uint32_t i = 0; i < 96; i += 4) {
        mmio_write(GICD_IPRIORITY + i, 0x00000000);
    }

    /* Set all SPI interrupts to level-triggered */
    for (uint32_t i = 32; i < 96; i += 16) {
        mmio_write(GICD_ICFGR + (i / 4), 0x00000000);
    }

    /* Enable distributor and CPU interface */
    mmio_write(GICD_CTLR, 1);
    mmio_write(GICC_CTLR, 1);
}

void gic_enable_irq(uint32_t irq) {
    uint32_t reg = irq / 32;
    uint32_t bit = 1 << (irq % 32);
    mmio_write(GICD_ISENABLER + reg * 4, bit);
}

void gic_disable_irq(uint32_t irq) {
    uint32_t reg = irq / 32;
    uint32_t bit = 1 << (irq % 32);
    mmio_write(GICD_ICENABLER + reg * 4, bit);
}

uint32_t gic_get_irq(void) {
    return mmio_read(GICC_IAR) & 0x3FF;
}

void gic_send_eoi(uint32_t irq) {
    mmio_write(GICC_EOIR, irq);
}

/* C-level IRQ handler — called from boot.s exception_irq */
typedef void (*isr_handler_t)(registers_t *regs);
static isr_handler_t irq_handlers[256] = {0};

void arm_irq_register(uint32_t irq, isr_handler_t handler) {
    if (irq < 256)
        irq_handlers[irq] = handler;
}

void irq_handler_c(registers_t *regs, uint32_t exc_num) {
    (void)exc_num;
    uint32_t irq = gic_get_irq();

    if (irq < 256 && irq_handlers[irq]) {
        irq_handlers[irq](regs);
    }

    gic_send_eoi(irq);
}

/* Exception handler for non-IRQ exceptions */
void exception_handler(registers_t *regs, uint32_t exc_num) {
    (void)regs;
    (void)exc_num;
    /* Hang on exception — in a real system we'd log and recover */
    while (1) {
        arch_halt();
    }
}
