/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxapp.c — app manifests and the language-runtime registry (zxapp.h). */
#include "zxapp.h"

static const char *const LANG[ZXAPP_LANG_COUNT] = {
    "c",      "sutra", "eve",    "kron",   "trebago",    "myra", "cpp",  "rust", "go",  "swift",
    "kotlin", "java",  "dotnet", "python", "typescript", "dart", "ruby", "php",  "wasm"};

/* Other names people write for the same languages. */
static const struct {
    const char *name;
    zxapp_lang_t lang;
} ALIAS[] = {
    {"cobol", ZXAPP_LANG_SUTRA},      {"ada", ZXAPP_LANG_EVE},
    {"apl", ZXAPP_LANG_KRON},         {"k", ZXAPP_LANG_KRON},
    {"fortran", ZXAPP_LANG_TREBAGO},  {"mumps", ZXAPP_LANG_MYRA},
    {"m", ZXAPP_LANG_MYRA},           {"c++", ZXAPP_LANG_CPP},
    {"cxx", ZXAPP_LANG_CPP},          {"golang", ZXAPP_LANG_GO},
    {"kt", ZXAPP_LANG_KOTLIN},        {"csharp", ZXAPP_LANG_DOTNET},
    {"c#", ZXAPP_LANG_DOTNET},        {"cs", ZXAPP_LANG_DOTNET},
    {"fsharp", ZXAPP_LANG_DOTNET},    {"f#", ZXAPP_LANG_DOTNET},
    {"vb", ZXAPP_LANG_DOTNET},        {"py", ZXAPP_LANG_PYTHON},
    {"ts", ZXAPP_LANG_TYPESCRIPT},    {"javascript", ZXAPP_LANG_TYPESCRIPT},
    {"js", ZXAPP_LANG_TYPESCRIPT},    {"rb", ZXAPP_LANG_RUBY},
    {"webassembly", ZXAPP_LANG_WASM},
};

static bool seq(const char *a, const char *b)
{
    uint32_t i = 0;
    for (; a[i] && a[i] == b[i]; i++) {
    }
    return a[i] == b[i];
}

/* Equal, ignoring ASCII case (language names). */
static bool seq_ci(const char *a, const char *b)
{
    uint32_t i = 0;
    for (;; i++) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x = (char) (x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = (char) (y - 'A' + 'a');
        if (x != y) return false;
        if (!x) return true;
    }
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
        if (seq_ci(name, LANG[i])) return (zxapp_lang_t) i;
    for (uint32_t i = 0; i < sizeof ALIAS / sizeof ALIAS[0]; i++)
        if (seq_ci(name, ALIAS[i].name)) return ALIAS[i].lang;
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
                put_err(a, r.line,
                        "an unknown language (a pillar, c, a libzxv SDK language or wasm)", 0);
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
            for (uint32_t i = 0; i < a->n; i++) /* A5: one per language */
                if (seq(a->logic[i].entry, L->entry) && a->logic[i].lang == L->lang) {
                    put_err(a, r.line, "a second entry in the same language named ", L->entry);
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

static int run_one(zxapp_t *a, zxapp_host_t *h, const zxapp_logic_t *L, const zxapp_arg_t *args,
                   uint32_t nargs, zxapp_num_t *out, uint32_t cap, uint32_t *n_out)
{
    const zxapp_runtime_t *rt = rt_for(h, L->lang);
    uint32_t got = 0;
    char err[ZXAPP_ERR_MAX];
    err[0] = 0;
    int rc = rt->call(rt->ctx, L->compiled, args, nargs, out, cap, &got, err, sizeof err);
    *n_out = got;
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

static bool runnable(const zxapp_logic_t *L, const zxapp_host_t *h, const char *entry)
{
    return seq(L->entry, entry) && L->loaded && rt_for(h, L->lang);
}

int zxapp_call(zxapp_t *a, zxapp_host_t *h, const char *entry, const zxapp_arg_t *args,
               uint32_t nargs, zxapp_num_t *out, uint32_t cap, uint32_t *n_out)
{
    uint32_t got = 0;
    if (n_out) *n_out = 0;
    if (!entry || (!out && cap) || (!args && nargs)) return ZXAPP_E_ARG;
    if (!zxapp_find(a, entry)) return ZXAPP_E_NAME;
    for (uint32_t i = 0; i < a->n; i++) /* the first that can run (A5) */
        if (runnable(&a->logic[i], h, entry)) {
            int rc = run_one(a, h, &a->logic[i], args, nargs, out, cap, &got);
            if (n_out) *n_out = got;
            return rc;
        }
    return ZXAPP_E_NORUNTIME;
}

int32_t zxapp_witness(const zxapp_num_t *x, uint32_t nx, const zxapp_num_t *y, uint32_t ny)
{
    if (nx != ny) return ZXAPP_BOTH;
    for (uint32_t i = 0; i < nx; i++)
        if (x[i].num != y[i].num || x[i].den != y[i].den) return ZXAPP_BOTH;
    return ZXAPP_TRUE;
}

int zxapp_call_witnessed(zxapp_t *a, zxapp_host_t *h, const char *entry, const zxapp_arg_t *args,
                         uint32_t nargs, zxapp_num_t *out, uint32_t cap, uint32_t *n_out,
                         int32_t *lp)
{
    zxapp_num_t other[ZXAPP_WITNESS_MAX];
    uint32_t n = 0, got = 0, tried = 0, answered = 0;
    bool differ = false;
    if (n_out) *n_out = 0;
    if (lp) *lp = ZXAPP_NEITHER;
    if (!entry || (!out && cap) || (!args && nargs) || !lp) return ZXAPP_E_ARG;
    if (!zxapp_find(a, entry)) return ZXAPP_E_NAME;
    for (uint32_t i = 0; i < a->n; i++) {
        if (!runnable(&a->logic[i], h, entry)) continue;
        tried++;
        if (!answered) { /* the first answer speaks for the entry */
            int rc = run_one(a, h, &a->logic[i], args, nargs, out, cap, &n);
            if (rc == ZXAPP_E_BUFFER) {
                if (n_out) *n_out = n;
                return rc;
            }
            if (rc == ZXAPP_OK) {
                answered++;
                differ |= tried > 1u; /* an earlier one refused what this answers */
            }
            continue;
        }
        int rc = run_one(a, h, &a->logic[i], args, nargs, other, ZXAPP_WITNESS_MAX, &got);
        if (rc == ZXAPP_OK) answered++;
        differ |= rc != ZXAPP_OK || zxapp_witness(out, n, other, got) != ZXAPP_TRUE;
    }
    if (!tried) return ZXAPP_E_NORUNTIME;
    if (!answered) return ZXAPP_E_RUNTIME; /* every witness refused */
    if (n_out) *n_out = n;
    *lp = differ ? ZXAPP_BOTH : tried == 1u ? ZXAPP_NEITHER : ZXAPP_TRUE;
    return differ ? ZXAPP_E_CONFLICT : ZXAPP_OK;
}
