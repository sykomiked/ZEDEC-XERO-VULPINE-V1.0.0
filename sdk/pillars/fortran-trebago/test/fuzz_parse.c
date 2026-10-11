/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* fuzz_parse.c — fuzzing the Trebago parser, printer and executor.
 *
 * One entry point, LLVMFuzzerTestOneInput, parses every input and checks:
 *   - no crash, and the same status and diagnostic on a second parse;
 *   - the status is one a parse may return, and a refusal's message
 *     starts "line L, column C: " with its own position;
 *   - an accepted program's canonical text parses back to the same text
 *     and digest, and lists the same parameters;
 *   - the program runs on small arrays made from the input bytes (every
 *     dimension 0..3, a low work limit) with an allowed status; a refusal
 *     leaves every output untouched; a second run, and a run of the
 *     re-parsed canonical text, give the same status and the same exact
 *     outputs; every output is reduced with a positive denominator.
 *
 * Built two ways, like the cobol-sutra fuzzer:
 *   - with -DTB_FUZZ_MAIN: a deterministic mutation fuzzer (fixed-seed
 *     xorshift; built-in seeds plus any files given), used by `make test`
 *     and `make fuzz`, and under ASan/UBSan in CI;
 *   - with clang -fsanitize=fuzzer: libFuzzer drives the same function.
 *
 *   fuzz_parse ITERATIONS [seed ...]      TB_FUZZ_SEED=n: another stream */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ztrebago.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static unsigned long long g_ok, g_refused, g_runs, g_run_ok;

static void die(const char *what, const uint8_t *d, size_t n) {
    fprintf(stderr, "fuzz_parse: INVARIANT BROKEN: %s (input %zu bytes)\n", what, n);
    FILE *f = fopen("fuzz-crash.txt", "wb");
    if (f) {
        fwrite(d, 1, n, f);
        fclose(f);
        fprintf(stderr, "  input written to fuzz-crash.txt\n");
    }
    abort();
}

static char *text_of(const tb_program *p) {
    size_t need = 0;
    char *b;
    if (tb_program_text(p, NULL, 0, &need) != ZXV_E_BUFFER) return NULL;
    b = (char *) malloc(need + 1u);
    if (!b) return NULL;
    if (tb_program_text(p, b, need + 1u, &need) != ZXV_OK) {
        free(b);
        return NULL;
    }
    return b;
}

#define MAXP 24
#define MAXE 16

typedef struct {
    tb_array a[MAXP];
    int64_t num[MAXP][MAXE], den[MAXP][MAXE];
    uint32_t n;
} binds;

/* Bindings for every parameter: dimension d gets (byte % 4), values come
 * from the input bytes; outputs are filled with a sentinel. Returns 0 when
 * a fixed extent is too large to run cheaply. */
static int make_binds(const tb_program *p, const uint8_t *d, size_t n, binds *b) {
    uint32_t np = 0;
    char dn[2 * MAXP][32];
    int64_t dv[2 * MAXP];
    uint32_t nd = 0, k = 0;
    tb_param_count(p, &np);
    if (np > MAXP) return 0;
    memset(b, 0, sizeof *b);
    for (uint32_t i = 0; i < np; i++) {
        tb_param pa;
        uint64_t elems = 1;
        tb_param_at(p, i, &pa);
        b->a[i].name = NULL;
        b->a[i].rank = pa.rank;
        for (uint32_t r = 0; r < pa.rank; r++) {
            int64_t e = pa.fixed[r];
            if (pa.dim[r][0]) {
                int found = -1;
                for (uint32_t j = 0; j < nd; j++)
                    if (strcmp(dn[j], pa.dim[r]) == 0) found = (int) j;
                if (found < 0) {
                    strcpy(dn[nd], pa.dim[r]);
                    dv[nd] = n ? (int64_t) (d[(nd * 7u) % n] % 4u) : 2;
                    found = (int) nd++;
                }
                e = dv[found];
            }
            b->a[i].extent[r] = e;
            elems *= (uint64_t) e;
        }
        if (elems > MAXE) return 0;
        for (uint64_t e = 0; e < MAXE; e++) {
            uint8_t v = n ? d[(k++ * 13u + 5u) % n] : (uint8_t) e;
            if (pa.role == TB_ROLE_OUT) {
                b->num[i][e] = 0x5A5A;
                b->den[i][e] = 0x5A5B;
            } else {
                b->num[i][e] = (int64_t) (v % 19u) - 9;
                if ((v & 0xF0u) == 0xF0u) b->num[i][e] = INT64_C(3037000499) * ((v & 1u) ? 1 : -1);
                b->den[i][e] = (int64_t) (v % 7u) + 1;
            }
        }
        b->a[i].num = b->num[i];
        b->a[i].den = b->den[i];
    }
    b->n = np;
    for (uint32_t i = 0; i < np; i++) {
        static char names[MAXP][40];
        tb_param pa;
        tb_param_at(p, i, &pa);
        strcpy(names[i], pa.name);
        b->a[i].name = names[i];
    }
    return 1;
}

static int allowed_run(int32_t st) {
    return st == ZXV_OK || st == ZXV_E_RANGE || st == TB_E_BOUNDS || st == TB_E_DIVZERO || st == TB_E_LIMIT ||
           st == TB_E_SHAPE;
}

static void run_checks(const tb_program *p, const tb_program *q, const uint8_t *d, size_t n) {
    static binds b1, b2, b3;
    tb_diag g1, g2, g3;
    int32_t s1, s2, s3;
    if (!make_binds(p, d, n, &b1)) return;
    b2 = b1;
    b3 = b1;
    for (uint32_t i = 0; i < b2.n; i++) {
        b2.a[i].num = b2.num[i];
        b2.a[i].den = b2.den[i];
        b3.a[i].num = b3.num[i];
        b3.a[i].den = b3.den[i];
    }
    s1 = tb_exec(p, b1.a, b1.n, 20000, &g1);
    s2 = tb_exec(p, b2.a, b2.n, 20000, &g2);
    s3 = tb_exec(q, b3.a, b3.n, 20000, &g3);
    g_runs++;
    if (!allowed_run(s1)) die("tb_exec returned a status it may not", d, n);
    if (s1 != s2 || memcmp(&g1, &g2, sizeof g1) != 0) die("tb_exec is not deterministic", d, n);
    if (s1 != s3) {
        fprintf(stderr, "%s\n%s\n", g1.msg, g3.msg);
        die("the canonical text runs differently", d, n);
    }
    if (memcmp(b1.num, b2.num, sizeof b1.num) || memcmp(b1.den, b2.den, sizeof b1.den) ||
        memcmp(b1.num, b3.num, sizeof b1.num) || memcmp(b1.den, b3.den, sizeof b1.den))
        die("outputs differ between equal runs", d, n);
    for (uint32_t i = 0; i < b1.n; i++) {
        uint64_t elems = 1;
        tb_param pa;
        tb_param_at(p, i, &pa);
        for (uint32_t r = 0; r < pa.rank; r++) elems *= (uint64_t) b1.a[i].extent[r];
        if (pa.role != TB_ROLE_OUT) continue;
        for (uint64_t e = 0; e < elems; e++) {
            int64_t rn, rd;
            if (s1 != ZXV_OK) {
                if (b1.num[i][e] != 0x5A5A || b1.den[i][e] != 0x5A5B) die("an output was written on a refusal", d, n);
                continue;
            }
            if (b1.den[i][e] <= 0 || zxv_rat_normalize(b1.num[i][e], b1.den[i][e], &rn, &rd) != ZXV_OK ||
                rn != b1.num[i][e] || rd != b1.den[i][e])
                die("an output is not reduced with a positive denominator", d, n);
        }
    }
    if (s1 == ZXV_OK) g_run_ok++;
}

static void accepted(const tb_program *p, const uint8_t *d, size_t n) {
    uint8_t d1[32], d2[32];
    char *s = text_of(p), *s2;
    tb_program *q = NULL;
    tb_diag dg;
    uint32_t c1 = 0, c2 = 0;
    if (!s) die("printing failed", d, n);
    if (tb_parse(s, strlen(s), &q, &dg) != ZXV_OK) {
        fprintf(stderr, "%s\n%s\n", dg.msg, s);
        die("canonical text does not parse", d, n);
    }
    s2 = text_of(q);
    if (!s2 || strcmp(s, s2) != 0) {
        fprintf(stderr, "%s\n---\n%s\n", s, s2 ? s2 : "(null)");
        die("canonical text is not a fixed point", d, n);
    }
    tb_program_digest(p, d1);
    tb_program_digest(q, d2);
    if (memcmp(d1, d2, 32)) die("digest differs after a round trip", d, n);
    tb_param_count(p, &c1);
    tb_param_count(q, &c2);
    if (c1 != c2) die("parameters differ after a round trip", d, n);
    for (uint32_t i = 0; i < c1; i++) {
        tb_param a, b;
        tb_param_at(p, i, &a);
        tb_param_at(q, i, &b);
        if (memcmp(&a, &b, sizeof a)) die("a parameter differs after a round trip", d, n);
    }
    run_checks(p, q, d, n);
    free(s);
    free(s2);
    tb_program_destroy(q);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    const char *text = (const char *) data;
    tb_program *a = NULL, *b = NULL;
    tb_diag d1, d2;
    int32_t s1 = tb_parse(text, size, &a, &d1);
    int32_t s2 = tb_parse(text, size, &b, &d2);
    if (s1 != s2 || memcmp(&d1, &d2, sizeof d1) != 0) die("parse is not deterministic", data, size);
    if (s1 == ZXV_OK) {
        g_ok++;
        accepted(a, data, size);
    } else if (s1 == TB_E_SYNTAX || s1 == TB_E_NAME || s1 == TB_E_SHAPE || s1 == TB_E_LIMIT ||
               s1 == ZXV_E_RANGE || s1 == ZXV_E_UTF8 || s1 == TB_E_DIVZERO) {
        char pre[64];
        g_refused++;
        if (a || b) die("a program came back with a refusal", data, size);
        if (d1.line == 0) die("a parse refusal without a line", data, size);
        snprintf(pre, sizeof pre, "line %u, column %u: ", d1.line, d1.col);
        if (strncmp(d1.msg, pre, strlen(pre)) != 0) die("message does not start with its position", data, size);
        {
            /* the reported line exists */
            uint32_t lines = 1;
            for (size_t i = 0; i < size; i++)
                if (data[i] == '\n') lines++;
            if (d1.line > lines) die("error line past the end of the input", data, size);
        }
    } else {
        die("tb_parse returned a status it may not", data, size);
    }
    tb_program_destroy(a);
    tb_program_destroy(b);
    return 0;
}

#ifdef TB_FUZZ_MAIN

static uint64_t g_rng = 0x2545F4914F6CDD1Dull;
static uint64_t rnd(void) {
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 7;
    g_rng ^= g_rng << 17;
    return g_rng;
}

static const char *const k_seeds[] = {
    "trebago dot\n  in x(n), y(n)\n  out s\n  s = sum(i, x(i) * y(i))\nend\n",
    "trebago mv\n  in a(m, n), x(n)\n  out y(m)\n  y(i) = sum(j, a(i, j) * x(j))\nend\n",
    ("trebago k\n  in x(n), alpha\n  inout z(n)\n  out p(n), h(n, n), t\n  const c = 1/3\n  var w(n)\n"
     "  z(i) = alpha * x(i) + z(i)\n  p(i) = scan(x(i) * c)\n  h(i, j) = 1 / (i + j - 1)\n"
     "  w(i) = x(n - i + 1) ** 2 - abs(min(x(i), -1, 0.5))\n  t = sum(i = 1:n - 1, max(w(i), p(i)))\nend\n"),
    ("trebago r\n  in a(n, n)\n  out b(n, n), s\n  b(i, j) = a(j, i) - sum(k = 1:i, a(k, j)) ** -1\n"
     "  s = sum(i, sum(j, b(i, j))) / 7 + 2 ** 61\nend\n"),
    "TREBAGO X ! c\n IN V(N)\n OUT S\n S = SUM(I, +((V(I)) * &\n  V(I)))\nEND\n",
};

static const char *const k_dict[] = {
    "sum(",  "scan(", "abs(", "min(",  "max(", "**",   " ** -", "(i)",   "(i, j)", "x(i)",  "x(n - i + 1)",
    "\n",    "&\n",   "! c",  " = ",   "1/3",  "0.25", "2 ** 62", "9223372036854775807", "i = 1:n, ",
    "in ",   "out ",  "var ", "inout ", "const ", "end", ", ",    ")",     "(",       "-",     "+",     "*",  "/",
};

static size_t mutate(uint8_t *buf, size_t n, size_t cap, uint8_t *const *pool, const size_t *plen, size_t npool) {
    int k = 1 + (int) (rnd() % 4u);
    while (k--) {
        uint64_t r = rnd() % 7u;
        size_t pos = n ? (size_t) (rnd() % (n + 1u)) : 0;
        if (r == 0 && n) {
            buf[rnd() % n] ^= (uint8_t) (1u << (rnd() % 8u));
        } else if (r == 1 && n) {
            static const char set[] = "()+-*/=,:!&\n 0123456789.ijknxyzs_aE";
            buf[rnd() % n] = (uint8_t) set[rnd() % (sizeof set - 1u)];
        } else if (r == 2) {
            const char *t = k_dict[rnd() % (sizeof k_dict / sizeof k_dict[0])];
            size_t tl = strlen(t);
            if (n + tl <= cap) {
                memmove(buf + pos + tl, buf + pos, n - pos);
                memcpy(buf + pos, t, tl);
                n += tl;
            }
        } else if (r == 3 && n) {
            size_t len = 1u + (size_t) (rnd() % 8u);
            if (pos + len > n) len = n - pos;
            memmove(buf + pos, buf + pos + len, n - pos - len);
            n -= len;
        } else if (r == 4 && n) {
            size_t from = (size_t) (rnd() % n), len = 1u + (size_t) (rnd() % 16u);
            if (from + len > n) len = n - from;
            if (n + len <= cap) {
                uint8_t tmp[16];
                memcpy(tmp, buf + from, len);
                memmove(buf + pos + len, buf + pos, n - pos);
                memcpy(buf + pos, tmp, len);
                n += len;
            }
        } else if (r == 5 && npool) {
            size_t j = (size_t) (rnd() % npool), from, len;
            if (!plen[j]) continue;
            from = (size_t) (rnd() % plen[j]);
            len = 1u + (size_t) (rnd() % 32u);
            if (from + len > plen[j]) len = plen[j] - from;
            if (n + len <= cap) {
                memmove(buf + pos + len, buf + pos, n - pos);
                memcpy(buf + pos, pool[j] + from, len);
                n += len;
            }
        } else if (n) {
            buf[rnd() % n] = (uint8_t) rnd();
        }
    }
    return n;
}

int main(int argc, char **argv) {
    enum { CAP = 4096, POOL = 256 };
    unsigned long long iters = argc > 1 ? strtoull(argv[1], NULL, 10) : 20000u;
    uint8_t *pool[POOL];
    size_t plen[POOL], npool = 0, nseed = 0;
    static uint8_t buf[CAP];
    const char *seed_env = getenv("TB_FUZZ_SEED");
    if (seed_env) g_rng ^= strtoull(seed_env, NULL, 10) * 0x9E3779B97F4A7C15ull;
    for (size_t i = 0; i < sizeof k_seeds / sizeof k_seeds[0]; i++) {
        plen[npool] = strlen(k_seeds[i]);
        pool[npool] = (uint8_t *) malloc(plen[npool]);
        memcpy(pool[npool], k_seeds[i], plen[npool]);
        npool++;
    }
    for (int a = 2; a < argc && npool < POOL; a++) {
        FILE *f = fopen(argv[a], "rb");
        size_t n;
        if (!f) continue;
        n = fread(buf, 1, CAP, f);
        fclose(f);
        pool[npool] = (uint8_t *) malloc(n ? n : 1u);
        memcpy(pool[npool], buf, n);
        plen[npool++] = n;
    }
    nseed = npool;
    for (size_t i = 0; i < npool; i++) LLVMFuzzerTestOneInput(pool[i], plen[i]);
    for (unsigned long long it = 0; it < iters; it++) {
        size_t j = (size_t) (rnd() % npool), n = plen[j];
        tb_program *p = NULL;
        memcpy(buf, pool[j], n);
        n = mutate(buf, n, CAP, pool, plen, npool);
        LLVMFuzzerTestOneInput(buf, n);
        /* keep accepted mutants: they reach deeper */
        if (tb_parse((const char *) buf, n, &p, NULL) == ZXV_OK) {
            size_t slot;
            if (npool < POOL) {
                slot = npool++;
            } else {
                slot = nseed + (size_t) (rnd() % (POOL - nseed));
                free(pool[slot]);
            }
            pool[slot] = (uint8_t *) malloc(n ? n : 1u);
            memcpy(pool[slot], buf, n);
            plen[slot] = n;
            tb_program_destroy(p);
        }
    }
    printf("fuzz_parse: %llu inputs: %llu accepted, %llu refused; %llu runs, %llu ok\n",
           iters + (unsigned long long) nseed, g_ok, g_refused, g_runs, g_run_ok);
    for (size_t i = 0; i < npool; i++) free(pool[i]);
    return 0;
}

#endif
