/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_terms.c — the first-boot agreement gate.
 *
 *   test_terms [path/to/agreement/en.txt]
 *
 * With a path, also checks that the embedded text is byte-identical to the
 * file (so a stale terms_text.c fails CI). */
#include "terms.h"
#include "sha256.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int passed, failed;
#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (c)                                                                                     \
            passed++;                                                                              \
        else {                                                                                     \
            failed++;                                                                              \
            printf("[FAIL] %s:%d %s\n", __FILE__, __LINE__, #c);                                   \
        }                                                                                          \
    } while (0)

static terms_reader_t R; /* 64 KB: keep it off the stack */

static terms_doc_t mk(const char *text)
{
    terms_doc_t d;
    memset(&d, 0, sizeof d);
    d.version = "1.0";
    d.lang = "en";
    d.text = text;
    d.len = strlen(text);
    d.controlling = 1;
    return d;
}

static int line_is(const terms_reader_t *r, uint32_t i, const char *want)
{
    char b[512];
    terms_line_text(r, i, b, sizeof b);
    return strcmp(b, want) == 0;
}

/* ------------------------------------------------------------------ */

static void test_utf8(void)
{
    CHECK(terms_utf8_valid("abc", 3));
    CHECK(terms_utf8_valid("", 0));
    CHECK(terms_utf8_valid("\xC3\xA9", 2));         /* é */
    CHECK(terms_utf8_valid("\xE4\xB8\xAD", 3));     /* 中 */
    CHECK(terms_utf8_valid("\xF0\x9F\x98\x80", 4)); /* 😀 */
    CHECK(terms_utf8_valid("\xF4\x8F\xBF\xBF", 4)); /* U+10FFFF */
    CHECK(terms_utf8_valid("\xED\x9F\xBF", 3));     /* U+D7FF */
    CHECK(terms_utf8_valid("\xEE\x80\x80", 3));     /* U+E000 */
    CHECK(!terms_utf8_valid("\xC0\x80", 2));        /* overlong */
    CHECK(!terms_utf8_valid("\xC1\xBF", 2));
    CHECK(!terms_utf8_valid("\xE0\x80\x80", 3));
    CHECK(!terms_utf8_valid("\xE0\x9F\xBF", 3));     /* overlong U+07FF */
    CHECK(!terms_utf8_valid("\xF0\x8F\xBF\xBF", 4)); /* overlong U+FFFF */
    CHECK(!terms_utf8_valid("\xED\xA0\x80", 3));     /* surrogate */
    CHECK(!terms_utf8_valid("\xED\xBF\xBF", 3));
    CHECK(!terms_utf8_valid("\xF4\x90\x80\x80", 4)); /* > U+10FFFF */
    CHECK(!terms_utf8_valid("\xF5\x80\x80\x80", 4));
    CHECK(!terms_utf8_valid("\xE4\xB8", 2));     /* cut */
    CHECK(!terms_utf8_valid("\x80", 1));         /* stray */
    CHECK(!terms_utf8_valid("\xE4\x38\xAD", 3)); /* bad continuation */
    CHECK(!terms_utf8_valid("\xFF", 1));
}

static void test_width(void)
{
    CHECK(terms_cp_width('a') == 1);
    CHECK(terms_cp_width(0xE9) == 1);
    CHECK(terms_cp_width(0x0301) == 0);
    CHECK(terms_cp_width(0x0300) == 0);
    CHECK(terms_cp_width(0x036F) == 0);
    CHECK(terms_cp_width(0x0370) == 1);
    CHECK(terms_cp_width(0x200B) == 0);
    CHECK(terms_cp_width(0x094D) == 0); /* virama */
    CHECK(terms_cp_width(0x0915) == 1); /* क */
    CHECK(terms_cp_width(0x064E) == 0); /* fatha */
    CHECK(terms_cp_width(0x0628) == 1); /* ب */
    CHECK(terms_cp_width(0x1100) == 2);
    CHECK(terms_cp_width(0x115F) == 2);
    CHECK(terms_cp_width(0x1160) == 1);
    CHECK(terms_cp_width(0x4E2D) == 2);
    CHECK(terms_cp_width(0x4DFF) == 1); /* hexagram symbols */
    CHECK(terms_cp_width(0x9FFF) == 2);
    CHECK(terms_cp_width(0xAC00) == 2);
    CHECK(terms_cp_width(0xD7A3) == 2);
    CHECK(terms_cp_width(0xD7A4) == 1);
    CHECK(terms_cp_width(0x3040) == 1);
    CHECK(terms_cp_width(0x3041) == 2);
    CHECK(terms_cp_width(0xFF01) == 2);
    CHECK(terms_cp_width(0xFF61) == 1); /* halfwidth */
    CHECK(terms_cp_width(0x1F600) == 2);
    CHECK(terms_cp_width(0x20000) == 2);
    CHECK(terms_cp_width(0x3FFFE) == 1);
}

static void test_hash(void)
{
    static const uint8_t abc[32] = {0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea,
                                    0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
                                    0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c,
                                    0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad};
    terms_doc_t d = mk("abc");
    uint8_t h[32];
    terms_hash(&d, h);
    CHECK(memcmp(h, abc, 32) == 0);
}

static void test_init_errors(void)
{
    terms_doc_t d = mk("Title\nbody");
    terms_doc_t bad = mk("Title\n\xC0\x80");
    terms_doc_t empty = mk("");
    CHECK(terms_reader_init(0, &d, 40, 10) == TERMS_ERR_ARG);
    CHECK(terms_reader_init(&R, 0, 40, 10) == TERMS_ERR_ARG);
    CHECK(terms_reader_init(&R, &d, 40, 0) == TERMS_ERR_ARG);
    CHECK(terms_reader_init(&R, &d, TERMS_MIN_COLS - 1, 10) == TERMS_ERR_ARG);
    CHECK(terms_reader_init(&R, &d, TERMS_MIN_COLS, 10) == TERMS_OK);
    CHECK(terms_reader_init(&R, &empty, 40, 10) == TERMS_ERR_ARG);
    CHECK(terms_reader_init(&R, &bad, 40, 10) == TERMS_ERR_UTF8);
    CHECK(R.nlines == 0);
    CHECK(!terms_can_accept(&R));
    CHECK(!terms_can_accept(0));
}

static void test_wrap_basic(void)
{
    terms_doc_t d = mk("Title\n\n1. HEAD [KEY]\nalpha beta gamma delta\n2. NEXT\nx\n");
    CHECK(terms_reader_init(&R, &d, 12, 3) == TERMS_OK);
    CHECK(R.nlines == 7);
    CHECK(line_is(&R, 0, "Title"));
    CHECK(R.lines[0].kind == TERMS_LINE_TITLE);
    CHECK(R.lines[0].section == 0 && R.lines[0].key == 0);
    CHECK(R.lines[1].kind == TERMS_LINE_BLANK && R.lines[1].len == 0);
    CHECK(line_is(&R, 2, "1. HEAD")); /* marker hidden */
    CHECK(R.lines[2].kind == TERMS_LINE_HEADING);
    CHECK(R.lines[2].key == 1 && R.lines[2].section == 1);
    CHECK(R.lines[2].cols == 7);
    CHECK(line_is(&R, 3, "alpha beta"));
    CHECK(R.lines[3].cols == 10 && R.lines[3].key == 1 && R.lines[3].section == 1);
    CHECK(R.lines[3].kind == TERMS_LINE_BODY);
    CHECK(line_is(&R, 4, "gamma delta"));
    CHECK(line_is(&R, 5, "2. NEXT"));
    CHECK(R.lines[5].key == 0 && R.lines[5].section == 2);
    CHECK(line_is(&R, 6, "x"));
    CHECK(R.lines[6].key == 0 && R.lines[6].section == 2);
    CHECK(R.nsections == 2);
    CHECK(R.section_no[0] == 1 && R.section_line[0] == 2 && R.section_key[0] == 1);
    CHECK(R.section_no[1] == 2 && R.section_line[1] == 5 && R.section_key[1] == 0);
}

static void test_headings(void)
{
    /* Not headings: no space, lower case, four digits, bare number. */
    terms_doc_t d = mk("T\n1.HEAD\n2. lower\n1234. FOUR\n7.\n12. TWELVE\n[KEY]\n3. X [KEY] y\n");
    CHECK(terms_reader_init(&R, &d, 40, 20) == TERMS_OK);
    CHECK(R.lines[1].kind == TERMS_LINE_BODY);
    CHECK(R.lines[2].kind == TERMS_LINE_BODY);
    CHECK(R.lines[3].kind == TERMS_LINE_BODY);
    CHECK(R.lines[4].kind == TERMS_LINE_BODY);
    CHECK(R.lines[5].kind == TERMS_LINE_HEADING && R.lines[5].section == 12);
    CHECK(R.lines[5].key == 0);
    CHECK(line_is(&R, 6, "[KEY]") && R.lines[6].kind == TERMS_LINE_BODY);
    CHECK(R.lines[7].kind == TERMS_LINE_HEADING && R.lines[7].key == 0);
    CHECK(line_is(&R, 7, "3. X [KEY] y"));
    CHECK(R.nsections == 2);
}

static void test_wrap_hard_and_hyphen(void)
{
    terms_doc_t d = mk("T\nabcdefghijklmnopqrstuvwxyz\nwell-known-thing-here\n");
    CHECK(terms_reader_init(&R, &d, 12, 10) == TERMS_OK);
    CHECK(line_is(&R, 1, "abcdefghijkl"));
    CHECK(line_is(&R, 2, "mnopqrstuvwx"));
    CHECK(line_is(&R, 3, "yz"));
    CHECK(line_is(&R, 4, "well-known-"));
    CHECK(line_is(&R, 5, "thing-here"));
    CHECK(R.nlines == 6);
}

static void test_wrap_spaces(void)
{
    terms_doc_t d = mk("T\naaaa    bbbbbbbbbb   \n   cc\n");
    CHECK(terms_reader_init(&R, &d, 12, 10) == TERMS_OK);
    CHECK(line_is(&R, 1, "aaaa"));
    CHECK(line_is(&R, 2, "bbbbbbbbbb")); /* trailing spaces cut */
    CHECK(line_is(&R, 3, "   cc"));      /* indentation kept */
    CHECK(R.nlines == 4);
}

static void test_wrap_cjk(void)
{
    /* 20 × 中 (width 2) at 12 columns: 6 per line. */
    char t[128];
    int i;
    terms_doc_t d;
    strcpy(t, "T\n");
    for (i = 0; i < 20; i++) strcat(t, "\xE4\xB8\xAD");
    d = mk(t);
    CHECK(terms_reader_init(&R, &d, 12, 10) == TERMS_OK);
    CHECK(R.nlines == 5);
    CHECK(R.lines[1].cols == 12 && R.lines[1].len == 18);
    CHECK(R.lines[2].cols == 12);
    CHECK(R.lines[3].cols == 12);
    CHECK(R.lines[4].cols == 4 && R.lines[4].len == 6);
}

static void test_wrap_mixed(void)
{
    /* A Latin word then CJK: break before the CJK, not inside "hello". */
    terms_doc_t d = mk("T\nhello\xE4\xB8\xAD\xE4\xB8\xAD\xE4\xB8\xAD\xE4\xB8\xAD\n"
                       "e\xCC\x81"
                       "e\xCC\x81"
                       "e\xCC\x81\n");
    CHECK(terms_reader_init(&R, &d, 12, 10) == TERMS_OK);
    CHECK(R.lines[1].cols == 11); /* hello + 3 CJK */
    CHECK(R.lines[2].cols == 2);
    CHECK(R.lines[3].cols == 3 && R.lines[3].len == 9); /* combining = 0 */
}

static void test_crlf(void)
{
    terms_doc_t d = mk("Title\r\n\r\n1. A [KEY]\r\nbody\r\n");
    CHECK(terms_reader_init(&R, &d, 20, 10) == TERMS_OK);
    CHECK(R.nlines == 4);
    CHECK(line_is(&R, 0, "Title"));
    CHECK(R.lines[1].kind == TERMS_LINE_BLANK);
    CHECK(line_is(&R, 2, "1. A") && R.lines[2].key == 1);
    CHECK(line_is(&R, 3, "body"));
}

/* Property: wrapping never loses or reorders text, never exceeds the width
 * (unless a single codepoint is wider), and lines are in order. */
static void check_cover(const terms_reader_t *r)
{
    const char *t = r->doc->text;
    size_t n = r->doc->len, p = 0;
    uint32_t i;
    int ok = 1, wide_ok = 1, ordered = 1;
    for (i = 0; i < r->nlines; i++) {
        const terms_line_t *ln = &r->lines[i];
        if (i && ln->off < r->lines[i - 1].off + r->lines[i - 1].len) ordered = 0;
        if (ln->cols > r->cols && ln->len > 4) wide_ok = 0;
        /* skip separators the wrapper dropped: spaces, CR, LF, " [KEY]" */
        while (p < ln->off) {
            char c = t[p];
            if (c != ' ' && c != '\n' && c != '\r' && !(p + 6 <= n && !memcmp(t + p, " [KEY]", 6)))
                ok = 0;
            if (p + 6 <= n && !memcmp(t + p, " [KEY]", 6))
                p += 6;
            else
                p++;
        }
        p = ln->off + ln->len;
    }
    while (p < n) {
        char c = t[p];
        if (c != ' ' && c != '\n' && c != '\r' && !(p + 6 <= n && !memcmp(t + p, " [KEY]", 6)))
            ok = 0;
        if (p + 6 <= n && !memcmp(t + p, " [KEY]", 6))
            p += 6;
        else
            p++;
    }
    CHECK(ok);
    CHECK(wide_ok);
    CHECK(ordered);
}

static void test_wrap_property(void)
{
    static char t[6000];
    uint32_t s = 12345;
    int round, cols;
    for (round = 0; round < 30; round++) {
        size_t n = 0;
        terms_doc_t d;
        strcpy(t, "Title\n");
        n = strlen(t);
        while (n < sizeof t - 16) {
            s = s * 1103515245u + 12345u;
            switch ((s >> 16) % 9) {
            case 0:
                t[n++] = ' ';
                break;
            case 1:
                t[n++] = '\n';
                break;
            case 2:
                memcpy(t + n, "\xE4\xB8\xAD", 3);
                n += 3;
                break;
            case 3:
                memcpy(t + n, "\xC3\xA9", 2);
                n += 2;
                break;
            case 4:
                t[n++] = '-';
                break;
            case 5:
                if ((s >> 20) % 16) {
                    t[n++] = '.';
                    break;
                }
                memcpy(t + n, "\n3. SEC [KEY]\n", 14);
                n += 14;
                break;
            default:
                t[n++] = (char) ('a' + (s >> 8) % 26);
                break;
            }
        }
        t[n] = 0;
        d = mk(t);
        cols = 12 + (int) (round * 3);
        if (terms_reader_init(&R, &d, (uint16_t) cols, 8) == TERMS_OK)
            check_cover(&R);
        else
            CHECK(0);
    }
}

/* The image carries the build-time SHA-256 next to the text; a text that no
 * longer matches it is never shown or recorded. */
static void test_fingerprint(const terms_doc_t *d)
{
    static char copy[1 << 20];
    terms_doc_t bad, ref_bad, plain;
    terms_record_t rec;
    uint8_t h[TERMS_HASH_LEN];
    CHECK(d->sha256 != 0);
    terms_hash(d, h);
    CHECK(d->sha256 && memcmp(h, d->sha256, TERMS_HASH_LEN) == 0);
    CHECK(terms_doc_check(d) == TERMS_OK);
    CHECK(terms_doc_check(0) == TERMS_ERR_ARG);
    CHECK(d->len < sizeof copy);
    memcpy(copy, d->text, d->len);
    copy[d->len / 2] ^= 0x01; /* one bit, mid-text */
    bad = *d;
    bad.text = copy;
    CHECK(terms_doc_check(&bad) == TERMS_ERR_TEXT);
    CHECK(terms_reader_init(&R, &bad, 40, 12) == TERMS_ERR_TEXT);
    CHECK(R.nlines == 0 && R.doc == 0);
    plain = bad; /* a text built without a fingerprint is not checked */
    plain.sha256 = 0;
    CHECK(terms_doc_check(&plain) == TERMS_OK);
    ref_bad = *d; /* a translation whose English reference is damaged */
    ref_bad.sha256 = 0;
    ref_bad.reference = &bad;
    CHECK(terms_doc_check(&ref_bad) == TERMS_ERR_TEXT);
    CHECK(terms_reader_init(&R, &ref_bad, 40, 12) == TERMS_ERR_TEXT);
    /* damaged after the reader opened it: accept refuses */
    CHECK(terms_reader_init(&R, d, 40, 12) == TERMS_OK);
    terms_end(&R);
    CHECK(terms_can_accept(&R));
    R.doc = &bad;
    CHECK(terms_accept(&R, "seat-1", 1, 0, &rec) == TERMS_ERR_TEXT);
    R.doc = d;
    CHECK(terms_accept(&R, "seat-1", 1, 0, &rec) == TERMS_OK);
    CHECK(memcmp(rec.shown, d->sha256, TERMS_HASH_LEN) == 0);
    copy[d->len / 2] ^= 0x01;
}

static void test_builtin(const char *path)
{
    const terms_doc_t *d = terms_builtin();
    CHECK(d && d->text && d->len > 100);
    CHECK(terms_utf8_valid(d->text, d->len));
    CHECK(d->reference == 0 && d->controlling == 1);
    CHECK(strcmp(d->lang, "en") == 0);
    CHECK(terms_builtin_lang("en-GB") == d);
    CHECK(terms_builtin_lang("fr") == d);
    CHECK(terms_builtin_lang(0) == d);
    CHECK(terms_reader_init(&R, d, 40, 12) == TERMS_OK);
    CHECK(R.nsections >= 2);
    check_cover(&R);
    { /* at least one [KEY] section, and it is highlighted */
        int k, any = 0;
        for (k = 0; k < R.nsections; k++) any |= R.section_key[k];
        CHECK(any);
    }
    if (path) {
        FILE *f = fopen(path, "rb");
        static char buf[1 << 20];
        size_t n = 0;
        CHECK(f != 0);
        if (f) {
            n = fread(buf, 1, sizeof buf, f);
            fclose(f);
        }
        CHECK(n == d->len && memcmp(buf, d->text, n) == 0);
        if (n != d->len || memcmp(buf, d->text, n))
            printf("  terms_text.c is stale: run tools/terms/embed_terms.py\n");
        { /* the fingerprint in the image is the file's own SHA-256 */
            uint8_t h[TERMS_HASH_LEN];
            sha256((const uint8_t *) buf, n, h);
            CHECK(d->sha256 && memcmp(h, d->sha256, TERMS_HASH_LEN) == 0);
        }
    }
    test_fingerprint(d);
}

static void test_measure(void)
{
    terms_doc_t d = mk("T\naaaa bbbb cccc dddd\n");
    struct {
        int calls;
    } ctx = {0};
    uint16_t (*fn)(void *, uint32_t);
    (void) fn;
    CHECK(terms_reader_init_ex(&R, &d, 0, 5, 0, 0) == TERMS_ERR_ARG);
    {
        /* every codepoint 8 px; 80 px = 10 cells */
        extern uint16_t test_px(void *, uint32_t);
        CHECK(terms_reader_init_ex(&R, &d, 80, 5, test_px, &ctx) == TERMS_OK);
    }
    CHECK(line_is(&R, 1, "aaaa bbbb"));
    CHECK(R.lines[1].cols == 72);
    CHECK(line_is(&R, 2, "cccc dddd"));
    CHECK(ctx.calls > 0);
    /* measured widths may be below TERMS_MIN_COLS */
    CHECK(terms_reader_resize(&R, 8, 5) == TERMS_OK);
    CHECK(R.lines[1].cols == 8);
}

uint16_t test_px(void *ctx, uint32_t cp)
{
    (void) cp;
    ((int *) ctx)[0]++;
    return 8;
}

/* A tall document: 1 title + 40 body lines + sections. */
static char TALL[4096];
static terms_doc_t tall_doc(void)
{
    int i;
    char *p = TALL;
    p += sprintf(p, "Agreement\n");
    for (i = 1; i <= 5; i++) {
        int j;
        p += sprintf(p, "%d. SECTION%s\n", i, i == 4 ? " [KEY]" : "");
        for (j = 0; j < 7; j++) p += sprintf(p, "line %d.%d\n", i, j);
    }
    p += sprintf(p, "End.\n");
    return mk(TALL);
}

static void test_gate_scroll(void)
{
    terms_doc_t d = tall_doc();
    terms_view_t v;
    int i;
    CHECK(terms_reader_init(&R, &d, 40, 10) == TERMS_OK);
    CHECK(R.nlines == 42);
    CHECK(!R.reached_end && !terms_can_accept(&R));
    v = terms_view(&R);
    CHECK(v.first == 0 && v.count == 10 && v.scroll256 == 0);
    CHECK(v.thumb256 == 10 * 256 / 42);
    CHECK(v.hint == TERMS_HINT_READ && !v.can_accept);
    CHECK(R.seen == R.lines[9].off + R.lines[9].len);
    for (i = 0; i < 31; i++) terms_scroll(&R, 1, TERMS_VIA_SCROLL);
    CHECK(R.top == 31 && !R.reached_end);
    CHECK(R.seen == R.lines[40].off + R.lines[40].len);
    terms_scroll(&R, 1, TERMS_VIA_SCROLL);
    CHECK(R.top == 32 && R.reached_end && R.via == TERMS_VIA_SCROLL);
    CHECK(terms_can_accept(&R));
    v = terms_view(&R);
    CHECK(v.scroll256 == 256 && v.can_accept && v.hint == TERMS_HINT_READY);
    CHECK(v.first == 32 && v.count == 10);
    terms_scroll(&R, 100, TERMS_VIA_SCROLL); /* clamps */
    CHECK(R.top == 32);
    terms_scroll(&R, -1000, TERMS_VIA_KEYBOARD);
    CHECK(R.top == 0 && R.reached_end && R.via == TERMS_VIA_SCROLL); /* first route kept */
    CHECK(terms_can_accept(&R));                                     /* scrolling back keeps it */
    CHECK(terms_view(&R).scroll256 == 0);
}

static void test_gate_keyboard(void)
{
    terms_doc_t d = tall_doc();
    CHECK(terms_reader_init(&R, &d, 40, 10) == TERMS_OK);
    terms_page(&R, 1);
    CHECK(R.top == 9); /* one line of context */
    terms_page(&R, -1);
    CHECK(R.top == 0);
    terms_page(&R, 1);
    terms_page(&R, 1);
    terms_page(&R, 1);
    CHECK(R.top == 27 && !R.reached_end);
    terms_page(&R, 1);
    CHECK(R.top == 32 && R.reached_end && R.via == TERMS_VIA_KEYBOARD);
    CHECK(terms_reader_init(&R, &d, 40, 10) == TERMS_OK);
    terms_end(&R);
    CHECK(R.top == 32 && R.reached_end && R.via == TERMS_VIA_KEYBOARD);
    terms_home(&R);
    CHECK(R.top == 0 && R.reached_end);
    CHECK(terms_reader_init(&R, &d, 40, 1) == TERMS_OK);
    terms_page(&R, 1);
    CHECK(R.top == 1); /* rows 1 still moves */
}

static void test_gate_section(void)
{
    terms_doc_t d = tall_doc();
    CHECK(terms_reader_init(&R, &d, 40, 10) == TERMS_OK);
    CHECK(R.nsections == 5);
    CHECK(terms_goto_section(&R, 9) == TERMS_ERR_ARG);
    CHECK(terms_goto_section(0, 1) == TERMS_ERR_ARG);
    CHECK(terms_goto_section(&R, 2) == TERMS_OK);
    CHECK(R.top == R.section_line[1] && R.top == 9);
    CHECK(!R.reached_end);
    CHECK(terms_goto_section(&R, 5) == TERMS_OK);
    CHECK(R.top == 32); /* clamped */
    CHECK(R.reached_end && R.via == TERMS_VIA_SECTION);
    CHECK(R.lines[R.section_line[3]].key == 1);
    CHECK(R.lines[R.section_line[3] + 1].key == 1);
    CHECK(R.lines[R.section_line[4]].key == 0);
}

static void test_gate_reader(void)
{
    terms_doc_t d = tall_doc();
    uint32_t last;
    CHECK(terms_reader_init(&R, &d, 40, 10) == TERMS_OK);
    last = R.lines[41].off + R.lines[41].len;
    terms_reader_spoke(&R, R.lines[20].off);
    CHECK(R.top == 20 && !R.reached_end); /* view followed */
    terms_reader_spoke(&R, R.lines[25].off);
    CHECK(R.top == 20); /* still on screen */
    terms_reader_spoke(&R, R.lines[41].off);
    CHECK(R.top == 32 && R.reached_end && R.via == TERMS_VIA_READER);
    CHECK(terms_reader_init(&R, &d, 40, 50) == TERMS_OK); /* fits */
    CHECK(R.reached_end && R.via == TERMS_VIA_FIT);
    { /* a reader with a tiny window: reaching the last byte counts */
        CHECK(terms_reader_init(&R, &d, 40, 1) == TERMS_OK);
        terms_reader_spoke(&R, last - 1);
        CHECK(R.top == 41);
        CHECK(R.reached_end); /* line 41 on screen */
        CHECK(terms_reader_init(&R, &d, 40, 1) == TERMS_OK);
        R.top = 0;
        terms_reader_spoke(&R, last);
        CHECK(R.reached_end && R.via == TERMS_VIA_READER);
        CHECK(R.seen >= last);
    }
}

static void test_resize(void)
{
    terms_doc_t d = tall_doc();
    uint32_t anchor;
    CHECK(terms_reader_init(&R, &d, 40, 10) == TERMS_OK);
    terms_scroll(&R, 15, TERMS_VIA_SCROLL);
    anchor = R.lines[R.top].off;
    CHECK(terms_reader_resize(&R, 12, 5) == TERMS_OK);
    CHECK(R.lines[R.top].off == anchor); /* same paragraph */
    CHECK(!R.reached_end);
    CHECK(terms_reader_resize(&R, 11, 5) == TERMS_ERR_ARG);
    CHECK(terms_reader_resize(&R, 12, 0) == TERMS_ERR_ARG);
    terms_end(&R);
    CHECK(R.reached_end);
    CHECK(terms_reader_resize(&R, 60, 20) == TERMS_OK);
    CHECK(R.reached_end); /* never lost */
    CHECK(R.top <= R.nlines - 20);
    CHECK(terms_reader_init(&R, &d, 40, 10) == TERMS_OK);
    CHECK(terms_reader_resize(&R, 40, 100) == TERMS_OK); /* now fits */
    CHECK(R.reached_end && R.via == TERMS_VIA_FIT && R.top == 0);
}

static void test_decline(void)
{
    terms_doc_t d = tall_doc();
    terms_record_t rec;
    CHECK(terms_reader_init(&R, &d, 40, 10) == TERMS_OK);
    CHECK(terms_accept(&R, "seat1", 1, 0, &rec) == TERMS_ERR_GATE);
    terms_end(&R);
    terms_decline(&R);
    CHECK(!terms_can_accept(&R));
    CHECK(terms_view(&R).hint == TERMS_HINT_DECLINED);
    CHECK(terms_accept(&R, "seat1", 1, 0, &rec) == TERMS_ERR_GATE);
    terms_decline(0);
}

static void test_render(void)
{
    terms_doc_t d = tall_doc();
    static char out[4096];
    char tiny[8];
    size_t n;
    CHECK(terms_reader_init(&R, &d, 40, 10) == TERMS_OK);
    n = terms_render_text(&R, out, sizeof out);
    CHECK(n == strlen(out));
    CHECK(strstr(out, "Agreement\n") == out);
    CHECK(strstr(out, "( I agree )") != 0);
    CHECK(strstr(out, "[ I agree ]") == 0);
    CHECK(strstr(out, "Read to the end to continue.") != 0);
    CHECK(strstr(out, "\xE2\x96\x8C") == 0); /* no key lines yet */
    terms_goto_section(&R, 4);
    terms_render_text(&R, out, sizeof out);
    CHECK(strstr(out, "\xE2\x96\x8C 4. SECTION\n") != 0);
    CHECK(strstr(out, "\xE2\x96\x8C line 4.0\n") != 0);
    CHECK(strstr(out, "\xE2\x96\x8C 5.") == 0);
    CHECK(strstr(out, "[KEY]") == 0);
    terms_end(&R);
    terms_render_text(&R, out, sizeof out);
    CHECK(strstr(out, "[ Decline ]   [ I agree ]\n") != 0);
    CHECK(strstr(out, "You've reached the end.") != 0);
    n = terms_render_text(&R, tiny, sizeof tiny);
    CHECK(n > sizeof tiny && strlen(tiny) == sizeof tiny - 1);
    CHECK(terms_render_text(&R, tiny, 0) == 0);
    CHECK(terms_render_text(0, tiny, sizeof tiny) == 0 && tiny[0] == 0);
}

static void test_line_text(void)
{
    terms_doc_t d = mk("T\n\xE4\xB8\xAD\xE4\xB8\xAD");
    char b[5];
    CHECK(terms_reader_init(&R, &d, 20, 5) == TERMS_OK);
    CHECK(terms_line_text(&R, 1, b, sizeof b) == 3); /* not 4: whole codepoints */
    CHECK(strcmp(b, "\xE4\xB8\xAD") == 0);
    CHECK(terms_line_text(&R, 1, b, 0) == 0);
    CHECK(terms_line_text(&R, 9, b, sizeof b) == 0 && b[0] == 0);
    CHECK(terms_line_text(&R, 0, b, sizeof b) == 1 && !strcmp(b, "T"));
}

static void test_limits(void)
{
    static char t[70000];
    terms_doc_t d;
    int i;
    size_t n = 0;
    t[n++] = 'T';
    for (i = 0; i < 4200; i++) {
        t[n++] = '\n';
        t[n++] = 'x';
    }
    t[n] = 0;
    d = mk(t);
    CHECK(terms_reader_init(&R, &d, 20, 5) == TERMS_ERR_FULL);
    CHECK(R.nlines == 0 && !terms_can_accept(&R));
    n = 0;
    t[n++] = 'T';
    for (i = 1; i <= 65; i++) n += (size_t) sprintf(t + n, "\n%d. S", i);
    t[n] = 0;
    d = mk(t);
    CHECK(terms_reader_init(&R, &d, 20, 5) == TERMS_ERR_FULL);
    { /* a resize that would overflow keeps the old layout */
        uint32_t before;
        n = 0;
        t[n++] = 'T';
        t[n++] = '\n';
        for (i = 0; i < 4200; i++) n += (size_t) sprintf(t + n, "aaaaaaaaaa ");
        t[n] = 0;
        d = mk(t);
        CHECK(terms_reader_init(&R, &d, 4000, 5) == TERMS_OK);
        before = R.nlines;
        CHECK(before > 10 && before < 20);
        CHECK(terms_reader_resize(&R, 12, 5) == TERMS_ERR_FULL);
        CHECK(R.nlines == before && R.cols == 4000);
    }
}

/* ---------------------------------------------------------------- records */

static void accept_doc(const terms_doc_t *d, const char *seat, uint64_t t, const uint8_t prev[32],
                       terms_record_t *rec)
{
    CHECK(terms_reader_init(&R, d, 40, 100) == TERMS_OK);
    CHECK(terms_accept(&R, seat, t, prev, rec) == TERMS_OK);
}

static void test_record_roundtrip(void)
{
    terms_doc_t d = tall_doc();
    terms_record_t rec, back;
    char line[TERMS_RECORD_MAX];
    uint8_t h[32], zero[32] = {0};
    size_t n;
    CHECK(terms_reader_init(&R, &d, 40, 10) == TERMS_OK);
    terms_end(&R);
    CHECK(terms_accept(&R, "seat-7.a_B", 1760000000ull, 0, &rec) == TERMS_OK);
    terms_hash(&d, h);
    CHECK(!memcmp(rec.shown, h, 32) && !memcmp(rec.ref, h, 32));
    CHECK(!memcmp(rec.prev, zero, 32));
    CHECK(rec.via == TERMS_VIA_KEYBOARD && rec.controlling == 1);
    CHECK(!strcmp(rec.version, "1.0") && !strcmp(rec.lang, "en"));
    n = terms_record_format(&rec, line, sizeof line);
    CHECK(n == strlen(line) && line[n - 1] == '\n');
    CHECK(!strncmp(
        line, "ZXVTERMS1 seat=seat-7.a_B ver=1.0 lang=en ctl=1 via=2 t=1760000000 shown=", 73));
    CHECK(terms_record_format(&rec, line, n) == 0); /* no room for NUL */
    CHECK(terms_record_format(&rec, line, sizeof line) == n);
    CHECK(terms_record_parse(line, n, &back) == TERMS_OK);
    CHECK(!memcmp(&back, &rec, sizeof rec));
    CHECK(terms_record_parse(line, n - 1, &back) == TERMS_OK); /* without '\n' */
    { /* every single-byte change is caught */
        size_t i;
        int all = 1;
        for (i = 0; i + 1 < n; i++) {
            char m[TERMS_RECORD_MAX];
            memcpy(m, line, n);
            m[i] = (char) (m[i] == 'a' ? 'b' : m[i] == '1' ? '2' : 'a');
            if (terms_record_parse(m, n, &back) == TERMS_OK) all = 0;
        }
        CHECK(all);
    }
}

static void test_record_parse_strict(void)
{
    terms_doc_t d = tall_doc();
    terms_record_t rec, back;
    char line[TERMS_RECORD_MAX], m[TERMS_RECORD_MAX + 8];
    size_t n;
    accept_doc(&d, "s", 0, 0, &rec);
    n = terms_record_format(&rec, line, sizeof line);
    CHECK(strstr(line, " t=0 ") != 0);
    CHECK(terms_record_parse(line, n, &back) == TERMS_OK && back.time == 0);
    CHECK(back.via == TERMS_VIA_FIT);
    /* trailing garbage */
    memcpy(m, line, n - 1);
    memcpy(m + n - 1, " x", 2);
    CHECK(terms_record_parse(m, n + 1, &back) == TERMS_ERR_PARSE);
    CHECK(terms_record_parse(line, 10, &back) == TERMS_ERR_PARSE);
    CHECK(terms_record_parse(0, 10, &back) == TERMS_ERR_ARG);
    CHECK(terms_record_parse(line, n, 0) == TERMS_ERR_ARG);
    /* re-signed records with out-of-range fields still fail to parse */
    {
        const char *bad[] = {
            "ZXVTERMS1 seat=s ver=1.0 lang=en ctl=2 via=0 t=0 ",
            "ZXVTERMS1 seat=s ver=1.0 lang=en ctl=9 via=0 t=0 ",
            "ZXVTERMS1 seat=s ver=1.0 lang=en ctl=1 via=40 t=0 ",
            "ZXVTERMS1 seat=s ver=1.0 lang=en ctl=1 via=0 t= ",
            "ZXVTERMS1 seat=s ver=1.0 lang=en ctl=1 via=32 t=0 ",
            "ZXVTERMS1 seat=s ver=1.0 lang=en ctl=1 via=0 t=01 ",
            "ZXVTERMS1 seat=s ver=1.0 lang=en ctl=1 via=0 t=18446744073709551616 ",
            "ZXVTERMS1 seat=s s ver=1.0 lang=en ctl=1 via=0 t=0 ",
            "ZXVTERMS1 seat= ver=1.0 lang=en ctl=1 via=0 t=0 ",
            "ZXVTERMS2 seat=s ver=1.0 lang=en ctl=1 via=0 t=0 ",
        };
        size_t k;
        for (k = 0; k < sizeof bad / sizeof bad[0]; k++) {
            size_t a = strlen(bad[k]), at;
            uint8_t self[32];
            memcpy(m, bad[k], a);
            at = a;
            memcpy(m + at, "shown=", 6);
            at += 6;
            memset(m + at, 'a', 64);
            at += 64;
            memcpy(m + at, " ref=", 5);
            at += 5;
            memset(m + at, 'a', 64);
            at += 64;
            memcpy(m + at, " prev=", 6);
            at += 6;
            memset(m + at, '0', 64);
            at += 64;
            sha256((const uint8_t *) m, at, self);
            memcpy(m + at, " rec=", 5);
            at += 5;
            {
                int b;
                for (b = 0; b < 32; b++) at += (size_t) sprintf(m + at, "%02x", self[b]);
            }
            CHECK(terms_record_parse(m, at, &back) == TERMS_ERR_PARSE);
        }
    }
    /* the largest time fits */
    rec.time = UINT64_MAX;
    {
        uint8_t prev[32] = {0};
        CHECK(terms_reader_init(&R, &d, 40, 100) == TERMS_OK);
        CHECK(terms_accept(&R, "s", UINT64_MAX, prev, &rec) == TERMS_OK);
        n = terms_record_format(&rec, line, sizeof line);
        CHECK(n > 0 && terms_record_parse(line, n, &back) == TERMS_OK);
        CHECK(back.time == UINT64_MAX);
    }
    /* upper-case hex is not the canonical spelling */
    {
        char *p = strstr(line, " shown=") + 7;
        size_t i;
        for (i = 0; i < 64; i++)
            if (p[i] >= 'a') {
                p[i] = (char) (p[i] - 32);
                break;
            }
        CHECK(terms_record_parse(line, n, &back) != TERMS_OK);
    }
}

static void test_seat(void)
{
    char s[64];
    CHECK(terms_seat_valid("a"));
    CHECK(terms_seat_valid("Seat_01.x-y"));
    CHECK(!terms_seat_valid(""));
    CHECK(!terms_seat_valid(0));
    CHECK(!terms_seat_valid("a b"));
    CHECK(!terms_seat_valid("a\nb"));
    CHECK(!terms_seat_valid("a=b"));
    CHECK(!terms_seat_valid("\xC3\xA9"));
    memset(s, 'a', 47);
    s[47] = 0;
    CHECK(terms_seat_valid(s));
    memset(s, 'a', 48);
    s[48] = 0;
    CHECK(!terms_seat_valid(s));
    {
        terms_doc_t d = mk("T\nx");
        terms_record_t rec;
        CHECK(terms_reader_init(&R, &d, 40, 10) == TERMS_OK);
        CHECK(terms_accept(&R, s, 1, 0, &rec) == TERMS_ERR_ARG);
        CHECK(terms_accept(&R, "ok", 1, 0, 0) == TERMS_ERR_ARG);
        CHECK(terms_accept(0, "ok", 1, 0, &rec) == TERMS_ERR_ARG);
        d.lang = "e n";
        CHECK(terms_reader_init(&R, &d, 40, 10) == TERMS_OK);
        CHECK(terms_accept(&R, "ok", 1, 0, &rec) == TERMS_ERR_ARG);
        d.lang = "en";
        d.version = "";
        CHECK(terms_reader_init(&R, &d, 40, 10) == TERMS_OK);
        CHECK(terms_accept(&R, "ok", 1, 0, &rec) == TERMS_ERR_ARG);
    }
}

static size_t append(char *log, size_t at, const terms_record_t *rec)
{
    return at + terms_record_format(rec, log + at, TERMS_RECORD_MAX);
}

static void test_log(void)
{
    terms_doc_t v1 = mk("Agreement\n1. ONE\nold text\n");
    terms_doc_t v2 = tall_doc();
    static char log[4 * TERMS_RECORD_MAX];
    terms_record_t a, b, c, d;
    uint8_t tip[32], zero[32] = {0};
    uint32_t bad;
    size_t n = 0, n1, n2;

    CHECK(terms_log_verify("", 0, tip, &bad) == 0 && !memcmp(tip, zero, 32) && bad == 0);
    CHECK(terms_log_verify(0, 0, tip, 0) == 0);
    CHECK(terms_log_verify(0, 5, tip, 0) == TERMS_ERR_ARG);
    CHECK(terms_log_status("", 0, "alice", &v2) == TERMS_STATUS_NEEDED);
    CHECK(terms_log_status("", 0, "bad seat", &v2) == TERMS_ERR_ARG);
    CHECK(terms_log_status("", 0, "alice", 0) == TERMS_ERR_ARG);

    accept_doc(&v1, "carol", 100, zero, &a);
    n = append(log, n, &a);
    n1 = n;
    CHECK(terms_log_verify(log, n, tip, &bad) == 1 && !memcmp(tip, a.self, 32));
    accept_doc(&v2, "alice", 200, tip, &b);
    n = append(log, n, &b);
    n2 = n;
    accept_doc(&v2, "bob", 300, b.self, &c);
    n = append(log, n, &c);
    CHECK(terms_log_verify(log, n, tip, &bad) == 3 && !memcmp(tip, c.self, 32));
    CHECK(terms_log_status(log, n, "alice", &v2) == TERMS_STATUS_CURRENT);
    CHECK(terms_log_status(log, n, "bob", &v2) == TERMS_STATUS_CURRENT);
    CHECK(terms_log_status(log, n, "carol", &v2) == TERMS_STATUS_OUTDATED);
    CHECK(terms_log_status(log, n, "carol", &v1) == TERMS_STATUS_CURRENT);
    CHECK(terms_log_status(log, n, "alice", &v1) == TERMS_STATUS_OUTDATED);
    CHECK(terms_log_status(log, n, "dave", &v2) == TERMS_STATUS_NEEDED);
    CHECK(terms_log_status(log, n, "alic", &v2) == TERMS_STATUS_NEEDED);
    /* carol accepts v2 later: current from then on */
    accept_doc(&v2, "carol", 400, tip, &d);
    n = append(log, n, &d);
    CHECK(terms_log_status(log, n, "carol", &v2) == TERMS_STATUS_CURRENT);
    /* blank lines are ignored */
    {
        static char gap[sizeof log + 4];
        memcpy(gap, log, n1);
        gap[n1] = '\n';
        memcpy(gap + n1 + 1, log + n1, n - n1);
        CHECK(terms_log_verify(gap, n + 1, tip, &bad) == 4);
    }
    /* drop the second record: the third's link breaks on line 2 */
    {
        static char cut[sizeof log];
        size_t m = n1;
        memcpy(cut, log, n1);
        memcpy(cut + m, log + n2, n - n2);
        m += n - n2;
        CHECK(terms_log_verify(cut, m, tip, &bad) == TERMS_ERR_CHAIN && bad == 2);
        CHECK(terms_log_status(cut, m, "bob", &v2) == TERMS_ERR_CHAIN);
    }
    /* edit a field without re-hashing */
    {
        char *t = strstr(log + n1, "t=200");
        t[2] = '9';
        CHECK(terms_log_verify(log, n, tip, &bad) == TERMS_ERR_CHAIN && bad == 2);
        t[2] = '2';
        CHECK(terms_log_verify(log, n, tip, &bad) == 4);
    }
    /* swap the first two records */
    {
        static char sw[sizeof log];
        size_t m = 0;
        memcpy(sw, log + n1, n2 - n1);
        m += n2 - n1;
        memcpy(sw + m, log, n1);
        m += n1;
        memcpy(sw + m, log + n2, n - n2);
        m += n - n2;
        CHECK(terms_log_verify(sw, m, tip, &bad) == TERMS_ERR_CHAIN && bad == 1);
    }
    /* garbage line */
    CHECK(terms_log_verify("hello\n", 6, tip, &bad) == TERMS_ERR_PARSE && bad == 1);
}

static void test_translation(void)
{
    terms_doc_t en = tall_doc();
    terms_doc_t zh = mk("\xE5\x8D\x8F\xE8\xAE\xAE\n1. \xE6\x9D\xA1\xE6\xAC\xBE [KEY]\n"
                        "\xE6\x96\x87\xE6\x9C\xAC\n");
    terms_doc_t id = mk("Perjanjian\n1. SATU\nteks\n");
    terms_record_t rec;
    static char log[2 * TERMS_RECORD_MAX];
    uint8_t he[32], hz[32];
    size_t n;
    zh.lang = "zh-Hans";
    zh.controlling = 0;
    zh.reference = &en;
    id.lang = "id";
    id.controlling = 1;
    id.reference = &en;
    accept_doc(&zh, "mei", 5, 0, &rec);
    terms_hash(&en, he);
    terms_hash(&zh, hz);
    CHECK(!memcmp(rec.shown, hz, 32) && !memcmp(rec.ref, he, 32));
    CHECK(rec.controlling == 0 && !strcmp(rec.lang, "zh-Hans"));
    CHECK(R.lines[1].key == 1 && R.lines[1].cols == 7); /* "1. " + 2 wide */
    n = terms_record_format(&rec, log, sizeof log);
    CHECK(strstr(log, " lang=zh-Hans ctl=0 ") != 0);
    /* accepting the Chinese text counts for the English and Indonesian too */
    CHECK(terms_log_status(log, n, "mei", &en) == TERMS_STATUS_CURRENT);
    CHECK(terms_log_status(log, n, "mei", &id) == TERMS_STATUS_CURRENT);
    accept_doc(&id, "budi", 6, rec.self, &rec);
    CHECK(rec.controlling == 1);
}

int main(int argc, char **argv)
{
    test_utf8();
    test_width();
    test_hash();
    test_init_errors();
    test_wrap_basic();
    test_headings();
    test_wrap_hard_and_hyphen();
    test_wrap_spaces();
    test_wrap_cjk();
    test_wrap_mixed();
    test_crlf();
    test_wrap_property();
    test_builtin(argc > 1 ? argv[1] : 0);
    test_measure();
    test_gate_scroll();
    test_gate_keyboard();
    test_gate_section();
    test_gate_reader();
    test_resize();
    test_decline();
    test_render();
    test_line_text();
    test_limits();
    test_record_roundtrip();
    test_record_parse_strict();
    test_seat();
    test_log();
    test_translation();
    printf("%d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
