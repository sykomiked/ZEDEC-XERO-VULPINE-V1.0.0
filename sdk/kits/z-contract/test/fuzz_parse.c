/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* fuzz_parse.c — fuzzing the contract parser and the checker.
 *
 * One entry point, LLVMFuzzerTestOneInput, checks for every input:
 *   - zc_parse never crashes and is deterministic (status, position and
 *     message are the same on a second run);
 *   - a refusal's offset is inside the input, and its line and column agree
 *     with that offset; the message starts "line L, column C: ", is
 *     NUL-terminated, and a truncated copy is a prefix of the full one;
 *   - an accepted contract formats to a canonical text that parses back to
 *     the same canonical text and digest;
 *   - for every function, values at each declared bound pass and values one
 *     step outside fail with BELOW / ABOVE; zc_check is deterministic and
 *     its reason and index are in range.
 *
 * Built two ways, like z-wasm's fuzz_validate:
 *   - with -DZC_FUZZ_MAIN: a deterministic mutation fuzzer (fixed-seed
 *     xorshift; seeds built in plus any .zc files given), used by
 *     `make test` / `make fuzz` and under ASan/UBSan in CI;
 *   - with clang -fsanitize=fuzzer: libFuzzer drives the same function.
 *
 *   fuzz_parse ITERATIONS [seed.zc ...]      ZC_FUZZ_SEED=n: another stream */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zc_internal.h"
#include "zcontract.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static unsigned long long g_ok, g_refused, g_checks, g_viol;

static void die(const char *what, const uint8_t *d, size_t n) {
    fprintf(stderr, "fuzz_parse: INVARIANT BROKEN: %s (input %zu bytes)\n", what, n);
    FILE *f = fopen("fuzz-crash.zc", "wb");
    if (f) {
        fwrite(d, 1, n, f);
        fclose(f);
        fprintf(stderr, "  input written to fuzz-crash.zc\n");
    }
    abort();
}

static char *canonical(const zc_contract *c) {
    size_t need = 0;
    char *b;
    if (zc_contract_format(c, NULL, 0, &need) != ZXV_E_BUFFER) return NULL;
    b = (char *) malloc(need + 1u);
    if (b && zc_contract_format(c, b, need + 1u, &need) != ZXV_OK) {
        free(b);
        return NULL;
    }
    return b;
}

/* Check `vals` twice; returns the reason. */
static int32_t check2(const zc_contract *c, const char *fn, int32_t part, zc_vals *v,
                      const uint8_t *d, size_t n) {
    int32_t r1 = -1, r2 = -2;
    uint32_t i1 = 0, i2 = 1;
    char m1[400], m2[400];
    int32_t s1 = zc_check(c, fn, part, v, &r1, &i1, m1, sizeof m1, NULL);
    int32_t s2 = zc_check(c, fn, part, v, &r2, &i2, m2, sizeof m2, NULL);
    g_checks++;
    if (s1 != s2 || r1 != r2 || i1 != i2 || strcmp(m1, m2) != 0) die("zc_check not deterministic", d, n);
    if ((s1 == ZXV_OK) != (r1 == ZC_V_OK) || (s1 != ZXV_OK && s1 != ZC_E_VIOLATION))
        die("zc_check status and reason disagree", d, n);
    if (r1 < 0 || r1 > ZC_V_COUNT) die("reason out of range", d, n);
    if (r1 != ZC_V_OK) g_viol++;
    return r1;
}

static void push_for(zc_vals *v, const zc_type *t, int which) {
    /* which: 0 lo, 1 hi, 2 a middle-ish value */
    switch (t->kind) {
    case ZC_T_INT:
        zc_vals_push_int(v, which == 0 && t->has_lo ? t->lo_n : which == 1 && t->has_hi ? t->hi_n : 0);
        break;
    case ZC_T_RAT:
        if (which == 0 && t->has_lo) zc_vals_push_rat(v, t->lo_n, t->lo_d);
        else if (which == 1 && t->has_hi) zc_vals_push_rat(v, t->hi_n, t->hi_d);
        else zc_vals_push_rat(v, 0, 1);
        break;
    case ZC_T_TRUTH: zc_vals_push_truth(v, which); break;
    case ZC_T_TAG: zc_vals_push_tag(v, which * 21); break;
    default: {
        static const uint8_t z[64];
        size_t len = which == 0 ? (size_t) (t->blo < 64 ? t->blo : 64)
                                : which == 1 && t->has_hi && t->bhi < 64 ? (size_t) t->bhi : 3u;
        zc_vals_push_bytes(v, z, len);
        break;
    }
    }
}

static void exercise(const zc_contract *c, const uint8_t *d, size_t n) {
    zc_vals *v = NULL;
    if (zc_vals_create(&v) != ZXV_OK) return;
    for (uint32_t k = 0; k < c->nfns; k++) {
        const zc_fn *f = &c->fns[k];
        for (int which = 0; which < 3; which++) {
            zc_vals_clear(v);
            for (uint32_t i = 0; i < f->nparams; i++) push_for(v, &f->params[i].type, which);
            (void) check2(c, f->name, ZC_PART_PARAMS, v, d, n);
            zc_vals_clear(v);
            if (f->has_result) push_for(v, &f->result, which);
            (void) check2(c, f->name, ZC_PART_RESULT, v, d, n);
            zc_vals_clear(v);
            if (f->has_proposes)
                for (uint32_t i = 0; i < f->max_proposals; i++) push_for(v, &f->proposes, which);
            (void) check2(c, f->name, ZC_PART_PROPOSALS, v, d, n);
        }
        /* bound semantics, one parameter at a time, the others at a value
         * inside their own type when there is an obvious one */
        for (uint32_t i = 0; i < f->nparams; i++) {
            const zc_type *t = &f->params[i].type;
            if (t->kind != ZC_T_INT) continue;
            for (int side = 0; side < 2; side++) {
                int has = side ? t->has_hi : t->has_lo;
                int64_t b = side ? t->hi_n : t->lo_n;
                if (!has) continue;
                int64_t out = side ? b + (b < INT64_MAX) : b - (b > -INT64_MAX);
                if (out == b) continue;
                zc_vals_clear(v);
                for (uint32_t j = 0; j < f->nparams; j++) {
                    if (j == i) zc_vals_push_int(v, b);
                    else push_for(v, &f->params[j].type, 0);
                }
                int32_t r_in = check2(c, f->name, ZC_PART_PARAMS, v, d, n);
                zc_vals_clear(v);
                for (uint32_t j = 0; j < f->nparams; j++) {
                    if (j == i) zc_vals_push_int(v, out);
                    else push_for(v, &f->params[j].type, 0);
                }
                int32_t r_out = -1;
                uint32_t idx = 99;
                zc_check(c, f->name, ZC_PART_PARAMS, v, &r_out, &idx, NULL, 0, NULL);
                g_checks++;
                /* the bound itself never fails at index i; one step out
                 * fails at i, unless an earlier parameter already failed */
                if (r_in != ZC_V_OK) {
                    uint32_t i_in = 99;
                    zc_vals_clear(v);
                    for (uint32_t j = 0; j < f->nparams; j++) {
                        if (j == i) zc_vals_push_int(v, b);
                        else push_for(v, &f->params[j].type, 0);
                    }
                    zc_check(c, f->name, ZC_PART_PARAMS, v, NULL, &i_in, NULL, 0, NULL);
                    if (i_in == i) die("a value at its declared bound was refused", d, n);
                    continue;
                }
                if (r_out != (side ? ZC_V_ABOVE : ZC_V_BELOW) || idx != i)
                    die("one step outside a bound was not BELOW/ABOVE at that parameter", d, n);
            }
        }
    }
    zc_vals_destroy(v);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    zc_contract *c1 = NULL, *c2 = NULL;
    uint32_t l1 = 0, l2 = 0, k1 = 0, k2 = 0;
    uint64_t o1 = 0, o2 = 0;
    char m1[400], m2[400], small[24];
    size_t ml1 = 0, ml2 = 0, mls = 0;
    const char *text = (const char *) data;
    int32_t s1 = zc_parse(text, size, &c1, &l1, &k1, &o1, m1, sizeof m1, &ml1);
    int32_t s2 = zc_parse(text, size, &c2, &l2, &k2, &o2, m2, sizeof m2, &ml2);
    if (s1 != s2 || l1 != l2 || k1 != k2 || o1 != o2 || strcmp(m1, m2) != 0 || ml1 != ml2)
        die("zc_parse not deterministic", data, size);
    if (s1 != ZXV_OK && s1 != ZC_E_SYNTAX) die("unexpected status", data, size);
    if (s1 == ZC_E_SYNTAX) {
        char pre[64];
        uint32_t line = 1;
        uint64_t ls = 0;
        g_refused++;
        if (o1 > size) die("error offset outside the input", data, size);
        for (uint64_t i = 0; i < o1; i++)
            if (data[i] == '\n') {
                line++;
                ls = i + 1u;
            }
        if (line != l1 || o1 - ls + 1u != k1) die("line/column disagree with the offset", data, size);
        snprintf(pre, sizeof pre, "line %u, column %u: ", (unsigned) l1, (unsigned) k1);
        if (strncmp(m1, pre, strlen(pre)) != 0 || strlen(m1) != ml1 || ml1 >= sizeof m1)
            die("message format", data, size);
        (void) zc_parse(text, size, &c2, NULL, NULL, NULL, small, sizeof small, &mls);
        if (mls != ml1 || strncmp(small, m1, sizeof small - 1u) != 0 || strlen(small) >= sizeof small)
            die("truncated message is not a prefix", data, size);
        return 0;
    }
    g_ok++;
    char *t1 = canonical(c1);
    if (!t1) die("format failed", data, size);
    zc_contract *c3 = NULL;
    if (zc_parse(t1, strlen(t1), &c3, NULL, NULL, NULL, m2, sizeof m2, NULL) != ZXV_OK) {
        fprintf(stderr, "canonical text refused: %s\n%s\n", m2, t1);
        die("canonical text does not parse", data, size);
    }
    char *t2 = canonical(c3);
    if (!t2 || strcmp(t1, t2) != 0) die("canonical text is not a fixed point", data, size);
    uint8_t d1[32], d3[32];
    if (zc_contract_digest(c1, d1) != ZXV_OK || zc_contract_digest(c3, d3) != ZXV_OK ||
        memcmp(d1, d3, 32) != 0)
        die("digest differs after a round trip", data, size);
    exercise(c1, data, size);
    free(t1);
    free(t2);
    zc_contract_destroy(c1);
    zc_contract_destroy(c2);
    zc_contract_destroy(c3);
    return 0;
}

#ifdef ZC_FUZZ_MAIN

static uint64_t g_rng = 0x9E3779B97F4A7C15ull;
static uint64_t rnd(void) {
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 7;
    g_rng ^= g_rng << 17;
    return g_rng;
}

static const char *const k_seeds[] = {
    "fn fee(amount: int[0..10^12], bps: int[0..10000]) -> int[0..0] proposes rat[0..10^12] den|100 max 1;",
    "fn f(a: rat[-1.5..3/4] den<=1000, t: truth{true, false}, g: tag{neither/neutral/emerging, true/*/*});",
    "fn order(order.price: rat[0..*] den|10000, order.qty: int[1..10^9], sym: bytes[1..12]) -> tag;",
    "# comment\nfn a();\nfn b(x: int[*..5]) -> truth{both};\nfn c() proposes int[1..*] max 64;\n",
    "fn g(x: int[-9223372036854775807..9223372036854775807], y: rat[1/3..2/3]);",
};

static const char *const k_dict[] = {
    "fn ", "int", "rat", "truth", "tag", "bytes", " den<=", " den|", " proposes ", " max ", " -> ",
    "..", "[", "]", "{", "}", "(", ")", ",", ":", ";", "^", "/", "*", "-", ".", "0", "1", "9",
    "10^12", "2^63", "9223372036854775807", "x", "a.b", "neither", "true", "false", "both",
    "neutral", "forbidden", "emerging", "manifest", "#", "\n", " ", "\xc3\xa9", "\xff", "1/0",
    "0.000001", "*/*/*",
};

static size_t mutate(uint8_t *buf, size_t n, size_t cap) {
    int rounds = 1 + (int) (rnd() % 4u);
    for (int r = 0; r < rounds; r++) {
        uint64_t op = rnd() % 6u;
        size_t pos = n ? (size_t) (rnd() % (n + 1u)) : 0u;
        if (op == 0 && n) { /* flip a byte */
            buf[pos % n] ^= (uint8_t) (1u << (rnd() % 8u));
        } else if (op == 1 && n) { /* random byte */
            buf[pos % n] = (uint8_t) rnd();
        } else if (op == 2) { /* insert a dictionary token */
            const char *t = k_dict[rnd() % (sizeof k_dict / sizeof k_dict[0])];
            size_t tl = strlen(t);
            if (n + tl <= cap) {
                memmove(buf + pos + tl, buf + pos, n - pos);
                memcpy(buf + pos, t, tl);
                n += tl;
            }
        } else if (op == 3 && n) { /* delete a range */
            size_t len = 1u + (size_t) (rnd() % 8u);
            size_t at = pos % n;
            if (len > n - at) len = n - at;
            memmove(buf + at, buf + at + len, n - at - len);
            n -= len;
        } else if (op == 4 && n) { /* duplicate a range */
            size_t at = pos % n, len = 1u + (size_t) (rnd() % 16u);
            if (len > n - at) len = n - at;
            if (n + len <= cap) {
                memmove(buf + at + len, buf + at, n - at);
                n += len;
            }
        } else if (n) { /* truncate */
            n = pos;
        }
    }
    return n;
}

int main(int argc, char **argv) {
    unsigned long long iters = argc > 1 ? strtoull(argv[1], NULL, 10) : 1000ull;
    const char *seed_env = getenv("ZC_FUZZ_SEED");
    uint8_t *seeds[64];
    size_t seed_len[64], nseeds = 0;
    static uint8_t buf[1 << 16];
    if (seed_env) g_rng ^= strtoull(seed_env, NULL, 10) * 0x2545F4914F6CDD1Dull;
    for (size_t i = 0; i < sizeof k_seeds / sizeof k_seeds[0]; i++) {
        seed_len[nseeds] = strlen(k_seeds[i]);
        seeds[nseeds] = (uint8_t *) malloc(seed_len[nseeds]);
        memcpy(seeds[nseeds], k_seeds[i], seed_len[nseeds]);
        nseeds++;
    }
    for (int a = 2; a < argc && nseeds < 64; a++) {
        FILE *f = fopen(argv[a], "rb");
        if (!f) continue;
        size_t n = fread(buf, 1, sizeof buf, f);
        fclose(f);
        seeds[nseeds] = (uint8_t *) malloc(n ? n : 1u);
        memcpy(seeds[nseeds], buf, n);
        seed_len[nseeds++] = n;
    }
    for (size_t i = 0; i < nseeds; i++) LLVMFuzzerTestOneInput(seeds[i], seed_len[i]);
    for (unsigned long long it = 0; it < iters; it++) {
        size_t s = (size_t) (rnd() % nseeds), n = seed_len[s];
        memcpy(buf, seeds[s], n);
        if (rnd() % 8u == 0) { /* splice in part of another seed */
            size_t o = (size_t) (rnd() % nseeds), at = n ? (size_t) (rnd() % n) : 0u;
            size_t take = seed_len[o] / 2u;
            if (at + take <= sizeof buf) {
                memcpy(buf + at, seeds[o], take);
                if (at + take > n) n = at + take;
            }
        }
        n = mutate(buf, n, sizeof buf);
        LLVMFuzzerTestOneInput(buf, n);
    }
    printf("fuzz_parse: %llu mutants + %zu seeds, 0 invariant failures\n", iters, nseeds);
    printf("  accepted %llu, refused %llu; %llu checks (%llu violations)\n", g_ok, g_refused,
           g_checks, g_viol);
    for (size_t i = 0; i < nseeds; i++) free(seeds[i]);
    return 0;
}

#endif /* ZC_FUZZ_MAIN */
