/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zt_gram.c — grammar-constrained decoding (see zt_gram.h). */
#include "zt_gram.h"

/* ---------------------------------------------------------------- compiler */

typedef struct {
    const uint8_t *s;
    uint32_t len, i;
    bool measure; /* count only: no storage is written or read */
    zt_gram_el_t *el;
    uint32_t n_el, cap_el;
    zt_gram_el_t *tmp;
    uint32_t tn, cap_tmp, tmax;
    uint32_t *rule;
    uint32_t n_rules, cap_rules, n_named;
    uint8_t (*cls)[32];
    uint32_t n_cls, cap_cls;
    int32_t code;
    uint32_t off;
    const char *what;
} gc_t;

static int32_t fail(gc_t *c, int32_t code, uint32_t off, const char *what)
{
    if (!c->code) {
        c->code = code;
        c->off = off;
        c->what = what;
    }
    return c->code;
}

static bool is_id(uint8_t ch)
{
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
           ch == '_' || ch == '-';
}

static bool is_sp(uint8_t ch)
{
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

/* spaces, line ends and '#' comments */
static uint32_t skip_ws(const uint8_t *s, uint32_t len, uint32_t i)
{
    while (i < len) {
        if (is_sp(s[i])) {
            i++;
        } else if (s[i] == '#') {
            while (i < len && s[i] != '\n') i++;
        } else {
            break;
        }
    }
    return i;
}

/* "name ::=" at i (spaces and tabs allowed before "::="): the position after it, or 0 */
static uint32_t def_at(const uint8_t *s, uint32_t len, uint32_t i, uint32_t *name_end)
{
    uint32_t j = i;
    while (j < len && is_id(s[j])) j++;
    if (j == i) return 0;
    *name_end = j;
    while (j < len && (s[j] == ' ' || s[j] == '\t')) j++;
    if (len - j < 3 || s[j] != ':' || s[j + 1] != ':' || s[j + 2] != '=') return 0;
    return j + 3;
}

/* Skip a quoted literal or a class starting at i (s[i] is '"' or '['). */
static uint32_t skip_quoted(const uint8_t *s, uint32_t len, uint32_t i)
{
    uint8_t close = s[i] == '"' ? '"' : ']';
    i++;
    while (i < len && s[i] != close) i += s[i] == '\\' && i + 1 < len ? 2u : 1u;
    return i < len ? i + 1 : len;
}

/* The next definition from i: its name's span; returns the position after
 * "::=", or 0 when there is none. */
static uint32_t next_def(const uint8_t *s, uint32_t len, uint32_t *i, uint32_t *n0, uint32_t *n1)
{
    while (*i < len) {
        uint32_t j = skip_ws(s, len, *i);
        if (j >= len) break;
        if (s[j] == '"' || s[j] == '[') {
            *i = skip_quoted(s, len, j);
        } else if (is_id(s[j])) {
            uint32_t e = 0, b = def_at(s, len, j, &e);
            if (b) {
                *n0 = j;
                *n1 = e;
                *i = b;
                return b;
            }
            *i = e ? e : j + 1;
            while (*i < len && is_id(s[*i])) (*i)++;
        } else {
            *i = j + 1;
        }
    }
    *i = len;
    return 0;
}

static bool name_eq(const uint8_t *a, uint32_t al, const uint8_t *b, uint32_t bl)
{
    if (al != bl) return false;
    for (uint32_t k = 0; k < al; k++)
        if (a[k] != b[k]) return false;
    return true;
}

/* index of the first definition called name, or -1 */
static int32_t find_def(const gc_t *c, const uint8_t *name, uint32_t nl)
{
    uint32_t i = 0, n0 = 0, n1 = 0;
    int32_t k = 0;
    while (next_def(c->s, c->len, &i, &n0, &n1)) {
        if (name_eq(c->s + n0, n1 - n0, name, nl)) return k;
        k++;
    }
    return -1;
}

static int32_t tmp_push(gc_t *c, uint8_t type, uint32_t v)
{
    if (c->tn >= ZT_GRAM_MAX_ELEMS) return fail(c, ZT_GRAM_ELIMIT, c->i, "grammar too large");
    if (!c->measure) {
        if (c->tn >= c->cap_tmp) return fail(c, ZT_GRAM_ESPACE, c->i, "arena too small");
        c->tmp[c->tn].type = type;
        c->tmp[c->tn].pad = 0;
        c->tmp[c->tn].v = (uint16_t) v;
    }
    c->tn++;
    if (c->tn > c->tmax) c->tmax = c->tn;
    return 0;
}

static zt_gram_el_t tmp_get(const gc_t *c, uint32_t k)
{
    zt_gram_el_t e = {ZT_GE_BYTE, 0, 0};
    if (!c->measure) e = c->tmp[k];
    return e;
}

static int32_t el_push(gc_t *c, zt_gram_el_t e)
{
    if (c->n_el >= ZT_GRAM_MAX_ELEMS) return fail(c, ZT_GRAM_ELIMIT, c->i, "grammar too large");
    if (!c->measure) {
        if (c->n_el >= c->cap_el) return fail(c, ZT_GRAM_ESPACE, c->i, "arena too small");
        c->el[c->n_el] = e;
    }
    c->n_el++;
    return 0;
}

static int32_t el_put(gc_t *c, uint8_t type, uint32_t v)
{
    zt_gram_el_t e = {type, 0, (uint16_t) v};
    return el_push(c, e);
}

static int32_t new_rule(gc_t *c, uint32_t *id)
{
    if (c->n_rules >= ZT_GRAM_MAX_RULES) return fail(c, ZT_GRAM_ELIMIT, c->i, "too many rules");
    if (!c->measure && c->n_rules >= c->cap_rules)
        return fail(c, ZT_GRAM_ESPACE, c->i, "arena too small");
    *id = c->n_rules++;
    return 0;
}

static void rule_at(gc_t *c, uint32_t id)
{
    if (!c->measure) c->rule[id] = c->n_el;
}

/* tmp[t0..tn) becomes rule id (closed by END); tmp shrinks back to t0 */
static int32_t emit_rule(gc_t *c, uint32_t t0, uint32_t id)
{
    rule_at(c, id);
    for (uint32_t k = t0; k < c->tn; k++)
        if (el_push(c, tmp_get(c, k))) return c->code;
    c->tn = t0;
    return el_put(c, ZT_GE_END, 0);
}

static int32_t add_class(gc_t *c, const uint8_t bm[32])
{
    if (c->n_cls >= 65535u) return fail(c, ZT_GRAM_ELIMIT, c->i, "too many classes");
    if (!c->measure) {
        if (c->n_cls >= c->cap_cls) return fail(c, ZT_GRAM_ESPACE, c->i, "arena too small");
        for (uint32_t k = 0; k < 32; k++) c->cls[c->n_cls][k] = bm[k];
    }
    return tmp_push(c, ZT_GE_CLASS, c->n_cls++);
}

static int hexval(uint8_t ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

/* one character of a literal or class, with escapes; -1 on error */
static int32_t read_char(gc_t *c)
{
    if (c->i >= c->len) return -1;
    uint8_t ch = c->s[c->i++];
    if (ch != '\\') return ch;
    if (c->i >= c->len) return -1;
    ch = c->s[c->i++];
    switch (ch) {
    case 'n':
        return '\n';
    case 'r':
        return '\r';
    case 't':
        return '\t';
    case '\\':
    case '"':
    case '\'':
    case ']':
    case '[':
    case '-':
    case '^':
        return ch;
    case 'x': {
        if (c->len - c->i < 2) return -1;
        int h = hexval(c->s[c->i]), l = hexval(c->s[c->i + 1]);
        if (h < 0 || l < 0) return -1;
        c->i += 2;
        return h * 16 + l;
    }
    default:
        return -1;
    }
}

static int32_t parse_alts(gc_t *c, uint32_t depth);

static int32_t read_uint(gc_t *c, uint32_t *v)
{
    uint32_t x = 0, n = 0;
    while (c->i < c->len && c->s[c->i] >= '0' && c->s[c->i] <= '9') {
        x = x * 10u + (uint32_t) (c->s[c->i++] - '0');
        if (x > ZT_GRAM_MAX_REP) return fail(c, ZT_GRAM_ELIMIT, c->i, "repetition count too large");
        n++;
    }
    if (!n) return fail(c, ZT_GRAM_ESYNTAX, c->i, "expected a number");
    *v = x;
    return 0;
}

/* R ::= x R | x (plus) or x R | ε (star) */
static int32_t rep_rule(gc_t *c, zt_gram_el_t x, bool plus, uint32_t *id)
{
    if (new_rule(c, id)) return c->code;
    rule_at(c, *id);
    if (el_push(c, x) || el_put(c, ZT_GE_REF, *id) || el_put(c, ZT_GE_ALT, 0)) return c->code;
    if (plus && el_push(c, x)) return c->code;
    return el_put(c, ZT_GE_END, 0);
}

/* O ::= x [next] | ε */
static int32_t opt_rule(gc_t *c, zt_gram_el_t x, bool has_next, uint32_t next, uint32_t *id)
{
    if (new_rule(c, id)) return c->code;
    rule_at(c, *id);
    if (el_push(c, x)) return c->code;
    if (has_next && el_put(c, ZT_GE_REF, next)) return c->code;
    if (el_put(c, ZT_GE_ALT, 0)) return c->code;
    return el_put(c, ZT_GE_END, 0);
}

static int32_t parse_postfix(gc_t *c, uint32_t p0)
{
    uint32_t at = skip_ws(c->s, c->len, c->i);
    if (at >= c->len) return 0;
    uint8_t ch = c->s[at];
    if (ch != '*' && ch != '+' && ch != '?' && ch != '{') return 0;
    c->i = at + 1;
    if (c->tn == p0) return fail(c, ZT_GRAM_ELEFTREC, at, "repeats a part that is empty");
    uint32_t id;
    if (c->tn - p0 > 1) {
        if (new_rule(c, &id) || emit_rule(c, p0, id) || tmp_push(c, ZT_GE_REF, id)) return c->code;
    }
    zt_gram_el_t x = tmp_get(c, p0);
    c->tn = p0;
    if (ch == '*' || ch == '+') {
        if (rep_rule(c, x, ch == '+', &id)) return c->code;
        return tmp_push(c, ZT_GE_REF, id);
    }
    if (ch == '?') {
        if (opt_rule(c, x, false, 0, &id)) return c->code;
        return tmp_push(c, ZT_GE_REF, id);
    }
    uint32_t m = 0, n = 0;
    bool unbounded = false;
    c->i = skip_ws(c->s, c->len, c->i);
    if (read_uint(c, &m)) return c->code;
    c->i = skip_ws(c->s, c->len, c->i);
    n = m;
    if (c->i < c->len && c->s[c->i] == ',') {
        c->i = skip_ws(c->s, c->len, c->i + 1);
        if (c->i < c->len && c->s[c->i] == '}') {
            unbounded = true;
        } else if (read_uint(c, &n)) {
            return c->code;
        }
        c->i = skip_ws(c->s, c->len, c->i);
    }
    if (c->i >= c->len || c->s[c->i] != '}') return fail(c, ZT_GRAM_ESYNTAX, c->i, "expected '}'");
    c->i++;
    if (!unbounded && n < m) return fail(c, ZT_GRAM_ESYNTAX, at, "{m,n} with n < m");
    for (uint32_t k = 0; k < m; k++)
        if (c->measure ? tmp_push(c, ZT_GE_BYTE, 0) : tmp_push(c, x.type, x.v)) return c->code;
    if (unbounded) {
        if (rep_rule(c, x, false, &id)) return c->code;
        return tmp_push(c, ZT_GE_REF, id);
    }
    if (n == m) return 0;
    /* n - m nested optionals, built from the innermost */
    uint32_t next = 0;
    for (uint32_t k = 0; k < n - m; k++) {
        if (opt_rule(c, x, k > 0, next, &id)) return c->code;
        next = id;
    }
    return tmp_push(c, ZT_GE_REF, next);
}

static int32_t parse_class(gc_t *c)
{
    uint32_t at = c->i;
    uint8_t bm[32] = {0};
    c->i++; /* '[' */
    bool neg = c->i < c->len && c->s[c->i] == '^';
    if (neg) c->i++;
    uint32_t count = 0;
    while (c->i < c->len && c->s[c->i] != ']') {
        int32_t a = read_char(c), b;
        if (a < 0) return fail(c, ZT_GRAM_ESYNTAX, c->i, "bad character in a class");
        b = a;
        if (c->i + 1 < c->len && c->s[c->i] == '-' && c->s[c->i + 1] != ']') {
            c->i++;
            b = read_char(c);
            if (b < 0 || b < a) return fail(c, ZT_GRAM_ESYNTAX, c->i, "bad range in a class");
        }
        for (int32_t k = a; k <= b; k++) bm[k >> 3] |= (uint8_t) (1u << (k & 7));
        count++;
    }
    if (c->i >= c->len) return fail(c, ZT_GRAM_ESYNTAX, at, "unterminated class");
    c->i++; /* ']' */
    if (!count) return fail(c, ZT_GRAM_ESYNTAX, at, "empty class");
    uint32_t ones = 0, one = 0;
    for (uint32_t k = 0; k < 256; k++)
        if ((bm[k >> 3] >> (k & 7)) & 1u) {
            ones++;
            one = k;
        }
    if (neg) {
        for (uint32_t k = 0; k < 32; k++) bm[k] = (uint8_t) ~bm[k];
        ones = 256 - ones;
        for (uint32_t k = 0; k < 256; k++)
            if ((bm[k >> 3] >> (k & 7)) & 1u) one = k;
    }
    if (!ones) return fail(c, ZT_GRAM_ESYNTAX, at, "class matches nothing");
    if (ones == 1) return tmp_push(c, ZT_GE_BYTE, one);
    return add_class(c, bm);
}

static int32_t parse_primary(gc_t *c, uint32_t depth)
{
    uint8_t ch = c->s[c->i];
    uint32_t at = c->i;
    if (ch == '"') {
        c->i++;
        while (c->i < c->len && c->s[c->i] != '"') {
            int32_t b = read_char(c);
            if (b < 0) return fail(c, ZT_GRAM_ESYNTAX, c->i, "bad character in a literal");
            if (tmp_push(c, ZT_GE_BYTE, (uint32_t) b)) return c->code;
        }
        if (c->i >= c->len) return fail(c, ZT_GRAM_ESYNTAX, at, "unterminated literal");
        c->i++;
        return 0;
    }
    if (ch == '[') return parse_class(c);
    if (ch == '.') {
        uint8_t bm[32];
        for (uint32_t k = 0; k < 32; k++) bm[k] = 0xFF;
        c->i++;
        return add_class(c, bm);
    }
    if (ch == '(') {
        if (depth + 1 > ZT_GRAM_MAX_NEST)
            return fail(c, ZT_GRAM_ELIMIT, at, "groups nest too deep");
        c->i++;
        uint32_t t0 = c->tn, id;
        if (parse_alts(c, depth + 1)) return c->code;
        c->i = skip_ws(c->s, c->len, c->i);
        if (c->i >= c->len || c->s[c->i] != ')')
            return fail(c, ZT_GRAM_ESYNTAX, at, "unclosed '('");
        c->i++;
        if (new_rule(c, &id) || emit_rule(c, t0, id)) return c->code;
        return tmp_push(c, ZT_GE_REF, id);
    }
    if (is_id(ch)) {
        uint32_t e = c->i;
        while (e < c->len && is_id(c->s[e])) e++;
        int32_t id = find_def(c, c->s + c->i, e - c->i);
        if (id < 0) return fail(c, ZT_GRAM_EUNDEF, at, "reference to an undefined rule");
        c->i = e;
        return tmp_push(c, ZT_GE_REF, (uint32_t) id);
    }
    return fail(c, ZT_GRAM_ESYNTAX, at, "unexpected character");
}

static int32_t parse_seq(gc_t *c, uint32_t depth)
{
    for (;;) {
        c->i = skip_ws(c->s, c->len, c->i);
        if (c->i >= c->len) return 0;
        uint8_t ch = c->s[c->i];
        uint32_t e;
        if (ch == '|' || ch == ')') return 0;
        if (is_id(ch) && def_at(c->s, c->len, c->i, &e)) return 0;
        uint32_t p0 = c->tn;
        if (parse_primary(c, depth) || parse_postfix(c, p0)) return c->code;
    }
}

static int32_t parse_alts(gc_t *c, uint32_t depth)
{
    if (parse_seq(c, depth)) return c->code;
    for (;;) {
        c->i = skip_ws(c->s, c->len, c->i);
        if (c->i >= c->len || c->s[c->i] != '|') return 0;
        c->i++;
        if (tmp_push(c, ZT_GE_ALT, 0) || parse_seq(c, depth)) return c->code;
    }
}

static uint32_t cstr_len(const char *s)
{
    uint32_t n = 0;
    while (s[n]) n++;
    return n;
}

static int32_t compile(gc_t *c, const char *root, uint32_t *root_id)
{
    /* definitions: count, and no name twice */
    uint32_t i = 0, n0 = 0, n1 = 0, k = 0;
    while (next_def(c->s, c->len, &i, &n0, &n1)) {
        if (find_def(c, c->s + n0, n1 - n0) != (int32_t) k)
            return fail(c, ZT_GRAM_EDUP, n0, "rule defined twice");
        k++;
        if (k > ZT_GRAM_MAX_RULES) return fail(c, ZT_GRAM_ELIMIT, n0, "too many rules");
    }
    if (!k) return fail(c, ZT_GRAM_ESYNTAX, 0, "no rules");
    if (!c->measure && k > c->cap_rules) return fail(c, ZT_GRAM_ESPACE, 0, "arena too small");
    c->n_named = c->n_rules = k;
    int32_t r = find_def(c, (const uint8_t *) root, cstr_len(root));
    if (r < 0) return fail(c, ZT_GRAM_ENOROOT, 0, "no rule with the root's name");
    *root_id = (uint32_t) r;
    /* bodies, in definition order */
    c->i = 0;
    for (uint32_t id = 0; id < k; id++) {
        c->i = skip_ws(c->s, c->len, c->i);
        uint32_t e = 0, b = c->i < c->len ? def_at(c->s, c->len, c->i, &e) : 0;
        if (!b) return fail(c, ZT_GRAM_ESYNTAX, c->i, "expected \"name ::=\"");
        c->i = b;
        c->tn = 0;
        if (parse_alts(c, 0)) return c->code;
        c->i = skip_ws(c->s, c->len, c->i);
        if (c->i < c->len && (c->s[c->i] == '|' || c->s[c->i] == ')'))
            return fail(c, ZT_GRAM_ESYNTAX, c->i, "unexpected character");
        if (emit_rule(c, 0, id)) return c->code;
    }
    c->i = skip_ws(c->s, c->len, c->i);
    if (c->i < c->len) return fail(c, ZT_GRAM_ESYNTAX, c->i, "unexpected text");
    return 0;
}

/* Left recursion, through parts that can be empty: Kahn's sort of the
 * "may begin with" graph must remove every rule. */
static int32_t leftrec_check(gc_t *c, uint8_t *nullable, uint32_t *indeg, uint16_t *edge,
                             uint8_t *gone)
{
    uint32_t R = c->n_rules, ne = 0;
    for (uint32_t r = 0; r < R; r++) nullable[r] = 0;
    for (bool changed = true; changed;) {
        changed = false;
        for (uint32_t r = 0; r < R; r++) {
            if (nullable[r]) continue;
            for (uint32_t a = c->rule[r];;) {
                bool all = true;
                while (c->el[a].type != ZT_GE_ALT && c->el[a].type != ZT_GE_END) {
                    if (c->el[a].type != ZT_GE_REF || !nullable[c->el[a].v]) all = false;
                    a++;
                }
                if (all) {
                    nullable[r] = 1;
                    changed = true;
                    break;
                }
                if (c->el[a].type == ZT_GE_END) break;
                a++;
            }
        }
    }
    for (uint32_t r = 0; r < R; r++) indeg[r] = 0, gone[r] = 0;
    for (uint32_t r = 0; r < R; r++)
        for (uint32_t a = c->rule[r];;) {
            bool open = true;
            while (c->el[a].type != ZT_GE_ALT && c->el[a].type != ZT_GE_END) {
                if (open && c->el[a].type == ZT_GE_REF) {
                    edge[2 * ne] = (uint16_t) r;
                    edge[2 * ne + 1] = c->el[a].v;
                    indeg[c->el[a].v]++;
                    ne++;
                    open = nullable[c->el[a].v] != 0;
                } else {
                    open = false;
                }
                a++;
            }
            if (c->el[a].type == ZT_GE_END) break;
            a++;
        }
    for (uint32_t done = 0; done < R;) {
        uint32_t r = 0;
        while (r < R && (gone[r] || indeg[r])) r++;
        if (r == R)
            return fail(c, ZT_GRAM_ELEFTREC, 0,
                        "left recursion, or a repeated part that can be empty");
        gone[r] = 1;
        done++;
        for (uint32_t e = 0; e < ne; e++)
            if (edge[2 * e] == r) indeg[edge[2 * e + 1]]--;
    }
    return 0;
}

static uint64_t al8(uint64_t x)
{
    return (x + 7u) & ~(uint64_t) 7u;
}

typedef struct {
    uint64_t el, tmp, rule, cls, nul, indeg, edge, gone, total;
} glay_t;

static void glayout(uint32_t n_el, uint32_t tmax, uint32_t n_rules, uint32_t n_cls, glay_t *L)
{
    uint64_t o = 0;
    L->el = o, o = al8(o + (uint64_t) n_el * sizeof(zt_gram_el_t));
    L->tmp = o, o = al8(o + (uint64_t) (tmax ? tmax : 1) * sizeof(zt_gram_el_t));
    L->rule = o, o = al8(o + (uint64_t) n_rules * 4u);
    L->cls = o, o = al8(o + (uint64_t) n_cls * 32u);
    L->nul = o, o = al8(o + n_rules);
    L->indeg = o, o = al8(o + (uint64_t) n_rules * 4u);
    L->edge = o, o = al8(o + (uint64_t) n_el * 4u);
    L->gone = o, o = al8(o + n_rules);
    L->total = o + 8u;
}

static void err_out(zt_gram_err_t *err, const gc_t *c)
{
    if (!err) return;
    err->code = c->code;
    err->off = c->off;
    err->what = c->code ? c->what : "";
}

static int32_t measure(const uint8_t *src, uint32_t len, const char *root, gc_t *c, glay_t *L)
{
    gc_t z = {0};
    *c = z;
    c->s = src;
    c->len = len;
    c->measure = true;
    uint32_t rid = 0;
    if (!src || !len) return fail(c, ZT_GRAM_EARG, 0, "empty grammar");
    if (compile(c, root ? root : "root", &rid)) return c->code;
    glayout(c->n_el, c->tmax, c->n_rules, c->n_cls, L);
    return 0;
}

uint64_t zt_gram_arena_bytes(const uint8_t *src, uint32_t len, const char *root, zt_gram_err_t *err)
{
    gc_t c;
    glay_t L;
    int32_t r = measure(src, len, root, &c, &L);
    err_out(err, &c);
    return r ? 0 : L.total;
}

int32_t zt_gram_compile(zt_gram_t *g, const uint8_t *src, uint32_t len, const char *root,
                        void *arena, uint64_t arena_bytes, zt_gram_err_t *err)
{
    gc_t m, c;
    glay_t L;
    if (!g) return ZT_GRAM_EARG;
    if (measure(src, len, root, &m, &L)) {
        err_out(err, &m);
        return m.code;
    }
    if (!arena || arena_bytes < L.total) {
        fail(&m, ZT_GRAM_ESPACE, 0, "arena too small");
        err_out(err, &m);
        return m.code;
    }
    uint8_t *b = (uint8_t *) arena + ((8 - ((uintptr_t) arena & 7)) & 7);
    gc_t z = {0};
    c = z;
    c.s = src;
    c.len = len;
    c.el = (zt_gram_el_t *) (b + L.el);
    c.cap_el = m.n_el;
    c.tmp = (zt_gram_el_t *) (b + L.tmp);
    c.cap_tmp = m.tmax;
    c.rule = (uint32_t *) (b + L.rule);
    c.cap_rules = m.n_rules;
    c.cls = (uint8_t(*)[32])(b + L.cls);
    c.cap_cls = m.n_cls;
    uint32_t rid = 0;
    if (!compile(&c, root ? root : "root", &rid))
        leftrec_check(&c, b + L.nul, (uint32_t *) (b + L.indeg), (uint16_t *) (b + L.edge),
                      b + L.gone);
    err_out(err, &c);
    if (c.code) return c.code;
    g->el = c.el;
    g->n_el = c.n_el;
    g->rule = c.rule;
    g->n_rules = c.n_rules;
    g->n_named = c.n_named;
    g->cls = (const uint8_t(*)[32]) c.cls;
    g->n_cls = c.n_cls;
    g->root = rid;
    return 0;
}

/* ---------------------------------------------------------------- matcher */

static uint16_t *stk(const zt_gram_state_t *s, const zt_gram_set_t *t, uint32_t i)
{
    return t->pos + (uint64_t) i * s->md;
}

static bool is_end(uint8_t type)
{
    return type == ZT_GE_END || type == ZT_GE_ALT;
}

static bool el_match(const zt_gram_t *g, zt_gram_el_t e, uint8_t b)
{
    if (e.type == ZT_GE_BYTE) return e.v == b;
    if (e.type == ZT_GE_CLASS) return (g->cls[e.v][b >> 3] >> (b & 7)) & 1u;
    return false;
}

static int32_t set_add(const zt_gram_state_t *s, zt_gram_set_t *t, const uint16_t *p, uint32_t d)
{
    for (uint32_t i = 0; i < t->n; i++) {
        if (t->dep[i] != d) continue;
        const uint16_t *q = stk(s, t, i);
        uint32_t k = 0;
        while (k < d && q[k] == p[k]) k++;
        if (k == d) return 0;
    }
    if (t->n >= s->ms) return ZT_GRAM_ECAP;
    uint16_t *q = stk(s, t, t->n);
    for (uint32_t k = 0; k < d; k++) q[k] = p[k];
    t->dep[t->n++] = (uint8_t) d;
    return 0;
}

static int32_t wl_push(zt_gram_state_t *s, const uint16_t *p, uint32_t d)
{
    if (d > s->md) return ZT_GRAM_ECAP;
    if (s->w.n >= s->ms) return ZT_GRAM_ECAP;
    uint16_t *q = stk(s, &s->w, s->w.n);
    for (uint32_t k = 0; k < d; k++) q[k] = p[k];
    s->w.dep[s->w.n++] = (uint8_t) d;
    return 0;
}

/* Expand every work item until its top is a byte or class (or it is
 * empty), into out. */
static int32_t wl_run(zt_gram_state_t *s, zt_gram_set_t *out)
{
    const zt_gram_t *g = s->g;
    uint16_t buf[ZT_GRAM_MAX_DEPTH + 1];
    int32_t r;
    while (s->w.n) {
        uint32_t it = --s->w.n, d = s->w.dep[it];
        const uint16_t *src = stk(s, &s->w, it);
        for (uint32_t k = 0; k < d; k++) buf[k] = src[k];
        for (;;) {
            if (!d) {
                if ((r = set_add(s, out, buf, 0))) return r;
                break;
            }
            zt_gram_el_t e = g->el[buf[d - 1]];
            if (is_end(e.type)) {
                d--;
                continue;
            }
            if (e.type != ZT_GE_REF) {
                if ((r = set_add(s, out, buf, d))) return r;
                break;
            }
            uint32_t cont = buf[d - 1] + 1u;
            if (is_end(g->el[cont].type))
                d--; /* the reference ends its alternative: nothing to come back to */
            else
                buf[d - 1] = (uint16_t) cont;
            if (d + 1 > s->md) return ZT_GRAM_ECAP;
            for (uint32_t a = g->rule[e.v];;) {
                buf[d] = (uint16_t) a;
                if ((r = wl_push(s, buf, d + 1))) return r;
                while (!is_end(g->el[a].type)) a++;
                if (g->el[a].type == ZT_GE_END) break;
                a++;
            }
            break;
        }
    }
    return 0;
}

/* dst = src advanced by byte b; returns dst's size or ECAP */
static int32_t advance(zt_gram_state_t *s, const zt_gram_set_t *src, zt_gram_set_t *dst, uint8_t b)
{
    const zt_gram_t *g = s->g;
    uint16_t buf[ZT_GRAM_MAX_DEPTH + 1];
    dst->n = 0;
    s->w.n = 0;
    for (uint32_t i = 0; i < src->n; i++) {
        uint32_t d = src->dep[i];
        if (!d) continue;
        const uint16_t *p = stk(s, src, i);
        if (!el_match(g, g->el[p[d - 1]], b)) continue;
        for (uint32_t k = 0; k < d; k++) buf[k] = p[k];
        buf[d - 1]++;
        int32_t r = wl_push(s, buf, d);
        if (!r) r = wl_run(s, dst);
        if (r) return r;
    }
    return (int32_t) dst->n;
}

static uint64_t set_bytes(uint32_t ms, uint32_t md)
{
    return al8((uint64_t) ms * md * 2u) + al8(ms);
}

uint64_t zt_gram_state_bytes(uint32_t max_stacks, uint32_t max_depth)
{
    if (!max_stacks || !max_depth || max_depth > ZT_GRAM_MAX_DEPTH || max_stacks > (1u << 20))
        return 0;
    return 4u * set_bytes(max_stacks, max_depth) + 8u;
}

int32_t zt_gram_reset(zt_gram_state_t *s)
{
    if (!s || !s->g) return ZT_GRAM_EARG;
    const zt_gram_t *g = s->g;
    uint16_t buf[1];
    s->x.n = s->w.n = 0;
    for (uint32_t a = g->rule[g->root];;) {
        buf[0] = (uint16_t) a;
        int32_t r = wl_push(s, buf, 1);
        if (!r) r = wl_run(s, &s->x);
        if (r) return r;
        while (!is_end(g->el[a].type)) a++;
        if (g->el[a].type == ZT_GE_END) break;
        a++;
    }
    zt_gram_set_t t = s->cur;
    s->cur = s->x;
    s->x = t;
    return 0;
}

int32_t zt_gram_state_init(zt_gram_state_t *s, const zt_gram_t *g, uint32_t max_stacks,
                           uint32_t max_depth, void *mem, uint64_t bytes)
{
    uint64_t need = zt_gram_state_bytes(max_stacks, max_depth);
    if (!s || !g || !need) return ZT_GRAM_EARG;
    if (!mem || bytes < need) return ZT_GRAM_ESPACE;
    uint8_t *b = (uint8_t *) mem + ((8 - ((uintptr_t) mem & 7)) & 7);
    zt_gram_set_t *sets[4] = {&s->cur, &s->x, &s->y, &s->w};
    for (uint32_t k = 0; k < 4; k++) {
        sets[k]->pos = (uint16_t *) b;
        b += al8((uint64_t) max_stacks * max_depth * 2u);
        sets[k]->dep = b;
        b += al8(max_stacks);
        sets[k]->n = 0;
    }
    s->g = g;
    s->ms = max_stacks;
    s->md = max_depth;
    return zt_gram_reset(s);
}

static int32_t run_text(zt_gram_state_t *s, const uint8_t *text, uint32_t len, bool commit)
{
    if (!s || !s->g || (len && !text)) return ZT_GRAM_EARG;
    const zt_gram_set_t *src = &s->cur;
    zt_gram_set_t *dst = &s->x;
    for (uint32_t i = 0; i < len; i++) {
        int32_t r = advance(s, src, dst, text[i]);
        if (r < 0) return r;
        if (r == 0) return commit ? ZT_GRAM_EREJECT : 0;
        src = dst;
        dst = dst == &s->x ? &s->y : &s->x;
    }
    if (commit && len) {
        zt_gram_set_t t = s->cur;
        if (src == &s->x) {
            s->cur = s->x;
            s->x = t;
        } else {
            s->cur = s->y;
            s->y = t;
        }
    }
    return commit ? 0 : 1;
}

int32_t zt_gram_accept(zt_gram_state_t *s, const uint8_t *text, uint32_t len)
{
    return run_text(s, text, len, true);
}

int32_t zt_gram_test(zt_gram_state_t *s, const uint8_t *text, uint32_t len)
{
    return run_text(s, text, len, false);
}

bool zt_gram_complete(const zt_gram_state_t *s)
{
    if (!s) return false;
    for (uint32_t i = 0; i < s->cur.n; i++)
        if (!s->cur.dep[i]) return true;
    return false;
}

bool zt_gram_finished(const zt_gram_state_t *s)
{
    if (!s || !s->cur.n) return false;
    for (uint32_t i = 0; i < s->cur.n; i++)
        if (s->cur.dep[i]) return false;
    return true;
}

void zt_gram_next_bytes(const zt_gram_state_t *s, uint8_t out[32])
{
    for (uint32_t k = 0; k < 32; k++) out[k] = 0;
    if (!s || !s->g) return;
    const zt_gram_t *g = s->g;
    for (uint32_t i = 0; i < s->cur.n; i++) {
        uint32_t d = s->cur.dep[i];
        if (!d) continue;
        zt_gram_el_t e = g->el[stk(s, &s->cur, i)[d - 1]];
        if (e.type == ZT_GE_BYTE) {
            out[e.v >> 3] |= (uint8_t) (1u << (e.v & 7));
        } else if (e.type == ZT_GE_CLASS) {
            for (uint32_t k = 0; k < 32; k++) out[k] |= g->cls[e.v][k];
        }
    }
}

/* ---------------------------------------------------------------- vocabulary */

uint64_t zt_gram_vocab_bytes(const zt_tok_t *t)
{
    if (!t || !t->n_vocab) return 0;
    uint64_t tot = 0;
    for (uint32_t i = 0; i < t->n_vocab; i++) tot += t->tok[i].len;
    if (tot > 0xFFFFFFFFull) return 0;
    return al8(((uint64_t) t->n_vocab + 1u) * 4u) + al8(t->n_vocab) + al8(tot) + 8u;
}

int32_t zt_gram_vocab_init(zt_gram_vocab_t *v, const zt_tok_t *t, void *mem, uint64_t bytes)
{
    uint64_t need = zt_gram_vocab_bytes(t);
    if (!v || !need) return ZT_GRAM_EARG;
    if (!mem || bytes < need) return ZT_GRAM_ESPACE;
    uint8_t *b = (uint8_t *) mem + ((8 - ((uintptr_t) mem & 7)) & 7);
    uint32_t n = t->n_vocab;
    uint32_t *off = (uint32_t *) b;
    b += al8(((uint64_t) n + 1u) * 4u);
    uint8_t *skip = b;
    b += al8(n);
    uint8_t *out = b;
    uint64_t cap = need - 8u - al8(((uint64_t) n + 1u) * 4u) - al8(n), w = 0;
    for (uint32_t i = 0; i < n; i++) {
        off[i] = (uint32_t) w;
        uint8_t ty = t->ttype[i];
        uint64_t got = 0;
        bool bad = ty == ZT_TOK_CONTROL || ty == ZT_TOK_UNUSED || ty == ZT_TOK_UNKNOWN;
        if (!bad) {
            int32_t id = (int32_t) i;
            if (zt_tok_decode(t, &id, 1, out + w, cap - w, &got)) return ZT_GRAM_ESPACE;
            w += got;
        }
        skip[i] = (uint8_t) (bad || !got);
    }
    off[n] = (uint32_t) w;
    v->n = n;
    v->off = off;
    v->bytes = out;
    v->skip = skip;
    v->n_end = 0;
    if (t->eos >= 0 && (uint32_t) t->eos < n) v->end[v->n_end++] = t->eos;
    return 0;
}

int32_t zt_gram_vocab_add_end(zt_gram_vocab_t *v, int32_t id)
{
    if (!v || id < 0 || (uint32_t) id >= v->n) return ZT_GRAM_EARG;
    for (uint32_t k = 0; k < v->n_end; k++)
        if (v->end[k] == id) return 0;
    if (v->n_end >= ZT_GRAM_MAX_END) return ZT_GRAM_ESPACE;
    v->end[v->n_end++] = id;
    return 0;
}

static bool vocab_is_end(const zt_gram_vocab_t *v, int32_t id)
{
    for (uint32_t k = 0; k < v->n_end; k++)
        if (v->end[k] == id) return true;
    return false;
}

int32_t zt_gram_mask(zt_gram_state_t *s, const zt_gram_vocab_t *v, uint8_t *allow, uint32_t n_vocab)
{
    if (!s || !s->g || !v || !allow || n_vocab != v->n) return ZT_GRAM_EARG;
    uint8_t nb[32];
    zt_gram_next_bytes(s, nb);
    bool done = zt_gram_complete(s);
    int32_t count = 0;
    for (uint32_t k = 0; k < (n_vocab + 7u) / 8u; k++) allow[k] = 0;
    for (uint32_t id = 0; id < n_vocab; id++) {
        bool ok;
        if (vocab_is_end(v, (int32_t) id)) {
            ok = done;
        } else if (v->skip[id]) {
            ok = false;
        } else {
            const uint8_t *p = v->bytes + v->off[id];
            uint32_t len = v->off[id + 1] - v->off[id];
            ok = (nb[p[0] >> 3] >> (p[0] & 7)) & 1u;
            if (ok && len > 1) {
                int32_t r = zt_gram_test(s, p, len);
                if (r < 0) return r;
                ok = r == 1;
            }
        }
        if (ok) {
            allow[id >> 3] |= (uint8_t) (1u << (id & 7));
            count++;
        }
    }
    return count;
}

int32_t zt_gram_accept_token(zt_gram_state_t *s, const zt_gram_vocab_t *v, int32_t id)
{
    if (!s || !v || id < 0 || (uint32_t) id >= v->n) return ZT_GRAM_EARG;
    if (vocab_is_end(v, id)) return zt_gram_complete(s) ? 0 : ZT_GRAM_EREJECT;
    if (v->skip[id]) return ZT_GRAM_EREJECT;
    return zt_gram_accept(s, v->bytes + v->off[id], v->off[id + 1] - v->off[id]);
}

int32_t zt_gram_hook_mask(void *ctx, uint8_t *allow, uint32_t n_vocab)
{
    zt_gram_hook_t *h = (zt_gram_hook_t *) ctx;
    if (!h) return ZT_GRAM_EARG;
    return zt_gram_mask(h->st, h->v, allow, n_vocab);
}

int32_t zt_gram_hook_accept(void *ctx, int32_t id)
{
    zt_gram_hook_t *h = (zt_gram_hook_t *) ctx;
    if (!h) return ZT_GRAM_EARG;
    return zt_gram_accept_token(h->st, h->v, id);
}
