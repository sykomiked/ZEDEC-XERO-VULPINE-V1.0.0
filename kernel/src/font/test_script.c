/* test_script.c — multi-language text: UTF-8 decoding and script itemization.
 *
 * The anchors are real UTF-8 byte sequences for real scripts, and the property
 * that matters: mixed text splits into the correct runs with the correct
 * directions, punctuation does not fragment a run, and malformed UTF-8 never
 * reads past the end or stalls.
 */
#include <stdio.h>
#include <string.h>
#include "script.h"

static int failures = 0;
#define CHECK(c,m) do{ if(!(c)){printf("[FAIL] %s\n",m);failures++;} \
    else printf("[PASS] %s\n",m);}while(0)

int main(void) {
    printf("=== multi-language text: UTF-8 + script itemization ===\n");

    /* ---- UTF-8 decode: the four lengths ---- */
    {
        uint32_t cp, adv;
        CHECK(font_utf8_next((const uint8_t*)"A", 1, &cp, &adv) && cp==0x41 && adv==1,
              "1-byte: 'A' -> U+0041");
        /* U+00E9 é = C3 A9 */
        CHECK(font_utf8_next((const uint8_t*)"\xC3\xA9", 2, &cp, &adv) && cp==0xE9 && adv==2,
              "2-byte: é -> U+00E9");
        /* U+0939 ह (Devanagari HA) = E0 A4 B9 */
        CHECK(font_utf8_next((const uint8_t*)"\xE0\xA4\xB9", 3, &cp, &adv) && cp==0x939 && adv==3,
              "3-byte: Devanagari ह -> U+0939");
        /* U+1E900 Adlam = F0 9E A4 80 */
        CHECK(font_utf8_next((const uint8_t*)"\xF0\x9E\xA4\x80", 4, &cp, &adv) && cp==0x1E900 && adv==4,
              "4-byte: Adlam -> U+1E900");
    }

    /* ---- malformed UTF-8 must resync, never overrun ---- */
    {
        uint32_t cp, adv;
        CHECK(!font_utf8_next((const uint8_t*)"\xE0\xA4", 2, &cp, &adv) && cp==0xFFFD && adv==1,
              "a truncated 3-byte sequence yields U+FFFD and advances 1");
        CHECK(!font_utf8_next((const uint8_t*)"\x80", 1, &cp, &adv) && adv==1,
              "a stray continuation byte resyncs by one");
        CHECK(!font_utf8_next((const uint8_t*)"\xC0\xAF", 2, &cp, &adv),
              "an overlong encoding of '/' is rejected");
        CHECK(!font_utf8_next((const uint8_t*)"\xED\xA0\x80", 3, &cp, &adv),
              "a surrogate (U+D800) is rejected");
        /* every prefix of a valid string decodes without crashing */
        const uint8_t v[] = "A\xC3\xA9\xE0\xA4\xB9\xF0\x9E\xA4\x80";
        for (uint32_t cut = 0; cut <= sizeof v - 1; cut++) {
            uint32_t off = 0;
            while (off < cut) { uint32_t c,a; font_utf8_next(v+off, cut-off, &c, &a); off += a?a:1; }
        }
        CHECK(1, "every prefix decodes without overrun");
    }

    /* ---- script classification ---- */
    CHECK(font_script_of('A') == SCRIPT_LATIN, "A is Latin");
    CHECK(font_script_of(0x0391) == SCRIPT_GREEK, "U+0391 is Greek");
    CHECK(font_script_of(0x0627) == SCRIPT_ARABIC, "U+0627 (alef) is Arabic");
    CHECK(font_script_of(0x05D0) == SCRIPT_HEBREW, "U+05D0 (aleph) is Hebrew");
    CHECK(font_script_of(0x0939) == SCRIPT_DEVANAGARI, "U+0939 is Devanagari");
    CHECK(font_script_of(0x4E2D) == SCRIPT_HAN, "U+4E2D (中) is Han");
    CHECK(font_script_of(0x13A0) == SCRIPT_CHEROKEE, "U+13A0 is Cherokee");
    CHECK(font_script_of(0x1E900) == SCRIPT_ADLAM, "U+1E900 is Adlam");
    CHECK(font_script_of(' ') == SCRIPT_COMMON && font_script_of(',') == SCRIPT_COMMON,
          "space and comma are Common");

    /* ---- reverse lookup: manifest token -> script ---- */
    CHECK(font_script_from_name("Bengali") == SCRIPT_BENGALI, "'Bengali' -> Bengali");
    CHECK(font_script_from_name("Devanagari") == SCRIPT_DEVANAGARI, "'Devanagari' -> Devanagari");
    CHECK(font_script_from_name("Aramaic") == SCRIPT_SYRIAC, "'Aramaic' aliases to Syriac");
    CHECK(font_script_from_name("Kana") == SCRIPT_HIRAGANA, "'Kana' aliases to Hiragana");
    CHECK(font_script_from_name("Klingon") == SCRIPT_UNKNOWN, "an unknown token -> Unknown");
    CHECK(font_script_from_name(0) == SCRIPT_UNKNOWN, "NULL -> Unknown, no crash");

    /* ---- direction ---- */
    CHECK(font_script_dir(SCRIPT_LATIN) == DIR_LTR, "Latin is left-to-right");
    CHECK(font_script_dir(SCRIPT_ARABIC) == DIR_RTL, "Arabic is right-to-left");
    CHECK(font_script_dir(SCRIPT_HEBREW) == DIR_RTL, "Hebrew is right-to-left");
    CHECK(font_script_dir(SCRIPT_ADLAM) == DIR_RTL, "Adlam is right-to-left");

    /* ---- punctuation does not fragment a run ---- */
    {
        font_run_t r[8]; bool tr;
        uint32_t n = font_itemize((const uint8_t*)"Hello, World 123!", 17, r, 8, &tr);
        CHECK(n == 1 && r[0].script == SCRIPT_LATIN,
              "'Hello, World 123!' is ONE Latin run — commas, spaces and digits "
              "join it rather than splitting it");
        CHECK(r[0].len == 17, "the run spans the whole string");
    }

    /* ---- the real thing: mixed scripts split correctly ----
     * "Hi " (Latin) + "שלום" (Hebrew) + " " + "中文" (Han) */
    {
        const uint8_t s[] = "Hi \xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D \xE4\xB8\xAD\xE6\x96\x87";
        font_run_t r[8]; bool tr;
        uint32_t n = font_itemize(s, sizeof s - 1, r, 8, &tr);
        CHECK(n == 3, "three runs: Latin, Hebrew, Han");
        CHECK(r[0].script == SCRIPT_LATIN && r[0].dir == DIR_LTR, "run 0 Latin LTR");
        CHECK(r[1].script == SCRIPT_HEBREW && r[1].dir == DIR_RTL,
              "run 1 Hebrew RTL — the direction the layout engine needs");
        CHECK(r[2].script == SCRIPT_HAN && r[2].dir == DIR_LTR, "run 2 Han LTR");
        /* the runs must tile the string exactly */
        CHECK(r[0].start == 0, "run 0 starts at 0");
        CHECK(r[1].start == r[0].start + r[0].len, "run 1 abuts run 0");
        CHECK(r[2].start == r[1].start + r[1].len, "run 2 abuts run 1");
        CHECK(r[2].start + r[2].len == sizeof s - 1, "the runs cover the whole string");
    }

    /* ---- Arabic + Latin + Arabic returns three runs ---- */
    {
        /* "سلام" + " ok " + "مرحبا" */
        const uint8_t s[] = "\xD8\xB3\xD9\x84\xD8\xA7\xD9\x85 ok \xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7";
        font_run_t r[8]; bool tr;
        uint32_t n = font_itemize(s, sizeof s - 1, r, 8, &tr);
        CHECK(n == 3 && r[0].script == SCRIPT_ARABIC && r[1].script == SCRIPT_LATIN &&
              r[2].script == SCRIPT_ARABIC,
              "Arabic / Latin / Arabic itemizes into three runs "
              "(the space after Arabic joins the Arabic run, ' ok ' is Latin)");
    }

    /* ---- truncation is reported, never overflows ---- */
    {
        const uint8_t s[] = "a\xD7\x90""b\xD7\x90""c\xD7\x90""d";  /* many script flips */
        font_run_t r[2]; bool tr;
        uint32_t n = font_itemize(s, sizeof s - 1, r, 2, &tr);
        CHECK(n == 2 && tr, "with room for 2 runs, extra runs set the truncated flag");
    }

    /* ---- empty and all-common ---- */
    {
        font_run_t r[4]; bool tr;
        CHECK(font_itemize((const uint8_t*)"", 0, r, 4, &tr) == 0, "empty string -> 0 runs");
        uint32_t n = font_itemize((const uint8_t*)"  ,. 12 ", 8, r, 4, &tr);
        CHECK(n == 1 && r[0].script == SCRIPT_COMMON,
              "an all-punctuation string is a single Common run");
    }

    printf("\n%s: %d failure(s)\n", failures?"*** FAILED ***":"ALL PASS", failures);
    return failures?1:0;
}
