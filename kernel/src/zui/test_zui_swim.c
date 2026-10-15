/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zui_swim.c — the chiglet swimming over the screen (zui_swim.h SW1-SW9). */
#include <stdio.h>
#include <string.h>

#include "zui_motion.h"
#include "zui_swim.h"
#include "zui_ttf_fixture.inc" /* ZUI_TTF[] */

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

#define W 800
#define H 600
static uint32_t PX[W * H], PX2[W * H];
static zui_swim_t A, B;
static const zui_rect_t WORK = {0, 0, W, H};
static const zui_chiglet_t LOOK = {{2, 5, 3, 1, 6, 2, 3, 1}, 0, ZUI_CHG_DECIDED};
static uint8_t DIG[CMP_HASH];

static cmp_decision_t dec(cmp_verdict_t v)
{
    cmp_decision_t d;
    memset(&d, 0, sizeof d);
    d.verdict = v;
    d.seq = 7;
    return d;
}

static void fresh(zui_swim_t *sw)
{
    zui_swim_init(sw, WORK, &LOOK);
    zui_swim_tick(sw, 1000);
}

/* run the clock forward in 16 ms frames up to t */
static uint32_t run(zui_swim_t *sw, uint32_t from, uint32_t to)
{
    for (uint32_t t = from; t < to; t += 16) zui_swim_tick(sw, t);
    zui_swim_tick(sw, to);
    return to;
}

static int32_t px(zui_swim_t *sw)
{
    int32_t x, y;
    zui_swim_pos(sw, &x, &y);
    return x;
}
static int32_t py(zui_swim_t *sw)
{
    int32_t x, y;
    zui_swim_pos(sw, &x, &y);
    return y;
}

static void ground(uint32_t *p)
{
    for (int i = 0; i < W * H; i++) p[i] = ZUI_VOID;
}

static void test_refusals(void)
{
    fresh(&A);
    zui_rect_t t = {100, 100, 200, 150};
    cmp_decision_t d = dec(CMP_REFUSE);
    CHECK(!zui_swim_offer(&A, t, &d, DIG, "x"), "SW2 a refused decision is not offered");
    d = dec(CMP_WAIT);
    CHECK(!zui_swim_offer(&A, t, &d, DIG, "x"), "SW2 WAIT is not offered");
    d = dec(CMP_PROCEED);
    d.quarantined = true;
    CHECK(!zui_swim_offer(&A, t, &d, DIG, "x"), "SW2 a quarantined decision is not offered");
    d = dec(CMP_PROCEED);
    d.seq = 0;
    CHECK(!zui_swim_offer(&A, t, &d, DIG, "x"), "SW2 an unlogged decision is not offered");
    d = dec(CMP_PROCEED);
    CHECK(!zui_swim_offer(&A, t, &d, DIG, ""), "SW2 empty text");
    char big[ZUI_SWIM_TEXT + 1];
    memset(big, 'a', sizeof big - 1);
    big[sizeof big - 1] = 0;
    CHECK(!zui_swim_offer(&A, t, &d, DIG, big), "SW2 text too long");
    big[ZUI_SWIM_TEXT - 1] = 0;
    CHECK(zui_swim_offer(&A, t, &d, DIG, big), "SW2 the longest text fits");
    fresh(&A);
    CHECK(!zui_swim_offer(&A, (zui_rect_t){1, 1, 0, 5}, &d, DIG, "x"), "SW2 empty target");
    CHECK(!zui_swim_offer(&A, t, &d, NULL, "x") && !zui_swim_offer(&A, t, NULL, DIG, "x") &&
              !zui_swim_offer(&A, t, &d, DIG, NULL),
          "SW2 missing arguments");
    CHECK(zui_swim_state(&A) == ZUI_SWIM_HIDDEN, "nothing offered, nothing shows");
    d = dec(CMP_ASK);
    CHECK(zui_swim_offer(&A, t, &d, DIG, "x") && zui_swim_state(&A) == ZUI_SWIM_ARRIVING,
          "SW2 ASK may be offered");
}

static void test_swim(void)
{
    zui_motion_reset();
    fresh(&A);
    fresh(&B);
    zui_rect_t t = {100, 200, 200, 150};
    cmp_decision_t d = dec(CMP_PROCEED);
    CHECK(zui_swim_offer(&A, t, &d, DIG, "Tidy these files?"), "offered");
    zui_swim_offer(&B, t, &d, DIG, "Tidy these files?");
    /* comes in from the nearest edge, unseen */
    zui_swim_tick(&A, 1000);
    CHECK(py(&A) < 0 && px(&A) == 100 + 200 + ZUI_SWIM_GAP + 2 * (int32_t) ZUI_SWIM_R &&
              zui_swim_alpha(&A) == 0,
          "SW5 it enters unseen from beyond the nearest edge, the top (%d %d)", px(&A), py(&A));
    /* SW7: it waits to the right of the target */
    int32_t wx = 100 + 200 + ZUI_SWIM_GAP + 2 * (int32_t) ZUI_SWIM_R, wy = 200 + 2 * ZUI_SWIM_R;
    bool waved = false, monotone = true;
    int64_t sx = px(&A), sy = py(&A), last = -1;
    for (uint32_t t2 = 1016; t2 < 1000 + ZUI_SWIM_TRAVEL_MS; t2 += 16) {
        zui_swim_tick(&A, t2);
        zui_swim_tick(&B, t2);
        if (px(&A) != wx) waved = true;
        /* progress along the path (the projection onto it) never goes back */
        int64_t along = (px(&A) - sx) * (wx - sx) + (py(&A) - sy) * (wy - sy);
        if (along < last) monotone = false;
        last = along;
        CHECK(px(&A) == px(&B) && py(&A) == py(&B) && zui_swim_alpha(&A) == zui_swim_alpha(&B),
              "SW8 the same clock gives the same swim");
        if (t2 == 1016 + 16 * 40) {
            CHECK(zui_swim_state(&A) == ZUI_SWIM_ARRIVING, "SW5 still swimming mid-way");
        }
    }
    CHECK(waved, "SW5 the swim waves side to side");
    CHECK(monotone, "SW5 it never swims backwards along the path");
    zui_swim_tick(&A, 1000 + ZUI_SWIM_TRAVEL_MS);
    CHECK(zui_swim_state(&A) == ZUI_SWIM_WAITING && px(&A) == wx && zui_swim_alpha(&A) == 255,
          "SW5 SW7 it arrives beside the target (%d %d, a %u)", px(&A), py(&A), zui_swim_alpha(&A));
    /* SW5: it bobs while it waits, by at most 3 px */
    int32_t lo = 9999, hi = -9999;
    for (uint32_t t2 = 3000; t2 < 3000 + ZUI_SWIM_BOB_MS; t2 += 50) {
        zui_swim_tick(&A, t2);
        lo = py(&A) < lo ? py(&A) : lo;
        hi = py(&A) > hi ? py(&A) : hi;
    }
    CHECK(hi - lo >= 4 && hi - lo <= 6 && lo >= wy - 3 && hi <= wy + 3, "SW5 the bob (%d..%d)", lo,
          hi);
    /* SW8: a clock stepping back is no time */
    zui_swim_tick(&A, 9000);
    int32_t y0 = py(&A);
    zui_swim_tick(&A, 8000);
    CHECK(py(&A) == y0, "SW8 a clock stepping back moves nothing");
}

static void test_places(void)
{
    zui_motion_reset();
    cmp_decision_t d = dec(CMP_PROCEED);
    int32_t e = 2 * (int32_t) ZUI_SWIM_R;
    struct {
        zui_rect_t t;
        int32_t x, y;
        const char *what;
    } C[] = {
        {{100, 100, 100, 100}, 100 + 100 + ZUI_SWIM_GAP + e, 100 + e, "room on the right"},
        {{600, 100, 200, 100}, 600 - ZUI_SWIM_GAP - e, 100 + e, "the left at the right edge"},
        {{0, 300, 800, 100}, 800 - e, 300 - ZUI_SWIM_GAP - e, "above a full-width target"},
        {{0, 0, 800, 600}, 800 - e, e, "inside a full-screen target"},
        {{0, 560, 50, 40}, 50 + ZUI_SWIM_GAP + e, 600 - e, "kept inside at the bottom"},
    };
    for (unsigned i = 0; i < sizeof C / sizeof C[0]; i++) {
        fresh(&A);
        zui_swim_offer(&A, C[i].t, &d, DIG, "x");
        run(&A, 1000, 1000 + ZUI_SWIM_TRAVEL_MS + 100);
        int32_t x, y;
        zui_swim_pos(&A, &x, &y);
        zui_motion_set_flag(ZUI_PREF_REDUCE_MOTION, true); /* no bob */
        zui_swim_tick(&A, 1000 + ZUI_SWIM_TRAVEL_MS + 200);
        zui_swim_pos(&A, &x, &y);
        zui_motion_reset();
        CHECK(x == C[i].x && y == C[i].y, "SW7 %s: (%d %d) want (%d %d)", C[i].what, x, y, C[i].x,
              C[i].y);
        CHECK(x - e >= 0 && y - e >= 0 && x + e <= W && y + e <= H, "SW7 wholly inside: %s",
              C[i].what);
    }
    /* entries from the nearest edge */
    fresh(&A);
    zui_swim_offer(&A, (zui_rect_t){600, 250, 50, 50}, &d, DIG, "x");
    CHECK(px(&A) > W, "SW5 from the right edge (%d)", px(&A));
    fresh(&A);
    zui_swim_offer(&A, (zui_rect_t){300, 450, 50, 50}, &d, DIG, "x");
    CHECK(py(&A) > H, "SW5 from the bottom edge (%d)", py(&A));
    fresh(&A);
    zui_swim_offer(&A, (zui_rect_t){300, 10, 50, 50}, &d, DIG, "x");
    CHECK(py(&A) < 0, "SW5 from the top edge (%d)", py(&A));
}

static void test_glass(void)
{
    zui_motion_reset();
    fresh(&A);
    /* the target's window on the left, another window across the way */
    zui_rect_t win[2] = {{0, 100, 250, 400}, {300, 100, 200, 400}};
    zui_swim_set_windows(&A, win, 2);
    cmp_decision_t d = dec(CMP_PROCEED);
    zui_swim_offer(&A, (zui_rect_t){600, 200, 60, 60}, &d, DIG, "first");
    run(&A, 1000, 1000 + ZUI_SWIM_TRAVEL_MS + 50);
    zui_rect_t t = {50, 200, 80, 80};
    CHECK(zui_swim_offer(&A, t, &d, DIG, "second") && zui_swim_state(&A) == ZUI_SWIM_ARRIVING,
          "SW2 a new offer replaces a waiting one");
    uint32_t seen_through = 0, z_back = 0, outside_full = 0, start = 1000 + ZUI_SWIM_TRAVEL_MS + 50;
    for (uint32_t t2 = start + 16; t2 <= start + ZUI_SWIM_TRAVEL_MS; t2 += 16) {
        zui_swim_tick(&A, t2);
        int32_t x = px(&A), y = py(&A);
        bool in_other = x >= 300 && x < 500 && y >= 100 && y < 500;
        if (in_other && zui_swim_alpha(&A) <= ZUI_SWIM_THROUGH_A) seen_through++;
        if (in_other && zui_swim_depth(&A) == -ZUI_SWIM_DEPTH_PX) z_back++;
        if (!in_other && zui_swim_depth(&A) != 0) outside_full++;
    }
    CHECK(seen_through > 5, "SW6 it fades inside a window it passes through (%u)", seen_through);
    CHECK(z_back > 5 && outside_full == 0, "SW6 behind the glass only inside one (%u %u)", z_back,
          outside_full);
    run(&A, start + ZUI_SWIM_TRAVEL_MS, start + ZUI_SWIM_TRAVEL_MS + 300);
    CHECK(zui_swim_state(&A) == ZUI_SWIM_WAITING && zui_swim_alpha(&A) == 255 &&
              zui_swim_depth(&A) == 0,
          "SW6 beside its target, in its own window, it is whole and at the glass");
    /* reduce transparency: opaque whenever it shows */
    fresh(&A);
    zui_swim_set_windows(&A, win, 2);
    zui_swim_offer(&A, t, &d, DIG, "x");
    zui_motion_set_flag(ZUI_PREF_REDUCE_TRANSPARENCY, true);
    bool all = true;
    for (uint32_t t2 = 1016; t2 <= 1000 + ZUI_SWIM_TRAVEL_MS; t2 += 16) {
        zui_swim_tick(&A, t2);
        if (zui_swim_alpha(&A) != 0 && zui_swim_alpha(&A) != 255) all = false;
    }
    CHECK(all, "SW6 reduce transparency: never translucent");
    zui_motion_reset();
}

static void test_reduced(void)
{
    zui_motion_reset();
    zui_motion_set_flag(ZUI_PREF_REDUCE_MOTION, true);
    fresh(&A);
    cmp_decision_t d = dec(CMP_PROCEED);
    zui_swim_offer(&A, (zui_rect_t){100, 100, 100, 100}, &d, DIG, "x");
    zui_swim_tick(&A, 1016);
    int32_t e = 2 * (int32_t) ZUI_SWIM_R;
    CHECK(zui_swim_state(&A) == ZUI_SWIM_WAITING && px(&A) == 100 + 100 + ZUI_SWIM_GAP + e &&
              py(&A) == 100 + e,
          "SW5 reduce motion: nothing travels, it is beside the target");
    CHECK(zui_swim_alpha(&A) > 0 && zui_swim_alpha(&A) < 255, "SW5 FADE: it fades in (%u)",
          zui_swim_alpha(&A));
    run(&A, 1016, 1300);
    CHECK(zui_swim_alpha(&A) == 255, "SW5 and is whole after the fade");
    int32_t y = py(&A);
    run(&A, 1300, 4000);
    CHECK(py(&A) == y, "SW5 no bob with reduce motion");
    /* CUT */
    zui_motion_set_crossfade(0);
    fresh(&A);
    zui_swim_offer(&A, (zui_rect_t){100, 100, 100, 100}, &d, DIG, "x");
    zui_swim_tick(&A, 1001);
    CHECK(zui_swim_alpha(&A) == 255 && zui_swim_state(&A) == ZUI_SWIM_WAITING,
          "SW5 CUT: it is simply there");
    zui_motion_reset();
}

/* arrive, still (reduced motion keeps it from bobbing) */
static void arrive(zui_swim_t *sw, zui_rect_t t, const char *text)
{
    fresh(sw);
    cmp_decision_t d = dec(CMP_PROCEED);
    zui_swim_offer(sw, t, &d, DIG, text);
    run(sw, 1000, 1000 + ZUI_SWIM_TRAVEL_MS + 50);
}

static void test_taps(void)
{
    zui_motion_reset();
    for (uint32_t i = 0; i < CMP_HASH; i++) DIG[i] = (uint8_t) (i * 7 + 1);
    zui_rect_t t = {100, 200, 200, 150};
    arrive(&A, t, "Tidy these files?");
    int32_t x = px(&A), y = py(&A);
    char buf[256];
    zui_swim_describe(&A, buf, sizeof buf);
    CHECK(strstr(buf, "has a suggestion") && !strstr(buf, "Tidy"),
          "SW3 SW9 the text stays unread until it is opened: %s", buf);
    /* SW1: only the body takes a tap */
    CHECK(zui_swim_hit(&A, x, y) && zui_swim_hit(&A, x + (int32_t) ZUI_SWIM_R - 1, y),
          "SW1 the body is hit");
    CHECK(!zui_swim_hit(&A, x + (int32_t) ZUI_SWIM_R + 2, y) && !zui_swim_hit(&A, 150, 250) &&
              !zui_swim_hit(&A, x, y + (int32_t) ZUI_SWIM_R + 2),
          "SW1 everywhere else passes through");
    uint8_t got[CMP_HASH];
    memset(got, 0, sizeof got);
    CHECK(zui_swim_tap(&A, 150, 250, got) == ZUI_SWIM_NONE, "SW1 a tap on the window below");
    zui_rect_t bub = zui_swim_bubble(&A);
    CHECK(!zui_swim_hit(&A, bub.x + 5, bub.y + 5), "SW1 no bubble before the tap");
    CHECK(zui_swim_tap(&A, x, y, got) == ZUI_SWIM_OPENED && zui_swim_state(&A) == ZUI_SWIM_OPEN,
          "SW3 a tap opens the bubble");
    zui_swim_describe(&A, buf, sizeof buf);
    CHECK(strstr(buf, "suggests: Tidy these files?"), "SW9 open, it reads the text: %s", buf);
    CHECK(zui_swim_hit(&A, bub.x + 5, bub.y + 5), "SW1 the open bubble takes taps");
    CHECK(bub.x + bub.w <= x - 2 * (int32_t) ZUI_SWIM_R || bub.x >= x + 2 * (int32_t) ZUI_SWIM_R,
          "the bubble does not cover the chiglet");
    cmp_decision_t d = dec(CMP_PROCEED);
    CHECK(!zui_swim_offer(&A, (zui_rect_t){400, 400, 50, 50}, &d, DIG, "other"),
          "SW2 an open offer is never replaced");
    CHECK(zui_swim_tap(&A, bub.x + 3, bub.y + 3, got) == ZUI_SWIM_NONE &&
              zui_swim_state(&A) == ZUI_SWIM_OPEN,
          "a tap on the bubble's face does nothing");
    CHECK(memcmp(got, (uint8_t[CMP_HASH]){0}, CMP_HASH) == 0, "SW3 nothing handed back yet");
    zui_rect_t ok = zui_swim_button(&A, 0);
    CHECK(zui_swim_tap(&A, ok.x + 2, ok.y + 2, got) == ZUI_SWIM_ACCEPTED &&
              memcmp(got, DIG, CMP_HASH) == 0,
          "SW3 Accept hands back the offer's digest");
    CHECK(zui_swim_state(&A) == ZUI_SWIM_LEAVING && !zui_swim_hit(&A, x, y),
          "it leaves after Accept and takes no more taps");
    run(&A, 3000, 3300);
    CHECK(zui_swim_state(&A) == ZUI_SWIM_HIDDEN, "and is gone");
    /* close by tapping the body again */
    arrive(&A, t, "x");
    zui_swim_tap(&A, px(&A), py(&A), got);
    CHECK(zui_swim_tap(&A, px(&A), py(&A), got) == ZUI_SWIM_CLOSED &&
              zui_swim_state(&A) == ZUI_SWIM_WAITING,
          "a second tap on the body closes the bubble");
    /* SW9 open without a pointer */
    CHECK(zui_swim_open(&A) == ZUI_SWIM_OPENED && zui_swim_state(&A) == ZUI_SWIM_OPEN &&
              zui_swim_open(&A) == ZUI_SWIM_NONE,
          "SW9 zui_swim_open opens it once");
    /* SW4 dismiss and snooze */
    zui_rect_t no = zui_swim_button(&A, 1);
    uint32_t at = A.now;
    CHECK(zui_swim_tap(&A, no.x + 2, no.y + 2, got) == ZUI_SWIM_DISMISSED, "SW4 Not now");
    run(&A, at, at + 300);
    CHECK(!zui_swim_offer(&A, t, &d, DIG, "again"), "SW4 snoozed");
    run(&A, at + 300, at + ZUI_SWIM_SNOOZE_MS - 1);
    CHECK(!zui_swim_offer(&A, t, &d, DIG, "again"), "SW4 still snoozed 1 ms before the end");
    run(&A, at + ZUI_SWIM_SNOOZE_MS - 1, at + ZUI_SWIM_SNOOZE_MS);
    CHECK(zui_swim_offer(&A, t, &d, DIG, "again"), "SW4 offers again after the snooze");
    /* withdraw */
    arrive(&A, t, "x");
    zui_swim_withdraw(&A);
    CHECK(zui_swim_state(&A) == ZUI_SWIM_LEAVING, "a withdrawn offer leaves");
}

static void test_pin_away(void)
{
    zui_motion_reset();
    zui_rect_t t = {100, 200, 200, 150};
    arrive(&A, t, "x");
    zui_swim_open(&A);
    zui_rect_t pin = zui_swim_button(&A, 2);
    uint8_t got[CMP_HASH];
    CHECK(zui_swim_tap(&A, pin.x + 2, pin.y + 2, got) == ZUI_SWIM_PIN, "SW4 Pin");
    run(&A, 3000, 3000 + ZUI_SWIM_TRAVEL_MS + 50);
    int32_t e = 2 * (int32_t) ZUI_SWIM_R;
    CHECK(zui_swim_state(&A) == ZUI_SWIM_PINNED && px(&A) == W - e - ZUI_SWIM_GAP &&
              py(&A) == H - e - ZUI_SWIM_GAP,
          "SW4 pinned, it rests in the corner (%d %d)", px(&A), py(&A));
    ground(PX);
    zui_surface_t s;
    zui_surface_init(&s, PX, W, H, W);
    zui_swim_draw(&A, &s, NULL, NULL, 0);
    int32_t dx = px(&A) + (int32_t) ZUI_SWIM_R - 4, dy = py(&A) - (int32_t) ZUI_SWIM_R + 4;
    CHECK(PX[dy * W + dx] == ZUI_GOLD, "SW4 the gold dot: an offer waits (%06x)", PX[dy * W + dx]);
    cmp_decision_t d = dec(CMP_PROCEED);
    CHECK(zui_swim_offer(&A, (zui_rect_t){300, 300, 60, 60}, &d, DIG, "y") &&
              zui_swim_state(&A) == ZUI_SWIM_PINNED,
          "SW4 pinned, a new offer does not make it swim");
    CHECK(zui_swim_tap(&A, px(&A), py(&A), got) == ZUI_SWIM_OPENED, "pinned, a tap opens it");
    zui_swim_tap(&A, px(&A), py(&A), got);
    CHECK(zui_swim_state(&A) == ZUI_SWIM_PINNED, "closing returns it to the corner");
    zui_swim_pin(&A, false);
    CHECK(zui_swim_state(&A) == ZUI_SWIM_ARRIVING, "SW4 unpinned, it swims to its offer");
    /* away */
    zui_swim_away(&A, true);
    CHECK(zui_swim_state(&A) == ZUI_SWIM_LEAVING &&
              !zui_swim_offer(&A, (zui_rect_t){300, 300, 60, 60}, &d, DIG, "y"),
          "SW4 away: it leaves and takes no offers");
    char buf[64];
    zui_swim_describe(&A, buf, sizeof buf);
    CHECK(strcmp(buf, "Your chiglet is away.") == 0, "SW9 away");
    zui_swim_away(&A, false);
    CHECK(zui_swim_offer(&A, (zui_rect_t){300, 300, 60, 60}, &d, DIG, "y"), "back from away");
    /* pinned with nothing to offer still rests in its corner */
    fresh(&A);
    zui_swim_pin(&A, true);
    run(&A, 1000, 1000 + ZUI_SWIM_TRAVEL_MS + 300);
    CHECK(zui_swim_state(&A) == ZUI_SWIM_PINNED && zui_swim_alpha(&A) == 255, "pinned, empty");
    zui_swim_describe(&A, buf, sizeof buf);
    CHECK(strcmp(buf, "Your chiglet is pinned in the corner.") == 0, "SW9 pinned");
    CHECK(zui_swim_tap(&A, px(&A), py(&A), got) == ZUI_SWIM_NONE &&
              zui_swim_state(&A) == ZUI_SWIM_PINNED,
          "SW3 with nothing to offer, a tap opens nothing (it never speaks unasked)");
}

static void test_draw(void)
{
    zui_motion_reset();
    zui_surface_t s;
    zui_surface_init(&s, PX, W, H, W);
    fresh(&A);
    ground(PX);
    zui_swim_draw(&A, &s, NULL, NULL, 0);
    bool clean = true;
    for (int i = 0; i < W * H; i++)
        if (PX[i] != ZUI_VOID) clean = false;
    CHECK(clean, "SW1 hidden, the layer is clear");
    zui_rect_t t = {100, 200, 200, 150};
    zui_motion_set_flag(ZUI_PREF_REDUCE_MOTION, true); /* still, so the frames compare */
    arrive(&A, t, "x");
    ground(PX);
    zui_swim_draw(&A, &s, NULL, NULL, 0);
    /* the same chiglet drawn straight onto the ground */
    ground(PX2);
    zui_surface_t s2;
    zui_surface_init(&s2, PX2, W, H, W);
    zui_chiglet_draw(&s2, &LOOK, px(&A), py(&A), (int32_t) ZUI_SWIM_R, 0);
    int worst = 0, changed = 0, far = 0;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            uint32_t a = PX[y * W + x], b = PX2[y * W + x];
            if (a != ZUI_VOID) changed++;
            int dxp = x - px(&A), dyp = y - py(&A);
            if (a != ZUI_VOID && (dxp * dxp + dyp * dyp > 2 * 56 * 56)) far++;
            for (int sh = 0; sh < 24; sh += 8) {
                int d = (int) ((a >> sh) & 255u) - (int) ((b >> sh) & 255u);
                if (d < 0) d = -d;
                if (d > worst) worst = d;
            }
        }
    CHECK(changed > 500, "the chiglet is drawn (%d px)", changed);
    CHECK(far == 0, "SW1 nothing outside its own drawing changes");
    zui_rect_t ex = zui_swim_extent(&A);
    bool in_extent = true;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            if (PX[y * W + x] != ZUI_VOID &&
                !(x >= ex.x && y >= ex.y && x < ex.x + ex.w && y < ex.y + ex.h))
                in_extent = false;
    CHECK(in_extent && ex.w == ZUI_SWIM_BOX, "SW1 every painted pixel is inside the extent");
    CHECK(worst <= 3, "the layer gives the same picture as drawing in place (worst %d)", worst);
    /* SW8: the same state draws the same pixels */
    memcpy(PX2, PX, sizeof PX);
    ground(PX);
    zui_swim_draw(&A, &s, NULL, NULL, 0);
    CHECK(memcmp(PX, PX2, sizeof PX) == 0, "SW8 same state, same pixels");
    /* open: the bubble shows (outline in gold) */
    zui_swim_open(&A);
    ground(PX);
    zui_swim_draw(&A, &s, NULL, NULL, 0);
    zui_rect_t bub = zui_swim_bubble(&A);
    CHECK(PX[(bub.y + bub.h / 2) * W + bub.x] != ZUI_VOID, "the open bubble is drawn");
    zui_rect_t ex2 = zui_swim_extent(&A);
    CHECK(ex2.x <= bub.x && ex2.x + ex2.w >= bub.x + bub.w && ex2.w > ZUI_SWIM_BOX,
          "SW1 open, the extent takes in the bubble");
    fresh(&B);
    CHECK(zui_swim_extent(&B).w == 0, "SW1 hidden, it paints nothing");
    /* depth: behind the glass shifts it in parallax; at the glass it does not */
    zui_depth_t dp;
    zui_depth_init(&dp, ZUI_DEPTH_PARALLAX, 40);
    dp.head_x = 200;
    zui_swim_tap(&A, px(&A), py(&A), NULL);
    ground(PX);
    zui_swim_draw(&A, &s, NULL, NULL, 0);
    memcpy(PX2, PX, sizeof PX);
    ground(PX);
    zui_swim_draw(&A, &s, NULL, &dp, 0);
    CHECK(memcmp(PX, PX2, sizeof PX) == 0, "SW6 waiting at the glass, depth moves nothing");
    zui_motion_reset();
}

/* the bubble's words: wrapped onto lines, the last cut, never past the bubble */
static void test_words(void)
{
    static zui_glyph_cache_t cache;
    static ttf_font_t font;
    CHECK(ttf_parse(&font, ZUI_TTF, sizeof ZUI_TTF), "fixture font");
    zui_glyph_cache_init(&cache);
    zui_text_t t = {&font, &cache, 12, false};
    zui_motion_reset();
    zui_motion_set_flag(ZUI_PREF_REDUCE_MOTION, true);
    zui_surface_t s;
    zui_surface_init(&s, PX, W, H, W);
    /* the fixture font draws only "A" (every glyph is 600/1000 em wide) */
    const char *texts[3] = {"AAAA", "AAAAA AAAAA AAAAA AAAAA AAAAA AAAAA AAAAA AAAAA",
                            "AAAAA AAAAA AAAAA AAAAA AAAAA AAAAA AAAAA AAAAA AAAAA AAAAA AAAAA "
                            "AAAAA AAAAA AAAAA AAAAA AAAAA AAAAA AAAAA AAAAA AAAAA"};
    int bands[3];
    for (int k = 0; k < 3; k++) {
        arrive(&A, (zui_rect_t){400, 200, 200, 150}, texts[k]);
        zui_swim_open(&A);
        ground(PX);
        zui_swim_draw(&A, &s, &t, NULL, 0);
        zui_rect_t b = zui_swim_bubble(&A);
        int lh = zui_text_ascent(&t) - zui_text_descent(&t) + 2, first = b.y + 10, n = 0;
        bool outside = false;
        for (int line = 0; line < 4; line++) {
            bool ink = false;
            for (int y = first + line * lh; y < first + (line + 1) * lh - 2; y++)
                for (int x = b.x + 12; x < b.x + b.w - 12; x++)
                    if (PX[y * W + x] == ZUI_TEXT) ink = true;
            n += ink;
        }
        for (int y = b.y + 10; y < b.y + b.h - ZUI_SWIM_BUTTON_H - 8; y++)
            for (int x = b.x + b.w - 11; x < b.x + b.w - 1; x++)
                if (PX[y * W + x] == ZUI_TEXT) outside = true;
        bands[k] = n;
        CHECK(!outside, "the words stay inside the bubble (%d)", k);
        zui_rect_t ok = zui_swim_button(&A, 0);
        bool label = false;
        for (int y = ok.y + 2; y < ok.y + ok.h - 2; y++)
            for (int x = ok.x + 2; x < ok.x + ok.w - 2; x++)
                if (PX[y * W + x] == ZUI_GOLD) label = true;
        CHECK(label, "Accept is labelled in gold (its A)");
    }
    CHECK(bands[0] == 1 && bands[1] == 2 && bands[2] == 3,
          "wrapped onto 1, 2+ and 3 lines (%d %d %d)", bands[0], bands[1], bands[2]);
    zui_motion_reset();
}

/* exact edges of SW7, the work area too small to hold it, entries, guards */
static void test_exact(void)
{
    zui_motion_reset();
    zui_motion_set_flag(ZUI_PREF_REDUCE_MOTION, true); /* CUT: it is simply there */
    cmp_decision_t d = dec(CMP_PROCEED);
    int32_t e = 2 * (int32_t) ZUI_SWIM_R, g = ZUI_SWIM_GAP;
    struct {
        zui_rect_t t;
        int32_t x, y;
        const char *what;
    } C[] = {
        {{100, 300, W - 100 - g - 2 * e, 50}, W - e, 300 + e, "right, exactly room"},
        {{g + 2 * e, 300, W - g - 2 * e - 1, 50}, e, 300 + e, "left, exactly room"},
        {{g + 2 * e - 1, 300, W - g - 2 * e + 1, 50}, W - e, 300 - g - e, "above: no room left"},
        {{0, g + 2 * e, W, 50}, W - e, e, "above, exactly room"},
        {{0, g + 2 * e - 1, W, 50}, W - e, g + 2 * e - 1 + e, "inside: no room above"},
    };
    for (unsigned i = 0; i < sizeof C / sizeof C[0]; i++) {
        fresh(&A);
        CHECK(zui_swim_offer(&A, C[i].t, &d, DIG, "x"), "offered %s", C[i].what);
        zui_swim_tick(&A, 1016);
        CHECK(px(&A) == C[i].x && py(&A) == C[i].y, "SW7 %s: (%d %d) want (%d %d)", C[i].what,
              px(&A), py(&A), C[i].x, C[i].y);
    }
    /* a work area narrower than the chiglet: it sits at the area's centre */
    zui_swim_init(&A, (zui_rect_t){10, 20, 2 * e - 1, 2 * e - 2}, &LOOK);
    zui_swim_tick(&A, 1000);
    zui_swim_offer(&A, (zui_rect_t){10, 20, 5, 5}, &d, DIG, "x");
    zui_swim_tick(&A, 1016);
    CHECK(px(&A) == 10 + (2 * e - 1) / 2 && py(&A) == 20 + (2 * e - 2) / 2,
          "SW7 a tiny work area: the centre (%d %d)", px(&A), py(&A));
    zui_swim_init(&A, (zui_rect_t){10, 20, 2 * e, 2 * e}, &LOOK);
    zui_swim_tick(&A, 1000);
    zui_swim_offer(&A, (zui_rect_t){10, 20, 5, 5}, &d, DIG, "x");
    zui_swim_tick(&A, 1016);
    CHECK(px(&A) == 10 + e && py(&A) == 20 + e, "SW7 a work area just its size (%d %d)", px(&A),
          py(&A));
    zui_motion_reset();
    /* entries: the left edge, and a tie goes to the left, then the top */
    struct {
        zui_rect_t t;
        bool on_x;
        int32_t at;
        const char *what;
    } E[] = {
        {{30, 250, 20, 20}, true, -e, "the left edge"},
        {{W - 50 - g - 2 * e - 20, 250, 20, 20}, true, W + e, "the right edge"},
        {{340, 100 - e, 20, 20}, false, -e, "the top edge"},
        {{340, H - 200 - e, 20, 20}, false, H + e, "the bottom edge"},
    };
    for (unsigned i = 0; i < sizeof E / sizeof E[0]; i++) {
        fresh(&A);
        zui_swim_offer(&A, E[i].t, &d, DIG, "x");
        int32_t x = px(&A), y = py(&A);
        CHECK(E[i].on_x ? x == E[i].at : y == E[i].at, "SW5 from %s (%d %d)", E[i].what, x, y);
    }
    /* the work area moves: a waiting chiglet goes to its new place */
    arrive(&A, (zui_rect_t){150, 100, 50, 100}, "x");
    zui_swim_set_work(&A, (zui_rect_t){0, 0, 400, 300});
    zui_motion_set_flag(ZUI_PREF_REDUCE_MOTION, true);
    zui_swim_tick(&A, 3000);
    CHECK(px(&A) == 150 + 50 + g + e && py(&A) == 100 + e, "a new work area keeps its place");
    zui_swim_set_work(&A, (zui_rect_t){0, 0, 300, 300});
    zui_swim_tick(&A, 3016);
    CHECK(px(&A) == 150 - g - e, "a narrower work area moves it to the left (%d)", px(&A));
    zui_swim_pin(&A, true);
    run(&A, 3016, 6000);
    zui_swim_set_work(&A, (zui_rect_t){0, 0, 500, 400});
    zui_swim_tick(&A, 6016);
    CHECK(px(&A) == 500 - e - g && py(&A) == 400 - e - g, "pinned, it follows the corner (%d %d)",
          px(&A), py(&A));
    fresh(&B);
    zui_swim_set_work(&B, (zui_rect_t){0, 0, 500, 400});
    zui_swim_tick(&B, 1016);
    CHECK(zui_swim_state(&B) == ZUI_SWIM_HIDDEN && px(&B) == 0,
          "hidden, a new work area moves nothing");
    zui_motion_reset();
    /* windows: past ZUI_SWIM_WINDOWS are not taken, NULL is none */
    static zui_rect_t many[ZUI_SWIM_WINDOWS + 1];
    for (unsigned i = 0; i < ZUI_SWIM_WINDOWS; i++) many[i] = (zui_rect_t){0, 0, 1, 1};
    many[ZUI_SWIM_WINDOWS] = (zui_rect_t){0, 0, W, 140}; /* the path, not the target */
    fresh(&A);
    zui_swim_set_windows(&A, many, ZUI_SWIM_WINDOWS + 1);
    zui_swim_offer(&A, (zui_rect_t){100, 100, 100, 100}, &d, DIG, "x");
    run(&A, 1000, 1500);
    CHECK(zui_swim_depth(&A) == 0 && zui_swim_alpha(&A) == 255,
          "SW6 a window past the limit is not swum through");
    zui_swim_set_windows(&A, many + 1, ZUI_SWIM_WINDOWS);
    zui_swim_tick(&A, 1516);
    CHECK(zui_swim_depth(&A) == -ZUI_SWIM_DEPTH_PX, "SW6 the 32nd window is");
    zui_swim_set_windows(&A, NULL, 5);
    zui_swim_tick(&A, 1532);
    CHECK(zui_swim_depth(&A) == 0, "SW6 no windows given, none");
    /* NULL is safe everywhere */
    zui_swim_init(NULL, WORK, &LOOK);
    zui_swim_set_look(NULL, &LOOK);
    zui_swim_set_work(NULL, WORK);
    zui_swim_set_windows(NULL, many, 2);
    zui_swim_withdraw(NULL);
    zui_swim_tick(NULL, 5);
    zui_swim_pin(NULL, true);
    zui_swim_away(NULL, true);
    zui_swim_draw(NULL, NULL, NULL, NULL, 0);
    char buf[8];
    CHECK(!zui_swim_offer(NULL, WORK, &d, DIG, "x") && !zui_swim_hit(NULL, 0, 0) &&
              zui_swim_tap(NULL, 0, 0, NULL) == ZUI_SWIM_NONE &&
              zui_swim_open(NULL) == ZUI_SWIM_NONE && zui_swim_state(NULL) == ZUI_SWIM_HIDDEN &&
              zui_swim_alpha(NULL) == 0 && zui_swim_depth(NULL) == 0 &&
              zui_swim_extent(NULL).w == 0 && zui_swim_bubble(NULL).w == 0 &&
              zui_swim_button(NULL, 0).w == 0 && zui_swim_describe(NULL, buf, sizeof buf) == 0 &&
              buf[0] == 0,
          "NULL is refused, never touched");
    fresh(&A);
    zui_swim_init(&A, WORK, NULL);
    zui_swim_set_look(&A, NULL);
    CHECK(A.look.mood == 0 && A.look.genes[0] == 0, "no look given, the empty look");
    zui_swim_set_look(&A, &LOOK);
    CHECK(memcmp(&A.look, &LOOK, sizeof LOOK) == 0, "a look given is taken");
}

static uint64_t fnv(uint64_t h, int64_t v)
{
    for (int i = 0; i < 8; i++) {
        h ^= (uint8_t) (v >> (8 * i));
        h *= 1099511628211ull;
    }
    return h;
}

/* SW8: the same calls give the same positions, alphas and pixels, frame by
 * frame. The digests pin today's motion and drawing exactly: a change to
 * either must change them here on purpose. */
static void test_golden(void)
{
    zui_motion_reset();
    cmp_decision_t d = dec(CMP_ASK);
    zui_rect_t wins[2] = {{250, 150, 300, 250}, {600, 400, 150, 150}};
    uint64_t h = 1469598103934665603ull;
    fresh(&A);
    zui_swim_set_windows(&A, wins, 2);
    zui_swim_offer(&A, (zui_rect_t){620, 420, 60, 60}, &d, DIG, "x"); /* in from the bottom */
    for (uint32_t t = 1000; t < 1000 + ZUI_SWIM_TRAVEL_MS + ZUI_SWIM_BOB_MS; t += 7) {
        zui_swim_tick(&A, t);
        h = fnv(h, A.x);
        h = fnv(h, A.y);
        h = fnv(h, zui_swim_alpha(&A));
        h = fnv(h, zui_swim_depth(&A));
        h = fnv(h, zui_swim_state(&A));
    }
    /* a second offer far away: the swim crosses the first window */
    zui_swim_offer(&A, (zui_rect_t){20, 20, 40, 40}, &d, DIG, "x");
    for (uint32_t t = 6400; t < 6400 + ZUI_SWIM_TRAVEL_MS + 200; t += 5) {
        zui_swim_tick(&A, t);
        h = fnv(h, A.x);
        h = fnv(h, A.y);
        h = fnv(h, zui_swim_alpha(&A));
        h = fnv(h, zui_swim_depth(&A));
    }
    /* leaving */
    zui_swim_withdraw(&A);
    for (uint32_t t = 8000; t < 8300; t += 3) {
        zui_swim_tick(&A, t);
        h = fnv(h, zui_swim_alpha(&A));
        h = fnv(h, zui_swim_state(&A));
    }
    printf("  motion digest %016llx\n", (unsigned long long) h);
    CHECK(h == 0xaf1233b5deab0522ull, "SW8 the motion digest is unchanged");
    /* pixels: mid-swim through the glass with depth, waiting, open, pinned */
    zui_surface_t s;
    zui_surface_init(&s, PX, W, H, W);
    zui_depth_t dp;
    zui_depth_init(&dp, ZUI_DEPTH_PARALLAX, 40);
    dp.head_x = 200;
    uint64_t p = 1469598103934665603ull;
    fresh(&A);
    zui_swim_set_windows(&A, wins, 2);
    zui_swim_offer(&A, (zui_rect_t){620, 420, 60, 60}, &d, DIG, "x");
    uint32_t stops[4] = {1300, 1700, 2500, 5000};
    for (int k = 0; k < 4; k++) {
        run(&A, k ? stops[k - 1] : 1000, stops[k]);
        if (k == 3) zui_swim_open(&A);
        ground(PX);
        zui_swim_draw(&A, &s, NULL, &dp, k & 1 ? 1 : -1);
        for (int i = 0; i < W * H; i++) p = fnv(p, PX[i]);
    }
    zui_swim_pin(&A, true);
    run(&A, 5000, 7000);
    zui_swim_offer(&A, (zui_rect_t){100, 100, 60, 60}, &d, DIG, "x");
    ground(PX);
    zui_swim_draw(&A, &s, NULL, NULL, 0);
    for (int i = 0; i < W * H; i++) p = fnv(p, PX[i]);
    printf("  pixel digest %016llx\n", (unsigned long long) p);
    CHECK(p == 0x03740105c70eb6a4ull, "SW8 the pixel digest is unchanged");
    zui_motion_reset();
}

/* the bubble's place, the extent's exact union, the buttons, describe's bounds */
static void test_layout(void)
{
    zui_motion_reset();
    zui_motion_set_flag(ZUI_PREF_REDUCE_MOTION, true);
    int32_t e = 2 * (int32_t) ZUI_SWIM_R, bw = ZUI_SWIM_BUBBLE_W, bh = ZUI_SWIM_BUBBLE_H;
    /* waiting at (cx, cy) = (400 + 50 + 8 + 48, 300 + 48): the bubble goes left of the body */
    arrive(&A, (zui_rect_t){400, 300, 50, 50}, "x");
    int32_t cx = px(&A), cy = py(&A);
    zui_swim_open(&A);
    zui_rect_t b = zui_swim_bubble(&A);
    CHECK(b.x == cx - e - bw && b.y == cy - bh / 2 && b.w == bw && b.h == bh,
          "the bubble sits left of the body (%d %d %d %d)", b.x, b.y, b.w, b.h);
    zui_rect_t x = zui_swim_extent(&A);
    CHECK(x.x == b.x && x.y == cy - ZUI_SWIM_BOX / 2 && x.w == cx + ZUI_SWIM_BOX / 2 - b.x &&
              x.h == ZUI_SWIM_BOX,
          "SW1 the extent is exactly body and bubble (%d %d %d %d)", x.x, x.y, x.w, x.h);
    int32_t btw = (bw - 2 * 8 - 2 * 6) / 3;
    for (uint32_t i = 0; i < 3; i++) {
        zui_rect_t r = zui_swim_button(&A, i);
        CHECK(r.x == b.x + 8 + (int32_t) i * (btw + 6) && r.y == b.y + bh - 8 - ZUI_SWIM_BUTTON_H &&
                  r.w == btw && r.h == ZUI_SWIM_BUTTON_H,
              "button %u in its place", i);
    }
    CHECK(zui_swim_button(&A, 3).w == 0, "there are three buttons");
    /* near the left edge: right of the body; near the top: kept inside */
    arrive(&A, (zui_rect_t){10, 0, 40, 30}, "x");
    cx = px(&A);
    zui_swim_open(&A);
    b = zui_swim_bubble(&A);
    CHECK(b.x == cx + e && b.y == 0, "by the left edge, right of the body, inside the top (%d %d)",
          b.x, b.y);
    x = zui_swim_extent(&A);
    CHECK(x.x == cx - ZUI_SWIM_BOX / 2 && x.y == py(&A) - ZUI_SWIM_BOX / 2 &&
              x.w == b.x + bw - x.x && x.h == ZUI_SWIM_BOX,
          "SW1 the extent reaches the bubble's far edge (%d %d %d %d)", x.x, x.y, x.w, x.h);
    /* a work area narrower than body and bubble: the bubble is kept inside it */
    zui_swim_init(&A, (zui_rect_t){0, 0, 300, 400}, &LOOK);
    zui_swim_tick(&A, 1000);
    cmp_decision_t d = dec(CMP_PROCEED);
    zui_swim_offer(&A, (zui_rect_t){100, 100, 40, 40}, &d, DIG, "x");
    zui_swim_tick(&A, 1016);
    zui_swim_open(&A);
    b = zui_swim_bubble(&A);
    CHECK(b.x == 300 - bw, "the bubble stops at the right edge (%d)", b.x);
    zui_swim_init(&A, (zui_rect_t){50, 0, 200, 400}, &LOOK);
    zui_swim_tick(&A, 1000);
    zui_swim_offer(&A, (zui_rect_t){60, 100, 20, 20}, &d, DIG, "x");
    zui_swim_tick(&A, 1016);
    zui_swim_open(&A);
    CHECK(zui_swim_bubble(&A).x == 50, "wider than the work area, it starts at its left edge");
    /* waiting: the extent is the body box alone */
    arrive(&A, (zui_rect_t){400, 300, 50, 50}, "x");
    x = zui_swim_extent(&A);
    CHECK(x.x == px(&A) - ZUI_SWIM_BOX / 2 && x.y == py(&A) - ZUI_SWIM_BOX / 2 &&
              x.w == ZUI_SWIM_BOX && x.h == ZUI_SWIM_BOX,
          "SW1 waiting, the extent is the body box");
    /* describe: the length it needs, cut to the buffer, always terminated */
    static const char want[] = "Your chiglet has a suggestion. Open it to read it.";
    char buf[64];
    memset(buf, 'z', sizeof buf);
    size_t n = zui_swim_describe(&A, buf, 10);
    CHECK(n == sizeof want - 1 && memcmp(buf, want, 9) == 0 && buf[9] == 0 && buf[10] == 'z',
          "SW9 cut to the buffer, its full length returned (%zu)", n);
    memset(buf, 'z', sizeof buf);
    n = zui_swim_describe(&A, buf, sizeof want);
    CHECK(n == sizeof want - 1 && strcmp(buf, want) == 0, "SW9 exactly room");
    memset(buf, 'z', sizeof buf);
    CHECK(zui_swim_describe(&A, buf, 1) == sizeof want - 1 && buf[0] == 0 && buf[1] == 'z',
          "SW9 one byte: only the terminator");
    CHECK(zui_swim_describe(&A, NULL, 0) == sizeof want - 1, "SW9 no buffer: the length");
    CHECK(zui_swim_describe(&A, NULL, 5) == 0, "SW9 a size with no buffer is refused");
    memset(buf, 'z', sizeof buf);
    zui_swim_describe(&A, buf, 0);
    CHECK(buf[0] == 'z', "SW9 a zero-size buffer is not written");
    fresh(&B);
    zui_swim_describe(&B, buf, sizeof buf);
    CHECK(strcmp(buf, "Your chiglet has nothing to suggest.") == 0, "SW9 nothing, not pinned");
    zui_motion_reset();
}

int main(void)
{
    test_refusals();
    test_swim();
    test_places();
    test_glass();
    test_reduced();
    test_taps();
    test_pin_away();
    test_draw();
    test_words();
    test_exact();
    test_golden();
    test_layout();
    printf("%d/%d checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
