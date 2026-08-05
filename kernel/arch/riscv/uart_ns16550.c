/* uart_ns16550.c — NS16550A UART driver for RISC-V (QEMU virt)
 *
 * Hardware-as-code: implements the NS16550A UART as a virtual device.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#include "riscv_arch.h"

static inline void uart_putc(char c) {
    while ((mmio_read8(UART0_BASE + UART_LSR) & 0x20) == 0) { }
    mmio_write8(UART0_BASE + UART_THR, (uint8_t)c);
}

static inline char uart_getc(void) {
    while ((mmio_read8(UART0_BASE + UART_LSR) & 0x01) == 0) { }
    return (char)mmio_read8(UART0_BASE + UART_RHR);
}

void uart_init(void) {
    /* Disable interrupts */
    mmio_write8(UART0_BASE + UART_IER, 0x00);

    /* Enable DLAB to set baud rate */
    mmio_write8(UART0_BASE + UART_LCR, 0x80);

    /* Set divisor to 1 (115200 baud at 1.8432 MHz or QEMU default) */
    mmio_write8(UART0_BASE + 0x00, 0x01);
    mmio_write8(UART0_BASE + 0x01, 0x00);

    /* 8 bits, no parity, 1 stop bit, disable DLAB */
    mmio_write8(UART0_BASE + UART_LCR, 0x03);

    /* Enable FIFO, clear them */
    mmio_write8(UART0_BASE + UART_FCR, 0x07);

    /* Enable RX interrupt */
    mmio_write8(UART0_BASE + UART_IER, 0x01);
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
    return (mmio_read8(UART0_BASE + UART_LSR) & 0x01) != 0;
}
