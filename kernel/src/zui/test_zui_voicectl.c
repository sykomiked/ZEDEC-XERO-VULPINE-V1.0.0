/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zui_voicectl.c — voice control (zui_voicectl.h VC1..VC7). */
#include <stdio.h>
#include <string.h>

#include "zui_tokens.h"
#include "zui_voicectl.h"
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

#define W_ 1280
#define H_ 800
static zui_ui_t UI;
static zui_voicectl_t VC;
static zui_screen_t laptop = {W_, H_, 256, ZUI_INPUT_POINTER};
static uint32_t FB[W_ * H_];
static zui_surface_t S;

static struct {
    zui_id_t root, top, save, saveas, open, del, field, list, held, jp;
    char buf[256];
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

static const char *FILES[] = {"alpha.txt", "beta.png", "gamma", "delta.md"};
static void row_fn(const zui_ui_t *ui, zui_id_t list, int32_t row, zui_row_t *out, void *ctx)
{
    (void) ui, (void) list, (void) ctx;
    snprintf(out->text, sizeof out->text, "%s", FILES[row % 4]);
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
    V.save = zui_button(&UI, V.top, "Save", ZUI_BTN_PRIMARY);
    V.saveas = zui_button(&UI, V.top, "Save as\xE2\x80\xA6", ZUI_BTN_SECONDARY);
    V.open = zui_button(&UI, V.top, "Open", ZUI_BTN_SECONDARY);
    V.del = zui_button(&UI, V.top, "Delete", ZUI_BTN_DANGER);
    zui_set_disabled(&UI, V.del, true);
    V.jp = zui_button(&UI, V.top, "\xE4\xBF\x9D\xE5\xAD\x98", ZUI_BTN_GHOST); /* 保存 */
    V.field = zui_field(&UI, V.root, V.buf, sizeof V.buf, true);
    zui_set_name(&UI, V.field, "Notes");
    V.list = zui_list(&UI, V.root, row_fn, 0);
    zui_set_name(&UI, V.list, "Files");
    zui_set_size(&UI, V.list, ZUI_SIZE_FLEX, 1);
    zui_list_set_rows(&UI, V.list, 4);
    V.held = zui_held(&UI, V.root);
    zui_held_set(&UI, V.held, "companion", 300, true, "A suggested reply", 1);
    zui_surface_init(&S, FB, W_, H_, W_);
    zui_ui_paint(&UI, &S);
    n_got = 0;
}

static zui_vc_action_t ACTS[32];
static int n_acts;
static void record(zui_voicectl_t *vc, const zui_vc_action_t *a, void *ctx)
{
    (void) vc, (void) ctx;
    if (n_acts < 32) ACTS[n_acts] = *a;
    n_acts++;
}

static void setup(bool apply)
{
    build();
    zui_vc_init(&VC);
    zui_vc_attach(&VC, &UI, 0, 0, 0);
    if (!apply) zui_vc_on_act(&VC, record, 0);
    n_acts = 0;
}

static bool got(zui_id_t w, zui_action_t act)
{
    for (int i = 0; i < n_got && i < 64; i++)
        if (GOT[i].w == w && GOT[i].act == act) return true;
    return false;
}

/* ---- VC1: parsing ------------------------------------------------------------------ */

static void test_parse(void)
{
    zui_vc_init(&VC);
    char arg[64];
    struct {
        const char *said;
        zui_vc_cmd_t cmd;
        const char *arg;
    } P[] = {
        {"Click Save.", ZUI_VC_CMD_CLICK, "save"},
        {"  TAP   3 ", ZUI_VC_CMD_TAP_NUMBER, "3"},
        {"tap three", ZUI_VC_CMD_TAP_NUMBER, "three"},
        {"number twenty", ZUI_VC_CMD_TAP_NUMBER, "twenty"},
        {"tap save", ZUI_VC_CMD_CLICK, "save"},
        {"press  Open", ZUI_VC_CMD_CLICK, "open"},
        {"5", ZUI_VC_CMD_NUMBER, "5"},
        {"Seven!", ZUI_VC_CMD_NUMBER, "seven"},
        {"show numbers", ZUI_VC_CMD_SHOW_NUMBERS, ""},
        {"Hide numbers.", ZUI_VC_CMD_HIDE_NUMBERS, ""},
        {"show grid", ZUI_VC_CMD_SHOW_GRID, ""},
        {"hide grid", ZUI_VC_CMD_HIDE_GRID, ""},
        {"cancel", ZUI_VC_CMD_CANCEL, ""},
        {"scroll down!", ZUI_VC_CMD_SCROLL_DOWN, ""},
        {"Scroll up", ZUI_VC_CMD_SCROLL_UP, ""},
        {"go back", ZUI_VC_CMD_BACK, ""},
        {"open Burrow", ZUI_VC_CMD_OPEN, "burrow"},
        {"launch glint", ZUI_VC_CMD_OPEN, "glint"},
        {"switch to Glint", ZUI_VC_CMD_SWITCH, "glint"},
        {"close window", ZUI_VC_CMD_CLOSE_WINDOW, ""},
        {"close this window.", ZUI_VC_CMD_CLOSE_WINDOW, ""},
        {"type Hello", ZUI_VC_CMD_TYPE, "hello"},
        {"select world", ZUI_VC_CMD_SELECT, "world"},
        {"start dictation", ZUI_VC_CMD_DICTATE_ON, ""},
        {"stop dictation", ZUI_VC_CMD_DICTATE_OFF, ""},
        {"hello there", ZUI_VC_CMD_COUNT, ""},
        {"twenty one", ZUI_VC_CMD_COUNT, ""},
        {"12345", ZUI_VC_CMD_COUNT, ""},
        {"tap", ZUI_VC_CMD_COUNT, ""},
        {"click ", ZUI_VC_CMD_COUNT, ""},
        {"", ZUI_VC_CMD_COUNT, ""},
        {"show  numbers  please", ZUI_VC_CMD_COUNT, ""},
    };
    for (unsigned i = 0; i < sizeof P / sizeof P[0]; i++) {
        zui_vc_cmd_t c = zui_vc_parse(&VC, P[i].said, arg, sizeof arg);
        CHECK(c == P[i].cmd && !strcmp(arg, P[i].arg), "'%s' -> %d '%s' (want %d '%s')", P[i].said,
              c, arg, P[i].cmd, P[i].arg);
    }
    CHECK(zui_vc_parse(&VC, 0, arg, sizeof arg) == ZUI_VC_CMD_COUNT, "NULL phrase");
    CHECK(zui_vc_parse(&VC, "type Hi There.", 0, 0) == ZUI_VC_CMD_TYPE &&
              !strcmp(VC.arg_raw, "Hi There."),
          "the slot as said keeps case and punctuation");
    zui_vc_parse(&VC, "click  Save  As", 0, 0);
    CHECK(!strcmp(VC.arg_raw, "Save As") && !strcmp(VC.raw, "click Save As"), "spaces squeezed");
}

/* ---- VC1: other languages ------------------------------------------------------------ */

static const zui_vc_phrase_t FR[] = {{ZUI_VC_CMD_CLICK, "cliquer sur %s"},
                                     {ZUI_VC_CMD_SHOW_NUMBERS, "afficher les numéros"},
                                     {ZUI_VC_CMD_TAP_NUMBER, "toucher %n"}};
static const char *const FR_NUM[] = {"zéro", "un", "deux", "trois"};
static const zui_vc_locale_t fr = {"fr", FR, 3, FR_NUM, 4};
static const zui_vc_phrase_t JA[] = {
    {ZUI_VC_CMD_CLICK,
     "%s\xE3\x82\x92\xE3\x82\xAF\xE3\x83\xAA\xE3\x83\x83\xE3\x82\xAF"}, /* %sをクリック */
    {ZUI_VC_CMD_BACK, "\xE6\x88\xBB\xE3\x82\x8B"},                      /* 戻る */
    {99, "bogus"},
    {ZUI_VC_CMD_BACK, 0}};
static const zui_vc_locale_t ja = {"ja", JA, 4, 0, 0};

static void test_locales(void)
{
    setup(false);
    CHECK(!zui_vc_add_locale(&VC, 0), "NULL locale");
    CHECK(zui_vc_add_locale(&VC, &fr) && zui_vc_add_locale(&VC, &ja) && VC.n_loc == 2,
          "two locales");
    CHECK(VC.loc[0] == &ja && VC.loc[1] == &fr, "the last added is tried first");
    char arg[64];
    CHECK(zui_vc_parse(&VC, "Cliquer sur Open", arg, sizeof arg) == ZUI_VC_CMD_CLICK &&
              !strcmp(arg, "open"),
          "French click");
    CHECK(zui_vc_parse(&VC, "toucher deux", arg, sizeof arg) == ZUI_VC_CMD_TAP_NUMBER,
          "French number words");
    CHECK(zui_vc_parse(&VC, "toucher two", arg, sizeof arg) == ZUI_VC_CMD_TAP_NUMBER,
          "number words from any active locale");
    CHECK(zui_vc_parse(&VC, "click open", arg, sizeof arg) == ZUI_VC_CMD_CLICK,
          "English still works");
    CHECK(zui_vc_hear(&VC, "\xE4\xBF\x9D\xE5\xAD\x98\xE3\x82\x92\xE3\x82\xAF\xE3\x83\xAA\xE3\x83"
                           "\x83\xE3\x82\xAF\xE3\x80\x82") == ZUI_VC_DONE,
          "保存をクリック。: the slot before the verb, no spaces");
    CHECK(n_acts == 1 && ACTS[0].target.id == V.jp, "the 保存 button");
    CHECK(zui_vc_hear(&VC, "\xE6\x88\xBB\xE3\x82\x8B") == ZUI_VC_DONE &&
              ACTS[1].kind == ZUI_VC_ACT_BACK,
          "戻る: back");
    CHECK(zui_vc_hear(&VC, "bogus") == ZUI_VC_UNKNOWN, "a bad command number is skipped");
    CHECK(zui_vc_add_locale(&VC, &fr) && zui_vc_add_locale(&VC, &fr), "four");
    CHECK(!zui_vc_add_locale(&VC, &ja), "a fifth is refused");
    zui_vc_clear_locales(&VC);
    CHECK(VC.n_loc == 0 && zui_vc_parse(&VC, "cliquer sur open", 0, 0) == ZUI_VC_CMD_COUNT,
          "cleared: English only");
}

/* ---- VC2: names ------------------------------------------------------------------------ */

static void test_names(void)
{
    setup(true);
    CHECK(zui_vc_hear(&VC, "Click Save") == ZUI_VC_DONE, "click save");
    CHECK(n_got == 1 && GOT[0].w == V.save && GOT[0].act == ZUI_ACT_PRESS,
          "the exact name wins over 'Save as…'");
    CHECK(UI.focus == V.save, "and it has focus");
    n_got = 0;
    CHECK(zui_vc_hear(&VC, "click save as") == ZUI_VC_DONE && got(V.saveas, ZUI_ACT_PRESS),
          "the ellipsis is ignored in names");
    n_got = 0;
    CHECK(zui_vc_hear(&VC, "click sa") == ZUI_VC_AMBIGUOUS, "two contain 'sa'");
    CHECK(VC.overlay == ZUI_VC_PICK && VC.n_pick == 2 && n_got == 0, "both numbered, nothing done");
    CHECK(zui_vc_numbered(&VC, 1)->id == V.save && zui_vc_numbered(&VC, 2)->id == V.saveas,
          "in screen order");
    CHECK(zui_vc_numbered(&VC, 0) == 0 && zui_vc_numbered(&VC, 3) == 0, "out of range");
    CHECK(zui_vc_hear(&VC, "9") == ZUI_VC_NO_MATCH && VC.overlay == ZUI_VC_PICK,
          "a number not shown: nothing");
    CHECK(zui_vc_hear(&VC, "two") == ZUI_VC_DONE && got(V.saveas, ZUI_ACT_PRESS),
          "'two' picks Save as");
    CHECK(VC.overlay == ZUI_VC_NONE, "the disambiguation numbers go");
    CHECK(zui_vc_hear(&VC, "click delete") == ZUI_VC_NO_MATCH, "a disabled button is not there");
    CHECK(zui_vc_hear(&VC, "click nothing here") == ZUI_VC_NO_MATCH, "no such name");
    n_got = 0;
    CHECK(zui_vc_hear(&VC, "click beta.png") == ZUI_VC_DONE, "a list row by its text");
    CHECK(got(V.list, ZUI_ACT_SELECT) && UI.w[V.list].u.list.sel == 1, "row 1 selected");
    n_got = 0;
    CHECK(zui_vc_hear(&VC, "tap files") == ZUI_VC_DONE && UI.focus == V.list &&
              got(V.list, ZUI_ACT_OPEN) && GOT[0].arg == 1,
          "the list itself: focused, and its selected row opened");
    CHECK(zui_vc_hear(&VC, "click notes") == ZUI_VC_DONE && UI.focus == V.field,
          "a field takes focus");
    /* word prefix beats substring */
    n_got = 0;
    CHECK(zui_vc_hear(&VC, "click open") == ZUI_VC_DONE && got(V.open, ZUI_ACT_PRESS),
          "open: exact");
    /* VC7: held answers only by their exact name */
    n_got = 0;
    CHECK(zui_vc_hear(&VC, "click acc") == ZUI_VC_NO_MATCH && n_got == 0,
          "a partial name never accepts AI output");
    CHECK(zui_vc_hear(&VC, "click accept") == ZUI_VC_DONE && got(V.held, ZUI_ACT_ACCEPT),
          "the exact name does, because the person said it");
    n_got = 0;
    CHECK(zui_vc_hear(&VC, "click reject") == ZUI_VC_DONE && got(V.held, ZUI_ACT_REJECT),
          "reject likewise");
    /* cancel with no overlay presses a Cancel button if there is one */
    CHECK(zui_vc_hear(&VC, "cancel") == ZUI_VC_NO_MATCH, "no Cancel button: nothing");
    /* a menu or palette hides the widgets */
    zui_menu_open(&UI, V.save, 10, 10, (const char *const[]){"One"}, 0, 1);
    CHECK(zui_vc_collect(&VC) == 0, "an open menu: no widget targets");
    zui_menu_close(&UI);
    CHECK(zui_vc_collect(&VC) > 5, "closed: back");
}

/* ---- VC3: numbers --------------------------------------------------------------------- */

static void test_numbers(void)
{
    setup(false);
    CHECK(zui_vc_hear(&VC, "3") == ZUI_VC_UNKNOWN, "a number with nothing shown, nowhere to type");
    CHECK(zui_vc_hear(&VC, "tap 3") == ZUI_VC_NO_MATCH, "tap 3 with nothing shown");
    CHECK(zui_vc_hear(&VC, "show numbers") == ZUI_VC_SHOWN && VC.overlay == ZUI_VC_NUMBERS,
          "numbers shown");
    CHECK(VC.n_pick == VC.nt && VC.nt >= 10, "every target numbered (%u)", VC.nt);
    CHECK(zui_vc_numbered(&VC, 1)->id == V.save && zui_vc_numbered(&VC, 2)->id == V.saveas &&
              zui_vc_numbered(&VC, 3)->id == V.open && zui_vc_numbered(&VC, 4)->id == V.jp,
          "in reading order, the disabled one skipped");
    const zui_vc_target_t *t5 = zui_vc_numbered(&VC, 5);
    CHECK(t5->id == V.field && t5->kind == ZUI_VC_T_WIDGET && !strcmp(t5->name, "Notes"),
          "the field");
    CHECK(zui_vc_numbered(&VC, 6)->id == V.list && zui_vc_numbered(&VC, 7)->kind == ZUI_VC_T_ROW &&
              zui_vc_numbered(&VC, 7)->row == 0 &&
              !strcmp(zui_vc_numbered(&VC, 8)->name, "beta.png"),
          "the list, then its rows");
    zui_rect_t lr = UI.w[V.list].r, r7 = zui_vc_numbered(&VC, 7)->r,
               r8 = zui_vc_numbered(&VC, 8)->r;
    CHECK(r7.x == lr.x && r7.w == lr.w && r8.y == r7.y + r7.h && r7.h > 0, "row rects stack");
    uint32_t last = VC.n_pick;
    CHECK(zui_vc_numbered(&VC, last)->held && !strcmp(zui_vc_numbered(&VC, last)->name, "Reject"),
          "the held answer's buttons are numbered too, marked held");
    CHECK(zui_vc_hear(&VC, "tap 3") == ZUI_VC_DONE && n_acts == 1 &&
              ACTS[0].kind == ZUI_VC_ACT_ACTIVATE && ACTS[0].target.id == V.open,
          "tap 3: Open");
    zui_rect_t o = UI.w[V.open].r;
    CHECK(ACTS[0].x == o.x + o.w / 2 && ACTS[0].y == o.y + o.h / 2, "at its centre");
    CHECK(VC.overlay == ZUI_VC_NUMBERS, "the numbers stay");
    CHECK(zui_vc_hear(&VC, "four") == ZUI_VC_DONE && ACTS[1].target.id == V.jp,
          "a bare number word");
    CHECK(zui_vc_hear(&VC, "number 2") == ZUI_VC_DONE && ACTS[2].target.id == V.saveas, "number 2");
    CHECK(zui_vc_hear(&VC, "tap 99") == ZUI_VC_NO_MATCH && n_acts == 3, "not shown");
    CHECK(zui_vc_hear(&VC, "hide grid") == ZUI_VC_NO_MATCH && VC.overlay == ZUI_VC_NUMBERS,
          "hide grid does not hide numbers");
    CHECK(zui_vc_hear(&VC, "hide numbers") == ZUI_VC_SHOWN && VC.overlay == ZUI_VC_NONE, "hidden");
    CHECK(zui_vc_hear(&VC, "hide numbers") == ZUI_VC_NO_MATCH, "already hidden");
    CHECK(zui_vc_numbered(&VC, 1) == 0, "no numbers now");
    zui_vc_hear(&VC, "show numbers");
    CHECK(zui_vc_hear(&VC, "cancel") == ZUI_VC_SHOWN && VC.overlay == ZUI_VC_NONE, "cancel hides");
    /* numbers counted again after an action that changed the screen */
    zui_vc_on_act(&VC, 0, 0);
    zui_vc_hear(&VC, "show numbers");
    uint32_t before = VC.n_pick;
    zui_set_hidden(&UI, V.open, true);
    zui_vc_hear(&VC, "tap 1");
    CHECK(VC.overlay == ZUI_VC_NUMBERS && VC.n_pick == before - 1, "recounted (%u -> %u)", before,
          VC.n_pick);
}

/* ---- VC4: grid ----------------------------------------------------------------------------- */

static void test_grid(void)
{
    setup(false);
    CHECK(zui_vc_hear(&VC, "show grid") == ZUI_VC_SHOWN && VC.overlay == ZUI_VC_GRID &&
              VC.grid.w == W_ && VC.grid.h == H_ && VC.grid_depth == 0,
          "the grid over the screen");
    zui_rect_t c = zui_vc_grid_cell(&VC, 5);
    CHECK(c.x == 426 && c.y == 266 && c.w == 427 && c.h == 267, "cell 5 (%d,%d %dx%d)", c.x, c.y,
          c.w, c.h);
    c = zui_vc_grid_cell(&VC, 9);
    CHECK(c.x + c.w == W_ && c.y + c.h == H_, "cell 9 reaches the corner");
    c = zui_vc_grid_cell(&VC, 1);
    CHECK(c.x == 0 && c.y == 0 && c.w == 426, "cell 1");
    CHECK(zui_vc_grid_cell(&VC, 0).w == 0 && zui_vc_grid_cell(&VC, 10).w == 0, "no cell 0 or 10");
    CHECK(zui_vc_hear(&VC, "5") == ZUI_VC_SHOWN && VC.grid.x == 426 && VC.grid.w == 427 &&
              VC.grid_depth == 1,
          "5 zooms in");
    CHECK(zui_vc_hear(&VC, "12") == ZUI_VC_NO_MATCH, "no cell 12");
    CHECK(zui_vc_hear(&VC, "five") == ZUI_VC_SHOWN && VC.grid_depth == 2 && VC.grid.w == 142,
          "again (%d)", VC.grid.w);
    zui_rect_t g = VC.grid;
    CHECK(zui_vc_hear(&VC, "5") == ZUI_VC_DONE && n_acts == 1 &&
              ACTS[0].kind == ZUI_VC_ACT_CLICK_AT,
          "a cell under 48 px clicks");
    zui_rect_t c5 = {g.x + g.w / 3, g.y + g.h / 3, g.w * 2 / 3 - g.w / 3, g.h * 2 / 3 - g.h / 3};
    CHECK(ACTS[0].x == c5.x + c5.w / 2 && ACTS[0].y == c5.y + c5.h / 2, "at the cell's centre");
    CHECK(VC.overlay == ZUI_VC_NONE, "and the grid goes");
    zui_vc_hear(&VC, "show grid");
    CHECK(zui_vc_hear(&VC, "tap 1") == ZUI_VC_DONE && ACTS[1].x == 213 && ACTS[1].y == 133,
          "tap 1 clicks at once (%d,%d)", ACTS[1].x, ACTS[1].y);
    zui_vc_hear(&VC, "show grid");
    CHECK(zui_vc_hear(&VC, "hide numbers") == ZUI_VC_NO_MATCH && VC.overlay == ZUI_VC_GRID,
          "hide numbers leaves the grid");
    CHECK(zui_vc_hear(&VC, "hide grid") == ZUI_VC_SHOWN && VC.overlay == ZUI_VC_NONE, "hide grid");
    zui_vc_set_area(&VC, (zui_rect_t){0, 0, 0, 0});
    CHECK(zui_vc_hear(&VC, "show grid") == ZUI_VC_NO_MATCH, "no area, no grid");
    /* a click on the grid lands in the ui */
    setup(true);
    zui_vc_hear(&VC, "show grid");
    zui_rect_t sv = UI.w[V.save].r;
    VC.grid = (zui_rect_t){sv.x, sv.y, sv.w * 3, sv.h * 3}; /* cell 1 is the Save button */
    CHECK(zui_vc_hear(&VC, "tap one") == ZUI_VC_DONE && got(V.save, ZUI_ACT_PRESS),
          "a grid click presses what is there");
}

/* ---- VC5, VC6 --------------------------------------------------------------------------- */

static void test_rest(void)
{
    setup(false);
    zui_focus(&UI, V.list, true);
    CHECK(zui_vc_hear(&VC, "scroll down") == ZUI_VC_DONE && ACTS[0].kind == ZUI_VC_ACT_SCROLL,
          "scroll down");
    zui_rect_t lr = UI.w[V.list].r;
    CHECK(ACTS[0].x == lr.x + lr.w / 2 && ACTS[0].y == lr.y + lr.h / 2 &&
              ACTS[0].dy == lr.h * 3 / 4,
          "on the focused list, three quarters of it");
    CHECK(zui_vc_hear(&VC, "scroll up") == ZUI_VC_DONE && ACTS[1].dy == -(lr.h * 3 / 4), "up");
    UI.focus = ZUI_NONE;
    zui_vc_hear(&VC, "scroll up");
    CHECK(ACTS[2].x == W_ / 2 && ACTS[2].y == H_ / 2 && ACTS[2].dy == -600,
          "no focus: the middle of the screen");
    zui_vc_set_area(&VC, (zui_rect_t){0, 0, 0, 0});
    CHECK(zui_vc_hear(&VC, "scroll up") == ZUI_VC_NO_MATCH, "nothing to scroll");
    zui_vc_attach(&VC, &UI, 0, 0, 0);
    CHECK(zui_vc_hear(&VC, "go back") == ZUI_VC_DONE && ACTS[3].kind == ZUI_VC_ACT_BACK, "back");
    /* apps */
    CHECK(zui_vc_hear(&VC, "open quillon") == ZUI_VC_DONE && ACTS[4].kind == ZUI_VC_ACT_OPEN_APP &&
              ACTS[4].app == -1 && !strcmp(ACTS[4].text, "quillon"),
          "no app list: the name as said goes to the launcher");
    static const char *const APPS[] = {"Burrow", "Glint", "Quillon Notes", 0};
    zui_vc_set_apps(&VC, APPS, 4);
    CHECK(zui_vc_hear(&VC, "open glint") == ZUI_VC_DONE && ACTS[5].app == 1 &&
              !strcmp(ACTS[5].text, "Glint"),
          "open glint");
    CHECK(zui_vc_hear(&VC, "launch quillon") == ZUI_VC_DONE && ACTS[6].app == 2,
          "by its first word");
    CHECK(zui_vc_hear(&VC, "open rro") == ZUI_VC_DONE && ACTS[7].app == 0, "by part of it");
    CHECK(zui_vc_hear(&VC, "open nothing") == ZUI_VC_NO_MATCH && n_acts == 8, "no such app");
    zui_vc_set_apps(&VC, 0, 9);
    CHECK(VC.n_apps == 0, "no list");
    CHECK(zui_vc_hear(&VC, "switch to glint") == ZUI_VC_NO_MATCH, "no windows");
    CHECK(zui_vc_hear(&VC, "close window") == ZUI_VC_NO_MATCH, "no window to close");
}

static void test_typing(void)
{
    setup(true);
    CHECK(zui_vc_hear(&VC, "type hello") == ZUI_VC_NO_MATCH, "no field focused: nothing typed");
    zui_focus(&UI, V.field, true);
    CHECK(zui_vc_hear(&VC, "Type Hello World.") == ZUI_VC_DONE, "type");
    CHECK(!strcmp(V.buf, "Hello World."), "as said, with its stop: '%s'", V.buf);
    CHECK(got(V.field, ZUI_ACT_CHANGE) && !got(V.field, ZUI_ACT_SUBMIT),
          "changed, never submitted");
    CHECK(zui_vc_hear(&VC, "select world") == ZUI_VC_DONE, "select");
    uint32_t a, b;
    zui_field_selection(&UI, V.field, &a, &b);
    CHECK(a == 6 && b == 11, "the word selected (%u..%u)", a, b);
    CHECK(zui_vc_hear(&VC, "select planet") == ZUI_VC_NO_MATCH, "not there");
    CHECK(zui_vc_hear(&VC, "type Earth") == ZUI_VC_DONE && !strcmp(V.buf, "Hello Earth."),
          "typing replaces the selection: '%s'", V.buf);
    /* VC6 dictation */
    zui_field_select(&UI, V.field, V.buf[0] ? 12 : 0, 12);
    CHECK(zui_vc_hear(&VC, "this is dictated, really") == ZUI_VC_DICTATED, "dictated");
    CHECK(!strcmp(V.buf, "Hello Earth. this is dictated, really"), "with a joining space: '%s'",
          V.buf);
    char longs[120];
    memset(longs, 'x', 100);
    longs[100] = 0;
    char said[140];
    snprintf(said, sizeof said, "type %s", longs);
    zui_field_select(&UI, V.field, 0, (uint32_t) strlen(V.buf));
    zui_vc_hear(&VC, said);
    CHECK(!strcmp(V.buf, longs), "a long text arrives in pieces, all of it (%u)",
          (unsigned) strlen(V.buf));
    zui_field_select(&UI, V.field, 0, 100);
    zui_vc_hear(&VC,
                "type caf\xC3\xA9 caf\xC3\xA9 caf\xC3\xA9 caf\xC3\xA9 caf\xC3\xA9 caf\xC3\xA9");
    CHECK(!strcmp(V.buf, "caf\xC3\xA9 caf\xC3\xA9 caf\xC3\xA9 caf\xC3\xA9 caf\xC3\xA9 caf\xC3\xA9"),
          "pieces never split a character");
    n_got = 0;
    zui_vc_set_dictation(&VC, ZUI_VC_DICT_OFF);
    CHECK(zui_vc_hear(&VC, "more words") == ZUI_VC_UNKNOWN, "dictation off: not typed");
    zui_vc_set_dictation(&VC, (zui_vc_dict_t) 9);
    CHECK(VC.dictation == ZUI_VC_DICT_AUTO, "a bad mode is auto");
    CHECK(zui_vc_hear(&VC, "start dictation") == ZUI_VC_SHOWN && VC.dictation == ZUI_VC_DICT_ON,
          "dictation on");
    zui_field_select(&UI, V.field, 0, (uint32_t) strlen(V.buf));
    CHECK(zui_vc_hear(&VC, "click save") == ZUI_VC_DICTATED && !strcmp(V.buf, "click save") &&
              !got(V.save, ZUI_ACT_PRESS),
          "dictation on: a command is typed, not run");
    CHECK(zui_vc_hear(&VC, "stop dictation") == ZUI_VC_SHOWN && VC.dictation == ZUI_VC_DICT_AUTO,
          "stop dictation");
    zui_focus(&UI, V.save, true);
    CHECK(zui_vc_hear(&VC, "lorem ipsum") == ZUI_VC_UNKNOWN, "no field: nowhere to dictate");
    CHECK(zui_vc_hear(&VC, "   ") == ZUI_VC_EMPTY && zui_vc_hear(&VC, 0) == ZUI_VC_EMPTY, "empty");
    zui_w(&UI, V.field)->u.field.readonly = true;
    zui_focus(&UI, V.field, true);
    CHECK(zui_vc_hear(&VC, "lorem ipsum") == ZUI_VC_UNKNOWN, "a read-only field is not typed into");
}

/* ---- the window manager ----------------------------------------------------------------- */

static zui_wm_t WM;
static void test_wm(void)
{
    zui_wm_init(&WM, W_, H_, false);
    zui_wm_use_dock(&WM, false); /* the strip, behind its switch */
    uint32_t a = zui_wm_open(&WM, "burrow", "Burrow", 0, 0);
    uint32_t b = zui_wm_open(&WM, "glint", "Glint Photos", 0, 0);
    zui_vc_init(&VC);
    zui_vc_attach(&VC, 0, 0, 0, &WM);
    CHECK(VC.area.w == W_ && VC.area.h == H_, "area from the wm");
    CHECK(zui_wm_front(&WM) == b, "b in front");
    CHECK(zui_vc_hear(&VC, "switch to burrow") == ZUI_VC_DONE && zui_wm_front(&WM) == a,
          "switch to burrow");
    CHECK(zui_vc_hear(&VC, "switch to photos") == ZUI_VC_DONE && zui_wm_front(&WM) == b,
          "by part of the title");
    CHECK(zui_vc_hear(&VC, "switch to nothing") == ZUI_VC_NO_MATCH, "no such window");
    CHECK(zui_vc_collect(&VC) == 2 && VC.t[0].kind == ZUI_VC_T_STRIP &&
              !strcmp(VC.t[1].name, "Glint Photos"),
          "strip slots are targets, named by title");
    CHECK(zui_vc_hear(&VC, "click burrow") == ZUI_VC_DONE && zui_wm_front(&WM) == a,
          "click a strip slot by name");
    CHECK(zui_vc_hear(&VC, "close window") == ZUI_VC_DONE && !zui_wm_get(&WM, a) &&
              zui_wm_front(&WM) == b,
          "close the window in front");
    CHECK(zui_vc_hear(&VC, "close window") == ZUI_VC_DONE && zui_wm_front(&WM) == 0,
          "and the last");
    CHECK(zui_vc_hear(&VC, "close window") == ZUI_VC_NO_MATCH, "none left");
    CHECK(zui_vc_hear(&VC, "go back") == ZUI_VC_DONE, "back through the wm");
}

/* VC8: the dock (the default) */
static int vlaunched;
static uint32_t v_launch(zui_wm_t *wm, const char *app, const char *title, void *ctx)
{
    (void) ctx;
    vlaunched++;
    return zui_wm_open(wm, app, title, 0, 0);
}

static void test_dock(void)
{
    zui_wm_init(&WM, W_, H_, false);
    uint32_t a = zui_wm_open(&WM, "burrow", "Burrow", 0, 0);
    uint32_t b = zui_wm_open(&WM, "glint", "Glint Photos", 0, 0);
    zui_dock_pin(&WM.dock, "atlas", "Atlas");
    WM.launch_app = v_launch;
    zui_vc_init(&VC);
    zui_vc_attach(&VC, 0, 0, 0, &WM);
    zui_rect_t seal = zui_dock_keepout(&WM.dock);
    CHECK(zui_vc_collect(&VC) == 4 && VC.t[0].kind == ZUI_VC_T_DOCK && VC.t[0].id == -1 &&
              !strcmp(VC.t[0].name, "Apps") && !strcmp(VC.t[1].name, "Glint Photos") &&
              !strcmp(VC.t[3].name, "Atlas") && VC.t[3].r.x == seal.x && VC.t[3].r.w == seal.w,
          "closed: the seal and every app on it, named by title");
    CHECK(zui_vc_hear(&VC, "click burrow") == ZUI_VC_DONE && zui_wm_front(&WM) == a,
          "click an open app by name: it comes forward");
    CHECK(zui_vc_hear(&VC, "click burrow") == ZUI_VC_DONE && zui_wm_get(&WM, a)->minimised,
          "the one in front: minimised, as its slot would");
    zui_wm_focus(&WM, b);
    CHECK(zui_vc_hear(&VC, "show numbers") == ZUI_VC_SHOWN && zui_dock_is_open(&WM.dock) &&
              VC.dock_shown,
          "show numbers opens the dock");
    zui_dock_slot_t s;
    zui_dock_slot(&WM.dock, 0, &s);
    const zui_vc_target_t *t = zui_vc_numbered(&VC, 2);
    int32_t h = WM.dock.icon / 2 + ZUI_DOCK_PAD;
    CHECK(VC.n_pick == 4 && t && t->kind == ZUI_VC_T_DOCK && t->row == 0 && t->r.x == s.cx - h &&
              t->r.y == s.cy - h,
          "its slots are numbered where they are");
    CHECK(zui_vc_hear(&VC, "hide numbers") == ZUI_VC_SHOWN && !zui_dock_is_open(&WM.dock) &&
              !VC.dock_shown,
          "and it closes with the numbers");
    zui_vc_hear(&VC, "show numbers");
    uint32_t n_atlas = 0;
    for (uint32_t i = 1; i <= VC.n_pick; i++)
        if (!strcmp(zui_vc_numbered(&VC, i)->name, "Atlas")) n_atlas = i;
    char say[16];
    snprintf(say, sizeof say, "tap %u", n_atlas);
    CHECK(n_atlas && zui_vc_hear(&VC, say) == ZUI_VC_DONE && vlaunched == 1 &&
              !strcmp(zui_wm_get(&WM, zui_wm_front(&WM))->app, "atlas"),
          "a pinned app by number: launched");
    CHECK(VC.overlay == ZUI_VC_NUMBERS && zui_dock_is_open(&WM.dock) && VC.n_pick == 4,
          "the numbers stay, counted again on the open dock");
    zui_vc_hear(&VC, "cancel");
    CHECK(!zui_dock_is_open(&WM.dock), "cancel closes it");
    CHECK(zui_vc_hear(&VC, "click apps") == ZUI_VC_DONE && zui_dock_is_open(&WM.dock),
          "click apps opens the dock");
    CHECK(zui_vc_hear(&VC, "click apps") == ZUI_VC_DONE && !zui_dock_is_open(&WM.dock),
          "and again closes it");
    /* open <app> */
    uint32_t atl = zui_wm_front(&WM);
    zui_wm_focus(&WM, a);
    CHECK(zui_vc_hear(&VC, "open atlas") == ZUI_VC_DONE && zui_wm_front(&WM) == atl &&
              vlaunched == 1,
          "open: an open app is raised, not launched again");
    zui_wm_close(&WM, atl);
    CHECK(zui_vc_hear(&VC, "open atlas") == ZUI_VC_DONE && vlaunched == 2,
          "open: a pinned app is launched");
    static const char *const LIST[] = {"Lantern", 0};
    zui_vc_set_apps(&VC, LIST, 1);
    CHECK(zui_vc_hear(&VC, "open lantern") == ZUI_VC_DONE && vlaunched == 3 &&
              !strcmp(zui_wm_get(&WM, zui_wm_front(&WM))->app, "Lantern"),
          "an app from the list is launched through the wm");
    CHECK(zui_vc_hear(&VC, "open nothing") == ZUI_VC_NO_MATCH, "no such app");
    CHECK(zui_vc_hear(&VC, "switch to photos") == ZUI_VC_DONE && zui_wm_front(&WM) == b,
          "switch to is by window, as before");
    /* a name several apps share: numbered on the open dock */
    zui_wm_open(&WM, "glint", "Glint Photos", 0, 0);
    CHECK(zui_vc_hear(&VC, "click glint") == ZUI_VC_AMBIGUOUS && zui_dock_is_open(&WM.dock) &&
              VC.n_pick == 2 && zui_vc_numbered(&VC, 1)->row >= 0,
          "ambiguous: the dock opens and both are numbered on their slots");
    CHECK(zui_vc_hear(&VC, "1") == ZUI_VC_DONE && !zui_dock_is_open(&WM.dock) &&
              VC.overlay == ZUI_VC_NONE,
          "choosing one closes the numbers and the dock");
    zui_vc_attach(&VC, 0, 0, 0, 0);
    zui_vc_action_t act;
    memset(&act, 0, sizeof act);
    act.kind = ZUI_VC_ACT_ACTIVATE;
    act.target.kind = ZUI_VC_T_DOCK;
    CHECK(!zui_vc_apply(&VC, &act), "a dock target with no wm");
}

static void test_apply_edges(void)
{
    setup(true);
    zui_vc_action_t a;
    memset(&a, 0, sizeof a);
    CHECK(!zui_vc_apply(&VC, 0), "NULL");
    a.kind = ZUI_VC_ACT_OPEN_APP;
    CHECK(!zui_vc_apply(&VC, &a), "opening an app is the launcher's");
    a.kind = ZUI_VC_ACT_ACTIVATE;
    a.target.id = 9999;
    CHECK(!zui_vc_apply(&VC, &a), "a bad widget");
    a.target.kind = ZUI_VC_T_STRIP;
    CHECK(!zui_vc_apply(&VC, &a), "a strip slot with no wm");
    a.kind = ZUI_VC_ACT_TYPE;
    a.widget = V.save;
    snprintf(a.text, sizeof a.text, "x");
    CHECK(!zui_vc_apply(&VC, &a), "typing into what is not focused");
    a.kind = ZUI_VC_ACT_SELECT_TEXT;
    a.widget = 9999;
    CHECK(!zui_vc_apply(&VC, &a), "selecting in a bad widget");
    a.kind = ZUI_VC_ACT_SWITCH_TO;
    CHECK(!zui_vc_apply(&VC, &a), "no wm to switch");
    a.kind = ZUI_VC_ACT_CLOSE_WINDOW;
    CHECK(!zui_vc_apply(&VC, &a), "no wm to close");
    a.kind = 77;
    CHECK(!zui_vc_apply(&VC, &a), "unknown action");
    zui_vc_init(&VC);
    a.kind = ZUI_VC_ACT_BACK;
    CHECK(!zui_vc_apply(&VC, &a), "back with nothing attached");
    a.kind = ZUI_VC_ACT_CLICK_AT;
    CHECK(!zui_vc_apply(&VC, &a), "click with nothing attached");
    /* the origin offset */
    setup(true);
    zui_vc_attach(&VC, &UI, 100, 40, 0);
    zui_vc_collect(&VC);
    CHECK(VC.t[0].r.x == UI.w[V.save].r.x + 100 && VC.t[0].r.y == UI.w[V.save].r.y + 40,
          "targets in screen space");
    n_got = 0;
    CHECK(zui_vc_hear(&VC, "click alpha.txt") == ZUI_VC_DONE && got(V.list, ZUI_ACT_SELECT) &&
              UI.w[V.list].u.list.sel == 0,
          "a row click with an origin lands on the row");
}

/* ---- drawing ----------------------------------------------------------------------------- */

static void clear_fb(void)
{
    for (int i = 0; i < W_ * H_; i++) FB[i] = ZUI_VOID;
}

static void test_draw(void)
{
    setup(false);
    clear_fb();
    zui_vc_draw(&VC, &S, 0);
    uint32_t any = 0;
    for (int i = 0; i < W_ * H_; i++) any += FB[i] != ZUI_VOID;
    CHECK(any == 0, "no overlay: nothing drawn");
    zui_vc_hear(&VC, "show numbers");
    zui_vc_draw(&VC, &S, 0);
    zui_rect_t r = UI.w[V.open].r;
    CHECK(FB[(r.y + 12) * W_ + r.x + 12] == ZUI_PANEL, "a badge in the corner of each target");
    uint32_t gold = 0;
    for (int i = 0; i < W_ * H_; i++) gold += FB[i] == ZUI_GOLD;
    CHECK(gold == 0, "badges are not gold");
    static zui_glyph_cache_t cache;
    static ttf_font_t font;
    CHECK(ttf_parse(&font, MINI_TTF, sizeof MINI_TTF), "fixture font");
    zui_glyph_cache_init(&cache);
    zui_text_t tx = {&font, &cache, 14, false};
    static uint32_t A[W_ * H_];
    memcpy(A, FB, sizeof A);
    clear_fb();
    zui_vc_draw(&VC, &S, &tx);
    CHECK(FB[(r.y + 12) * W_ + r.x + 12] == ZUI_PANEL, "with text the badges still draw");
    memcpy(A, FB, sizeof A);
    clear_fb();
    zui_vc_draw(&VC, &S, &tx);
    CHECK(memcmp(A, FB, sizeof A) == 0, "deterministic");
    zui_vc_hear(&VC, "click sa");
    clear_fb();
    zui_vc_draw(&VC, &S, 0);
    zui_rect_t sv = UI.w[V.saveas].r;
    CHECK(FB[(sv.y + sv.h - 1) * W_ + sv.x + sv.w / 2] != ZUI_VOID, "a choice is outlined");
    CHECK(FB[(r.y + 12) * W_ + r.x + 12] == ZUI_VOID, "what is not a choice has no badge");
    zui_vc_hear(&VC, "cancel");
    zui_vc_hear(&VC, "show grid");
    clear_fb();
    zui_vc_draw(&VC, &S, 0);
    CHECK(FB[100 * W_ + 426] == ZUI_TEXT_2 && FB[100 * W_ + 425] == ZUI_ENGRAVE_LO,
          "grid lines engraved");
    CHECK(FB[266 * W_ + 100] == ZUI_TEXT_2, "and across");
    zui_rect_t c5 = zui_vc_grid_cell(&VC, 5);
    CHECK(FB[(c5.y + c5.h / 2) * W_ + c5.x + c5.w / 2] == ZUI_PANEL, "a number in each cell");
}

int main(void)
{
    test_parse();
    test_locales();
    test_names();
    test_numbers();
    test_grid();
    test_rest();
    test_typing();
    test_wm();
    test_dock();
    test_apply_edges();
    test_draw();
    printf("%s test_zui_voicectl: %d/%d checks\n", fails ? "[FAIL]" : "[PASS]", checks - fails,
           checks);
    return fails ? 1 : 0;
}
