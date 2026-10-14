/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zui_motion.c — reduced motion and the comfort preferences
 * (zui_motion.h MO1..MO7). */
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "zui_motion.h"

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

/* a watcher that remembers */
static int calls;
static uint32_t last_changed;
static zui_motion_prefs_t seen;
static void watch(const zui_motion_prefs_t *p, uint32_t changed, void *ctx)
{
    (void) ctx;
    calls++;
    last_changed = changed;
    seen = *p;
}
static int calls2;
static void watch2(const zui_motion_prefs_t *p, uint32_t changed, void *ctx)
{
    (void) p, (void) changed;
    calls2++;
    if (ctx) (*(int *) ctx)++;
}

static void test_defaults(void)
{
    zui_motion_prefs_t d;
    memset(&d, 0x5A, sizeof d);
    zui_motion_defaults(&d);
    CHECK(!d.reduce_motion && !d.reduce_transparency && !d.increase_contrast && !d.bold_text &&
              !d.flash_alert && !d.mono_audio,
          "defaults: every flag off");
    CHECK(d.text_scale_pct == 100 && d.cursor_pct == 100, "defaults: 100 %% text and pointer");
    CHECK(d.crossfade_ms == ZUI_MOTION_FADE_MS, "defaults: the crossfade length");
    zui_motion_defaults(0);
    CHECK(1, "defaults(NULL) is harmless");
    const zui_motion_prefs_t *g = zui_motion_get();
    CHECK(g && memcmp(g, &d, sizeof d) == 0, "the process starts on the defaults");
    CHECK(zui_motion_reset() == 0, "reset on defaults changes nothing");
    CHECK(zui_motion_set(0) == 0, "set(NULL) changes nothing");
}

static void test_set_and_clamp(void)
{
    uint32_t g0 = zui_motion_generation();
    zui_motion_prefs_t p;
    zui_motion_defaults(&p);
    p.text_scale_pct = 50;
    p.cursor_pct = 9999;
    p.crossfade_ms = 5000;
    uint32_t c = zui_motion_set(&p);
    CHECK(c == (ZUI_PREF_CURSOR | ZUI_PREF_CROSSFADE), "only the real changes reported (%x)", c);
    CHECK(zui_motion_get()->text_scale_pct == 100, "text scale clamps up to 100");
    CHECK(zui_motion_get()->cursor_pct == 400, "pointer clamps down to 400");
    CHECK(zui_motion_get()->crossfade_ms == ZUI_MOTION_MS, "crossfade clamps to ZUI_MOTION_MS");
    CHECK(zui_motion_generation() == g0 + 1, "a change bumps the generation once");
    CHECK(zui_motion_set(&p) == 0 && zui_motion_generation() == g0 + 1,
          "the same set again is no change");
    CHECK(zui_motion_set_text_scale(400) == ZUI_PREF_TEXT_SCALE &&
              zui_motion_get()->text_scale_pct == 310,
          "text scale clamps to 310");
    CHECK(zui_motion_set_text_scale(310) == 0, "same text scale: no change");
    CHECK(zui_motion_set_text_scale(99) == ZUI_PREF_TEXT_SCALE &&
              zui_motion_get()->text_scale_pct == 100,
          "99 clamps to 100");
    CHECK(zui_motion_set_text_scale(205) && zui_motion_get()->text_scale_pct == 205,
          "in-range kept exactly");
    CHECK(zui_motion_set_cursor(0) == ZUI_PREF_CURSOR && zui_motion_get()->cursor_pct == 100,
          "pointer 0 clamps to 100");
    CHECK(zui_motion_set_cursor(250) && zui_motion_get()->cursor_pct == 250, "pointer 250 kept");
    CHECK(zui_motion_set_crossfade(0) == ZUI_PREF_CROSSFADE && zui_motion_get()->crossfade_ms == 0,
          "crossfade 0");
    CHECK(zui_motion_set_crossfade(181) && zui_motion_get()->crossfade_ms == 180,
          "crossfade 181 clamps to 180");
    struct {
        uint32_t bit;
        size_t off;
    } F[] = {
        {ZUI_PREF_REDUCE_MOTION, offsetof(zui_motion_prefs_t, reduce_motion)},
        {ZUI_PREF_REDUCE_TRANSPARENCY, offsetof(zui_motion_prefs_t, reduce_transparency)},
        {ZUI_PREF_INCREASE_CONTRAST, offsetof(zui_motion_prefs_t, increase_contrast)},
        {ZUI_PREF_BOLD_TEXT, offsetof(zui_motion_prefs_t, bold_text)},
        {ZUI_PREF_FLASH_ALERT, offsetof(zui_motion_prefs_t, flash_alert)},
        {ZUI_PREF_MONO_AUDIO, offsetof(zui_motion_prefs_t, mono_audio)},
    };
    for (unsigned i = 0; i < sizeof F / sizeof F[0]; i++) {
        const bool *b = (const bool *) ((const char *) zui_motion_get() + F[i].off);
        CHECK(zui_motion_set_flag(F[i].bit, true) == F[i].bit && *b, "flag %u on", i);
        CHECK(zui_motion_set_flag(F[i].bit, true) == 0, "flag %u on again: no change", i);
        /* no other flag moved */
        int others = 0;
        for (unsigned j = 0; j < sizeof F / sizeof F[0]; j++)
            if (j != i && *(const bool *) ((const char *) zui_motion_get() + F[j].off)) others++;
        CHECK(others == 0, "flag %u alone", i);
        CHECK(zui_motion_set_flag(F[i].bit, false) == F[i].bit && !*b, "flag %u off", i);
    }
    CHECK(zui_motion_set_flag(ZUI_PREF_TEXT_SCALE, true) == 0, "a non-flag bit is refused");
    CHECK(zui_motion_set_flag(ZUI_PREF_REDUCE_MOTION | ZUI_PREF_BOLD_TEXT, true) == 0,
          "two bits at once is refused");
    uint32_t r = zui_motion_reset();
    CHECK(r == (ZUI_PREF_TEXT_SCALE | ZUI_PREF_CURSOR | ZUI_PREF_CROSSFADE),
          "reset reports what it put back (%x)", r);
}

static void test_watch(void)
{
    zui_motion_reset();
    calls = 0;
    CHECK(zui_motion_watch(watch, 0), "watch");
    CHECK(calls == 1 && last_changed == ZUI_PREF_ALL, "a new watcher is told everything at once");
    CHECK(zui_motion_watch(watch, 0) && calls == 2, "watching twice re-syncs");
    zui_motion_set_flag(ZUI_PREF_REDUCE_MOTION, true);
    CHECK(calls == 3 && last_changed == ZUI_PREF_REDUCE_MOTION && seen.reduce_motion,
          "a change is told once, with its bit");
    zui_motion_set_flag(ZUI_PREF_REDUCE_MOTION, true);
    CHECK(calls == 3, "no change, no call");
    bool flag = false;
    CHECK(zui_motion_watch(zui_motion_sync_flag, &flag) && flag, "sync_flag copies at once");
    zui_motion_set_flag(ZUI_PREF_REDUCE_MOTION, false);
    CHECK(!flag, "sync_flag follows off");
    flag = true;
    zui_motion_set_text_scale(150);
    CHECK(flag, "sync_flag ignores other changes");
    zui_motion_sync_flag(zui_motion_get(), ZUI_PREF_ALL, 0);
    CHECK(1, "sync_flag with no ctx is harmless");
    CHECK(zui_motion_unwatch(zui_motion_sync_flag, &flag), "unwatch");
    CHECK(!zui_motion_unwatch(zui_motion_sync_flag, &flag), "unwatch twice: false");
    CHECK(!zui_motion_unwatch(0, 0), "unwatch NULL: false");
    CHECK(!zui_motion_watch(0, 0), "watch NULL: false");
    zui_motion_set_flag(ZUI_PREF_REDUCE_MOTION, true);
    CHECK(flag, "an unwatched flag is left alone");
    CHECK(zui_motion_unwatch(watch, 0), "unwatch the first");
    /* fill the table */
    int counts[ZUI_MOTION_HOOKS + 1];
    memset(counts, 0, sizeof counts);
    uint32_t ok = 0;
    for (uint32_t i = 0; i < ZUI_MOTION_HOOKS + 1; i++) ok += zui_motion_watch(watch2, &counts[i]);
    CHECK(ok == ZUI_MOTION_HOOKS, "ZUI_MOTION_HOOKS watchers fit, then full (%u)", ok);
    CHECK(counts[ZUI_MOTION_HOOKS] == 0, "a refused watcher is never called");
    calls2 = 0;
    zui_motion_set_flag(ZUI_PREF_BOLD_TEXT, true);
    CHECK(calls2 == (int) ZUI_MOTION_HOOKS, "every watcher told");
    CHECK(zui_motion_unwatch(watch2, &counts[3]), "a middle one leaves");
    CHECK(zui_motion_watch(watch2, &counts[ZUI_MOTION_HOOKS]), "its slot is reused");
    for (uint32_t i = 0; i <= ZUI_MOTION_HOOKS; i++) zui_motion_unwatch(watch2, &counts[i]);
    calls2 = 0;
    zui_motion_reset();
    CHECK(calls2 == 0, "all gone");
}

static void test_animation(void)
{
    zui_motion_reset();
    CHECK(zui_motion_style() == ZUI_MOTION_FULL, "full motion");
    CHECK(zui_motion_duration(360) == 360 && zui_motion_duration(0) == 0, "full: ms unchanged");
    CHECK(zui_motion_step(16, 360) == 16, "full: a step is a step");
    CHECK(zui_motion_progress_q8(0, 360) == 0 && zui_motion_progress_q8(180, 360) == 128 &&
              zui_motion_progress_q8(360, 360) == 256 && zui_motion_progress_q8(999, 360) == 256,
          "progress 0, half, end, past");
    CHECK(zui_motion_progress_q8(90, 360) == 64, "a quarter");
    CHECK(zui_motion_ease_q8(0) == 0 && zui_motion_ease_q8(256) == 256 &&
              zui_motion_ease_q8(999) == 256,
          "ease ends");
    CHECK(zui_motion_ease_q8(128) == 224, "ease out: half time is 7/8 there (%u)",
          zui_motion_ease_q8(128));
    uint32_t prev = 0, mono = 1;
    for (uint32_t t = 0; t <= 256; t++) {
        uint32_t v = zui_motion_ease_q8(t);
        if (v < prev || v < t) mono = 0;
        prev = v;
    }
    CHECK(mono, "ease is monotonic and never behind linear");
    CHECK(zui_motion_travel(100, 0, 360) == 0 && zui_motion_travel(100, 180, 360) == 50 &&
              zui_motion_travel(100, 360, 360) == 100,
          "full: doors travel linearly");
    CHECK(zui_motion_travel(-80, 90, 360) == -20, "travel keeps the sign");
    CHECK(zui_motion_travel(7, 3, 0) == 7, "a zero-length move is done");
    CHECK(zui_motion_fade_a255(0, 360) == 255 && zui_motion_fade_a255(360, 360) == 0,
          "fade 255 -> 0");
    CHECK(zui_motion_fade_a255(180, 360) == 127, "fade at half (%u)",
          zui_motion_fade_a255(180, 360));

    /* reduced, with the crossfade */
    zui_motion_set_flag(ZUI_PREF_REDUCE_MOTION, true);
    CHECK(zui_motion_style() == ZUI_MOTION_FADE, "reduced with a fade");
    CHECK(zui_motion_duration(360) == ZUI_MOTION_FADE_MS, "reduced: the crossfade length");
    CHECK(zui_motion_duration(50) == 50, "never longer than the original");
    CHECK(zui_motion_step(40, 360) == 120, "a step covers 3x the animation (120 ms total)");
    CHECK(zui_motion_step(1, 360) == 3, "and rounds up so it always ends");
    CHECK(zui_motion_step(40, 100) == 40, "a short animation keeps its time");
    CHECK(zui_motion_progress_q8(60, 360) == 128, "progress over the fade");
    CHECK(zui_motion_progress_q8(120, 360) == 256, "done after the fade");
    CHECK(zui_motion_ease_q8(100) == 100, "reduced: linear");
    CHECK(zui_motion_travel(100, 359, 360) == 0, "reduced: no travel before the end");
    CHECK(zui_motion_travel(100, 360, 360) == 100, "reduced: there at the end");
    CHECK(zui_motion_fade_a255(60, 360) == 127 && zui_motion_fade_a255(120, 360) == 0,
          "reduced: the crossfade runs over its own length");
    /* simulate the window doors with the step: they finish in 120 ms of ticks */
    uint32_t t = 0, real = 0;
    while (t < 360 && real < 1000) {
        t += zui_motion_step(16, 360);
        real += 16;
    }
    CHECK(real == 128, "doors done after 8 ticks of 16 ms (%u)", real);

    /* reduced, cut */
    zui_motion_set_crossfade(0);
    CHECK(zui_motion_style() == ZUI_MOTION_CUT, "reduced with a cut");
    CHECK(zui_motion_duration(360) == 0, "cut: zero");
    CHECK(zui_motion_step(1, 360) == 360, "cut: one tick finishes");
    CHECK(zui_motion_progress_q8(0, 360) == 256, "cut: done at once");
    CHECK(zui_motion_fade_a255(0, 360) == 0, "cut: gone at once");
    CHECK(zui_motion_step(5, 0) == 5, "a zero-length animation steps as is");
    zui_motion_set_flag(ZUI_PREF_REDUCE_MOTION, false);
    CHECK(zui_motion_style() == ZUI_MOTION_FULL && zui_motion_duration(360) == 360,
          "crossfade 0 without reduce motion is still full motion");
    CHECK(zui_motion_step(0xFFFFFFFFu, 360) == 0xFFFFFFFFu, "full: huge steps pass through");
    zui_motion_set_flag(ZUI_PREF_REDUCE_MOTION, true);
    zui_motion_set_crossfade(1);
    CHECK(zui_motion_step(0xFFFFFFFFu, 360) == 0xFFFFFFFFu, "huge step saturates");
    zui_motion_reset();
}

static void test_text_and_more(void)
{
    zui_motion_reset();
    CHECK(zui_motion_text_px(14) == 14, "100 %%");
    zui_motion_set_text_scale(310);
    CHECK(zui_motion_text_px(14) == 43, "310 %%: 43.4 -> 43 (%d)", zui_motion_text_px(14));
    zui_motion_set_text_scale(150);
    CHECK(zui_motion_text_px(15) == 23, "150 %%: 22.5 rounds to 23");
    CHECK(zui_motion_text_px(0) == 0, "zero stays zero");
    zui_motion_set_cursor(200);
    CHECK(zui_motion_cursor_px(24) == 48, "pointer 200 %%");
    zui_motion_set_cursor(125);
    CHECK(zui_motion_cursor_px(10) == 13, "pointer 125 %%: 12.5 -> 13");
    CHECK(zui_motion_alpha(128) == 128 && zui_motion_alpha(0) == 0 && zui_motion_alpha(999) == 255,
          "alpha passes (clamped) without reduce transparency");
    zui_motion_set_flag(ZUI_PREF_REDUCE_TRANSPARENCY, true);
    CHECK(zui_motion_alpha(1) == 255 && zui_motion_alpha(200) == 255, "reduced: opaque");
    CHECK(zui_motion_alpha(0) == 0, "reduced: invisible stays invisible");
    CHECK(zui_motion_scheme(ZUI_SCHEME_LIGHT) == ZUI_SCHEME_LIGHT, "scheme passes");
    zui_motion_set_flag(ZUI_PREF_INCREASE_CONTRAST, true);
    CHECK(zui_motion_scheme(ZUI_SCHEME_LIGHT) == ZUI_SCHEME_HIGH_CONTRAST &&
              zui_motion_scheme(ZUI_SCHEME_DARK) == ZUI_SCHEME_HIGH_CONTRAST,
          "increase contrast: high contrast");
    zui_motion_reset();
}

static int sounds, flashes;
static uint32_t flash_ms;
static void snd(void *c)
{
    (void) c;
    sounds++;
}
static void fl(void *c, uint32_t ms)
{
    (void) c;
    flashes++;
    flash_ms = ms;
}

static void test_alert_audio(void)
{
    zui_motion_reset();
    zui_alert_out_t o = {snd, fl, 0};
    CHECK(!zui_motion_alert(&o) && sounds == 1 && flashes == 0, "default: the sound");
    zui_motion_set_flag(ZUI_PREF_FLASH_ALERT, true);
    CHECK(zui_motion_alert(&o) && sounds == 1 && flashes == 1, "flash instead of the sound");
    CHECK(flash_ms == ZUI_MOTION_FLASH_MS, "one flash, ZUI_MOTION_FLASH_MS long");
    zui_alert_out_t nf = {snd, 0, 0};
    CHECK(!zui_motion_alert(&nf) && sounds == 2, "no flash available: the sound still plays");
    zui_alert_out_t none = {0, 0, 0};
    CHECK(!zui_motion_alert(&none) && !zui_motion_alert(0), "nothing to use: harmless");

    int16_t a[8] = {100, -100, 3, 4, -3, -4, 32767, 32767};
    int16_t b[8];
    memcpy(b, a, sizeof a);
    CHECK(!zui_motion_mix_mono(b, 4) && memcmp(a, b, sizeof a) == 0, "stereo left alone");
    zui_motion_set_flag(ZUI_PREF_MONO_AUDIO, true);
    CHECK(zui_motion_mix_mono(b, 4), "mono mixes");
    CHECK(b[0] == 0 && b[1] == 0, "opposite cancel");
    CHECK(b[2] == 3 && b[3] == 3, "3,4 -> 3 (floor)");
    CHECK(b[4] == -4 && b[5] == -4, "-3,-4 -> -4 (floor)");
    CHECK(b[6] == 32767 && b[7] == 32767, "full scale stays without overflow");
    int16_t lo[2] = {-32768, -32768};
    zui_motion_mix_mono(lo, 1);
    CHECK(lo[0] == -32768 && lo[1] == -32768, "negative full scale");
    CHECK(!zui_motion_mix_mono(0, 4), "NULL buffer refused");
    zui_motion_reset();
}

int main(void)
{
    test_defaults();
    test_set_and_clamp();
    test_watch();
    test_animation();
    test_text_and_more();
    test_alert_audio();
    printf("%s test_zui_motion: %d/%d checks\n", fails ? "[FAIL]" : "[PASS]", checks - fails,
           checks);
    return fails ? 1 : 0;
}
