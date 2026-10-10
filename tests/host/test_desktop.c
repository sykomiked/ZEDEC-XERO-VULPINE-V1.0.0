/* test_desktop.c — Desktop Environment Tests (W7)
 *
 * Tests for window management, app registration, notifications,
 * system tray, event-driven clock, and coverage verification.
 *
 * Author: 36N9 Genetics, LLC
 * License: Apache-2.0
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "desktop.h"

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void name(void)
#define RUN(name) do { \
    tests_run++; \
    printf("  [TEST] %s ... ", #name); \
    name(); \
    tests_passed++; \
    printf("PASS\n"); \
} while(0)

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf("FAIL: %s\n", msg); \
        tests_failed++; \
        return; \
    } \
} while(0)

#define PASS() return

/* ===== Init Tests ===== */

TEST(init_test) {
    desktop_t desk;
    desktop_init(&desk, 1920, 1080, 32);
    ASSERT(desk.screen_w == 1920, "screen width");
    ASSERT(desk.screen_h == 1080, "screen height");
    ASSERT(desk.bpp == 32, "bpp");
    ASSERT(desk.num_windows == 0, "no windows");
    ASSERT(desk.num_apps == 0, "no apps");
    ASSERT(desk.num_notifications == 0, "no notifications");
    ASSERT(desk.num_tray_icons == 0, "no tray icons");
    ASSERT(desk.taskbar_visible, "taskbar visible");
    ASSERT(!desk.start_menu_open, "start menu closed");
    ASSERT(desk.z_counter == 1, "z counter starts at 1");
    PASS();
}

TEST(theme_test) {
    desktop_t desk;
    desktop_init(&desk, 1920, 1080, 32);
    ASSERT(desk.theme.desktop_bg == 0x000000, "dragon bg is black");
    ASSERT(desk.theme.taskbar_fg == 0xFFD700, "taskbar fg is gold");
    ASSERT(desk.theme.accent == 0xFF6600, "accent is fire orange");
    ASSERT(desk.theme.animations, "animations enabled");
    ASSERT(!desk.theme.sounds, "sounds disabled by default");
    ASSERT(strcmp(desk.theme.wallpaper, "/usr/share/dragon-wallpaper.png") == 0,
           "wallpaper path");
    PASS();
}

/* ===== Window Tests ===== */

TEST(create_window_test) {
    desktop_t desk;
    desktop_init(&desk, 1920, 1080, 32);
    uint32_t id = desktop_create_window(&desk, "Test Window", 100, 100, 640, 480, 0);
    ASSERT(id > 0, "window created with valid id");
    ASSERT(desk.num_windows == 1, "1 window");
    ASSERT(desk.active_window == id, "active window is the new one");

    desktop_window_t *w = desktop_get_window(&desk, id);
    ASSERT(w != NULL, "window found");
    ASSERT(strcmp(w->title, "Test Window") == 0, "title matches");
    ASSERT(w->x == 100, "x position");
    ASSERT(w->y == 100, "y position");
    ASSERT(w->w == 640, "width");
    ASSERT(w->h == 480, "height");
    ASSERT(w->active, "window is active");
    ASSERT(w->visible, "window is visible");
    ASSERT(w->state == WIN_NORMAL, "state is normal");
    ASSERT(w->resizable, "window is resizable");
    ASSERT(w->closable, "window is closable");
    PASS();
}

TEST(close_window_test) {
    desktop_t desk;
    desktop_init(&desk, 1920, 1080, 32);
    uint32_t id = desktop_create_window(&desk, "Close Me", 0, 0, 100, 100, 0);
    ASSERT(desktop_close_window(&desk, id) == 0, "close succeeds");
    desktop_window_t *w = desktop_get_window(&desk, id);
    ASSERT(w->state == WIN_CLOSING, "state is closing");
    ASSERT(!w->active, "not active");
    ASSERT(!w->visible, "not visible");
    PASS();
}

TEST(minimize_window_test) {
    desktop_t desk;
    desktop_init(&desk, 1920, 1080, 32);
    uint32_t id = desktop_create_window(&desk, "Minimize Me", 50, 50, 200, 200, 0);
    ASSERT(desktop_minimize_window(&desk, id) == 0, "minimize succeeds");
    desktop_window_t *w = desktop_get_window(&desk, id);
    ASSERT(w->state == WIN_MINIMIZED, "state is minimized");
    ASSERT(!w->visible, "not visible");
    ASSERT(w->prev_x == 50, "prev x saved");
    ASSERT(w->prev_w == 200, "prev w saved");
    PASS();
}

TEST(maximize_window_test) {
    desktop_t desk;
    desktop_init(&desk, 1920, 1080, 32);
    uint32_t id = desktop_create_window(&desk, "Maximize Me", 50, 50, 200, 200, 0);
    ASSERT(desktop_maximize_window(&desk, id) == 0, "maximize succeeds");
    desktop_window_t *w = desktop_get_window(&desk, id);
    ASSERT(w->state == WIN_MAXIMIZED, "state is maximized");
    ASSERT(w->x == 0, "x is 0");
    ASSERT(w->y == 0, "y is 0");
    ASSERT(w->w == 1920, "w is screen width");
    ASSERT(w->h == 1040, "h is screen height minus taskbar");
    ASSERT(w->prev_x == 50, "prev x saved");
    PASS();
}

TEST(focus_window_test) {
    desktop_t desk;
    desktop_init(&desk, 1920, 1080, 32);
    uint32_t id1 = desktop_create_window(&desk, "W1", 0, 0, 100, 100, 0);
    uint32_t id2 = desktop_create_window(&desk, "W2", 0, 0, 100, 100, 0);
    ASSERT(desk.active_window == id2, "W2 is active (most recent)");

    ASSERT(desktop_focus_window(&desk, id1) == 0, "focus W1");
    ASSERT(desk.active_window == id1, "W1 is now active");

    desktop_window_t *w1 = desktop_get_window(&desk, id1);
    desktop_window_t *w2 = desktop_get_window(&desk, id2);
    ASSERT(w1->z_order > w2->z_order, "W1 has higher z-order");
    PASS();
}

TEST(move_window_test) {
    desktop_t desk;
    desktop_init(&desk, 1920, 1080, 32);
    uint32_t id = desktop_create_window(&desk, "Move Me", 0, 0, 100, 100, 0);
    ASSERT(desktop_move_window(&desk, id, 200, 300) == 0, "move succeeds");
    desktop_window_t *w = desktop_get_window(&desk, id);
    ASSERT(w->x == 200, "x is 200");
    ASSERT(w->y == 300, "y is 300");
    PASS();
}

TEST(resize_window_test) {
    desktop_t desk;
    desktop_init(&desk, 1920, 1080, 32);
    uint32_t id = desktop_create_window(&desk, "Resize Me", 0, 0, 100, 100, 0);
    ASSERT(desktop_resize_window(&desk, id, 800, 600) == 0, "resize succeeds");
    desktop_window_t *w = desktop_get_window(&desk, id);
    ASSERT(w->w == 800, "w is 800");
    ASSERT(w->h == 600, "h is 600");
    PASS();
}

TEST(get_nonexistent_window_test) {
    desktop_t desk;
    desktop_init(&desk, 1920, 1080, 32);
    ASSERT(desktop_get_window(&desk, 999) == NULL, "nonexistent returns null");
    PASS();
}

/* ===== App Tests ===== */

TEST(register_app_test) {
    desktop_t desk;
    desktop_init(&desk, 1920, 1080, 32);
    uint32_t id = desktop_register_app(&desk, "Terminal", "term.png", "/bin/pterm");
    ASSERT(id > 0, "app registered");
    ASSERT(desk.num_apps == 1, "1 app");
    ASSERT(strcmp(desk.apps[0].name, "Terminal") == 0, "name matches");
    ASSERT(strcmp(desk.apps[0].icon, "term.png") == 0, "icon matches");
    ASSERT(strcmp(desk.apps[0].executable, "/bin/pterm") == 0, "executable matches");
    ASSERT(!desk.apps[0].running, "not running initially");
    PASS();
}

TEST(launch_app_test) {
    desktop_t desk;
    desktop_init(&desk, 1920, 1080, 32);
    uint32_t id = desktop_register_app(&desk, "Terminal", "term.png", "/bin/pterm");
    ASSERT(desktop_launch_app(&desk, id) == 0, "launch succeeds");
    ASSERT(desk.apps[0].running, "app is running");
    ASSERT(desk.apps[0].window_id > 0, "app has a window");
    ASSERT(desk.num_windows == 1, "1 window created");
    PASS();
}

TEST(launch_app_twice_test) {
    desktop_t desk;
    desktop_init(&desk, 1920, 1080, 32);
    uint32_t id = desktop_register_app(&desk, "Terminal", "term.png", "/bin/pterm");
    desktop_launch_app(&desk, id);
    ASSERT(desktop_launch_app(&desk, id) == 0, "second launch is no-op success");
    ASSERT(desk.num_windows == 1, "still 1 window");
    PASS();
}

TEST(pin_to_taskbar_test) {
    desktop_t desk;
    desktop_init(&desk, 1920, 1080, 32);
    uint32_t id = desktop_register_app(&desk, "Terminal", "term.png", "/bin/pterm");
    ASSERT(desktop_pin_to_taskbar(&desk, id) == 0, "pin succeeds");
    ASSERT(desk.apps[0].pinned_to_taskbar, "app is pinned");
    PASS();
}

/* ===== Notification Tests ===== */

TEST(notify_test) {
    desktop_t desk;
    desktop_init(&desk, 1920, 1080, 32);
    uint32_t id = desktop_notify(&desk, "Alert", "System update available", "System", 2);
    ASSERT(id > 0, "notification created");
    ASSERT(desk.num_notifications == 1, "1 notification");
    ASSERT(strcmp(desk.notifications[0].title, "Alert") == 0, "title matches");
    ASSERT(strcmp(desk.notifications[0].message, "System update available") == 0,
           "message matches");
    ASSERT(desk.notifications[0].priority == 2, "priority is 2");
    ASSERT(!desk.notifications[0].read, "not read initially");
    PASS();
}

TEST(dismiss_notification_test) {
    desktop_t desk;
    desktop_init(&desk, 1920, 1080, 32);
    uint32_t id = desktop_notify(&desk, "Alert", "Test", "App", 1);
    ASSERT(desktop_dismiss_notification(&desk, id) == 0, "dismiss succeeds");
    ASSERT(desk.notifications[0].read, "notification is read");
    PASS();
}

/* ===== Tray Tests ===== */

TEST(tray_add_test) {
    desktop_t desk;
    desktop_init(&desk, 1920, 1080, 32);
    uint32_t id = desktop_tray_add(&desk, "Network", "Network status", 0);
    ASSERT(id > 0, "tray icon added");
    ASSERT(desk.num_tray_icons == 1, "1 tray icon");
    ASSERT(desk.tray[0].active, "icon is active");
    ASSERT(strcmp(desk.tray[0].name, "Network") == 0, "name matches");
    PASS();
}

TEST(tray_remove_test) {
    desktop_t desk;
    desktop_init(&desk, 1920, 1080, 32);
    uint32_t id = desktop_tray_add(&desk, "Network", "Network status", 0);
    ASSERT(desktop_tray_remove(&desk, id) == 0, "remove succeeds");
    ASSERT(!desk.tray[0].active, "icon is inactive");
    PASS();
}

/* ===== Clock Tests ===== */

TEST(clock_init_test) {
    desktop_t desk;
    desktop_init(&desk, 1920, 1080, 32);
    ASSERT(!desk.clock.clock_active, "clock not active initially");
    ASSERT(desk.clock.external_ticks == 0, "no external ticks");
    ASSERT(!desk.clock.needs_sync, "no sync needed initially");
    PASS();
}

TEST(clock_sync_test) {
    desktop_t desk;
    desktop_init(&desk, 1920, 1080, 32);
    desktop_clock_sync(&desk, 1000000);
    ASSERT(desk.clock.external_ticks == 1000000, "external ticks set");
    ASSERT(desk.clock.clock_active, "clock is active after sync");
    ASSERT(!desk.clock.needs_sync, "sync no longer needed");
    ASSERT(desktop_clock_get(&desk) == 1000000, "clock_get returns external ticks");
    PASS();
}

TEST(clock_enable_disable_test) {
    desktop_t desk;
    desktop_init(&desk, 1920, 1080, 32);
    desktop_clock_enable(&desk);
    ASSERT(desk.clock.clock_active, "clock enabled");
    desktop_clock_disable(&desk);
    ASSERT(!desk.clock.clock_active, "clock disabled");
    PASS();
}

TEST(clock_drift_test) {
    desktop_t desk;
    desktop_init(&desk, 1920, 1080, 32);
    /* Advance local counter before first sync so last_sync_tick > 0 */
    desk.clock.local_counter = 1000;
    desktop_clock_sync(&desk, 1000);
    /* Advance local counter by 500 */
    desk.clock.local_counter = 1500;
    /* Second sync — local advanced 500, external advanced 1000 */
    desktop_clock_sync(&desk, 2000);
    /* drift = (500 - 1000) * 1e6 / 1000 = -500000 ppm */
    ASSERT(desk.clock.drift_ppm == -500000, "drift is exactly -500000 ppm (integer, local slower)");
    PASS();
}

/* ===== Coverage Tests ===== */

TEST(coverage_empty_test) {
    desktop_t desk;
    desktop_init(&desk, 1920, 1080, 32);
    ASSERT(desktop_verify_coverage(&desk), "empty desktop has coverage");
    ASSERT(desk.coverage_r == (uint32_t) Q16_ONE, "r is 1.0 with no windows");
    PASS();
}

TEST(coverage_with_windows_test) {
    desktop_t desk;
    desktop_init(&desk, 1920, 1080, 32);
    desktop_create_window(&desk, "W1", 0, 0, 100, 100, 0);
    desktop_create_window(&desk, "W2", 0, 0, 100, 100, 0);
    desktop_minimize_window(&desk, 2);
    ASSERT(desktop_verify_coverage(&desk), "coverage verified");
    ASSERT(desk.coverage_r == (uint32_t) Q16_ONE / 2, "r is 0.5 (1 of 2 visible)");
    PASS();
}

/* ===== Main ===== */

int main(void) {
    printf("\n=== ZXV Desktop (W7) Tests ===\n\n");

    RUN(init_test);
    RUN(theme_test);
    RUN(create_window_test);
    RUN(close_window_test);
    RUN(minimize_window_test);
    RUN(maximize_window_test);
    RUN(focus_window_test);
    RUN(move_window_test);
    RUN(resize_window_test);
    RUN(get_nonexistent_window_test);
    RUN(register_app_test);
    RUN(launch_app_test);
    RUN(launch_app_twice_test);
    RUN(pin_to_taskbar_test);
    RUN(notify_test);
    RUN(dismiss_notification_test);
    RUN(tray_add_test);
    RUN(tray_remove_test);
    RUN(clock_init_test);
    RUN(clock_sync_test);
    RUN(clock_enable_disable_test);
    RUN(clock_drift_test);
    RUN(coverage_empty_test);
    RUN(coverage_with_windows_test);

    printf("\n=== Results: %d/%d passed, %d failed ===\n",
           tests_passed, tests_run, tests_failed);
    if (tests_failed == 0) {
        printf("ALL TESTS PASSED\n");
    }
    return tests_failed > 0 ? 1 : 0;
}
