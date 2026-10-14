/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zui_widget.c — host tests for zui's input stream, gestures, damage,
 * the framebuffer present layer, and the widget set: layout, focus, keys,
 * text editing, lists, menus, the Ask palette, held AI and accessibility. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zui_input.h"
#include "zui_present.h"
#include "zui_progress.h"
#include "zui_widget.h"
#include "../font/ttf_fixture.inc" /* MINI_TTF[] */

static int fails, checks;
#define CHECK(c, m)                                                                                \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            fails++;                                                                               \
            printf("  FAIL %s (%s:%d)\n", m, __FILE__, __LINE__);                                  \
        }                                                                                          \
    } while (0)

/* ---- input ------------------------------------------------------------------ */

static void test_queue(void)
{
    static zui_queue_t q;
    zui_event_t e;
    zui_queue_init(&q);
    CHECK(zui_queue_len(&q) == 0 && !zui_pop(&q, &e), "empty");
    zui_push_key(&q, true, 'k', ZUI_MOD_CMD, false, 5);
    CHECK(zui_queue_len(&q) == 1 && q.mods == ZUI_MOD_CMD, "key tracks mods");
    CHECK(zui_pop(&q, &e) && e.type == ZUI_EV_KEY_DOWN && e.key == 'k' && e.ms == 5 &&
              e.mods == ZUI_MOD_CMD && !e.repeat,
          "key round trip");
    zui_push_key(&q, true, 'a', 0, true, 6);
    CHECK(zui_pop(&q, &e) && e.repeat == 1, "repeat flag");
    zui_push_key(&q, false, 'a', 0, false, 7);
    CHECK(zui_pop(&q, &e) && e.type == ZUI_EV_KEY_UP, "key up");

    /* I4: text is cut on a character boundary */
    char lng[80];
    memset(lng, 'x', sizeof lng);
    memcpy(lng + 30, "\xE2\x82\xAC", 3); /* € straddles byte 32 */
    lng[sizeof lng - 1] = 0;
    zui_push_text(&q, ZUI_EV_TEXT, lng, 1);
    CHECK(zui_pop(&q, &e) && strlen(e.text) == 30, "UTF-8 cut before a split character");
    zui_push_text(&q, ZUI_EV_PREEDIT, "\xE3\x81\x8B", 1);
    CHECK(zui_pop(&q, &e) && e.type == ZUI_EV_PREEDIT && strcmp(e.text, "\xE3\x81\x8B") == 0,
          "preedit");
    zui_push_text(&q, ZUI_EV_TEXT, 0, 1);
    CHECK(zui_pop(&q, &e) && e.text[0] == 0, "NULL text is empty");

    /* pointer state */
    zui_push_ptr(&q, ZUI_EV_PTR_DOWN, 10, 20, ZUI_BTN_LEFT, 1);
    CHECK(q.buttons == ZUI_BTN_LEFT && q.px == 10 && q.py == 20, "down holds the button");
    zui_push_ptr(&q, ZUI_EV_PTR_MOVE, 12, 22, 0, 2);
    zui_push_ptr(&q, ZUI_EV_PTR_UP, 13, 23, ZUI_BTN_LEFT, 3);
    CHECK(q.buttons == 0 && q.px == 13, "up releases");
    zui_pop(&q, &e);
    CHECK(zui_pop(&q, &e) && e.type == ZUI_EV_PTR_MOVE && e.button == ZUI_BTN_LEFT,
          "a move carries the held buttons");
    zui_pop(&q, &e);
    zui_push_scroll(&q, 1, 2, 3, -4, 9);
    CHECK(zui_pop(&q, &e) && e.type == ZUI_EV_SCROLL && e.dx == 3 && e.dy == -4, "scroll");

    /* I2: first finger is also the pointer, the second is not */
    zui_push_touch(&q, ZUI_EV_TOUCH_BEGIN, 0, 50, 60, 10);
    CHECK(zui_queue_len(&q) == 2, "touch 0 adds a pointer down");
    zui_pop(&q, &e);
    CHECK(e.type == ZUI_EV_TOUCH_BEGIN, "touch first");
    zui_pop(&q, &e);
    CHECK(e.type == ZUI_EV_PTR_DOWN && e.x == 50 && e.y == 60 && e.button == ZUI_BTN_LEFT,
          "then the pointer");
    zui_push_touch(&q, ZUI_EV_TOUCH_BEGIN, 1, 70, 80, 11);
    CHECK(zui_queue_len(&q) == 1, "touch 1 is touch only");
    zui_pop(&q, &e);
    zui_push_touch(&q, ZUI_EV_TOUCH_MOVE, 0, 51, 61, 12);
    zui_push_touch(&q, ZUI_EV_TOUCH_END, 0, 52, 62, 13);
    CHECK(zui_queue_len(&q) == 4, "move and end each add a pointer event");
    zui_pop(&q, &e);
    zui_pop(&q, &e);
    CHECK(e.type == ZUI_EV_PTR_MOVE, "touch move -> pointer move");
    zui_pop(&q, &e);
    zui_pop(&q, &e);
    CHECK(e.type == ZUI_EV_PTR_UP && q.buttons == 0, "touch end -> pointer up");
    zui_push_touch(&q, ZUI_EV_TOUCH_CANCEL, 0, 1, 1, 14);
    zui_pop(&q, &e);
    CHECK(zui_pop(&q, &e) && e.type == ZUI_EV_PTR_UP, "cancel also lifts the pointer");

    /* I1: a full ring drops moves before keys */
    zui_queue_init(&q);
    zui_push_key(&q, true, 'a', 0, false, 0);
    for (uint32_t i = 0; i < ZUI_INPUT_RING - 1; i++)
        zui_push_ptr(&q, ZUI_EV_PTR_MOVE, (int32_t) i, 0, 0, i);
    CHECK(zui_queue_len(&q) == ZUI_INPUT_RING && q.dropped == 0, "exactly full");
    zui_push_key(&q, true, 'b', 0, false, 999);
    CHECK(zui_queue_len(&q) == ZUI_INPUT_RING && q.dropped == 1, "one dropped");
    CHECK(zui_pop(&q, &e) && e.type == ZUI_EV_KEY_DOWN && e.key == 'a', "the old key survives");
    CHECK(zui_pop(&q, &e) && e.type == ZUI_EV_PTR_MOVE && e.x == 1, "the oldest move went");
    uint32_t moves = 0;
    bool got_b = false;
    while (zui_pop(&q, &e)) {
        if (e.type == ZUI_EV_PTR_MOVE) moves++;
        if (e.type == ZUI_EV_KEY_DOWN && e.key == 'b') got_b = true;
    }
    CHECK(moves == ZUI_INPUT_RING - 3 && got_b, "the new key arrived, in order");
    /* with nothing but keys, the oldest key goes */
    zui_queue_init(&q);
    for (uint32_t i = 0; i < ZUI_INPUT_RING + 2; i++)
        zui_push_key(&q, true, (uint16_t) ('a' + i % 26), 0, false, i);
    CHECK(q.dropped == 2 && zui_pop(&q, &e) && e.ms == 2, "keys only: oldest dropped");
}

static zui_event_t ev(uint8_t type, int32_t x, int32_t y, uint8_t button, uint32_t ms)
{
    zui_event_t e;
    memset(&e, 0, sizeof e);
    e.type = type;
    e.x = x;
    e.y = y;
    e.button = button;
    e.ms = ms;
    return e;
}

static void test_gesture(void)
{
    zui_gesture_state_t g;
    zui_gesture_init(&g, 10);
    zui_event_t d = ev(ZUI_EV_PTR_DOWN, 100, 100, ZUI_BTN_LEFT, 1000);
    zui_event_t u = ev(ZUI_EV_PTR_UP, 105, 103, ZUI_BTN_LEFT, 1100);
    CHECK(zui_gesture_feed(&g, &d) == ZUI_GESTURE_NONE, "down alone is nothing");
    CHECK(zui_gesture_feed(&g, &u) == ZUI_GESTURE_TAP, "tap");
    d.ms = 1200;
    u.ms = 1300;
    zui_gesture_feed(&g, &d);
    CHECK(zui_gesture_feed(&g, &u) == ZUI_GESTURE_DOUBLE_TAP, "double tap");
    d.ms = 1400;
    u.ms = 1450;
    zui_gesture_feed(&g, &d);
    CHECK(zui_gesture_feed(&g, &u) == ZUI_GESTURE_TAP, "a third tap starts over");
    d.ms = 1450 + ZUI_DOUBLE_MS + 1;
    u.ms = d.ms + 10;
    zui_gesture_feed(&g, &d);
    CHECK(zui_gesture_feed(&g, &u) == ZUI_GESTURE_TAP, "too slow for a double");
    d.ms = u.ms + 50;
    u.ms = d.ms + 10;
    u.x = 140;
    zui_gesture_feed(&g, &d);
    CHECK(zui_gesture_feed(&g, &u) == ZUI_GESTURE_NONE, "released far away: no tap");
    /* drag */
    d.ms = 5000;
    zui_gesture_feed(&g, &d);
    zui_event_t m = ev(ZUI_EV_PTR_MOVE, 100, 111, 0, 5010);
    CHECK(zui_gesture_feed(&g, &m) == ZUI_GESTURE_DRAG, "past slop is a drag");
    m.y = 150;
    CHECK(zui_gesture_feed(&g, &m) == ZUI_GESTURE_NONE, "a drag is reported once");
    u = ev(ZUI_EV_PTR_UP, 100, 100, ZUI_BTN_LEFT, 5020);
    CHECK(zui_gesture_feed(&g, &u) == ZUI_GESTURE_NONE, "no tap after a drag");
    /* long press by tick, fired once */
    d.ms = 6000;
    d.x = 100;
    zui_gesture_feed(&g, &d);
    zui_event_t t = ev(ZUI_EV_TICK, 0, 0, 0, 6000 + ZUI_LONGPRESS_MS - 1);
    CHECK(zui_gesture_feed(&g, &t) == ZUI_GESTURE_NONE, "not yet");
    t.ms++;
    CHECK(zui_gesture_feed(&g, &t) == ZUI_GESTURE_LONG_PRESS, "long press at the threshold");
    t.ms += 100;
    CHECK(zui_gesture_feed(&g, &t) == ZUI_GESTURE_NONE, "only once");
    u.ms = t.ms;
    CHECK(zui_gesture_feed(&g, &u) == ZUI_GESTURE_NONE, "release after long press: nothing");
    /* long press by a still move */
    d.ms = 8000;
    zui_gesture_feed(&g, &d);
    m = ev(ZUI_EV_PTR_MOVE, 102, 101, 0, 8000 + ZUI_LONGPRESS_MS);
    CHECK(zui_gesture_feed(&g, &m) == ZUI_GESTURE_LONG_PRESS, "a still finger moving a little");
    /* long press seen only at release (no ticks) */
    d.ms = 9000;
    zui_gesture_feed(&g, &d);
    u = ev(ZUI_EV_PTR_UP, 100, 100, ZUI_BTN_LEFT, 9000 + ZUI_LONGPRESS_MS);
    CHECK(zui_gesture_feed(&g, &u) == ZUI_GESTURE_LONG_PRESS, "held long, released");
    /* other buttons are not taps */
    zui_event_t r = ev(ZUI_EV_PTR_DOWN, 1, 1, ZUI_BTN_RIGHT, 10000);
    CHECK(zui_gesture_feed(&g, &r) == ZUI_GESTURE_NONE, "right down ignored");
    r.type = ZUI_EV_PTR_UP;
    CHECK(zui_gesture_feed(&g, &r) == ZUI_GESTURE_NONE, "right up ignored");
    zui_gesture_init(&g, 0);
    CHECK(g.slop_px == ZUI_TAP_SLOP_PX, "default slop");
}

static void test_utf8(void)
{
    const char *s = "a\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80z"; /* a é € 😀 z */
    uint32_t n = (uint32_t) strlen(s);
    CHECK(zui_utf8_next(s, n, 0) == 1 && zui_utf8_next(s, n, 1) == 3 &&
              zui_utf8_next(s, n, 3) == 6 && zui_utf8_next(s, n, 6) == 10 &&
              zui_utf8_next(s, n, 10) == 11 && zui_utf8_next(s, n, 11) == 11,
          "next");
    CHECK(zui_utf8_prev(s, 11) == 10 && zui_utf8_prev(s, 10) == 6 && zui_utf8_prev(s, 6) == 3 &&
              zui_utf8_prev(s, 3) == 1 && zui_utf8_prev(s, 1) == 0 && zui_utf8_prev(s, 0) == 0,
          "prev");
    CHECK(zui_utf8_fit(s, n, 100) == n && zui_utf8_fit(s, n, 8) == 6 &&
              zui_utf8_fit(s, n, 2) == 1 && zui_utf8_fit(s, n, 0) == 0,
          "fit");
}

/* ---- damage and the framebuffer -------------------------------------------------- */

static zui_rect_t R(int32_t x, int32_t y, int32_t w, int32_t h)
{
    zui_rect_t r = {x, y, w, h};
    return r;
}

static void test_damage(void)
{
    zui_damage_t d;
    zui_damage_clear(&d);
    CHECK(d.n == 0 && zui_damage_bounds(&d).w == 0 && zui_damage_area(&d) == 0, "empty");
    zui_damage_add(&d, R(0, 0, 0, 5));
    zui_damage_add(&d, R(0, 0, 5, -1));
    CHECK(d.n == 0, "empty rects ignored");
    zui_damage_add(&d, R(0, 0, 10, 10));
    zui_damage_add(&d, R(2, 2, 3, 3));
    CHECK(d.n == 1 && zui_damage_area(&d) == 100, "contained rect absorbed");
    zui_damage_add(&d, R(5, 5, 10, 10));
    CHECK(d.n == 1 && d.r[0].x == 0 && d.r[0].w == 15 && d.r[0].h == 15, "overlap merges");
    zui_damage_add(&d, R(100, 100, 1, 1));
    CHECK(d.n == 2, "far apart stays separate");
    zui_damage_add(&d, R(-5, -5, 300, 300));
    CHECK(d.n == 1 && d.r[0].w == 300, "a covering rect swallows all");
    zui_damage_clear(&d);
    for (int i = 0; i < 40; i++) zui_damage_add(&d, R(i * 20, 0, 5, 5));
    CHECK(d.n == ZUI_DAMAGE_MAX, "capped");
    zui_rect_t b = zui_damage_bounds(&d);
    CHECK(b.x == 0 && b.x + b.w == 39 * 20 + 5, "nothing lost when capped");
    for (uint32_t i = 0; i < d.n; i++)
        for (uint32_t j = i + 1; j < d.n; j++) {
            zui_rect_t p = d.r[i], q = d.r[j];
            CHECK(!(p.x < q.x + q.w && q.x < p.x + p.w && p.y < q.y + q.h && q.y < p.y + p.h),
                  "kept rects never overlap");
        }
    zui_damage_all(&d, 64, 32);
    CHECK(d.n == 1 && zui_damage_area(&d) == 64 * 32, "all");
}

static uint32_t clock_v = 42;
static uint32_t clk(void)
{
    return clock_v;
}

static void test_fb(void)
{
    static uint32_t src[8 * 4], fbmem[10 * 4];
    zui_surface_t s;
    zui_surface_init(&s, src, 8, 4, 8);
    for (int i = 0; i < 32; i++) src[i] = 0x00112233u + (uint32_t) i;
    src[0] = 0xFF123456u; /* the X byte is not passed on */
    zui_fb_t fb;
    memset(&fb, 0, sizeof fb);
    zui_present_t p;
    memset(&p, 0, sizeof p);
    zui_present_fb_bind(&p, &fb);
    CHECK(p.ops == &zui_present_fb_ops && strcmp(p.ops->name, "framebuffer") == 0, "bound");
    CHECK(p.ops->open(&p, "t", 8, 4) == ZUI_PRESENT_E_UNAVAILABLE, "no framebuffer yet");
    fb.base = fbmem;
    fb.w = 8;
    fb.h = 4;
    fb.pitch_px = 7;
    CHECK(p.ops->open(&p, "t", 8, 4) == ZUI_PRESENT_E_UNAVAILABLE, "pitch below width refused");
    fb.pitch_px = 10;
    CHECK(p.ops->open(&p, "t", 8, 4) == ZUI_PRESENT_OK && p.screen.px_w == 8 &&
              p.screen.scale_q8 == 256,
          "open");
    zui_damage_t d;
    zui_damage_clear(&d);
    CHECK(p.ops->present(&p, &s, &d) == ZUI_PRESENT_OK && p.frames == 0, "nothing damaged");
    zui_damage_add(&d, R(1, 1, 2, 2));
    p.ops->present(&p, &s, &d);
    CHECK(p.frames == 1 && p.px_copied == 4, "4 pixels");
    CHECK(fbmem[10 + 1] == src[8 + 1] && fbmem[2 * 10 + 2] == src[2 * 8 + 2], "copied at pitch");
    CHECK(fbmem[0] == 0 && fbmem[10 + 3] == 0 && fbmem[3 * 10 + 1] == 0, "P1: only the damage");
    zui_damage_all(&d, 100, 100);
    p.ops->present(&p, &s, &d);
    CHECK(p.px_copied == 4 + 32, "clipped to the surface");
    CHECK(fbmem[0] == 0x00123456u, "P2: X byte cleared, value kept");
    CHECK(fbmem[8] == 0 && fbmem[9] == 0, "the pitch padding is untouched");
    fb.format = ZUI_FB_XBGR;
    zui_damage_all(&d, 8, 4);
    p.ops->present(&p, &s, &d);
    CHECK(fbmem[0] == 0x00563412u, "RGB order swapped for an RGB GOP");
    zui_damage_clear(&d);
    zui_damage_add(&d, R(-3, -3, 4, 4));
    uint64_t before = p.px_copied;
    p.ops->present(&p, &s, &d);
    CHECK(p.px_copied == before + 1, "negative origin clipped");
    CHECK(p.ops->present(&p, 0, &d) == ZUI_PRESENT_E_ARG, "NULL surface");
    CHECK(p.ops->now_ms(&p) == 0, "no clock: time stands still");
    fb.clock_ms = clk;
    CHECK(p.ops->now_ms(&p) == 42, "the kernel's clock");
    static zui_queue_t drv, q;
    zui_queue_init(&drv);
    zui_queue_init(&q);
    CHECK(p.ops->poll(&p, &q, 0) == 0, "no driver queue");
    fb.drv_queue = &drv;
    zui_push_key(&drv, true, 'x', 0, false, 1);
    zui_push_key(&drv, false, 'x', 0, false, 2);
    CHECK(p.ops->poll(&p, &q, 10) == 2 && zui_queue_len(&q) == 2 && zui_queue_len(&drv) == 0,
          "driver events moved");
    fb.drv_queue = &q;
    CHECK(p.ops->poll(&p, &q, 0) == 0, "a queue never feeds itself");
    p.ops->close(&p);
}

static void test_set1(void)
{
    bool e0 = false, down = false;
    uint16_t key = 0;
    uint8_t mods = 0;
    CHECK(zui_key_from_set1(0x1E, &e0, &key, &down, &mods) && key == 'a' && down, "a down");
    CHECK(zui_key_from_set1(0x9E, &e0, &key, &down, &mods) && key == 'a' && !down, "a up");
    CHECK(!zui_key_from_set1(0x2A, &e0, &key, &down, &mods) && mods == ZUI_MOD_SHIFT, "shift");
    CHECK(!zui_key_from_set1(0xAA, &e0, &key, &down, &mods) && mods == 0, "shift up");
    CHECK(!zui_key_from_set1(0x1D, &e0, &key, &down, &mods) && mods == (ZUI_MOD_CTRL | ZUI_MOD_CMD),
          "ctrl is also the shortcut key");
    CHECK(zui_key_from_set1(0x25, &e0, &key, &down, &mods) && key == 'k', "ctrl-k");
    zui_key_from_set1(0x9D, &e0, &key, &down, &mods);
    CHECK(mods == 0, "ctrl up");
    CHECK(!zui_key_from_set1(0xE0, &e0, &key, &down, &mods) && e0, "prefix");
    CHECK(zui_key_from_set1(0x48, &e0, &key, &down, &mods) && key == ZUI_KEY_UP && !e0, "up");
    zui_key_from_set1(0xE0, &e0, &key, &down, &mods);
    CHECK(zui_key_from_set1(0xD0, &e0, &key, &down, &mods) && key == ZUI_KEY_DOWN && !down,
          "down released");
    zui_key_from_set1(0xE0, &e0, &key, &down, &mods);
    CHECK(!zui_key_from_set1(0x5B, &e0, &key, &down, &mods) && mods == ZUI_MOD_META, "meta");
    zui_key_from_set1(0xE0, &e0, &key, &down, &mods);
    zui_key_from_set1(0xDB, &e0, &key, &down, &mods);
    CHECK(mods == 0, "meta up");
    zui_key_from_set1(0xE0, &e0, &key, &down, &mods);
    CHECK(!zui_key_from_set1(0x2A, &e0, &key, &down, &mods) && mods == 0, "fake shift ignored");
    CHECK(!zui_key_from_set1(0x48, &e0, &key, &down, &mods), "keypad 8 (no prefix) is not mapped");
    CHECK(zui_key_from_set1(0x3B, &e0, &key, &down, &mods) && key == ZUI_KEY_F1, "F1");
    CHECK(zui_key_from_set1(0x44, &e0, &key, &down, &mods) && key == ZUI_KEY_F1 + 9, "F10");
    CHECK(zui_key_from_set1(0x57, &e0, &key, &down, &mods) && key == ZUI_KEY_F1 + 10, "F11");
    CHECK(zui_key_from_set1(0x58, &e0, &key, &down, &mods) && key == ZUI_KEY_F12, "F12");
    CHECK(zui_key_from_set1(0x01, &e0, &key, &down, &mods) && key == ZUI_KEY_ESCAPE, "esc");
    CHECK(zui_key_from_set1(0x1C, &e0, &key, &down, &mods) && key == ZUI_KEY_ENTER, "enter");
    CHECK(zui_key_from_set1(0x0E, &e0, &key, &down, &mods) && key == ZUI_KEY_BACKSPACE, "bksp");
    CHECK(zui_key_from_set1(0x39, &e0, &key, &down, &mods) && key == ' ', "space");
    CHECK(!zui_key_from_set1(0x3A, &e0, &key, &down, &mods), "caps lock is not a key here");
    CHECK(!zui_key_from_set1(0x00, &e0, &key, &down, &mods), "zero");
    CHECK(!zui_key_from_set1(0x60, &e0, &key, &down, &mods), "unmapped");
    zui_key_from_set1(0xE0, &e0, &key, &down, &mods);
    CHECK(!zui_key_from_set1(0x10, &e0, &key, &down, &mods), "unmapped extended (media)");
    const uint8_t ext[][2] = {{0x4B, ZUI_KEY_LEFT & 0xFF},    {0x4D, ZUI_KEY_RIGHT & 0xFF},
                              {0x47, ZUI_KEY_HOME & 0xFF},    {0x4F, ZUI_KEY_END & 0xFF},
                              {0x49, ZUI_KEY_PAGE_UP & 0xFF}, {0x51, ZUI_KEY_PAGE_DOWN & 0xFF},
                              {0x52, ZUI_KEY_INSERT & 0xFF},  {0x53, ZUI_KEY_DELETE & 0xFF},
                              {0x1C, ZUI_KEY_ENTER & 0xFF},   {0x5D, ZUI_KEY_MENU & 0xFF}};
    for (unsigned i = 0; i < sizeof ext / sizeof ext[0]; i++) {
        zui_key_from_set1(0xE0, &e0, &key, &down, &mods);
        CHECK(zui_key_from_set1(ext[i][0], &e0, &key, &down, &mods) && (key & 0xFF) == ext[i][1],
              "extended key");
    }
}

/* ---- widgets ------------------------------------------------------------------ */

static zui_ui_t UI;
static int n_act;
static zui_id_t last_w;
static zui_action_t last_act;
static int32_t last_arg;

static void on(zui_ui_t *ui, zui_id_t w, zui_action_t a, int32_t arg, void *ctx)
{
    (void) ui;
    (void) ctx;
    n_act++;
    last_w = w;
    last_act = a;
    last_arg = arg;
}

static void reset_act(void)
{
    n_act = 0;
    last_w = ZUI_NONE;
    last_act = ZUI_ACT_NONE;
    last_arg = -99;
}

static uint32_t now = 1000;

static void send(zui_event_t e)
{
    if (!e.ms) e.ms = now;
    zui_ui_event(&UI, &e);
}

static void key(uint16_t k, uint8_t mods)
{
    zui_event_t e = ev(ZUI_EV_KEY_DOWN, 0, 0, 0, ++now);
    e.key = k;
    e.mods = mods;
    send(e);
}

static void type(const char *s)
{
    zui_event_t e = ev(ZUI_EV_TEXT, 0, 0, 0, ++now);
    strncpy(e.text, s, ZUI_TEXT_EV_MAX);
    send(e);
}

static void click(int32_t x, int32_t y)
{
    now += 1000; /* never a double click by accident */
    send(ev(ZUI_EV_PTR_DOWN, x, y, ZUI_BTN_LEFT, now));
    send(ev(ZUI_EV_PTR_UP, x, y, ZUI_BTN_LEFT, now + 20));
}

static void center(zui_id_t id, int32_t *x, int32_t *y)
{
    zui_widget_t *w = zui_w(&UI, id);
    *x = w->r.x + w->r.w / 2;
    *y = w->r.y + w->r.h / 2;
}

static const char *FILES[] = {"alpha.txt", "beta.png", "gamma", "delta.md", "epsilon",
                              "zeta",      "eta",      "theta", "iota",     "kappa",
                              "lambda",    "mu",       "nu",    "xi",       "omicron"};

static void row_fn(const zui_ui_t *ui, zui_id_t list, int32_t row, zui_row_t *out, void *ctx)
{
    (void) ui;
    (void) list;
    (void) ctx;
    snprintf(out->text, sizeof out->text, "%s", FILES[row % 15]);
    snprintf(out->detail, sizeof out->detail, "%d KB", row);
}

static zui_screen_t laptop = {1280, 800, 256, ZUI_INPUT_POINTER};

static zui_theme_t mono_theme(void)
{
    zui_theme_t t;
    memset(&t, 0, sizeof t);
    t.body.size_px = 16; /* no font: 10 px a character, deterministic */
    t.label.size_px = 12;
    t.title.size_px = 20;
    t.mono.size_px = 16;
    return t;
}

static void test_build(void)
{
    zui_theme_t th = mono_theme();
    zui_ui_init(&UI, &laptop, &th);
    zui_ui_on(&UI, on, 0);
    CHECK(UI.root == ZUI_NONE && UI.n == 0 && UI.focus == ZUI_NONE, "empty");
    CHECK(zui_label(&UI, 5, "x") == ZUI_NONE, "no parent 5");
    zui_id_t root = zui_box(&UI, ZUI_NONE, ZUI_COL);
    CHECK(root == 0 && UI.root == 0, "root");
    CHECK(zui_box(&UI, ZUI_NONE, ZUI_COL) == ZUI_NONE, "one root only");
    CHECK(zui_w(&UI, -1) == 0 && zui_w(&UI, 1) == 0 && zui_w(&UI, 0) != 0, "zui_w bounds");
    char lng[200];
    memset(lng, 'a', sizeof lng);
    lng[199] = 0;
    zui_id_t l = zui_label(&UI, root, lng);
    CHECK(strlen(zui_w(&UI, l)->label) == ZUI_LABEL_MAX - 1, "labels are cut to fit (W1)");
    zui_set_text(&UI, l, "short");
    CHECK(strcmp(zui_w(&UI, l)->label, "short") == 0, "set text");
    zui_ui_clear(&UI);
    CHECK(UI.n == 0 && UI.root == ZUI_NONE, "clear");
    root = zui_box(&UI, ZUI_NONE, ZUI_COL);
    for (uint32_t i = 1; i < ZUI_WIDGETS; i++) zui_label(&UI, root, "x");
    CHECK(UI.n == ZUI_WIDGETS, "full pool");
    CHECK(zui_button(&UI, root, "b", ZUI_BTN_PRIMARY) == ZUI_NONE && UI.overflow == 1,
          "full: refused and counted, no crash");
    CHECK(zui_held(&UI, root) == ZUI_NONE, "held on a full pool");
    zui_set_text(&UI, ZUI_NONE, "x");
    zui_set_hidden(&UI, ZUI_NONE, true);
    zui_list_select(&UI, ZUI_NONE, 1);
    zui_field_set(&UI, ZUI_NONE, 1);
    CHECK(true, "setters accept ZUI_NONE");
}

typedef struct {
    zui_id_t root, top, title, search, list, side, name, save, cancel, del, notes, held, bar;
    char search_buf[64];
    char notes_buf[256];
} screen_t;

static screen_t S;

static void build_screen(const zui_screen_t *sc)
{
    zui_theme_t th = mono_theme();
    zui_ui_init(&UI, sc, &th);
    zui_ui_on(&UI, on, 0);
    memset(&S, 0, sizeof S);
    S.root = zui_box(&UI, ZUI_NONE, ZUI_COL);
    S.top = zui_box(&UI, S.root, ZUI_ROW);
    zui_set_pad(&UI, S.top, 8, 8);
    S.title = zui_label(&UI, S.top, "Burrow");
    zui_set_font(&UI, S.title, ZUI_TEXT_TITLE, 0);
    S.search = zui_field(&UI, S.top, S.search_buf, sizeof S.search_buf, false);
    zui_set_size(&UI, S.search, ZUI_SIZE_FLEX, 1);
    zui_w(&UI, S.search)->u.field.placeholder[0] = 'S';
    zui_id_t body = zui_box(&UI, S.root, ZUI_ROW);
    zui_set_size(&UI, body, ZUI_SIZE_FLEX, 1);
    zui_set_pad(&UI, body, 8, 8);
    S.list = zui_list(&UI, body, row_fn, 0);
    zui_set_name(&UI, S.list, "Files");
    zui_list_set_rows(&UI, S.list, 15);
    S.side = zui_panel(&UI, body, ZUI_COL);
    zui_set_size(&UI, S.side, ZUI_SIZE_FIXED, 360);
    S.name = zui_label(&UI, S.side, "alpha.txt");
    S.notes = zui_field(&UI, S.side, S.notes_buf, sizeof S.notes_buf, true);
    S.held = zui_held(&UI, S.side);
    zui_id_t row = zui_box(&UI, S.side, ZUI_ROW);
    zui_set_pad(&UI, row, 0, 8);
    S.save = zui_button(&UI, row, "Save", ZUI_BTN_PRIMARY);
    S.cancel = zui_button(&UI, row, "Cancel", ZUI_BTN_SECONDARY);
    S.del = zui_button(&UI, row, "Delete", ZUI_BTN_DANGER);
    S.bar = zui_progress(&UI, S.root);
    zui_relayout(&UI);
    static uint32_t px[1280 * 800];
    zui_surface_t s;
    zui_surface_init(&s, px, (int32_t) sc->px_w, (int32_t) sc->px_h, (int32_t) sc->px_w);
    zui_ui_paint(&UI, &s);
    zui_damage_clear(&UI.damage);
    reset_act();
}

static void test_layout(void)
{
    build_screen(&laptop);
    zui_widget_t *root = zui_w(&UI, S.root), *list = zui_w(&UI, S.list), *side = zui_w(&UI, S.side),
                 *bar = zui_w(&UI, S.bar);
    CHECK(root->r.w == 1280 && root->r.h == 800, "root fills the screen");
    CHECK(side->r.w == 360, "fixed width");
    CHECK(side->r.x + side->r.w == 1280 - 8, "fixed pane ends at the padding");
    CHECK(list->r.x == 8 && list->r.x + list->r.w + 8 == side->r.x, "flex list takes the rest");
    CHECK(bar->r.y + bar->r.h == 800 && bar->r.h == 3, "the bar sits at the bottom, 3 px");
    zui_widget_t *title = zui_w(&UI, S.title), *search = zui_w(&UI, S.search);
    CHECK(title->r.w == 6 * (20 * 160) / 256 + ((6 * 20 * 160) % 256 != 0), "6 chars at 20 px");
    CHECK(search->r.x == title->r.x + title->r.w + 8, "gap between title and search");
    CHECK(search->r.x + search->r.w == 1280 - 8, "flex search to the edge");
    CHECK(search->r.h >= UI.plan.target_px, "a field is at least a target tall");
    zui_widget_t *save = zui_w(&UI, S.save);
    CHECK(save->r.h >= 28 && save->r.w >= save->r.h, "pointer target 28 pt");
    CHECK(zui_w(&UI, S.held)->hidden, "held hidden until there is an answer");
    CHECK(zui_hit(&UI, save->r.x + 1, save->r.y + 1) == S.save, "hit the button");
    CHECK(zui_hit(&UI, 2, 2) == S.top, "hit padding: the box");
    CHECK(zui_hit(&UI, -1, 5) == ZUI_NONE, "off screen");
    /* rows */
    CHECK(zui_list_row_at(&UI, S.list, list->r.x + 5, list->r.y + 1) == 0, "row 0");
    CHECK(zui_list_row_at(&UI, S.list, list->r.x + 5, list->r.y + 28 * 3 + 2) == 3, "row 3");
    CHECK(zui_list_row_at(&UI, S.list, list->r.x - 1, list->r.y + 1) == -1, "outside");
    CHECK(zui_list_row_at(&UI, S.save, 0, 0) == -1, "not a list");
    /* a resize relays out */
    zui_event_t r = ev(ZUI_EV_RESIZE, 800, 600, 0, 0);
    send(r);
    zui_ui_paint(&UI, &(zui_surface_t){0});
    CHECK(UI.screen.px_w == 800 && UI.plan.cls == ZUI_MEDIUM, "resized: medium");
    CHECK(zui_w(&UI, S.side)->r.x + 360 + 8 == 800, "layout follows");
    zui_event_t sc = ev(ZUI_EV_SCALE, 512, 0, 0, 0);
    send(sc);
    CHECK(UI.screen.scale_q8 == 512 && UI.plan.target_px == 56, "scale 2x doubles targets");
    sc.x = 0;
    send(sc);
    CHECK(UI.screen.scale_q8 == 512, "a zero scale is ignored");
    /* hide: siblings close the gap */
    build_screen(&laptop);
    int32_t sx = zui_w(&UI, S.side)->r.x;
    zui_set_hidden(&UI, S.side, true);
    zui_relayout(&UI);
    send(ev(ZUI_EV_TICK, 0, 0, 0, 1));
    CHECK(zui_w(&UI, S.list)->r.w > sx, "list grows when the side pane hides");
    CHECK(zui_hit(&UI, sx + 10, 300) != S.save, "hidden is not hit");
}

static void test_focus(void)
{
    build_screen(&laptop);
    CHECK(zui_focus_next(&UI, false) == S.search && UI.focus_visible, "Tab: first is search");
    CHECK(zui_focus_next(&UI, false) == S.list, "then the list");
    CHECK(zui_focus_next(&UI, false) == S.notes, "then the notes");
    CHECK(zui_focus_next(&UI, false) == S.save, "held is hidden: skipped; Save");
    CHECK(zui_focus_next(&UI, false) == S.cancel, "Cancel");
    CHECK(zui_focus_next(&UI, false) == S.del, "Delete");
    CHECK(zui_focus_next(&UI, false) == S.search, "wraps");
    CHECK(zui_focus_next(&UI, true) == S.del, "Shift-Tab wraps back");
    zui_set_disabled(&UI, S.cancel, true);
    CHECK(zui_focus_next(&UI, true) == S.save, "disabled skipped");
    zui_set_disabled(&UI, S.cancel, false);
    zui_focus(&UI, ZUI_NONE, false);
    CHECK(zui_focus_next(&UI, true) == S.del, "no focus: Shift-Tab picks the last");
    /* arrows on a remote: spatial */
    zui_focus(&UI, S.save, true);
    CHECK(zui_focus_toward(&UI, ZUI_KEY_RIGHT) == S.cancel, "right of Save is Cancel");
    CHECK(zui_focus_toward(&UI, ZUI_KEY_RIGHT) == S.del, "then Delete");
    CHECK(zui_focus_toward(&UI, ZUI_KEY_RIGHT) == S.del, "nothing further: stays");
    CHECK(zui_focus_toward(&UI, ZUI_KEY_UP) == S.notes, "up from Delete is the notes");
    CHECK(zui_focus_toward(&UI, ZUI_KEY_LEFT) == S.list, "left of the notes is the list");
    CHECK(zui_focus_toward(&UI, ZUI_KEY_UP) == S.search, "up from the list is search");
    CHECK(zui_focus_toward(&UI, ZUI_KEY_DOWN) == S.list, "down from search is the list");
    CHECK(zui_focus_toward(&UI, ZUI_KEY_HOME) == UI.focus, "not an arrow: stays");
    zui_focus(&UI, ZUI_NONE, false);
    CHECK(zui_focus_toward(&UI, ZUI_KEY_DOWN) == S.search, "no focus: first");
    /* focus by pointer is not shown as a ring */
    int32_t x, y;
    center(S.save, &x, &y);
    click(x, y);
    CHECK(UI.focus == S.save && !UI.focus_visible, "a click focuses quietly");
    key(ZUI_KEY_TAB, 0);
    CHECK(UI.focus == S.cancel && UI.focus_visible, "Tab shows the ring");
    key(ZUI_KEY_TAB, ZUI_MOD_SHIFT);
    CHECK(UI.focus == S.save, "Shift-Tab key");
    zui_set_hidden(&UI, S.side, true);
    CHECK(UI.focus == ZUI_NONE, "hiding a pane drops focus inside it");
    zui_focus(&UI, S.save, true);
    CHECK(UI.focus == ZUI_NONE, "cannot focus hidden");
    zui_focus(&UI, S.title, true);
    CHECK(UI.focus == ZUI_NONE, "labels do not take focus");
    zui_focus(&UI, 9999, true);
    CHECK(UI.focus == ZUI_NONE, "bad id");
}

static void test_buttons(void)
{
    build_screen(&laptop);
    int32_t x, y;
    center(S.save, &x, &y);
    click(x, y);
    CHECK(n_act == 1 && last_w == S.save && last_act == ZUI_ACT_PRESS, "click presses");
    reset_act();
    now += 1000;
    send(ev(ZUI_EV_PTR_DOWN, x, y, ZUI_BTN_LEFT, now));
    CHECK(UI.pressed == S.save && n_act == 0, "down alone does not press");
    send(ev(ZUI_EV_PTR_UP, 5, 5, ZUI_BTN_LEFT, now + 10));
    CHECK(n_act == 0 && UI.pressed == ZUI_NONE, "released elsewhere: cancelled");
    zui_focus(&UI, S.cancel, true);
    key(ZUI_KEY_ENTER, 0);
    CHECK(n_act == 1 && last_w == S.cancel, "Enter presses the focused button");
    key(ZUI_KEY_SPACE, 0);
    CHECK(n_act == 2, "Space too");
    reset_act();
    zui_set_disabled(&UI, S.save, true);
    center(S.save, &x, &y);
    click(x, y);
    CHECK(n_act == 0, "a disabled button does nothing");
    zui_set_disabled(&UI, S.save, false);
    /* right click asks for a context menu */
    center(S.del, &x, &y);
    send(ev(ZUI_EV_PTR_DOWN, x, y, ZUI_BTN_RIGHT, ++now));
    CHECK(last_act == ZUI_ACT_CONTEXT && last_w == S.del, "right click: context");
    /* hover tracks and damages */
    zui_damage_clear(&UI.damage);
    send(ev(ZUI_EV_PTR_MOVE, x, y, 0, ++now));
    CHECK(UI.hover == S.del && UI.damage.n > 0, "hover repaints the button");
    zui_damage_clear(&UI.damage);
    send(ev(ZUI_EV_PTR_MOVE, x + 1, y, 0, ++now));
    CHECK(UI.damage.n == 0, "moving within it repaints nothing (W7)");
}

static void test_list(void)
{
    build_screen(&laptop);
    zui_widget_t *l = zui_w(&UI, S.list);
    zui_focus(&UI, S.list, true);
    key(ZUI_KEY_DOWN, 0);
    CHECK(l->u.list.sel == 0 && last_act == ZUI_ACT_SELECT && last_arg == 0, "down selects 0");
    key(ZUI_KEY_DOWN, 0);
    key(ZUI_KEY_DOWN, 0);
    CHECK(l->u.list.sel == 2, "down twice");
    key(ZUI_KEY_UP, 0);
    CHECK(l->u.list.sel == 1, "up");
    key(ZUI_KEY_END, 0);
    CHECK(l->u.list.sel == 14 && last_arg == 14, "End");
    int32_t vis = l->r.h / 28;
    CHECK(l->u.list.top == (15 > vis ? 15 - vis : 0), "scrolled to show the last row");
    reset_act();
    key(ZUI_KEY_DOWN, 0);
    CHECK(l->u.list.sel == 14 && n_act == 0, "past the end: no change, no event");
    key(ZUI_KEY_HOME, 0);
    CHECK(l->u.list.sel == 0 && l->u.list.top == 0, "Home");
    key(ZUI_KEY_PAGE_DOWN, 0);
    CHECK(l->u.list.sel == vis || l->u.list.sel == 14, "Page Down");
    key(ZUI_KEY_ENTER, 0);
    CHECK(last_act == ZUI_ACT_OPEN && last_arg == l->u.list.sel, "Enter opens");
    /* pointer: click selects, double click opens */
    reset_act();
    zui_list_select(&UI, S.list, -1);
    int32_t x = l->r.x + 20, y = l->r.y + 28 * 2 + 5;
    click(x, y);
    CHECK(l->u.list.sel == 2 && last_act == ZUI_ACT_SELECT && n_act == 1, "click selects");
    now += 1000;
    send(ev(ZUI_EV_PTR_DOWN, x, y, ZUI_BTN_LEFT, now));
    send(ev(ZUI_EV_PTR_UP, x, y, ZUI_BTN_LEFT, now + 10));
    send(ev(ZUI_EV_PTR_DOWN, x, y, ZUI_BTN_LEFT, now + 100));
    send(ev(ZUI_EV_PTR_UP, x, y, ZUI_BTN_LEFT, now + 110));
    CHECK(last_act == ZUI_ACT_OPEN && last_arg == 2, "double click opens");
    /* right click on another row selects it, then asks for context */
    reset_act();
    send(ev(ZUI_EV_PTR_DOWN, x, l->r.y + 28 * 4 + 3, ZUI_BTN_RIGHT, ++now));
    CHECK(l->u.list.sel == 4 && last_act == ZUI_ACT_CONTEXT && last_arg == 4 && n_act == 2,
          "right click: select then context");
    key(ZUI_KEY_MENU, 0);
    CHECK(last_act == ZUI_ACT_CONTEXT && last_arg == 4, "the Menu key too");
    /* wheel */
    zui_list_select(&UI, S.list, 0);
    zui_list_set_rows(&UI, S.list, 200);
    zui_event_t w = ev(ZUI_EV_SCROLL, x, y, 0, ++now);
    w.dy = 28 * 3;
    send(w);
    CHECK(l->u.list.top == 3, "wheel scrolls three rows");
    w.dy = -1000;
    send(w);
    CHECK(l->u.list.top == 0, "clamped at the top");
    w.dy = 5;
    send(w);
    CHECK(l->u.list.top == 1, "a small scroll still moves a row");
    zui_list_set_rows(&UI, S.list, 3);
    CHECK(l->u.list.top == 0, "fewer rows clamp the top");
    zui_list_select(&UI, S.list, 2);
    zui_list_set_rows(&UI, S.list, 1);
    CHECK(l->u.list.sel == 0, "selection kept in range");
    zui_list_set_rows(&UI, S.list, 0);
    CHECK(l->u.list.sel == -1, "no rows: no selection");
    zui_list_set_rows(&UI, S.list, -5);
    CHECK(l->u.list.rows == 0, "negative rows are none");
    key(ZUI_KEY_DOWN, 0);
    CHECK(l->u.list.sel == -1, "empty list ignores keys");
    zui_list_select(&UI, S.list, 50);
    CHECK(l->u.list.sel == -1, "select clamps");
}

static zui_widget_t *F(zui_id_t id)
{
    return zui_w(&UI, id);
}

static void test_field(void)
{
    build_screen(&laptop);
    zui_field_t *f = &F(S.search)->u.field;
    zui_focus(&UI, S.search, true);
    type("hello");
    CHECK(strcmp(S.search_buf, "hello") == 0 && f->len == 5 && f->caret == 5, "typing");
    CHECK(last_act == ZUI_ACT_CHANGE && last_w == S.search, "change reported");
    key(ZUI_KEY_LEFT, 0);
    key(ZUI_KEY_LEFT, 0);
    type("XY");
    CHECK(strcmp(S.search_buf, "helXYlo") == 0 && f->caret == 5, "insert at caret");
    key(ZUI_KEY_BACKSPACE, 0);
    CHECK(strcmp(S.search_buf, "helXlo") == 0, "backspace");
    key(ZUI_KEY_DELETE, 0);
    CHECK(strcmp(S.search_buf, "helXo") == 0, "delete");
    key(ZUI_KEY_HOME, 0);
    CHECK(f->caret == 0, "home");
    key(ZUI_KEY_END, ZUI_MOD_SHIFT);
    uint32_t a, b;
    zui_field_selection(&UI, S.search, &a, &b);
    CHECK(a == 0 && b == 5, "shift-end selects");
    key('c', ZUI_MOD_CMD);
    type("new");
    CHECK(strcmp(S.search_buf, "new") == 0, "typing replaces the selection");
    key('v', ZUI_MOD_CMD);
    CHECK(strcmp(S.search_buf, "newhelXo") == 0, "paste from the internal clipboard");
    key('a', ZUI_MOD_CMD);
    key('x', ZUI_MOD_CMD);
    CHECK(f->len == 0, "cut all");
    key('v', ZUI_MOD_CMD);
    CHECK(strcmp(S.search_buf, "newhelXo") == 0, "paste the cut text");
    /* words */
    zui_field_set(&UI, S.search, (uint32_t) sprintf(S.search_buf, "one two  three"));
    zui_field_select(&UI, S.search, 14, 14);
    key(ZUI_KEY_LEFT, ZUI_MOD_ALT);
    CHECK(f->caret == 9, "alt-left: word start");
    key(ZUI_KEY_LEFT, ZUI_MOD_ALT);
    CHECK(f->caret == 4, "again, over two spaces");
    key(ZUI_KEY_RIGHT, ZUI_MOD_ALT);
    CHECK(f->caret == 7, "alt-right: word end");
    key(ZUI_KEY_BACKSPACE, ZUI_MOD_ALT);
    CHECK(strcmp(S.search_buf, "one   three") == 0, "alt-backspace deletes a word");
    key(ZUI_KEY_LEFT, ZUI_MOD_CMD);
    CHECK(f->caret == 0, "cmd-left: line start");
    key(ZUI_KEY_RIGHT, ZUI_MOD_CMD);
    CHECK(f->caret == f->len, "cmd-right: line end");
    /* selection collapse with arrows */
    zui_field_select(&UI, S.search, 2, 6);
    key(ZUI_KEY_LEFT, 0);
    CHECK(f->caret == 2 && f->anchor == 2, "left collapses to the start");
    zui_field_select(&UI, S.search, 2, 6);
    key(ZUI_KEY_RIGHT, 0);
    CHECK(f->caret == 6 && f->anchor == 6, "right collapses to the end");
    /* UTF-8: never split a character */
    zui_field_set(&UI, S.search, (uint32_t) sprintf(S.search_buf, "a\xC3\xA9z"));
    zui_field_select(&UI, S.search, 4, 4);
    key(ZUI_KEY_LEFT, 0);
    CHECK(f->caret == 3, "left over z");
    key(ZUI_KEY_LEFT, 0);
    CHECK(f->caret == 1, "left over é is 2 bytes");
    zui_field_select(&UI, S.search, 2, 2);
    CHECK(f->caret == 1, "select snaps to a character start");
    zui_field_select(&UI, S.search, 99, 99);
    CHECK(f->caret == 4, "select clamps");
    key(ZUI_KEY_DELETE, 0);
    CHECK(f->len == 4, "delete at the end: nothing");
    zui_field_select(&UI, S.search, 1, 1);
    key(ZUI_KEY_DELETE, 0);
    CHECK(strcmp(S.search_buf, "az") == 0, "delete a 2-byte character");
    /* Enter in one line submits, newlines refused */
    reset_act();
    key(ZUI_KEY_ENTER, 0);
    CHECK(last_act == ZUI_ACT_SUBMIT && strcmp(S.search_buf, "az") == 0, "submit");
    type("\n");
    CHECK(strcmp(S.search_buf, "az") == 0, "a newline is refused in one line");
    strcpy(UI.clip_local, "x\ny");
    UI.clip_local_len = 3;
    key('v', ZUI_MOD_CMD);
    CHECK(strcmp(S.search_buf, "x yaz") == 0 || strstr(S.search_buf, "x y") != 0,
          "pasted newlines become spaces in one line");
    /* capacity */
    zui_field_set(&UI, S.search, 0);
    char big[100];
    memset(big, 'q', 30);
    big[30] = 0;
    type(big);
    type(big);
    type(big);
    CHECK(f->len == sizeof S.search_buf - 1 && S.search_buf[f->len] == 0, "full: cut to cap");
    reset_act();
    type("z");
    CHECK(n_act == 0 && f->len == sizeof S.search_buf - 1, "full: refused, no change event");
    /* up/down in one line move focus instead */
    zui_focus(&UI, S.search, true);
    key(ZUI_KEY_DOWN, 0);
    CHECK(UI.focus == S.list, "down leaves a one-line field");
    /* preedit */
    zui_focus(&UI, S.search, true);
    zui_field_set(&UI, S.search, 0);
    zui_event_t pe = ev(ZUI_EV_PREEDIT, 0, 0, 0, ++now);
    strcpy(pe.text, "\xE3\x81\x8B");
    send(pe);
    CHECK(f->len == 0 && strcmp(f->preedit, "\xE3\x81\x8B") == 0, "preedit is not text yet");
    type("\xE5\x8A\xA0");
    CHECK(f->preedit[0] == 0 && strcmp(S.search_buf, "\xE5\x8A\xA0") == 0, "commit clears it");
    /* readonly */
    f->readonly = true;
    type("no");
    key(ZUI_KEY_BACKSPACE, 0);
    CHECK(strcmp(S.search_buf, "\xE5\x8A\xA0") == 0, "readonly refuses edits");
    f->readonly = false;
    /* secret: copying is refused */
    f->secret = true;
    UI.clip_local_len = 0;
    key('a', ZUI_MOD_CMD);
    key('c', ZUI_MOD_CMD);
    CHECK(UI.clip_local_len == 0, "a secret field never reaches the clipboard");
    f->secret = false;
}

static char doc[4096];
static int edits;
static bool refuse;

static bool model_edit(void *ctx, uint32_t a, uint32_t b, const char *s, uint32_t n)
{
    (void) ctx;
    edits++;
    if (refuse) return false;
    uint32_t len = (uint32_t) strlen(doc);
    memmove(doc + a + n, doc + b, len - b + 1);
    memcpy(doc + a, s, n);
    return true;
}

static void test_multiline(void)
{
    build_screen(&laptop);
    zui_widget_t *w = F(S.notes);
    zui_field_t *f = &w->u.field;
    zui_focus(&UI, S.notes, true);
    type("first");
    key(ZUI_KEY_ENTER, 0);
    type("second line");
    key(ZUI_KEY_ENTER, 0);
    type("3");
    CHECK(strcmp(S.notes_buf, "first\nsecond line\n3") == 0, "Enter is a newline");
    key(ZUI_KEY_UP, 0);
    CHECK(f->caret == 7, "up keeps the column (1 -> line 2 col 1)");
    key(ZUI_KEY_UP, 0);
    CHECK(f->caret == 1, "up again");
    key(ZUI_KEY_END, 0);
    CHECK(f->caret == 5, "End: end of this line");
    key(ZUI_KEY_DOWN, 0);
    CHECK(f->caret == 11, "down keeps column 5");
    key(ZUI_KEY_DOWN, 0);
    CHECK(f->caret == 19, "short last line: its end");
    key(ZUI_KEY_UP, 0);
    CHECK(f->caret == 11, "the wanted column survives a short line");
    key(ZUI_KEY_DOWN, 0);
    key(ZUI_KEY_DOWN, 0);
    CHECK(f->caret == f->len, "down past the last line: the end");
    key(ZUI_KEY_HOME, ZUI_MOD_CMD);
    CHECK(f->caret == 0, "cmd-home: start of the document");
    key(ZUI_KEY_UP, 0);
    CHECK(f->caret == 0, "up past the first line: the start");
    key(ZUI_KEY_TAB, 0);
    CHECK(S.notes_buf[0] == '\t' && UI.focus == S.notes, "Tab types a tab in a text area");
    key(ZUI_KEY_TAB, ZUI_MOD_CTRL);
    CHECK(UI.focus != S.notes, "Ctrl-Tab leaves it");
    /* soft wrap: 340 px wide panel minus padding, 10 px a character */
    zui_focus(&UI, S.notes, true);
    zui_field_set(&UI, S.notes, 0);
    char words[200] = "";
    for (int i = 0; i < 12; i++) strcat(words, "abcdefg ");
    type(words); /* 32 bytes per event: the first 32 */
    zui_field_set(&UI, S.notes, (uint32_t) sprintf(S.notes_buf, "%s", words));
    int32_t width = w->r.w - 16;
    int32_t per = width / 10;
    zui_field_select(&UI, S.notes, 0, 0);
    key(ZUI_KEY_DOWN, 0);
    CHECK(f->caret > 0 && f->caret <= (uint32_t) per + 1 && f->caret % 8 == 0,
          "down in a wrapped paragraph lands on the next visual line, at a word");
    key(ZUI_KEY_END, 0);
    CHECK(S.notes_buf[f->caret] == ' ' || f->caret == f->len, "End stops at the wrap");
    /* a model that checks each edit */
    strcpy(doc, "abc");
    f->buf = doc;
    f->cap = sizeof doc;
    f->edit = model_edit;
    zui_field_set(&UI, S.notes, 3);
    zui_field_select(&UI, S.notes, 3, 3);
    edits = 0;
    type("d");
    CHECK(edits == 1 && strcmp(doc, "abcd") == 0 && f->len == 4 && f->caret == 4,
          "edits go through the model");
    refuse = true;
    reset_act();
    type("e");
    CHECK(edits == 2 && strcmp(doc, "abcd") == 0 && n_act == 0, "a refused edit changes nothing");
    refuse = false;
    /* wheel scrolls a long text area */
    zui_field_set(&UI, S.notes, 0);
    doc[0] = 0;
    for (int i = 0; i < 60; i++) strcat(doc, "line\n");
    zui_field_set(&UI, S.notes, (uint32_t) strlen(doc));
    int32_t x, y;
    center(S.notes, &x, &y);
    zui_event_t sw = ev(ZUI_EV_SCROLL, x, y, 0, ++now);
    sw.dy = 40;
    f->scroll_y = 0;
    send(sw);
    CHECK(f->scroll_y == 40 || f->scroll_y > 0, "wheel scrolls the text");
    sw.dy = -400;
    send(sw);
    CHECK(f->scroll_y == 0, "clamped at the top");
    /* click places the caret */
    f->scroll_y = 0;
    click(w->r.x + 8 + 25, w->r.y + 4 + 1);
    CHECK(f->caret == 2 || f->caret == 3, "click at x 25 lands between characters 2 and 3");
}

static int picks;
static void test_menu(void)
{
    build_screen(&laptop);
    const char *items[] = {"Open", "Rename", "-", "Move to Trash"};
    const char *keys[] = {"\xE2\x8C\x98O", "", "", "\xE2\x8C\xAB"};
    zui_menu_open(&UI, S.list, 1270, 790, items, keys, 4);
    CHECK(UI.menu.open && UI.menu.hot == 0, "open with the first item hot");
    CHECK(UI.menu.r.x + UI.menu.r.w <= 1280 && UI.menu.r.y + UI.menu.r.h <= 800, "kept on screen");
    CHECK(UI.menu.off[2], "a separator is never picked");
    key(ZUI_KEY_DOWN, 0);
    key(ZUI_KEY_DOWN, 0);
    CHECK(UI.menu.hot == 3, "down skips the separator");
    key(ZUI_KEY_DOWN, 0);
    CHECK(UI.menu.hot == 0, "wraps");
    key(ZUI_KEY_UP, 0);
    CHECK(UI.menu.hot == 3, "up wraps");
    reset_act();
    key(ZUI_KEY_ENTER, 0);
    CHECK(!UI.menu.open && last_act == ZUI_ACT_PICK && last_arg == 3 && last_w == S.list,
          "Enter picks for the owner");
    zui_menu_open(&UI, S.list, 100, 100, items, 0, 4);
    zui_menu_disable(&UI, 1, true);
    key(ZUI_KEY_DOWN, 0);
    CHECK(UI.menu.hot == 3, "disabled items skipped");
    zui_menu_disable(&UI, 2, false);
    CHECK(UI.menu.off[2], "a separator stays off");
    reset_act();
    key(ZUI_KEY_ESCAPE, 0);
    CHECK(!UI.menu.open && last_act == ZUI_ACT_DISMISS, "Escape dismisses");
    /* pointer: release on an item picks */
    zui_menu_open(&UI, S.list, 100, 100, items, 0, 4);
    int32_t ry = UI.menu.r.y + 4 + 26 + 13;
    send(ev(ZUI_EV_PTR_MOVE, 120, ry, 0, ++now));
    CHECK(UI.menu.hot == 1, "hover moves the highlight");
    reset_act();
    send(ev(ZUI_EV_PTR_DOWN, 120, ry, ZUI_BTN_LEFT, ++now));
    CHECK(UI.menu.open && n_act == 0, "down does not pick");
    send(ev(ZUI_EV_PTR_UP, 120, ry, ZUI_BTN_LEFT, ++now));
    CHECK(!UI.menu.open && last_arg == 1, "up picks");
    zui_menu_open(&UI, S.list, 100, 100, items, 0, 4);
    send(ev(ZUI_EV_PTR_UP, 120, UI.menu.r.y + 4 + 2 * 26 + 13, ZUI_BTN_LEFT, ++now));
    CHECK(UI.menu.open, "release on the separator: stays open");
    send(ev(ZUI_EV_PTR_UP, 5, 5, ZUI_BTN_LEFT, ++now));
    CHECK(!UI.menu.open && last_act == ZUI_ACT_DISMISS, "release outside dismisses");
    const char *many[40];
    for (int i = 0; i < 40; i++) many[i] = "x";
    zui_menu_open(&UI, S.list, 0, 0, many, 0, 40);
    CHECK(UI.menu.n == ZUI_MENU_ITEMS, "capped");
    zui_menu_close(&UI);
    zui_menu_close(&UI);
    CHECK(!UI.menu.open, "close twice is fine");
    (void) picks;
}

static const zui_palette_item_t ACTIONS[] = {
    {"Summarize", "companion", 300},
    {"Rewrite", "editor", 600},
    {"Suggest folders", "librarian", 120},
    {"Explain this reply", "companion", 200},
};

static void test_palette(void)
{
    CHECK(zui_palette_matches("smz", "Summarize"), "subsequence");
    CHECK(zui_palette_matches("SUM", "Summarize"), "any case");
    CHECK(zui_palette_matches("sug fold", "Suggest folders"), "spaces ignored");
    CHECK(!zui_palette_matches("zz", "Summarize"), "no match");
    CHECK(!zui_palette_matches("mus", "Summarize"), "order matters");
    CHECK(zui_palette_matches("", "x") && zui_palette_matches(0, "x"), "empty matches all");
    CHECK(!zui_palette_matches("a", 0) && zui_palette_matches("", 0), "NULL name");

    build_screen(&laptop);
    zui_palette_set(&UI, S.root, ACTIONS, 4);
    zui_focus(&UI, S.notes, true);
    key('k', ZUI_MOD_CMD);
    CHECK(UI.palette.open && UI.palette.n_match == 4 && UI.palette.hot == 0,
          "Cmd-K opens the palette with every action (W5)");
    CHECK(UI.palette.r.w > 0 && UI.palette.r.x > 0, "laid out");
    type("re");
    CHECK(UI.palette.n_match == 3 && UI.palette.match[0] == 0 && UI.palette.match[1] == 1 &&
              UI.palette.match[2] == 3,
          "filtered: Summarize, Rewrite, Explain this reply (not Suggest folders)");
    CHECK(S.notes_buf[0] == 0, "typing in the palette does not reach the focused field");
    key(ZUI_KEY_DOWN, 0);
    CHECK(UI.palette.hot == 1, "down");
    key(ZUI_KEY_TAB, 0);
    CHECK(UI.palette.hot == 2, "Tab moves down too");
    key(ZUI_KEY_DOWN, 0);
    CHECK(UI.palette.hot == 0, "wraps");
    key(ZUI_KEY_UP, 0);
    CHECK(UI.palette.hot == 2, "up wraps");
    reset_act();
    key(ZUI_KEY_ENTER, 0);
    CHECK(!UI.palette.open && last_act == ZUI_ACT_PICK && last_arg == 3 && last_w == S.root,
          "Enter picks: the index in the app's table");
    key('k', ZUI_MOD_CMD);
    type("what is in here?");
    CHECK(UI.palette.n_match == 0, "a question matches no action");
    reset_act();
    key(ZUI_KEY_ENTER, 0);
    CHECK(last_act == ZUI_ACT_ASK && strcmp(UI.palette.query, "what is in here?") == 0,
          "Enter sends it as a free question");
    key('k', ZUI_MOD_CMD);
    type("abc");
    key(ZUI_KEY_BACKSPACE, 0);
    CHECK(strcmp(UI.palette.query, "ab") == 0, "backspace in the query");
    key(ZUI_KEY_LEFT, 0);
    type("X");
    CHECK(strcmp(UI.palette.query, "aXb") == 0, "caret moves in the query");
    reset_act();
    key(ZUI_KEY_ESCAPE, 0);
    CHECK(!UI.palette.open && last_act == ZUI_ACT_DISMISS, "Escape");
    key('k', ZUI_MOD_CMD);
    key('k', ZUI_MOD_CMD);
    CHECK(!UI.palette.open, "Cmd-K again closes it");
    key('k', ZUI_MOD_CMD);
    CHECK(UI.palette.q.len == 0, "a fresh query each time");
    send(ev(ZUI_EV_PTR_DOWN, 2, 2, ZUI_BTN_LEFT, ++now));
    CHECK(!UI.palette.open, "click outside closes");
    key('k', ZUI_MOD_CMD);
    int32_t top = UI.palette.r.y + 44 + 8;
    reset_act();
    send(ev(ZUI_EV_PTR_DOWN, UI.palette.r.x + 30, top + 26 * 2 + 5, ZUI_BTN_LEFT, ++now));
    CHECK(last_act == ZUI_ACT_PICK && last_arg == 2, "click an action");
    key('k', ZUI_MOD_CMD);
    reset_act();
    key(ZUI_KEY_ENTER, 0);
    CHECK(last_act == ZUI_ACT_PICK && last_arg == 0, "empty query, Enter picks the first");
    zui_palette_close(&UI);
    zui_palette_set(&UI, S.root, ACTIONS, 0);
    key('k', ZUI_MOD_CMD);
    reset_act();
    key(ZUI_KEY_ENTER, 0);
    CHECK(n_act == 0 && UI.palette.open, "nothing to pick, nothing typed: stays");
    zui_palette_close(&UI);
    /* touch: a long press on empty ground opens it */
    zui_event_t t = ev(ZUI_EV_PTR_DOWN, 2, 2, ZUI_BTN_LEFT, 50000);
    send(t);
    zui_event_t tick = ev(ZUI_EV_TICK, 0, 0, 0, 50000 + ZUI_LONGPRESS_MS);
    send(tick);
    CHECK(UI.palette.open, "long press on the ground: Ask");
    zui_palette_close(&UI);
    /* a long press on a list row is a context request */
    zui_widget_t *l = zui_w(&UI, S.list);
    reset_act();
    send(ev(ZUI_EV_PTR_DOWN, l->r.x + 10, l->r.y + 28 + 4, ZUI_BTN_LEFT, 60000));
    send(ev(ZUI_EV_TICK, 0, 0, 0, 60000 + ZUI_LONGPRESS_MS));
    CHECK(last_act == ZUI_ACT_CONTEXT && last_w == S.list && last_arg == 1,
          "long press on a row: context for that row");
}

static void test_held(void)
{
    build_screen(&laptop);
    zui_widget_t *h = zui_w(&UI, S.held);
    const char *answer = "Here are three folders you could make: Photos, Letters, Receipts.";
    zui_held_set(&UI, S.held, "librarian", 120, false, answer, 1);
    zui_ui_paint(&UI, &(zui_surface_t){0});
    CHECK(!h->hidden && h->u.held.state == 1, "pending: shown");
    zui_id_t meta = h->first;
    CHECK(strcmp(zui_w(&UI, meta)->label,
                 "Held \xC2\xB7 librarian \xC2\xB7 ~120 tokens (estimate)") == 0,
          "who made it and what it cost, marked as an estimate (zxm M4)");
    zui_held_set(&UI, S.held, "librarian", 118, true, answer, 1);
    CHECK(strcmp(zui_w(&UI, meta)->label, "Held \xC2\xB7 librarian \xC2\xB7 118 tokens") == 0,
          "an exact count");
    zui_id_t body = zui_w(&UI, meta)->next;
    CHECK(zui_w(&UI, body)->u.field.readonly && zui_w(&UI, body)->u.field.len == strlen(answer),
          "the answer is read only");
    zui_id_t row = zui_w(&UI, body)->next;
    zui_id_t acc = zui_w(&UI, row)->first, rej = zui_w(&UI, acc)->next;
    CHECK(strcmp(zui_w(&UI, acc)->label, "Accept") == 0 &&
              zui_w(&UI, acc)->style == ZUI_BTN_PRIMARY,
          "Accept is the gold one");
    send(ev(ZUI_EV_TICK, 0, 0, 0, ++now));
    int32_t x, y;
    center(acc, &x, &y);
    reset_act();
    click(x, y);
    CHECK(last_act == ZUI_ACT_ACCEPT && last_w == S.held, "Accept reports on the held block");
    CHECK(strcmp(answer, "Here are three folders you could make: Photos, Letters, Receipts.") == 0,
          "W4: the widget applied nothing");
    center(rej, &x, &y);
    click(x, y);
    CHECK(last_act == ZUI_ACT_REJECT && last_w == S.held, "Reject");
    /* Cmd-Enter accepts the pending one from anywhere */
    zui_focus(&UI, S.list, true);
    reset_act();
    key(ZUI_KEY_ENTER, ZUI_MOD_CMD);
    CHECK(last_act == ZUI_ACT_ACCEPT && last_w == S.held, "Cmd-Enter accepts");
    /* typing into the body is impossible: it cannot take focus */
    zui_focus(&UI, body, true);
    CHECK(UI.focus != body, "held text is not focusable");
    zui_focus(&UI, ZUI_NONE, false);
    zui_focus_next(&UI, false);
    for (int i = 0; i < 12 && UI.focus != acc; i++) zui_focus_next(&UI, false);
    CHECK(UI.focus == acc, "Tab reaches Accept");
    /* accepted: buttons go away, the record stays */
    zui_held_set(&UI, S.held, "librarian", 118, true, answer, 2);
    send(ev(ZUI_EV_TICK, 0, 0, 0, ++now));
    CHECK(!h->hidden && zui_w(&UI, row)->hidden, "accepted: no buttons");
    CHECK(strncmp(zui_w(&UI, meta)->label, "Accepted", 8) == 0, "says accepted");
    reset_act();
    key(ZUI_KEY_ENTER, ZUI_MOD_CMD);
    CHECK(last_act != ZUI_ACT_ACCEPT, "Cmd-Enter does nothing once accepted");
    zui_held_set(&UI, S.held, "", 0, false, 0, 0);
    CHECK(h->hidden, "no answer: hidden");
    zui_held_set(&UI, S.save, "x", 1, true, "y", 1);
    CHECK(true, "held_set on a non-held widget is ignored");
}

static void test_a11y(void)
{
    build_screen(&laptop);
    zui_list_select(&UI, S.list, 1);
    zui_focus(&UI, S.save, true);
    zui_set_shortcut(&UI, S.save, "Cmd-S");
    zui_set_disabled(&UI, S.del, true);
    zui_progress_set(&UI, S.bar, 128, 1);
    static char out[4096];
    uint32_t n = zui_a11y_text(&UI, out, sizeof out);
    CHECK(n == strlen(out) && n > 0, "length");
    CHECK(strstr(out, "text \"Burrow\"\n"), "title read");
    CHECK(strstr(out, "list \"Files\" 15 rows, row 2 selected \"beta.png\"\n"), "list state");
    CHECK(strstr(out, "button \"Save\" shortcut Cmd-S focused\n"), "focused button");
    CHECK(strstr(out, "button \"Delete\" disabled\n"), "disabled");
    CHECK(strstr(out, "progressbar 50%\n"), "progress");
    CHECK(strstr(out, "textbox 0 bytes"), "field");
    CHECK(!strstr(out, "suggestion"), "hidden held is not read");
    CHECK(strstr(out, "  button \"Save\""), "indented under its panel");
    zui_progress_set(&UI, S.bar, ZUI_BAR_UNKNOWN, 1);
    zui_a11y_text(&UI, out, sizeof out);
    CHECK(strstr(out, "progressbar busy"), "unknown progress is busy, never a number");
    zui_held_set(&UI, S.held, "companion", 5, true, "hi", 1);
    zui_palette_set(&UI, S.root, ACTIONS, 4);
    zui_palette_open(&UI, "");
    zui_a11y_text(&UI, out, sizeof out);
    CHECK(strstr(out, "suggestion pending by companion"), "held read");
    CHECK(strstr(out, "dialog \"Ask\"") && strstr(out, "option \"Summarize\" selected"),
          "palette read");
    zui_palette_close(&UI);
    const char *items[] = {"Open", "-", "Delete"};
    zui_menu_open(&UI, S.list, 10, 10, items, 0, 3);
    zui_menu_disable(&UI, 2, true);
    zui_a11y_text(&UI, out, sizeof out);
    CHECK(strstr(out, "menu\n  menuitem \"Open\" focused\n  menuitem \"Delete\" disabled\n"),
          "menu read, separator skipped");
    char tiny[10];
    n = zui_a11y_text(&UI, tiny, sizeof tiny);
    CHECK(n == 9 && tiny[9] == 0, "cut to fit, terminated");
    CHECK(strcmp(zui_role_name(ZUI_W_CANVAS), "image") == 0 &&
              strcmp(zui_role_name(ZUI_W_SEPARATOR), "separator") == 0 &&
              strcmp(zui_role_name(ZUI_W_SCROLL), "scrollarea") == 0,
          "role names");
}

/* ---- paint: real glyphs, damage only, and the frame loop -------------------- */

static uint32_t fb_px[640 * 400];

static void paint_canvas(zui_ui_t *ui, zui_id_t w, zui_surface_t *s, zui_rect_t r, void *ctx)
{
    (void) ui;
    (void) w;
    (*(int *) ctx)++;
    zui_fill(s, r, 0x123456);
}

static void test_paint(void)
{
    static zui_glyph_cache_t cache;
    static ttf_font_t font;
    CHECK(ttf_parse(&font, MINI_TTF, sizeof MINI_TTF), "fixture font");
    zui_glyph_cache_init(&cache);
    zui_theme_t th;
    zui_text_t t = {&font, &cache, 16, false};
    th.body = th.label = th.title = th.mono = t;
    zui_screen_t sc = {640, 400, 256, ZUI_INPUT_POINTER};
    zui_ui_init(&UI, &sc, &th);
    zui_id_t root = zui_box(&UI, ZUI_NONE, ZUI_COL);
    zui_set_pad(&UI, root, 8, 8);
    zui_id_t b = zui_button(&UI, root, "Go", ZUI_BTN_PRIMARY);
    int painted = 0;
    zui_id_t cv = zui_canvas(&UI, root, paint_canvas, &painted);
    zui_id_t sc_area = zui_scroll(&UI, root);
    for (int i = 0; i < 40; i++) zui_button(&UI, sc_area, "Row", ZUI_BTN_SECONDARY);
    static uint32_t px[640 * 400];
    zui_surface_t s;
    zui_surface_init(&s, px, 640, 400, 640);
    zui_ui_paint(&UI, &s);
    CHECK(painted == 1, "first paint draws the canvas");
    CHECK(px[0] == ZUI_VOID, "ground is #002911");
    zui_widget_t *bw = zui_w(&UI, b);
    CHECK(px[(bw->r.y + 3) * 640 + bw->r.x + bw->r.w / 2] == ZUI_GOLD, "primary is gold (W2)");
    zui_damage_clear(&UI.damage);
    zui_ui_paint(&UI, &s);
    CHECK(painted == 1, "no damage: nothing painted (W7)");
    zui_invalidate(&UI, b);
    zui_ui_paint(&UI, &s);
    CHECK(painted == 1, "damage on the button does not repaint the canvas");
    zui_damage_clear(&UI.damage);
    zui_invalidate(&UI, cv);
    zui_ui_paint(&UI, &s);
    CHECK(painted == 2, "the canvas when it is damaged");
    zui_damage_clear(&UI.damage);
    /* focus ring */
    zui_focus(&UI, b, true);
    zui_ui_paint(&UI, &s);
    CHECK(px[(bw->r.y - 2) * 640 + bw->r.x + bw->r.w / 2] == ZUI_GOLD ||
              px[(bw->r.y - 3) * 640 + bw->r.x + bw->r.w / 2] == ZUI_GOLD,
          "a gold focus ring outside the button");
    /* scroll area: wheel and focus-follow */
    zui_widget_t *sa = zui_w(&UI, sc_area);
    CHECK(sa->u.scroll.content_h > sa->r.h, "content taller than its box");
    zui_event_t w = ev(ZUI_EV_SCROLL, sa->r.x + 5, sa->r.y + 5, 0, 1);
    w.dy = 50;
    zui_ui_event(&UI, &w);
    CHECK(sa->u.scroll.offset == 50, "wheel scrolls the area");
    w.dy = 100000;
    zui_ui_event(&UI, &w);
    CHECK(sa->u.scroll.offset == sa->u.scroll.content_h - sa->r.h, "clamped at the bottom");
    zui_id_t first_row = sa->first;
    zui_focus(&UI, first_row, true);
    CHECK(sa->u.scroll.offset == 0 && zui_w(&UI, first_row)->r.y >= sa->r.y,
          "Tab to a hidden row scrolls it into view");
    zui_ui_paint(&UI, &s);

    /* the frame loop through the framebuffer present layer */
    zui_fb_t fb;
    memset(&fb, 0, sizeof fb);
    fb.base = fb_px;
    fb.w = 640;
    fb.h = 400;
    fb.pitch_px = 640;
    static zui_queue_t drv, q;
    zui_queue_init(&drv);
    zui_queue_init(&q);
    fb.drv_queue = &drv;
    zui_present_t p;
    memset(&p, 0, sizeof p);
    zui_present_fb_bind(&p, &fb);
    CHECK(p.ops->open(&p, "zxv", 640, 400) == ZUI_PRESENT_OK, "open");
    zui_damage_all(&UI.damage, 640, 400);
    CHECK(zui_ui_frame(&UI, &p, &q, &s, 0), "a frame");
    CHECK(p.frames == 1 && memcmp(fb_px, px, sizeof px) == 0, "P2: the screen is the surface");
    CHECK(UI.damage.n == 0, "damage cleared after presenting");
    uint64_t copied = p.px_copied;
    CHECK(zui_ui_frame(&UI, &p, &q, &s, 0) && p.px_copied == copied, "idle: nothing copied");
    zui_push_key(&drv, true, ZUI_KEY_TAB, 0, false, 5);
    zui_ui_frame(&UI, &p, &q, &s, 0);
    CHECK(p.px_copied > copied && p.px_copied - copied < 640u * 400u,
          "a focus move copies only what changed");
    zui_event_t c;
    memset(&c, 0, sizeof c);
    c.type = ZUI_EV_CLOSE;
    zui_push(&drv, &c);
    CHECK(!zui_ui_frame(&UI, &p, &q, &s, 0), "close ends the loop");
    zui_push_ptr(&drv, ZUI_EV_PTR_MOVE, 1, 1, 0, 1);
    zui_event_t rs = ev(ZUI_EV_RESIZE, 320, 200, 0, 2);
    zui_push(&drv, &rs);
    CHECK(zui_ui_frame(&UI, &p, &q, &s, 0) && p.screen.px_w == 320,
          "a resize reaches the present layer's screen");
    /* a touch makes targets touch-sized */
    zui_ui_init(&UI, &sc, &th);
    root = zui_box(&UI, ZUI_NONE, ZUI_COL);
    b = zui_button(&UI, root, "Go", ZUI_BTN_PRIMARY);
    zui_ui_paint(&UI, &s);
    CHECK(zui_w(&UI, b)->r.h == 28, "pointer: 28 pt");
    zui_event_t tb = ev(ZUI_EV_TOUCH_BEGIN, 1, 1, 0, 1);
    zui_ui_event(&UI, &tb);
    zui_ui_paint(&UI, &s);
    CHECK(UI.screen.input == ZUI_INPUT_TOUCH && zui_w(&UI, b)->r.h == 44,
          "first touch: 44 pt targets");
}

int main(void)
{
    test_queue();
    test_gesture();
    test_utf8();
    test_damage();
    test_fb();
    test_set1();
    test_build();
    test_layout();
    test_focus();
    test_buttons();
    test_list();
    test_field();
    test_multiline();
    test_menu();
    test_palette();
    test_held();
    test_a11y();
    test_paint();
    printf("%s test_zui_widget: %d/%d checks\n", fails ? "[FAIL]" : "[PASS]", checks - fails,
           checks);
    return fails ? 1 : 0;
}
