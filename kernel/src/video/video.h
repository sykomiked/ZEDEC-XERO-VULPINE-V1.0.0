/* video.h — ZEDEC XERO pqOS Video/Graphics Driver Subsystem
 *
 * Supports: VBE/VESA, framebuffer, 2D acceleration, multi-display
 * Features: Resolution switching, double buffering, 2D blit, alpha blend
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * License: SEL-3.3 + CC BY 4.0 + OPL v1.1
 * 36N9 Genetics, LLC
 */
#ifndef VIDEO_H
#define VIDEO_H

#include <stdint.h>
#include <stdbool.h>
#include "m5_types.h"

/* ===== Video modes ===== */
typedef enum {
    VIDEO_MODE_TEXT_80x25 = 0,
    VIDEO_MODE_VBE,
    VIDEO_MODE_FRAMEBUFFER,
    VIDEO_MODE_HEADLESS,
} video_mode_t;

/* ===== Pixel formats ===== */
typedef enum {
    PIXEL_RGB332 = 0,
    PIXEL_RGB565,
    PIXEL_RGB888,
    PIXEL_XRGB8888,
    PIXEL_ARGB8888,
} pixel_format_t;

/* ===== 2D acceleration ops ===== */
typedef enum {
    BLIT_COPY = 0,
    BLIT_ALPHA,
    BLIT_SOLID_FILL,
    BLIT_SCALED,
    BLIT_ROTATED,
    BLIT_FLIPPED,
} blit_op_t;

/* ===== Display ===== */
typedef struct {
    uint32_t display_id;
    bool active;
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
    pixel_format_t format;
    uint32_t pitch;
    void *framebuffer;
    void *backbuffer;
    uint32_t fb_size;
    bool double_buffered;
    bool vsync;
    /* Physical info */
    uint32_t dpi;
    uint32_t refresh_hz;
} video_display_t;

#define VIDEO_MAX_DISPLAYS  4
#define VIDEO_MAX_SPRITES   256

/* ===== 2D context ===== */
typedef struct {
    uint32_t display_id;
    int32_t clip_x, clip_y, clip_w, clip_h;
    uint32_t fg_color;
    uint32_t bg_color;
    bool alpha_blending;
    uint8_t alpha;
} video_context_t;

/* ===== Video device (hardware-as-code) ===== */
typedef struct {
    uint32_t device_id;
    char name[128];
    video_mode_t mode;

    /* Registers */
    uint32_t reg_mode;
    uint32_t reg_resolution;
    uint32_t reg_scanline;
    uint32_t reg_palette;

    /* DMA */
    uint8_t *dma_buffer;
    uint32_t dma_size;

    /* IRQ */
    bool irq_vsync;
    bool irq_flip_done;
    bool irq_display_change;

    /* Displays */
    video_display_t displays[VIDEO_MAX_DISPLAYS];
    uint32_t num_displays;
    uint32_t primary_display;

    /* 2D context */
    video_context_t ctx;

    /* Capabilities */
    bool supports_2d_accel;
    bool supports_3d;
    bool supports_multi_display;
    uint32_t max_width;
    uint32_t max_height;

    /* M5 coordinates */
    m5_coords_t m5;
    double coverage_r;
    double coverage_l;
} video_device_t;

/* ===== API ===== */
void video_init(video_device_t *dev, const char *name);
int video_set_mode(video_device_t *dev, video_mode_t mode);
int video_set_resolution(video_device_t *dev, uint32_t display_id,
                         uint32_t w, uint32_t h, uint32_t bpp, pixel_format_t fmt);
int video_set_framebuffer(video_device_t *dev, uint32_t display_id, void *fb, uint32_t size);
void *video_get_framebuffer(video_device_t *dev, uint32_t display_id);
int video_flip(video_device_t *dev, uint32_t display_id);
int video_vsync_wait(video_device_t *dev, uint32_t display_id);

/* 2D operations */
void video_set_color(video_device_t *dev, uint32_t fg, uint32_t bg);
void video_set_clip(video_device_t *dev, int32_t x, int32_t y, int32_t w, int32_t h);
void video_put_pixel(video_device_t *dev, int32_t x, int32_t y, uint32_t color);
void video_fill_rect(video_device_t *dev, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color);
void video_draw_line(video_device_t *dev, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint32_t color);
void video_draw_circle(video_device_t *dev, int32_t cx, int32_t cy, int32_t r, uint32_t color);
void video_blit(video_device_t *dev, int32_t dst_x, int32_t dst_y,
                int32_t src_x, int32_t src_y, int32_t w, int32_t h, blit_op_t op);
void video_draw_text(video_device_t *dev, int32_t x, int32_t y, const char *text, uint32_t color);

/* Display management */
uint32_t video_add_display(video_device_t *dev, uint32_t w, uint32_t h, uint32_t bpp);
int video_set_primary(video_device_t *dev, uint32_t display_id);
video_display_t *video_get_display(video_device_t *dev, uint32_t display_id);

/* IRQ */
void video_handle_irq(video_device_t *dev);

/* Coverage */
bool video_verify_coverage(video_device_t *dev);

#endif /* VIDEO_H */
