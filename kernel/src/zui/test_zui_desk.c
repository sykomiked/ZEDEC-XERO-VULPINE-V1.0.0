/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zui_desk.c — the desktop chrome every screen shares (zui_desk.h D1..D4). */
#include <stdio.h>
#include <string.h>

#include "zui_desk.h"
#include "zui_tokens.h"

static int fails, checks;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            fails++;                                                                               \
            printf("  FAIL (%s:%d) ", __FILE__, __LINE__);                                         \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

#define W  1280
#define H  800
#define C1 0x204060u /* what the app paints (test only) */

static uint32_t PX[W * H], PX2[W * H];
static zui_desk_t D;

static int paints, events;
static zui_event_t last;
static void a_paint(void *ctx, zui_surface_t *s, zui_rect_t c, uint32_t tick)
{
    (void) ctx;
    (void) tick;
    paints++;
    zui_fill(s, c, C1);
}
static bool a_event(void *ctx, const zui_event_t *e, zui_rect_t c)
{
    (void) ctx;
    (void) c;
    events++;
    last = *e;
    return true;
}
static const zui_wm_client_t CL = {a_paint, a_event, 0, 0};

static bool ptr(zui_ev_type_t t, int32_t x, int32_t y)
{
    zui_event_t e;
    memset(&e, 0, sizeof e);
    e.type = t;
    e.x = x;
    e.y = y;
    e.button = ZUI_BTN_LEFT;
    return zui_desk_event(&D, &e);
}
static void tap(int32_t x, int32_t y)
{
    ptr(ZUI_EV_PTR_DOWN, x, y);
    ptr(ZUI_EV_PTR_UP, x, y);
}
static int32_t mid_x(zui_rect_t r)
{
    return r.x + r.w / 2;
}
static int32_t mid_y(zui_rect_t r)
{
    return r.y + r.h / 2;
}
static bool inside(zui_rect_t outer, zui_rect_t r)
{
    return r.x >= outer.x && r.y >= outer.y && r.x + r.w <= outer.x + outer.w &&
           r.y + r.h <= outer.y + outer.h;
}
static uint32_t hash(const uint32_t *p)
{
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < W * H; i++) h = (h ^ p[i]) * 16777619u;
    return h;
}

static void geometry(void)
{
    zui_desk_init(&D, W, H, 256u, false, 0);
    zui_rect_t bar = zui_desk_bar(&D), rail = zui_desk_rail(&D), a = zui_desk_area(&D);
    CHECK(bar.x == 0 && bar.y == 0 && bar.w == W && bar.h == ZUI_DESK_BAR_PT, "D1 bar %d", bar.h);
    CHECK(rail.x == 0 && rail.y == bar.h && rail.w == ZUI_DESK_RAIL_PT && rail.h == H - bar.h,
          "D2 rail");
    CHECK(a.x == rail.w && a.y == bar.h && a.w == W - rail.w && a.h == H - bar.h, "D3 area");
    zui_rect_t t0 = zui_desk_tile(&D, 0), t1 = zui_desk_tile(&D, 1);
    CHECK(t0.w == ZUI_DESK_TILE_PT && t0.h == ZUI_DESK_TILE_PT, "tile size");
    CHECK(inside(rail, t0) && inside(rail, t1), "tiles live in the rail");
    CHECK(t1.y > t0.y + t0.h, "tiles stack with a gap for the name");
    CHECK(mid_x(t0) == rail.w / 2, "tiles are centred in the rail");
    zui_rect_t home = zui_desk_switch(&D, false), sp = zui_desk_switch(&D, true);
    CHECK(home.x + home.w == sp.x && home.w == sp.w, "the switch is two halves");
    CHECK(home.x + home.w == W / 2, "the switch sits in the centre");
    CHECK(inside(bar, home) && inside(bar, sp), "the switch is in the bar");

    /* D4: points scale */
    zui_desk_init(&D, W * 2, H * 2, 512u, false, 0);
    CHECK(zui_desk_pt(&D, 10) == 20, "2x");
    CHECK(zui_desk_bar(&D).h == 2 * ZUI_DESK_BAR_PT, "the bar scales");
    CHECK(zui_desk_rail(&D).w == 2 * ZUI_DESK_RAIL_PT, "the rail scales");
    zui_desk_init(&D, W, H, 384u, false, 0);
    CHECK(zui_desk_pt(&D, 10) == 15, "1.5x");
    zui_desk_init(&D, W, H, 0u, false, 0);
    CHECK(D.scale_q8 == 256u, "scale 0 means 1x");
    zui_desk_resize(&D, 640, 480, 512u);
    CHECK(D.w == 640 && D.h == 480 && D.scale_q8 == 512u, "resize");
}

static void rail(void)
{
    zui_desk_init(&D, W, H, 256u, false, 0);
    CHECK(D.hover == -1 && D.press == -1 && D.act_app == -1, "starts idle");
    CHECK(zui_desk_add(&D, "Vino", "V") == 0, "first app");
    CHECK(zui_desk_add(&D, "Fleet", "F") == 1, "second app");
    CHECK(D.napps == 2 && strcmp(D.app[1].name, "Fleet") == 0 && strcmp(D.app[1].mark, "F") == 0,
          "the rail keeps its apps");
    for (uint32_t i = 2; i < ZUI_DESK_APPS; i++) zui_desk_add(&D, "x", "x");
    CHECK(zui_desk_add(&D, "full", "f") == -1, "a full rail says so");
    CHECK(D.napps == ZUI_DESK_APPS, "and stays full");

    zui_desk_init(&D, W, H, 256u, false, 0);
    zui_desk_add(&D, "Vino", "V");
    zui_desk_add(&D, "Fleet", "F");
    zui_rect_t t1 = zui_desk_tile(&D, 1);
    CHECK(ptr(ZUI_EV_PTR_MOVE, mid_x(t1), mid_y(t1)) && D.hover == 1, "hover a tile");
    CHECK(!ptr(ZUI_EV_PTR_MOVE, mid_x(t1), zui_desk_tile(&D, 3).y) && D.hover == -1,
          "no tile past the last app");
    ptr(ZUI_EV_PTR_DOWN, mid_x(t1), mid_y(t1));
    CHECK(D.press == 1 && D.act == ZUI_DESK_NONE, "press is not launch");
    ptr(ZUI_EV_PTR_UP, mid_x(t1), mid_y(t1));
    CHECK(D.act == ZUI_DESK_LAUNCH && D.act_app == 1 && D.press == -1, "D2 release launches");
    zui_rect_t t0 = zui_desk_tile(&D, 0);
    ptr(ZUI_EV_PTR_DOWN, mid_x(t1), mid_y(t1));
    ptr(ZUI_EV_PTR_UP, mid_x(t0), mid_y(t0));
    CHECK(D.act == ZUI_DESK_NONE && D.act_app == -1, "sliding off cancels");
    ptr(ZUI_EV_PTR_MOVE, mid_x(t0), mid_y(t0));
    CHECK(D.act == ZUI_DESK_NONE, "each event resets the act");

    zui_rect_t sp = zui_desk_switch(&D, true), home = zui_desk_switch(&D, false);
    CHECK(ptr(ZUI_EV_PTR_DOWN, mid_x(sp), mid_y(sp)) && D.act == ZUI_DESK_SPACES, "D1 Spaces");
    ptr(ZUI_EV_PTR_UP, mid_x(sp), mid_y(sp));
    CHECK(ptr(ZUI_EV_PTR_DOWN, mid_x(home), mid_y(home)) && D.act == ZUI_DESK_HOME, "D1 Desktop");
    ptr(ZUI_EV_PTR_UP, mid_x(home), mid_y(home));
    CHECK(ptr(ZUI_EV_PTR_DOWN, 4, 4) && D.act == ZUI_DESK_NONE, "the bar eats its clicks");
    ptr(ZUI_EV_PTR_UP, 4, 4);
}

static void windows(void)
{
    zui_desk_init(&D, W, H, 256u, false, 0);
    zui_desk_add(&D, "Vino", "V");
    zui_desk_add(&D, "Fleet", "F");
    CHECK(zui_desk_front_app(&D) == -1, "no window yet");
    CHECK(zui_desk_open(&D, 5, "x", &CL) == 0, "no such app");
    uint32_t a = zui_desk_open(&D, 0, 0, &CL);
    CHECK(a != 0 && D.win_of[0] == a, "open records the window");
    CHECK(zui_desk_front_app(&D) == 0, "the new window is in front");
    uint32_t b = zui_desk_open(&D, 1, "Fleet title", &CL);
    CHECK(b != 0 && b != a && zui_desk_front_app(&D) == 1, "second window in front");
    CHECK(zui_desk_open(&D, 0, 0, &CL) == a, "opening again reuses the window");
    CHECK(zui_desk_front_app(&D) == 0, "and brings it to the front");
    zui_wm_progress(&D.wm, a, 256u);
    zui_wm_progress(&D.wm, b, 256u);
    zui_desk_tick(&D, 5000u);
    CHECK(zui_wm_get(&D.wm, a)->phase == ZUI_WIN_OPEN, "the tick runs the doors");

    /* D3: the area is the window manager's screen */
    CHECK(D.wm.w == zui_desk_area(&D).w && D.wm.h == zui_desk_area(&D).h, "wm gets the area");
    zui_rect_t c = zui_wm_content(&D.wm, zui_wm_get(&D.wm, a));
    zui_rect_t area = zui_desk_area(&D);
    events = 0;
    tap(area.x + c.x + c.w / 2, area.y + c.y + c.h / 2);
    CHECK(events > 0, "a click in the content reaches the app");
    CHECK(last.x == c.w / 2 && last.y == c.h / 2, "relative to the content (%d,%d)", last.x,
          last.y);

    /* D4: the same events draw the same pixels; the app paints in the area */
    zui_surface_t s;
    zui_surface_init(&s, PX, W, H, W);
    paints = 0;
    zui_desk_draw(&D, &s, 7u);
    CHECK(paints > 0, "the app painted");
    CHECK(PX[(uint32_t) (area.y + c.y + c.h / 2) * W + (uint32_t) (area.x + c.x + c.w / 2)] == C1,
          "content lands at the area offset");
    CHECK(PX[(uint32_t) (H - 2) * W + 2] != C1, "nothing paints over the rail");
    zui_surface_init(&s, PX2, W, H, W);
    zui_desk_draw(&D, &s, 7u);
    CHECK(hash(PX) == hash(PX2), "deterministic");
    uint32_t px_bar = PX[2u * W + (uint32_t) (W / 3)];
    zui_desk_draw_bar(&D, &s);
    CHECK(PX2[2u * W + (uint32_t) (W / 3)] == px_bar, "draw_bar draws the same bar");

    /* closing clears the rail's record */
    CHECK(zui_wm_close(&D.wm, a), "close");
    CHECK(D.win_of[0] == 0 && D.win_of[1] == b, "on_window forgets only the closed one");
    CHECK(zui_desk_front_app(&D) == 1, "the other comes forward");
    uint32_t a2 = zui_desk_open(&D, 0, 0, &CL);
    CHECK(a2 != 0 && D.win_of[0] == a2, "it opens again after close");
}

static void status(void)
{
    zui_desk_init(&D, W, H, 256u, false, 0);
    zui_desk_status(&D, "Economy 5 of 5");
    CHECK(strcmp(D.status, "Economy 5 of 5") == 0, "status");
    char big[ZUI_DESK_STATUS * 2];
    memset(big, 'a', sizeof big - 1);
    big[sizeof big - 1] = 0;
    zui_desk_status(&D, big);
    CHECK(strlen(D.status) == ZUI_DESK_STATUS - 1, "status is bounded");
}

int main(void)
{
    geometry();
    rail();
    windows();
    status();
    if (fails) {
        printf("test_zui_desk: %d of %d checks failed\n", fails, checks);
        return 1;
    }
    printf("[PASS] test_zui_desk: %d/%d checks\n", checks, checks);
    return 0;
}
