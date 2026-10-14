/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zui_gesture.c — host tests for the touchpad recogniser: integer
 * geometry, palms, pointer motion, taps by finger count, two-finger
 * scroll / zoom / rotate, natural scrolling, inertia, three-finger swipes,
 * four-finger pinch, finger-count changes, and the ordinary events. */
#include <stdio.h>
#include <string.h>

#include "zui_gesture.h"

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

static zui_tpad_t TP;
static zui_tpad_ev_t LOG[512];
static uint32_t NLOG;
static uint32_t MS;

static void reset(void)
{
    zui_tpad_init(&TP);
    NLOG = 0;
    MS = 1000;
}

static uint32_t frame(const zui_tpad_contact_t *c, uint32_t n)
{
    zui_tpad_ev_t out[ZUI_TPAD_EV_MAX];
    uint32_t k = zui_tpad_frame(&TP, c, n, MS, out, ZUI_TPAD_EV_MAX);
    for (uint32_t i = 0; i < k; i++)
        if (NLOG < 512) LOG[NLOG++] = out[i];
    MS += 10;
    return k;
}

static uint32_t tick(uint32_t ms)
{
    zui_tpad_ev_t out[ZUI_TPAD_EV_MAX];
    uint32_t k = zui_tpad_tick(&TP, ms, out, ZUI_TPAD_EV_MAX);
    for (uint32_t i = 0; i < k; i++)
        if (NLOG < 512) LOG[NLOG++] = out[i];
    return k;
}

static uint32_t count(uint8_t kind)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < NLOG; i++)
        if (LOG[i].kind == kind) n++;
    return n;
}

static int32_t last_of(uint8_t kind)
{
    for (uint32_t i = NLOG; i > 0; i--)
        if (LOG[i - 1].kind == kind) return (int32_t) (i - 1);
    return -1;
}

static zui_tpad_contact_t C(uint16_t id, int32_t x, int32_t y)
{
    zui_tpad_contact_t c = {id, x, y, 10};
    return c;
}

static void lift(void)
{
    frame(0, 0);
}

/* ---- geometry -------------------------------------------------------------------------- */

static void test_geometry(void)
{
    CHECK(zui_tpad_isqrt(0) == 0 && zui_tpad_isqrt(-4) == 0 && zui_tpad_isqrt(1) == 1 &&
              zui_tpad_isqrt(15) == 3 && zui_tpad_isqrt(16) == 4 && zui_tpad_isqrt(17) == 4,
          "isqrt small");
    CHECK(zui_tpad_isqrt(25000000) == 5000 && zui_tpad_isqrt(4611686014132420609LL) == 2147483647,
          "isqrt large");
    CHECK(zui_tpad_atan2_deg(0, 10) == 0 && zui_tpad_atan2_deg(10, 0) == 90 &&
              zui_tpad_atan2_deg(0, -10) == 180 && zui_tpad_atan2_deg(-10, 0) == 270,
          "axes");
    CHECK(zui_tpad_atan2_deg(10, 10) == 45 && zui_tpad_atan2_deg(10, -10) == 135 &&
              zui_tpad_atan2_deg(-10, -10) == 225 && zui_tpad_atan2_deg(-10, 10) == 315,
          "diagonals");
    CHECK(zui_tpad_atan2_deg(577, 1000) == 30 && zui_tpad_atan2_deg(1000, 577) == 60 &&
              zui_tpad_atan2_deg(-577, 1000) == 330 && zui_tpad_atan2_deg(1000, -577) == 120,
          "30 and 60 degrees");
    CHECK(zui_tpad_atan2_deg(0, 0) == 0, "origin");
    /* within a degree over a sweep */
    static const int32_t ref[][3] = {{17, 1000, 1},   {176, 1000, 10}, {1000, 3, 90},
                                     {-1000, 3, 270}, {3, -1000, 180}, {-358, -1000, 200}};
    for (uint32_t i = 0; i < sizeof ref / sizeof ref[0]; i++) {
        int32_t a = zui_tpad_atan2_deg(ref[i][0], ref[i][1]);
        int32_t d = a - ref[i][2];
        CHECK(d >= -1 && d <= 1, "atan2(%d,%d) = %d, want %d", ref[i][0], ref[i][1], a, ref[i][2]);
    }
}

/* ---- G1..G3 ---------------------------------------------------------------------------- */

static void test_pointer_and_taps(void)
{
    reset();
    zui_tpad_set_bounds(&TP, 100, 80, 50, 40);
    zui_tpad_contact_t c = C(1, 200, 200);
    CHECK(frame(&c, 1) == 0, "landing sends nothing");
    c.x = 210;
    c.y = 195;
    frame(&c, 1);
    CHECK(NLOG == 1 && LOG[0].kind == ZUI_TPAD_MOVE && LOG[0].dx == 10 && LOG[0].dy == -5 &&
              LOG[0].x == 60 && LOG[0].y == 35,
          "one finger moves the pointer");
    c.x = 400;
    frame(&c, 1);
    CHECK(LOG[NLOG - 1].x == 99, "the pointer stops at the edge");
    c.y = -500;
    frame(&c, 1);
    CHECK(LOG[NLOG - 1].y == 0, "and at the top");
    lift();
    CHECK(count(ZUI_TPAD_CLICK) == 0, "a move is not a tap");

    /* speed with remainders */
    reset();
    TP.cfg.speed_q8 = 128; /* half speed */
    c = C(1, 0, 0);
    frame(&c, 1);
    for (int i = 1; i <= 4; i++) {
        c.x = i;
        frame(&c, 1);
    }
    int32_t sum = 0;
    for (uint32_t i = 0; i < NLOG; i++) sum += LOG[i].dx;
    CHECK(sum == 2 && NLOG == 2, "half speed: four 1 px moves make 2 px, no motion lost (%d)", sum);
    lift();

    /* taps */
    reset();
    zui_tpad_set_bounds(&TP, 100, 80, 7, 9);
    c = C(1, 200, 200);
    frame(&c, 1);
    c.x = 203;
    frame(&c, 1);
    lift();
    int32_t k = last_of(ZUI_TPAD_CLICK);
    CHECK(k >= 0 && LOG[k].fingers == 1 && LOG[k].x == 10 && LOG[k].y == 9,
          "a one-finger tap clicks at the pointer");
    NLOG = 0;
    zui_tpad_contact_t two[2] = {C(1, 100, 100), C(2, 160, 100)};
    frame(two, 2);
    lift();
    CHECK(count(ZUI_TPAD_SECONDARY_CLICK) == 1 && count(ZUI_TPAD_CLICK) == 0,
          "a two-finger tap is a secondary click");
    NLOG = 0;
    frame(two, 1); /* fingers land a frame apart */
    frame(two, 2);
    frame(&two[1], 1); /* and lift a frame apart */
    lift();
    CHECK(count(ZUI_TPAD_SECONDARY_CLICK) == 1 && count(ZUI_TPAD_CLICK) == 0,
          "staggered fingers still make one secondary click");
    NLOG = 0;
    zui_tpad_contact_t three[3] = {C(1, 100, 100), C(2, 140, 100), C(3, 180, 100)};
    frame(three, 3);
    lift();
    CHECK(count(ZUI_TPAD_MIDDLE_CLICK) == 1, "three-finger tap: middle");
    NLOG = 0;
    zui_tpad_contact_t four[4] = {C(1, 100, 100), C(2, 140, 100), C(3, 180, 100), C(4, 220, 100)};
    frame(four, 4);
    lift();
    CHECK(NLOG == 0, "four-finger tap: nothing");
    NLOG = 0;
    c = C(1, 200, 200);
    frame(&c, 1);
    MS += 300;
    lift();
    CHECK(count(ZUI_TPAD_CLICK) == 0, "too long is not a tap");
    NLOG = 0;
    TP.cfg.tap_to_click = false;
    frame(&c, 1);
    lift();
    CHECK(count(ZUI_TPAD_CLICK) == 0, "tap-to-click off");
    TP.cfg.tap_to_click = true;
    NLOG = 0;
    frame(&c, 1);
    c.x += 11;
    frame(&c, 1);
    c.x -= 11;
    frame(&c, 1);
    lift();
    CHECK(count(ZUI_TPAD_CLICK) == 0, "moving past the slop and back is not a tap");
}

static void test_palm(void)
{
    reset();
    zui_tpad_contact_t p = {9, 50, 300, 60};
    zui_tpad_contact_t pc[2] = {p, C(1, 200, 200)};
    frame(&p, 1);
    CHECK(TP.fingers == 0 && TP.nf == 1 && TP.f[0].palm, "a large contact is a palm");
    p.x = 300;
    frame(&p, 1);
    CHECK(NLOG == 0, "a palm moving does nothing");
    p.size = 8;
    pc[0] = p;
    frame(pc, 2);
    CHECK(TP.fingers == 1, "a palm stays a palm when it reads small; one finger counts");
    pc[1].x = 220;
    frame(pc, 2);
    CHECK(NLOG == 1 && LOG[0].kind == ZUI_TPAD_MOVE && LOG[0].dx == 20,
          "the finger beside the palm moves the pointer alone");
    frame(&pc[0], 1);
    frame(0, 0);
    CHECK(TP.nf == 0, "everything lifted");
    TP.cfg.palm_px = 100;
    frame(&p, 1);
    CHECK(TP.fingers == 1, "the palm threshold is a setting");
}

/* ---- G4, G5 ----------------------------------------------------------------------------- */

static void scroll_by(int32_t dy_per_frame, int frames, uint32_t step_ms)
{
    zui_tpad_contact_t two[2] = {C(1, 100, 300), C(2, 160, 300)};
    frame(two, 2);
    for (int i = 0; i < frames; i++) {
        two[0].y += dy_per_frame;
        two[1].y += dy_per_frame;
        MS += step_ms - 10;
        frame(two, 2);
    }
}

static void test_scroll(void)
{
    reset();
    zui_tpad_contact_t two[2] = {C(1, 100, 300), C(2, 160, 300)};
    frame(two, 2);
    two[0].y -= 5;
    two[1].y -= 5;
    frame(two, 2);
    CHECK(NLOG == 0, "under the scroll threshold: nothing");
    two[0].y -= 5;
    two[1].y -= 5;
    frame(two, 2);
    CHECK(NLOG == 1 && LOG[0].kind == ZUI_TPAD_SCROLL && LOG[0].phase == ZUI_TPAD_BEGIN &&
              LOG[0].dy == 10 && LOG[0].dx == 0 && LOG[0].fingers == 2,
          "fingers up 10: natural scroll begins, content up 10 (everything since the start)");
    two[0].y -= 7;
    two[1].y -= 7;
    two[0].x += 4;
    two[1].x += 4;
    frame(two, 2);
    CHECK(NLOG == 2 && LOG[1].phase == ZUI_TPAD_UPDATE && LOG[1].dy == 7 && LOG[1].dx == -4,
          "updates are frame deltas");
    frame(two, 2);
    CHECK(NLOG == 2, "no motion, no update");
    MS += 200;
    lift();
    CHECK(NLOG == 3 && LOG[2].phase == ZUI_TPAD_END && !TP.coasting,
          "a slow lift ends without coasting");
    CHECK(count(ZUI_TPAD_SECONDARY_CLICK) == 0, "a scroll is not a tap");

    /* traditional */
    reset();
    TP.cfg.natural = false;
    scroll_by(-10, 2, 16);
    CHECK(LOG[0].kind == ZUI_TPAD_SCROLL && LOG[0].dy == -10 && LOG[1].dy == -10,
          "natural off: content moves against the fingers");

    /* inertia */
    reset();
    scroll_by(-20, 6, 16);
    lift();
    CHECK(TP.coasting && LOG[NLOG - 1].phase != ZUI_TPAD_END, "a fast lift coasts");
    int32_t v0 = TP.vy_q8;
    CHECK(v0 > 15 * 256 && v0 <= 20 * 256, "velocity about 20 px a frame (%d)", v0);
    uint32_t n0 = NLOG;
    CHECK(tick(MS + 5) == 0, "less than a frame: nothing");
    tick(MS + 16);
    CHECK(NLOG == n0 + 1 && LOG[n0].phase == ZUI_TPAD_INERTIA && LOG[n0].dy > 0 && LOG[n0].dy <= 20,
          "the first coasting frame (%d)", LOG[n0].dy);
    int32_t total = LOG[n0].dy;
    uint32_t t = MS + 16;
    int guard = 0;
    while (TP.coasting && guard++ < 1000) {
        t += 16;
        uint32_t a = NLOG;
        tick(t);
        for (uint32_t i = a; i < NLOG; i++)
            if (LOG[i].phase == ZUI_TPAD_INERTIA) total += LOG[i].dy;
    }
    CHECK(!TP.coasting && LOG[NLOG - 1].phase == ZUI_TPAD_END, "it slows to a stop and ends");
    CHECK(total > 200 && total < 450, "the coast travels a bounded distance (%d)", total);
    /* the same coast in one long tick goes the same distance */
    reset();
    scroll_by(-20, 6, 16);
    lift();
    n0 = NLOG;
    tick(MS + 16 * 300);
    int32_t once = 0;
    for (uint32_t i = n0; i < NLOG; i++)
        if (LOG[i].phase == ZUI_TPAD_INERTIA) once += LOG[i].dy;
    CHECK(once == total && LOG[NLOG - 1].phase == ZUI_TPAD_END,
          "deterministic: one long tick = many short ones (%d vs %d)", once, total);
    /* touching stops it */
    reset();
    scroll_by(-20, 6, 16);
    lift();
    tick(MS + 16);
    zui_tpad_contact_t one = C(5, 10, 10);
    frame(&one, 1);
    CHECK(!TP.coasting && LOG[NLOG - 1].kind == ZUI_TPAD_SCROLL &&
              LOG[NLOG - 1].phase == ZUI_TPAD_END,
          "a touch stops the coast at once");
    CHECK(tick(MS + 1000) == 0, "and no more ticks");
    /* a pause before lifting: no coast */
    reset();
    scroll_by(-20, 6, 16);
    MS += 100;
    lift();
    CHECK(!TP.coasting && LOG[NLOG - 1].phase == ZUI_TPAD_END, "held still then lifted: no coast");
    /* a small out[] during a coast */
    reset();
    scroll_by(-20, 6, 16);
    lift();
    zui_tpad_ev_t one_slot[1];
    CHECK(zui_tpad_tick(&TP, MS + 16, one_slot, 1) == 0, "a coast needs two slots");
}

static void test_zoom_rotate(void)
{
    reset();
    zui_tpad_contact_t two[2] = {C(1, 100, 200), C(2, 200, 200)};
    frame(two, 2);
    two[0].x -= 10;
    two[1].x += 10;
    frame(two, 2);
    CHECK(NLOG == 0, "a small spread: nothing yet");
    two[0].x -= 5;
    two[1].x += 5;
    frame(two, 2);
    CHECK(NLOG == 1 && LOG[0].kind == ZUI_TPAD_ZOOM && LOG[0].phase == ZUI_TPAD_BEGIN &&
              LOG[0].scale_q8 == 130 * 256 / 100,
          "zoom begins at 130/100 (%d)", LOG[0].scale_q8);
    two[0].x -= 35;
    two[1].x += 35;
    frame(two, 2);
    CHECK(LOG[1].phase == ZUI_TPAD_UPDATE && LOG[1].scale_q8 == 512, "twice as far: 2x (q8 512)");
    two[0].y += 3; /* a drift of the centre is not a scroll */
    two[1].y += 3;
    frame(two, 2);
    CHECK(count(ZUI_TPAD_SCROLL) == 0, "locked: a zoom does not turn into a scroll");
    lift();
    CHECK(LOG[NLOG - 1].kind == ZUI_TPAD_ZOOM && LOG[NLOG - 1].phase == ZUI_TPAD_END &&
              LOG[NLOG - 1].scale_q8 == 512,
          "zoom ends with its scale");
    /* pinch in */
    reset();
    zui_tpad_contact_t p[2] = {C(1, 0, 0), C(2, 200, 0)};
    frame(p, 2);
    p[0].x = 50;
    p[1].x = 150;
    frame(p, 2);
    CHECK(LOG[0].kind == ZUI_TPAD_ZOOM && LOG[0].scale_q8 == 128, "pinch in to half");
    lift();
    /* rotate: turn two fingers about their centre */
    reset();
    zui_tpad_contact_t r[2] = {C(1, 100, 200), C(2, 300, 200)}; /* 0 degrees, radius 100 */
    frame(r, 2);
    r[0].x = 200 - 98; /* about 10 degrees clockwise (y down) */
    r[0].y = 200 - 17;
    r[1].x = 200 + 98;
    r[1].y = 200 + 17;
    frame(r, 2);
    CHECK(NLOG == 0, "10 degrees: under the threshold");
    r[0].x = 200 - 94; /* 20 degrees */
    r[0].y = 200 - 34;
    r[1].x = 200 + 94;
    r[1].y = 200 + 34;
    frame(r, 2);
    CHECK(NLOG == 1 && LOG[0].kind == ZUI_TPAD_ROTATE && LOG[0].phase == ZUI_TPAD_BEGIN &&
              LOG[0].angle >= 19 && LOG[0].angle <= 21,
          "rotate begins at 20 degrees clockwise (%d)", LOG[0].angle);
    r[0].x = 200 - 87; /* 30 */
    r[0].y = 200 - 50;
    r[1].x = 200 + 87;
    r[1].y = 200 + 50;
    frame(r, 2);
    CHECK(LOG[1].phase == ZUI_TPAD_UPDATE && LOG[1].angle >= 29 && LOG[1].angle <= 31,
          "30 degrees (%d)", LOG[1].angle);
    frame(r, 2);
    CHECK(NLOG == 2, "no turn, no update");
    lift();
    CHECK(LOG[NLOG - 1].kind == ZUI_TPAD_ROTATE && LOG[NLOG - 1].phase == ZUI_TPAD_END,
          "rotate ends");
    /* counter-clockwise across the 0/360 seam */
    reset();
    zui_tpad_contact_t w[2] = {C(1, 100, 200), C(2, 300, 200)};
    frame(w, 2);
    w[0].x = 200 - 94;
    w[0].y = 200 + 34;
    w[1].x = 200 + 94;
    w[1].y = 200 - 34;
    frame(w, 2);
    CHECK(NLOG == 1 && LOG[0].angle <= -19 && LOG[0].angle >= -21,
          "counter-clockwise is negative, across the seam (%d)", LOG[0].angle);
    lift();
}

/* ---- G6, G7 ------------------------------------------------------------------------------ */

static void swipe3(int32_t dx, int32_t dy)
{
    zui_tpad_contact_t f[3] = {C(1, 300, 300), C(2, 340, 300), C(3, 380, 300)};
    frame(f, 3);
    for (int i = 0; i < 10; i++) {
        for (int j = 0; j < 3; j++) {
            f[j].x += dx / 10;
            f[j].y += dy / 10;
        }
        frame(f, 3);
    }
    lift();
}

static void test_swipes(void)
{
    reset();
    swipe3(-100, 0);
    CHECK(count(ZUI_TPAD_SWIPE_LEFT) == 1 && NLOG == 1 && LOG[0].fingers == 3,
          "three fingers left: next app, once");
    CHECK(ZUI_TPAD_APP_NEXT == ZUI_TPAD_SWIPE_LEFT, "the name says what it is for");
    NLOG = 0;
    swipe3(100, 10);
    CHECK(count(ZUI_TPAD_SWIPE_RIGHT) == 1 && NLOG == 1, "right: previous app");
    NLOG = 0;
    swipe3(10, -100);
    CHECK(count(ZUI_TPAD_SWIPE_UP) == 1 && NLOG == 1, "up: all windows");
    NLOG = 0;
    swipe3(0, 100);
    CHECK(count(ZUI_TPAD_SWIPE_DOWN) == 1 && NLOG == 1, "down: the desktop");
    NLOG = 0;
    swipe3(-50, 0);
    CHECK(NLOG == 0, "a short swipe does nothing");
    NLOG = 0;
    TP.cfg.swipe_px = 40;
    swipe3(-50, 0);
    CHECK(count(ZUI_TPAD_SWIPE_LEFT) == 1, "the threshold is a setting");

    /* four-finger pinch */
    reset();
    zui_tpad_contact_t f[4] = {C(1, 100, 100), C(2, 300, 100), C(3, 100, 300), C(4, 300, 300)};
    frame(f, 4);
    for (int i = 0; i < 5; i++) {
        f[0].x += 10;
        f[0].y += 10;
        f[1].x -= 10;
        f[1].y += 10;
        f[2].x += 10;
        f[2].y -= 10;
        f[3].x -= 10;
        f[3].y -= 10;
        frame(f, 4);
    }
    lift();
    CHECK(count(ZUI_TPAD_PINCH4_IN) == 1 && NLOG == 1 && LOG[0].fingers == 4,
          "four fingers pinch in, once");
    NLOG = 0;
    zui_tpad_contact_t g[4] = {C(1, 150, 150), C(2, 250, 150), C(3, 150, 250), C(4, 250, 250)};
    frame(g, 4);
    for (int i = 0; i < 5; i++) {
        g[0].x -= 10;
        g[0].y -= 10;
        g[1].x += 10;
        g[1].y -= 10;
        g[2].x -= 10;
        g[2].y += 10;
        g[3].x += 10;
        g[3].y += 10;
        frame(g, 4);
    }
    lift();
    CHECK(count(ZUI_TPAD_PINCH4_OUT) == 1 && NLOG == 1, "and out");

    /* G7: a count change ends a scroll; the rest move the pointer */
    reset();
    scroll_by(-10, 3, 16);
    uint32_t n0 = NLOG;
    zui_tpad_contact_t one = C(1, 100, 270);
    frame(&one, 1);
    CHECK(NLOG == n0 + 1 && LOG[n0].kind == ZUI_TPAD_SCROLL && LOG[n0].phase == ZUI_TPAD_END &&
              !TP.coasting,
          "lifting one of two ends the scroll, no coast");
    one.x += 5;
    frame(&one, 1);
    CHECK(LOG[NLOG - 1].kind == ZUI_TPAD_MOVE && LOG[NLOG - 1].dx == 5,
          "the finger left moves the pointer, with no jump");
    lift();
    CHECK(count(ZUI_TPAD_CLICK) == 0, "no tap after a gesture");
    /* adding a third finger to a two-finger scroll */
    reset();
    scroll_by(-10, 2, 16);
    zui_tpad_contact_t th[3] = {C(1, 100, 280), C(2, 160, 280), C(3, 220, 280)};
    n0 = NLOG;
    frame(th, 3);
    CHECK(LOG[n0].phase == ZUI_TPAD_END, "a third finger ends the scroll");
    for (int i = 0; i < 10; i++) {
        for (int j = 0; j < 3; j++) th[j].x -= 10;
        frame(th, 3);
    }
    CHECK(count(ZUI_TPAD_SWIPE_LEFT) == 1, "and can then swipe");
    lift();
    /* too many contacts */
    reset();
    zui_tpad_contact_t many[12];
    for (uint16_t i = 0; i < 12; i++) many[i] = C(i, i * 20, 0);
    frame(many, 12);
    CHECK(TP.nf == ZUI_TPAD_CONTACTS && TP.fingers == ZUI_TPAD_CONTACTS, "at most ten tracked");
    lift();
    CHECK(TP.nf == 0 && TP.fingers == 0 && NLOG == 0, "and all lift; ten-finger taps do nothing");
    /* out[] of one */
    reset();
    zui_tpad_contact_t two[2] = {C(1, 100, 100), C(2, 160, 100)};
    zui_tpad_ev_t o1[1];
    zui_tpad_frame(&TP, two, 2, 0, o1, 1);
    CHECK(zui_tpad_frame(&TP, 0, 0, 5, o1, 0) == 0, "cap 0 writes nothing");
}

/* ---- G8 ------------------------------------------------------------------------------------ */

static void test_events(void)
{
    reset();
    zui_tpad_set_bounds(&TP, 640, 480, 320, 240);
    zui_event_t e[4];
    zui_tpad_ev_t g;
    memset(&g, 0, sizeof g);
    g.kind = ZUI_TPAD_MOVE;
    g.x = 330;
    g.y = 245;
    g.ms = 77;
    CHECK(zui_tpad_events(&TP, &g, e, 4) == 1 && e[0].type == ZUI_EV_PTR_MOVE && e[0].x == 330 &&
              e[0].y == 245 && e[0].ms == 77,
          "a move is a pointer move");
    g.kind = ZUI_TPAD_CLICK;
    CHECK(zui_tpad_events(&TP, &g, e, 4) == 2 && e[0].type == ZUI_EV_PTR_DOWN &&
              e[0].button == ZUI_BTN_LEFT && e[1].type == ZUI_EV_PTR_UP &&
              e[1].button == ZUI_BTN_LEFT,
          "a click is a left press and release");
    g.kind = ZUI_TPAD_SECONDARY_CLICK;
    CHECK(zui_tpad_events(&TP, &g, e, 4) == 2 && e[0].button == ZUI_BTN_RIGHT,
          "secondary is the right button");
    g.kind = ZUI_TPAD_MIDDLE_CLICK;
    CHECK(zui_tpad_events(&TP, &g, e, 4) == 2 && e[1].button == ZUI_BTN_MIDDLE, "middle");
    CHECK(zui_tpad_events(&TP, &g, e, 1) == 1, "never more than cap");
    g.kind = ZUI_TPAD_SCROLL;
    g.dx = 0;
    g.dy = 12;
    CHECK(zui_tpad_events(&TP, &g, e, 4) == 1 && e[0].type == ZUI_EV_SCROLL && e[0].dy == 12 &&
              e[0].x == 320 && e[0].y == 240,
          "a scroll is ZUI_EV_SCROLL at the pointer");
    g.dy = 0;
    CHECK(zui_tpad_events(&TP, &g, e, 4) == 0, "an empty scroll (END) sends nothing");
    g.kind = ZUI_TPAD_SWIPE_UP;
    CHECK(zui_tpad_events(&TP, &g, e, 4) == 0, "swipes are for the window manager");
    g.kind = ZUI_TPAD_ZOOM;
    CHECK(zui_tpad_events(&TP, &g, e, 4) == 0, "so is zoom");

    /* a whole pipeline into the queue */
    static zui_queue_t q;
    zui_queue_init(&q);
    reset();
    zui_tpad_set_bounds(&TP, 640, 480, 320, 240);
    zui_tpad_contact_t c = C(1, 10, 10);
    zui_tpad_ev_t out[ZUI_TPAD_EV_MAX];
    uint32_t n = zui_tpad_frame(&TP, &c, 1, 0, out, ZUI_TPAD_EV_MAX);
    n = zui_tpad_frame(&TP, 0, 0, 50, out, ZUI_TPAD_EV_MAX);
    for (uint32_t i = 0; i < n; i++) {
        uint32_t k = zui_tpad_events(&TP, &out[i], e, 4);
        for (uint32_t j = 0; j < k; j++) zui_push(&q, &e[j]);
    }
    zui_event_t pe;
    CHECK(zui_queue_len(&q) == 2 && zui_pop(&q, &pe) && pe.type == ZUI_EV_PTR_DOWN && pe.x == 320 &&
              q.buttons == 0,
          "tap to click reaches the queue as a click at the pointer");
}

int main(void)
{
    test_geometry();
    test_pointer_and_taps();
    test_palm();
    test_scroll();
    test_zoom_rotate();
    test_swipes();
    test_events();
    printf("%s test_zui_gesture: %d/%d checks\n", fails ? "[FAIL]" : "[PASS]", checks - fails,
           checks);
    return fails ? 1 : 0;
}
