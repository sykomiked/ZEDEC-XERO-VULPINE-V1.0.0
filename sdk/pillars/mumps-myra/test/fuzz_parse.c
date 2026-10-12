/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* fuzz_parse.c — fuzzing the global-map and MYRA parsers, the value codec,
 * references and collation.
 *
 * One entry point, LLVMFuzzerTestOneInput, feeds every input to the MYRA
 * parser, the global-map parser and the auto-detecting zm_schema_parse,
 * and checks for each:
 *   - no crash, and the same status, position and message on a second run;
 *   - the status is ZXV_OK, ZM_E_SYNTAX, ZM_E_UNSUPPORTED, ZXV_E_UTF8 or
 *     ZXV_E_CAPACITY;
 *   - a refusal's offset is inside the input, its line and column agree
 *     with the offset, and the message starts "line L, column C: ";
 *   - an accepted schema's canonical MYRA parses back to the same text and
 *     digest, and its printed global map parses to the same digest;
 *   - the codec, on every node of an accepted schema, with values cut from
 *     the input: decoding never crashes; a decoded value encodes to a
 *     canonical string that decodes and encodes to itself, and every
 *     numeric piece of it is an M canonical number.
 * It also reads the input as a global reference and as three subscripts,
 * and checks the collation laws on them.
 *
 * Built two ways, like the cobol-sutra fuzzer:
 *   - with -DZM_FUZZ_MAIN: a deterministic mutation fuzzer (fixed-seed
 *     xorshift; built-in seeds plus any files given), used by `make test`
 *     and `make fuzz`, and under ASan/UBSan in CI;
 *   - with clang -fsanitize=fuzzer: libFuzzer drives the same function.
 *
 *   fuzz_parse ITERATIONS [seed ...]      ZM_FUZZ_SEED=n: another stream */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zm_internal.h"
#include "zmyra.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static unsigned long long g_ok, g_refused, g_values, g_valid;

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

static int32_t parse(int which, const char *t, size_t n, zm_schema **s, zm_diag *d) {
    if (which == 0) return zm_myra_parse(t, n, s, d);
    if (which == 1) return zm_gmap_parse(t, n, s, d);
    return zm_schema_parse(t, n, s, d);
}

static char *text_of(const zm_schema *s, int gm) {
    size_t need = 0;
    char *b;
    int32_t st = gm ? zm_schema_gmap(s, NULL, 0, &need) : zm_schema_myra(s, NULL, 0, &need);
    if (st != ZXV_E_BUFFER) return NULL;
    b = (char *) malloc(need + 1u);
    if (!b) return NULL;
    st = gm ? zm_schema_gmap(s, b, need + 1u, &need) : zm_schema_myra(s, b, need + 1u, &need);
    if (st != ZXV_OK) {
        free(b);
        return NULL;
    }
    return b;
}

/* A reference that matches node `node`: a small valid value per key. */
static int ref_for(const zm_schema *s, uint32_t node, char *b, size_t cap) {
    char pat[8192];
    size_t n = 0, k = 0;
    zm_node_info info;
    zm_sb sb;
    if (zm_node_info_at(s, node, &info) != ZXV_OK) return 0;
    if (zm_node_pattern(s, node, pat, sizeof pat, &n) != ZXV_OK) return 0;
    zm_sb_init(&sb, cap - 1u);
    zm_sb_chr(&sb, '^');
    zm_sb_str(&sb, s->gname);
    if (s->nodes[node].nsubs) {
        zm_sb_chr(&sb, '(');
        for (uint32_t i = 0; i < s->nodes[node].nsubs; i++) {
            const zm_sub *x = &s->nodes[node].subs[i];
            if (i) zm_sb_chr(&sb, ',');
            if (x->kind == ZM_SUB_LIT) {
                zm_sb_sub(&sb, x->lit, x->litlen, x->isnum);
            } else {
                const zm_field *f = &s->nodes[node].f[x->field].f;
                if (f->type == ZM_T_TEXT) zm_sb_str(&sb, "\"k\"");
                else if (f->type == ZM_T_NUM && f->len == f->scale) zm_sb_str(&sb, ".5");
                else zm_sb_str(&sb, "1");
            }
            k++;
        }
        zm_sb_chr(&sb, ')');
    }
    if (sb.err != ZXV_OK) {
        zm_sb_free(&sb);
        return 0;
    }
    memcpy(b, sb.b, sb.n + 1u);
    zm_sb_free(&sb);
    (void) k;
    return 1;
}

static void check_values(const zm_schema *s, const uint8_t *data, size_t size) {
    uint32_t nn = 0;
    char ref[8192];
    zm_node_count(s, &nn);
    for (uint32_t node = 0; node < nn && node < 8u; node++) {
        zm_record *r = NULL;
        uint32_t got = 99;
        if (!ref_for(s, node, ref, sizeof ref)) continue;
        if (zm_node_match(s, ref, strlen(ref), &got) != ZXV_OK || got != node)
            die("a reference built from a node's pattern does not match it", data, size);
        /* values: the whole input, and each of its lines */
        for (size_t b = 0, e; b <= size; b = e + 1u) {
            const char *v = (const char *) data + b;
            size_t vn;
            int32_t st;
            for (e = b; e < size && data[e] != '\n'; e++) {
            }
            vn = e - b;
            st = zm_node_decode(s, ref, strlen(ref), v, vn, &r);
            g_values++;
            if (st == ZXV_OK) {
                char *c1 = (char *) malloc(ZM_MAX_VALUE + 1u), *c2 = (char *) malloc(ZM_MAX_VALUE + 1u);
                size_t n1 = 0, n2 = 0;
                zm_record *r2 = NULL;
                zm_node_info info;
                g_valid++;
                if (!c1 || !c2) die("out of memory", data, size);
                if (zm_record_encode(r, c1, ZM_MAX_VALUE + 1u, &n1) != ZXV_OK)
                    die("a decoded value does not encode", data, size);
                if (zm_node_decode(s, ref, strlen(ref), c1, n1, &r2) != ZXV_OK ||
                    zm_record_encode(r2, c2, ZM_MAX_VALUE + 1u, &n2) != ZXV_OK || n1 != n2 ||
                    memcmp(c1, c2, n1) != 0)
                    die("the canonical value is not stable", data, size);
                zm_node_info_at(s, node, &info);
                for (uint32_t f = 0; f < info.keys + info.pieces; f++) {
                    zm_field fd;
                    int32_t isnull = 1;
                    char t[64];
                    size_t tn = 0;
                    zm_field_at(s, node, f, &fd);
                    zm_is_null(r2, fd.name, &isnull);
                    if (fd.type == ZM_T_TEXT || isnull) continue;
                    if (zm_get_mtext(r2, fd.name, t, sizeof t, &tn) != ZXV_OK || !zm_is_canonic(t, tn))
                        die("a numeric piece is not M canonical", data, size);
                }
                zm_record_destroy(r2);
                free(c1);
                free(c2);
            } else if (st != ZM_E_DATA && st != ZM_E_FIT && st != ZXV_E_TOO_LONG) {
                die("decode returned an unexpected status", data, size);
            }
            zm_record_destroy(r);
            r = NULL;
            if (e >= size) break;
        }
    }
}

static void check_collation(const uint8_t *data, size_t size) {
    const char *t = (const char *) data;
    size_t a = size / 3u, b = 2u * size / 3u;
    const char *x = t, *y = t + a, *z = t + b;
    size_t nx = a, ny = b - a, nz = size - b;
    int32_t xy, yx, yz, xz, xx;
    zm_ref r;
    zm_collate(x, nx, y, ny, &xy);
    zm_collate(y, ny, x, nx, &yx);
    zm_collate(y, ny, z, nz, &yz);
    zm_collate(x, nx, z, nz, &xz);
    zm_collate(x, nx, x, nx, &xx);
    if (xy != -yx || xx != 0) die("collation is not antisymmetric", data, size);
    if ((xy <= 0 && yz <= 0 && xz > 0) || (xy >= 0 && yz >= 0 && xz < 0))
        die("collation is not transitive", data, size);
    if (zm_is_canonic(t, size)) {
        zm_cnum c;
        if (!zm_cnum_split(t, size, &c)) die("a canonical number does not split", data, size);
    }
    if (size <= ZM_MAX_REF && zm_ref_parse(t, size, &r) == ZXV_OK) {
        int32_t c = 99;
        if (zm_ref_collate(t, size, t, size, &c) != ZXV_OK || c != 0)
            die("a reference does not collate equal to itself", data, size);
    }
}

/* An input is schema text, optionally followed by a line "--" and value
 * strings, one per line. Without the separator the whole input is used
 * for both. */
static size_t split_at(const uint8_t *d, size_t n, size_t *vals) {
    for (size_t i = 0; i + 4u <= n; i++)
        if (d[i] == '\n' && d[i + 1u] == '-' && d[i + 2u] == '-' && d[i + 3u] == '\n') {
            *vals = i + 4u;
            return i + 1u;
        }
    *vals = 0;
    return n;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t full) {
    const char *t = (const char *) data;
    size_t vals = 0, size;
    if (full > 65536u) return 0;
    size = split_at(data, full, &vals);
    check_collation(data, full);
    for (int which = 0; which < 3; which++) {
        zm_schema *s = NULL, *s2 = NULL;
        zm_diag d1, d2;
        int32_t st = parse(which, t, size, &s, &d1);
        int32_t st2 = parse(which, t, size, &s2, &d2);
        if (st != st2) die("the status differs between two runs", data, size);
        if (st != ZXV_OK) {
            uint32_t line = 1, col;
            uint64_t ls = 0;
            char head[64];
            if (st != ZM_E_SYNTAX && st != ZM_E_UNSUPPORTED && st != ZXV_E_UTF8 &&
                st != ZXV_E_CAPACITY)
                die("unexpected refusal status", data, size);
            if (memcmp(&d1, &d2, sizeof d1) != 0) die("the diagnostic differs", data, size);
            if (d1.offset > size) die("the refusal offset is outside the input", data, size);
            for (uint64_t i = 0; i < d1.offset; i++)
                if (t[i] == '\n') {
                    line++;
                    ls = i + 1u;
                }
            col = (uint32_t) (d1.offset - ls + 1u);
            if (line != d1.line || col != d1.col) die("line/column do not match the offset", data, size);
            snprintf(head, sizeof head, "line %u, column %u: ", (unsigned) line, (unsigned) col);
            if (strncmp(d1.msg, head, strlen(head)) != 0) die("message prefix", data, size);
            g_refused++;
            continue;
        }
        g_ok++;
        {
            char *c1 = text_of(s, 0), *g = text_of(s, 1);
            zm_schema *p = NULL, *q = NULL;
            uint8_t da[32], db[32], dc[32];
            char *c2;
            if (!c1 || !g) die("printing an accepted schema failed", data, size);
            if (zm_myra_parse(c1, strlen(c1), &p, NULL) != ZXV_OK)
                die("the canonical MYRA does not parse", data, size);
            c2 = text_of(p, 0);
            if (!c2 || strcmp(c1, c2) != 0) die("the canonical MYRA is not a fixed point", data, size);
            if (zm_gmap_parse(g, strlen(g), &q, NULL) != ZXV_OK)
                die("the printed global map does not parse", data, size);
            zm_schema_digest(s, da);
            zm_schema_digest(p, db);
            zm_schema_digest(q, dc);
            if (memcmp(da, db, 32) != 0 || memcmp(da, dc, 32) != 0)
                die("the digests differ after printing", data, size);
            check_values(s, data + vals, full - vals);
            free(c1);
            free(c2);
            free(g);
            zm_schema_destroy(p);
            zm_schema_destroy(q);
        }
        zm_schema_destroy(s);
        zm_schema_destroy(s2);
    }
    return 0;
}

#ifdef ZM_FUZZ_MAIN

static unsigned long long g_x = 0x2545F4914F6CDD1Dull;
static unsigned long long rng(void) {
    g_x ^= g_x << 13;
    g_x ^= g_x >> 7;
    g_x ^= g_x << 17;
    return g_x;
}

static const char *k_seeds[] = {
    ("^ZXPAY(batch:i9,seq:i9)=ref:t16!^amount:n15.2!^ccy:t3!^from:i10!^to:i10!^value-date:d\n"
     "^ZXPAY(batch:i9)=opened:d!^count:i6\n^ZXPAY(\"B\",ref:t16,batch:i9,seq:i9)=\n"
     "--\nR^12.5^NGN^1^2^67854\n67854^3\nR^0.5^NGN^1^2\n\n"),
    ("GLOBAL ^ZXT\nNODE ^ZXT(ID,\"X\",-2.5)\n  KEY ID TEXT(4)\n  PIECE 2 A NUMBER(5,2) REQUIRED\n"
     "  PIECE 1 B DATE\n--\n67854^-.25\n^1\n0^999.99\n"),
    "^%Z=a:t3^^b:n3.3\n--\n.125\nabc^^.5\nab^c^-.001\n",
    "^ZXT(k:n4.4,\"say \"\"x\"\"\",1)=v:i18!\n--\n999999999999999999\n-0\n-999999999999999999\n",
    "^ZXT(1",
    "GLOBAL ^ZXT\nNODE ^ZXT(A)\nKEY A INTEGER(1)\nNODE ^ZXT(B)\nKEY B DATE\n",
};

static const char *k_tokens[] = {"--\n", "^",  "(",   ")",    ",",   "=",     "!",     ":",     "\"",
                                 ";",  "\n",  " ",    "t16", "i9",    "n15.2", "d",     ".5",
                                 "-",  "0",   "1E3",  "01",  "^ZXT",  "GLOBAL ", "NODE ", "KEY ",
                                 "PIECE ", "TEXT(", "INTEGER(", "NUMBER(", "DATE", "REQUIRED",
                                 "\"\"", "%",  "\xc3\xa9", "\xff", "\t", "\r\n", "99999999999999999999"};

static void mutate(uint8_t *b, size_t *n, size_t cap) {
    int ops = 1 + (int) (rng() % 4u);
    while (ops--) {
        size_t at = *n ? (size_t) (rng() % (*n + 1u)) : 0;
        switch (rng() % 5u) {
        case 0: /* flip a byte */
            if (*n) b[rng() % *n] = (uint8_t) rng();
            break;
        case 1: /* delete a run */
            if (*n) {
                size_t len = 1u + (size_t) (rng() % 8u);
                if (at + len > *n) len = *n - at;
                memmove(b + at, b + at + len, *n - at - len);
                *n -= len;
            }
            break;
        case 2: { /* insert a token */
            const char *tk = k_tokens[rng() % (sizeof k_tokens / sizeof *k_tokens)];
            size_t len = strlen(tk);
            if (*n + len < cap) {
                memmove(b + at + len, b + at, *n - at);
                memcpy(b + at, tk, len);
                *n += len;
            }
            break;
        }
        case 3: /* duplicate a run */
            if (*n && *n * 2u < cap) {
                size_t len = 1u + (size_t) (rng() % (*n));
                if (at + len > *n) len = *n - at;
                memmove(b + at + len, b + at, *n - at);
                *n += len;
            }
            break;
        default: /* truncate */
            *n = at;
            break;
        }
    }
}

int main(int argc, char **argv) {
    long iters = argc > 1 ? strtol(argv[1], NULL, 10) : 1000;
    char **seeds = NULL;
    size_t *seed_len = NULL, nseeds = 0;
    const char *env = getenv("ZM_FUZZ_SEED");
    uint8_t *buf = (uint8_t *) malloc(65536u);
    if (env) g_x ^= (unsigned long long) strtoull(env, NULL, 10) * 0x9E3779B97F4A7C15ull;
    seeds = (char **) calloc((size_t) argc + sizeof k_seeds / sizeof *k_seeds, sizeof *seeds);
    seed_len = (size_t *) calloc((size_t) argc + sizeof k_seeds / sizeof *k_seeds, sizeof *seed_len);
    if (!buf || !seeds || !seed_len) return 2;
    for (size_t i = 0; i < sizeof k_seeds / sizeof *k_seeds; i++) {
        seeds[nseeds] = (char *) k_seeds[i];
        seed_len[nseeds++] = strlen(k_seeds[i]);
    }
    for (int i = 2; i < argc; i++) {
        FILE *f = fopen(argv[i], "rb");
        char *b;
        size_t n;
        if (!f) continue;
        b = (char *) malloc(65536u);
        n = b ? fread(b, 1, 65535u, f) : 0;
        fclose(f);
        if (!b) continue;
        seeds[nseeds] = b;
        seed_len[nseeds++] = n;
    }
    for (size_t i = 0; i < nseeds; i++) LLVMFuzzerTestOneInput((const uint8_t *) seeds[i], seed_len[i]);
    for (long it = 0; it < iters; it++) {
        size_t s = (size_t) (rng() % nseeds), n = seed_len[s];
        memcpy(buf, seeds[s], n);
        mutate(buf, &n, 65536u);
        LLVMFuzzerTestOneInput(buf, n);
    }
    printf("fuzz_parse: %ld inputs (+%zu seeds): %llu parses accepted, %llu refused; %llu values "
           "decoded, %llu valid\n",
           iters, nseeds, g_ok, g_refused, g_values, g_valid);
    for (size_t i = sizeof k_seeds / sizeof *k_seeds; i < nseeds; i++) free(seeds[i]);
    free(seeds);
    free(seed_len);
    free(buf);
    return 0;
}

#endif
