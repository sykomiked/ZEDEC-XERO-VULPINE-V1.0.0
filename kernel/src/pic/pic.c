/* pic.c — 8259 PIC implementation
 * Remaps IRQ 0-15 to ISRs 32-47.
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#include "pic.h"

/* ---- MODBIND DECLARATION — L4 devices -------------------------------------
 * Authored as a COMMENT, deliberately, and not as code. The ZXV_PROVIDES /
 * ZXV_REQUIRES macro mechanism is being built concurrently in kernel/include/
 * and does not exist in this tree yet. Hand-rolling an mb_module_t here would
 * stand up a SECOND registry competing with that one, which is worse than
 * waiting -- so this block is written in the exact shape
 * PROVENANCE/EVENT_SPACE_BRINGUP.md §1 specifies and converts verbatim when
 * the header lands. Same block, same reasoning, in the other five re-homed
 * x86 device modules; see PROVENANCE/X86_REHOME.md.
 *
 *   ZXV_PROVIDES(irq_ctrl_ready)
 *   ZXV_REQUIRES()                 -- nothing
 *   ZXV_BRINGUP(pic_init)
 *
 * Argued from the code, not from the name: pic_init touches only the four
 * 8259 port addresses defined in pic.h, calls nothing outside this file, and
 * `nm -u` on the object is empty. It has no requirement to declare. It is the
 * root of this cluster -- keyboard, mouse and timer all reach pic_unmask, and
 * none of them reach anything else.
 */

#ifndef TEST_HOST
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ __volatile__("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ __volatile__("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
#else
static inline void outb(uint16_t port, uint8_t val) { (void)port; (void)val; }
static inline uint8_t inb(uint16_t port) { (void)port; return 0; }
#endif

void pic_init(void) {
    outb(PIC1_CMD, 0x11); outb(PIC2_CMD, 0x11);
    outb(PIC1_DATA, 0x20); outb(PIC2_DATA, 0x28);
    outb(PIC1_DATA, 0x04); outb(PIC2_DATA, 0x02);
    outb(PIC1_DATA, 0x01); outb(PIC2_DATA, 0x01);
    outb(PIC1_DATA, 0xFF); outb(PIC2_DATA, 0xFF);
}

void pic_send_eoi(uint8_t irq) {
    if (irq >= 8)
        outb(PIC2_CMD, PIC_EOI);
    outb(PIC1_CMD, PIC_EOI);
}

void pic_mask(uint8_t irq) {
    if (irq < 8) {
        outb(PIC1_DATA, inb(PIC1_DATA) | (1 << irq));
    } else {
        outb(PIC2_DATA, inb(PIC2_DATA) | (1 << (irq - 8)));
    }
}

void pic_unmask(uint8_t irq) {
    if (irq < 8) {
        outb(PIC1_DATA, inb(PIC1_DATA) & ~(1 << irq));
    } else {
        outb(PIC2_DATA, inb(PIC2_DATA) & ~(1 << (irq - 8)));
        outb(PIC1_DATA, inb(PIC1_DATA) & ~(1 << IRQ_CASCADE));
    }
}
