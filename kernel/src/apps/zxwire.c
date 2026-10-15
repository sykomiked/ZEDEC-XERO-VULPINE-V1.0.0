/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxwire.c — circuits of entries (zxwire.h). */
#include "zxwire.h"

static bool seq(const char *a, const char *b)
{
    uint32_t i = 0;
    for (; a[i] && a[i] == b[i]; i++) {
    }
    return a[i] == b[i];
}

static uint32_t slen(const char *s)
{
    uint32_t n = 0;
    while (s[n]) n++;
    return n;
}

/* Append to an error buffer (cap ZXAPP_ERR_MAX), cut short when full. */
static void cat(char *d, const char *s)
{
    uint32_t k = slen(d);
    for (; s && *s && k + 1u < ZXAPP_ERR_MAX; s++) d[k++] = *s;
    d[k] = 0;
}

static void say(char *d, const char *a, const char *b, const char *c)
{
    d[0] = 0;
    cat(d, a);
    cat(d, b);
    cat(d, c);
}

static bool word_is(const char *w, uint32_t n, const char *lit)
{
    uint32_t i = 0;
    for (; i < n && lit[i] && w[i] == lit[i]; i++) {
    }
    return i == n && !lit[i];
}

static bool copy(char *d, uint32_t cap, const char *w, uint32_t n)
{
    if (n + 1u > cap) return false;
    for (uint32_t i = 0; i < n; i++) d[i] = w[i];
    d[n] = 0;
    return true;
}

static bool ident(const char *w, uint32_t n)
{
    if (!n) return false;
    for (uint32_t i = 0; i < n; i++) {
        char c = w[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' ||
                  (i && c >= '0' && c <= '9');
        if (!ok) return false;
    }
    return true;
}

static bool dotted(const char *w, uint32_t n)
{
    uint32_t b = 0;
    for (uint32_t i = 0; i <= n; i++)
        if (i == n || w[i] == '.') {
            if (!ident(w + b, i - b)) return false;
            b = i + 1u;
        }
    return true;
}

/* A whole number, at most 18 digits so it always fits. */
static bool whole(const char *w, uint32_t n, int64_t *v)
{
    uint32_t i = 0;
    bool neg = n && w[0] == '-';
    if (neg) i++;
    if (i == n || n - i > 18u) return false;
    int64_t x = 0;
    for (; i < n; i++) {
        if (w[i] < '0' || w[i] > '9') return false;
        x = x * 10 + (w[i] - '0');
    }
    *v = neg ? -x : x;
    return true;
}

/* Up to 4 space-separated words; the 4th is the rest of the line. */
static uint32_t split(const char *p, uint32_t n, const char *w[4], uint32_t wl[4])
{
    uint32_t k = 0, i = 0;
    while (k < 4) {
        while (i < n && (p[i] == ' ' || p[i] == '\t')) i++;
        if (i >= n || p[i] == '#') break;
        uint32_t b = i;
        if (k == 3) {
            uint32_t e = n;
            for (uint32_t j = i; j < n; j++)
                if (p[j] == '#') {
                    e = j;
                    break;
                }
            while (e > b && (p[e - 1] == ' ' || p[e - 1] == '\t')) e--;
            w[k] = p + b;
            wl[k++] = e - b;
            break;
        }
        while (i < n && p[i] != ' ' && p[i] != '\t') i++;
        w[k] = p + b;
        wl[k++] = i - b;
    }
    return k;
}

static int bad(zxwire_circuit_t *c, uint32_t line, const char *a, const char *b)
{
    say(c->err, a, b, 0);
    c->err_line = line;
    return ZXAPP_E_SYNTAX;
}

/* A name in the circuit: an input, or a part declared before `upto`. */
static int find_name(const zxwire_circuit_t *c, const char *w, uint32_t n, uint32_t upto,
                     zxwire_src_t *s)
{
    for (uint32_t i = 0; i < c->ninputs; i++)
        if (word_is(w, n, c->input[i])) {
            s->kind = ZXWIRE_SRC_INPUT;
            s->index = (uint8_t) i;
            return 1;
        }
    /* the slot after the last part is the one being parsed (zeroed otherwise) */
    uint32_t last = c->nparts < ZXWIRE_PARTS ? c->nparts + 1u : c->nparts;
    for (uint32_t i = 0; i < last; i++)
        if (c->part[i].name[0] && word_is(w, n, c->part[i].name)) {
            if (i >= upto) return -1; /* W3: later parts would make a loop */
            s->kind = ZXWIRE_SRC_PART;
            s->index = (uint8_t) i;
            return 1;
        }
    return 0;
}

static int parse_src(zxwire_circuit_t *c, uint32_t line, const char *w, uint32_t n, uint32_t upto,
                     zxwire_src_t *s)
{
    for (uint32_t i = 0; i < sizeof *s; i++) ((uint8_t *) s)[i] = 0;
    if (n && ((w[0] >= '0' && w[0] <= '9') || w[0] == '-')) { /* a setting: 1,2,3 */
        s->kind = ZXWIRE_SRC_CONST;
        uint32_t b = 0;
        for (uint32_t i = 0; i <= n; i++)
            if (i == n || w[i] == ',') {
                if (s->nconst >= ZXWIRE_CONST || !whole(w + b, i - b, &s->konst[s->nconst]))
                    return bad(c, line, "a setting is up to 8 whole numbers joined by commas", 0);
                s->nconst++;
                b = i + 1u;
            }
        return ZXAPP_OK;
    }
    int f = find_name(c, w, n, upto, s);
    if (f < 0) return bad(c, line, "a part can only take earlier parts (no loops)", 0);
    if (!f) return bad(c, line, "an unknown source (an input, an earlier part or a setting)", 0);
    return ZXAPP_OK;
}

static int parse_part(zxwire_circuit_t *c, uint32_t line, const char *const w[4],
                      const uint32_t wl[4], uint32_t k)
{
    if (k < 3) return bad(c, line, "part needs: part <name> <entry> [<input>=<source> ...]", 0);
    if (c->nparts >= ZXWIRE_PARTS) return bad(c, line, "a circuit has at most 16 parts", 0);
    zxwire_part_t *P = &c->part[c->nparts];
    for (uint32_t i = 0; i < sizeof *P; i++) ((uint8_t *) P)[i] = 0;
    if (!ident(w[1], wl[1]) || !copy(P->name, sizeof P->name, w[1], wl[1]) ||
        word_is(w[1], wl[1], "out"))
        return bad(c, line, "a part's name is a short identifier, not \"out\"", 0);
    bool dup = false;
    for (uint32_t i = 0; i < c->ninputs; i++) dup |= seq(c->input[i], P->name);
    for (uint32_t i = 0; i < c->nparts; i++) dup |= seq(c->part[i].name, P->name);
    if (dup) return bad(c, line, "a name used twice: ", P->name);
    if (!dotted(w[2], wl[2]) || !copy(P->entry, sizeof P->entry, w[2], wl[2]))
        return bad(c, line, "a part's entry is a name like block.entry", 0);
    if (k == 4) {
        const char *p = w[3];
        uint32_t n = wl[3], i = 0;
        while (i < n) {
            while (i < n && (p[i] == ' ' || p[i] == '\t')) i++;
            uint32_t b = i, eq = 0;
            while (i < n && p[i] != ' ' && p[i] != '\t') {
                if (p[i] == '=' && !eq) eq = i;
                i++;
            }
            if (i == b) break;
            if (!eq) return bad(c, line, "an input is wired as <input>=<source>", 0);
            if (P->nports >= ZXAPP_INPUTS) return bad(c, line, "a part has at most 8 inputs", 0);
            char *port = P->port[P->nports];
            if (!ident(p + b, eq - b) || !copy(port, ZXAPP_ID_MAX, p + b, eq - b))
                return bad(c, line, "an input's name is a short identifier", 0);
            for (uint32_t j = 0; j < P->nports; j++)
                if (seq(P->port[j], port)) return bad(c, line, "an input wired twice: ", port);
            int rc = parse_src(c, line, p + eq + 1u, i - eq - 1u, c->nparts, &P->src[P->nports]);
            if (rc != ZXAPP_OK) return rc;
            P->nports++;
        }
    }
    c->nparts++;
    return ZXAPP_OK;
}

static int parse_at(zxwire_circuit_t *c, uint32_t line, const char *const w[4],
                    const uint32_t wl[4], uint32_t k)
{
    const char *xy[4];
    uint32_t xyl[4];
    int64_t x, y;
    if (k != 4 || split(w[3], wl[3], xy, xyl) != 1 || !whole(w[2], wl[2], &x) ||
        !whole(xy[0], xyl[0], &y) || x < 0 || y < 0 || x > 100000 || y > 100000)
        return bad(c, line, "at needs: at <name> <x> <y>, whole numbers from 0 to 100000", 0);
    if (word_is(w[1], wl[1], "out")) {
        c->out_x = (int32_t) x, c->out_y = (int32_t) y, c->out_placed = true;
        return ZXAPP_OK;
    }
    zxwire_src_t s;
    if (find_name(c, w[1], wl[1], c->nparts, &s) != 1)
        return bad(c, line, "at names a part, an input or out, after it is declared", 0);
    if (s.kind == ZXWIRE_SRC_INPUT) {
        c->in_x[s.index] = (int32_t) x, c->in_y[s.index] = (int32_t) y;
        c->in_placed[s.index] = true;
    } else {
        c->part[s.index].x = (int32_t) x, c->part[s.index].y = (int32_t) y;
        c->part[s.index].placed = true;
    }
    return ZXAPP_OK;
}

int zxwire_parse(zxwire_circuit_t *c, const zxapp_provide_t *P, const char *src, uint32_t n)
{
    for (uint32_t i = 0; i < sizeof *c; i++) ((uint8_t *) c)[i] = 0;
    if (!P) return bad(c, 0, "a circuit needs a provides line: its inputs and its answer", 0);
    c->kind = P->kind;
    c->ninputs = P->ninputs;
    for (uint32_t i = 0; i < P->ninputs; i++)
        copy(c->input[i], ZXAPP_ID_MAX, P->input[i], slen(P->input[i]));
    bool have_out = false;
    uint32_t at = 0, line = 0;
    while (src && at < n) {
        uint32_t b = at;
        while (at < n && src[at] != '\n') at++;
        uint32_t e = at;
        if (at < n) at++;
        if (e > b && src[e - 1] == '\r') e--;
        line++;
        const char *w[4];
        uint32_t wl[4];
        uint32_t k = split(src + b, e - b, w, wl);
        if (!k) continue;
        int rc = ZXAPP_OK;
        if (word_is(w[0], wl[0], "part"))
            rc = parse_part(c, line, w, wl, k);
        else if (word_is(w[0], wl[0], "at"))
            rc = parse_at(c, line, w, wl, k);
        else if (word_is(w[0], wl[0], "out")) {
            if (have_out) return bad(c, line, "a circuit has one out line", 0);
            if (k != 2) return bad(c, line, "out needs: out <source>", 0);
            rc = parse_src(c, line, w[1], wl[1], c->nparts, &c->out);
            have_out = true;
        } else
            return bad(c, line, "a circuit line is part, out or at", 0);
        if (rc != ZXAPP_OK) return rc;
    }
    if (!have_out) return bad(c, line, "a circuit needs an out line: its answer", 0);
    return ZXAPP_OK;
}

bool zxwire_resolve(const char *self, const char *entry, char out[ZXAPP_ENTRY_MAX])
{
    uint32_t dot = 0, k = 0;
    for (uint32_t i = 0; self[i]; i++)
        if (self[i] == '.') dot = i + 1u;
    uint32_t el = slen(entry);
    if (dot + el + 1u > ZXAPP_ENTRY_MAX) return false;
    for (uint32_t i = 0; i < dot; i++) out[k++] = self[i];
    for (uint32_t i = 0; i < el; i++) out[k++] = entry[i];
    out[k] = 0;
    return true;
}

uint8_t zxwire_src_kind(const zxwire_circuit_t *c, const zxwire_src_t *s, const uint8_t *kinds)
{
    (void) c;
    if (s->kind == ZXWIRE_SRC_INPUT || s->kind == ZXWIRE_SRC_CONST) return ZXAPP_KIND_NUM;
    if (s->kind == ZXWIRE_SRC_PART && kinds) return kinds[s->index];
    return 0;
}

/* ---- the hook (W1) ------------------------------------------------------------ */

static void app_err(zxapp_t *a, uint32_t line, const char *m1, const char *m2, const char *m3)
{
    say(a->err, m1, m2, m3);
    a->err_line = line;
}

/* Put "<entry><m1><m2>: " in front of the error a part's call left. */
static void wrap(zxapp_t *a, const zxapp_logic_t *L, const char *m1, const char *m2)
{
    char inner[ZXAPP_ERR_MAX];
    say(inner, a->err, 0, 0);
    say(a->err, L->entry, m1, m2);
    if (inner[0]) cat(a->err, ": ");
    cat(a->err, inner);
    a->err_line = L->line;
}

static int w_check(void *ctx, zxapp_t *a, const zxapp_logic_t *L)
{
    zxwire_t *w = ctx;
    if (w->depth) return ZXAPP_E_ARG; /* not while a circuit runs */
    zxwire_circuit_t *c = &w->c[0];
    if (zxwire_parse(c, zxapp_contract(a, L->entry), L->src, L->src_len) != ZXAPP_OK) {
        app_err(a, L->line + c->err_line, L->entry, ": ", c->err);
        return ZXAPP_E_RUNTIME;
    }
    for (uint32_t i = 0; i < c->nparts; i++) {
        const zxwire_part_t *P = &c->part[i];
        char full[ZXAPP_ENTRY_MAX];
        const zxapp_logic_t *E = zxwire_resolve(L->entry, P->entry, full) ? zxapp_find(a, full) : 0;
        if (!E) {
            app_err(a, L->line, L->entry, ": no entry for part ", P->name);
            cat(a->err, " (");
            cat(a->err, P->entry);
            cat(a->err, "; add a use line, or export it)");
            return ZXAPP_E_NAME;
        }
        if (seq(full, L->entry)) {
            app_err(a, L->line, L->entry, ": a circuit cannot be its own part", 0);
            return ZXAPP_E_RUNTIME;
        }
        const zxapp_provide_t *C = zxapp_contract(a, full);
        if (!C) {
            app_err(a, L->line, L->entry, ": part ", P->name);
            cat(a->err, " has no provides line, so its inputs are unknown");
            return ZXAPP_E_CONTRACT;
        }
        for (uint32_t j = 0; j < P->nports; j++) {
            bool known = false;
            for (uint32_t q = 0; q < C->ninputs && !known; q++)
                known = seq(C->input[q], P->port[j]);
            if (!known) {
                app_err(a, L->line, L->entry, ": part ", P->name);
                cat(a->err, " has no input ");
                cat(a->err, P->port[j]);
                return ZXAPP_E_CONTRACT;
            }
        }
        if (P->nports != C->ninputs) {
            app_err(a, L->line, L->entry, ": part ", P->name);
            cat(a->err, " has inputs not wired, every one needs a source");
            return ZXAPP_E_CONTRACT;
        }
    }
    return ZXAPP_OK;
}

/* W4: hold, saying which wire. */
static int held(zxapp_t *a, const zxapp_logic_t *L, const char *m1, const char *m2, const char *m3)
{
    app_err(a, L->line, L->entry, ": held: ", m1);
    cat(a->err, m2);
    cat(a->err, m3);
    return ZXAPP_E_CONFLICT;
}

static int w_run(void *ctx, zxapp_t *a, zxapp_host_t *h, const zxapp_logic_t *L,
                 const zxapp_arg_t *args, uint32_t nargs, zxapp_num_t *out, uint32_t cap,
                 uint32_t *n_out, int32_t *lp)
{
    zxwire_t *w = ctx;
    *n_out = 0;
    *lp = ZXAPP_NEITHER;
    if (w->depth >= ZXWIRE_DEPTH) {
        app_err(a, L->line, L->entry, ": circuits nest deeper than 4", 0);
        return ZXAPP_E_RUNTIME;
    }
    uint32_t d = w->depth++;
    zxwire_circuit_t *c = &w->c[d];
    int rc = ZXAPP_OK;
    int32_t join = ZXAPP_TRUE;
    uint8_t kinds[ZXWIRE_PARTS] = {0};
    if (zxwire_parse(c, zxapp_contract(a, L->entry), L->src, L->src_len) != ZXAPP_OK ||
        nargs != c->ninputs) {
        app_err(a, L->line, L->entry, ": ", c->err[0] ? c->err : "the wrong inputs");
        rc = ZXAPP_E_RUNTIME;
        goto done;
    }
    for (uint32_t i = 0; i < c->nparts && rc == ZXAPP_OK; i++) {
        const zxwire_part_t *P = &c->part[i];
        char full[ZXAPP_ENTRY_MAX];
        const zxapp_provide_t *C =
            zxwire_resolve(L->entry, P->entry, full) ? zxapp_contract(a, full) : 0;
        if (!C) {
            app_err(a, L->line, L->entry, ": no entry for part ", P->name);
            rc = ZXAPP_E_NAME;
            break;
        }
        kinds[i] = C->kind;
        zxapp_arg_t pa[ZXAPP_INPUTS];
        const zxwire_src_t *ps[ZXAPP_INPUTS];
        uint32_t np = 0;
        for (uint32_t q = 0; q < C->ninputs && rc == ZXAPP_OK; q++) { /* B3: contract order */
            uint32_t j = 0;
            while (j < P->nports && !seq(P->port[j], C->input[q])) j++;
            if (j == P->nports) {
                app_err(a, L->line, L->entry, ": part ", P->name);
                cat(a->err, " has an input not wired: ");
                cat(a->err, C->input[q]);
                rc = ZXAPP_E_CONTRACT;
                break;
            }
            const zxwire_src_t *s = &P->src[j];
            ps[np] = s;
            pa[np].name = C->input[q];
            if (s->kind == ZXWIRE_SRC_INPUT) {
                pa[np].v = args[s->index].v;
                pa[np].n = args[s->index].n;
            } else if (s->kind == ZXWIRE_SRC_CONST) {
                pa[np].v = s->konst;
                pa[np].n = s->nconst;
            } else { /* W4: an earlier part's answer, whole numbers only */
                const zxwire_part_t *S = &c->part[s->index];
                if (kinds[s->index] != ZXAPP_KIND_NUM) {
                    rc = held(a, L, "a truth from ", S->name, " is wired into ");
                    cat(a->err, P->name);
                    cat(a->err, ".");
                    cat(a->err, C->input[q]);
                    cat(a->err, ", which takes whole numbers");
                    break;
                }
                int64_t *v = w->arg[d][q];
                for (uint32_t e = 0; e < w->nval[d][s->index]; e++) {
                    const zxapp_num_t *x = &w->val[d][s->index][e];
                    if (x->den != 1) {
                        rc = held(a, L, S->name, " answered a fraction, and ", P->name);
                        cat(a->err, ".");
                        cat(a->err, C->input[q]);
                        cat(a->err, " takes whole numbers");
                        break;
                    }
                    v[e] = x->num;
                }
                pa[np].v = v;
                pa[np].n = w->nval[d][s->index];
            }
            np++;
        }
        if (rc != ZXAPP_OK) break;
        /* W2: a one-number setting is the same for every row the part gets */
        uint32_t rows = 0;
        for (uint32_t q = 0; q < np; q++)
            if (ps[q]->kind != ZXWIRE_SRC_CONST && pa[q].n > rows) rows = pa[q].n;
        for (uint32_t q = 0; q < np && rows > 1u && rows <= ZXWIRE_VALUES; q++) {
            if (ps[q]->kind != ZXWIRE_SRC_CONST || ps[q]->nconst != 1u) continue;
            for (uint32_t e = 0; e < rows; e++) w->arg[d][q][e] = ps[q]->konst[0];
            pa[q].v = w->arg[d][q];
            pa[q].n = rows;
        }
        int32_t plp = ZXAPP_NEITHER;
        rc = zxapp_call_witnessed(a, h, full, pa, np, w->val[d][i], ZXWIRE_VALUES, &w->nval[d][i],
                                  &plp);
        if (rc == ZXAPP_E_CONFLICT) {
            wrap(a, L, ": held at part ", P->name);
        } else if (rc == ZXAPP_E_BUFFER) {
            app_err(a, L->line, L->entry, ": part ", P->name);
            cat(a->err, " answers more than 64 values");
            rc = ZXAPP_E_RUNTIME;
        } else if (rc == ZXAPP_E_NORUNTIME) {
            app_err(a, L->line, L->entry, ": part ", P->name);
            cat(a->err, " has no language runtime in this build");
            rc = ZXAPP_E_RUNTIME;
        } else if (rc != ZXAPP_OK) {
            wrap(a, L, ": part ", P->name);
            rc = ZXAPP_E_RUNTIME;
        } else if (plp != ZXAPP_TRUE)
            join = ZXAPP_NEITHER;
    }
    if (rc != ZXAPP_OK) goto done;
    {
        const zxwire_src_t *o = &c->out;
        uint8_t k = zxwire_src_kind(c, o, kinds);
        if (k != c->kind) {
            rc = held(a, L, "out is wired to ", k == ZXAPP_KIND_TRUTH ? "a truth" : "a number",
                      c->kind == ZXAPP_KIND_TRUTH ? ", the circuit answers a truth"
                                                  : ", the circuit answers numbers");
            goto done;
        }
        uint32_t n = o->kind == ZXWIRE_SRC_PART    ? w->nval[d][o->index]
                     : o->kind == ZXWIRE_SRC_CONST ? o->nconst
                                                   : args[o->index].n;
        *n_out = n;
        if (n > cap) {
            rc = ZXAPP_E_BUFFER;
            goto done;
        }
        for (uint32_t e = 0; e < n; e++)
            if (o->kind == ZXWIRE_SRC_PART)
                out[e] = w->val[d][o->index][e];
            else {
                out[e].num = o->kind == ZXWIRE_SRC_CONST ? o->konst[e] : args[o->index].v[e];
                out[e].den = 1;
            }
        *lp = join;
    }
done:
    w->depth--;
    return rc;
}

void zxwire_register(zxapp_host_t *h, zxwire_t *w)
{
    w->depth = 0;
    w->hook.ctx = w;
    w->hook.check = w_check;
    w->hook.run = w_run;
    h->wiring = &w->hook;
}
