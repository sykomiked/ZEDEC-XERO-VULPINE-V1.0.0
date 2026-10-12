/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zui_wm.c — windows in the app space (zui_wm.h WM1..WM8). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zui_tokens.h"
#include "zui_wm.h"

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
#define C1 0x204060u /* what app 1 paints (test only) */
#define C2 0x406020u

static uint32_t PX[W * H], PX2[W * H];
static zui_surface_t S;
static zui_wm_t WM;

typedef struct {
    uint32_t rgb;
    int paints, events, closed;
    zui_event_t last;
} app_t;
static app_t A1 = {C1, 0, 0, 0, {0}}, A2 = {C2, 0, 0, 0, {0}};

static void a_paint(void *ctx, zui_surface_t *s, zui_rect_t c, uint32_t tick)
{
    (void) tick;
    app_t *a = ctx;
    a->paints++;
    zui_fill(s, c, a->rgb);
}
static bool a_event(void *ctx, const zui_event_t *e, zui_rect_t c)
{
    (void) c;
    app_t *a = ctx;
    a->events++;
    a->last = *e;
    return true;
}
static void a_closed(void *ctx)
{
    ((app_t *) ctx)->closed++;
}

static int marks;
static void mark(zui_surface_t *s, zui_rect_t box, uint32_t lit, void *ctx)
{
    (void) lit, (void) ctx;
    marks++;
    zui_fill(s, box, ZUI_GOLD);
}

static int opens, closes;
static uint32_t last_open;
static void on_window(zui_wm_t *wm, uint32_t id, bool open, void *ctx)
{
    (void) wm, (void) ctx;
    if (open) {
        opens++;
        last_open = id;
    } else {
        closes++;
    }
}

static int themed;
static void theme(zui_surface_t *s, zui_rect_t area, const zui_wm_window_t *w,
                  zui_launch_state_t st, uint32_t tick, void *ctx)
{
    (void) s, (void) area, (void) w, (void) tick, (void) ctx;
    themed += st.gather == 256u ? 1 : 0;
}

static uint32_t at(int32_t x, int32_t y)
{
    return S.px[y * S.stride + x] & 0xFFFFFFu;
}

static zui_event_t ev(uint8_t type, int32_t x, int32_t y, uint32_t ms)
{
    zui_event_t e;
    memset(&e, 0, sizeof e);
    e.type = type;
    e.x = x;
    e.y = y;
    e.ms = ms;
    e.button = 1;
    return e;
}

static uint32_t now = 1000;
static bool down(int32_t x, int32_t y)
{
    now += 1000;
    zui_event_t e = ev(ZUI_EV_PTR_DOWN, x, y, now);
    return zui_wm_event(&WM, &e);
}
static bool move(int32_t x, int32_t y)
{
    zui_event_t e = ev(ZUI_EV_PTR_MOVE, x, y, now);
    return zui_wm_event(&WM, &e);
}
static bool up(int32_t x, int32_t y)
{
    zui_event_t e = ev(ZUI_EV_PTR_UP, x, y, now);
    return zui_wm_event(&WM, &e);
}
static bool keyev(uint8_t type, uint16_t k, uint8_t mods)
{
    zui_event_t e = ev(type, 0, 0, now);
    e.key = k;
    e.mods = mods;
    return zui_wm_event(&WM, &e);
}

static const zui_wm_client_t CL1 = {a_paint, a_event, a_closed, &A1};
static const zui_wm_client_t CL2 = {a_paint, a_event, a_closed, &A2};

static zui_launch_t launch_with_mark(void)
{
    zui_launch_t l;
    memset(&l, 0, sizeof l);
    l.mark = mark;
    return l;
}

static void open_fully(uint32_t id)
{
    zui_wm_progress(&WM, id, 256);
    zui_wm_tick(&WM, ZUI_WM_OPEN_MS);
}

static void test_launch(void)
{
    zui_wm_init(&WM, W, H, false);
    WM.on_window = on_window;
    zui_rect_t a = zui_wm_work(&WM);
    CHECK(a.w == W && a.h == H - ZUI_WM_STRIP && !WM.compact, "the work area is above the strip");
    zui_launch_t l = launch_with_mark();
    uint32_t id = zui_wm_open(&WM, "glint", "Glint", &l, &CL1);
    zui_wm_window_t *w = zui_wm_get(&WM, id);
    CHECK(id && opens == 1 && last_open == id, "WM2: opening is announced");
    CHECK(w && w->phase == ZUI_WIN_LAUNCHING && zui_wm_front(&WM) == id, "it launches in front");
    CHECK(w->r.x >= a.x && w->r.y >= a.y && w->r.x + w->r.w <= a.w && w->r.y + w->r.h <= a.h,
          "inside the work area");
    marks = 0;
    zui_wm_draw(&WM, &S, 0);
    zui_rect_t c = zui_wm_content(&WM, w);
    CHECK(marks > 0 && A1.paints == 0, "WM2: the app's mark, not the app (%d)", marks);
    CHECK(at(c.x + c.w / 2, c.y + c.h / 3) != C1, "the launch screen fills the window");
    CHECK(at(2, 2) == ZUI_VOID, "the ground around it");
    zui_event_t e = ev(ZUI_EV_PTR_DOWN, c.x + 10, c.y + 10, now);
    zui_wm_event(&WM, &e);
    up(c.x + 10, c.y + 10);
    CHECK(A1.events == 0, "WM7: a launching window gets no events");
    zui_wm_progress(&WM, id, 100);
    CHECK(w->phase == ZUI_WIN_LAUNCHING && w->progress == 100, "real progress, still loading");
    zui_wm_tick(&WM, 10000);
    CHECK(w->phase == ZUI_WIN_LAUNCHING, "time alone never finishes a launch");
    zui_wm_progress(&WM, id, 300);
    CHECK(w->phase == ZUI_WIN_OPENING && w->progress == 256, "loaded: the doors");
    zui_wm_tick(&WM, ZUI_WM_OPEN_MS / 2);
    zui_wm_draw(&WM, &S, 1);
    CHECK(A1.paints == 1 && at(c.x + c.w / 2, c.y + c.h / 2) == C1, "the app shows between them");
    CHECK(at(c.x + 2, c.y + c.h / 2) != C1 && at(c.x + c.w - 3, c.y + c.h / 2) != C1,
          "both doors still cover the sides");
    zui_wm_tick(&WM, ZUI_WM_OPEN_MS);
    zui_wm_draw(&WM, &S, 2);
    CHECK(w->phase == ZUI_WIN_OPEN && at(c.x + 2, c.y + c.h / 2) == C1, "open: all the app");
    zui_wm_progress(&WM, id, 0);
    CHECK(w->phase == ZUI_WIN_OPEN, "progress after opening changes nothing");
    memcpy(PX2, PX, sizeof PX);
    zui_wm_draw(&WM, &S, 2);
    CHECK(!memcmp(PX, PX2, sizeof PX), "WM8: the same state draws the same pixels");

    /* a theme draws the launch screen instead */
    WM.launch_draw = theme;
    uint32_t t = zui_wm_open(&WM, "quillon", "Quillon", 0, 0);
    zui_wm_progress(&WM, t, 128);
    themed = 0;
    zui_wm_draw(&WM, &S, 3);
    CHECK(themed == 1, "the theme gets the same state (gathered at 128)");
    WM.launch_draw = 0;
    zui_wm_close(&WM, t);
}

static void test_windows(void)
{
    zui_wm_init(&WM, W, H, false);
    WM.on_window = on_window;
    memset(&A1, 0, sizeof A1);
    memset(&A2, 0, sizeof A2);
    A1.rgb = C1;
    A2.rgb = C2;
    uint32_t a = zui_wm_open(&WM, "burrow", "Burrow", 0, &CL1);
    uint32_t b = zui_wm_open(&WM, "quillon", "Quillon", 0, &CL2);
    open_fully(a);
    open_fully(b);
    zui_wm_window_t *wa = zui_wm_get(&WM, a), *wb = zui_wm_get(&WM, b);
    CHECK(wb->r.x == wa->r.x + 32 && wb->r.y == wa->r.y + 32, "WM1: windows cascade");
    CHECK(zui_wm_front(&WM) == b && WM.mru[0] == b && WM.mru[1] == a, "the newest is in front");

    /* WM7: events go to the window under the pointer, relative to it */
    zui_rect_t ca = zui_wm_content(&WM, wa);
    CHECK(down(ca.x + 5, ca.y + 5) && zui_wm_front(&WM) == a, "a press raises the window");
    CHECK(A1.events == 1 && A1.last.x == 5 && A1.last.y == 5, "coordinates inside the content");
    CHECK(move(ca.x + 2000, ca.y + 7) && A1.events == 2 && A1.last.x == 2000,
          "a drag stays with the app that started it");
    up(ca.x + 9, ca.y + 9);
    CHECK(A1.events == 3 && A1.last.type == ZUI_EV_PTR_UP, "and so does letting go");
    CHECK(keyev(ZUI_EV_KEY_DOWN, 'a', 0) && A1.events == 4 && A1.last.key == 'a',
          "keys go to the window in front");
    zui_event_t t = ev(ZUI_EV_TEXT, 0, 0, now);
    strcpy(t.text, "é");
    CHECK(zui_wm_event(&WM, &t) && !strcmp(A1.last.text, "é"), "and text");

    /* WM3: move */
    zui_rect_t r0 = wa->r;
    int32_t tx = wa->r.x + 60, ty = wa->r.y + 10;
    CHECK(down(tx, ty), "the title");
    move(tx + 100, ty + 50);
    up(tx + 100, ty + 50);
    CHECK(wa->r.x == r0.x + 100 && wa->r.y == r0.y + 50 && wa->r.w == r0.w, "moved by the drag");
    down(wa->r.x + 60, wa->r.y + 10);
    move(wa->r.x + 60 - 5000, wa->r.y + 10 - 5000);
    up(0, 0);
    CHECK(wa->r.y == 0 && wa->r.x == 64 - wa->r.w, "the title bar stays on screen (%d,%d)", wa->r.x,
          wa->r.y);
    zui_wm_move(&WM, a, (zui_rect_t){100, 100, 600, 400});

    /* WM3: resize */
    int32_t rx = wa->r.x + wa->r.w - 2, ry = wa->r.y + wa->r.h - 2;
    CHECK(down(rx, ry), "the corner");
    move(rx + 40, ry + 30);
    up(rx + 40, ry + 30);
    CHECK(wa->r.w == 640 && wa->r.h == 430 && wa->r.x == 100, "the corner resizes: %dx%d", wa->r.w,
          wa->r.h);
    down(wa->r.x + 1, wa->r.y + 200);
    move(wa->r.x + 1 + 1000, wa->r.y + 200);
    up(0, 0);
    CHECK(wa->r.w == ZUI_WM_MIN_W && wa->r.x + wa->r.w == 740,
          "the left edge: never below the minimum, the right edge stays");
    down(wa->r.x + 100, wa->r.y - 3);
    move(wa->r.x + 100, wa->r.y - 3 - 50);
    up(0, 0);
    CHECK(wa->r.y == 50 && wa->r.h == 480, "the top edge, from just outside: %d %d", wa->r.y,
          wa->r.h);

    /* WM3: maximise, by double click and by button */
    zui_rect_t before = wa->r;
    int32_t mx = wa->r.x + 50, my = wa->r.y + 8;
    down(mx, my);
    up(mx, my);
    now += 100 - 1000; /* the next press comes 100 ms later */
    down(mx, my);
    up(mx, my);
    CHECK(wa->maximised && wa->r.w == W && wa->r.h == H - ZUI_WM_STRIP, "a double click fills");
    CHECK(!zui_wm_move(&WM, a, (zui_rect_t){0, 0, 400, 300}), "a full window does not move");
    uint32_t id;
    zui_rect_t mb = {wa->r.x + wa->r.w - 2 * ZUI_WM_TITLE, wa->r.y, ZUI_WM_TITLE, ZUI_WM_TITLE};
    CHECK(zui_wm_hit(&WM, mb.x + 5, mb.y + 5, &id, 0) == 3 && id == a, "the maximise button");
    down(mb.x + 5, mb.y + 5);
    up(0, 0);
    CHECK(!wa->maximised && !memcmp(&wa->r, &before, sizeof before), "and back where it was");

    /* minimise and close buttons */
    zui_rect_t nb = {wa->r.x + wa->r.w - 3 * ZUI_WM_TITLE, wa->r.y, ZUI_WM_TITLE, ZUI_WM_TITLE};
    down(nb.x + 5, nb.y + 5);
    up(0, 0);
    CHECK(wa->minimised && zui_wm_front(&WM) == b, "minimised: the next one is in front");
    memset(PX, 0, sizeof PX);
    zui_wm_draw(&WM, &S, 0);
    CHECK(at(wa->r.x + 50, wa->r.y + 100) != C1, "a minimised window is not drawn");

    /* WM6: the strip */
    uint32_t ids[4];
    CHECK(zui_wm_strip(&WM, ids, 4) == 2 && ids[0] == a && ids[1] == b, "in opening order");
    zui_rect_t s0 = zui_wm_strip_slot(&WM, 0, 2), s1 = zui_wm_strip_slot(&WM, 1, 2);
    CHECK(s0.x + s0.w < s1.x && s0.y >= H - ZUI_WM_STRIP, "slots side by side in the strip");
    CHECK(at(s1.x + s1.w / 2, s1.y + s1.h - 2) == ZUI_GOLD, "the window in front is marked gold");
    down(s0.x + 5, s0.y + 5);
    up(0, 0);
    CHECK(!wa->minimised && zui_wm_front(&WM) == a, "a press restores and raises");
    down(s0.x + 5, s0.y + 5);
    up(0, 0);
    CHECK(wa->minimised, "a press on the one in front minimises it");
    zui_wm_focus(&WM, a);

    /* WM5: the switcher */
    CHECK(WM.mru[0] == a && WM.mru[1] == b, "most recent first");
    CHECK(keyev(ZUI_EV_KEY_DOWN, ZUI_KEY_TAB, ZUI_MOD_CMD) && WM.switching && WM.pick == 1,
          "Cmd-Tab: the next one chosen");
    memset(PX, 0, sizeof PX);
    zui_wm_draw(&WM, &S, 0);
    bool gold = false;
    for (int32_t x = 0; x < W && !gold; x++) gold = at(x, (H - ZUI_WM_STRIP) / 2 - 20) == ZUI_GOLD;
    CHECK(gold, "the switcher shows the choice in gold");
    int before_events = A1.events;
    keyev(ZUI_EV_KEY_DOWN, 'x', ZUI_MOD_CMD);
    CHECK(A1.events == before_events, "the switcher has the keyboard");
    keyev(ZUI_EV_KEY_DOWN, ZUI_KEY_TAB, ZUI_MOD_CMD);
    CHECK(WM.pick == 0, "Tab wraps");
    keyev(ZUI_EV_KEY_DOWN, ZUI_KEY_TAB, ZUI_MOD_CMD | ZUI_MOD_SHIFT);
    CHECK(WM.pick == 1, "Shift-Tab goes back");
    keyev(ZUI_EV_KEY_UP, ZUI_KEY_TAB, ZUI_MOD_CMD);
    CHECK(WM.switching, "still held");
    keyev(ZUI_EV_KEY_UP, 0, 0);
    CHECK(!WM.switching && zui_wm_front(&WM) == b && WM.mru[0] == b, "letting go of Cmd switches");
    keyev(ZUI_EV_KEY_DOWN, ZUI_KEY_TAB, ZUI_MOD_CMD);
    keyev(ZUI_EV_KEY_DOWN, ZUI_KEY_ESCAPE, ZUI_MOD_CMD);
    CHECK(!WM.switching && zui_wm_front(&WM) == b, "Esc stays");
    keyev(ZUI_EV_KEY_DOWN, ZUI_KEY_TAB, ZUI_MOD_CMD);
    keyev(ZUI_EV_KEY_DOWN, ZUI_KEY_ENTER, ZUI_MOD_CMD);
    CHECK(zui_wm_front(&WM) == a, "Enter switches");
    keyev(ZUI_EV_KEY_DOWN, ZUI_KEY_TAB, ZUI_MOD_CMD);
    CHECK(down(5, 5) && !WM.switching && zui_wm_front(&WM) == a, "a press dismisses it");
    up(5, 5);
    zui_wm_minimise(&WM, a);
    keyev(ZUI_EV_KEY_DOWN, ZUI_KEY_TAB, ZUI_MOD_CMD);
    keyev(ZUI_EV_KEY_UP, 0, 0);
    CHECK(zui_wm_front(&WM) == a && !wa->minimised, "switching to a minimised app restores it");

    /* Cmd-M, Cmd-W */
    CHECK(keyev(ZUI_EV_KEY_DOWN, 'm', ZUI_MOD_CMD) && wa->minimised, "Cmd-M");
    zui_wm_focus(&WM, a);
    closes = 0;
    CHECK(keyev(ZUI_EV_KEY_DOWN, 'w', ZUI_MOD_CMD) && !zui_wm_get(&WM, a) && A1.closed == 1 &&
              closes == 1,
          "Cmd-W closes, and the app and shell are told");
    CHECK(zui_wm_front(&WM) == b && WM.nm == 1 && WM.nz == 1, "the other is in front");
    zui_rect_t cb = {wb->r.x + wb->r.w - ZUI_WM_TITLE, wb->r.y, ZUI_WM_TITLE, ZUI_WM_TITLE};
    down(cb.x + 5, cb.y + 5);
    up(0, 0);
    CHECK(!zui_wm_get(&WM, b) && A2.closed == 1 && zui_wm_front(&WM) == 0, "the close button");
    CHECK(keyev(ZUI_EV_KEY_DOWN, ZUI_KEY_TAB, ZUI_MOD_CMD) && !WM.switching,
          "nothing open: no switcher");
    CHECK(!zui_wm_close(&WM, b) && !zui_wm_focus(&WM, 999) && !zui_wm_get(&WM, 0), "bad ids");
}

static void test_limits(void)
{
    zui_wm_init(&WM, W, H, false);
    uint32_t ids[ZUI_WM_MAX];
    for (uint32_t i = 0; i < ZUI_WM_MAX; i++) ids[i] = zui_wm_open(&WM, "x", 0, 0, 0);
    CHECK(ids[ZUI_WM_MAX - 1u] && zui_wm_open(&WM, "x", 0, 0, 0) == 0, "at most ZUI_WM_MAX");
    CHECK(!strcmp(zui_wm_get(&WM, ids[0])->title, "x"), "no title: the app's name");
    zui_wm_close(&WM, ids[3]);
    CHECK(zui_wm_open(&WM, "y", "Y", 0, 0) != 0, "a closed slot is reused");
    char longname[100];
    memset(longname, 'n', sizeof longname - 1);
    longname[sizeof longname - 1] = 0;
    uint32_t l = zui_wm_open(&WM, longname, longname, 0, 0);
    CHECK(!l, "still full");
    zui_wm_close(&WM, ids[4]);
    l = zui_wm_open(&WM, longname, longname, 0, 0);
    CHECK(l && strlen(zui_wm_get(&WM, l)->title) == ZUI_WM_NAME_MAX - 1u, "long names are cut");
    for (uint32_t i = 0; i < ZUI_WM_MAX; i++) zui_wm_progress(&WM, ids[i], 128);
    memset(PX, 0, sizeof PX);
    zui_wm_draw(&WM, &S, 5); /* every window and the strip, without a crash */
    CHECK(at(W / 2, H - 2) != 0, "the strip is drawn");
}

static void test_phone(void)
{
    zui_wm_init(&WM, W, H, false);
    uint32_t a = zui_wm_open(&WM, "glint", "Glint", 0, &CL1);
    open_fully(a);
    zui_wm_event(&WM, &(zui_event_t){.type = ZUI_EV_RESIZE, .x = 390, .y = 800});
    zui_wm_window_t *w = zui_wm_get(&WM, a);
    zui_rect_t work = zui_wm_work(&WM);
    CHECK(WM.compact && !memcmp(&w->r, &work, sizeof work), "WM4: a phone fills the work area");
    CHECK(!zui_wm_move(&WM, a, (zui_rect_t){0, 0, 330, 300}) && !zui_wm_maximise(&WM, a, true),
          "and does not move or resize");
    uint32_t id;
    uint8_t edges = 0;
    CHECK(zui_wm_hit(&WM, 1, 300, &id, &edges) == 0 && !edges, "no edges to grab");
    CHECK(zui_wm_hit(&WM, 390 - ZUI_WM_TITLE - 5, 5, &id, 0) == 2, "minimise next to close");
    uint32_t b = zui_wm_open(&WM, "quillon", "Quillon", 0, &CL2);
    zui_wm_window_t *wb = zui_wm_get(&WM, b);
    CHECK(!memcmp(&wb->r, &work, sizeof work), "a new window fills it too");
    keyev(ZUI_EV_KEY_DOWN, ZUI_KEY_TAB, ZUI_MOD_CMD);
    keyev(ZUI_EV_KEY_UP, 0, 0);
    CHECK(zui_wm_front(&WM) == a, "the switcher works on a phone");
    zui_wm_event(&WM, &(zui_event_t){.type = ZUI_EV_RESIZE, .x = W, .y = H});
    CHECK(!WM.compact && w->r.w < W, "back on a laptop the window has its own size again");

    zui_wm_t T;
    zui_wm_init(&T, W, H, true);
    uint32_t t = zui_wm_open(&T, "x", 0, 0, 0);
    zui_wm_window_t *tw = zui_wm_get(&T, t);
    CHECK(zui_wm_content(&T, tw).y == tw->r.y + ZUI_WM_TITLE_T, "touch: a taller title bar");
}

/* A picture for people (not checked): ZXV_WM_SHOT names a PPM file and
 * ZXV_WM_FONT a TrueType file for the titles. */
static zui_glyph_cache_t CACHE;
static void lit_mark(zui_surface_t *s, zui_rect_t box, uint32_t lit, void *ctx)
{
    (void) ctx;
    int32_t d = box.w < box.h ? box.w : box.h;
    zui_rect_t r = {box.x + (box.w - d) / 2, box.y + (box.h - d) / 2, d, d};
    zui_fill_rrect(s, r, d / 2, zui_mix(ZUI_PANEL, ZUI_GOLD, lit > 255u ? 255u : lit), 255);
}
static void panel_paint(void *ctx, zui_surface_t *s, zui_rect_t c, uint32_t tick)
{
    (void) ctx, (void) tick;
    zui_fill(s, (zui_rect_t){c.x, c.y, 220, c.h}, ZUI_PANEL);
    zui_vline(s, c.x + 220, c.y, c.h, 1, ZUI_HAIRLINE);
}
static void picture(void)
{
    const char *out = getenv("ZXV_WM_SHOT"), *font = getenv("ZXV_WM_FONT");
    if (!out) return;
    static ttf_font_t F;
    static uint8_t data[4u << 20];
    static zui_text_t T;
    FILE *fp = font ? fopen(font, "rb") : 0;
    if (fp) {
        size_t n = fread(data, 1, sizeof data, fp);
        fclose(fp);
        if (ttf_parse(&F, data, (uint32_t) n)) {
            zui_glyph_cache_init(&CACHE);
            T = (zui_text_t){&F, &CACHE, 14, false};
        }
    }
    zui_wm_init(&WM, W, H, false);
    WM.text = T.font ? &T : 0;
    static const zui_wm_client_t P = {panel_paint, 0, 0, 0};
    zui_launch_t l;
    memset(&l, 0, sizeof l);
    l.mark = lit_mark;
    l.n = 3;
    for (uint32_t i = 0; i < 3; i++) l.parts[i] = lit_mark;
    uint32_t a = zui_wm_open(&WM, "burrow", "Burrow", &l, &P);
    uint32_t b = zui_wm_open(&WM, "quillon", "Quillon \xC2\xB7 notes/todo.txt", &l, &P);
    uint32_t c = zui_wm_open(&WM, "murmuration", "Murmuration", &l, &P);
    open_fully(a);
    open_fully(b);
    zui_wm_move(&WM, a, (zui_rect_t){40, 40, 700, 480});
    zui_wm_move(&WM, b, (zui_rect_t){300, 160, 640, 440});
    zui_wm_move(&WM, c, (zui_rect_t){560, 60, 680, 520});
    zui_wm_progress(&WM, c, 150);
    zui_wm_draw(&WM, &S, 40);
    FILE *f = fopen(out, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int32_t i = 0; i < W * H; i++) {
        unsigned char rgb[3] = {(unsigned char) (PX[i] >> 16), (unsigned char) (PX[i] >> 8),
                                (unsigned char) PX[i]};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

int main(void)
{
    zui_surface_init(&S, PX, W, H, W);
    test_launch();
    test_windows();
    test_limits();
    test_phone();
    picture();
    printf("%s test_zui_wm: %d/%d checks\n", fails ? "[FAIL]" : "[PASS]", checks - fails, checks);
    return fails ? 1 : 0;
}
