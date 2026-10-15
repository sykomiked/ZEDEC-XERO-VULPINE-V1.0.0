/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* bv_syntax.c — Babel values in a language's print syntax (bv_syntax.h). */
#include "bv_syntax.h"

static bool blank(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

/* Ends a token: a blank, or a bracket the syntax uses, or a quote. */
static bool delim(int syn, char c)
{
    if (blank(c)) return true;
    if (syn == BV_SYNTAX_LOGO) return c == '[' || c == ']' || c == '{' || c == '}';
    if (syn == BV_SYNTAX_NUMBERS) return false;
    return c == '(' || c == ')' || c == '"' || c == ';';
}

static bool same(const char *s, uint32_t n, const char *w)
{
    uint32_t i = 0;
    for (; i < n && w[i] && s[i] == w[i]; i++) {
    }
    return i == n && !w[i];
}

static bool digit(char c)
{
    return c >= '0' && c <= '9';
}

/* [+-]?digits as a magnitude; false when it is not that shape. */
static bool digits(const char *s, uint32_t n, bool *neg, uint64_t *m, bool *over)
{
    uint32_t i = 0;
    *neg = false;
    *over = false;
    if (i < n && (s[i] == '-' || s[i] == '+')) *neg = s[i++] == '-';
    if (i == n) return false;
    uint64_t v = 0;
    for (; i < n; i++) {
        if (!digit(s[i])) return false;
        uint64_t d = (uint64_t) (s[i] - '0');
        if (v > (UINT64_MAX - d) / 10u)
            *over = true;
        else
            v = v * 10u + d;
    }
    *m = v;
    return true;
}

enum { TOK_SYMBOL, TOK_INTEGER, TOK_RATIO, TOK_TRUE, TOK_FALSE, TOK_EMPTY, TOK_FLOAT, TOK_BAD };

/* What an atom is in a syntax. A float-shaped atom is TOK_FLOAT (refused:
 * no floats); a Scheme "#" atom that is not a truth is TOK_BAD. */
static int classify(int syn, const char *s, uint32_t n)
{
    bool neg, over;
    uint64_t m;
    if (digits(s, n, &neg, &m, &over)) return TOK_INTEGER;
    if (syn == BV_SYNTAX_LISP || syn == BV_SYNTAX_SCHEME) {
        for (uint32_t i = 0; i < n; i++)
            if (s[i] == '/' && digits(s, i, &neg, &m, &over) && i + 1u < n && digit(s[i + 1u]) &&
                digits(s + i + 1u, n - i - 1u, &neg, &m, &over) && !neg)
                return TOK_RATIO;
    }
    /* [+-]? digits? . digits, or digits with an exponent: a float */
    uint32_t i = 0, d = 0;
    if (i < n && (s[i] == '-' || s[i] == '+')) i++;
    while (i < n && digit(s[i])) i++, d++;
    if (i < n && (s[i] == '.' || s[i] == 'e' || s[i] == 'E') && i + 1u < n) {
        uint32_t j = i + 1u, e = 0;
        if (s[i] != '.' && (s[j] == '-' || s[j] == '+')) j++;
        while (j < n && digit(s[j])) j++, e++;
        if (j == n && e && (d || s[i] == '.')) return TOK_FLOAT;
    }
    if (d && i + 1u == n && s[i] == '.') return TOK_BAD; /* "1." is 1 in LISP */
    if (syn == BV_SYNTAX_NUMBERS) return TOK_BAD;
    if (syn == BV_SYNTAX_LISP) {
        if (same(s, n, "T")) return TOK_TRUE;
        if (same(s, n, "NIL")) return TOK_EMPTY;
        if (n && s[0] == '#') return TOK_BAD; /* #x10, #\a: reader macros */
    }
    if (syn == BV_SYNTAX_SCHEME && n && s[0] == '#') {
        if (same(s, n, "#t") || same(s, n, "#true")) return TOK_TRUE;
        if (same(s, n, "#f") || same(s, n, "#false")) return TOK_FALSE;
        return TOK_BAD;
    }
    if (syn == BV_SYNTAX_LOGO) {
        if (same(s, n, "true")) return TOK_TRUE;
        if (same(s, n, "false")) return TOK_FALSE;
    }
    if (n == 1 && s[0] == '.' && syn != BV_SYNTAX_LOGO) return TOK_BAD;
    return TOK_SYMBOL;
}

static int32_t atom(bv_arena_t *a, int syn, const char *s, uint32_t n)
{
    bool neg, over;
    uint64_t m;
    switch (classify(syn, s, n)) {
    case TOK_INTEGER: {
        digits(s, n, &neg, &m, &over);
        if (over) return BV_E_OVERFLOW;
        if (neg ? m > (uint64_t) INT64_MAX + 1u : m > (uint64_t) INT64_MAX) return BV_E_OVERFLOW;
        return bv_int(a, neg ? (int64_t) (0u - m) : (int64_t) m);
    }
    case TOK_RATIO: {
        uint32_t k = 0;
        while (s[k] != '/') k++;
        uint64_t dm;
        bool dneg, dover;
        digits(s, k, &neg, &m, &over);
        digits(s + k + 1u, n - k - 1u, &dneg, &dm, &dover);
        if (over || dover) return BV_E_OVERFLOW;
        if (!dm) return BV_E_DOMAIN;
        if (m > (uint64_t) INT64_MAX + 1u || dm > (uint64_t) INT64_MAX) return BV_E_OVERFLOW;
        /* -2^63 / d: reduce through bv_ratio with the sign on the denominator */
        if (m == (uint64_t) INT64_MAX + 1u)
            return neg ? bv_ratio(a, INT64_MIN, (int64_t) dm) : BV_E_OVERFLOW;
        int64_t num = (int64_t) m;
        return bv_ratio(a, neg ? -num : num, (int64_t) dm);
    }
    case TOK_TRUE:
        return bv_truth(a, BV_TRUE);
    case TOK_FALSE:
        return bv_truth(a, BV_FALSE);
    case TOK_EMPTY:
        return bv_list(a, 0, 0);
    case TOK_SYMBOL:
        return bv_symbol(a, s, n);
    case TOK_FLOAT:
        return BV_E_DOMAIN;
    default:
        return BV_E_FORM;
    }
}

/* Push a finished value onto the open list. */
static int32_t push(bv_reader_t *w, uint32_t *sp, uint32_t depth, int32_t v)
{
    if (v < 0) return v;
    if (*sp >= BV_MAX_KIDS) return BV_E_LIMIT;
    if (w->dot[depth] == 2u) return BV_E_FORM; /* (a . b c) */
    /* dot 3 marks a vector, which takes no dot */
    if (w->dot[depth] == 1u) w->dot[depth] = 2u;
    w->stack[(*sp)++] = v;
    return BV_OK;
}

/* Close the list at depth: its values become one list, or a dotted chain. */
static int32_t close_list(bv_arena_t *a, bv_reader_t *w, uint32_t *sp, uint32_t depth)
{
    uint32_t base = w->open[depth];
    if (w->dot[depth] == 1u) return BV_E_FORM; /* (a .) */
    if (w->dot[depth] != 2u) {                 /* 0, or 3 for a vector */
        int32_t v = bv_list(a, &w->stack[base], *sp - base);
        *sp = base;
        return v;
    }
    int32_t v = w->stack[*sp - 1u];
    for (uint32_t i = *sp - 1u; i > base; i--) {
        v = bv_pair(a, w->stack[i - 1u], v);
        if (v < 0) return v;
    }
    *sp = base;
    return v;
}

/* Logo: the values printed on one line become one value. */
static int32_t end_line(bv_arena_t *a, bv_reader_t *w, uint32_t *sp, uint32_t line)
{
    uint32_t k = *sp - line;
    if (k == 1u) return BV_OK;
    if (!k && *sp >= BV_MAX_KIDS) return BV_E_LIMIT;
    int32_t v = bv_list(a, &w->stack[line], k);
    if (v < 0) return v;
    *sp = line;
    w->stack[(*sp)++] = v;
    return BV_OK;
}

int32_t bv_read(bv_arena_t *a, bv_reader_t *w, int syn, const char *s, uint32_t n, uint32_t *at)
{
    uint32_t nn = a->nn, nk = a->nk, nb = a->nb;
    uint32_t i = 0, sp = 0, depth = 0, line = 0;
    int32_t st = BV_OK;
    bool lisp = syn == BV_SYNTAX_LISP || syn == BV_SYNTAX_SCHEME;
    if (syn < BV_SYNTAX_NUMBERS || syn > BV_SYNTAX_LOGO) {
        st = BV_E_DOMAIN;
        goto out;
    }
    w->open[0] = 0;
    w->dot[0] = 0;
    w->closer[0] = 0;
    while (i < n) {
        char c = s[i];
        if (c == '\n' && syn == BV_SYNTAX_LOGO && !depth) {
            if ((st = end_line(a, w, &sp, line)) < 0) goto out;
            line = sp;
            i++;
            continue;
        }
        if (blank(c)) {
            i++;
            continue;
        }
        if (syn == BV_SYNTAX_NUMBERS) {
            uint32_t j = i;
            while (j < n && !blank(s[j])) j++;
            if (classify(syn, s + i, j - i) != TOK_INTEGER) {
                st = BV_E_FORM;
                goto out;
            }
            if ((st = push(w, &sp, depth, atom(a, syn, s + i, j - i))) < 0) goto out;
            i = j;
            continue;
        }
        char closer = 0;
        uint8_t dot = 0;
        if (lisp && c == '(') closer = ')';
        if (lisp && c == '#' && i + 1u < n && s[i + 1u] == '(') {
            closer = ')';
            dot = 3u; /* a vector: a list that takes no dot */
            i++;
        }
        if (syn == BV_SYNTAX_LOGO && c == '[') closer = ']';
        if (syn == BV_SYNTAX_LOGO && c == '{') closer = '}';
        if (closer) {
            if (depth + 1u >= BV_MAX_DEPTH) {
                st = BV_E_LIMIT;
                goto out;
            }
            if (w->dot[depth] == 2u) {
                st = BV_E_FORM;
                goto out;
            }
            depth++;
            w->open[depth] = sp;
            w->closer[depth] = (uint8_t) closer;
            w->dot[depth] = dot;
            i++;
            continue;
        }
        if (c == ')' || c == ']' || c == '}') {
            if (!depth || w->closer[depth] != (uint8_t) c) {
                st = BV_E_FORM;
                goto out;
            }
            int32_t v = close_list(a, w, &sp, depth);
            depth--;
            if ((st = push(w, &sp, depth, v)) < 0) goto out;
            i++;
            continue;
        }
        if (lisp && c == '"') {
            /* the bytes go to the arena once, unescaped, through a second pass */
            uint32_t j = i + 1u, len = 0;
            for (; j < n && s[j] != '"'; j++, len++)
                if (s[j] == '\\') {
                    if (++j == n) break;
                    char e = s[j];
                    if (!(e == '"' || e == '\\' ||
                          (syn == BV_SYNTAX_SCHEME && (e == 'n' || e == 't')))) {
                        i = j;
                        st = BV_E_FORM;
                        goto out;
                    }
                }
            if (j >= n) {
                st = BV_E_FORM;
                goto out;
            }
            if (len > BV_MAX_BYTES - a->nb) {
                st = BV_E_LIMIT;
                goto out;
            }
            /* unescape into the arena's free bytes, then make the text there */
            uint8_t *dst = &a->byte[a->nb];
            uint32_t k = 0;
            for (uint32_t q = i + 1u; q < j; q++) {
                char e = s[q];
                if (e == '\\') {
                    e = s[++q];
                    if (e == 'n') e = '\n';
                    if (e == 't') e = '\t';
                }
                dst[k++] = (uint8_t) e;
            }
            /* bv_text copies forward from dst to the same place: safe */
            if ((st = push(w, &sp, depth, bv_text(a, (const char *) dst, k))) < 0) goto out;
            i = j + 1u;
            if (i < n && !delim(syn, s[i])) {
                st = BV_E_FORM;
                goto out;
            }
            continue;
        }
        uint32_t j = i;
        while (j < n && !delim(syn, s[j])) j++;
        if (j == i) { /* a lone ';' or '"' where the syntax has none */
            st = BV_E_FORM;
            goto out;
        }
        if (lisp && j - i == 1u && c == '.') {
            if (!depth || w->dot[depth] || sp == w->open[depth]) {
                st = BV_E_FORM;
                goto out;
            }
            w->dot[depth] = 1u;
            i = j;
            continue;
        }
        if ((st = push(w, &sp, depth, atom(a, syn, s + i, j - i))) < 0) goto out;
        i = j;
    }
    if (depth) {
        st = BV_E_FORM;
        goto out;
    }
    if (syn == BV_SYNTAX_LOGO && line < sp && (st = end_line(a, w, &sp, line)) < 0) goto out;
    st = bv_list(a, w->stack, sp);
out:
    if (at) *at = i;
    if (st < 0) {
        a->nn = nn;
        a->nk = nk;
        a->nb = nb;
    }
    return st;
}

/* ---- writing --------------------------------------------------------------- */

typedef struct {
    char *p;
    uint32_t n, cap;
    int32_t st;
} out_t;

static void emit(out_t *o, const char *s, uint32_t n)
{
    if (o->st < 0) return;
    if (n > o->cap - o->n) {
        o->st = BV_E_LIMIT;
        return;
    }
    for (uint32_t i = 0; i < n; i++) o->p[o->n++] = s[i];
}

static void emits(out_t *o, const char *s)
{
    uint32_t n = 0;
    while (s[n]) n++;
    emit(o, s, n);
}

static void emit_int(out_t *o, int64_t v)
{
    char t[24];
    uint32_t k = 0;
    uint64_t m = v < 0 ? (uint64_t) 0 - (uint64_t) v : (uint64_t) v;
    do t[k++] = (char) ('0' + m % 10u);
    while ((m /= 10u) != 0);
    if (v < 0) emit(o, "-", 1);
    while (k) emit(o, &t[--k], 1);
}

static void fail(out_t *o, int32_t st)
{
    if (o->st >= 0) o->st = st;
}

/* Recursion depth is the value's depth, at most BV_MAX_DEPTH. */
static void wr(const bv_arena_t *a, int32_t v, int syn, out_t *o)
{
    bool lisp = syn == BV_SYNTAX_LISP || syn == BV_SYNTAX_SCHEME;
    bv_kind_t k = bv_kind(a, v);
    if (syn == BV_SYNTAX_NUMBERS && k != BV_INTEGER) {
        fail(o, BV_E_DOMAIN);
        return;
    }
    switch (k) {
    case BV_INTEGER:
        emit_int(o, bv_num(a, v));
        return;
    case BV_RATIO:
        if (!lisp) break;
        emit_int(o, bv_num(a, v));
        emit(o, "/", 1);
        emit_int(o, bv_den(a, v));
        return;
    case BV_TRUTH: {
        int64_t t = bv_num(a, v);
        if (t != BV_TRUE && t != BV_FALSE) break;
        static const char *const W[3][2] = {{"T", "NIL"}, {"#t", "#f"}, {"true", "false"}};
        emits(o, W[syn - BV_SYNTAX_LISP][t == BV_FALSE]);
        return;
    }
    case BV_TEXT: {
        if (!lisp) break;
        const uint8_t *p = bv_data(a, v);
        emit(o, "\"", 1);
        for (uint32_t i = 0; i < bv_len(a, v); i++) {
            char c = (char) p[i];
            if (c == '"' || c == '\\')
                emit(o, "\\", 1);
            else if (syn == BV_SYNTAX_SCHEME && (c == '\n' || c == '\t')) {
                emits(o, c == '\n' ? "\\n" : "\\t");
                continue;
            }
            emit(o, &c, 1);
        }
        emit(o, "\"", 1);
        return;
    }
    case BV_SYMBOL: {
        const char *p = (const char *) bv_data(a, v);
        uint32_t n = bv_len(a, v);
        for (uint32_t i = 0; i < n; i++)
            if (delim(syn, p[i]) || (lisp && (p[i] == '#' || p[i] == '\\' || p[i] == '|')) ||
                (syn == BV_SYNTAX_LOGO && (p[i] == '(' || p[i] == ')' || p[i] == '"')))
                goto refuse;
        if (classify(syn, p, n) != TOK_SYMBOL) goto refuse;
        if (syn == BV_SYNTAX_LISP) /* LISP's reader would upcase it */
            for (uint32_t i = 0; i < n; i++)
                if (p[i] >= 'a' && p[i] <= 'z') goto refuse;
        emit(o, p, n);
        return;
    }
    case BV_LIST: {
        uint32_t n = bv_len(a, v);
        if (!n && syn == BV_SYNTAX_LISP) {
            emits(o, "NIL");
            return;
        }
        emit(o, lisp ? "(" : "[", 1);
        for (uint32_t i = 0; i < n; i++) {
            if (i) emit(o, " ", 1);
            wr(a, bv_at(a, v, i), syn, o);
        }
        emit(o, lisp ? ")" : "]", 1);
        return;
    }
    case BV_PAIR: {
        if (!lisp) break;
        emit(o, "(", 1);
        int32_t p = v;
        for (bool first = true; bv_kind(a, p) == BV_PAIR; p = bv_at(a, p, 1), first = false) {
            if (!first) emit(o, " ", 1);
            wr(a, bv_at(a, p, 0), syn, o);
        }
        emit(o, " . ", 3);
        wr(a, p, syn, o);
        emit(o, ")", 1);
        return;
    }
    default:
        break;
    }
refuse:
    fail(o, BV_E_DOMAIN);
}

int32_t bv_write(const bv_arena_t *a, int32_t v, int syn, char *out, uint32_t cap)
{
    if (v < 0 || (uint32_t) v >= a->nn) return BV_E_HANDLE;
    if (syn < BV_SYNTAX_NUMBERS || syn > BV_SYNTAX_LOGO) return BV_E_DOMAIN;
    if (cap > (uint32_t) INT32_MAX) cap = (uint32_t) INT32_MAX;
    out_t o = {out, 0, cap, BV_OK};
    wr(a, v, syn, &o);
    return o.st < 0 ? o.st : (int32_t) o.n;
}
