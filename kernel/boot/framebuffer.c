/* framebuffer.c — VGA text mode framebuffer + serial output implementation */
#include "framebuffer.h"

/* COM1 serial port */
#define COM1_PORT 0x3F8
#define COM1_DATA (COM1_PORT + 0)
#define COM1_IER  (COM1_PORT + 1)
#define COM1_FCR  (COM1_PORT + 2)
#define COM1_LCR  (COM1_PORT + 3)
#define COM1_MCR  (COM1_PORT + 4)
#define COM1_LSR  (COM1_PORT + 5)

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ __volatile__("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ __volatile__("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static void serial_init(void) {
    outb(COM1_IER, 0x00);
    outb(COM1_LCR, 0x80);
    outb(COM1_DATA, 0x03);
    outb(COM1_IER, 0x00);
    outb(COM1_LCR, 0x03);
    outb(COM1_FCR, 0xC7);
    outb(COM1_MCR, 0x0B);
}

static int serial_can_send(void) {
    return inb(COM1_LSR) & 0x20;
}

static void serial_putc(char c) {
    while (!serial_can_send());
    outb(COM1_DATA, (uint8_t)c);
}

static uint32_t fb_col = 0;
static uint32_t fb_row = 0;

void fb_init(void) {
    fb_col = 0;
    fb_row = 0;
    serial_init();
    fb_clear();
}

void fb_clear(void) {
    for (uint32_t i = 0; i < FB_WIDTH * FB_HEIGHT; i++) {
        FB_MEMORY[i] = (uint16_t)' ' | (uint16_t)(FB_WHITE_ON_BLACK << 8);
    }
    fb_col = 0;
    fb_row = 0;
}

void fb_putc(char c) {
    if (c == '\n') {
        fb_col = 0;
        fb_row++;
        if (fb_row >= FB_HEIGHT) {
            fb_row = 0;
        }
        return;
    }
    if (c == '\r') {
        fb_col = 0;
        return;
    }
    uint32_t pos = fb_row * FB_WIDTH + fb_col;
    FB_MEMORY[pos] = (uint16_t)c | (uint16_t)(FB_WHITE_ON_BLACK << 8);
    fb_col++;
    if (fb_col >= FB_WIDTH) {
        fb_col = 0;
        fb_row++;
        if (fb_row >= FB_HEIGHT) {
            fb_row = 0;
        }
    }
}

void fb_puts(const char *str) {
    while (*str) {
        fb_putc(*str);
        serial_putc(*str);
        str++;
    }
}
