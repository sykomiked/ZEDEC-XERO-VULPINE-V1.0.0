/* gic.h — ARM Generic Interrupt Controller (GICv2) for QEMU virt
 * Replaces the x86 8259 PIC.
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#ifndef ARM_GIC_H
#define ARM_GIC_H

#include <stdint.h>
#include "registers.h"

/* GIC Distributor registers */
#define GICD_CTLR       (GIC_DIST_BASE + 0x000)
#define GICD_TYPER      (GIC_DIST_BASE + 0x004)
#define GICD_ISENABLER  (GIC_DIST_BASE + 0x100)
#define GICD_ICENABLER  (GIC_DIST_BASE + 0x180)
#define GICD_ISPENDR    (GIC_DIST_BASE + 0x200)
#define GICD_ICPENDR    (GIC_DIST_BASE + 0x280)
#define GICD_IPRIORITY  (GIC_DIST_BASE + 0x400)
#define GICD_ITARGETSR  (GIC_DIST_BASE + 0x800)
#define GICD_ICFGR      (GIC_DIST_BASE + 0xC00)

/* GIC CPU interface registers */
#define GICC_CTLR       (GIC_CPU_BASE + 0x000)
#define GICC_PMR        (GIC_CPU_BASE + 0x004)
#define GICC_IAR        (GIC_CPU_BASE + 0x0C)
#define GICC_EOIR       (GIC_CPU_BASE + 0x010)

void gic_init(void);
void gic_enable_irq(uint32_t irq);
void gic_disable_irq(uint32_t irq);
void gic_send_eoi(uint32_t irq);
uint32_t gic_get_irq(void);

/* Compatibility with x86 PIC API */
#define pic_init        gic_init
#define pic_unmask(irq) gic_enable_irq(irq)
#define pic_mask(irq)   gic_disable_irq(irq)
#define pic_send_eoi(irq) gic_send_eoi(irq)

/* IRQ numbers (compatibility with x86 defines) */
#define IRQ_TIMER       27
#define IRQ_KEYBOARD    33      /* UART on ARM */
#define IRQ_CASCADE     0
#define IRQ_MOUSE       34

#endif /* ARM_GIC_H */
