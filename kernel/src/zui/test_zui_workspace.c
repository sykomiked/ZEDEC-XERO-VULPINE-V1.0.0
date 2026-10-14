/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zui_workspace.c — saved workspaces, the panel, shortcuts, themes and
 * comfort settings (zui_workspace.h WS1..WS9). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zui_tokens.h"
#include "zui_workspace.h"

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

static zui_settings_t ST, ST2;
static zui_wm_t WM, WM2;
static zui_workspace_t WS, WS2;
static uint8_t SEAT, ANA, BO;
static char buf[32768], buf2[32768];

static bool req(zui_rect_t a, zui_rect_t b)
{
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

static bool inside(zui_rect_t r, zui_rect_t a)
{
    return r.x >= a.x && r.y >= a.y && r.x + r.w <= a.x + a.w && r.y + r.h <= a.y + a.h;
}

static void fresh(void)
{
    zui_set_init(&ST);
    SEAT = zui_set_seat(&ST, "seat0");
    ANA = zui_set_user(&ST, "ana");
    BO = zui_set_user(&ST, "bo");
    zui_ws_init(&WS, &ST, (zui_set_view_t){SEAT, ANA, 0});
}

static void test_schema(void)
{
    zui_set_init(&ST);
    CHECK(zui_ws_schema(&ST) == 0, "schema");
    uint32_t n = ST.nkeys;
    CHECK(n == ZUI_WS_SAVED * (5u + ZUI_WS_WINS * 6u) + 3u + ZUI_WS_PINNED + ZUI_ACT_COUNT + 6u,
          "every key (%u)", n);
    CHECK(zui_ws_schema(&ST) == 0 && ST.nkeys == n, "a second call adds nothing");
    const char *keys[] = {"workspace.0.name",     "workspace.3.w7.state", "workspace.2.front",
                          "panel.position",       "panel.size",           "panel.autohide",
                          "panel.pin.0",          "panel.pin.7",          "shortcut.switch-next",
                          "shortcut.lock-now",    "shortcut.quit-app",    "theme.scheme",
                          "theme.accent",         "theme.focus",          "theme.text-scale",
                          "theme.reduced-motion", "theme.density"};
    int all = 1;
    for (uint32_t i = 0; i < sizeof keys / sizeof keys[0]; i++)
        if (zui_set_key(&ST, keys[i]) < 0) {
            all = 0;
            printf("  missing %s\n", keys[i]);
        }
    CHECK(all, "named keys exist");
    CHECK(zui_set_key(&ST, "workspace.4.name") < 0 && zui_set_key(&ST, "workspace.0.w8.x") < 0 &&
              zui_set_key(&ST, "panel.pin.8") < 0,
          "nothing past the caps");
    CHECK(zui_set_int(&ST, (zui_set_view_t){0, 0, 0}, "panel.size") == ZUI_WM_STRIP,
          "the panel is the wm strip's size by default");
    /* a store already full refuses the schema */
    zui_set_init(&ST2);
    char k[16];
    for (uint32_t i = 0; i < ZUI_SET_KEYS - 10u; i++) {
        snprintf(k, sizeof k, "x.%u", i);
        zui_set_def_bool(&ST2, k, false);
    }
    CHECK(zui_ws_schema(&ST2) == ZUI_SET_E_FULL, "no room for the schema");
    CHECK(zui_ws_init(&WS2, &ST2, (zui_set_view_t){0, 0, 0}) == ZUI_SET_E_FULL, "init says so");
    /* where writes go */
    fresh();
    CHECK(WS.layer == ZUI_LAYER_USER && WS.scope.user == ANA && !WS.scope.seat && !WS.scope.app,
          "a user's workspace writes to the user layer");
    zui_ws_init(&WS2, &ST, (zui_set_view_t){SEAT, 0, 0});
    CHECK(WS2.layer == ZUI_LAYER_SEAT && WS2.scope.seat == SEAT && !WS2.scope.user,
          "the login screen writes to the seat");
    zui_ws_init(&WS2, &ST, (zui_set_view_t){0, 0, 0});
    CHECK(WS2.layer == ZUI_LAYER_DEVICE && !WS2.scope.seat, "nobody: the device");
}

/* ---- WS2, WS3 ---------------------------------------------------------- */

static int open_calls;
static const char *skip_app;
static int closed_seen;
static void cl_closed(void *ctx)
{
    (void) ctx;
    closed_seen++;
}
static bool opener(void *ctx, const char *app, zui_wm_client_t *c, zui_launch_t *l)
{
    (void) l;
    open_calls++;
    if (skip_app && strcmp(app, skip_app) == 0) return false;
    c->closed = cl_closed;
    c->ctx = ctx;
    return true;
}

static uint32_t A, B, C, D;

static void layout(zui_wm_t *wm)
{
    zui_wm_init(wm, 1280, 800, false);
    A = zui_wm_open(wm, "burrow", "Burrow", 0, 0);
    B = zui_wm_open(wm, "quillon", "Quillon", 0, 0);
    C = zui_wm_open(wm, "murmur", "Murmur", 0, 0);
    D = zui_wm_open(wm, "far", "Far", 0, 0);
    zui_wm_move(wm, A, (zui_rect_t){40, 40, 700, 480});
    zui_wm_move(wm, B, (zui_rect_t){300, 160, 640, 440});
    zui_wm_move(wm, C, (zui_rect_t){560, 60, 680, 520});
    zui_wm_move(wm, D, (zui_rect_t){1000, 300, 600, 300});
    zui_wm_maximise(wm, B, true);
    zui_wm_minimise(wm, C);
    zui_wm_focus(wm, A);
}

static uint32_t strip_ids[ZUI_WM_MAX];
static zui_wm_window_t *nth(zui_wm_t *wm, uint32_t i)
{
    zui_wm_strip(wm, strip_ids, ZUI_WM_MAX);
    return zui_wm_get(wm, strip_ids[i]);
}

static void test_save_restore(void)
{
    fresh();
    layout(&WM);
    zui_set_view_t va = WS.view;
    CHECK(WM.win[3].r.x == 1000 && WM.win[3].r.w == 600, "far hangs off the right edge");
    CHECK(zui_wm_front(&WM) == A, "burrow in front");
    CHECK(zui_ws_save(&WS, &WM, "work") == 4, "four saved");
    CHECK(zui_set_int(&ST, va, "workspace.0.n") == 4, "n");
    CHECK(zui_set_int(&ST, va, "workspace.0.front") == 0, "front is the first");
    CHECK(zui_set_int(&ST, va, "workspace.0.sw") == 1280 &&
              zui_set_int(&ST, va, "workspace.0.sh") == 800 - ZUI_WM_STRIP,
          "the work area");
    char b[ZUI_SET_STR_MAX];
    zui_set_str(&ST, va, "workspace.0.name", b, sizeof b);
    CHECK(strcmp(b, "work") == 0, "name");
    zui_set_str(&ST, va, "workspace.0.w1.app", b, sizeof b);
    CHECK(strcmp(b, "quillon") == 0, "opening order");
    CHECK(zui_set_int(&ST, va, "workspace.0.w1.x") == 300 &&
              zui_set_int(&ST, va, "workspace.0.w1.w") == 640 &&
              zui_set_int(&ST, va, "workspace.0.w1.y") == 160 &&
              zui_set_int(&ST, va, "workspace.0.w1.h") == 440,
          "a maximised window keeps its frame to go back to");
    CHECK(zui_set_int(&ST, va, "workspace.0.w1.state") == 1, "maximised");
    CHECK(zui_set_int(&ST, va, "workspace.0.w2.state") == 2, "minimised");
    CHECK(zui_set_int(&ST, va, "workspace.0.w0.state") == 0, "normal");
    zui_set_value_t pv;
    CHECK(zui_set_peek(&ST, ZUI_LAYER_USER, WS.scope, "workspace.0.w4.app", &pv, 0) ==
              ZUI_SET_E_NOKEY,
          "unused windows store nothing");
    /* same screen: the same frames */
    zui_wm_init(&WM2, 1280, 800, false);
    open_calls = 0;
    CHECK(zui_ws_restore(&WS, &WM2, "work", opener, 0) == 4, "four opened");
    CHECK(open_calls == 4, "the callback asked for each");
    zui_wm_window_t *w0 = nth(&WM2, 0), *w1 = nth(&WM2, 1), *w2 = nth(&WM2, 2), *w3 = nth(&WM2, 3);
    CHECK(strcmp(w0->app, "burrow") == 0 && strcmp(w1->app, "quillon") == 0 &&
              strcmp(w2->app, "murmur") == 0 && strcmp(w3->app, "far") == 0,
          "in order");
    CHECK(req(w0->r, (zui_rect_t){40, 40, 700, 480}), "burrow's frame (%d %d %d %d)", w0->r.x,
          w0->r.y, w0->r.w, w0->r.h);
    CHECK(w1->maximised && req(w1->r, zui_wm_work(&WM2)), "quillon maximised");
    CHECK(req(w1->restore, (zui_rect_t){300, 160, 640, 440}), "and its frame to go back to");
    CHECK(w2->minimised && !w0->minimised && !w1->minimised && !w3->minimised, "murmur minimised");
    CHECK(req(w2->r, (zui_rect_t){560, 60, 680, 520}), "a minimised window keeps its frame");
    CHECK(req(w3->r, (zui_rect_t){680, 300, 600, 300}), "far pulled inside (%d)", w3->r.x);
    CHECK(zui_wm_front(&WM2) == w0->id, "burrow in front again");
    CHECK(w0->client.closed == cl_closed, "the callback's client");
    zui_wm_close(&WM2, w0->id);
    CHECK(closed_seen == 1, "the client is live");
    /* a screen twice the size: twice the frames */
    zui_wm_init(&WM2, 2560, 1504 + ZUI_WM_STRIP, false);
    CHECK(zui_ws_restore(&WS, &WM2, "work", 0, 0) == 4, "no callback is fine");
    w0 = nth(&WM2, 0), w1 = nth(&WM2, 1), w2 = nth(&WM2, 2), w3 = nth(&WM2, 3);
    CHECK(req(w0->r, (zui_rect_t){80, 80, 1400, 960}), "doubled (%d %d %d %d)", w0->r.x, w0->r.y,
          w0->r.w, w0->r.h);
    CHECK(req(w1->restore, (zui_rect_t){600, 320, 1280, 880}), "doubled restore");
    CHECK(req(w2->r, (zui_rect_t){1120, 120, 1360, 1040}), "doubled minimised");
    CHECK(req(w3->r, (zui_rect_t){1360, 600, 1200, 600}), "doubled and clamped (%d)", w3->r.x);
    CHECK(zui_wm_front(&WM2) == w0->id, "front");
    /* smaller: scaled, clamped, wholly inside */
    zui_wm_init(&WM2, 800, 600, false);
    CHECK(zui_ws_restore(&WS, &WM2, "work", 0, 0) == 4, "restore small");
    w0 = nth(&WM2, 0), w3 = nth(&WM2, 3);
    zui_rect_t wa = zui_wm_work(&WM2);
    CHECK(req(w0->r, (zui_rect_t){25, 29, ZUI_WM_MIN_W > 437 ? ZUI_WM_MIN_W : 437, 352}),
          "scaled down (%d %d %d %d)", w0->r.x, w0->r.y, w0->r.w, w0->r.h);
    CHECK(w3->r.w == 375 && w3->r.h == 220 && w3->r.x == 800 - 375 && w3->r.y == 220,
          "small, at least the minimum, inside (%d %d %d)", w3->r.x, w3->r.w, w3->r.h);
    int all = 1;
    static const int32_t SIZES[][2] = {{640, 480},   {800, 600},   {1024, 768}, {1366, 768},
                                       {1920, 1080}, {3840, 2160}, {600, 300},  {700, 4000}};
    for (uint32_t s = 0; s < sizeof SIZES / sizeof SIZES[0]; s++) {
        zui_wm_init(&WM2, SIZES[s][0], SIZES[s][1], false);
        zui_ws_restore(&WS, &WM2, "work", 0, 0);
        wa = zui_wm_work(&WM2);
        for (uint32_t i = 0; i < 4; i++) {
            zui_wm_window_t *w = nth(&WM2, i);
            zui_rect_t r = w->maximised ? w->restore : w->r;
            if (!inside(r, wa) || r.w < (ZUI_WM_MIN_W < wa.w ? ZUI_WM_MIN_W : wa.w)) {
                all = 0;
                printf("  %dx%d window %u: %d %d %d %d\n", SIZES[s][0], SIZES[s][1], i, r.x, r.y,
                       r.w, r.h);
            }
        }
        if (zui_wm_front(&WM2) != nth(&WM2, 0)->id) all = 0;
    }
    CHECK(all, "every screen size: every frame inside the work area, front kept");
    /* a phone: in order, nothing moved */
    zui_wm_init(&WM2, 400, 800, true);
    CHECK(WM2.compact, "compact");
    CHECK(zui_ws_restore(&WS, &WM2, "work", 0, 0) == 4, "four on the phone");
    wa = zui_wm_work(&WM2);
    all = 1;
    for (uint32_t i = 0; i < 4; i++) {
        zui_wm_window_t *w = nth(&WM2, i);
        if (!req(w->r, wa) || w->maximised || w->minimised) all = 0;
    }
    CHECK(all, "each fills the work area, none maximised or minimised");
    CHECK(strcmp(nth(&WM2, 3)->app, "far") == 0, "opening order kept");
    CHECK(zui_wm_front(&WM2) == nth(&WM2, 0)->id, "the front one raised");
    /* a skipped app */
    zui_wm_init(&WM2, 1280, 800, false);
    skip_app = "quillon";
    open_calls = 0;
    CHECK(zui_ws_restore(&WS, &WM2, "work", opener, 0) == 3, "one gone");
    CHECK(open_calls == 4 && strcmp(nth(&WM2, 1)->app, "murmur") == 0, "the rest in order");
    skip_app = "burrow";
    zui_wm_init(&WM2, 1280, 800, false);
    CHECK(zui_ws_restore(&WS, &WM2, "work", opener, 0) == 3, "the front app gone");
    CHECK(zui_wm_front(&WM2) == nth(&WM2, 2)->id, "then the last opened, not minimised, is front");
    skip_app = 0;
    /* a full manager */
    zui_wm_init(&WM2, 1280, 800, false);
    for (uint32_t i = 0; i < ZUI_WM_MAX - 2u; i++) zui_wm_open(&WM2, "x", 0, 0, 0);
    CHECK(zui_ws_restore(&WS, &WM2, "work", 0, 0) == 2, "only two fit");
    CHECK(zui_ws_restore(&WS, &WM2, "nope", 0, 0) == ZUI_WS_E_NOTFOUND, "unknown name");
    CHECK(zui_ws_restore(&WS, &WM2, 0, 0, 0) == ZUI_WS_E_NOTFOUND, "no name");
    /* more windows than a workspace keeps */
    zui_wm_init(&WM2, 1280, 800, false);
    for (uint32_t i = 0; i < 10; i++) {
        char nm[8];
        snprintf(nm, sizeof nm, "app%u", i);
        zui_wm_open(&WM2, nm, 0, 0, 0);
    }
    CHECK(zui_ws_save(&WS, &WM2, "busy") == (int) ZUI_WS_WINS, "the first eight kept");
    zui_set_str(&ST, va, "workspace.1.w7.app", b, sizeof b);
    CHECK(strcmp(b, "app7") == 0, "the eighth");
    CHECK(zui_set_int(&ST, va, "workspace.1.front") == -1, "front past the eighth: none");
    /* names, replace, full, forget */
    zui_wm_init(&WM2, 1280, 800, false);
    zui_wm_open(&WM2, "solo", 0, 0, 0);
    CHECK(zui_ws_save(&WS, &WM2, "work") == 1, "replace work");
    CHECK(zui_set_int(&ST, va, "workspace.0.n") == 1, "in its own slot");
    CHECK(zui_set_peek(&ST, ZUI_LAYER_USER, WS.scope, "workspace.0.w3.app", &pv, 0) ==
              ZUI_SET_E_NOKEY,
          "the old windows dropped");
    CHECK(zui_ws_save(&WS, &WM2, "c") == 1 && zui_ws_save(&WS, &WM2, "d") == 1, "c, d");
    CHECK(zui_ws_save(&WS, &WM2, "e") == ZUI_SET_E_FULL, "a fifth name");
    CHECK(zui_ws_save(&WS, &WM2, "") == ZUI_SET_E_RANGE, "empty name");
    CHECK(zui_ws_save(&WS, &WM2, "a\nb") == ZUI_SET_E_RANGE, "control in a name");
    CHECK(zui_ws_save(&WS, &WM2, 0) == ZUI_SET_E_RANGE, "null name");
    char names[ZUI_WS_SAVED][ZUI_SET_STR_MAX];
    CHECK(zui_ws_names(&WS, names, ZUI_WS_SAVED) == 4 && strcmp(names[0], "work") == 0 &&
              strcmp(names[1], "busy") == 0 && strcmp(names[3], "d") == 0,
          "names in slot order");
    CHECK(zui_ws_names(&WS, names, 1) == 4, "count past cap");
    CHECK(zui_ws_forget(&WS, "busy") == 0, "forget");
    CHECK(zui_ws_names(&WS, names, ZUI_WS_SAVED) == 3 && strcmp(names[1], "c") == 0, "gone");
    CHECK(zui_ws_forget(&WS, "busy") == ZUI_WS_E_NOTFOUND, "forget twice");
    CHECK(zui_ws_save(&WS, &WM2, "e") == 1 && zui_set_int(&ST, va, "workspace.1.n") == 1,
          "the freed slot reused");
    /* another person has their own */
    zui_ws_init(&WS2, &ST, (zui_set_view_t){SEAT, BO, 0});
    CHECK(zui_ws_names(&WS2, names, ZUI_WS_SAVED) == 0, "bo has none");
    CHECK(zui_ws_restore(&WS2, &WM2, "work", 0, 0) == ZUI_WS_E_NOTFOUND, "not ana's");
    /* a device-wide workspace shows for everyone and cannot be forgotten by a user */
    zui_workspace_t dev;
    zui_ws_init(&dev, &ST, (zui_set_view_t){0, 0, 0});
    CHECK(zui_ws_save(&dev, &WM2, "kiosk") == 1, "device workspace");
    CHECK(zui_ws_names(&WS2, names, ZUI_WS_SAVED) == 1 && strcmp(names[0], "kiosk") == 0,
          "bo sees it");
    CHECK(zui_ws_forget(&WS2, "kiosk") == ZUI_SET_E_LOCKED, "bo cannot forget it");
}

/* ---- WS4 --------------------------------------------------------------- */

static void test_panel(void)
{
    fresh();
    zui_ws_panel_t p;
    zui_ws_panel(&WS, &p);
    CHECK(p.pos == ZUI_PANEL_BOTTOM && p.size == ZUI_WM_STRIP && !p.autohide && p.npinned == 0,
          "defaults");
    CHECK(zui_ws_set_panel(&WS, ZUI_PANEL_LEFT, 64, true) == 0, "set");
    zui_ws_panel(&WS, &p);
    CHECK(p.pos == ZUI_PANEL_LEFT && p.size == 64 && p.autohide, "read back");
    CHECK(zui_ws_set_panel(&WS, ZUI_PANEL_TOP, 66, false) == ZUI_SET_E_RANGE, "off step");
    CHECK(zui_ws_set_panel(&WS, ZUI_PANEL_TOP, 28, false) == ZUI_SET_E_RANGE, "too small");
    CHECK(zui_ws_set_panel(&WS, ZUI_PANEL_TOP, 132, false) == ZUI_SET_E_RANGE, "too big");
    CHECK(zui_ws_set_panel(&WS, (zui_panel_pos_t) 4, 64, false) == ZUI_SET_E_RANGE, "no edge");
    zui_ws_panel(&WS, &p);
    CHECK(p.pos == ZUI_PANEL_LEFT && p.size == 64 && p.autohide, "refusals changed nothing");
    CHECK(zui_ws_set_panel(&WS, ZUI_PANEL_TOP, ZUI_WS_PANEL_MIN, false) == 0, "min");
    CHECK(zui_ws_set_panel(&WS, ZUI_PANEL_TOP, ZUI_WS_PANEL_MAX, false) == 0, "max");
    /* rectangles */
    zui_ws_set_panel(&WS, ZUI_PANEL_BOTTOM, 48, false);
    CHECK(req(zui_ws_panel_rect(&WS, 1280, 800), (zui_rect_t){0, 752, 1280, 48}), "bottom");
    CHECK(req(zui_ws_work(&WS, 1280, 800), (zui_rect_t){0, 0, 1280, 752}), "work above");
    zui_ws_set_panel(&WS, ZUI_PANEL_LEFT, 64, false);
    CHECK(req(zui_ws_panel_rect(&WS, 1280, 800), (zui_rect_t){0, 0, 64, 800}), "left");
    CHECK(req(zui_ws_work(&WS, 1280, 800), (zui_rect_t){64, 0, 1216, 800}), "work right of it");
    zui_ws_set_panel(&WS, ZUI_PANEL_RIGHT, 72, false);
    CHECK(req(zui_ws_panel_rect(&WS, 1280, 800), (zui_rect_t){1208, 0, 72, 800}), "right");
    CHECK(req(zui_ws_work(&WS, 1280, 800), (zui_rect_t){0, 0, 1208, 800}), "work left of it");
    zui_ws_set_panel(&WS, ZUI_PANEL_TOP, 40, false);
    CHECK(req(zui_ws_panel_rect(&WS, 1280, 800), (zui_rect_t){0, 0, 1280, 40}), "top");
    CHECK(req(zui_ws_work(&WS, 1280, 800), (zui_rect_t){0, 40, 1280, 760}), "work below");
    zui_ws_set_panel(&WS, ZUI_PANEL_TOP, 40, true);
    CHECK(req(zui_ws_panel_rect(&WS, 1280, 800), (zui_rect_t){0, 0, 0, 0}), "auto-hidden");
    CHECK(req(zui_ws_work(&WS, 1280, 800), (zui_rect_t){0, 0, 1280, 800}), "all the screen");
    /* pins */
    CHECK(zui_ws_pin(&WS, "burrow", 0) == 0, "pin");
    CHECK(zui_ws_pin(&WS, "quillon", 99) == 0, "pin last");
    CHECK(zui_ws_pin(&WS, "murmur", 1) == 0, "pin between");
    zui_ws_panel(&WS, &p);
    CHECK(p.npinned == 3 && strcmp(p.pinned[0], "burrow") == 0 &&
              strcmp(p.pinned[1], "murmur") == 0 && strcmp(p.pinned[2], "quillon") == 0,
          "in the chosen order");
    CHECK(zui_ws_pin(&WS, "quillon", 0) == 0, "pinning again moves it");
    zui_ws_panel(&WS, &p);
    CHECK(p.npinned == 3 && strcmp(p.pinned[0], "quillon") == 0 &&
              strcmp(p.pinned[1], "burrow") == 0 && strcmp(p.pinned[2], "murmur") == 0,
          "moved to the front, no twice");
    CHECK(zui_ws_pin(&WS, "burrow", 99) == 0, "move to the end");
    zui_ws_panel(&WS, &p);
    CHECK(strcmp(p.pinned[2], "burrow") == 0 && strcmp(p.pinned[1], "murmur") == 0, "at the end");
    CHECK(zui_ws_unpin(&WS, "murmur") == 0, "unpin");
    zui_ws_panel(&WS, &p);
    CHECK(p.npinned == 2 && strcmp(p.pinned[0], "quillon") == 0 &&
              strcmp(p.pinned[1], "burrow") == 0,
          "closed up");
    CHECK(zui_ws_unpin(&WS, "murmur") == ZUI_WS_E_NOTFOUND, "not pinned");
    CHECK(zui_ws_unpin(&WS, 0) == ZUI_WS_E_NOTFOUND, "null");
    CHECK(zui_ws_pin(&WS, "", 0) == ZUI_SET_E_RANGE, "empty app");
    zui_set_set_str(&ST, ZUI_LAYER_USER, WS.scope, "panel.pin.5", "quillon");
    zui_ws_panel(&WS, &p);
    CHECK(p.npinned == 2, "a pin twice by hand shows once");
    zui_set_set_str(&ST, ZUI_LAYER_USER, WS.scope, "panel.pin.5", "");
    for (uint32_t i = 0; i < ZUI_WS_PINNED - 2u; i++) {
        char nm[8];
        snprintf(nm, sizeof nm, "p%u", i);
        zui_ws_pin(&WS, nm, 99);
    }
    zui_ws_panel(&WS, &p);
    CHECK(p.npinned == ZUI_WS_PINNED, "full");
    CHECK(zui_ws_pin(&WS, "more", 0) == ZUI_SET_E_FULL, "one too many");
    CHECK(zui_ws_pin(&WS, "p0", 0) == 0, "a move when full is fine");
    zui_ws_panel(&WS, &p);
    CHECK(strcmp(p.pinned[0], "p0") == 0 && p.npinned == ZUI_WS_PINNED, "moved");
    /* device default pins; a person's list replaces them; reset brings them back */
    zui_set_set_str(&ST, ZUI_LAYER_DEVICE, (zui_set_view_t){0, 0, 0}, "panel.pin.0", "files");
    zui_set_set_str(&ST, ZUI_LAYER_DEVICE, (zui_set_view_t){0, 0, 0}, "panel.pin.1", "help");
    zui_ws_panel(&WS, &p);
    CHECK(p.npinned == ZUI_WS_PINNED && strcmp(p.pinned[0], "p0") == 0, "ana's list wins");
    CHECK(zui_ws_pins_reset(&WS) == 0, "reset");
    zui_ws_panel(&WS, &p);
    CHECK(p.npinned == 2 && strcmp(p.pinned[0], "files") == 0 && strcmp(p.pinned[1], "help") == 0,
          "the device's pins");
    CHECK(zui_ws_unpin(&WS, "files") == 0, "unpin a device pin");
    zui_ws_panel(&WS, &p);
    CHECK(p.npinned == 1 && strcmp(p.pinned[0], "help") == 0, "for ana only");
    zui_ws_init(&WS2, &ST, (zui_set_view_t){SEAT, BO, 0});
    zui_ws_panel(&WS2, &p);
    CHECK(p.npinned == 2, "bo still has both");
    /* a panel size locked by the device */
    zui_set_set_int(&ST, ZUI_LAYER_DEVICE, (zui_set_view_t){0, 0, 0}, "panel.size", 96);
    zui_set_lock(&ST, ZUI_LAYER_DEVICE, (zui_set_view_t){0, 0, 0}, "panel.size", true);
    CHECK(zui_ws_set_panel(&WS, ZUI_PANEL_BOTTOM, 48, false) == ZUI_SET_E_LOCKED, "locked");
    zui_ws_panel(&WS, &p);
    CHECK(p.size == 96, "the device's size");
}

/* ---- WS5, WS6 ---------------------------------------------------------- */

static zui_event_t key(uint16_t k, uint8_t mods)
{
    zui_event_t e;
    memset(&e, 0, sizeof e);
    e.type = ZUI_EV_KEY_DOWN;
    e.key = k;
    e.mods = mods;
    return e;
}

static void test_chords(void)
{
    zui_chord_t c;
    char b[ZUI_WS_CHORD_MAX];
    CHECK(zui_chord_parse("cmd+tab", &c) && c.mods == ZUI_MOD_CMD && c.key == ZUI_KEY_TAB, "tab");
    CHECK(zui_chord_parse("shift+cmd+l", &c) && c.mods == (ZUI_MOD_CMD | ZUI_MOD_SHIFT) &&
              c.key == 'l',
          "any order");
    CHECK(zui_chord_format(c, b, sizeof b) == 11 && strcmp(b, "cmd+shift+l") == 0, "canonical (%s)",
          b);
    const char *round[] = {"cmd+tab",    "cmd+ctrl+alt+shift+meta+backspace",
                           "f1",         "shift+f12",
                           "alt+f10",    "ctrl+alt+delete",
                           "cmd+,",      "cmd+\\",
                           "meta+space", "esc",
                           "cmd+0",      "cmd+pagedown",
                           "back",       "menu",
                           "alt+left",   "ctrl+="};
    int all = 1;
    for (uint32_t i = 0; i < sizeof round / sizeof round[0]; i++) {
        if (!zui_chord_parse(round[i], &c) || zui_chord_format(c, b, sizeof b) < 0 ||
            strcmp(b, round[i]) != 0) {
            all = 0;
            printf("  round %s -> %s\n", round[i], b);
        }
    }
    CHECK(all, "canonical chords round-trip");
    CHECK(zui_chord_parse("f1", &c) && c.key == ZUI_KEY_F1 && c.mods == 0, "f1 alone is fine");
    CHECK(zui_chord_parse("f12", &c) && c.key == ZUI_KEY_F12, "f12");
    const char *bad[] = {"cmd+",   "+a",     "cmd++a",   "a",     "shift+a",   "space",
                         "f13",    "f0",     "f01",      "cmd+A", "cmd+cmd+a", "hyper+a",
                         "cmd+ab", "cmd+fx", "cmd+tabs", "Cmd+a", "cmd+é"};
    all = 1;
    for (uint32_t i = 0; i < sizeof bad / sizeof bad[0]; i++)
        if (zui_chord_parse(bad[i], &c)) {
            all = 0;
            printf("  accepted %s\n", bad[i]);
        }
    CHECK(all, "bad chords refused (bare text keys, unknown names, doubles)");
    CHECK(!zui_chord_parse(0, &c), "null");
    CHECK(zui_chord_parse("", &c) && c.key == 0 && c.mods == 0, "empty is unbound");
    c = (zui_chord_t){0, 0};
    CHECK(zui_chord_format(c, b, sizeof b) == 0 && b[0] == 0, "unbound formats empty");
    c = (zui_chord_t){ZUI_MOD_CMD, 'w'};
    CHECK(zui_chord_format(c, b, 5) == ZUI_SET_E_SPACE, "no room");
    CHECK(zui_chord_format(c, b, 6) == 5, "just room");
    CHECK(zui_ws_action("lock-now") == ZUI_ACT_LOCK_NOW && zui_ws_action("nope") == -1 &&
              zui_ws_action(0) == -1,
          "action names");
    CHECK(strcmp(zui_ws_action_name(ZUI_ACT_SWITCH_NEXT), "switch-next") == 0 &&
              strcmp(zui_ws_action_name(ZUI_ACT_COUNT), "?") == 0,
          "names back");
}

static void test_shortcuts(void)
{
    fresh();
    zui_chord_t c = zui_ws_binding(&WS, ZUI_ACT_SWITCH_NEXT);
    CHECK(c.mods == ZUI_MOD_CMD && c.key == ZUI_KEY_TAB, "cmd+tab by default");
    c = zui_ws_binding(&WS, ZUI_ACT_CLOSE);
    CHECK(c.mods == ZUI_MOD_CMD && c.key == 'w', "cmd+w");
    c = zui_ws_binding(&WS, ZUI_ACT_MINIMISE);
    CHECK(c.mods == ZUI_MOD_CMD && c.key == 'm', "cmd+m");
    c = zui_ws_binding(&WS, ZUI_ACT_LOCK_NOW);
    CHECK(c.mods == (ZUI_MOD_CMD | ZUI_MOD_SHIFT) && c.key == 'l', "lock now: cmd+shift+l");
    c = zui_ws_binding(&WS, ZUI_ACT_ASK);
    CHECK(c.mods == ZUI_MOD_CMD && c.key == 'k', "Ask: cmd+k");
    CHECK(!zui_ws_conflict(&WS, 0, 0), "defaults do not conflict");
    c = zui_ws_binding(&WS, ZUI_ACT_COUNT);
    CHECK(c.key == 0, "no action, no chord");
    /* matching */
    zui_event_t e = key('w', ZUI_MOD_CMD | ZUI_MOD_CTRL);
    CHECK(zui_ws_match(&WS, &e) == ZUI_ACT_CLOSE, "ctrl-w (cmd)");
    e = key('W', ZUI_MOD_CMD | ZUI_MOD_CTRL);
    CHECK(zui_ws_match(&WS, &e) == ZUI_ACT_CLOSE, "either case");
    e = key('w', ZUI_MOD_CTRL);
    CHECK(zui_ws_match(&WS, &e) == -1, "ctrl without cmd: not cmd");
    e = key('w', ZUI_MOD_CMD | ZUI_MOD_META);
    CHECK(zui_ws_match(&WS, &e) == -1, "meta is not ctrl where cmd is ctrl");
    WS.cmd_is_meta = true;
    CHECK(zui_ws_match(&WS, &e) == ZUI_ACT_CLOSE, "cmd-w where cmd is meta");
    e = key('f', ZUI_MOD_CMD | ZUI_MOD_META | ZUI_MOD_CTRL);
    CHECK(zui_ws_match(&WS, &e) == ZUI_ACT_MAXIMISE, "cmd+ctrl+f where cmd is meta");
    WS.cmd_is_meta = false;
    CHECK(zui_ws_match(&WS, &e) == -1, "where cmd is ctrl, that is cmd+meta+f");
    e = key(ZUI_KEY_TAB, ZUI_MOD_CMD | ZUI_MOD_CTRL | ZUI_MOD_SHIFT);
    CHECK(zui_ws_match(&WS, &e) == ZUI_ACT_SWITCH_PREV, "cmd+shift+tab");
    e = key('l', ZUI_MOD_CMD | ZUI_MOD_CTRL | ZUI_MOD_SHIFT);
    CHECK(zui_ws_match(&WS, &e) == ZUI_ACT_LOCK_NOW, "lock now");
    e = key('l', ZUI_MOD_CMD | ZUI_MOD_CTRL);
    CHECK(zui_ws_match(&WS, &e) == -1, "cmd+l is not lock now");
    e = key('w', ZUI_MOD_CMD | ZUI_MOD_CTRL);
    e.type = ZUI_EV_KEY_UP;
    CHECK(zui_ws_match(&WS, &e) == -1, "key up");
    CHECK(zui_ws_match(&WS, 0) == -1, "null");
    /* rebinding */
    int h = -1;
    CHECK(zui_ws_bind(&WS, ZUI_ACT_ASK, "cmd+w", &h) == ZUI_WS_E_CONFLICT && h == ZUI_ACT_CLOSE,
          "taken: names the holder");
    CHECK(zui_ws_binding(&WS, ZUI_ACT_ASK).key == 'k', "unchanged");
    CHECK(zui_ws_bind(&WS, ZUI_ACT_ASK, "space+cmd", &h) == ZUI_SET_E_TYPE, "bad chord");
    CHECK(zui_ws_bind(&WS, ZUI_ACT_ASK, "space", &h) == ZUI_SET_E_TYPE, "bare space");
    CHECK(zui_ws_bind(&WS, ZUI_ACT_ASK, "meta+cmd+space", &h) == 0, "rebound");
    char b[ZUI_SET_STR_MAX];
    zui_set_str(&ST, WS.view, "shortcut.ask", b, sizeof b);
    CHECK(strcmp(b, "cmd+meta+space") == 0, "stored canonical (%s)", b);
    e = key(' ', ZUI_MOD_CMD | ZUI_MOD_CTRL | ZUI_MOD_META);
    CHECK(zui_ws_match(&WS, &e) == ZUI_ACT_ASK, "matches");
    e = key('k', ZUI_MOD_CMD | ZUI_MOD_CTRL);
    CHECK(zui_ws_match(&WS, &e) == -1, "the old chord is free");
    CHECK(zui_ws_bind(&WS, ZUI_ACT_ASK, "meta+cmd+space", &h) == 0, "same chord again is fine");
    CHECK(zui_ws_bind(&WS, ZUI_ACT_QUIT, "cmd+k", &h) == 0, "quit takes the freed cmd+k");
    CHECK(zui_ws_bind_reset(&WS, ZUI_ACT_ASK, &h) == ZUI_WS_E_CONFLICT && h == ZUI_ACT_QUIT,
          "reset refused: quit has cmd+k");
    CHECK(zui_ws_binding(&WS, ZUI_ACT_ASK).key == ' ', "ask unchanged");
    CHECK(zui_ws_bind(&WS, ZUI_ACT_QUIT, "", &h) == 0, "unbind quit");
    CHECK(zui_ws_binding(&WS, ZUI_ACT_QUIT).key == 0, "unbound");
    e = key('q', ZUI_MOD_CMD | ZUI_MOD_CTRL);
    CHECK(zui_ws_match(&WS, &e) == -1, "nothing on cmd+q");
    CHECK(zui_ws_bind(&WS, ZUI_ACT_NEW_WINDOW, "", &h) == 0, "unbind another");
    CHECK(!zui_ws_conflict(&WS, 0, 0), "two unbound are no conflict");
    CHECK(zui_ws_bind_reset(&WS, ZUI_ACT_ASK, &h) == 0, "now ask resets");
    CHECK(zui_ws_binding(&WS, ZUI_ACT_ASK).key == 'k', "back to cmd+k");
    CHECK(zui_ws_bind(&WS, ZUI_ACT_LOCK_NOW, "", &h) == ZUI_SET_E_RANGE, "lock now stays bound");
    CHECK(zui_ws_bind(&WS, ZUI_ACT_LOCK_NOW, "ctrl+alt+l", &h) == 0, "but may move");
    e = key('l', ZUI_MOD_CTRL | ZUI_MOD_ALT);
    CHECK(zui_ws_match(&WS, &e) == ZUI_ACT_LOCK_NOW, "moved");
    CHECK(zui_ws_bind(&WS, ZUI_ACT_COUNT, "cmd+j", &h) == ZUI_SET_E_NOKEY, "no action");
    CHECK(zui_ws_bind_reset(&WS, ZUI_ACT_COUNT, &h) == ZUI_SET_E_NOKEY, "no action reset");
    CHECK(zui_ws_bind_reset_all(&WS) == 0, "reset all");
    int ok = 1;
    for (uint32_t a = 0; a < ZUI_ACT_COUNT; a++) {
        zui_chord_t x = zui_ws_binding(&WS, (zui_ws_action_t) a);
        zui_chord_t y = zui_ws_default_chord((zui_ws_action_t) a);
        if (x.key != y.key || x.mods != y.mods || !x.key) ok = 0;
    }
    CHECK(ok, "every default back, every one bound");
    /* hand-written text: a bad chord counts as the default; conflicts found */
    zui_set_view_t sa = WS.scope;
    zui_set_set_str(&ST, ZUI_LAYER_USER, sa, "shortcut.minimise", "nonsense");
    c = zui_ws_binding(&WS, ZUI_ACT_MINIMISE);
    CHECK(c.mods == ZUI_MOD_CMD && c.key == 'm', "nonsense reads as the default");
    zui_set_set_str(&ST, ZUI_LAYER_USER, sa, "shortcut.lock-now", "");
    CHECK(zui_ws_binding(&WS, ZUI_ACT_LOCK_NOW).key == 'l', "lock now cannot be emptied by hand");
    zui_set_set_str(&ST, ZUI_LAYER_USER, sa, "shortcut.quit-app", "cmd+w");
    int a = -1, bb = -1;
    CHECK(zui_ws_conflict(&WS, &a, &bb) && a == ZUI_ACT_CLOSE && bb == ZUI_ACT_QUIT,
          "a conflict from hand-written text (%d %d)", a, bb);
    e = key('w', ZUI_MOD_CMD | ZUI_MOD_CTRL);
    CHECK(zui_ws_match(&WS, &e) == ZUI_ACT_CLOSE, "the first action wins the match");
    /* a device default that differs: reset shows it, and checks against it */
    zui_ws_bind_reset_all(&WS);
    zui_set_set_str(&ST, ZUI_LAYER_DEVICE, (zui_set_view_t){0, 0, 0}, "shortcut.ask", "cmd+j");
    CHECK(zui_ws_binding(&WS, ZUI_ACT_ASK).key == 'j', "device rebinding");
    CHECK(zui_ws_bind(&WS, ZUI_ACT_NEW_WINDOW, "cmd+j", &h) == ZUI_WS_E_CONFLICT &&
              h == ZUI_ACT_ASK,
          "conflicts with the device's binding");
    CHECK(zui_ws_bind(&WS, ZUI_ACT_ASK, "cmd+p", &h) == 0, "ana's own");
    CHECK(zui_ws_bind(&WS, ZUI_ACT_NEW_WINDOW, "cmd+j", &h) == 0, "cmd+j now free for ana");
    CHECK(zui_ws_bind_reset(&WS, ZUI_ACT_ASK, &h) == ZUI_WS_E_CONFLICT && h == ZUI_ACT_NEW_WINDOW,
          "reset would show the device's cmd+j: refused");
}

/* ---- WS7, WS8 ---------------------------------------------------------- */

static int theme_calls;
static void on_theme(zui_settings_t *st, uint32_t k, const zui_set_value_t *v, void *ctx)
{
    (void) st, (void) k, (void) v, (void) ctx;
    theme_calls++;
}

static void test_themes(void)
{
    /* the rules, measured */
    int ok = 1;
    for (uint32_t s = 0; s < ZUI_SCHEME_COUNT; s++)
        if (zui_ws_theme_check((zui_scheme_t) s, ZUI_ACCENT_GOLD)) ok = 0;
    CHECK(ok, "every set's own gold passes");
    for (uint32_t a = ZUI_ACCENT_AMBER; a <= ZUI_ACCENT_ROSE; a++) {
        CHECK(zui_ws_theme_check(ZUI_SCHEME_DARK, (zui_accent_t) a) == 0, "%s on dark",
              zui_accent_name((zui_accent_t) a));
        CHECK(zui_ws_theme_check(ZUI_SCHEME_HIGH_CONTRAST, (zui_accent_t) a) == 0,
              "%s on high contrast", zui_accent_name((zui_accent_t) a));
        CHECK(zui_ws_theme_check(ZUI_SCHEME_LIGHT, (zui_accent_t) a) == ZUI_WS_E_CONTRAST,
              "%s refused on light", zui_accent_name((zui_accent_t) a));
    }
    CHECK(zui_ws_theme_check(ZUI_SCHEME_DARK, ZUI_ACCENT_CORAL) == 0, "coral on dark (4.6:1)");
    CHECK(zui_ws_theme_check(ZUI_SCHEME_HIGH_CONTRAST, ZUI_ACCENT_CORAL) == ZUI_WS_E_CONTRAST,
          "coral refused on high contrast: under 7:1");
    CHECK(zui_ws_theme_check(ZUI_SCHEME_LIGHT, ZUI_ACCENT_CORAL) == ZUI_WS_E_CONTRAST,
          "coral refused on light");
    for (uint32_t a = ZUI_ACCENT_BRONZE; a <= ZUI_ACCENT_PLUM; a++) {
        CHECK(zui_ws_theme_check(ZUI_SCHEME_LIGHT, (zui_accent_t) a) == 0, "%s on light",
              zui_accent_name((zui_accent_t) a));
        CHECK(zui_ws_theme_check(ZUI_SCHEME_DARK, (zui_accent_t) a) == ZUI_WS_E_CONTRAST,
              "%s refused on dark", zui_accent_name((zui_accent_t) a));
        CHECK(zui_ws_theme_check(ZUI_SCHEME_HIGH_CONTRAST, (zui_accent_t) a) == ZUI_WS_E_CONTRAST,
              "%s refused on high contrast", zui_accent_name((zui_accent_t) a));
    }
    CHECK(zui_ws_theme_check(ZUI_SCHEME_COUNT, ZUI_ACCENT_GOLD) == ZUI_SET_E_RANGE, "no set");
    CHECK(zui_ws_theme_check(ZUI_SCHEME_DARK, ZUI_ACCENT_COUNT) == ZUI_SET_E_RANGE, "no accent");
    CHECK(strcmp(zui_accent_name(ZUI_ACCENT_PLUM), "plum") == 0 &&
              strcmp(zui_accent_name(ZUI_ACCENT_COUNT), "?") == 0,
          "accent names");
    /* the focus ring */
    CHECK(zui_ws_focus_check(ZUI_SCHEME_DARK, ZUI_C_GOLD) == 0, "gold ring");
    CHECK(zui_ws_focus_check(ZUI_SCHEME_DARK, ZUI_C_TEXT) == 0, "text ring");
    CHECK(zui_ws_focus_check(ZUI_SCHEME_DARK, ZUI_C_HAIRLINE) == ZUI_WS_E_CONTRAST,
          "a hairline ring is too faint");
    CHECK(zui_ws_focus_check(ZUI_SCHEME_DARK, ZUI_C_VOID) == ZUI_WS_E_CONTRAST, "the ground");
    CHECK(zui_ws_focus_check(ZUI_SCHEME_LIGHT, ZUI_C_EDGE) == 0, "light edge is 3:1");
    CHECK(zui_ws_focus_check(ZUI_SCHEME_LIGHT, ZUI_C_ENGRAVE_HI) == ZUI_WS_E_CONTRAST,
          "white on light");
    CHECK(zui_ws_focus_check(ZUI_SCHEME_DARK, ZUI_C_COUNT) == ZUI_SET_E_RANGE, "no role");
    for (uint32_t s = 0; s < ZUI_SCHEME_COUNT; s++)
        CHECK(zui_ws_focus_check((zui_scheme_t) s, ZUI_C_GOLD) == 0, "gold ring in set %u", s);

    fresh();
    int nw = 0;
    zui_set_watch(&ST, WS.view, "theme.", on_theme, &nw);
    zui_ws_theme_t t;
    zui_ws_theme(&WS, &t);
    CHECK(t.scheme == ZUI_SCHEME_DARK && t.accent == ZUI_ACCENT_GOLD && t.focus == ZUI_C_GOLD &&
              t.text_scale == 100 && !t.reduced_motion && t.density == ZUI_DENSITY_REGULAR,
          "defaults");
    zui_colors_t col;
    zui_ws_colors(&WS, &col);
    CHECK(memcmp(&col, zui_scheme_colors(ZUI_SCHEME_DARK), sizeof col) == 0,
          "the dark set, unchanged");
    theme_calls = 0;
    CHECK(zui_ws_set_theme(&WS, ZUI_SCHEME_DARK, ZUI_ACCENT_SKY) == 0, "dark + sky");
    CHECK(theme_calls == 1, "one change, one call (%d)", theme_calls);
    zui_ws_colors(&WS, &col);
    CHECK(col.c[ZUI_C_GOLD] == 0x7CC4FFu && col.c[ZUI_C_GOLD_HI] == 0xA6D8FFu &&
              col.c[ZUI_C_GOLD_GLOW] == 0xA6D8FFu,
          "the accent replaces the gold roles");
    CHECK(col.c[ZUI_C_TEXT] == ZUI_TEXT && col.c[ZUI_C_VOID] == ZUI_VOID, "nothing else");
    CHECK(zui_ws_set_theme(&WS, ZUI_SCHEME_LIGHT, ZUI_ACCENT_SKY) == ZUI_WS_E_CONTRAST,
          "sky on light refused");
    zui_ws_theme(&WS, &t);
    CHECK(t.scheme == ZUI_SCHEME_DARK && t.accent == ZUI_ACCENT_SKY, "and nothing changed");
    theme_calls = 0;
    CHECK(zui_ws_set_theme(&WS, ZUI_SCHEME_LIGHT, ZUI_ACCENT_OCEAN) == 0, "light + ocean");
    CHECK(theme_calls == 2, "scheme and accent: two calls");
    zui_ws_theme(&WS, &t);
    CHECK(t.scheme == ZUI_SCHEME_LIGHT && t.accent == ZUI_ACCENT_OCEAN, "set");
    zui_ws_colors(&WS, &col);
    CHECK(col.c[ZUI_C_VOID] == zui_scheme_color(ZUI_SCHEME_LIGHT, ZUI_C_VOID) &&
              col.c[ZUI_C_GOLD] == 0x0B57A8u,
          "light set, ocean accent");
    CHECK(zui_ws_set_theme(&WS, ZUI_SCHEME_HIGH_CONTRAST, ZUI_ACCENT_GOLD) == 0, "hc + gold");
    zui_ws_colors(&WS, &col);
    CHECK(memcmp(&col, zui_scheme_colors(ZUI_SCHEME_HIGH_CONTRAST), sizeof col) == 0,
          "the high contrast set as it is");
    /* a failing pair written by hand: gold shows */
    zui_set_set_int(&ST, ZUI_LAYER_USER, WS.scope, "theme.scheme", ZUI_SCHEME_LIGHT);
    zui_set_set_int(&ST, ZUI_LAYER_USER, WS.scope, "theme.accent", ZUI_ACCENT_AMBER);
    zui_ws_theme(&WS, &t);
    CHECK(t.scheme == ZUI_SCHEME_LIGHT && t.accent == ZUI_ACCENT_GOLD, "falls back to gold");
    zui_ws_colors(&WS, &col);
    CHECK(memcmp(&col, zui_scheme_colors(ZUI_SCHEME_LIGHT), sizeof col) == 0, "the light set");
    /* focus */
    CHECK(zui_ws_set_focus(&WS, ZUI_C_HAIRLINE) == ZUI_WS_E_CONTRAST, "faint ring refused");
    CHECK(zui_ws_set_focus(&WS, ZUI_C_TEXT) == 0, "text ring");
    zui_ws_theme(&WS, &t);
    CHECK(t.focus == ZUI_C_TEXT, "kept");
    zui_set_set_int(&ST, ZUI_LAYER_USER, WS.scope, "theme.focus", ZUI_C_HAIRLINE);
    zui_ws_theme(&WS, &t);
    CHECK(t.focus == ZUI_C_GOLD, "a faint ring by hand shows gold");
    /* a locked scheme */
    zui_set_set_int(&ST, ZUI_LAYER_DEVICE, (zui_set_view_t){0, 0, 0}, "theme.scheme",
                    ZUI_SCHEME_HIGH_CONTRAST);
    zui_set_lock(&ST, ZUI_LAYER_DEVICE, (zui_set_view_t){0, 0, 0}, "theme.scheme", true);
    theme_calls = 0;
    CHECK(zui_ws_set_theme(&WS, ZUI_SCHEME_DARK, ZUI_ACCENT_JADE) == ZUI_SET_E_LOCKED,
          "the device holds high contrast");
    CHECK(theme_calls == 0, "nothing written");
    zui_ws_theme(&WS, &t);
    CHECK(t.scheme == ZUI_SCHEME_HIGH_CONTRAST && t.accent == ZUI_ACCENT_AMBER,
          "the user's amber, which fails on light, passes on high contrast");
    CHECK(zui_ws_set_theme(&WS, ZUI_SCHEME_HIGH_CONTRAST, ZUI_ACCENT_JADE) == 0,
          "the locked scheme as it is, with jade: fine");
    CHECK(theme_calls == 1, "only the accent written");
    zui_set_value_t pv;
    CHECK(zui_set_peek(&ST, ZUI_LAYER_USER, WS.scope, "theme.scheme", &pv, 0) == 0 &&
              pv.i == ZUI_SCHEME_LIGHT,
          "the user's own scheme left alone under the lock");
    zui_ws_theme(&WS, &t);
    CHECK(t.scheme == ZUI_SCHEME_HIGH_CONTRAST && t.accent == ZUI_ACCENT_JADE, "in effect");
    CHECK(zui_ws_set_theme(&WS, ZUI_SCHEME_HIGH_CONTRAST, ZUI_ACCENT_PINE) == ZUI_WS_E_CONTRAST,
          "the rules still apply under a lock");
    zui_set_lock(&ST, ZUI_LAYER_DEVICE, (zui_set_view_t){0, 0, 0}, "theme.scheme", false);
    zui_workspace_t dev;
    zui_ws_init(&dev, &ST, (zui_set_view_t){0, 0, 0});
    zui_set_lock(&ST, ZUI_LAYER_SYSTEM, (zui_set_view_t){0, 0, 0}, "theme.accent", true);
    CHECK(zui_ws_set_theme(&dev, ZUI_SCHEME_DARK, ZUI_ACCENT_JADE) == ZUI_SET_E_LOCKED,
          "a system-locked accent stops the device too");
    CHECK(zui_ws_set_theme(&dev, ZUI_SCHEME_DARK, ZUI_ACCENT_GOLD) == 0,
          "the locked value itself is fine below the user");
}

static void test_comfort(void)
{
    fresh();
    CHECK(zui_ws_text_pt(&WS, ZUI_T_BODY) == ZUI_T_BODY, "100%%");
    CHECK(zui_ws_space(&WS, ZUI_S4) == ZUI_S4, "regular");
    CHECK(zui_ws_motion_ms(&WS, ZUI_MOTION_MS) == ZUI_MOTION_MS, "motion");
    CHECK(zui_ws_set_comfort(&WS, 150, true, ZUI_DENSITY_ROOMY) == 0, "set");
    CHECK(zui_ws_text_pt(&WS, ZUI_T_BODY) == 21, "14 pt at 150%% is 21");
    CHECK(zui_ws_text_pt(&WS, ZUI_T_CAPTION) == 17, "11 pt at 150%% is 16.5, rounded 17");
    CHECK(zui_ws_motion_ms(&WS, ZUI_MOTION_MS) == 0, "reduced motion: 0 ms");
    CHECK(zui_ws_space(&WS, ZUI_S4) == 20, "roomy 16 -> 20");
    CHECK(zui_ws_space(&WS, 6) == 8, "roomy 6 -> 7.5 rounds 8");
    CHECK(zui_ws_target(&WS, 40, true) == 50, "roomy target grows");
    CHECK(zui_ws_set_comfort(&WS, 90, false, ZUI_DENSITY_COMPACT) == 0, "compact");
    CHECK(zui_ws_text_pt(&WS, ZUI_T_BODY) == 13, "14 at 90%% is 12.6 -> 13");
    CHECK(zui_ws_space(&WS, ZUI_S4) == 12, "compact 16 -> 12");
    CHECK(zui_ws_space(&WS, 6) == 5, "compact 6 -> 4.5 rounds 5");
    CHECK(zui_ws_target(&WS, ZUI_TOUCH_MIN, true) == ZUI_TOUCH_MIN, "touch never below 44");
    CHECK(zui_ws_target(&WS, ZUI_POINTER_MIN, false) == ZUI_POINTER_MIN, "pointer never below 28");
    CHECK(zui_ws_target(&WS, 80, false) == 60, "bigger targets shrink");
    CHECK(zui_ws_motion_ms(&WS, ZUI_MOTION_MS) == ZUI_MOTION_MS, "motion back");
    CHECK(zui_ws_set_comfort(&WS, 85, false, ZUI_DENSITY_REGULAR) == ZUI_SET_E_RANGE, "off step");
    CHECK(zui_ws_set_comfort(&WS, 70, false, ZUI_DENSITY_REGULAR) == ZUI_SET_E_RANGE, "small");
    CHECK(zui_ws_set_comfort(&WS, 210, false, ZUI_DENSITY_REGULAR) == ZUI_SET_E_RANGE, "big");
    CHECK(zui_ws_set_comfort(&WS, 200, false, (zui_density_t) 3) == ZUI_SET_E_RANGE, "density");
    zui_ws_theme_t t;
    zui_ws_theme(&WS, &t);
    CHECK(t.text_scale == 90 && t.density == ZUI_DENSITY_COMPACT, "refusals changed nothing");
    CHECK(zui_ws_set_comfort(&WS, 200, false, ZUI_DENSITY_REGULAR) == 0, "200%%");
    CHECK(zui_ws_text_pt(&WS, ZUI_T_DISPLAY) == 64, "display doubled");
    CHECK(zui_ws_set_comfort(&WS, 80, false, ZUI_DENSITY_REGULAR) == 0, "80%%");
    CHECK(zui_ws_text_pt(&WS, ZUI_T_LABEL) == 10, "12 at 80%% is 9.6 -> 10");
    /* another person on the same seat is untouched */
    zui_ws_init(&WS2, &ST, (zui_set_view_t){SEAT, BO, 0});
    CHECK(zui_ws_text_pt(&WS2, ZUI_T_BODY) == ZUI_T_BODY, "bo at 100%%");
}

/* ---- WS9: everything round-trips --------------------------------------- */

static void test_persist(void)
{
    fresh();
    layout(&WM);
    zui_ws_save(&WS, &WM, "work");
    zui_ws_set_panel(&WS, ZUI_PANEL_RIGHT, 80, true);
    zui_ws_pin(&WS, "quillon", 0);
    zui_ws_pin(&WS, "app \"two\"", 1);
    zui_ws_bind(&WS, ZUI_ACT_ASK, "alt+space", 0);
    zui_ws_bind(&WS, ZUI_ACT_QUIT, "", 0);
    zui_ws_set_theme(&WS, ZUI_SCHEME_DARK, ZUI_ACCENT_ROSE);
    zui_ws_set_focus(&WS, ZUI_C_TEXT_2);
    zui_ws_set_comfort(&WS, 130, true, ZUI_DENSITY_COMPACT);
    int n = zui_set_save(&ST, ZUI_LAYER_USER, WS.scope, buf, sizeof buf);
    CHECK(n > 0, "saved (%d)", n);
    CHECK(strncmp(buf, "zxcfg 1\n# user ana\n", 19) == 0, "header");
    CHECK(strstr(buf, "\npanel.pin.1 = \"app \\\"two\\\"\"\n") != 0, "escaped pin");
    CHECK(strstr(buf, "\nshortcut.ask = \"alt+space\"\n") != 0, "a shortcut line");
    CHECK(strstr(buf, "\nshortcut.quit-app = \"\"\n") != 0, "unbound is kept");
    CHECK(strstr(buf, "\ntheme.accent = rose\n") != 0, "accent by name");
    CHECK(strstr(buf, "\ntheme.focus = text 2\n") != 0, "focus role by name");
    CHECK(strstr(buf, "\nworkspace.0.w1.state = maximised\n") != 0, "window state by name");
    CHECK(strstr(buf, "\nworkspace.0.w3.x = 1000\n") != 0, "a window's x");
    /* into a fresh store, on another machine: same everything */
    zui_set_init(&ST2);
    zui_set_seat(&ST2, "seat9");
    uint8_t u = zui_set_user(&ST2, "ana");
    zui_ws_init(&WS2, &ST2, (zui_set_view_t){1, u, 0});
    uint32_t line = 0;
    CHECK(zui_set_load(&ST2, ZUI_LAYER_USER, WS2.scope, buf, (uint32_t) n, &line) == 0,
          "loads (line %u)", line);
    int n2 = zui_set_save(&ST2, ZUI_LAYER_USER, WS2.scope, buf2, sizeof buf2);
    CHECK(n2 == n && memcmp(buf, buf2, (size_t) n) == 0, "same bytes");
    zui_ws_panel_t p, p2;
    zui_ws_panel(&WS, &p);
    zui_ws_panel(&WS2, &p2);
    CHECK(memcmp(&p, &p2, sizeof p) == 0, "same panel");
    CHECK(p2.pos == ZUI_PANEL_RIGHT && p2.size == 80 && p2.autohide && p2.npinned == 2 &&
              strcmp(p2.pinned[1], "app \"two\"") == 0,
          "panel values");
    int same = 1;
    for (uint32_t a = 0; a < ZUI_ACT_COUNT; a++) {
        zui_chord_t x = zui_ws_binding(&WS, (zui_ws_action_t) a);
        zui_chord_t y = zui_ws_binding(&WS2, (zui_ws_action_t) a);
        if (x.key != y.key || x.mods != y.mods) same = 0;
    }
    CHECK(same, "same shortcuts");
    CHECK(zui_ws_binding(&WS2, ZUI_ACT_QUIT).key == 0, "quit still unbound");
    zui_ws_theme_t t, t2;
    zui_ws_theme(&WS, &t);
    zui_ws_theme(&WS2, &t2);
    CHECK(t.scheme == t2.scheme && t.accent == t2.accent && t.focus == t2.focus &&
              t.text_scale == t2.text_scale && t.reduced_motion == t2.reduced_motion &&
              t.density == t2.density,
          "same theme");
    CHECK(t2.accent == ZUI_ACCENT_ROSE && t2.focus == ZUI_C_TEXT_2 && t2.text_scale == 130 &&
              t2.reduced_motion && t2.density == ZUI_DENSITY_COMPACT,
          "theme values");
    zui_wm_init(&WM, 1280, 800, false);
    zui_wm_init(&WM2, 1280, 800, false);
    CHECK(zui_ws_restore(&WS, &WM, "work", 0, 0) == 4 &&
              zui_ws_restore(&WS2, &WM2, "work", 0, 0) == 4,
          "both restore");
    same = 1;
    for (uint32_t i = 0; i < 4; i++) {
        zui_wm_window_t *a = nth(&WM, i), *b = nth(&WM2, i);
        if (!req(a->r, b->r) || !req(a->restore, b->restore) || a->maximised != b->maximised ||
            a->minimised != b->minimised || strcmp(a->app, b->app))
            same = 0;
    }
    CHECK(same && zui_wm_front(&WM) == nth(&WM, 0)->id && zui_wm_front(&WM2) == nth(&WM2, 0)->id,
          "the same windows");
}

int main(void)
{
    test_schema();
    test_save_restore();
    test_panel();
    test_chords();
    test_shortcuts();
    test_themes();
    test_comfort();
    test_persist();
    printf("%s test_zui_workspace: %d/%d checks\n", fails ? "[FAIL]" : "[PASS]", checks - fails,
           checks);
    return fails ? 1 : 0;
}
