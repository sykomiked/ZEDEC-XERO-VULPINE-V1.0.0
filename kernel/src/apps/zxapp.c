/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxapp.c — app manifests and the language-runtime registry (zxapp.h). */
#include "zxapp.h"

static const char *const LANG[ZXAPP_LANG_COUNT] = {"c", "sutra", "eve", "kron", "trebago", "myra"};

static bool seq(const char *a, const char *b)
{
    uint32_t i = 0;
    for (; a[i] && a[i] == b[i]; i++) {
    }
    return a[i] == b[i];
}

static void put_err(zxapp_t *a, uint32_t line, const char *m1, const char *m2)
{
    uint32_t n = 0;
    for (const char *p = m1; p && *p && n + 1u < ZXAPP_ERR_MAX; p++) a->err[n++] = *p;
    for (const char *p = m2; p && *p && n + 1u < ZXAPP_ERR_MAX; p++) a->err[n++] = *p;
    a->err[n] = 0;
    a->err_line = line;
}

const char *zxapp_lang_name(zxapp_lang_t l)
{
    return l >= 0 && l < ZXAPP_LANG_COUNT ? LANG[l] : "?";
}

zxapp_lang_t zxapp_lang_of(const char *name)
{
    for (int i = 0; i < ZXAPP_LANG_COUNT; i++)
        if (seq(name, LANG[i])) return (zxapp_lang_t) i;
    return ZXAPP_LANG_UNKNOWN;
}

/* ---- parsing --------------------------------------------------------------- */

typedef struct {
    const char *s;
    uint32_t n, at, line;
} rd_t;

/* The next line, without its newline (and a trailing CR). */
static bool next_line(rd_t *r, const char **p, uint32_t *len)
{
    if (r->at >= r->n) return false;
    uint32_t b = r->at;
    while (r->at < r->n && r->s[r->at] != '\n') r->at++;
    uint32_t e = r->at;
    if (r->at < r->n) r->at++;
    if (e > b && r->s[e - 1] == '\r') e--;
    *p = r->s + b;
    *len = e - b;
    r->line++;
    return true;
}

static bool space(char c)
{
    return c == ' ' || c == '\t';
}

/* Split a line into up to 4 words (the 4th takes the rest of the line,
 * trimmed). '#' outside a word starts a comment. Returns the count. */
static uint32_t words(const char *p, uint32_t n, const char *w[4], uint32_t wl[4])
{
    uint32_t k = 0, i = 0;
    while (k < 4) {
        while (i < n && space(p[i])) i++;
        if (i >= n || p[i] == '#') break;
        uint32_t b = i;
        if (k == 3) {
            uint32_t e = n;
            for (uint32_t j = i; j < n; j++)
                if (p[j] == '#' && space(p[j - 1])) {
                    e = j;
                    break;
                }
            while (e > b && space(p[e - 1])) e--;
            w[k] = p + b;
            wl[k++] = e - b;
            break;
        }
        while (i < n && !space(p[i])) i++;
        w[k] = p + b;
        wl[k++] = i - b;
    }
    return k;
}

static bool word_is(const char *w, uint32_t n, const char *lit)
{
    uint32_t i = 0;
    for (; i < n && lit[i] && w[i] == lit[i]; i++) {
    }
    return i == n && !lit[i];
}

static bool word_copy(char *d, uint32_t cap, const char *w, uint32_t n)
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

int zxapp_parse(zxapp_t *a, const char *text, uint32_t len)
{
    uint8_t *z = (uint8_t *) a;
    for (uint32_t i = 0; i < sizeof *a; i++) z[i] = 0;
    if (!text) {
        put_err(a, 0, "no manifest text", 0);
        return ZXAPP_E_ARG;
    }
    rd_t r = {text, len, 0, 0};
    const char *p;
    uint32_t n;
    bool header = false;
    while (next_line(&r, &p, &n)) {
        const char *w[4];
        uint32_t wl[4];
        uint32_t k = words(p, n, w, wl);
        if (!k) continue;
        if (!header) {
            if (k != 2 || !word_is(w[0], wl[0], "zxapp") || !word_is(w[1], wl[1], "1")) {
                put_err(a, r.line, "the first line must be \"zxapp 1\"", 0);
                return ZXAPP_E_SYNTAX;
            }
            header = true;
            continue;
        }
        if (word_is(w[0], wl[0], "id") || word_is(w[0], wl[0], "name") ||
            word_is(w[0], wl[0], "view") || word_is(w[0], wl[0], "model")) {
            if (k < 2) {
                put_err(a, r.line, "a value is missing", 0);
                return ZXAPP_E_SYNTAX;
            }
            /* name may have spaces: everything after the directive */
            const char *v = w[1];
            uint32_t vl = (k >= 3) ? (uint32_t) ((w[k - 1] + wl[k - 1]) - w[1]) : wl[1];
            bool ok;
            if (word_is(w[0], wl[0], "id"))
                ok = k == 2 && ident(v, vl) && word_copy(a->id, sizeof a->id, v, vl);
            else if (word_is(w[0], wl[0], "name"))
                ok = word_copy(a->name, sizeof a->name, v, vl);
            else if (word_is(w[0], wl[0], "view"))
                ok = k == 2 && word_copy(a->view, sizeof a->view, v, vl);
            else
                ok = k == 2 && word_copy(a->model, sizeof a->model, v, vl);
            if (!ok) {
                put_err(a, r.line, "a bad or too long value", 0);
                return ZXAPP_E_SYNTAX;
            }
            continue;
        }
        if (word_is(w[0], wl[0], "logic")) {
            if (k != 4) {
                put_err(a, r.line, "logic needs: logic <language> <entry> <path | <<>", 0);
                return ZXAPP_E_SYNTAX;
            }
            char lang[16];
            if (!word_copy(lang, sizeof lang, w[1], wl[1]) ||
                zxapp_lang_of(lang) == ZXAPP_LANG_UNKNOWN) {
                put_err(a, r.line, "an unknown language (sutra, eve, kron, trebago, myra, c)", 0);
                return ZXAPP_E_SYNTAX;
            }
            if (!ident(w[2], wl[2]) || wl[2] + 1u > ZXAPP_ID_MAX) {
                put_err(a, r.line, "an entry name must be a short identifier", 0);
                return ZXAPP_E_SYNTAX;
            }
            if (a->n >= ZXAPP_LOGIC_MAX) {
                put_err(a, r.line, "too many logic entries", 0);
                return ZXAPP_E_FULL;
            }
            zxapp_logic_t *L = &a->logic[a->n];
            L->lang = zxapp_lang_of(lang);
            word_copy(L->entry, sizeof L->entry, w[2], wl[2]);
            if (zxapp_find(a, L->entry)) {
                put_err(a, r.line, "a second entry named ", L->entry);
                return ZXAPP_E_SYNTAX;
            }
            L->line = r.line;
            if (word_is(w[3], wl[3], "<<")) {
                uint32_t start = r.at, end = r.at;
                bool closed = false;
                while (next_line(&r, &p, &n)) {
                    if (n == 1 && p[0] == '.') {
                        closed = true;
                        break;
                    }
                    end = r.at;
                }
                if (!closed) {
                    put_err(a, L->line, "an inline source has no closing \".\" line", 0);
                    return ZXAPP_E_SYNTAX;
                }
                L->src = text + start;
                L->src_len = end - start;
            } else if (!word_copy(L->path, sizeof L->path, w[3], wl[3])) {
                put_err(a, r.line, "the path is too long", 0);
                return ZXAPP_E_SYNTAX;
            }
            a->n++;
            continue;
        }
        put_err(a, r.line, "an unknown directive (id, name, view, model, logic)", 0);
        return ZXAPP_E_SYNTAX;
    }
    if (!header) {
        put_err(a, 0, "an empty manifest", 0);
        return ZXAPP_E_SYNTAX;
    }
    if (!a->id[0]) {
        put_err(a, 0, "the manifest has no id", 0);
        return ZXAPP_E_SYNTAX;
    }
    return ZXAPP_OK;
}

const zxapp_logic_t *zxapp_find(const zxapp_t *a, const char *entry)
{
    for (uint32_t i = 0; i < a->n; i++)
        if (seq(a->logic[i].entry, entry)) return &a->logic[i];
    return 0;
}

/* ---- runtimes ---------------------------------------------------------------- */

void zxapp_host_init(zxapp_host_t *h)
{
    h->n = 0;
    h->read_file = 0;
    h->read_ctx = 0;
}

static const zxapp_runtime_t *rt_for(const zxapp_host_t *h, zxapp_lang_t lang)
{
    for (uint32_t i = 0; i < h->n; i++)
        if (h->rt[i].lang == lang) return &h->rt[i];
    return 0;
}

bool zxapp_host_has(const zxapp_host_t *h, zxapp_lang_t lang)
{
    return rt_for(h, lang) != 0;
}

int zxapp_host_register(zxapp_host_t *h, const zxapp_runtime_t *rt)
{
    if (!rt || !rt->load || !rt->call || rt->lang < 0 || rt->lang >= ZXAPP_LANG_COUNT)
        return ZXAPP_E_ARG;
    for (uint32_t i = 0; i < h->n; i++)
        if (h->rt[i].lang == rt->lang) {
            h->rt[i] = *rt; /* a newer runtime for the same language replaces it */
            return ZXAPP_OK;
        }
    if (h->n >= ZXAPP_RUNTIMES) return ZXAPP_E_FULL;
    h->rt[h->n++] = *rt;
    return ZXAPP_OK;
}

int zxapp_load(zxapp_t *a, zxapp_host_t *h)
{
    int loaded = 0;
    for (uint32_t i = 0; i < a->n; i++) {
        zxapp_logic_t *L = &a->logic[i];
        if (L->loaded) {
            loaded++;
            continue;
        }
        const zxapp_runtime_t *rt = rt_for(h, L->lang);
        if (!rt) continue; /* A2: the C model covers it */
        const char *src = L->src;
        uint32_t n = L->src_len;
        if (!src) {
            if (!h->read_file) {
                put_err(a, L->line, "no file reader for ", L->path);
                return ZXAPP_E_RUNTIME;
            }
            int32_t got = h->read_file(h->read_ctx, L->path, h->file_buf, sizeof h->file_buf);
            if (got < 0) {
                put_err(a, L->line, "cannot read ", L->path);
                return ZXAPP_E_RUNTIME;
            }
            src = h->file_buf;
            n = (uint32_t) got;
        }
        char err[ZXAPP_ERR_MAX];
        err[0] = 0;
        if (rt->load(rt->ctx, src, n, &L->compiled, err, sizeof err) != ZXAPP_OK) {
            put_err(a, L->line, L->entry, ": ");
            uint32_t k = 0;
            while (a->err[k]) k++;
            for (uint32_t j = 0; err[j] && k + 1u < ZXAPP_ERR_MAX; j++) a->err[k++] = err[j];
            a->err[k] = 0;
            return ZXAPP_E_RUNTIME;
        }
        L->loaded = true;
        loaded++;
    }
    return loaded;
}

void zxapp_unload(zxapp_t *a, zxapp_host_t *h)
{
    for (uint32_t i = 0; i < a->n; i++) {
        zxapp_logic_t *L = &a->logic[i];
        if (!L->loaded) continue;
        const zxapp_runtime_t *rt = rt_for(h, L->lang);
        if (rt && rt->unload) rt->unload(rt->ctx, L->compiled);
        L->compiled = 0;
        L->loaded = false;
    }
}

int zxapp_call(zxapp_t *a, zxapp_host_t *h, const char *entry, const zxapp_arg_t *args,
               uint32_t nargs, zxapp_num_t *out, uint32_t cap, uint32_t *n_out)
{
    if (n_out) *n_out = 0;
    if (!entry || (!out && cap) || (!args && nargs)) return ZXAPP_E_ARG;
    const zxapp_logic_t *L = zxapp_find(a, entry);
    if (!L) return ZXAPP_E_NAME;
    const zxapp_runtime_t *rt = rt_for(h, L->lang);
    if (!L->loaded || !rt) return ZXAPP_E_NORUNTIME;
    uint32_t got = 0;
    char err[ZXAPP_ERR_MAX];
    err[0] = 0;
    int rc = rt->call(rt->ctx, L->compiled, args, nargs, out, cap, &got, err, sizeof err);
    if (n_out) *n_out = got;
    if (rc == ZXAPP_E_BUFFER) return rc;
    if (rc != ZXAPP_OK) {
        put_err(a, L->line, L->entry, ": ");
        uint32_t k = 0;
        while (a->err[k]) k++;
        for (uint32_t j = 0; err[j] && k + 1u < ZXAPP_ERR_MAX; j++) a->err[k++] = err[j];
        a->err[k] = 0;
        return ZXAPP_E_RUNTIME;
    }
    return ZXAPP_OK;
}
