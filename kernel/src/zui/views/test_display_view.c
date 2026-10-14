/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_display_view.c — the Display settings panel (views/display.h D1..D5):
 * the model on an injected list of screens and on the kernel's standard
 * modes, then the panel driven by clicks and keys through zui's event path,
 * apply, keep, revert and the 15-second automatic revert. Run by
 * test_display_view.sh. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "display.h"
#include "zui_tokens.h"
#include "../../display/display.h"

static int fails, checks;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            fails++;                                                                               \
            printf("  FAIL %s:%d ", __FILE__, __LINE__);                                           \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

static const dsp_mode_t BUILTIN[] = {
    {2560, 1600, {120000, 60000}, 2},
    {1920, 1200, {60000}, 1},
    {1280, 800, {60000, 59940}, 2},
};
static const dsp_mode_t EXTERNAL[] = {
    {3840, 2160, {60000, 30000}, 2},
    {1920, 1080, {144000, 120000, 60000, 59940}, 4},
    {0, 0, {60000}, 1}, /* no size: skipped */
    {800, 600, {0}, 0}, /* no rate: skipped */
};

static dsp_model_t M;
static dsp_config_t APPLIED[16];
static int napplied, refuse;
static int apply_cb(void *ctx, const dsp_config_t *c)
{
    (void) ctx;
    if (refuse) return -9;
    if (napplied < 16) APPLIED[napplied] = *c;
    napplied++;
    return DSP_OK;
}

static void fresh(uint32_t now)
{
    dsp_init(&M, now);
    M.apply = apply_cb;
    napplied = 0;
    refuse = 0;
    dsp_add_screen(&M, "Built-in", BUILTIN, 3);
    dsp_add_screen(&M, "External", EXTERNAL, 4);
}

/* ---------------- the model ---------------- */

static void test_model(void)
{
    fresh(0);
    CHECK(M.nscreen == 2 && M.screen[0].nmode == 3 && M.screen[1].nmode == 2,
          "screens and modes (unusable modes skipped): %u %u", M.screen[0].nmode,
          M.screen[1].nmode);
    const dsp_settings_t *p = dsp_proposed(&M);
    CHECK(p->mode == 0 && p->rate == 0 && p->scale == 175 && p->orient == DSP_ROT_0 &&
              p->depth == 128 && p->night == DSP_NIGHT_OFF && p->warmth == 128 &&
              p->night_from == 21 * 60 && p->night_to == 7 * 60,
          "defaults");
    CHECK(M.applied.s[1].scale == 250, "a 2160-line screen suggests 250%%");
    CHECK(M.proposed.theme == DSP_THEME_DARK && !dsp_pending(&M), "dark, nothing pending");
    CHECK(dsp_add_screen(&M, "x", BUILTIN, 0) == -1 && dsp_add_screen(&M, "x", 0, 1) == -1,
          "no modes: refused");
    dsp_mode_t bad = {0, 0, {0}, 0};
    CHECK(dsp_add_screen(&M, "x", &bad, 1) == -1 && M.nscreen == 2, "only unusable modes");
    CHECK(dsp_add_screen(&M, "3", BUILTIN, 1) == 2 && dsp_add_screen(&M, "4", BUILTIN, 1) == 3 &&
              dsp_add_screen(&M, "5", BUILTIN, 1) == -1,
          "at most DSP_SCREENS");
    dsp_mode_t many = {640, 480, {1, 2, 3, 4, 5, 6}, 9};
    fresh(0);
    dsp_add_screen(&M, "many", &many, 1);
    CHECK(M.screen[2].mode[0].nrate == DSP_RATES, "rates capped");
    CHECK(dsp_select(&M, 9) == DSP_E_ARG && dsp_select(&M, 1) == DSP_OK && M.sel == 1, "select");

    /* D1: proposals */
    fresh(0);
    CHECK(dsp_set_scale(&M, 200) == DSP_OK && dsp_proposed(&M)->scale == 200 &&
              dsp_applied(&M)->scale == 175 && dsp_pending(&M),
          "a proposal is not applied");
    dsp_discard(&M);
    CHECK(!dsp_pending(&M) && dsp_proposed(&M)->scale == 175, "discard");
    CHECK(dsp_set_scale(&M, 99) == DSP_E_ARG && dsp_set_scale(&M, 301) == DSP_E_ARG,
          "D3: scale range");
    dsp_set_scale(&M, 112);
    CHECK(dsp_proposed(&M)->scale == 100, "rounds down to the step");
    dsp_set_scale(&M, 113);
    CHECK(dsp_proposed(&M)->scale == 125, "rounds up to the step");
    dsp_set_scale(&M, 300);
    CHECK(dsp_proposed(&M)->scale == 300, "300%%");
    CHECK(dsp_set_depth(&M, 257) == DSP_E_ARG && dsp_set_depth(&M, 256) == DSP_OK &&
              dsp_set_depth(&M, 0) == DSP_OK && dsp_proposed(&M)->depth == 0,
          "depth effect 0..256");
    CHECK(dsp_set_warmth(&M, 300) == DSP_E_ARG && dsp_set_warmth(&M, 200) == DSP_OK, "warmth");
    CHECK(dsp_set_orient(&M, 4) == DSP_E_ARG && dsp_set_orient(&M, DSP_ROT_270) == DSP_OK,
          "orientation");
    CHECK(dsp_set_night(&M, 3) == DSP_E_ARG && dsp_set_night(&M, DSP_NIGHT_SCHEDULE) == DSP_OK,
          "night light");
    CHECK(dsp_set_schedule(&M, 1440, 0) == DSP_E_ARG &&
              dsp_set_schedule(&M, 0, 1440) == DSP_E_ARG &&
              dsp_set_schedule(&M, 22 * 60, 6 * 60 + 30) == DSP_OK,
          "schedule");
    CHECK(dsp_set_theme(&M, 3) == DSP_E_ARG && dsp_set_theme(&M, DSP_THEME_HIGH_CONTRAST) == DSP_OK,
          "theme");
    CHECK(dsp_set_rate(&M, 2) == DSP_E_ARG && dsp_set_rate(&M, 1) == DSP_OK, "rate");
    CHECK(dsp_set_mode(&M, 3) == DSP_E_ARG, "no such mode");

    /* D3: a mode change keeps the rate when it can */
    fresh(0);
    dsp_select(&M, 1);
    dsp_set_mode(&M, 1);
    CHECK(dsp_proposed(&M)->rate == 2, "60 Hz kept by value (index 2 there): %u",
          dsp_proposed(&M)->rate);
    dsp_set_rate(&M, 3); /* 59.94 */
    dsp_set_mode(&M, 0);
    CHECK(dsp_proposed(&M)->rate == 0, "59.94 Hz is not offered at 4K: its highest");
    dsp_set_rate(&M, 1); /* 30 */
    dsp_set_mode(&M, 1);
    CHECK(dsp_proposed(&M)->rate == 0, "30 Hz not at 1080p: the highest, 144 Hz");
    dsp_set_rate(&M, 2); /* 60 */
    dsp_set_mode(&M, 0);
    CHECK(dsp_proposed(&M)->rate == 0 && M.screen[1].mode[0].rate[0] == 60000, "60 Hz kept");
    CHECK(M.proposed.s[0].mode == 0 && M.proposed.s[0].scale == 175, "the other screen untouched");

    /* D2: apply, keep, revert */
    fresh(1000);
    CHECK(dsp_apply(&M, 1000) == DSP_E_STATE && napplied == 0, "nothing to apply");
    dsp_set_scale(&M, 200);
    dsp_set_theme(&M, DSP_THEME_LIGHT);
    CHECK(dsp_apply(&M, 2000) == DSP_OK && napplied == 1, "apply calls the callback once");
    CHECK(APPLIED[0].s[0].scale == 200 && APPLIED[0].theme == DSP_THEME_LIGHT, "with the proposal");
    CHECK(M.confirming && dsp_confirm_left_ms(&M) == DSP_CONFIRM_MS && !dsp_pending(&M),
          "D2: 15 s to confirm");
    CHECK(dsp_set_scale(&M, 250) == DSP_E_STATE && dsp_set_theme(&M, 0) == DSP_E_STATE &&
              dsp_set_mode(&M, 1) == DSP_E_STATE,
          "no proposals while it asks");
    CHECK(dsp_apply(&M, 2001) == DSP_E_STATE, "no second apply");
    dsp_discard(&M);
    CHECK(M.proposed.s[0].scale == 200, "discard does nothing while asking");
    CHECK(!dsp_tick(&M, 2000 + DSP_CONFIRM_MS - 1) && M.confirming && dsp_confirm_left_ms(&M) == 1,
          "1 ms left");
    CHECK(dsp_keep(&M) == DSP_OK && !M.confirming && M.keeps == 1, "keep");
    CHECK(dsp_applied(&M)->scale == 200 && M.applied.theme == DSP_THEME_LIGHT && napplied == 1,
          "kept as applied");
    CHECK(dsp_keep(&M) == DSP_E_STATE && dsp_revert(&M) == DSP_E_STATE, "nothing to confirm");
    CHECK(!dsp_tick(&M, 999999), "ticks without a countdown do nothing");

    dsp_set_orient(&M, DSP_ROT_90);
    dsp_apply(&M, 5000);
    CHECK(dsp_revert(&M) == DSP_OK && M.reverts == 1 && napplied == 3, "manual revert reapplies");
    CHECK(APPLIED[2].s[0].orient == DSP_ROT_0 && APPLIED[2].s[0].scale == 200,
          "the earlier settings, exactly");
    CHECK(dsp_applied(&M)->orient == DSP_ROT_0 && dsp_proposed(&M)->orient == DSP_ROT_0 &&
              !dsp_pending(&M),
          "proposal back too");

    dsp_set_depth(&M, 64);
    dsp_apply(&M, 10000);
    CHECK(!dsp_tick(&M, 10000 + 14999), "not yet");
    CHECK(dsp_tick(&M, 10000 + 15000) && !M.confirming && M.reverts == 2,
          "D2: reverts by itself at 15 s");
    CHECK(dsp_applied(&M)->depth == 128 && APPLIED[4].s[0].depth == 128, "depth back to 128");

    /* the callback can refuse: nothing changes */
    dsp_set_scale(&M, 100);
    refuse = 1;
    CHECK(dsp_apply(&M, 20000) == DSP_E_APPLY && !M.confirming && dsp_pending(&M) &&
              dsp_applied(&M)->scale == 200,
          "a refused apply keeps the proposal unapplied");
    refuse = 0;
    /* the countdown across the clock's wrap */
    dsp_apply(&M, 0xFFFFF000u);
    CHECK(!dsp_tick(&M, 0xFFFFF000u + 14000u) && dsp_tick(&M, 0xFFFFF000u + 15000u),
          "wraps cleanly");
    /* no callback at all: the model still keeps state */
    M.apply = 0;
    dsp_set_scale(&M, 150);
    CHECK(dsp_apply(&M, 1) == DSP_OK && dsp_revert(&M) == DSP_OK && dsp_applied(&M)->scale == 200,
          "no callback");

    /* night light */
    dsp_settings_t s;
    memset(&s, 0, sizeof s);
    s.night = DSP_NIGHT_OFF;
    CHECK(!dsp_night_active(&s, 0), "off");
    s.night = DSP_NIGHT_ON;
    CHECK(dsp_night_active(&s, 720), "on");
    s.night = DSP_NIGHT_SCHEDULE;
    s.night_from = 21 * 60;
    s.night_to = 7 * 60;
    CHECK(dsp_night_active(&s, 23 * 60) && dsp_night_active(&s, 0) && dsp_night_active(&s, 419) &&
              !dsp_night_active(&s, 420) && !dsp_night_active(&s, 12 * 60) &&
              dsp_night_active(&s, 21 * 60) && !dsp_night_active(&s, 21 * 60 - 1),
          "a schedule past midnight");
    s.night_from = 9 * 60;
    s.night_to = 17 * 60;
    CHECK(dsp_night_active(&s, 600) && !dsp_night_active(&s, 17 * 60) &&
              !dsp_night_active(&s, 8 * 60) && dsp_night_active(&s, 600 + 1440),
          "a daytime schedule; minutes wrap");
    s.night_to = s.night_from;
    CHECK(!dsp_night_active(&s, 9 * 60), "an empty schedule");
    uint32_t r, g, b;
    dsp_night_gain(0, &r, &g, &b);
    CHECK(r == 256 && g == 256 && b == 256, "warmth 0: no change");
    dsp_night_gain(256, &r, &g, &b);
    CHECK(r == 256 && g == 166 && b == 76, "warmth 256: %u %u %u", r, g, b);
    dsp_night_gain(9999, &r, &g, &b);
    CHECK(b == 76, "clamped");
    uint32_t pb = 257, ok = 1;
    for (uint32_t w = 0; w <= 256; w++) {
        dsp_night_gain(w, &r, &g, &b);
        ok &= b <= pb && b <= g && g <= r;
        pb = b;
    }
    CHECK(ok, "warmer is never bluer");

    /* names and text */
    CHECK(!strcmp(dsp_theme_name(0), "Dark") && !strcmp(dsp_theme_name(1), "Light") &&
              !strcmp(dsp_theme_name(2), "High contrast") && !strcmp(dsp_theme_name(3), "?"),
          "D4: theme names");
    CHECK(!strcmp(dsp_orient_name(1), "Portrait") && !strcmp(dsp_orient_name(9), "?") &&
              !strcmp(dsp_night_name(2), "Scheduled"),
          "names");
    char t[64];
    dsp_mode_text(t, sizeof t, &BUILTIN[0]);
    CHECK(!strcmp(t, "2560 \xC3\x97 1600"), "mode text: %s", t);
    dsp_rate_text(t, sizeof t, 59940);
    CHECK(!strcmp(t, "59.94 Hz"), "%s", t);
    dsp_rate_text(t, sizeof t, 60000);
    CHECK(!strcmp(t, "60 Hz"), "%s", t);
    dsp_rate_text(t, sizeof t, 59900);
    CHECK(!strcmp(t, "59.9 Hz"), "%s", t);
    dsp_rate_text(t, sizeof t, 143856);
    CHECK(!strcmp(t, "143.86 Hz"), "%s", t);
    dsp_minutes_text(t, sizeof t, 7 * 60);
    CHECK(!strcmp(t, "07:00"), "%s", t);
    dsp_minutes_text(t, sizeof t, 21 * 60 + 30 + 1440);
    CHECK(!strcmp(t, "21:30"), "%s", t);
    CHECK(dsp_scale_for(4320) == 250 && dsp_scale_for(1440) == 175 && dsp_scale_for(1080) == 150 &&
              dsp_scale_for(900) == 125 && dsp_scale_for(720) == 100,
          "suggested scales");
}

static void test_kernel_modes(void)
{
    dsp_init(&M, 0);
    CHECK(dsp_add_kernel_screen(&M, "Panel") == 0, "a screen from display.h");
    uint32_t want = 0;
    for (uint32_t i = 0; i < zxv_display_mode_count(); i++) {
        uint32_t w, h;
        const char *n;
        zxv_display_mode(i, &w, &h, &n);
        want += w <= ZXV_DISPLAY_MAX_W && h <= ZXV_DISPLAY_MAX_H &&
                (uint64_t) w * h <= ZXV_DISPLAY_MAX_PIXELS;
    }
    if (want > DSP_MODES) want = DSP_MODES;
    CHECK(M.screen[0].nmode == want && want > 3, "the modes that fit the budget: %u of %u",
          M.screen[0].nmode, zxv_display_mode_count());
    uint32_t ok = 1;
    for (uint32_t i = 0; i < M.screen[0].nmode; i++) {
        const dsp_mode_t *md = &M.screen[0].mode[i];
        ok &= (uint64_t) md->w * md->h <= ZXV_DISPLAY_MAX_PIXELS && md->nrate == 1 &&
              md->rate[0] == 60000;
        zxv_display_t d;
        ok &= zxv_display_negotiate(&d, 0, 0, 0, md->w, md->h) == 0 && d.w == md->w;
    }
    CHECK(ok, "each offered mode is one the kernel accepts");
    CHECK(M.screen[0].mode[0].w == ZXV_DISPLAY_MAX_W && M.screen[0].mode[0].h == ZXV_DISPLAY_MAX_H,
          "largest first: the budget's own");
}

/* ---------------- the panel, driven by events ---------------- */

static zui_ui_t UI;
static uint32_t PX[1280 * 800];
static zui_surface_t SURF;
static uint32_t now = 1000;
static const zui_screen_t LAPTOP = {1280, 800, 256, ZUI_INPUT_POINTER};
static const zui_screen_t PHONE = {390, 800, 256, ZUI_INPUT_TOUCH};
static display_view_t V;
static int unhandled;

static void on(zui_ui_t *ui, zui_id_t w, zui_action_t a, int32_t arg, void *ctx)
{
    (void) ui;
    if (!display_view_action(ctx, w, a, arg) && a != ZUI_ACT_CHANGE && a != ZUI_ACT_DISMISS)
        unhandled++;
}

static void screen(const zui_screen_t *sc)
{
    zui_theme_t th;
    memset(&th, 0, sizeof th);
    th.body.size_px = 16; /* no font: fixed advance, deterministic */
    th.label.size_px = 12;
    th.title.size_px = 20;
    th.mono.size_px = 16;
    zui_ui_init(&UI, sc, &th);
    zui_ui_on(&UI, on, &V);
    unhandled = 0;
    zui_surface_init(&SURF, PX, (int32_t) sc->px_w, (int32_t) sc->px_h, (int32_t) sc->px_w);
}

static void paint(void)
{
    zui_damage_all(&UI.damage, SURF.w, SURF.h);
    zui_ui_paint(&UI, &SURF);
}

static zui_event_t ev(uint8_t type, int32_t x, int32_t y)
{
    zui_event_t e;
    memset(&e, 0, sizeof e);
    e.type = type;
    e.x = x;
    e.y = y;
    e.button = ZUI_BTN_LEFT;
    e.ms = now;
    return e;
}

static void send(zui_event_t e)
{
    zui_ui_event(&UI, &e);
    paint();
}

static bool shown(zui_id_t id)
{
    for (zui_widget_t *w = zui_w(&UI, id); w; w = zui_w(&UI, w->parent))
        if (w->hidden) return false;
    return true;
}

static void press_at(int32_t x, int32_t y)
{
    now += 1000; /* never a double click by accident */
    send(ev(ZUI_EV_PTR_DOWN, x, y));
    now += 20;
    send(ev(ZUI_EV_PTR_UP, x, y));
}

static bool click(zui_id_t id)
{
    paint();
    zui_widget_t *w = zui_w(&UI, id);
    if (!w || !shown(id) || w->r.w <= 0 || w->r.h <= 0) return false;
    press_at(w->r.x + w->r.w / 2, w->r.y + w->r.h / 2);
    return true;
}

static void key(uint16_t k)
{
    zui_event_t e = ev(ZUI_EV_KEY_DOWN, 0, 0);
    e.key = k;
    send(e);
}

/* Pick menu item i with the pointer, as a person would. */
static bool pick(int32_t i)
{
    if (!UI.menu.open) return false;
    int32_t rh = UI.screen.input == ZUI_INPUT_POINTER ? 26 : ZUI_TOUCH_MIN;
    int32_t x = UI.menu.r.x + UI.menu.r.w / 2, y = UI.menu.r.y + ZUI_S1 + i * rh + rh / 2;
    now += 1000;
    send(ev(ZUI_EV_PTR_MOVE, x, y));
    send(ev(ZUI_EV_PTR_UP, x, y));
    return !UI.menu.open;
}

static const char *text_of(zui_id_t id)
{
    return zui_w(&UI, id)->label;
}

static bool disabled(zui_id_t id)
{
    return zui_w(&UI, id)->disabled;
}

/* D5: the gold buttons on screen. */
static int golds(void)
{
    int n = 0;
    for (uint32_t i = 0; i < UI.n; i++)
        if (UI.w[i].kind == ZUI_W_BUTTON && UI.w[i].style == ZUI_BTN_PRIMARY && shown((zui_id_t) i))
            n++;
    return n;
}

static void tick(uint32_t ms)
{
    now += ms;
    display_view_tick(&V, now);
    paint();
}

static void test_panel(void)
{
    fresh(now);
    screen(&LAPTOP);
    display_view_build(&V, &UI, &M);
    paint();
    CHECK((PX[0] & 0xFFFFFFu) == ZUI_VOID, "the ground");
    uint32_t white = 0;
    for (uint32_t i = 0; i < 1280u * 800u; i++) white += (PX[i] & 0xFFFFFFu) == 0xFFFFFFu;
    CHECK(white == 0, "no white pixels (%u)", white);
    CHECK(!strcmp(text_of(V.sh.title), "Display"), "the title");
    CHECK(!strcmp(text_of(V.res), "2560 \xC3\x97 1600") && !strcmp(text_of(V.rate), "120 Hz") &&
              !strcmp(text_of(V.scale_val), "175%") && !strcmp(text_of(V.orient), "Landscape") &&
              !strcmp(text_of(V.depth_val), "128 / 256") && !strcmp(text_of(V.night), "Off") &&
              !strcmp(text_of(V.theme), "Dark"),
          "the settings shown");
    CHECK(UI.w[V.screens].u.list.rows == 2 && UI.w[V.screens].u.list.sel == 0, "two screens");
    CHECK(disabled(V.apply) && disabled(V.discard) && !shown(V.confirm), "nothing to apply");
    CHECK(!shown(V.warmth_row) && !shown(V.sched_row), "night light rows hidden while off");
    CHECK(golds() == 1, "D5: one gold button (Apply)");

    /* D1: the controls only propose */
    CHECK(click(V.scale_up), "click +");
    CHECK(dsp_proposed(&M)->scale == 200 && dsp_applied(&M)->scale == 175 && napplied == 0,
          "D1: proposed, not applied");
    CHECK(!strcmp(text_of(V.scale_val), "200%") && !disabled(V.apply) &&
              !strcmp(text_of(V.pending), "Changes not applied yet"),
          "shown as pending");
    for (int i = 0; i < 6; i++) click(V.scale_up);
    CHECK(dsp_proposed(&M)->scale == 300 && disabled(V.scale_up), "stops at 300%%");
    for (int i = 0; i < 12; i++) click(V.scale_dn);
    CHECK(dsp_proposed(&M)->scale == 100 && disabled(V.scale_dn), "stops at 100%%");

    /* a menu, by pointer */
    CHECK(click(V.res) && UI.menu.open && UI.menu.n == 3 && UI.menu.owner == V.res,
          "the resolution menu");
    CHECK(UI.menu.hot == 0 && !strcmp(UI.menu.item[2], "1280 \xC3\x97 800"),
          "lists the modes, current one first");
    CHECK(pick(2), "pick 1280 x 800");
    CHECK(dsp_proposed(&M)->mode == 2 && !strcmp(text_of(V.res), "1280 \xC3\x97 800"),
          "mode proposed");
    CHECK(!strcmp(text_of(V.rate), "60 Hz"), "120 Hz is not there: its highest");
    CHECK(click(V.rate) && UI.menu.n == 2 && pick(1) && dsp_proposed(&M)->rate == 1 &&
              !strcmp(text_of(V.rate), "59.94 Hz"),
          "refresh rate");
    /* a menu, by keyboard: current highlighted, Down, Enter */
    CHECK(click(V.theme) && UI.menu.hot == 0, "theme menu at Dark");
    key(ZUI_KEY_DOWN);
    key(ZUI_KEY_DOWN);
    key(ZUI_KEY_ENTER);
    CHECK(M.proposed.theme == DSP_THEME_HIGH_CONTRAST && !strcmp(text_of(V.theme), "High contrast"),
          "high contrast by keys");
    CHECK(click(V.orient) && pick(1) && !strcmp(text_of(V.orient), "Portrait"), "portrait");
    CHECK(click(V.theme) && UI.menu.hot == 2, "the menu opens at the current choice");
    key(ZUI_KEY_ESCAPE);
    CHECK(!UI.menu.open && M.proposed.theme == DSP_THEME_HIGH_CONTRAST, "Escape keeps it");

    /* depth effect */
    for (int i = 0; i < 9; i++) click(V.depth_dn);
    CHECK(dsp_proposed(&M)->depth == 0 && disabled(V.depth_dn) &&
              !strcmp(text_of(V.depth_val), "0 / 256"),
          "depth effect down to 0");
    for (int i = 0; i < 20; i++) click(V.depth_up);
    CHECK(dsp_proposed(&M)->depth == 256 && disabled(V.depth_up), "and up to 256");
    zui_widget_t *trk = zui_w(&UI, V.depth_track);
    CHECK(trk && trk->kind == ZUI_W_CANVAS && trk->u.canvas.ctx == &M.proposed.s[0].depth &&
              trk->r.w > 0,
          "the track shows the proposed level");

    /* night light */
    CHECK(click(V.night) && pick(2) && dsp_proposed(&M)->night == DSP_NIGHT_SCHEDULE,
          "scheduled night light");
    CHECK(shown(V.warmth_row) && shown(V.sched_row), "warmth and schedule shown");
    CHECK(!strcmp(text_of(V.from_val), "21:00") && !strcmp(text_of(V.to_val), "07:00"),
          "default schedule");
    click(V.from_up);
    click(V.to_dn);
    click(V.to_dn);
    CHECK(dsp_proposed(&M)->night_from == 21 * 60 + 30 && dsp_proposed(&M)->night_to == 6 * 60,
          "schedule in 30 minute steps");
    CHECK(!strcmp(text_of(V.from_val), "21:30") && !strcmp(text_of(V.to_val), "06:00"), "shown");
    for (int i = 0; i < 13; i++) click(V.to_dn);
    CHECK(dsp_proposed(&M)->night_to == 23 * 60 + 30, "wraps past midnight backwards");
    click(V.warmth_up);
    CHECK(dsp_proposed(&M)->warmth == 144 && !strcmp(text_of(V.warmth_val), "144 / 256"), "warmth");
    CHECK(click(V.night) && pick(1) && shown(V.warmth_row) && !shown(V.sched_row),
          "on: warmth, no schedule");

    /* discard */
    CHECK(click(V.discard) && !dsp_pending(&M) && !strcmp(text_of(V.scale_val), "175%") &&
              !strcmp(text_of(V.theme), "Dark") && disabled(V.apply),
          "discard puts every control back");

    /* the second screen, from the list */
    paint();
    zui_widget_t *lst = zui_w(&UI, V.screens);
    int32_t lx = lst->r.x + lst->r.w / 2, ly = -1;
    for (int32_t y = lst->r.y; y < lst->r.y + lst->r.h && ly < 0; y++)
        if (zui_list_row_at(&UI, V.screens, lx, y) == 1) ly = y + 4;
    CHECK(ly > 0, "row 2 is on screen");
    press_at(lx, ly);
    CHECK(M.sel == 1 && !strcmp(text_of(V.res), "3840 \xC3\x97 2160") &&
              !strcmp(text_of(V.scale_val), "250%"),
          "selecting a screen shows its settings");
    zui_row_t rw;
    memset(&rw, 0, sizeof rw);
    lst->u.list.row(&UI, V.screens, 1, &rw, lst->u.list.row_ctx);
    CHECK(!strcmp(rw.text, "External") && !strcmp(rw.detail, "3840 \xC3\x97 2160"),
          "a list row: name and applied mode");
    CHECK(trk->u.canvas.ctx == &M.proposed.s[1].depth, "the track follows the screen");

    /* D2: apply, the countdown, keep */
    click(V.scale_dn);
    CHECK(dsp_proposed(&M)->scale == 225, "225%%");
    tick(10);
    CHECK(click(V.apply) && napplied == 1 && M.confirming, "Apply");
    CHECK(APPLIED[0].s[1].scale == 225, "the callback got the proposal");
    CHECK(shown(V.confirm) && shown(V.keep) && shown(V.revert) && !shown(V.apply) &&
              !shown(V.discard),
          "the confirmation replaces Apply");
    CHECK(golds() == 1 && zui_w(&UI, V.keep)->style == ZUI_BTN_PRIMARY, "D5: Keep is the gold one");
    CHECK(strstr(text_of(V.confirm_text), "Reverting in 15 s") != NULL, "%s",
          text_of(V.confirm_text));
    CHECK(disabled(V.scale_up) && disabled(V.res) && disabled(V.theme) && disabled(V.screens),
          "nothing else can change while it asks");
    CHECK(!strcmp(text_of(V.sh.status), "Confirm"), "status");
    zui_w(&UI, V.scale_up)->disabled = false; /* even if a click got through */
    click(V.scale_up);
    CHECK(dsp_proposed(&M)->scale == 225 && shown(V.sh.error), "the model refuses it");
    tick(0); /* the click above took 1.02 s */
    CHECK(strstr(text_of(V.confirm_text), "14 s") != NULL, "counts down: %s",
          text_of(V.confirm_text));
    tick(1000);
    CHECK(strstr(text_of(V.confirm_text), "13 s") != NULL, "counts down: %s",
          text_of(V.confirm_text));
    CHECK(click(V.keep) && !M.confirming && M.keeps == 1 && !shown(V.confirm) && shown(V.apply) &&
              dsp_applied(&M)->scale == 225 && napplied == 1,
          "Keep");
    CHECK(!shown(V.sh.error), "the error line clears");

    /* revert by the button */
    click(V.depth_dn);
    click(V.apply);
    CHECK(click(V.revert) && M.reverts == 1 && napplied == 3 && APPLIED[2].s[1].depth == 128 &&
              !strcmp(text_of(V.depth_val), "128 / 256"),
          "Revert puts the old settings back");

    /* D2: no answer in 15 s reverts */
    click(V.orient);
    pick(1);
    click(V.apply);
    uint32_t t0 = now;
    for (int s = 1; s < 15; s++) {
        tick(1000 - (now - t0) % 1000u);
    }
    CHECK(M.confirming && strstr(text_of(V.confirm_text), "in 1 s") != NULL, "one second left: %s",
          text_of(V.confirm_text));
    tick(DSP_CONFIRM_MS - (now - t0));
    CHECK(!M.confirming && M.reverts == 2 && !shown(V.confirm) &&
              !strcmp(text_of(V.orient), "Landscape") && dsp_applied(&M)->orient == DSP_ROT_0,
          "reverted by itself at 15 s");
    CHECK(APPLIED[napplied - 1].s[1].orient == DSP_ROT_0, "through the callback");

    /* a refusing screen */
    click(V.scale_up);
    refuse = 1;
    click(V.apply);
    CHECK(!M.confirming && shown(V.sh.error) &&
              !strcmp(text_of(V.sh.error), "The screen did not take these settings"),
          "a refusal is said, nothing changes");
    refuse = 0;
    CHECK(!disabled(V.apply), "and Apply can be tried again");

    char a11y[16384];
    zui_a11y_text(&UI, a11y, sizeof a11y);
    CHECK(strstr(a11y, "button \"Apply\"") && strstr(a11y, "Depth effect") &&
              strstr(a11y, "More depth effect strength") && strstr(a11y, "list \"Screens\""),
          "accessible names");
    CHECK(unhandled == 0, "every action was the panel's (%d)", unhandled);

    /* a phone: it builds and paints */
    fresh(now);
    screen(&PHONE);
    display_view_build(&V, &UI, &M);
    paint();
    CHECK((PX[0] & 0xFFFFFFu) == ZUI_VOID && UI.overflow == 0, "phone");

    /* the kernel's modes in the panel */
    dsp_init(&M, now);
    dsp_add_kernel_screen(&M, "Panel");
    screen(&LAPTOP);
    display_view_build(&V, &UI, &M);
    CHECK(click(V.res) && UI.menu.n == M.screen[0].nmode, "kernel modes in the menu (%u)",
          UI.menu.n);
    key(ZUI_KEY_ESCAPE);
    CHECK(disabled(V.rate), "one rate: nothing to choose");

    /* no screens at all */
    dsp_init(&M, now);
    screen(&LAPTOP);
    display_view_build(&V, &UI, &M);
    paint();
    CHECK(!strcmp(text_of(V.res), "No screen") && disabled(V.res) && disabled(V.scale_up),
          "no screen: nothing to change");
}

int main(void)
{
    test_model();
    test_kernel_modes();
    test_panel();
    printf("%s test_display_view: %d/%d checks\n", fails ? "[FAIL]" : "[PASS]", checks - fails,
           checks);
    return fails ? 1 : 0;
}
