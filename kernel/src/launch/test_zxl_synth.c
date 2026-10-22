/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zxl_synth.c — the launch sounds, made live: every app's cues sound,
 * at their level, in their key, each its own, and exactly when they are
 * called for. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "zxl_synth.h"

static int pass, fail;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        if (c)                                                                                     \
            pass++;                                                                                \
        else {                                                                                     \
            fail++;                                                                                \
            printf("[FAIL] %s:%d ", __FILE__, __LINE__);                                           \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

#define MAXF (48000 * 3)
static zxl_player_t P;
static int16_t buf[MAXF * 2];

static zxl_event_t ev_of(uint32_t kind, const zxl_theme_t *t, uint32_t step, uint32_t part,
                         uint32_t gain)
{
    zxl_event_t e;
    memset(&e, 0, sizeof e);
    e.kind = (uint8_t) kind;
    e.step = (uint8_t) step;
    e.part = (uint8_t) part;
    e.audible = 1;
    e.gain_q8 = (uint16_t) gain;
    e.note = (uint16_t) (kind == ZXL_EV_TICK ? zxl_tick_note(t, step) : 0);
    return e;
}

/* one cue on its own: returns its frames */
static uint32_t render_cue(const zxl_theme_t *t, uint32_t kind, uint32_t step, uint32_t part,
                           uint32_t gain)
{
    zxl_player_init(&P);
    zxl_event_t e = ev_of(kind, t, step, part, gain);
    uint32_t n = zxl_cue_frames(t, kind);
    zxl_player_play(&P, t, &e);
    P.prep.on = 0; /* one cue on its own: not its end's background preparation */
    zxl_player_render(&P, buf, n);
    return n;
}

static int32_t peak(uint32_t from, uint32_t to)
{
    int32_t m = 0;
    for (uint32_t i = 2 * from; i < 2 * to; i++) {
        int32_t v = buf[i] < 0 ? -buf[i] : buf[i];
        if (v > m) m = v;
    }
    return m;
}

static double energy(uint32_t from, uint32_t to, int ch)
{
    double e = 0;
    for (uint32_t i = from; i < to; i++) e += (double) buf[2 * i + ch] * buf[2 * i + ch];
    return e;
}

/* Goertzel: the power of frequency f over the first n frames (both channels) */
static double power_at(double f, uint32_t n)
{
    double w = 2 * 3.14159265358979323846 * f / 48000, c = 2 * cos(w), s1 = 0, s2 = 0;
    for (uint32_t i = 0; i < n; i++) {
        double h = 0.5 - 0.5 * cos(2 * 3.14159265358979323846 * i / n); /* Hann */
        double s = h * (buf[2 * i] + buf[2 * i + 1]) + c * s1 - s2;
        s2 = s1;
        s1 = s;
    }
    return s1 * s1 + s2 * s2 - c * s1 * s2;
}

static uint64_t fnv(uint32_t n)
{
    uint64_t h = 1469598103934665603ull;
    const uint8_t *b = (const uint8_t *) buf;
    for (uint32_t i = 0; i < n * 4; i++) h = (h ^ b[i]) * 1099511628211ull;
    return h;
}

static const char *HAND[6] = {"Banker", "Flint", "Murmuration", "Coffer", "Cutwater", "Commons"};

/* every cue of the six hand-made apps and a spread of the rest: the right
 * length, the reference's level, nothing clipped, never silent at its start */
static void test_levels(void)
{
    static const uint32_t KINDS[3] = {ZXL_EV_BEGIN, ZXL_EV_TICK, ZXL_EV_END};
    static const char *SOME[] = {"Banker",   "Flint",     "Murmuration", "Coffer",
                                 "Cutwater", "Commons",   "Hollow",      "Keystone",
                                 "Burin",    "Cartulary", "Chorale",     "Satchel"};
    for (uint32_t i = 0; i < sizeof SOME / sizeof SOME[0]; i++) {
        const zxl_theme_t *t = zxl_theme_pick(SOME[i]);
        for (int k = 0; k < 3; k++) {
            uint32_t n = render_cue(t, KINDS[k], 2, 1, 256);
            int hand = i < 6;
            uint32_t want = KINDS[k] == ZXL_EV_BEGIN ? 700 : KINDS[k] == ZXL_EV_END ? 850 : 450;
            if (hand && KINDS[k] == ZXL_EV_TICK) {
                static const uint32_t TP[6] = {450, 400, 400, 600, 400, 450};
                want = TP[i];
            }
            int32_t pk = peak(0, n), exp = (int32_t) (32767u * (want * 256u / 1000u) / 256u);
            CHECK(n >= 48000 / 5 && n <= 48000 * 22 / 10, "%s cue %d is %u frames", t->app,
                  KINDS[k], n);
            CHECK(pk >= exp - exp / 50 && pk <= exp + 1, "%s cue %d peaks at %d, the recipe's %d",
                  t->app, KINDS[k], pk, exp);
            CHECK(peak(0, 48000 / 20) > 0, "%s cue %d sounds from the start", t->app, KINDS[k]);
            CHECK(zxl_player_active(&P) == 0, "%s cue %d is over after its length", t->app,
                  KINDS[k]);
        }
    }
    /* the six are their own recipes: lengths as the reference's */
    CHECK(zxl_cue_frames(zxl_theme_pick("Flint"), ZXL_EV_TICK) == 12000, "Flint's tick is 0.25 s");
    CHECK(zxl_cue_frames(zxl_theme_pick("Cutwater"), ZXL_EV_END) == 100800,
          "Cutwater's end is 2.1 s");
    CHECK(zxl_cue_frames(zxl_theme_pick("Hollow"), ZXL_EV_BEGIN) == 67200,
          "every other begin is 1.4 s");
    CHECK(zxl_cue_frames(zxl_theme_pick("Hollow"), ZXL_EV_LIGHT) == 0, "LIGHT has no sound");
}

/* the tick is the note the launch asked for: its fundamental stands above
 * the semitones either side, for every app in the catalogue */
static void test_pitch(void)
{
    uint32_t ok = 0;
    for (uint32_t i = 0; i < ZXL_APPS; i++) {
        const zxl_theme_t *t = zxl_curated(i);
        render_cue(t, ZXL_EV_TICK, 0, 2, 256);
        double f = 440.0 * pow(2.0, ((double) zxl_tick_note(t, 0) - 69) / 12);
        int hand = 0;
        for (int h = 0; h < 6; h++) hand |= !strcmp(t->app, HAND[h]);
        if (hand) f = 0; /* the six tick up their own scales: checked below */
        if (!f) {
            ok++;
            continue;
        }
        uint32_t n = zxl_cue_frames(t, ZXL_EV_TICK);
        double on = power_at(f, n), lo = power_at(f / 1.0594630943592953, n),
               hi = power_at(f * 1.0594630943592953, n);
        if (on > 4 * lo && on > 4 * hi)
            ok++;
        else
            CHECK(0, "%s tick at %.1f Hz: %.3g vs %.3g / %.3g", t->app, f, on, lo, hi);
    }
    CHECK(ok == ZXL_APPS, "every app's tick sounds its own note (%u of %u)", ok, ZXL_APPS);
    /* the Banker's ticks climb D4 E4 F#4 A4 B4 D5, the centre E5 */
    static const double BURROW[7] = {293.66, 329.63, 369.99, 440.0, 493.88, 587.33, 659.26};
    for (uint32_t s = 0; s < 7; s++) {
        render_cue(zxl_theme_pick("Banker"), ZXL_EV_TICK, s, 2, 256);
        double on = power_at(BURROW[s], 9600), off = power_at(BURROW[s] * 1.0595, 9600);
        CHECK(on > 4 * off, "the Banker's tick %u is its own step", s);
    }
}

/* every app's begin and end are its own: no two of the 407 alike */
static void test_unique(void)
{
    static uint64_t hb[ZXL_APPS];
    uint32_t dup = 0;
    for (uint32_t i = 0; i < ZXL_APPS; i++) {
        uint32_t n = render_cue(zxl_curated(i), ZXL_EV_BEGIN, 0, 0, 256);
        hb[i] = fnv(n);
        for (uint32_t j = 0; j < i; j++) dup += hb[j] == hb[i];
    }
    CHECK(dup == 0, "407 begins, all different (%u alike)", dup);
    /* and the same cue twice is the same sound, sample for sample */
    uint32_t n = render_cue(zxl_theme_pick("Chorale"), ZXL_EV_END, 0, 0, 256);
    uint64_t a = fnv(n);
    render_cue(zxl_theme_pick("Chorale"), ZXL_EV_END, 0, 0, 256);
    CHECK(fnv(n) == a, "deterministic: the same end twice is identical");
}

/* panning: the k-th tile's tick sits where its tile is; a begin leans to its
 * motif's side */
static void test_pan(void)
{
    const zxl_theme_t *t = zxl_theme_pick("Hollow"); /* motif 10: leans right */
    uint32_t n = render_cue(t, ZXL_EV_TICK, 0, 0, 256);
    CHECK(energy(0, n, 0) > 3 * energy(0, n, 1), "the leftmost tile's tick is on the left");
    n = render_cue(t, ZXL_EV_TICK, 0, 4, 256);
    CHECK(energy(0, n, 1) > 3 * energy(0, n, 0), "the rightmost tile's tick is on the right");
    n = render_cue(t, ZXL_EV_TICK, 0, 2, 256);
    double l = energy(0, n, 0), r = energy(0, n, 1);
    CHECK(l < 1.2 * r && r < 1.2 * l, "the centre's tick is in the middle");
    n = render_cue(zxl_theme_pick("Satchel"), ZXL_EV_BEGIN, 0, 0, 256); /* motif 11: left */
    CHECK(energy(0, 48000 / 2, 0) > energy(0, 48000 / 2, 1), "an odd motif's call leans left");
    n = render_cue(t, ZXL_EV_BEGIN, 0, 0, 256);
    CHECK(energy(0, 48000 / 2, 1) > energy(0, 48000 / 2, 0), "an even motif's call leans right");
}

/* gain and the sound setting: the event's gain scales the cue; silent
 * events, LIGHT and nonsense never sound */
static void test_gain(void)
{
    const zxl_theme_t *t = zxl_theme_pick("Coffer");
    uint32_t n = render_cue(t, ZXL_EV_BEGIN, 0, 0, 256);
    int32_t full = peak(0, n);
    render_cue(t, ZXL_EV_BEGIN, 0, 0, ZXL_DUCK_Q8);
    int32_t duck = peak(0, n);
    CHECK(duck * 256 >= full * ZXL_DUCK_Q8 - 256 && duck * 256 <= full * ZXL_DUCK_Q8 + 256,
          "ducked under a screen reader: %d of %d", duck, full);
    zxl_player_init(&P);
    zxl_event_t e = ev_of(ZXL_EV_BEGIN, t, 0, 0, 256);
    e.audible = 0;
    CHECK(zxl_player_play(&P, t, &e) == -1 && !zxl_player_active(&P),
          "a silent event plays nothing");
    e = ev_of(ZXL_EV_LIGHT, t, 0, 0, 256);
    CHECK(zxl_player_play(&P, t, &e) == -1, "LIGHT plays nothing");
    e = ev_of(ZXL_EV_BEGIN, t, 0, 0, 0);
    CHECK(zxl_player_play(&P, t, &e) == -1, "gain 0 plays nothing");
    CHECK(zxl_player_play(&P, 0, &e) == -1 && zxl_player_play(0, t, &e) == -1, "NULLs refused");
    zxl_player_render(&P, buf, 4800);
    CHECK(peak(0, 4800) == 0, "nothing playing: silence");
}

/* LIVE: each cue starts the moment it is called for, however fast or slow
 * the load: a tick called 0.3 s into the begin sounds 0.3 s in, not at a
 * recorded time; a slow load spaces its ticks out */
static void test_live(void)
{
    const zxl_theme_t *t = zxl_theme_pick("Hollow");
    /* the onset itself: silence, then a tick exactly where it was called */
    zxl_player_init(&P);
    zxl_player_render(&P, buf, 7200);
    zxl_event_t e = ev_of(ZXL_EV_TICK, t, 3, 2, 256);
    zxl_player_play(&P, t, &e);
    zxl_player_render(&P, buf, 960);
    CHECK(peak(0, 48) > 0 || peak(0, 480) > 1000,
          "the tick starts in the very block it was called");
    /* a slow load: the gap between two ticks is the load's, to the block */
    zxl_player_init(&P);
    e = ev_of(ZXL_EV_TICK, t, 0, 0, 256);
    zxl_player_play(&P, t, &e);
    uint32_t total = 0, second = 0;
    for (uint32_t b = 0; b < 100; b++) { /* 100 blocks of 960 = 2 s */
        if (b == 60) {                   /* the next part is done 1.2 s later */
            e = ev_of(ZXL_EV_TICK, t, 1, 1, 256);
            zxl_player_play(&P, t, &e);
            second = total;
        }
        zxl_player_render(&P, buf, 960);
        if (b == 59) CHECK(peak(0, 960) < 200, "the first tick has died away before the second");
        if (b == 60)
            CHECK(peak(0, 960) > 5000, "the second tick sounds 1.2 s later, as the load did");
        total += 960;
    }
    CHECK(second == 57600, "called at 1.2 s");
}

/* more cues than room: the oldest gives way, never a crash or a pile-up */
static void test_overflow(void)
{
    const zxl_theme_t *t = zxl_theme_pick("Flint");
    zxl_player_init(&P);
    for (uint32_t k = 0; k < ZXL_SYN_CUES + 3; k++) {
        zxl_event_t e = ev_of(ZXL_EV_TICK, t, k % 7, k % 5, 256);
        CHECK(zxl_player_play(&P, t, &e) == 0, "cue %u starts", k);
    }
    CHECK(zxl_player_active(&P) == ZXL_SYN_CUES, "at most %u at once", ZXL_SYN_CUES);
    zxl_player_render(&P, buf, 48000);
    CHECK(zxl_player_active(&P) == 0, "all over a second later");
    int32_t pk = peak(0, 48000);
    CHECK(pk <= 32768 && pk > 1000, "mixed, held to full scale, never wrapping");
}

/* an end is ready when the app is: its loudness was worked out in the
 * background while the app loaded */
static void test_prepare(void)
{
    const zxl_theme_t *t = zxl_theme_pick("Cutwater");
    zxl_player_init(&P);
    zxl_event_t e = ev_of(ZXL_EV_BEGIN, t, 0, 0, 256);
    zxl_player_play(&P, t, &e);
    CHECK(P.prep.on && P.prep.kind == ZXL_EV_END, "the begin sets the end's preparation going");
    zxl_player_render(&P, buf, 48000 * 6 / 10);
    CHECK(!P.prep.on, "done within 0.6 s of loading");
    int known = 0;
    for (uint32_t i = 0; i < ZXL_SYN_PEAKS; i++)
        known |= P.peaks[i].theme == t && P.peaks[i].kind == ZXL_EV_END;
    CHECK(known, "the end's loudness is known before the end is called");
    /* and it is the same loudness as working it out on the spot */
    e = ev_of(ZXL_EV_END, t, 0, 0, 256);
    zxl_player_init(&P);
    zxl_player_play(&P, t, &e);
    int64_t direct = P.cue[0].scale_q32;
    zxl_player_init(&P);
    zxl_event_t b = ev_of(ZXL_EV_BEGIN, t, 0, 0, 256);
    zxl_player_play(&P, t, &b);
    zxl_player_render(&P, buf, 48000);
    zxl_player_play(&P, t, &e);
    int64_t prepared = 0;
    for (uint32_t i = 0; i < ZXL_SYN_CUES; i++)
        if (P.cue[i].on && P.cue[i].kind == ZXL_EV_END) prepared = P.cue[i].scale_q32;
    CHECK(prepared == direct, "prepared and on-the-spot loudness agree");
    /* an end called before its preparation finished still sounds right */
    zxl_player_init(&P);
    zxl_player_play(&P, t, &b);
    zxl_player_render(&P, buf, 256);
    zxl_player_play(&P, t, &e);
    prepared = 0;
    for (uint32_t i = 0; i < ZXL_SYN_CUES; i++)
        if (P.cue[i].on && P.cue[i].kind == ZXL_EV_END) prepared = P.cue[i].scale_q32;
    CHECK(prepared == direct && !P.prep.on, "an early end finishes the job first");
}

/* each tier sounds like its material, under every app's own phrase */
static void test_tiers(void)
{
    CHECK(zxl_synth_tier(zxl_theme_pick("Hollow")) == 1, "Hollow (2) is a base app");
    CHECK(zxl_synth_tier(zxl_theme_pick("Cartulary")) == 2, "Cartulary (145) is a composite");
    CHECK(zxl_synth_tier(zxl_curated(270)) == 2, "app 271 is the last composite");
    CHECK(zxl_synth_tier(zxl_curated(271)) == 3, "app 272 is the first crystal app");
    CHECK(zxl_synth_tier(zxl_theme_pick("Chorale")) == 3, "Chorale (407) is a crystal app");
    CHECK(zxl_synth_tier(zxl_group_theme(4)) == 1 && zxl_synth_tier(0) == 1,
          "a person's app sounds like a base app");
    uint32_t n[4] = {0}, k;
    for (uint32_t i = 0; i < ZXL_APPS; i++) n[zxl_synth_tier(zxl_curated(i))]++;
    CHECK(n[1] == 144 && n[2] == 127 && n[3] == 136, "144 / 127 / 136 (%u %u %u)", n[1], n[2],
          n[3]);
    /* a crystal app's tick rings a bell an octave over its note; a base
     * app's does not (both glass, which has no octave of its own) */
    double ratio[4] = {0};
    for (k = 1; k <= 3; k += 2) {
        const zxl_theme_t *t = 0;
        for (uint32_t i = 0; i < ZXL_APPS && !t; i++) {
            const zxl_theme_t *c = zxl_curated(i);
            if (c->timbre == ZXL_TIMBRE_GLASS && zxl_synth_tier(c) == k) t = c;
        }
        uint32_t len = render_cue(t, ZXL_EV_TICK, 0, 2, 256);
        double f = 440.0 * pow(2.0, ((double) zxl_tick_note(t, 0) - 69) / 12);
        ratio[k] = power_at(2 * f, len) / power_at(f, len);
    }
    CHECK(ratio[3] > 10 * ratio[1] && ratio[3] > 0.02,
          "a crystal app's tick has a bell an octave over it (%.4f vs %.4f)", ratio[3], ratio[1]);
    /* a composite's tick is a tick and a tock: a second click 60 ms on */
    const zxl_theme_t *c = zxl_theme_pick("Cartulary");
    render_cue(c, ZXL_EV_TICK, 0, 2, 256);
    int32_t d = 0;
    for (uint32_t i = 2 * 2870; i < 2 * 2900; i += 2) {
        int32_t v = buf[i] - buf[i - 2];
        if (v < 0) v = -v;
        if (v > d) d = v;
    }
    CHECK(d > 300, "the escapement's tock at 60 ms (%d)", d);
}

int main(void)
{
    test_levels();
    test_pitch();
    test_unique();
    test_pan();
    test_gain();
    test_live();
    test_overflow();
    test_prepare();
    test_tiers();
    printf("test_zxl_synth: %d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
