/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_i18n_enochian.c — the Enochian core / operator-language edge.
 * Build: gcc -std=c11 -DTEST_HOST -Isrc/i18n -Isrc/swarm (see LANGUAGES.md). */
#include <stdio.h>
#include <string.h>
#include "i18n_enochian.h"
#include "i18n_msgid.h"

static unsigned pass, fail;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        if (c)                                                                                     \
            pass++;                                                                                \
        else {                                                                                     \
            fail++;                                                                                \
            printf("[FAIL] %s:%d: ", __FILE__, __LINE__);                                          \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)
#define EQS(a, b) CHECK(strcmp((a), (b)) == 0, "got \"%s\" want \"%s\"", (a), (b))

static void test_render(void)
{
    const char *words[] = {"OL", "SONF", "VORSG", "GOHO", "IAD", "BALT", "MADRIAX"};
    char a[128], b[128];
    for (int form = 0; form < 2; form++) {
        for (unsigned i = 0; i < sizeof words / sizeof words[0]; i++) {
            uint32_t n = (uint32_t) strlen(words[i]);
            int32_t r = i18n_enochian_render(words[i], n, (i18n_en_form_t) form, a, sizeof a);
            int32_t s = form ? swarm_en_iast(words[i], n, b, sizeof b)
                             : swarm_en_sanskrit(words[i], n, b, sizeof b);
            CHECK(r == s && r > 0, "len %d vs %d for %s", (int) r, (int) s, words[i]);
            EQS(a, b);
        }
        /* several words: each word rendered alone, joined by one space */
        const char *text = "  OL   SONF\tVORSG\n";
        char want[256] = "";
        for (unsigned i = 0; i < 3; i++) {
            int32_t s = form
                            ? swarm_en_iast(words[i], (uint32_t) strlen(words[i]), b, sizeof b)
                            : swarm_en_sanskrit(words[i], (uint32_t) strlen(words[i]), b, sizeof b);
            CHECK(s > 0, "word");
            if (i) strcat(want, " ");
            strcat(want, b);
        }
        int32_t r =
            i18n_enochian_render(text, (uint32_t) strlen(text), (i18n_en_form_t) form, a, sizeof a);
        CHECK(r == (int32_t) strlen(want), "multi len");
        EQS(a, want);
        /* too small: -1, and a NUL-terminated prefix that ends at a word */
        char small[6];
        CHECK(i18n_enochian_render(text, (uint32_t) strlen(text), (i18n_en_form_t) form, small,
                                   sizeof small) == -1,
              "overflow");
        CHECK(strlen(small) < sizeof small, "terminated");
    }
    /* non-letters are dropped, an empty result stays empty */
    CHECK(i18n_enochian_render("123 !!", 6, I18N_EN_DEVANAGARI, a, sizeof a) == 0 && a[0] == 0,
          "non-letters dropped");
    CHECK(i18n_enochian_render("OL", 2, I18N_EN_IAST, a, 0) == -1, "cap 0");
    /* Devanagari output is Devanagari: every code point in U+0900..U+097F or a space */
    i18n_enochian_render("MADRIAX OL", 10, I18N_EN_DEVANAGARI, a, sizeof a);
    int ok = 1;
    for (uint32_t i = 0; a[i];) {
        uint32_t cp;
        int32_t n = i18n_utf8_decode((const uint8_t *) a + i, (uint32_t) strlen(a + i), &cp);
        if (n <= 0 || !(cp == ' ' || (cp >= 0x0900 && cp <= 0x097F))) ok = 0;
        i += n > 0 ? (uint32_t) n : 1u;
    }
    CHECK(ok, "Devanagari block only: %s", a);
}

static void test_edge(void)
{
    swarm_lang_table_t t;
    swarm_lang_init(&t);
    i18n_msg_info_t info;
    const char *key_before = i18n_msg_key(I18N_PAY_CONFIRM_TITLE);
    EQS(i18n_locale_tag(i18n_edge_locale(&t)), "en");
    EQS(i18n_edge_msg(&t, I18N_PAY_CONFIRM_TITLE, &info), "Confirm payment");
    CHECK(info.review == I18N_REVIEW_SOURCE, "en is the source");

    CHECK(swarm_lang_install(&t, "sa", "Sanskrit", 7) >= 0, "install sa");
    CHECK(swarm_lang_install(&t, "sw", "Kiswahili", 8) >= 0, "install sw");
    CHECK(swarm_lang_set_operator(&t, "sa"), "operator sa");
    EQS(i18n_locale_tag(i18n_edge_locale(&t)), "sa");
    EQS(i18n_edge_msg(&t, I18N_PAY_CONFIRM_TITLE, &info),
        "\xE0\xA4\xA7\xE0\xA4\xA8\xE0\xA4\xAA\xE0\xA5\x8D\xE0\xA4\xB0\xE0\xA4\xA6\xE0\xA4\xBE"
        "\xE0\xA4\xA8\xE0\xA4\x82 \xE0\xA4\xA8\xE0\xA4\xBF\xE0\xA4\xB6\xE0\xA5\x8D\xE0\xA4\x9A"
        "\xE0\xA4\xBF\xE0\xA4\xA8\xE0\xA5\x81");
    CHECK(info.review == I18N_REVIEW_MACHINE && !info.fallback, "sa is machine, not fallback");

    /* the core is unchanged by the operator language */
    EQS(swarm_lang_core(), "enochian");
    EQS(i18n_msg_key(I18N_PAY_CONFIRM_TITLE), key_before);
    CHECK(i18n_msg_id("PAY_CONFIRM_TITLE") == I18N_PAY_CONFIRM_TITLE, "id stable");

    CHECK(swarm_lang_set_operator(&t, "sw"), "operator sw");
    EQS(i18n_edge_msg(&t, I18N_PAY_CONFIRM_TITLE, &info), "Thibitisha malipo");
    EQS(swarm_lang_core(), "enochian");
    EQS(i18n_msg_key(I18N_PAY_CONFIRM_TITLE), key_before);

    /* an operator language with no catalog falls back to en, and says so */
    CHECK(swarm_lang_install(&t, "kr", "Kanuri", 9) >= 0, "install kr");
    CHECK(swarm_lang_set_operator(&t, "kr"), "operator kr");
    EQS(i18n_locale_tag(i18n_edge_locale(&t)), "kr");
    EQS(i18n_edge_msg(&t, I18N_PAY_CONFIRM_TITLE, &info), "Confirm payment");
    CHECK(info.fallback, "kr falls back");
    CHECK(i18n_edge_locale(0) == i18n_locale_default(), "null table");
}

int main(void)
{
    test_render();
    test_edge();
    printf("[%s] test_i18n_enochian: %u passed, %u failed\n", fail ? "FAIL" : "PASS", pass, fail);
    return fail ? 1 : 0;
}
