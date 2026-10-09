/* video.h — ZEDEC XERO pqOS Video/Graphics Driver Subsystem
 *
 * A pure-software 2D rasteriser that draws into a CALLER-SUPPLIED framebuffer,
 * plus a thin ops struct for the two things software cannot do by itself:
 * waiting for a scanline retrace and handing a page to a scanout engine.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 * 36N9 Genetics, LLC
 *
 * ===========================================================================
 * WHAT IS REAL (no hardware required, fully implemented, pixel-exact)
 * ===========================================================================
 *   put_pixel, fill_rect, draw_line (integer Bresenham), draw_circle
 *   (integer midpoint), blit (copy / alpha / solid / 2x scale / 90deg rotate /
 *   horizontal flip), draw_text (8x16 bitmap font), rectangular clipping,
 *   five pixel formats with exact pack/unpack, multi-display bookkeeping,
 *   the double-buffer page swap, IRQ flag processing and coverage checking.
 *   None of that touches a register. All of it is tested against literal
 *   pixel values in test_video.c.
 *
 * ===========================================================================
 * LIMITATIONS — read before believing anything below
 * ===========================================================================
 *  1. THERE IS NO DISPLAY CONTROLLER IN THIS FILE. Nothing here can make
 *     light come out of a panel. A real driver must fill in video_ops_t and
 *     call video_bind_ops(). Until it does:
 *       - video_vsync_wait()  returns VIDEO_ENODEV. It never returns success,
 *                             and it never spins pretending to wait.
 *       - video_flip()        performs the in-memory page swap (that part is
 *                             real and observable) and then returns
 *                             VIDEO_ENOSCANOUT, because nothing was handed to
 *                             a scanout engine. It returns VIDEO_OK without a
 *                             backend ONLY in VIDEO_MODE_HEADLESS, where
 *                             "presented" genuinely means "now the front
 *                             buffer" and there is no panel by definition.
 *       - video_set_mode() / video_set_resolution() configure the SOFTWARE
 *                             surface description. VIDEO_OK from them means
 *                             "this geometry is valid and drawing will now use
 *                             it" — NOT "a monitor changed mode". The only
 *                             field that ever claims a physical scanout is
 *                             video_display_t.scanout_live, and no code path
 *                             sets it true without a bound backend saying so.
 *                             video_set_mode() also refuses any non-HEADLESS
 *                             mode when no display resolves (VIDEO_ENODISPLAY):
 *                             a bound backend must not make "there was nothing
 *                             to program" read as success.
 *     And once a display is INACTIVE — which is how a failed connector probe
 *     leaves it — video_flip(), video_vsync_wait() and video_read_pixel() all
 *     refuse it, so no acknowledgement from a stale backend and no IRQ latch
 *     can re-assert scanout_live on a panel that is not there.
 *  2. NO 3D. dev->supports_3d is initialised to false and there is no path
 *     that sets it true. There is no texture unit, shader or depth buffer.
 *  3. NO DMA ENGINE. dma_buffer/dma_size stay NULL/0; blits are CPU copies.
 *  4. NO PALETTE HARDWARE. Nothing in this file ever writes reg_palette or
 *     programs a LUT; PIXEL_RGB332 is a fixed 3:3:2 truncation, not an index
 *     into a palette, so there is no colour table to load.
 *  5. The font covers ASCII 32..126 only. Anything else (including UTF-8
 *     continuation bytes) renders as a hollow .notdef box. There is no
 *     kerning, no anti-aliasing, no bidi and no shaping.
 *  6. video_blit() uses a single module-static row buffer, so it is NOT
 *     re-entrant and must not be called from an IRQ handler that could
 *     interrupt another blit. Blits wider than VIDEO_BLIT_MAX_W or taller
 *     than VIDEO_BLIT_MAX_H are refused outright — BOTH extents are bounded,
 *     because the blit loops are O(w*h) and an unbounded h would let a single
 *     hostile call spin the kernel for hours.
 *  7. BLIT_SCALED and BLIT_ROTATED refuse overlapping source/destination
 *     rectangles (they would need a full-surface temporary). BLIT_COPY,
 *     BLIT_ALPHA and BLIT_FLIPPED handle overlap correctly.
 *  8. Rasterisation is bounded on purpose so a hostile coordinate cannot spin
 *     the kernel: lines longer than VIDEO_MAX_LINE_STEPS steps stop early,
 *     circles with r > VIDEO_MAX_RADIUS are refused, draw_text stops after
 *     VIDEO_TEXT_MAX_CHARS characters. Every real display is far below these.
 *     A line that stops at the step cap is TRUNCATED, not clipped correctly —
 *     the pixels past the cap are neither drawn nor counted.
 *  9. In VIDEO_MODE_TEXT_80x25 all 2D entry points are inert: the framebuffer
 *     is a character-cell array there, not pixels, so writing pixels into it
 *     would corrupt it and reading pixels out of it would be fiction.
 *     video_read_pixel() therefore returns false in that mode too.
 * 10. Coordinates are int32 and clipping is rectangular only. No scissor
 *     stack, no regions, no transforms.
 * 11. Geometry is capped twice: by the caller-visible dev->max_width /
 *     max_height, and by the hard VIDEO_HARD_MAX_DIM ceiling that no caller
 *     can raise. The hard ceiling is what keeps pitch*height inside 32 bits,
 *     so raising max_width cannot be turned into an integer overflow.
 * 12. dev->num_displays is treated as UNTRUSTED everywhere: every loop over
 *     the display array clamps it to VIDEO_MAX_DISPLAYS first.
 */
#ifndef VIDEO_H
#define VIDEO_H

#include <stdint.h>
#include <stdbool.h>
#include "m5_types.h"
#include "edp_risk.h"   /* m5_coords_t */

/* ===== Video modes ===== */
typedef enum {
    VIDEO_MODE_TEXT_80x25 = 0,
    VIDEO_MODE_VBE,
    VIDEO_MODE_FRAMEBUFFER,
    VIDEO_MODE_HEADLESS,
} video_mode_t;

/* ===== Pixel formats =====
 * Colours cross the API as canonical 0xAARRGGBB. Each format below states the
 * EXACT bytes stored, little-endian, because a compositor that guesses gets
 * blue text. Pack/unpack are exact inverses in the direction that matters:
 * unpack(pack(x)) loses only the bits the format cannot hold, and
 * pack(unpack(raw)) == raw for every raw value, so a blit never drifts. */
typedef enum {
    PIXEL_RGB332 = 0,  /* 1 byte  : RRRGGGBB                                   */
    PIXEL_RGB565,      /* 2 bytes : RRRRRGGG GGGBBBBB  (stored low byte first) */
    PIXEL_RGB888,      /* 3 bytes : B, G, R                                    */
    PIXEL_XRGB8888,    /* 4 bytes : B, G, R, 0x00                              */
    PIXEL_ARGB8888,    /* 4 bytes : B, G, R, A                                 */
} pixel_format_t;

/* ===== 2D acceleration ops =====
 * video_blit() has one (w,h) pair, so each op below states what the
 * DESTINATION extent actually is. Source pixels outside the surface are
 * skipped; destination writes obey the clip rectangle. */
typedef enum {
    BLIT_COPY = 0,     /* dst w x h  <- src w x h, overlap-safe               */
    BLIT_ALPHA,        /* as COPY, blended with ctx.alpha (and src alpha on
                        * PIXEL_ARGB8888), overlap-safe                       */
    BLIT_SOLID_FILL,   /* dst w x h  <- ctx.fg_color, source ignored          */
    BLIT_SCALED,       /* dst 2w x 2h <- src w x h, nearest neighbour,
                        * refuses overlapping rectangles                      */
    BLIT_ROTATED,      /* dst h x w  <- src w x h rotated 90 deg CLOCKWISE,
                        * refuses overlapping rectangles                      */
    BLIT_FLIPPED,      /* dst w x h  <- src w x h mirrored horizontally,
                        * overlap-safe                                        */
} blit_op_t;

/* ===== Return codes ===== */
#define VIDEO_OK            0
#define VIDEO_EINVAL       (-1)  /* bad argument / geometry / format          */
#define VIDEO_ENODISPLAY   (-2)  /* no display with that id                   */
#define VIDEO_ENOBUF       (-3)  /* no framebuffer (or no back buffer) bound  */
#define VIDEO_ENODEV       (-4)  /* the work needs hardware; none is bound    */
#define VIDEO_ENOSCANOUT   (-5)  /* memory work done; nothing drives a panel  */
#define VIDEO_EBACKEND     (-6)  /* the bound backend reported a failure      */
#define VIDEO_ENOSPC       (-7)  /* buffer too small for the geometry         */
/* There is deliberately no overlap error code: video_blit() returns void, so
 * it cannot report one. BLIT_SCALED / BLIT_ROTATED on overlapping rectangles
 * draw NOTHING — check the rectangles yourself before calling. */

/* ===== Display ===== */
typedef struct {
    uint32_t display_id;      /* 1-based; 0 is never a valid display id */
    bool active;
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
    pixel_format_t format;
    uint32_t pitch;           /* bytes per scanline */
    void *framebuffer;        /* FRONT page (what a scanout engine would read) */
    void *backbuffer;         /* BACK page — the drawing target when double
                               * buffered. NULL when single buffered. */
    uint32_t fb_size;         /* size of the whole caller-supplied allocation */
    bool double_buffered;
    bool vsync;               /* last vsync observed (set by IRQ or backend) */
    /* Physical info */
    uint32_t dpi;
    uint32_t refresh_hz;

    /* --- added --- */
    void *fb_base;            /* allocation base; framebuffer/backbuffer are
                               * pages inside it and swap on every flip, so the
                               * base must be remembered separately */
    uint32_t page_bytes;      /* pitch * height: the size of ONE page */
    bool scanout_live;        /* ONLY ever true because a bound backend said so.
                               * This is the single field that claims pixels are
                               * physically being scanned out. */
} video_display_t;

#define VIDEO_MAX_DISPLAYS  4
#define VIDEO_MAX_SPRITES   256

/* ===== 2D context ===== */
typedef struct {
    uint32_t display_id;      /* which display the 2D calls draw into */
    int32_t clip_x, clip_y, clip_w, clip_h;  /* as requested; intersected with
                                              * the surface at draw time */
    uint32_t fg_color;
    uint32_t bg_color;
    bool alpha_blending;      /* when true, EVERY primitive blends with
                               * ctx.alpha instead of storing the colour */
    uint8_t alpha;            /* 0 = keep destination, 255 = replace it */
} video_context_t;

/* ===== Hardware backend =====
 * Everything that must touch silicon goes through here, and nothing else in
 * this subsystem does. A real driver fills this in and calls
 * video_bind_ops(); every entry point that needs one of these and does not
 * have it returns VIDEO_ENODEV rather than pretending. All callbacks return 0
 * for success and non-zero for failure. */
typedef struct video_ops {
    /* Program a CRTC. Called by video_set_mode()/video_set_resolution(). */
    int (*set_mode)(void *ctx, uint32_t display_id, uint32_t w, uint32_t h,
                    uint32_t bpp, pixel_format_t fmt);
    /* Point the scanout engine at `front` (pitch bytes per line). */
    int (*flip)(void *ctx, uint32_t display_id, void *front, uint32_t pitch);
    /* Block until the next vertical retrace. */
    int (*vsync_wait)(void *ctx, uint32_t display_id);
    /* Re-read a connector's geometry after a hotplug IRQ. Return non-zero if
     * the connector is gone; the display is then marked inactive. */
    int (*probe)(void *ctx, uint32_t display_id, uint32_t *w, uint32_t *h);
    void *ctx;
} video_ops_t;

/* ===== Video device (hardware-as-code) ===== */
typedef struct {
    uint32_t device_id;
    char name[128];
    video_mode_t mode;

    /* Registers — recorded software shadows, not MMIO */
    uint32_t reg_mode;
    uint32_t reg_resolution;
    uint32_t reg_scanline;
    uint32_t reg_palette;

    /* DMA — see LIMITATIONS 3: never populated by this file */
    uint8_t *dma_buffer;
    uint32_t dma_size;

    /* IRQ latches — set by a driver's ISR, consumed by video_handle_irq() */
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
    bool supports_2d_accel;      /* true: the software rasteriser IS the engine */
    bool supports_3d;            /* always false here — see LIMITATIONS 2 */
    bool supports_multi_display;
    uint32_t max_width;
    uint32_t max_height;

    /* M5 coordinates */
    m5_coords_t m5;
    double coverage_r;
    double coverage_l;

    /* --- hardware backend --- */
    video_ops_t ops;
    bool ops_bound;

    /* --- accounting -----------------------------------------------------
     * Every counter below increments ONLY for work that actually happened.
     * In particular stat_scanout_flips counts flips a backend acknowledged,
     * which is a strict subset of stat_page_flips (pointer swaps). */
    uint64_t stat_pixels_written;   /* pixels stored into a surface */
    /* Pixel writes that were asked for and rejected. Exact for put_pixel,
     * fill_rect and draw_line (whose whole-primitive reject adds the exact
     * max(dx,dy)+1 it would have emitted). draw_circle and draw_text do NOT
     * itemise their whole-primitive trivial rejects — computing that count
     * would mean running the rasteriser anyway — so this counter is a lower
     * bound for those two, never an over-count for any of them. */
    uint64_t stat_pixels_clipped;
    uint64_t stat_page_flips;       /* front/back pointer swaps performed */
    uint64_t stat_scanout_flips;    /* flips a bound backend acknowledged */
    uint64_t stat_vsync_waits;      /* retraces a backend actually waited for */
    uint64_t stat_irq_vsync;
    uint64_t stat_irq_flip;
    uint64_t stat_irq_hotplug;
} video_device_t;

/* ===== Rasteriser bounds (see LIMITATIONS 6 and 8) ===== */
#define VIDEO_FONT_W            8
#define VIDEO_FONT_H            16
#define VIDEO_FONT_FIRST        32     /* the glyph table is ASCII-32 indexed */
#define VIDEO_FONT_LAST         126
#define VIDEO_FONT_GLYPHS       96     /* 95 printable + 1 .notdef */
#define VIDEO_BLIT_MAX_W        4096
#define VIDEO_BLIT_MAX_H        4096
#define VIDEO_MAX_LINE_STEPS    4194304
#define VIDEO_MAX_RADIUS        65536
#define VIDEO_TEXT_MAX_CHARS    4096
#define VIDEO_DEFAULT_MAX_W     4096
#define VIDEO_DEFAULT_MAX_H     4096
/* The ceiling no caller can raise (LIMITATIONS 11). dev->max_width and
 * dev->max_height are plain struct fields, so a caller can set them to
 * anything; every geometry entry point checks this constant as well, which is
 * what keeps pitch (<= dim*4) and pitch*height inside 32 bits. */
#define VIDEO_HARD_MAX_DIM      16384

/* Coverage floor: r and l below are both fractions in [0,1], so demanding
 * their product reach 1.0 demands BOTH be exactly 1 — every active display
 * fully consistent AND the primary/context displays real. It is reachable
 * (a correctly configured device passes) and it is refusable (a device with
 * no displays, an unbound framebuffer, or a bad pitch fails). */
#define VIDEO_COVERAGE_FLOOR    1.0

/* ===== API ===== */
void video_init(video_device_t *dev, const char *name);
/* Records the software mode (always) and programs a CRTC (only through a bound
 * backend). Any mode other than VIDEO_MODE_HEADLESS needs a display to apply
 * to, so it returns VIDEO_ENODISPLAY when the primary display does not resolve
 * — a bound backend does NOT turn "there was nothing to program" into OK. */
int video_set_mode(video_device_t *dev, video_mode_t mode);
int video_set_resolution(video_device_t *dev, uint32_t display_id,
                         uint32_t w, uint32_t h, uint32_t bpp, pixel_format_t fmt);
int video_set_framebuffer(video_device_t *dev, uint32_t display_id, void *fb, uint32_t size);
void *video_get_framebuffer(video_device_t *dev, uint32_t display_id);
/* Both of these refuse an INACTIVE display with VIDEO_ENODISPLAY: once a probe
 * has reported the connector gone, no page swap and no backend acknowledgement
 * may be used to re-assert scanout_live on it. */
int video_flip(video_device_t *dev, uint32_t display_id);
int video_vsync_wait(video_device_t *dev, uint32_t display_id);

/* 2D operations. All of these draw into the BACK page when the context's
 * display is double buffered, otherwise into the front page. */
void video_set_color(video_device_t *dev, uint32_t fg, uint32_t bg);
void video_set_clip(video_device_t *dev, int32_t x, int32_t y, int32_t w, int32_t h);
void video_put_pixel(video_device_t *dev, int32_t x, int32_t y, uint32_t color);
void video_fill_rect(video_device_t *dev, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color);
void video_draw_line(video_device_t *dev, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint32_t color);
void video_draw_circle(video_device_t *dev, int32_t cx, int32_t cy, int32_t r, uint32_t color);
void video_blit(video_device_t *dev, int32_t dst_x, int32_t dst_y,
                int32_t src_x, int32_t src_y, int32_t w, int32_t h, blit_op_t op);
/* Draws foreground pixels only — the glyph background is left untouched, so
 * text composites over whatever is already there. */
void video_draw_text(video_device_t *dev, int32_t x, int32_t y, const char *text, uint32_t color);

/* Display management */
uint32_t video_add_display(video_device_t *dev, uint32_t w, uint32_t h, uint32_t bpp);
int video_set_primary(video_device_t *dev, uint32_t display_id);
video_display_t *video_get_display(video_device_t *dev, uint32_t display_id);

/* IRQ: consumes the latched irq_* flags. Counters advance only for flags that
 * were actually set. irq_flip_done raises scanout_live only when a bound
 * backend actually has a flip callback and the display is still active — a
 * latch alone is not evidence that this file ever handed a page to a scanout
 * engine. */
void video_handle_irq(video_device_t *dev);

/* Coverage: see VIDEO_COVERAGE_FLOOR. Returns false for an empty device, for
 * any active display without a large enough framebuffer, and for a device
 * whose primary/context display id does not resolve. */
bool video_verify_coverage(video_device_t *dev);

/* ===== Hardware binding ===== */
/* Returns VIDEO_EINVAL for a NULL device or an ops struct with no callbacks
 * at all (that is not a backend, and binding it would let ENODEV silently
 * turn into "success"). */
int video_bind_ops(video_device_t *dev, const video_ops_t *ops);
void video_unbind_ops(video_device_t *dev);
bool video_has_backend(const video_device_t *dev);

/* Read a pixel back out of a display's DRAW target, converted to canonical
 * 0xAARRGGBB. Returns false (and leaves *out alone) when the coordinate is
 * outside the surface, no framebuffer is bound, the display is inactive, or
 * the device is in VIDEO_MODE_TEXT_80x25 — where there are no pixels to read
 * (LIMITATIONS 9), only character cells. Ignores the clip rectangle: clipping
 * restricts writes, not inspection. */
bool video_read_pixel(video_device_t *dev, uint32_t display_id,
                      int32_t x, int32_t y, uint32_t *out);

#endif /* VIDEO_H */
