/* keyboard.h — PS/2 Keyboard driver
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>
#include <stdbool.h>
#include "../idt/idt.h"

#define KB_BUFFER_SIZE 256
#define KB_DATA_PORT  0x60
#define KB_STATUS_PORT 0x64
#define KB_CMD_PORT   0x64

typedef struct keyboard_state {
    uint8_t buffer[KB_BUFFER_SIZE];
    uint32_t buf_head;
    uint32_t buf_tail;
    uint32_t buf_count;
    bool shift;
    bool ctrl;
    bool alt;
    bool caps_lock;
    bool num_lock;
    bool scroll_lock;
} keyboard_state_t;

void keyboard_init(void);
void keyboard_handler(registers_t *regs);
int keyboard_getchar(void);
int keyboard_has_data(void);
keyboard_state_t *keyboard_get_state(void);

extern const char scancode_to_ascii[128];
extern const char scancode_shift[128];

#endif
