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
        CHECK(t && t->reveal == ZXL_REVEAL_VAULT, "every app opens with the boot's vault");
        CHECK(t && t->ambient < ZXL_AMB_COUNT && t->ambient != ZXL_AMB_NONE, "ambient set");
        CHECK(t && t->octave >= 3 && t->octave <= 5, "octave sane");
    }
    CHECK(zxl_group_theme(0) == 0, "group 0 has no default");
    CHECK(zxl_group_theme(13) == 0, "group 13 has no default");
    CHECK(zxl_theme_for("Nobody", 0) == zxl_group_theme(1), "bad group falls back to group 1");
    CHECK(zxl_theme_for("Nobody", 99) == zxl_group_theme(1),
          "bad group falls back to group 1 (high)");
    CHECK(zxl_theme_for(0, 5) == zxl_group_theme(5), "NULL app uses its group");
    CHECK(zxl_theme_for("Warble", 5) != zxl_group_theme(5), "Warble has its own sequence");
    CHECK(zxl_theme_for("NotAnApp", 5) == zxl_group_theme(5), "an app off the list uses its group");
    CHECK(zxl_theme_for("NotAnApp", 5)->ambient == ZXL_AMB_WATER,
          "media group has water behind it");

    /* every app on the list has its own entry, in list order */
    CHECK(zxl_curated_count() == ZXL_APPS, "all 407 apps have their own entry");
    CHECK(zxl_curated(ZXL_APPS) == 0, "past the end is NULL");
    static const struct {
        uint32_t at;
        const char *name;
    } hand[] = {{0, "Banker"},    {12, "Flint"},       {26, "Commons"},
                {55, "Cutwater"}, {60, "Murmuration"}, {72, "Coffer"}};
    for (uint32_t i = 0; i < 6; i++) {
        const zxl_theme_t *t = zxl_curated(hand[i].at);
        CHECK(t && strcmp(t->app, hand[i].name) == 0, "list order");
        CHECK(zxl_theme_for(hand[i].name, 1) == t, "found by name whatever the group");
    }
    CHECK(strcmp(zxl_curated(143)->app, "Stillwater") == 0, "the list ends on Stillwater");
    uint32_t in_e = 0;
    for (uint32_t i = 0; i < ZXL_APPS; i++) {
        const zxl_theme_t *a = zxl_curated(i), *g = zxl_group_theme(a->group);
        CHECK(g != 0, "each app is in a real group");
        if (i < ZXL_TIER1) CHECK(a->group == 1 + i / 12, "tier 1: twelve apps a group, in order");
        CHECK(g && a->timbre == g->timbre && a->ambient == g->ambient,
              "an app sounds and moves like its group");
        CHECK(a->reveal == ZXL_REVEAL_VAULT, "every app opens with the boot's vault");
        CHECK(a->motif < ZXL_MOTIFS && a->key <= ZXL_B, "motif and key in range");
        CHECK(a->octave >= 3 && a->octave <= 6, "octave sane");
        CHECK(zxl_theme_for(a->app, 0) == a, "every app found by its name");
        CHECK(zxl_theme_pick(a->app) == a, "every app's sequence can be picked");
        in_e += a->key == ZXL_BOOT_KEY;
        for (uint32_t j = i + 1; j < ZXL_APPS; j++) {
            const zxl_theme_t *b = zxl_curated(j);
            if (a->key == b->key && a->octave == b->octave && a->timbre == b->timbre &&
                a->motif == b->motif)
                CHECK(0, "no two apps share a sound sequence");
            if (j < ZXL_TIER1 && a->group == b->group && a->key == b->key && a->octave == b->octave)
                CHECK(0, "no two tier-1 apps in a group share a scale");
            if (strcmp(a->app, b->app) == 0) CHECK(0, "names are unique");
        }
    }
    CHECK(in_e == 1, "only the swarm is in the boot's key");
    CHECK(strcmp(zxl_curated(ZXL_TIER1)->app, "Cartulary") == 0, "tier 2 follows tier 1");
    CHECK(strcmp(zxl_curated(ZXL_APPS - 1)->app, "Chorale") == 0, "the catalogue ends on app 407");
    uint32_t per[ZXL_GROUPS + 1] = {0};
    for (uint32_t i = 0; i < ZXL_APPS; i++) per[zxl_curated(i)->group]++;
    for (uint32_t g = 1; g <= ZXL_GROUPS; g++)
        CHECK(per[g] >= 12, "every family has its twelve and more");
    CHECK(zxl_theme_for("banker", 1) == zxl_group_theme(1), "names are case-sensitive");
    CHECK(zxl_theme_for("Bankerx", 6) == zxl_group_theme(6), "no prefix match");
    CHECK(zxl_theme_for("Burro", 6) == zxl_group_theme(6), "no short match");
    CHECK(zxl_theme_for("", 4) == zxl_group_theme(4), "empty name uses its group");
    const zxl_theme_t *m = zxl_theme_for("Murmuration", 6);
    CHECK(m->key == ZXL_E, "the swarm shares the OS key");
    CHECK(m->reveal == ZXL_REVEAL_VAULT && m->ambient == ZXL_AMB_FLOCK,
          "the swarm: the vault, a flock behind");
    CHECK(zxl_theme_for("Banker", 1)->reveal == ZXL_REVEAL_VAULT &&
              zxl_theme_for("Banker", 1)->ambient == ZXL_AMB_ARCHES,
          "vault, own ambient");
    CHECK(zxl_theme_for("Flint", 2)->reveal == ZXL_REVEAL_VAULT &&
              zxl_theme_for("Flint", 2)->ambient == ZXL_AMB_SPARKS,
          "vault, own ambient");
    CHECK(zxl_theme_for("Coffer", 7)->reveal == ZXL_REVEAL_VAULT &&
              zxl_theme_for("Coffer", 7)->ambient == ZXL_AMB_DUST,
          "vault, own ambient");
    CHECK(zxl_theme_for("Cutwater", 5)->reveal == ZXL_REVEAL_VAULT &&
              zxl_theme_for("Cutwater", 5)->ambient == ZXL_AMB_WATER,
          "vault, own ambient");
    CHECK(zxl_theme_for("Commons", 3)->reveal == ZXL_REVEAL_VAULT &&
              zxl_theme_for("Commons", 3)->ambient == ZXL_AMB_RINGS,
          "vault, own ambient");
    CHECK(zxl_theme_for("Commons", 3)->key == ZXL_F, "Commons in F");
    CHECK(zxl_theme_for("Coffer", 7)->timbre == ZXL_TIMBRE_COIN, "Coffer rings coins");
    for (uint32_t i = 0; i < 6; i++)
        for (uint32_t j = i + 1; j < 6; j++) {
            const zxl_theme_t *a = zxl_curated(hand[i].at), *b = zxl_curated(hand[j].at);
            CHECK(a->ambient != b->ambient, "each hand-made app has its own motion behind it");
        }
}

static void test_notes(void)
{
    const zxl_theme_t *b = zxl_theme_for("Banker", 1); /* D4 = MIDI 62 */
    CHECK(zxl_tick_note(b, 0) == 62, "Banker step 0 is D4");
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
    const zxl_theme_t *t = zxl_theme_for("Banker", 1);
    CHECK(zxl_effective_reveal(t, &p) == ZXL_REVEAL_VAULT, "theme reveal: the vault");
    CHECK(zxl_effective_reveal(t, 0) == ZXL_REVEAL_VAULT, "NULL prefs keep the theme reveal");
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
    const zxl_theme_t *t = zxl_theme_for("Banker", 1);
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

/* every app in the catalogue knows its icon file; nothing else does */
static void test_icon_files(void)
{
    uint32_t named = 0, tier = 0;
    for (uint32_t i = 0; i < zxl_curated_count(); i++) {
        const char *f = zxl_icon_file(zxl_curated(i)->app);
        if (!f) continue;
        named++;
        size_t n = strlen(f);
        if (n > 4 && !strcmp(f + n - 4, ".png") && f[0] != '/' && !strstr(f, "..")) tier++;
    }
    CHECK(named == ZXL_APPS, "all 407 apps have an icon file");
    CHECK(tier == ZXL_APPS, "each a .png inside the icons folder");
    CHECK(!strcmp(zxl_icon_file("Banker"), "001-burrow.png"),
          "the Banker keeps the icon drawn as 001-burrow.png");
    CHECK(!strcmp(zxl_icon_file("Cartulary"), "l2/145-dragon-archivist.png"),
          "a tier-2 app's is in l2/");
    CHECK(!strcmp(zxl_icon_file("Chorale"), zxl_icon_file("Chorale")) &&
              strncmp(zxl_icon_file("Chorale"), "l3/407-", 7) == 0,
          "the last app's is in l3/");
    CHECK(zxl_icon_file("My Tool") == 0 && zxl_icon_file(0) == 0, "an app a person made: none");
}

static void test_pick(void)
{
    CHECK(zxl_theme_pick("Banker") == zxl_theme_for("Banker", 1), "pick a curated sequence");
    CHECK(zxl_theme_pick("banker") == zxl_theme_for("Banker", 1), "pick ignores case");
    CHECK(zxl_theme_pick("MURMURATION")->key == ZXL_E, "the swarm's sequence is in E");
    for (uint32_t i = 0; i < zxl_curated_count(); i++)
        CHECK(zxl_theme_pick(zxl_curated(i)->app) == zxl_curated(i),
              "every curated sequence pickable");
    CHECK(zxl_theme_pick("group1") == zxl_group_theme(1), "group1");
    CHECK(zxl_theme_pick("Group7") == zxl_group_theme(7), "Group7");
    CHECK(zxl_theme_pick("group12") == zxl_group_theme(12), "group12");
    CHECK(zxl_theme_pick("group0") == 0, "group0 is not a sequence");
    CHECK(zxl_theme_pick("group13") == 0, "group13 is not a sequence");
    CHECK(zxl_theme_pick("group") == 0, "bare group");
    CHECK(zxl_theme_pick("group1x") == 0, "trailing junk");
    CHECK(zxl_theme_pick("group123") == 0, "three digits");
    CHECK(zxl_theme_pick("groupA") == 0, "letter for a number");
    CHECK(zxl_theme_pick("Burro") == 0, "prefix of a name");
    CHECK(zxl_theme_pick("Bankers") == 0, "name plus more");
    CHECK(zxl_theme_pick("") == 0, "empty");
    CHECK(zxl_theme_pick(0) == 0, "NULL");
    CHECK(zxl_icon_ink(1) == ZXL_ICON_PALETTE, "system icons held to the palette");
    CHECK(zxl_icon_ink(0) == ZXL_ICON_OWN, "a person's app keeps its own colours");
    CHECK(ZXL_GOLD_RGB == 0xF2C230u && ZXL_GROUND_RGB == 0x002911u, "the boot's palette");
    CHECK(ZXL_TILES == 5 && ZXL_TILES <= ZXL_MAX_PARTS, "five tiles, as the boot");
}

/* the drawing decides when a tile lights (zui's launch screen) */
static void test_screen_lit(void)
{
    zxl_launch_t l;
    zxl_event_t ev[16];
    const zxl_theme_t *t = zxl_theme_for("Banker", 1);
    zxl_launch_init(&l, t, 5, 0, 0);
    CHECK(drain(&l, ev, 16) == 1 && ev[0].kind == ZXL_EV_BEGIN, "begin first");
    CHECK(zxl_launch_lit(&l, 4) == 0, "an outer tile lights");
    CHECK(drain(&l, ev, 16) == 1 && ev[0].kind == ZXL_EV_TICK && ev[0].part == 4 &&
              ev[0].step == 0 && ev[0].note == zxl_tick_note(t, 0),
          "first lit plays the first step");
    CHECK(zxl_launch_lit(&l, 4) == -1, "a tile lights once");
    CHECK(zxl_launch_lit(&l, 5) == -1, "no sixth tile");
    CHECK(zxl_launch_lit(0, 0) == -1, "NULL launch");
    CHECK(zxl_launch_lit(&l, 0) == 0 && zxl_launch_lit(&l, 1) == 0 && zxl_launch_lit(&l, 3) == 0,
          "the rest of the outer tiles");
    CHECK(drain(&l, ev, 16) == 3 && ev[0].step == 1 && ev[1].step == 2 && ev[2].step == 3,
          "each lit tile the next step");
    CHECK(zxl_launch_lit(&l, 2) == 0, "the centre lights");
    CHECK(drain(&l, ev, 16) == 2, "the centre's tick, then the mark");
    CHECK(ev[0].kind == ZXL_EV_TICK && ev[0].part == 2 && ev[0].step == ZXL_TOP_STEP,
          "the centre plays the top step");
    CHECK(ev[1].kind == ZXL_EV_LIGHT && !ev[1].audible, "the mark lights silently");
    CHECK(zxl_launch_lit(&l, 0) == -1, "nothing lights after gathering");
    CHECK(drain(&l, ev, 16) == 0, "no END before the app is ready");
    zxl_launch_finish(&l);
    CHECK(drain(&l, ev, 16) == 1 && ev[0].kind == ZXL_EV_END && ev[0].audible, "ready: END");
    zxl_launch_finish(&l);
    CHECK(drain(&l, ev, 16) == 0, "END once");

    /* a window off screen while it gathered: finish lights the rest silently */
    zxl_launch_init(&l, t, 5, 0, 0);
    zxl_launch_lit(&l, 0);
    drain(&l, ev, 16);
    zxl_launch_finish(&l);
    int n = drain(&l, ev, 16);
    CHECK(n == 2 && ev[0].kind == ZXL_EV_LIGHT && ev[1].kind == ZXL_EV_END,
          "no burst of ticks, just the mark and the end");
    CHECK(l.lit == 0x1Fu && l.phase == ZXL_PH_OPEN, "every tile lit, open");
    zxl_launch_finish(0);

    /* finish straight after BEGIN, and mixing progress with lit tiles */
    zxl_launch_init(&l, t, 3, 0, 0);
    zxl_launch_finish(&l);
    n = drain(&l, ev, 16);
    CHECK(n == 3 && ev[0].kind == ZXL_EV_BEGIN && ev[1].kind == ZXL_EV_LIGHT &&
              ev[2].kind == ZXL_EV_END,
          "finish at once: begin, mark, end");
    zxl_launch_init(&l, t, 3, 0, 0);
    drain(&l, ev, 16);
    zxl_launch_lit(&l, 0);
    zxl_launch_part(&l, 2, 256);
    zxl_launch_part(&l, 1, 256);
    n = drain(&l, ev, 16);
    CHECK(n == 4 && ev[0].step == 0 && ev[1].step == 1 && ev[2].step == ZXL_TOP_STEP &&
              ev[3].kind == ZXL_EV_LIGHT,
          "lit tiles and real progress share the steps");
    zxl_launch_rest(&l, 256);
    CHECK(drain(&l, ev, 16) == 1 && ev[0].kind == ZXL_EV_END, "and the rest ends it");

    /* a warm launch is already open */
    zxl_launch_init(&l, t, 5, 0, 1);
    drain(&l, ev, 16);
    CHECK(zxl_launch_lit(&l, 0) == -1, "warm: nothing to light");
    zxl_launch_finish(&l);
    CHECK(drain(&l, ev, 16) == 0, "warm: END already played");

    /* the mute setting reaches lit ticks too */
    zxl_prefs_t p = zxl_prefs_default();
    p.sounds = 0;
    zxl_launch_init(&l, t, 1, &p, 0);
    drain(&l, ev, 16);
    zxl_launch_lit(&l, 0);
    CHECK(drain(&l, ev, 16) == 2 && !ev[0].audible && ev[0].gain_q8 == 0, "muted tick");
}

/* the real load from fabrication: verified blocks fill the tiles, the build
 * breathes, only "done" ends it, a failure never does */
static void test_fab_blocks(void)
{
    zxl_launch_t l;
    zxl_event_t ev[16];
    int n;
    const zxl_theme_t *t = zxl_theme_for("Banker", 1);
    zxl_launch_init(&l, t, 5, 0, 0);
    drain(&l, ev, 16); /* BEGIN */
    CHECK(zxl_launch_screen_p(&l) == 0, "fab: nothing fetched, the bar at 0");

    /* 30 blocks over 3 levels: the outer pair [0,10), the inner pair
     * [10,20), the centre [20,30) */
    CHECK(zxl_launch_blocks(&l, 0, 30) == 0, "fab: the start's answer");
    CHECK(drain(&l, ev, 16) == 0 && zxl_launch_screen_p(&l) == 0, "fab: 0 of 30: nothing moves");
    zxl_launch_blocks(&l, 5, 30);
    CHECK(l.part_p[0] == 128 && l.part_p[4] == 128 && l.part_p[1] == 0 && l.part_p[2] == 0,
          "fab: half the outer pair's blocks, the outer pair half full, together");
    CHECK(zxl_launch_screen_p(&l) == 21, "fab: the bar at 5/30 of the gathering (42/2)");
    CHECK(drain(&l, ev, 16) == 0, "fab: no tile lit yet");
    zxl_launch_blocks(&l, 10, 30);
    n = drain(&l, ev, 16);
    CHECK(n == 2 && ev[0].kind == ZXL_EV_TICK && ev[0].part == 0 && ev[1].part == 4,
          "fab: the outer pair's blocks in: both outer tiles tick");
    CHECK(zxl_launch_screen_p(&l) == 42, "fab: the bar at 10/30 (85/2)");
    zxl_launch_blocks(&l, 3, 30);
    CHECK(l.part_p[0] == 256 && l.load_p == 85, "fab: counts never move backwards");
    zxl_launch_blocks(&l, 25, 30);
    n = drain(&l, ev, 16);
    CHECK(n == 2 && ev[0].part == 1 && ev[1].part == 3 && l.part_p[2] == 128,
          "fab: the inner pair ticks, the centre half full");
    CHECK(!(l.lit & 4) && l.phase == ZXL_PH_GATHER && l.rest != ZXL_UNKNOWN,
          "fab: the centre waits for every block");
    CHECK(zxl_launch_screen_p(&l) == 106, "fab: the bar at 25/30 (213/2)");

    zxl_launch_blocks(&l, 30, 30);
    n = drain(&l, ev, 16);
    CHECK(n == 2 && ev[0].kind == ZXL_EV_TICK && ev[0].part == 2 && ev[0].step == ZXL_TOP_STEP &&
              ev[1].kind == ZXL_EV_LIGHT,
          "fab: every block in: the centre's top step, the mark lights");
    CHECK(l.phase == ZXL_PH_RELEASE && l.rest == ZXL_UNKNOWN,
          "fab: the build runs for an unknown time");
    CHECK(zxl_launch_screen_p(&l) == 192, "fab: mark lit, the bar above it breathing");
    zxl_launch_blocks(&l, 40, 30);
    CHECK(drain(&l, ev, 16) == 0 && l.phase == ZXL_PH_RELEASE,
          "fab: blocks alone never end the launch");
    zxl_launch_finish(&l); /* phase "done" */
    n = drain(&l, ev, 16);
    CHECK(n == 1 && ev[0].kind == ZXL_EV_END && zxl_launch_screen_p(&l) == 256,
          "fab: done: the end, the doors");
    CHECK(zxl_launch_blocks(&l, 30, 30) == -1, "fab: nothing after the end");

    /* a failed step or a cancel never ends it */
    zxl_launch_init(&l, t, 5, 0, 0);
    drain(&l, ev, 16);
    zxl_launch_blocks(&l, 10, 30);
    drain(&l, ev, 16);
    zxl_launch_fail(&l);
    zxl_launch_finish(&l);
    CHECK(drain(&l, ev, 16) == 0 && l.phase == ZXL_PH_GATHER, "fab: failed: finish refused");
    CHECK(zxl_launch_blocks(&l, 30, 30) == -1 && l.part_p[2] == 0,
          "fab: failed: later answers ignored");
    CHECK(zxl_launch_screen_p(&l) == 42, "fab: failed: the screen stays where the work stopped");
    zxl_launch_fail(0); /* no crash */

    /* failing after every block is in, while it builds */
    zxl_launch_init(&l, t, 5, 0, 0);
    zxl_launch_blocks(&l, 30, 30);
    zxl_launch_fail(&l);
    zxl_launch_finish(&l);
    drain(&l, ev, 16);
    CHECK(l.phase == ZXL_PH_RELEASE && zxl_launch_screen_p(&l) == 192,
          "fab: a failed build never opens");

    /* warm: only the end; no blocks taken */
    zxl_launch_init(&l, t, 5, 0, 1);
    CHECK(zxl_launch_blocks(&l, 1, 30) == -1 && zxl_launch_screen_p(&l) == 256,
          "fab: warm takes no blocks");
    n = drain(&l, ev, 16);
    CHECK(n == 1 && ev[0].kind == ZXL_EV_END, "fab: warm plays only the end");

    /* nothing to fetch */
    zxl_launch_init(&l, t, 5, 0, 0);
    zxl_launch_blocks(&l, 0, 0);
    CHECK(l.phase == ZXL_PH_RELEASE && l.rest == ZXL_UNKNOWN && zxl_launch_screen_p(&l) == 192,
          "fab: 0 blocks: straight to the build");

    /* the screen lit the centre early (its gold came up a little before the
     * last blocks): the bar still waits for the blocks */
    zxl_launch_init(&l, t, 5, 0, 0);
    zxl_launch_blocks(&l, 28, 30);
    for (uint32_t i = 0; i < 5; i++) zxl_launch_lit(&l, i);
    CHECK(l.phase == ZXL_PH_RELEASE, "fab: every tile lit on screen");
    CHECK(zxl_launch_screen_p(&l) == 119, "fab: the bar stays at 28/30 (238/2), not the mark");
    zxl_launch_blocks(&l, 30, 30);
    CHECK(zxl_launch_screen_p(&l) == 192, "fab: then the build");

    /* huge counts, odd splits */
    zxl_launch_init(&l, t, 5, 0, 0);
    zxl_launch_blocks(&l, 4000000000u, 0xFFFFFFFFu);
    CHECK(l.part_p[0] == 256 && l.part_p[1] == 256 && l.part_p[2] > 200 && l.part_p[2] < 256,
          "fab: 4e9 of 2^32-1 blocks without overflow");
    zxl_launch_init(&l, t, 4, 0, 0);
    zxl_launch_blocks(&l, 1, 3); /* 2 levels: [0,1), [1,3) */
    CHECK(l.part_p[0] == 256 && l.part_p[3] == 256 && l.part_p[1] == 0,
          "fab: an even part count pairs inward too");
    zxl_launch_init(&l, t, 1, 0, 0);
    zxl_launch_blocks(&l, 1, 2);
    CHECK(l.part_p[0] == 128, "fab: one part takes every block");
    CHECK(zxl_launch_blocks(0, 1, 1) == -1 && zxl_launch_screen_p(0) == 0, "fab: NULL");
}

int main(void)
{
    test_themes();
    test_icon_files();
    test_notes();
    test_prefs();
    test_sequence();
    test_rest_before_parts();
    test_shapes();
    test_warm();
    test_sound_policy();
    test_pick();
    test_screen_lit();
    test_fab_blocks();
    printf("test_zxl_launch: %d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
