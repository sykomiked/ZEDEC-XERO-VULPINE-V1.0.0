/* uart.h — PL011 UART for ARM (QEMU virt machine)
 * Replaces VGA framebuffer + COM1 serial.
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#ifndef ARM_UART_H
#define ARM_UART_H

#include <stdint.h>

void uart_init(void);
void uart_putc(char c);
void uart_puts(const char *str);
void uart_clear(void);
char uart_getc(void);
int uart_has_data(void);

#endif /* ARM_UART_H */
