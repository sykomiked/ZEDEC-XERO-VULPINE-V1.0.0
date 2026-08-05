/* keyboard.c — PS/2 Keyboard driver implementation
 * US QWERTY scancode set 1 translation.
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#include "keyboard.h"
#include "../pic/pic.h"

#ifndef TEST_HOST
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ __volatile__("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
#else
static inline uint8_t inb(uint16_t port) { (void)port; return 0; }
#endif

static keyboard_state_t kb_state;

const char scancode_to_ascii[128] = {
    0, 27, '1','2','3','4','5','6','7','8','9','0','-','=', '\b',
    '\t', 'q','w','e','r','t','y','u','i','o','p','[',']', '\n',
    0, 'a','s','d','f','g','h','j','k','l',';','\'', '`',
    0, '\\', 'z','x','c','v','b','n','m',',','.','/', 0,
    '*', 0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '7','8','9','-','4','5','6','+',
    '1','2','3','0','.', 0, 0, 0, 0, 0,
};

const char scancode_shift[128] = {
    0, 27, '!','@','#','$','%','^','&','*','(',')','_','+', '\b',
    '\t', 'Q','W','E','R','T','Y','U','I','O','P','{','}', '\n',
    0, 'A','S','D','F','G','H','J','K','L',':','"', '~',
    0, '|', 'Z','X','C','V','B','N','M','<','>','?', 0,
    '*', 0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '7','8','9','-','4','5','6','+',
    '1','2','3','0','.', 0, 0, 0, 0, 0,
};

void keyboard_init(void) {
    for (int i = 0; i < KB_BUFFER_SIZE; i++) kb_state.buffer[i] = 0;
    kb_state.buf_head = 0;
    kb_state.buf_tail = 0;
    kb_state.buf_count = 0;
    kb_state.shift = false;
    kb_state.ctrl = false;
    kb_state.alt = false;
    kb_state.caps_lock = false;
    kb_state.num_lock = false;
    kb_state.scroll_lock = false;
    pic_unmask(IRQ_KEYBOARD);
}

void keyboard_handler(registers_t *regs) {
    (void)regs;
    uint8_t scancode = inb(KB_DATA_PORT);

    if (scancode & 0x80) {
        uint8_t key = scancode & 0x7F;
        if (key == 0x2A || key == 0x36) kb_state.shift = false;
        if (key == 0x1D) kb_state.ctrl = false;
        if (key == 0x38) kb_state.alt = false;
        return;
    }

    switch (scancode) {
        case 0x2A: case 0x36: kb_state.shift = true; return;
        case 0x1D: kb_state.ctrl = true; return;
        case 0x38: kb_state.alt = true; return;
        case 0x3A: kb_state.caps_lock = !kb_state.caps_lock; return;
        case 0x45: kb_state.num_lock = !kb_state.num_lock; return;
        case 0x46: kb_state.scroll_lock = !kb_state.scroll_lock; return;
    }

    char c = 0;
    if (scancode < 128) {
        if (kb_state.shift)
            c = scancode_shift[scancode];
        else if (kb_state.caps_lock && scancode_to_ascii[scancode] >= 'a' && scancode_to_ascii[scancode] <= 'z')
            c = scancode_to_ascii[scancode] - 32;
        else
            c = scancode_to_ascii[scancode];
    }

    if (c && kb_state.buf_count < KB_BUFFER_SIZE) {
        kb_state.buffer[kb_state.buf_head] = (uint8_t)c;
        kb_state.buf_head = (kb_state.buf_head + 1) % KB_BUFFER_SIZE;
        kb_state.buf_count++;
    }
}

int keyboard_has_data(void) {
    return kb_state.buf_count > 0;
}

int keyboard_getchar(void) {
    if (kb_state.buf_count == 0) return -1;
    uint8_t c = kb_state.buffer[kb_state.buf_tail];
    kb_state.buf_tail = (kb_state.buf_tail + 1) % KB_BUFFER_SIZE;
    kb_state.buf_count--;
    return (int)c;
}

keyboard_state_t *keyboard_get_state(void) {
    return &kb_state;
}
