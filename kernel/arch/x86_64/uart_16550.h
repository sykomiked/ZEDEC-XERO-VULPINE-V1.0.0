/* uart_16550.h — x86-64 NS16550A COM1 serial console
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * License: SEL-3.3
 */
#ifndef UART_16550_H
#define UART_16550_H

#include <stdint.h>

void uart_init(void);
void uart_putc(char c);
void uart_puts(const char *s);
void uart_put_hex(uint64_t val);
void uart_put_dec(uint64_t val);

#endif
