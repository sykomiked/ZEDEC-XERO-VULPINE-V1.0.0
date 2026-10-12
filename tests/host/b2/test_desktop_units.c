/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_desktop_units.c: unit tests for kernel/src/desktop/desktop.c.
 *
 * Covers everything tests/host/test_desktop.c covers (init, theme, window
 * create/close/minimize/maximize/focus/move/resize, apps, notifications,
 * tray, event clock and coverage) and adds the exact field values, NULL and
 * unknown-id guards, full tables (64 windows, 128 apps, 32 notifications,
 * 16 tray icons), string truncation, the drift overflow clamp, the coverage
 * floor boundaries, and the module's declared bring-up hook. */
#define _GNU_SOURCE
#include <link.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "desktop.h"
#include "zxv_decl.h"

static int checks, failures;
#define CHECK(c)                                                              \
    do {                                                                      \
        checks++;                                                             \
        if (!(c)) {                                                           \
            failures++;                                                       \
            printf("FAIL line %d: %s\n", __LINE__, #c);                       \
        }                                                                     \
    } while (0)

/* ---- finding the file's static ZXV_DECLARE record -------------------------
 * The bring-up hook is static, so the only way to reach it is through the
 * declaration record the linker keeps. Scan the program's own loaded segments
 * for a zxv_decl_t {mod, bringup} whose mod->name is the module name. */
struct seg { uintptr_t lo, hi; int x, w; };
static struct seg segs[32];
static int nsegs;

static int phdr_cb(struct dl_phdr_info *info, size_t sz, void *data) {
    (void)sz;
    (void)data;
    for (int i = 0; i < info->dlpi_phnum && nsegs < 32; i++) {
        const ElfW(Phdr) *p = &info->dlpi_phdr[i];
        if (p->p_type != PT_LOAD || !(p->p_flags & PF_R)) continue;
        segs[nsegs].lo = info->dlpi_addr + p->p_vaddr;
        segs[nsegs].hi = segs[nsegs].lo + p->p_memsz;
        segs[nsegs].x = (p->p_flags & PF_X) != 0;
        segs[nsegs].w = (p->p_flags & PF_W) != 0;
        nsegs++;
    }
    return 1; /* the first entry is the main program: stop there */
}

static int in_seg(uintptr_t p, size_t n, int need_x) {
    for (int i = 0; i < nsegs; i++)
        if (p >= segs[i].lo && p + n <= segs[i].hi && (!need_x || segs[i].x)) return 1;
    return 0;
}

__attribute__((no_sanitize_address)) static zxv_bringup_fn find_bringup(const char *name) {
    size_t len = 0;
    while (name[len]) len++;
    len++;
    nsegs = 0;
    dl_iterate_phdr(phdr_cb, NULL);
    for (int s = 0; s < nsegs; s++) {
        for (uintptr_t a = (segs[s].lo + 7u) & ~(uintptr_t)7u; a + 16u <= segs[s].hi; a += 8u) {
            const uintptr_t *w = (const uintptr_t *)a;
            uintptr_t m = w[0], f = w[1];
            if (!f || !in_seg(f, 1, 1) || !in_seg(m, len, 0)) continue;
            const char *mn = (const char *)m;
            size_t i = 0;
            while (i < len && mn[i] == name[i]) i++;
            if (i == len) return (zxv_bringup_fn)f;
        }
    }
    return NULL;
}

/* The bring-up builds its desktop in a function-static desktop_t. Every
 * desktop this test makes is on the heap, so the only desktop_t in the
 * program's writable segments carrying the dragon wallpaper path is that one.
 * Call after find_bringup() (which fills segs). */
__attribute__((no_sanitize_address)) static desktop_t *find_static_desk(void) {
    static const char wp[] = "/usr/share/dragon-wallpaper.png";
    size_t off = offsetof(desktop_t, theme.wallpaper);
    for (int s = 0; s < nsegs; s++) {
        if (!segs[s].w) continue;
        for (uintptr_t b = (segs[s].lo + 7u) & ~(uintptr_t)7u; b + sizeof(desktop_t) <= segs[s].hi;
             b += 8u) {
            const char *c = (const char *)(b + off);
            size_t i = 0;
            while (i < sizeof(wp) && c[i] == wp[i]) i++;
            if (i == sizeof(wp)) return (desktop_t *)b;
        }
    }
    return NULL;
}

static desktop_t *new_desk(uint32_t w, uint32_t h, uint32_t bpp) {
    desktop_t *d = malloc(sizeof(*d)); /* exact size: ASan sees one-past writes */
    memset(d, 0xA5, sizeof(*d));
    desktop_init(d, w, h, bpp);
    return d;
}

static void test_init_theme(void) {
    desktop_init(NULL, 1, 1, 1);
    desktop_set_dragon_theme(NULL);
    desktop_t *d = new_desk(1920, 1080, 32);
    CHECK(d->screen_w == 1920);
    CHECK(d->screen_h == 1080);
    CHECK(d->bpp == 32);
    CHECK(d->num_windows == 0);
    CHECK(d->num_apps == 0);
    CHECK(d->num_notifications == 0);
    CHECK(d->num_tray_icons == 0);
    CHECK(d->active_window == 0);
    CHECK(d->device_id == 0);
    CHECK(d->z_counter == 1);
    CHECK(d->taskbar_visible);
    CHECK(!d->start_menu_open);
    CHECK(d->taskbar_x == 0);
    CHECK(d->taskbar_y == 1040);
    CHECK(d->taskbar_w == 1920);
    CHECK(d->taskbar_h == 40);
    CHECK(d->m5.omega == 1);
    CHECK(d->m5.phi == 0);
    CHECK(d->m5.chi == 0);
    CHECK(d->coverage_l == 0);
    CHECK(!d->clock.clock_active);
    CHECK(d->clock.external_ticks == 0);
    CHECK(d->clock.last_sync_tick == 0);
    CHECK(d->clock.local_counter == 0);
    CHECK(d->clock.drift_ppm == 0);
    CHECK(!d->clock.needs_sync);
    CHECK(!desktop_clock_needs_sync(d));
    CHECK(desktop_clock_get(d) == 0);

    dragon_theme_t *t = &d->theme;
    CHECK(t->desktop_bg == 0x000000);
    CHECK(t->taskbar_bg == 0x1A0000);
    CHECK(t->taskbar_fg == 0xFFD700);
    CHECK(t->window_title_bg == 0x330000);
    CHECK(t->window_title_fg == 0xFFD700);
    CHECK(t->window_border == 0x660000);
    CHECK(t->window_bg == 0x0A0A0A);
    CHECK(t->accent == 0xFF6600);
    CHECK(t->text_color == 0xF0E0E0);
    CHECK(t->link_color == 0xFF6600);
    CHECK(t->selection_color == 0xAA0000);
    CHECK(t->notification_bg == 0x1A0000);
    CHECK(t->notification_fg == 0xFFD700);
    CHECK(strcmp(t->wallpaper, "/usr/share/dragon-wallpaper.png") == 0);
    CHECK(t->animations);
    CHECK(!t->sounds);

    /* the theme setter repaints a changed theme */
    memset(t, 0x5A, sizeof(*t));
    desktop_set_dragon_theme(d);
    CHECK(t->accent == 0xFF6600);
    CHECK(t->desktop_bg == 0);
    CHECK(strcmp(t->wallpaper, "/usr/share/dragon-wallpaper.png") == 0);
    CHECK(t->animations && !t->sounds);
    free(d);

    /* small screen: taskbar sits at h - 40 */
    d = new_desk(320, 240, 8);
    CHECK(d->taskbar_y == 200 && d->taskbar_w == 320 && d->bpp == 8);
    free(d);
}

static void test_create_window(void) {
    CHECK(desktop_create_window(NULL, "x", 0, 0, 1, 1, 0) == 0);
    desktop_t *d = new_desk(1920, 1080, 32);
    uint32_t id = desktop_create_window(d, "Test Window", 100, -7, 640, 480, 9);
    CHECK(id == 1);
    CHECK(d->num_windows == 1);
    CHECK(d->active_window == 1);
    CHECK(d->z_counter == 2);
    desktop_window_t *w = desktop_get_window(d, id);
    CHECK(w == &d->windows[0]);
    CHECK(w->window_id == 1);
    CHECK(strcmp(w->title, "Test Window") == 0);
    CHECK(w->x == 100 && w->y == -7 && w->w == 640 && w->h == 480);
    CHECK(w->prev_x == 100 && w->prev_y == -7 && w->prev_w == 640 && w->prev_h == 480);
    CHECK(w->active && w->visible);
    CHECK(w->state == WIN_NORMAL);
    CHECK(w->app_id == 9);
    CHECK(w->resizable && w->closable && w->minimizable && !w->always_on_top);
    CHECK(w->z_order == 1);
    CHECK(w->framebuffer == NULL && w->fb_size == 0);
    CHECK(w->title_color == 0x330000);
    CHECK(w->border_color == 0x660000);
    CHECK(w->bg_color == 0x0A0A0A);

    /* NULL title, and the window copies the theme current at creation */
    d->theme.window_title_bg = 0x111111;
    d->theme.window_border = 0x222222;
    d->theme.window_bg = 0x333333;
    uint32_t id2 = desktop_create_window(d, NULL, 0, 0, 1, 1, 0);
    CHECK(id2 == 2);
    w = desktop_get_window(d, id2);
    CHECK(strcmp(w->title, "Untitled") == 0);
    CHECK(w->z_order == 2);
    CHECK(w->title_color == 0x111111 && w->border_color == 0x222222 && w->bg_color == 0x333333);
    CHECK(d->active_window == 2);

    /* title truncation at 127 characters */
    char longt[300];
    memset(longt, 'T', sizeof(longt) - 1);
    longt[sizeof(longt) - 1] = 0;
    uint32_t id3 = desktop_create_window(d, longt, 0, 0, 1, 1, 0);
    w = desktop_get_window(d, id3);
    CHECK(strlen(w->title) == 127);
    CHECK(w->title[126] == 'T');
    char exact[128];
    memset(exact, 'E', 127);
    exact[127] = 0;
    w = desktop_get_window(d, desktop_create_window(d, exact, 0, 0, 1, 1, 0));
    CHECK(strcmp(w->title, exact) == 0);
    w = desktop_get_window(d, desktop_create_window(d, "", 0, 0, 1, 1, 0));
    CHECK(w->title[0] == 0);
    free(d);

    /* full table: 64 windows, the 65th is refused */
    d = new_desk(800, 600, 32);
    for (uint32_t i = 1; i <= DESKTOP_MAX_WINDOWS; i++)
        CHECK(desktop_create_window(d, "w", (int32_t)i, 0, 1, 1, i) == i);
    CHECK(d->num_windows == 64);
    CHECK(d->z_counter == 65);
    CHECK(desktop_create_window(d, "over", 0, 0, 1, 1, 0) == 0);
    CHECK(d->num_windows == 64);
    CHECK(d->active_window == 64);
    CHECK(d->z_counter == 65);
    w = desktop_get_window(d, 64);
    CHECK(w == &d->windows[63] && w->x == 64 && w->app_id == 64 && w->z_order == 64);
    CHECK(desktop_get_window(d, 1) == &d->windows[0]);
    CHECK(desktop_get_window(d, 65) == NULL);
    CHECK(desktop_get_window(d, 0) == NULL);
    CHECK(desktop_get_window(NULL, 1) == NULL);
    free(d);
}

static void test_window_ops(void) {
    desktop_t *d = new_desk(1920, 1080, 32);
    uint32_t a = desktop_create_window(d, "A", 50, 60, 200, 210, 0);
    uint32_t b = desktop_create_window(d, "B", 5, 6, 7, 8, 0);
    desktop_window_t *wa = desktop_get_window(d, a);
    desktop_window_t *wb = desktop_get_window(d, b);

    /* move */
    CHECK(desktop_move_window(NULL, a, 1, 1) == -1);
    CHECK(desktop_move_window(d, 99, 1, 1) == -1);
    CHECK(desktop_move_window(d, a, 300, -400) == 0);
    CHECK(wa->x == 300 && wa->y == -400);
    CHECK(wa->prev_x == 50 && wa->prev_y == 60);
    CHECK(wb->x == 5 && wb->y == 6);

    /* resize */
    CHECK(desktop_resize_window(NULL, a, 1, 1) == -1);
    CHECK(desktop_resize_window(d, 99, 1, 1) == -1);
    CHECK(desktop_resize_window(d, a, 800, 600) == 0);
    CHECK(wa->w == 800 && wa->h == 600);
    CHECK(wa->prev_w == 200 && wa->prev_h == 210);
    wb->resizable = false;
    CHECK(desktop_resize_window(d, b, 100, 100) == -1);
    CHECK(wb->w == 7 && wb->h == 8);

    /* minimize saves the CURRENT geometry */
    CHECK(desktop_minimize_window(NULL, a) == -1);
    CHECK(desktop_minimize_window(d, 99) == -1);
    CHECK(desktop_minimize_window(d, a) == 0);
    CHECK(wa->state == WIN_MINIMIZED);
    CHECK(!wa->visible);
    CHECK(wa->active);
    CHECK(wa->prev_x == 300 && wa->prev_y == -400 && wa->prev_w == 800 && wa->prev_h == 600);
    CHECK(wa->x == 300 && wa->w == 800);
    CHECK(wb->visible && wb->state == WIN_NORMAL);

    /* maximize from minimized: saves geometry, fills the screen, shows it */
    wa->x = 11;
    wa->y = 12;
    wa->w = 13;
    wa->h = 14;
    CHECK(desktop_maximize_window(NULL, a) == -1);
    CHECK(desktop_maximize_window(d, 99) == -1);
    CHECK(desktop_maximize_window(d, a) == 0);
    CHECK(wa->state == WIN_MAXIMIZED);
    CHECK(wa->x == 0 && wa->y == 0 && wa->w == 1920 && wa->h == 1040);
    CHECK(wa->visible);
    CHECK(wa->prev_x == 11 && wa->prev_y == 12 && wa->prev_w == 13 && wa->prev_h == 14);
    /* maximize again: the saved geometry is NOT overwritten with the max one */
    CHECK(desktop_maximize_window(d, a) == 0);
    CHECK(wa->prev_x == 11 && wa->prev_y == 12 && wa->prev_w == 13 && wa->prev_h == 14);
    CHECK(wa->w == 1920 && wa->h == 1040);

    /* focus */
    CHECK(d->active_window == b);
    CHECK(d->z_counter == 3);
    CHECK(desktop_focus_window(NULL, a) == -1);
    CHECK(desktop_focus_window(d, 99) == -1);
    CHECK(desktop_focus_window(d, a) == 0);
    CHECK(d->active_window == a);
    CHECK(wa->z_order == 3);
    CHECK(d->z_counter == 4);
    CHECK(wa->z_order > wb->z_order);
    CHECK(desktop_focus_window(d, b) == 0);
    CHECK(wb->z_order == 4 && d->z_counter == 5 && d->active_window == b);

    /* close: the slot stays (findable) but is dead and hidden */
    CHECK(desktop_close_window(NULL, a) == -1);
    CHECK(desktop_close_window(d, 99) == -1);
    CHECK(desktop_close_window(d, a) == 0);
    CHECK(desktop_get_window(d, a) == wa);
    CHECK(wa->state == WIN_CLOSING);
    CHECK(!wa->active && !wa->visible);
    CHECK(d->num_windows == 2);
    CHECK(wb->active && wb->visible);
    /* a closed window cannot be focused and does not move the counter */
    CHECK(desktop_focus_window(d, a) == -1);
    CHECK(d->active_window == b);
    CHECK(d->z_counter == 5);
    CHECK(wa->z_order == 3);
    free(d);
}

static void test_apps(void) {
    CHECK(desktop_register_app(NULL, "a", "b", "c") == 0);
    CHECK(desktop_launch_app(NULL, 1) == -1);
    CHECK(desktop_pin_to_taskbar(NULL, 1) == -1);
    desktop_t *d = new_desk(1920, 1080, 32);
    uint32_t t = desktop_register_app(d, "Terminal", "term.png", "/bin/pterm");
    uint32_t e = desktop_register_app(d, NULL, NULL, NULL);
    CHECK(t == 1 && e == 2);
    CHECK(d->num_apps == 2);
    CHECK(d->apps[0].app_id == 1);
    CHECK(strcmp(d->apps[0].name, "Terminal") == 0);
    CHECK(strcmp(d->apps[0].icon, "term.png") == 0);
    CHECK(strcmp(d->apps[0].executable, "/bin/pterm") == 0);
    CHECK(!d->apps[0].running && !d->apps[0].pinned_to_taskbar && d->apps[0].window_id == 0);
    CHECK(d->apps[1].app_id == 2);
    CHECK(strcmp(d->apps[1].name, "Unknown") == 0);
    CHECK(strcmp(d->apps[1].icon, "default") == 0);
    CHECK(d->apps[1].executable[0] == 0);
    CHECK(!d->apps[1].running && !d->apps[1].pinned_to_taskbar && d->apps[1].window_id == 0);

    /* truncation: name/icon 63, executable 127 */
    char big[300];
    memset(big, 'n', sizeof(big) - 1);
    big[sizeof(big) - 1] = 0;
    uint32_t bg = desktop_register_app(d, big, big, big);
    CHECK(bg == 3);
    CHECK(strlen(d->apps[2].name) == 63);
    CHECK(strlen(d->apps[2].icon) == 63);
    CHECK(strlen(d->apps[2].executable) == 127);

    /* pin */
    CHECK(desktop_pin_to_taskbar(d, 99) == -1);
    CHECK(desktop_pin_to_taskbar(d, e) == 0);
    CHECK(d->apps[1].pinned_to_taskbar);
    CHECK(!d->apps[0].pinned_to_taskbar);
    CHECK(desktop_pin_to_taskbar(d, t) == 0);
    CHECK(d->apps[0].pinned_to_taskbar);

    /* launch: unknown id, then the second app (search goes past index 0) */
    CHECK(desktop_launch_app(d, 99) == -1);
    CHECK(desktop_launch_app(d, 0) == -1);
    CHECK(d->num_windows == 0);
    CHECK(desktop_create_window(d, "pre", 0, 0, 1, 1, 0) == 1);
    CHECK(desktop_launch_app(d, e) == 0);
    CHECK(d->apps[1].running);
    CHECK(!d->apps[0].running);
    CHECK(d->apps[1].window_id == 2);
    CHECK(d->num_windows == 2);
    desktop_window_t *w = desktop_get_window(d, 2);
    CHECK(strcmp(w->title, "Unknown") == 0);
    CHECK(w->x == 100 && w->y == 100 && w->w == 640 && w->h == 480);
    CHECK(w->app_id == e);
    CHECK(d->active_window == 2);
    /* second launch is a no-op success */
    CHECK(desktop_launch_app(d, e) == 0);
    CHECK(d->num_windows == 2);
    CHECK(d->apps[1].window_id == 2);
    CHECK(desktop_launch_app(d, t) == 0);
    CHECK(d->apps[0].window_id == 3);
    CHECK(strcmp(desktop_get_window(d, 3)->title, "Terminal") == 0);
    free(d);

    /* full app table; launch with a full window table still marks running */
    d = new_desk(1920, 1080, 32);
    for (uint32_t i = 1; i <= DESKTOP_MAX_APPS; i++)
        CHECK(desktop_register_app(d, "a", "i", "x") == i);
    CHECK(desktop_register_app(d, "over", "i", "x") == 0);
    CHECK(d->num_apps == 128);
    CHECK(d->apps[127].app_id == 128);
    CHECK(desktop_pin_to_taskbar(d, 128) == 0 && d->apps[127].pinned_to_taskbar);
    CHECK(desktop_pin_to_taskbar(d, 129) == -1);
    for (uint32_t i = 0; i < DESKTOP_MAX_WINDOWS; i++) desktop_create_window(d, "f", 0, 0, 1, 1, 0);
    CHECK(desktop_launch_app(d, 128) == 0);
    CHECK(d->apps[127].running);
    CHECK(d->apps[127].window_id == 0);
    CHECK(d->num_windows == 64);
    free(d);
}

static void test_notifications_tray(void) {
    CHECK(desktop_notify(NULL, "a", "b", "c", 1) == 0);
    CHECK(desktop_dismiss_notification(NULL, 1) == -1);
    CHECK(desktop_tray_add(NULL, "a", "b", 1) == 0);
    CHECK(desktop_tray_remove(NULL, 1) == -1);
    desktop_t *d = new_desk(1920, 1080, 32);
    d->clock.local_counter = 777;
    uint32_t n1 = desktop_notify(d, "Alert", "System update available", "System", 2);
    d->clock.local_counter = 778;
    uint32_t n2 = desktop_notify(d, NULL, NULL, NULL, 3);
    CHECK(n1 == 1 && n2 == 2);
    CHECK(d->num_notifications == 2);
    desktop_notification_t *a = &d->notifications[0], *b = &d->notifications[1];
    CHECK(a->notif_id == 1);
    CHECK(strcmp(a->title, "Alert") == 0);
    CHECK(strcmp(a->message, "System update available") == 0);
    CHECK(strcmp(a->app_name, "System") == 0);
    CHECK(a->priority == 2);
    CHECK(!a->read);
    CHECK(a->duration_ms == 5000);
    CHECK(a->timestamp == 777);
    CHECK(b->notif_id == 2 && b->timestamp == 778 && b->priority == 3);
    CHECK(b->title[0] == 0 && b->message[0] == 0 && b->app_name[0] == 0);
    /* the slot is zeroed before the strings are copied in */
    CHECK(a->title[5] == 0 && a->title[6] == 0 && a->title[127] == 0);
    CHECK(a->message[24] == 0 && a->message[255] == 0);
    CHECK(a->app_name[7] == 0 && a->app_name[63] == 0);
    CHECK(b->title[1] == 0 && b->message[1] == 0 && b->app_name[1] == 0);
    char big[400];
    memset(big, 'm', sizeof(big) - 1);
    big[sizeof(big) - 1] = 0;
    desktop_notify(d, big, big, big, 0);
    CHECK(strlen(d->notifications[2].title) == 127);
    CHECK(strlen(d->notifications[2].message) == 255);
    CHECK(strlen(d->notifications[2].app_name) == 63);

    CHECK(desktop_dismiss_notification(d, 99) == -1);
    CHECK(desktop_dismiss_notification(d, 2) == 0);
    CHECK(b->read && !a->read);
    CHECK(desktop_dismiss_notification(d, 1) == 0);
    CHECK(a->read);
    for (uint32_t i = 4; i <= DESKTOP_MAX_NOTIFICATIONS; i++)
        CHECK(desktop_notify(d, "t", "m", "a", 1) == i);
    CHECK(desktop_notify(d, "t", "m", "a", 1) == 0);
    CHECK(d->num_notifications == 32);
    CHECK(desktop_dismiss_notification(d, 32) == 0 && d->notifications[31].read);
    CHECK(desktop_dismiss_notification(d, 33) == -1);

    /* tray */
    uint32_t t1 = desktop_tray_add(d, "Network", "Network status", 5);
    uint32_t t2 = desktop_tray_add(d, NULL, NULL, 6);
    CHECK(t1 == 1 && t2 == 2);
    CHECK(d->num_tray_icons == 2);
    CHECK(d->tray[0].icon_id == 1);
    CHECK(strcmp(d->tray[0].name, "Network") == 0);
    CHECK(strcmp(d->tray[0].tooltip, "Network status") == 0);
    CHECK(d->tray[0].app_id == 5);
    CHECK(d->tray[0].active);
    CHECK(d->tray[0].name[8] == 0 && d->tray[0].name[63] == 0);
    CHECK(d->tray[0].tooltip[15] == 0 && d->tray[0].tooltip[127] == 0);
    CHECK(d->tray[1].name[0] == 0 && d->tray[1].tooltip[0] == 0);
    CHECK(d->tray[1].app_id == 6 && d->tray[1].active && d->tray[1].icon_id == 2);
    desktop_tray_add(d, big, big, 0);
    CHECK(strlen(d->tray[2].name) == 63);
    CHECK(strlen(d->tray[2].tooltip) == 127);
    CHECK(desktop_tray_remove(d, 99) == -1);
    CHECK(desktop_tray_remove(d, 1) == 0);
    CHECK(!d->tray[0].active);
    d->tray[0].active = true;
    CHECK(desktop_tray_remove(d, 2) == 0);
    CHECK(!d->tray[1].active && d->tray[0].active);
    for (uint32_t i = 4; i <= DESKTOP_MAX_ICONS; i++)
        CHECK(desktop_tray_add(d, "i", "t", 0) == i);
    CHECK(desktop_tray_add(d, "i", "t", 0) == 0);
    CHECK(d->num_tray_icons == 16);
    CHECK(desktop_tray_remove(d, 16) == 0 && !d->tray[15].active);
    CHECK(desktop_tray_remove(d, 17) == -1);
    free(d);
}

static void test_clock(void) {
    desktop_clock_sync(NULL, 5);
    desktop_clock_enable(NULL);
    desktop_clock_disable(NULL);
    CHECK(desktop_clock_get(NULL) == 0);
    CHECK(!desktop_clock_needs_sync(NULL));

    desktop_t *d = new_desk(1920, 1080, 32);
    desktop_clock_enable(d);
    CHECK(d->clock.clock_active);
    desktop_clock_disable(d);
    CHECK(!d->clock.clock_active);
    d->clock.needs_sync = true;
    CHECK(desktop_clock_needs_sync(d));

    /* first sync (last_sync_tick == 0): no drift computed */
    d->clock.drift_ppm = 123;
    desktop_clock_sync(d, 1000000);
    CHECK(d->clock.external_ticks == 1000000);
    CHECK(d->clock.clock_active);
    CHECK(!d->clock.needs_sync);
    CHECK(!desktop_clock_needs_sync(d));
    CHECK(d->clock.last_sync_tick == 0);
    CHECK(d->clock.drift_ppm == 123);
    CHECK(desktop_clock_get(d) == 1000000);

    /* drift = (500 - 1000) * 1e6 / 1000 = -500000 ppm */
    d->clock.local_counter = 1000;
    desktop_clock_sync(d, 1000);
    CHECK(d->clock.last_sync_tick == 1000);
    CHECK(d->clock.drift_ppm == 123);
    d->clock.local_counter = 1500;
    desktop_clock_sync(d, 2000);
    CHECK(d->clock.drift_ppm == -500000);
    CHECK(d->clock.last_sync_tick == 1500);
    CHECK(d->clock.external_ticks == 2000);

    /* local faster: local +3000, external +1000 from 1500 -> +2000000 ppm */
    d->clock.local_counter = 4500;
    desktop_clock_sync(d, 2500);
    CHECK(d->clock.drift_ppm == 2000000);
    CHECK(d->clock.last_sync_tick == 4500);
    /* truncation toward zero: local +1, external +3 -> (1-3)*1e6/3 */
    d->clock.local_counter = 4501;
    desktop_clock_sync(d, 4503);
    CHECK(d->clock.drift_ppm == -666666);
    /* equal rates: zero drift */
    d->clock.local_counter = 4601;
    desktop_clock_sync(d, 4601);
    CHECK(d->clock.drift_ppm == 0);

    /* external not past the last sync tick: drift left alone */
    d->clock.drift_ppm = 42;
    d->clock.needs_sync = true;
    d->clock.clock_active = false;
    d->clock.local_counter = 5000;
    desktop_clock_sync(d, 4601);
    CHECK(d->clock.drift_ppm == 42);
    CHECK(d->clock.external_ticks == 4601);
    CHECK(d->clock.last_sync_tick == 5000);
    CHECK(d->clock.clock_active && !d->clock.needs_sync);
    d->clock.local_counter = 6000;
    desktop_clock_sync(d, 4000);
    CHECK(d->clock.drift_ppm == 42);
    CHECK(d->clock.last_sync_tick == 6000);
    /* external just one past last sync: delta 1 */
    d->clock.local_counter = 6000;
    desktop_clock_sync(d, 6001);
    CHECK(d->clock.drift_ppm == -1000000);
    free(d);

    /* overflow clamp, positive: diff = 2^62 - 2 overflows * 1e6 */
    d = new_desk(1920, 1080, 32);
    d->clock.local_counter = 1;
    desktop_clock_sync(d, 1);
    d->clock.local_counter = 1 + ((uint64_t)1 << 62);
    desktop_clock_sync(d, 3);
    CHECK(d->clock.drift_ppm == INT64_MAX / 2);
    /* overflow clamp, negative: diff = 0 - (2^62 + 6) */
    d->clock.local_counter = 10;
    desktop_clock_sync(d, 0); /* resync at 10 (no drift: 0 < 10) */
    CHECK(d->clock.drift_ppm == INT64_MAX / 2);
    desktop_clock_sync(d, 10 + ((uint64_t)1 << 62) + 6);
    CHECK(d->clock.drift_ppm == (INT64_MIN + 1) / (int64_t)(((uint64_t)1 << 62) + 6));
    CHECK(d->clock.drift_ppm == -1);
    /* the negative clamp is INT64_MIN + 1 exactly: pick external deltas
     * that divide 2^63 and 2^63 - 1 so the quotient shows the constant */
    d->clock.local_counter = 30;
    desktop_clock_sync(d, 0);
    desktop_clock_sync(d, 30 + ((uint64_t)1 << 44));
    CHECK(d->clock.drift_ppm == -524287);
    d->clock.local_counter = 40;
    desktop_clock_sync(d, 0);
    desktop_clock_sync(d, 40 + 20303320287433ull);
    CHECK(d->clock.drift_ppm == -454279);
    /* largest non-overflowing diff: 9223372036854 * 1e6 fits */
    d->clock.local_counter = 20;
    desktop_clock_sync(d, 0);
    d->clock.local_counter = 20 + 9223372036854ull + 1;
    desktop_clock_sync(d, 21);
    CHECK(d->clock.drift_ppm == (int64_t)9223372036854 * 1000000);
    free(d);
}

static void test_render_input(void) {
    desktop_t *d = new_desk(640, 480, 32);
    uint32_t fb[4] = {1, 2, 3, 4};
    desktop_render(d, fb);
    desktop_render_taskbar(d, fb);
    desktop_render_window(d, &d->windows[0], fb);
    desktop_render_notifications(d, fb);
    desktop_render_wallpaper(d, fb);
    CHECK(fb[0] == 1 && fb[3] == 4);
    CHECK(desktop_handle_mouse(NULL, 1, 2, 3) == -1);
    CHECK(desktop_handle_mouse(d, 1, 2, 3) == 0);
    CHECK(desktop_handle_key(NULL, 1, true) == -1);
    CHECK(desktop_handle_key(d, 1, true) == 0);
    free(d);
}

static void test_coverage(void) {
    CHECK(!desktop_verify_coverage(NULL));
    desktop_t *d = new_desk(1920, 1080, 32);
    d->coverage_r = 5;
    d->coverage_l = 5;
    CHECK(desktop_verify_coverage(d));
    CHECK(d->coverage_r == (uint32_t)Q16_ONE);
    CHECK(d->coverage_l == 0);

    /* windows only: r = visible/num, l counts as 1 */
    desktop_create_window(d, "W1", 0, 0, 1, 1, 0);
    CHECK(desktop_verify_coverage(d));
    CHECK(d->coverage_r == 65536);
    CHECK(d->coverage_l == 0);
    desktop_create_window(d, "W2", 0, 0, 1, 1, 0);
    desktop_minimize_window(d, 2);
    CHECK(desktop_verify_coverage(d)); /* exactly 0.5: on the floor passes */
    CHECK(d->coverage_r == 32768);
    desktop_create_window(d, "W3", 0, 0, 1, 1, 0);
    desktop_minimize_window(d, 3);
    CHECK(!desktop_verify_coverage(d)); /* 1/3 < 0.5 */
    CHECK(d->coverage_r == 21845);
    desktop_maximize_window(d, 3);
    CHECK(desktop_verify_coverage(d)); /* 2/3 */
    CHECK(d->coverage_r == 43690);
    desktop_close_window(d, 1);
    desktop_close_window(d, 3);
    CHECK(!desktop_verify_coverage(d)); /* 0/3 */
    CHECK(d->coverage_r == 0);
    /* only the first num_windows slots are counted */
    d->windows[3].visible = true;
    CHECK(!desktop_verify_coverage(d));
    CHECK(d->coverage_r == 0);
    free(d);

    /* apps only: r = 1, l = running/apps */
    d = new_desk(1920, 1080, 32);
    desktop_register_app(d, "a", "i", "x");
    d->apps[1].running = true; /* beyond num_apps: not counted */
    CHECK(!desktop_verify_coverage(d)); /* 0 of 1 running */
    CHECK(d->coverage_r == 65536 && d->coverage_l == 0);
    desktop_register_app(d, "b", "i", "x");
    d->apps[1].running = true; /* running without a window */
    CHECK(desktop_verify_coverage(d)); /* 1/2 */
    CHECK(d->coverage_l == 32768);
    CHECK(d->coverage_r == 65536);
    desktop_register_app(d, "c", "i", "x");
    CHECK(!desktop_verify_coverage(d)); /* 1/3 */
    CHECK(d->coverage_l == 21845);
    d->apps[0].running = true;
    CHECK(desktop_verify_coverage(d)); /* 2/3 */
    CHECK(d->coverage_l == 43690);
    free(d);

    /* both: product r*l against 0.5 */
    d = new_desk(1920, 1080, 32);
    desktop_register_app(d, "a", "i", "x");
    desktop_register_app(d, "b", "i", "x");
    desktop_launch_app(d, 1);
    desktop_launch_app(d, 2);
    CHECK(desktop_verify_coverage(d));
    CHECK(d->coverage_r == 65536 && d->coverage_l == 65536);
    desktop_minimize_window(d, 2); /* r = 1/2, l = 1 -> 0.5 */
    CHECK(desktop_verify_coverage(d));
    desktop_register_app(d, "c", "i", "x"); /* l = 2/3 -> 1/3 */
    CHECK(!desktop_verify_coverage(d));
    CHECK(d->coverage_r == 32768 && d->coverage_l == 43690);
    free(d);
}

static void test_bringup(void) {
    zxv_bringup_fn fn = find_bringup("desktop");
    CHECK(fn != NULL);
    if (fn) CHECK(fn() == 0);
    desktop_t *d = find_static_desk();
    CHECK(d != NULL);
    if (!d) return;
    CHECK(d->screen_w == 1024 && d->screen_h == 768 && d->bpp == 32);
    CHECK(d->taskbar_y == 728);
    CHECK(d->num_windows == 1);
    CHECK(d->active_window == 1);
    CHECK(d->z_counter == 3); /* created at z 1, focused at z 2 */
    desktop_window_t *w = &d->windows[0];
    CHECK(strcmp(w->title, "zxv") == 0);
    CHECK(w->window_id == 1 && w->z_order == 2);
    CHECK(w->x == 0 && w->y == 0 && w->w == 320 && w->h == 200);
    CHECK(w->app_id == 0);
}

int main(void) {
    test_init_theme();
    test_create_window();
    test_window_ops();
    test_apps();
    test_notifications_tray();
    test_clock();
    test_render_input();
    test_coverage();
    test_bringup();
    if (failures) {
        printf("%d of %d checks failed\n", failures, checks);
        return 1;
    }
    printf("%d checks passed\n", checks);
    return 0;
}
