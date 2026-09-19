/* uart_pl011_arm32.c — PL011 UART driver for ARM32
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#include "arm32_arch.h"

void uart_init(void) {
    /* Disable UART */
    ARM32_UART0_CR = 0x00000000;
    /* Set baud rate to 38400 (assuming 24MHz UART clock) */
    ARM32_UART0_IBRD = 39;
    ARM32_UART0_FBRD = 1;
    /* 8N1, FIFO enabled */
    ARM32_UART0_LCRH = 0x00000070;
    /* Enable UART, TX, RX */
    ARM32_UART0_CR = 0x00000301;
}

void uart_putc(char c) {
    if (c == '\n') uart_putc('\r');
    arm32_uart_putc(c);
}

char uart_getc(void) {
    return arm32_uart_getc();
}

void uart_puts(const char *s) {
    while (*s) {
        uart_putc(*s++);
    }
}

void uart_hex(uint32_t val) {
    char hex[] = "0123456789ABCDEF";
    uart_puts("0x");
    for (int i = 28; i >= 0; i -= 4) {
        uart_putc(hex[(val >> i) & 0xF]);
    }
}

void uart_dec(uint32_t val) {
    char buf[12];
    int i = 10;
    buf[10] = '\0';
    if (val == 0) { uart_putc('0'); return; }
    while (val > 0 && i > 0) {
        buf[--i] = '0' + (val % 10);
        val /= 10;
    }
    uart_puts(&buf[i]);
}

/* uart_put_dec — the 64-bit decimal helper the shared drivers call.
 * arm32 already had uart_dec(uint32_t); the portable device layer needs the
 * 64-bit name, so provide it here rather than narrowing the shared callers. */
void uart_put_dec(uint64_t val) {
    if (val == 0) { uart_putc(0x30); return; }
    char tmp[20];
    int i = 0;
    while (val > 0) { tmp[i++] = (char)(0x30 + (val % 10)); val /= 10; }
    while (i > 0) uart_putc(tmp[--i]);
}
