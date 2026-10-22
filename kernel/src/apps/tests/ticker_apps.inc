/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Ticker's model against zxvd's real dispatcher (LINK). zxvd_people.c,
 * which serves xlate.* in the daemon, is not linked into this test, so
 * xlate.languages, xlate.detect and xlate.translate are answered here by a
 * small host in their own shapes (api/zxvd.openrpc.json), set as zxvd's
 * people ops as test_zxvd_rpc.c does; the requests still go through
 * zxvd_rpc_handle_line. Slots: @num.ticker_caption_least_ms,
 * @num.ticker_caption_pace_ms, @logic.ticker_caption_hold_ok,
 * @logic.ticker_caption_record_ok. */
#include "ticker_model.h"

static uint32_t TICK_DETECTS, TICK_TRANSLATES;

static int tick_people(const char *method, const char *params, size_t len, zxvd_buf_t *out,
                       char *msg, size_t msg_cap)
{
    char ps[8192];
    if (len >= sizeof ps) len = sizeof ps - 1u;
    memcpy(ps, params, len);
    ps[len] = 0;
    if (!strcmp(method, "xlate.languages")) {
        zxvd_buf_printf(out, "{\"count\":4,\"languages\":[{\"tag\":\"en\",\"name\":\"English\"},"
                             "{\"tag\":\"de\",\"name\":\"German\"},"
                             "{\"tag\":\"fr\",\"name\":\"French\"},"
                             "{\"tag\":\"es\",\"name\":\"Spanish\"}]}");
        return 0;
    }
    if (!strcmp(method, "xlate.detect")) {
        TICK_DETECTS++;
        if (strstr(ps, "zzrefuse")) {
            snprintf(msg, msg_cap, "cannot tell");
            return -32030;
        }
        const char *tag = strstr(ps, "Guten")     ? "de"
                          : strstr(ps, "Bonjour") ? "fr"
                          : strstr(ps, "Hola")    ? "es"
                                                  : "en";
        zxvd_buf_printf(out,
                        "{\"language\":\"%s\",\"name\":\"x\",\"confidence\":90,\"letters\":5,"
                        "\"reliable\":true}",
                        tag);
        return 0;
    }
    if (!strcmp(method, "xlate.translate")) {
        TICK_TRANSLATES++;
        if (strstr(ps, "Bonjour")) {
            snprintf(msg, msg_cap, "no model can generate");
            return -32031;
        }
        if (strstr(ps, "Hola")) {
            snprintf(msg, msg_cap, "bad params");
            return -32602;
        }
        if (strstr(ps, "Guten Tag")) {
            zxvd_buf_printf(out, "{\"text\":\"Good day to you, this is a much longer sentence "
                                 "that the translation makes, so long that it takes more "
                                 "than one caption to show\",\"translated\":true,\"machine\":true,"
                                 "\"from\":\"de\",\"to\":\"en\"}");
            return 0;
        }
        zxvd_buf_printf(out, "{\"text\":\"Good morning\",\"translated\":true,\"machine\":true,"
                             "\"from\":\"de\",\"to\":\"en\"}");
        return 0;
    }
    snprintf(msg, msg_cap, "not here");
    return -32601;
}

static ticker_t TICK;
static ticker_caption_t TICK_CAPS[8];

APPS_TEST(ticker)
{
    static const zxvd_people_ops_t people = {tick_people};

    /* TK1-TK3 together: a caption may go exactly when it has stayed its least time */
    {
        bool same = true;
        for (int64_t chars = 1; chars <= 84; chars++) {
            int64_t least = ticker_least_ms(chars);
            for (int64_t shown = least - 2; shown <= least + 2; shown++) {
                bool go = ticker_hold_ok(shown, ticker_pace(shown, chars), 2, 42);
                same = same && go == (shown >= least);
            }
        }
        CHECK(same, "ticker: pace >= 60 and a second is the least time, for 1..84 characters");
    }

    /* TM1: characters of UTF-8 */
    CHECK(ticker_chars("ab", 2) == 2 && ticker_chars("\xc3\xa4x", 3) == 2 &&
              ticker_chars("\xf0\x9f\x98\x80", 4) == 1 && ticker_chars("a\tb\nc", 5) == 5 &&
              ticker_chars("\xc3", 1) == -1 && ticker_chars("\xed\xa0\x80", 3) == -1 &&
              ticker_chars("\xc0\x80", 2) == -1 && ticker_chars("a\rb", 3) == -1 &&
              ticker_chars("\x7f", 1) == -1 && ticker_chars(NULL, 0) == -1,
          "ticker: characters, and what is not text");

    /* TM4: lines of at most 42, two to a caption */
    {
        int32_t k = ticker_break("  Hello   there  ", 17, TICK_CAPS, 8);
        CHECK(k == 1 && TICK_CAPS[0].nlines == 1 && !strcmp(TICK_CAPS[0].line[0], "Hello there") &&
                  TICK_CAPS[0].chars == 11 && TICK_CAPS[0].longest == 11,
              "ticker: one short line, spaces folded");
        static const char *s42 =
            "aaaaaaaaaa bbbbbbbbbb cccccccccc dddddddddd"; /* 43: one too many */
        k = ticker_break(s42, (uint32_t) strlen(s42), TICK_CAPS, 8);
        CHECK(k == 1 && TICK_CAPS[0].nlines == 2 && TICK_CAPS[0].longest == 32 &&
                  TICK_CAPS[0].chars == 42 && !strcmp(TICK_CAPS[0].line[1], "dddddddddd"),
              "ticker: 43 characters break before the last word");
        static const char *f42 = "aaaaaaaaaa bbbbbbbbbb cccccccccc ddddddddd"; /* 42 exactly */
        k = ticker_break(f42, (uint32_t) strlen(f42), TICK_CAPS, 8);
        CHECK(k == 1 && TICK_CAPS[0].nlines == 1 && TICK_CAPS[0].longest == 42,
              "ticker: 42 fit one line");
        static char word[200];
        memset(word, 'w', 100);
        k = ticker_break(word, 100, TICK_CAPS, 8);
        CHECK(k == 2 && TICK_CAPS[0].nlines == 2 && TICK_CAPS[0].longest == 42 &&
                  TICK_CAPS[0].chars == 84 && TICK_CAPS[1].nlines == 1 && TICK_CAPS[1].chars == 16,
              "ticker: a word longer than a line is cut, 100 = 42 + 42 + 16");
        memset(word, 0, sizeof word);
        for (uint32_t i = 0; i < 43; i++) memcpy(word + 2 * i, "\xc3\xa4", 2);
        k = ticker_break(word, 86, TICK_CAPS, 8);
        CHECK(k == 1 && TICK_CAPS[0].nlines == 2 && TICK_CAPS[0].longest == 42 &&
                  TICK_CAPS[0].chars == 43 && strlen(TICK_CAPS[0].line[0]) == 84,
              "ticker: cut at a character, never inside one");
        memset(word, 'w', 100);
        CHECK(ticker_break(word, 100, TICK_CAPS, 1) == -1 &&
                  ticker_break("   ", 3, TICK_CAPS, 8) == 0 &&
                  ticker_break("\xc3", 1, TICK_CAPS, 8) == -1,
              "ticker: more captions than room, only spaces, not text");
    }

    /* the person's language, and nothing sent for bad words */
    CHECK(ticker_init(&TICK, &LINK, "e") == ZXM_E_ARG &&
              ticker_init(&TICK, &LINK, "en-") == ZXM_E_ARG &&
              ticker_init(&TICK, &LINK, "en_GB") == ZXM_E_ARG &&
              ticker_init(&TICK, NULL, "en") == ZXM_E_ARG,
          "ticker: a language that is no BCP 47 tag is refused");
    CHECK(ticker_init(&TICK, &LINK, "en") == ZXM_OK && !strcmp(TICK.to, "en"), "ticker: English");
    int before = sent;
    static char huge[TICKER_TEXT_MAX + 2];
    memset(huge, 'x', sizeof huge - 1u);
    CHECK(ticker_heard(&TICK, 1000, 1, "", 0) == ZXM_E_ARG &&
              ticker_heard(&TICK, 1000, 1, huge, TICKER_TEXT_MAX + 1u) == ZXM_E_ARG &&
              ticker_heard(&TICK, 1000, 1, "\xc3", 1) == ZXM_E_ARG &&
              ticker_heard(&TICK, 1000, 1000, "hi", 2) == ZXM_E_ARG &&
              ticker_heard(&TICK, -1, 1, "hi", 2) == ZXM_E_ARG &&
              ticker_heard(&TICK, 1000000000000000, 1, "hi", 2) == ZXM_E_ARG &&
              ticker_heard(&TICK, 1000, 1, "   ", 3) == ZXM_E_ARG && sent == before &&
              ticker_waiting(&TICK) == 0,
          "ticker: bad words are refused before anything is sent");

    /* TM2: the languages */
    zxvd_rpc_set_people_ops(NULL);
    CHECK(ticker_languages(&TICK) == ZXM_E_REMOTE && TICK.nlangs == 0,
          "ticker: xlate.languages not served: refused");
    zxvd_rpc_set_people_ops(&people);
    CHECK(ticker_languages(&TICK) == ZXM_OK && TICK.nlangs == 4 &&
              ticker_lang_place(&TICK, "de") == 2 && ticker_lang_place(&TICK, "EN-gb") == 1 &&
              ticker_lang_place(&TICK, "und") == 0 && ticker_lang_place(&TICK, "pt") == 0,
          "ticker: xlate.languages: %s", LINK.err);

    /* TM2, TM3: the person's own language is not translated */
    CHECK(ticker_heard(&TICK, 1000, 1, "Hello there", 11) == ZXM_OK && TICK_DETECTS == 1 &&
              TICK_TRANSLATES == 0 && ticker_waiting(&TICK) == 1,
          "ticker: English heard, English shown: %s", LINK.err);
    /* TM3: German is translated, and marked machine */
    CHECK(ticker_heard(&TICK, 2000, 2, "Guten Morgen", 12) == ZXM_OK && TICK_TRANSLATES == 1 &&
              ticker_waiting(&TICK) == 2,
          "ticker: German translated: %s", LINK.err);
    /* TM3: no model: shown as heard */
    CHECK(ticker_heard(&TICK, 3000, 3, "Bonjour", 7) == ZXM_OK && TICK.no_model &&
              ticker_waiting(&TICK) == 3,
          "ticker: no model: French shown as heard: %s", LINK.err);
    /* TM3: any other refusal changes nothing */
    CHECK(ticker_heard(&TICK, 4000, 4, "Hola", 4) == ZXM_E_REMOTE && ticker_waiting(&TICK) == 3,
          "ticker: a translation refused: nothing added");
    /* TM2: a language zxvd cannot tell is not translated */
    CHECK(ticker_heard(&TICK, 4000, 4, "zzrefuse", 8) == ZXM_OK && ticker_waiting(&TICK) == 4 &&
              TICK_TRANSLATES == 3,
          "ticker: language not known: shown as heard: %s", LINK.err);

    /* TM6: on screen, each until it has stayed long enough */
    bool moved = false;
    CHECK(ticker_tick(&TICK, 10000, &moved) == ZXM_OK && moved && TICK.showing &&
              !strcmp(TICK.screen.cap.line[0], "Hello there") && !TICK.screen.cap.translated &&
              TICK.screen.cap.lang == 1 && TICK.screen.cap.speaker == 1 &&
              TICK.screen.least_ms == 1000 && ticker_waiting(&TICK) == 3,
          "ticker: the first caption shown");
    CHECK(ticker_save(&TICK) == ZXM_OK && ticker_save(&TICK) == ZXM_OK && TICK.nsaved == 1 &&
              TICK.saved[0].at_ms == 1000,
          "ticker: the person saves it, once");
    CHECK(ticker_tick(&TICK, 10999, &moved) == ZXM_OK && !moved &&
              !strcmp(TICK.screen.cap.line[0], "Hello there"),
          "ticker: 999 ms is too soon");
    CHECK(ticker_tick(&TICK, 11000, &moved) == ZXM_OK && moved &&
              !strcmp(TICK.screen.cap.line[0], "Good morning") && TICK.screen.cap.translated &&
              TICK.screen.cap.machine && !strcmp(TICK.screen.cap.from, "de") &&
              TICK.screen.cap.lang == 2,
          "ticker: a second later, the German, translated, marked machine");
    CHECK(ticker_tick(&TICK, 10500, &moved) == ZXM_E_ARG &&
              ticker_tick(&TICK, -1, &moved) == ZXM_E_ARG &&
              ticker_tick(&TICK, 11000, NULL) == ZXM_E_ARG,
          "ticker: the clock never goes back");
    CHECK(ticker_tick(&TICK, 12000, &moved) == ZXM_OK && moved &&
              !strcmp(TICK.screen.cap.line[0], "Bonjour") && !TICK.screen.cap.translated &&
              TICK.screen.cap.lang == 3,
          "ticker: then French as heard");
    CHECK(ticker_tick(&TICK, 13000, &moved) == ZXM_OK && moved &&
              !strcmp(TICK.screen.cap.line[0], "zzrefuse") && TICK.screen.cap.lang == 0 &&
              !strcmp(TICK.screen.cap.from, "und") && ticker_waiting(&TICK) == 0,
          "ticker: then the words of no known language");
    CHECK(ticker_tick(&TICK, 99000, &moved) == ZXM_OK && !moved && TICK.showing,
          "ticker: the last caption stays");

    /* TM4, TM6: a long translation makes several captions, each its least time */
    CHECK(ticker_heard(&TICK, 20000, 5, "Guten Tag", 9) == ZXM_OK && ticker_waiting(&TICK) == 2,
          "ticker: a long translation, two captions: %s", LINK.err);
    CHECK(ticker_tick(&TICK, 100000, &moved) == ZXM_OK && moved && TICK.screen.cap.nlines == 2 &&
              TICK.screen.cap.longest <= TICKER_LINE_CHARS && TICK.screen.least_ms > 1000,
          "ticker: the first of them");
    {
        int64_t least = TICK.screen.least_ms;
        CHECK(ticker_tick(&TICK, 100000 + least - 1, &moved) == ZXM_OK && !moved &&
                  ticker_tick(&TICK, 100000 + least, &moved) == ZXM_OK && moved,
              "ticker: 60 ms a character: %lld ms, not one less", (long long) least);
    }

    /* TM5: the ring is bounded; a caption that does not fit adds nothing */
    ticker_clear(&TICK);
    CHECK(!TICK.showing && ticker_waiting(&TICK) == 0 && TICK.nsaved == 1,
          "ticker: cleared, the saved kept");
    {
        int rc = ZXM_OK;
        for (uint32_t i = 0; i < TICKER_QUEUE && rc == ZXM_OK; i++)
            rc = ticker_heard(&TICK, 30000 + i, 1, "Hello", 5);
        before = sent;
        CHECK(rc == ZXM_OK && ticker_waiting(&TICK) == TICKER_QUEUE &&
                  ticker_heard(&TICK, 40000, 1, "Hello", 5) == ZXM_E_STATE && sent == before &&
                  ticker_waiting(&TICK) == TICKER_QUEUE,
              "ticker: a full ring refuses before sending (backpressure)");
        ticker_clear(&TICK);
        for (uint32_t i = 0; i < TICKER_QUEUE - 1u && rc == ZXM_OK; i++)
            rc = ticker_heard(&TICK, 30000 + i, 1, "Hello", 5);
        CHECK(rc == ZXM_OK && ticker_heard(&TICK, 40000, 5, "Guten Tag", 9) == ZXM_E_STATE &&
                  ticker_waiting(&TICK) == TICKER_QUEUE - 1u,
              "ticker: two captions with room for one: none added");
    }

    /* TM7: saved captions are bounded and go only when the person clears them */
    ticker_clear(&TICK);
    CHECK(ticker_save(&TICK) == ZXM_E_STATE, "ticker: nothing on screen to save");
    {
        int rc = ZXM_OK;
        char w[16];
        for (uint32_t i = 1; i < TICKER_SAVED && rc == ZXM_OK; i++) {
            int n = snprintf(w, sizeof w, "word %u", i);
            rc = ticker_heard(&TICK, 50000 + i, 1, w, (uint32_t) n);
            if (rc == ZXM_OK) rc = ticker_tick(&TICK, 200000 + 2000 * (int64_t) i, &moved);
            if (rc == ZXM_OK) rc = ticker_save(&TICK);
        }
        CHECK(rc == ZXM_OK && TICK.nsaved == TICKER_SAVED, "ticker: %u saved", TICKER_SAVED);
        rc = ticker_heard(&TICK, 90000, 1, "one more", 8);
        CHECK(rc == ZXM_OK && ticker_tick(&TICK, 300000, &moved) == ZXM_OK && moved &&
                  ticker_save(&TICK) == ZXM_E_STATE && TICK.nsaved == TICKER_SAVED,
              "ticker: the saved list is bounded");
        ticker_clear_saved(&TICK);
        CHECK(TICK.nsaved == 0 && TICK.showing, "ticker: the person clears the saved");
    }
    CHECK(ticker_set_language(&TICK, "de") == ZXM_OK &&
              ticker_set_language(&TICK, "") == ZXM_E_ARG && !strcmp(TICK.to, "de"),
          "ticker: the person's language changed");
    zxvd_rpc_set_people_ops(NULL);
}
