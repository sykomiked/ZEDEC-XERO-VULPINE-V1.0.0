/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_present.h — putting a finished zui surface on a real screen.
 *
 * zui paints every pixel itself (zui_canvas.h). A present layer is the thin
 * per-platform part that (a) shows those pixels and (b) turns the platform's
 * keyboard, pointer and touch input into zui events (zui_input.h). Nothing
 * else is platform code: no system widgets, no browser.
 *
 *   Backend      Where                    File
 *   Metal        macOS (CAMetalLayer)     kernel/arch/hosted/zui_present_metal.m
 *   Vulkan       Linux, Windows           kernel/arch/hosted/zui_present_vulkan.c
 *   Direct3D 11  Windows                  kernel/arch/hosted/zui_present_d3d11.c
 *   X11 (shm)    Linux without a GPU      kernel/arch/hosted/zui_present_x11.c
 *   Framebuffer  bare metal (UEFI GOP,    kernel/src/zui/zui_present_fb.c
 *                ramfb) and headless
 *
 *   P1 DAMAGE ONLY. present() gets the rectangles that changed since the last
 *      frame and copies only those; a backend may widen them (a GPU upload of
 *      the bounding box) but never skips one.
 *   P2 SAME PIXELS. The surface is XRGB8888; a backend converts to its native
 *      order (BGRA on Metal and Direct3D, the GOP's RGB or BGR) without
 *      changing a value, so a screenshot from any backend matches the
 *      software render byte for byte.
 *   P3 THE APP OWNS MEMORY. The surface's pixels belong to the caller; on a
 *      ZUI_EV_RESIZE the caller re-inits its surface and repaints. Backends
 *      keep only their own GPU or window objects.
 *   P4 SCALE IS A FACT. Each backend reports the panel's pixels per point
 *      (Retina is 512 = 2x) in screen.scale_q8; zui_layout decides the rest.
 *
 * Integer only. The interface and the framebuffer backend are freestanding.
 */
#ifndef ZXV_ZUI_PRESENT_H
#define ZXV_ZUI_PRESENT_H

#include <stdbool.h>
#include <stdint.h>
#include "zui_canvas.h"
#include "zui_input.h"
#include "zui_layout.h"

#define ZUI_DAMAGE_MAX 16u /* rectangles kept per frame before merging */

/* The rectangles that changed this frame. Adding past ZUI_DAMAGE_MAX merges
 * the new one into the rectangle it grows least. */
typedef struct {
    zui_rect_t r[ZUI_DAMAGE_MAX];
    uint32_t n;
} zui_damage_t;

void zui_damage_clear(zui_damage_t *d);
void zui_damage_add(zui_damage_t *d, zui_rect_t r);
void zui_damage_all(zui_damage_t *d, int32_t w, int32_t h); /* the whole surface */
zui_rect_t zui_damage_bounds(const zui_damage_t *d);        /* w == 0 when empty */
int64_t zui_damage_area(const zui_damage_t *d);             /* sum of areas */

typedef struct zui_present zui_present_t;

typedef struct {
    const char *name;
    /* Open a window (or take the framebuffer) about w_pt x h_pt points. */
    int (*open)(zui_present_t *p, const char *title, int32_t w_pt, int32_t h_pt);
    void (*close)(zui_present_t *p);
    /* P1, P2: copy the damaged parts of s to the screen. */
    int (*present)(zui_present_t *p, const zui_surface_t *s, const zui_damage_t *d);
    /* Move pending platform input into q; wait at most wait_ms for some
     * (0 = do not block). Returns the number of events pushed, or < 0 when
     * the window is gone. */
    int (*poll)(zui_present_t *p, zui_queue_t *q, uint32_t wait_ms);
    uint32_t (*now_ms)(zui_present_t *p); /* monotonic milliseconds */
} zui_present_ops_t;

enum {
    ZUI_PRESENT_OK = 0,
    ZUI_PRESENT_E_UNAVAILABLE = -1, /* no display, no GPU, no driver      */
    ZUI_PRESENT_E_ARG = -2,
    ZUI_PRESENT_E_LOST = -3 /* the window or device went away         */
};

struct zui_present {
    const zui_present_ops_t *ops;
    void *impl;          /* the backend's own state                    */
    zui_screen_t screen; /* P4: pixels, scale and input kind right now */
    uint64_t frames;     /* present() calls that copied something      */
    uint64_t px_copied;  /* pixels copied, all frames                  */
};

/* ---- the framebuffer backend (bare metal and headless) ------------------- */

typedef enum {
    ZUI_FB_XRGB = 0, /* 0x00RRGGBB in memory order B,G,R,X (GOP BGR) */
    ZUI_FB_XBGR      /* 0x00BBGGRR: memory order R,G,B,X (GOP RGB)   */
} zui_fb_format_t;

typedef struct {
    volatile uint32_t *base; /* the linear framebuffer                      */
    uint32_t pitch_px;       /* pixels per scan line (GOP PixelsPerScanLine) */
    uint32_t w, h;
    zui_fb_format_t format;
    uint32_t scale_q8; /* 256 unless the firmware says otherwise          */
    /* Input on bare metal comes from the keyboard and mouse drivers, which
     * push into this queue themselves (zui_key_from_set1 below); poll()
     * reports what they pushed since the last call. */
    zui_queue_t *drv_queue;
    uint32_t (*clock_ms)(void); /* the kernel's tick, or NULL (time stands still) */
    uint32_t fake_ms;
} zui_fb_t;

extern const zui_present_ops_t zui_present_fb_ops;

/* Bind p to a framebuffer described by fb (fb must outlive p). */
void zui_present_fb_bind(zui_present_t *p, zui_fb_t *fb);

/* PC keyboard scan code set 1 (what the PS/2 controller and most firmware
 * deliver) to a zui key. *e0 carries the 0xE0 prefix between calls. Returns
 * true when the byte completed a key, with *key, *down, and *mods updated
 * (shift/ctrl/alt/meta tracked across calls; ZUI_MOD_CMD mirrors ctrl). */
bool zui_key_from_set1(uint8_t byte, bool *e0, uint16_t *key, bool *down, uint8_t *mods);

#endif /* ZXV_ZUI_PRESENT_H */
