/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* fuzz_gen.c — fuzzing the spec parser and generator.
 *
 * For every input: zf_gen never crashes and is deterministic (status,
 * position, message and output identical on a second run); a refusal's
 * offset is inside the input and agrees with its line and column, and its
 * message starts "line L, column C: "; an accepted spec gives a header with
 * one wrapper prototype per declared function and a source with one
 * _Generic signature check per function.
 *
 * -DZF_FUZZ_MAIN gives a deterministic mutation fuzzer with built-in seeds
 * (`make test`, `make fuzz`, CI under ASan/UBSan); clang -fsanitize=fuzzer
 * drives the same entry point.
 *
 *   fuzz_gen ITERATIONS          ZF_FUZZ_SEED=n: another stream */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zffi.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static unsigned long long g_ok, g_refused;

static void die(const char *what, const uint8_t *d, size_t n) {
    fprintf(stderr, "fuzz_gen: INVARIANT BROKEN: %s (input %zu bytes)\n", what, n);
    FILE *f = fopen("fuzz-crash.zf", "wb");
    if (f) {
        fwrite(d, 1, n, f);
        fclose(f);
        fprintf(stderr, "  input written to fuzz-crash.zf\n");
    }
    abort();
}

static size_t count(const char *hay, const char *needle) {
    size_t k = 0;
    for (const char *p = strstr(hay, needle); p; p = strstr(p + 1, needle)) k++;
    return k;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    char *h1 = NULL, *c1 = NULL, *h2 = NULL, *c2 = NULL, m1[400], m2[400];
    uint32_t l1 = 0, l2 = 0, k1 = 0, k2 = 0;
    uint64_t o1 = 0, o2 = 0;
    const char *s = (const char *) data;
    int32_t s1 = zf_gen(s, size, "g.h", &h1, NULL, &c1, NULL, &l1, &k1, &o1, m1, sizeof m1);
    int32_t s2 = zf_gen(s, size, "g.h", &h2, NULL, &c2, NULL, &l2, &k2, &o2, m2, sizeof m2);
    if (s1 != s2) die("status not deterministic", data, size);
    if (s1 == ZF_E_SPEC) {
        char pre[64];
        uint32_t line = 1;
        uint64_t ls = 0;
        g_refused++;
        if (l1 != l2 || k1 != k2 || o1 != o2 || strcmp(m1, m2) != 0) die("refusal not deterministic", data, size);
        if (h1 || c1) die("output on refusal", data, size);
        if (o1 > size) die("offset outside the input", data, size);
        for (uint64_t i = 0; i < o1; i++)
            if (data[i] == '\n') {
                line++;
                ls = i + 1u;
            }
        if (line != l1 || o1 - ls + 1u != k1) die("line/column disagree with the offset", data, size);
        snprintf(pre, sizeof pre, "line %u, column %u: ", (unsigned) l1, (unsigned) k1);
        if (strncmp(m1, pre, strlen(pre)) != 0) die("message format", data, size);
        return 0;
    }
    if (s1 != ZXV_OK) die("unexpected status", data, size);
    g_ok++;
    if (!h1 || !c1 || strcmp(h1, h2) != 0 || strcmp(c1, c2) != 0) die("output not deterministic", data, size);
    size_t protos = count(h1, "\nint32_t zf_"), checks = count(c1, "_Static_assert(_Generic(&");
    if (protos == 0 || protos != checks) die("one prototype and one signature check per function", data, size);
    zf_gen_free(h1);
    zf_gen_free(c1);
    zf_gen_free(h2);
    zf_gen_free(c2);
    return 0;
}

#ifdef ZF_FUZZ_MAIN

static uint64_t g_rng = 0x9E3779B97F4A7C15ull;
static uint64_t rnd(void) {
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 7;
    g_rng ^= g_rng << 17;
    return g_rng;
}

static const char *const k_seeds[] = {
    "library libm;\ninclude <math.h>;\nfn sqrt(x: f64 from rat) -> f64 as rat;\n"
    "fn ldexp(x: f64 from rat, e: int[-1100..1100]) -> f64 as rat needs BUDGET;\n",
    "library libc;\ninclude <stdlib.h>;\ninclude \"string.h\";\n"
    "fn abs(x: int[-INT_MAX..INT_MAX]) -> int[0..INT_MAX];\n"
    "fn strncmp(a: cstr[0..256], b: cstr, n: size[0..256]) -> int;\n",
    "# c\nlibrary x;\nfn f() -> void needs LEDGER_READ|ATTEST;\nfn g(a: u64[0..18446744073709551615]) -> u8;\n"
    "fn h(a: f32 from rat, b: i64[INT64_MIN..-1]) -> f32 as rat needs none;\n",
};

static const char *const k_dict[] = {
    "library ", "include ", "fn ", "(", ")", "[", "]", "..", "->", ":", ";", ",", "|", "<", ">", "\"",
    "-", "0", "1", "9", "18446744073709551616", "9223372036854775808", "int", "uint", "size", "cstr",
    "f64", "f32", "i8", "u64", "void", "from rat", "as rat", "needs ", "none", "BUDGET", "INT_MAX",
    "zf_", "#", "\n", " ", "\xc3\xa9", "\xff", "../", "x_len", "x_num", "double",
};

static size_t mutate(uint8_t *buf, size_t n, size_t cap) {
    int rounds = 1 + (int) (rnd() % 4u);
    for (int r = 0; r < rounds; r++) {
        uint64_t op = rnd() % 6u;
        size_t pos = n ? (size_t) (rnd() % (n + 1u)) : 0u;
        if (op == 0 && n) {
            buf[pos % n] ^= (uint8_t) (1u << (rnd() % 8u));
        } else if (op == 1 && n) {
            buf[pos % n] = (uint8_t) rnd();
        } else if (op == 2) {
            const char *t = k_dict[rnd() % (sizeof k_dict / sizeof k_dict[0])];
            size_t tl = strlen(t);
            if (n + tl <= cap) {
                memmove(buf + pos + tl, buf + pos, n - pos);
                memcpy(buf + pos, t, tl);
                n += tl;
            }
        } else if (op == 3 && n) {
            size_t len = 1u + (size_t) (rnd() % 8u), at = pos % n;
            if (len > n - at) len = n - at;
            memmove(buf + at, buf + at + len, n - at - len);
            n -= len;
        } else if (op == 4 && n) {
            size_t at = pos % n, len = 1u + (size_t) (rnd() % 24u);
            if (len > n - at) len = n - at;
            if (n + len <= cap) {
                memmove(buf + at + len, buf + at, n - at);
                n += len;
            }
        } else if (n) {
            n = pos;
        }
    }
    return n;
}

int main(int argc, char **argv) {
    unsigned long long iters = argc > 1 ? strtoull(argv[1], NULL, 10) : 1000ull;
    const char *seed_env = getenv("ZF_FUZZ_SEED");
    const size_t nseeds = sizeof k_seeds / sizeof k_seeds[0];
    static uint8_t buf[1 << 14];
    if (seed_env) g_rng ^= strtoull(seed_env, NULL, 10) * 0x2545F4914F6CDD1Dull;
    for (size_t i = 0; i < nseeds; i++) LLVMFuzzerTestOneInput((const uint8_t *) k_seeds[i], strlen(k_seeds[i]));
    for (unsigned long long it = 0; it < iters; it++) {
        size_t s = (size_t) (rnd() % nseeds), n = strlen(k_seeds[s]);
        memcpy(buf, k_seeds[s], n);
        n = mutate(buf, n, sizeof buf);
        LLVMFuzzerTestOneInput(buf, n);
    }
    printf("fuzz_gen: %llu mutants + %zu seeds, 0 invariant failures (accepted %llu, refused %llu)\n", iters,
           nseeds, g_ok, g_refused);
    return 0;
}

#endif /* ZF_FUZZ_MAIN */
