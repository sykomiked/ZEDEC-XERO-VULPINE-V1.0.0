/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* twelvefold_model.c — Twelvefold's model (twelvefold_model.h). */
#include <string.h>

#include "lpres.h"
#include "twelvefold_model.h"

#define NEST_MAX 64u /* parser recursion: "~" and "(" nested */

/* the length of s, looking at no more than max + 1 bytes */
static size_t blen(const char *s, size_t max)
{
    const char *e = memchr(s, 0, max + 1u);
    return e ? (size_t) (e - s) : max + 1u;
}

/* ---- TF1, TF2: the parser ---- */

typedef struct {
    const char *s;
    uint32_t i, n, nest, at;
    bool bad;
    twelvefold_formula_t *f;
    uint8_t depth[TWELVEFOLD_NODES];
} parser_t;

static int fail(parser_t *p)
{
    if (!p->bad) p->at = p->i;
    p->bad = true;
    return -1;
}

static void skip(parser_t *p)
{
    while (p->i < p->n && p->s[p->i] == ' ') p->i++;
}

static int mk(parser_t *p, uint8_t op, uint8_t val, int a, int b)
{
    twelvefold_formula_t *f = p->f;
    if (f->n >= TWELVEFOLD_NODES) return fail(p);
    uint8_t d = 0;
    if (a >= 0) d = (uint8_t) (p->depth[a] + 1u);
    if (b >= 0 && p->depth[b] + 1u > d) d = (uint8_t) (p->depth[b] + 1u);
    f->node[f->n] =
        (twelvefold_node_t){op, val, (uint8_t) (a < 0 ? 0 : a), (uint8_t) (b < 0 ? 0 : b)};
    p->depth[f->n] = d;
    if (op == TWELVEFOLD_OP_VAR) f->vars |= UINT32_C(1) << val;
    return f->n++;
}

static int p_formula(parser_t *p);

static int p_atom(parser_t *p)
{
    skip(p);
    if (p->i >= p->n) return fail(p);
    char c = p->s[p->i];
    if (c >= 'a' && c <= 'z') {
        p->i++;
        return mk(p, TWELVEFOLD_OP_VAR, (uint8_t) (c - 'a'), -1, -1);
    }
    if (c == 'T' || c == 'F' || c == 'B' || c == 'N') {
        p->i++;
        uint8_t code = c == 'T'   ? TWELVEFOLD_CODE_T
                       : c == 'F' ? TWELVEFOLD_CODE_F
                       : c == 'B' ? TWELVEFOLD_CODE_B
                                  : TWELVEFOLD_CODE_N;
        return mk(p, TWELVEFOLD_OP_CONST, code, -1, -1);
    }
    if (c != '(') return fail(p);
    p->i++;
    int r = p_formula(p);
    if (r < 0) return -1;
    skip(p);
    if (p->i >= p->n || p->s[p->i] != ')') return fail(p);
    p->i++;
    return r;
}

static int p_unary(parser_t *p)
{
    skip(p);
    if (p->i >= p->n || p->s[p->i] != '~') return p_atom(p);
    p->i++;
    if (++p->nest > NEST_MAX) return fail(p);
    int a = p_unary(p);
    p->nest--;
    return a < 0 ? -1 : mk(p, TWELVEFOLD_OP_NOT, 0, a, -1);
}

static int p_both(parser_t *p)
{
    int a = p_unary(p);
    for (;;) {
        if (a < 0) return -1;
        skip(p);
        if (p->i >= p->n || p->s[p->i] != '&') return a;
        p->i++;
        int b = p_unary(p);
        if (b < 0) return -1;
        a = mk(p, TWELVEFOLD_OP_AND, 0, a, b);
    }
}

static int p_either(parser_t *p)
{
    int a = p_both(p);
    for (;;) {
        if (a < 0) return -1;
        skip(p);
        if (p->i >= p->n || p->s[p->i] != '|') return a;
        p->i++;
        int b = p_both(p);
        if (b < 0) return -1;
        a = mk(p, TWELVEFOLD_OP_OR, 0, a, b);
    }
}

static int p_formula(parser_t *p)
{
    if (++p->nest > NEST_MAX) return fail(p);
    int a = p_either(p);
    if (a >= 0) {
        skip(p);
        if (p->i + 1u < p->n && p->s[p->i] == '-' && p->s[p->i + 1u] == '>') {
            p->i += 2u;
            int b = p_formula(p);
            a = b < 0 ? -1 : mk(p, TWELVEFOLD_OP_IMP, 0, a, b);
        }
    }
    p->nest--;
    return a;
}

static uint32_t popcount(uint32_t m)
{
    uint32_t c = 0;
    for (; m; m &= m - 1u) c++;
    return c;
}

int twelvefold_parse(twelvefold_formula_t *f, const char *text, uint32_t *at)
{
    static twelvefold_formula_t tmp;
    static parser_t p;
    uint32_t where = 0;
    if (!at) at = &where;
    *at = 0;
    if (!f || !text) return ZXM_E_ARG;
    const char *end = memchr(text, 0, TWELVEFOLD_TEXT_MAX + 1u);
    if (!end) {
        *at = TWELVEFOLD_TEXT_MAX;
        return ZXM_E_ARG;
    }
    memset(&tmp, 0, sizeof tmp);
    memset(&p, 0, sizeof p);
    p.s = text;
    p.n = (uint32_t) (end - text);
    p.f = &tmp;
    int root = p_formula(&p);
    if (root >= 0) {
        skip(&p);
        if (p.i != p.n) root = fail(&p);
    }
    if (root < 0) {
        *at = p.at;
        return ZXM_E_ARG;
    }
    tmp.root = (uint8_t) root;
    tmp.depth = p.depth[root];
    if (!twelvefold_limits_ok(popcount(tmp.vars), tmp.depth)) { /* TF2 */
        *at = p.n;
        return ZXM_E_ARG;
    }
    memcpy(tmp.text, text, p.n);
    tmp.text[p.n] = 0;
    *f = tmp;
    return ZXM_OK;
}

/* ---- TF3: evaluation, by lpres's own tables ---- */

static uint8_t ev(const twelvefold_formula_t *f, uint8_t k, const uint8_t *code)
{
    const twelvefold_node_t *x = &f->node[k];
    switch (x->op) {
    case TWELVEFOLD_OP_VAR:
        return code[x->val];
    case TWELVEFOLD_OP_CONST:
        return x->val;
    case TWELVEFOLD_OP_NOT:
        return (uint8_t) lpres_negate((lpres_state_t) ev(f, x->a, code));
    case TWELVEFOLD_OP_AND:
        return (uint8_t) lpres_conjoin((lpres_state_t) ev(f, x->a, code),
                                       (lpres_state_t) ev(f, x->b, code));
    case TWELVEFOLD_OP_OR:
        return (uint8_t) lpres_disjoin((lpres_state_t) ev(f, x->a, code),
                                       (lpres_state_t) ev(f, x->b, code));
    default: /* IMP: ~a | b */
        return (uint8_t) lpres_disjoin(lpres_negate((lpres_state_t) ev(f, x->a, code)),
                                       (lpres_state_t) ev(f, x->b, code));
    }
}

uint8_t twelvefold_eval(const twelvefold_formula_t *f, const uint8_t *code)
{
    if (!f || !code || !f->n) return 255u;
    for (uint32_t v = 0; v < 26u; v++)
        if ((f->vars >> v & 1u) && code[v] > 3u) return 255u;
    return ev(f, f->root, code);
}

uint8_t twelvefold_connective(uint8_t op, uint8_t a, uint8_t b)
{
    if (a > 3u || (op != TWELVEFOLD_OP_NOT && b > 3u)) return 255u;
    lpres_state_t x = (lpres_state_t) a, y = (lpres_state_t) b;
    switch (op) {
    case TWELVEFOLD_OP_NOT:
        return (uint8_t) lpres_negate(x);
    case TWELVEFOLD_OP_AND:
        return (uint8_t) lpres_conjoin(x, y);
    case TWELVEFOLD_OP_OR:
        return (uint8_t) lpres_disjoin(x, y);
    case TWELVEFOLD_OP_IMP:
        return (uint8_t) lpres_disjoin(lpres_negate(x), y);
    default:
        return 255u;
    }
}

uint8_t twelvefold_tag(uint8_t code, uint8_t pol, uint8_t phase, uint8_t *action)
{
    if (code > 3u || pol > 3u || phase > 3u) return 255u;
    lpx_t tag = lpx_make(lpx_base_from_lpres((lpres_state_t) code), pol, phase);
    if (action) *action = (uint8_t) lpx_action(tag);
    return tag;
}

/* ---- TF4, TF5: tables and consequence ---- */

static int64_t ROWV[TWELVEFOLD_ROWS]; /* one int64 a row, for witness_kind_count */

/* the letters of mask in order; their count, or -1 past the limit */
static int letters(uint32_t mask, char *var)
{
    uint32_t n = popcount(mask);
    if (!twelvefold_limits_ok(n, 0)) return -1;
    n = 0;
    for (uint32_t v = 0; v < 26u; v++)
        if (mask >> v & 1u) var[n++] = (char) ('a' + v);
    return (int) n;
}

/* assignment k of the variables var[0..nv), into code[] by letter (TF3) */
static void assign(uint32_t k, const char *var, uint32_t nv, uint8_t *code)
{
    for (uint32_t j = nv; j-- > 0;) {
        code[var[j] - 'a'] = (uint8_t) (k & 3u);
        k >>= 2;
    }
}

int twelvefold_table(const twelvefold_t *t, uint32_t i, twelvefold_table_t *out)
{
    uint8_t code[26] = {0};
    if (!t || !out || i >= t->n) return ZXM_E_ARG;
    const twelvefold_formula_t *f = &t->f[i];
    memset(out, 0, sizeof *out);
    int nv = letters(f->vars, out->var);
    if (nv < 0) return ZXM_E_ARG;
    out->nvars = (uint8_t) nv;
    out->rows = UINT32_C(1) << (2u * (uint32_t) nv);
    lpres_state_t meet = LPRES_STATE_TRUE;
    for (uint32_t k = 0; k < out->rows; k++) {
        assign(k, out->var, (uint32_t) nv, code);
        out->value[k] = ev(f, f->root, code);
        ROWV[k] = out->value[k];
        meet = lpres_conjoin(meet, (lpres_state_t) out->value[k]);
    }
    for (uint8_t c = 0; c < 4u; c++) out->count[c] = witness_kind_count(ROWV, out->rows, c);
    out->meet = (uint8_t) meet;
    out->tag = twelvefold_tag(out->meet, LPX_P_NEUTRAL, LPX_PH_MANIFEST, &out->action);
    return ZXM_OK;
}

int twelvefold_follows(const twelvefold_t *t, const uint32_t *prem, uint32_t np, uint32_t concl,
                       twelvefold_verdict_t *out)
{
    uint8_t code[26] = {0};
    if (!t || !out || (np && !prem) || np > TWELVEFOLD_FORMULAS || concl >= t->n) return ZXM_E_ARG;
    uint32_t mask = t->f[concl].vars;
    for (uint32_t j = 0; j < np; j++) {
        if (prem[j] >= t->n) return ZXM_E_ARG;
        mask |= t->f[prem[j]].vars;
    }
    memset(out, 0, sizeof *out);
    int nv = letters(mask, out->var); /* TF2: the shared variables too */
    if (nv < 0) return ZXM_E_ARG;
    out->nvars = (uint8_t) nv;
    out->rows = UINT32_C(1) << (2u * (uint32_t) nv);
    out->counter = -1;
    for (uint32_t k = 0; k < out->rows; k++) {
        assign(k, out->var, (uint32_t) nv, code);
        lpres_state_t p = LPRES_STATE_TRUE;
        for (uint32_t j = 0; j < np; j++)
            p = lpres_conjoin(p, (lpres_state_t) ev(&t->f[prem[j]], t->f[prem[j]].root, code));
        uint8_t c = ev(&t->f[concl], t->f[concl].root, code);
        bool kept = twelvefold_kept(p, c) == 1;
        ROWV[k] = kept ? TWELVEFOLD_CODE_T : TWELVEFOLD_CODE_F; /* a truth's code */
        if (!kept && out->counter < 0) {
            out->counter = k;
            out->premise = (uint8_t) p;
            out->conclusion = c;
        }
    }
    out->breaks = twelvefold_breaks(ROWV, out->rows); /* TR4 */
    out->follows = out->breaks == 0;
    return ZXM_OK;
}

/* ---- the set ---- */

void twelvefold_init(twelvefold_t *t, zxm_link_t *link)
{
    memset(t, 0, sizeof *t);
    t->link = link;
}

int twelvefold_add(twelvefold_t *t, const char *text, uint32_t *at)
{
    if (t->n >= TWELVEFOLD_FORMULAS) return ZXM_E_STATE;
    int rc = twelvefold_parse(&t->f[t->n], text, at);
    if (rc) return rc;
    t->n++;
    t->dirty = true;
    return ZXM_OK;
}

int twelvefold_remove(twelvefold_t *t, uint32_t i)
{
    if (i >= t->n) return ZXM_E_ARG;
    memmove(&t->f[i], &t->f[i + 1u], (size_t) (t->n - i - 1u) * sizeof t->f[0]);
    t->n--;
    t->dirty = true;
    return ZXM_OK;
}

/* ---- TF6: the Playground ---- */

static uint8_t verdict_code(const char *v)
{
    if (!strcmp(v, "TRUE")) return TWELVEFOLD_CODE_T;
    if (!strcmp(v, "FALSE")) return TWELVEFOLD_CODE_F;
    if (!strcmp(v, "BOTH")) return TWELVEFOLD_CODE_B;
    if (!strcmp(v, "NEITHER")) return TWELVEFOLD_CODE_N;
    return 255u;
}

/* a string member of obj cut to cap - 1 bytes on a character boundary; ""
 * when it is absent */
static int get_cut(const zxm_link_t *l, int32_t obj, const char *key, char *out, uint32_t cap)
{
    static char big[4096];
    out[0] = 0;
    int32_t k = zxm_get(l, obj, key);
    if (k < 0) return ZXM_OK;
    int rc = zxm_get_str(l, obj, key, big, sizeof big);
    if (rc) return rc;
    size_t bl = blen(big, cap - 1u);
    if (bl > cap - 1u) bl = cap - 1u;
    uint32_t n = zxm_utf8_cut(big, (uint32_t) bl);
    memcpy(out, big, n);
    out[n] = 0;
    return ZXM_OK;
}

int twelvefold_catalogue(twelvefold_t *t)
{
    zxm_link_t *l = t->link;
    zxm_begin(l, "play.catalogue");
    int rc = zxm_call(l);
    if (rc) return rc;
    if (l->result < 0 || l->tok[l->result].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    uint32_t n = zxm_len(l, l->result), k = 0;
    for (uint32_t i = 0; i < n && k < TWELVEFOLD_TEMPLATES; i++) {
        int32_t o = zxm_at(l, l->result, i);
        twelvefold_template_t *x = &t->tpl[k];
        if (o < 0 || get_cut(l, o, "id", x->id, sizeof x->id) || !x->id[0] ||
            get_cut(l, o, "title", x->title, sizeof x->title) ||
            get_cut(l, o, "lang", x->lang, sizeof x->lang))
            return ZXM_E_PROTOCOL;
        k++;
    }
    t->ntpl = k;
    return ZXM_OK;
}

static bool name_ok(const char *s, uint32_t max)
{
    size_t n = s ? blen(s, max + 1u) : 0;
    if (!n || n > max) return false;
    for (size_t i = 0; i < n; i++)
        if ((unsigned char) s[i] < 0x20u || s[i] == '/' || s[i] == '\\') return false;
    return true;
}

int twelvefold_play(twelvefold_t *t, const char *template_id, const char *file, const char *text)
{
    static char req[ZXM_REQ_MAX];
    static twelvefold_play_t got;
    zxm_link_t *l = t->link;
    if (!name_ok(template_id, TWELVEFOLD_ID_MAX) || (!file) != (!text)) return ZXM_E_ARG;
    uint32_t tn = 0;
    if (text) {
        if (!name_ok(file, TWELVEFOLD_ID_MAX)) return ZXM_E_ARG;
        const char *e = memchr(text, 0, TWELVEFOLD_PLAYFILE_MAX + 1u);
        if (!e) return ZXM_E_ARG;
        tn = (uint32_t) (e - text);
    }
    /* {"template":"..","files":{"..":".."}}, every string escaped */
    uint32_t o = 0;
    int32_t k;
    memcpy(req, "{\"template\":\"", 13);
    o = 13;
    k = zxm_escape(template_id, (uint32_t) strlen(template_id), req + o, sizeof req - o);
    if (k < 0) return ZXM_E_TOOBIG;
    o += (uint32_t) k;
    if (text) {
        static const char mid[] = "\",\"files\":{\"";
        if (o + sizeof mid > sizeof req) return ZXM_E_TOOBIG;
        memcpy(req + o, mid, sizeof mid - 1u);
        o += (uint32_t) sizeof mid - 1u;
        k = zxm_escape(file, (uint32_t) strlen(file), req + o, sizeof req - o);
        if (k < 0 || o + (uint32_t) k + 3u > sizeof req) return ZXM_E_TOOBIG;
        o += (uint32_t) k;
        memcpy(req + o, "\":\"", 3);
        o += 3u;
        k = zxm_escape(text, tn, req + o, sizeof req - o);
        if (k < 0 || o + (uint32_t) k + 3u > sizeof req) return ZXM_E_TOOBIG;
        o += (uint32_t) k;
        memcpy(req + o, "\"}}", 3);
        o += 3u;
    } else {
        if (o + 2u > sizeof req) return ZXM_E_TOOBIG;
        memcpy(req + o, "\"}", 2);
        o += 2u;
    }
    zxm_begin(l, "play.run");
    zxm_p_raw(l, req, o);
    int rc = zxm_call(l);
    if (rc) return rc;
    char v[16], appr[24];
    memset(&got, 0, sizeof got);
    if (zxm_get_str(l, l->result, "verdict", v, sizeof v)) return ZXM_E_PROTOCOL;
    got.verdict = verdict_code(v);
    if (got.verdict > 3u) return ZXM_E_PROTOCOL;
    if (get_cut(l, l->result, "headline", got.headline, sizeof got.headline) ||
        get_cut(l, l->result, "approval", appr, sizeof appr))
        return ZXM_E_PROTOCOL;
    got.approval = !strcmp(appr, "required");
    int32_t steps = zxm_get(l, l->result, "steps");
    if (steps >= 0) {
        if (l->tok[steps].type != W4_J_ARR) return ZXM_E_PROTOCOL;
        got.steps = zxm_len(l, steps);
        for (uint32_t i = 0; i < got.steps; i++) {
            bool ran = false;
            int32_t s = zxm_at(l, steps, i);
            if (s >= 0 && !zxm_get_bool(l, s, "ran", &ran) && ran) got.ran++;
        }
    }
    memcpy(got.template_id, template_id, strlen(template_id) + 1u);
    t->play = got;
    return ZXM_OK;
}

/* ---- TF7: the Hoard ---- */

static const char HEAD[] = "# twelvefold 1\n";
static char FILEBUF[TWELVEFOLD_FILE_MAX + 1u];

static bool where_ok(const char *space, const char *path)
{
    size_t n = path ? blen(path, TWELVEFOLD_PATH_MAX + 1u) : 0;
    return n && n <= TWELVEFOLD_PATH_MAX && space &&
           blen(space, ZXM_SPACE_MAX + 1u) <= ZXM_SPACE_MAX;
}

static void set_where(twelvefold_t *t, const char *space, const char *path)
{
    memcpy(t->space, space, strlen(space) + 1u);
    if (!strcmp(t->space, "home")) t->space[0] = 0;
    memcpy(t->path, path, strlen(path) + 1u);
}

static int write_set(twelvefold_t *t, const char *space, const char *path)
{
    zxm_link_t *l = t->link;
    uint32_t o = (uint32_t) sizeof HEAD - 1u;
    memcpy(FILEBUF, HEAD, o);
    for (uint32_t i = 0; i < t->n; i++) {
        uint32_t n = (uint32_t) strlen(t->f[i].text);
        memcpy(FILEBUF + o, t->f[i].text, n);
        o += n;
        FILEBUF[o++] = '\n';
    }
    zxm_begin(l, "files.write");
    zxm_p_space(l, space);
    zxm_p_str(l, "path", path);
    zxm_p_strn(l, "text", FILEBUF, o);
    return zxm_call(l);
}

int twelvefold_save(twelvefold_t *t)
{
    if (!t->path[0]) return ZXM_E_STATE;
    int rc = write_set(t, t->space, t->path);
    if (!rc) t->dirty = false;
    return rc;
}

int twelvefold_save_as(twelvefold_t *t, const char *space, const char *path)
{
    if (!where_ok(space, path)) return ZXM_E_ARG;
    int rc = write_set(t, space, path);
    if (rc) return rc;
    set_where(t, space, path);
    t->dirty = false;
    return ZXM_OK;
}

int twelvefold_open(twelvefold_t *t, const char *space, const char *path)
{
    static char chunk[49153];
    static twelvefold_formula_t set[TWELVEFOLD_FORMULAS];
    zxm_link_t *l = t->link;
    uint32_t len = 0, n = 0;
    uint64_t off = 0;
    if (!where_ok(space, path)) return ZXM_E_ARG;
    for (;;) {
        zxm_begin(l, "files.read");
        zxm_p_space(l, space);
        zxm_p_str(l, "path", path);
        zxm_p_u64(l, "offset", off);
        int rc = zxm_call(l);
        if (rc) return rc;
        uint64_t size, next;
        bool eof;
        int32_t tk = zxm_get(l, l->result, "text");
        if (zxm_get_u64(l, l->result, "size", &size) || zxm_get_u64(l, l->result, "next", &next) ||
            zxm_get_bool(l, l->result, "eof", &eof) || tk < 0)
            return ZXM_E_PROTOCOL;
        if (size > TWELVEFOLD_FILE_MAX) return ZXM_E_TOOBIG;
        int32_t got = w4_json_str(l->resp, &l->tok[tk], chunk, sizeof chunk);
        if (got < 0 || (uint32_t) got > TWELVEFOLD_FILE_MAX - len) return ZXM_E_TOOBIG;
        memcpy(FILEBUF + len, chunk, (size_t) got);
        len += (uint32_t) got;
        if (eof) break;
        if (next <= off) return ZXM_E_PROTOCOL;
        off = next;
    }
    FILEBUF[len] = 0;
    uint32_t hl = (uint32_t) sizeof HEAD - 1u;
    if (len < hl || memcmp(FILEBUF, HEAD, hl)) return ZXM_E_ARG;
    /* each line a formula, checked apart: a bad file leaves the set as it was */
    for (uint32_t p = hl; p < len;) {
        char *nl = memchr(FILEBUF + p, '\n', len - p);
        uint32_t e = nl ? (uint32_t) (nl - FILEBUF) : len;
        if (e > p) {
            if (n >= TWELVEFOLD_FORMULAS) return ZXM_E_ARG;
            FILEBUF[e] = 0;
            if (twelvefold_parse(&set[n], FILEBUF + p, NULL)) return ZXM_E_ARG;
            n++;
        }
        p = e + 1u;
    }
    memcpy(t->f, set, (size_t) n * sizeof set[0]);
    t->n = n;
    t->dirty = false;
    set_where(t, space, path);
    return ZXM_OK;
}
