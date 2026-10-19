/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zui_rosetta.c — Rosetta, the language bridge (zui_rosetta.h R1..R6):
 * every translated locale to every other through the pivot, the bridges of
 * the script-only locales, the provider for gaps, trust, and Babel. */
#include <stdio.h>
#include <string.h>

#include "zui_rosetta.h"

static int fails, checks;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            fails++;                                                                               \
            if (fails <= 40) {                                                                     \
                printf("  FAIL (%s:%d) ", __FILE__, __LINE__);                                     \
                printf(__VA_ARGS__);                                                               \
                printf("\n");                                                                      \
            }                                                                                      \
        }                                                                                          \
    } while (0)

#define N(a) ((uint32_t) (sizeof(a) / sizeof((a)[0])))
#define LOC  zui_locale_find

/* What the screen reader says, in its English (zui_reader.c). */
static const char *const SAID[] = {
    "Save, button, shortcut Command S",
    "Hidden, check box, checked",
    "alpha.txt, 1 of 3",
    "Settings, heading level 1",
    "Files, list, 12 items",
    "Notes, text field, 1 character",
    "AI suggestion, about 40 tokens, from Claude",
    "screen reader on",
    "no next heading",
    "AI suggestion, accept with Command Return, or reject",
    "progress bar, 75 percent",
    "menu item, disabled",
};

/* the reader's words as a locale says them: zui_locale_phrase */
static void said_in(const zui_locale_t *l, const char *en, char *out, uint32_t cap)
{
    zui_locale_phrase(l, en, out, cap);
}

static void test_pivot(void)
{
    static char a[2048], b[2048], c[2048];
    uint32_t pairs = 0, exact = 0;
    for (uint32_t i = 0; i < zui_locale_count_all(); i++) {
        const zui_locale_t *from = zui_locale_at(i);
        if (zui_locale_review(from) == ZUI_REVIEW_UNTRANSLATED) continue;
        for (uint32_t s = 0; s < N(SAID); s++) {
            said_in(from, SAID[s], a, sizeof a);
            /* back to English: exactly what the reader said */
            zui_rosetta_report_t rep;
            zui_rosetta_translate(0, from, LOC("en"), a, b, sizeof b, &rep);
            CHECK(!strcmp(b, SAID[s]), "%s -> en: \"%s\" gave \"%s\"", zui_locale_tag(from), a, b);
            CHECK(rep.parts == rep.bridged + rep.kept && rep.provided == 0, "%s: counts add up",
                  zui_locale_tag(from));
            /* to every other translated locale: what that locale says itself */
            for (uint32_t j = 0; j < zui_locale_count_all(); j++) {
                const zui_locale_t *to = zui_locale_at(j);
                if (zui_locale_review(to) == ZUI_REVIEW_UNTRANSLATED) continue;
                said_in(to, SAID[s], c, sizeof c);
                zui_rosetta_translate(0, from, to, a, b, sizeof b, 0);
                pairs++;
                if (!strcmp(b, c)) exact++;
            }
        }
    }
    CHECK(exact == pairs, "pivot: %u of %u locale pairs exact", exact, pairs);
}

static void test_numbers(void)
{
    static char a[512], b[512];
    /* Persian writes Persian digits; Hindi gets its own way of writing them */
    zui_locale_position(LOC("fa"), 3, 12, a, sizeof a);
    zui_rosetta_translate(0, LOC("fa"), LOC("hi"), a, b, sizeof b, 0);
    zui_locale_position(LOC("hi"), 3, 12, a, sizeof a);
    CHECK(!strcmp(a, b), "fa -> hi position: %s / %s", a, b);
    zui_locale_count(LOC("ru"), ZUI_COUNT_ITEM, 1234, a, sizeof a);
    zui_rosetta_translate(0, LOC("ru"), LOC("de"), a, b, sizeof b, 0);
    zui_locale_count(LOC("de"), ZUI_COUNT_ITEM, 1234, a, sizeof a);
    CHECK(!strcmp(a, b), "ru -> de grouped count: %s / %s", a, b);
    zui_locale_count(LOC("zh-Hant"), ZUI_COUNT_TOKEN, 7, a, sizeof a);
    zui_rosetta_translate(0, LOC("zh-Hant"), LOC("en"), a, b, sizeof b, 0);
    CHECK(!strcmp(b, "7 tokens"), "zh-Hant -> en: %s", b);
}

static void test_bridges(void)
{
    static char a[512], b[512];
    zui_rosetta_report_t rep;
    said_in(LOC("ru"), "Hidden, check box, checked", a, sizeof a);
    zui_rosetta_translate(0, LOC("ru"), LOC("hbo"), a, b, sizeof b, &rep);
    said_in(LOC("he"), "Hidden, check box, checked", a, sizeof a);
    CHECK(!strcmp(b, a) && rep.spoken == LOC("he"), "Biblical Hebrew is bridged by Hebrew");
    CHECK(zui_rosetta_target(LOC("arc")) == LOC("syr") &&
              zui_rosetta_target(LOC("peo")) == LOC("fa") &&
              zui_rosetta_target(LOC("akk")) == LOC("en") &&
              zui_rosetta_target(LOC("la")) == LOC("la") &&
              zui_rosetta_target(LOC("sa-Sidd")) == LOC("sa"),
          "bridges");
    for (uint32_t i = 0; i < zui_locale_count_all(); i++) {
        const zui_locale_t *t = zui_rosetta_target(zui_locale_at(i));
        CHECK(zui_locale_review(t) != ZUI_REVIEW_UNTRANSLATED, "%s bridges to a translated locale",
              zui_locale_tag(zui_locale_at(i)));
    }
    /* reading from a script-only locale reads its English words */
    zui_rosetta_translate(0, LOC("akk"), LOC("de"), "list, 3 items", b, sizeof b, &rep);
    said_in(LOC("de"), "list, 3 items", a, sizeof a);
    CHECK(!strcmp(a, b) && rep.bridged == 2, "akk -> de: %s", b);
}

/* a stand-in translator: wraps the text so the test can see it was used */
static int provider_calls;
static uint32_t wrap(const zui_locale_t *from, const zui_locale_t *to, const char *text, uint32_t n,
                     char *out, uint32_t cap, void *ctx)
{
    (void) from;
    (void) ctx;
    provider_calls++;
    if (!strncmp(text, "decline", 7)) return 0;
    return (uint32_t) snprintf(out, cap, "[%s:%.*s]", zui_locale_tag(to), (int) n, text);
}

static void test_gaps_and_trust(void)
{
    static char b[512];
    zui_rosetta_report_t rep;
    zui_rosetta_translate(0, LOC("en"), LOC("fr"), "Quarterly report, button", b, sizeof b, &rep);
    CHECK(rep.parts == 2 && rep.bridged == 1 && rep.kept == 1 && rep.provided == 0,
          "without a provider the name is kept");
    CHECK(rep.trust == ZUI_REVIEW_MACHINE, "French words are machine-made");
    zui_rosetta_t r = {wrap, 0, 0, 0};
    provider_calls = 0;
    zui_rosetta_translate(&r, LOC("en"), LOC("fr"), "Quarterly report, button", b, sizeof b, &rep);
    CHECK(!strncmp(b, "[fr:Quarterly report]", 21) && rep.provided == 1 && rep.kept == 0 &&
              provider_calls == 1,
          "the provider fills the gap: %s", b);
    zui_rosetta_translate(&r, LOC("en"), LOC("fr"), "decline this, button", b, sizeof b, &rep);
    CHECK(rep.kept == 1 && rep.provided == 0 && !strncmp(b, "decline this", 12),
          "a declined part is kept: %s", b);
    zui_rosetta_translate(&r, LOC("de"), LOC("en"), "Knopf", b, sizeof b, &rep);
    CHECK(rep.trust == ZUI_REVIEW_MACHINE, "a provided part is machine-made");
    zui_rosetta_translate(0, LOC("de"), LOC("en"), "Schaltfläche", b, sizeof b, &rep);
    CHECK(!strcmp(b, "button") && rep.trust == ZUI_REVIEW_SOURCE && rep.spoken == LOC("en"),
          "English through the pivot is the source: %s", b);
    zui_rosetta_translate(&r, LOC("de"), LOC("de"), "Speichern", b, sizeof b, &rep);
    CHECK(!strcmp(b, "Speichern") && rep.provided == 0, "same language: nothing to translate");
    zui_rosetta_translate(0, LOC("en"), LOC("ru"), "Error: Disk is full", b, sizeof b, &rep);
    CHECK(!strcmp(b, "Ошибка: Disk is full") && rep.kept == 1, "error message: %s", b);
    zui_rosetta_translate(0, LOC("en"), LOC("ru"), 0, b, sizeof b, &rep);
    CHECK(b[0] == 0 && rep.parts == 0, "no text");
    /* cut at a whole character */
    char small[8];
    zui_rosetta_translate(0, LOC("en"), LOC("ru"), "button", small, sizeof small, 0);
    CHECK(strlen(small) <= 7 && (strlen(small) % 2) == 0, "cut whole: %s", small);
}

static uint32_t explain(const char *code, uint32_t n, char *english, uint32_t cap, void *ctx)
{
    (void) ctx;
    if (!n) return 0;
    (void) code;
    return (uint32_t) snprintf(english, cap, "Adds two numbers, 3 items");
}

static void test_babel(void)
{
    static char b[512], c[512];
    zui_rosetta_report_t rep;
    zui_rosetta_t r = {0, 0, explain, 0};
    uint32_t k = zui_rosetta_explain(&r, LOC("es"), "x = a + b", 9, b, sizeof b, &rep);
    zui_locale_count(LOC("es"), ZUI_COUNT_ITEM, 3, c, sizeof c);
    CHECK(k > 0 && strstr(b, "Adds two numbers") && strstr(b, c) && rep.bridged == 1 &&
              rep.kept == 1 && rep.trust == ZUI_REVIEW_MACHINE,
          "Babel's explanation, carried: %s", b);
    CHECK(zui_rosetta_explain(&r, LOC("es"), "", 0, b, sizeof b, &rep) == 0 && b[0] == 0,
          "nothing to explain");
    zui_rosetta_t none = {0, 0, 0, 0};
    CHECK(zui_rosetta_explain(&none, LOC("es"), "x", 1, b, sizeof b, 0) == 0, "no Babel");
}

int main(void)
{
    test_pivot();
    test_numbers();
    test_bridges();
    test_gaps_and_trust();
    test_babel();
    printf("%s test_zui_rosetta: %d/%d checks\n", fails ? "[FAIL]" : "[PASS]", checks - fails,
           checks);
    return fails ? 1 : 0;
}
