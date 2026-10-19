/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_x11_window.h — the X11 window and input shared by two present layers.
 *
 * Both Linux backends put zui pixels into the same kind of window: the X11
 * one copies them with MIT-SHM (zui_present_x11.c), the Vulkan one renders
 * into it through VK_KHR_xlib_surface (zui_present_vulkan.c). Opening the
 * window, reading the panel scale, and turning X events into zui events
 * (keys, text through an XIM input context, pointer, wheel, focus, resize,
 * close) live here once.
 *
 * The scale is the Xft.dpi resource / 96 (scale_q8 = 256 when it is absent).
 * Text input needs the process locale: the program should call
 * setlocale(LC_CTYPE, "") before opening a window so the input method can
 * commit UTF-8; without it, plain ASCII typing still works.
 */
#ifndef ZXV_ZUI_X11_WINDOW_H
#define ZXV_ZUI_X11_WINDOW_H

#include <stdbool.h>
#include <stdint.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include "zui_present.h"

#define ZUI_X11_PREEDIT_MAX 128 /* code points of composition kept */

typedef struct {
    Display *dpy;
    Window win;
    int screen;
    Visual *visual;
    int depth;
    Atom wm_protocols, wm_delete;
    XIM xim;
    XIC xic;
    XICCallback cb_start; /* pre-edit callbacks, kept as long as the IC */
    XIMCallback cb_done, cb_draw, cb_caret;
    /* Composition text while an input method is composing (callback
     * pre-edit style); code points, sent out as UTF-8 PREEDIT events. */
    uint32_t preedit[ZUI_X11_PREEDIT_MAX];
    uint32_t preedit_n;
    zui_queue_t *q; /* the caller's queue, only while poll() runs */
    int pushed;     /* events pushed during this poll()           */
    uint8_t buttons;
    uint8_t held[32]; /* keycodes held down, to mark auto-repeat  */
    bool closed;
    bool exposed; /* part of the window needs its pixels again  */
    int32_t ex0, ey0, ex1, ey1;
} zui_x11_window_t;

/* Open and map a window of about w_pt x h_pt points; fills scr (pixels,
 * scale, pointer input). Returns ZUI_PRESENT_OK or ZUI_PRESENT_E_UNAVAILABLE
 * (no display). */
int zui_x11_open(zui_x11_window_t *xw, zui_screen_t *scr, const char *title, int32_t w_pt,
                 int32_t h_pt);
void zui_x11_close(zui_x11_window_t *xw);

/* Move pending X events into q as zui events, waiting at most wait_ms for the
 * first one. Updates scr on a resize. Returns the number of events pushed, or
 * ZUI_PRESENT_E_LOST when the window or the connection is gone. */
int zui_x11_poll(zui_x11_window_t *xw, zui_screen_t *scr, zui_queue_t *q, uint32_t wait_ms);

/* The area Expose events asked for since the last call, if any. */
bool zui_x11_take_expose(zui_x11_window_t *xw, zui_rect_t *r);

uint32_t zui_x11_now_ms(void); /* CLOCK_MONOTONIC in ms */

#endif /* ZXV_ZUI_X11_WINDOW_H */
