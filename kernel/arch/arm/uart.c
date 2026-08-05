/* uart.c — PL011 UART output for ARM (QEMU virt machine)
 * Replaces VGA text mode framebuffer + COM1 serial for boot output.
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#include "uart.h"
#include "arch.h"

/* PL011 UART registers */
#define UART_DR     (UART0_BASE + 0x00)  /* Data register */
#define UART_FR     (UART0_BASE + 0x18)  /* Flag register */
#define UART_IBRD   (UART0_BASE + 0x24)  /* Integer baud rate divisor */
#define UART_FBRD   (UART0_BASE + 0x28)  /* Fractional baud rate divisor */
#define UART_LCRH   (UART0_BASE + 0x2C)  /* Line control */
#define UART_CR     (UART0_BASE + 0x30)  /* Control register */
#define UART_IMSC   (UART0_BASE + 0x38)  /* Interrupt mask set/clear */

#define UART_FR_TXFF (1 << 5)  /* Transmit FIFO full */

static uint32_t uart_col = 0;
static uint32_t uart_row = 0;

void uart_init(void) {
    /* Disable UART */
    mmio_write(UART_CR, 0);

    /* Set baud rate to 38400 (QEMU default for virt machine) */
    /* For 24MHz clock: IBRD=39, FBRD=25 → 38400 baud */
    mmio_write(UART_IBRD, 39);
    mmio_write(UART_FBRD, 25);

    /* 8N1, FIFO enabled */
    mmio_write(UART_LCRH, (1 << 4) | (3 << 5));

    /* Disable all interrupts */
    mmio_write(UART_IMSC, 0);

    /* Enable UART, TX, RX */
    mmio_write(UART_CR, (1 << 0) | (1 << 8) | (1 << 9));

    uart_col = 0;
    uart_row = 0;
}

static void uart_putc_raw(char c) {
    /* Wait for transmit FIFO to have space */
    while (mmio_read(UART_FR) & UART_FR_TXFF) {
        /* spin */
    }
    mmio_write8(UART_DR, (uint8_t)c);
}

void uart_putc(char c) {
    if (c == '\n') {
        uart_putc_raw('\r');
        uart_putc_raw('\n');
        uart_col = 0;
        uart_row++;
        return;
    }
    if (c == '\r') {
        uart_putc_raw('\r');
        uart_col = 0;
        return;
    }
    uart_putc_raw(c);
    uart_col++;
    if (uart_col >= 80) {
        uart_col = 0;
        uart_row++;
    }
}

void uart_puts(const char *str) {
    while (*str) {
        uart_putc(*str);
        str++;
    }
}

void uart_clear(void) {
    /* Send form feed + home cursor for terminal emulators */
    uart_puts("\033[2J\033[H");
    uart_col = 0;
    uart_row = 0;
}

char uart_getc(void) {
    /* Wait for data in receive FIFO */
    while (mmio_read(UART_FR) & (1 << 4)) {
        /* RX FIFO empty — spin */
    }
    return (char)(mmio_read(UART_DR) & 0xFF);
}

int uart_has_data(void) {
    return !(mmio_read(UART_FR) & (1 << 4));
}
