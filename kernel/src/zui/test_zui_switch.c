/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zui_switch.c — switch access (zui_switch.h SW1..SW9). */
#include <stdio.h>
#include <string.h>

#include "zui_switch.h"
#include "zui_tokens.h"
#include "../font/ttf_fixture.inc" /* MINI_TTF[] */

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

/* ---- a screen ----------------------------------------------------------------- */

#define SW_W 1280
#define SW_H 800
static zui_ui_t UI;
static zui_switch_t SW;
static zui_screen_t laptop = {SW_W, SW_H, 256, ZUI_INPUT_POINTER};
static uint32_t FB[SW_W * SW_H];
static zui_surface_t S;

static struct {
    zui_id_t root, top, a, b, c, mid, list, field, bot, ok, no, hid, dis;
    char buf[64];
} V;

static struct {
    zui_id_t w;
    zui_action_t act;
    int32_t arg;
} GOT[64];
static int n_got;
static void on(zui_ui_t *ui, zui_id_t w, zui_action_t act, int32_t arg, void *ctx)
{
    (void) ui, (void) ctx;
    if (n_got < 64) {
        GOT[n_got].w = w;
        GOT[n_got].act = act;
        GOT[n_got].arg = arg;
    }
    n_got++;
}

static void row_fn(const zui_ui_t *ui, zui_id_t list, int32_t row, zui_row_t *out, void *ctx)
{
    (void) ui, (void) list, (void) ctx;
    snprintf(out->text, sizeof out->text, "row %d", row);
}

static void build(void)
{
    zui_theme_t th;
    memset(&th, 0, sizeof th);
    th.body.size_px = 16;
    th.label.size_px = 12;
    th.title.size_px = 20;
    th.mono.size_px = 16;
    zui_ui_init(&UI, &laptop, &th);
    zui_ui_on(&UI, on, 0);
    memset(&V, 0, sizeof V);
    V.root = zui_box(&UI, ZUI_NONE, ZUI_COL);
    V.top = zui_box(&UI, V.root, ZUI_ROW);
    V.a = zui_button(&UI, V.top, "Alpha", ZUI_BTN_SECONDARY);
    V.b = zui_button(&UI, V.top, "Beta", ZUI_BTN_SECONDARY);
    V.c = zui_button(&UI, V.top, "Gamma", ZUI_BTN_SECONDARY);
    V.hid = zui_button(&UI, V.top, "Hidden", ZUI_BTN_SECONDARY);
    zui_set_hidden(&UI, V.hid, true);
    V.mid = zui_box(&UI, V.root, ZUI_ROW);
    zui_set_size(&UI, V.mid, ZUI_SIZE_FLEX, 1);
    V.list = zui_list(&UI, V.mid, row_fn, 0);
    zui_set_size(&UI, V.list, ZUI_SIZE_FLEX, 1);
    zui_list_set_rows(&UI, V.list, 10);
    V.field = zui_field(&UI, V.mid, V.buf, sizeof V.buf, false);
    zui_set_size(&UI, V.field, ZUI_SIZE_FLEX, 1);
    V.bot = zui_box(&UI, V.root, ZUI_ROW);
    V.ok = zui_button(&UI, V.bot, "OK", ZUI_BTN_PRIMARY);
    V.no = zui_button(&UI, V.bot, "No", ZUI_BTN_SECONDARY);
    V.dis = zui_button(&UI, V.bot, "Off", ZUI_BTN_SECONDARY);
    zui_set_disabled(&UI, V.dis, true);
    zui_surface_init(&S, FB, SW_W, SW_H, SW_W);
    zui_ui_paint(&UI, &S);
    n_got = 0;
}

static zui_sw_action_t ACTS[32];
static int n_acts;
static bool pass_on; /* also perform it */
static void record(zui_switch_t *sw, const zui_sw_action_t *a, void *ctx)
{
    (void) ctx;
    if (n_acts < 32) ACTS[n_acts] = *a;
    n_acts++;
    if (pass_on) zui_switch_apply(sw, a);
}

static int moves;
static void on_move(zui_switch_t *sw, void *ctx)
{
    (void) sw, (void) ctx;
    moves++;
}

static void setup(zui_sw_mode_t mode)
{
    build();
    zui_sw_config_t c;
    zui_sw_config_default(&c);
    c.mode = mode;
    zui_switch_init(&SW, &c);
    zui_switch_attach(&SW, &UI, 0, 0, 0);
    zui_switch_on_act(&SW, record, 0);
    n_acts = 0;
    pass_on = false;
}

static void sel(void)
{
    zui_switch_tick(&SW, 200); /* past the debounce */
    zui_switch_press(&SW, ZUI_SW_SELECT, true);
    zui_switch_press(&SW, ZUI_SW_SELECT, false);
}

static void nxt(void)
{
    zui_switch_tick(&SW, 0);
    SW.fired_once = false; /* stepping fast in tests: no debounce */
    zui_switch_press(&SW, ZUI_SW_NEXT, true);
    zui_switch_press(&SW, ZUI_SW_NEXT, false);
}

static bool group_has(uint32_t g, zui_id_t id)
{
    for (uint32_t i = 0; i < SW.g[g].n; i++)
        if (SW.t[SW.g[g].first + i].kind == ZUI_SW_T_WIDGET && SW.t[SW.g[g].first + i].id == id)
            return true;
    return false;
}

/* ---- tests ----------------------------------------------------------------------- */

static void test_config(void)
{
    zui_sw_config_t c;
    zui_sw_config_default(&c);
    CHECK(c.mode == ZUI_SW_AUTO && c.grouping == ZUI_SW_ROWS && c.interval_ms == 1000 &&
              c.first_ms == 500 && c.dwell_ms == 0 && c.debounce_ms == 100 && c.loops == 3 &&
              c.menu && c.point && c.menu_mask == 0x3F && c.point_ms == 50 &&
              c.key_select == ZUI_KEY_SPACE && c.key_next == ZUI_KEY_ENTER,
          "defaults");
    zui_sw_config_default(0);
    c.mode = 9;
    c.grouping = 9;
    c.interval_ms = 1;
    c.dwell_ms = 99999;
    c.loops = 0;
    c.point_ms = 0;
    c.point_step = -3;
    c.menu_mask = (1u << ZUI_SW_ACT_ACTIVATE) | (1u << ZUI_SW_ACT_CANCEL);
    zui_switch_init(&SW, &c);
    CHECK(SW.cfg.mode == ZUI_SW_AUTO && SW.cfg.grouping == ZUI_SW_ROWS, "bad enums -> defaults");
    CHECK(SW.cfg.interval_ms == 100 && SW.cfg.dwell_ms == 10000 && SW.cfg.loops == 1 &&
              SW.cfg.point_ms == 10 && SW.cfg.point_step == 0,
          "clamped");
    CHECK(SW.n_menu == 2 && SW.menu_verbs[0] == ZUI_SW_ACT_ACTIVATE &&
              SW.menu_verbs[1] == ZUI_SW_ACT_CANCEL,
          "the menu holds the masked verbs in order");
    c.interval_ms = 99999;
    c.loops = 99;
    c.mode = ZUI_SW_STEP;
    c.grouping = ZUI_SW_COLUMNS;
    zui_switch_config(&SW, &c);
    CHECK(SW.cfg.interval_ms == 20000 && SW.cfg.loops == 20 && SW.cfg.mode == ZUI_SW_STEP &&
              SW.cfg.grouping == ZUI_SW_COLUMNS,
          "upper clamps, step and columns kept");
    zui_switch_init(&SW, 0);
    CHECK(SW.state == ZUI_SW_OFF && SW.cfg.interval_ms == 1000 && SW.n_menu == 6,
          "init(NULL): defaults, off");
    CHECK(!strcmp(zui_switch_word(&SW, ZUI_SW_ACT_ACTIVATE), "Activate") &&
              !strcmp(zui_switch_word(&SW, ZUI_SW_ACT_CANCEL), "Cancel") &&
              !strcmp(zui_switch_word(&SW, ZUI_SW_ACT_COUNT), ""),
          "English words");
    static const char *const FR[ZUI_SW_ACT_COUNT] = {"Activer",     "Appui long", "Défiler haut",
                                                     "Défiler bas", "Retour",     0};
    zui_switch_set_words(&SW, FR);
    CHECK(!strcmp(zui_switch_word(&SW, ZUI_SW_ACT_ACTIVATE), "Activer") &&
              !strcmp(zui_switch_word(&SW, ZUI_SW_ACT_CANCEL), "Cancel"),
          "a locale's words; a missing one falls back");
    CHECK(!strcmp(zui_switch_word(0, ZUI_SW_ACT_BACK), "Back"), "no switch: English");
    /* off: switches and keys do nothing */
    zui_event_t e;
    memset(&e, 0, sizeof e);
    e.type = ZUI_EV_KEY_DOWN;
    e.key = ZUI_KEY_SPACE;
    CHECK(!zui_switch_event(&SW, &e), "off: keys pass through");
    zui_switch_press(&SW, ZUI_SW_SELECT, true);
    zui_switch_tick(&SW, 5000);
    CHECK(SW.state == ZUI_SW_OFF && SW.now_ms == 0, "off: nothing moves");
}

static void test_targets(void)
{
    setup(ZUI_SW_AUTO);
    zui_switch_start(&SW);
    CHECK(SW.state == ZUI_SW_GROUP && SW.group == 0, "starts on the first group");
    CHECK(SW.nt == 7, "seven targets: three buttons, list, field, two buttons (%u)", SW.nt);
    CHECK(SW.ng == 4 && SW.g[3].point, "three rows and the point scan (%u)", SW.ng);
    CHECK(SW.g[0].n == 3 && group_has(0, V.a) && group_has(0, V.b) && group_has(0, V.c),
          "row 1: the top buttons");
    CHECK(SW.t[SW.g[0].first].id == V.a && SW.t[SW.g[0].first + 2].id == V.c, "left to right");
    CHECK(SW.g[1].n == 2 && group_has(1, V.list) && group_has(1, V.field), "row 2");
    CHECK(SW.g[2].n == 2 && group_has(2, V.ok) && group_has(2, V.no), "row 3");
    bool hid = false, dis = false;
    for (uint32_t i = 0; i < SW.nt; i++) {
        if (SW.t[i].id == V.hid) hid = true;
        if (SW.t[i].id == V.dis) dis = true;
    }
    CHECK(!hid && !dis, "hidden and disabled widgets are not targets");
    zui_rect_t ra = zui_w(&UI, V.a)->r, rc = zui_w(&UI, V.c)->r;
    CHECK(SW.g[0].r.x == ra.x && SW.g[0].r.x + SW.g[0].r.w == rc.x + rc.w,
          "a group's box spans its items");
    CHECK(SW.g[3].r.w == SW_W && SW.g[3].r.h == SW_H, "the point group is the area");
    /* columns */
    SW.cfg.grouping = ZUI_SW_COLUMNS;
    zui_switch_start(&SW);
    CHECK(SW.ng >= 3, "columns (%u groups)", SW.ng);
    bool sorted = true;
    for (uint32_t g = 1; g + 1u < SW.ng; g++)
        if (SW.g[g].r.x + SW.g[g].r.w / 2 < SW.g[g - 1].r.x + SW.g[g - 1].r.w / 2) sorted = false;
    CHECK(sorted, "columns run left to right");
    bool vert = true;
    for (uint32_t g = 0; g + 1u < SW.ng; g++)
        for (uint32_t i = 1; i < SW.g[g].n; i++)
            if (SW.t[SW.g[g].first + i].r.y < SW.t[SW.g[g].first + i - 1].r.y) vert = false;
    CHECK(vert, "items of a column run top to bottom");
    /* linear, no point: one group scans as items */
    SW.cfg.grouping = ZUI_SW_LINEAR;
    SW.cfg.point = false;
    zui_switch_start(&SW);
    CHECK(SW.ng == 1 && SW.state == ZUI_SW_ITEM && SW.g[0].n == 7, "linear: straight to items");
    CHECK(SW.t[0].id == V.a && SW.t[3].id == V.list && SW.t[6].id == V.no, "in tree order");
    /* nothing at all */
    zui_ui_clear(&UI);
    zui_switch_start(&SW);
    CHECK(SW.state == ZUI_SW_IDLE && SW.ng == 0, "no targets, no point scan: rest");
    SW.cfg.point = true;
    zui_switch_start(&SW);
    CHECK(SW.state == ZUI_SW_POINT_X && SW.px == 0, "only the point scan: straight into it");
    /* the palette is modal: no widget targets */
    build();
    zui_palette_open(&UI, "");
    zui_switch_start(&SW);
    CHECK(SW.nt == 0 && SW.state == ZUI_SW_POINT_X, "an open palette hides the widgets");
    zui_palette_close(&UI);
    /* a widget scrolled off screen is not a target */
    zui_switch_attach(&SW, &UI, 0, 0, 0);
    SW.cfg.grouping = ZUI_SW_ROWS;
    zui_switch_start(&SW);
    uint32_t before = SW.nt;
    zui_w(&UI, V.no)->r.x = SW_W + 10;
    UI.layout_dirty = false;
    zui_switch_start(&SW);
    CHECK(SW.nt == before - 1, "off screen: left out");
    zui_relayout(&UI);
    /* an origin offset moves the targets to screen space */
    zui_switch_attach(&SW, &UI, 100, 50, 0);
    zui_switch_start(&SW);
    CHECK(SW.t[0].r.x == zui_w(&UI, V.a)->r.x + 100 && SW.t[0].r.y == zui_w(&UI, V.a)->r.y + 50,
          "ui origin applied");
    CHECK(SW.area.x == 100 && SW.area.y == 50 && SW.area.w == SW_W, "area from the ui");
    zui_switch_set_area(&SW, (zui_rect_t){0, 0, 640, 480});
    CHECK(SW.area.w == 640, "area set");
}

static void test_auto(void)
{
    setup(ZUI_SW_AUTO);
    SW.on_move = on_move;
    moves = 0;
    zui_switch_start(&SW);
    CHECK(moves == 1, "starting is a move");
    zui_switch_tick(&SW, 1499);
    CHECK(SW.group == 0, "the first stop waits interval + first_ms");
    zui_switch_tick(&SW, 1);
    CHECK(SW.group == 1 && moves == 2, "then moves");
    zui_switch_tick(&SW, 999);
    CHECK(SW.group == 1, "999 ms: still");
    zui_switch_tick(&SW, 1);
    CHECK(SW.group == 2, "1000 ms a stop");
    zui_switch_tick(&SW, 2000);
    CHECK(SW.group == 0 && SW.passes == 1, "two stops in one tick, and a lap");
    zui_switch_tick(&SW, 1500 + 3000);
    CHECK(SW.group == 0 && SW.passes == 2, "second lap");
    zui_switch_tick(&SW, 1500 + 2999);
    CHECK(SW.state == ZUI_SW_GROUP && SW.group == 3, "not yet");
    zui_switch_tick(&SW, 1);
    CHECK(SW.state == ZUI_SW_IDLE, "three laps with no choice: rest");
    zui_rect_t r;
    CHECK(!zui_switch_highlight(&SW, &r) && r.w == 0, "no highlight at rest");
    zui_switch_tick(&SW, 100000);
    CHECK(SW.state == ZUI_SW_IDLE, "rest stays");
    sel();
    CHECK(SW.state == ZUI_SW_GROUP && SW.group == 0 && SW.passes == 0, "SELECT wakes it");
    /* a held switch stops the scan */
    zui_switch_tick(&SW, 1000);
    SW.cfg.dwell_ms = 5000;
    zui_switch_press(&SW, ZUI_SW_NEXT, true);
    zui_switch_tick(&SW, 3000);
    CHECK(SW.group == 0, "held: nothing moves");
    zui_switch_press(&SW, ZUI_SW_NEXT, false);
    zui_switch_tick(&SW, 499);
    CHECK(SW.group == 0, "release: the count goes on (1000 + 499 < 1500)");
    zui_switch_tick(&SW, 1);
    CHECK(SW.group == 1, "and moves at 1500");
    SW.cfg.dwell_ms = 0;
    /* NEXT steps early in auto */
    zui_switch_tick(&SW, 300);
    nxt();
    CHECK(SW.group == 2 && SW.elapsed == 0, "NEXT steps early and restarts the count");
    /* item scan, the way back, laps */
    sel();
    zui_switch_start(&SW);
    sel();
    CHECK(SW.state == ZUI_SW_ITEM && SW.group == 0 && SW.item == 0, "into the first row");
    zui_switch_tick(&SW, 1500);
    CHECK(SW.item == 1, "items step");
    zui_switch_tick(&SW, 2000);
    CHECK(SW.item == 3, "the stop after the last item is the group (way back)");
    CHECK(zui_switch_highlight(&SW, &r) && r.x == SW.g[0].r.x && r.w == SW.g[0].r.w,
          "highlight: the whole group");
    sel();
    CHECK(SW.state == ZUI_SW_GROUP && SW.group == 0, "selecting it goes back to the groups");
    sel();
    CHECK(SW.state == ZUI_SW_ITEM, "in again");
    zui_switch_tick(&SW, 1500 + 3000 + 1500 + 3000 + 1500 + 2999);
    CHECK(SW.state == ZUI_SW_ITEM, "two laps and most of a third");
    zui_switch_tick(&SW, 1);
    CHECK(SW.state == ZUI_SW_GROUP && SW.group == 0, "three laps in items: back to groups");
}

static void test_choose_and_menu(void)
{
    setup(ZUI_SW_STEP);
    zui_switch_start(&SW);
    zui_switch_tick(&SW, 60000);
    CHECK(SW.group == 0 && SW.state == ZUI_SW_GROUP, "step: time moves nothing");
    for (int i = 0; i < 20; i++) nxt();
    CHECK(SW.state == ZUI_SW_GROUP && SW.group == 0, "step: no rest after many laps (20 %% 4)");
    sel();
    nxt();
    CHECK(SW.state == ZUI_SW_ITEM && SW.item == 1, "on Beta");
    sel();
    CHECK(SW.state == ZUI_SW_MENU && SW.hot == 0 && SW.chosen.id == V.b, "the action menu");
    CHECK(zui_switch_menu_verb(&SW) == ZUI_SW_ACT_ACTIVATE, "starting on Activate");
    zui_rect_t m = zui_switch_menu_rect(&SW), br = zui_w(&UI, V.b)->r;
    CHECK(m.y == br.y + br.h + ZUI_S1 && m.x == br.x && m.h == 6 * 32 && m.w == 176,
          "the menu sits under the target");
    zui_rect_t r;
    zui_switch_highlight(&SW, &r);
    CHECK(r.y == m.y && r.h == 32, "highlight on the first menu row");
    nxt();
    zui_switch_highlight(&SW, &r);
    CHECK(r.y == m.y + 32 && zui_switch_menu_verb(&SW) == ZUI_SW_ACT_LONG_PRESS, "Long press");
    for (int i = 0; i < 5; i++) nxt();
    CHECK(SW.hot == 0, "the menu wraps");
    sel();
    CHECK(n_acts == 1 && ACTS[0].verb == ZUI_SW_ACT_ACTIVATE && ACTS[0].target.id == V.b &&
              ACTS[0].target.kind == ZUI_SW_T_WIDGET,
          "Activate on Beta handed on");
    CHECK(ACTS[0].x == br.x + br.w / 2 && ACTS[0].y == br.y + br.h / 2, "at its centre");
    CHECK(SW.state == ZUI_SW_GROUP && SW.group == 0 && SW.actions == 1,
          "after an action the scan starts again");
    CHECK(n_got == 0, "a callback that does not apply leaves the ui alone");
    /* cancel does nothing */
    sel();
    sel();
    for (int i = 0; i < 5; i++) nxt();
    CHECK(zui_switch_menu_verb(&SW) == ZUI_SW_ACT_CANCEL, "Cancel");
    sel();
    CHECK(n_acts == 1 && SW.state == ZUI_SW_GROUP, "cancel: no action, back to the start");
    CHECK(zui_switch_menu_verb(&SW) == ZUI_SW_ACT_COUNT, "no menu verb outside the menu");

    /* the default apply on the ui */
    zui_switch_on_act(&SW, 0, 0);
    sel(); /* row 1 */
    nxt();
    nxt(); /* Gamma */
    sel();
    sel(); /* Activate */
    CHECK(n_got == 1 && GOT[0].w == V.c && GOT[0].act == ZUI_ACT_PRESS, "Gamma pressed");
    CHECK(UI.focus == V.c && UI.focus_visible, "and focused, as from the keyboard");
    n_got = 0;
    sel();
    sel(); /* Alpha */
    nxt();
    sel(); /* Long press */
    CHECK(n_got == 1 && GOT[0].w == V.a && GOT[0].act == ZUI_ACT_CONTEXT, "long press: context");
    /* the field just takes focus */
    n_got = 0;
    nxt();
    sel();
    nxt();
    sel(); /* row 2, item 2: the field */
    sel();
    CHECK(UI.focus == V.field && n_got == 0, "a field is focused, nothing pressed");
    /* scroll down on the list */
    nxt();
    sel();
    sel(); /* list */
    nxt();
    nxt();
    nxt();
    CHECK(zui_switch_menu_verb(&SW) == ZUI_SW_ACT_SCROLL_DOWN, "Scroll down");
    zui_w(&UI, V.list)->u.list.top = 0;
    sel();
    CHECK(SW.actions == 5, "five actions so far (%u)", SW.actions);
    /* menu off: choosing activates at once */
    SW.cfg.menu = false;
    n_got = 0;
    zui_switch_start(&SW);
    nxt();
    nxt();
    sel(); /* row 3 */
    sel(); /* OK */
    CHECK(n_got == 1 && GOT[0].w == V.ok && GOT[0].act == ZUI_ACT_PRESS && SW.state == ZUI_SW_GROUP,
          "no menu: OK pressed straight away");
    /* a group of one is chosen straight through */
    zui_set_hidden(&UI, V.no, true);
    zui_switch_start(&SW);
    n_got = 0;
    nxt();
    nxt();
    sel();
    CHECK(n_got == 1 && GOT[0].w == V.ok, "a group of one: no item step");
    zui_set_hidden(&UI, V.no, false);
    /* apply refuses nonsense */
    zui_sw_action_t a;
    memset(&a, 0, sizeof a);
    a.verb = ZUI_SW_ACT_CANCEL;
    CHECK(!zui_switch_apply(&SW, &a) && !zui_switch_apply(&SW, 0), "cancel and NULL do nothing");
    a.verb = ZUI_SW_ACT_ACTIVATE;
    a.target.kind = ZUI_SW_T_WIDGET;
    a.target.id = 9999;
    CHECK(!zui_switch_apply(&SW, &a), "a bad widget");
    a.target.kind = ZUI_SW_T_WINDOW;
    CHECK(!zui_switch_apply(&SW, &a), "a window with no wm");
    a.target.kind = ZUI_SW_T_STRIP;
    CHECK(!zui_switch_apply(&SW, &a), "a strip with no wm");
    a.target.kind = 77;
    CHECK(!zui_switch_apply(&SW, &a), "an unknown kind");
}

static void test_dwell_debounce_keys(void)
{
    setup(ZUI_SW_STEP);
    SW.cfg.dwell_ms = 300;
    zui_switch_start(&SW);
    zui_switch_press(&SW, ZUI_SW_NEXT, true);
    zui_switch_tick(&SW, 299);
    CHECK(SW.group == 0, "held 299 ms: not yet");
    zui_switch_tick(&SW, 1);
    CHECK(SW.group == 1, "300 ms: counts");
    zui_switch_tick(&SW, 1000);
    CHECK(SW.group == 1, "once per hold");
    zui_switch_press(&SW, ZUI_SW_NEXT, true);
    zui_switch_tick(&SW, 1000);
    CHECK(SW.group == 1, "a second down while held is ignored");
    zui_switch_press(&SW, ZUI_SW_NEXT, false);
    zui_switch_press(&SW, ZUI_SW_NEXT, true);
    zui_switch_tick(&SW, 100);
    zui_switch_press(&SW, ZUI_SW_NEXT, false);
    zui_switch_tick(&SW, 1000);
    CHECK(SW.group == 1, "a tap shorter than the dwell does nothing");
    SW.cfg.dwell_ms = 0;
    zui_switch_press(&SW, ZUI_SW_NEXT, true);
    CHECK(SW.group == 2, "no dwell: at once");
    zui_switch_press(&SW, ZUI_SW_NEXT, false);
    zui_switch_tick(&SW, 99);
    zui_switch_press(&SW, ZUI_SW_NEXT, true);
    CHECK(SW.group == 2, "a bounce within 100 ms is ignored");
    zui_switch_press(&SW, ZUI_SW_NEXT, false);
    zui_switch_tick(&SW, 1);
    zui_switch_press(&SW, ZUI_SW_NEXT, true);
    CHECK(SW.group == 3, "at 100 ms it counts");
    zui_switch_press(&SW, ZUI_SW_NEXT, false);
    zui_switch_press(&SW, (zui_sw_switch_t) 7, true);
    CHECK(SW.group == 3, "an unknown switch");
    /* keys */
    zui_event_t e;
    memset(&e, 0, sizeof e);
    e.type = ZUI_EV_KEY_DOWN;
    e.key = ZUI_KEY_ENTER;
    zui_switch_tick(&SW, 500);
    CHECK(zui_switch_event(&SW, &e) && SW.group == 0, "Enter is NEXT");
    e.repeat = 1;
    zui_switch_tick(&SW, 500);
    CHECK(zui_switch_event(&SW, &e) && SW.group == 0, "auto-repeat is swallowed");
    e.type = ZUI_EV_KEY_UP;
    e.repeat = 0;
    CHECK(zui_switch_event(&SW, &e) && !SW.held[ZUI_SW_NEXT], "key up releases");
    e.type = ZUI_EV_KEY_DOWN;
    e.key = ZUI_KEY_SPACE;
    zui_switch_tick(&SW, 500);
    CHECK(zui_switch_event(&SW, &e) && SW.state == ZUI_SW_ITEM, "Space is SELECT");
    e.key = 'q';
    CHECK(!zui_switch_event(&SW, &e), "other keys are not used");
    e.type = ZUI_EV_TEXT;
    e.key = ZUI_KEY_SPACE;
    CHECK(!zui_switch_event(&SW, &e), "text is not a switch");
    CHECK(!zui_switch_event(&SW, 0), "NULL");
    SW.cfg.key_next = 0;
    e.type = ZUI_EV_KEY_DOWN;
    e.key = 0;
    CHECK(!zui_switch_event(&SW, &e), "a switch with no key");
    zui_switch_stop(&SW);
    CHECK(SW.state == ZUI_SW_OFF && !SW.held[0] && !SW.held[1], "stop");
}

static void test_point(void)
{
    setup(ZUI_SW_STEP);
    zui_switch_start(&SW);
    for (int i = 0; i < 3; i++) nxt();
    CHECK(SW.g[SW.group].point, "the last group is the point scan");
    zui_rect_t r;
    zui_switch_highlight(&SW, &r);
    CHECK(r.w == SW_W && r.h == SW_H, "highlighting the screen");
    sel();
    CHECK(SW.state == ZUI_SW_POINT_X && SW.px == 0, "the vertical line at the left");
    nxt();
    CHECK(SW.px == SW_W / 64, "steps 1/64 of the width (%d)", SW.px);
    zui_switch_highlight(&SW, &r);
    CHECK(r.x == SW.px - 1 && r.w == 2 && r.h == SW_H, "highlight: the line");
    /* walk to the middle of Beta */
    zui_rect_t b = zui_w(&UI, V.b)->r;
    SW.cfg.point_step = 1;
    while (SW.px < b.x + b.w / 2) nxt();
    sel();
    CHECK(SW.state == ZUI_SW_POINT_Y && SW.py == 0, "x fixed, y sweeps");
    while (SW.py < b.y + b.h / 2) nxt();
    zui_switch_highlight(&SW, &r);
    CHECK(r.y == SW.py - 1 && r.h == 2 && r.w == SW_W, "highlight: the horizontal line");
    sel();
    CHECK(SW.state == ZUI_SW_MENU && SW.chosen.kind == ZUI_SW_T_POINT, "the menu for the point");
    zui_switch_on_act(&SW, 0, 0);
    n_got = 0;
    sel();
    CHECK(n_got == 1 && GOT[0].w == V.b && GOT[0].act == ZUI_ACT_PRESS,
          "a click at the point pressed Beta (%d)", n_got);
    /* wrap and laps in auto */
    setup(ZUI_SW_AUTO);
    SW.cfg.point_step = 400;
    zui_switch_start(&SW);
    for (int i = 0; i < 3; i++) nxt();
    sel();
    CHECK(SW.state == ZUI_SW_POINT_X, "point x");
    zui_switch_tick(&SW, 550);
    CHECK(SW.px == 400, "50 ms a step plus the first wait");
    zui_switch_tick(&SW, 100);
    CHECK(SW.px == 1200, "two steps");
    zui_switch_tick(&SW, 50);
    CHECK(SW.px == 0 && SW.passes == 1, "wraps to the left: a lap");
    zui_switch_tick(&SW, 550 + 150 + 550 + 149);
    CHECK(SW.state == ZUI_SW_POINT_X, "almost three laps");
    zui_switch_tick(&SW, 1);
    CHECK(SW.state == ZUI_SW_GROUP && SW.group == 0, "the point scan gives up: back to the start");
    /* the menu gives up too */
    sel();
    sel();
    CHECK(SW.state == ZUI_SW_MENU, "menu");
    zui_switch_tick(&SW, 1500 + 5000 + 1500 + 5000 + 1500 + 5000);
    CHECK(SW.state == ZUI_SW_GROUP && n_acts == 0, "three laps of the menu: nothing done");
    /* only the point scan, giving up: rest */
    zui_ui_clear(&UI);
    zui_switch_start(&SW);
    CHECK(SW.state == ZUI_SW_POINT_X, "only point");
    zui_switch_tick(&SW, 550 + 150 + 550 + 150 + 550 + 150);
    CHECK(SW.state == ZUI_SW_IDLE, "it rests rather than spin");
    /* long press and scroll at a point */
    build();
    zui_switch_attach(&SW, &UI, 0, 0, 0);
    zui_sw_action_t a;
    memset(&a, 0, sizeof a);
    a.target.kind = ZUI_SW_T_POINT;
    zui_rect_t ar = zui_w(&UI, V.a)->r;
    a.x = ar.x + ar.w / 2;
    a.y = ar.y + ar.h / 2;
    a.verb = ZUI_SW_ACT_LONG_PRESS;
    n_got = 0;
    CHECK(zui_switch_apply(&SW, &a) && n_got == 1 && GOT[0].w == V.a &&
              GOT[0].act == ZUI_ACT_CONTEXT,
          "long press at a point: a right click");
    a.verb = ZUI_SW_ACT_BACK;
    CHECK(!zui_switch_apply(&SW, &a), "back with nothing to close is unused");
}

static void test_menu_open(void)
{
    setup(ZUI_SW_STEP);
    static const char *const items[] = {"Open", "-", "Rename", "Delete"};
    zui_menu_open(&UI, V.a, 100, 100, items, 0, 4);
    zui_menu_disable(&UI, 3, true);
    zui_switch_on_act(&SW, 0, 0);
    zui_switch_start(&SW);
    CHECK(SW.nt == 2 && SW.t[0].kind == ZUI_SW_T_MENU_ITEM && SW.t[0].id == 0 && SW.t[1].id == 2,
          "an open menu: its usable items only");
    CHECK(SW.t[1].r.y == UI.menu.r.y + 4 + 2 * 26 && SW.t[1].r.h == 26,
          "rows where the menu has them");
    CHECK(SW.state == ZUI_SW_GROUP && SW.ng == 2, "menu items and the point scan");
    sel();
    nxt();
    sel();
    n_got = 0;
    sel(); /* Activate */
    CHECK(n_got == 1 && GOT[0].act == ZUI_ACT_PICK && GOT[0].arg == 2 && !UI.menu.open,
          "Rename picked");
    zui_menu_open(&UI, V.a, 100, 100, items, 0, 4);
    zui_switch_start(&SW);
    sel();
    sel();
    for (int i = 0; i < 4; i++) nxt();
    CHECK(zui_switch_menu_verb(&SW) == ZUI_SW_ACT_BACK, "Back");
    n_got = 0;
    sel();
    CHECK(!UI.menu.open && n_got == 1 && GOT[0].act == ZUI_ACT_DISMISS, "back closes the menu");
    zui_sw_action_t a;
    memset(&a, 0, sizeof a);
    a.target.kind = ZUI_SW_T_MENU_ITEM;
    CHECK(!zui_switch_apply(&SW, &a), "a menu item with no menu open");
    zui_menu_open(&UI, V.a, 100, 100, items, 0, 4);
    zui_menu_disable(&UI, 3, true);
    a.target.id = 3;
    CHECK(!zui_switch_apply(&SW, &a), "a disabled item");
    a.target.id = 99;
    CHECK(!zui_switch_apply(&SW, &a), "an item out of range");
    a.verb = ZUI_SW_ACT_SCROLL_UP;
    CHECK(!zui_switch_apply(&SW, &a), "a menu item does not scroll");
}

/* ---- the window manager ----------------------------------------------------------- */

static zui_wm_t WM;
static int wm_events;
static bool w_event(void *ctx, const zui_event_t *e, zui_rect_t c)
{
    (void) ctx, (void) e, (void) c;
    wm_events++;
    return true;
}

static void test_wm(void)
{
    zui_wm_init(&WM, SW_W, SW_H, false);
    zui_wm_use_dock(&WM, false); /* the strip, behind its switch */
    zui_wm_client_t cl = {0, w_event, 0, 0};
    uint32_t a = zui_wm_open(&WM, "burrow", "Burrow", 0, &cl);
    uint32_t b = zui_wm_open(&WM, "glint", "Glint", 0, &cl);
    zui_wm_progress(&WM, a, 256);
    zui_wm_progress(&WM, b, 256);
    zui_wm_tick(&WM, ZUI_WM_OPEN_MS);
    zui_sw_config_t c;
    zui_sw_config_default(&c);
    c.mode = ZUI_SW_STEP;
    c.menu = false;
    zui_switch_init(&SW, &c);
    zui_switch_attach(&SW, 0, 0, 0, &WM);
    CHECK(SW.area.w == SW_W && SW.area.h == SW_H, "the area is the wm's screen");
    zui_switch_start(&SW);
    CHECK(SW.ng == 3, "windows, strip, point (%u)", SW.ng);
    CHECK(SW.g[0].n == 2 && SW.t[0].kind == ZUI_SW_T_WINDOW && SW.t[0].id == (int32_t) a,
          "windows in opening order");
    CHECK(SW.g[1].n == 2 && SW.t[2].kind == ZUI_SW_T_STRIP && SW.t[3].id == (int32_t) b,
          "strip slots");
    zui_rect_t slot = zui_wm_strip_slot(&WM, 0, 2);
    CHECK(SW.t[2].r.x == slot.x && SW.t[2].r.w == slot.w, "a slot's rect");
    CHECK(zui_wm_front(&WM) == b, "b in front");
    sel();
    sel(); /* window a */
    CHECK(zui_wm_front(&WM) == a, "choosing a window raises it");
    nxt();
    sel();
    nxt();
    sel(); /* strip slot b */
    CHECK(zui_wm_front(&WM) == b, "a strip slot pressed raises its window");
    nxt();
    sel();
    nxt();
    sel(); /* strip slot b again: it is in front, so it minimises */
    CHECK(zui_wm_get(&WM, b)->minimised, "pressing the front one minimises it");
    zui_switch_start(&SW);
    CHECK(SW.g[0].n == 1 && SW.g[1].n == 2, "a minimised window leaves the windows group");
    /* scroll on a window goes to its app */
    zui_sw_action_t act;
    memset(&act, 0, sizeof act);
    act.target = SW.t[0];
    zui_rect_t cr = zui_wm_content(&WM, zui_wm_get(&WM, a));
    act.x = cr.x + cr.w / 2;
    act.y = cr.y + cr.h / 2;
    act.verb = ZUI_SW_ACT_SCROLL_DOWN;
    wm_events = 0;
    CHECK(zui_switch_apply(&SW, &act) && wm_events == 1, "scroll goes to the app");
    act.verb = ZUI_SW_ACT_BACK;
    CHECK(zui_switch_apply(&SW, &act) && wm_events >= 2, "back goes to the app");
    act.target.id = 999;
    CHECK(!zui_switch_apply(&SW, &act), "a closed window");
    act.target.kind = ZUI_SW_T_STRIP;
    act.verb = ZUI_SW_ACT_SCROLL_UP;
    CHECK(!zui_switch_apply(&SW, &act), "the strip does not scroll");
    /* a point click goes through the wm */
    act.target.kind = ZUI_SW_T_POINT;
    act.verb = ZUI_SW_ACT_ACTIVATE;
    wm_events = 0;
    CHECK(zui_switch_apply(&SW, &act) && wm_events == 2, "point click: down and up to the app");
}

/* SW1, SW2, SW8 with the dock (zui_wm.h WM9, the default) */
static void test_dock(void)
{
    zui_wm_init(&WM, SW_W, SW_H, false);
    zui_wm_client_t cl = {0, w_event, 0, 0};
    uint32_t a = zui_wm_open(&WM, "burrow", "Burrow", 0, &cl);
    uint32_t b = zui_wm_open(&WM, "glint", "Glint", 0, &cl);
    zui_wm_progress(&WM, a, 256);
    zui_wm_progress(&WM, b, 256);
    zui_wm_tick(&WM, ZUI_WM_OPEN_MS);
    zui_dock_pin(&WM.dock, "atlas", "Atlas");
    zui_sw_config_t c;
    zui_sw_config_default(&c);
    c.mode = ZUI_SW_STEP;
    c.menu = false;
    zui_switch_init(&SW, &c);
    zui_switch_attach(&SW, 0, 0, 0, &WM);
    zui_switch_start(&SW);
    zui_rect_t k = zui_dock_keepout(&WM.dock);
    CHECK(SW.ng == 3 && SW.g[1].n == 1 && SW.t[2].kind == ZUI_SW_T_DOCK && SW.t[2].id == -1 &&
              SW.t[2].r.x == k.x && SW.t[2].r.y == k.y && SW.t[2].r.w == k.w,
          "windows, the dock's seal, point (%u)", SW.ng);
    nxt();
    sel(); /* the seal: a group of one */
    CHECK(zui_dock_is_open(&WM.dock) && WM.dock.kbd, "choosing the seal opens the dock");
    CHECK(SW.ng == 5 && SW.state == ZUI_SW_GROUP && SW.group == 2 && SW.g[2].n == 2 &&
              SW.g[3].n == 1,
          "the scan goes on over its rings, each a group (%u, %u)", SW.ng, SW.group);
    zui_dock_slot_t ds;
    zui_dock_slot(&WM.dock, 0, &ds);
    int32_t h = WM.dock.icon / 2 + ZUI_DOCK_PAD;
    CHECK(SW.t[3].kind == ZUI_SW_T_DOCK && SW.t[3].id == ds.item && SW.t[3].r.x == ds.cx - h &&
              SW.t[3].r.w == 2 * h,
          "a slot's item and box");
    sel();
    sel(); /* ring 0, the first slot: glint, in front */
    CHECK(zui_wm_get(&WM, b)->minimised && !zui_dock_is_open(&WM.dock),
          "choosing the app in front minimises it; the dock closes");
    nxt();
    sel(); /* the seal again */
    CHECK(SW.group == 2, "on the rings");
    nxt();
    sel(); /* ring 1: atlas alone */
    uint32_t f = zui_wm_front(&WM);
    CHECK(f && f != a && f != b && !strcmp(zui_wm_get(&WM, f)->app, "atlas"),
          "choosing a pinned app launches it");
    /* a window chosen while the dock is open closes it first */
    zui_dock_bloom(&WM.dock, true, true);
    zui_sw_action_t act;
    memset(&act, 0, sizeof act);
    act.target.kind = ZUI_SW_T_WINDOW;
    act.target.id = (int32_t) a;
    CHECK(zui_switch_apply(&SW, &act) && !zui_dock_is_open(&WM.dock) && zui_wm_front(&WM) == a,
          "a window: the dock closes, the window comes forward");
    zui_dock_bloom(&WM.dock, true, true);
    act.target.kind = ZUI_SW_T_POINT;
    zui_dock_slot(&WM.dock, 0, &ds);
    act.x = ds.cx;
    act.y = ds.cy;
    CHECK(zui_switch_apply(&SW, &act) && !zui_dock_is_open(&WM.dock),
          "a point on a dock slot presses it there");
    /* scrolling a slot turns its ring */
    char nm[8];
    for (int i = 0; i < 14; i++) {
        snprintf(nm, sizeof nm, "p%d", i);
        zui_dock_pin(&WM.dock, nm, 0);
    }
    for (int i = 0; i < 4; i++) zui_wm_open(&WM, "more", 0, 0, 0);
    zui_dock_bloom(&WM.dock, true, true);
    zui_switch_start(&SW);
    const zui_sw_group_t *last = &SW.g[SW.ng - 2u];
    act.target = SW.t[last->first];
    act.verb = ZUI_SW_ACT_SCROLL_DOWN;
    const zui_dock_ring_t *r2 = &WM.dock.ring[2];
    CHECK(act.target.kind == ZUI_SW_T_DOCK && r2->n > r2->cap && zui_switch_apply(&SW, &act) &&
              r2->rot == 1,
          "scroll on a turning ring's slot turns it");
    act.target = SW.t[SW.g[2].first];
    CHECK(!zui_switch_apply(&SW, &act), "a ring that does not turn");
    act.target.id = -1;
    CHECK(!zui_switch_apply(&SW, &act), "the seal does not scroll");
    act.verb = ZUI_SW_ACT_BACK;
    CHECK(zui_switch_apply(&SW, &act) && !zui_dock_is_open(&WM.dock), "back closes the dock");
    zui_switch_attach(&SW, 0, 0, 0, 0);
    act.verb = ZUI_SW_ACT_ACTIVATE;
    CHECK(!zui_switch_apply(&SW, &act), "a dock target with no wm");
}

/* ---- SW9 drawing ------------------------------------------------------------------- */

static void clear_fb(void)
{
    for (int i = 0; i < SW_W * SW_H; i++) FB[i] = ZUI_VOID;
}

static uint32_t at(int32_t x, int32_t y)
{
    return FB[y * SW_W + x];
}

static void test_draw(void)
{
    setup(ZUI_SW_STEP);
    zui_switch_start(&SW);
    clear_fb();
    zui_switch_draw(&SW, &S, 0);
    zui_rect_t g = SW.g[0].r;
    int32_t my = g.y + g.h / 2, mx = g.x + g.w / 2;
    CHECK(at(g.x + g.w, my) == ZUI_GOLD && at(g.x + g.w + 1, my) == ZUI_GOLD,
          "a gold edge 2 px wide on the right");
    CHECK(at(g.x + g.w + 2, my) != ZUI_GOLD, "and no wider");
    CHECK(at(mx, g.y + g.h) == ZUI_GOLD && at(mx, g.y + g.h + 1) == ZUI_GOLD, "and below");
    CHECK(at(g.x + g.w + 3, my) != ZUI_VOID, "engraved outside the gold");
    CHECK(at(g.x + 5, my) == ZUI_VOID, "the inside is left to show the target");
    uint32_t gold = 0;
    for (int i = 0; i < SW_W * SW_H; i++) gold += FB[i] == ZUI_GOLD;
    static uint32_t A[SW_W * SW_H];
    memcpy(A, FB, sizeof A);
    clear_fb();
    zui_switch_draw(&SW, &S, 0);
    CHECK(memcmp(A, FB, sizeof A) == 0, "deterministic");
    /* items: the group faint, the item gold */
    sel();
    nxt();
    clear_fb();
    zui_switch_draw(&SW, &S, 0);
    zui_rect_t t = SW.t[SW.g[0].first + 1].r;
    CHECK(at(t.x - 2, t.y + t.h / 2) == ZUI_GOLD && at(t.x - 1, t.y + t.h / 2) == ZUI_GOLD,
          "the item edged in gold");
    CHECK(at(t.x - 4, t.y + t.h / 2) == ZUI_ENGRAVE_LO, "engraved: the wall of the cut outside");
    CHECK(at(t.x - 3, t.y + t.h / 2) == ZUI_VOID, "between cut and gold, the ground");
    uint32_t gold_items = 0;
    for (int i = 0; i < SW_W * SW_H; i++) gold_items += FB[i] == ZUI_GOLD;
    CHECK(gold_items < gold, "only the item is gold, not the group (%u < %u)", gold_items, gold);
    /* the way back: dashed */
    nxt();
    nxt();
    clear_fb();
    zui_switch_draw(&SW, &S, 0);
    uint32_t dash = 0;
    for (int x = g.x; x < g.x + g.w; x++) dash += at(x, g.y + g.h + 1) == ZUI_GOLD;
    CHECK(dash > 0 && dash < (uint32_t) g.w, "the way back is dashed (%u of %d)", dash, g.w);
    /* the menu */
    nxt();
    sel();
    clear_fb();
    zui_switch_draw(&SW, &S, 0);
    zui_rect_t m = zui_switch_menu_rect(&SW);
    CHECK(at(m.x + m.w / 2, m.y + 32 * 3 + 16) == ZUI_PANEL, "the menu is a carved panel");
    CHECK(at(m.x + m.w / 2, m.y + 16) == ZUI_RAISED, "the hot row is raised");
    CHECK(at(m.x + m.w / 2, m.y + 2) == ZUI_GOLD, "and edged in gold");
    CHECK(at(m.x + m.w / 2, m.y + 32 + 2) != ZUI_GOLD, "the other rows are not");
    /* with text: the words draw (the fixture inks only "A": "Activate") */
    static zui_glyph_cache_t cache;
    static ttf_font_t font;
    CHECK(ttf_parse(&font, MINI_TTF, sizeof MINI_TTF), "fixture font");
    zui_glyph_cache_init(&cache);
    zui_text_t tx = {&font, &cache, 16, false};
    memcpy(A, FB, sizeof A);
    clear_fb();
    zui_switch_draw(&SW, &S, &tx);
    uint32_t ink = 0, diff = 0;
    for (int y = m.y; y < m.y + 32; y++)
        for (int x = m.x; x < m.x + m.w; x++) {
            ink += at(x, y) != ZUI_RAISED && at(x, y) != ZUI_GOLD && at(x, y) != ZUI_PANEL;
            diff += at(x, y) != A[y * SW_W + x];
        }
    CHECK(ink > 0 && diff > 0, "the hot row's word is drawn (%u px)", ink);
    /* point scan lines */
    setup(ZUI_SW_STEP);
    SW.cfg.point_step = 100;
    zui_switch_start(&SW);
    for (int i = 0; i < 3; i++) nxt();
    sel();
    nxt();
    clear_fb();
    zui_switch_draw(&SW, &S, 0);
    CHECK(at(99, 400) == ZUI_GOLD && at(100, 400) == ZUI_GOLD && at(98, 400) == ZUI_ENGRAVE_LO &&
              at(101, 400) == ZUI_ENGRAVE_HI,
          "the sweeping line: gold, engraved either side");
    sel();
    nxt();
    clear_fb();
    zui_switch_draw(&SW, &S, 0);
    CHECK(at(99, 300) == ZUI_TEXT_2, "the fixed line is light green");
    CHECK(at(500, 99) == ZUI_GOLD && at(500, 100) == ZUI_GOLD, "the sweeping one gold");
    /* menu placement */
    SW.state = ZUI_SW_MENU;
    SW.chosen.r = (zui_rect_t){1200, 700, 60, 60};
    m = zui_switch_menu_rect(&SW);
    CHECK(m.y + m.h == 700 - ZUI_S1 && m.x == SW_W - 176,
          "no room below: above, inside the screen");
    SW.chosen.r = (zui_rect_t){10, 100, 60, 650};
    m = zui_switch_menu_rect(&SW);
    CHECK(m.y == SW_H - m.h, "no room either way: at the bottom");
    SW.state = ZUI_SW_OFF;
    clear_fb();
    zui_switch_draw(&SW, &S, 0);
    uint32_t any = 0;
    for (int i = 0; i < SW_W * SW_H; i++) any += FB[i] != ZUI_VOID;
    CHECK(any == 0, "off: nothing drawn");
}

int main(void)
{
    test_config();
    test_targets();
    test_auto();
    test_choose_and_menu();
    test_dwell_debounce_keys();
    test_point();
    test_menu_open();
    test_wm();
    test_dock();
    test_draw();
    printf("%s test_zui_switch: %d/%d checks\n", fails ? "[FAIL]" : "[PASS]", checks - fails,
           checks);
    return fails ? 1 : 0;
}
