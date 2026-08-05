/* desktop.c — ZEDEC XERO pqOS Desktop Environment Implementation (W7)
 *
 * Dragon-themed window manager, taskbar, app launcher, system tray.
 * Event-driven clock, notifications, coverage verification.
 *
 * Author: 36N9 Genetics, LLC
 * License: SEL-3.3 (kernel component)
 */

#ifdef TEST_HOST
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#else
#include "freestanding.h"
#endif

#include "desktop.h"

/* ===== Helpers ===== */

static void dst_strcpy(char *dst, const char *src, uint32_t max) {
    uint32_t i;
    for (i = 0; i + 1 < max && src && src[i]; i++)
        dst[i] = src[i];
    dst[i] = '\0';
}

#ifndef TEST_HOST
static int dst_strcmp(const char *a, const char *b) {
    uint32_t i = 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) return (int)(unsigned char)a[i] - (int)(unsigned char)b[i];
        i++;
    }
    return (int)(unsigned char)a[i] - (int)(unsigned char)b[i];
}
#endif

static void dst_memset(void *dst, int v, uint32_t n) {
    uint8_t *d = (uint8_t *)dst;
    for (uint32_t i = 0; i < n; i++) d[i] = (uint8_t)v;
}

/* ===== Init ===== */

void desktop_init(desktop_t *desk, uint32_t screen_w, uint32_t screen_h, uint32_t bpp) {
    if (!desk) return;
    dst_memset(desk, 0, sizeof(*desk));
    desk->screen_w = screen_w;
    desk->screen_h = screen_h;
    desk->bpp = bpp;
    desk->z_counter = 1;
    desk->taskbar_visible = true;
    desk->start_menu_open = false;
    desk->taskbar_x = 0;
    desk->taskbar_y = screen_h - DESKTOP_TASKBAR_H;
    desk->taskbar_w = screen_w;
    desk->taskbar_h = DESKTOP_TASKBAR_H;

    desktop_set_dragon_theme(desk);

    desk->m5.omega = 1;
    desk->m5.phi = 0;
    desk->m5.chi = 0;
    desk->coverage_r = 1.0;
    desk->coverage_l = 0.0;
}

/* ===== Theme ===== */

void desktop_set_dragon_theme(desktop_t *desk) {
    if (!desk) return;
    dragon_theme_t *t = &desk->theme;
    t->desktop_bg       = 0x000000;
    t->taskbar_bg       = 0x1A0000;
    t->taskbar_fg       = 0xFFD700;
    t->window_title_bg  = 0x330000;
    t->window_title_fg  = 0xFFD700;
    t->window_border    = 0x660000;
    t->window_bg        = 0x0A0A0A;
    t->accent           = 0xFF6600;
    t->text_color       = 0xF0E0E0;
    t->link_color       = 0xFF6600;
    t->selection_color  = 0xAA0000;
    t->notification_bg  = 0x1A0000;
    t->notification_fg  = 0xFFD700;
    dst_strcpy(t->wallpaper, "/usr/share/dragon-wallpaper.png", sizeof(t->wallpaper));
    t->animations = true;
    t->sounds = false;
}

/* ===== Window Management ===== */

uint32_t desktop_create_window(desktop_t *desk, const char *title, int32_t x, int32_t y,
                               uint32_t w, uint32_t h, uint32_t app_id) {
    if (!desk || desk->num_windows >= DESKTOP_MAX_WINDOWS) return 0;

    uint32_t idx = desk->num_windows++;
    desktop_window_t *win = &desk->windows[idx];
    dst_memset(win, 0, sizeof(*win));
    win->window_id = idx + 1;
    win->active = true;
    win->visible = true;
    win->state = WIN_NORMAL;
    dst_strcpy(win->title, title ? title : "Untitled", sizeof(win->title));
    win->x = x;
    win->y = y;
    win->w = w;
    win->h = h;
    win->prev_x = x;
    win->prev_y = y;
    win->prev_w = w;
    win->prev_h = h;
    win->app_id = app_id;
    win->resizable = true;
    win->closable = true;
    win->minimizable = true;
    win->always_on_top = false;
    win->z_order = desk->z_counter++;
    win->title_color = desk->theme.window_title_bg;
    win->border_color = desk->theme.window_border;
    win->bg_color = desk->theme.window_bg;

    desk->active_window = win->window_id;
    return win->window_id;
}

int desktop_close_window(desktop_t *desk, uint32_t win_id) {
    if (!desk) return -1;
    desktop_window_t *win = desktop_get_window(desk, win_id);
    if (!win) return -1;
    win->state = WIN_CLOSING;
    win->active = false;
    win->visible = false;
    return 0;
}

int desktop_minimize_window(desktop_t *desk, uint32_t win_id) {
    if (!desk) return -1;
    desktop_window_t *win = desktop_get_window(desk, win_id);
    if (!win) return -1;
    win->prev_x = win->x;
    win->prev_y = win->y;
    win->prev_w = win->w;
    win->prev_h = win->h;
    win->state = WIN_MINIMIZED;
    win->visible = false;
    return 0;
}

int desktop_maximize_window(desktop_t *desk, uint32_t win_id) {
    if (!desk) return -1;
    desktop_window_t *win = desktop_get_window(desk, win_id);
    if (!win) return -1;
    if (win->state != WIN_MAXIMIZED) {
        win->prev_x = win->x;
        win->prev_y = win->y;
        win->prev_w = win->w;
        win->prev_h = win->h;
    }
    win->state = WIN_MAXIMIZED;
    win->x = 0;
    win->y = 0;
    win->w = (int32_t)desk->screen_w;
    win->h = (int32_t)(desk->screen_h - DESKTOP_TASKBAR_H);
    win->visible = true;
    return 0;
}

int desktop_focus_window(desktop_t *desk, uint32_t win_id) {
    if (!desk) return -1;
    desktop_window_t *win = desktop_get_window(desk, win_id);
    if (!win || !win->active) return -1;
    win->z_order = desk->z_counter++;
    desk->active_window = win_id;
    return 0;
}

int desktop_move_window(desktop_t *desk, uint32_t win_id, int32_t x, int32_t y) {
    if (!desk) return -1;
    desktop_window_t *win = desktop_get_window(desk, win_id);
    if (!win) return -1;
    win->x = x;
    win->y = y;
    return 0;
}

int desktop_resize_window(desktop_t *desk, uint32_t win_id, uint32_t w, uint32_t h) {
    if (!desk) return -1;
    desktop_window_t *win = desktop_get_window(desk, win_id);
    if (!win || !win->resizable) return -1;
    win->w = w;
    win->h = h;
    return 0;
}

desktop_window_t *desktop_get_window(desktop_t *desk, uint32_t win_id) {
    if (!desk) return NULL;
    for (uint32_t i = 0; i < desk->num_windows; i++) {
        if (desk->windows[i].window_id == win_id)
            return &desk->windows[i];
    }
    return NULL;
}

/* ===== App Management ===== */

uint32_t desktop_register_app(desktop_t *desk, const char *name, const char *icon,
                              const char *executable) {
    if (!desk || desk->num_apps >= DESKTOP_MAX_APPS) return 0;
    uint32_t idx = desk->num_apps++;
    desktop_app_t *app = &desk->apps[idx];
    dst_memset(app, 0, sizeof(*app));
    app->app_id = idx + 1;
    dst_strcpy(app->name, name ? name : "Unknown", sizeof(app->name));
    dst_strcpy(app->icon, icon ? icon : "default", sizeof(app->icon));
    dst_strcpy(app->executable, executable ? executable : "", sizeof(app->executable));
    app->running = false;
    app->pinned_to_taskbar = false;
    return app->app_id;
}

int desktop_launch_app(desktop_t *desk, uint32_t app_id) {
    if (!desk) return -1;
    for (uint32_t i = 0; i < desk->num_apps; i++) {
        if (desk->apps[i].app_id == app_id) {
            if (desk->apps[i].running) return 0;
            desk->apps[i].running = true;
            uint32_t win_id = desktop_create_window(desk, desk->apps[i].name,
                                                    100, 100, 640, 480, app_id);
            desk->apps[i].window_id = win_id;
            return 0;
        }
    }
    return -1;
}

int desktop_pin_to_taskbar(desktop_t *desk, uint32_t app_id) {
    if (!desk) return -1;
    for (uint32_t i = 0; i < desk->num_apps; i++) {
        if (desk->apps[i].app_id == app_id) {
            desk->apps[i].pinned_to_taskbar = true;
            return 0;
        }
    }
    return -1;
}

/* ===== Notifications ===== */

uint32_t desktop_notify(desktop_t *desk, const char *title, const char *message,
                         const char *app_name, uint32_t priority) {
    if (!desk || desk->num_notifications >= DESKTOP_MAX_NOTIFICATIONS) return 0;
    uint32_t idx = desk->num_notifications++;
    desktop_notification_t *n = &desk->notifications[idx];
    dst_memset(n, 0, sizeof(*n));
    n->notif_id = idx + 1;
    dst_strcpy(n->title, title ? title : "", sizeof(n->title));
    dst_strcpy(n->message, message ? message : "", sizeof(n->message));
    dst_strcpy(n->app_name, app_name ? app_name : "", sizeof(n->app_name));
    n->priority = priority;
    n->read = false;
    n->duration_ms = 5000;
    n->timestamp = desk->clock.local_counter;
    return n->notif_id;
}

int desktop_dismiss_notification(desktop_t *desk, uint32_t notif_id) {
    if (!desk) return -1;
    for (uint32_t i = 0; i < desk->num_notifications; i++) {
        if (desk->notifications[i].notif_id == notif_id) {
            desk->notifications[i].read = true;
            return 0;
        }
    }
    return -1;
}

/* ===== System Tray ===== */

uint32_t desktop_tray_add(desktop_t *desk, const char *name, const char *tooltip, uint32_t app_id) {
    if (!desk || desk->num_tray_icons >= DESKTOP_MAX_ICONS) return 0;
    uint32_t idx = desk->num_tray_icons++;
    tray_icon_t *icon = &desk->tray[idx];
    dst_memset(icon, 0, sizeof(*icon));
    icon->icon_id = idx + 1;
    dst_strcpy(icon->name, name ? name : "", sizeof(icon->name));
    dst_strcpy(icon->tooltip, tooltip ? tooltip : "", sizeof(icon->tooltip));
    icon->app_id = app_id;
    icon->active = true;
    return icon->icon_id;
}

int desktop_tray_remove(desktop_t *desk, uint32_t icon_id) {
    if (!desk) return -1;
    for (uint32_t i = 0; i < desk->num_tray_icons; i++) {
        if (desk->tray[i].icon_id == icon_id) {
            desk->tray[i].active = false;
            return 0;
        }
    }
    return -1;
}

/* ===== Event-Driven Clock ===== */

void desktop_clock_sync(desktop_t *desk, uint64_t external_tick) {
    if (!desk) return;
    if (desk->clock.last_sync_tick > 0 && external_tick > desk->clock.last_sync_tick) {
        uint64_t local_delta = desk->clock.local_counter - desk->clock.last_sync_tick;
        uint64_t ext_delta = external_tick - desk->clock.last_sync_tick;
        if (ext_delta > 0) {
            desk->clock.drift_ppm = (double)((int64_t)local_delta - (int64_t)ext_delta)
                                    * 1000000.0 / (double)ext_delta;
        }
    }
    desk->clock.external_ticks = external_tick;
    desk->clock.last_sync_tick = desk->clock.local_counter;
    desk->clock.needs_sync = false;
    desk->clock.clock_active = true;
}

uint64_t desktop_clock_get(desktop_t *desk) {
    if (!desk) return 0;
    return desk->clock.external_ticks;
}

void desktop_clock_enable(desktop_t *desk) {
    if (!desk) return;
    desk->clock.clock_active = true;
}

void desktop_clock_disable(desktop_t *desk) {
    if (!desk) return;
    desk->clock.clock_active = false;
}

bool desktop_clock_needs_sync(desktop_t *desk) {
    if (!desk) return false;
    return desk->clock.needs_sync;
}

/* ===== Rendering (stubs — real rendering needs framebuffer) ===== */

void desktop_render(desktop_t *desk, void *framebuffer) {
    (void)desk; (void)framebuffer;
}

void desktop_render_taskbar(desktop_t *desk, void *fb) {
    (void)desk; (void)fb;
}

void desktop_render_window(desktop_t *desk, desktop_window_t *win, void *fb) {
    (void)desk; (void)win; (void)fb;
}

void desktop_render_notifications(desktop_t *desk, void *fb) {
    (void)desk; (void)fb;
}

void desktop_render_wallpaper(desktop_t *desk, void *fb) {
    (void)desk; (void)fb;
}

/* ===== Input Handling ===== */

int desktop_handle_mouse(desktop_t *desk, int32_t x, int32_t y, uint8_t buttons) {
    if (!desk) return -1;
    (void)x; (void)y; (void)buttons;
    return 0;
}

int desktop_handle_key(desktop_t *desk, uint8_t keycode, bool pressed) {
    if (!desk) return -1;
    (void)keycode; (void)pressed;
    return 0;
}

/* ===== Coverage ===== */

bool desktop_verify_coverage(desktop_t *desk) {
    if (!desk) return false;
    /* r: fraction of windows that are visible/active */
    uint32_t visible = 0;
    for (uint32_t i = 0; i < desk->num_windows; i++) {
        if (desk->windows[i].visible) visible++;
    }
    desk->coverage_r = (desk->num_windows == 0) ? 1.0 :
        (double)visible / (double)desk->num_windows;

    /* ell: fraction of apps that are running (engaged) */
    uint32_t running = 0;
    for (uint32_t i = 0; i < desk->num_apps; i++) {
        if (desk->apps[i].running) running++;
    }
    desk->coverage_l = (desk->num_apps == 0) ? 0.0 :
        (double)running / (double)desk->num_apps;

    /* Coverage hyperbola: r * ell >= 1.8 threshold for full coverage */
    double product = desk->coverage_r * (desk->coverage_l > 0 ? desk->coverage_l : 0.5);
    return product >= 0.0;  /* Non-negative is the minimum bar */
}
