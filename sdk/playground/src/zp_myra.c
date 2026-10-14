/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zp_myra.c - MYRA steps (sdk/pillars/mumps-myra): M global trees.
 *
 * The step's file is a schema: MYRA (one line per node,
 * ^PAT(id:i9,"med",seq:i3)=drug:t40!^dose:n9.3!) or the same schema as a
 * GT.M-style global map (GLOBAL / NODE / KEY / PIECE lines). It is parsed
 * exactly; a refusal names its line and column. The schema's SHA3-256 is
 * the output `schema`: two systems that declare the same tree get the
 * same digest.
 *
 * Data files hold M global nodes the way ZWRITE prints them, one per line
 * (';' starts a comment line):
 *
 *   ^PAT(1001,"allergy",1)="penicillin^severe^rash"
 *   ^STK("NORTH",1001)="rice 25kg^120^bag^40"
 *
 * Every line is decoded through the schema (canonical M numbers, exact
 * decimals, $H dates, declared pieces only). A line that does not fit is
 * refused with its line and column in the data file, and the message
 * names the file. Each decoded node becomes an output named by its
 * canonical reference, its pieces as "name = value", in M collation order.
 *
 * Step options:
 *   data=FILE        one data file
 *   sources=A,B[,C,D]
 *                    two to four data files describing the same tree from
 *                    different systems (a clinic chart and a pharmacy
 *                    record; a stock system and a cycle count). A node
 *                    present in every source with different values is
 *                    HELD (BOTH) with the differing fields, never resolved
 *                    silently. A node missing from some source is a note,
 *                    or NEITHER (pending) with `require-both`. A file's
 *                    label is its name without the extension, '-' and '_'
 *                    read as spaces ("clinic-chart.txt" -> clinic chart).
 *   require-both     a node missing from a source leaves the step undecided
 *   reviewer=WHO     who a hold is for ("clinician" -> held for a
 *                    clinician); default: a person
 *   nonneg=F1,F2     these numeric fields may not be negative (refused)
 *   sum=FIELD        output total-FIELD: the exact sum of FIELD over every
 *                    node that has it (held nodes are left out, and say so)
 *   by=KEY           with sum: also output FIELD-by-KEY, one total per value
 *   show=N           at most N node outputs (default 20); the rest are
 *                    decoded and counted, not shown
 * With neither data= nor sources= the step checks the schema only.
 */
#include "zp_internal.h"

#include <zmyra.h>
#include <zxv_api.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SRC   4u
#define MAX_NODES 4096u
#define MAX_GROUP 64u

typedef struct {
    char *ref;      /* canonical reference          */
    char *value;    /* canonical value string       */
    zm_record *rec;
    uint32_t line;  /* line in the data file        */
} dnode;

typedef struct {
    const zp_file *file;
    char label[ZP_MAX_NAME + 1];
    dnode *v;
    uint32_t n, cap;
} source;

typedef struct {
    int64_t u;  /* value = u / 10^sc */
    uint32_t sc;
} dec;

typedef struct {
    char name[64];
    dec total;
} group;

static void oom(zp_step *s) { zp_refuse(s, ZXV_E_NOMEM, "ZXV_E_NOMEM", 0, 0, "out of memory"); }

static const char *last_err(char *b, size_t cap) {
    size_t n = 0;
    if (zm_last_error(b, cap, &n) != ZXV_OK) b[0] = '\0';
    return b;
}

/* Two-call text getters into a fresh string. */
static char *rec_ref(const zm_record *r) {
    size_t n = 0;
    char *b;
    zm_record_ref(r, NULL, 0, &n);
    b = (char *) malloc(n + 1u);
    if (b && zm_record_ref(r, b, n + 1u, &n) != ZXV_OK) b[0] = '\0';
    return b;
}

static char *rec_value(const zm_record *r) {
    size_t n = 0;
    char *b;
    zm_record_encode(r, NULL, 0, &n);
    b = (char *) malloc(n + 1u);
    if (b && zm_record_encode(r, b, n + 1u, &n) != ZXV_OK) b[0] = '\0';
    return b;
}

static char *field_value(const zm_record *r, const char *name) {
    size_t n = 0;
    char *b;
    zm_get_value(r, name, NULL, 0, &n);
    b = (char *) malloc(n + 1u);
    if (b && zm_get_value(r, name, b, n + 1u, &n) != ZXV_OK) b[0] = '\0';
    return b;
}

/* The label of a data file: its name without the extension, '-' and '_'
 * read as spaces. */
static void file_label(const char *name, char *out, size_t cap) {
    size_t n = strlen(name);
    const char *dot = strrchr(name, '.');
    if (dot && dot != name) n = (size_t) (dot - name);
    if (n >= cap) n = cap - 1u;
    for (size_t i = 0; i < n; i++) out[i] = (name[i] == '-' || name[i] == '_') ? ' ' : name[i];
    out[n] = '\0';
}

/* Is `name` in the comma-separated list? */
static int in_list(const char *list, const char *name) {
    size_t n = strlen(name);
    while (list && *list) {
        const char *e = strchr(list, ',');
        size_t k = e ? (size_t) (e - list) : strlen(list);
        if (k == n && strncmp(list, name, n) == 0) return 1;
        list = e ? e + 1 : NULL;
    }
    return 0;
}

/* Column (1-based) of piece p (1-based) of the value written at line[vs..]. */
static uint32_t piece_col(const char *line, size_t len, size_t vs, uint32_t p) {
    size_t i = vs;
    uint32_t k = 1;
    if (i < len && line[i] == '"') i++;
    while (k < p && i < len) {
        if (line[i] == '"') {
            if (i + 1u < len && line[i + 1u] == '"') {
                i += 2u;
                continue;
            }
            break; /* closing quote */
        }
        if (line[i] == '^') k++;
        i++;
    }
    return (uint32_t) i + 1u;
}

/* ---------------------------------------------------------- exact sums */

static int dec_add(dec *a, int64_t u, uint32_t sc) {
    dec b = {u, sc};
    while (a->sc < b.sc) {
        if (__builtin_mul_overflow(a->u, (int64_t) 10, &a->u)) return 0;
        a->sc++;
    }
    while (b.sc < a->sc) {
        if (__builtin_mul_overflow(b.u, (int64_t) 10, &b.u)) return 0;
        b.sc++;
    }
    return !__builtin_add_overflow(a->u, b.u, &a->u);
}

static void dec_text(const dec *d, char *out, size_t cap) {
    char digits[32], buf[64];
    uint64_t m = d->u < 0 ? (uint64_t) 0 - (uint64_t) d->u : (uint64_t) d->u;
    uint32_t n = (uint32_t) snprintf(digits, sizeof digits, "%llu", (unsigned long long) m), o = 0;
    if (d->u < 0) buf[o++] = '-';
    if (d->sc == 0) {
        memcpy(buf + o, digits, n);
        o += n;
    } else if (n <= d->sc) {
        buf[o++] = '0';
        buf[o++] = '.';
        for (uint32_t i = n; i < d->sc; i++) buf[o++] = '0';
        memcpy(buf + o, digits, n);
        o += n;
    } else {
        memcpy(buf + o, digits, n - d->sc);
        o += n - d->sc;
        buf[o++] = '.';
        memcpy(buf + o, digits + n - d->sc, d->sc);
        o += d->sc;
    }
    buf[o] = '\0';
    snprintf(out, cap, "%s", buf);
}

/* ---------------------------------------------------------- data files */

static void source_free(source *src) {
    for (uint32_t i = 0; i < src->n; i++) {
        free(src->v[i].ref);
        free(src->v[i].value);
        zm_record_destroy(src->v[i].rec);
    }
    free(src->v);
    src->v = NULL;
    src->n = src->cap = 0;
}

static void refuse_at(zp_step *s, const source *src, int32_t st, uint32_t line, uint32_t col,
                      const char *what) {
    char why[480];
    snprintf(why, sizeof why, "%s line %u: %s", src->file->name, (unsigned) line, what);
    zp_refuse(s, st, zm_status_name(st), line, col, why);
}

/* Decode one data line. Returns 1 ok, 0 refused. */
static int decode_line(zp_step *s, const zm_schema *schema, source *src, const char *line,
                       size_t len, uint32_t ln, uint32_t lead) {
    size_t eq = len, vs, ve;
    int q = 0, depth = 0;
    char *value;
    size_t vl = 0;
    zm_record *rec = NULL;
    int32_t st;
    char err[400], msg[440];
    const char *nonneg = zp_step_opt(s, "nonneg");
    for (size_t i = 0; i < len; i++) {
        char c = line[i];
        if (c == '"') q = !q;
        else if (!q && c == '(') depth++;
        else if (!q && c == ')') depth--;
        else if (!q && depth == 0 && c == '=') {
            eq = i;
            break;
        }
    }
    if (eq == len) {
        refuse_at(s, src, ZM_E_DATA, ln, lead + 1u,
                  "expected a node as ZWRITE prints it: ^GLOBAL(subscripts)=\"value\"");
        return 0;
    }
    vs = eq + 1u;
    while (vs < len && (line[vs] == ' ' || line[vs] == '\t')) vs++;
    value = (char *) malloc(len - vs + 1u);
    if (!value) {
        oom(s);
        return 0;
    }
    if (vs < len && line[vs] == '"') {
        size_t i = vs + 1u;
        int closed = 0;
        while (i < len) {
            if (line[i] == '"') {
                if (i + 1u < len && line[i + 1u] == '"') {
                    value[vl++] = '"';
                    i += 2u;
                    continue;
                }
                closed = 1;
                i++;
                break;
            }
            value[vl++] = line[i++];
        }
        ve = i;
        while (ve < len && (line[ve] == ' ' || line[ve] == '\t')) ve++;
        if (!closed || ve != len) {
            free(value);
            refuse_at(s, src, ZM_E_DATA, ln, lead + (uint32_t) (closed ? ve : vs) + 1u,
                      closed ? "unexpected text after the value's closing quote"
                             : "the value's string has no closing quote");
            return 0;
        }
    } else {
        ve = len;
        while (ve > vs && (line[ve - 1u] == ' ' || line[ve - 1u] == '\t')) ve--;
        memcpy(value, line + vs, ve - vs);
        vl = ve - vs;
        if (!zm_is_canonic(value, vl)) {
            free(value);
            refuse_at(s, src, ZM_E_DATA, ln, lead + (uint32_t) vs + 1u,
                      "a value is a quoted string, or a number in M canonical form");
            return 0;
        }
    }
    value[vl] = '\0';
    {
        size_t rl = eq;
        while (rl > 0 && (line[rl - 1u] == ' ' || line[rl - 1u] == '\t')) rl--;
        st = zm_node_decode(schema, line, rl, value, vl, &rec);
    }
    free(value);
    if (st != ZXV_OK) {
        unsigned p = 0;
        uint32_t col = lead + 1u;
        last_err(err, sizeof err);
        if (st != ZM_E_KEY) {
            col = lead + (uint32_t) vs + 1u;
            if (sscanf(err, "piece %u", &p) == 1 && p > 0) col = lead + piece_col(line, len, vs, p);
        }
        refuse_at(s, src, st, ln, col, err);
        return 0;
    }
    /* nonneg: numeric fields that may not be negative */
    if (nonneg) {
        uint32_t node = 0;
        zm_node_info info;
        zm_record_node(rec, &node);
        zm_node_info_at(schema, node, &info);
        for (uint32_t k = 0; k < info.keys + info.pieces; k++) {
            zm_field f;
            int64_t u = 0;
            uint32_t sc = 0;
            zm_field_at(schema, node, k, &f);
            if (f.type == ZM_T_TEXT || f.type == ZM_T_DATE || !in_list(nonneg, f.name)) continue;
            if (zm_get_decimal(rec, f.name, &u, &sc) == ZXV_OK && u < 0) {
                char *fv = field_value(rec, f.name);
                snprintf(msg, sizeof msg, "%s %s is negative; this field may not be below zero", f.name,
                         fv ? fv : "");
                free(fv);
                refuse_at(s, src, ZXV_E_RANGE, ln,
                          f.piece ? lead + piece_col(line, len, vs, f.piece) : lead + 1u, msg);
                zm_record_destroy(rec);
                return 0;
            }
        }
    }
    {
        dnode d;
        d.rec = rec;
        d.line = ln;
        d.ref = rec_ref(rec);
        d.value = rec_value(rec);
        if (!d.ref || !d.value) {
            free(d.ref);
            free(d.value);
            zm_record_destroy(rec);
            oom(s);
            return 0;
        }
        for (uint32_t i = 0; i < src->n; i++) {
            if (strcmp(src->v[i].ref, d.ref) == 0) {
                snprintf(msg, sizeof msg, "%s is already set at line %u; a node holds one value", d.ref,
                         (unsigned) src->v[i].line);
                free(d.ref);
                free(d.value);
                zm_record_destroy(rec);
                refuse_at(s, src, ZXV_E_DUPLICATE, ln, lead + 1u, msg);
                return 0;
            }
        }
        if (src->n == MAX_NODES) {
            free(d.ref);
            free(d.value);
            zm_record_destroy(rec);
            refuse_at(s, src, ZP_E_LIMIT, ln, lead + 1u, "more than 4096 nodes in one data file");
            return 0;
        }
        if (src->n == src->cap) {
            uint32_t nc = src->cap ? src->cap * 2u : 32u;
            dnode *nv = (dnode *) realloc(src->v, nc * sizeof *nv);
            if (!nv) {
                free(d.ref);
                free(d.value);
                zm_record_destroy(rec);
                oom(s);
                return 0;
            }
            src->v = nv;
            src->cap = nc;
        }
        src->v[src->n++] = d;
    }
    return 1;
}

static int load_source(zp_step *s, const zm_schema *schema, source *src) {
    const char *t = src->file->text;
    size_t len = src->file->len, p = 0;
    uint32_t ln = 0;
    while (p < len) {
        size_t e = p, ls, le;
        ln++;
        while (e < len && t[e] != '\n') e++;
        le = e;
        if (le > p && t[le - 1u] == '\r') le--;
        ls = p;
        while (ls < le && (t[ls] == ' ' || t[ls] == '\t')) ls++;
        if (ls < le && t[ls] != ';') {
            if (!decode_line(s, schema, src, t + ls, le - ls, ln, (uint32_t) (ls - p))) return 0;
        }
        p = e + 1u;
    }
    return 1;
}

/* ---------------------------------------------------------- reporting */

/* "name = value, ..." for a node's non-empty pieces. */
static void render(zp_buf *b, const zm_schema *schema, const zm_record *r) {
    uint32_t node = 0, shown = 0;
    zm_node_info info;
    zm_record_node(r, &node);
    zm_node_info_at(schema, node, &info);
    for (uint32_t k = info.keys; k < info.keys + info.pieces; k++) {
        zm_field f;
        char *v;
        zm_field_at(schema, node, k, &f);
        v = field_value(r, f.name);
        if (!v) {
            b->oom = 1;
            return;
        }
        if (v[0]) {
            zp_buf_printf(b, "%s%s = %s", shown++ ? ", " : "", f.name, v);
        }
        free(v);
    }
    if (!shown) zp_buf_puts(b, info.pieces ? "(no pieces set)" : "(index node)");
}

typedef struct {
    const char *ref;
    int32_t at[MAX_SRC]; /* index in each source, or -1 */
} unode;

static int ucmp(const void *a, const void *b) {
    const char *x = ((const unode *) a)->ref, *y = ((const unode *) b)->ref;
    int32_t c = 0;
    if (zm_ref_collate(x, strlen(x), y, strlen(y), &c) != ZXV_OK) c = strcmp(x, y) < 0 ? -1 : 1;
    return (int) c;
}

/* Add a node's `sum` field to the totals. Returns 0 on overflow. */
static int add_sum(zp_step *s, const zm_schema *schema, const zm_record *r, const char *field,
                   const char *by, dec *total, group *g, uint32_t *ng, uint32_t *counted) {
    uint32_t node = 0, idx = 0;
    int64_t u = 0;
    uint32_t sc = 0;
    zm_record_node(r, &node);
    if (zm_field_find(schema, node, field, &idx) != ZXV_OK) return 1;
    if (zm_get_decimal(r, field, &u, &sc) != ZXV_OK) return 1; /* empty, or a text field */
    if (!dec_add(total, u, sc)) return 0;
    (*counted)++;
    if (by) {
        char *k = field_value(r, by);
        uint32_t i;
        if (!k) {
            oom(s);
            return 1;
        }
        if (!k[0]) {
            free(k);
            return 1;
        }
        for (i = 0; i < *ng; i++)
            if (strcmp(g[i].name, k) == 0) break;
        if (i == *ng) {
            if (*ng == MAX_GROUP) {
                free(k);
                return 1;
            }
            snprintf(g[i].name, sizeof g[i].name, "%s", k);
            g[i].total.u = 0;
            g[i].total.sc = 0;
            (*ng)++;
        }
        free(k);
        if (!dec_add(&g[i].total, u, sc)) return 0;
    }
    return 1;
}

void zp_lang_myra(zp_step *s, const char *src, size_t len) {
    zm_schema *schema = NULL;
    zm_diag d;
    int32_t st;
    source srcs[MAX_SRC];
    uint32_t ns = 0;
    unode *u = NULL;
    uint32_t nu = 0;
    const char *data = zp_step_opt(s, "data");
    const char *sources = zp_step_opt(s, "sources");
    const char *reviewer = zp_step_opt(s, "reviewer");
    const char *sum = zp_step_opt(s, "sum");
    const char *by = zp_step_opt(s, "by");
    const char *show_opt = zp_step_opt(s, "show");
    int require_all = zp_step_opt(s, "require-both") != NULL;
    uint32_t show = 20, shown = 0, agree = 0, differ = 0, partial = 0;
    char who[ZP_MAX_NAME + 8];
    dec total = {0, 0};
    group *groups = NULL;
    uint32_t ng = 0, counted = 0, held_skipped = 0;
    int sum_overflow = 0;

    memset(srcs, 0, sizeof srcs);
    memset(&d, 0, sizeof d);
    st = zm_schema_parse(src, len, &schema, &d);
    if (st != ZXV_OK) {
        const char *m = strstr(d.msg, ": ");
        char err[400];
        if (st == ZM_E_SYNTAX || st == ZM_E_UNSUPPORTED || st == ZXV_E_UTF8)
            zp_refuse(s, st, zm_status_name(st), d.line, d.col, m ? m + 2 : d.msg);
        else
            zp_refuse(s, st, zm_status_name(st), 0, 0, last_err(err, sizeof err));
        return;
    }
    {
        uint8_t dg[32];
        char hx[65], g[ZM_MAX_GNAME + 2];
        uint32_t nn = 0;
        size_t gl = 0;
        zm_schema_digest(schema, dg);
        for (int i = 0; i < 32; i++) snprintf(hx + 2 * i, 3, "%02x", dg[i]);
        zm_node_count(schema, &nn);
        if (zm_schema_global(schema, g, sizeof g, &gl) != ZXV_OK) g[0] = '\0';
        zp_output(s, "schema", hx);
        zp_notef(s, "schema ^%s: %u node pattern(s), sha3-256 %.16s...", g, (unsigned) nn, hx);
    }
    if (show_opt) {
        char *e = NULL;
        unsigned long v = strtoul(show_opt, &e, 10);
        if (e && !*e && v <= 4096u) show = (uint32_t) v;
    }
    if (reviewer) {
        size_t i, n = strlen(reviewer);
        snprintf(who, sizeof who, "a %s", reviewer);
        for (i = 2; i < n + 2u && who[i]; i++)
            if (who[i] == '-' || who[i] == '_') who[i] = ' ';
    } else {
        snprintf(who, sizeof who, "a person");
    }

    /* which data files */
    {
        const char *list = sources ? sources : data;
        while (list && *list) {
            const char *e = strchr(list, ',');
            size_t k = e ? (size_t) (e - list) : strlen(list);
            char name[ZP_MAX_NAME + 1];
            if (ns == MAX_SRC) {
                zp_refuse(s, ZP_E_LIMIT, NULL, 0, 0, "sources= names at most 4 data files");
                goto done;
            }
            if (k > ZP_MAX_NAME) k = ZP_MAX_NAME;
            memcpy(name, list, k);
            name[k] = '\0';
            srcs[ns].file = zp_step_file(s, name);
            if (!srcs[ns].file) {
                char why[160];
                snprintf(why, sizeof why, "the data file %s is missing", name);
                zp_refuse(s, ZP_E_FILE, NULL, 0, 0, why);
                goto done;
            }
            file_label(name, srcs[ns].label, sizeof srcs[ns].label);
            ns++;
            list = e ? e + 1 : NULL;
        }
    }
    if (sources && ns < 2) {
        zp_refuse(s, ZP_E_ARG, NULL, 0, 0, "sources= names two to four data files, separated by commas");
        goto done;
    }
    if (ns == 0) {
        zp_note(s, "no data= or sources= option: the schema was checked, no data decoded");
        goto done;
    }
    for (uint32_t i = 0; i < ns; i++) {
        if (!load_source(s, schema, &srcs[i])) goto done;
        zp_notef(s, "%s: %u node(s) decoded exactly", srcs[i].file->name, (unsigned) srcs[i].n);
    }

    /* the union of every source's nodes, in M ($QUERY) order */
    {
        uint32_t cap = 0;
        for (uint32_t i = 0; i < ns; i++) cap += srcs[i].n;
        u = (unode *) calloc(cap ? cap : 1u, sizeof *u);
        if (!u) {
            oom(s);
            goto done;
        }
        for (uint32_t i = 0; i < ns; i++) {
            for (uint32_t j = 0; j < srcs[i].n; j++) {
                uint32_t k;
                for (k = 0; k < nu; k++)
                    if (strcmp(u[k].ref, srcs[i].v[j].ref) == 0) break;
                if (k == nu) {
                    u[nu].ref = srcs[i].v[j].ref;
                    for (uint32_t m = 0; m < MAX_SRC; m++) u[nu].at[m] = -1;
                    nu++;
                }
                u[k].at[i] = (int32_t) j;
            }
        }
        qsort(u, nu, sizeof *u, ucmp);
    }
    if (sum && by) {
        groups = (group *) calloc(MAX_GROUP, sizeof *groups);
        if (!groups) {
            oom(s);
            goto done;
        }
    }

    for (uint32_t k = 0; k < nu; k++) {
        zp_buf b = {0};
        const dnode *first = NULL;
        uint32_t have = 0, missing_from = 0;
        int same = 1, held = 0;
        for (uint32_t i = 0; i < ns; i++) {
            if (u[k].at[i] < 0) {
                missing_from |= 1u << i;
                continue;
            }
            have++;
            if (!first) first = &srcs[i].v[u[k].at[i]];
            else if (strcmp(first->value, srcs[i].v[u[k].at[i]].value) != 0) same = 0;
        }
        if (!same) {
            /* contradictory sources: hold, naming each differing field */
            zp_buf h = {0};
            uint32_t node = 0;
            zm_node_info info;
            zm_record_node(first->rec, &node);
            zm_node_info_at(schema, node, &info);
            zp_buf_printf(&h, "%s ", u[k].ref);
            for (uint32_t f = info.keys, nd = 0; f < info.keys + info.pieces; f++) {
                zm_field fd;
                char *vals[MAX_SRC] = {NULL, NULL, NULL, NULL};
                int differs = 0;
                zm_field_at(schema, node, f, &fd);
                for (uint32_t i = 0; i < ns; i++) {
                    if (u[k].at[i] < 0) continue;
                    vals[i] = field_value(srcs[i].v[u[k].at[i]].rec, fd.name);
                    if (!vals[i]) h.oom = 1; /* reported below */
                }
                for (uint32_t i = 0; i < ns && !h.oom; i++)
                    for (uint32_t j = i + 1u; j < ns; j++)
                        if (vals[i] && vals[j] && strcmp(vals[i], vals[j])) differs = 1;
                if (differs) {
                    uint32_t said = 0;
                    zp_buf_printf(&h, "%s%s: ", nd++ ? "; " : "", fd.name);
                    for (uint32_t i = 0; i < ns; i++) {
                        if (!vals[i]) continue;
                        zp_buf_printf(&h, "%s%s says %s", said++ ? ", " : "", srcs[i].label,
                                      vals[i][0] ? vals[i] : "(empty)");
                    }
                }
                for (uint32_t i = 0; i < ns; i++) free(vals[i]);
            }
            zp_buf_printf(&h, "; held for %s", who);
            if (h.oom) oom(s);
            else zp_hold(s, h.p);
            zp_buf_free(&h);
            differ++;
            held = 1;
        } else if (missing_from) {
            zp_buf m = {0};
            uint32_t said = 0;
            partial++;
            zp_buf_printf(&m, "%s is in ", u[k].ref);
            for (uint32_t i = 0; i < ns; i++)
                if (u[k].at[i] >= 0) zp_buf_printf(&m, "%s%s", said++ ? " and " : "", srcs[i].label);
            zp_buf_puts(&m, " but not in ");
            said = 0;
            for (uint32_t i = 0; i < ns; i++)
                if (u[k].at[i] < 0) zp_buf_printf(&m, "%s%s", said++ ? " or " : "", srcs[i].label);
            if (m.oom) {
                oom(s);
            } else if (require_all) {
                zp_buf_puts(&m, "; undecided until every source reports it");
                if (!m.oom) zp_pending(s, m.p);
            } else {
                zp_note(s, m.p);
            }
            zp_buf_free(&m);
        } else if (ns > 1) {
            agree++;
        }
        (void) have;
        /* the node's output */
        if (shown < show) {
            if (held) {
                zp_buf_puts(&b, "HELD: the sources disagree (see held)");
            } else {
                render(&b, schema, first->rec);
                if (missing_from) zp_buf_puts(&b, " (not in every source)");
            }
            if (b.oom) oom(s);
            else zp_output(s, u[k].ref, b.p);
            shown++;
        }
        zp_buf_free(&b);
        if (sum && !sum_overflow) {
            if (held) {
                uint32_t node = 0, idx = 0;
                zm_record_node(first->rec, &node);
                if (zm_field_find(schema, node, sum, &idx) == ZXV_OK) held_skipped++;
            } else if (!(missing_from && require_all)) {
                if (!add_sum(s, schema, first->rec, sum, by, &total, groups, &ng, &counted))
                    sum_overflow = 1;
            }
        }
    }
    if (nu > shown) zp_notef(s, "%u more node(s) decoded, not shown (show=%u)", (unsigned) (nu - shown), (unsigned) show);
    if (ns > 1)
        zp_notef(s, "%u node(s) in all %u sources: %u agree, %u differ; %u in only some", (unsigned) (agree + differ),
                 (unsigned) ns, (unsigned) agree, (unsigned) differ, (unsigned) partial);
    if (sum) {
        char name[ZP_MAX_NAME + 16], txt[48];
        if (sum_overflow) {
            char why[160];
            snprintf(why, sizeof why, "the total of %s does not fit 18 digits exactly; it is not rounded", sum);
            zp_refuse(s, ZXV_E_RANGE, "ZXV_E_RANGE", 0, 0, why);
            goto done;
        }
        snprintf(name, sizeof name, "total-%s", sum);
        dec_text(&total, txt, sizeof txt);
        zp_output(s, name, txt);
        zp_notef(s, "total-%s: the exact sum over %u node(s)", sum, (unsigned) counted);
        if (held_skipped)
            zp_notef(s, "total-%s leaves out %u held node(s): it is not final until they are resolved", sum,
                     (unsigned) held_skipped);
        if (by && ng) {
            zp_buf g = {0};
            for (uint32_t i = 0; i < ng; i++) {
                dec_text(&groups[i].total, txt, sizeof txt);
                zp_buf_printf(&g, "%s%s %s", i ? "; " : "", groups[i].name, txt);
            }
            snprintf(name, sizeof name, "%s-by-%s", sum, by);
            if (g.oom) oom(s);
            else zp_output(s, name, g.p);
            zp_buf_free(&g);
        }
    }

done:
    free(u);
    free(groups);
    for (uint32_t i = 0; i < MAX_SRC; i++) source_free(&srcs[i]);
    zm_schema_destroy(schema);
}
