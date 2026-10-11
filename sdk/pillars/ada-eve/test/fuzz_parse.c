/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* fuzz_parse.c — fuzzing the Ada and Eve front ends, the printers and the
 * codecs.
 *
 * One entry point, LLVMFuzzerTestOneInput, parses every input as Ada and
 * as Eve and checks:
 *   - no crash, and the same status and diagnostic on a second parse;
 *   - the status is one a parse may return, and a refusal's message
 *     starts "line L, column C: " with its own position, inside the text;
 *   - an accepted package's canonical Eve text and its printed Ada both
 *     parse back to the same digest, and the canonical text is a fixed
 *     point;
 *   - for every type (up to 4 KiB a value): the initial value passes
 *     ze_value_check; every scalar reached by a path takes a value made
 *     from the input bytes or refuses it leaving the bytes unchanged; a
 *     value read back as text stores back to the same bytes.
 *
 * Built two ways, like the sibling pillars' fuzzers:
 *   - with -DZE_FUZZ_MAIN: a deterministic mutation fuzzer (fixed-seed
 *     xorshift; built-in seeds plus any files given), used by `make test`
 *     and `make fuzz`, and under ASan/UBSan in CI;
 *   - with clang -fsanitize=fuzzer: libFuzzer drives the same function.
 *
 *   fuzz_parse ITERATIONS [seed ...]      ZE_FUZZ_SEED=n: another stream */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zeve.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static unsigned long long g_ok, g_refused, g_puts, g_put_ok;

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

typedef int32_t (*printer)(const ze_pkg *, char *, size_t, size_t *);

static char *print(const ze_pkg *p, printer f, size_t *len) {
    size_t need = 0;
    char *b;
    if (f(p, NULL, 0, &need) != ZXV_E_BUFFER) return NULL;
    b = (char *) malloc(need + 1u);
    if (!b) return NULL;
    if (f(p, b, need + 1u, len) != ZXV_OK || *len != need) {
        free(b);
        return NULL;
    }
    return b;
}

static int allowed(int32_t st) {
    return st == ZE_E_SYNTAX || st == ZE_E_UNSUPPORTED || st == ZE_E_NAME || st == ZE_E_CONSTRAINT ||
           st == ZE_E_LIMIT;
}

/* A scalar's path inside type t: the first leaf, descending through
 * records (component k % count) and arrays (the first index). */
static int leaf_path(const ze_pkg *p, uint32_t t, unsigned k, char *buf, size_t cap) {
    size_t n = 0;
    ze_type ti;
    buf[0] = '\0';
    for (int depth = 0; depth < 16; depth++) {
        if (ze_type_at(p, t, &ti) != ZXV_OK) return 0;
        if (ti.kind == ZE_K_RECORD) {
            ze_component c;
            if (ze_component_at(p, t, k % ti.count, &c) != ZXV_OK) return 0;
            int r = snprintf(buf + n, cap - n, "%s%s", n ? "." : "", c.name);
            if (r < 0 || (size_t) r >= cap - n) return 0;
            n += (size_t) r;
            t = c.type;
            k /= 3u;
        } else if (ti.kind == ZE_K_ARRAY) {
            ze_type it;
            int r = snprintf(buf + n, cap - n, "(");
            if (r < 0 || (size_t) r >= cap - n) return 0;
            n += (size_t) r;
            for (uint32_t i = 0; i < ti.dims; i++) {
                ze_dim d;
                char lit[ZE_MAX_NAME + 1];
                size_t ll = 0;
                if (ze_array_dim(p, t, i, &d) != ZXV_OK) return 0;
                if (d.index != UINT32_MAX && ze_type_at(p, d.index, &it) == ZXV_OK && it.kind == ZE_K_ENUM) {
                    if (ze_enum_literal(p, d.index, (uint32_t) d.hi, lit, sizeof lit, &ll) != ZXV_OK) return 0;
                    r = snprintf(buf + n, cap - n, "%s%s", i ? ", " : "", lit);
                } else {
                    r = snprintf(buf + n, cap - n, "%s%lld", i ? ", " : "", (long long) d.hi);
                }
                if (r < 0 || (size_t) r >= cap - n) return 0;
                n += (size_t) r;
            }
            r = snprintf(buf + n, cap - n, ")");
            if (r < 0 || (size_t) r >= cap - n) return 0;
            n += (size_t) r;
            t = ti.elem;
        } else {
            return 1;
        }
    }
    return 0;
}

static void check_values(const ze_pkg *p, const uint8_t *data, size_t size) {
    uint32_t count = 0;
    static uint8_t v[4096], w[4096];
    char path[512], text[128], back[128];
    if (ze_type_count(p, &count) != ZXV_OK) die("type count", data, size);
    for (uint32_t t = 0; t < count; t++) {
        ze_type ti;
        uint32_t ft;
        uint64_t off;
        size_t n, tl;
        if (ze_type_at(p, t, &ti) != ZXV_OK) die("type_at", data, size);
        if (ti.size == 0 || ti.size > sizeof v) continue;
        n = (size_t) ti.size;
        if (ze_value_init(p, t, v, n) != ZXV_OK) die("init", data, size);
        if (ze_value_check(p, t, v, n) != ZXV_OK) die("the initial value does not pass check", data, size);
        for (unsigned k = 0; k < 3; k++) {
            ze_type lt;
            int32_t st;
            if (!leaf_path(p, t, k + (unsigned) size, path, sizeof path)) continue;
            if (ze_find(p, t, path, &ft, &off) != ZXV_OK) {
                fprintf(stderr, "path '%s' of type %u\n", path, (unsigned) t);
                die("a generated path does not resolve", data, size);
            }
            if (ze_type_at(p, ft, &lt) != ZXV_OK || off + lt.size > ti.size) die("a path outside the value", data, size);
            /* a value from the input bytes */
            tl = size > 20 ? 20 : size;
            {
                size_t start = size > tl ? (size_t) (k * 7u) % (size - tl + 1u) : 0;
                memcpy(text, data + start, tl);
            }
            if (k == 1) tl = (size_t) snprintf(text, sizeof text, "%lld", (long long) (lt.lo / 2 + lt.hi / 2));
            memcpy(w, v, n);
            g_puts++;
            st = ze_put_value(p, t, v, n, path, text, tl);
            if (st == ZXV_OK) {
                size_t bl = 0;
                g_put_ok++;
                if (ze_value_check(p, t, v, n) != ZXV_OK) die("a stored value fails check", data, size);
                if (ze_get_value(p, t, v, n, path, back, sizeof back, &bl) == ZXV_OK) {
                    memcpy(w, v, n);
                    if (ze_put_value(p, t, v, n, path, back, bl) != ZXV_OK || memcmp(v, w, n) != 0)
                        die("a value read back does not store back to the same bytes", data, size);
                }
            } else if (memcmp(v, w, n) != 0) {
                die("a refused put changed the bytes", data, size);
            } else if (st != ZE_E_FIT && st != ZXV_E_FORMAT && st != ZXV_E_ARG && st != ZXV_E_RANGE) {
                die("an unexpected put status", data, size);
            }
        }
    }
}

static void one(const uint8_t *data, size_t size, int ada) {
    ze_pkg *p = NULL, *q = NULL;
    ze_diag d1, d2;
    char pos[64];
    int32_t s1, s2;
    memset(&d1, 0, sizeof d1);
    memset(&d2, 0, sizeof d2);
    s1 = ada ? ze_ada_parse((const char *) data, size, &p, &d1) : ze_eve_parse((const char *) data, size, &p, &d1);
    s2 = ada ? ze_ada_parse((const char *) data, size, &q, &d2) : ze_eve_parse((const char *) data, size, &q, &d2);
    if (s1 != s2) die("a second parse gave another status", data, size);
    if (s1 != ZXV_OK) {
        int r;
        g_refused++;
        if (p || q) die("a refusal returned a package", data, size);
        if (!allowed(s1)) die("a parse status outside the documented set", data, size);
        if (memcmp(&d1, &d2, sizeof d1) != 0) die("a second parse gave another diagnostic", data, size);
        r = snprintf(pos, sizeof pos, "line %u, column %u: ", d1.line, d1.col);
        if (r < 0 || strncmp(d1.msg, pos, (size_t) r) != 0) die("the message does not start with its position", data,
                                                                size);
        if (d1.line == 0 || d1.col == 0 || d1.offset > size) die("a position outside the text", data, size);
        if (!memchr(d1.msg, '\0', sizeof d1.msg)) die("an unterminated message", data, size);
        return;
    }
    g_ok++;
    {
        uint8_t da[32], db[32];
        size_t el = 0, al = 0, el2 = 0;
        char *eve = print(p, ze_pkg_eve, &el), *adat = print(p, ze_pkg_ada, &al), *eve2;
        ze_pkg *pe = NULL, *pa = NULL;
        ze_diag d;
        if (!eve || !adat) die("a package does not print", data, size);
        if (ze_pkg_digest(p, da) != ZXV_OK || ze_pkg_digest(q, db) != ZXV_OK || memcmp(da, db, 32) != 0)
            die("two parses of one text differ", data, size);
        if (ze_eve_parse(eve, el, &pe, &d) != ZXV_OK) die("the canonical Eve text does not parse", data, size);
        if (ze_ada_parse(adat, al, &pa, &d) != ZXV_OK) die("the printed Ada does not parse", data, size);
        if (ze_pkg_digest(pe, db) != ZXV_OK || memcmp(da, db, 32) != 0)
            die("the canonical Eve text has another digest", data, size);
        if (ze_pkg_digest(pa, db) != ZXV_OK || memcmp(da, db, 32) != 0)
            die("the printed Ada has another digest", data, size);
        eve2 = print(pe, ze_pkg_eve, &el2);
        if (!eve2 || el2 != el || memcmp(eve, eve2, el) != 0) die("the canonical text is not a fixed point", data, size);
        free(eve);
        free(eve2);
        free(adat);
        ze_pkg_destroy(pe);
        ze_pkg_destroy(pa);
    }
    check_values(p, data, size);
    ze_pkg_destroy(p);
    ze_pkg_destroy(q);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size > 8192) return 0;
    one(data, size, 1);
    one(data, size, 0);
    return 0;
}

#ifdef ZE_FUZZ_MAIN
static const char *const k_seeds[] = {
    "package P is\n   type Money is delta 0.01 digits 15;\n   type C is (A, B, C2);\n"
    "   type L is array (C, 1 .. 3) of Money;\n   type R is record\n      X : Money range -1.0 .. 1.0;\n"
    "      Y : C range A .. B;\n      Z : String (1 .. 4);\n      W : L;\n   end record;\nend P;\n",
    "eve p\nmoney = dec(15, 2)\nc = (a, b, c2)\nl = [c, 3] money\nr {\n  x: money -1 .. 1\n"
    "  y: c a .. b\n  z: str(4)\n  w: l\n}\n",
    "package Q with Pure is type T is range -2 ** 63 .. 2 ** 63 - 1; subtype S is T range 0 .. 9;"
    " type A is array (S range 1 .. 2, Boolean) of Boolean; end Q;\n",
    "eve q\nt = -2**63 .. 2**63 - 1\ns = t 0 .. 9\nn = str(3)\nrow = [2] n\ngrid { r: row; f: boolean }\n",
    "package R is\n   type D is delta 1.0E-18 digits 18;\n   type E is delta 1.0 digits 1;\n"
    "   type K is range 16#7F# .. 2#1111_1111#;\nend R;\n",
};

static uint64_t g_rng = 0x9E3779B97F4A7C15ull;
static uint64_t rnd(void) {
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 7;
    g_rng ^= g_rng << 17;
    return g_rng;
}

static const char *const k_tokens[] = {
    " is ",  " range ", " .. ", " delta 0.01 digits 5", "(", ")", ";", ",", "\n", " record ", " end record;",
    "2**63", "-",       "1.0",  "String (1 .. 3)",      "{", "}", "[", "]", ":", "dec(18, 18)", "str(1)",
    " type ", " subtype ", "array (", " of ", "#", "--", "\xc3\xa9", "\xff", "0.005", "99999999999999999999",
};

static size_t mutate(uint8_t *buf, size_t n, size_t cap, uint8_t *const *pool, const size_t *plen, size_t npool) {
    int k = 1 + (int) (rnd() % 4u);
    while (k--) {
        uint64_t r = rnd() % 7u;
        size_t at = n ? (size_t) (rnd() % (n + 1u)) : 0;
        if (r == 0 && n) { /* flip a byte */
            buf[at % n] = (uint8_t) rnd();
        } else if (r == 1 && n) { /* delete a run */
            size_t len = 1 + (size_t) (rnd() % 8u);
            if (at + len > n) len = n - at;
            memmove(buf + at, buf + at + len, n - at - len);
            n -= len;
        } else if (r == 2 || r == 3) { /* insert a token */
            const char *t = k_tokens[rnd() % (sizeof k_tokens / sizeof k_tokens[0])];
            size_t len = strlen(t);
            if (n + len <= cap) {
                memmove(buf + at + len, buf + at, n - at);
                memcpy(buf + at, t, len);
                n += len;
            }
        } else if (r == 4 && n) { /* change a digit */
            for (size_t i = 0; i < n; i++) {
                size_t j = (at + i) % n;
                if (buf[j] >= '0' && buf[j] <= '9') {
                    buf[j] = (uint8_t) ('0' + rnd() % 10u);
                    break;
                }
            }
        } else if (r == 5 && npool) { /* splice from another input */
            size_t j = (size_t) (rnd() % npool), from, len;
            if (plen[j]) {
                from = (size_t) (rnd() % plen[j]);
                len = 1 + (size_t) (rnd() % 32u);
                if (from + len > plen[j]) len = plen[j] - from;
                if (n + len <= cap) {
                    memmove(buf + at + len, buf + at, n - at);
                    memcpy(buf + at, pool[j] + from, len);
                    n += len;
                }
            }
        } else if (n) { /* duplicate a line */
            size_t s = at, e;
            while (s > 0 && buf[s - 1] != '\n') s--;
            e = at;
            while (e < n && buf[e] != '\n') e++;
            if (e < n) e++;
            if (n + (e - s) <= cap && e > s) {
                memmove(buf + e + (e - s), buf + e, n - e);
                memcpy(buf + e, buf + s, e - s);
                n += e - s;
            }
        }
    }
    return n;
}

static uint8_t *read_file(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    uint8_t *b = NULL;
    long sz;
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) == 0 && (sz = ftell(f)) >= 0 && sz <= 8192 && fseek(f, 0, SEEK_SET) == 0) {
        b = (uint8_t *) malloc((size_t) sz + 1u);
        if (b && fread(b, 1, (size_t) sz, f) != (size_t) sz) {
            free(b);
            b = NULL;
        }
        *len = (size_t) sz;
    }
    fclose(f);
    return b;
}

int main(int argc, char **argv) {
    enum { CAP = 8192, POOL = 256 };
    unsigned long long iters = argc > 1 ? strtoull(argv[1], NULL, 10) : 20000u;
    uint8_t *pool[POOL];
    size_t plen[POOL], npool = 0, nseed;
    static uint8_t buf[CAP];
    const char *seed_env = getenv("ZE_FUZZ_SEED");
    if (seed_env) g_rng ^= (strtoull(seed_env, NULL, 10) + 1u) * 0xD1B54A32D192ED03ull;
    if (g_rng == 0) g_rng = 1; /* xorshift never leaves 0 */
    for (size_t i = 0; i < sizeof k_seeds / sizeof k_seeds[0]; i++) {
        plen[npool] = strlen(k_seeds[i]);
        pool[npool] = (uint8_t *) malloc(plen[npool]);
        if (!pool[npool]) return 1;
        memcpy(pool[npool], k_seeds[i], plen[npool]);
        npool++;
    }
    for (int i = 2; i < argc && npool < POOL / 2; i++) {
        size_t n = 0;
        uint8_t *b = read_file(argv[i], &n);
        if (!b) {
            fprintf(stderr, "fuzz_parse: cannot read %s\n", argv[i]);
            return 1;
        }
        pool[npool] = b;
        plen[npool++] = n;
    }
    nseed = npool;
    for (size_t i = 0; i < npool; i++) LLVMFuzzerTestOneInput(pool[i], plen[i]);
    for (unsigned long long it = 0; it < iters; it++) {
        size_t j = (size_t) (rnd() % npool), n = plen[j];
        ze_pkg *p = NULL;
        memcpy(buf, pool[j], n);
        n = mutate(buf, n, CAP, pool, plen, npool);
        LLVMFuzzerTestOneInput(buf, n);
        /* keep accepted mutants: they reach deeper */
        if (ze_ada_parse((const char *) buf, n, &p, NULL) == ZXV_OK ||
            ze_eve_parse((const char *) buf, n, &p, NULL) == ZXV_OK) {
            size_t slot = npool;
            ze_pkg_destroy(p);
            if (npool == POOL) {
                slot = nseed + (size_t) (rnd() % (POOL - nseed));
                free(pool[slot]);
            } else {
                npool++;
            }
            pool[slot] = (uint8_t *) malloc(n ? n : 1u);
            if (!pool[slot]) return 1;
            memcpy(pool[slot], buf, n);
            plen[slot] = n;
        }
    }
    printf("fuzz_parse: %llu inputs (x2 front ends): %llu accepted, %llu refused; %llu puts, %llu stored\n",
           iters + (unsigned long long) nseed, g_ok, g_refused, g_puts, g_put_ok);
    for (size_t i = 0; i < npool; i++) free(pool[i]);
    return 0;
}
#endif
