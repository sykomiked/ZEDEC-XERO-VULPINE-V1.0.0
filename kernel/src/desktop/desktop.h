/* desktop.h — ZEDEC XERO pqOS Desktop Environment
 *
 * Dragon-themed window manager, taskbar, app launcher, system tray.
 * Features: Windows, drag/drop, resize, minimize/maximize, themes,
 * notifications, file manager, settings panel, clock (event-driven).
 *
 * The clock is event-driven: it only ticks when interfacing with
 * external systems, not continuously like traditional OS clocks.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * License: SEL-3.3 + CC BY 4.0 + OPL v1.1
 * 36N9 Genetics, LLC
 */
#ifndef DESKTOP_H
#define DESKTOP_H

#include <stdint.h>
#include <stdbool.h>
#include "m5_types.h"
#include "edp_risk.h"

#define DESKTOP_MAX_WINDOWS     64
#define DESKTOP_MAX_APPS        128
#define DESKTOP_MAX_NOTIFICATIONS 32
#define DESKTOP_TITLE_BAR_H     24
#define DESKTOP_TASKBAR_H       40
#define DESKTOP_MAX_ICONS       16

/* ===== Window states ===== */
typedef enum {
    WIN_NORMAL = 0,
    WIN_MINIMIZED,
    WIN_MAXIMIZED,
    WIN_FULLSCREEN,
    WIN_CLOSING,
} win_state_t;

/* ===== Window ===== */
typedef struct {
    uint32_t window_id;
    bool active;
    bool visible;
    win_state_t state;
    char title[128];
    int32_t x, y, w, h;
    int32_t prev_x, prev_y, prev_w, prev_h;
    void *framebuffer;
    uint32_t fb_size;
    uint32_t app_id;
    bool resizable;
    bool closable;
    bool minimizable;
    bool always_on_top;
    uint32_t z_order;
    /* Dragon theme */
    uint32_t title_color;
    uint32_t border_color;
    uint32_t bg_color;
} desktop_window_t;

/* ===== App ===== */
typedef struct {
    uint32_t app_id;
    char name[64];
    char icon[64];
    char executable[128];
    bool running;
    uint32_t window_id;
    bool pinned_to_taskbar;
} desktop_app_t;

/* ===== Notification ===== */
typedef struct {
    uint32_t notif_id;
    char title[128];
    char message[256];
    char app_name[64];
    uint64_t timestamp;
    uint32_t duration_ms;
    bool read;
    uint32_t priority;  /* 0=low, 1=normal, 2=high, 3=urgent */
} desktop_notification_t;

/* ===== System tray icon ===== */
typedef struct {
    uint32_t icon_id;
    char name[64];
    char tooltip[128];
    uint32_t app_id;
    bool active;
} tray_icon_t;

/* ===== Event-driven clock ===== */
typedef struct {
    uint64_t external_ticks;    /* Only updated on external interface */
    uint64_t last_sync_tick;    /* Last time we synced with external clock */
    bool needs_sync;            /* True when external interface requested */
    double drift_ppm;           /* Clock drift in parts per million */
    uint64_t local_counter;     /* Free-running local counter (not clock-dependent) */
    bool clock_active;          /* Only true when interfacing externally */
} event_clock_t;

/* ===== Dragon theme ===== */
typedef struct {
    uint32_t desktop_bg;        /* Deep black with red gradient */
    uint32_t taskbar_bg;        /* Dark crimson */
    uint32_t taskbar_fg;        /* Gold */
    uint32_t window_title_bg;   /* Dark red */
    uint32_t window_title_fg;   /* Gold */
    uint32_t window_border;     /* Dragon scale pattern color */
    uint32_t window_bg;         /* Near black */
    uint32_t accent;            /* Dragon fire orange */
    uint32_t text_color;        /* Warm white */
    uint32_t link_color;        /* Fire orange */
    uint32_t selection_color;   /* Dragon red */
    uint32_t notification_bg;   /* Dark crimson */
    uint32_t notification_fg;   /* Gold */
    char wallpaper[128];        /* Dragon wallpaper path */
    bool animations;            /* Dragon fire animations */
    bool sounds;                /* Dragon roar sounds */
} dragon_theme_t;

/* ===== Desktop environment ===== */
typedef struct {
    uint32_t device_id;
    char name[128];

    /* Display dimensions */
    uint32_t screen_w, screen_h;
    uint32_t bpp;

    /* Windows */
    desktop_window_t windows[DESKTOP_MAX_WINDOWS];
    uint32_t num_windows;
    uint32_t active_window;
    uint32_t z_counter;

    /* Apps */
    desktop_app_t apps[DESKTOP_MAX_APPS];
    uint32_t num_apps;

    /* Notifications */
    desktop_notification_t notifications[DESKTOP_MAX_NOTIFICATIONS];
    uint32_t num_notifications;

    /* System tray */
    tray_icon_t tray[DESKTOP_MAX_ICONS];
    uint32_t num_tray_icons;

    /* Theme */
    dragon_theme_t theme;

    /* Event-driven clock */
    event_clock_t clock;

    /* Taskbar state */
    bool taskbar_visible;
    bool start_menu_open;
    uint32_t taskbar_x, taskbar_y, taskbar_w, taskbar_h;

    /* M5 coordinates */
    m5_coords_t m5;
    double coverage_r;
    double coverage_l;
} desktop_t;

/* ===== API ===== */
void desktop_init(desktop_t *desk, uint32_t screen_w, uint32_t screen_h, uint32_t bpp);
void desktop_set_dragon_theme(desktop_t *desk);

/* Window management */
uint32_t desktop_create_window(desktop_t *desk, const char *title, int32_t x, int32_t y,
                               uint32_t w, uint32_t h, uint32_t app_id);
int desktop_close_window(desktop_t *desk, uint32_t win_id);
int desktop_minimize_window(desktop_t *desk, uint32_t win_id);
int desktop_maximize_window(desktop_t *desk, uint32_t win_id);
int desktop_focus_window(desktop_t *desk, uint32_t win_id);
int desktop_move_window(desktop_t *desk, uint32_t win_id, int32_t x, int32_t y);
int desktop_resize_window(desktop_t *desk, uint32_t win_id, uint32_t w, uint32_t h);
desktop_window_t *desktop_get_window(desktop_t *desk, uint32_t win_id);

/* App management */
uint32_t desktop_register_app(desktop_t *desk, const char *name, const char *icon,
                              const char *executable);
int desktop_launch_app(desktop_t *desk, uint32_t app_id);
int desktop_pin_to_taskbar(desktop_t *desk, uint32_t app_id);

/* Notifications */
uint32_t desktop_notify(desktop_t *desk, const char *title, const char *message,
                         const char *app_name, uint32_t priority);
int desktop_dismiss_notification(desktop_t *desk, uint32_t notif_id);

/* System tray */
uint32_t desktop_tray_add(desktop_t *desk, const char *name, const char *tooltip, uint32_t app_id);
int desktop_tray_remove(desktop_t *desk, uint32_t icon_id);

/* Event-driven clock */
void desktop_clock_sync(desktop_t *desk, uint64_t external_tick);
uint64_t desktop_clock_get(desktop_t *desk);
void desktop_clock_enable(desktop_t *desk);
void desktop_clock_disable(desktop_t *desk);
bool desktop_clock_needs_sync(desktop_t *desk);

/* Rendering */
void desktop_render(desktop_t *desk, void *framebuffer);
void desktop_render_taskbar(desktop_t *desk, void *fb);
void desktop_render_window(desktop_t *desk, desktop_window_t *win, void *fb);
void desktop_render_notifications(desktop_t *desk, void *fb);
void desktop_render_wallpaper(desktop_t *desk, void *fb);

/* Input handling */
int desktop_handle_mouse(desktop_t *desk, int32_t x, int32_t y, uint8_t buttons);
int desktop_handle_key(desktop_t *desk, uint8_t keycode, bool pressed);

/* Coverage */
bool desktop_verify_coverage(desktop_t *desk);

#endif /* DESKTOP_H */
