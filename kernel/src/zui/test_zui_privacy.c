/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zui_privacy.c — sensor in-use tracking, the indicator and its log
 * (zui_privacy.h PV1..PV5). */
#include <stdio.h>
#include <string.h>

#include "zui_privacy.h"
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

static zui_privacy_t P;

#define CAM ZUI_SENSOR_CAMERA
#define MIC ZUI_SENSOR_MIC
#define SCR ZUI_SENSOR_SCREEN
#define LOC ZUI_SENSOR_LOCATION

static void test_count(void)
{
    zui_privacy_init(&P);
    zui_priv_state_t st = zui_privacy_state(&P, 0);
    CHECK(st.live == 0 && st.lit == 0, "nothing at first");
    CHECK(P.linger_ms == ZUI_PRIV_LINGER_MS, "the default linger");

    /* PV1: counted, nested */
    CHECK(zui_privacy_begin(&P, "notes", CAM, 1000) == 1, "first begin");
    CHECK(zui_privacy_begin(&P, "notes", CAM, 1100) == 2, "nested begin");
    CHECK(zui_privacy_refs(&P, "notes", CAM) == 2, "two uses");
    CHECK(zui_privacy_refs(&P, "notes", MIC) == 0 && zui_privacy_refs(&P, "chat", CAM) == 0,
          "per app and per sensor");
    st = zui_privacy_state(&P, 1200);
    CHECK(st.live == ZUI_SENSOR_BIT(CAM) && st.lit == st.live && st.apps[CAM] == 1,
          "camera live, one app");
    CHECK(zui_privacy_end(&P, "notes", CAM, 1300), "one end");
    st = zui_privacy_state(&P, 1300);
    CHECK(st.live == ZUI_SENSOR_BIT(CAM), "still live: one use left");
    CHECK(zui_privacy_end(&P, "notes", CAM, 2000), "the last end");
    st = zui_privacy_state(&P, 2000);
    CHECK(st.live == 0 && st.lit == ZUI_SENSOR_BIT(CAM), "PV2: not live, still lit");
    CHECK(!zui_privacy_end(&P, "notes", CAM, 2001), "an end with no begin is refused");
    CHECK(zui_privacy_refs(&P, "notes", CAM) == 0, "never below zero");

    /* PV2: linger */
    st = zui_privacy_state(&P, 2000 + ZUI_PRIV_LINGER_MS - 1);
    CHECK(st.lit == ZUI_SENSOR_BIT(CAM), "lit until linger_ms has passed");
    st = zui_privacy_state(&P, 2000 + ZUI_PRIV_LINGER_MS);
    CHECK(st.lit == 0, "then dark");
    P.linger_ms = 500;
    CHECK(zui_privacy_state(&P, 2499).lit && !zui_privacy_state(&P, 2500).lit,
          "linger is settable");
    P.linger_ms = 0;
    CHECK(!zui_privacy_state(&P, 2000).lit, "linger 0: dark at once");
    P.linger_ms = ZUI_PRIV_LINGER_MS;

    /* several apps on one sensor */
    CHECK(zui_privacy_begin(&P, "chat", MIC, 3000) == 1, "chat mic");
    CHECK(zui_privacy_begin(&P, "notes", MIC, 3001) == 1, "notes mic");
    CHECK(zui_privacy_begin(&P, "maps", LOC, 3002) == 1, "maps location");
    st = zui_privacy_state(&P, 3003);
    CHECK(st.live == (ZUI_SENSOR_BIT(MIC) | ZUI_SENSOR_BIT(LOC)) && st.apps[MIC] == 2 &&
              st.apps[LOC] == 1 && st.apps[CAM] == 0,
          "mic by two, location by one");
    const char *who[4];
    CHECK(zui_privacy_users(&P, MIC, who, 4) == 2 && strcmp(who[0], "chat") == 0 &&
              strcmp(who[1], "notes") == 0,
          "users in the order they began");
    CHECK(zui_privacy_users(&P, MIC, who, 1) == 2 && strcmp(who[0], "chat") == 0,
          "cap: the count, the first written");
    CHECK(zui_privacy_users(&P, MIC, 0, 0) == 2, "count only");
    CHECK(zui_privacy_users(&P, SCR, who, 4) == 0, "no one captures the screen");
    CHECK(zui_privacy_users(&P, ZUI_SENSOR_COUNT, who, 4) == 0, "a bad sensor");
    CHECK(zui_privacy_end(&P, "chat", MIC, 3500), "chat stops");
    st = zui_privacy_state(&P, 3500);
    CHECK((st.live & ZUI_SENSOR_BIT(MIC)) && st.apps[MIC] == 1, "notes keeps it live");
    CHECK(zui_privacy_users(&P, MIC, who, 4) == 1 && strcmp(who[0], "notes") == 0, "only notes");

    /* PV1: an app that goes away */
    CHECK(zui_privacy_begin(&P, "notes", CAM, 3600) == 1 &&
              zui_privacy_begin(&P, "notes", CAM, 3601) == 2,
          "notes camera twice");
    CHECK(zui_privacy_end_app(&P, "notes", 4000) == 2, "notes closes: camera and mic ended");
    st = zui_privacy_state(&P, 4000);
    CHECK(st.live == ZUI_SENSOR_BIT(LOC), "only maps' location is live");
    CHECK((st.lit & ZUI_SENSOR_BIT(CAM)) && (st.lit & ZUI_SENSOR_BIT(MIC)), "both still lit");
    CHECK(zui_privacy_end_app(&P, "notes", 4001) == 0, "nothing left to end");
    CHECK(zui_privacy_end_app(&P, "maps", 4100) == 1, "maps closes");

    /* bad arguments */
    CHECK(zui_privacy_begin(&P, 0, CAM, 1) == 0 && zui_privacy_begin(&P, "", CAM, 1) == 0 &&
              zui_privacy_begin(&P, "x", ZUI_SENSOR_COUNT, 1) == 0 &&
              zui_privacy_begin(0, "x", CAM, 1) == 0,
          "begin refuses bad arguments");
    CHECK(!zui_privacy_end(&P, 0, CAM, 1) && !zui_privacy_end(&P, "x", (zui_sensor_t) 9, 1) &&
              !zui_privacy_end(0, "x", CAM, 1),
          "end too");
    CHECK(zui_privacy_end_app(&P, 0, 1) == 0 && zui_privacy_end_app(0, "x", 1) == 0, "end_app too");
    CHECK(zui_privacy_state(0, 5).lit == 0, "NULL state");
}

static void test_clock(void)
{
    /* PV5: a clock that steps back cannot shorten the light */
    zui_privacy_init(&P);
    zui_privacy_begin(&P, "cam", CAM, 10000);
    zui_privacy_end(&P, "cam", CAM, 12000);
    zui_privacy_begin(&P, "x", LOC, 500); /* the clock stepped back */
    const zui_priv_log_t *L = zui_privacy_log(&P, 0);
    CHECK(L && L->start == 12000, "a begin at an earlier time is taken as the latest (%llu)",
          L ? (unsigned long long) L->start : 0ull);
    zui_priv_state_t st = zui_privacy_state(&P, 100);
    CHECK(st.lit & ZUI_SENSOR_BIT(CAM), "asking at an earlier time still shows the light");
    st = zui_privacy_state(&P, 12000 + ZUI_PRIV_LINGER_MS);
    CHECK(!(st.lit & ZUI_SENSOR_BIT(CAM)), "and it goes out on time");
    /* a use that starts at time 0 */
    zui_privacy_init(&P);
    zui_privacy_begin(&P, "a", SCR, 0);
    zui_privacy_end(&P, "a", SCR, 0);
    L = zui_privacy_log(&P, 0);
    CHECK(L && !L->live && L->start == 0 && L->end == 0, "a use at time 0 is closed, not live");
    CHECK(zui_privacy_state(&P, 0).lit == ZUI_SENSOR_BIT(SCR), "and lit");
}

static void test_log(void)
{
    zui_privacy_init(&P);
    CHECK(zui_privacy_log_count(&P) == 0 && zui_privacy_log(&P, 0) == 0, "empty log");
    zui_privacy_begin(&P, "notes", CAM, 100);
    zui_privacy_begin(&P, "notes", CAM, 150);
    zui_privacy_begin(&P, "chat", MIC, 200);
    zui_privacy_end(&P, "notes", CAM, 300);
    CHECK(zui_privacy_log_count(&P) == 2, "one entry per app and sensor use, not per begin");
    const zui_priv_log_t *L0 = zui_privacy_log(&P, 0), *L1 = zui_privacy_log(&P, 1);
    CHECK(L0 && strcmp(L0->app, "chat") == 0 && L0->sensor == MIC && L0->live && L0->start == 200,
          "newest first: chat, live");
    CHECK(L1 && strcmp(L1->app, "notes") == 0 && L1->sensor == CAM && L1->live && L1->uses == 2,
          "notes, still live after one of two ends, two uses");
    zui_privacy_end(&P, "notes", CAM, 400);
    CHECK(!L1->live && L1->end == 400 && L1->start == 100, "closed at the last end");
    CHECK(L1->seq < L0->seq, "seq in the order uses began");
    /* a new use after the end is a new entry */
    zui_privacy_begin(&P, "notes", CAM, 500);
    CHECK(zui_privacy_log_count(&P) == 3 && zui_privacy_log(&P, 0)->start == 500 &&
              zui_privacy_log(&P, 0)->live,
          "a later use, a new entry");
    CHECK(zui_privacy_log(&P, 3) == 0 && zui_privacy_log(0, 0) == 0 &&
              zui_privacy_log_count(0) == 0,
          "past the end");

    /* PV3: the ring keeps the newest, counts the dropped */
    zui_privacy_init(&P);
    char id[16];
    for (uint32_t i = 0; i < ZUI_PRIV_LOG + 5u; i++) {
        snprintf(id, sizeof id, "app%u", i);
        zui_privacy_begin(&P, id, LOC, 1000u + i);
        zui_privacy_end(&P, id, LOC, 1000u + i + 1u);
    }
    CHECK(zui_privacy_log_count(&P) == ZUI_PRIV_LOG && P.dropped == 5, "full: %u kept, %u dropped",
          zui_privacy_log_count(&P), P.dropped);
    CHECK(strcmp(zui_privacy_log(&P, 0)->app, "app68") == 0, "newest kept");
    CHECK(strcmp(zui_privacy_log(&P, ZUI_PRIV_LOG - 1u)->app, "app5") == 0,
          "the five oldest dropped");
    /* a live use whose entry was dropped still ends cleanly */
    zui_privacy_init(&P);
    zui_privacy_begin(&P, "long", CAM, 1);
    for (uint32_t i = 0; i < ZUI_PRIV_LOG; i++) {
        snprintf(id, sizeof id, "s%u", i);
        zui_privacy_begin(&P, id, MIC, 2u + i);
        zui_privacy_end(&P, id, MIC, 3u + i);
    }
    CHECK(P.dropped == 1, "the long use's entry was dropped");
    CHECK(zui_privacy_refs(&P, "long", CAM) == 1 &&
              (zui_privacy_state(&P, 100).live & ZUI_SENSOR_BIT(CAM)),
          "but it is still tracked and lit (PV4: nothing hides)");
    CHECK(zui_privacy_begin(&P, "long", CAM, 100) == 2, "and nests");
    CHECK(zui_privacy_end(&P, "long", CAM, 101) && zui_privacy_end(&P, "long", CAM, 102),
          "ends cleanly");
    CHECK(!(zui_privacy_state(&P, 102).live & ZUI_SENSOR_BIT(CAM)), "no longer live");
}

static void test_full(void)
{
    /* PV4: a full table refuses, it does not lose track */
    zui_privacy_init(&P);
    char id[16];
    for (uint32_t i = 0; i < ZUI_PRIV_USERS; i++) {
        snprintf(id, sizeof id, "a%u", i);
        CHECK(zui_privacy_begin(&P, id, (zui_sensor_t) (i % 4u), 10) == 1, "use %u", i);
    }
    CHECK(zui_privacy_begin(&P, "late", CAM, 11) == 0 && P.refused == 1, "the next is refused");
    CHECK(zui_privacy_begin(&P, "a0", CAM, 12) == 2, "an app already using it can still nest");
    zui_priv_state_t st = zui_privacy_state(&P, 12);
    CHECK(st.live == 0xF && st.apps[0] == ZUI_PRIV_USERS / 4u, "all four live, eight each");
    CHECK(zui_privacy_end_app(&P, "a5", 13) == 1, "one ends");
    CHECK(zui_privacy_begin(&P, "late", CAM, 14) == 1, "now there is room");

    /* long ids are cut whole, and matched as cut */
    zui_privacy_init(&P);
    char lng[80];
    memset(lng, 'x', sizeof lng);
    lng[sizeof lng - 1] = 0;
    CHECK(zui_privacy_begin(&P, lng, CAM, 1) == 1, "a long id");
    CHECK(zui_privacy_refs(&P, lng, CAM) == 1, "found by the same long id");
    CHECK(strlen(zui_privacy_log(&P, 0)->app) == ZUI_PRIV_APP_MAX - 1u, "cut to fit");
    char u8[40];
    memset(u8, 'y', sizeof u8);
    memcpy(u8 + 30, "\xC3\xA9\xC3\xA9", 4); /* the first é across byte 31 */
    u8[34] = 0;
    CHECK(zui_privacy_begin(&P, u8, MIC, 1) == 1, "a UTF-8 id");
    const char *who[2];
    CHECK(zui_privacy_users(&P, MIC, who, 2) == 1 && strlen(who[0]) == 30,
          "cut before a split character (%u)", (unsigned) strlen(who[0]));
    CHECK(zui_privacy_end_app(&P, u8, 2) == 1 && zui_privacy_end_app(&P, lng, 2) == 1,
          "ended by the same ids");
}

static void test_names(void)
{
    CHECK(strcmp(zui_sensor_name(CAM), "camera") == 0 &&
              strcmp(zui_sensor_name(MIC), "microphone") == 0 &&
              strcmp(zui_sensor_name(SCR), "screen capture") == 0 &&
              strcmp(zui_sensor_name(LOC), "location") == 0 &&
              strcmp(zui_sensor_name(ZUI_SENSOR_COUNT), "?") == 0,
          "names");
    CHECK(zui_sensor_rgb(CAM) == ZUI_OK && zui_sensor_rgb(MIC) == ZUI_INFO &&
              zui_sensor_rgb(SCR) == ZUI_ALERT && zui_sensor_rgb(LOC) == ZUI_TEXT_2,
          "status colours");
    for (uint32_t i = 0; i <= (uint32_t) ZUI_SENSOR_COUNT; i++)
        CHECK(zui_sensor_rgb((zui_sensor_t) i) != ZUI_GOLD, "never gold (%u)", i);
}

static void test_describe(void)
{
    char out[256];
    zui_privacy_init(&P);
    CHECK(zui_privacy_describe(&P, 0, out, sizeof out) == strlen("no sensors in use") &&
              strcmp(out, "no sensors in use") == 0,
          "nothing: %s", out);
    zui_privacy_begin(&P, "notes", CAM, 100);
    zui_privacy_begin(&P, "chat", CAM, 101);
    zui_privacy_begin(&P, "rec", MIC, 102);
    zui_privacy_end(&P, "rec", MIC, 200);
    uint32_t n = zui_privacy_describe(&P, 300, out, sizeof out);
    CHECK(strcmp(out, "camera in use by notes, chat; microphone used recently") == 0 &&
              n == strlen(out),
          "live and recent: %s", out);
    char tiny[8];
    n = zui_privacy_describe(&P, 300, tiny, sizeof tiny);
    CHECK(n == 7 && strcmp(tiny, "camera ") == 0, "cut to fit, terminated: '%s'", tiny);
    CHECK(zui_privacy_describe(&P, 300, out, 0) == 0, "no room");
    CHECK(zui_privacy_describe(0, 300, out, sizeof out) == 0 && out[0] == 0, "NULL");
}

#define W 200
#define H 40
static uint32_t PX[W * H];

static void test_draw(void)
{
    zui_surface_t s;
    zui_surface_init(&s, PX, W, H, W);
    zui_rect_t bar = {0, 0, W, H};
    zui_fill(&s, bar, ZUI_VOID);
    zui_privacy_init(&P);
    CHECK(zui_privacy_draw(&P, &s, bar, 10, 0) == 0, "nothing lit: nothing drawn");
    bool clean = true;
    for (int i = 0; i < W * H; i++) clean = clean && PX[i] == ZUI_VOID;
    CHECK(clean, "the bar untouched");

    zui_privacy_begin(&P, "notes", CAM, 0);
    zui_privacy_begin(&P, "rec", SCR, 0);
    zui_privacy_begin(&P, "maps", LOC, 0);
    zui_privacy_end(&P, "maps", LOC, 100);
    int32_t w = zui_privacy_draw(&P, &s, bar, 10, 200);
    /* pad 6, three dots of 10, two gaps of 5: 12 + 30 + 10 = 52 */
    CHECK(w == 52, "three dots: width %d", w);
    int32_t x0 = W - w, cy = H / 2;
    CHECK(PX[cy * W + 1] == ZUI_VOID && PX[cy * W + x0 - 2] == ZUI_VOID, "right-aligned");
    int32_t pad = 6, dot = 10, gap = 5;
    int32_t c0 = x0 + pad + dot / 2, c1 = c0 + dot + gap, c2 = c1 + dot + gap;
    CHECK(PX[cy * W + c0] == ZUI_OK, "camera dot, live: %06x", PX[cy * W + c0]);
    CHECK(PX[cy * W + c1] == ZUI_ALERT, "screen dot, live: %06x", PX[cy * W + c1]);
    uint32_t faded = PX[cy * W + c2];
    CHECK(faded != ZUI_TEXT_2 && faded != ZUI_PANEL, "location lingering: faded (%06x)", faded);
    CHECK(PX[cy * W + c0 + dot / 2 + 2] == ZUI_PANEL, "between dots: the carved pill");
    bool gold = false;
    for (int i = 0; i < W * H; i++) gold = gold || PX[i] == ZUI_GOLD;
    CHECK(!gold, "no gold anywhere");

    /* deterministic: the same state, the same pixels */
    static uint32_t again[W * H];
    zui_surface_t s2;
    zui_surface_init(&s2, again, W, H, W);
    zui_fill(&s2, bar, ZUI_VOID);
    zui_privacy_draw(&P, &s2, bar, 10, 200);
    CHECK(memcmp(PX, again, sizeof PX) == 0, "PV5: the same pixels");

    /* after the linger only the live ones */
    zui_fill(&s, bar, ZUI_VOID);
    w = zui_privacy_draw(&P, &s, bar, 10, 100 + ZUI_PRIV_LINGER_MS);
    CHECK(w == 37, "two dots: width %d", w);
    /* a rectangle too small is never overrun */
    zui_fill(&s, bar, ZUI_VOID);
    zui_rect_t small = {100, 10, 20, 8};
    w = zui_privacy_draw(&P, &s, small, 10, 200);
    CHECK(w == 20, "clamped to the rectangle (%d)", w);
    bool inside = true;
    for (int32_t y = 0; y < H; y++)
        for (int32_t x = 0; x < W; x++)
            if (PX[y * W + x] != ZUI_VOID && !(x >= 100 && x < 120 && y >= 10 && y < 18))
                inside = false;
    CHECK(inside, "nothing drawn outside it");
    /* the caller's clip is kept and respected */
    zui_fill(&s, bar, ZUI_VOID);
    zui_clip(&s, (zui_rect_t){0, 0, W - 20, H});
    zui_privacy_draw(&P, &s, bar, 10, 200);
    bool clipped = true;
    for (int32_t y = 0; y < H; y++)
        for (int32_t x = W - 20; x < W; x++) clipped = clipped && PX[y * W + x] == ZUI_VOID;
    CHECK(clipped && s.clip.w == W - 20 && s.clip.h == H, "inside the caller's clip, kept after");
    CHECK(PX[(H / 2) * W + W - 40] != ZUI_VOID, "and drawn where it may");
    zui_unclip(&s);
    CHECK(zui_privacy_draw(0, &s, bar, 10, 0) == 0 && zui_privacy_draw(&P, 0, bar, 10, 0) == 0,
          "NULL");
}

int main(void)
{
    test_count();
    test_clock();
    test_log();
    test_full();
    test_names();
    test_describe();
    test_draw();
    printf("%s test_zui_privacy: %d/%d checks\n", fails ? "[FAIL]" : "[PASS]", checks - fails,
           checks);
    return fails ? 1 : 0;
}
