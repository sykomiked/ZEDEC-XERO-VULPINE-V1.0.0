/* isr_stubs.s — Assembly ISR stubs for VOVINA SHAKINA
 * Generates 48 interrupt stubs: ISRs 0-31 (CPU exceptions) and IRQs 32-47 (PIC).
 * Each stub pushes a fake error code (if the CPU didn't push one),
 * pushes the interrupt number, saves all registers, calls irq_handler,
 * restores registers, and returns via iret.
 * Author: H.M. Michael-Laurence: Curzi (c)
 */

.macro ISR_NOERRCODE n
.global isr\n
isr\n:
    pushl $0          /* fake error code */
    pushl $\n         /* interrupt number */
    jmp isr_common
.endm

.macro ISR_ERRCODE n
.global isr\n
isr\n:
    /* CPU already pushed an error code */
    pushl $\n         /* interrupt number */
    jmp isr_common
.endm

.section .text
.code32

/* CPU exception ISRs 0-31 */
ISR_NOERRCODE 0    /* #DE Divide Error */
ISR_NOERRCODE 1    /* #DB Debug */
ISR_NOERRCODE 2    /* NMI */
ISR_NOERRCODE 3    /* #BP Breakpoint */
ISR_NOERRCODE 4    /* #OF Overflow */
ISR_NOERRCODE 5    /* #BR BOUND Range */
ISR_NOERRCODE 6    /* #UD Invalid Opcode */
ISR_NOERRCODE 7    /* #NM Device Not Available */
ISR_ERRCODE   8    /* #DF Double Fault */
ISR_NOERRCODE 9    /* Coprocessor Segment Overrun */
ISR_ERRCODE   10   /* #TS Invalid TSS */
ISR_ERRCODE   11   /* #NP Segment Not Present */
ISR_ERRCODE   12   /* #SS Stack Fault */
ISR_ERRCODE   13   /* #GP General Protection */
ISR_ERRCODE   14   /* #PF Page Fault */
ISR_NOERRCODE 15   /* Reserved */
ISR_NOERRCODE 16   /* #MF FPU Error */
ISR_ERRCODE   17   /* #AC Alignment Check */
ISR_NOERRCODE 18   /* #MC Machine Check */
ISR_NOERRCODE 19   /* #XM SIMD FPU Error */
ISR_NOERRCODE 20   /* Reserved */
ISR_NOERRCODE 21   /* Reserved */
ISR_NOERRCODE 22   /* Reserved */
ISR_NOERRCODE 23   /* Reserved */
ISR_NOERRCODE 24   /* Reserved */
ISR_NOERRCODE 25   /* Reserved */
ISR_NOERRCODE 26   /* Reserved */
ISR_NOERRCODE 27   /* Reserved */
ISR_NOERRCODE 28   /* Reserved */
ISR_NOERRCODE 29   /* Reserved */
ISR_NOERRCODE 30   /* Reserved */
ISR_NOERRCODE 31   /* Reserved */

/* PIC IRQ ISRs 32-47 */
ISR_NOERRCODE 32   /* IRQ 0 - Timer */
ISR_NOERRCODE 33   /* IRQ 1 - Keyboard */
ISR_NOERRCODE 34   /* IRQ 2 - Cascade */
ISR_NOERRCODE 35   /* IRQ 3 - COM2 */
ISR_NOERRCODE 36   /* IRQ 4 - COM1 */
ISR_NOERRCODE 37   /* IRQ 5 - LPT2 */
ISR_NOERRCODE 38   /* IRQ 6 - Floppy */
ISR_NOERRCODE 39   /* IRQ 7 - LPT1 */
ISR_NOERRCODE 40   /* IRQ 8 - RTC */
ISR_NOERRCODE 41   /* IRQ 9 - PCI A */
ISR_NOERRCODE 42   /* IRQ 10 - PCI B */
ISR_NOERRCODE 43   /* IRQ 11 - PCI C */
ISR_NOERRCODE 44   /* IRQ 12 - Mouse */
ISR_NOERRCODE 45   /* IRQ 13 - FPU */
ISR_NOERRCODE 46   /* IRQ 14 - Primary ATA */
ISR_NOERRCODE 47   /* IRQ 15 - Secondary ATA */

/* Common ISR handler: save regs, call C, restore regs, iret */
isr_common:
    /* Save general-purpose registers */
    pushl %eax
    pushl %ecx
    pushl %edx
    pushl %ebx
    pushl %esp     /* current esp (points to saved regs) */
    pushl %ebp
    pushl %esi
    pushl %edi

    /* Save data segment */
    movw %ds, %ax
    pushl %eax

    /* Load kernel data segment */
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs

    /* Push pointer to registers_t struct on stack (it's already there) */
    /* The registers are laid out matching registers_t:
       edi, esi, ebp, esp, ebx, edx, ecx, eax, int_no, err_code, ... */
    /* Actually the stack now has: ds, edi, esi, ebp, esp, ebx, edx, ecx, eax, int_no, err_code, eip, cs, eflags */
    /* This matches registers_t layout if we push esp as the argument */
    pushl %esp     /* pass registers_t* as argument */
    call irq_handler
    addl $4, %esp  /* pop the argument */

    /* Restore data segment */
    popl %eax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs

    /* Restore general-purpose registers */
    popl %edi
    popl %esi
    popl %ebp
    popl %esp     /* this restores original esp (before our saves) */
    popl %ebx
    popl %edx
    popl %ecx
    popl %eax

    /* Pop error code and interrupt number */
    addl $8, %esp

    /* Return from interrupt */
    iret
