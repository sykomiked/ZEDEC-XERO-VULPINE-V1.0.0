/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_speech_tidy.c — host tests for what read-aloud says: hashes, ids,
 * codes and long numbers named and counted, links, files and addresses said
 * the way people say them, code blocks described, markup silent, pauses at
 * headings and bullets but not inside a wrapped sentence, the exact spelling
 * on request, other languages' words, and output that never overruns. */
#include <stdio.h>
#include <string.h>

#include "speech_tidy.h"

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

static char OUT[4096];

static const char *tidy(const char *in)
{
    speech_tidy(in, (uint32_t) strlen(in), OUT, sizeof OUT, 0);
    return OUT;
}
#define SAYS(in, want)                                                                             \
    CHECK(!strcmp(tidy(in), want), "'%s'\n      said '%s'\n      want '%s'", in, OUT, want)

static void test_kind(void)
{
#define K(s, k)                                                                                    \
    CHECK(speech_tidy_kind(s, (uint32_t) strlen(s)) == k, "kind of %s: %d", s,                     \
          speech_tidy_kind(s, (uint32_t) strlen(s)))
    K("hello", SPEECH_TK_WORD);
    K("1954a42", SPEECH_TK_HASH);
    K("1954a4", SPEECH_TK_WORD); /* 6: too short to be a hash */
    K("4bea0160fe2f16193a9b6f4169c71711b1550dc0", SPEECH_TK_HASH);
    K("deadbeef", SPEECH_TK_WORD);   /* hex letters, no digit: a word */
    K("20261015", SPEECH_TK_WORD);   /* a date */
    K("0xDEADBEEF", SPEECH_TK_WORD); /* 8 hex digits: a value */
    K("0x1DEADBEEF", SPEECH_TK_HASH);
    K("123e4567-e89b-12d3-a456-426614174000", SPEECH_TK_ID);
    K("cmsg_01SiwTxCAS9qLbWY3tFVthQA45Zfezwbepmv4Egn8S9x", SPEECH_TK_ID);
    K("session_01FtDYJ3B4T8xtesCymEi2VZ", SPEECH_TK_ID);
    K("eyJhbGciOiJIUzI1NiJ9", SPEECH_TK_CODE);
    K("aGVsbG8gd29ybGQgaGVsbG8gd29ybGQ=", SPEECH_TK_CODE);
    K("claude-opus-5-5", SPEECH_TK_WORD);
    K("Qwen2.5-1.5B", SPEECH_TK_WORD);
    K("x86_64", SPEECH_TK_WORD);
    K("sha256", SPEECH_TK_WORD);
    K("Windows10Pro", SPEECH_TK_WORD);
    K("ZEDEC-XERO-VULPINE-V1.0.0", SPEECH_TK_WORD);
    K("internationalization", SPEECH_TK_WORD);
    K("1234567890123", SPEECH_TK_NUMBER);
    K("123456789012", SPEECH_TK_WORD);
    K("https://github.com/x", SPEECH_TK_LINK);
    K("HTTP://X.ORG", SPEECH_TK_LINK);
    K("www.example.com", SPEECH_TK_LINK);
    K("www.", SPEECH_TK_WORD);
    K("bob@example.com", SPEECH_TK_EMAIL);
    K("@bob", SPEECH_TK_WORD);
    K("bob@", SPEECH_TK_WORD);
    K("bob@host", SPEECH_TK_WORD);
    K("/usr/bin/cc", SPEECH_TK_PATH);
    K("src/zui/zui_hear.c", SPEECH_TK_PATH);
    K("~/Downloads", SPEECH_TK_PATH);
    K("./run.sh", SPEECH_TK_PATH);
    K("C:\\Users\\me", SPEECH_TK_PATH);
    K("and/or", SPEECH_TK_WORD);
    K("1/2/2026", SPEECH_TK_WORD);
    K("---", SPEECH_TK_JUNK);
    K("|", SPEECH_TK_JUNK);
    K("-", SPEECH_TK_JUNK);
    K("+", SPEECH_TK_WORD);
    K("&", SPEECH_TK_WORD);
    K("", SPEECH_TK_JUNK);
    CHECK(speech_tidy_kind(0, 3) == SPEECH_TK_JUNK, "NULL");
#undef K
}

static void test_names(void)
{
    SAYS("Merged in commit 4bea0160fe2f16193a9b6f4169c71711b1550dc0.", "Merged in commit a hash.");
    SAYS("Head is 1954a42 now", "Head is a hash now");
    SAYS("The session is session_01FtDYJ3B4T8xtesCymEi2VZ.", "The session is an ID.");
    SAYS("Token: eyJhbGciOiJIUzI1NiJ9", "Token: a code");
    SAYS("Card 1234567890123456 expired", "Card a long number expired");
    SAYS("ids 4bea0160fe2f, 1954a42d55bf and 0dc0aa11ee22.", "ids 2 hashes and a hash.");
    SAYS("4bea0160fe2f 1954a42d55bf 0dc0aa11ee22", "3 hashes");
    SAYS("(4bea0160fe2f, 1954a42d55bf)", "(2 hashes)");
    SAYS("4bea0160fe2f. 1954a42d55bf", "a hash. a hash"); /* a sentence between */
    SAYS("4bea0160fe2f eyJhbGciOiJIUzI1NiJ9", "a hash a code");
    SAYS("`4bea0160fe2f`", "a hash");
    SAYS("Run `make verify-all` now.", "Run make verify-all now.");
    SAYS("Pull request #153 is open", "Pull request number 153 is open");
    SAYS("#tag", "#tag");
    SAYS("the zxv_policy engine", "the zxv policy engine");
    SAYS("x86_64 build", "x86_64 build");
    SAYS("__init__", "init");
}

static void test_links(void)
{
    SAYS("See https://github.com/sykomiked/ZEDEC/pull/153?x=1#y for it.",
         "See a link to github.com for it.");
    SAYS("Open www.example.org/a.", "Open a link to example.org.");
    SAYS("<https://user@host.net:8080/p>", "<a link to host.net>");
    SAYS("http://4bea0160fe2f16193a9b.cdn/x", "a link");
    SAYS("http://", "a link");
    SAYS("Write to bob.smith@example.co.uk today",
         "Write to bob.smith at example dot co dot uk today");
    SAYS("Mail a7f3k29dd81kq2@relay.example.com", "Mail an email address");
    SAYS("Open src/zui/zui_hear.c to see.", "Open the file zui hear.c to see.");
    SAYS("Saved to ~/Downloads/ZXV_Hardware/", "Saved to the folder ZXV Hardware");
    SAYS("Run ./run.sh", "Run the file run.sh");
    SAYS("C:\\Users\\me\\notes.txt", "the file notes.txt");
    SAYS("rock and/or roll", "rock and/or roll");
}

static void test_markup(void)
{
    SAYS("# Install\nRun the installer", "Install. Run the installer");
    SAYS("## Done.\nok", "Done. ok");
    SAYS("This is a long sentence that\nwraps onto a second line.",
         "This is a long sentence that wraps onto a second line.");
    SAYS("Steps:\n- open it\n- read it\n- close it", "Steps: open it. read it. close it");
    SAYS("- a bullet that\n  wraps\n- next", "a bullet that wraps. next");
    SAYS("first paragraph\n\nsecond paragraph", "first paragraph. second paragraph");
    SAYS("> quoted words", "quoted words");
    SAYS("It is **really** _good_ and *fine*.", "It is really good and fine.");
    SAYS("one\n---\ntwo", "one two");
    SAYS("| Name | Size |\n|------|------|\n| a | 3 |", "Name Size a 3");
    SAYS("Before\n```c\nint x = 0x4bea0160fe2f;\nreturn x;\n```\nAfter.",
         "Before. There is some code here. After.");
    SAYS("```\nonly code\n```", "There is some code here.");
    SAYS("text\n~~~\ncode\n~~~", "text. There is some code here.");
    SAYS("```\nnever closed", "There is some code here.");
    SAYS("a\r\nb\tc", "a b c");
    SAYS("bell\x07 here", "bell here");
    SAYS("", "");
    SAYS("   \n\n  ", "");
    SAYS("Caf\xC3\xA9 \xE2\x80\x94 ok", "Caf\xC3\xA9 \xE2\x80\x94 ok");
    SAYS("1 + 1 - 2", "1 + 1 2");
}

static void test_spell(void)
{
    char o[512];
    const char *in = "commit 4bea0160fe2f, ok";
    speech_spell(in, (uint32_t) strlen(in), o, sizeof o, 0);
    CHECK(!strcmp(o, "commit 4 b e a, 0 1 6 0, f e 2 f, ok"), "spelled: %s", o);
    in = "id cmsg_01Sa9";
    speech_spell(in, (uint32_t) strlen(in), o, sizeof o, 0);
    CHECK(!strcmp(o, "id cmsg_01Sa9"), "not an id (too short): %s", o);
    in = "key sk-ab12Cd34Ef56";
    speech_spell(in, (uint32_t) strlen(in), o, sizeof o, 0);
    CHECK(!strcmp(o, "key s k dash a b 1 2, capital C d 3 4, capital E f 5 6"), "id spelled: %s",
          o);
    in = "uuid 123E4567-E89B-12D3-A456-426614174000.";
    speech_spell(in, (uint32_t) strlen(in), o, sizeof o, 0);
    CHECK(!strncmp(o, "uuid 1 2 3 E, 4 5 6 7 dash E 8 9 B dash", 39), "upper-only: no capital: %s",
          o);
    CHECK(o[strlen(o) - 1] == '.', "trailing stop kept: %s", o);
    in = "`4bea0160fe2f`";
    speech_spell(in, (uint32_t) strlen(in), o, sizeof o, 0);
    CHECK(!strcmp(o, "4 b e a, 0 1 6 0, f e 2 f"), "backticks dropped: %s", o);
    in = "Hello  world\nagain";
    speech_spell(in, (uint32_t) strlen(in), o, sizeof o, 0);
    CHECK(!strcmp(o, "Hello world again"), "words as they are: %s", o);
    in = "v a+b/c=d:e.f_g";
    speech_spell(in, (uint32_t) strlen(in), o, sizeof o, 0);
    CHECK(!strcmp(o, "v a+b/c=d:e.f_g"), "a word is not spelled: %s", o);
    in = "aGVsbG8gd29ybGQ+aGVsbG8gd29ybGQ/x1=";
    speech_spell(in, (uint32_t) strlen(in), o, sizeof o, 0);
    CHECK(strstr(o, "plus") && strstr(o, "slash") && strstr(o, "equals") && strstr(o, "capital G"),
          "symbols by name: %s", o);
    in = "a 1234567890123";
    speech_spell(in, (uint32_t) strlen(in), o, sizeof o, 0);
    CHECK(!strcmp(o, "a 1 2 3 4, 5 6 7 8, 9 0 1 2, 3"), "long number: %s", o);
    CHECK(speech_spell(0, 3, o, sizeof o, 0) == 0 && !o[0], "NULL in");
    CHECK(speech_spell(in, 3, 0, 9, 0) == 0, "NULL out");
    CHECK(speech_spell("x", 1, o, 0, 0) == 0, "cap 0");
}

static void test_words(void)
{
    speech_tidy_words_t fr = speech_tidy_en;
    fr.tag = "fr";
    fr.hash = "une empreinte";
    fr.hashes = "%u empreintes";
    fr.link_to = "un lien vers %s";
    fr.code_block = "Il y a du code ici.";
    char o[256];
    const char *in = "voir 4bea0160fe2f 1954a42d55bf https://github.com/a";
    speech_tidy(in, (uint32_t) strlen(in), o, sizeof o, &fr);
    CHECK(!strcmp(o, "voir 2 empreintes un lien vers github.com"), "French words: %s", o);
    in = "```\nx\n```";
    speech_tidy_fn(in, (uint32_t) strlen(in), o, sizeof o, &fr);
    CHECK(!strcmp(o, "Il y a du code ici."), "the hook form: %s", o);
    speech_tidy_fn(in, (uint32_t) strlen(in), o, sizeof o, 0);
    CHECK(!strcmp(o, "There is some code here."), "NULL words: English: %s", o);
    CHECK(!strcmp(speech_tidy_en.tag, "en"), "tag");
}

static void test_bounds(void)
{
    char o[64];
    const char *in = "one two three four five six seven eight nine ten eleven twelve";
    memset(o, 'Z', sizeof o);
    uint32_t n = speech_tidy(in, (uint32_t) strlen(in), o, 16, 0);
    CHECK(n == 15 && o[15] == 0 && o[16] == 'Z' && !strncmp(o, "one two three f", 15),
          "cut at cap: %u '%s'", n, o);
    in = "caf\xC3\xA9\xC3\xA9";
    n = speech_tidy(in, (uint32_t) strlen(in), o, 5, 0);
    CHECK(n == 3 && !strcmp(o, "caf"), "never inside a character: %u", n);
    n = speech_tidy(in, (uint32_t) strlen(in), o, 6, 0);
    CHECK(n == 5 && !strcmp(o, "caf\xC3\xA9"), "a whole character fits: %u", n);
    CHECK(speech_tidy(0, 3, o, sizeof o, 0) == 0 && !o[0], "NULL in");
    CHECK(speech_tidy("x", 1, 0, 9, 0) == 0, "NULL out");
    CHECK(speech_tidy("x", 1, o, 0, 0) == 0, "cap 0");
    o[0] = 'Z';
    CHECK(speech_tidy("x", 1, o, 1, 0) == 0 && !o[0], "cap 1: empty");
    /* n is honoured: no reading past it */
    CHECK(speech_tidy("abc def", 3, o, sizeof o, 0) == 3 && !strcmp(o, "abc"), "n bytes only");
    /* a run rewritten after the buffer filled */
    in = "4bea0160fe2f 1954a42d55bf 0dc0aa11ee22";
    n = speech_tidy(in, (uint32_t) strlen(in), o, 9, 0);
    CHECK(n <= 8 && !strncmp(o, "3 hashes", n), "count rewritten in a small buffer: '%s'", o);
    /* big input, small output, many kinds: never overruns */
    static char big[8192];
    uint32_t k = 0;
    while (k + 60 < sizeof big) {
        memcpy(big + k, "# h\n- 4bea0160fe2f https://a.io/x bob@a.io src/a/b.c **x** |\n", 60);
        k += 60;
    }
    static char out2[300];
    memset(out2, 'Z', sizeof out2);
    n = speech_tidy(big, k, out2, 256, 0);
    CHECK(n == 255 && out2[255] == 0 && out2[256] == 'Z', "a long page fills, no more: %u", n);
    n = speech_spell(big, k, out2, 256, 0);
    CHECK(n <= 255 && out2[256] == 'Z', "spelling too: %u", n);
}

int main(void)
{
    test_kind();
    test_names();
    test_links();
    test_markup();
    test_spell();
    test_words();
    test_bounds();
    printf("%s test_speech_tidy: %d/%d checks\n", fails ? "[FAIL]" : "[PASS]", checks - fails,
           checks);
    return fails ? 1 : 0;
}
