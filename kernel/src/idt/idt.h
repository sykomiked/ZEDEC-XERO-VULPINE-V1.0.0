/* idt.h — Interrupt Descriptor Table
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#ifndef IDT_H
#define IDT_H

#include <stdint.h>
#include <stdbool.h>

#define IDT_ENTRIES 256

typedef struct registers registers_t;

typedef struct idt_entry {
    uint16_t base_low;
    uint16_t selector;
    uint8_t  zero;
    uint8_t  flags;
    uint16_t base_high;
} __attribute__((packed)) idt_entry_t;

typedef struct idt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed)) idt_ptr_t;

typedef void (*isr_handler_t)(registers_t *regs);

struct registers {
    uint32_t ds;
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
    uint32_t int_no, err_code;
    uint32_t eip, cs, eflags, useresp, ss;
} __attribute__((packed));

void idt_init(void);
void idt_set_gate(int idx, uint32_t base, uint16_t sel, uint8_t flags);
void idt_register_handler(int irq, isr_handler_t handler);
void irq_handler(registers_t *regs);

/* ISR stub declarations (defined in isr_stubs.s) */
extern void isr0(void),  isr1(void),  isr2(void),  isr3(void),  isr4(void);
extern void isr5(void),  isr6(void),  isr7(void),  isr8(void),  isr9(void);
extern void isr10(void), isr11(void), isr12(void), isr13(void), isr14(void);
extern void isr15(void), isr16(void), isr17(void), isr18(void), isr19(void);
extern void isr20(void), isr21(void), isr22(void), isr23(void), isr24(void);
extern void isr25(void), isr26(void), isr27(void), isr28(void), isr29(void);
extern void isr30(void), isr31(void);
extern void isr32(void), isr33(void), isr34(void), isr35(void), isr36(void);
extern void isr37(void), isr38(void), isr39(void), isr40(void), isr41(void);
extern void isr42(void), isr43(void), isr44(void), isr45(void), isr46(void);
extern void isr47(void);

#endif
