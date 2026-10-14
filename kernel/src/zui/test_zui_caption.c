/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zui_caption.c — live captions (zui_caption.h CA1..CA7). */
#include <stdio.h>
#include <string.h>

#include "zui_caption.h"
#include "zui_motion.h"
#include "zui_palette.h"
#include "zui_tokens.h"
#include "zui_ttf_fixture.inc" /* ZUI_TTF[]: every glyph 600/1000 em wide */

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

static zui_caption_t C;

/* With no font each character is half the size wide: 24 px -> 12 px. */
static void fresh(void)
{
    zui_caption_init(&C, 0);
}

static void test_style(void)
{
    fresh();
    CHECK(C.style.size_px == 24 && C.style.bg_q8 == 192 && C.style.max_lines == 2 &&
              C.style.position == ZUI_CAP_BOTTOM && !C.style.high_contrast &&
              C.style.width_pct == 80,
          "default style");
    CHECK(C.hold_ms == ZUI_CAP_HOLD_MS, "default hold");
    zui_cap_style_t st;
    zui_cap_style_default(&st);
    st.size_px = 2;
    st.bg_q8 = 999;
    st.position = 7;
    st.max_lines = 0;
    st.width_pct = 5;
    st.margin_px = -4;
    st.pad_px = -1;
    zui_caption_set_style(&C, &st);
    CHECK(C.style.size_px == ZUI_CAP_SIZE_MIN, "size clamps up");
    CHECK(C.style.bg_q8 == 256, "opacity clamps to 256");
    CHECK(C.style.position == ZUI_CAP_BOTTOM, "unknown position is bottom");
    CHECK(C.style.max_lines == 1, "at least one line");
    CHECK(C.style.width_pct == 20, "width share clamps up");
    CHECK(C.style.margin_px == 0 && C.style.pad_px == 0, "no negative spacing");
    st.size_px = 999;
    st.max_lines = 99;
    st.width_pct = 150;
    st.position = ZUI_CAP_TOP;
    zui_caption_set_style(&C, &st);
    CHECK(C.style.size_px == ZUI_CAP_SIZE_MAX && C.style.max_lines == ZUI_CAP_LINES_MAX &&
              C.style.width_pct == 100 && C.style.position == ZUI_CAP_TOP,
          "upper clamps, top kept");
    zui_caption_set_style(&C, 0);
    CHECK(C.style.size_px == ZUI_CAP_SIZE_MAX, "NULL style ignored");
    zui_cap_style_default(0);
    CHECK(zui_caption_set_speaker(&C, 3, "Ana") && !strcmp(C.speaker[3], "Ana"), "speaker name");
    CHECK(!zui_caption_set_speaker(&C, ZUI_CAP_SPEAKERS, "X"), "speaker out of range");
    fresh();
    CHECK(zui_caption_measure(&C, "hello", 5) == 60, "fallback measure: 12 px a character");
    CHECK(zui_caption_measure(&C, "\xC3\xA9", 2) == 12, "one character, two bytes");
    CHECK(zui_caption_measure(&C, "e\xCC\x81", 3) == 12, "a combining mark has no width");
    CHECK(zui_caption_line_h(&C) == 30, "fallback line height 5/4 of the size");
}

static void test_partials(void)
{
    fresh();
    CHECK(zui_caption_feed(&C, 0, 400, 0, false, "hello") == ZUI_CAP_ADDED, "partial added");
    CHECK(zui_caption_layout(&C, 1000) == 1 && !strcmp(zui_caption_line(&C, 0), "hello"),
          "shown: '%s'", zui_caption_line(&C, 0));
    CHECK(C.line_w[0] == 60, "its width");
    CHECK(zui_caption_transcript_count(&C) == 0, "a partial is not in the transcript");
    CHECK(zui_caption_feed(&C, 0, 700, 0, false, "hello wor") == ZUI_CAP_REPLACED,
          "partial replaced");
    CHECK(C.n_live == 1, "still one segment");
    zui_caption_layout(&C, 1000);
    CHECK(!strcmp(zui_caption_line(&C, 0), "hello wor"), "replaced text shows");
    CHECK(zui_caption_feed(&C, 0, 900, 0, true, "Hello world.") == ZUI_CAP_REPLACED,
          "the final replaces the partial");
    CHECK(C.n_live == 1 && C.live[0].final && C.live[0].end_ms == 900, "final in place");
    CHECK(zui_caption_transcript_count(&C) == 1, "the final is in the transcript");
    CHECK(zui_caption_feed(&C, 0, 950, 0, false, "late") == ZUI_CAP_IGNORED,
          "a late partial cannot undo a final");
    zui_caption_layout(&C, 1000);
    CHECK(!strcmp(zui_caption_line(&C, 0), "Hello world."), "final kept");
    CHECK(zui_caption_feed(&C, 0, 950, 0, true, "Hello, world.") == ZUI_CAP_REPLACED,
          "a correction");
    CHECK(zui_caption_transcript_count(&C) == 1 &&
              !strcmp(zui_caption_transcript_get(&C, 0)->text, "Hello, world."),
          "the correction replaces the transcript entry");
    CHECK(zui_caption_feed(&C, 0, 950, 0, true, "") == ZUI_CAP_IGNORED, "an empty final ignored");
    /* withdraw */
    CHECK(zui_caption_feed(&C, 1000, 1200, 0, false, "uh") == ZUI_CAP_ADDED, "another partial");
    CHECK(zui_caption_feed(&C, 1000, 1200, 0, false, "  ") == ZUI_CAP_WITHDRAWN,
          "an empty partial withdraws it");
    CHECK(C.n_live == 1, "gone");
    CHECK(zui_caption_feed(&C, 5000, 5200, 0, false, "") == ZUI_CAP_IGNORED, "nothing to withdraw");
    CHECK(zui_caption_feed(&C, 5000, 5200, 0, false, 0) == ZUI_CAP_IGNORED, "NULL text");
    CHECK(zui_caption_feed(0, 0, 0, 0, false, "x") == ZUI_CAP_IGNORED, "NULL caption");
    /* supersede */
    fresh();
    zui_caption_feed(&C, 0, 300, 0, false, "a");
    zui_caption_feed(&C, 0, 300, 1, false, "other");
    CHECK(zui_caption_feed(&C, 500, 800, 0, false, "b") == ZUI_CAP_ADDED, "a new partial");
    CHECK(C.n_live == 2, "it dropped speaker 0's older partial, kept speaker 1's");
    CHECK(C.live[0].speaker == 1 && !strcmp(C.live[1].text, "b"), "what is left");
    CHECK(zui_caption_feed(&C, 400, 900, 0, true, "bee") == ZUI_CAP_ADDED, "a final");
    CHECK(C.n_live == 2 && C.live[1].final && !strcmp(C.live[1].text, "bee"),
          "the final dropped the partial inside it");
    CHECK(zui_caption_feed(&C, 2000, 2100, 0, false, "c") == ZUI_CAP_ADDED, "partial after");
    CHECK(zui_caption_feed(&C, 3000, 3100, 0, true, "d") == ZUI_CAP_ADDED, "a final after it");
    CHECK(C.n_live == 4, "a final does not drop a partial outside it");
    /* order and end clamp */
    fresh();
    zui_caption_feed(&C, 2000, 2500, 0, true, "second");
    zui_caption_feed(&C, 1000, 900, 0, true, "first");
    CHECK(!strcmp(C.live[0].text, "first") && C.live[0].end_ms == 1000,
          "start order; end clamps to start");
    zui_caption_layout(&C, 1000);
    CHECK(!strcmp(zui_caption_line(&C, 0), "first second"), "joined: '%s'",
          zui_caption_line(&C, 0));
    zui_caption_feed(&C, 1000, 1100, 0, true, "first2");
    CHECK(C.n_live == 2, "a final with the same start corrects");
    zui_caption_feed(&C, 2000, 2600, 1, true, "same start other speaker");
    CHECK(C.n_live == 3 && !strcmp(C.live[2].text, "same start other speaker"),
          "equal starts keep arrival order");
    /* controls become spaces, long text is cut on a character */
    fresh();
    zui_caption_feed(&C, 0, 1, 0, true, "a\nb\tc");
    CHECK(!strcmp(C.live[0].text, "a b c"), "controls -> spaces");
    char big[400];
    memset(big, 'x', sizeof big);
    big[253] = (char) 0xE2, big[254] = (char) 0x82, big[255] = (char) 0xAC; /* a euro on the edge */
    big[399] = 0;
    zui_caption_feed(&C, 10, 11, 0, true, big);
    CHECK(strlen(C.live[1].text) == 253, "cut before a split character (%u)",
          (unsigned) strlen(C.live[1].text));
}

static void test_wrap(void)
{
    fresh();
    zui_caption_feed(&C, 0, 1, 0, true, "the quick brown fox jumps");
    CHECK(zui_caption_layout(&C, 120) == 2, "two lines kept");
    CHECK(C.total_lines == 3, "of three made");
    CHECK(!strcmp(zui_caption_line(&C, 0), "brown fox") &&
              !strcmp(zui_caption_line(&C, 1), "jumps"),
          "the last two: '%s' '%s'", zui_caption_line(&C, 0), zui_caption_line(&C, 1));
    CHECK(C.line_w[0] == 108 && C.line_w[1] == 60, "widths without the trailing space");
    CHECK(!strcmp(zui_caption_line(&C, 2), ""), "past the end is empty");
    zui_cap_style_t st = C.style;
    st.max_lines = 4;
    zui_caption_set_style(&C, &st);
    CHECK(zui_caption_layout(&C, 120) == 3 && !strcmp(zui_caption_line(&C, 0), "the quick"),
          "with room, all three");
    CHECK(zui_caption_layout(&C, 108) == 3 && !strcmp(zui_caption_line(&C, 0), "the quick"),
          "exactly 9 characters fit 108 px");
    CHECK(zui_caption_layout(&C, 107) == 4 && C.total_lines == 5 &&
              !strcmp(zui_caption_line(&C, 0), "quick") &&
              !strcmp(zui_caption_line(&C, 3), "jumps"),
          "107 px: one word a line, the last four kept ('%s')", zui_caption_line(&C, 0));
    /* many spaces collapse at the break */
    fresh();
    zui_caption_set_style(&C, &st);
    zui_caption_feed(&C, 0, 1, 0, true, "ab     cd");
    zui_caption_layout(&C, 36);
    CHECK(C.n_lines == 2 && !strcmp(zui_caption_line(&C, 0), "ab") &&
              !strcmp(zui_caption_line(&C, 1), "cd"),
          "spaces never start or end a line");
    /* a word longer than a line */
    fresh();
    zui_caption_set_style(&C, &st);
    zui_caption_feed(&C, 0, 1, 0, true, "abcdefghijklmnopqrstuvwxy");
    zui_caption_layout(&C, 120);
    CHECK(C.n_lines == 3 && !strcmp(zui_caption_line(&C, 0), "abcdefghij") &&
              !strcmp(zui_caption_line(&C, 1), "klmnopqrst") &&
              !strcmp(zui_caption_line(&C, 2), "uvwxy"),
          "long word broken between characters");
    zui_caption_layout(&C, 1);
    CHECK(C.total_lines == 25, "a width below one character: one per line (%u)", C.total_lines);
    zui_caption_layout(&C, -50);
    CHECK(C.total_lines == 25, "negative width acts as tiny");
    /* combining mark kept with its letter */
    fresh();
    zui_caption_set_style(&C, &st);
    zui_caption_feed(&C, 0, 1, 0, true,
                     "abcde\xCC\x81"
                     "fgh");
    zui_caption_layout(&C, 60);
    CHECK(C.n_lines == 2 && !strcmp(zui_caption_line(&C, 0), "abcde\xCC\x81"),
          "the accent stays on its e");
    /* Japanese: no spaces */
    fresh();
    zui_caption_set_style(&C, &st);
    zui_caption_feed(&C, 0, 1, 0, true,
                     "\xE4\xBB\x8A\xE6\x97\xA5\xE3\x81\xAF\xE8\x89\xAF\xE3\x81\x84\xE5\xA4\xA9"
                     "\xE6\xB0\x97\xE3\x81\xA7\xE3\x81\x99\xE3\x80\x82"); /* 今日は良い天気です。 */
    zui_caption_layout(&C, 48);
    CHECK(C.n_lines == 3, "ideographs break (%u lines)", C.n_lines);
    CHECK(!strcmp(zui_caption_line(&C, 0), "\xE4\xBB\x8A\xE6\x97\xA5\xE3\x81\xAF\xE8\x89\xAF"),
          "line 1: four characters");
    CHECK(!strcmp(zui_caption_line(&C, 2), "\xE3\x81\x99\xE3\x80\x82"), "line 3: su + full stop");
    zui_caption_layout(&C, 36);
    CHECK(!strcmp(zui_caption_line(&C, C.n_lines - 1), "\xE3\x81\x99\xE3\x80\x82"),
          "the full stop never starts a line, it takes su with it");
    CHECK(!strcmp(zui_caption_line(&C, C.n_lines - 2), "\xE6\xB0\x97\xE3\x81\xA7"),
          "so the line before is two characters");
    /* mixed: Latin word then ideographs */
    fresh();
    zui_caption_set_style(&C, &st);
    zui_caption_feed(&C, 0, 1, 0, true, "ok\xE4\xBB\x8A\xE6\x97\xA5");
    zui_caption_layout(&C, 36);
    CHECK(C.n_lines == 2 && !strcmp(zui_caption_line(&C, 0), "ok\xE4\xBB\x8A"),
          "break before an ideograph after Latin");
    /* layout cache */
    fresh();
    zui_caption_feed(&C, 0, 1, 0, true, "one");
    zui_caption_layout(&C, 500);
    C.line[0][0] = 'X';
    zui_caption_layout(&C, 500);
    CHECK(C.line[0][0] == 'X', "same width, nothing new: lines kept");
    zui_caption_layout(&C, 400);
    CHECK(C.line[0][0] == 'o', "a new width lays out again");
    C.line[0][0] = 'X';
    zui_caption_feed(&C, 5, 6, 0, false, "two");
    zui_caption_layout(&C, 400);
    CHECK(!strcmp(C.line[0], "one two"), "new text lays out again");
}

static void test_speakers(void)
{
    fresh();
    zui_cap_style_t st = C.style;
    st.max_lines = 4;
    zui_caption_set_style(&C, &st);
    zui_caption_feed(&C, 0, 500, 0, true, "hi there");
    zui_caption_feed(&C, 600, 900, 1, true, "hello");
    zui_caption_feed(&C, 1000, 1200, 1, false, "again");
    zui_caption_layout(&C, 1000);
    CHECK(C.n_lines == 2 && !strcmp(zui_caption_line(&C, 0), "hi there") &&
              !strcmp(zui_caption_line(&C, 1), ">> hello again"),
          "a speaker change starts a line with >> ('%s' '%s')", zui_caption_line(&C, 0),
          zui_caption_line(&C, 1));
    zui_caption_set_speaker(&C, 1, "Ana");
    zui_caption_layout(&C, 1000);
    CHECK(!strcmp(zui_caption_line(&C, 1), "Ana: hello again"), "a named speaker");
    zui_caption_set_speaker(&C, 0, "Bo");
    zui_caption_layout(&C, 1000);
    CHECK(!strcmp(zui_caption_line(&C, 0), "Bo: hi there"), "the first speaker named when known");
    zui_caption_feed(&C, 1300, 1400, 0, true, "bye");
    zui_caption_layout(&C, 1000);
    CHECK(C.n_lines == 3 && !strcmp(zui_caption_line(&C, 2), "Bo: bye"), "back to Bo");
    zui_caption_feed(&C, 1500, 1600, 900, true, "far");
    zui_caption_layout(&C, 1000);
    CHECK(!strcmp(zui_caption_line(&C, 3), ">> far"), "a speaker past the name table");
    zui_caption_clear(&C);
    CHECK(zui_caption_layout(&C, 1000) == 0 && zui_caption_transcript_count(&C) == 4,
          "clear empties the screen, keeps the transcript");
}

static void test_tick(void)
{
    fresh();
    zui_caption_feed(&C, 0, 1000, 0, true, "done");
    zui_caption_feed(&C, 500, 1000, 1, false, "maybe");
    zui_caption_tick(&C, 4999);
    CHECK(C.n_live == 2, "both still up at 4999");
    zui_caption_tick(&C, 5000);
    CHECK(C.n_live == 1 && !C.live[0].final, "the final leaves hold_ms after it ends");
    zui_caption_tick(&C, 8999);
    CHECK(C.n_live == 1, "a partial stays twice as long");
    zui_caption_tick(&C, 9000);
    CHECK(C.n_live == 0 && C.now_ms == 9000, "a stale partial leaves");
    CHECK(zui_caption_layout(&C, 500) == 0, "nothing on screen");
    C.hold_ms = 0xFFFFFFFFu;
    zui_caption_feed(&C, 0xFFFFFF00u, 0xFFFFFFF0u, 0, true, "edge");
    zui_caption_tick(&C, 0xFFFFFFFFu);
    CHECK(C.n_live == 1, "no wrap-around at the clock's end");
}

static void test_capacity_and_transcript(void)
{
    fresh();
    for (uint32_t i = 0; i < ZUI_CAP_LIVE + 1; i++) {
        char t[8];
        snprintf(t, sizeof t, "p%u", i);
        zui_caption_feed(&C, i * 10, i * 10 + 5, (uint16_t) i, false, t);
    }
    CHECK(C.n_live == ZUI_CAP_LIVE && C.live_dropped == 1, "live is bounded");
    CHECK(!strcmp(C.live[0].text, "p1"), "the oldest went");
    fresh();
    zui_caption_feed(&C, 0, 1, 0, true, "f0");
    for (uint32_t i = 1; i < ZUI_CAP_LIVE + 1; i++)
        zui_caption_feed(&C, i, i, (uint16_t) i, false, "p");
    CHECK(C.n_live == ZUI_CAP_LIVE && C.live[0].speaker == 1 && C.live[0].final == false,
          "when full, a final goes before any partial");
    fresh();
    zui_caption_set_speaker(&C, 2, "Ana");
    for (uint32_t i = 0; i < ZUI_CAP_TRANSCRIPT + 6; i++) {
        char t[16];
        snprintf(t, sizeof t, "line %u", i);
        zui_caption_feed(&C, i * 1000, i * 1000 + 900, (uint16_t) (i % 3), true, t);
    }
    CHECK(zui_caption_transcript_count(&C) == ZUI_CAP_TRANSCRIPT, "the ring is full");
    CHECK(C.ring_dropped == 6, "six dropped");
    CHECK(!strcmp(zui_caption_transcript_get(&C, 0)->text, "line 6"), "oldest kept is line 6");
    CHECK(!strcmp(zui_caption_transcript_get(&C, ZUI_CAP_TRANSCRIPT - 1)->text, "line 69"),
          "newest is line 69");
    CHECK(zui_caption_transcript_get(&C, ZUI_CAP_TRANSCRIPT) == 0, "past the end");
    static char out[8192];
    uint32_t n = zui_caption_transcript_text(&C, out, sizeof out);
    CHECK(n == strlen(out), "length returned");
    CHECK(!strncmp(out, "[0:06] line 6\n[0:07] line 7\n[0:08] Ana: line 8\n", 44),
          "format: '%.60s'", out);
    CHECK(strstr(out, "[1:09] line 69\n") != 0, "minutes");
    fresh();
    zui_caption_feed(&C, 3725000, 3726000, 0, true, "late");
    zui_caption_transcript_text(&C, out, sizeof out);
    CHECK(!strcmp(out, "[1:02:05] late\n"), "hours: '%s'", out);
    char tiny[8];
    n = zui_caption_transcript_text(&C, tiny, sizeof tiny);
    CHECK(n == 7 && !strcmp(tiny, "[1:02:0"), "cut to fit");
    CHECK(zui_caption_transcript_text(&C, 0, 5) == 0 &&
              zui_caption_transcript_text(&C, tiny, 0) == 0,
          "no room");
}

#define SW 1000
#define SH 600
static uint32_t PX[SW * SH];

static void test_draw(void)
{
    zui_surface_t s;
    zui_surface_init(&s, PX, SW, SH, SW);
    zui_rect_t area = {0, 0, SW, SH};
    fresh();
    CHECK(zui_caption_box(&C, area).w == 0, "nothing to show: no box");
    for (int i = 0; i < SW * SH; i++) PX[i] = 0x123456;
    zui_caption_draw(&C, &s, area);
    CHECK(PX[SH / 2 * SW + SW / 2] == 0x123456, "and nothing drawn");
    zui_caption_feed(&C, 0, 1, 0, true, "hello world");
    zui_rect_t b = zui_caption_box(&C, area);
    CHECK(b.w == 148 && b.h == 46, "box size %dx%d", b.w, b.h);
    CHECK(b.x == 426 && b.y == 530, "centred at the bottom (%d,%d)", b.x, b.y);
    zui_cap_style_t st = C.style;
    st.position = ZUI_CAP_TOP;
    zui_caption_set_style(&C, &st);
    b = zui_caption_box(&C, area);
    CHECK(b.y == 24, "top");
    zui_rect_t off = {100, 50, SW - 100, SH - 50};
    b = zui_caption_box(&C, off);
    CHECK(b.y == 74 && b.x == 100 + (900 - 148) / 2, "the area's own origin");
    /* opacity */
    st.position = ZUI_CAP_BOTTOM;
    st.bg_q8 = 256;
    zui_caption_set_style(&C, &st);
    b = zui_caption_box(&C, area);
    zui_caption_draw(&C, &s, area);
    uint32_t mid = PX[(b.y + b.h / 2) * SW + b.x + b.w / 2];
    CHECK(mid == ZUI_VOID, "opaque: the #002911 ground (%06x)", mid);
    CHECK(PX[(b.y - 2) * SW + b.x + 10] == 0x123456, "outside the box untouched");
    CHECK(PX[b.y * SW + b.x + b.w / 2] != ZUI_VOID, "an engraved edge");
    for (int i = 0; i < SW * SH; i++) PX[i] = 0x123456;
    st.bg_q8 = 0;
    zui_caption_set_style(&C, &st);
    zui_caption_draw(&C, &s, area);
    CHECK(PX[(b.y + b.h / 2) * SW + b.x + b.w / 2] == 0x123456, "opacity 0: no ground");
    st.bg_q8 = 128;
    zui_caption_set_style(&C, &st);
    zui_caption_draw(&C, &s, area);
    mid = PX[(b.y + b.h / 2) * SW + b.x + b.w / 2];
    CHECK(mid == zui_mix(0x123456, ZUI_VOID, 127), "half: a blend (%06x)", mid);
    zui_motion_set_flag(ZUI_PREF_REDUCE_TRANSPARENCY, true);
    zui_caption_draw(&C, &s, area);
    CHECK(PX[(b.y + b.h / 2) * SW + b.x + b.w / 2] == ZUI_VOID, "reduce transparency: opaque");
    zui_motion_reset();
    st.high_contrast = true;
    st.bg_q8 = 10;
    zui_caption_set_style(&C, &st);
    zui_caption_draw(&C, &s, area);
    CHECK(PX[(b.y + b.h / 2) * SW + b.x + b.w / 2] ==
              zui_scheme_color(ZUI_SCHEME_HIGH_CONTRAST, ZUI_C_VOID),
          "high contrast: its ground, opaque");
    CHECK(PX[(b.y) * SW + b.x + b.w / 2] == zui_scheme_color(ZUI_SCHEME_HIGH_CONTRAST, ZUI_C_EDGE),
          "high contrast: a strong edge");

    /* with a font: zui text measurement and real glyphs */
    static zui_glyph_cache_t cache;
    static ttf_font_t font;
    CHECK(ttf_parse(&font, ZUI_TTF, sizeof ZUI_TTF), "fixture font");
    zui_glyph_cache_init(&cache);
    zui_text_t t = {&font, &cache, 16, false};
    zui_caption_init(&C, &t);
    zui_cap_style_default(&st);
    st.size_px = 20;
    st.bg_q8 = 256;
    zui_caption_set_style(&C, &st);
    CHECK(zui_caption_measure(&C, "AA", 2) == 24, "measured with the font at the style size");
    CHECK(zui_caption_line_h(&C) == 16 + 4 + 5, "line height from the font");
    zui_caption_feed(&C, 0, 1, 0, true, "AAAA AAAA AAAA");
    CHECK(zui_caption_layout(&C, 110) == 2 && C.total_lines == 2 &&
              !strcmp(zui_caption_line(&C, 0), "AAAA AAAA"),
          "breaks by the font's advances ('%s')", zui_caption_line(&C, 0));
    for (int i = 0; i < SW * SH; i++) PX[i] = 0;
    zui_caption_draw(&C, &s, area);
    b = zui_caption_box(&C, area);
    uint32_t ink = 0;
    for (int y = b.y; y < b.y + b.h; y++)
        for (int x = b.x; x < b.x + b.w; x++)
            if (PX[y * SW + x] != ZUI_VOID && PX[y * SW + x] != 0) ink++;
    CHECK(ink > 200, "glyphs drawn (%u px)", ink);
    static uint32_t A[SW * SH];
    memcpy(A, PX, sizeof A);
    for (int i = 0; i < SW * SH; i++) PX[i] = 0;
    zui_caption_draw(&C, &s, area);
    CHECK(memcmp(A, PX, sizeof A) == 0, "the same captions draw the same pixels");
}

int main(void)
{
    zui_motion_reset();
    test_style();
    test_partials();
    test_wrap();
    test_speakers();
    test_tick();
    test_capacity_and_transcript();
    test_draw();
    printf("%s test_zui_caption: %d/%d checks\n", fails ? "[FAIL]" : "[PASS]", checks - fails,
           checks);
    return fails ? 1 : 0;
}
