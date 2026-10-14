/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zp_core.c - manifests, the step pipeline, verdicts and the result JSON.
 *
 * template.zxv, one directive per line ('#' starts a comment line):
 *
 *   id ID                  lower-case letters, digits and '-'
 *   title TEXT
 *   lang LANG              the template's main language (for the catalogue)
 *   audience TEXT          who it is for
 *   safety none|advisory|settlement
 *   summary TEXT
 *   inputs FILE            NAME = VALUE lines, optional
 *   step NAME LANG FILE [gate] [advisory] [option | key=value ...]
 *   draft FILE TEXT        what the local AI may draft, and how
 *
 * The run: inputs are read; then each step in order gets its file with
 * ${name} replaced by the input or earlier output of that name, and runs.
 */
#include "zp_internal.h"

#include <zxv_api.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------ buffers */

void zp_buf_put(zp_buf *b, const char *s, size_t n) {
    if (b->oom) return;
    if (b->len + n + 1u > b->cap) {
        size_t nc = b->cap ? b->cap : 256u;
        char *np;
        while (nc < b->len + n + 1u) nc *= 2u;
        np = (char *) realloc(b->p, nc);
        if (!np) {
            b->oom = 1;
            return;
        }
        b->p = np;
        b->cap = nc;
    }
    if (n) memcpy(b->p + b->len, s, n);
    b->len += n;
    b->p[b->len] = '\0';
}

void zp_buf_puts(zp_buf *b, const char *s) { zp_buf_put(b, s, strlen(s)); }

void zp_buf_printf(zp_buf *b, const char *fmt, ...) {
    char tmp[512];
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if ((size_t) n < sizeof tmp) {
        zp_buf_put(b, tmp, (size_t) n);
    } else {
        char *big = (char *) malloc((size_t) n + 1u);
        if (!big) {
            b->oom = 1;
            return;
        }
        va_start(ap, fmt);
        vsnprintf(big, (size_t) n + 1u, fmt, ap);
        va_end(ap);
        zp_buf_put(b, big, (size_t) n);
        free(big);
    }
}

void zp_buf_json(zp_buf *b, const char *s, size_t n) {
    static const char hx[] = "0123456789abcdef";
    zp_buf_put(b, "\"", 1);
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char) s[i];
        char e[7];
        if (c == '"' || c == '\\') {
            e[0] = '\\';
            e[1] = (char) c;
            zp_buf_put(b, e, 2);
        } else if (c == '\n') {
            zp_buf_put(b, "\\n", 2);
        } else if (c == '\t') {
            zp_buf_put(b, "\\t", 2);
        } else if (c < 0x20u || c == 0x7fu) {
            e[0] = '\\';
            e[1] = 'u';
            e[2] = '0';
            e[3] = '0';
            e[4] = hx[c >> 4];
            e[5] = hx[c & 15u];
            zp_buf_put(b, e, 6);
        } else {
            zp_buf_put(b, (const char *) &s[i], 1);
        }
    }
    zp_buf_put(b, "\"", 1);
}

static void jstr(zp_buf *b, const char *s) { zp_buf_json(b, s, strlen(s)); }

void zp_buf_free(zp_buf *b) {
    free(b->p);
    b->p = NULL;
    b->len = b->cap = 0;
}

static char *dup_n(const char *s, size_t n) {
    char *d = (char *) malloc(n + 1u);
    if (!d) return NULL;
    memcpy(d, s, n);
    d[n] = '\0';
    return d;
}

static char *dup_s(const char *s) { return dup_n(s, strlen(s)); }

void zp_line_col(const char *text, size_t len, size_t off, uint32_t *line, uint32_t *col) {
    uint32_t l = 1, c = 1;
    if (off > len) off = len;
    for (size_t i = 0; i < off; i++) {
        if (text[i] == '\n') {
            l++;
            c = 1;
        } else {
            c++;
        }
    }
    *line = l;
    *col = c;
}

/* ------------------------------------------------------------ languages */

typedef struct {
    const char *name;
    zp_lang_fn fn;
} lang_t;

static const lang_t g_langs[] = {
    {"treaty", zp_lang_treaty}, {"sutra", zp_lang_sutra}, {"trebago", zp_lang_trebago},
    {"eve", zp_lang_eve},       {"kron", zp_lang_kron},   {"myra", zp_lang_myra},
};
#define N_LANGS (sizeof g_langs / sizeof g_langs[0])

uint32_t zp_lang_count(void) { return (uint32_t) N_LANGS; }

const char *zp_lang_name(uint32_t index) { return index < N_LANGS ? g_langs[index].name : NULL; }

static zp_lang_fn find_lang(const char *name) {
    for (size_t i = 0; i < N_LANGS; i++)
        if (strcmp(g_langs[i].name, name) == 0) return g_langs[i].fn;
    return NULL;
}

const char *zp_status_name(int32_t status) {
    switch (status) {
    case ZP_OK: return "ZP_OK";
    case ZP_E_ARG: return "ZP_E_ARG";
    case ZP_E_NOMEM: return "ZP_E_NOMEM";
    case ZP_E_MANIFEST: return "ZP_E_MANIFEST";
    case ZP_E_FILE: return "ZP_E_FILE";
    case ZP_E_NOTFOUND: return "ZP_E_NOTFOUND";
    case ZP_E_LIMIT: return "ZP_E_LIMIT";
    case ZP_E_IO: return "ZP_E_IO";
    default: return zxv_status_name(status);
    }
}

/* ------------------------------------------------------------ templates */

uint32_t zp_template_count(void) { return zp_builtin_template_count; }

const zp_template *zp_template_at(uint32_t index) {
    return index < zp_builtin_template_count ? &zp_builtin_templates[index] : NULL;
}

const zp_template *zp_template_find(const char *id) {
    if (!id) return NULL;
    for (uint32_t i = 0; i < zp_builtin_template_count; i++)
        if (strcmp(zp_builtin_templates[i].id, id) == 0) return &zp_builtin_templates[i];
    return NULL;
}

const zp_file *zp_template_file(const zp_template *t, const char *name) {
    if (!t || !name) return NULL;
    for (uint32_t i = 0; i < t->n_files; i++)
        if (strcmp(t->files[i].name, name) == 0) return &t->files[i];
    return NULL;
}

/* ------------------------------------------------------------ manifest */

#define MAX_OPTS 8u

typedef struct {
    char name[ZP_MAX_NAME + 1];
    char lang[16];
    char file[ZP_MAX_NAME + 1];
    int gate, advisory;
    char okey[MAX_OPTS][24];
    char oval[MAX_OPTS][ZP_MAX_NAME + 1];
    uint32_t n_opt;
} step_def;

typedef struct {
    char id[ZP_MAX_NAME + 1];
    char title[128];
    char lang[16];
    char audience[128];
    char safety[16];
    char summary[512];
    char inputs[ZP_MAX_NAME + 1];
    char draft_file[ZP_MAX_NAME + 1];
    char draft_text[1024];
    step_def step[ZP_MAX_STEPS];
    uint32_t n_step;
} manifest;

static void copy_field(char *dst, size_t cap, const char *s, size_t n) {
    if (n >= cap) n = cap - 1u;
    memcpy(dst, s, n);
    dst[n] = '\0';
}

static int is_space(char c) { return c == ' ' || c == '\t' || c == '\r'; }

/* Parse the manifest. On failure writes the line and reason to `why`. */
static int32_t parse_manifest(const char *text, size_t len, manifest *m, char *why, size_t whycap) {
    size_t i = 0;
    uint32_t line = 0;
    memset(m, 0, sizeof *m);
    strcpy(m->safety, "none");
    while (i < len) {
        size_t e = i, ks, ke, vs;
        char key[16];
        line++;
        while (e < len && text[e] != '\n') e++;
        {
            size_t le = e;
            while (le > i && is_space(text[le - 1])) le--;
            ks = i;
            while (ks < le && is_space(text[ks])) ks++;
            if (ks == le || text[ks] == '#') {
                i = e + 1u;
                continue;
            }
            ke = ks;
            while (ke < le && !is_space(text[ke])) ke++;
            vs = ke;
            while (vs < le && is_space(text[vs])) vs++;
            copy_field(key, sizeof key, text + ks, ke - ks);
            if (strcmp(key, "id") == 0) {
                copy_field(m->id, sizeof m->id, text + vs, le - vs);
            } else if (strcmp(key, "title") == 0) {
                copy_field(m->title, sizeof m->title, text + vs, le - vs);
            } else if (strcmp(key, "lang") == 0) {
                copy_field(m->lang, sizeof m->lang, text + vs, le - vs);
            } else if (strcmp(key, "audience") == 0) {
                copy_field(m->audience, sizeof m->audience, text + vs, le - vs);
            } else if (strcmp(key, "safety") == 0) {
                copy_field(m->safety, sizeof m->safety, text + vs, le - vs);
                if (strcmp(m->safety, "none") && strcmp(m->safety, "advisory") &&
                    strcmp(m->safety, "settlement")) {
                    snprintf(why, whycap, "template.zxv line %u: safety must be none, advisory or settlement", line);
                    return ZP_E_MANIFEST;
                }
            } else if (strcmp(key, "summary") == 0) {
                copy_field(m->summary, sizeof m->summary, text + vs, le - vs);
            } else if (strcmp(key, "inputs") == 0) {
                copy_field(m->inputs, sizeof m->inputs, text + vs, le - vs);
            } else if (strcmp(key, "draft") == 0) {
                size_t fe = vs, ts;
                while (fe < le && !is_space(text[fe])) fe++;
                ts = fe;
                while (ts < le && is_space(text[ts])) ts++;
                copy_field(m->draft_file, sizeof m->draft_file, text + vs, fe - vs);
                copy_field(m->draft_text, sizeof m->draft_text, text + ts, le - ts);
            } else if (strcmp(key, "step") == 0) {
                step_def *sd;
                char tok[4][ZP_MAX_NAME + 1];
                uint32_t nt = 0;
                size_t p = vs;
                if (m->n_step >= ZP_MAX_STEPS) {
                    snprintf(why, whycap, "template.zxv line %u: more than %u steps", line, ZP_MAX_STEPS);
                    return ZP_E_LIMIT;
                }
                sd = &m->step[m->n_step];
                while (p < le) {
                    size_t te = p;
                    while (te < le && !is_space(text[te])) te++;
                    if (nt < 3) {
                        copy_field(tok[nt], sizeof tok[nt], text + p, te - p);
                        nt++;
                    } else {
                        char t[ZP_MAX_NAME + 1];
                        char *eq;
                        copy_field(t, sizeof t, text + p, te - p);
                        eq = strchr(t, '=');
                        if (strcmp(t, "gate") == 0) {
                            sd->gate = 1;
                        } else if (strcmp(t, "advisory") == 0) {
                            sd->advisory = 1;
                        } else if (eq && eq != t && sd->n_opt < MAX_OPTS) {
                            *eq = '\0';
                            copy_field(sd->okey[sd->n_opt], sizeof sd->okey[0], t, strlen(t));
                            copy_field(sd->oval[sd->n_opt], sizeof sd->oval[0], eq + 1, strlen(eq + 1));
                            sd->n_opt++;
                        } else if (!eq && sd->n_opt < MAX_OPTS && strlen(t) < sizeof sd->okey[0]) {
                            copy_field(sd->okey[sd->n_opt], sizeof sd->okey[0], t, strlen(t));
                            strcpy(sd->oval[sd->n_opt], "1");
                            sd->n_opt++;
                        } else {
                            snprintf(why, whycap, "template.zxv line %u: bad step option '%s'", line, t);
                            return ZP_E_MANIFEST;
                        }
                    }
                    p = te;
                    while (p < le && is_space(text[p])) p++;
                }
                if (nt < 3) {
                    snprintf(why, whycap, "template.zxv line %u: step needs NAME LANG FILE", line);
                    return ZP_E_MANIFEST;
                }
                copy_field(sd->name, sizeof sd->name, tok[0], strlen(tok[0]));
                copy_field(sd->lang, sizeof sd->lang, tok[1], strlen(tok[1]));
                copy_field(sd->file, sizeof sd->file, tok[2], strlen(tok[2]));
                if (!find_lang(sd->lang)) {
                    snprintf(why, whycap, "template.zxv line %u: unknown language '%s'", line, sd->lang);
                    return ZP_E_MANIFEST;
                }
                m->n_step++;
            } else {
                snprintf(why, whycap, "template.zxv line %u: unknown directive '%s'", line, key);
                return ZP_E_MANIFEST;
            }
        }
        i = e + 1u;
    }
    if (!m->id[0] || !m->title[0]) {
        snprintf(why, whycap, "template.zxv: id and title are required");
        return ZP_E_MANIFEST;
    }
    if (m->n_step == 0) {
        snprintf(why, whycap, "template.zxv: no steps");
        return ZP_E_MANIFEST;
    }
    return ZP_OK;
}

/* ------------------------------------------------------------ the run */

typedef struct {
    char *name, *value;
} kv_t;

typedef struct {
    kv_t *v;
    uint32_t n, cap;
} env_t;

static int env_set(env_t *e, const char *name, const char *value) {
    char *nv = dup_s(value);
    if (!nv) return 0;
    for (uint32_t i = 0; i < e->n; i++)
        if (strcmp(e->v[i].name, name) == 0) {
            free(e->v[i].value);
            e->v[i].value = nv;
            return 1;
        }
    if (e->n == e->cap) {
        uint32_t nc = e->cap ? e->cap * 2u : 16u;
        kv_t *np = (kv_t *) realloc(e->v, nc * sizeof *np);
        if (!np) {
            free(nv);
            return 0;
        }
        e->v = np;
        e->cap = nc;
    }
    e->v[e->n].name = dup_s(name);
    if (!e->v[e->n].name) {
        free(nv);
        return 0;
    }
    e->v[e->n].value = nv;
    e->n++;
    return 1;
}

static const char *env_get(const env_t *e, const char *name) {
    for (uint32_t i = 0; i < e->n; i++)
        if (strcmp(e->v[i].name, name) == 0) return e->v[i].value;
    return NULL;
}

static void env_free(env_t *e) {
    for (uint32_t i = 0; i < e->n; i++) {
        free(e->v[i].name);
        free(e->v[i].value);
    }
    free(e->v);
    memset(e, 0, sizeof *e);
}

struct zp_step {
    const step_def *def;
    env_t *env;
    const zp_file *files;
    uint32_t n_files;
    int32_t verdict;
    int verdict_set; /* a FALSE/BOTH/NEITHER was reported */
    zp_buf outputs, truths, notes, held, pending;
    uint32_t n_out, n_truth, n_note, n_held, n_pending;
    int has_error;
    int32_t err_status;
    char err_name[48];
    uint32_t err_line, err_col;
    char err_msg[512];
    char first_held[256], first_pending[256];
    int oom;
};

/* Verdict severity for folding: FALSE > BOTH > NEITHER > TRUE. */
static int severity(int32_t lp) {
    switch (lp) {
    case ZXV_LP_FALSE: return 3;
    case ZXV_LP_BOTH: return 2;
    case ZXV_LP_NEITHER: return 1;
    default: return 0;
    }
}

static void raise_verdict(zp_step *s, int32_t lp) {
    if (severity(lp) > severity(s->verdict)) s->verdict = lp;
    if (lp != ZXV_LP_TRUE) s->verdict_set = 1;
}

const char *zp_step_name(const zp_step *s) { return s->def->name; }

const char *zp_step_opt(const zp_step *s, const char *key) {
    for (uint32_t i = 0; i < s->def->n_opt; i++)
        if (strcmp(s->def->okey[i], key) == 0) return s->def->oval[i];
    return NULL;
}

const zp_file *zp_step_file(const zp_step *s, const char *name) {
    for (uint32_t i = 0; i < s->n_files; i++)
        if (strcmp(s->files[i].name, name) == 0) return &s->files[i];
    return NULL;
}

const char *zp_input(const zp_step *s, const char *name) { return env_get(s->env, name); }

uint32_t zp_input_count(const zp_step *s) { return s->env->n; }

void zp_input_at(const zp_step *s, uint32_t i, const char **name, const char **value) {
    *name = i < s->env->n ? s->env->v[i].name : NULL;
    *value = i < s->env->n ? s->env->v[i].value : NULL;
}

void zp_output(zp_step *s, const char *name, const char *text) {
    if (s->n_out++) zp_buf_puts(&s->outputs, ",");
    zp_buf_puts(&s->outputs, "{\"name\":");
    jstr(&s->outputs, name);
    zp_buf_puts(&s->outputs, ",\"value\":");
    jstr(&s->outputs, text);
    zp_buf_puts(&s->outputs, "}");
    if (!env_set(s->env, name, text)) s->oom = 1;
}

void zp_truths(zp_step *s, const char *name, const int32_t *lp, uint32_t n) {
    zp_buf txt = {0};
    if (s->n_truth++) zp_buf_puts(&s->truths, ",");
    zp_buf_puts(&s->truths, "{\"name\":");
    jstr(&s->truths, name);
    zp_buf_puts(&s->truths, ",\"values\":[");
    for (uint32_t i = 0; i < n; i++) {
        if (i) {
            zp_buf_puts(&s->truths, ",");
            zp_buf_puts(&txt, " ");
        }
        jstr(&s->truths, zxv_lp_name(lp[i]));
        zp_buf_puts(&txt, zxv_lp_name(lp[i]));
        raise_verdict(s, lp[i] == ZXV_LP_FALSE ? ZXV_LP_TRUE : lp[i]);
    }
    zp_buf_puts(&s->truths, "]}");
    /* A FALSE element is an answer ("no"), not a refusal: only BOTH holds
     * the step and NEITHER leaves it undecided. */
    {
        uint32_t nb = 0, nn = 0;
        for (uint32_t i = 0; i < n; i++) {
            if (lp[i] == ZXV_LP_BOTH) nb++;
            if (lp[i] == ZXV_LP_NEITHER) nn++;
        }
        if (nb) zp_notef(s, "%s: %u of %u held as BOTH (contradictory evidence) for review", name, nb, n);
        if (nn) zp_notef(s, "%s: %u of %u NEITHER (no evidence yet)", name, nn, n);
    }
    if (txt.p && !env_set(s->env, name, txt.p)) s->oom = 1;
    if (txt.oom) s->oom = 1;
    zp_buf_free(&txt);
}

void zp_refuse(zp_step *s, int32_t status, const char *status_name, uint32_t line, uint32_t col,
               const char *why) {
    raise_verdict(s, ZXV_LP_FALSE);
    if (s->has_error) return; /* keep the first */
    s->has_error = 1;
    s->err_status = status;
    snprintf(s->err_name, sizeof s->err_name, "%s", status_name ? status_name : zp_status_name(status));
    s->err_line = line;
    s->err_col = col;
    snprintf(s->err_msg, sizeof s->err_msg, "%s", why ? why : "");
}

static void list_add(zp_buf *b, uint32_t *n, const char *text) {
    if ((*n)++) zp_buf_puts(b, ",");
    jstr(b, text);
}

void zp_hold(zp_step *s, const char *why) {
    raise_verdict(s, ZXV_LP_BOTH);
    if (!s->first_held[0]) snprintf(s->first_held, sizeof s->first_held, "%s", why);
    list_add(&s->held, &s->n_held, why);
}

void zp_pending(zp_step *s, const char *why) {
    raise_verdict(s, ZXV_LP_NEITHER);
    if (!s->first_pending[0]) snprintf(s->first_pending, sizeof s->first_pending, "%s", why);
    list_add(&s->pending, &s->n_pending, why);
}

void zp_note(zp_step *s, const char *text) { list_add(&s->notes, &s->n_note, text); }

void zp_notef(zp_step *s, const char *fmt, ...) {
    char tmp[768];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    zp_note(s, tmp);
}

static void step_free(zp_step *s) {
    zp_buf_free(&s->outputs);
    zp_buf_free(&s->truths);
    zp_buf_free(&s->notes);
    zp_buf_free(&s->held);
    zp_buf_free(&s->pending);
}

static int step_oom(const zp_step *s) {
    return s->oom || s->outputs.oom || s->truths.oom || s->notes.oom || s->held.oom || s->pending.oom;
}

/* Replace ${name}. A missing name leaves the step undecided (NEITHER). */
static char *substitute(zp_step *s, const char *src, size_t len, size_t *out_len, int *missing) {
    zp_buf b = {0};
    size_t i = 0;
    *missing = 0;
    while (i < len) {
        if (src[i] == '$' && i + 1u < len && src[i + 1u] == '{') {
            size_t e = i + 2u;
            while (e < len && src[e] != '}' && src[e] != '\n') e++;
            if (e < len && src[e] == '}' && e - (i + 2u) <= ZP_MAX_NAME) {
                char name[ZP_MAX_NAME + 1];
                const char *v;
                copy_field(name, sizeof name, src + i + 2u, e - (i + 2u));
                v = env_get(s->env, name);
                if (v) {
                    zp_buf_puts(&b, v);
                } else {
                    char why[160];
                    snprintf(why, sizeof why, "needs a value for ${%s}: add it to the inputs or to an earlier step", name);
                    zp_pending(s, why);
                    *missing = 1;
                }
                i = e + 1u;
                continue;
            }
        }
        zp_buf_put(&b, src + i, 1);
        i++;
    }
    if (!b.p) zp_buf_put(&b, "", 0);
    if (b.oom) {
        zp_buf_free(&b);
        return NULL;
    }
    *out_len = b.len;
    return b.p;
}

/* NAME = VALUE lines into the environment. Bad lines are reported. */
static void read_inputs(env_t *env, const char *text, size_t len, zp_buf *errs, uint32_t *nerr) {
    size_t i = 0;
    uint32_t line = 0;
    while (i < len) {
        size_t e = i, a, b, eq;
        line++;
        while (e < len && text[e] != '\n') e++;
        a = i;
        while (a < e && is_space(text[a])) a++;
        b = e;
        while (b > a && is_space(text[b - 1])) b--;
        if (a < b && text[a] != '#') {
            eq = a;
            while (eq < b && text[eq] != '=') eq++;
            if (eq == b) {
                char m[96];
                snprintf(m, sizeof m, "inputs line %u: expected NAME = VALUE", line);
                list_add(errs, nerr, m);
            } else {
                size_t ne = eq, vs = eq + 1u;
                char name[ZP_MAX_NAME + 1];
                char *val;
                while (ne > a && is_space(text[ne - 1])) ne--;
                while (vs < b && is_space(text[vs])) vs++;
                copy_field(name, sizeof name, text + a, ne - a);
                val = dup_n(text + vs, b - vs);
                if (val) {
                    env_set(env, name, val);
                    free(val);
                }
            }
        }
        i = e + 1u;
    }
}

static const char *verdict_word(int32_t lp) { return zxv_lp_name(lp); }

typedef struct {
    zp_file f[ZP_MAX_FILES];
    uint32_t n;
} fileset;

static int32_t merge_files(const zp_template *t, const zp_file *ov, uint32_t nov, fileset *fs) {
    fs->n = 0;
    for (uint32_t i = 0; t && i < t->n_files; i++) fs->f[fs->n++] = t->files[i];
    for (uint32_t i = 0; i < nov; i++) {
        uint32_t k;
        if (!ov[i].name || !ov[i].text || strchr(ov[i].name, '/') || strchr(ov[i].name, '\\')) return ZP_E_ARG;
        if (ov[i].len > ZP_MAX_FILE) return ZP_E_LIMIT;
        for (k = 0; k < fs->n; k++)
            if (strcmp(fs->f[k].name, ov[i].name) == 0) break;
        if (k == fs->n) {
            if (fs->n >= ZP_MAX_FILES) return ZP_E_LIMIT;
            fs->n++;
        }
        fs->f[k] = ov[i];
    }
    return ZP_OK;
}

static const zp_file *fs_get(const fileset *fs, const char *name) {
    for (uint32_t i = 0; i < fs->n; i++)
        if (strcmp(fs->f[i].name, name) == 0) return &fs->f[i];
    return NULL;
}

static void emit_step(zp_buf *o, const zp_step *s, int ran) {
    const step_def *d = s->def;
    zp_buf_puts(o, "{\"name\":");
    jstr(o, d->name);
    zp_buf_puts(o, ",\"lang\":");
    jstr(o, d->lang);
    zp_buf_puts(o, ",\"file\":");
    jstr(o, d->file);
    zp_buf_printf(o, ",\"gate\":%s,\"advisory\":%s,\"ran\":%s,\"verdict\":", d->gate ? "true" : "false",
                  d->advisory ? "true" : "false", ran ? "true" : "false");
    jstr(o, verdict_word(s->verdict));
    zp_buf_puts(o, ",\"outputs\":[");
    if (s->outputs.p) zp_buf_puts(o, s->outputs.p);
    zp_buf_puts(o, "],\"truths\":[");
    if (s->truths.p) zp_buf_puts(o, s->truths.p);
    zp_buf_puts(o, "],\"held\":[");
    if (s->held.p) zp_buf_puts(o, s->held.p);
    zp_buf_puts(o, "],\"pending\":[");
    if (s->pending.p) zp_buf_puts(o, s->pending.p);
    zp_buf_puts(o, "],\"notes\":[");
    if (s->notes.p) zp_buf_puts(o, s->notes.p);
    zp_buf_puts(o, "]");
    if (s->has_error) {
        zp_buf_printf(o, ",\"error\":{\"status\":%d,\"name\":", (int) s->err_status);
        jstr(o, s->err_name);
        zp_buf_printf(o, ",\"line\":%u,\"col\":%u,\"message\":", s->err_line, s->err_col);
        jstr(o, s->err_msg);
        zp_buf_puts(o, "}");
    }
    zp_buf_puts(o, "}");
}

static int32_t finish(zp_buf *o, char **out, size_t *out_len) {
    if (o->oom) {
        zp_buf_free(o);
        return ZP_E_NOMEM;
    }
    *out = o->p;
    if (out_len) *out_len = o->len;
    return ZP_OK;
}

/* A result for a template that could not even start. */
static int32_t emit_refusal(const char *id, int32_t status, const char *why, char **out, size_t *out_len) {
    zp_buf o = {0};
    zp_buf_printf(&o, "{\"playground\":\"%s\",\"template\":", ZP_VERSION);
    jstr(&o, id ? id : "");
    zp_buf_puts(&o, ",\"verdict\":\"FALSE\",\"headline\":");
    jstr(&o, why);
    zp_buf_printf(&o, ",\"status\":%d,\"status_name\":", (int) status);
    jstr(&o, zp_status_name(status));
    zp_buf_puts(&o, ",\"approval\":\"not required\",\"steps\":[]}\n");
    return finish(&o, out, out_len);
}

static int32_t run_manifest(const manifest *m, const fileset *fs, const char *extra_inputs,
                            size_t extra_len, char **out, size_t *out_len) {
    env_t env = {0};
    zp_buf o = {0}, ierr = {0}, steps = {0};
    uint32_t nierr = 0;
    int32_t run_verdict = ZXV_LP_TRUE;
    int blocked = 0, needs_approval = 0, any_settle = 0;
    char headline[640] = "";
    int32_t rc;

    if (m->inputs[0]) {
        const zp_file *inf = fs_get(fs, m->inputs);
        if (!inf) {
            char why[160];
            snprintf(why, sizeof why, "the inputs file %s is missing", m->inputs);
            return emit_refusal(m->id, ZP_E_FILE, why, out, out_len);
        }
        read_inputs(&env, inf->text, inf->len, &ierr, &nierr);
    }
    if (extra_inputs) read_inputs(&env, extra_inputs, extra_len, &ierr, &nierr);

    for (uint32_t k = 0; k < m->n_step; k++) {
        const step_def *d = &m->step[k];
        zp_step s;
        const zp_file *f = fs_get(fs, d->file);
        int ran = 0;
        memset(&s, 0, sizeof s);
        s.def = d;
        s.env = &env;
        s.files = fs->f;
        s.n_files = fs->n;
        s.verdict = ZXV_LP_TRUE;
        if (!f) {
            char why[160];
            snprintf(why, sizeof why, "the file %s is missing", d->file);
            zp_refuse(&s, ZP_E_FILE, NULL, 0, 0, why);
        } else if (d->gate && blocked) {
            zp_pending(&s, "not run: an earlier step was refused, held or undecided, and this step is gated");
        } else {
            int missing = 0;
            size_t sl = 0;
            char *src = substitute(&s, f->text, f->len, &sl, &missing);
            if (!src) {
                s.oom = 1;
            } else if (!missing) {
                find_lang(d->lang)(&s, src, sl);
                ran = 1;
            }
            free(src);
        }
        if (step_oom(&s)) {
            step_free(&s);
            zp_buf_free(&steps);
            zp_buf_free(&ierr);
            env_free(&env);
            return ZP_E_NOMEM;
        }
        if (d->advisory && ran && s.verdict == ZXV_LP_TRUE) {
            needs_approval = 1;
            zp_note(&s, "ADVISORY: this is a proposal. Nothing was actuated; a person must review and approve it.");
        }
        if (strcmp(d->lang, "treaty") == 0 && ran) any_settle = 1;
        if (s.verdict != ZXV_LP_TRUE) blocked = 1;
        if (severity(s.verdict) > severity(run_verdict)) {
            run_verdict = s.verdict;
            if (s.verdict == ZXV_LP_FALSE)
                snprintf(headline, sizeof headline, "Refused at step '%s': %s", d->name,
                         s.err_msg[0] ? s.err_msg : "a rule was broken");
            else if (s.verdict == ZXV_LP_BOTH)
                snprintf(headline, sizeof headline, "Held for review at step '%s': %s%s", d->name,
                         s.first_held[0] ? s.first_held : "contradictory evidence",
                         s.n_held > 1 ? " (and more, see held)" : "");
            else
                snprintf(headline, sizeof headline, "Undecided at step '%s': %s", d->name,
                         s.first_pending[0] ? s.first_pending : "more information is needed");
        }
        if (k) zp_buf_puts(&steps, ",");
        emit_step(&steps, &s, ran);
        step_free(&s);
    }

    if (run_verdict == ZXV_LP_TRUE)
        snprintf(headline, sizeof headline, "%s", needs_approval ? "All checks passed. Proposal ready for a person to approve."
                                                                  : "All checks passed.");
    zp_buf_printf(&o, "{\"playground\":\"%s\",\"template\":", ZP_VERSION);
    jstr(&o, m->id);
    zp_buf_puts(&o, ",\"title\":");
    jstr(&o, m->title);
    zp_buf_puts(&o, ",\"verdict\":");
    jstr(&o, verdict_word(run_verdict));
    zp_buf_puts(&o, ",\"headline\":");
    jstr(&o, headline);
    zp_buf_puts(&o, ",\"safety\":");
    jstr(&o, m->safety);
    zp_buf_printf(&o, ",\"approval\":\"%s\"", needs_approval || run_verdict == ZXV_LP_BOTH ? "required" : "not required");
    if (any_settle) zp_buf_puts(&o, ",\"ledger\":\"scratch engine (simulation); the owner's books were not touched\"");
    zp_buf_puts(&o, ",\"input_errors\":[");
    if (ierr.p) zp_buf_puts(&o, ierr.p);
    zp_buf_puts(&o, "],\"steps\":[");
    if (steps.p) zp_buf_puts(&o, steps.p);
    zp_buf_puts(&o, "]}\n");
    if (steps.oom || ierr.oom) o.oom = 1;
    zp_buf_free(&steps);
    zp_buf_free(&ierr);
    env_free(&env);
    rc = finish(&o, out, out_len);
    return rc;
}

int32_t zp_run(const zp_template *t, const zp_file *files, uint32_t n_files, char **out, size_t *out_len) {
    fileset fs;
    manifest m;
    const zp_file *mf;
    char why[256];
    int32_t st;
    if (!out || (!t && !n_files)) return ZP_E_ARG;
    *out = NULL;
    st = merge_files(t, files, n_files, &fs);
    if (st != ZP_OK) return emit_refusal(t ? t->id : "", st, "the edited files could not be used", out, out_len);
    mf = fs_get(&fs, "template.zxv");
    if (!mf) return emit_refusal(t ? t->id : "", ZP_E_MANIFEST, "template.zxv is missing", out, out_len);
    st = parse_manifest(mf->text, mf->len, &m, why, sizeof why);
    if (st != ZP_OK) return emit_refusal(t ? t->id : "", st, why, out, out_len);
    return run_manifest(&m, &fs, NULL, 0, out, out_len);
}

int32_t zp_run_one(const char *lang, const char *src, size_t len, const char *inputs, size_t inputs_len,
                   char **out, size_t *out_len) {
    manifest m;
    fileset fs;
    if (!lang || !src || !out) return ZP_E_ARG;
    *out = NULL;
    if (!find_lang(lang)) return emit_refusal("", ZP_E_ARG, "unknown language", out, out_len);
    if (len > ZP_MAX_FILE) return emit_refusal("", ZP_E_LIMIT, "the source is too long", out, out_len);
    memset(&m, 0, sizeof m);
    strcpy(m.id, "scratch");
    strcpy(m.title, "Scratch run");
    strcpy(m.safety, "none");
    copy_field(m.lang, sizeof m.lang, lang, strlen(lang));
    m.n_step = 1;
    strcpy(m.step[0].name, "run");
    copy_field(m.step[0].lang, sizeof m.step[0].lang, lang, strlen(lang));
    strcpy(m.step[0].file, "source");
    fs.n = 1;
    fs.f[0].name = "source";
    fs.f[0].text = src;
    fs.f[0].len = len;
    return run_manifest(&m, &fs, inputs, inputs ? inputs_len : 0, out, out_len);
}

/* ------------------------------------------------------------ catalogue */

int32_t zp_catalogue_json(char **out, size_t *out_len) {
    zp_buf o = {0};
    if (!out) return ZP_E_ARG;
    zp_buf_puts(&o, "[");
    for (uint32_t i = 0; i < zp_builtin_template_count; i++) {
        const zp_template *t = &zp_builtin_templates[i];
        const zp_file *mf = zp_template_file(t, "template.zxv");
        manifest m;
        char why[256];
        if (!mf || parse_manifest(mf->text, mf->len, &m, why, sizeof why) != ZP_OK) continue;
        if (o.len > 1) zp_buf_puts(&o, ",");
        zp_buf_puts(&o, "{\"id\":");
        jstr(&o, m.id);
        zp_buf_puts(&o, ",\"title\":");
        jstr(&o, m.title);
        zp_buf_puts(&o, ",\"lang\":");
        jstr(&o, m.lang);
        zp_buf_puts(&o, ",\"audience\":");
        jstr(&o, m.audience);
        zp_buf_puts(&o, ",\"safety\":");
        jstr(&o, m.safety);
        zp_buf_puts(&o, ",\"summary\":");
        jstr(&o, m.summary);
        zp_buf_puts(&o, ",\"steps\":[");
        for (uint32_t k = 0; k < m.n_step; k++) {
            if (k) zp_buf_puts(&o, ",");
            zp_buf_puts(&o, "{\"name\":");
            jstr(&o, m.step[k].name);
            zp_buf_puts(&o, ",\"lang\":");
            jstr(&o, m.step[k].lang);
            zp_buf_puts(&o, ",\"file\":");
            jstr(&o, m.step[k].file);
            zp_buf_puts(&o, "}");
        }
        zp_buf_puts(&o, "],\"files\":[");
        for (uint32_t k = 0; k < t->n_files; k++) {
            if (k) zp_buf_puts(&o, ",");
            zp_buf_puts(&o, "{\"name\":");
            jstr(&o, t->files[k].name);
            zp_buf_puts(&o, ",\"text\":");
            zp_buf_json(&o, t->files[k].text, t->files[k].len);
            zp_buf_puts(&o, "}");
        }
        zp_buf_puts(&o, "]");
        if (m.draft_file[0]) {
            zp_buf_puts(&o, ",\"draft\":{\"file\":");
            jstr(&o, m.draft_file);
            zp_buf_puts(&o, ",\"instruction\":");
            jstr(&o, m.draft_text);
            zp_buf_puts(&o, "}");
        }
        zp_buf_puts(&o, "}");
    }
    zp_buf_puts(&o, "]\n");
    return finish(&o, out, out_len);
}

/* ------------------------------------------------------------ requests */

static int read_len(const char **p, const char *end, size_t *out) {
    size_t v = 0;
    int any = 0;
    while (*p < end && **p >= '0' && **p <= '9') {
        if (v > ZP_MAX_FILE) return 0;
        v = v * 10u + (size_t) (**p - '0');
        (*p)++;
        any = 1;
    }
    *out = v;
    return any;
}

int32_t zp_request_parse(const char *body, size_t len, zp_request *r) {
    const char *p = body, *end = body + len;
    if (!body || !r) return ZP_E_ARG;
    memset(r, 0, sizeof *r);
    while (p < end) {
        const char *ls = p;
        while (p < end && *p != ' ' && *p != '\n') p++;
        if ((size_t) (p - ls) == 8 && memcmp(ls, "template", 8) == 0 && p < end && *p == ' ') {
            const char *v = ++p;
            while (p < end && *p != '\n') p++;
            if ((size_t) (p - v) > ZP_MAX_NAME) return ZP_E_LIMIT;
            memcpy(r->template_id, v, (size_t) (p - v));
            r->template_id[p - v] = '\0';
            if (p < end) p++;
        } else if ((size_t) (p - ls) == 7 && memcmp(ls, "request", 7) == 0 && p < end && *p == ' ') {
            size_t n;
            p++;
            if (!read_len(&p, end, &n) || p >= end || *p != '\n' || (size_t) (end - p - 1) < n) return ZP_E_ARG;
            r->request = p + 1;
            r->request_len = n;
            p += 1 + n;
            if (p < end && *p == '\n') p++;
        } else if ((size_t) (p - ls) == 4 && memcmp(ls, "file", 4) == 0 && p < end && *p == ' ') {
            const char *ns = ++p;
            size_t n, nl;
            while (p < end && *p != ' ' && *p != '\n') p++;
            nl = (size_t) (p - ns);
            if (nl == 0 || nl > ZP_MAX_NAME || p >= end || *p != ' ') return ZP_E_ARG;
            p++;
            if (!read_len(&p, end, &n) || p >= end || *p != '\n' || (size_t) (end - p - 1) < n) return ZP_E_ARG;
            if (n > ZP_MAX_FILE) return ZP_E_LIMIT;
            if (r->n_files >= ZP_MAX_FILES) return ZP_E_LIMIT;
            memcpy(r->names[r->n_files], ns, nl);
            r->names[r->n_files][nl] = '\0';
            if (memchr(r->names[r->n_files], '/', nl) || memchr(r->names[r->n_files], '\\', nl)) return ZP_E_ARG;
            r->files[r->n_files].name = r->names[r->n_files];
            r->files[r->n_files].text = p + 1;
            r->files[r->n_files].len = n;
            r->n_files++;
            p += 1 + n;
            if (p < end && *p == '\n') p++;
        } else if (p == ls && p < end && *p == '\n') {
            p++;
        } else {
            return ZP_E_ARG;
        }
    }
    return ZP_OK;
}

/* ------------------------------------------------------------ AI drafts */

int32_t zp_draft_prompt(const zp_template *t, const zp_file *files, uint32_t n_files, const char *request,
                        char **out, size_t *out_len) {
    fileset fs;
    manifest m;
    const zp_file *mf, *df;
    char why[256];
    const char *lang = NULL;
    zp_buf o = {0};
    int32_t st;
    if (!t || !out) return ZP_E_ARG;
    *out = NULL;
    if ((st = merge_files(t, files, n_files, &fs)) != ZP_OK) return st;
    mf = fs_get(&fs, "template.zxv");
    if (!mf) return ZP_E_MANIFEST;
    if ((st = parse_manifest(mf->text, mf->len, &m, why, sizeof why)) != ZP_OK) return st;
    if (!m.draft_file[0]) return ZP_E_NOTFOUND;
    for (uint32_t k = 0; k < m.n_step; k++)
        if (strcmp(m.step[k].file, m.draft_file) == 0) lang = m.step[k].lang;
    if (!lang) return ZP_E_MANIFEST;
    df = fs_get(&fs, m.draft_file);
    zp_buf_puts(&o, "You draft one file for the ZXV Playground. Reply with the file's text only, no commentary.\n");
    zp_buf_puts(&o, "Your draft is untrusted: it is parsed and checked exactly like text a person typed, and refused if it breaks a rule.\n\n");
    zp_buf_printf(&o, "Language: %s\n%s\n\n", lang, zp_lang_rules(lang));
    zp_buf_printf(&o, "Task: %s\n", m.draft_text);
    if (request && *request) zp_buf_printf(&o, "The user asks: %s\n", request);
    if (df) {
        zp_buf_printf(&o, "\nCurrent %s:\n", m.draft_file);
        zp_buf_put(&o, df->text, df->len);
        if (df->len && df->text[df->len - 1] != '\n') zp_buf_puts(&o, "\n");
    }
    return finish(&o, out, out_len);
}

const char *zp_lang_rules(const char *lang) {
    if (strcmp(lang, "treaty") == 0)
        return "A treaty: the line 'TREATY NAME ASSET NET|SETL', then 1-16 lines 'PARTY ID DEPOSIT CLAIM NUM/DEN', "
               "then 0-64 lines 'OWE FROM TO AMOUNT', then 'END'. IDs are 1-8 capital letters or digits. Amounts are "
               "whole minor units (cents). Each CLAIM is at most its DEPOSIT. The weights NUM/DEN are in lowest terms and "
               "add up to exactly 1. Lines starting with # are comments.";
    if (strcmp(lang, "kron") == 0)
        return "KRON: exact APL or K array expressions on rationals (1r3) and four-valued truth (true false neither both). "
               "Right to left, no precedence. K spelling: x:1 2 3; +/x; x%2. No floats, no exponent notation.";
    if (strcmp(lang, "trebago") == 0)
        return "Trebago: 'trebago NAME', declarations 'in x(n), y', 'out s', 'const c = 1/3', then index equations "
               "such as 's = sum(i, x(i) * y(i))' and 'end'. Exact rationals only.";
    if (strcmp(lang, "eve") == 0)
        return "Eve: 'eve NAME', then type lines: 'money = dec(15, 2)', 'level = 0 .. 100', 'mode = (off, low, high)', "
               "records 'name { field: type }'. Ranges are hard limits.";
    if (strcmp(lang, "myra") == 0)
        return "MYRA: one line per M global node: subscripts with types, then ^-piece fields with types.";
    if (strcmp(lang, "sutra") == 0)
        return "Sutra layout: 'record NAME' then indented 'field: picture' lines such as 'amount: s9(13)v99 comp-3'.";
    return "";
}
