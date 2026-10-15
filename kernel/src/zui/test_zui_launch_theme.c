/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zui_launch_theme.c — every app's launch screen, live (LT1..LT7). */
#include <stdio.h>
#include <string.h>

#include "zui_launch_theme.h"

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

#define W 1280
#define H 800
static uint32_t PX[W * H];
static zui_surface_t S;
static zui_wm_t WM;
static zui_launch_theme_t LT;

/* the shell: records what it was asked to draw and play */
#define MAXP 64
static struct {
    uint32_t kind, note, gain, window;
    const zxl_theme_t *theme;
} played[MAXP];
static int nplayed, icons, logos[ZUI_LT_LOGOS], prev_open, prev_close, prev_cues;
static zxl_icon_ink_t last_ink;
static char last_app[64];

static void sh_icon(void *ctx, const char *app, zxl_icon_ink_t ink, zui_surface_t *s,
                    zui_rect_t box, uint32_t lit)
{
    (void) ctx, (void) lit;
    icons++;
    last_ink = ink;
    snprintf(last_app, sizeof last_app, "%s", app);
    zui_fill(s, box, 0x123456u);
}
static void sh_logo(void *ctx, uint32_t which, zui_surface_t *s, zui_rect_t box, uint32_t lit)
{
    (void) ctx, (void) s, (void) box, (void) lit;
    if (which < ZUI_LT_LOGOS) logos[which]++;
}
static void sh_play(void *ctx, uint32_t window, const zxl_theme_t *theme, const zxl_event_t *ev)
{
    (void) ctx;
    if (nplayed < MAXP) {
        played[nplayed].kind = ev->kind;
        played[nplayed].note = ev->note;
        played[nplayed].gain = ev->gain_q8;
        played[nplayed].window = window;
        played[nplayed].theme = theme;
    }
    nplayed++;
}
static void prev_on_window(zui_wm_t *wm, uint32_t id, bool open, void *ctx)
{
    (void) wm, (void) id, (void) ctx;
    if (open)
        prev_open++;
    else
        prev_close++;
}
static void prev_cue(zui_wm_t *wm, uint32_t id, int cue, void *ctx)
{
    (void) wm, (void) id, (void) cue, (void) ctx;
    prev_cues++;
}

static const zui_lt_shell_t SHELL = {sh_icon, sh_logo, sh_play, 0};

static void reset(const zxl_prefs_t *p)
{
    zui_wm_init(&WM, W, H, false);
    WM.on_window = prev_on_window;
    WM.cue = prev_cue;
    zui_launch_theme_attach(&LT, &WM, &SHELL, p);
    nplayed = icons = prev_open = prev_close = prev_cues = 0;
    memset(logos, 0, sizeof logos);
}

static int count(uint32_t kind)
{
    int n = 0;
    for (int i = 0; i < nplayed && i < MAXP; i++) n += played[i].kind == kind;
    return n;
}

/* the app's real load, a step at a time, drawing a frame after each */
static void load_to(uint32_t id, uint32_t p, uint32_t tick)
{
    zui_wm_progress(&WM, id, p);
    zui_wm_draw(&WM, &S, tick);
}

static void test_live_cold_launch(void)
{
    reset(0);
    uint32_t id = zui_launch_theme_open(&LT, "Burrow", "Burrow", "", true, false, 0);
    CHECK(id != 0, "the window opens");
    CHECK(prev_open == 1, "LT7: the shell's own on_window still hears it");
    CHECK(nplayed == 1 && played[0].kind == ZXL_EV_BEGIN, "LT2: BEGIN as the window opens");
    CHECK(played[0].window == id && played[0].theme == zxl_theme_for("Burrow", 1),
          "Burrow's own sequence");
    CHECK(zui_launch_theme_of(&LT, id) == zxl_theme_for("Burrow", 1), "the window's theme");

    zui_wm_draw(&WM, &S, 0);
    CHECK(icons > 0 && strcmp(last_app, "Burrow") == 0, "LT1: the app's icon on top");
    CHECK(last_ink == ZXL_ICON_PALETTE, "LT6: a system app's icon held to the palette");
    for (int i = 0; i < ZUI_LT_LOGOS; i++) CHECK(logos[i] > 0, "LT1: logo %d on its tile", i);
    CHECK(nplayed == 1, "nothing loaded: no tick yet");

    /* LT3: ticks follow the real load, one per tile as it lights */
    int prev = nplayed;
    for (uint32_t p = 4; p <= 128; p += 4) {
        load_to(id, p, p);
        CHECK(nplayed - prev <= 2, "never a burst while drawing every step (p=%u)", p);
        prev = nplayed;
    }
    CHECK(count(ZXL_EV_TICK) == 5, "five tiles, five ticks (got %d)", count(ZXL_EV_TICK));
    CHECK(count(ZXL_EV_END) == 0, "LT4: no END before the app is ready");
    const zxl_theme_t *b = zxl_theme_for("Burrow", 1);
    CHECK(played[1].note == zxl_tick_note(b, 0) && played[2].note == zxl_tick_note(b, 1) &&
              played[3].note == zxl_tick_note(b, 2) && played[4].note == zxl_tick_note(b, 3),
          "the ticks climb the app's scale");
    CHECK(played[5].kind == ZXL_EV_TICK && played[5].note == zxl_tick_note(b, ZXL_TOP_STEP),
          "the centre crest plays the top step");

    for (uint32_t p = 132; p < 256; p += 8) load_to(id, p, p);
    CHECK(count(ZXL_EV_END) == 0, "releasing: still no END");
    load_to(id, 256, 300);
    CHECK(count(ZXL_EV_END) == 1 && played[nplayed - 1].kind == ZXL_EV_END,
          "LT4: END when the app is ready");
    load_to(id, 256, 301);
    CHECK(count(ZXL_EV_END) == 1, "END once");
    CHECK(prev_cues == 3, "LT7: the earlier cue hook still hears BEGIN, MIDDLE, END");

    zui_wm_close(&WM, id);
    CHECK(prev_close == 1 && zui_launch_theme_of(&LT, id) == 0, "closing frees the slot");
}

static void test_speed_is_the_load(void)
{
    /* the same app loading fast (two frames) and slow (many) plays the same
     * cues: only their spacing differs, and that is the load's */
    reset(0);
    uint32_t id = zui_launch_theme_open(&LT, "Warble", 0, 0, true, false, 0);
    load_to(id, 64, 0);
    load_to(id, 256, 1);
    int fast = nplayed;
    CHECK(count(ZXL_EV_BEGIN) == 1 && count(ZXL_EV_END) == 1, "fast: begin and end");
    CHECK(count(ZXL_EV_TICK) >= 1 && count(ZXL_EV_TICK) <= 5, "fast: the ticks it drew");
    reset(0);
    id = zui_launch_theme_open(&LT, "Warble", 0, 0, true, false, 0);
    for (uint32_t p = 1; p <= 256; p++) load_to(id, p, p);
    CHECK(count(ZXL_EV_TICK) == 5 && nplayed == 7, "slow: begin, five ticks, end");
    CHECK(fast <= nplayed, "a fast load never plays more than a slow one");
}

static void test_off_screen_and_warm(void)
{
    /* LT4: a window that loads without being drawn ends without a burst */
    reset(0);
    uint32_t id = zui_launch_theme_open(&LT, "Coffer", 0, 0, true, false, 0);
    zui_wm_progress(&WM, id, 256);
    CHECK(nplayed == 2 && played[0].kind == ZXL_EV_BEGIN && played[1].kind == ZXL_EV_END,
          "begin, then end, no ticks");

    /* LT1: warm, already loaded: only the end */
    reset(0);
    id = zui_launch_theme_open(&LT, "Coffer", 0, 0, true, true, 0);
    CHECK(id != 0, "warm opens");
    CHECK(nplayed == 1 && played[0].kind == ZXL_EV_END, "warm: END only");
    zui_wm_draw(&WM, &S, 0);
    CHECK(nplayed == 1, "warm: drawing adds nothing");
}

static void test_themes_and_ink(void)
{
    reset(0);
    /* LT1: the manifest's launch name wins; a person's app keeps its colours */
    uint32_t id = zui_launch_theme_open(&LT, "Moss", "Moss", "Murmuration", false, false, 0);
    CHECK(zui_launch_theme_of(&LT, id) == zxl_theme_for("Murmuration", 6),
          "picked the swarm's sequence");
    zui_wm_draw(&WM, &S, 0);
    CHECK(last_ink == ZXL_ICON_OWN, "LT6: a person's app keeps its own colours");
    uint32_t id2 = zui_launch_theme_open(&LT, "Moss", 0, "group5", false, false, 0);
    CHECK(zui_launch_theme_of(&LT, id2) == zxl_group_theme(5), "a group's sequence");
    uint32_t id3 = zui_launch_theme_open(&LT, "Moss", 0, "nonsense", false, false, 0);
    CHECK(zui_launch_theme_of(&LT, id3) == zxl_group_theme(1), "unknown name: group 1's");
    uint32_t id4 = zui_launch_theme_open(&LT, "Quillon", 0, "", true, false, 0);
    CHECK(zui_launch_theme_of(&LT, id4) == zxl_theme_for("Quillon", 0), "the app's own entry");

    /* LT7: a window opened straight through the manager still gets its sounds */
    nplayed = 0;
    uint32_t id5 = zui_wm_open(&WM, "Flint", "Flint", 0, 0);
    CHECK(zui_launch_theme_of(&LT, id5) == zxl_theme_for("Flint", 2), "its app's sequence");
    CHECK(nplayed == 1 && played[0].kind == ZXL_EV_BEGIN, "and its begin");
    zui_wm_progress(&WM, id5, 256);
    CHECK(nplayed == 2 && played[1].kind == ZXL_EV_END, "and its end");

    CHECK(zui_launch_theme_open(0, "x", 0, 0, false, false, 0) == 0, "NULL theme");
    CHECK(zui_launch_theme_open(&LT, 0, 0, 0, false, false, 0) == 0, "NULL app");
    CHECK(zui_launch_theme_of(0, 1) == 0 && zui_launch_theme_of(&LT, 0) == 0, "no window");
}

static void test_full_house(void)
{
    reset(0);
    uint32_t n = 0;
    while (zui_launch_theme_open(&LT, "Burrow", 0, 0, true, false, 0)) n++;
    CHECK(n == ZUI_WM_MAX, "as many windows as the manager holds (%u)", n);
    uint32_t used = 0;
    for (uint32_t i = 0; i < ZUI_WM_MAX; i++) used += LT.slot[i].used;
    CHECK(used == ZUI_WM_MAX, "a slot each, none leaked by the refused open");
}

static void test_sound_setting(void)
{
    zxl_prefs_t p = zxl_prefs_default();
    p.sounds = 0; /* Keystone > Sound > App launch sounds: off */
    reset(&p);
    uint32_t id = zui_launch_theme_open(&LT, "Burrow", 0, 0, true, false, 0);
    for (uint32_t q = 4; q <= 256; q += 4) load_to(id, q, q);
    CHECK(nplayed == 0, "LT5: sound off plays nothing");

    reset(0);
    p = zxl_prefs_default();
    p.screen_reader = 1;
    zui_launch_theme_prefs(&LT, &p);
    id = zui_launch_theme_open(&LT, "Burrow", 0, 0, true, false, 0);
    for (uint32_t q = 4; q <= 256; q += 4) load_to(id, q, q);
    CHECK(nplayed == 2 && count(ZXL_EV_TICK) == 0, "LT5: screen reader: no ticks");
    CHECK(played[0].gain == ZXL_DUCK_Q8 && played[1].gain == ZXL_DUCK_Q8, "begin, end ducked");
    zui_launch_theme_prefs(0, &p);
    zui_launch_theme_attach(0, &WM, &SHELL, 0);
}

/* LT8: the real load from fabrication drives the screen and the sounds */
static uint32_t seen_release, seen_gather, launch_draws;
static void rec_launch(zui_surface_t *s, zui_rect_t area, const zui_wm_window_t *w,
                       zui_launch_state_t st, uint32_t tick, void *ctx)
{
    (void) ctx;
    launch_draws++;
    seen_release = st.release;
    seen_gather = st.gather;
    zui_launch_draw(s, area, 256, &w->launch, st, tick);
}

static void test_fab_load(void)
{
    reset(0);
    /* LT7: a theme's launch screen set before attaching still draws */
    zui_wm_init(&WM, W, H, false);
    WM.launch_draw = rec_launch;
    zui_launch_theme_attach(&LT, &WM, &SHELL, 0);
    uint32_t id = zui_launch_theme_open(&LT, "Burrow", "Burrow", "", true, false, 0);
    CHECK(zui_launch_theme_load(&LT, id, ZUI_LT_FETCH, 0, 40) == 0, "LT8: the start's answer");
    zui_wm_draw(&WM, &S, 0);
    CHECK(launch_draws > 0 && WM.win[0].progress == 0, "LT8: nothing fetched, nothing moves");
    uint32_t last = 0;
    int ok = 1;
    for (uint32_t b = 4; b < 40; b += 4) {
        zui_launch_theme_load(&LT, id, ZUI_LT_FETCH, b, 40);
        zui_wm_draw(&WM, &S, b);
        uint32_t p = zui_wm_get(&WM, id)->progress, want = b * 256u / 40u / 2u;
        if (p != want || p < last) ok = 0;
        last = p;
    }
    CHECK(ok, "LT8: the bar is the verified blocks, never ahead (%u)", last);
    CHECK(count(ZXL_EV_TICK) >= 4 && count(ZXL_EV_END) == 0, "LT8: ticks as the tiles fill (%d)",
          count(ZXL_EV_TICK));
    zui_launch_theme_load(&LT, id, ZUI_LT_FETCH, 40, 40);
    zui_wm_draw(&WM, &S, 50);
    CHECK(count(ZXL_EV_TICK) == 5 && zui_wm_get(&WM, id)->progress == 192,
          "LT8: every block in: the fifth tick, the mark lit");
    CHECK(seen_release == ZUI_BAR_UNKNOWN, "LT8: the bar breathes while the app is built");
    for (uint32_t t = 51; t < 60; t++) zui_wm_draw(&WM, &S, t);
    CHECK(count(ZXL_EV_END) == 0, "LT8: building: no end");
    CHECK(zui_launch_theme_load(&LT, id, ZUI_LT_DONE, 40, 40) == 0, "LT8: done");
    CHECK(count(ZXL_EV_END) == 1 && played[nplayed - 1].kind == ZXL_EV_END,
          "LT8: done (the target CID matched): the end plays");
    CHECK(zui_wm_get(&WM, id)->phase != ZUI_WIN_LAUNCHING, "LT8: the doors open");
    CHECK(zui_launch_theme_load(&LT, id, ZUI_LT_DONE, 40, 40) == -1 && count(ZXL_EV_END) == 1,
          "LT8: the end once");

    /* a failed step: never the end, whatever comes after */
    reset(0);
    id = zui_launch_theme_open(&LT, "Coffer", 0, 0, true, false, 0);
    zui_launch_theme_load(&LT, id, ZUI_LT_FETCH, 10, 40);
    CHECK(zui_launch_theme_load(&LT, id, ZUI_LT_FAILED, 0, 0) == 0, "LT8: a failed step");
    CHECK(zui_launch_theme_load(&LT, id, ZUI_LT_DONE, 40, 40) == -1, "LT8: failed: done refused");
    CHECK(zui_launch_theme_load(&LT, id, ZUI_LT_FAILED, 0, 0) == -1, "LT8: failed once");
    CHECK(zui_wm_get(&WM, id)->progress == 32 && count(ZXL_EV_END) == 0,
          "LT8: failed: the screen stays where the work stopped, no end");

    /* cancelled while building */
    reset(0);
    id = zui_launch_theme_open(&LT, "Coffer", 0, 0, true, false, 0);
    zui_launch_theme_load(&LT, id, ZUI_LT_FETCH, 40, 40);
    zui_launch_theme_load(&LT, id, ZUI_LT_FAILED, 0, 0);
    zui_wm_progress(&WM, id, 256); /* even if something else pushes it */
    CHECK(count(ZXL_EV_END) == 0, "LT8: a cancelled build never plays the end");

    /* warm: only the end, no blocks taken */
    reset(0);
    id = zui_launch_theme_open(&LT, "Coffer", 0, 0, true, true, 0);
    CHECK(zui_launch_theme_load(&LT, id, ZUI_LT_FETCH, 1, 40) == -1 && nplayed == 1 &&
              played[0].kind == ZXL_EV_END,
          "LT8: warm: the end only");
    CHECK(zui_launch_theme_load(&LT, 999, ZUI_LT_FETCH, 1, 2) == -1 &&
              zui_launch_theme_load(0, id, ZUI_LT_FETCH, 1, 2) == -1,
          "LT8: unknown window refused");
}

int main(void)
{
    zui_surface_init(&S, PX, W, H, W);
    test_live_cold_launch();
    test_speed_is_the_load();
    test_off_screen_and_warm();
    test_themes_and_ink();
    test_full_house();
    test_sound_setting();
    test_fab_load();
    printf("test_zui_launch_theme: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
