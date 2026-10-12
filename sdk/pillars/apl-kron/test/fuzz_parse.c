/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* fuzz_parse.c — a deterministic mutation fuzzer for the parser and the
 * evaluator, and the same check as a libFuzzer target.
 *
 * For every input, in every spelling (AUTO, APL, K, CANON):
 *   - kr_parse returns OK or a KRON/libzxv refusal from the allowed set,
 *     and a refusal carries a diagnostic;
 *   - parsing twice gives the same digest (determinism);
 *   - the canonical text parses back (CANON) to itself, with the same
 *     digest (fixed point);
 *   - the program and its canonical re-parse evaluate, under the same
 *     bindings for their free names and a small work limit, to the same
 *     status and, when OK, the same value text.
 *
 * Build: make fuzz (KR_FUZZ_MAIN: `fuzz_parse ITERS seed-files...`), or
 * make fuzz-libfuzzer (LLVMFuzzerTestOneInput). The PRNG is a fixed-seed
 * xorshift (KR_FUZZ_SEED=n picks another stream), so a failure reproduces
 * from the seed and the iteration count. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zkron.h"

#define FZ_WORK (UINT64_C(1) << 18)

static void die(const char *what, const uint8_t *data, size_t len, int32_t spell) {
    fprintf(stderr, "fuzz_parse: %s (spelling %d) on %zu bytes:\n", what, (int) spell, len);
    fwrite(data, 1, len, stderr);
    fputc('\n', stderr);
    abort();
}

static int allowed(int32_t st) {
    if (st == ZXV_OK) return 1;
    if (st <= KR_E_SYNTAX && st >= KR_E_LIMIT) return 1;
    return st == ZXV_E_RANGE || st == ZXV_E_TOO_LONG || st == ZXV_E_UTF8;
}

/* A small pool of binding values; free name i gets pool[(i + salt) % 4]. */
static kr_value *g_pool[4];
static unsigned long g_parsed, g_evaluated; /* inputs that parsed; evaluations that gave a value */

static void pool_init(void) {
    static const int64_t s1[2] = {3, 0}, s2[2] = {2, 3};
    static const int64_t n1[3] = {1, -2, 3}, d1[3] = {1, 1, 2};
    static const int64_t n2[6] = {1, 2, 3, 4, 5, 6}, d2[6] = {1, 1, 1, 3, 1, 1};
    static const int64_t n0[1] = {7}, d0[1] = {1};
    static const int32_t t[3] = {1, 2, 0};
    if (g_pool[0]) return;
    if (kr_value_make(KR_KIND_NUM, 0, NULL, n0, d0, NULL, &g_pool[0]) != ZXV_OK ||
        kr_value_make(KR_KIND_NUM, 1, s1, n1, d1, NULL, &g_pool[1]) != ZXV_OK ||
        kr_value_make(KR_KIND_NUM, 2, s2, n2, d2, NULL, &g_pool[2]) != ZXV_OK ||
        kr_value_make(KR_KIND_TRUTH, 1, s1, NULL, NULL, t, &g_pool[3]) != ZXV_OK) {
        fprintf(stderr, "fuzz_parse: pool values\n");
        abort();
    }
}

typedef struct {
    int32_t st;
    char text[4096];
    size_t len;
} fz_result;

static void run(const kr_program *p, unsigned salt, fz_result *r) {
    uint32_t nf = 0;
    char names[8][40];
    kr_binding b[8];
    kr_value *v = NULL;
    kr_diag d;
    if (kr_free_count(p, &nf) != ZXV_OK) abort();
    if (nf > 8) nf = 8; /* the rest stay unbound: KR_E_NAME on both sides */
    for (uint32_t i = 0; i < nf; i++) {
        size_t n = 0;
        if (kr_free_at(p, i, names[i], sizeof names[i], &n) != ZXV_OK) abort();
        b[i].name = names[i];
        b[i].value = g_pool[(i + salt) & 3u];
    }
    r->st = kr_eval(p, b, nf, FZ_WORK, &v, &d);
    r->len = 0;
    r->text[0] = '\0';
    if (r->st == ZXV_OK) {
        int32_t ts = kr_value_text(v, r->text, sizeof r->text, &r->len);
        if (ts == ZXV_E_BUFFER) {
            r->len = (size_t) -1; /* too long to compare here: compare status only */
            r->text[0] = '\0';
        } else if (ts != ZXV_OK) {
            abort();
        }
    } else if (!d.msg[0]) {
        r->st = 1; /* flags a refusal without a diagnostic */
    }
    kr_value_destroy(v);
}

static void check_one(const uint8_t *data, size_t len) {
    static char text[1 << 16], text2[1 << 16];
    pool_init();
    for (int32_t spell = KR_SPELL_AUTO; spell <= KR_SPELL_CANON; spell++) {
        kr_program *p = NULL, *q = NULL, *c = NULL;
        kr_diag d;
        uint8_t dp[32], dq[32], dc[32];
        size_t n = 0, n2 = 0;
        int32_t st = kr_parse((const char *) data, len, spell, &p, &d), st2;
        if (!allowed(st)) die(kr_status_name(st), data, len, spell);
        if (st != ZXV_OK) {
            if (!d.msg[0]) die("a refusal without a diagnostic", data, len, spell);
            if (p) die("a refusal that returned a program", data, len, spell);
            continue;
        }
        g_parsed++;
        st2 = kr_parse((const char *) data, len, spell, &q, &d);
        if (st2 != ZXV_OK) die("the second parse refused", data, len, spell);
        kr_program_digest(p, dp);
        kr_program_digest(q, dq);
        if (memcmp(dp, dq, 32) != 0) die("two parses differ", data, len, spell);
        kr_program_destroy(q);

        st = kr_program_text(p, text, sizeof text, &n);
        if (st == ZXV_E_BUFFER) { /* a canonical text over 64 KiB: skip the fixed-point check */
            kr_program_destroy(p);
            continue;
        }
        if (st != ZXV_OK) die("kr_program_text", data, len, spell);
        st = kr_parse(text, n, KR_SPELL_CANON, &c, &d);
        if (st != ZXV_OK) {
            fprintf(stderr, "canonical text refused: %s\n%s\n", d.msg, text);
            die("the canonical text does not parse", data, len, spell);
        }
        if (kr_program_text(c, text2, sizeof text2, &n2) != ZXV_OK || n2 != n || memcmp(text, text2, n) != 0) {
            fprintf(stderr, "first:\n%s\nsecond:\n%s\n", text, text2);
            die("the canonical text is not a fixed point", data, len, spell);
        }
        kr_program_digest(c, dc);
        if (memcmp(dp, dc, 32) != 0) die("the canonical digest differs", data, len, spell);

        for (unsigned salt = 0; salt < 2; salt++) {
            static fz_result ra, rb;
            run(p, salt, &ra);
            run(c, salt, &rb);
            if (ra.st == 1 || rb.st == 1) die("an evaluation refusal without a diagnostic", data, len, spell);
            if (!allowed(ra.st) && ra.st != ZXV_E_NOMEM) die(kr_status_name(ra.st), data, len, spell);
            if (ra.st != rb.st || ra.len != rb.len || memcmp(ra.text, rb.text, ra.len == (size_t) -1 ? 0 : ra.len)) {
                fprintf(stderr, "program %s / %s\ncanonical %s / %s\n%s\n", kr_status_name(ra.st), ra.text,
                        kr_status_name(rb.st), rb.text, text);
                die("the program and its canonical form evaluate differently", data, len, spell);
            }
            if (ra.st == ZXV_OK) g_evaluated++;
        }
        kr_program_destroy(c);
        kr_program_destroy(p);
    }
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t len);
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t len) {
    if (len > 4096) return 0;
    check_one(data, len);
    return 0;
}

#ifdef KR_FUZZ_MAIN
static uint64_t g_rng = UINT64_C(0x4b524f4e20260101);

static uint64_t rnd(void) {
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 7;
    g_rng ^= g_rng << 17;
    return g_rng;
}

/* Tokens worth inserting: glyphs, K symbols, words, numbers, names. */
static const char *const g_tok[] = {
    "+", "-", "×", "÷", "⌈", "⌊", "|", "*", "%", "&", "<", ">", "=", "≠", "≤", "≥", "~", "∧", "∨",
    "⍳", "⍴", "⌽", "⊖", "⍉", ",", "⍪", "↑", "↓", "⍋", "⍒", "≡", "≢", "⍷", "⊃", "/", "⌿", "\\",
    "⍀", "∘.", ".", "\\:", "!", "#", "_", "^", "?", "@", "<=", ">=", "<>", "/:", "'", "¨",
    "(", ")", "[", "]", ";", "⋄", "\n", "←", ":", " ", "  ", "¯", "¯1", "-1", "0", "1", "2",
    "3", "1r3", "2r0", "0.5", "1e3", "9223372036854775807", "4611686018427387904", "x", "y",
    "abc", "kron", "true", "false", "neither", "both", "(vec 1 2)", "(mat 2 2 1 2 3 4)",
    "(add 1 2)", "(reduce add x)", "(let x 3)", "iota", "rev", "reshape", "\xff", "\xe2\x8d",
    "1 2 3", "2 3⍴⍳6", "+/", "×/", "⌈\\", "1 0 1", "0⍴0",
};
#define NTOK (sizeof g_tok / sizeof g_tok[0])

/* Built-in seeds, so the fuzzer does not spend its time inside comments. */
static const char *const g_seed[] = {
    "+/⍳10", "×\\1 2 3 4", "x←2 3⍴⍳6 ⋄ (⍉x)+.×x", "(⍳3)∘.≤⍳3", "⌽⊖2 2⍴1r2 ¯3 4 5",
    "3↑¯2↓⍳9", "x←5 3 4 1 ⋄ x[⍋x]", "(1 2 3≡1 2 3)∧0<1", "+/1r3 1r6 1r2", "⌈/⌊/3 3⍴⍳9",
    "x←4 1 2 ⋄ x⍳2 7", "1⌽2 3⍴⍳6", "(2 2⍴⍳4)[1;0]", "+⌿2 3⍴⍳6", "-\\1 2 3", "1 0 1/1 2 3",
    "+/!10", "x:2 3#!6;(+x)", "(!3)*\\:!3", "|/1 5 2", "x:5 3 4 1;x@<x", "3#|!5", "-/1 2 3",
    "1 2 3~1 2 3", "x: 1r3 2;x%2", "kron\n(reduce add (iota 10))", "kron\n(let x 3)\n(mul x x)",
    "kron\n(outer lt (vec 1 2) (vec 2 1))", "kron\n(index2 (mat 2 2 1 2 3 4) 1 _)",
};
#define NSEED (sizeof g_seed / sizeof g_seed[0])

static size_t mutate(uint8_t *b, size_t len, size_t cap, uint8_t *const *seeds, const size_t *slen,
                     size_t nseed) {
    int k = 1 + (rnd() % 4u == 0 ? (int) (rnd() % 4u) : 0); /* mostly one mutation */
    while (k--) {
        unsigned op = (unsigned) (rnd() % 6u);
        size_t at = len ? (size_t) (rnd() % (len + 1u)) : 0;
        if (op == 0 && len) { /* flip a byte */
            b[at % len] ^= (uint8_t) (1u << (rnd() % 8u));
        } else if (op == 1 || op == 5) { /* insert a token */
            const char *t = g_tok[rnd() % NTOK];
            size_t tl = strlen(t);
            if (len + tl <= cap) {
                memmove(b + at + tl, b + at, len - at);
                memcpy(b + at, t, tl);
                len += tl;
            }
        } else if (op == 2 && len) { /* delete a span */
            size_t span = 1u + (size_t) (rnd() % 8u);
            if (at >= len) at = len - 1u;
            if (at + span > len) span = len - at;
            memmove(b + at, b + at + span, len - at - span);
            len -= span;
        } else if (op == 3 && nseed) { /* splice from a seed */
            size_t s = (size_t) (rnd() % nseed);
            if (slen[s]) {
                size_t from = (size_t) (rnd() % slen[s]);
                size_t span = 1u + (size_t) (rnd() % 24u);
                if (from + span > slen[s]) span = slen[s] - from;
                if (len + span <= cap) {
                    memmove(b + at + span, b + at, len - at);
                    memcpy(b + at, seeds[s] + from, span);
                    len += span;
                }
            }
        } else if (op == 4 && len) { /* duplicate a span */
            size_t from = (size_t) (rnd() % len);
            size_t span = 1u + (size_t) (rnd() % 12u);
            if (from + span > len) span = len - from;
            if (len + span <= cap) {
                memmove(b + at + span, b + at, len - at);
                memmove(b + at, b + (from >= at ? from + span : from), span);
                len += span;
            }
        }
    }
    return len;
}

static uint8_t *slurp(const char *path, size_t *out) {
    FILE *f = fopen(path, "rb");
    uint8_t *b = NULL;
    size_t n = 0, cap = 0, r;
    if (!f) return NULL;
    for (;;) {
        if (n == cap) {
            uint8_t *nb;
            cap = cap ? cap * 2u : 4096u;
            nb = (uint8_t *) realloc(b, cap);
            if (!nb) {
                free(b);
                fclose(f);
                return NULL;
            }
            b = nb;
        }
        r = fread(b + n, 1, cap - n, f);
        if (!r) break;
        n += r;
    }
    fclose(f);
    *out = n;
    return b;
}

int main(int argc, char **argv) {
    enum { CAP = 4096 };
    static uint8_t buf[CAP];
    uint8_t *seeds[64 + NSEED];
    size_t slen[64 + NSEED], nseed = 0, nfile;
    long iters = argc > 1 ? atol(argv[1]) : 20000;
    const char *seed_env = getenv("KR_FUZZ_SEED");
    if (seed_env && *seed_env) { /* another stream of inputs: KR_FUZZ_SEED=1..n */
        uint64_t sd = (uint64_t) strtoull(seed_env, NULL, 10);
        g_rng ^= sd * UINT64_C(0x9e3779b97f4a7c15);
        if (!g_rng) g_rng = 1;
    }
    for (size_t i = 0; i < NSEED; i++) {
        slen[nseed] = strlen(g_seed[i]);
        seeds[nseed] = (uint8_t *) malloc(slen[nseed]);
        if (!seeds[nseed]) return 2;
        memcpy(seeds[nseed], g_seed[i], slen[nseed]);
        check_one(seeds[nseed], slen[nseed]);
        nseed++;
    }
    for (int i = 2; i < argc && nseed < 64 + NSEED; i++) {
        seeds[nseed] = slurp(argv[i], &slen[nseed]);
        if (!seeds[nseed]) {
            fprintf(stderr, "fuzz_parse: cannot read %s\n", argv[i]);
            return 2;
        }
        if (slen[nseed] > CAP / 2) slen[nseed] = CAP / 2;
        check_one(seeds[nseed], slen[nseed]);
        nseed++;
    }
    nfile = nseed - NSEED;
    for (long it = 0; it < iters; it++) {
        size_t len = 0;
        if (nseed && rnd() % 8u) {
            size_t s = (size_t) (rnd() % nseed);
            memcpy(buf, seeds[s], slen[s]);
            len = slen[s];
        }
        len = mutate(buf, len, CAP, seeds, slen, nseed);
        check_one(buf, len);
    }
    for (size_t i = 0; i < nseed; i++) free(seeds[i]);
    for (int i = 0; i < 4; i++) kr_value_destroy(g_pool[i]);
    printf("fuzz_parse: %ld mutated inputs and %zu seed files (and %zu built in), 4 spellings each: "
           "%lu parses, %lu values, no failures\n", iters, nfile, (size_t) NSEED, g_parsed, g_evaluated);
    return 0;
}
#endif
