/* x86_console.h — the x86-64 console after [BOOT_OK]: typed lines from the
 * PS/2 keyboard and from COM1, and the login commands the arm64 shell has.
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
#ifndef X86_CONSOLE_H
#define X86_CONSOLE_H

#include <stdint.h>

/* Brings up the i8042 keyboard port when there is one and says so; COM1 is
 * always read. Prints the console's line and its first prompt. */
void x86_console_init(void (*out)(const char *));

/* Reads every byte waiting on both inputs; a whole line runs its command.
 * Never blocks. */
void x86_console_poll(void);

/* The line editor and the commands, without the hardware: feeds one byte as
 * if typed. Exposed so a host test drives exactly what the keyboard does. */
void x86_console_feed(uint8_t c);

/* Set-1 scancode (as the i8042 translates) to the byte it types, or 0 when
 * the code types nothing (a release, a modifier, an unmapped key). Keeps the
 * shift and caps-lock state between calls. */
uint8_t x86_console_scancode(uint8_t sc);

#endif /* X86_CONSOLE_H */
