/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_bv_syntax.c — reading what programs print and writing literals they
 * take (bv_syntax.h): every syntax's forms and refusals, the reference
 * tools' real output from Lingua's golden folder, one Collatz trace read
 * from eight languages and compared, generated round trips and random
 * input. Usage: test_bv_syntax <golden dir> [iterations]. */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bv_syntax.h"

static unsigned checks, fails;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            fails++;                                                                               \
            printf("[FAIL] %s:%d: ", __FILE__, __LINE__);                                          \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

static bv_arena_t A, B;
static bv_reader_t R;
static char out[1u << 17], out2[1u << 17];
static const char *GOLD;

/* read from an exact-size heap copy, so a read past the end is caught */
static int32_t rd(bv_arena_t *a, int syn, const char *s, uint32_t *at)
{
    uint32_t n = (uint32_t) strlen(s);
    char *q = malloc(n ? n : 1u);
    memcpy(q, s, n);
    int32_t v = bv_read(a, &R, syn, q, n, at);
    free(q);
    return v;
}

/* read s, write the whole list back in the same syntax */
static const char *again(int syn, const char *s)
{
    bv_reset(&A);
    int32_t v = rd(&A, syn, s, 0);
    if (v < 0) return bv_status_name(v);
    /* NUMBERS spells integers only: the list is shown in Scheme */
    int32_t n =
        bv_write(&A, v, syn == BV_SYNTAX_NUMBERS ? BV_SYNTAX_SCHEME : syn, out, sizeof out - 1u);
    if (n < 0) return bv_status_name(n);
    out[n] = 0;
    return out;
}

static const char *wr1(int syn, int32_t v)
{
    int32_t n = bv_write(&A, v, syn, out, sizeof out - 1u);
    if (n < 0) return bv_status_name(n);
    out[n] = 0;
    return out;
}

static void test_forms(void)
{
    static const struct {
        int syn;
        const char *in, *want;
    } c[] = {
        {BV_SYNTAX_NUMBERS, "  1  -2 \n 3\t+4 ", "(1 -2 3 4)"},
        {BV_SYNTAX_NUMBERS, "", "()"},
        {BV_SYNTAX_LISP, "(1 2 (3)) T NIL", "((1 2 (3)) T NIL)"},
        {BV_SYNTAX_LISP, "(BOB . 25)", "((BOB . 25))"},
        {BV_SYNTAX_LISP, "(1 2 . 3)", "((1 2 . 3))"},
        {BV_SYNTAX_LISP, "(1 . (2 3))", "((1 2 3))"},
        {BV_SYNTAX_LISP, "(1 . NIL)", "((1))"},
        {BV_SYNTAX_LISP, "#(1 2) ()", "((1 2) NIL)"},
        {BV_SYNTAX_LISP, "\"a \\\"b\\\" \\\\\"", "(\"a \\\"b\\\" \\\\\")"},
        {BV_SYNTAX_LISP, "6/4 -1/3 -9223372036854775808/2", "(3/2 -1/3 -4611686018427387904)"},
        {BV_SYNTAX_LISP, "1+ - +", "(1+ - +)"},
        {BV_SYNTAX_LISP, "(())", "((NIL))"},
        {BV_SYNTAX_SCHEME, "(1 #t #f #true #false ())", "((1 #t #f #t #f ()))"},
        {BV_SYNTAX_SCHEME, "\"hi\\n\\t\" plain", "(\"hi\\n\\t\" plain)"},
        {BV_SYNTAX_SCHEME, "((dave . 19) (a . 3))", "(((dave . 19) (a . 3)))"},
        {BV_SYNTAX_SCHEME, "\"x\"(1)", "(\"x\" (1))"},
        {BV_SYNTAX_LOGO, "[1 2 3]\n55\n", "[[1 2 3] 55]"},
        {BV_SYNTAX_LOGO, "a b [c d] e\n", "[[a b [c d] e]]"},
        {BV_SYNTAX_LOGO, "true\nfalse", "[true false]"},
        {BV_SYNTAX_LOGO, "\n{1 2}\n", "[[] [1 2]]"},
        {BV_SYNTAX_LOGO, "[[a b] [[c d] e]]", "[[[a b] [[c d] e]]]"},
        {BV_SYNTAX_LOGO, "", "[]"},
    };
    for (unsigned i = 0; i < sizeof c / sizeof c[0]; i++) {
        const char *got = again(c[i].syn, c[i].in);
        CHECK(strcmp(got, c[i].want) == 0, "form %u: %s -> %s, want %s", i, c[i].in, got,
              c[i].want);
    }
    bv_reset(&A);
    int32_t v = rd(&A, BV_SYNTAX_LISP, "(BOB . 25) \"x\" 3/4 T", 0);
    CHECK(bv_len(&A, v) == 4 && bv_kind(&A, bv_at(&A, v, 0)) == BV_PAIR &&
              bv_kind(&A, bv_at(&A, v, 1)) == BV_TEXT && bv_kind(&A, bv_at(&A, v, 2)) == BV_RATIO &&
              bv_kind(&A, bv_at(&A, v, 3)) == BV_TRUTH &&
              bv_kind(&A, bv_at(&A, bv_at(&A, v, 0), 0)) == BV_SYMBOL,
          "kinds: pair, text, ratio, truth, symbol");
    v = rd(&A, BV_SYNTAX_LOGO, "5\n[5]\n", 0);
    CHECK(bv_kind(&A, bv_at(&A, v, 0)) == BV_INTEGER && bv_kind(&A, bv_at(&A, v, 1)) == BV_LIST,
          "Logo: print [5] reads as 5, show [5] as [5]");
}

static void test_refusals(void)
{
    static const struct {
        int syn;
        const char *in;
        int32_t st;
        uint32_t at;
    } c[] = {
        {BV_SYNTAX_NUMBERS, "1 x 3", BV_E_FORM, 2},
        {BV_SYNTAX_NUMBERS, "1 2.5", BV_E_FORM, 2},
        {BV_SYNTAX_NUMBERS, "9223372036854775808", BV_E_OVERFLOW, 0},
        {BV_SYNTAX_NUMBERS, "-9223372036854775809", BV_E_OVERFLOW, 0},
        {BV_SYNTAX_NUMBERS, "99999999999999999999", BV_E_OVERFLOW, 0},
        {BV_SYNTAX_LISP, "(1 2", BV_E_FORM, 4},
        {BV_SYNTAX_LISP, "1 2)", BV_E_FORM, 3},
        {BV_SYNTAX_LISP, "(1 . 2 3)", BV_E_FORM, 7},
        {BV_SYNTAX_LISP, "(1 .)", BV_E_FORM, 4},
        {BV_SYNTAX_LISP, "(. 1)", BV_E_FORM, 1},
        {BV_SYNTAX_LISP, ". 1", BV_E_FORM, 0},
        {BV_SYNTAX_LISP, "(1 . 2 . 3)", BV_E_FORM, 7},
        {BV_SYNTAX_LISP, "#(1 . 2)", BV_E_FORM, 4},
        {BV_SYNTAX_LISP, "(1 . 2 (3))", BV_E_FORM, 7},
        {BV_SYNTAX_LISP, "1.5", BV_E_DOMAIN, 0},
        {BV_SYNTAX_LISP, "-.5", BV_E_DOMAIN, 0},
        {BV_SYNTAX_LISP, "1e10", BV_E_DOMAIN, 0},
        {BV_SYNTAX_LISP, "1.", BV_E_FORM, 0},
        {BV_SYNTAX_LISP, "#x10", BV_E_FORM, 0},
        {BV_SYNTAX_LISP, "1/0", BV_E_DOMAIN, 0},
        {BV_SYNTAX_LISP, "9223372036854775808/3", BV_E_OVERFLOW, 0},
        {BV_SYNTAX_LISP, "1/9223372036854775808", BV_E_OVERFLOW, 0},
        {BV_SYNTAX_LISP, "\"abc", BV_E_FORM, 0},
        {BV_SYNTAX_LISP, "\"a\\nb\"", BV_E_FORM, 3},
        {BV_SYNTAX_LISP, "\"a\"b", BV_E_FORM, 3},
        {BV_SYNTAX_LISP, "\"\\", BV_E_FORM, 0},
        {BV_SYNTAX_LISP, "; x", BV_E_FORM, 0},
        {BV_SYNTAX_LISP, "(1 ]", BV_E_FORM, 3},
        {BV_SYNTAX_SCHEME, "#\\a", BV_E_FORM, 0},
        {BV_SYNTAX_SCHEME, "#(1 2]", BV_E_FORM, 6}, /* "2]" is a symbol */
        {BV_SYNTAX_SCHEME, "\"\\x41;\"", BV_E_FORM, 2},
        {BV_SYNTAX_SCHEME, "\"\xC3\"", BV_E_TEXT, 0},
        {BV_SYNTAX_LOGO, "[1 2", BV_E_FORM, 4},
        {BV_SYNTAX_LOGO, "[1 2}", BV_E_FORM, 4},
        {BV_SYNTAX_LOGO, "]", BV_E_FORM, 0},
        {BV_SYNTAX_LOGO, "3.5", BV_E_DOMAIN, 0},
        {BV_SYNTAX_LOGO, "a\xFF", BV_E_TEXT, 0},
        {0, "1", BV_E_DOMAIN, 0},
        {5, "1", BV_E_DOMAIN, 0},
    };
    for (unsigned i = 0; i < sizeof c / sizeof c[0]; i++) {
        bv_reset(&A);
        uint32_t at = 999;
        int32_t v = rd(&A, c[i].syn, c[i].in, &at);
        CHECK(v == c[i].st && at == c[i].at && A.nn == 0 && A.nb == 0 && A.nk == 0,
              "refusal %u (%s): %s at %u, want %s at %u; nothing left behind", i, c[i].in,
              bv_status_name(v), at, bv_status_name(c[i].st), c[i].at);
    }
    /* nesting: 63 open lists inside the top-level list fit, 64 do not */
    char deep[200];
    for (unsigned k = 62; k <= 64; k++) {
        unsigned j = 0;
        for (unsigned i = 0; i < k; i++) deep[j++] = '(';
        for (unsigned i = 0; i < k; i++) deep[j++] = ')';
        deep[j] = 0;
        bv_reset(&A);
        int32_t v = rd(&A, BV_SYNTAX_SCHEME, deep, 0);
        CHECK(k < 64 ? v >= 0 : v == BV_E_LIMIT, "%u nested lists: %s", k, bv_status_name(v));
    }
    /* the reader's stack: as many values as the arena takes */
    static char many[3u * BV_MAX_KIDS + 8u];
    unsigned j = 0;
    for (unsigned i = 0; i < BV_MAX_KIDS + 1u; i++) {
        many[j++] = '1';
        many[j++] = ' ';
    }
    many[j] = 0;
    bv_reset(&A);
    CHECK(rd(&A, BV_SYNTAX_NUMBERS, many, 0) == BV_E_LIMIT, "one value more than the stack");
    many[2u * BV_MAX_KIDS - 2u] = 0;
    bv_reset(&A);
    int32_t v = rd(&A, BV_SYNTAX_NUMBERS, many, 0);
    CHECK(v == BV_E_LIMIT, "a full stack still needs room for the list's kids: %s",
          bv_status_name(v));
    many[2u * 2000u] = 0;
    bv_reset(&A);
    v = rd(&A, BV_SYNTAX_NUMBERS, many, 0);
    CHECK(v >= 0 && bv_len(&A, v) == 2000, "2000 values");
    /* Logo empty lines up to the stack's end */
    static char lines[BV_MAX_KIDS + 2u];
    memset(lines, '\n', BV_MAX_KIDS + 1u);
    lines[BV_MAX_KIDS + 1u] = 0;
    bv_reset(&A);
    CHECK(rd(&A, BV_SYNTAX_LOGO, lines, 0) == BV_E_LIMIT, "too many empty lines");
}

static void test_write(void)
{
    bv_reset(&A);
    int32_t e[3] = {bv_int(&A, INT64_MIN), bv_ratio(&A, -1, 3), bv_truth(&A, BV_TRUE)};
    int32_t l = bv_list(&A, e, 3);
    CHECK(strcmp(wr1(BV_SYNTAX_LISP, l), "(-9223372036854775808 -1/3 T)") == 0, "LISP: %s", out);
    CHECK(strcmp(wr1(BV_SYNTAX_SCHEME, l), "(-9223372036854775808 -1/3 #t)") == 0, "Scheme: %s",
          out);
    CHECK(strcmp(wr1(BV_SYNTAX_LOGO, l), "DOMAIN") == 0, "Logo has no ratios");
    CHECK(strcmp(wr1(BV_SYNTAX_NUMBERS, l), "DOMAIN") == 0 &&
              strcmp(wr1(BV_SYNTAX_NUMBERS, e[0]), "-9223372036854775808") == 0,
          "NUMBERS: integers only");
    CHECK(strcmp(wr1(BV_SYNTAX_LISP, bv_truth(&A, BV_FALSE)), "NIL") == 0 &&
              strcmp(wr1(BV_SYNTAX_SCHEME, bv_truth(&A, BV_FALSE)), "#f") == 0 &&
              strcmp(wr1(BV_SYNTAX_LOGO, bv_truth(&A, BV_FALSE)), "false") == 0,
          "false in each");
    CHECK(strcmp(wr1(BV_SYNTAX_SCHEME, bv_truth(&A, BV_BOTH)), "DOMAIN") == 0 &&
              strcmp(wr1(BV_SYNTAX_SCHEME, bv_truth(&A, BV_NEITHER)), "DOMAIN") == 0,
          "BOTH and NEITHER have no spelling");
    uint8_t b[1] = {0};
    int32_t nm = bv_symbol(&A, "k", 1), one = bv_int(&A, 1);
    int32_t refused[] = {bv_bytes(&A, b, 1), bv_record(&A, &nm, &one, 1), bv_nothing(&A)};
    for (unsigned i = 0; i < 3; i++)
        for (int syn = BV_SYNTAX_NUMBERS; syn <= BV_SYNTAX_LOGO; syn++)
            CHECK(strcmp(wr1(syn, refused[i]), "DOMAIN") == 0, "refused %u in %d", i, syn);
    static const struct {
        const char *sym;
        int lisp, scheme, logo; /* 1 written, 0 refused */
    } s[] = {
        {"ABC", 1, 1, 1}, {"abc", 0, 1, 1}, {"1+", 1, 1, 1},   {"12", 0, 0, 0},   {"1/2", 0, 0, 1},
        {"1.5", 0, 0, 0}, {"T", 0, 1, 1},   {"NIL", 0, 1, 1},  {"true", 0, 1, 0}, {"#t", 0, 0, 1},
        {"A B", 0, 0, 0}, {"A(", 0, 0, 0},  {"A[", 1, 1, 0},   {".", 0, 0, 1},    {"A\"", 0, 0, 0},
        {"A;B", 0, 0, 1}, {"A|B", 0, 0, 1}, {"A\\B", 0, 0, 1}, {"A#", 0, 0, 1},
    };
    for (unsigned i = 0; i < sizeof s / sizeof s[0]; i++) {
        int32_t v = bv_symbol(&A, s[i].sym, (uint32_t) strlen(s[i].sym));
        int want[3] = {s[i].lisp, s[i].scheme, s[i].logo};
        for (int syn = BV_SYNTAX_LISP; syn <= BV_SYNTAX_LOGO; syn++) {
            const char *got = wr1(syn, v);
            bool ok = want[syn - BV_SYNTAX_LISP] ? strcmp(got, s[i].sym) == 0
                                                 : strcmp(got, "DOMAIN") == 0;
            CHECK(ok, "symbol %s in %d: %s", s[i].sym, syn, got);
        }
    }
    int32_t t = bv_text(&A, "a\"b\\c\nd\te", 9);
    CHECK(strcmp(wr1(BV_SYNTAX_LISP, t), "\"a\\\"b\\\\c\nd\te\"") == 0, "LISP text: %s", out);
    CHECK(strcmp(wr1(BV_SYNTAX_SCHEME, t), "\"a\\\"b\\\\c\\nd\\te\"") == 0, "Scheme text: %s", out);
    CHECK(strcmp(wr1(BV_SYNTAX_LOGO, t), "DOMAIN") == 0, "Logo has no strings");
    int32_t p = bv_pair(&A, one, bv_pair(&A, bv_int(&A, 2), bv_int(&A, 3)));
    CHECK(strcmp(wr1(BV_SYNTAX_LISP, p), "(1 2 . 3)") == 0 &&
              strcmp(wr1(BV_SYNTAX_LOGO, p), "DOMAIN") == 0,
          "a dotted chain: %s", out);
    CHECK(strcmp(wr1(BV_SYNTAX_LISP, bv_list(&A, 0, 0)), "NIL") == 0 &&
              strcmp(wr1(BV_SYNTAX_SCHEME, bv_list(&A, 0, 0)), "()") == 0 &&
              strcmp(wr1(BV_SYNTAX_LOGO, bv_list(&A, 0, 0)), "[]") == 0,
          "the empty list");
    CHECK(bv_write(&A, -1, BV_SYNTAX_LISP, out, 10) == BV_E_HANDLE &&
              bv_write(&A, (int32_t) A.nn, BV_SYNTAX_LISP, out, 10) == BV_E_HANDLE &&
              bv_write(&A, one, 0, out, 10) == BV_E_DOMAIN,
          "bad handle or syntax");
    int32_t n = bv_write(&A, l, BV_SYNTAX_LISP, out, sizeof out);
    for (int32_t cap = 0; cap < n; cap++) {
        memset(out, 'Z', sizeof out2);
        CHECK(bv_write(&A, l, BV_SYNTAX_LISP, out, (uint32_t) cap) == BV_E_LIMIT && out[cap] == 'Z',
              "cap %d is short and nothing goes past it", cap);
    }
    CHECK(bv_write(&A, l, BV_SYNTAX_LISP, out, (uint32_t) n) == n, "exactly enough");
}

/* ---- the reference tools' real output -------------------------------------- */

static char *slurp(const char *name, uint32_t *n)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", GOLD, name);
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    static char data[1u << 16];
    size_t k = fread(data, 1, sizeof data, f);
    fclose(f);
    *n = (uint32_t) k;
    return data;
}

static int syntax_of(const char *name)
{
    static const struct {
        const char *ext;
        int syn;
    } m[] = {{".f.out", BV_SYNTAX_NUMBERS},   {".bas.out", BV_SYNTAX_NUMBERS},
             {".fs.out", BV_SYNTAX_NUMBERS},  {".pas.out", BV_SYNTAX_NUMBERS},
             {".alg.out", BV_SYNTAX_NUMBERS}, {".lisp.out", BV_SYNTAX_LISP},
             {".scm.out", BV_SYNTAX_SCHEME},  {".lg.out", BV_SYNTAX_LOGO}};
    size_t l = strlen(name);
    for (unsigned i = 0; i < sizeof m / sizeof m[0]; i++) {
        size_t e = strlen(m[i].ext);
        if (l > e && strcmp(name + l - e, m[i].ext) == 0) return m[i].syn;
    }
    return 0;
}

/* every integer in v, depth first, into a flat list in b */
static void flatten(const bv_arena_t *a, int32_t v, int32_t *dst, uint32_t *k)
{
    if (bv_kind(a, v) == BV_INTEGER) {
        if (*k < 4096u) dst[(*k)++] = bv_int(&B, bv_num(a, v));
        return;
    }
    if (bv_kind(a, v) == BV_LIST || bv_kind(a, v) == BV_PAIR)
        for (uint32_t i = 0; i < bv_len(a, v); i++) flatten(a, bv_at(a, v, i), dst, k);
}

static void test_golden(void)
{
    DIR *d = GOLD ? opendir(GOLD) : 0;
    CHECK(d != 0, "the golden folder opens");
    if (!d) return;
    unsigned read = 0, numeric = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        int syn = syntax_of(e->d_name);
        if (!syn) continue;
        uint32_t n, at;
        const char *s = slurp(e->d_name, &n);
        bv_reset(&A);
        int32_t v = s ? bv_read(&A, &R, syn, s, n, &at) : BV_E_HANDLE;
        /* some programs print words after their numbers: NUMBERS refuses
         * the first word by its offset, and everything before it reads */
        if (syn == BV_SYNTAX_NUMBERS && v == BV_E_FORM) {
            CHECK(at < n, "%s: a word at %u", e->d_name, at);
            v = bv_read(&A, &R, syn, s, at, 0);
        }
        CHECK(v >= 0, "%s reads: %s at %u", e->d_name, bv_status_name(v), at);
        if (v < 0) continue;
        read++;
        if (syn == BV_SYNTAX_NUMBERS) numeric++;
        /* what was read writes back, and that reads the same again */
        int32_t w = bv_write(&A, v, syn == BV_SYNTAX_LISP ? BV_SYNTAX_LISP : BV_SYNTAX_SCHEME, out,
                             sizeof out);
        if (syn == BV_SYNTAX_NUMBERS || syn == BV_SYNTAX_LOGO) {
            /* as a Scheme literal (Logo's words are symbols, its truths #t/#f) */
            CHECK(w >= 0, "%s writes as Scheme: %s", e->d_name, bv_status_name(w));
        }
        if (w < 0) continue;
        bv_reset(&B);
        int32_t x = bv_read(&B, &R, syn == BV_SYNTAX_LISP ? BV_SYNTAX_LISP : BV_SYNTAX_SCHEME, out,
                            (uint32_t) w, 0);
        CHECK(x >= 0 && bv_len(&B, x) == 1 && bv_equal(&A, v, &B, bv_at(&B, x, 0)),
              "%s: written and read again, the same value", e->d_name);
    }
    closedir(d);
    CHECK(read >= 20 && numeric >= 8, "the golden outputs read: %u (%u numeric)", read, numeric);

    /* lists.lisp and lists.scm print the same program's results */
    uint32_t n;
    const char *s = slurp("lists.lisp.out", &n);
    bv_reset(&A);
    int32_t lv = s ? bv_read(&A, &R, BV_SYNTAX_LISP, s, n, 0) : -1;
    s = slurp("lists.scm.out", &n);
    bv_reset(&B);
    int32_t sv = s ? bv_read(&B, &R, BV_SYNTAX_SCHEME, s, n, 0) : -1;
    CHECK(lv >= 0 && sv >= 0 && bv_len(&A, lv) >= 14 && bv_len(&B, sv) >= 14,
          "LISP and Scheme print their values");
    if (lv >= 0 && sv >= 0) {
        CHECK(bv_witness(&A, bv_at(&A, lv, 0), &B, bv_at(&B, sv, 0)) == BV_TRUE,
              "the same list of numbers: TRUE");
        CHECK(bv_witness(&A, bv_at(&A, lv, 6), &B, bv_at(&B, sv, 6)) == BV_BOTH &&
                  bv_kind(&A, bv_at(&A, lv, 6)) == BV_PAIR,
              "(BOB . 25) and (bob . 25): the symbols' case differs, BOTH");
        CHECK(bv_witness(&A, bv_at(&A, lv, 8), &B, bv_at(&B, sv, 8)) == BV_BOTH,
              "NIL and #f: LISP's false is the empty list, Scheme's is not: BOTH");
    }

    /* one Collatz trace, eight languages, compared as numbers */
    static const char *const coll[] = {"collatz.f.out",   "collatz.bas.out", "collatz.fs.out",
                                       "collatz.pas.out", "collatz.alg.out", "collatz.lisp.out",
                                       "collatz.scm.out", "collatz.lg.out"};
    static int32_t flat[8][4096];
    uint32_t k[8] = {0};
    bv_reset(&B);
    int32_t lists[8];
    for (unsigned i = 0; i < 8; i++) {
        s = slurp(coll[i], &n);
        bv_reset(&A);
        uint32_t at = n;
        int32_t v = s ? bv_read(&A, &R, syntax_of(coll[i]), s, n, &at) : -1;
        if (v == BV_E_FORM && s) v = bv_read(&A, &R, syntax_of(coll[i]), s, at, 0); /* "GCD" */
        CHECK(v >= 0, "%s reads", coll[i]);
        if (v < 0) return;
        flatten(&A, v, flat[i], &k[i]);
        /* the trace is 12 lines of two numbers; a "GCD" label follows */
        CHECK(k[i] >= 24, "%s: %u numbers", coll[i], k[i]);
        lists[i] = bv_list(&B, flat[i], k[i] < 24 ? k[i] : 24);
    }
    for (unsigned i = 1; i < 8; i++)
        CHECK(bv_witness(&B, lists[0], &B, lists[i]) == BV_TRUE, "%s and %s agree (TRUE)", coll[0],
              coll[i]);
    flat[7][3] = bv_int(&B, 999);
    CHECK(bv_witness(&B, lists[0], &B, bv_list(&B, flat[7], 24)) == BV_BOTH,
          "one number changed: BOTH");
}

/* ---- generated values and random text --------------------------------------- */

static uint64_t rng = 0x2545F4914F6CDD1Dull;
static uint64_t rnd(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return rng;
}

/* a value every syntax at or above `syn`'s richness can spell */
static int32_t gen(int syn, unsigned depth)
{
    unsigned k = (unsigned) (rnd() % (depth < 4 ? 7u : 5u));
    char s[8];
    if (syn == BV_SYNTAX_NUMBERS) k = 0;
    switch (k) {
    case 0:
        return bv_int(&A, (int64_t) rnd() >> (rnd() % 64));
    case 1:
        if (syn == BV_SYNTAX_LOGO) return bv_int(&A, 7);
        return bv_ratio(&A, (int64_t) (rnd() % 1000u) - 500, 1 + (int64_t) (rnd() % 50u));
    case 2:
        return bv_truth(&A, syn == BV_SYNTAX_LISP ? BV_TRUE : 1 + (int64_t) (rnd() % 2u));
    case 3: {
        unsigned n = 1u + (unsigned) (rnd() % 5u);
        for (unsigned i = 0; i < n; i++) {
            static const char al[] = "ABCxyz-+*?!<=>";
            s[i] = al[rnd() % (syn == BV_SYNTAX_LISP ? 3u : 14u)];
        }
        int32_t v = bv_symbol(&A, s, n);
        /* a symbol that spells a number or a keyword is another value: skip */
        int32_t w = bv_write(&A, v, syn, out2, sizeof out2);
        return w >= 0 ? v : bv_int(&A, 1);
    }
    case 4: {
        if (syn == BV_SYNTAX_LOGO) return bv_int(&A, 2);
        unsigned n = (unsigned) (rnd() % 6u);
        for (unsigned i = 0; i < n; i++) s[i] = "a\"\\ \n\t("[rnd() % 7u];
        return bv_text(&A, s, n);
    }
    case 5: {
        int32_t e[5];
        unsigned n = (unsigned) (rnd() % 5u);
        for (unsigned i = 0; i < n; i++) e[i] = gen(syn, depth + 1);
        return bv_list(&A, e, n);
    }
    default:
        if (syn == BV_SYNTAX_LOGO) return bv_list(&A, 0, 0);
        return bv_pair(&A, gen(syn, depth + 1), gen(syn, depth + 1));
    }
}

static void test_generated(unsigned iters)
{
    unsigned ok = 0;
    for (unsigned it = 0; it < iters; it++) {
        int syn = BV_SYNTAX_NUMBERS + (int) (it % 4u);
        bv_reset(&A);
        int32_t v = gen(syn, 0);
        int32_t n = v >= 0 ? bv_write(&A, v, syn, out, sizeof out) : v;
        if (n < 0) {
            CHECK(0, "generated %u writes in %d: %s", it, syn, bv_status_name(n));
            continue;
        }
        /* Logo PRINT would drop brackets, but show-style text reads back on one line */
        bv_reset(&B);
        int32_t w = bv_read(&B, &R, syn, out, (uint32_t) n, 0);
        int32_t got = w >= 0 && bv_len(&B, w) == 1 ? bv_at(&B, w, 0) : -1;
        if (got >= 0 && bv_equal(&A, v, &B, got))
            ok++;
        else
            CHECK(0, "generated %u in %d: %.*s", it, syn, n, out);
    }
    CHECK(ok == iters, "every generated value round-trips: %u of %u", ok, iters);

    /* random text: never a crash; what reads writes and reads the same */
    unsigned accepted = 0;
    static const char al[] = "()#.\"\\ \n[]{}-/0123456789TNILtruefalsxyzA;";
    for (unsigned it = 0; it < iters; it++) {
        int syn = BV_SYNTAX_NUMBERS + (int) (it % 4u);
        uint32_t n = (uint32_t) (rnd() % 24u);
        char *s = malloc(n ? n : 1u);
        for (uint32_t i = 0; i < n; i++) s[i] = al[rnd() % (sizeof al - 1u)];
        bv_reset(&A);
        int32_t v = bv_read(&A, &R, syn, s, n, 0);
        free(s);
        if (v < 0) continue;
        accepted++;
        int wsyn = syn == BV_SYNTAX_LISP ? BV_SYNTAX_LISP : BV_SYNTAX_SCHEME;
        int32_t m = bv_write(&A, v, wsyn, out, sizeof out);
        if (m < 0) continue; /* e.g. a LISP symbol in lower case */
        bv_reset(&B);
        int32_t w = bv_read(&B, &R, wsyn, out, (uint32_t) m, 0);
        if (!(w >= 0 && bv_len(&B, w) == 1 && bv_equal(&A, v, &B, bv_at(&B, w, 0))))
            CHECK(0, "random %u in %d re-reads: %.*s", it, syn, m, out);
    }
    printf("generated: %u round trips; random: %u accepted\n", ok, accepted);
}

int main(int argc, char **argv)
{
    GOLD = argc > 1 ? argv[1] : 0;
    unsigned iters = argc > 2 ? (unsigned) strtoul(argv[2], 0, 10) : 20000u;
    test_forms();
    test_refusals();
    test_write();
    test_golden();
    test_generated(iters);
    printf("%u checks, %u failures\n", checks, fails);
    return fails ? 1 : 0;
}
