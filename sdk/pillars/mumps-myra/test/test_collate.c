/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_collate.c — M collation: a hand-ordered list (every pair), numbers
 * against exact integer arithmetic, the order laws on random strings, and
 * $QUERY order of references. test_golden checks the same order against
 * GT.M's own $ORDER. */
#include "zm_internal.h"
#include "zm_test.h"

static int32_t cmp(const char *a, const char *b) {
    int32_t c = 99;
    if (zm_collate(a, strlen(a), b, strlen(b), &c) != ZXV_OK) return 99;
    return c;
}

/* In M order, each strictly before the next. */
static const char *k_order[] = {
    "",                     /* the null subscript first */
    "-999999999999999999", "-10", "-3.14", "-2.3", "-2.25", "-1", "-.5",
    "-.000000000000000001", "0", ".0000000000000000001", ".000000000000000001", ".5", "1",
    "1.5", "2", "3.14", "9", "10", "99.99", "100", "999999999999999999",
    "1000000000000000000000", "10000000000000000000000000000000000000000000000",
    /* then strings, byte by byte */
    " 1", "!", "\"", "+1", "-", "-0", "-a", ".", "0.5", "01", "1 ", "1.0", "10A",
    "1234567890123456789", "1E3", "9A", "A", "AA", "AB", "B", "Z", "a", "ab", "abc", "b", "z", "~",
    "\xc3\x89", "\xef\xbf\xbd"};

static void hand_order(void) {
    size_t n = sizeof k_order / sizeof *k_order;
    for (size_t i = 0; i < n; i++)
        for (size_t j = 0; j < n; j++) {
            int32_t want = i < j ? -1 : (i > j ? 1 : 0);
            CHECK(cmp(k_order[i], k_order[j]) == want, "'%s' vs '%s': %d, want %d", k_order[i],
                  k_order[j], (int) cmp(k_order[i], k_order[j]), (int) want);
        }
    {
        int32_t c;
        CHECK(zm_collate("1", 1, "2", 1, NULL) == ZXV_E_ARG, "NULL out");
        CHECK(zm_collate(NULL, 1, "2", 1, &c) == ZXV_E_ARG, "NULL with a length");
        CHECK(zm_collate(NULL, 0, "", 0, &c) == ZXV_OK && c == 0, "two empties");
        CHECK(zm_collate("12", 1, "1", 1, &c) == ZXV_OK && c == 0, "lengths are honoured");
        CHECK(zm_collate("a\0b", 3, "a\0c", 3, &c) == ZXV_OK && c == -1, "NUL bytes compare");
    }
}

/* Canonical numbers at scales 0..9 against exact int64 arithmetic. */
static void numbers(void) {
    int bad = 0;
    for (int i = 0; i < 200000; i++) {
        uint32_t sa = (uint32_t) (rnd() % 10u), sb = (uint32_t) (rnd() % 10u);
        int64_t a = (int64_t) (rnd() % 1000000000u), b = (int64_t) (rnd() % 1000000000u);
        char ta[32], tb[32];
        size_t na, nb;
        int32_t c = 99, want;
        int64_t xa, xb;
        if (rnd() & 1u) a = -a;
        if (rnd() & 1u) b = -b;
        if (i % 7 == 0) b = a, sb = sa; /* equal */
        na = zm_dec_mtext(a, sa, ta);
        nb = zm_dec_mtext(b, sb, tb);
        xa = a * zm_pow10[9 - sa];
        xb = b * zm_pow10[9 - sb];
        want = xa < xb ? -1 : (xa > xb ? 1 : 0);
        if (zm_collate(ta, na, tb, nb, &c) != ZXV_OK || c != want) {
            if (bad < 10) fprintf(stderr, "numbers: %s vs %s: %d, want %d\n", ta, tb, (int) c, (int) want);
            bad++;
        }
    }
    CHECK(bad == 0, "200000 number pairs agree with exact arithmetic (%d bad)", bad);
    g_checks += 200000 - 1;
}

static void rand_text(char *b, size_t *n) {
    static const char alpha[] = "0123456789.-E aZ~";
    size_t k = 1u + (size_t) (rnd() % 8u);
    for (size_t i = 0; i < k; i++) b[i] = alpha[rnd() % (sizeof alpha - 1u)];
    if (rnd() % 4u == 0) k = zm_dec_mtext((int64_t) (rnd() % 100000u) - 50000, (uint32_t) (rnd() % 4u), b);
    b[k] = '\0';
    *n = k;
}

/* The order laws on random strings: antisymmetry, reflexivity, transitivity. */
static void laws(void) {
    int bad = 0;
    for (int i = 0; i < 100000; i++) {
        char a[32], b[32], c[32];
        size_t na, nb, nc;
        int32_t ab, ba, bc, ac, aa;
        rand_text(a, &na);
        rand_text(b, &nb);
        rand_text(c, &nc);
        zm_collate(a, na, b, nb, &ab);
        zm_collate(b, nb, a, na, &ba);
        zm_collate(b, nb, c, nc, &bc);
        zm_collate(a, na, c, nc, &ac);
        zm_collate(a, na, a, na, &aa);
        if (ab != -ba || aa != 0) bad++;
        if (ab == 0 && (na != nb || memcmp(a, b, na) != 0)) bad++; /* 0 only for equal text */
        if (ab <= 0 && bc <= 0 && ac > 0) bad++;
        if (ab >= 0 && bc >= 0 && ac < 0) bad++;
    }
    CHECK(bad == 0, "order laws on 100000 random triples (%d bad)", bad);
    g_checks += 100000 - 1;
}

static void references(void) {
    static const char *order[] = {"^A",       "^A(-1)",     "^A(1)",          "^A(1,2)",
                                  "^A(1,\"x\")", "^A(1.5)",  "^A(2)",          "^A(10)",
                                  "^A(\"10A\")", "^A(\"x\")", "^A(\"x\",1)",    "^AB",
                                  "^B(1)",    "^a"};
    size_t n = sizeof order / sizeof *order;
    int32_t c = 99;
    for (size_t i = 0; i < n; i++)
        for (size_t j = 0; j < n; j++) {
            int32_t want = i < j ? -1 : (i > j ? 1 : 0);
            c = 99;
            CHECK(zm_ref_collate(order[i], strlen(order[i]), order[j], strlen(order[j]), &c) == ZXV_OK &&
                      c == want,
                  "%s vs %s: %d", order[i], order[j], (int) c);
        }
    CHECK(zm_ref_collate("^A(\"10\")", 8, "^A(10)", 6, &c) == ZXV_OK && c == 0, "\"10\" is 10");
    CHECK(zm_ref_collate("^A(01)", 6, "^A(1)", 5, &c) == ZM_E_KEY, "01 is refused");
    CHECK(zm_ref_collate("A(1)", 4, "^A(1)", 5, &c) == ZM_E_KEY, "no ^");
    CHECK(zm_ref_collate("^A(1)", 5, "^A(1)", 5, NULL) == ZXV_E_ARG, "NULL out");
}

int main(void) {
    hand_order();
    numbers();
    laws();
    references();
    return report("test_collate");
}
