/* framebuffer.h — Simple VGA text mode framebuffer for freestanding kernel */
#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

#include <stdint.h>

#define FB_WIDTH 80
#define FB_HEIGHT 25
#define FB_MEMORY ((volatile uint16_t*)0xB8000)
#define FB_WHITE_ON_BLACK 0x0F

void fb_init(void);
void fb_putc(char c);
void fb_puts(const char *str);
void fb_clear(void);

#endif
