/* boot_arm64.s — Minimal ARM64 entry point for VOVINA SHAKINA
 *
 * Target: QEMU virt machine, loaded via -kernel.
 * Sets up a stack and calls kernel_main.
 *
 * Author: 36N9 Genetics, LLC
 * License: SEL-3.3
 */

.section .text.boot
.global _start
.global _hang

_start:
    /* Preserve the load base in x9 and boot data in x10/x11 */
    mov     x9, x0
    mov     x10, x1
    mov     x11, x2

    /* Early serial heartbeat for QEMU virt PL011 @ 0x09000000 */
    ldr     x3, =0x09000000
    mov     w4, #0
    str     w4, [x3, #0x30]           /* CR = 0 disable */
    mov     w4, #0x70
    str     w4, [x3, #0x2c]           /* LCR_H = 8N1, FIFO */
    mov     w4, #13
    str     w4, [x3, #0x24]           /* IBRD */
    mov     w4, #1
    str     w4, [x3, #0x28]           /* FBRD */
    mov     w4, #0x301
    str     w4, [x3, #0x30]           /* CR = enable+tx+rx */
1:
    ldr     w4, [x3, #0x18]           /* FR */
    tbz     w4, #7, 1b                /* wait for TXFE */
    mov     w4, #'B'
    str     w4, [x3]                  /* DR */

    /* Set up the stack at the top of our reserved BSS */
    ldr     x2, =_stack_top
    mov     sp, x2

    /* Clear BSS */
    ldr     x0, =_bss_start
    ldr     x1, =_bss_end
    sub     x2, x1, x0
    cbz     x2, .Lbss_done
.Lclear:
    str     xzr, [x0], #8
    sub     x2, x2, #8
    cbnz    x2, .Lclear
.Lbss_done:

    /* Call kernel_main */
    mov     x0, x9
    mov     x1, x10
    mov     x2, x11
    bl      kernel_main

_hang:
    wfe
    b       _hang
