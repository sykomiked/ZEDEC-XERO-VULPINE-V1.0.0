/* uart_ns16550.c — NS16550A UART driver for RISC-V (board from riscv_board.h)
 *
 * Hardware-as-code: implements the NS16550A UART as a virtual device.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#include "riscv_arch.h"
#include "riscv_board.h"

/* Register access through the board profile (riscv_board.h): stride 1 byte
 * with byte accesses (QEMU virt), or stride 4 with 32-bit accesses, as a
 * reg-io-width 4 device (PolarFire MMUART) requires. */
static inline uint8_t ureg_read(uint32_t reg)
{
    const rv_board_t *b = rv_board();
    uintptr_t a = b->uart_base + ((uintptr_t) reg << b->uart_reg_shift);
    return b->uart_reg_shift == 2 ? (uint8_t) mmio_read(a) : mmio_read8(a);
}

static inline void ureg_write(uint32_t reg, uint8_t v)
{
    const rv_board_t *b = rv_board();
    uintptr_t a = b->uart_base + ((uintptr_t) reg << b->uart_reg_shift);
    if (b->uart_reg_shift == 2)
        mmio_write(a, v);
    else
        mmio_write8(a, v);
}

static inline void uart_putc(char c) {
    while ((ureg_read(UART_LSR) & 0x20) == 0) {
    }
    ureg_write(UART_THR, (uint8_t) c);
}

static inline char uart_getc(void) {
    while ((ureg_read(UART_LSR) & 0x01) == 0) {
    }
    return (char) ureg_read(UART_RHR);
}

void uart_init(void) {
    /* Disable interrupts */
    ureg_write(UART_IER, 0x00);

    if (!rv_board()->uart_keep_baud) {
        /* Enable DLAB to set baud rate */
        ureg_write(UART_LCR, 0x80);

        /* Set divisor to 1 (115200 baud at 1.8432 MHz or QEMU default) */
        ureg_write(0x00, 0x01);
        ureg_write(0x01, 0x00);
    }
    /* On a board whose firmware already runs the console (PolarFire: HSS /
     * OpenSBI at 115200 from the MMUART's own clock), the divisor is left as
     * the firmware set it: rewriting it needs the UART clock, which this
     * kernel does not read from the device tree. */

    /* 8 bits, no parity, 1 stop bit, disable DLAB */
    ureg_write(UART_LCR, 0x03);

    /* Enable FIFO, clear them */
    ureg_write(UART_FCR, 0x07);

    /* Enable RX interrupt */
    ureg_write(UART_IER, 0x01);
}

void uart_puts(const char *s) {
    while (*s) {
        if (*s == '\n') uart_putc('\r');
        uart_putc(*s++);
    }
}

void uart_put_hex(uint64_t val) {
    char buf[17];
    int i;
    for (i = 15; i >= 0; i--) {
        int nibble = (val >> (i * 4)) & 0xF;
        buf[15 - i] = nibble < 10 ? '0' + nibble : 'A' + nibble - 10;
    }
    buf[16] = 0;
    uart_puts(buf);
}

void uart_put_dec(uint64_t val) {
    if (val == 0) { uart_putc('0'); return; }
    char tmp[20];
    int i = 0;
    while (val > 0) {
        tmp[i++] = '0' + (val % 10);
        val /= 10;
    }
    while (i > 0) uart_putc(tmp[--i]);
}

bool uart_rx_ready(void) {
    return (ureg_read(UART_LSR) & 0x01) != 0;
}
