/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zt_gram.c — grammar-constrained decoding (zt_gram.h).
 *
 * Compile errors (syntax, undefined, duplicate, left recursion through
 * empty parts, limits, arena size); the matcher against hand-written
 * predicates on random strings (numbers, nested brackets, repetition
 * bounds), with every prefix checked for "can still be completed"; the
 * capacity error leaving the state unchanged; the treaty grammar against
 * the treaty parser (every emitted treaty admitted; no mutation the parser
 * accepts is refused); JSON samples; the token mask against an independent
 * predicate on a synthetic vocabulary and on the tokenizer fixture; and a
 * digest of every mask, printed so two machines can be compared.
 *
 *   gcc -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -DTEST_HOST \
 *       -Isrc/tensor -Isrc/treaty src/tensor/test_zt_gram.c src/tensor/zt_gram.c \
 *       src/tensor/zt_gram_lib.c src/tensor/zt_tok.c src/tensor/zt_gguf.c src/tensor/zt.c \
 *       src/treaty/zxt.c -o /tmp/test_zt_gram && /tmp/test_zt_gram
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "zt_gram.h"
#include "zxt.h"
#include "test_tok_fixture.h"

static int pass, fail;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        if (c) {                                                                                   \
            pass++;                                                                                \
        } else {                                                                                   \
            fail++;                                                                                \
            printf("[FAIL] %s:%d: ", __FILE__, __LINE__);                                          \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

static uint64_t rng = 0x2026101200000001ull;
static uint64_t rnd(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return rng;
}
static uint32_t rndn(uint32_t n)
{
    return (uint32_t) (rnd() % n);
}

static uint64_t digest = 0xcbf29ce484222325ull;
static void dig(const void *p, size_t n)
{
    const uint8_t *b = p;
    for (size_t i = 0; i < n; i++) digest = (digest ^ b[i]) * 0x100000001b3ull;
}

typedef struct {
    zt_gram_t g;
    void *arena;
    zt_gram_state_t st;
    void *mem;
} gm_t;

static int32_t gm_open(gm_t *m, const char *src, const char *root, uint32_t ms, uint32_t md,
                       zt_gram_err_t *err)
{
    memset(m, 0, sizeof *m);
    uint32_t len = (uint32_t) strlen(src);
    uint64_t need = zt_gram_arena_bytes((const uint8_t *) src, len, root, err);
    if (!need) return err->code;
    m->arena = malloc(need);
    int32_t r = zt_gram_compile(&m->g, (const uint8_t *) src, len, root, m->arena, need, err);
    if (r) return r;
    uint64_t sb = zt_gram_state_bytes(ms, md);
    m->mem = malloc(sb);
    return zt_gram_state_init(&m->st, &m->g, ms, md, m->mem, sb);
}

static void gm_close(gm_t *m)
{
    free(m->arena);
    free(m->mem);
}

/* accept the whole string from the start; 1 complete, 0 viable prefix, -1 rejected */
static int run(gm_t *m, const char *s, uint32_t n)
{
    zt_gram_reset(&m->st);
    if (zt_gram_accept(&m->st, (const uint8_t *) s, n)) return -1;
    return zt_gram_complete(&m->st) ? 1 : 0;
}

/* ---- compile errors ---- */
static void test_compile(void)
{
    static const struct {
        const char *g;
        int32_t code;
    } bad[] = {
        {"", ZT_GRAM_EARG},
        {"just words", ZT_GRAM_ESYNTAX},
        {"root ::= x", ZT_GRAM_EUNDEF},
        {"root ::= \"a\"\nroot ::= \"b\"", ZT_GRAM_EDUP},
        {"a ::= \"x\"", ZT_GRAM_ENOROOT},
        {"root ::= root \"a\" | \"b\"", ZT_GRAM_ELEFTREC},
        {"root ::= a\na ::= b \"x\"\nb ::= root | \"y\"", ZT_GRAM_ELEFTREC},
        {"root ::= e root \"x\" | \"y\"\ne ::= \"\" | \"z\"", ZT_GRAM_ELEFTREC},
        {"root ::= (\"a\"?)*", ZT_GRAM_ELEFTREC},
        {"root ::= \"\"*", ZT_GRAM_ELEFTREC},
        {"root ::= \"abc", ZT_GRAM_ESYNTAX},
        {"root ::= [abc", ZT_GRAM_ESYNTAX},
        {"root ::= []", ZT_GRAM_ESYNTAX},
        {"root ::= [z-a]", ZT_GRAM_ESYNTAX},
        {"root ::= [^\\x00-\\xff]", ZT_GRAM_ESYNTAX},
        {"root ::= (\"a\"", ZT_GRAM_ESYNTAX},
        {"root ::= \"a\")", ZT_GRAM_ESYNTAX},
        {"root ::= \"a\"{3,2}", ZT_GRAM_ESYNTAX},
        {"root ::= \"a\"{999}", ZT_GRAM_ELIMIT},
        {"root ::= \"a\"{2", ZT_GRAM_ESYNTAX},
        {"root ::= \"\\q\"", ZT_GRAM_ESYNTAX},
        {"root ::= \"\\x4\"", ZT_GRAM_ESYNTAX},
        {"root ::= | ", 0},
        {"root ::= ((((((((((((((((((((((((((((((((((\"a\"))))))))))))))))))))))))))))))))))",
         ZT_GRAM_ELIMIT},
    };
    for (uint32_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        zt_gram_err_t err;
        uint32_t len = (uint32_t) strlen(bad[i].g);
        uint64_t need = zt_gram_arena_bytes((const uint8_t *) bad[i].g, len, 0, &err);
        int32_t code = err.code;
        if (need) {
            void *a = malloc(need);
            zt_gram_t g;
            code = zt_gram_compile(&g, (const uint8_t *) bad[i].g, len, 0, a, need, &err);
            free(a);
        }
        CHECK(code == bad[i].code && code == err.code, "grammar %u: %d expected %d (%s at %u)", i,
              code, bad[i].code, err.what, err.off);
    }
    /* arena size: exact need works, one byte less refused */
    const char *src = "root ::= num (\",\" num)*\nnum ::= [0-9]+";
    zt_gram_err_t err;
    uint64_t need = zt_gram_arena_bytes((const uint8_t *) src, (uint32_t) strlen(src), 0, &err);
    uint8_t *a = malloc(need + 8);
    zt_gram_t g;
    CHECK(zt_gram_compile(&g, (const uint8_t *) src, (uint32_t) strlen(src), 0, a + 1, need - 8,
                          &err) == ZT_GRAM_ESPACE,
          "a short arena is refused");
    CHECK(zt_gram_compile(&g, (const uint8_t *) src, (uint32_t) strlen(src), 0, a + 1, need,
                          &err) == 0 &&
              g.n_named == 2 && g.n_rules == 5,
          "compiles in exactly the measured bytes (misaligned), 2 named + 3 generated rules (%u)",
          g.n_rules);
    free(a);
    /* another root, comments, newlines inside groups, '.' */
    gm_t m;
    int32_t r = gm_open(&m,
                        "# a comment\nstart ::= \"<\" (\n  \"a\" |   # first\n  \"b\"\n) . "
                        "\">\"\nroot ::= \"x\"",
                        "start", 16, 16, &err);
    CHECK(r == 0 && run(&m, "<a!>", 4) == 1 && run(&m, "<b\x00>", 4) == 1 &&
              run(&m, "x", 1) == -1 && run(&m, "<c!>", 4) == -1,
          "root by name, comments, multi-line group, '.' matches any byte");
    gm_close(&m);
}

/* ---- the matcher against predicates ---- */

/* -?(0|[1-9][0-9]*)(\.[0-9]+)? */
static bool num_ok(const char *s, uint32_t n)
{
    uint32_t i = 0;
    if (i < n && s[i] == '-') i++;
    if (i >= n) return false;
    if (s[i] == '0') {
        i++;
    } else if (s[i] >= '1' && s[i] <= '9') {
        while (i < n && s[i] >= '0' && s[i] <= '9') i++;
    } else {
        return false;
    }
    if (i < n && s[i] == '.') {
        i++;
        uint32_t d = 0;
        while (i < n && s[i] >= '0' && s[i] <= '9') i++, d++;
        if (!d) return false;
    }
    return i == n;
}

/* some completion of at most 2 bytes over the alphabet exists */
static bool num_viable(const char *s, uint32_t n, const char *alpha)
{
    char b[64];
    memcpy(b, s, n);
    if (num_ok(b, n)) return true;
    for (const char *a = alpha; *a; a++) {
        b[n] = *a;
        if (num_ok(b, n + 1)) return true;
        for (const char *c = alpha; *c; c++) {
            b[n + 1] = *c;
            if (num_ok(b, n + 2)) return true;
        }
    }
    return false;
}

/* (), [] nested, any number at top level */
static int brk(const char *s, uint32_t n) /* 1 complete, 0 viable, -1 dead */
{
    char st[256];
    uint32_t d = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (s[i] == '(' || s[i] == '[') {
            st[d++] = s[i];
        } else if (s[i] == ')' || s[i] == ']') {
            if (!d || st[d - 1] != (s[i] == ')' ? '(' : '[')) return -1;
            d--;
        } else {
            return -1;
        }
    }
    return d ? 0 : 1;
}

static void test_match(void)
{
    zt_gram_err_t err;
    gm_t m;
    const char *alpha = "-.0129a";
    CHECK(gm_open(&m, "root ::= \"-\"? (\"0\" | [1-9] [0-9]*) (\".\" [0-9]+)?", 0, 32, 16, &err) ==
              0,
          "number grammar compiles");
    uint32_t agree = 0, total = 0;
    for (uint32_t it = 0; it < 20000; it++) {
        char s[12];
        uint32_t n = 1 + rndn(8);
        for (uint32_t i = 0; i < n; i++) s[i] = alpha[rndn(7)];
        /* every prefix: viable exactly when a completion exists */
        zt_gram_reset(&m.st);
        bool ok = true;
        for (uint32_t i = 0; i < n && ok; i++) {
            bool want = num_viable(s, i + 1, alpha);
            int32_t r = zt_gram_accept(&m.st, (const uint8_t *) s + i, 1);
            ok = (r == 0) == want;
            if (r) break;
            ok = ok && zt_gram_complete(&m.st) == num_ok(s, i + 1);
        }
        agree += ok;
        total++;
    }
    CHECK(agree == total, "numbers: matcher == predicate on every prefix of %u strings (%u)", total,
          agree);
    zt_gram_reset(&m.st);
    uint8_t nb[32];
    zt_gram_next_bytes(&m.st, nb);
    bool nb_ok = true;
    for (uint32_t c = 0; c < 256; c++)
        nb_ok = nb_ok && (((nb[c >> 3] >> (c & 7)) & 1u) == (c == '-' || (c >= '0' && c <= '9')));
    CHECK(nb_ok && !zt_gram_complete(&m.st) && !zt_gram_finished(&m.st),
          "next bytes at the start are '-' and digits");
    zt_gram_accept(&m.st, (const uint8_t *) "-0", 2);
    CHECK(zt_gram_complete(&m.st) && !zt_gram_finished(&m.st),
          "\"-0\" is complete, '.' may follow");
    CHECK(zt_gram_accept(&m.st, (const uint8_t *) "5", 1) == ZT_GRAM_EREJECT &&
              zt_gram_complete(&m.st),
          "a refused byte leaves the state as it was");
    gm_close(&m);

    /* nested brackets; depth capacity */
    CHECK(gm_open(&m, "root ::= item*\nitem ::= \"(\" root \")\" | \"[\" root \"]\"", 0, 64, 64,
                  &err) == 0,
          "bracket grammar compiles");
    agree = total = 0;
    for (uint32_t it = 0; it < 20000; it++) {
        char s[24];
        uint32_t n = rndn(20);
        for (uint32_t i = 0; i < n; i++) s[i] = "()[]"[rndn(4)];
        if (rndn(4) == 0) /* well formed half the time */
            for (uint32_t i = 0; i + 1 < n; i += 2) s[i] = '(', s[i + 1] = ')';
        int want = brk(s, n), got = run(&m, s, n);
        agree += want == got;
        total++;
    }
    CHECK(agree == total, "brackets: matcher == predicate on %u strings (%u)", total, agree);
    char deep[60];
    for (uint32_t i = 0; i < 30; i++) deep[i] = '(';
    for (uint32_t i = 0; i < 30; i++) deep[30 + i] = ')';
    CHECK(run(&m, deep, 60) == 1, "30 deep nests within depth 64");
    gm_close(&m);
    CHECK(gm_open(&m, "root ::= item*\nitem ::= \"(\" root \")\"", 0, 64, 16, &err) == 0,
          "same grammar, depth 16");
    zt_gram_reset(&m.st);
    int32_t r = 0;
    uint32_t got_to = 0;
    for (; got_to < 40 && !r; got_to++) r = zt_gram_accept(&m.st, (const uint8_t *) "(", 1);
    uint8_t before[32], after[32];
    zt_gram_next_bytes(&m.st, before);
    CHECK(r == ZT_GRAM_ECAP && got_to < 16, "too deep: ECAP (after %u bytes), never a silent cut",
          got_to);
    CHECK(zt_gram_accept(&m.st, (const uint8_t *) ")", 1) == 0, "the state still works after ECAP");
    zt_gram_next_bytes(&m.st, after);
    (void) after;
    gm_close(&m);

    /* bounded repetition: x{2,4} y? */
    CHECK(gm_open(&m, "root ::= \"ab\"{2,4} \"c\"{1,} \"d\"{0,2} \"e\"{2}", 0, 32, 16, &err) == 0,
          "repetition grammar compiles");
    agree = total = 0;
    for (uint32_t na = 0; na < 6; na++)
        for (uint32_t nc = 0; nc < 4; nc++)
            for (uint32_t nd = 0; nd < 4; nd++)
                for (uint32_t ne = 0; ne < 4; ne++) {
                    char s[64];
                    uint32_t n = 0;
                    for (uint32_t k = 0; k < na; k++) s[n++] = 'a', s[n++] = 'b';
                    for (uint32_t k = 0; k < nc; k++) s[n++] = 'c';
                    for (uint32_t k = 0; k < nd; k++) s[n++] = 'd';
                    for (uint32_t k = 0; k < ne; k++) s[n++] = 'e';
                    bool want = na >= 2 && na <= 4 && nc >= 1 && nd <= 2 && ne == 2;
                    agree += (run(&m, s, n) == 1) == want;
                    total++;
                }
    CHECK(agree == total, "{m,n} {m,} {0,n} {m}: %u of %u cases", agree, total);
    gm_close(&m);

    /* class escapes and negation */
    CHECK(gm_open(&m, "root ::= [^a-c\\]\\-] [\\x41-\\x43\\n] \"\\\"\\t\\\\\"", 0, 16, 8, &err) ==
              0,
          "escape grammar compiles (%s)", err.what);
    CHECK(run(&m, "zA\"\t\\", 5) == 1 && run(&m, "aA\"\t\\", 5) == -1 && run(&m, "]A", 2) == -1 &&
              run(&m, "-A", 2) == -1 && run(&m, "z\n\"\t\\", 5) == 1 && run(&m, "zD", 2) == -1,
          "class negation, escaped ']' and '-', \\x ranges, literal escapes");
    gm_close(&m);
}

/* ---- the treaty grammar against the treaty parser ---- */
static void rand_treaty(zxt_treaty_t *t)
{
    zxt_clear(t);
    const char *L = "ABCDEFGHIJKLMNOPQRSTUVWXYZ", *C = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-";
    uint32_t nl = 1 + rndn(30);
    t->name[0] = L[rndn(26)];
    for (uint32_t i = 1; i < nl; i++) t->name[i] = C[rndn(37)];
    if (t->name[nl - 1] == '-') t->name[nl - 1] = 'Z';
    for (uint32_t i = 0; i < 3; i++) t->asset[i] = L[rndn(26)];
    t->op = rndn(2) ? ZXT_OP_NET : ZXT_OP_SETL;
    uint32_t np = 1 + rndn(16);
    t->n_party = (uint8_t) np;
    for (uint32_t i = 0; i < np; i++) {
        zxt_party_t *p = &t->party[i];
        uint32_t il = 1 + rndn(8);
        do {
            p->id[0] = L[rndn(26)];
            for (uint32_t k = 1; k < il; k++) p->id[k] = C[rndn(37)];
            if (p->id[il - 1] == '-') p->id[il - 1] = '7';
            bool dup = false;
            for (uint32_t k = 0; k < i; k++) dup = dup || !strcmp(t->party[k].id, p->id);
            if (!dup) break;
        } while (1);
        int64_t big = rndn(4) == 0 ? 999999999999999999LL : 1000000;
        p->deposit = (int64_t) (rnd() % (uint64_t) (big + 1));
        p->claim = p->deposit ? (int64_t) (rnd() % (uint64_t) (p->deposit + 1)) : 0;
        p->w_num = 1;
        p->w_den = np;
    }
    if (np == 1) {
        t->party[0].w_num = 1;
        t->party[0].w_den = 1;
    }
    uint32_t no = np > 1 ? rndn(65) : 0;
    t->n_owe = (uint8_t) no;
    for (uint32_t j = 0; j < no; j++) {
        t->owe[j].from = (uint8_t) rndn(np);
        do t->owe[j].to = (uint8_t) rndn(np);
        while (t->owe[j].to == t->owe[j].from);
        t->owe[j].amount = 1 + (int64_t) (rnd() % 999999999999999999ull);
    }
}

static void test_sutra(void)
{
    uint32_t len = 0;
    const char *src = zt_gram_lib("zxt-sutra", &len);
    zt_gram_err_t err;
    gm_t m;
    CHECK(src && gm_open(&m, src, 0, 256, 32, &err) == 0, "zxt-sutra grammar compiles (%s)",
          src ? err.what : "missing");
    static char text[ZXT_SUTRA_MAX + 1], mut[ZXT_SUTRA_MAX + 2];
    uint32_t emitted = 0, admitted = 0, muts = 0, parse_ok = 0, sound = 0, refused = 0;
    for (uint32_t it = 0; it < 400; it++) {
        zxt_treaty_t t;
        rand_treaty(&t);
        uint32_t n = 0;
        if (zxt_emit_sutra(&t, text, sizeof text, &n) != ZXT_OK) continue;
        emitted++;
        admitted += run(&m, text, n) == 1;
        dig(text, n);
        for (uint32_t k = 0; k < 20; k++) {
            uint32_t mn = n;
            memcpy(mut, text, n);
            uint32_t at = rndn(n);
            switch (rndn(3)) {
            case 0:
                mut[at] = " \n0-AZ9/x"[rndn(9)];
                break;
            case 1:
                memmove(mut + at, mut + at + 1, n - at - 1);
                mn--;
                break;
            default:
                memmove(mut + at + 1, mut + at, n - at);
                mut[at] = " 0A-"[rndn(4)];
                mn++;
            }
            zxt_treaty_t o;
            uint32_t eo;
            bool pok = zxt_parse_sutra(mut, mn, &o, &eo) == ZXT_OK;
            int g = run(&m, mut, mn);
            muts++;
            parse_ok += pok;
            sound += !pok || g == 1; /* the parser's language is inside the grammar's */
            refused += g != 1;
        }
    }
    CHECK(emitted >= 390 && admitted == emitted,
          "every canonical treaty the emitter writes is admitted (%u of %u)", admitted, emitted);
    CHECK(sound == muts, "no mutation the parser accepts is refused (%u mutations, %u parse)", muts,
          parse_ok);
    CHECK(refused > muts / 2, "most mutations are refused by the grammar alone (%u of %u)", refused,
          muts);
    static const char *bad[] = {
        "TREATY C USD NET\nEND\n",                             /* no party */
        "TREATY C USD NET\nOWE A B 1\nEND\n",                  /* owe before party */
        "TREATY C USD NET\nPARTY A 01 0 1/1\nEND\n",           /* leading zero */
        "TREATY C- USD NET\nPARTY A 1 0 1/1\nEND\n",           /* name ends in '-' */
        "TREATY C USDX NET\nPARTY A 1 0 1/1\nEND\n",           /* asset of 4 */
        "TREATY C USD NET\nPARTY A 1 0 1/0\nEND\n",            /* zero denominator */
        "TREATY C USD NET\nPARTY A 1 0 1/1\nOWE A B 0\nEND\n", /* zero obligation */
        "TREATY C USD NET\nPARTY A 1 0 1/1\nEND\nX",           /* text after END */
    };
    uint32_t nbad = 0;
    for (uint32_t i = 0; i < sizeof bad / sizeof bad[0]; i++)
        nbad += run(&m, bad[i], (uint32_t) strlen(bad[i])) != 1;
    CHECK(nbad == sizeof bad / sizeof bad[0], "hand-made bad treaties refused (%u)", nbad);
    const char *good = "TREATY CORRIDOR-7 USD SETL\nPARTY ACME 5000 4000 1/2\nPARTY BETA 3000 "
                       "3000 1/2\nOWE ACME BETA 1200\nEND\n";
    CHECK(run(&m, good, (uint32_t) strlen(good)) == 1 && zt_gram_finished(&m.st),
          "the zxt.h example is admitted and nothing may follow END");
    gm_close(&m);
}

static void test_json(void)
{
    uint32_t len = 0;
    const char *src = zt_gram_lib("json", &len);
    zt_gram_err_t err;
    gm_t m;
    CHECK(src && gm_open(&m, src, 0, 256, 128, &err) == 0, "json grammar compiles (%s)", err.what);
    static const char *good[] = {"{}",
                                 "[]",
                                 "0",
                                 "-0.5e+10",
                                 "\"a\\u00e9\\n\"",
                                 " { \"k\" : [1, 2.5, true, null, {}] } ",
                                 "[[[[[[[[[[1]]]]]]]]]]",
                                 "{\"a\":{\"b\":{\"c\":\"\xc3\xa9\"}}}",
                                 "false"};
    static const char *bad[] = {"{",     "[1,]",    "01", "\"\x01\"", "{\"a\" 1}", "tru",
                                "[1 2]", "\"\\x\"", "1.", "{'a':1}",  "-",         "1e"};
    uint32_t ng = 0, nb = 0;
    for (uint32_t i = 0; i < sizeof good / sizeof good[0]; i++)
        ng += run(&m, good[i], (uint32_t) strlen(good[i])) == 1;
    for (uint32_t i = 0; i < sizeof bad / sizeof bad[0]; i++)
        nb += run(&m, bad[i], (uint32_t) strlen(bad[i])) != 1;
    CHECK(ng == sizeof good / sizeof good[0], "valid JSON admitted (%u)", ng);
    CHECK(nb == sizeof bad / sizeof bad[0], "invalid JSON refused (%u)", nb);
    CHECK(zt_gram_lib("nope", &len) == 0 && zt_gram_lib(0, &len) == 0, "unknown names: NULL");
    gm_close(&m);
}

/* ---- the token mask ---- */
static void test_mask(void)
{
    zt_gram_err_t err;
    gm_t m;
    gm_open(&m, "root ::= \"-\"? (\"0\" | [1-9] [0-9]*) (\".\" [0-9]+)?", 0, 32, 16, &err);
    /* synthetic vocabulary: every string of 1..3 bytes over the alphabet, plus specials */
    const char *alpha = "-.0129a";
    enum { NV = 7 + 49 + 343 + 3 };
    static uint32_t off[NV + 1];
    static uint8_t bytes[NV * 3], skip[NV];
    uint32_t n = 0, w = 0;
    for (uint32_t L = 1; L <= 3; L++) {
        uint32_t cnt = L == 1 ? 7 : L == 2 ? 49 : 343;
        for (uint32_t k = 0; k < cnt; k++) {
            off[n] = w;
            uint32_t x = k;
            for (uint32_t j = 0; j < L; j++) bytes[w++] = (uint8_t) alpha[x % 7], x /= 7;
            skip[n++] = 0;
        }
    }
    for (uint32_t k = 0; k < 3; k++) off[n] = w, skip[n++] = 1; /* control, empty */
    off[n] = w;
    zt_gram_vocab_t v = {NV, off, bytes, skip, {NV - 1}, 1};
    static uint8_t allow[(NV + 7) / 8];
    uint32_t steps = 0, agree = 0, tot = 0;
    for (uint32_t it = 0; it < 300; it++) {
        char s[32];
        uint32_t sl = 0;
        zt_gram_reset(&m.st);
        for (uint32_t step = 0; step < 6; step++) {
            int32_t cnt = zt_gram_mask(&m.st, &v, allow, NV);
            dig(allow, sizeof allow);
            if (cnt <= 0) break;
            int32_t c2 = 0;
            for (uint32_t id = 0; id < NV; id++) {
                bool bit = (allow[id >> 3] >> (id & 7)) & 1u, want;
                if (id == NV - 1)
                    want = num_ok(s, sl);
                else if (skip[id])
                    want = false;
                else {
                    uint32_t tl = off[id + 1] - off[id];
                    memcpy(s + sl, bytes + off[id], tl);
                    want = num_viable(s, sl + tl, alpha);
                }
                agree += bit == want;
                tot++;
                c2 += bit;
            }
            agree += c2 == cnt;
            tot++;
            /* take a random allowed non-end token */
            uint32_t pick = 0, seen = 0;
            for (uint32_t id = 0; id + 1 < NV; id++)
                if (((allow[id >> 3] >> (id & 7)) & 1u) && rndn(++seen) == 0) pick = id;
            if (!seen) break;
            CHECK(zt_gram_accept_token(&m.st, &v, (int32_t) pick) == 0, "allowed token accepted");
            memcpy(s + sl, bytes + off[pick], off[pick + 1] - off[pick]);
            sl += off[pick + 1] - off[pick];
            steps++;
        }
        bool done = zt_gram_complete(&m.st);
        CHECK(zt_gram_accept_token(&m.st, &v, NV - 1) == (done ? 0 : ZT_GRAM_EREJECT),
              "the end token needs a complete text");
    }
    CHECK(agree == tot, "mask == independent predicate for every token at %u steps (%u/%u)", steps,
          agree, tot);
    CHECK(zt_gram_mask(&m.st, &v, allow, NV - 1) == ZT_GRAM_EARG, "vocabulary size mismatch");
    gm_close(&m);

    /* the tokenizer fixture: decode table and the JSON grammar's first mask */
    zt_gguf_t tg;
    zt_tok_t tok;
    zt_gguf_open(&tg, (const uint8_t *) TOK_GGUF, TOK_GGUF_LEN);
    uint64_t tb = zt_tok_arena_bytes(&tg);
    uint8_t *tar = malloc(tb);
    zt_tok_load(&tok, &tg, tar, tb);
    uint64_t vb = zt_gram_vocab_bytes(&tok);
    uint8_t *vm = malloc(vb);
    zt_gram_vocab_t tv;
    CHECK(zt_gram_vocab_init(&tv, &tok, vm, vb - 1) == ZT_GRAM_ESPACE &&
              zt_gram_vocab_init(&tv, &tok, vm, vb) == 0 && tv.n == tok.n_vocab,
          "vocabulary table: short buffer refused, %u tokens decoded", tv.n);
    uint32_t same = 0;
    for (uint32_t id = 0; id < tv.n; id++) {
        uint8_t buf[256];
        uint64_t bl = 0;
        int32_t i = (int32_t) id;
        zt_tok_decode(&tok, &i, 1, buf, sizeof buf, &bl);
        same += tv.skip[id] ||
                (bl == tv.off[id + 1] - tv.off[id] && !memcmp(buf, tv.bytes + tv.off[id], bl));
    }
    CHECK(same == tv.n, "table bytes == zt_tok_decode for every usable token (%u)", same);
    uint32_t jl;
    const char *js = zt_gram_lib("json", &jl);
    gm_open(&m, js, 0, 256, 128, &err);
    uint8_t *al = malloc((tv.n + 7) / 8);
    int32_t cnt = zt_gram_mask(&m.st, &tv, al, tv.n);
    uint32_t bad = 0;
    for (uint32_t id = 0; id < tv.n; id++) {
        bool bit = (al[id >> 3] >> (id & 7)) & 1u;
        if (tv.skip[id] || id == (uint32_t) tok.eos) {
            bad += bit && id != (uint32_t) tok.eos;
            continue;
        }
        zt_gram_reset(&m.st);
        bool ok = zt_gram_accept(&m.st, tv.bytes + tv.off[id], tv.off[id + 1] - tv.off[id]) == 0;
        bad += bit != ok;
    }
    dig(al, (tv.n + 7) / 8);
    CHECK(cnt > 0 && bad == 0, "JSON's first mask on the fixture vocabulary: %d tokens, %u wrong",
          cnt, bad);
    free(al);
    gm_close(&m);
    free(vm);
    free(tar);
}

/* ---- edges: escapes, bounds, arguments (no digest input) ---- */
static int32_t compile_code(const char *src)
{
    zt_gram_err_t err;
    uint32_t len = (uint32_t) strlen(src);
    uint64_t need = zt_gram_arena_bytes((const uint8_t *) src, len, 0, &err);
    if (!need) return err.code;
    void *a = malloc(need);
    zt_gram_t g;
    int32_t r = zt_gram_compile(&g, (const uint8_t *) src, len, 0, a, need, &err);
    free(a);
    return r;
}

static void test_edges(void)
{
    zt_gram_err_t err;
    gm_t m;
    /* hex escapes in both cases, other escapes, a class range by escapes */
    int32_t r =
        gm_open(&m, "root ::= \"\\x4A\\x4b\\xFf\\n\\t\\r\\\\\\\"\" [\\x41-\\x43]", 0, 8, 8, &err);
    CHECK(r == 0 && run(&m, "JK\xff\n\t\r\\\"B", 9) == 1 && run(&m, "JK\xff\n\t\r\\\"D", 9) == -1 &&
              run(&m, "jk", 2) == -1,
          "\\xHH in upper and lower case, \\n \\t \\r \\\\ \\\" and an escaped range");
    gm_close(&m);
    CHECK(compile_code("root ::= \"\\x4g\"") == ZT_GRAM_ESYNTAX, "\\x with a bad digit");
    CHECK(compile_code("root ::= \"\\x") == ZT_GRAM_ESYNTAX, "\\x at the end of the text");
    CHECK(compile_code("root ::= \"\\") == ZT_GRAM_ESYNTAX, "a backslash at the end of the text");
    CHECK(compile_code("root ::= [\\") == ZT_GRAM_ESYNTAX, "a class ending in a backslash");
    CHECK(compile_code("root ::= [a-") == ZT_GRAM_ESYNTAX, "a class ending mid-range");
    CHECK(compile_code("root ::= \"a\"{") == ZT_GRAM_ESYNTAX, "an open brace at the end");
    CHECK(compile_code("root ::= \"a\"{x}") == ZT_GRAM_ESYNTAX, "a brace without a number");
    CHECK(compile_code("root ::= \"a\"{1,x}") == ZT_GRAM_ESYNTAX, "a bad upper bound");
    CHECK(compile_code("root ::= \"a\"{1,999}") == ZT_GRAM_ELIMIT, "an upper bound over 255");
    CHECK(compile_code("root ::= \"a\"{255}") == 0, "a bound of exactly 255");
    CHECK(compile_code("root ::= \"a\" ::= \"b\"") == ZT_GRAM_ESYNTAX, "a second ::= on a line");
    CHECK(compile_code("root ::= \"a\"\n::= \"b\"") == ZT_GRAM_ESYNTAX, "::= without a name");
    CHECK(compile_code("root ::= \"a\" @") == ZT_GRAM_ESYNTAX, "a stray character");

    /* {m,} is unbounded, {m} exact, {0,n} up to n, single-byte and negated classes */
    r = gm_open(&m, "root ::= \"a\"{2,} \"b\"{0,2} [c] [^d]", 0, 16, 16, &err);
    char s[300];
    memset(s, 'a', sizeof s);
    s[200] = 'b';
    s[201] = 'c';
    s[202] = 'e';
    CHECK(r == 0 && run(&m, "aace", 4) == 1 && run(&m, "ace", 3) == -1 &&
              run(&m, "aabbce", 6) == 1 && run(&m, "aabbbce", 7) == -1 &&
              run(&m, "aacd", 4) == -1 && run(&m, s, 203) == 1 && run(&m, "aa", 2) == 0,
          "{2,} takes 200 a's, {0,2} at most two, [c] one byte, [^d] anything else");
    gm_close(&m);

    /* nesting: exactly the limit compiles, one more does not */
    char deep[200];
    uint32_t k = 0;
    k += (uint32_t) sprintf(deep + k, "root ::= ");
    for (uint32_t i = 0; i < ZT_GRAM_MAX_NEST; i++) deep[k++] = '(';
    k += (uint32_t) sprintf(deep + k, "\"a\"");
    for (uint32_t i = 0; i < ZT_GRAM_MAX_NEST; i++) deep[k++] = ')';
    deep[k] = 0;
    int32_t at = compile_code(deep);
    memmove(deep + 10, deep + 9, k - 9 + 1);
    deep[9] = '(';
    deep[k + 1] = ')';
    deep[k + 2] = 0;
    int32_t over = compile_code(deep);
    CHECK(at == 0 && over == ZT_GRAM_ELIMIT, "nesting of %u compiles (%d), %u does not (%d)",
          ZT_GRAM_MAX_NEST, at, ZT_GRAM_MAX_NEST + 1, over);

    /* state sizes and arguments */
    CHECK(zt_gram_state_bytes(0, 8) == 0 && zt_gram_state_bytes(8, 0) == 0 &&
              zt_gram_state_bytes(8, ZT_GRAM_MAX_DEPTH + 1) == 0 &&
              zt_gram_state_bytes((1u << 20) + 1, 8) == 0 && zt_gram_state_bytes(1u << 20, 1) > 0 &&
              zt_gram_state_bytes(8, ZT_GRAM_MAX_DEPTH) > zt_gram_state_bytes(8, 1) &&
              zt_gram_state_bytes(9, 8) > zt_gram_state_bytes(8, 8),
          "state sizes: zero and over-limit sizes refused, sizes grow");
    r = gm_open(&m, "root ::= \"ab\"", 0, 4, 4, &err);
    uint64_t sb = zt_gram_state_bytes(4, 4);
    uint8_t *mem = malloc(sb + 8);
    zt_gram_state_t st;
    CHECK(r == 0 && zt_gram_state_init(&st, &m.g, 4, 4, mem + 1, sb - 8) == ZT_GRAM_ESPACE &&
              zt_gram_state_init(&st, &m.g, 4, 4, 0, sb) != 0 &&
              zt_gram_state_init(0, &m.g, 4, 4, mem, sb) == ZT_GRAM_EARG &&
              zt_gram_state_init(&st, 0, 4, 4, mem, sb) == ZT_GRAM_EARG &&
              zt_gram_state_init(&st, &m.g, 0, 4, mem, sb) == ZT_GRAM_EARG &&
              zt_gram_state_init(&st, &m.g, 4, 4, mem + 1, sb) == 0,
          "state_init refuses short or missing memory and bad arguments, accepts misaligned");
    CHECK(zt_gram_accept(0, (const uint8_t *) "a", 1) == ZT_GRAM_EARG &&
              zt_gram_accept(&st, 0, 1) == ZT_GRAM_EARG && zt_gram_accept(&st, 0, 0) == 0 &&
              zt_gram_reset(0) == ZT_GRAM_EARG && !zt_gram_complete(0) && !zt_gram_finished(0) &&
              zt_gram_test(0, (const uint8_t *) "a", 1) == ZT_GRAM_EARG,
          "null arguments to the matcher");
    CHECK(zt_gram_accept(&st, (const uint8_t *) "a", 1) == 0 && !zt_gram_complete(&st) &&
              zt_gram_accept(&st, (const uint8_t *) "x", 1) == ZT_GRAM_EREJECT &&
              zt_gram_accept(&st, (const uint8_t *) "b", 1) == 0 && zt_gram_finished(&st),
          "a rejected byte leaves the state where it was");
    free(mem);
    gm_close(&m);

    /* ambiguity: every byte reaches the same seven stacks again; merged, the set never grows */
    r = gm_open(&m, "root ::= (\"a\" | \"a\" | \"a\" | \"a\" | [a] | [a-a])*", 0, 7, 4, &err);
    char as[64];
    memset(as, 'a', sizeof as);
    CHECK(r == 0 && run(&m, as, 64) == 1 && m.st.cur.n == 7,
          "six ambiguous alternatives, 64 bytes, still seven stacks (%u)", m.st.cur.n);
    gm_close(&m);
    /* a set that is really too small is a capacity error, state unchanged */
    r = gm_open(&m, "root ::= \"a\" \"b\" | \"a\" \"c\" | \"a\" \"d\"", 0, 2, 4, &err);
    int32_t ra = r ? r : zt_gram_reset(&m.st);
    CHECK(r == ZT_GRAM_ECAP ||
              (r == 0 && (ra == ZT_GRAM_ECAP ||
                          zt_gram_accept(&m.st, (const uint8_t *) "a", 1) == ZT_GRAM_ECAP)),
          "three live paths in a two-stack set: ECAP (%d)", ra);
    gm_close(&m);

    /* vocabulary arguments */
    zt_gram_vocab_t v;
    uint8_t al[4];
    CHECK(zt_gram_vocab_bytes(0) == 0 && zt_gram_vocab_init(0, 0, 0, 0) == ZT_GRAM_EARG &&
              zt_gram_vocab_init(&v, 0, al, sizeof al) == ZT_GRAM_EARG &&
              zt_gram_vocab_add_end(0, 1) == ZT_GRAM_EARG &&
              zt_gram_mask(0, 0, al, 8) == ZT_GRAM_EARG &&
              zt_gram_accept_token(0, 0, 0) == ZT_GRAM_EARG,
          "null arguments to the vocabulary and mask");
}

int main(void)
{
    test_compile();
    test_match();
    test_sutra();
    test_json();
    test_mask();
    test_edges();
    printf("digest %016llx\n", (unsigned long long) digest);
    CHECK(digest == 0xda53b33d55e8c3e6ull,
          "every mask and emitted treaty == the golden digest every CPU gives");
    printf("%d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
