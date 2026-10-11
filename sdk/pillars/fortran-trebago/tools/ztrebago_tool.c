/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* ztrebago_tool.c — the command-line tool.
 *
 *   ztrebago check  FILE...           parse; print the digest or the refusal
 *   ztrebago canon  FILE              the canonical text
 *   ztrebago params FILE              what a caller binds
 *   ztrebago same   FILE FILE         do two files hold the same program?
 *   ztrebago run    FILE BINDING...   run on exact values, print the outputs
 *   ztrebago golden FILE...           run the "!>" cases in each file
 *
 * A binding is NAME=v1,v2,... for a vector (the extent is the count),
 * NAME(m,n)=... for a matrix in column-major order, NAME=v for a scalar,
 * and NAME(m) for an output whose extent no input sets. Values are exact
 * text: 3, -0.25, 1/3. Options before the command: --work N (work limit).
 *
 * Golden files hold cases in comment lines:
 *   !> case NAME              start a new case (optional for the first)
 *   !> in x = 1, 1/2, 1/3     a binding (in or inout), as above
 *   !> out y(3)               an output's extent, when no input sets it
 *   !> expect s = 11/6        every value of an output, column-major
 *   !> refuse STATUS [L:C]    the run (or the parse) must be refused so
 * Integer code only (R11). */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ztrebago.h"

static uint64_t g_work_limit;

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

static void show_refusal(const char *path, const char *text, size_t len, const tb_diag *d) {
    fprintf(stderr, "%s:%u:%u: %s\n", path, (unsigned) d->line, (unsigned) d->col, d->msg);
    if (d->line) {
        uint32_t line = 1;
        size_t s = 0, e;
        for (size_t i = 0; i < len && line < d->line; i++)
            if (text[i] == '\n') {
                line++;
                s = i + 1u;
            }
        e = s;
        while (e < len && text[e] != '\n') e++;
        fprintf(stderr, "  %.*s\n  %*s^\n", (int) (e - s), text + s, (int) (d->col ? d->col - 1u : 0u), "");
    }
}

static tb_program *load(const char *path, char **out_text, size_t *out_len) {
    size_t len = 0;
    char *text = slurp(path, &len);
    tb_program *p = NULL;
    tb_diag d;
    int32_t st;
    if (!text) {
        fprintf(stderr, "%s: cannot read\n", path);
        return NULL;
    }
    st = tb_parse(text, len, &p, &d);
    if (st != ZXV_OK) {
        show_refusal(path, text, len, &d);
        free(text);
        return NULL;
    }
    if (out_text) {
        *out_text = text;
        *out_len = len;
    } else {
        free(text);
    }
    return p;
}

static void hex16(const uint8_t *d, char *out) {
    static const char hx[] = "0123456789abcdef";
    for (int i = 0; i < 32; i++) {
        out[2 * i] = hx[d[i] >> 4];
        out[2 * i + 1] = hx[d[i] & 15];
    }
    out[64] = '\0';
}

static char *canon(const tb_program *p) {
    size_t need = 0;
    char *b;
    tb_program_text(p, NULL, 0, &need);
    b = (char *) malloc(need + 1u);
    if (b && tb_program_text(p, b, need + 1u, &need) != ZXV_OK) {
        free(b);
        b = NULL;
    }
    return b;
}

/* ===== bindings ===== */

typedef struct {
    char name[TB_MAX_NAME + 1];
    int has_values, has_extent;
    uint32_t rank;
    int64_t extent[2];
    int64_t *num, *den;
    uint64_t count;
} binding;

typedef struct {
    binding b[64];
    uint32_t n;
} bindset;

static void bindset_free(bindset *s) {
    for (uint32_t i = 0; i < s->n; i++) {
        free(s->b[i].num);
        free(s->b[i].den);
    }
    s->n = 0;
}

static char *trim(char *s) {
    char *e;
    while (*s == ' ' || *s == '\t') s++;
    e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) *--e = '\0';
    return s;
}

/* Parse "v1, v2, ..." into a binding's values. */
static int parse_values(binding *b, char *list) {
    uint64_t cap = 16;
    b->num = (int64_t *) malloc(cap * sizeof(int64_t));
    b->den = (int64_t *) malloc(cap * sizeof(int64_t));
    b->count = 0;
    if (!b->num || !b->den) return 0;
    for (char *tok = strtok(list, ","); tok; tok = strtok(NULL, ",")) {
        char *v = trim(tok);
        if (!*v) continue;
        if (b->count == cap) {
            cap *= 2u;
            b->num = (int64_t *) realloc(b->num, cap * sizeof(int64_t));
            b->den = (int64_t *) realloc(b->den, cap * sizeof(int64_t));
            if (!b->num || !b->den) return 0;
        }
        if (zxv_rat_parse(v, &b->num[b->count], &b->den[b->count]) != ZXV_OK) {
            fprintf(stderr, "'%s' is not an exact value (3, -0.25, 1/3)\n", v);
            return 0;
        }
        b->count++;
    }
    b->has_values = 1;
    return 1;
}

/* NAME[(e1[,e2])][=values] */
static int parse_binding(bindset *s, const char *spec) {
    char buf[1 << 16], *eq, *lp, *name;
    binding *b;
    if (s->n >= 64u || strlen(spec) >= sizeof buf) return 0;
    strcpy(buf, spec);
    b = &s->b[s->n];
    memset(b, 0, sizeof *b);
    eq = strchr(buf, '=');
    if (eq) *eq = '\0';
    lp = strchr(buf, '(');
    if (lp) {
        char *rp = strchr(lp, ')');
        long long e0 = 0, e1 = 0;
        int k;
        if (!rp) return 0;
        *rp = '\0';
        k = sscanf(lp + 1, "%lld , %lld", &e0, &e1);
        if (k < 1) return 0;
        b->has_extent = 1;
        b->rank = (uint32_t) k;
        b->extent[0] = e0;
        b->extent[1] = k > 1 ? e1 : 0;
        *lp = '\0';
    }
    name = trim(buf);
    if (!*name || strlen(name) > TB_MAX_NAME) return 0;
    strcpy(b->name, name);
    if (eq && !parse_values(b, eq + 1)) return 0;
    s->n++;
    return 1;
}

static int name_eq(const char *a, const char *b) {
    for (; *a && *b; a++, b++) {
        char x = (*a >= 'A' && *a <= 'Z') ? (char) (*a - 'A' + 'a') : *a;
        char y = (*b >= 'A' && *b <= 'Z') ? (char) (*b - 'A' + 'a') : *b;
        if (x != y) return 0;
    }
    return *a == *b;
}

static binding *find_binding(bindset *s, const char *name) {
    for (uint32_t i = 0; i < s->n; i++)
        if (name_eq(s->b[i].name, name)) return &s->b[i];
    return NULL;
}

/* Turn the bindings into tb_arrays, sizing outputs from the dimensions
 * the inputs set. Returns the count, or -1 (message printed). */
static int make_arrays(const tb_program *p, bindset *s, tb_array *arr) {
    uint32_t np = 0;
    char dname[64][TB_MAX_NAME + 1];
    int64_t dval[64];
    uint32_t nd = 0;
    tb_param_count(p, &np);
    /* pass 1: dimensions from bindings with values or extents */
    for (int pass = 0; pass < 2; pass++) {
        for (uint32_t i = 0; i < np; i++) {
            tb_param pa;
            binding *b;
            tb_param_at(p, i, &pa);
            b = find_binding(s, pa.name);
            if (pass == 0) {
                if (!b) continue;
                if (!b->has_extent) {
                    b->rank = pa.rank;
                    if (pa.rank == 1 && b->has_values) {
                        b->extent[0] = (int64_t) b->count;
                        b->has_extent = 1;
                    } else if (pa.rank == 0) {
                        b->has_extent = 1;
                    }
                }
                if (b->has_extent)
                    for (uint32_t r = 0; r < pa.rank; r++)
                        if (pa.dim[r][0] && nd < 64u) {
                            int known = 0;
                            for (uint32_t k = 0; k < nd; k++)
                                if (strcmp(dname[k], pa.dim[r]) == 0) known = 1;
                            if (!known) {
                                strcpy(dname[nd], pa.dim[r]);
                                dval[nd++] = b->extent[r];
                            }
                        }
                continue;
            }
            /* pass 2: outputs without bindings or extents */
            if (!b) {
                if (pa.role != TB_ROLE_OUT) {
                    fprintf(stderr, "no values for '%s' (%s)\n", pa.name, pa.role == TB_ROLE_IN ? "in" : "inout");
                    return -1;
                }
                if (s->n >= 64u) return -1;
                b = &s->b[s->n++];
                memset(b, 0, sizeof *b);
                strcpy(b->name, pa.name);
                b->rank = pa.rank;
            }
            if (!b->has_extent) {
                for (uint32_t r = 0; r < pa.rank; r++) {
                    int found = 0;
                    if (!pa.dim[r][0]) {
                        b->extent[r] = pa.fixed[r];
                        continue;
                    }
                    for (uint32_t k = 0; k < nd; k++)
                        if (strcmp(dname[k], pa.dim[r]) == 0) {
                            b->extent[r] = dval[k];
                            found = 1;
                        }
                    if (!found) {
                        fprintf(stderr, "give the extent of '%s', as in %s(N)\n", pa.name, pa.name);
                        return -1;
                    }
                }
                b->has_extent = 1;
            }
        }
    }
    for (uint32_t i = 0; i < s->n; i++) {
        binding *b = &s->b[i];
        uint64_t elems = 1;
        for (uint32_t r = 0; r < b->rank; r++) elems *= (uint64_t) (b->extent[r] > 0 ? b->extent[r] : 0);
        if (b->has_values) {
            if (b->count != elems && !(b->rank == 0 && b->count == 1u)) {
                fprintf(stderr, "'%s' has %llu values for %llu elements\n", b->name,
                        (unsigned long long) b->count, (unsigned long long) elems);
                return -1;
            }
        } else {
            b->num = (int64_t *) calloc(elems ? elems : 1u, sizeof(int64_t));
            b->den = (int64_t *) calloc(elems ? elems : 1u, sizeof(int64_t));
            if (!b->num || !b->den) return -1;
            for (uint64_t k = 0; k < (elems ? elems : 1u); k++) b->den[k] = 1;
            b->count = elems;
        }
        memset(&arr[i], 0, sizeof arr[i]);
        arr[i].name = b->name;
        arr[i].num = b->num;
        arr[i].den = b->den;
        arr[i].rank = b->rank;
        arr[i].extent[0] = b->rank > 0 ? b->extent[0] : 0;
        arr[i].extent[1] = b->rank > 1 ? b->extent[1] : 0;
    }
    return (int) s->n;
}

static void print_value(FILE *f, int64_t n, int64_t d) {
    char s[96];
    size_t len = 0;
    if (zxv_rat_format(n, d, s, sizeof s, &len) != ZXV_OK) strcpy(s, "?");
    fputs(s, f);
}

static void print_outputs(const tb_program *p, bindset *s) {
    uint32_t np = 0;
    tb_param_count(p, &np);
    for (uint32_t i = 0; i < np; i++) {
        tb_param pa;
        binding *b;
        tb_param_at(p, i, &pa);
        if (pa.role == TB_ROLE_IN) continue;
        b = find_binding(s, pa.name);
        if (!b) continue;
        if (pa.rank < 2) {
            printf("%s = ", pa.name);
            for (uint64_t k = 0; k < b->count; k++) {
                if (k) printf(", ");
                print_value(stdout, b->num[k], b->den[k]);
            }
            printf("\n");
        } else {
            for (int64_t r = 0; r < b->extent[0]; r++) {
                printf("%s(%lld,:) = ", pa.name, (long long) r + 1);
                for (int64_t c = 0; c < b->extent[1]; c++) {
                    uint64_t k = (uint64_t) r + (uint64_t) c * (uint64_t) b->extent[0];
                    if (c) printf(", ");
                    print_value(stdout, b->num[k], b->den[k]);
                }
                printf("\n");
            }
        }
    }
}

static int cmd_run(const char *path, int argc, char **argv) {
    tb_program *p = load(path, NULL, NULL);
    bindset s;
    tb_array arr[64];
    tb_diag d;
    int n, rc = 0;
    int32_t st;
    if (!p) return 1;
    memset(&s, 0, sizeof s);
    for (int i = 0; i < argc; i++)
        if (!parse_binding(&s, argv[i])) {
            fprintf(stderr, "bad binding '%s' (NAME=v1,v2 / NAME(m,n)=... / NAME(m))\n", argv[i]);
            bindset_free(&s);
            tb_program_destroy(p);
            return 2;
        }
    n = make_arrays(p, &s, arr);
    if (n < 0) {
        rc = 2;
    } else {
        st = tb_exec(p, arr, (uint32_t) n, g_work_limit, &d);
        if (st != ZXV_OK) {
            size_t len = 0;
            char *text = slurp(path, &len);
            show_refusal(path, text ? text : "", text ? len : 0, &d);
            free(text);
            rc = 1;
        } else {
            uint64_t w = 0;
            print_outputs(p, &s);
            tb_last_work(&w);
            fprintf(stderr, "(%llu rational operations)\n", (unsigned long long) w);
        }
    }
    bindset_free(&s);
    tb_program_destroy(p);
    return rc;
}

/* ===== golden ===== */

static int32_t status_by_name(const char *n) {
    static const int32_t codes[] = {TB_E_SYNTAX, TB_E_NAME, TB_E_SHAPE, TB_E_BOUNDS, TB_E_DIVZERO, TB_E_LIMIT};
    for (size_t i = 0; i < sizeof codes / sizeof codes[0]; i++)
        if (strcmp(n, tb_status_name(codes[i])) == 0) return codes[i];
    for (int32_t c = 0; c >= -99; c--)
        if (strcmp(n, zxv_status_name(c)) == 0) return c;
    return 1;
}

typedef struct {
    char name[64];
    bindset s;
    char expect[64][TB_MAX_NAME + 1];
    binding expv[64];
    uint32_t nexp;
    int32_t refuse; /* 1: none expected */
    uint32_t rline, rcol;
    int has_pos;
} gcase;

static unsigned long long g_cases, g_values;

static int run_case(const char *path, const tb_program *p, int32_t parse_st, const tb_diag *pd, gcase *c) {
    tb_array arr[64];
    tb_diag d;
    int n, bad = 0;
    int32_t st;
    g_cases++;
    if (parse_st != ZXV_OK) {
        st = parse_st;
        d = *pd;
    } else {
        n = make_arrays(p, &c->s, arr);
        if (n < 0) {
            fprintf(stderr, "%s [%s]: bad bindings\n", path, c->name);
            return 1;
        }
        st = tb_exec(p, arr, (uint32_t) n, g_work_limit, &d);
    }
    if (c->refuse != 1) {
        if (st != c->refuse) {
            fprintf(stderr, "FAIL %s [%s]: expected %s, got %s: %s\n", path, c->name, tb_status_name(c->refuse),
                    tb_status_name(st), st ? d.msg : "");
            return 1;
        }
        if (c->has_pos && (d.line != c->rline || d.col != c->rcol)) {
            fprintf(stderr, "FAIL %s [%s]: %s at %u:%u, expected %u:%u: %s\n", path, c->name, tb_status_name(st),
                    (unsigned) d.line, (unsigned) d.col, (unsigned) c->rline, (unsigned) c->rcol, d.msg);
            return 1;
        }
        g_values++;
        return 0;
    }
    if (st != ZXV_OK) {
        fprintf(stderr, "FAIL %s [%s]: refused: %s\n", path, c->name, d.msg);
        return 1;
    }
    for (uint32_t e = 0; e < c->nexp; e++) {
        binding *got = find_binding(&c->s, c->expect[e]);
        binding *want = &c->expv[e];
        if (!got || got->count != want->count) {
            fprintf(stderr, "FAIL %s [%s]: %s has %llu values, expected %llu\n", path, c->name, c->expect[e],
                    got ? (unsigned long long) got->count : 0ull, (unsigned long long) want->count);
            bad = 1;
            continue;
        }
        for (uint64_t k = 0; k < want->count; k++) {
            int64_t wn, wd;
            zxv_rat_normalize(want->num[k], want->den[k], &wn, &wd);
            g_values++;
            if (got->num[k] != wn || got->den[k] != wd) {
                fprintf(stderr, "FAIL %s [%s]: %s element %llu is %lld/%lld, expected %lld/%lld\n", path,
                        c->name, c->expect[e], (unsigned long long) k + 1u, (long long) got->num[k],
                        (long long) got->den[k], (long long) wn, (long long) wd);
                bad = 1;
            }
        }
    }
    return bad;
}

static void gcase_free(gcase *c) {
    bindset_free(&c->s);
    for (uint32_t i = 0; i < c->nexp; i++) {
        free(c->expv[i].num);
        free(c->expv[i].den);
    }
}

static void gcase_init(gcase *c, const char *name) {
    memset(c, 0, sizeof *c);
    snprintf(c->name, sizeof c->name, "%s", name);
    c->refuse = 1;
}

static int cmd_golden(int argc, char **argv) {
    int fails = 0;
    unsigned files = 0;
    for (int f = 0; f < argc; f++) {
        size_t len = 0;
        char *text = slurp(argv[f], &len);
        tb_program *p = NULL;
        tb_diag pd;
        int32_t pst;
        gcase c;
        int started = 0;
        char *line, *save = NULL, *copy;
        if (!text) {
            fprintf(stderr, "%s: cannot read\n", argv[f]);
            fails++;
            continue;
        }
        files++;
        pst = tb_parse(text, len, &p, &pd);
        if (pst == ZXV_OK) {
            /* the canonical text is a fixed point */
            char *c1 = canon(p), *c2 = NULL;
            tb_program *q = NULL;
            tb_diag qd;
            if (!c1 || tb_parse(c1, strlen(c1), &q, &qd) != ZXV_OK || !(c2 = canon(q)) || strcmp(c1, c2) != 0) {
                fprintf(stderr, "FAIL %s: canonical text does not parse back to itself\n", argv[f]);
                fails++;
            }
            free(c1);
            free(c2);
            tb_program_destroy(q);
        }
        copy = (char *) malloc(len + 1u);
        memcpy(copy, text, len + 1u);
        gcase_init(&c, "1");
        for (line = strtok_r(copy, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
            char *l = trim(line);
            if (strncmp(l, "!>", 2) != 0) continue;
            l = trim(l + 2);
            if (strncmp(l, "case", 4) == 0 && (l[4] == ' ' || !l[4])) {
                if (started) {
                    fails += run_case(argv[f], p, pst, &pd, &c);
                    gcase_free(&c);
                }
                gcase_init(&c, trim(l + 4));
                started = 0;
            } else if (strncmp(l, "in ", 3) == 0 || strncmp(l, "out ", 4) == 0) {
                started = 1;
                if (!parse_binding(&c.s, trim(l + (l[0] == 'i' ? 3 : 4)))) {
                    fprintf(stderr, "%s: bad directive '%s'\n", argv[f], l);
                    fails++;
                }
            } else if (strncmp(l, "expect ", 7) == 0) {
                bindset tmp;
                started = 1;
                memset(&tmp, 0, sizeof tmp);
                if (c.nexp >= 64u || !parse_binding(&tmp, trim(l + 7))) {
                    fprintf(stderr, "%s: bad directive '%s'\n", argv[f], l);
                    fails++;
                    bindset_free(&tmp);
                    continue;
                }
                strcpy(c.expect[c.nexp], tmp.b[0].name);
                c.expv[c.nexp++] = tmp.b[0];
            } else if (strncmp(l, "refuse ", 7) == 0) {
                char sname[64] = {0};
                unsigned ln = 0, co = 0;
                int k = sscanf(l + 7, "%63s %u:%u", sname, &ln, &co);
                started = 1;
                c.refuse = status_by_name(sname);
                c.has_pos = k == 3;
                c.rline = ln;
                c.rcol = co;
                if (c.refuse == 1) {
                    fprintf(stderr, "%s: unknown status '%s'\n", argv[f], sname);
                    fails++;
                }
            } else {
                fprintf(stderr, "%s: unknown directive '%s'\n", argv[f], l);
                fails++;
            }
        }
        if (started) fails += run_case(argv[f], p, pst, &pd, &c);
        gcase_free(&c);
        free(copy);
        free(text);
        tb_program_destroy(p);
    }
    printf("golden: %u programs, %llu cases, %llu values and refusals checked, %d failed\n", files, g_cases,
           g_values, fails);
    return fails ? 1 : 0;
}

int main(int argc, char **argv) {
    int a = 1;
    while (a < argc && strncmp(argv[a], "--", 2) == 0) {
        if (strcmp(argv[a], "--work") == 0 && a + 1 < argc) {
            g_work_limit = strtoull(argv[a + 1], NULL, 10);
            a += 2;
        } else {
            fprintf(stderr, "unknown option %s\n", argv[a]);
            return 2;
        }
    }
    if (a >= argc) {
        fprintf(stderr, "usage: ztrebago [--work N] check|canon|params|same|run|golden FILE ...\n");
        return 2;
    }
    {
        const char *cmd = argv[a++];
        if (strcmp(cmd, "golden") == 0) return cmd_golden(argc - a, argv + a);
        if (a >= argc) {
            fprintf(stderr, "%s: which file?\n", cmd);
            return 2;
        }
        if (strcmp(cmd, "check") == 0) {
            int rc = 0;
            for (; a < argc; a++) {
                tb_program *p = load(argv[a], NULL, NULL);
                uint8_t dg[32];
                char hx[65];
                if (!p) {
                    rc = 1;
                    continue;
                }
                tb_program_digest(p, dg);
                hex16(dg, hx);
                printf("%s: ok, sha3-256 %s\n", argv[a], hx);
                tb_program_destroy(p);
            }
            return rc;
        }
        if (strcmp(cmd, "canon") == 0) {
            tb_program *p = load(argv[a], NULL, NULL);
            char *c;
            if (!p) return 1;
            c = canon(p);
            fputs(c ? c : "", stdout);
            free(c);
            tb_program_destroy(p);
            return 0;
        }
        if (strcmp(cmd, "params") == 0) {
            tb_program *p = load(argv[a], NULL, NULL);
            uint32_t n = 0;
            static const char *const roles[] = {"", "in", "out", "inout"};
            if (!p) return 1;
            tb_param_count(p, &n);
            for (uint32_t i = 0; i < n; i++) {
                tb_param pa;
                tb_param_at(p, i, &pa);
                printf("%-5s %s", roles[pa.role], pa.name);
                for (uint32_t r = 0; r < pa.rank; r++) {
                    if (pa.dim[r][0])
                        printf("%s%s", r ? ", " : "(", pa.dim[r]);
                    else
                        printf("%s%lld", r ? ", " : "(", (long long) pa.fixed[r]);
                }
                printf("%s\n", pa.rank ? ")" : "");
            }
            tb_program_destroy(p);
            return 0;
        }
        if (strcmp(cmd, "same") == 0) {
            tb_program *p, *q;
            uint8_t d1[32], d2[32];
            char hx[65];
            int same;
            if (a + 1 >= argc) return 2;
            p = load(argv[a], NULL, NULL);
            q = load(argv[a + 1], NULL, NULL);
            if (!p || !q) {
                tb_program_destroy(p);
                tb_program_destroy(q);
                return 1;
            }
            tb_program_digest(p, d1);
            tb_program_digest(q, d2);
            same = memcmp(d1, d2, 32) == 0;
            hex16(d1, hx);
            if (same)
                printf("same program: %s and %s, sha3-256 %.12s...\n", argv[a], argv[a + 1], hx);
            else
                printf("different programs: %s and %s\n", argv[a], argv[a + 1]);
            tb_program_destroy(p);
            tb_program_destroy(q);
            return same ? 0 : 1;
        }
        if (strcmp(cmd, "run") == 0) return cmd_run(argv[a], argc - a - 1, argv + a + 1);
        fprintf(stderr, "unknown command %s\n", cmd);
        return 2;
    }
}
