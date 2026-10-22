/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zl_kpy.c — Kaṭapayādi numbers and digital-root labels (zl_kpy.h). */
#include <stdio.h>
#include <string.h>
#include "zl.h"
#include "zl_kpy.h"
#include "zl_sutra.h"

static unsigned checks, fails;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            fails++;                                                                               \
            printf("[FAIL] " __VA_ARGS__);                                                         \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

static uint64_t rng = 7;
static uint64_t next(void)
{
    rng = rng * 6364136223846793005ull + 1442695040888963407ull;
    return rng ^ rng >> 29;
}

/* the digital root by summing decimal digits until one is left */
static uint32_t dr_slow(uint64_t n)
{
    while (n > 9u) {
        uint64_t s = 0;
        for (; n; n /= 10u) s += n % 10u;
        n = s;
    }
    return (uint32_t) n;
}

int main(void)
{
    /* fixed answers: the classical letters, and roots of the alphabet */
    static const struct {
        const char *name;
        int32_t n;
        uint64_t v;
    } FIX[] = {
        {"gam", 2, 53},       {"kṛ", 1, 1},     {"sthā", 1, 7},
        {"kṣi", 1, 6},        {"dhā", 1, 9},    {"grah", 2, 82},
        {"ña", 1, 0},         {"a", 1, 0},      {"śri", 1, 2},
        {"ṭhaḍhaṇa", 3, 542}, {"bhūpa", 2, 14}, {"saṃdhi", 2, 97},
        {"veṣṭ", 2, 14},      {"jñā", 1, 0},    {"kaṭapayādi", 5, 81111},
    };
    for (uint32_t i = 0; i < sizeof FIX / sizeof FIX[0]; i++) {
        uint64_t v = 0;
        int32_t n = zl_kpy_value(FIX[i].name, &v);
        CHECK(n == FIX[i].n && v == FIX[i].v, "%s is %llu in %d digits, want %llu in %d",
              FIX[i].name, (unsigned long long) v, n, (unsigned long long) FIX[i].v, FIX[i].n);
    }
    /* refusals: not IAST, malformed UTF-8, too many digits */
    {
        uint64_t v;
        uint8_t d[2];
        CHECK(zl_kpy_value("gaX", &v) < 0 && zl_kpy_value("ga1", &v) < 0 &&
                  zl_kpy_value("f", &v) < 0,
              "letters outside IAST Sanskrit");
        CHECK(zl_kpy_value("g\xc3", &v) < 0 && zl_kpy_value("\xff", &v) < 0, "malformed UTF-8");
        CHECK(zl_kpy_value("kakakakakakakakakakakakakakakakakakakaka", &v) < 0 &&
                  zl_kpy_value("kakakakakakakakakakakakakakakakakakaka", &v) == 19,
              "19 digits, not 20");
        CHECK(zl_kpy_digits("kakaka", d, 2) < 0 && zl_kpy_value(0, &v) < 0, "no room");
        char out[8];
        CHECK(zl_kpy_spell(100, 2, out, sizeof out) < 0 &&
                  zl_kpy_spell(5, 0, out, sizeof out) < 0 && zl_kpy_spell(99, 2, out, 4) < 0,
              "spell refusals");
    }
    /* every root in the alphabet has a number */
    for (uint32_t op = 0; op < zl_sutra_count(); op++) {
        uint64_t v;
        CHECK(zl_kpy_value(zl_sutra_root(op), &v) >= 1, "√%s has no number", zl_sutra_root(op));
    }
    /* spell and read back, every digit count, leading zeros kept */
    for (uint32_t i = 0; i < 200000; i++) {
        uint32_t n = 1u + (uint32_t) (next() % ZL_KPY_MAXDIG);
        uint64_t lim = 1;
        for (uint32_t k = 0; k < n; k++) lim *= 10u;
        uint64_t v = next() % lim, back = 0;
        char out[64];
        int32_t len = zl_kpy_spell(v, n, out, sizeof out);
        CHECK(len > 0 && zl_kpy_value(out, &back) == (int32_t) n && back == v,
              "spell %llu in %u digits", (unsigned long long) v, n);
        if (fails) break;
    }
    /* the digital root and residues of a big-endian number */
    for (uint32_t i = 0; i < 200000; i++) {
        uint64_t v = i < 1000 ? i : next();
        uint8_t b[8];
        for (uint32_t k = 0; k < 8; k++) b[k] = (uint8_t) (v >> (56u - 8u * k));
        CHECK(zl_dr9(v) == dr_slow(v) && zl_dr9_be(b, 8) == dr_slow(v), "dr9 of %llu",
              (unsigned long long) v);
        CHECK(zl_mod_be(b, 8, 12) == v % 12u && zl_mod_be(b, 8, 144) == v % 144u &&
                  zl_mod_be(b, 8, 108) == v % 108u && zl_mod_be(b, 8, 9) == v % 9u,
              "residues of %llu", (unsigned long long) v);
        if (fails) break;
    }
    /* a CID's 32 hash bytes: a long number, not four summed words */
    {
        uint8_t h[32] = {0};
        CHECK(zl_dr9_be(h, 32) == 0, "all zero");
        h[31] = 9;
        CHECK(zl_dr9_be(h, 32) == 9, "9");
        h[0] = 1; /* 2^248 + 9; 2^248 = 4^124 = 4 (mod 9) since 4^3 = 1 */
        CHECK(zl_dr9_be(h, 32) == 4 && zl_mod_be(h, 32, 9) == 4, "2^248 + 9");
    }
    /* why a root-9 nonce proves nothing: any number reaches it within 8
     * steps, and one number in 9 is already there */
    {
        uint32_t at9 = 0, worst = 0;
        for (uint32_t i = 0; i < 90000; i++) {
            uint64_t v = next() >> 1, k = 0;
            while (zl_dr9(v + k) != 9u) k++;
            worst = k > worst ? (uint32_t) k : worst;
            at9 += k == 0;
        }
        CHECK(worst <= 8u && at9 > 9000u && at9 < 11000u,
              "root 9: worst %u steps, %u of 90000 already", worst, at9);
    }
    printf("%u checks, %u failures\n", checks, fails);
    return fails != 0;
}
