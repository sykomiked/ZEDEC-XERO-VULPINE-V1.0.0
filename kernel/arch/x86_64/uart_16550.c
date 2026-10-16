/* uart_16550.c — x86-64 NS16550A COM1 serial console
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#include "uart_16550.h"
#include "x86_64_arch.h"
#include "efifb_con.h"

#define COM1     0x3F8
#define VGA_BASE 0xB8000ULL
#define VGA_COLS 80u
#define VGA_ROWS 25u

static int g_uart_ok;
static uint32_t g_vga_row = 5, g_vga_col;

static void vga_putc(char c)
{
    volatile uint16_t *vga = (volatile uint16_t *) (uintptr_t) VGA_BASE;
    if (c == '\r') {
        g_vga_col = 0;
        return;
    }
    if (c == '\n') {
        g_vga_col = 0;
        g_vga_row++;
    } else if ((uint8_t) c >= 0x20) {
        vga[g_vga_row * VGA_COLS + g_vga_col] = (uint16_t) ((uint8_t) c | 0x0700u);
        if (++g_vga_col >= VGA_COLS) {
            g_vga_col = 0;
            g_vga_row++;
        }
    }
    if (g_vga_row >= VGA_ROWS) {
        for (uint32_t i = 0; i < (VGA_ROWS - 1) * VGA_COLS; i++) vga[i] = vga[i + VGA_COLS];
        for (uint32_t i = (VGA_ROWS - 1) * VGA_COLS; i < VGA_ROWS * VGA_COLS; i++) vga[i] = 0x0720u;
        g_vga_row = VGA_ROWS - 1;
    }
}

void uart_init(void)
{
    outb(COM1 + 1, 0x00); /* disable all interrupts */
    outb(COM1 + 3, 0x80); /* enable DLAB */
    outb(COM1 + 0, 0x03); /* set divisor to 3 (38400 baud) */
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03); /* 8 bits, no parity, one stop bit */
    outb(COM1 + 2, 0xC7); /* enable FIFO, clear, 14-byte threshold */
    outb(COM1 + 4, 0x0B); /* IRQs enabled, RTS/DSR set */
    outb(COM1 + 4, 0x1E); /* set in loopback mode for testing */
    outb(COM1 + 0, 0xAE); /* test serial chip */

    /* If the serial chip is not a 16550A, leave g_uart_ok = 0 so uart_putc
     * never spins waiting for a missing port's LSR. */
    if (inb(COM1 + 0) != 0xAE) return;

    outb(COM1 + 4, 0x0F); /* set normal mode */
    g_uart_ok = 1;
}

static int uart_ready(void)
{
    return inb(COM1 + 5) & 0x20;
}

void uart_putc(char c)
{
    if (g_uart_ok) {
        for (uint32_t spin = 0; spin < 100000u && !uart_ready(); spin++) {
        }
        outb(COM1, (uint8_t) c);
    }
    if (efifb_active())
        efifb_putc(c);
    else
        vga_putc(c);
}

void uart_puts(const char *s)
{
    while (*s) {
        if (*s == '\n') uart_putc('\r');
        uart_putc(*s++);
    }
}

void uart_put_hex(uint64_t val)
{
    char buf[17];
    int i;
    for (i = 15; i >= 0; i--) {
        int nibble = (val >> (i * 4)) & 0xF;
        buf[15 - i] = nibble < 10 ? '0' + nibble : 'A' + nibble - 10;
    }
    buf[16] = 0;
    uart_puts(buf);
}

void uart_put_dec(uint64_t val)
{
    if (val == 0) {
        uart_putc('0');
        return;
    }
    char tmp[20];
    int i = 0;
    while (val > 0) {
        tmp[i++] = '0' + (val % 10);
        val /= 10;
    }
    while (i > 0) uart_putc(tmp[--i]);
}

int uart_try_getc(void)
{
    if (!(inb(COM1 + 5) & 0x01)) return -1; /* LSR.DR: no byte received */
    return inb(COM1);
}
