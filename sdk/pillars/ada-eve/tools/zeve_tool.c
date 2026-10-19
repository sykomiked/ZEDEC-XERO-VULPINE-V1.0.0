/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zeve_tool.c — the command-line tool.
 *
 *   zeve check  FILE...           parse; print the digest and type count
 *   zeve canon  FILE              the canonical Eve text
 *   zeve ada    FILE              the same package as an Ada spec
 *   zeve name   FILE              the package name (lower case)
 *   zeve types  FILE              every type: kind, range, encoded size
 *   zeve same   FILE FILE         do two files declare the same types?
 *   zeve decode FILE TYPE HEX     every scalar of one encoded value
 *   zeve golden FILE...           run golden scripts (test/golden, *.golden)
 *   zeve probe  FILE              an Ada main that prints what GNAT knows
 *                                 about every type (make golden-gnat)
 *   zeve probe-expect FILE        what that program must print, from
 *                                 libzeve's reading of the same file
 *
 * FILE is Eve when it ends in .eve, otherwise an Ada specification. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zeve.h"

static char *slurp(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    char *b = NULL;
    size_t n = 0, cap = 0;
    if (!f) return NULL;
    for (;;) {
        size_t r;
        if (n + 4096u + 1u > cap) {
            char *nb;
            cap = cap ? cap * 2u : 8192u;
            nb = (char *) realloc(b, cap);
            if (!nb) {
                free(b);
                fclose(f);
                return NULL;
            }
            b = nb;
        }
        r = fread(b + n, 1, cap - n - 1u, f);
        n += r;
        if (r == 0) break;
    }
    fclose(f);
    b[n] = '\0';
    *out_len = n;
    return b;
}

static int ends_with(const char *s, const char *suf) {
    size_t a = strlen(s), b = strlen(suf);
    return a >= b && strcmp(s + a - b, suf) == 0;
}

/* Parse FILE: on refusal print a compiler-style error (or, quiet, fill
 * *st and *d) and return NULL. */
static ze_pkg *load_ex(const char *path, int quiet, int32_t *out_st, ze_diag *out_d) {
    size_t len = 0;
    char *text = slurp(path, &len);
    ze_pkg *p = NULL;
    ze_diag d;
    int32_t st;
    if (!text) {
        fprintf(stderr, "%s: cannot read\n", path);
        if (out_st) *out_st = ZXV_E_IO;
        return NULL;
    }
    st = ends_with(path, ".eve") ? ze_eve_parse(text, len, &p, &d) : ze_ada_parse(text, len, &p, &d);
    if (out_st) *out_st = st;
    if (out_d) *out_d = d;
    if (st != ZXV_OK && !quiet) {
        const char *msg = strstr(d.msg, ": ");
        size_t ls = (size_t) d.offset, le;
        if (ls > len) ls = len;
        while (ls > 0 && text[ls - 1u] != '\n') ls--;
        le = ls;
        while (le < len && text[le] != '\n' && text[le] != '\r') le++;
        fprintf(stderr, "%s:%u:%u: %s [%s]\n", path, (unsigned) d.line, (unsigned) d.col, msg ? msg + 2 : d.msg,
                ze_status_name(st));
        fprintf(stderr, "  %.*s\n  %*s^\n", (int) (le - ls), text + ls, (int) (d.col - 1u), "");
    }
    free(text);
    return p;
}

static ze_pkg *load(const char *path) { return load_ex(path, 0, NULL, NULL); }

static void hex(const uint8_t *b, size_t n, char *out) {
    static const char k[] = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) {
        out[2 * i] = k[b[i] >> 4];
        out[2 * i + 1] = k[b[i] & 15u];
    }
    out[2 * n] = '\0';
}

static int unhex(const char *s, uint8_t *out, size_t cap, size_t *out_n) {
    size_t n = strlen(s);
    if (n % 2u || n / 2u > cap) return -1;
    for (size_t i = 0; i < n / 2u; i++) {
        int v = 0;
        for (int k = 0; k < 2; k++) {
            char c = s[2 * i + (size_t) k];
            int d = (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10
                    : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
            if (d < 0) return -1;
            v = v * 16 + d;
        }
        out[i] = (uint8_t) v;
    }
    *out_n = n / 2u;
    return 0;
}

/* A text from a two-call libzeve function, malloc'd. */
static char *text_of(const ze_pkg *p, int ada) {
    size_t need = 0;
    char *b;
    int32_t st = ada ? ze_pkg_ada(p, NULL, 0, &need) : ze_pkg_eve(p, NULL, 0, &need);
    if (st != ZXV_E_BUFFER) return NULL;
    b = (char *) malloc(need + 1u);
    if (!b) return NULL;
    st = ada ? ze_pkg_ada(p, b, need + 1u, &need) : ze_pkg_eve(p, b, need + 1u, &need);
    if (st != ZXV_OK) {
        free(b);
        return NULL;
    }
    return b;
}

static const char *kind_name(int32_t k) {
    switch (k) {
    case ZE_K_INT: return "int";
    case ZE_K_DEC: return "dec";
    case ZE_K_ENUM: return "enum";
    case ZE_K_TEXT: return "str";
    case ZE_K_ARRAY: return "array";
    case ZE_K_RECORD: return "record";
    default: return "?";
    }
}

static const char *last_err(void) {
    static char b[600];
    size_t n = 0;
    if (ze_last_error(b, sizeof b, &n) != ZXV_OK) b[0] = '\0';
    return b;
}

/* ---- check, canon, ada, types, same ---- */

static int cmd_check(int argc, char **argv) {
    int bad = 0;
    for (int i = 0; i < argc; i++) {
        ze_pkg *p = load(argv[i]);
        uint8_t d[32];
        char h[65];
        uint32_t n = 0;
        if (!p) {
            bad = 1;
            continue;
        }
        ze_pkg_digest(p, d);
        hex(d, 32, h);
        ze_type_count(p, &n);
        printf("ok %s: %u types, sha3-256 %s\n", argv[i], (unsigned) n - 1u, h);
        ze_pkg_destroy(p);
    }
    return bad;
}

static int cmd_print(const char *path, int ada) {
    ze_pkg *p = load(path);
    char *t;
    if (!p) return 1;
    t = text_of(p, ada);
    ze_pkg_destroy(p);
    if (!t) return 1;
    fputs(t, stdout);
    free(t);
    return 0;
}

static int cmd_name(const char *path) {
    ze_pkg *p = load(path);
    char b[80];
    size_t n = 0;
    if (!p) return 1;
    ze_pkg_name(p, b, sizeof b, &n);
    printf("%s\n", b);
    ze_pkg_destroy(p);
    return 0;
}

static int cmd_types(const char *path) {
    ze_pkg *p = load(path);
    uint32_t n = 0;
    if (!p) return 1;
    ze_type_count(p, &n);
    for (uint32_t i = 1; i < n; i++) {
        ze_type t;
        ze_type_at(p, i, &t);
        printf("%4u %-20s %-6s%s", (unsigned) i, t.name[0] ? t.name : "(anonymous)", kind_name(t.kind),
               t.is_subtype ? " subtype" : "");
        if (t.kind == ZE_K_INT || t.kind == ZE_K_DEC || t.kind == ZE_K_ENUM)
            printf(" %lld .. %lld", (long long) t.lo, (long long) t.hi);
        if (t.kind == ZE_K_DEC) printf(" (digits %u, scale %u)", (unsigned) t.digits, (unsigned) t.scale);
        printf("  %llu bytes\n", (unsigned long long) t.size);
        if (t.kind == ZE_K_RECORD)
            for (uint32_t c = 0; c < t.count; c++) {
                ze_component cp;
                ze_type ct;
                ze_component_at(p, i, c, &cp);
                ze_type_at(p, cp.type, &ct);
                printf("       %-20s at %llu, %llu bytes\n", cp.name, (unsigned long long) cp.offset,
                       (unsigned long long) ct.size);
            }
    }
    ze_pkg_destroy(p);
    return 0;
}

static int cmd_same(const char *a, const char *b) {
    ze_pkg *pa = load(a), *pb = load(b);
    int32_t same = 0;
    uint8_t d[32];
    char h[65];
    int rc = 1;
    if (pa && pb && ze_pkg_same(pa, pb, &same) == ZXV_OK) {
        ze_pkg_digest(pa, d);
        hex(d, 32, h);
        if (same) {
            printf("same types: %s and %s, sha3-256 %s\n", a, b, h);
            rc = 0;
        } else {
            printf("DIFFERENT: %s and %s\n", a, b);
        }
    }
    ze_pkg_destroy(pa);
    ze_pkg_destroy(pb);
    return rc;
}

/* ---- decode: every scalar of a value ---- */

static void dump(const ze_pkg *p, uint32_t root, const uint8_t *v, size_t n, uint32_t t, char *path, size_t pl) {
    ze_type ty;
    ze_type_at(p, t, &ty);
    if (ty.kind == ZE_K_RECORD) {
        for (uint32_t c = 0; c < ty.count; c++) {
            ze_component cp;
            size_t k = pl, nl;
            ze_component_at(p, t, c, &cp);
            nl = strlen(cp.name);
            if (pl + nl + 2u >= 512u) continue;
            if (pl) path[k++] = '.';
            memcpy(path + k, cp.name, nl + 1u);
            dump(p, root, v, n, cp.type, path, k + nl);
            path[pl] = '\0';
        }
    } else if (ty.kind == ZE_K_ARRAY) {
        ze_dim dm[4];
        int64_t sub[4];
        for (uint32_t d = 0; d < ty.dims; d++) {
            ze_array_dim(p, t, d, &dm[d]);
            sub[d] = dm[d].lo;
        }
        for (uint32_t e = 0; e < ty.count; e++) {
            char s[256];
            size_t sl = 0;
            s[sl++] = '(';
            for (uint32_t d = 0; d < ty.dims; d++) {
                char one[80];
                size_t ol = 0;
                if (dm[d].index != UINT32_MAX) {
                    ze_type it;
                    ze_type_at(p, dm[d].index, &it);
                    if (it.kind == ZE_K_ENUM) ze_enum_literal(p, dm[d].index, (uint32_t) sub[d], one, sizeof one, &ol);
                }
                if (!ol) ol = (size_t) snprintf(one, sizeof one, "%lld", (long long) sub[d]);
                if (sl + ol + 3u >= sizeof s) break;
                if (d) s[sl++] = ',';
                memcpy(s + sl, one, ol);
                sl += ol;
            }
            s[sl++] = ')';
            s[sl] = '\0';
            if (pl + sl + 1u < 512u) {
                memcpy(path + pl, s, sl + 1u);
                dump(p, root, v, n, ty.elem, path, pl + sl);
                path[pl] = '\0';
            }
            for (uint32_t d = ty.dims; d > 0; d--) {
                if (++sub[d - 1u] <= dm[d - 1u].hi) break;
                sub[d - 1u] = dm[d - 1u].lo;
            }
        }
    } else {
        char b[1024];
        size_t bl = 0;
        int32_t st = ze_get_value(p, root, v, n, path, b, sizeof b, &bl);
        if (st == ZXV_OK) printf("%s = %s\n", path[0] ? path : "(value)", b);
        else printf("%s: %s %s\n", path[0] ? path : "(value)", ze_status_name(st), last_err());
    }
}

static int cmd_decode(const char *file, const char *type, const char *hx) {
    ze_pkg *p = load(file);
    uint32_t t;
    ze_type ty;
    uint8_t *v;
    size_t n = 0;
    char path[512] = "";
    int rc = 1;
    if (!p) return 1;
    if (ze_type_find(p, type, &t) != ZXV_OK) {
        fprintf(stderr, "no type %s\n", type);
        ze_pkg_destroy(p);
        return 1;
    }
    ze_type_at(p, t, &ty);
    v = (uint8_t *) malloc(strlen(hx) / 2u + 1u);
    if (v && unhex(hx, v, strlen(hx) / 2u + 1u, &n) == 0 && n == ty.size) {
        dump(p, t, v, n, t, path, 0);
        rc = ze_value_check(p, t, v, n) == ZXV_OK ? 0 : 1;
        if (rc) printf("check: %s\n", last_err());
    } else {
        fprintf(stderr, "the value must be %llu bytes of hex\n", (unsigned long long) ty.size);
    }
    free(v);
    ze_pkg_destroy(p);
    return rc;
}

/* ---- golden scripts ---- */

static int g_checks, g_fail;

static void gfail(const char *file, int line, const char *fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 3, 4)))
#endif
    ;
static void gfail(const char *file, int line, const char *fmt, ...) {
    va_list ap;
    g_fail++;
    fprintf(stderr, "FAIL %s:%d: ", file, line);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, " [%s]\n", last_err());
}

/* Split a line into words; "..." is one word without its quotes. */
static int words(char *s, char **w, int max) {
    int n = 0;
    while (*s && n < max) {
        while (*s == ' ' || *s == '\t') s++;
        if (!*s || *s == '#') break;
        if (*s == '"') {
            w[n++] = ++s;
            while (*s && *s != '"') s++;
        } else {
            w[n++] = s;
            while (*s && *s != ' ' && *s != '\t') s++;
        }
        if (*s) *s++ = '\0';
    }
    return n;
}

static int32_t status_of(const char *name) {
    static const int32_t codes[] = {ZE_E_SYNTAX, ZE_E_UNSUPPORTED, ZE_E_NAME, ZE_E_CONSTRAINT, ZE_E_DATA,
                                    ZE_E_FIT,    ZE_E_LIMIT,       ZXV_OK,    ZXV_E_ARG,       ZXV_E_FORMAT,
                                    ZXV_E_RANGE, ZXV_E_NOT_FOUND};
    for (size_t i = 0; i < sizeof codes / sizeof codes[0]; i++)
        if (strcmp(ze_status_name(codes[i]), name) == 0) return codes[i];
    return 1;
}

static int run_golden(const char *file) {
    size_t len = 0;
    char *text = slurp(file, &len), *line, *next;
    char dir[512];
    const char *slash = strrchr(file, '/');
    ze_pkg *p = NULL;
    uint32_t root = 0;
    ze_type rt;
    uint8_t *val = NULL;
    int ln = 0;
    if (!text) {
        gfail(file, 0, "cannot read");
        return 1;
    }
    memset(&rt, 0, sizeof rt);
    if (slash && (size_t) (slash - file) + 2u < sizeof dir) {
        memcpy(dir, file, (size_t) (slash - file) + 1u);
        dir[slash - file + 1] = '\0';
    } else {
        dir[0] = '\0';
    }
    for (line = text; line && *line; line = next) {
        char *w[8], path[1024];
        int n;
        next = strchr(line, '\n');
        if (next) *next++ = '\0';
        ln++;
        n = words(line, w, 8);
        if (n == 0) continue;
        g_checks++;
#define PATHOF(x) (snprintf(path, sizeof path, "%s%s", dir, (x)) < (int) sizeof path ? path : "")
        if (!strcmp(w[0], "layout") && n == 2) {
            ze_pkg_destroy(p);
            free(val);
            val = NULL;
            p = load(PATHOF(w[1]));
            if (!p) {
                gfail(file, ln, "%s does not parse", w[1]);
                break;
            }
        } else if (!p && strcmp(w[0], "refuse-layout") != 0) {
            gfail(file, ln, "no layout loaded");
            break;
        } else if ((!strcmp(w[0], "same") || !strcmp(w[0], "differ")) && n == 2) {
            ze_pkg *q = load(PATHOF(w[1]));
            int32_t same = -1;
            if (!q || ze_pkg_same(p, q, &same) != ZXV_OK || same != !strcmp(w[0], "same"))
                gfail(file, ln, "%s %s: digests %s", w[0], w[1], same == 1 ? "equal" : "differ");
            ze_pkg_destroy(q);
        } else if (!strcmp(w[0], "refuse-layout") && n == 4) {
            int32_t st = 0;
            ze_diag d;
            char where[32];
            ze_pkg *q = load_ex(PATHOF(w[1]), 1, &st, &d);
            snprintf(where, sizeof where, "%u:%u", (unsigned) d.line, (unsigned) d.col);
            if (q || st != status_of(w[2]) || strcmp(where, w[3]) != 0)
                gfail(file, ln, "%s: got %s at %s, want %s at %s", w[1], ze_status_name(st), q ? "-" : where, w[2],
                      w[3]);
            ze_pkg_destroy(q);
        } else if (!strcmp(w[0], "digest") && n == 2) {
            uint8_t d[32];
            char h[65];
            ze_pkg_digest(p, d);
            hex(d, 32, h);
            if (strcmp(h, w[1]) != 0) gfail(file, ln, "digest %s, want %s", h, w[1]);
        } else if (!strcmp(w[0], "type") && n == 2) {
            free(val);
            val = NULL;
            if (ze_type_find(p, w[1], &root) != ZXV_OK) {
                gfail(file, ln, "no type %s", w[1]);
                break;
            }
            ze_type_at(p, root, &rt);
            val = (uint8_t *) calloc(1, (size_t) rt.size);
            if (!val || ze_value_init(p, root, val, (size_t) rt.size) != ZXV_OK) {
                gfail(file, ln, "cannot initialise %s", w[1]);
                break;
            }
        } else if (!val) {
            gfail(file, ln, "no type selected");
            break;
        } else if (!strcmp(w[0], "size") && n == 2) {
            if (rt.size != strtoull(w[1], NULL, 10))
                gfail(file, ln, "size %llu, want %s", (unsigned long long) rt.size, w[1]);
        } else if (!strcmp(w[0], "init") && n == 1) {
            if (ze_value_init(p, root, val, (size_t) rt.size) != ZXV_OK) gfail(file, ln, "init");
        } else if (!strcmp(w[0], "put") && n == 3) {
            int32_t st = ze_put_value(p, root, val, (size_t) rt.size, w[1], w[2], strlen(w[2]));
            if (st != ZXV_OK) gfail(file, ln, "put %s %s: %s", w[1], w[2], ze_status_name(st));
        } else if (!strcmp(w[0], "put-fail") && n == 4) {
            uint8_t *before = (uint8_t *) malloc((size_t) rt.size + 1u);
            int32_t st;
            if (before) memcpy(before, val, (size_t) rt.size);
            st = ze_put_value(p, root, val, (size_t) rt.size, w[1], w[2], strlen(w[2]));
            if (st != status_of(w[3])) gfail(file, ln, "put %s %s: %s, want %s", w[1], w[2], ze_status_name(st), w[3]);
            else if (before && memcmp(before, val, (size_t) rt.size) != 0)
                gfail(file, ln, "a refused put changed the value");
            free(before);
        } else if (!strcmp(w[0], "get") && n == 3) {
            char b[2048];
            size_t bl = 0;
            int32_t st = ze_get_value(p, root, val, (size_t) rt.size, w[1], b, sizeof b, &bl);
            if (st != ZXV_OK || strcmp(b, w[2]) != 0)
                gfail(file, ln, "get %s: %s '%s', want '%s'", w[1], ze_status_name(st), st == ZXV_OK ? b : "", w[2]);
        } else if (!strcmp(w[0], "get-fail") && n == 3) {
            char b[2048];
            size_t bl = 0;
            int32_t st = ze_get_value(p, root, val, (size_t) rt.size, w[1], b, sizeof b, &bl);
            if (st != status_of(w[2])) gfail(file, ln, "get %s: %s, want %s", w[1], ze_status_name(st), w[2]);
        } else if (!strcmp(w[0], "rat") && n == 3) {
            int64_t nu = 0, de = 0;
            char b[64];
            int32_t st = ze_get_rat(p, root, val, (size_t) rt.size, w[1], &nu, &de);
            snprintf(b, sizeof b, "%lld/%lld", (long long) nu, (long long) de);
            if (st != ZXV_OK || strcmp(b, w[2]) != 0) gfail(file, ln, "rat %s: %s %s, want %s", w[1], ze_status_name(st), b, w[2]);
        } else if (!strcmp(w[0], "bytes") && n == 2) {
            char *h = (char *) malloc(2u * (size_t) rt.size + 1u);
            if (h) {
                hex(val, (size_t) rt.size, h);
                if (strcmp(h, w[1]) != 0) gfail(file, ln, "bytes %s, want %s", h, w[1]);
            }
            free(h);
        } else if (!strcmp(w[0], "load") && n == 2) {
            size_t got = 0;
            if (unhex(w[1], val, (size_t) rt.size, &got) != 0 || got != rt.size) gfail(file, ln, "load: bad hex");
        } else if (!strcmp(w[0], "check") && n == 1) {
            if (ze_value_check(p, root, val, (size_t) rt.size) != ZXV_OK) gfail(file, ln, "check");
        } else if (!strcmp(w[0], "check-fail") && n == 2) {
            int32_t st = ze_value_check(p, root, val, (size_t) rt.size);
            if (st != status_of(w[1])) gfail(file, ln, "check: %s, want %s", ze_status_name(st), w[1]);
        } else {
            gfail(file, ln, "unknown directive '%s' (%d words)", w[0], n);
        }
#undef PATHOF
    }
    free(val);
    ze_pkg_destroy(p);
    free(text);
    return 0;
}

static int cmd_golden(int argc, char **argv) {
    for (int i = 0; i < argc; i++) run_golden(argv[i]);
    printf("golden: %d files, %d checks, %d failed\n", argc, g_checks, g_fail);
    return g_fail ? 1 : 0;
}

/* ---- probe: what GNAT says about every type ---- */

static void ada_name(char *dst, const char *lower) {
    int up = 1;
    size_t i = 0;
    for (; lower[i]; i++) {
        char c = lower[i];
        dst[i] = (up && c >= 'a' && c <= 'z') ? (char) (c - 32) : c;
        up = (c == '_');
    }
    dst[i] = '\0';
}

static void upper(char *s) {
    for (; *s; s++)
        if (*s >= 'a' && *s <= 'z') *s = (char) (*s - 32);
}

/* An Ada literal of a scalar type's value v (for an aggregate). */
static void ada_lit(const ze_pkg *p, uint32_t t, int64_t v, char *out, size_t cap) {
    ze_type ty;
    ze_type_at(p, t, &ty);
    if (ty.kind == ZE_K_ENUM) {
        char lit[80];
        size_t n = 0;
        ze_enum_literal(p, t, (uint32_t) v, lit, sizeof lit, &n);
        ada_name(out, lit);
    } else if (ty.kind == ZE_K_DEC) {
        int64_t pw = 1;
        uint64_t u = v < 0 ? (uint64_t) 0 - (uint64_t) v : (uint64_t) v;
        for (uint32_t k = 0; k < ty.scale; k++) pw *= 10;
        if (ty.scale == 0) snprintf(out, cap, "%s%llu.0", v < 0 ? "-" : "", (unsigned long long) u);
        else
            snprintf(out, cap, "%s%llu.%0*llu", v < 0 ? "-" : "", (unsigned long long) (u / (uint64_t) pw), (int) ty.scale,
                     (unsigned long long) (u % (uint64_t) pw));
    } else {
        snprintf(out, cap, "%lld", (long long) v);
    }
}

/* GNAT's T'Image of a scalar value. */
static void ada_image(const ze_pkg *p, uint32_t t, int64_t v, char *out, size_t cap) {
    ze_type ty;
    ze_type_at(p, t, &ty);
    if (ty.kind == ZE_K_ENUM) {
        ada_lit(p, t, v, out, cap);
        upper(out);
    } else {
        char b[64];
        ada_lit(p, t, v, b, sizeof b);
        snprintf(out, cap, "%s%s", v < 0 ? "" : " ", b);
    }
}

static int is_scalar(int32_t k) { return k == ZE_K_INT || k == ZE_K_DEC || k == ZE_K_ENUM; }

static int cmd_probe(const char *file, int expect) {
    ze_pkg *p = load(file);
    uint32_t n = 0;
    char pkg[80], pk[80];
    size_t pl = 0;
    if (!p) return 1;
    ze_pkg_name(p, pk, sizeof pk, &pl);
    ada_name(pkg, pk);
    ze_type_count(p, &n);
    if (!expect) {
        printf("--  generated by zeve probe from %s; do not edit\n", file);
        printf("with Ada.Text_IO; use Ada.Text_IO;\nwith %s; use %s;\nprocedure Probe_Main is\n", pkg, pkg);
        printf("   function Sp (S : String) return String is (\" \" & S);\n");
        for (uint32_t i = 1; i < n; i++) {
            ze_type t;
            char an[80];
            ze_type_at(p, i, &t);
            if (!t.name[0] || t.kind != ZE_K_RECORD) continue;
            ada_name(an, t.name);
            for (int k = 0; k < 2; k++) {
                printf("   V_%s_%s : constant %s := (", an, k ? "Hi" : "Lo", an);
                for (uint32_t c = 0; c < t.count; c++) {
                    ze_component cp;
                    ze_type ct;
                    char cn[80], lit[96];
                    ze_component_at(p, i, c, &cp);
                    ze_type_at(p, cp.type, &ct);
                    ada_name(cn, cp.name);
                    printf("%s%s => ", c ? ", " : "", cn);
                    if (is_scalar(ct.kind)) {
                        ada_lit(p, cp.type, k ? ct.hi : ct.lo, lit, sizeof lit);
                        printf("%s", lit);
                    } else if (ct.kind == ZE_K_TEXT) {
                        printf("(others => ' ')");
                    } else {
                        printf("<>");
                    }
                }
                printf(");\n");
            }
        }
        printf("begin\n");
    }
    for (uint32_t i = 1; i < n; i++) {
        ze_type t;
        char an[80], lo[96], hi[96];
        ze_type_at(p, i, &t);
        if (!t.name[0]) continue;
        ada_name(an, t.name);
        if (is_scalar(t.kind)) {
            if (t.kind == ZE_K_DEC) {
                if (expect) printf("%s digits %u scale %u\n", t.name, (unsigned) t.digits, (unsigned) t.scale);
                else
                    printf("   Put_Line (\"%s digits\" & Integer'Image (%s'Digits) & \" scale\" & Integer'Image (%s'Scale));\n",
                           t.name, an, an);
            }
            if (expect) {
                ada_image(p, i, t.lo, lo, sizeof lo);
                ada_image(p, i, t.hi, hi, sizeof hi);
                printf("%s first%s%s last%s%s\n", t.name, lo[0] == ' ' || lo[0] == '-' ? "" : " ", lo,
                       hi[0] == ' ' || hi[0] == '-' ? "" : " ", hi);
            } else {
                printf("   Put_Line (\"%s first\" & %s (%s'Image (%s'First)) & \" last\" & %s (%s'Image (%s'Last)));\n",
                       t.name, t.kind == ZE_K_ENUM ? "Sp" : "", an, an, t.kind == ZE_K_ENUM ? "Sp" : "", an, an);
            }
            if (t.kind == ZE_K_ENUM && !t.is_subtype) {
                if (expect) {
                    printf("%s literals", t.name);
                    for (uint32_t k = 0; k < t.count; k++) {
                        ada_image(p, i, k, lo, sizeof lo);
                        printf(" %s", lo);
                    }
                    printf("\n");
                } else {
                    printf("   Put (\"%s literals\");\n   for X in %s loop\n      Put (\" \" & %s'Image (X));\n"
                           "   end loop;\n   New_Line;\n",
                           t.name, an, an);
                }
            }
        } else if (t.kind == ZE_K_TEXT) {
            if (expect) printf("%s length %u\n", t.name, (unsigned) t.count);
            else
                printf("   Put_Line (\"%s length\" & Integer'Image (%s'Length));\n", t.name, an);
        } else if (t.kind == ZE_K_ARRAY) {
            for (uint32_t d = 0; d < t.dims; d++) {
                ze_dim dm;
                ze_array_dim(p, i, d, &dm);
                if (expect) {
                    if (dm.index == UINT32_MAX) {
                        snprintf(lo, sizeof lo, "%s%lld", dm.lo < 0 ? "" : " ", (long long) dm.lo);
                    } else {
                        ada_image(p, dm.index, dm.lo, lo, sizeof lo);
                    }
                    printf("%s dim %u length %lld first %s\n", t.name, (unsigned) d + 1u,
                           (long long) (dm.hi - dm.lo + 1), lo);
                } else {
                    char ix[80];
                    if (dm.index == UINT32_MAX) snprintf(ix, sizeof ix, "Integer");
                    else {
                        ze_type it;
                        ze_type_at(p, dm.index, &it);
                        if (it.name[0]) ada_name(ix, it.name);
                        else snprintf(ix, sizeof ix, "Integer");
                    }
                    printf("   Put_Line (\"%s dim %u length\" & Integer'Image (%s'Length (%u)) & \" first \" & "
                           "%s'Image (%s'First (%u)));\n",
                           t.name, (unsigned) d + 1u, an, (unsigned) d + 1u, ix, an, (unsigned) d + 1u);
                }
            }
        } else if (t.kind == ZE_K_RECORD) {
            if (expect) printf("%s components %u\n", t.name, (unsigned) t.count);
            else
                printf("   Put_Line (\"%s components %u\" & (if V_%s_Lo = V_%s_Hi then \"\" else \"\"));\n", t.name,
                       (unsigned) t.count, an, an);
        }
    }
    if (!expect) printf("end Probe_Main;\n");
    ze_pkg_destroy(p);
    return 0;
}

static int usage(void) {
    fprintf(stderr,
            "usage: zeve check FILE... | canon FILE | ada FILE | name FILE | types FILE | same A B |\n"
            "            decode FILE TYPE HEX | golden FILE... | probe FILE | probe-expect FILE\n"
            "FILE is Eve when it ends in .eve, otherwise an Ada package specification.\n");
    return 2;
}

int main(int argc, char **argv) {
    if (argc < 3) return usage();
    if (!strcmp(argv[1], "check")) return cmd_check(argc - 2, argv + 2);
    if (!strcmp(argv[1], "canon") && argc == 3) return cmd_print(argv[2], 0);
    if (!strcmp(argv[1], "ada") && argc == 3) return cmd_print(argv[2], 1);
    if (!strcmp(argv[1], "name") && argc == 3) return cmd_name(argv[2]);
    if (!strcmp(argv[1], "types") && argc == 3) return cmd_types(argv[2]);
    if (!strcmp(argv[1], "same") && argc == 4) return cmd_same(argv[2], argv[3]);
    if (!strcmp(argv[1], "decode") && argc == 5) return cmd_decode(argv[2], argv[3], argv[4]);
    if (!strcmp(argv[1], "golden")) return cmd_golden(argc - 2, argv + 2);
    if (!strcmp(argv[1], "probe") && argc == 3) return cmd_probe(argv[2], 0);
    if (!strcmp(argv[1], "probe-expect") && argc == 3) return cmd_probe(argv[2], 1);
    return usage();
}
