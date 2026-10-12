/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zmyra — the mumps-myra command-line tool.
 *
 *   zmyra schema FILE              canonical MYRA (a global map or a MYRA file)
 *   zmyra gmap FILE                the same schema as a global map
 *   zmyra fields FILE              every node and field, one per line
 *   zmyra same FILE1 FILE2         exit 0 if both declare the same schema
 *   zmyra decode FILE REF VALUE    decode one node's value; print every field
 *                                  and the canonical value string
 *   zmyra collate                  sort the lines of stdin in M collation order
 *                                  (duplicates dropped, as in an M array)
 *   zmyra canonic                  for each line of stdin: 1 if M treats it as
 *                                  a canonical number, else 0
 *
 * Refusals are printed as FILE:LINE:COLUMN: message [STATUS] with the line
 * and a caret. Exit status: 0 ok, 1 refused or different, 2 usage. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zmyra.h"

static char *slurp(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    char *b = NULL;
    size_t n = 0, cap = 0;
    if (!f) {
        fprintf(stderr, "zmyra: cannot open %s\n", path);
        return NULL;
    }
    for (;;) {
        size_t got;
        if (cap - n < 4096u) {
            size_t nc = cap ? cap * 2u : 8192u;
            char *nb = (char *) realloc(b, nc);
            if (!nb) {
                free(b);
                fclose(f);
                return NULL;
            }
            b = nb;
            cap = nc;
        }
        got = fread(b + n, 1, cap - n - 1u, f);
        n += got;
        if (got == 0) break;
    }
    fclose(f);
    b[n] = '\0';
    *out_len = n;
    return b;
}

static const char *last_err(void) {
    static char b[600];
    size_t n = 0;
    if (zm_last_error(b, sizeof b, &n) != ZXV_OK) b[0] = '\0';
    return b;
}

static zm_schema *load(const char *path) {
    size_t n = 0;
    char *t = slurp(path, &n);
    zm_schema *s = NULL;
    zm_diag d;
    int32_t st;
    if (!t) return NULL;
    st = zm_schema_parse(t, n, &s, &d);
    if (st != ZXV_OK) {
        const char *msg = strstr(d.msg, ": ");
        size_t ls = d.offset, le;
        if (st != ZM_E_SYNTAX && st != ZM_E_UNSUPPORTED && st != ZXV_E_UTF8) {
            fprintf(stderr, "%s: %s [%s]\n", path, last_err(), zm_status_name(st));
            free(t);
            return NULL;
        }
        fprintf(stderr, "%s:%u:%u: %s [%s]\n", path, (unsigned) d.line, (unsigned) d.col,
                msg ? msg + 2 : d.msg, zm_status_name(st));
        while (ls > 0 && t[ls - 1u] != '\n') ls--;
        le = ls;
        while (le < n && t[le] != '\n') le++;
        fprintf(stderr, "%.*s\n%*s^\n", (int) (le - ls), t + ls, (int) (d.col - 1u), "");
    }
    free(t);
    return s;
}

static void print_text(int32_t (*fn)(const zm_schema *, char *, size_t, size_t *),
                       const zm_schema *s) {
    size_t n = 0;
    char *b;
    if (fn(s, NULL, 0, &n) != ZXV_E_BUFFER) return;
    b = (char *) malloc(n + 1u);
    if (b && fn(s, b, n + 1u, &n) == ZXV_OK) fputs(b, stdout);
    free(b);
}

static void hex(const uint8_t *d, size_t n) {
    for (size_t i = 0; i < n; i++) printf("%02x", d[i]);
}

static const char *type_name(const zm_field *f, char *b, size_t cap) {
    switch (f->type) {
    case ZM_T_TEXT: snprintf(b, cap, "TEXT(%u)", (unsigned) f->len); break;
    case ZM_T_INT: snprintf(b, cap, "INTEGER(%u)", (unsigned) f->len); break;
    case ZM_T_NUM: snprintf(b, cap, "NUMBER(%u,%u)", (unsigned) f->len, (unsigned) f->scale); break;
    default: snprintf(b, cap, "DATE"); break;
    }
    return b;
}

static int cmd_fields(const zm_schema *s) {
    uint32_t nn = 0;
    zm_node_count(s, &nn);
    for (uint32_t i = 0; i < nn; i++) {
        zm_node_info info;
        char pat[1024], tb[32];
        size_t n = 0;
        zm_node_info_at(s, i, &info);
        if (zm_node_pattern(s, i, pat, sizeof pat, &n) != ZXV_OK) strcpy(pat, "?");
        printf("node %u %s: %u subscripts, %u pieces, width %u\n", (unsigned) i + 1u, pat,
               (unsigned) info.subs, (unsigned) info.pieces, (unsigned) info.width);
        for (uint32_t k = 0; k < info.keys + info.pieces; k++) {
            zm_field f;
            zm_field_at(s, i, k, &f);
            if (f.sub)
                printf("  key %u %-20s %s\n", (unsigned) f.sub, f.name, type_name(&f, tb, sizeof tb));
            else
                printf("  piece %u %-18s %s%s\n", (unsigned) f.piece, f.name,
                       type_name(&f, tb, sizeof tb), f.required ? " required" : "");
        }
    }
    return 0;
}

static int cmd_decode(const zm_schema *s, const char *ref, const char *value) {
    zm_record *r = NULL;
    uint32_t node = 0;
    zm_node_info info;
    char b[4096];
    size_t n = 0;
    int32_t st = zm_node_decode(s, ref, strlen(ref), value, strlen(value), &r);
    if (st != ZXV_OK) {
        fprintf(stderr, "zmyra: %s [%s]\n", last_err(), zm_status_name(st));
        return 1;
    }
    zm_record_node(r, &node);
    zm_node_info_at(s, node, &info);
    printf("node %u\n", (unsigned) node + 1u);
    for (uint32_t k = 0; k < info.keys + info.pieces; k++) {
        zm_field f;
        zm_field_at(s, node, k, &f);
        if (zm_get_value(r, f.name, b, sizeof b, &n) != ZXV_OK) strcpy(b, "?");
        printf("  %s = %s\n", f.name, b);
    }
    if ((st = zm_record_encode(r, b, sizeof b, &n)) == ZXV_OK) printf("value %s\n", b);
    else printf("value <%s>\n", zm_status_name(st));
    zm_record_destroy(r);
    return 0;
}

/* Lines of stdin, without the newline (and a CR before it). */
static char **read_lines(size_t *out_n) {
    char **v = NULL, line[8192];
    size_t n = 0, cap = 0;
    while (fgets(line, sizeof line, stdin)) {
        size_t k = strlen(line);
        while (k && (line[k - 1u] == '\n' || line[k - 1u] == '\r')) line[--k] = '\0';
        if (n == cap) {
            size_t nc = cap ? cap * 2u : 256u;
            char **nv = (char **) realloc(v, nc * sizeof *nv);
            if (!nv) break;
            v = nv;
            cap = nc;
        }
        v[n] = (char *) malloc(k + 1u);
        if (!v[n]) break;
        memcpy(v[n], line, k + 1u);
        n++;
    }
    *out_n = n;
    return v;
}

static int line_cmp(const void *a, const void *b) {
    const char *x = *(const char *const *) a, *y = *(const char *const *) b;
    int32_t c = 0;
    zm_collate(x, strlen(x), y, strlen(y), &c);
    return c;
}

static int cmd_collate(void) {
    size_t n = 0;
    char **v = read_lines(&n);
    qsort(v, n, sizeof *v, line_cmp);
    for (size_t i = 0; i < n; i++) {
        if (v[i][0] && (i == 0 || line_cmp(&v[i - 1u], &v[i]) != 0)) printf("%s\n", v[i]);
    }
    for (size_t i = 0; i < n; i++) free(v[i]);
    free(v);
    return 0;
}

static int cmd_canonic(void) {
    size_t n = 0;
    char **v = read_lines(&n);
    for (size_t i = 0; i < n; i++) {
        printf("%d %s\n", zm_is_canonic(v[i], strlen(v[i])), v[i]);
        free(v[i]);
    }
    free(v);
    return 0;
}

static int usage(void) {
    fprintf(stderr, "usage: zmyra schema|gmap|fields FILE\n"
                    "       zmyra same FILE1 FILE2\n"
                    "       zmyra decode FILE REF VALUE\n"
                    "       zmyra collate|canonic < lines\n");
    return 2;
}

int main(int argc, char **argv) {
    zm_schema *a = NULL, *b = NULL;
    int rc = 0;
    if (argc < 2) return usage();
    if (strcmp(argv[1], "collate") == 0 && argc == 2) return cmd_collate();
    if (strcmp(argv[1], "canonic") == 0 && argc == 2) return cmd_canonic();
    if (argc < 3) return usage();
    if (!(a = load(argv[2]))) return 1;
    if (strcmp(argv[1], "schema") == 0 && argc == 3) {
        print_text(zm_schema_myra, a);
    } else if (strcmp(argv[1], "gmap") == 0 && argc == 3) {
        print_text(zm_schema_gmap, a);
    } else if (strcmp(argv[1], "fields") == 0 && argc == 3) {
        rc = cmd_fields(a);
    } else if (strcmp(argv[1], "decode") == 0 && argc == 5) {
        rc = cmd_decode(a, argv[3], argv[4]);
    } else if (strcmp(argv[1], "same") == 0 && argc == 4) {
        uint8_t d[32];
        int32_t same = 0;
        if (!(b = load(argv[3]))) {
            zm_schema_destroy(a);
            return 1;
        }
        zm_schema_same(a, b, &same);
        zm_schema_digest(a, d);
        if (same) {
            printf("same schema: %s and %s, sha3-256 ", argv[2], argv[3]);
            hex(d, 32);
            printf("\n");
        } else {
            printf("different schemas: %s and %s\n", argv[2], argv[3]);
            rc = 1;
        }
    } else {
        rc = usage();
    }
    zm_schema_destroy(a);
    zm_schema_destroy(b);
    return rc;
}
