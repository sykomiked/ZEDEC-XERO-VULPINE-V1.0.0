/* vbe.h — VBE/VGA graphics mode linear framebuffer driver
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#ifndef VBE_H
#define VBE_H

#include <stdint.h>

#define VBE_DEFAULT_WIDTH  1024
#define VBE_DEFAULT_HEIGHT 768
#define VBE_DEFAULT_BPP    32

typedef struct vbe_mode_info {
    uint16_t width;
    uint16_t height;
    uint8_t  bpp;
    uintptr_t framebuffer_addr;   /* full pointer width: real GOP FBs can be >4GiB */
    uint32_t pitch;
    uint8_t  memory_model;
    bool     valid;
} vbe_mode_info_t;

typedef struct vbe_state {
    vbe_mode_info_t mode;
    uint32_t *fb;
    uint32_t fb_size;
    uint16_t width;
    uint16_t height;
    uint8_t  bpp;
    uint32_t pitch;
} vbe_state_t;

void vbe_init(vbe_state_t *vbe, uint16_t width, uint16_t height, uint8_t bpp);
void vbe_init_fb(vbe_state_t *vbe, uint16_t width, uint16_t height, uint8_t bpp, uintptr_t fb_addr);
void vbe_set_pixel(vbe_state_t *vbe, int32_t x, int32_t y, uint32_t color);
uint32_t vbe_get_pixel(vbe_state_t *vbe, int32_t x, int32_t y);
void vbe_fill_rect(vbe_state_t *vbe, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color);
void vbe_draw_line(vbe_state_t *vbe, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t color);
void vbe_clear_screen(vbe_state_t *vbe, uint32_t color);
void vbe_draw_char(vbe_state_t *vbe, int32_t x, int32_t y, char c, uint32_t fg, uint32_t bg);
void vbe_draw_text_ex(vbe_state_t *vbe, int32_t x, int32_t y, const char *str,
                      uint32_t fg, uint32_t bg, int32_t scale, int draw_bg);
void vbe_draw_string(vbe_state_t *vbe, int32_t x, int32_t y, const char *str, uint32_t fg, uint32_t bg);

#define RGB(r,g,b) (((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))
#define COLOR_BLACK   RGB(0,0,0)
#define COLOR_WHITE   RGB(255,255,255)
#define COLOR_RED     RGB(255,0,0)
#define COLOR_GREEN   RGB(0,255,0)
#define COLOR_BLUE    RGB(0,0,255)
#define COLOR_YELLOW  RGB(255,255,0)
#define COLOR_CYAN    RGB(0,255,255)
#define COLOR_MAGENTA RGB(255,0,255)
#define COLOR_GRAY    RGB(128,128,128)
#define COLOR_DARK_GRAY RGB(40,40,40)
#define COLOR_LIGHT_GRAY RGB(200,200,200)

#endif
