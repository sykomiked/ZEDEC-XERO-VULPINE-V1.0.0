/* uart_pl011_arm32.c — PL011 UART driver for ARM32
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * License: SEL-3.3 + CC BY 4.0 + OPL v1.1
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
