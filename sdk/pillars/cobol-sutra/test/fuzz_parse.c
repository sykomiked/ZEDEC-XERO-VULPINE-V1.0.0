/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* fuzz_parse.c — fuzzing the copybook and Sutra parsers and the codec.
 *
 * One entry point, LLVMFuzzerTestOneInput, feeds every input to the
 * Sutra parser and to the copybook parser in fixed and free format, and
 * checks for each:
 *   - no crash, and the same status, position and message on a second run;
 *   - the status is ZXV_OK, ZS_E_SYNTAX or ZS_E_UNSUPPORTED;
 *   - a refusal's offset is inside the input, its line and column agree
 *     with the offset, and the message starts "line L, column C: ";
 *   - an accepted layout's canonical Sutra parses back to the same text
 *     and digest, and its printed copybook parses to the same digest;
 *   - every field lies inside the record and inside its parent;
 *   - the codec on a record filled from the input bytes: decoding never
 *     crashes; a decoded value written back decodes to the same value; an
 *     initial record passes zs_record_check.
 *
 * Built two ways, like the z-contract fuzzer:
 *   - with -DZS_FUZZ_MAIN: a deterministic mutation fuzzer (fixed-seed
 *     xorshift; built-in seeds plus any files given), used by `make test`
 *     and `make fuzz`, and under ASan/UBSan in CI;
 *   - with clang -fsanitize=fuzzer: libFuzzer drives the same function.
 *
 *   fuzz_parse ITERATIONS [seed ...]      ZS_FUZZ_SEED=n: another stream */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zs_internal.h"
#include "zsutra.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static unsigned long long g_ok, g_refused, g_values;

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

static int32_t parse(int which, const char *t, size_t n, zs_layout **l, zs_diag *d) {
    if (which == 0) return zs_sutra_parse(t, n, l, d);
    return zs_copybook_parse(t, n, which == 1 ? ZS_CB_FIXED : ZS_CB_FREE, l, d);
}

static char *text_of(const zs_layout *l, int copybook) {
    size_t need = 0;
    char *b;
    int32_t st = copybook ? zs_layout_copybook(l, NULL, 0, &need) : zs_layout_sutra(l, NULL, 0, &need);
    if (st != ZXV_E_BUFFER) return NULL;
    b = (char *) malloc(need + 1u);
    if (!b) return NULL;
    st = copybook ? zs_layout_copybook(l, b, need + 1u, &need) : zs_layout_sutra(l, b, need + 1u, &need);
    if (st != ZXV_OK) {
        free(b);
        return NULL;
    }
    return b;
}

/* "NAME(1,1,...)" for the first occurrence of a field. */
static void ref_of(const zs_field *f, char *b, size_t cap) {
    size_t k = (size_t) snprintf(b, cap, "%s", f->name);
    if (!f->dims) return;
    for (uint32_t i = 0; i < f->dims; i++) k += (size_t) snprintf(b + k, cap - k, i ? ",1" : "(1");
    snprintf(b + k, cap - k, ")");
}

static void codec(const zs_layout *l, const uint8_t *d, size_t n) {
    uint64_t size = 0;
    uint32_t nf = 0;
    uint8_t *r, *r2;
    zs_layout_size(l, &size);
    if (size == 0 || size > 65536u) return;
    r = (uint8_t *) malloc((size_t) size);
    r2 = (uint8_t *) malloc((size_t) size);
    if (!r || !r2) {
        free(r);
        free(r2);
        return;
    }
    if (zs_record_init(l, r, (size_t) size) != ZXV_OK || zs_record_check(l, r, (size_t) size) != ZXV_OK)
        die("an initial record does not pass zs_record_check", d, n);
    for (uint64_t i = 0; i < size; i++) r[i] = n ? d[i % n] : (uint8_t) i;
    zs_field_count(l, &nf);
    for (uint32_t i = 0; i < nf && i < 256u; i++) {
        zs_field f;
        char ref[128], out[64];
        int64_t v = 0, v2 = 0;
        uint32_t sc = 0, sc2 = 0;
        size_t on = 0;
        zs_field_at(l, i, &f);
        if (f.kind != ZS_K_NUM || strcmp(f.name, "FILLER") == 0) continue;
        ref_of(&f, ref, sizeof ref);
        /* a value made from the input: encode, decode, compare */
        {
            uint64_t h = 1469598103934665603ull + i;
            int64_t lim = 1, w;
            for (size_t k = 0; k < n && k < 64u; k++) h = (h ^ d[k]) * 1099511628211ull;
            for (uint32_t k = 0; k < f.digits; k++) lim *= 10;
            w = (int64_t) (h % (uint64_t) lim);
            if (f.sign != ZS_SIGN_NONE && (h >> 63)) w = -w;
            memcpy(r2, r, (size_t) size);
            if (zs_put_decimal(l, r2, (size_t) size, ref, w, f.scale) != ZXV_OK ||
                zs_get_decimal(l, r2, (size_t) size, ref, &v2, &sc2) != ZXV_OK || v2 != w)
                die("an encoded value does not decode to itself", d, n);
            if (zs_put_decimal(l, r2, (size_t) size, ref, lim, f.scale) != ZS_E_FIT)
                die("10^digits was not refused", d, n);
            g_values++;
        }
        for (int style = 0; style < 2; style++) {
            zs_layout_set_overpunch((zs_layout *) l, style);
            if (zs_get_decimal(l, r, (size_t) size, ref, &v, &sc) != ZXV_OK) continue;
            g_values++;
            if (zs_get_value(l, r, (size_t) size, ref, out, sizeof out, &on) != ZXV_OK)
                die("a decodable value has no text", d, n);
            memcpy(r2, r, (size_t) size);
            if (zs_put_decimal(l, r2, (size_t) size, ref, v, sc) != ZXV_OK)
                die("a decoded value cannot be written back", d, n);
            if (zs_get_decimal(l, r2, (size_t) size, ref, &v2, &sc2) != ZXV_OK || v2 != v || sc2 != sc)
                die("write-back changed the value", d, n);
            if (zs_put_value(l, r2, (size_t) size, ref, out, on) != ZXV_OK ||
                zs_get_decimal(l, r2, (size_t) size, ref, &v2, &sc2) != ZXV_OK || v2 != v)
                die("the text of a value does not round-trip", d, n);
        }
    }
    zs_layout_set_overpunch((zs_layout *) l, ZS_OVERPUNCH_IBM);
    free(r);
    free(r2);
}

static void accepted(const zs_layout *l, const uint8_t *d, size_t n) {
    uint8_t d1[32], d2[32], d3[32];
    char *s = text_of(l, 0), *c = text_of(l, 1);
    zs_layout *l2 = NULL, *l3 = NULL;
    zs_diag dg;
    uint32_t nf = 0;
    uint64_t size = 0;
    if (!s || !c) die("printing failed", d, n);
    if (zs_sutra_parse(s, strlen(s), &l2, &dg) != ZXV_OK) {
        fprintf(stderr, "%s\n%s\n", dg.msg, s);
        die("canonical Sutra does not parse", d, n);
    }
    if (zs_copybook_parse(c, strlen(c), ZS_CB_FIXED, &l3, &dg) != ZXV_OK) {
        fprintf(stderr, "%s\n%s\n", dg.msg, c);
        die("the printed copybook does not parse", d, n);
    }
    {
        char *s2 = text_of(l2, 0);
        if (!s2 || strcmp(s, s2) != 0) die("canonical Sutra is not a fixed point", d, n);
        free(s2);
    }
    zs_layout_digest(l, d1);
    zs_layout_digest(l2, d2);
    zs_layout_digest(l3, d3);
    if (memcmp(d1, d2, 32) || memcmp(d1, d3, 32)) die("digest differs after a round trip", d, n);
    zs_layout_size(l, &size);
    zs_field_count(l, &nf);
    for (uint32_t i = 0; i < nf; i++) {
        zs_field f, p;
        zs_field_at(l, i, &f);
        zs_field_at(l, f.parent, &p);
        if (f.offset + f.size * (f.occurs ? f.occurs : 1u) > size) die("field outside the record", d, n);
        if (i && (f.offset < p.offset || f.offset + f.size * (f.occurs ? f.occurs : 1u) > p.offset + p.size))
            die("field outside its parent", d, n);
        if (f.kind == ZS_K_NUM && (f.digits < 1 || f.digits > 18 || f.scale > f.digits))
            die("numeric picture out of range", d, n);
    }
    codec(l, d, n);
    free(s);
    free(c);
    zs_layout_destroy(l2);
    zs_layout_destroy(l3);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    const char *text = (const char *) data;
    for (int which = 0; which < 3; which++) {
        zs_layout *a = NULL, *b = NULL;
        zs_diag d1, d2;
        int32_t s1 = parse(which, text, size, &a, &d1);
        int32_t s2 = parse(which, text, size, &b, &d2);
        if (s1 != s2 || memcmp(&d1, &d2, sizeof d1) != 0) die("parse is not deterministic", data, size);
        if (s1 == ZXV_OK) {
            g_ok++;
            accepted(a, data, size);
        } else if (s1 == ZS_E_SYNTAX || s1 == ZS_E_UNSUPPORTED) {
            char pre[64];
            uint32_t line = 1;
            uint64_t ls = 0;
            g_refused++;
            if (a || b) die("a layout came back with a refusal", data, size);
            if (d1.offset > size) die("error offset outside the input", data, size);
            for (uint64_t i = 0; i < d1.offset; i++)
                if (data[i] == '\n') {
                    line++;
                    ls = i + 1u;
                }
            if (line != d1.line || d1.offset - ls + 1u != d1.col)
                die("line/column disagree with the offset", data, size);
            snprintf(pre, sizeof pre, "line %u, column %u: ", (unsigned) d1.line, (unsigned) d1.col);
            if (strncmp(d1.msg, pre, strlen(pre)) != 0 || strlen(d1.msg) >= sizeof d1.msg)
                die("message format", data, size);
        } else {
            die("unexpected status", data, size);
        }
        zs_layout_destroy(a);
        zs_layout_destroy(b);
    }
    return 0;
}

#ifdef ZS_FUZZ_MAIN

static uint64_t g_rng = 0x9E3779B97F4A7C15ull;
static uint64_t rnd(void) {
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 7;
    g_rng ^= g_rng << 17;
    return g_rng;
}

static const char *const k_seeds[] = {
    "record r\n  a: s9(13)v99 comp-3\n  b[3]:\n    c: 9(4) comp\n    d: x(2)\n  e: sv99 sign leading separate\n",
    "       01 R.\n          05 A PIC S9(5)V99.\n          05 B OCCURS 2.\n"
    "             10 C PIC 9 COMP-3.\n          05 FILLER PIC X(3).\n",
    "01 r. 05 a pic x(4) value 'a.b'. 05 g comp-3. 10 n pic s9(3). 88 ok value 1 thru 5.\n",
    "record t\n a[2]:\n   b[2]:\n     c: s9(18) comp\n d: 9(18) sign trailing\n",
    "record w\n  a: s9(18) sign leading separate\n  b: s9(17)v9 sign trailing separate\n"
    "  c: s9(18) comp-3\n  d: 9(18) comp\n",
    "       01 R.\n          05 A PIC S9(3) SIGN IS LEADING SEPARATE CHARACTER.\n"
    "          05 B PIC 9(9) BINARY OCCURS 3 TIMES INDEXED BY I.\n",
};

static const char *const k_dict[] = {
    "01 ", "05 ", "10 ", "88 ", "66 ", "77 ", "PIC ", "PICTURE IS ", "S9(13)V99", "9(4)", "X(8)",
    "COMP", "COMP-3", "COMP-1", "BINARY", "PACKED-DECIMAL", "DISPLAY", "OCCURS 3", " TIMES",
    "INDEXED BY I", "SIGN LEADING SEPARATE", "TRAILING", "VALUE 'x'", "VALUE ZERO", "REDEFINES A",
    "FILLER", ".", ". ", "\n", "      *", "      -", "*>", "\t", "record ", "  ", ":", "[2]",
    "comp-3", "sign leading", "separate", "#", "x(65535)", "9(18)", "9(19)", "v9", "sv9(6)",
    "(", ")", "'", "\"", "\xc3\xa9", "\xff", "\r\n", "99999999", "0", "S", "V", "Z",
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
    const char *seed_env = getenv("ZS_FUZZ_SEED");
    uint8_t *seeds[64];
    size_t seed_len[64], nseeds = 0;
    static uint8_t buf[1 << 15];
    if (seed_env) g_rng ^= strtoull(seed_env, NULL, 10) * 0x2545F4914F6CDD1Dull;
    for (size_t i = 0; i < sizeof k_seeds / sizeof k_seeds[0]; i++) {
        seed_len[nseeds] = strlen(k_seeds[i]);
        seeds[nseeds] = (uint8_t *) malloc(seed_len[nseeds]);
        memcpy(seeds[nseeds], k_seeds[i], seed_len[nseeds]);
        nseeds++;
    }
    for (int a = 2; a < argc && nseeds < 64; a++) {
        FILE *f = fopen(argv[a], "rb");
        size_t n;
        if (!f) continue;
        n = fread(buf, 1, sizeof buf, f);
        fclose(f);
        seeds[nseeds] = (uint8_t *) malloc(n ? n : 1u);
        memcpy(seeds[nseeds], buf, n);
        seed_len[nseeds++] = n;
    }
    for (size_t i = 0; i < nseeds; i++) LLVMFuzzerTestOneInput(seeds[i], seed_len[i]);
    for (unsigned long long it = 0; it < iters; it++) {
        size_t s = (size_t) (rnd() % nseeds), n = seed_len[s];
        memcpy(buf, seeds[s], n);
        if (rnd() % 8u == 0) {
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
    printf("fuzz_parse: %llu mutants + %zu seeds x 3 parsers, 0 invariant failures\n", iters, nseeds);
    printf("  accepted %llu, refused %llu; %llu values round-tripped\n", g_ok, g_refused, g_values);
    for (size_t i = 0; i < nseeds; i++) free(seeds[i]);
    return 0;
}

#endif /* ZS_FUZZ_MAIN */
