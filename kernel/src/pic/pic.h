/* pic.h — Programmable Interrupt Controller (8259 PIC)
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#ifndef PIC_H
#define PIC_H

#include <stdint.h>

#define PIC1_CMD   0x20
#define PIC1_DATA  0x21
#define PIC2_CMD   0xA0
#define PIC2_DATA  0xA1
#define PIC_EOI    0x20

#define IRQ_TIMER    0
#define IRQ_KEYBOARD 1
#define IRQ_CASCADE  2
#define IRQ_COM2     3
#define IRQ_COM1     4
#define IRQ_LPT2     5
#define IRQ_FLOPPY   6
#define IRQ_LPT1     7
#define IRQ_RTC      8
#define IRQ_PCI_A    9
#define IRQ_PCI_B    10
#define IRQ_PCI_C    11
#define IRQ_MOUSE    12
#define IRQ_FPU      13
#define IRQ_PRIMARY_ATA   14
#define IRQ_SECONDARY_ATA 15

void pic_init(void);
void pic_send_eoi(uint8_t irq);
void pic_mask(uint8_t irq);
void pic_unmask(uint8_t irq);

#endif
