/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zxl_launch.c — app launch screens: themes, sound policy, and a
 * sequence that only moves when the real load does. Exact expected values. */
#include <stdio.h>
#include <string.h>
#include "zxl_launch.h"

static int pass, fail;
#define CHECK(c, msg)                                                                              \
    do {                                                                                           \
        if (c)                                                                                     \
            pass++;                                                                                \
        else {                                                                                     \
            fail++;                                                                                \
            printf("[FAIL] %s:%d %s\n", __FILE__, __LINE__, msg);                                  \
        }                                                                                          \
    } while (0)

static int drain(zxl_launch_t *l, zxl_event_t *out, int max)
{
    int n = 0;
    zxl_event_t e;
    while (n < max && zxl_launch_next(l, &e)) out[n++] = e;
    return n;
}

static void test_themes(void)
{
    /* every group has a default, numbered 1..12, none in the boot's key */
    for (uint32_t g = 1; g <= ZXL_GROUPS; g++) {
        const zxl_theme_t *t = zxl_group_theme(g);
        CHECK(t != 0, "group default exists");
        CHECK(t && t->group == g, "group default carries its group");
        CHECK(t && t->app == 0, "group default has no app name");
        CHECK(t && t->key != ZXL_BOOT_KEY, "group default avoids the boot key");
        CHECK(t && t->key <= ZXL_B, "key is a pitch class");
        CHECK(t && t->timbre < ZXL_TIMBRE_COUNT, "timbre in range");
        CHECK(t && t->reveal < ZXL_REVEAL_FADE, "group reveal is a themed one, not the fade");
        CHECK(t && t->reveal != ZXL_REVEAL_VAULT, "the vault stays the boot's");
        CHECK(t && t->ambient < ZXL_AMB_COUNT && t->ambient != ZXL_AMB_NONE, "ambient set");
        CHECK(t && t->octave >= 3 && t->octave <= 5, "octave sane");
    }
    CHECK(zxl_group_theme(0) == 0, "group 0 has no default");
    CHECK(zxl_group_theme(13) == 0, "group 13 has no default");
    CHECK(zxl_theme_for("Nobody", 0) == zxl_group_theme(1), "bad group falls back to group 1");
    CHECK(zxl_theme_for("Nobody", 99) == zxl_group_theme(1),
          "bad group falls back to group 1 (high)");
    CHECK(zxl_theme_for(0, 5) == zxl_group_theme(5), "NULL app uses its group");
    CHECK(zxl_theme_for("Warble", 5) == zxl_group_theme(5), "uncurated app uses its group");
    CHECK(zxl_theme_for("Gesso", 5)->reveal == ZXL_REVEAL_WAKE, "media group opens like water");

    /* the six curated apps */
    CHECK(zxl_curated_count() == 6, "six curated apps");
    CHECK(zxl_curated(6) == 0, "curated past the end is NULL");
    static const char *names[] = {"Burrow",   "Flint",       "Commons",
                                  "Cutwater", "Murmuration", "Coffer"};
    for (uint32_t i = 0; i < 6; i++) {
        const zxl_theme_t *t = zxl_curated(i);
        CHECK(t && strcmp(t->app, names[i]) == 0, "curated order");
        CHECK(zxl_theme_for(names[i], 1) == t, "curated found by name whatever the group");
        CHECK(t && zxl_group_theme(t->group) != 0, "curated app is in a real group");
    }
    CHECK(zxl_theme_for("burrow", 1) == zxl_group_theme(1), "names are case-sensitive");
    CHECK(zxl_theme_for("Burrowx", 6) == zxl_group_theme(6), "no prefix match");
    CHECK(zxl_theme_for("Burro", 6) == zxl_group_theme(6), "no short match");
    CHECK(zxl_theme_for("", 4) == zxl_group_theme(4), "empty name uses its group");
    const zxl_theme_t *m = zxl_theme_for("Murmuration", 6);
    CHECK(m->key == ZXL_E, "the swarm shares the OS key");
    CHECK(m->reveal == ZXL_REVEAL_SCATTER && m->ambient == ZXL_AMB_FLOCK,
          "the swarm scatters like a flock");
    CHECK(zxl_theme_for("Burrow", 1)->reveal == ZXL_REVEAL_IRIS, "Burrow opens like a tunnel");
    CHECK(zxl_theme_for("Flint", 2)->reveal == ZXL_REVEAL_SCANLINE, "Flint opens like a CRT");
    CHECK(zxl_theme_for("Coffer", 7)->reveal == ZXL_REVEAL_LID, "Coffer lifts its lid");
    CHECK(zxl_theme_for("Cutwater", 5)->reveal == ZXL_REVEAL_WAKE, "Cutwater parts the water");
    CHECK(zxl_theme_for("Commons", 3)->reveal == ZXL_REVEAL_PETALS, "Commons opens four rooms");
    CHECK(zxl_theme_for("Commons", 3)->key == ZXL_F, "Commons in F");
    CHECK(zxl_theme_for("Coffer", 7)->timbre == ZXL_TIMBRE_COIN, "Coffer rings coins");
    for (uint32_t i = 0; i < 6; i++)
        for (uint32_t j = i + 1; j < 6; j++) {
            const zxl_theme_t *a = zxl_curated(i), *b = zxl_curated(j);
            CHECK(a->reveal != b->reveal, "each curated app opens its own way");
            CHECK(a->key != b->key || a->timbre != b->timbre,
                  "each curated app sounds like itself");
        }
}

static void test_notes(void)
{
    const zxl_theme_t *b = zxl_theme_for("Burrow", 1); /* D4 = MIDI 62 */
    CHECK(zxl_tick_note(b, 0) == 62, "Burrow step 0 is D4");
    CHECK(zxl_tick_note(b, 1) == 64, "step 1 is E4");
    CHECK(zxl_tick_note(b, 2) == 66, "step 2 is F#4");
    CHECK(zxl_tick_note(b, 3) == 69, "step 3 is A4");
    CHECK(zxl_tick_note(b, 4) == 71, "step 4 is B4");
    CHECK(zxl_tick_note(b, 5) == 74, "step 5 is D5");
    CHECK(zxl_tick_note(b, ZXL_TOP_STEP) == 76, "top step is E5");
    CHECK(zxl_tick_note(b, 99) == 76, "steps clamp to the top");
    CHECK(zxl_tick_note(0, 0) == 0, "NULL theme gives 0");
    const zxl_theme_t *m = zxl_theme_for("Murmuration", 6); /* E5 = 76 */
    CHECK(zxl_tick_note(m, 0) == 76, "Murmuration starts on E5");
    CHECK(zxl_tick_note(m, ZXL_TOP_STEP) == 90, "Murmuration tops out on F#6");
    const zxl_theme_t *c = zxl_theme_for("Coffer", 7); /* G5 = 79 */
    CHECK(zxl_tick_note(c, 0) == 79, "Coffer starts on G5");
    for (uint32_t s = 0; s < ZXL_TOP_STEP; s++)
        CHECK(zxl_tick_note(c, s + 1) > zxl_tick_note(c, s), "the ticks climb");
}

static void test_prefs(void)
{
    zxl_prefs_t p = zxl_prefs_default();
    CHECK(p.sounds == 1, "launch sounds on by default");
    CHECK(p.volume_q8 == 256, "full volume by default");
    CHECK(!p.system_muted && !p.focus_quiet && !p.screen_reader && !p.reduce_motion,
          "flags off by default");
    CHECK(strcmp(ZXL_SETTING_KEY, "sound.app_launch") == 0, "setting key");
    const zxl_theme_t *t = zxl_theme_for("Burrow", 1);
    CHECK(zxl_effective_reveal(t, &p) == ZXL_REVEAL_IRIS, "theme reveal");
    CHECK(zxl_effective_reveal(t, 0) == ZXL_REVEAL_IRIS, "NULL prefs keep the theme reveal");
    p.reduce_motion = 1;
    CHECK(zxl_effective_reveal(t, &p) == ZXL_REVEAL_FADE, "reduce motion fades");
    CHECK(zxl_effective_reveal(0, &p) == ZXL_REVEAL_FADE, "reduce motion fades without a theme");
    p.reduce_motion = 0;
    CHECK(zxl_effective_reveal(0, &p) == ZXL_REVEAL_VAULT, "no theme falls back to the vault");
    zxl_theme_t bad = *t;
    bad.reveal = 200;
    CHECK(zxl_effective_reveal(&bad, &p) == ZXL_REVEAL_VAULT,
          "a bad reveal falls back to the vault");
    bad.reveal = ZXL_REVEAL_COUNT;
    CHECK(zxl_effective_reveal(&bad, &p) == ZXL_REVEAL_VAULT, "COUNT is out of range");
    bad.reveal = ZXL_REVEAL_FADE;
    CHECK(zxl_effective_reveal(&bad, &p) == ZXL_REVEAL_FADE, "FADE is in range");
}

/* A five-part load in the order a real one might finish. */
static void test_sequence(void)
{
    zxl_launch_t l;
    zxl_event_t ev[16];
    int n;
    const zxl_theme_t *t = zxl_theme_for("Burrow", 1);
    CHECK(zxl_launch_init(0, t, 5, 0, 0) == -1, "NULL launch refused");
    CHECK(zxl_launch_init(&l, 0, 5, 0, 0) == -1, "NULL theme refused");
    CHECK(zxl_launch_init(&l, t, 5, 0, 0) == 0, "init");
    CHECK(l.n == 5 && l.phase == ZXL_PH_GATHER && l.lit == 0 && l.nlit == 0, "fresh state");
    n = drain(&l, ev, 16);
    CHECK(n == 1 && ev[0].kind == ZXL_EV_BEGIN, "BEGIN first, alone");
    CHECK(ev[0].audible == 1 && ev[0].gain_q8 == 256 && ev[0].note == 0, "BEGIN audible at full");
    CHECK(zxl_launch_next(&l, &ev[0]) == 0, "queue empty");
    CHECK(zxl_launch_next(&l, 0) == 0, "NULL out refused");
    CHECK(zxl_launch_next(0, &ev[0]) == 0, "NULL launch has nothing");

    CHECK(zxl_launch_part(&l, 5, 256) == -1, "part past n refused");
    CHECK(zxl_launch_part(0, 0, 256) == -1, "NULL launch refused");

    /* part 0 done but the head has not reached it: 256/5 = 51 < 85 (rank 0 needs 256*1/6=42) */
    CHECK(zxl_launch_part(&l, 0, 256) == 0, "report part 0");
    CHECK(zxl_gather_p(&l) == 51, "gather is the mean");
    n = drain(&l, ev, 16);
    CHECK(n == 1 && ev[0].kind == ZXL_EV_TICK && ev[0].part == 0 && ev[0].step == 0,
          "part 0 lights: step 0");
    CHECK(ev[0].note == 62 && ev[0].audible && ev[0].gain_q8 == 256, "tick is D4, audible");

    /* the centre loads early: it must wait for everything */
    zxl_launch_part(&l, 2, 256);
    n = drain(&l, ev, 16);
    CHECK(n == 0, "the centre waits for the whole load");
    CHECK(!(l.lit & 4), "centre not lit");

    /* part 1 half loaded: nothing; progress never runs backwards */
    zxl_launch_part(&l, 1, 128);
    zxl_launch_part(&l, 1, 40);
    CHECK(l.part_p[1] == 128, "progress does not go backwards");
    CHECK(drain(&l, ev, 16) == 0, "half a part lights nothing");
    zxl_launch_part(&l, 1, 9999);
    CHECK(l.part_p[1] == 256, "progress clamps at full");
    /* g = (256*3)/5 = 153 >= rank 1 need 128 */
    n = drain(&l, ev, 16);
    CHECK(n == 1 && ev[0].part == 1 && ev[0].step == 1 && ev[0].note == 64, "part 1: step 1, E4");

    /* parts 3 and 4 finish in one report each */
    zxl_launch_part(&l, 4, 256);
    n = drain(&l, ev, 16);
    CHECK(n == 1 && ev[0].part == 4 && ev[0].step == 2, "part 4 next step");
    zxl_launch_part(&l, 3, 255);
    CHECK(drain(&l, ev, 16) == 0, "255 is not done");
    zxl_launch_part(&l, 3, 256);
    n = drain(&l, ev, 16);
    CHECK(n == 3, "part 3, the centre, and the mark");
    CHECK(ev[0].kind == ZXL_EV_TICK && ev[0].part == 2 && ev[0].step == ZXL_TOP_STEP,
          "centre: top step (index order)");
    CHECK(ev[0].note == 76, "centre tick is E5");
    CHECK(ev[1].kind == ZXL_EV_TICK && ev[1].part == 3 && ev[1].step == 3, "part 3: step 3");
    CHECK(ev[2].kind == ZXL_EV_LIGHT && ev[2].audible == 0 && ev[2].gain_q8 == 0,
          "the mark lights, silently");
    CHECK(l.phase == ZXL_PH_RELEASE && l.lit == 0x1F && l.nlit == 4, "release phase");

    /* the rest of the load: unknown, then real numbers, END only at full */
    zxl_launch_rest(&l, ZXL_UNKNOWN);
    CHECK(drain(&l, ev, 16) == 0 && l.rest == ZXL_UNKNOWN, "unknown: no END");
    zxl_launch_rest(&l, 100);
    CHECK(l.rest == 100 && drain(&l, ev, 16) == 0, "100: no END");
    zxl_launch_rest(&l, 50);
    CHECK(l.rest == 100, "rest does not go backwards");
    zxl_launch_rest(&l, 255);
    CHECK(drain(&l, ev, 16) == 0, "255: no END");
    zxl_launch_rest(&l, 1000);
    n = drain(&l, ev, 16);
    CHECK(n == 1 && ev[0].kind == ZXL_EV_END && ev[0].audible && ev[0].gain_q8 == 256,
          "END at full");
    CHECK(l.phase == ZXL_PH_OPEN, "open");
    zxl_launch_rest(&l, 256);
    zxl_launch_part(&l, 0, 256);
    CHECK(drain(&l, ev, 16) == 0, "END fires once");
    zxl_launch_rest(0, 256); /* no crash */
    CHECK(zxl_gather_p(0) == 0, "NULL gather is 0");
}

static void test_rest_before_parts(void)
{
    zxl_launch_t l;
    zxl_event_t ev[16];
    int n;
    zxl_launch_init(&l, zxl_theme_for("Flint", 2), 3, 0, 0);
    drain(&l, ev, 16);
    zxl_launch_rest(&l, 256); /* the rest is ready first */
    CHECK(drain(&l, ev, 16) == 0, "no END before the parts");
    zxl_launch_part(&l, 0, 256);
    zxl_launch_part(&l, 2, 256);
    n = drain(&l, ev, 16);
    CHECK(n == 2 && ev[0].part == 0 && ev[0].step == 0 && ev[1].part == 2 && ev[1].step == 1,
          "outer pair");
    zxl_launch_part(&l, 1, 256);
    n = drain(&l, ev, 16);
    CHECK(n == 3, "centre, light, end together");
    CHECK(ev[0].step == ZXL_TOP_STEP && ev[1].kind == ZXL_EV_LIGHT && ev[2].kind == ZXL_EV_END,
          "in order");
    CHECK(ev[0].note == 12 * 5 + ZXL_A + 14, "Flint's top tick");
}

static void test_shapes(void)
{
    zxl_launch_t l;
    zxl_event_t ev[16];
    int n;
    const zxl_theme_t *t = zxl_group_theme(4);
    /* one part: it is the centre */
    zxl_launch_init(&l, t, 1, 0, 0);
    drain(&l, ev, 16);
    zxl_launch_part(&l, 0, 256);
    n = drain(&l, ev, 16);
    CHECK(n == 2 && ev[0].step == ZXL_TOP_STEP && ev[1].kind == ZXL_EV_LIGHT,
          "single part is the centre");
    /* zero parts clamps to one, too many clamps to seven */
    zxl_launch_init(&l, t, 0, 0, 0);
    CHECK(l.n == 1, "0 parts -> 1");
    zxl_launch_init(&l, t, 40, 0, 0);
    CHECK(l.n == ZXL_MAX_PARTS, "40 parts -> 7");
    /* four parts: no true centre, the inner pair lights last at the top */
    zxl_launch_init(&l, t, 4, 0, 0);
    drain(&l, ev, 16);
    zxl_launch_part(&l, 1, 256);
    zxl_launch_part(&l, 2, 256);
    CHECK(drain(&l, ev, 16) == 0, "inner pair waits for the outer pair");
    zxl_launch_part(&l, 0, 256);
    n = drain(&l, ev, 16);
    CHECK(n == 1 && ev[0].part == 0 && ev[0].step == 0, "outer left at 192 >= 64");
    zxl_launch_part(&l, 3, 256);
    n = drain(&l, ev, 16);
    CHECK(n == 4, "three ticks and the light");
    CHECK(ev[0].part == 1 && ev[0].step == ZXL_TOP_STEP, "inner left top");
    CHECK(ev[1].part == 2 && ev[1].step == ZXL_TOP_STEP, "inner right top");
    CHECK(ev[2].part == 3 && ev[2].step == 1, "outer right step 1");
    CHECK(ev[3].kind == ZXL_EV_LIGHT, "light");
    /* seven parts all at once: six steps then the top; never more than the queue */
    zxl_launch_init(&l, t, 7, 0, 0);
    drain(&l, ev, 16);
    for (uint32_t i = 0; i < 7; i++) zxl_launch_part(&l, i, 256);
    zxl_launch_rest(&l, 256);
    n = drain(&l, ev, 16);
    CHECK(n == 9, "seven ticks, light, end");
    uint32_t seen = 0;
    for (int i = 0; i < 7; i++) {
        CHECK(ev[i].kind == ZXL_EV_TICK, "ticks first");
        seen |= 1u << ev[i].part;
        if (ev[i].part == 3)
            CHECK(ev[i].step == ZXL_TOP_STEP, "centre of seven at the top");
        else
            CHECK(ev[i].step < ZXL_TOP_STEP, "others below the top");
    }
    CHECK(seen == 0x7F, "every part lit once");
    /* rank thresholds for seven: inner = 3, rank 0 needs 32, rank 1 needs 96, rank 2 needs 160 */
    zxl_launch_init(&l, t, 7, 0, 0);
    drain(&l, ev, 16);
    zxl_launch_part(&l, 1, 256); /* g = 36: rank 1 needs 96 */
    CHECK(drain(&l, ev, 16) == 0, "rank 1 waits for 96");
    zxl_launch_part(&l, 0, 256); /* g = 73: rank 0 lights, rank 1 still waits */
    n = drain(&l, ev, 16);
    CHECK(n == 1 && ev[0].part == 0, "rank 0 at 73");
    zxl_launch_part(&l, 6, 256); /* g = 109: rank 0 right and rank 1 left */
    n = drain(&l, ev, 16);
    CHECK(n == 2 && ev[0].part == 1 && ev[0].step == 1 && ev[1].part == 6 && ev[1].step == 2,
          "rank 1 at 109, in index order");
    zxl_launch_part(&l, 2, 256); /* g = 146: rank 2 needs 160 */
    CHECK(drain(&l, ev, 16) == 0, "rank 2 waits for 160");
    zxl_launch_part(&l, 5, 256); /* g = 182 */
    n = drain(&l, ev, 16);
    CHECK(n == 2 && ev[0].part == 2 && ev[1].part == 5, "rank 1 right and rank 2 left at 182");
}

static void test_warm(void)
{
    zxl_launch_t l;
    zxl_event_t ev[16];
    int n;
    zxl_launch_init(&l, zxl_theme_for("Coffer", 7), 5, 0, 1);
    n = drain(&l, ev, 16);
    CHECK(n == 1 && ev[0].kind == ZXL_EV_END, "warm launch: END only");
    CHECK(l.warm == 1 && l.phase == ZXL_PH_OPEN && l.lit == 0x1F, "warm state");
    zxl_launch_part(&l, 0, 256);
    CHECK(drain(&l, ev, 16) == 0, "nothing after a warm open");
}

static void test_sound_policy(void)
{
    zxl_launch_t l;
    zxl_event_t ev[16];
    int n;
    const zxl_theme_t *t = zxl_theme_for("Commons", 3);
    zxl_prefs_t p = zxl_prefs_default();

    p.sounds = 0; /* the mute setting */
    zxl_launch_init(&l, t, 1, &p, 0);
    zxl_launch_part(&l, 0, 256);
    zxl_launch_rest(&l, 256);
    n = drain(&l, ev, 16);
    CHECK(n == 4, "muted: same events");
    for (int i = 0; i < n; i++) CHECK(!ev[i].audible && ev[i].gain_q8 == 0, "muted: silent");
    CHECK(ev[1].note == zxl_tick_note(t, ZXL_TOP_STEP), "muted ticks still carry their note");

    p = zxl_prefs_default();
    p.system_muted = 1;
    zxl_launch_init(&l, t, 1, &p, 0);
    drain(&l, ev, 16);
    CHECK(!ev[0].audible, "system mute silences");
    p = zxl_prefs_default();
    p.focus_quiet = 1;
    zxl_launch_init(&l, t, 1, &p, 0);
    drain(&l, ev, 16);
    CHECK(!ev[0].audible, "focus mode silences");
    p = zxl_prefs_default();
    p.volume_q8 = 0;
    zxl_launch_init(&l, t, 1, &p, 0);
    drain(&l, ev, 16);
    CHECK(!ev[0].audible && ev[0].gain_q8 == 0, "volume 0 is silent");
    p = zxl_prefs_default();
    p.volume_q8 = 128;
    zxl_launch_init(&l, t, 1, &p, 0);
    drain(&l, ev, 16);
    CHECK(ev[0].audible && ev[0].gain_q8 == 128, "volume follows");
    p = zxl_prefs_default();
    p.volume_q8 = 999;
    zxl_launch_init(&l, t, 1, &p, 0);
    drain(&l, ev, 16);
    CHECK(ev[0].gain_q8 == 256, "volume clamps");
    p = zxl_prefs_default();
    p.volume_q8 = 1;
    zxl_launch_init(&l, t, 1, &p, 0);
    drain(&l, ev, 16);
    CHECK(ev[0].audible && ev[0].gain_q8 == 1, "volume 1 still sounds");

    p = zxl_prefs_default();
    p.screen_reader = 1; /* never over speech */
    zxl_launch_init(&l, t, 3, &p, 0);
    for (uint32_t i = 0; i < 3; i++) zxl_launch_part(&l, i, 256);
    zxl_launch_rest(&l, 256);
    n = drain(&l, ev, 16);
    CHECK(n == 6, "begin, three ticks, light, end");
    CHECK(ev[0].kind == ZXL_EV_BEGIN && ev[0].audible && ev[0].gain_q8 == 96, "begin ducked");
    for (int i = 1; i <= 3; i++)
        CHECK(ev[i].kind == ZXL_EV_TICK && !ev[i].audible && ev[i].gain_q8 == 0, "ticks dropped");
    CHECK(ev[5].kind == ZXL_EV_END && ev[5].audible && ev[5].gain_q8 == 96, "end ducked");
    p.volume_q8 = 128;
    zxl_launch_init(&l, t, 1, &p, 0);
    drain(&l, ev, 16);
    CHECK(ev[0].gain_q8 == 48, "ducking scales with volume");
}

int main(void)
{
    test_themes();
    test_notes();
    test_prefs();
    test_sequence();
    test_rest_before_parts();
    test_shapes();
    test_warm();
    test_sound_policy();
    printf("test_zxl_launch: %d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
