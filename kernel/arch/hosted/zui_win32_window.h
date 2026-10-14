/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_win32_window.h — the Win32 window and input shared by two present
 * layers.
 *
 * Direct3D 11 (zui_present_d3d11.c) and Vulkan on Windows
 * (zui_present_vulkan.c, VK_KHR_win32_surface) show pixels in the same kind
 * of window; creating it, tracking its DPI, and turning window messages into
 * zui events (keys, WM_CHAR text, IME composition, mouse, wheel, touch
 * through WM_POINTER, size, DPI, focus, close) live here once.
 *
 * Windows calls the window procedure whenever it likes (during creation,
 * inside a resize drag), not only while poll() runs, so events are kept in a
 * small ring here and handed to the caller's queue by poll().
 *
 * The process is made per-monitor DPI aware (v2) when the system allows it;
 * scale_q8 = window DPI * 256 / 96. Link with -luser32 -limm32 -lgdi32.
 */
#ifndef ZXV_ZUI_WIN32_WINDOW_H
#define ZXV_ZUI_WIN32_WINDOW_H

#ifndef _WIN32_WINNT
#    define _WIN32_WINNT 0x0A00 /* the WM_POINTER and DPI messages; resolved at run time */
#endif
#ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#    define NOMINMAX
#endif
#include <windows.h>
#include <stdbool.h>
#include <stdint.h>
#include "zui_present.h"

#define ZUI_W32_RING  512u /* events kept between polls */
#define ZUI_W32_TOUCH 10u  /* fingers tracked at once   */

typedef struct {
    HWND hwnd;
    HINSTANCE inst;
    zui_screen_t scr; /* the window's facts as of the last message */
    zui_event_t ring[ZUI_W32_RING];
    uint32_t head, tail;
    uint8_t buttons;
    WCHAR high; /* first half of a UTF-16 surrogate pair from WM_CHAR */
    bool closed;
    bool redraw;                     /* WM_PAINT: show the last frame again */
    UINT32 touch_ptr[ZUI_W32_TOUCH]; /* Windows pointer id per zui touch id */
    bool touch_used[ZUI_W32_TOUCH];
} zui_win32_window_t;

/* Create and show a window of about w_pt x h_pt points; fills scr. */
int zui_win32_open(zui_win32_window_t *ww, zui_screen_t *scr, const char *title, int32_t w_pt,
                   int32_t h_pt);
void zui_win32_close(zui_win32_window_t *ww);

/* Pump messages, waiting at most wait_ms for the first; move the events into
 * q and the current size and scale into scr. Returns the number of events,
 * or ZUI_PRESENT_E_LOST once the window is destroyed. */
int zui_win32_poll(zui_win32_window_t *ww, zui_screen_t *scr, zui_queue_t *q, uint32_t wait_ms);

/* True once after WM_PAINT: the backend should show its last frame again. */
bool zui_win32_take_redraw(zui_win32_window_t *ww);

uint32_t zui_win32_now_ms(void);

#endif /* ZXV_ZUI_WIN32_WINDOW_H */
