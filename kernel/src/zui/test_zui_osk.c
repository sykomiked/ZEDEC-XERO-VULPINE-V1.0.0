/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zui_osk.c — host tests for the on-screen keyboard: pages built from
 * layouts, quarter-unit rows, touch targets and hit slots, the events it
 * sends, shift and caps lock, key repeat, two-finger typing, and the
 * pixels it draws. */
#include <stdio.h>
#include <string.h>

#include "zui_osk.h"
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

static zui_keymap_t KM;
static zui_osk_t OSK;
static zui_event_t LOG[256];
static uint32_t NLOG;
static char DOC[256];
static uint32_t T = 1000;

static void reset_log(void)
{
    NLOG = 0;
    DOC[0] = 0;
}

static void feed(const zui_event_t *e)
{
    zui_event_t out[ZUI_OSK_EV_MAX];
    uint32_t n = zui_osk_feed(&OSK, e, out, ZUI_OSK_EV_MAX);
    for (uint32_t i = 0; i < n; i++) {
        if (NLOG < 256) LOG[NLOG++] = out[i];
        if (out[i].type == ZUI_EV_TEXT) strcat(DOC, out[i].text);
        if (out[i].type == ZUI_EV_KEY_DOWN && out[i].key == ZUI_KEY_BACKSPACE && DOC[0])
            DOC[strlen(DOC) - 1] = 0;
    }
}

static zui_event_t ev(uint8_t type, int32_t x, int32_t y, uint16_t id, uint32_t ms)
{
    zui_event_t e;
    memset(&e, 0, sizeof e);
    e.type = type;
    e.x = x;
    e.y = y;
    e.touch_id = id;
    e.button = ZUI_BTN_LEFT;
    e.ms = ms;
    return e;
}

static int32_t find(const char *label)
{
    for (uint32_t i = 0; i < OSK.nkeys; i++)
        if (strcmp(OSK.keys[i].label, label) == 0) return (int32_t) i;
    return -1;
}

static int32_t find_kind(uint8_t kind)
{
    for (uint32_t i = 0; i < OSK.nkeys; i++)
        if (OSK.keys[i].kind == kind) return (int32_t) i;
    return -1;
}

static void cx(int32_t k, int32_t *x, int32_t *y)
{
    *x = OSK.keys[k].r.x + OSK.keys[k].r.w / 2;
    *y = OSK.keys[k].r.y + OSK.keys[k].r.h / 2;
}

static void touch_key(int32_t k)
{
    int32_t x, y;
    cx(k, &x, &y);
    zui_event_t a = ev(ZUI_EV_TOUCH_BEGIN, x, y, 0, T);
    zui_event_t b = ev(ZUI_EV_TOUCH_END, x, y, 0, T + 60);
    feed(&a);
    feed(&b);
    T += 100;
}

static void touch(const char *label)
{
    int32_t k = find(label);
    CHECK(k >= 0, "key %s exists", label);
    if (k >= 0) touch_key(k);
}

static uint32_t row_q(uint32_t r)
{
    uint32_t q = 0;
    for (uint32_t i = 0; i < OSK.nkeys; i++)
        if (OSK.keys[i].row == r) q += OSK.keys[i].qw;
    return q;
}

static uint32_t row_n(uint32_t r)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < OSK.nkeys; i++)
        if (OSK.keys[i].row == r && OSK.keys[i].kind == ZUI_OSKK_CHAR) n++;
    return n;
}

static void setup(const char *layout, int32_t w, int32_t h)
{
    zui_keymap_load(&KM, layout);
    zui_osk_init(&OSK, &KM);
    zui_rect_t r = {0, 0, w, h};
    zui_osk_layout(&OSK, r, 256);
    reset_log();
}

/* ---- O1, O2: pages ------------------------------------------------------------------- */

static void test_pages(void)
{
    setup("us", 800, 240);
    CHECK(OSK.nrows == 4, "four rows");
    CHECK(row_n(0) == 10 && row_n(1) == 9 && row_n(2) == 7, "US letters: 10 / 9 / 7");
    CHECK(OSK.keys[0].pos == ZUI_KP(ZUI_KP_ROW_D, 1) && strcmp(OSK.keys[0].label, "q") == 0 &&
              OSK.keys[0].key == 'q',
          "first key is the layout's D01");
    CHECK(OSK.grid_q == 40, "grid is ten keys of four quarters (%u)", OSK.grid_q);
    for (uint32_t r = 0; r < 4; r++) CHECK(row_q(r) <= OSK.grid_q, "row %u fits", r);
    CHECK(row_q(2) == 40 && row_q(3) == 40, "shift row and bottom row stretch to the grid");
    int32_t a = find("a");
    CHECK(a >= 0 && OSK.keys[a].qx == 2, "a centred row starts half a key in");
    int32_t sp = find_kind(ZUI_OSKK_SPACE);
    CHECK(sp >= 0 && OSK.keys[sp].qw == 40 - 6 - 4 - 4 - 8, "the space bar takes the rest");
    int32_t sh = find_kind(ZUI_OSKK_SHIFT), bk = find_kind(ZUI_OSKK_BACKSPACE);
    CHECK(OSK.keys[sh].qw == 6 && OSK.keys[bk].qw == 6, "shift and backspace: 1.5 keys");

    setup("de", 800, 240);
    CHECK(row_n(0) == 12 && strcmp(OSK.keys[10].label, "ü") == 0 &&
              strcmp(OSK.keys[11].label, "ß") == 0,
          "German top row: ü, then ß from the digit row (its shift is ?)");
    CHECK(row_n(1) == 11, "German home row has ö ä");
    CHECK(find("z") >= 0 && OSK.keys[find("z")].pos == ZUI_KP(ZUI_KP_ROW_D, 6), "z where it is");
    CHECK(OSK.grid_q == 48, "grid widens for twelve keys (%u)", OSK.grid_q);
    setup("fr", 800, 240);
    CHECK(row_n(0) == 10 && strcmp(OSK.keys[0].label, "a") == 0, "AZERTY top row");
    CHECK(row_n(1) == 11 && find("m") >= 0 && OSK.keys[find("m")].row == 1 && find("ù") >= 0,
          "m and ù on the home row");
    CHECK(find("é") < 0, "é (shift 2) stays off the letters page");
    CHECK(row_n(2) == 6, "AZERTY bottom: w x c v b n");
    setup("ru", 800, 240);
    CHECK(row_n(0) == 13 && row_n(1) == 11 && row_n(2) == 9, "Russian 13 / 11 / 9 (%u %u %u)",
          row_n(0), row_n(1), row_n(2));
    CHECK(find("ё") >= 0 && OSK.keys[find("ё")].row == 0, "ё from the digit row joins the top");
    setup("ar", 800, 240);
    CHECK(find("ذ") >= 0 && OSK.keys[find("ذ")].row == 0, "Arabic ذ joins the top row");
    CHECK(find("لا") < 0, "a two-character key is left to the hardware layout");
    setup("he", 800, 240);
    CHECK(find("/") < 0 && find("ק") >= 0, "Hebrew: punctuation keys are not letters");

    /* shift shows capitals */
    setup("de", 800, 240);
    zui_osk_set_page(&OSK, ZUI_OSK_SHIFT);
    CHECK(find("Ü") >= 0 && find("Q") >= 0 && find("q") < 0, "shift page: capitals");
    zui_osk_set_page(&OSK, ZUI_OSK_SYMBOLS);
    CHECK(find("@") >= 0 && find("1") >= 0 && find("ABC") >= 0 && find("123") >= 0, "symbols page");
    CHECK(OSK.keys[find("(")].key == '(' && OSK.keys[find("(")].pos == -1, "fixed keys");
    zui_osk_set_page(&OSK, ZUI_OSK_NUMBERS);
    CHECK(OSK.nrows == 4 && OSK.grid_q == 32 && find("5") >= 0 && OSK.keys[find("5")].qw == 8,
          "numbers page: a keypad");
    zui_osk_set_page(&OSK, 9);
    CHECK(OSK.page == ZUI_OSK_NUMBERS, "an unknown page is ignored");

    /* no layout */
    zui_osk_init(&OSK, 0);
    CHECK(find("q") >= 0 && find("m") >= 0 && row_n(0) == 10, "no layout: a plain QWERTY");
    zui_osk_set_page(&OSK, ZUI_OSK_SHIFT);
    CHECK(find("Q") >= 0, "and its capitals");
    zui_osk_set_keymap(&OSK, &KM);
    CHECK(find("Q") >= 0 && find("Ü") >= 0, "set_keymap keeps the page");
}

/* ---- O3: targets ---------------------------------------------------------------------- */

static void test_targets(void)
{
    setup("us", 800, 240);
    CHECK(OSK.row_h == 60 && OSK.top == 0 && !OSK.cramped, "four 60 px rows, room for all");
    int32_t q = find("q");
    CHECK(OSK.keys[q].hit.x == 0 && OSK.keys[q].hit.w == 80 && OSK.keys[q].hit.h == 60,
          "q slot: 80 x 60");
    CHECK(OSK.keys[q].r.x == 3 && OSK.keys[q].r.w == 74 && OSK.keys[q].r.h == 54,
          "q cap is inset by the gap");
    CHECK(zui_osk_hit(&OSK, 0, 0) == q && zui_osk_hit(&OSK, 79, 59) == q, "slot corners");
    CHECK(zui_osk_hit(&OSK, 80, 10) == find("w"), "the next slot starts at once");
    CHECK(zui_osk_hit(&OSK, 2, 70) == find("a"), "the row's left margin belongs to a");
    CHECK(zui_osk_hit(&OSK, 798, 70) == find("l"), "the right margin belongs to l");
    CHECK(zui_osk_hit(&OSK, -1, 10) == -1 && zui_osk_hit(&OSK, 10, 240) == -1,
          "outside the keyboard");
    for (uint32_t i = 0; i < OSK.nkeys; i++)
        CHECK(OSK.keys[i].hit.h >= ZUI_TOUCH_MIN, "key %u is at least 44 tall", i);
    /* every pixel in the keyboard hits some key */
    uint32_t miss = 0;
    for (int32_t y = 0; y < 240; y += 3)
        for (int32_t x = 0; x < 800; x += 3)
            if (zui_osk_hit(&OSK, x, y) < 0) miss++;
    CHECK(miss == 0, "no tap falls between keys (%u misses)", miss);

    /* a short rect: rows keep 44, the keyboard sits at the bottom */
    setup("us", 800, 100);
    CHECK(OSK.row_h == 44 && OSK.top == 100 - 176, "short rect: 44 px rows from the bottom");
    CHECK(zui_osk_min_height(&OSK, 256) == 176 && zui_osk_min_height(&OSK, 512) == 352 &&
              zui_osk_min_height(&OSK, 0) == 176,
          "min height scales");
    /* a phone: 360 px wide is cramped */
    setup("us", 360, 240);
    CHECK(OSK.cramped, "36 px keys are cramped");
    miss = 0;
    for (int32_t y = 0; y < 240; y += 2)
        for (int32_t x = 0; x < 360; x += 2)
            if (zui_osk_hit(&OSK, x, y) < 0) miss++;
    CHECK(miss == 0, "cramped slots still cover everything");
    /* 2x scale */
    zui_rect_t r = {10, 20, 1600, 400};
    zui_osk_layout(&OSK, r, 512);
    CHECK(OSK.touch_px == 88 && OSK.row_h == 100 && !OSK.cramped && OSK.top == 20,
          "2x: 88 px targets (%d)", OSK.row_h);
    CHECK(OSK.keys[0].hit.x == 10 && OSK.keys[0].r.x == 16, "offset rect and 2x gap");
}

/* ---- O4..O6: events -------------------------------------------------------------------- */

static void test_events(void)
{
    setup("us", 800, 240);
    touch("h");
    CHECK(NLOG == 3 && LOG[0].type == ZUI_EV_KEY_DOWN && LOG[0].key == 'h' && LOG[0].mods == 0 &&
              LOG[1].type == ZUI_EV_TEXT && strcmp(LOG[1].text, "h") == 0 &&
              LOG[2].type == ZUI_EV_KEY_UP && LOG[2].key == 'h',
          "a tap is key down, text, key up");
    CHECK(LOG[0].ms == T - 100 + 60, "sent on release, at release time");
    touch("i");
    int32_t sp = find_kind(ZUI_OSKK_SPACE);
    touch_key(sp);
    CHECK(strcmp(DOC, "hi ") == 0 && LOG[NLOG - 3].key == ZUI_KEY_SPACE, "space");
    reset_log();
    touch_key(find_kind(ZUI_OSKK_ENTER));
    CHECK(NLOG == 2 && LOG[0].key == ZUI_KEY_ENTER && LOG[1].type == ZUI_EV_KEY_UP,
          "enter is a key only");
    reset_log();
    touch_key(find_kind(ZUI_OSKK_BACKSPACE));
    CHECK(NLOG == 2 && LOG[0].key == ZUI_KEY_BACKSPACE, "backspace is a key only");

    /* sliding */
    reset_log();
    int32_t x, y, x2, y2;
    cx(find("q"), &x, &y);
    cx(find("w"), &x2, &y2);
    zui_event_t e = ev(ZUI_EV_TOUCH_BEGIN, x, y, 0, T);
    feed(&e);
    CHECK(OSK.pressed == find("q") && NLOG == 0, "press: nothing sent yet");
    e = ev(ZUI_EV_TOUCH_MOVE, x2, y2, 0, T + 20);
    feed(&e);
    CHECK(OSK.pressed == find("w"), "slide to w");
    e = ev(ZUI_EV_TOUCH_MOVE, x2, -50, 0, T + 30);
    feed(&e);
    CHECK(OSK.pressed == find("w"), "sliding off the keyboard keeps the key");
    e = ev(ZUI_EV_TOUCH_MOVE, x, y, 7, T + 30);
    feed(&e);
    CHECK(OSK.pressed == find("w"), "another finger's move is not ours");
    e = ev(ZUI_EV_TOUCH_END, x2, y2, 0, T + 40);
    feed(&e);
    CHECK(strcmp(DOC, "w") == 0, "lifting sends the key under the finger");
    T += 100;

    /* cancel */
    reset_log();
    e = ev(ZUI_EV_TOUCH_BEGIN, x, y, 0, T);
    feed(&e);
    e = ev(ZUI_EV_TOUCH_CANCEL, x, y, 0, T + 10);
    feed(&e);
    CHECK(NLOG == 0 && OSK.pressed == -1, "a cancelled touch sends nothing");
    T += 100;

    /* a tap outside */
    e = ev(ZUI_EV_TOUCH_BEGIN, 10, 900, 0, T);
    feed(&e);
    e = ev(ZUI_EV_TOUCH_END, 10, 900, 0, T + 10);
    feed(&e);
    CHECK(NLOG == 0 && OSK.pressed == -1, "outside: nothing");

    /* two fingers */
    reset_log();
    cx(find("t"), &x, &y);
    cx(find("o"), &x2, &y2);
    e = ev(ZUI_EV_TOUCH_BEGIN, x, y, 0, T);
    feed(&e);
    e = ev(ZUI_EV_TOUCH_BEGIN, x2, y2, 1, T + 30);
    feed(&e);
    CHECK(strcmp(DOC, "t") == 0 && OSK.pressed == find("o"), "O6: a new finger sends the held key");
    e = ev(ZUI_EV_TOUCH_END, x, y, 0, T + 40);
    feed(&e);
    CHECK(strcmp(DOC, "t") == 0, "the first finger lifting later does nothing");
    e = ev(ZUI_EV_TOUCH_END, x2, y2, 1, T + 50);
    feed(&e);
    CHECK(strcmp(DOC, "to") == 0, "the second sends on its own release");
    T += 100;

    /* pointer: ignored once touch was seen; works on a fresh keyboard */
    reset_log();
    cx(find("a"), &x, &y);
    e = ev(ZUI_EV_PTR_DOWN, x, y, 0, T);
    feed(&e);
    e = ev(ZUI_EV_PTR_UP, x, y, 0, T + 10);
    feed(&e);
    CHECK(NLOG == 0, "O6: pointer events are ignored after a touch");
    setup("us", 800, 240);
    e = ev(ZUI_EV_PTR_DOWN, x, y, 0, T);
    feed(&e);
    cx(find("s"), &x2, &y2);
    e = ev(ZUI_EV_PTR_MOVE, x2, y2, 0, T + 5);
    feed(&e);
    e = ev(ZUI_EV_PTR_UP, x2, y2, 0, T + 10);
    feed(&e);
    CHECK(strcmp(DOC, "s") == 0, "a mouse works the keyboard too, sliding as well");
    e = ev(ZUI_EV_PTR_DOWN, x, y, 0, T);
    e.button = ZUI_BTN_RIGHT;
    feed(&e);
    CHECK(OSK.pressed == -1, "only the left button");
    T += 100;

    /* queue */
    static zui_queue_t qq;
    zui_queue_init(&qq);
    cx(find("z"), &x, &y);
    e = ev(ZUI_EV_PTR_DOWN, x, y, 0, T);
    zui_osk_feed_queue(&OSK, &e, &qq);
    e = ev(ZUI_EV_PTR_UP, x, y, 0, T + 5);
    CHECK(zui_osk_feed_queue(&OSK, &e, &qq) == 3 && zui_queue_len(&qq) == 3, "onto a queue");

    /* a small out[] */
    cx(find("x"), &x, &y);
    e = ev(ZUI_EV_PTR_DOWN, x, y, 0, T);
    zui_event_t small[1];
    zui_osk_feed(&OSK, &e, small, 1);
    e = ev(ZUI_EV_PTR_UP, x, y, 0, T + 5);
    CHECK(zui_osk_feed(&OSK, &e, small, 1) == 1 && small[0].type == ZUI_EV_KEY_DOWN,
          "never more than cap");
}

static void test_shift(void)
{
    setup("us", 800, 240);
    touch("⇧");
    CHECK(OSK.page == ZUI_OSK_SHIFT && !OSK.caps_lock, "shift: capitals");
    touch("H");
    CHECK(strcmp(DOC, "H") == 0 && LOG[0].mods == ZUI_MOD_SHIFT && LOG[0].key == 'h',
          "a capital with shift held, the key still 'h'");
    CHECK(OSK.page == ZUI_OSK_LETTERS, "one capital, then back");
    touch("i");
    CHECK(strcmp(DOC, "Hi") == 0, "small again");
    /* double tap locks */
    int32_t sh = find_kind(ZUI_OSKK_SHIFT);
    int32_t x, y;
    cx(sh, &x, &y);
    zui_event_t e = ev(ZUI_EV_TOUCH_BEGIN, x, y, 0, T);
    feed(&e);
    e = ev(ZUI_EV_TOUCH_END, x, y, 0, T + 30);
    feed(&e);
    e = ev(ZUI_EV_TOUCH_BEGIN, x, y, 0, T + 150);
    feed(&e);
    e = ev(ZUI_EV_TOUCH_END, x, y, 0, T + 180);
    feed(&e);
    T += 300;
    CHECK(OSK.page == ZUI_OSK_SHIFT && OSK.caps_lock, "two quick taps: caps lock");
    reset_log();
    touch("O");
    touch("K");
    CHECK(strcmp(DOC, "OK") == 0 && OSK.caps_lock, "caps lock stays");
    touch("⇧");
    CHECK(OSK.page == ZUI_OSK_LETTERS && !OSK.caps_lock, "a third tap: off");
    /* slow second tap: back to letters */
    touch("⇧");
    T += 1000;
    touch("⇧");
    CHECK(OSK.page == ZUI_OSK_LETTERS && !OSK.caps_lock, "a slow second tap turns shift off");
    /* pages */
    reset_log();
    touch("?123");
    CHECK(OSK.page == ZUI_OSK_SYMBOLS && NLOG == 0, "?123: symbols; page keys send nothing");
    touch("@");
    touch("(");
    CHECK(strcmp(DOC, "@(") == 0 && LOG[0].key == '@', "symbols type");
    touch("123");
    CHECK(OSK.page == ZUI_OSK_NUMBERS, "123: numbers");
    touch("4");
    touch("2");
    touch("0");
    touch(".");
    CHECK(strcmp(DOC, "@(420.") == 0, "the keypad types (%s)", DOC);
    touch_key(find_kind(ZUI_OSKK_BACKSPACE));
    CHECK(strcmp(DOC, "@(420") == 0, "keypad backspace");
    touch("#+=");
    CHECK(OSK.page == ZUI_OSK_SYMBOLS, "#+=: symbols");
    touch("ABC");
    CHECK(OSK.page == ZUI_OSK_LETTERS, "ABC: letters");
    /* shift held over a new finger: the page turns back under the second */
    reset_log();
    touch("⇧");
    int32_t a = find("A"), b;
    cx(a, &x, &y);
    e = ev(ZUI_EV_TOUCH_BEGIN, x, y, 0, T);
    feed(&e);
    int32_t bx, by;
    b = find("B");
    cx(b, &bx, &by);
    e = ev(ZUI_EV_TOUCH_BEGIN, bx, by, 1, T + 20);
    feed(&e);
    CHECK(strcmp(DOC, "A") == 0 && OSK.page == ZUI_OSK_LETTERS && OSK.pressed == find("b"),
          "after the capital the new finger lands on the small b");
    e = ev(ZUI_EV_TOUCH_END, bx, by, 1, T + 40);
    feed(&e);
    CHECK(strcmp(DOC, "Ab") == 0, "Ab");
    T += 100;
}

/* ---- O5: repeat ------------------------------------------------------------------------- */

static void test_repeat(void)
{
    setup("us", 800, 240);
    int32_t x, y;
    cx(find_kind(ZUI_OSKK_BACKSPACE), &x, &y);
    strcpy(DOC, "abcdefghij");
    zui_event_t e = ev(ZUI_EV_TOUCH_BEGIN, x, y, 0, 0);
    feed(&e);
    e = ev(ZUI_EV_TICK, 0, 0, 0, 499);
    feed(&e);
    CHECK(NLOG == 0, "nothing before the delay");
    e = ev(ZUI_EV_TICK, 0, 0, 0, 500);
    feed(&e);
    CHECK(NLOG == 1 && LOG[0].key == ZUI_KEY_BACKSPACE && LOG[0].repeat == 0 &&
              strcmp(DOC, "abcdefghi") == 0,
          "at the delay: the first key down");
    e = ev(ZUI_EV_TICK, 0, 0, 0, 649);
    feed(&e);
    CHECK(NLOG == 3 && LOG[1].repeat == 1 && LOG[1].ms == 550 && LOG[2].ms == 600,
          "then every 50 ms, marked repeat, at their own times");
    e = ev(ZUI_EV_TOUCH_MOVE, x + 400, y - 100, 0, 650);
    feed(&e);
    CHECK(NLOG == 4 && OSK.pressed == find_kind(ZUI_OSKK_BACKSPACE),
          "a repeating key does not slide");
    e = ev(ZUI_EV_TOUCH_END, x, y, 0, 660);
    feed(&e);
    CHECK(NLOG == 5 && LOG[4].type == ZUI_EV_KEY_UP && strcmp(DOC, "abcdef") == 0,
          "release: key up only (%s)", DOC);
    /* a long catch-up is bounded by cap and resumes */
    reset_log();
    cx(find("k"), &x, &y);
    e = ev(ZUI_EV_TOUCH_BEGIN, x, y, 0, 10000);
    feed(&e);
    e = ev(ZUI_EV_TICK, 0, 0, 0, 10500 + 50 * 20);
    feed(&e);
    CHECK(NLOG == ZUI_OSK_EV_MAX && strcmp(DOC, "kkkk") == 0, "four repeats fit in eight");
    feed(&e);
    feed(&e);
    feed(&e);
    feed(&e);
    feed(&e);
    CHECK(strlen(DOC) == 21, "the rest follow on later events (%u)", (unsigned) strlen(DOC));
    e = ev(ZUI_EV_TOUCH_CANCEL, x, y, 0, 12000);
    feed(&e);
    CHECK(LOG[NLOG - 1].type == ZUI_EV_KEY_UP && OSK.pressed == -1,
          "a cancel while repeating lets the key up");
    /* shift and enter do not repeat */
    reset_log();
    cx(find_kind(ZUI_OSKK_ENTER), &x, &y);
    e = ev(ZUI_EV_TOUCH_BEGIN, x, y, 0, 20000);
    feed(&e);
    e = ev(ZUI_EV_TICK, 0, 0, 0, 21000);
    feed(&e);
    CHECK(NLOG == 0, "enter does not repeat");
    e = ev(ZUI_EV_TOUCH_END, x, y, 0, 21010);
    feed(&e);
    CHECK(NLOG == 2, "it sends once on release");
    /* a repeating capital still returns to small letters */
    touch("⇧");
    reset_log();
    cx(find("J"), &x, &y);
    e = ev(ZUI_EV_TOUCH_BEGIN, x, y, 0, 30000);
    feed(&e);
    e = ev(ZUI_EV_TICK, 0, 0, 0, 30500);
    feed(&e);
    e = ev(ZUI_EV_TOUCH_END, x, y, 0, 30510);
    feed(&e);
    CHECK(strcmp(DOC, "J") == 0 && OSK.page == ZUI_OSK_LETTERS, "held capital: J, then small");
    /* custom timing */
    OSK.repeat_delay_ms = 100;
    OSK.repeat_ms = 0;
    reset_log();
    cx(find("j"), &x, &y);
    e = ev(ZUI_EV_TOUCH_BEGIN, x, y, 0, 40000);
    feed(&e);
    e = ev(ZUI_EV_TICK, 0, 0, 0, 40101);
    feed(&e);
    CHECK(strlen(DOC) == 2, "a zero interval repeats each millisecond (%s)", DOC);
}

/* ---- O7: pixels ------------------------------------------------------------------------- */

static uint32_t PX[400 * 200];

static void test_paint(void)
{
    zui_surface_t s;
    zui_surface_init(&s, PX, 400, 200, 400);
    for (uint32_t i = 0; i < 400 * 200; i++) PX[i] = 0x123456;
    setup("us", 400, 200);
    zui_rect_t r = {0, 20, 400, 180};
    zui_osk_layout(&OSK, r, 256);
    zui_osk_paint(&OSK, &s, 0);
    CHECK(PX[0] == 0x123456, "outside the rect is untouched");
    CHECK(PX[20 * 400 + 0] == ZUI_VOID, "the ground is #002911");
    int32_t x, y;
    cx(find("q"), &x, &y);
    CHECK(PX[y * 400 + x] == ZUI_PANEL, "a letter cap is the first carved step");
    cx(find_kind(ZUI_OSKK_ENTER), &x, &y);
    CHECK(PX[y * 400 + x] == ZUI_GOLD, "Enter is gold");
    cx(find_kind(ZUI_OSKK_SHIFT), &x, &y);
    CHECK(PX[y * 400 + x] == ZUI_VOID, "shift is carved into the ground");
    uint32_t gold = 0;
    for (uint32_t i = 0; i < 400 * 200; i++)
        if (PX[i] == ZUI_GOLD) gold++;
    int32_t ek = find_kind(ZUI_OSKK_ENTER);
    CHECK(gold > 0 && gold <= (uint32_t) (OSK.keys[ek].r.w * OSK.keys[ek].r.h),
          "gold only on the Enter key");
    /* engraving: the cap's top edge is not the fill */
    int32_t q = find("q");
    uint32_t edge = PX[OSK.keys[q].r.y * 400 + OSK.keys[q].r.x + OSK.keys[q].r.w / 2];
    CHECK(edge != ZUI_PANEL, "the cap is engraved");
    /* pressed */
    cx(find("w"), &x, &y);
    zui_event_t e = ev(ZUI_EV_TOUCH_BEGIN, x, y, 0, 0);
    feed(&e);
    zui_osk_paint(&OSK, &s, 0);
    CHECK(PX[y * 400 + x] == ZUI_RAISED, "a pressed key is raised");
    e = ev(ZUI_EV_TOUCH_END, x, y, 0, 5);
    feed(&e);
    /* caps lock and shift marks */
    OSK.page = ZUI_OSK_SHIFT;
    OSK.caps_lock = true;
    zui_osk_paint(&OSK, &s, 0);
    cx(find_kind(ZUI_OSKK_SHIFT), &x, &y);
    CHECK(PX[y * 400 + x] == ZUI_RAISED, "caps lock shows on the shift key");
    int32_t sk = find_kind(ZUI_OSKK_SHIFT);
    uint32_t bar = 0;
    for (int32_t yy = OSK.keys[sk].r.y; yy < OSK.keys[sk].r.y + OSK.keys[sk].r.h; yy++)
        if (PX[yy * 400 + x] == ZUI_TEXT) bar++;
    CHECK(bar > 0, "with a light bar under it");
    /* deterministic */
    static uint32_t A[400 * 200];
    memcpy(A, PX, sizeof A);
    zui_osk_paint(&OSK, &s, 0);
    CHECK(memcmp(A, PX, sizeof A) == 0, "the same state paints the same pixels");

    /* with a font (the fixture inks only "A"): labels in light green */
    static zui_glyph_cache_t cache;
    static ttf_font_t font;
    CHECK(ttf_parse(&font, MINI_TTF, sizeof MINI_TTF), "fixture font");
    zui_glyph_cache_init(&cache);
    zui_text_t t = {&font, &cache, 16, false};
    zui_osk_set_page(&OSK, ZUI_OSK_SHIFT);
    zui_osk_paint(&OSK, &s, &t);
    memcpy(A, PX, sizeof A);
    zui_osk_paint(&OSK, &s, 0);
    uint32_t diff = 0;
    for (uint32_t i = 0; i < 400 * 200; i++)
        if (A[i] != PX[i]) diff++;
    CHECK(diff > 0, "labels draw (%u pixels)", diff);
    uint32_t green = 0;
    for (uint32_t i = 0; i < 400 * 200; i++)
        if (A[i] == ZUI_TEXT) green++;
    CHECK(green > 0, "in light green");
    /* a short rect still paints its ground up to the rect top */
    zui_rect_t sr = {0, 150, 400, 50};
    zui_osk_layout(&OSK, sr, 256);
    for (uint32_t i = 0; i < 400 * 200; i++) PX[i] = 0x123456;
    zui_osk_paint(&OSK, &s, 0);
    CHECK(PX[199 * 400 + 1] == ZUI_VOID && PX[OSK.top * 400 + 1] == ZUI_VOID,
          "an overflowing keyboard paints from its own top");
}

int main(void)
{
    test_pages();
    test_targets();
    test_events();
    test_shift();
    test_repeat();
    test_paint();
    printf("%s test_zui_osk: %d/%d checks\n", fails ? "[FAIL]" : "[PASS]", checks - fails, checks);
    return fails ? 1 : 0;
}
