/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zui_chgvoice.c — host tests for the Chiglet's voice: only speaking
 * when asked, tap to stop, saying a status once, the status words, sentence
 * chunks, resume and repeat, quiet words, waiting for another voice, no
 * borrowed voice, the phrases it hears, the pop-up's phases under full and
 * reduced motion, the mouth, and the drawing. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zui_chgvoice.h"
#include "zui_motion.h"
#include "zui_tokens.h"
#include "truetype.h"

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

/* ---- a speaker that remembers ------------------------------------------- */

#define HEARD_MAX 64
static struct {
    char text[ZUI_CV_CHUNK];
    char lang[ZUI_CV_LANG];
    uint32_t vol, rate;
} HEARD[HEARD_MAX];
static int n_heard, n_stops, refuse_lang_not_en, refuse_all, play_inside;
static zui_cv_t *INSIDE;

static int speak(const char *text, const char *lang, uint32_t vol, uint32_t rate, void *ctx)
{
    (void) ctx;
    if (refuse_all) return -2;
    if (refuse_lang_not_en && strncmp(lang, "en", 2) != 0) return -1;
    if (n_heard < HEARD_MAX) {
        snprintf(HEARD[n_heard].text, sizeof HEARD[n_heard].text, "%s", text);
        snprintf(HEARD[n_heard].lang, sizeof HEARD[n_heard].lang, "%s", lang);
        HEARD[n_heard].vol = vol;
        HEARD[n_heard].rate = rate;
    }
    n_heard++;
    if (play_inside && INSIDE) zui_cv_spoken(INSIDE, 0); /* a synchronous speaker */
    return 0;
}

static void stop(void *ctx)
{
    (void) ctx;
    n_stops++;
}

static void reset(void)
{
    n_heard = n_stops = refuse_lang_not_en = refuse_all = play_inside = 0;
    INSIDE = 0;
    memset(HEARD, 0, sizeof HEARD);
}

static const zui_chiglet_t WHO = {{1, 2, 3, 4, 5, 6, 7, 8}, 2, ZUI_CHG_DECIDED, 0};

static void fresh(zui_cv_t *cv)
{
    reset();
    zui_motion_reset();
    zui_cv_init(cv, &WHO, speak, stop, 0);
    zui_cv_set_anchor(cv, (zui_rect_t){560, 400, 48, 48});
}

static zui_cv_facts_t facts(void)
{
    zui_cv_facts_t f;
    zui_cv_facts_init(&f);
    f.need_you = 1;
    f.need_what = "the export";
    f.running = 3;
    f.finished = 2;
    f.failed = 0;
    f.budget_pct = 40;
    return f;
}

/* play every chunk to the end, one ms per chunk */
static uint32_t play_all(zui_cv_t *cv, uint32_t now)
{
    for (int i = 0; i < 200 && cv->playing; i++) zui_cv_spoken(cv, ++now);
    return now;
}

/* ---- CV4: the status words ------------------------------------------------- */

static void test_status_text(void)
{
    char out[1024];
    zui_cv_facts_t f = facts();
    swarm_feeling_t calm = {SWARM_EMO_CALM, 2};
    zui_cv_status_text(0, &f, calm, ZUI_HEALTH_OK, true, out, sizeof out);
    CHECK(!strcmp(out,
                  "One thing is waiting for you: the export. I feel calm. 3 tasks are working. "
                  "2 tasks finished. I have 40 percent of my token budget left."),
          "full status: %s", out);
    zui_cv_status_text(0, &f, calm, ZUI_HEALTH_OK, false, out, sizeof out);
    CHECK(!strcmp(out, "One thing is waiting for you: the export. I feel calm."), "short: %s", out);

    /* nothing known: only the feeling */
    zui_cv_facts_t none;
    zui_cv_facts_init(&none);
    zui_cv_status_text(0, &none, calm, ZUI_HEALTH_OK, true, out, sizeof out);
    CHECK(!strcmp(out, "I feel calm."), "unknown facts are not said: %s", out);
    zui_cv_status_text(0, 0, calm, ZUI_HEALTH_OK, true, out, sizeof out);
    CHECK(!strcmp(out, "I feel calm."), "no facts at all: %s", out);

    /* health leads the feeling, and is in the short form */
    zui_cv_status_text(0, &f, calm, ZUI_HEALTH_SLOW, false, out, sizeof out);
    CHECK(!strcmp(out, "One thing is waiting for you: the export. I am running slowly."),
          "slow: %s", out);
    zui_cv_status_text(0, &none, calm, ZUI_HEALTH_FAILING, false, out, sizeof out);
    CHECK(!strcmp(out, "Something in me is failing. I feel calm."), "failing: %s", out);
    zui_cv_status_text(0, &none, calm, ZUI_HEALTH_STOPPED, false, out, sizeof out);
    CHECK(!strncmp(out, "I have stopped.", 15), "stopped: %s", out);

    /* very, and why */
    swarm_feeling_t worry = {SWARM_EMO_WORRY, 5};
    none.why = "the build is slow";
    zui_cv_status_text(0, &none, worry, ZUI_HEALTH_OK, true, out, sizeof out);
    CHECK(!strcmp(out, "I feel very worried, because the build is slow."), "very why: %s", out);
    swarm_feeling_t cur = {SWARM_EMO_CURIOUS, 3};
    zui_cv_status_text(0, &none, cur, ZUI_HEALTH_OK, true, out, sizeof out);
    CHECK(!strcmp(out, "I feel curious, because the build is slow."), "why: %s", out);
    swarm_feeling_t cur4 = {SWARM_EMO_CURIOUS, 4};
    none.why = "";
    zui_cv_status_text(0, &none, cur4, ZUI_HEALTH_OK, true, out, sizeof out);
    CHECK(!strcmp(out, "I feel very curious."), "very at 4, empty why ignored: %s", out);

    /* counts: one, many, none */
    zui_cv_facts_init(&none);
    none.need_you = 0;
    none.running = 0;
    none.failed = 1;
    none.finished = 1;
    zui_cv_status_text(0, &none, calm, ZUI_HEALTH_OK, true, out, sizeof out);
    CHECK(!strcmp(out, "Nothing needs you right now. I feel calm. One task failed. Nothing is "
                       "running. One task finished."),
          "ones and none: %s", out);
    none.need_you = 4;
    none.need_what = 0;
    none.failed = 2;
    none.running = 1;
    none.finished = 0;
    none.budget_pct = 250;
    zui_cv_status_text(0, &none, calm, ZUI_HEALTH_OK, true, out, sizeof out);
    CHECK(!strcmp(out, "4 things are waiting for you. I feel calm. 2 tasks failed. One task is "
                       "working. I have 100 percent of my token budget left."),
          "many, finished 0 unsaid, budget capped: %s", out);
    none.need_you = 1;
    zui_cv_status_text(0, &none, calm, ZUI_HEALTH_OK, false, out, sizeof out);
    CHECK(!strcmp(out, "One thing is waiting for you. I feel calm."), "one, no name: %s", out);
    none.need_you = 12;
    none.need_what = "a signature";
    zui_cv_status_text(0, &none, calm, ZUI_HEALTH_OK, false, out, sizeof out);
    CHECK(!strcmp(out, "12 things are waiting for you, first a signature. I feel calm."),
          "many with a name: %s", out);
    none.budget_pct = 0;
    none.need_you = ZUI_CV_UNKNOWN;
    none.failed = none.running = ZUI_CV_UNKNOWN;
    zui_cv_status_text(0, &none, calm, ZUI_HEALTH_OK, true, out, sizeof out);
    CHECK(!strcmp(out, "I feel calm. I have 0 percent of my token budget left."), "0 %%: %s", out);

    /* every feeling has its word */
    for (int e = 0; e < SWARM_EMO_COUNT; e++) {
        swarm_feeling_t fe = {(swarm_emotion_t) e, 1};
        uint32_t n = zui_cv_status_text(0, 0, fe, ZUI_HEALTH_OK, true, out, sizeof out);
        char want[64];
        snprintf(want, sizeof want, "I feel %s.", zui_emote_word((swarm_emotion_t) e));
        CHECK(!strcmp(out, want) && n == strlen(want), "feeling %d: %s", e, out);
    }

    /* cut to fit, NUL-terminated, never inside a character */
    char small[12];
    uint32_t n = zui_cv_status_text(0, &f, calm, ZUI_HEALTH_OK, true, small, sizeof small);
    CHECK(n == 11 && small[11] == 0 && !strncmp(small, "One thing i", 11), "cut: %u %s", n, small);
    CHECK(zui_cv_status_text(0, &f, calm, ZUI_HEALTH_OK, true, 0, 10) == 0, "no buffer");
    CHECK(zui_cv_status_text(0, &f, calm, ZUI_HEALTH_OK, true, small, 0) == 0, "zero cap");
    swarm_feeling_t joy = {SWARM_EMO_JOY, 1};
    zui_cv_facts_t u;
    zui_cv_facts_init(&u);
    u.why = "\xC3\xA9t\xC3\xA9 est l\xC3\xA0";
    char cut[22]; /* "I feel joyful, becau" + start of é would split it */
    zui_cv_status_text(0, &u, joy, ZUI_HEALTH_OK, true, cut, sizeof cut);
    CHECK(strlen(cut) <= 21 && ((unsigned char) cut[strlen(cut) - 1] & 0xC0u) != 0xC0u,
          "no half character: %s", cut);

    /* a table of other words */
    zui_cv_words_t fr = zui_cv_words_en;
    fr.tag = "fr";
    fr.feel = "Je me sens %s.";
    fr.need_none = "Rien ne vous attend.";
    zui_cv_facts_init(&u);
    u.need_you = 0;
    zui_cv_status_text(&fr, &u, calm, ZUI_HEALTH_OK, false, out, sizeof out);
    CHECK(!strcmp(out, "Rien ne vous attend. Je me sens calm."), "another table: %s", out);
    fr.need_none = 0; /* a missing sentence is skipped */
    zui_cv_status_text(&fr, &u, calm, ZUI_HEALTH_OK, false, out, sizeof out);
    CHECK(!strcmp(out, "Je me sens calm."), "missing pattern skipped: %s", out);
}

/* ---- CV5: chunks ------------------------------------------------------------ */

static void test_chunks(void)
{
    const char *t = "  Hello there. How are you? Fine!\nNext line  ";
    uint32_t len = (uint32_t) strlen(t), s = 0, e;
    e = zui_cv_chunk_end(t, len, &s, 200);
    CHECK(s == 2 && e == 14, "first sentence %u..%u", s, e);
    s = e;
    e = zui_cv_chunk_end(t, len, &s, 200);
    CHECK(s == 15 && e == 27, "second %u..%u", s, e);
    s = e;
    e = zui_cv_chunk_end(t, len, &s, 200);
    CHECK(s == 28 && e == 33, "third %u..%u", s, e);
    s = e;
    e = zui_cv_chunk_end(t, len, &s, 200);
    CHECK(s == 34 && e == len, "the rest %u..%u", s, e);
    s = len;
    CHECK(zui_cv_chunk_end(t, len, &s, 200) == len && s == len, "end");
    s = 44;
    CHECK(zui_cv_chunk_end(t, len, &s, 200) == len && s == len, "trailing spaces are the end");

    /* a decimal point and "e.g." inside a word are not ends */
    const char *d = "Pi is 3.14 today. Done.";
    s = 0;
    e = zui_cv_chunk_end(d, (uint32_t) strlen(d), &s, 200);
    CHECK(e == 17, "decimal not an end: %u", e);
    /* closing quotes and brackets stay with their sentence */
    const char *q = "He said \"go.\" Then (stop!) ok";
    s = 0;
    e = zui_cv_chunk_end(q, (uint32_t) strlen(q), &s, 200);
    CHECK(e == 13, "quote kept: %u", e);
    s = e;
    e = zui_cv_chunk_end(q, (uint32_t) strlen(q), &s, 200);
    CHECK(e == 26, "bracket kept: %u", e);
    const char *dots = "Wait... what?! Yes";
    s = 0;
    e = zui_cv_chunk_end(dots, (uint32_t) strlen(dots), &s, 200);
    CHECK(e == 7, "ellipsis: %u", e);
    s = e;
    e = zui_cv_chunk_end(dots, (uint32_t) strlen(dots), &s, 200);
    CHECK(e == 14, "?! together: %u", e);

    /* CJK sentence ends */
    const char *ja = "\xE3\x81\x93\xE3\x82\x93\xE3\x80\x82\xE3\x81\xAF\xEF\xBC\x81\xE3\x81\x82";
    uint32_t jl = (uint32_t) strlen(ja);
    s = 0;
    e = zui_cv_chunk_end(ja, jl, &s, 200);
    CHECK(e == 9, "。 ends: %u", e);
    s = e;
    e = zui_cv_chunk_end(ja, jl, &s, 200);
    CHECK(e == 15, "！ ends: %u", e);
    const char *zh = "\xE5\xA5\xBD\xEF\xBC\x9F\xE5\x97\x8E";
    s = 0;
    CHECK(zui_cv_chunk_end(zh, (uint32_t) strlen(zh), &s, 200) == 6, "？ ends");

    /* a long sentence breaks at its last space within max */
    const char *lng = "one two three four five six seven.";
    s = 0;
    e = zui_cv_chunk_end(lng, (uint32_t) strlen(lng), &s, 16);
    CHECK(e == 13, "space break: %u", e);
    /* a sentence end just past max is not taken */
    const char *edge = "abc def. ghi";
    s = 0;
    e = zui_cv_chunk_end(edge, (uint32_t) strlen(edge), &s, 7);
    CHECK(e == 3, "end past max: %u", e);
    s = 0;
    e = zui_cv_chunk_end(edge, (uint32_t) strlen(edge), &s, 8);
    CHECK(e == 8, "end exactly at max: %u", e);
    /* no spaces: breaks between characters, never inside one */
    const char *nos = "\xE3\x81\x82\xE3\x81\x84\xE3\x81\x86\xE3\x81\x88"; /* 4 kana */
    s = 0;
    e = zui_cv_chunk_end(nos, 12, &s, 7);
    CHECK(e == 6, "kana boundary: %u", e);
    const char *ascii = "abcdefghijklmnop";
    s = 0;
    e = zui_cv_chunk_end(ascii, 16, &s, 5);
    CHECK(e == 5, "plain cut: %u", e);
    s = 0;
    e = zui_cv_chunk_end(ascii, 16, &s, 1);
    CHECK(e == 4, "max raised to 4: %u", e);
    CHECK(zui_cv_chunk_end(0, 3, &s, 4) == 0, "NULL text");
    CHECK(zui_cv_chunk_end(ascii, 16, 0, 4) == 0, "NULL start");
    /* the chunks cover the text in order, whatever max is */
    const char *para = "First one. Second, longer sentence that goes on! Third? Fourth.";
    for (uint32_t mx = 4; mx < 70; mx += 3) {
        uint32_t L = (uint32_t) strlen(para), at = 0, k = 0;
        char joined[128] = {0};
        while (k++ < 100) {
            uint32_t st = at;
            uint32_t en = zui_cv_chunk_end(para, L, &st, mx);
            if (en <= st) break;
            CHECK(en - st <= mx, "chunk within max %u", mx);
            strncat(joined, para + st, en - st);
            at = en;
        }
        uint32_t nonspace = 0, jn = 0;
        for (uint32_t i = 0; i < L; i++) nonspace += para[i] != ' ';
        for (uint32_t i = 0; joined[i]; i++) jn += joined[i] != ' ';
        CHECK(jn == nonspace, "every letter said once at max %u (%u vs %u)", mx, jn, nonspace);
    }
}

/* ---- CV1, CV2: only when asked; tap to stop --------------------------------- */

static void test_tap(void)
{
    zui_cv_t cv;
    fresh(&cv);
    for (uint32_t t = 0; t < 100000; t += 1000) zui_cv_tick(&cv, t);
    CHECK(n_heard == 0 && cv.phase == ZUI_CV_HIDDEN, "nothing is said unasked");
    CHECK(!zui_cv_talking(&cv) && zui_cv_bubble(&cv)[0] == 0, "quiet");

    zui_cv_facts_t f = facts();
    uint32_t now = 100000;
    CHECK(zui_cv_tap(&cv, &f, now) == ZUI_CV_SPEAKING, "tap speaks");
    CHECK(n_heard == 1 && !strcmp(HEARD[0].text, "One thing is waiting for you: the export."),
          "first sentence first: %s", HEARD[0].text);
    CHECK(!strcmp(HEARD[0].lang, "en") && HEARD[0].vol == 70 && HEARD[0].rate == 100,
          "lang, volume, rate go with it");
    CHECK(cv.playing && zui_cv_talking(&cv), "playing");
    CHECK(!strcmp(zui_cv_bubble(&cv), HEARD[0].text), "the bubble shows the words");
    CHECK(cv.phase == ZUI_CV_RISING, "it rises");
    zui_cv_spoken(&cv, now + 900);
    CHECK(n_heard == 2 && !strcmp(HEARD[1].text, "I feel calm."), "second: %s", HEARD[1].text);
    zui_cv_spoken(&cv, now + 1500);
    CHECK(n_heard == 2 && !zui_cv_talking(&cv), "short status is two sentences");
    CHECK(cv.said == 2, "said counts %u", cv.said);
    zui_cv_spoken(&cv, now + 1600); /* a stray done changes nothing */
    CHECK(n_heard == 2 && cv.said == 2, "stray spoken ignored");

    /* tap while speaking stops it */
    fresh(&cv);
    zui_cv_tap(&cv, &f, 10);
    CHECK(n_heard == 1, "speaking");
    CHECK(zui_cv_tap(&cv, &f, 20) == ZUI_CV_STOPPED, "tap stops");
    CHECK(n_stops == 1 && !cv.playing && !zui_cv_talking(&cv), "speaker told to stop");
    zui_cv_spoken(&cv, 30);
    CHECK(n_heard == 1, "nothing after the stop");
    CHECK(cv.stops == 1, "stop counted");
    CHECK(zui_cv_stop(&cv, 40) == ZUI_CV_NOTHING, "stop when quiet");
    CHECK(n_stops == 1, "no stop call when quiet");

    /* hold: the full status, even while speaking */
    fresh(&cv);
    zui_cv_tap(&cv, &f, 10);
    CHECK(zui_cv_hold(&cv, &f, 20) == ZUI_CV_SPEAKING, "hold speaks");
    CHECK(n_stops == 1, "hold cut the short one");
    play_all(&cv, 20);
    CHECK(n_heard == 1 + 5, "full status is five sentences: %d", n_heard);
    CHECK(!strcmp(HEARD[5].text, "I have 40 percent of my token budget left."), "last: %s",
          HEARD[5].text);

    /* bad arguments */
    CHECK(zui_cv_tap(0, &f, 0) == ZUI_CV_BAD && zui_cv_hold(0, &f, 0) == ZUI_CV_BAD, "NULL cv");
    CHECK(zui_cv_status(0, &f, true, 0) == ZUI_CV_BAD && zui_cv_read(0, "x", 0, 0) == ZUI_CV_BAD,
          "NULL cv 2");
    CHECK(zui_cv_stop(0, 0) == ZUI_CV_BAD && zui_cv_resume(0, 0) == ZUI_CV_BAD &&
              zui_cv_repeat(0, 0) == ZUI_CV_BAD && zui_cv_hear(0, "x", 0, 0) == ZUI_CV_BAD,
          "NULL cv 3");
    CHECK(zui_cv_hear(&cv, 0, 0, 0) == ZUI_CV_BAD, "NULL words");
    zui_cv_spoken(0, 0);
    zui_cv_tick(0, 0);
    zui_cv_level(0, 9);
    zui_cv_draw(0, 0, 0, 0);
    CHECK(!zui_cv_hit(0, 0, 0) && !zui_cv_talking(0) && zui_cv_bubble(0)[0] == 0, "NULL safe");
}

/* ---- CV3: say it once ------------------------------------------------------- */

static void test_same(void)
{
    zui_cv_t cv;
    fresh(&cv);
    zui_cv_facts_t f = facts();
    zui_cv_tap(&cv, &f, 1000);
    play_all(&cv, 1000);
    CHECK(n_heard == 2, "said");
    CHECK(zui_cv_tap(&cv, &f, 30000) == ZUI_CV_SAME, "same words soon: SAME");
    CHECK(n_heard == 3 && !strcmp(HEARD[2].text, "Nothing has changed."), "says so: %s",
          HEARD[2].text);
    play_all(&cv, 30000);
    CHECK(zui_cv_tap(&cv, &f, 1000 + ZUI_CV_SAME_MS) == ZUI_CV_SPEAKING, "after a minute: again");
    CHECK(!strcmp(HEARD[3].text, "One thing is waiting for you: the export."), "in full");
    play_all(&cv, 70000);
    f.need_you = 0;
    CHECK(zui_cv_tap(&cv, &f, 70100) == ZUI_CV_SPEAKING, "changed facts: said");
    CHECK(!strcmp(HEARD[n_heard - 1].text, "Nothing needs you right now."), "new words");
    play_all(&cv, 70100);
    /* the full form is different words from the short one */
    CHECK(zui_cv_hold(&cv, &f, 70200) == ZUI_CV_SPEAKING, "full after short");
    play_all(&cv, 70200);
    /* repeat always repeats */
    int before = n_heard;
    CHECK(zui_cv_repeat(&cv, 70300) == ZUI_CV_SPEAKING, "repeat");
    play_all(&cv, 70300);
    CHECK(n_heard - before == 5, "repeat says all of the status: %d", n_heard - before);
    /* a changed feeling is a change */
    zui_cv_t c2;
    fresh(&c2);
    zui_cv_tap(&c2, &f, 5);
    play_all(&c2, 5);
    swarm_feeling_t w = {SWARM_EMO_WORRY, 2};
    zui_cv_set_feeling(&c2, w, ZUI_HEALTH_OK, 10);
    CHECK(zui_cv_tap(&c2, &f, 20) == ZUI_CV_SPEAKING, "feeling changed: said");
    /* the clock wrapping is not "soon" forever: ms are unsigned differences */
    zui_cv_t c3;
    fresh(&c3);
    zui_cv_tap(&c3, &f, 0xFFFFFF00u);
    play_all(&c3, 0xFFFFFF00u);
    CHECK(zui_cv_tap(&c3, &f, 0x100u) == ZUI_CV_SAME, "wrap: 512 ms later is soon");
}

/* ---- CV5: read, resume, repeat ----------------------------------------- */

static void test_read(void)
{
    zui_cv_t cv;
    fresh(&cv);
    const char *sec = "The swarm has twelve models. Each one has a budget. Budgets scale by "
                      "Fibonacci. That is all.";
    CHECK(zui_cv_read(&cv, sec, "Budgets", 0) == ZUI_CV_SPEAKING, "read");
    CHECK(!strcmp(HEARD[0].text, "Reading Budgets."), "title first: %s", HEARD[0].text);
    zui_cv_spoken(&cv, 1);
    CHECK(!strcmp(HEARD[1].text, "The swarm has twelve models."), "then the text");
    zui_cv_spoken(&cv, 2);
    CHECK(!strcmp(HEARD[2].text, "Each one has a budget."), "next");
    CHECK(zui_cv_stop(&cv, 3) == ZUI_CV_STOPPED, "stop mid read");
    CHECK(zui_cv_resume(&cv, 4) == ZUI_CV_SPEAKING, "resume");
    CHECK(!strcmp(HEARD[3].text, "Each one has a budget."), "resume starts that chunk again: %s",
          HEARD[3].text);
    zui_cv_spoken(&cv, 5);
    CHECK(!strcmp(HEARD[4].text, "Budgets scale by Fibonacci."), "and goes on");
    CHECK(zui_cv_repeat(&cv, 6) == ZUI_CV_SPEAKING, "repeat in a read");
    CHECK(!strcmp(HEARD[5].text, "Budgets scale by Fibonacci."), "repeats the chunk: %s",
          HEARD[5].text);
    CHECK(n_stops == 2, "repeat stopped what was playing: %d", n_stops);
    play_all(&cv, 7);
    CHECK(!strcmp(HEARD[n_heard - 1].text, "That is all."), "to the end");
    CHECK(zui_cv_resume(&cv, 50) == ZUI_CV_NOTHING, "nothing to resume after the end");

    /* no title */
    fresh(&cv);
    zui_cv_read(&cv, "Just this.", 0, 0);
    CHECK(n_heard == 1 && !strcmp(HEARD[0].text, "Just this."), "no title line");
    zui_cv_read(&cv, "Again.", "", 0);
    CHECK(!strcmp(HEARD[1].text, "Again."), "empty title is no title");
    CHECK(n_stops == 1, "a new read stops the old");
    /* empty */
    fresh(&cv);
    CHECK(zui_cv_read(&cv, "   ", "x", 0) == ZUI_CV_NOTHING, "blank");
    CHECK(!strcmp(HEARD[0].text, "There is nothing to read."), "says so");
    CHECK(zui_cv_read(&cv, 0, 0, 0) == ZUI_CV_NOTHING, "NULL text");
    CHECK(zui_cv_resume(&cv, 1) == ZUI_CV_NOTHING, "nothing to resume");
    /* repeat with nothing said */
    fresh(&cv);
    CHECK(zui_cv_repeat(&cv, 0) == ZUI_CV_NOTHING, "nothing to repeat");
    /* resume after a status is nothing */
    zui_cv_facts_t f = facts();
    zui_cv_tap(&cv, &f, 0);
    zui_cv_stop(&cv, 1);
    CHECK(zui_cv_resume(&cv, 2) == ZUI_CV_NOTHING, "a status does not resume");

    /* long text is cut at a character, and every chunk fits */
    fresh(&cv);
    static char big[ZUI_CV_TEXT * 2];
    for (uint32_t i = 0; i < sizeof big - 1; i++) big[i] = (i % 11 == 10) ? ' ' : 'a';
    big[sizeof big - 1] = 0;
    zui_cv_read(&cv, big, "Big", 0);
    CHECK(cv.len == ZUI_CV_TEXT - 1, "kept %u", cv.len);
    play_all(&cv, 0);
    int ok = 1;
    for (int i = 0; i < n_heard && i < HEARD_MAX; i++)
        if (strlen(HEARD[i].text) > ZUI_CV_CHUNK - 1) ok = 0;
    CHECK(ok && n_heard > 10, "chunks fit: %d", n_heard);
    /* a title on text that fills the buffer is still first */
    CHECK(!strcmp(HEARD[0].text, "Reading Big."), "title on full text: %s", HEARD[0].text);
    /* multi-byte text is never split */
    fresh(&cv);
    static char kana[ZUI_CV_TEXT + 64];
    uint32_t k = 0;
    while (k + 3 < sizeof kana - 1) {
        kana[k++] = (char) 0xE3;
        kana[k++] = (char) 0x81;
        kana[k++] = (char) 0x82;
    }
    kana[k] = 0;
    zui_cv_read(&cv, kana, 0, 0);
    CHECK(cv.len % 3 == 0, "cut on a character: %u", cv.len);
    play_all(&cv, 0);
    ok = 1;
    for (int i = 0; i < n_heard && i < HEARD_MAX; i++)
        if (strlen(HEARD[i].text) % 3) ok = 0;
    CHECK(ok, "every kana chunk whole");
}

/* ---- CV6: words only; volume and rate ----------------------------------- */

static void test_words(void)
{
    zui_cv_t cv;
    fresh(&cv);
    zui_cv_set_mode(&cv, ZUI_CV_WORDS);
    zui_cv_read(&cv, "Short one. A rather longer second sentence for the bubble.", 0, 1000);
    CHECK(n_heard == 0, "words mode: nothing spoken");
    CHECK(!strcmp(zui_cv_bubble(&cv), "Short one."), "shown: %s", zui_cv_bubble(&cv));
    CHECK(zui_cv_talking(&cv), "talking in words");
    zui_cv_tick(&cv, 1000 + 1499);
    CHECK(!strcmp(zui_cv_bubble(&cv), "Short one."), "at least 1.5 s up");
    zui_cv_tick(&cv, 1000 + 1500);
    CHECK(!strncmp(zui_cv_bubble(&cv), "A rather", 8), "then the next");
    /* 47 bytes: 600 + 60 * 47 = 3420 ms */
    zui_cv_tick(&cv, 2500 + 3419);
    CHECK(zui_cv_talking(&cv), "still up");
    zui_cv_tick(&cv, 2500 + 3420);
    CHECK(!zui_cv_talking(&cv), "done");
    CHECK(cv.said == 2, "both counted: %u", cv.said);
    /* the rate scales it */
    fresh(&cv);
    zui_cv_set_mode(&cv, ZUI_CV_WORDS);
    zui_cv_set_rate(&cv, 200);
    zui_cv_read(&cv, "A rather longer second sentence for the bubble.", 0, 0);
    zui_cv_tick(&cv, 1709);
    CHECK(zui_cv_talking(&cv), "double rate: 1710 ms");
    zui_cv_tick(&cv, 1710);
    CHECK(!zui_cv_talking(&cv), "done at 1710");
    /* long chunks are capped at 12 s */
    fresh(&cv);
    zui_cv_set_mode(&cv, ZUI_CV_WORDS);
    zui_cv_set_rate(&cv, 50);
    char longs[230];
    memset(longs, 'b', sizeof longs - 1);
    longs[sizeof longs - 1] = 0;
    zui_cv_read(&cv, longs, 0, 0);
    zui_cv_tick(&cv, 11999);
    CHECK(zui_cv_talking(&cv), "under the cap");
    zui_cv_tick(&cv, 12000);
    CHECK(!zui_cv_talking(&cv), "12 s cap");
    /* stop in words mode */
    zui_cv_read(&cv, "One. Two.", 0, 20000);
    CHECK(zui_cv_stop(&cv, 20001) == ZUI_CV_STOPPED && n_stops == 0, "stop words: no speaker");
    /* clamps */
    zui_cv_set_volume(&cv, 500);
    CHECK(cv.volume == 100, "volume cap");
    zui_cv_set_rate(&cv, 10);
    CHECK(cv.rate_pct == 50, "rate floor");
    zui_cv_set_rate(&cv, 900);
    CHECK(cv.rate_pct == 200, "rate cap");
    zui_cv_set_mode(&cv, (zui_cv_mode_t) 7);
    CHECK(cv.mode == ZUI_CV_VOICE, "bad mode is voice");
    /* volume and rate reach the speaker */
    fresh(&cv);
    zui_cv_set_volume(&cv, 33);
    zui_cv_set_rate(&cv, 120);
    zui_cv_read(&cv, "Hi.", 0, 0);
    CHECK(HEARD[0].vol == 33 && HEARD[0].rate == 120, "settings sent");
}

/* ---- CV7: never over another voice --------------------------------------- */

static void test_busy(void)
{
    zui_cv_t cv;
    fresh(&cv);
    zui_cv_set_busy(&cv, true, 0);
    zui_cv_read(&cv, "One. Two.", 0, 0);
    CHECK(n_heard == 0 && cv.waiting && zui_cv_talking(&cv), "waits while busy");
    zui_cv_tick(&cv, 500);
    CHECK(n_heard == 0, "still waiting");
    zui_cv_set_busy(&cv, false, 600);
    CHECK(n_heard == 1 && !strcmp(HEARD[0].text, "One."), "goes when free");
    zui_cv_set_busy(&cv, true, 700);
    zui_cv_spoken(&cv, 800);
    CHECK(n_heard == 1 && cv.waiting, "next waits");
    zui_cv_set_busy(&cv, false, 850); /* freed without a tick */
    CHECK(n_heard == 2, "goes at once");
    /* freed between ticks */
    zui_cv_t c2;
    fresh(&c2);
    c2.busy = true;
    zui_cv_read(&c2, "A.", 0, 0);
    c2.busy = false;
    zui_cv_tick(&c2, 10);
    CHECK(n_heard == 1, "tick sends it");
    CHECK(n_stops == 0, "nothing interrupted");
    /* stop while waiting */
    fresh(&c2);
    zui_cv_set_busy(&c2, true, 0);
    zui_cv_read(&c2, "A. B.", 0, 0);
    CHECK(zui_cv_stop(&c2, 1) == ZUI_CV_STOPPED && n_stops == 0, "stop a waiting one");
    zui_cv_set_busy(&c2, false, 2);
    CHECK(n_heard == 0, "and it never speaks");
}

/* ---- CV8: no borrowed voice --------------------------------------------- */

static void test_no_voice(void)
{
    zui_cv_t cv;
    fresh(&cv);
    refuse_lang_not_en = 1;
    zui_cv_set_lang(&cv, "ja");
    zui_cv_read(&cv, "One. Two.", 0, 0);
    CHECK(n_heard == 0, "refused: nothing heard");
    CHECK(cv.refused == 1 && cv.words_only, "words instead");
    CHECK(!strcmp(cv.note, "No voice for ja yet, so here are the words."), "note: %s", cv.note);
    CHECK(!strcmp(zui_cv_bubble(&cv), "One."), "the words shown");
    zui_cv_tick(&cv, 1500);
    CHECK(!strcmp(zui_cv_bubble(&cv), "Two."), "rest as words");
    CHECK(cv.refused == 1, "asked once: the rest is words");
    zui_cv_tick(&cv, 3000);
    CHECK(!zui_cv_talking(&cv), "done");
    /* the next utterance tries the voice again */
    zui_cv_set_lang(&cv, "en-GB");
    zui_cv_read(&cv, "Hello.", 0, 4000);
    CHECK(n_heard == 1 && !strcmp(HEARD[0].lang, "en-GB"), "voice again for English");
    CHECK(cv.note[0] == 0, "note cleared");
    /* no speaker at all */
    zui_cv_t c2;
    reset();
    zui_cv_init(&c2, &WHO, 0, 0, 0);
    zui_cv_set_lang(&c2, "");
    zui_cv_read(&c2, "Hi.", 0, 0);
    CHECK(c2.words_only &&
              !strcmp(c2.note, "No voice for this language yet, so here are the words."),
          "no engine: %s", c2.note);
    zui_cv_set_lang(&c2, 0);
    CHECK(c2.lang[0] == 0, "NULL lang");
    zui_cv_set_lang(&c2, "a-very-long-language-tag-x");
    CHECK(strlen(c2.lang) == ZUI_CV_LANG - 1, "lang cut");
    /* refused mid way: the rest is words */
    fresh(&cv);
    zui_cv_read(&cv, "One. Two. Three.", 0, 0);
    refuse_all = 1;
    zui_cv_spoken(&cv, 10);
    CHECK(cv.words_only && !strcmp(zui_cv_bubble(&cv), "Two."), "engine gave up mid way");
    refuse_all = 0;
    zui_cv_tick(&cv, 1510);
    CHECK(n_heard == 1 && !strcmp(zui_cv_bubble(&cv), "Three."), "stays words");
}

/* ---- a speaker that plays inside speak ------------------------------------ */

static void test_sync_speaker(void)
{
    zui_cv_t cv;
    fresh(&cv);
    INSIDE = &cv;
    play_inside = 1;
    zui_cv_read(&cv, "A. B. C.", 0, 0);
    CHECK(n_heard == 3 && !zui_cv_talking(&cv), "all three, no recursion trouble: %d", n_heard);
    CHECK(cv.said == 3 && !cv.playing, "counted");
}

/* ---- zui_cv_hear ----------------------------------------------------------- */

static char SEC_NAME[96];
static int32_t section(const char *name, char *text, uint32_t cap, char *title, uint32_t tcap,
                       void *ctx)
{
    (void) ctx;
    snprintf(SEC_NAME, sizeof SEC_NAME, "%s", name);
    if (!strcmp(name, "")) {
        snprintf(text, cap, "Selected words. More.");
        title[0] = 0;
        (void) tcap;
        return (int32_t) strlen(text);
    }
    if (!strcmp(name, "budget")) {
        snprintf(text, cap, "Budget text.");
        snprintf(title, tcap, "Budget");
        return (int32_t) strlen(text);
    }
    return -1;
}

static void test_hear(void)
{
    zui_cv_t cv;
    fresh(&cv);
    zui_cv_facts_t f = facts();
    CHECK(zui_cv_hear(&cv, "open mail", &f, 0) == ZUI_CV_NOT_MINE, "not mine");
    CHECK(n_heard == 0, "and not said");
    CHECK(zui_cv_hear(&cv, "How are you?", &f, 0) == ZUI_CV_SPEAKING, "how are you");
    CHECK(!strcmp(HEARD[0].text, "One thing is waiting for you: the export."), "short status");
    play_all(&cv, 0);
    CHECK(n_heard == 2, "two sentences");
    CHECK(zui_cv_hear(&cv, "  STATUS   report. ", &f, 100) == ZUI_CV_SPEAKING, "status report");
    play_all(&cv, 100);
    CHECK(n_heard == 7, "full: %d", n_heard);
    CHECK(zui_cv_hear(&cv, "what's going on", &f, 200000) == ZUI_CV_SPEAKING, "what's going on");
    CHECK(zui_cv_hear(&cv, "stop", &f, 200001) == ZUI_CV_STOPPED, "stop");
    CHECK(zui_cv_hear(&cv, "be quiet", &f, 200002) == ZUI_CV_STOPPED, "quiet when quiet is ok");
    CHECK(zui_cv_hear(&cv, "Please, repeat!", &f, 200003) == ZUI_CV_SPEAKING, "please repeat");

    /* reading sections */
    fresh(&cv);
    zui_cv_set_section(&cv, section, 0);
    CHECK(zui_cv_hear(&cv, "read me this section", &f, 0) == ZUI_CV_SPEAKING, "read this");
    CHECK(SEC_NAME[0] == 0, "asked for this section");
    CHECK(!strcmp(HEARD[0].text, "Selected words."), "no title: %s", HEARD[0].text);
    CHECK(zui_cv_hear(&cv, "stop talking", &f, 1) == ZUI_CV_STOPPED, "stop talking");
    CHECK(zui_cv_hear(&cv, "keep reading", &f, 2) == ZUI_CV_SPEAKING, "keep reading");
    CHECK(!strcmp(HEARD[1].text, "Selected words."), "from that chunk");
    const char *forms[] = {"read the budget section", "read me the budget", "read budget",
                           "read the budget",         "Read budget please", "read me budget",
                           "read budget to me"};
    for (unsigned i = 0; i < sizeof forms / sizeof *forms; i++) {
        int before = n_heard;
        CHECK(zui_cv_hear(&cv, forms[i], &f, 10 + i) == ZUI_CV_SPEAKING, "%s", forms[i]);
        CHECK(!strcmp(SEC_NAME, "budget"), "%s -> %s", forms[i], SEC_NAME);
        CHECK(n_heard == before + 1 && !strcmp(HEARD[n_heard - 1].text, "Reading Budget."),
              "%s titled", forms[i]);
    }
    CHECK(zui_cv_hear(&cv, "read the weather", &f, 50) == ZUI_CV_NOTHING, "unknown section");
    CHECK(!strcmp(HEARD[n_heard - 1].text, "I cannot find weather."), "says so: %s",
          HEARD[n_heard - 1].text);
    zui_cv_set_section(&cv, 0, 0);
    CHECK(zui_cv_hear(&cv, "read this", &f, 60) == ZUI_CV_NOTHING, "no section source");
    CHECK(!strcmp(HEARD[n_heard - 1].text, "I cannot find that section."), "this: %s",
          HEARD[n_heard - 1].text);
    CHECK(zui_cv_hear(&cv, "read ", &f, 70) == ZUI_CV_NOT_MINE, "bare read is not mine");

    /* settings by voice */
    fresh(&cv);
    CHECK(zui_cv_hear(&cv, "louder", &f, 0) == ZUI_CV_SET && cv.volume == 80, "louder");
    CHECK(zui_cv_hear(&cv, "speak up", &f, 0) == ZUI_CV_SET && cv.volume == 90, "speak up");
    zui_cv_hear(&cv, "louder", &f, 0);
    zui_cv_hear(&cv, "louder", &f, 0);
    CHECK(cv.volume == 100, "volume capped");
    CHECK(zui_cv_hear(&cv, "quieter", &f, 0) == ZUI_CV_SET && cv.volume == 90, "quieter");
    CHECK(zui_cv_hear(&cv, "softer", &f, 0) == ZUI_CV_SET && cv.volume == 80, "softer");
    cv.volume = 5;
    zui_cv_hear(&cv, "quieter", &f, 0);
    CHECK(cv.volume == 0, "volume floor");
    CHECK(zui_cv_hear(&cv, "faster", &f, 0) == ZUI_CV_SET && cv.rate_pct == 110, "faster");
    CHECK(zui_cv_hear(&cv, "speed up", &f, 0) == ZUI_CV_SET && cv.rate_pct == 120, "speed up");
    CHECK(zui_cv_hear(&cv, "slower", &f, 0) == ZUI_CV_SET && cv.rate_pct == 110, "slower");
    CHECK(zui_cv_hear(&cv, "slow down", &f, 0) == ZUI_CV_SET && cv.rate_pct == 100, "slow down");
    CHECK(n_heard == 0, "settings say nothing");
    CHECK(zui_cv_hear(&cv, "go on", &f, 0) == ZUI_CV_NOTHING, "go on with nothing");
    CHECK(zui_cv_hear(&cv, "again", &f, 0) == ZUI_CV_NOTHING, "again with nothing");
    CHECK(zui_cv_hear(&cv, "", &f, 0) == ZUI_CV_NOT_MINE, "empty");
    CHECK(zui_cv_hear(&cv, "stopwatch", &f, 0) == ZUI_CV_NOT_MINE, "not a prefix match");
}

/* ---- CV9: the pop-up -------------------------------------------------------- */

static void test_popup(void)
{
    zui_cv_t cv;
    fresh(&cv);
    zui_cv_read(&cv, "One.", 0, 1000);
    CHECK(cv.phase == ZUI_CV_RISING, "rising");
    zui_cv_tick(&cv, 1000 + ZUI_CV_RISE_MS - 1);
    CHECK(cv.phase == ZUI_CV_RISING, "still rising");
    zui_cv_tick(&cv, 1000 + ZUI_CV_RISE_MS);
    CHECK(cv.phase == ZUI_CV_SHOWN, "shown");
    zui_cv_spoken(&cv, 2000);
    zui_cv_tick(&cv, 2000 + ZUI_CV_HOLD_MS - 1);
    CHECK(cv.phase == ZUI_CV_SHOWN && !strcmp(zui_cv_bubble(&cv), "One."), "held with words");
    zui_cv_tick(&cv, 2000 + ZUI_CV_HOLD_MS);
    CHECK(cv.phase == ZUI_CV_SETTLING, "settling");
    zui_cv_tick(&cv, 2000 + ZUI_CV_HOLD_MS + ZUI_CV_RISE_MS - 1);
    CHECK(cv.phase == ZUI_CV_SETTLING, "still settling");
    zui_cv_tick(&cv, 2000 + ZUI_CV_HOLD_MS + ZUI_CV_RISE_MS);
    CHECK(cv.phase == ZUI_CV_HIDDEN && zui_cv_bubble(&cv)[0] == 0, "hidden, bubble cleared");

    /* a stop settles at once */
    zui_cv_read(&cv, "One. Two.", 0, 10000);
    zui_cv_tick(&cv, 10000 + ZUI_CV_RISE_MS);
    zui_cv_stop(&cv, 10300);
    zui_cv_tick(&cv, 10300);
    CHECK(cv.phase == ZUI_CV_SETTLING, "stop: settle now");

    /* asked again while settling: rises from where it is */
    zui_cv_tick(&cv, 10300 + ZUI_CV_RISE_MS / 2);
    zui_cv_read(&cv, "Back.", 0, 10300 + ZUI_CV_RISE_MS / 2);
    CHECK(cv.phase == ZUI_CV_RISING, "rises again");
    uint32_t at = 10300 + ZUI_CV_RISE_MS / 2;
    CHECK(at - cv.phase_ms >= ZUI_CV_RISE_MS / 2 - 2 && at - cv.phase_ms <= ZUI_CV_RISE_MS / 2 + 2,
          "from half way: %u", at - cv.phase_ms);
    /* asked while shown: stays shown */
    zui_cv_tick(&cv, at + ZUI_CV_RISE_MS);
    zui_cv_read(&cv, "More.", 0, at + ZUI_CV_RISE_MS + 1);
    CHECK(cv.phase == ZUI_CV_SHOWN, "stays up");
    /* never hides while it talks */
    zui_cv_tick(&cv, at + 100000);
    CHECK(cv.phase == ZUI_CV_SHOWN, "up while talking");

    /* reduce motion: a cut (crossfade 0) */
    fresh(&cv);
    zui_motion_set_flag(ZUI_PREF_REDUCE_MOTION, true);
    zui_motion_set_crossfade(0);
    CHECK(zui_motion_style() == ZUI_MOTION_CUT, "cut style");
    zui_cv_read(&cv, "One.", 0, 0);
    CHECK(cv.phase == ZUI_CV_SHOWN, "cut: shown at once");
    zui_cv_spoken(&cv, 10);
    zui_cv_tick(&cv, 10 + ZUI_CV_HOLD_MS);
    CHECK(cv.phase == ZUI_CV_HIDDEN, "cut: hidden at once");
    zui_motion_reset();
}

/* ---- the mouth -------------------------------------------------------------- */

static void test_level(void)
{
    zui_cv_t cv;
    fresh(&cv);
    zui_cv_level(&cv, 200);
    CHECK(cv.level == 200, "rises at once");
    zui_cv_level(&cv, 0);
    CHECK(cv.level < 200 && cv.level > 100, "falls over frames: %u", cv.level);
    uint32_t prev = cv.level;
    for (int i = 0; i < 12; i++) zui_cv_level(&cv, 0);
    CHECK(cv.level < prev && cv.level == 0, "reaches 0: %u", cv.level);
    zui_cv_level(&cv, 999);
    CHECK(cv.level == 256, "capped");
    zui_cv_tick(&cv, 0);
    CHECK(cv.level < 256, "not playing: decays");

    int16_t sil[64] = {0}, loud[64];
    for (int i = 0; i < 64; i++) loud[i] = (int16_t) (i & 1 ? 12000 : -12000);
    CHECK(zui_cv_level_pcm(sil, 64) == 0, "silence 0");
    CHECK(zui_cv_level_pcm(loud, 64) == 256, "loud 256");
    int16_t mid[2] = {3000, -3000};
    CHECK(zui_cv_level_pcm(mid, 2) == 128, "mid: %u", zui_cv_level_pcm(mid, 2));
    int16_t mn[1] = {-32768};
    CHECK(zui_cv_level_pcm(mn, 1) == 256, "most negative");
    CHECK(zui_cv_level_pcm(0, 4) == 0 && zui_cv_level_pcm(loud, 0) == 0, "empty");
}

/* ---- drawing ---------------------------------------------------------------- */

#define W 640
#define H 480
static uint32_t PX[W * H];

static uint64_t hash(void)
{
    uint64_t h = 1469598103934665603ull;
    for (uint32_t i = 0; i < W * H; i++) h = (h ^ PX[i]) * 1099511628211ull;
    return h;
}

static uint64_t draw(zui_cv_t *cv, const zui_text_t *t, uint32_t now)
{
    zui_surface_t s;
    for (uint32_t i = 0; i < W * H; i++) PX[i] = ZUI_VOID;
    zui_surface_init(&s, PX, W, H, W);
    zui_cv_draw(cv, &s, t, now);
    return hash();
}

static uint32_t changed(void)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < W * H; i++) n += PX[i] != ZUI_VOID;
    return n;
}

static uint8_t FONT_DATA[200000];
static ttf_font_t FONT;
static zui_glyph_cache_t CACHE;

static bool load_font(void)
{
    const char *paths[] = {"../assets/fonts/ascw-golden/ASCW-Golden-Latin.ttf",
                           "assets/fonts/ascw-golden/ASCW-Golden-Latin.ttf"};
    for (unsigned i = 0; i < 2; i++) {
        FILE *f = fopen(paths[i], "rb");
        if (!f) continue;
        size_t n = fread(FONT_DATA, 1, sizeof FONT_DATA, f);
        fclose(f);
        if (n > 0 && ttf_parse(&FONT, FONT_DATA, (uint32_t) n)) return true;
    }
    return false;
}

static void test_draw(void)
{
    zui_cv_t cv;
    fresh(&cv);
    zui_text_t t;
    memset(&t, 0, sizeof t);
    t.size_px = 14;
    bool have_font = load_font();
    CHECK(have_font, "the ASCW Golden Latin face loads (run from kernel/)");
    if (!have_font) return;
    zui_glyph_cache_init(&CACHE);
    t.font = &FONT;
    t.cache = &CACHE;
    uint64_t rest = draw(&cv, &t, 0);
    uint32_t rest_px = changed();
    CHECK(rest_px > 300, "resting chiglet drawn: %u", rest_px);
    CHECK(draw(&cv, &t, 0) == rest, "deterministic");
    /* only inside the anchor (and its halo) when resting */
    int outside = 0;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            if (PX[y * W + x] != ZUI_VOID && (x < 520 || y < 360)) outside++;
    CHECK(outside == 0, "rest stays in its corner: %d", outside);

    zui_cv_read(&cv, "Hello there, this is your Chiglet speaking.", 0, 0);
    zui_cv_tick(&cv, ZUI_CV_RISE_MS);
    uint64_t up = draw(&cv, &t, ZUI_CV_RISE_MS);
    CHECK(up != rest && changed() > rest_px * 3, "popped up bigger, with bubble: %u", changed());
    int bubble = 0;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < 400; x++) bubble += PX[y * W + x] != ZUI_VOID;
    CHECK(bubble > 1000, "bubble to the left: %d", bubble);
    /* in the very corner: the bubble is pulled back on screen, whole */
    zui_rect_t a = {600, 440, 40, 40};
    zui_cv_set_anchor(&cv, a);
    draw(&cv, &t, ZUI_CV_RISE_MS);
    int top_rows = 0, bubble_px = 0;
    for (int x = 0; x < W; x++) top_rows += PX[ZUI_S4 * W + x] != ZUI_VOID;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < 560; x++) bubble_px += PX[y * W + x] != ZUI_VOID;
    CHECK(bubble_px > 1000, "corner: bubble still drawn: %d", bubble_px);
    (void) top_rows;
    zui_cv_set_anchor(&cv, (zui_rect_t){0, 0, 0, 0});
    CHECK(cv.anchor.w == 40, "bad anchor ignored");
    zui_cv_set_anchor(&cv, (zui_rect_t){4, 300, 48, 48});
    draw(&cv, &t, ZUI_CV_RISE_MS);
    int right = 0;
    for (int y = 0; y < H; y++)
        for (int x = 150; x < W; x++) right += PX[y * W + x] != ZUI_VOID;
    CHECK(right > 1000, "no room on the left: bubble on the right: %d", right);
    /* no room either side (a narrow screen): the bubble goes above it */
    {
        static uint32_t nar[500 * 360];
        for (int i = 0; i < 500 * 360; i++) nar[i] = ZUI_VOID;
        zui_surface_t ns;
        zui_surface_init(&ns, nar, 500, 360, 500);
        zui_cv_set_anchor(&cv, (zui_rect_t){420, 280, 56, 56});
        zui_cv_draw(&cv, &ns, &t, ZUI_CV_RISE_MS);
        int above = 0, beside = 0;
        for (int y = 0; y < 170; y++)
            for (int x = 0; x < 500; x++) above += nar[y * 500 + x] != ZUI_VOID;
        for (int y = 230; y < 330; y++)
            for (int x = 0; x < 300; x++) beside += nar[y * 500 + x] != ZUI_VOID;
        CHECK(above > 1000 && beside == 0, "narrow: bubble above, body uncovered (%d, %d)", above,
              beside);
    }
    zui_cv_set_anchor(&cv, (zui_rect_t){560, 400, 48, 48});

    /* the mouth moves with the level, and not under reduce motion */
    cv.level = 0;
    uint64_t closed = draw(&cv, &t, ZUI_CV_RISE_MS);
    zui_cv_level(&cv, 256);
    uint64_t open = draw(&cv, &t, ZUI_CV_RISE_MS);
    CHECK(open != closed, "mouth follows level");
    zui_motion_set_flag(ZUI_PREF_REDUCE_MOTION, true);
    zui_cv_level(&cv, 0);
    for (int i = 0; i < 12; i++) zui_cv_level(&cv, 0);
    uint64_t r0 = draw(&cv, &t, 5000);
    zui_cv_level(&cv, 256);
    uint64_t r1 = draw(&cv, &t, 5000);
    CHECK(r0 == r1, "reduce motion: mouth held still");
    uint64_t r2 = draw(&cv, &t, 5125);
    CHECK(r1 == r2, "reduce motion: motes still");
    zui_motion_reset();
    uint64_t m0 = draw(&cv, &t, 5000), m1 = draw(&cv, &t, 5125);
    CHECK(m0 != m1, "motes move with full motion");

    /* NULL text, or text with no font: no bubble, still drawn */
    zui_text_t nofont = t;
    nofont.font = 0;
    for (int k = 0; k < 2; k++) {
        draw(&cv, k ? &nofont : 0, ZUI_CV_RISE_MS);
        int left = 0;
        for (int y = 0; y < H; y++)
            for (int x = 0; x < 400; x++) left += PX[y * W + x] != ZUI_VOID;
        CHECK(left == 0 && changed() > 0, "no text, no bubble: %d", left);
    }
    /* long words wrap within the bubble: nothing is drawn left of it */
    zui_cv_read(&cv,
                "Supercalifragilisticexpialidociousandthensomemoreletterstofillthewholeline "
                "and then many short words that need several lines to fit inside.",
                0, 6000);
    zui_cv_tick(&cv, 6000);
    draw(&cv, &t, 6000);
    int far_left = 0;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < 140; x++) far_left += PX[y * W + x] != ZUI_VOID;
    CHECK(far_left == 0, "wrapped inside its 360 px: %d", far_left);

    /* the note is drawn too (one more line) */
    fresh(&cv);
    refuse_all = 1;
    zui_cv_read(&cv, "Hi.", 0, 0);
    zui_cv_tick(&cv, ZUI_CV_RISE_MS);
    draw(&cv, &t, ZUI_CV_RISE_MS);
    uint32_t with_note = changed();
    zui_cv_t c2;
    fresh(&c2);
    zui_cv_read(&c2, "Hi.", 0, 0);
    zui_cv_tick(&c2, ZUI_CV_RISE_MS);
    draw(&c2, &t, ZUI_CV_RISE_MS);
    CHECK(with_note > changed(), "note adds a line: %u vs %u", with_note, changed());

    /* the hit test: resting anchor, and the popped body */
    fresh(&cv);
    CHECK(zui_cv_hit(&cv, 580, 420), "rest hit");
    CHECK(!zui_cv_hit(&cv, 300, 300), "miss");
    CHECK(!zui_cv_hit(&cv, 584, 360), "above the rest: miss while hidden");
    zui_cv_read(&cv, "Hi.", 0, 0);
    zui_cv_tick(&cv, ZUI_CV_RISE_MS);
    CHECK(zui_cv_hit(&cv, 584, 380), "popped body hit");

    /* the feeling's face morphs */
    fresh(&cv);
    zui_cv_read(&cv, "Hi.", 0, 0);
    zui_cv_tick(&cv, 1000);
    uint64_t calm = draw(&cv, &t, 1000);
    swarm_feeling_t joy = {SWARM_EMO_JOY, 5};
    zui_cv_set_feeling(&cv, joy, ZUI_HEALTH_OK, 1000);
    uint64_t mid = draw(&cv, &t, 1000 + ZUI_EMOTE_MORPH_MS / 2);
    uint64_t end = draw(&cv, &t, 1000 + ZUI_EMOTE_MORPH_MS);
    CHECK(calm != mid && mid != end, "the face morphs");
    zui_cv_set_feeling(&cv, joy, ZUI_HEALTH_FAILING, 3000);
    CHECK(draw(&cv, &t, 3000) != end, "health ring shows");
    zui_cv_set_genes(&cv, &WHO);
    zui_chiglet_t other = WHO;
    other.genes[0] = 3;
    zui_cv_set_genes(&cv, &other);
    uint64_t g0 = draw(&cv, &t, 3000);
    zui_cv_set_genes(&cv, &other);
    CHECK(cv.who.genes[0] == 3, "genes kept");
    zui_cv_set_genes(&cv, &WHO);
    CHECK(draw(&cv, &t, 3000) != g0, "its look is its genes");
    zui_cv_set_genes(&cv, 0);
    CHECK(cv.who.genes[0] == WHO.genes[0], "NULL genes ignored");
    zui_cv_set_words(&cv, 0);
    CHECK(cv.words == &zui_cv_words_en, "NULL words: English");

    /* tiny surface: nothing written outside it */
    static uint32_t guard[16 * 16 + 64];
    for (unsigned i = 0; i < sizeof guard / 4; i++) guard[i] = 0xDEADBEEFu;
    zui_surface_t ts;
    zui_surface_init(&ts, guard + 32, 16, 16, 16);
    zui_cv_draw(&cv, &ts, &t, 3000);
    int bad = 0;
    for (unsigned i = 0; i < 32; i++)
        bad += guard[i] != 0xDEADBEEFu || guard[32 + 256 + i] != 0xDEADBEEFu;
    CHECK(bad == 0, "tiny surface: guards intact: %d", bad);
}

/* CV10: read the way a person would */
static void finish(zui_cv_t *cv)
{
    for (int i = 0; i < 40 && zui_cv_talking(cv); i++) zui_cv_spoken(cv, 0);
}

static void test_human(void)
{
    zui_cv_t cv;
    fresh(&cv);
    zui_cv_read(&cv, "Merged in commit 4bea0160fe2f16193a9b6f4169c71711b1550dc0.", 0, 0);
    CHECK(n_heard == 1 && !strcmp(HEARD[0].text, "Merged in commit a hash."), "hash named: %s",
          HEARD[0].text);
    CHECK(!strcmp(cv.chunk, "Merged in commit a hash."), "the bubble says the same: %s", cv.chunk);
    finish(&cv);
    CHECK(zui_cv_spell(&cv, 10) == ZUI_CV_SPEAKING, "spell that");
    CHECK(strstr(HEARD[1].text, "Merged in commit 4 b e a, 0 1 6 0,") == HEARD[1].text,
          "spelled in fours: %s", HEARD[1].text);
    finish(&cv);
    fresh(&cv);
    CHECK(zui_cv_spell(&cv, 0) == ZUI_CV_NOTHING && n_heard == 0, "nothing read yet");
    zui_cv_read(&cv, "Steps:\n- open https://github.com/a/b\n- run `make`\n```\nint x;\n```", 0, 0);
    finish(&cv);
    char all[512] = "";
    for (int i = 0; i < n_heard && i < HEARD_MAX; i++) {
        if (i) strcat(all, " ");
        strcat(all, HEARD[i].text);
    }
    CHECK(!strcmp(all, "Steps: open a link to github.com. run make. There is some code here."),
          "markup silent, link and code named: %s", all);
    fresh(&cv);
    zui_cv_read(&cv, "--- *** ---", 0, 0);
    CHECK(n_heard == 1 && !strcmp(HEARD[0].text, zui_cv_words_en.nothing),
          "only rules: nothing to read (%s)", HEARD[0].text);
    fresh(&cv);
    zui_cv_read(&cv, "It is done.", "Build 1954a42d55bf", 0);
    CHECK(!strcmp(HEARD[0].text, "Reading Build a hash."), "the title too: %s", HEARD[0].text);
    fresh(&cv);
    zui_cv_set_verbatim(&cv, true);
    CHECK(cv.verbatim, "verbatim on");
    zui_cv_read(&cv, "Commit 1954a42d55bf.", "T 1954a42d55bf", 0);
    CHECK(!strcmp(HEARD[0].text, "Reading T 1954a42d55bf."), "verbatim: as written: %s",
          HEARD[0].text);
    zui_cv_set_verbatim(&cv, false);
    zui_cv_set_verbatim(0, true);
    CHECK(!cv.verbatim, "verbatim off");
    /* the facts of a status */
    fresh(&cv);
    zui_cv_facts_t f = facts();
    f.need_you = 1;
    f.need_what = "review of 1954a42d55bf";
    zui_cv_status(&cv, &f, false, 0);
    CHECK(strstr(HEARD[0].text, "waiting for you: review of a hash.") != 0, "status fact: %s",
          HEARD[0].text);
    char out[256];
    f.why = "job `cmsg_01SiwTxCAS9qLbWY3tFVth` is slow";
    zui_cv_status_text(0, &f, (swarm_feeling_t){0}, ZUI_HEALTH_OK, true, out, sizeof out);
    CHECK(strstr(out, "because job an ID is slow") != 0, "the why: %s", out);
    /* hear it */
    fresh(&cv);
    zui_cv_read(&cv, "Key sk-ab12Cd34Ef56Gh78.", 0, 0);
    finish(&cv);
    CHECK(zui_cv_hear(&cv, "Spell that, please", &f, 5) == ZUI_CV_SPEAKING &&
              strstr(HEARD[n_heard - 1].text, "capital C") != 0,
          "'spell that' heard: %s", HEARD[n_heard - 1].text);
    finish(&cv);
    CHECK(zui_cv_hear(&cv, "read that exactly", &f, 6) == ZUI_CV_SPEAKING, "read that exactly");
    finish(&cv);
    /* another language's words */
    fresh(&cv);
    static speech_tidy_words_t sfr;
    static zui_cv_words_t wfr;
    sfr = speech_tidy_en;
    sfr.hash = "une empreinte";
    wfr = zui_cv_words_en;
    wfr.say = &sfr;
    zui_cv_set_words(&cv, &wfr);
    zui_cv_read(&cv, "voir 1954a42d55bf", 0, 0);
    CHECK(!strcmp(HEARD[0].text, "voir une empreinte"), "French: %s", HEARD[0].text);
    wfr.say = 0;
    finish(&cv);
    zui_cv_read(&cv, "voir 1954a42d55bf", 0, 1);
    CHECK(!strcmp(HEARD[n_heard - 1].text, "voir a hash"), "no tidy words: English: %s",
          HEARD[n_heard - 1].text);
    CHECK(zui_cv_spell(0, 0) == ZUI_CV_BAD, "NULL");
}

/* CV11: face to face */
static int n_hook, hook_open;
static void on_call(bool open, void *ctx)
{
    (void) ctx;
    n_hook++;
    hook_open = open;
}

static void dump_ppm(const char *path)
{
    FILE *f = path ? fopen(path, "wb") : 0;
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (uint32_t i = 0; i < W * H; i++) {
        uint8_t px[3] = {(uint8_t) (PX[i] >> 16), (uint8_t) (PX[i] >> 8), (uint8_t) PX[i]};
        fwrite(px, 1, 3, f);
    }
    fclose(f);
}

static void test_call(void)
{
    zui_cv_t cv;
    fresh(&cv);
    n_hook = hook_open = 0;
    zui_cv_set_call_hook(&cv, on_call, 0);
    zui_cv_facts_t f = facts();
    CHECK(!zui_cv_in_call(&cv), "no call at first");
    CHECK(zui_cv_hear(&cv, "bye", &f, 0) == ZUI_CV_NOT_MINE, "bye with no call is not its");
    CHECK(zui_cv_hear(&cv, "Let's talk.", &f, 10) == ZUI_CV_SPEAKING && zui_cv_in_call(&cv),
          "let's talk opens it");
    CHECK(n_hook == 1 && hook_open, "the host is told: listen");
    CHECK(n_heard == 1 && !strcmp(HEARD[0].text, "I am here."), "hello: %s", HEARD[0].text);
    CHECK(cv.calls == 1, "counted");
    CHECK(zui_cv_hear(&cv, "talk to me", &f, 20) == ZUI_CV_SAME && n_hook == 1, "already open");
    CHECK(zui_cv_call(&cv, true, 20) == ZUI_CV_NOTHING, "open twice: nothing");
    finish(&cv);
    /* it does not settle away while the call is open */
    zui_cv_tick(&cv, 30);
    zui_cv_tick(&cv, 30 + ZUI_CV_HOLD_MS + 1000);
    CHECK(cv.phase == ZUI_CV_SHOWN, "stays up in a call (phase %d)", cv.phase);
    /* the mic and what was heard */
    zui_cv_listening(&cv, true, 5000);
    CHECK(cv.listening, "listening");
    zui_cv_tick(&cv, 5000 + ZUI_CV_CALL_IDLE_MS + 1);
    CHECK(zui_cv_in_call(&cv), "never closed while the person is speaking");
    zui_cv_heard(&cv, "how are you", 200000);
    CHECK(!cv.listening && !strcmp(cv.heard, "how are you"), "heard kept: %s", cv.heard);
    CHECK(zui_cv_hear(&cv, "how are you", &f, 200000) == ZUI_CV_SPEAKING, "it answers");
    /* a visitor */
    zui_chiglet_t peer = {{9, 8, 7, 6, 5, 4, 3, 2}, 1, ZUI_CHG_DECIDED, 0};
    swarm_feeling_t joy = {SWARM_EMO_JOY, 3};
    CHECK(zui_cv_visit(&cv, &peer, "Ember", joy, 200100) == ZUI_CV_SET && cv.visiting &&
              !strcmp(cv.guest_name, "Ember"),
          "a visitor in the call");
    CHECK(zui_cv_visit(&cv, 0, "x", joy, 0) == ZUI_CV_BAD, "NULL visitor");
    /* drawn: the window, the visitor, End */
    zui_text_t t;
    memset(&t, 0, sizeof t);
    t.size_px = 14;
    if (load_font()) {
        zui_glyph_cache_init(&CACHE);
        t.font = &FONT;
        t.cache = &CACHE;
    }
    zui_cv_set_name(&cv, "Pip");
    zui_cv_listening(&cv, true, 200200);
    zui_cv_heard(&cv, "Chiglet, how are you?", 200200);
    zui_cv_listening(&cv, true, 200200);
    uint64_t h1 = draw(&cv, &t, 200300);
    zui_rect_t w = cv.call_rect;
    CHECK(w.w == 520 && w.h == 400 && w.x == W - ZUI_S4 - 520 && w.y == H - ZUI_S4 - 400,
          "a call tile in its corner: %d,%d %dx%d", w.x, w.y, w.w, w.h);
    uint32_t in = 0, out = 0;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            bool on = PX[y * W + x] != ZUI_VOID;
            bool inw = x >= w.x && y >= w.y && x < w.x + w.w && y < w.y + w.h;
            if (on && inw) in++;
            if (on && !inw) out++;
        }
    CHECK(in > 100000 && out == 0, "drawn inside its window only: %u in, %u out", in, out);
    dump_ppm(getenv("ZXV_CV_CALL_PPM"));
    CHECK(draw(&cv, &t, 200300) == h1, "deterministic");
    zui_rect_t e = cv.end_rect;
    CHECK(e.w > 0 && e.x + e.w <= w.x + w.w && e.y >= w.y, "End in the bar");
    CHECK(zui_cv_call_hit(&cv, e.x + 2, e.y + 2) == ZUI_CV_HIT_END, "End hit");
    CHECK(zui_cv_call_hit(&cv, w.x + w.w / 2, w.y + w.h / 2) == ZUI_CV_HIT_WINDOW, "window hit");
    CHECK(zui_cv_call_hit(&cv, 1, 1) == ZUI_CV_HIT_NONE, "elsewhere");
    CHECK(zui_cv_call_hit(0, 1, 1) == ZUI_CV_HIT_NONE, "NULL");
    zui_cv_visit_end(&cv, 200400);
    uint64_t h2 = draw(&cv, &t, 200400);
    CHECK(!cv.visiting && h2 != h1, "the visitor leaves");
    zui_cv_listening(&cv, false, 200400);
    CHECK(draw(&cv, &t, 200400) != h2, "the mic mark changes when it stops listening");
    /* reduce motion: in place at once */
    zui_motion_set_flag(ZUI_PREF_REDUCE_MOTION, true);
    draw(&cv, &t, cv.call_ms);
    CHECK(changed() > 100000, "reduce motion: whole at once");
    zui_motion_set_flag(ZUI_PREF_REDUCE_MOTION, false);
    /* bye closes it and says so */
    finish(&cv);
    int before = n_heard;
    CHECK(zui_cv_hear(&cv, "Goodbye!", &f, 210000) == ZUI_CV_STOPPED && !zui_cv_in_call(&cv),
          "goodbye closes it");
    CHECK(n_heard == before + 1 && !strcmp(HEARD[n_heard - 1].text, "Bye for now."), "bye: %s",
          HEARD[n_heard - 1].text);
    CHECK(n_hook == 2 && !hook_open && !cv.visiting && !cv.listening && !cv.heard[0],
          "the host is told: stop listening");
    CHECK(zui_cv_visit(&cv, &peer, "Ember", joy, 210001) == ZUI_CV_NOTHING,
          "no visit without a call");
    CHECK(zui_cv_call(&cv, false, 0) == ZUI_CV_NOTHING, "close twice: nothing");
    finish(&cv);
    zui_cv_tick(&cv, 210000 + ZUI_CV_HOLD_MS + 10);
    zui_cv_tick(&cv, 210000 + ZUI_CV_HOLD_MS + 10 + ZUI_CV_RISE_MS + 1);
    CHECK(cv.phase == ZUI_CV_HIDDEN, "and settles away (phase %d)", cv.phase);
    /* a quiet call closes itself, silently */
    CHECK(zui_cv_call(&cv, true, 300000) == ZUI_CV_SPEAKING, "open again");
    finish(&cv);
    before = n_heard;
    zui_cv_tick(&cv, 300000 + ZUI_CV_CALL_IDLE_MS - 1);
    CHECK(zui_cv_in_call(&cv), "not before two minutes");
    zui_cv_tick(&cv, 300000 + ZUI_CV_CALL_IDLE_MS);
    CHECK(!zui_cv_in_call(&cv) && n_heard == before && !hook_open, "idle: closed without a word");
    /* other words */
    static zui_cv_words_t wq;
    wq = zui_cv_words_en;
    wq.call_hello = 0;
    zui_cv_set_words(&cv, &wq);
    before = n_heard;
    CHECK(zui_cv_call(&cv, true, 400000) == ZUI_CV_SPEAKING && n_heard == before,
          "no hello words: opens quietly");
    zui_cv_set_words(&cv, 0);
    zui_cv_set_name(&cv, 0);
    CHECK(!cv.name[0], "name cleared");
    CHECK(zui_cv_call(0, true, 0) == ZUI_CV_BAD && !zui_cv_in_call(0), "NULL");
    zui_cv_set_call_hook(0, 0, 0);
    zui_cv_listening(0, 1, 0);
    zui_cv_heard(0, "x", 0);
    zui_cv_visit_end(0, 0);
    zui_cv_set_name(0, "x");
    zui_rect_t tiny = zui_cv_call_rect(&cv, 100, 90);
    CHECK(tiny.x == 0 && tiny.y == 0 && tiny.w == 100 && tiny.h == 90, "a tiny screen: all of it");
    zui_cv_set_anchor(&cv, (zui_rect_t){10, 10, 48, 48});
    zui_rect_t tl = zui_cv_call_rect(&cv, W, H);
    CHECK(tl.x == ZUI_S4 && tl.y == ZUI_S4, "top-left corner Chiglet: top-left tile");
}

int main(void)
{
    test_status_text();
    test_chunks();
    test_tap();
    test_same();
    test_read();
    test_words();
    test_busy();
    test_no_voice();
    test_sync_speaker();
    test_hear();
    test_popup();
    test_level();
    test_draw();
    test_human();
    test_call();
    printf("%s test_zui_chgvoice: %d/%d checks\n", fails ? "[FAIL]" : "[PASS]", checks - fails,
           checks);
    return fails ? 1 : 0;
}
