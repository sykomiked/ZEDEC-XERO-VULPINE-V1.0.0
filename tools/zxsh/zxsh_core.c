/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxsh_core.c — the paraconsistent shell's language and logic. See
 * zxsh_core.h (S1-S9). No allocation, no stdio. */
#include "zxsh_core.h"
#include <string.h>
#include "../../kernel/src/robin_debanks/sha256.h"

enum {
    K_WORD,
    K_NL,
    K_SEMI,
    K_AND,
    K_OR,
    K_PIPE,
    K_XCHK,
    K_AMP,    /* &   background */
    K_LT,     /* <   (redirects from here to K_ALLAPP) */
    K_GT,     /* >   1> */
    K_GTGT,   /* >>  1>> */
    K_ERR,    /* 2>  */
    K_ERRAPP, /* 2>> */
    K_E2O,    /* 2>&1 */
    K_O2E,    /* >&2 1>&2 */
    K_ALL,    /* &>  */
    K_ALLAPP, /* &>> */
    K_EOF
};
#define IS_REDIR(k) ((k) >= K_LT && (k) <= K_ALLAPP)
#define SPLIT       '\x1f' /* marks where an unquoted $(...) or $@ splits into words */

#define R(run)                                                                                     \
    ((run) && !sh->aborted && !sh->exiting && !sh->syntax && !sh->need_more && !sh->interrupt &&   \
     !sh->brk && !sh->cont)

static const char *const HOST_BUILTINS[] = {"cd",     "pwd", "history",  "qlog", "zxv",
                                            "source", ".",   "type",     "jobs", "wait",
                                            "split",  "tab", "together", NULL};
static const char *const CORE_BUILTINS[] = {
    "true",    "false", "both",     "neither", "verdict", "exit", "set",  "unset",
    "export",  "held",  "resolve",  "drop",    "why",     "cap",  "help", "alias",
    "unalias", "break", "continue", ":",       "sandbox", NULL};

/* ---------------------------------------------------------------- output */

static void emit(zxsh_t *sh, int fd, const char *s, size_t n)
{
    if (fd == 1 && sh->cap_buf) {
        for (size_t i = 0; i < n && *sh->cap_len < ZXSH_CAPTURE; i++)
            sh->cap_buf[(*sh->cap_len)++] = s[i];
        return;
    }
    if (sh->ops && sh->ops->out) sh->ops->out(sh->u, fd, s, n);
}

static void say(zxsh_t *sh, int fd, const char *s)
{
    emit(sh, fd, s, strlen(s));
}

static void say_u(zxsh_t *sh, int fd, uint32_t v)
{
    char b[12];
    int i = 11;
    b[i] = 0;
    do {
        b[--i] = (char) ('0' + v % 10u);
        v /= 10u;
    } while (v && i > 0);
    say(sh, fd, b + i);
}

static void set_why(zxsh_t *sh, const char *a, const char *b)
{
    size_t n = 0;
    for (; a && *a && n + 1 < ZXSH_WHY_MAX; a++) sh->why[n++] = *a;
    for (; b && *b && n + 1 < ZXSH_WHY_MAX; b++) sh->why[n++] = *b;
    sh->why[n] = 0;
}

static void say_tag(zxsh_t *sh, int fd, lpx_t t)
{
    say(sh, fd, lpx_base_name(lpx_base(t)));
    say(sh, fd, "/");
    say(sh, fd, lpx_pol_name(lpx_pol(t)));
    say(sh, fd, "/");
    say(sh, fd, lpx_phase_name(lpx_phase(t)));
}

char zxsh_letter(lpx_t t)
{
    static const char L[4] = {'F', 'N', 'B', 'T'};
    return lpx_forbidden(t) ? '!' : L[lpx_base(t)];
}

const char *zxsh_base_word(lpx_t t)
{
    return lpx_base_name(lpx_base(t));
}

/* ------------------------------------------------------------- variables */

static bool name_ok(const char *s, size_t n)
{
    if (n == 0 || n >= ZXSH_NAME_MAX) return false;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        bool alpha = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
        if (!alpha && !(i > 0 && c >= '0' && c <= '9')) return false;
    }
    return true;
}

static zxsh_var_t *var_find(const zxsh_t *sh, const char *name, size_t n)
{
    for (unsigned i = 0; i < ZXSH_MAX_VARS; i++) {
        const zxsh_var_t *v = &sh->vars[i];
        if (v->name[0] && strlen(v->name) == n && memcmp(v->name, name, n) == 0)
            return (zxsh_var_t *) v;
    }
    return NULL;
}

const char *zxsh_get(const zxsh_t *sh, const char *name)
{
    zxsh_var_t *v = var_find(sh, name, strlen(name));
    return v ? v->value : NULL;
}

bool zxsh_set(zxsh_t *sh, const char *name, const char *value)
{
    size_t n = strlen(name), m = strlen(value);
    if (!name_ok(name, n) || m >= ZXSH_VALUE_MAX) return false;
    zxsh_var_t *v = var_find(sh, name, n);
    for (unsigned i = 0; !v && i < ZXSH_MAX_VARS; i++)
        if (!sh->vars[i].name[0]) v = &sh->vars[i];
    if (!v) return false;
    memcpy(v->name, name, n + 1);
    memcpy(v->value, value, m + 1);
    return true;
}

static void var_unset(zxsh_t *sh, const char *name)
{
    zxsh_var_t *v = var_find(sh, name, strlen(name));
    if (v) memset(v, 0, sizeof *v);
}

/* ---------------------------------------------------------- capabilities */

static const struct {
    const char *name;
    uint32_t bit;
} CAPS[] = {{"exec", ZXSH_CAP_EXEC},
            {"read", ZXSH_CAP_READ},
            {"write", ZXSH_CAP_WRITE},
            {"net", ZXSH_CAP_NET},
            {"resolve", ZXSH_CAP_RESOLVE}};
#define NCAPS (sizeof CAPS / sizeof CAPS[0])

uint32_t zxsh_cap_parse(const char *list)
{
    uint32_t m = 0;
    const char *p = list;
    while (p && *p) {
        const char *e = p;
        while (*e && *e != ',') e++;
        size_t n = (size_t) (e - p);
        bool found = false;
        if (n == 3 && memcmp(p, "all", 3) == 0) {
            m |= ZXSH_CAP_ALL;
            found = true;
        } else if (n == 4 && memcmp(p, "none", 4) == 0) {
            found = true;
        }
        for (unsigned i = 0; !found && i < NCAPS; i++)
            if (strlen(CAPS[i].name) == n && memcmp(CAPS[i].name, p, n) == 0) {
                m |= CAPS[i].bit;
                found = true;
            }
        if (!found) return 0;
        p = *e ? e + 1 : e;
    }
    return m;
}

/* ----------------------------------------------------------------- lexer */

static bool is_opch(char c)
{
    return c == ';' || c == '|' || c == '&' || c == '<' || c == '>' || c == '\n';
}

static bool push_tok(zxsh_t *sh, uint8_t kind, uint32_t off, uint32_t len)
{
    if (sh->ntok >= ZXSH_MAX_TOKENS - 1u) {
        sh->syntax = true;
        set_why(sh, "input too long", NULL);
        return false;
    }
    sh->tok[sh->ntok].kind = kind;
    sh->tok[sh->ntok].off = off;
    sh->tok[sh->ntok].len = len;
    sh->ntok++;
    return true;
}

/* s[i] starts $( or `: the index just past its end, 0 when unterminated.
 * Quotes and nested $( ) inside are skipped whole. */
static uint32_t skip_sub(const char *s, uint32_t i)
{
    if (s[i] == '`') {
        for (i++; s[i] && s[i] != '`'; i++)
            if (s[i] == '\\' && s[i + 1]) i++;
        return s[i] ? i + 1 : 0;
    }
    unsigned depth = 0;
    for (i += 2;; i++) {
        char c = s[i];
        if (!c) return 0;
        if (c == '\\' && s[i + 1])
            i++;
        else if (c == '\'') {
            for (i++; s[i] && s[i] != '\''; i++) {
            }
            if (!s[i]) return 0;
        } else if (c == '"') {
            for (i++; s[i] && s[i] != '"'; i++)
                if (s[i] == '\\' && s[i + 1]) i++;
            if (!s[i]) return 0;
        } else if (c == '(')
            depth++;
        else if (c == ')') {
            if (!depth) return i + 1;
            depth--;
        }
    }
}

static void lex(zxsh_t *sh)
{
    const char *s = sh->src;
    uint32_t i = 0;
    sh->ntok = 0;
    while (s[i]) {
        char c = s[i];
        if (c == ' ' || c == '\t' || c == '\r') {
            i++;
            continue;
        }
        if (c == '\\' && s[i + 1] == '\n') {
            i += 2;
            continue;
        }
        if (c == '\\' && s[i + 1] == 0) { /* continuation at end of input */
            sh->need_more = true;
            return;
        }
        if (c == '#') {
            while (s[i] && s[i] != '\n') i++;
            continue;
        }
        uint8_t k = K_EOF;
        uint32_t n = 1;
        if (c == '\n')
            k = K_NL;
        else if (c == ';')
            k = K_SEMI;
        else if (c == '&' && s[i + 1] == '&')
            k = K_AND, n = 2;
        else if (c == '&' && s[i + 1] == '>' && s[i + 2] == '>')
            k = K_ALLAPP, n = 3;
        else if (c == '&' && s[i + 1] == '>')
            k = K_ALL, n = 2;
        else if (c == '&')
            k = K_AMP;
        else if (c == '|' && s[i + 1] == '|')
            k = K_OR, n = 2;
        else if (c == '|')
            k = K_PIPE;
        else if (c == '<' && s[i + 1] == '?' && s[i + 2] == '>')
            k = K_XCHK, n = 3;
        else if (c == '<')
            k = K_LT;
        else if (c == '2' && s[i + 1] == '>' && s[i + 2] == '&' && s[i + 3] == '1')
            k = K_E2O, n = 4;
        else if (c == '2' && s[i + 1] == '>' && s[i + 2] == '>')
            k = K_ERRAPP, n = 3;
        else if (c == '2' && s[i + 1] == '>')
            k = K_ERR, n = 2;
        else if (c == '1' && s[i + 1] == '>' && s[i + 2] == '&' && s[i + 3] == '2')
            k = K_O2E, n = 4;
        else if (c == '1' && s[i + 1] == '>' && s[i + 2] == '>')
            k = K_GTGT, n = 3;
        else if (c == '1' && s[i + 1] == '>')
            k = K_GT, n = 2;
        else if (c == '>' && s[i + 1] == '&' && s[i + 2] == '2')
            k = K_O2E, n = 3;
        else if (c == '>' && s[i + 1] == '>')
            k = K_GTGT, n = 2;
        else if (c == '>')
            k = K_GT;
        if (k != K_EOF) {
            if (!push_tok(sh, k, i, n)) return;
            i += n;
            continue;
        }
        /* a word */
        uint32_t st = i;
        while (s[i] && !(s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || is_opch(s[i]))) {
            if (s[i] == '\\') {
                if (!s[i + 1]) {
                    sh->need_more = true;
                    return;
                }
                i += 2;
            } else if ((s[i] == '$' && s[i + 1] == '(') || s[i] == '`') {
                uint32_t e = skip_sub(s, i);
                if (!e) {
                    sh->need_more = true;
                    return;
                }
                i = e;
            } else if (s[i] == '\'' || s[i] == '"') {
                char q = s[i++];
                while (s[i] && s[i] != q) {
                    if (q == '"' && s[i] == '\\' && s[i + 1])
                        i++;
                    else if (q == '"' && ((s[i] == '$' && s[i + 1] == '(') || s[i] == '`')) {
                        uint32_t e = skip_sub(s, i);
                        if (!e) break;
                        i = e;
                        continue;
                    }
                    i++;
                }
                if (!s[i]) {
                    sh->need_more = true;
                    return;
                }
                i++;
            } else if (s[i] == '(' || s[i] == ')') {
                sh->syntax = true; /* S10: ( ) groups and NAME() functions are bash's */
                set_why(sh, "zxsh does not read ( ) groups or functions", NULL);
                return;
            } else {
                i++;
            }
        }
        if (!push_tok(sh, K_WORD, st, i - st)) return;
    }
    push_tok(sh, K_EOF, i, 0);
}

static bool tok_is(const zxsh_t *sh, uint32_t i, const char *kw)
{
    const zxsh_tok_t *t = &sh->tok[i];
    size_t n = strlen(kw);
    return t->kind == K_WORD && t->len == n && memcmp(sh->src + t->off, kw, n) == 0;
}

static bool at_terminator(const zxsh_t *sh)
{
    static const char *const T[] = {"then", "else", "fi", "do", "done", NULL};
    if (sh->tok[sh->pos].kind == K_EOF) return true;
    for (unsigned i = 0; T[i]; i++)
        if (tok_is(sh, sh->pos, T[i])) return true;
    if (sh->in_clause && (tok_is(sh, sh->pos, "both") || tok_is(sh, sh->pos, "neither")))
        return true;
    return false;
}

static void syntax(zxsh_t *sh, const char *msg)
{
    if (sh->syntax || sh->need_more) return;
    if (sh->tok[sh->pos].kind == K_EOF) {
        sh->need_more = true; /* the construct may continue on the next line */
        return;
    }
    sh->syntax = true;
    set_why(sh, "syntax: ", msg);
}

static void skip_nl(zxsh_t *sh)
{
    while (sh->tok[sh->pos].kind == K_NL) sh->pos++;
}

/* ------------------------------------------------------------- expansion */

/* $((...)): integer arithmetic. + - * / % ( ) unary -, comparisons
 * < <= > >= == != (1 or 0), && ||, decimal numbers and variable names (an
 * unset or non-numeric variable is 0). Division by zero is an error. */
typedef struct {
    const zxsh_t *sh;
    const char *p, *e;
    bool bad;
} arith_t;

static int64_t ar_or(arith_t *a);

static void ar_ws(arith_t *a)
{
    while (a->p < a->e && (*a->p == ' ' || *a->p == '\t' || *a->p == '\n')) a->p++;
}

static int64_t ar_num(const char *v)
{
    int64_t r = 0;
    bool neg = *v == '-';
    if (neg) v++;
    for (; *v >= '0' && *v <= '9'; v++) r = r * 10 + (*v - '0');
    return neg ? -r : r;
}

static int64_t ar_atom(arith_t *a)
{
    ar_ws(a);
    if (a->p >= a->e) {
        a->bad = true;
        return 0;
    }
    char c = *a->p;
    if (c == '(') {
        a->p++;
        int64_t v = ar_or(a);
        ar_ws(a);
        if (a->p < a->e && *a->p == ')')
            a->p++;
        else
            a->bad = true;
        return v;
    }
    if (c == '-' || c == '+' || c == '!') {
        a->p++;
        int64_t v = ar_atom(a);
        return c == '-' ? -v : c == '!' ? !v : v;
    }
    if (c >= '0' && c <= '9') {
        int64_t v = 0;
        while (a->p < a->e && *a->p >= '0' && *a->p <= '9') v = v * 10 + (*a->p++ - '0');
        return v;
    }
    const char *n0 = a->p;
    if (*a->p == '$') n0 = ++a->p;
    while (a->p < a->e && name_ok(n0, (size_t) (a->p - n0 + 1))) a->p++;
    if (a->p == n0) {
        a->bad = true;
        return 0;
    }
    const zxsh_var_t *v = var_find(a->sh, n0, (size_t) (a->p - n0));
    return v ? ar_num(v->value) : 0;
}

static int64_t ar_mul(arith_t *a)
{
    int64_t v = ar_atom(a);
    for (;;) {
        ar_ws(a);
        if (a->p >= a->e || (*a->p != '*' && *a->p != '/' && *a->p != '%')) return v;
        char op = *a->p++;
        int64_t r = ar_atom(a);
        if (op == '*')
            v = (int64_t) ((uint64_t) v * (uint64_t) r);
        else if (r == 0 || (v == INT64_MIN && r == -1))
            a->bad = true;
        else
            v = op == '/' ? v / r : v % r;
    }
}

static int64_t ar_add(arith_t *a)
{
    int64_t v = ar_mul(a);
    for (;;) {
        ar_ws(a);
        if (a->p >= a->e || (*a->p != '+' && *a->p != '-')) return v;
        char op = *a->p++;
        int64_t r = ar_mul(a);
        v = (int64_t) (op == '+' ? (uint64_t) v + (uint64_t) r : (uint64_t) v - (uint64_t) r);
    }
}

static int64_t ar_cmp(arith_t *a)
{
    int64_t v = ar_add(a);
    for (;;) {
        ar_ws(a);
        const char *p = a->p;
        if (p + 1 < a->e && (p[0] == '<' || p[0] == '>' || p[0] == '=' || p[0] == '!') &&
            p[1] == '=') {
            a->p += 2;
            int64_t r = ar_add(a);
            v = p[0] == '<' ? v <= r : p[0] == '>' ? v >= r : p[0] == '=' ? v == r : v != r;
        } else if (p < a->e && (p[0] == '<' || p[0] == '>')) {
            a->p++;
            int64_t r = ar_add(a);
            v = p[0] == '<' ? v < r : v > r;
        } else {
            return v;
        }
    }
}

static int64_t ar_or(arith_t *a)
{
    int64_t v = ar_cmp(a);
    for (;;) {
        ar_ws(a);
        if (a->p + 1 < a->e && a->p[0] == '&' && a->p[1] == '&') {
            a->p += 2;
            int64_t r = ar_cmp(a);
            v = v && r;
        } else if (a->p + 1 < a->e && a->p[0] == '|' && a->p[1] == '|') {
            a->p += 2;
            int64_t r = ar_cmp(a);
            v = v || r;
        } else {
            return v;
        }
    }
}

/* Decimal text of v into b (at least 21 bytes); returns b. */
static char *i64_text(int64_t v, char *b)
{
    char t[24];
    int n = 0;
    uint64_t u = v < 0 ? 0u - (uint64_t) v : (uint64_t) v;
    do {
        t[n++] = (char) ('0' + u % 10u);
        u /= 10u;
    } while (u);
    int k = 0;
    if (v < 0) b[k++] = '-';
    while (n) b[k++] = t[--n];
    b[k] = 0;
    return b;
}

/* The value of $NAME: the shell's variable, then the host environment. */
static const char *lookup(zxsh_t *sh, const char *name, size_t n)
{
    zxsh_var_t *v = var_find(sh, name, n);
    if (v) return v->value;
    if (n == 7 && !memcmp(name, "VERDICT", 7)) return zxsh_base_word(sh->last);
    char nm[ZXSH_NAME_MAX];
    if (!sh->ops->getenv || n >= sizeof nm) return "";
    memcpy(nm, name, n);
    nm[n] = 0;
    const char *e = sh->ops->getenv(sh->u, nm);
    return e ? e : "";
}

/* Expand token i into the arena. *glob is set when an unquoted * ? [ was
 * seen. Unquoted $(...) results and $@ are split at SPLIT marks unless
 * assign (the word is NAME=value). NULL when the arena is full. */
static char *expand(zxsh_t *sh, uint32_t i, bool *glob, bool assign)
{
    const char *s = sh->src + sh->tok[i].off;
    uint32_t n = sh->tok[i].len;
    char *d = sh->arena + sh->arena_used;
    uint32_t cap = ZXSH_ARENA - sh->arena_used, o = 0;
    bool dq = false;
    char num[24];
    *glob = false;
#define PUT(ch)                                                                                    \
    do {                                                                                           \
        if (o + 1 >= cap) return NULL;                                                             \
        d[o++] = (ch);                                                                             \
    } while (0)
    for (uint32_t k = 0; k < n; k++) {
        char c = s[k];
        if (!dq && c == '\'') {
            for (k++; k < n && s[k] != '\''; k++) PUT(s[k]);
        } else if (c == '"') {
            dq = !dq;
        } else if (c == '\\' && k + 1 < n) {
            char x = s[++k];
            if (dq && x != '"' && x != '\\' && x != '$' && x != '`') PUT('\\');
            PUT(x);
        } else if ((c == '$' && k + 1 < n && s[k + 1] == '(') || c == '`') {
            /* the token was lexed whole, so the end is inside it */
            uint32_t e = (uint32_t) (skip_sub(s, k) - 0);
            if (!e || e > n) return NULL;
            bool arith = c == '$' && k + 2 < n && s[k + 2] == '(' && e >= k + 5 && s[e - 2] == ')';
            uint32_t b0 = c == '`' ? k + 1 : k + (arith ? 3u : 2u),
                     b1 = c == '`' ? e - 1 : e - (arith ? 2u : 1u);
            if (arith) {
                arith_t a = {sh, s + b0, s + b1, false};
                int64_t v = ar_or(&a);
                ar_ws(&a);
                if (a.bad || a.p != a.e) {
                    set_why(sh, "bad arithmetic", NULL);
                    return NULL;
                }
                for (const char *p = i64_text(v, num); *p; p++) PUT(*p);
            } else {
                char text[2048];
                uint32_t len = 0;
                if (b1 - b0 >= sizeof text) return NULL;
                memcpy(text, s + b0, b1 - b0);
                text[b1 - b0] = 0;
                if (sh->ops->capture)
                    sh->ops->capture(sh->u, sh, text, sh->subst, ZXSH_CAPTURE, &len);
                while (len && (sh->subst[len - 1] == '\n' || sh->subst[len - 1] == '\r')) len--;
                for (uint32_t q = 0; q < len; q++) {
                    char x = sh->subst[q];
                    bool ws = x == ' ' || x == '\t' || x == '\n';
                    PUT(ws && !dq && !assign ? SPLIT : x);
                }
            }
            k = e - 1;
        } else if (c == '$' && k + 1 < n) {
            const char *val = NULL;
            uint32_t a = k + 1, b = a + 1;
            char x = s[a];
            bool split_val = false;
            if (x == '?') {
                val = i64_text(sh->status < 0 ? zxsh_code(sh->last) : sh->status, num);
            } else if (x >= '0' && x <= '9') {
                char nm[8] = {'a', 'r', 'g', x, 0};
                val = lookup(sh, nm, 4);
            } else if (x == '#') {
                val = lookup(sh, "argc", 4);
                if (!*val) val = "0";
            } else if (x == '@' || x == '*') {
                val = lookup(sh, "args", 4);
                split_val = x == '@' && !assign;
            } else if (x == '{') {
                while (b < n && s[b] != '}') b++;
                if (b >= n) return NULL;
                val = lookup(sh, s + a + 1, b - a - 1);
                b++;
            } else {
                b = a;
                while (b < n && name_ok(s + a, b - a + 1)) b++;
                if (b == a) {
                    PUT('$');
                    continue;
                }
                val = lookup(sh, s + a, b - a);
            }
            for (; *val; val++) PUT(*val == SPLIT && !split_val ? ' ' : *val);
            k = b - 1;
        } else if (!dq && c == '~' && k == 0 && (n == 1 || s[1] == '/')) {
            const char *h = lookup(sh, "HOME", 4);
            if (!*h) h = "~";
            for (; *h; h++) PUT(*h);
        } else {
            if (!dq && (c == '*' || c == '?' || c == '[')) *glob = true;
            PUT(c);
        }
    }
#undef PUT
    d[o] = 0;
    sh->arena_used += o + 1;
    return d;
}

/* NAME=... at the start of token i (unquoted)? */
static bool tok_assign(const zxsh_t *sh, uint32_t i)
{
    const char *s = sh->src + sh->tok[i].off;
    uint32_t n = sh->tok[i].len, k = 0;
    while (k < n && s[k] != '=') k++;
    return k < n && name_ok(s, k);
}

/* ------------------------------------------------------------ quarantine */

static void quarantine(zxsh_t *sh, lpx_t v, uint32_t off, uint32_t end)
{
    uint8_t dig[32];
    sha256((const uint8_t *) sh->src + off, end - off, dig);
    if (!lpx_qlog_append(&sh->qlog, v, 1, 0, dig)) {
        sh->locked = true; /* S6: a full log fails closed */
        say(sh, 2, "zxsh: quarantine log full; the shell is locked\n");
    } else if (sh->ops->quarantine) {
        char cmd[ZXSH_HOLD_TEXT];
        uint32_t n = end - off < sizeof cmd - 1 ? end - off : (uint32_t) sizeof cmd - 1;
        memcpy(cmd, sh->src + off, n);
        cmd[n] = 0;
        sh->ops->quarantine(sh->u, &sh->qlog.rec[sh->qlog.n - 1], cmd);
    }
    sh->aborted = true;
    say(sh, 2, "zxsh: forbidden (");
    say(sh, 2, sh->why[0] ? sh->why : "a check refused it");
    say(sh, 2, "): quarantined as record ");
    say_u(sh, 2, sh->qlog.n);
    say(sh, 2, ", stopping\n");
}

/* ----------------------------------------------------------------- holds */

static void hold(zxsh_t *sh, lpx_t cond, bool is_if, uint32_t off, uint32_t end)
{
    zxsh_hold_t *h = NULL;
    for (unsigned i = 0; i < ZXSH_MAX_HELD && !h; i++)
        if (!sh->held[i].used) h = &sh->held[i];
    while (end > off && (sh->src[end - 1] == ' ' || sh->src[end - 1] == '\t' ||
                         sh->src[end - 1] == '\n' || sh->src[end - 1] == ';'))
        end--;
    if (!h || end - off >= ZXSH_HOLD_TEXT) {
        say(sh, 2, "zxsh: cannot hold (queue full or text too long); the rest is skipped\n");
        return;
    }
    h->used = true;
    h->id = ++sh->next_hold;
    h->tag = lpx_make(lpx_base(cond), lpx_pol(cond), LPX_PH_POTENTIAL);
    h->is_if = is_if;
    memcpy(h->text, sh->src + off, end - off);
    h->text[end - off] = 0;
    say(sh, 2, "zxsh: held #");
    say_u(sh, 2, h->id);
    say(sh, 2, " (");
    say_tag(sh, 2, cond);
    say(sh, 2, " -> ");
    say(sh, 2, lpx_action_name(lpx_action(cond)));
    say(sh, 2, "): ");
    say(sh, 2, h->text);
    say(sh, 2, "\n");
}

unsigned zxsh_held_count(const zxsh_t *sh)
{
    unsigned n = 0;
    for (unsigned i = 0; i < ZXSH_MAX_HELD; i++) n += sh->held[i].used;
    return n;
}

static zxsh_hold_t *hold_find(zxsh_t *sh, const char *id)
{
    uint32_t v = 0;
    if (*id == '#') id++;
    if (!*id) return NULL;
    for (; *id; id++) {
        if (*id < '0' || *id > '9' || v > 100000000u) return NULL;
        v = v * 10u + (uint32_t) (*id - '0');
    }
    for (unsigned i = 0; i < ZXSH_MAX_HELD; i++)
        if (sh->held[i].used && sh->held[i].id == v) return &sh->held[i];
    return NULL;
}

/* ------------------------------------------------------------- builtins */

static int base_word(const char *w)
{
    if (!strcmp(w, "true")) return LPX_B_TRUE;
    if (!strcmp(w, "false")) return LPX_B_FALSE;
    if (!strcmp(w, "both")) return LPX_B_BOTH;
    if (!strcmp(w, "neither")) return LPX_B_NEITHER;
    return -1;
}

static int pol_word(const char *w)
{
    if (!strcmp(w, "negentropic")) return LPX_P_NEGENTROPIC;
    if (!strcmp(w, "neutral")) return LPX_P_NEUTRAL;
    if (!strcmp(w, "entropic")) return LPX_P_ENTROPIC;
    return -1;
}

static const lpx_t T_OK = (LPX_B_TRUE << 4) | (LPX_P_NEUTRAL << 2) | LPX_PH_MANIFEST;
static const lpx_t T_NO = (LPX_B_FALSE << 4) | (LPX_P_NEUTRAL << 2) | LPX_PH_MANIFEST;

static lpx_t refuse(zxsh_t *sh, const char *a, const char *b)
{
    set_why(sh, a, b);
    say(sh, 2, "zxsh: ");
    say(sh, 2, sh->why);
    say(sh, 2, "\n");
    return T_NO;
}

static void print_held(zxsh_t *sh)
{
    for (unsigned i = 0; i < ZXSH_MAX_HELD; i++) {
        const zxsh_hold_t *h = &sh->held[i];
        if (!h->used) continue;
        say(sh, 1, "#");
        say_u(sh, 1, h->id);
        say(sh, 1, "  ");
        say_tag(sh, 1, h->tag);
        say(sh, 1, "  ");
        say(sh, 1, lpx_action_name(lpx_action(h->tag)));
        say(sh, 1, "  ");
        say(sh, 1, h->text);
        say(sh, 1, "\n");
    }
}

/* help: the command list. Every line is "name|synopsis|what it does". */
static const char *const HELP[] = {
    "#Everyday commands work as in sh and bash. Every program on your PATH runs unchanged (git, "
    "make, cc, python, npm, cargo, ...).",
    "cd|cd [DIR | - | ~]|change directory (- goes back)",
    "pwd|pwd|print the current directory",
    "export|export NAME=value ...|set a variable and pass it to programs",
    "unset|unset NAME ...|remove variables",
    "set|set|list the shell's variables",
    "alias|alias [NAME=value ...]|define or list aliases (typed lines only)",
    "unalias|unalias NAME ...|remove aliases",
    "source|source FILE (or . FILE)|run a file's commands in this shell",
    "type|type NAME ...|say whether a name is a builtin, an alias or a program",
    "history|history [verify]|past lines with their verdicts; verify checks the hash chain",
    "jobs|jobs|background jobs (cmd &) and their verdicts",
    "wait|wait [%N]|wait for background jobs; the verdict combines them",
    "exit|exit [N]|leave the shell",
    "true|true / false / :|plain verdicts",
    "#Syntax: ; && || | < > >> 2> 2>&1 >&2 &> & $(...) `...` $((1+2)) $VAR ${VAR} $1 $# $@ $? "
    "NAME=value cmd",
    "#  if ... then ... else ... fi, for x in ...; do ...; done, while/until ...; do ...; done, "
    "break, continue",
    "#  A line zxsh cannot read (case, functions, [[ ]], ...) is handed to bash, which says so.",
    "#What zxsh adds: every command ends true, false, both (a contradiction) or neither (no "
    "answer).",
    "verdict|true|false|both|neither [polarity]|state a verdict (verdict both works anywhere)",
    "why|why|explain the last verdict and what it caused",
    "held|held|lines waiting on a both or neither result",
    "resolve|resolve ID true|false|both|neither|decide a held line yourself; the rest of it then "
    "runs",
    "drop|drop ID|discard a held line",
    "<?>|a <?> b|run both and compare: agreement is true, disagreement is both",
    "if|if c; then A; else B; both C; neither D; fi|one branch for each of the four answers",
    "cap|cap [drop NAME]|session capabilities (exec read write net resolve); they only narrow",
    "sandbox|sandbox CAPS -- cmd|one command with fewer capabilities",
    "qlog|qlog [verify]|the quarantine log of forbidden results (crashes)",
    "zxv|zxv METHOD [JSON]|call the ZXV daemon",
    "#Sub-terminals: several terminals on one screen, all running at once.",
    "split|split [-v] [CMD]|open a sub-terminal beside this one (-v: below), running a shell or "
    "CMD",
    "tab|tab [CMD]|open a sub-terminal in a new tab",
    "together|together 'CMD' 'CMD' ...|run commands side by side; the verdict is true or false "
    "when they agree, both when they do not",
    "#Keys inside sub-terminals: Ctrl-B then % split right, \" split below, arrows or o move, c "
    "new tab, n/p next/previous tab, 1-9 jump, x close, q leave, ? help.",
    "#Line editing: arrows, Home/End, Ctrl-A/E/K/U/W, Up/Down for history, Tab completes commands "
    "and files, Ctrl-R searches history.",
    NULL};

static lpx_t help(zxsh_t *sh, const char *name)
{
    for (unsigned i = 0; HELP[i]; i++) {
        const char *l = HELP[i];
        if (l[0] == '#') {
            if (!name) {
                say(sh, 1, l + 1);
                say(sh, 1, "\n");
            }
            continue;
        }
        const char *b1 = strchr(l, '|'), *b2 = b1 ? strchr(b1 + 1, '|') : NULL;
        /* synopses contain | themselves: the description is after the last | */
        const char *last = strrchr(l, '|');
        if (!b1 || !b2) continue;
        size_t nl = (size_t) (b1 - l);
        if (name && (strlen(name) != nl || memcmp(name, l, nl))) continue;
        say(sh, 1, "  ");
        size_t syn = (size_t) (last - b1 - 1);
        emit(sh, 1, b1 + 1, syn);
        for (size_t k = syn; k < 34; k++) emit(sh, 1, " ", 1);
        say(sh, 1, " ");
        say(sh, 1, last + 1);
        say(sh, 1, "\n");
        if (name) return T_OK;
    }
    if (name) return refuse(sh, "no help for ", name);
    say(sh, 1, "help NAME shows one command.\n");
    return T_OK;
}

int zxsh_code(lpx_t t)
{
    if (lpx_forbidden(t)) return 3;
    switch (lpx_base(t)) {
    case LPX_B_TRUE:
        return 0;
    case LPX_B_FALSE:
        return 1;
    case LPX_B_BOTH:
        return 4;
    default:
        return 5;
    }
}

const char *zxsh_alias_get(const zxsh_t *sh, const char *name)
{
    for (unsigned i = 0; i < ZXSH_MAX_ALIAS; i++)
        if (sh->alias[i].name[0] && !strcmp(sh->alias[i].name, name)) return sh->alias[i].value;
    return NULL;
}

bool zxsh_alias_set(zxsh_t *sh, const char *name, const char *value)
{
    size_t n = strlen(name), m = strlen(value);
    if (!name_ok(name, n) || m >= ZXSH_VALUE_MAX) return false;
    zxsh_alias_t *al = NULL;
    for (unsigned i = 0; i < ZXSH_MAX_ALIAS && !al; i++)
        if (!strcmp(sh->alias[i].name, name)) al = &sh->alias[i];
    for (unsigned i = 0; i < ZXSH_MAX_ALIAS && !al; i++)
        if (!sh->alias[i].name[0]) al = &sh->alias[i];
    if (!al) return false;
    memcpy(al->name, name, n + 1);
    memcpy(al->value, value, m + 1);
    return true;
}

bool zxsh_is_builtin(const char *name)
{
    for (unsigned i = 0; CORE_BUILTINS[i]; i++)
        if (!strcmp(name, CORE_BUILTINS[i])) return true;
    return false;
}

/* Returns true when argv[0] is a core builtin (and *v is its verdict). */
static bool builtin(zxsh_t *sh, const zxsh_stage_t *st, lpx_t *v)
{
    int argc = st->argc;
    char *const *a = st->argv;
    int b = base_word(a[0]);
    sh->why[0] = 0;
    if (!strcmp(a[0], "verdict")) { /* verdict both [polarity] */
        if (argc < 2 || (b = base_word(a[1])) < 0) {
            *v = refuse(sh, "usage: ", "verdict true|false|both|neither [polarity]");
            return true;
        }
        a++;
        argc--;
    }
    if (b >= 0) {
        int p = argc > 1 ? pol_word(a[1]) : LPX_P_NEUTRAL;
        if (p < 0 || argc > 2) {
            *v = refuse(sh, "usage: ", "true|false|both|neither [negentropic|neutral|entropic]");
            return true;
        }
        *v = zxsh_tag((uint8_t) b, (uint8_t) p);
        set_why(sh, "stated as ", a[0]);
        return true;
    }
    if (!strcmp(a[0], "exit")) {
        sh->exiting = true;
        sh->exit_code = 0;
        if (argc > 1) {
            int eb = base_word(a[1]);
            sh->exit_code = eb == LPX_B_TRUE ? 0 : eb == LPX_B_FALSE ? 1 : eb >= 0 ? 2 : 0;
            for (const char *p = a[1]; eb < 0 && *p >= '0' && *p <= '9'; p++)
                sh->exit_code = sh->exit_code * 10 + (*p - '0');
        }
        *v = T_OK;
        return true;
    }
    if (!strcmp(a[0], "set")) {
        for (unsigned i = 0; i < ZXSH_MAX_VARS; i++)
            if (sh->vars[i].name[0]) {
                say(sh, 1, sh->vars[i].name);
                say(sh, 1, "=");
                say(sh, 1, sh->vars[i].value);
                say(sh, 1, "\n");
            }
        *v = T_OK;
        return true;
    }
    if (!strcmp(a[0], "unset")) {
        for (int i = 1; i < argc; i++) var_unset(sh, a[i]);
        *v = T_OK;
        return true;
    }
    if (!strcmp(a[0], "export")) {
        *v = T_OK;
        for (int i = 1; i < argc; i++) {
            char *eq = strchr(a[i], '=');
            if (eq) {
                *eq = 0;
                if (!zxsh_set(sh, a[i], eq + 1)) *v = refuse(sh, "bad variable: ", a[i]);
            }
            const char *val = zxsh_get(sh, a[i]);
            if (val && sh->ops->setenv) sh->ops->setenv(sh->u, a[i], val);
        }
        return true;
    }
    if (!strcmp(a[0], "held")) {
        print_held(sh);
        unsigned n = zxsh_held_count(sh);
        /* nothing waiting is a plain truth; something waiting is too */
        *v = T_OK;
        set_why(sh, n ? "items are held" : "nothing is held", NULL);
        return true;
    }
    if (!strcmp(a[0], "resolve") || !strcmp(a[0], "drop")) {
        bool drop = a[0][0] == 'd';
        if (!(sh->caps & ZXSH_CAP_RESOLVE)) {
            *v = refuse(sh, a[0], ": needs the resolve capability (a person at the keyboard)");
            return true;
        }
        if (argc != (drop ? 2 : 3) || (!drop && base_word(a[2]) < 0)) {
            *v = refuse(sh, "usage: ", drop ? "drop ID" : "resolve ID true|false|both|neither");
            return true;
        }
        zxsh_hold_t *h = hold_find(sh, a[1]);
        if (!h) {
            *v = refuse(sh, "no such hold: ", a[1]);
            return true;
        }
        /* S5: potential -> emerging (decided) -> manifest (applied) ->
         * dissolving (cleared), one Gray step each. */
        lpx_advance(&h->tag, LPX_PH_EMERGING);
        if (!drop) {
            size_t n = 0;
            const char *pre = h->is_if ? "if verdict " : "verdict ";
            for (; *pre; pre++) sh->pending[n++] = *pre;
            for (const char *w = a[2]; *w; w++) sh->pending[n++] = *w;
            sh->pending[n++] = h->is_if ? ';' : ' ';
            sh->pending[n++] = ' ';
            size_t m = strlen(h->text);
            memcpy(sh->pending + n, h->text, m + 1);
            sh->has_pending = true;
        }
        lpx_advance(&h->tag, LPX_PH_MANIFEST);
        lpx_advance(&h->tag, LPX_PH_DISSOLVING);
        h->used = false;
        *v = T_OK;
        set_why(sh, drop ? "dropped" : "resolved", NULL);
        return true;
    }
    if (!strcmp(a[0], "why")) {
        say_tag(sh, 1, sh->last);
        say(sh, 1, " -> ");
        say(sh, 1, lpx_action_name(lpx_action(sh->last)));
        if (sh->last_why[0]) {
            say(sh, 1, ": ");
            say(sh, 1, sh->last_why);
        }
        say(sh, 1, "\n");
        *v = sh->last; /* why does not change the verdict it explains */
        memcpy(sh->why, sh->last_why, sizeof sh->why);
        return true;
    }
    if (!strcmp(a[0], "cap")) {
        if (argc == 1) {
            for (unsigned i = 0; i < NCAPS; i++)
                if (sh->caps & CAPS[i].bit) {
                    say(sh, 1, CAPS[i].name);
                    say(sh, 1, "\n");
                }
            *v = T_OK;
            return true;
        }
        if (strcmp(a[1], "drop") || argc < 3) {
            *v = refuse(sh, "usage: ", "cap [drop NAME[,NAME]...]");
            return true;
        }
        for (int i = 2; i < argc; i++) {
            uint32_t m = zxsh_cap_parse(a[i]);
            if (!m && strcmp(a[i], "none")) {
                *v = refuse(sh, "unknown capability: ", a[i]);
                return true;
            }
            sh->caps &= ~m; /* S9: only ever narrows */
        }
        *v = T_OK;
        return true;
    }
    if (!strcmp(a[0], "help")) {
        *v = help(sh, argc > 1 ? a[1] : NULL);
        return true;
    }
    if (!strcmp(a[0], ":")) {
        *v = T_OK;
        return true;
    }
    if (!strcmp(a[0], "alias")) {
        *v = T_OK;
        if (argc == 1)
            for (unsigned i = 0; i < ZXSH_MAX_ALIAS; i++)
                if (sh->alias[i].name[0]) {
                    say(sh, 1, "alias ");
                    say(sh, 1, sh->alias[i].name);
                    say(sh, 1, "='");
                    say(sh, 1, sh->alias[i].value);
                    say(sh, 1, "'\n");
                }
        for (int i = 1; i < argc; i++) {
            char *eq = strchr(a[i], '=');
            if (eq) {
                *eq = 0;
                if (!zxsh_alias_set(sh, a[i], eq + 1)) *v = refuse(sh, "bad alias: ", a[i]);
            } else {
                const char *val = zxsh_alias_get(sh, a[i]);
                if (!val) {
                    *v = refuse(sh, "no such alias: ", a[i]);
                    continue;
                }
                say(sh, 1, "alias ");
                say(sh, 1, a[i]);
                say(sh, 1, "='");
                say(sh, 1, val);
                say(sh, 1, "'\n");
            }
        }
        return true;
    }
    if (!strcmp(a[0], "unalias")) {
        *v = T_OK;
        for (int i = 1; i < argc; i++) {
            zxsh_alias_t *al = NULL;
            for (unsigned k = 0; k < ZXSH_MAX_ALIAS && !al; k++)
                if (!strcmp(sh->alias[k].name, a[i])) al = &sh->alias[k];
            if (al)
                memset(al, 0, sizeof *al);
            else
                *v = refuse(sh, "no such alias: ", a[i]);
        }
        return true;
    }
    if (!strcmp(a[0], "break") || !strcmp(a[0], "continue")) {
        unsigned levels = 1;
        if (argc > 1) {
            levels = 0;
            for (const char *p = a[1]; *p >= '0' && *p <= '9' && levels < 1000; p++)
                levels = levels * 10 + (unsigned) (*p - '0');
        }
        if (!sh->loops || !levels) {
            *v = refuse(sh, a[0], ": only meaningful inside a for, while or until loop");
            return true;
        }
        if (levels > sh->loops) levels = sh->loops;
        if (a[0][0] == 'b')
            sh->brk = levels;
        else
            sh->brk = levels - 1, sh->cont = 1;
        *v = T_OK;
        return true;
    }
    return false;
}

/* --------------------------------------------------------------- parser */

static lpx_t eval_list(zxsh_t *sh, bool run);

static bool is_host_builtin(const char *a)
{
    for (unsigned i = 0; HOST_BUILTINS[i]; i++)
        if (!strcmp(a, HOST_BUILTINS[i])) return true;
    return false;
}

/* Run one parsed pipeline (already expanded). */
static lpx_t dispatch(zxsh_t *sh, zxsh_stage_t *st, unsigned n, char *cap, uint32_t *caplen)
{
    lpx_t v;
    uint32_t saved_caps = sh->caps;
    bool sandboxed = false;
    sh->why[0] = 0;
    if (n == 1 && !strcmp(st[0].argv[0], "sandbox")) {
        int dd = 0;
        for (int i = 1; i < st[0].argc; i++)
            if (!strcmp(st[0].argv[i], "--")) {
                dd = i;
                break;
            }
        if (dd != 2 || dd + 1 >= st[0].argc)
            return refuse(sh, "usage: ", "sandbox CAP[,CAP] -- command [args]");
        uint32_t m = zxsh_cap_parse(st[0].argv[1]);
        if (!m && strcmp(st[0].argv[1], "none"))
            return refuse(sh, "unknown capability: ", st[0].argv[1]);
        sandboxed = true;
        sh->caps &= m;
        int k = 0;
        for (int i = dd + 1; i < st[0].argc; i++, k++) {
            st[0].argv[k] = st[0].argv[i];
            st[0].glob[k] = st[0].glob[i];
        }
        st[0].argv[k] = NULL;
        st[0].argc = k;
    }
    bool redir = st[0].in || st[0].out || st[0].err || st[0].err_to_out || st[0].out_to_err;
    if (n == 1 && !redir && !is_host_builtin(st[0].argv[0])) {
        if (cap) {
            *caplen = 0;
            sh->cap_buf = cap;
            sh->cap_len = caplen;
        }
        bool was = builtin(sh, &st[0], &v);
        sh->cap_buf = NULL;
        if (was) {
            if (sandboxed) sh->caps = saved_caps;
            return v;
        }
    }
    for (unsigned i = 0; i < n; i++) {
        if (st[i].in && !(sh->caps & ZXSH_CAP_READ)) {
            v = refuse(sh, "denied: < needs the read capability", NULL);
            goto out;
        }
        if ((st[i].out || st[i].err) && !(sh->caps & ZXSH_CAP_WRITE)) {
            v = refuse(sh, "denied: > needs the write capability", NULL);
            goto out;
        }
        if (!is_host_builtin(st[i].argv[0]) && !zxsh_is_builtin(st[i].argv[0]) &&
            !(sh->caps & ZXSH_CAP_EXEC)) {
            v = refuse(sh, "denied: running a program needs the exec capability: ", st[i].argv[0]);
            goto out;
        }
        if (!strcmp(st[i].argv[0], "zxv") && !(sh->caps & ZXSH_CAP_NET)) {
            v = refuse(sh, "denied: zxv needs the net capability", NULL);
            goto out;
        }
    }
    sh->status = -1;
    v = sh->ops->exec(sh->u, sh, st, n, cap, ZXSH_CAPTURE, caplen, sh->why, ZXSH_WHY_MAX);
out:
    sh->caps = saved_caps;
    return v;
}

static lpx_t eval_if(zxsh_t *sh, bool run);
static lpx_t eval_if_clauses(zxsh_t *sh, bool run, lpx_t cond);
static lpx_t eval_for(zxsh_t *sh, bool run);
static lpx_t eval_while(zxsh_t *sh, bool run);

static bool compound(const zxsh_t *sh)
{
    return tok_is(sh, sh->pos, "if") || tok_is(sh, sh->pos, "for") ||
           tok_is(sh, sh->pos, "while") || tok_is(sh, sh->pos, "until");
}

static void finish(zxsh_t *sh, lpx_t v, uint32_t off, uint32_t end)
{
    if (sh->status < 0) sh->status = zxsh_code(v);
    sh->last = v;
    memcpy(sh->last_why, sh->why, sizeof sh->why);
    if (lpx_forbidden(v)) quarantine(sh, v, off, end);
}

static lpx_t eval_pipeline(zxsh_t *sh, bool run, bool capture, char *cap, uint32_t *caplen)
{
    bool neg = false;
    lpx_t v = T_OK;
    if (tok_is(sh, sh->pos, "!")) {
        neg = true;
        sh->pos++;
    }
    if (compound(sh)) {
        if (capture) {
            syntax(sh, "an if/for/while cannot be cross-checked; wrap it in a script");
            return v;
        }
        v = tok_is(sh, sh->pos, "if")    ? eval_if(sh, run)
            : tok_is(sh, sh->pos, "for") ? eval_for(sh, run)
                                         : eval_while(sh, run);
        if (sh->tok[sh->pos].kind == K_PIPE || IS_REDIR(sh->tok[sh->pos].kind))
            syntax(sh, "an if/for/while cannot be piped or redirected");
        if (neg && R(run)) {
            sh->last = v = lpx_not(v);
            sh->status = zxsh_code(v);
        }
        return v;
    }
    uint32_t mark = sh->arena_used, off = sh->tok[sh->pos].off, end = off;
    zxsh_stage_t st[ZXSH_MAX_STAGES];
    unsigned n = 0;
    bool overflow = false;
    if (R(run)) sh->why[0] = 0;
    for (;;) {
        if (n >= ZXSH_MAX_STAGES) {
            syntax(sh, "too many pipeline stages");
            return v;
        }
        zxsh_stage_t *s = &st[n];
        memset(s, 0, sizeof *s);
        for (;;) {
            uint8_t k = sh->tok[sh->pos].kind;
            if (k == K_WORD) {
                static const char *const BASH[] = {"case", "esac",     "[[",     "{",
                                                   "}",    "function", "select", NULL};
                for (unsigned b = 0; s->argc == 0 && BASH[b]; b++)
                    if (tok_is(sh, sh->pos, BASH[b])) {
                        sh->syntax = true;
                        set_why(sh, "zxsh does not read ", BASH[b]);
                        return v;
                    }
                if (R(run)) {
                    bool g, as = tok_assign(sh, sh->pos);
                    char *w = expand(sh, sh->pos, &g, as);
                    if (as && s->argc == 0 && w && s->nenv < (int) ZXSH_MAX_ENV) {
                        s->env[s->nenv++] = w; /* NAME=value before the command */
                    } else if (!w) {
                        overflow = true;
                    } else if (!strchr(w, SPLIT)) {
                        if (s->argc >= (int) ZXSH_MAX_ARGS)
                            overflow = true;
                        else
                            s->glob[s->argc] = g, s->argv[s->argc++] = w;
                    } else {
                        for (char *p = w; *p;) { /* split an unquoted $(...) or $@ */
                            char *e = p;
                            while (*e && *e != SPLIT) e++;
                            bool more = *e != 0;
                            *e = 0;
                            if (*p) {
                                if (s->argc >= (int) ZXSH_MAX_ARGS)
                                    overflow = true;
                                else
                                    s->glob[s->argc] = g, s->argv[s->argc++] = p;
                            }
                            p = more ? e + 1 : e;
                        }
                    }
                } else if (!(s->argc == 0 && tok_assign(sh, sh->pos))) {
                    s->argc++;
                } else {
                    s->nenv++;
                }
                end = sh->tok[sh->pos].off + sh->tok[sh->pos].len;
                sh->pos++;
            } else if (k == K_E2O || k == K_O2E) {
                if (k == K_E2O)
                    s->err_to_out = true;
                else
                    s->out_to_err = true;
                end = sh->tok[sh->pos].off + sh->tok[sh->pos].len;
                sh->pos++;
            } else if (IS_REDIR(k)) {
                sh->pos++;
                if (sh->tok[sh->pos].kind != K_WORD) {
                    syntax(sh, "a redirect needs a file name");
                    return v;
                }
                if (R(run)) {
                    bool g;
                    char *w = expand(sh, sh->pos, &g, true);
                    if (!w)
                        overflow = true;
                    else if (k == K_LT)
                        s->in = w;
                    else if (k == K_GT || k == K_GTGT)
                        s->out = w, s->append = (k == K_GTGT);
                    else if (k == K_ERR || k == K_ERRAPP)
                        s->err = w, s->err_append = (k == K_ERRAPP);
                    else
                        s->out = w, s->append = (k == K_ALLAPP), s->err_to_out = true;
                }
                end = sh->tok[sh->pos].off + sh->tok[sh->pos].len;
                sh->pos++;
            } else {
                break;
            }
        }
        bool assign_only = n == 0 && s->nenv && sh->tok[sh->pos].kind != K_PIPE;
        if (s->argc == 0 && !assign_only) {
            if (!R(run)) {
                syntax(sh, "expected a command");
                return v;
            }
            s->argv[s->argc++] = (char *) "true"; /* every word expanded to nothing */
        }
        n++;
        if (sh->tok[sh->pos].kind != K_PIPE) break;
        sh->pos++;
        skip_nl(sh);
        if (compound(sh) || at_terminator(sh)) {
            syntax(sh, "expected a command after |");
            return v;
        }
    }
    if (!R(run)) return v;
    sh->status = -1;
    if (overflow) {
        if (!sh->why[0]) set_why(sh, "too many arguments or words too long", NULL);
        v = refuse(sh, sh->why, NULL);
    } else if (n == 1 && st[0].argc == 0) { /* NAME=value ... alone sets variables */
        sh->why[0] = 0;
        v = T_OK;
        for (int i = 0; i < st[0].nenv; i++) {
            char *eq = strchr(st[0].env[i], '=');
            *eq = 0;
            if (!zxsh_set(sh, st[0].env[i], eq + 1))
                v = refuse(sh, "no room for variable ", st[0].env[i]);
            else if (sh->ops->getenv && sh->ops->setenv && sh->ops->getenv(sh->u, st[0].env[i]))
                sh->ops->setenv(sh->u, st[0].env[i],
                                eq + 1); /* already exported: keep it in step */
        }
    } else {
        v = dispatch(sh, st, n, capture ? cap : NULL, caplen);
    }
    if (neg) {
        v = lpx_not(v);
        sh->status = zxsh_code(v);
    }
    sh->arena_used = mark;
    finish(sh, v, off, end);
    return v;
}

/* S4 */
static lpx_t eval_check(zxsh_t *sh, bool run)
{
    uint32_t save = sh->pos;
    eval_pipeline(sh, false, false, NULL, NULL);
    bool x = sh->tok[sh->pos].kind == K_XCHK;
    sh->pos = save;
    if (!x || sh->syntax || sh->need_more) return eval_pipeline(sh, run, false, NULL, NULL);
    uint32_t la = 0, lb = 0;
    lpx_t a = eval_pipeline(sh, run, true, sh->capa, &la);
    sh->pos++; /* <?> */
    skip_nl(sh);
    if (at_terminator(sh)) {
        syntax(sh, "expected a command after <?>");
        return a;
    }
    lpx_t b = eval_pipeline(sh, run, true, sh->capb, &lb);
    if (sh->tok[sh->pos].kind == K_XCHK) syntax(sh, "one <?> per check; group the rest");
    if (!R(run)) return a;
    bool same = la == lb && memcmp(sh->capa, sh->capb, la) == 0;
    emit(sh, 1, sh->capa, la);
    if (!same) {
        say(sh, 1, "--- <?> the second command said:\n");
        emit(sh, 1, sh->capb, lb);
    }
    uint8_t ja = (uint8_t) lpx_base_to_lpres(lpx_base(a)),
            jb = (uint8_t) lpx_base_to_lpres(lpx_base(b));
    uint8_t base = lpx_base_from_lpres((lpres_state_t) (ja | jb));
    if (lpx_base(a) == LPX_B_TRUE && lpx_base(b) == LPX_B_TRUE && !same) base = LPX_B_BOTH;
    uint8_t pol = (same && lpx_base(a) == lpx_base(b)) ? LPX_P_NEGENTROPIC : LPX_P_ENTROPIC;
    lpx_t v = lpx_make(base, pol, LPX_PH_MANIFEST);
    if (la >= ZXSH_CAPTURE || lb >= ZXSH_CAPTURE)
        set_why(sh, "compared the first 8192 bytes only", NULL);
    else if (!same)
        set_why(sh, "the two commands' outputs differ", NULL);
    else if (lpx_base(a) != lpx_base(b))
        set_why(sh, "the two commands' verdicts differ", NULL);
    else
        set_why(sh, "two commands agree", NULL);
    sh->last = v;
    memcpy(sh->last_why, sh->why, sizeof sh->why);
    return v;
}

/* S3 */
static lpx_t eval_andor(zxsh_t *sh, bool run)
{
    lpx_t v = eval_check(sh, run);
    bool held = false;
    uint32_t hold_off = 0;
    lpx_t hold_tag = v;
    while (sh->tok[sh->pos].kind == K_AND || sh->tok[sh->pos].kind == K_OR) {
        bool is_and = sh->tok[sh->pos].kind == K_AND;
        uint32_t op_off = sh->tok[sh->pos].off;
        sh->pos++;
        skip_nl(sh);
        if (at_terminator(sh)) {
            syntax(sh, "expected a command after && or ||");
            return v;
        }
        bool go = false;
        if (R(run) && !held) {
            lpx_action_t act = lpx_action(v);
            if (act == LPX_ACT_PROCEED)
                go = is_and;
            else if (act == LPX_ACT_REFUSE)
                go = !is_and;
            else if (act == LPX_ACT_WAIT || act == LPX_ACT_ESCALATE) {
                held = true;
                hold_off = op_off;
                hold_tag = v;
            }
        }
        lpx_t r = eval_check(sh, go);
        if (go) v = r;
    }
    if (held && R(run)) hold(sh, hold_tag, false, hold_off, sh->tok[sh->pos].off);
    return v;
}

static lpx_t eval_list(zxsh_t *sh, bool run)
{
    lpx_t v = T_OK;
    for (;;) {
        while (sh->tok[sh->pos].kind == K_NL || sh->tok[sh->pos].kind == K_SEMI) sh->pos++;
        if (at_terminator(sh) || sh->syntax || sh->need_more) return v;
        uint8_t k = sh->tok[sh->pos].kind;
        if (k != K_WORD && !IS_REDIR(k)) {
            syntax(sh, "expected a command");
            return v;
        }
        /* S10: `list &` runs in the background; find its end with a dry pass */
        uint32_t start = sh->pos;
        bool bg = false;
        if (R(run) && sh->ops->background) {
            eval_andor(sh, false);
            bg = sh->tok[sh->pos].kind == K_AMP;
            if (!bg) sh->pos = start;
        }
        if (bg) {
            uint32_t off = sh->tok[start].off, end = sh->tok[sh->pos].off;
            while (end > off && (sh->src[end - 1] == ' ' || sh->src[end - 1] == '\t')) end--;
            uint32_t mark = sh->arena_used;
            if (end - off + 1 > ZXSH_ARENA - sh->arena_used) {
                v = refuse(sh, "background command too long", NULL);
            } else {
                char *text = sh->arena + sh->arena_used;
                memcpy(text, sh->src + off, end - off);
                text[end - off] = 0;
                sh->arena_used += end - off + 1;
                sh->why[0] = 0;
                v = sh->ops->background(sh->u, sh, text);
            }
            sh->arena_used = mark;
            sh->last = v;
            sh->status = zxsh_code(v);
            memcpy(sh->last_why, sh->why, sizeof sh->why);
        } else {
            v = eval_andor(sh, run);
        }
        k = sh->tok[sh->pos].kind;
        if (k == K_AMP) {
            sh->pos++;
            k = sh->tok[sh->pos].kind;
            if (k != K_NL && k != K_SEMI && k != K_EOF && !at_terminator(sh) && k != K_WORD &&
                !IS_REDIR(k)) {
                syntax(sh, "expected a command after &");
                return v;
            }
            continue;
        }
        if (k != K_NL && k != K_SEMI && !at_terminator(sh)) {
            syntax(sh, "expected ; or a new line");
            return v;
        }
    }
}

/* S10: while / until. A condition that is neither a plain yes nor a plain
 * no (both, neither, a weakening truth) stops the loop and is its verdict. */
static lpx_t eval_while(zxsh_t *sh, bool run)
{
    bool until = tok_is(sh, sh->pos, "until");
    sh->pos++;
    uint32_t cond = sh->pos;
    bool outer = sh->in_clause;
    sh->in_clause = false;
    eval_list(sh, false);
    if (!tok_is(sh, sh->pos, "do")) {
        sh->in_clause = outer;
        syntax(sh, "expected do");
        return T_OK;
    }
    uint32_t body = ++sh->pos;
    eval_list(sh, false);
    if (!tok_is(sh, sh->pos, "done")) {
        sh->in_clause = outer;
        syntax(sh, "expected done");
        return T_OK;
    }
    uint32_t after = sh->pos + 1;
    lpx_t acc = T_OK;
    if (R(run)) {
        sh->loops++;
        for (;;) {
            sh->pos = cond;
            lpx_t c = eval_list(sh, true);
            if (!R(true)) break;
            lpx_action_t act = lpx_action(c);
            bool go = act == (until ? LPX_ACT_REFUSE : LPX_ACT_PROCEED);
            bool stop = act == (until ? LPX_ACT_PROCEED : LPX_ACT_REFUSE);
            if (!go && !stop) { /* both / neither / weakening */
                acc = c;
                set_why(sh, "the loop stopped: its condition was ", zxsh_base_word(c));
                memcpy(sh->last_why, sh->why, sizeof sh->why);
                break;
            }
            if (stop) break;
            sh->pos = body;
            acc = lpx_and(acc, eval_list(sh, true));
            if (sh->cont) sh->cont = 0;
            if (sh->brk) {
                sh->brk--;
                break;
            }
        }
        sh->loops--;
        sh->last = acc;
        if (sh->status < 0) sh->status = zxsh_code(acc);
    }
    sh->in_clause = outer;
    sh->pos = after;
    return acc;
}

/* S7 */
static lpx_t eval_if(zxsh_t *sh, bool run)
{
    sh->pos++; /* if */
    bool outer = sh->in_clause;
    sh->in_clause = false;
    lpx_t cond = eval_list(sh, run);
    sh->in_clause = outer;
    if (!tok_is(sh, sh->pos, "then")) {
        syntax(sh, "expected then");
        return cond;
    }
    sh->in_clause = true;
    lpx_t res = eval_if_clauses(sh, run, cond);
    sh->in_clause = outer;
    return res;
}

static lpx_t eval_if_clauses(zxsh_t *sh, bool run, lpx_t cond)
{
    uint32_t then_tok = sh->pos;
    /* which clauses exist? (a dry pass over the rest of the if) */
    bool has[4] = {false, false, false, false}; /* then else both neither */
    static const char *const CL[4] = {"then", "else", "both", "neither"};
    for (int pass = 0; pass < 2; pass++) {
        int choose = -1;
        bool run_any = pass == 1 && R(run);
        if (run_any) {
            lpx_action_t act = lpx_action(cond);
            if (act == LPX_ACT_PROCEED)
                choose = 0;
            else if (act == LPX_ACT_REFUSE)
                choose = 1;
            else if (act != LPX_ACT_ROLLBACK) {
                if (lpx_base(cond) == LPX_B_BOTH && has[2])
                    choose = 2;
                else if (lpx_base(cond) == LPX_B_NEITHER && has[3])
                    choose = 3;
                else
                    choose = 4; /* hold */
            }
        }
        sh->pos = then_tok;
        lpx_t v = T_OK;
        bool seen[4] = {false, false, false, false};
        while (!sh->syntax && !sh->need_more) {
            int c = -1;
            for (int i = 0; i < 4; i++)
                if (tok_is(sh, sh->pos, CL[i])) c = i;
            if (c < 0) break;
            if (seen[c] || (c == 0) != (sh->pos == then_tok)) {
                syntax(sh, "misplaced or repeated if clause");
                return cond;
            }
            seen[c] = true;
            sh->pos++;
            lpx_t r = eval_list(sh, run_any && choose == c);
            if (run_any && choose == c) v = r;
        }
        if (!tok_is(sh, sh->pos, "fi")) {
            syntax(sh, "expected fi");
            return cond;
        }
        sh->pos++;
        if (pass == 0) {
            memcpy(has, seen, sizeof has);
            if (!R(run)) return cond;
            continue;
        }
        if (choose == 4) {
            hold(sh, cond, true, sh->tok[then_tok].off, sh->tok[sh->pos - 1].off + 2);
            return cond;
        }
        if (choose < 0) return cond;
        if (!has[choose]) v = T_OK; /* S7: nothing was asked */
        sh->last = v;
        return v;
    }
    return cond;
}

/* S8 */
static lpx_t eval_for(zxsh_t *sh, bool run)
{
    sh->pos++; /* for */
    const zxsh_tok_t *nt = &sh->tok[sh->pos];
    if (nt->kind != K_WORD || !name_ok(sh->src + nt->off, nt->len)) {
        syntax(sh, "for needs a variable name");
        return T_OK;
    }
    char name[ZXSH_NAME_MAX];
    memcpy(name, sh->src + nt->off, nt->len);
    name[nt->len] = 0;
    sh->pos++;
    if (!tok_is(sh, sh->pos, "in")) {
        syntax(sh, "expected in");
        return T_OK;
    }
    sh->pos++;
    uint32_t w0 = sh->pos;
    while (sh->tok[sh->pos].kind == K_WORD && !tok_is(sh, sh->pos, "do")) sh->pos++;
    uint32_t w1 = sh->pos;
    while (sh->tok[sh->pos].kind == K_NL || sh->tok[sh->pos].kind == K_SEMI) sh->pos++;
    if (!tok_is(sh, sh->pos, "do")) {
        syntax(sh, "expected do");
        return T_OK;
    }
    sh->pos++;
    uint32_t body = sh->pos;
    lpx_t acc = T_OK;
    bool outer = sh->in_clause;
    sh->in_clause = false;
    bool ran = false;
    if (R(run)) {
        sh->loops++;
        bool done = false;
        for (uint32_t i = w0; i < w1 && !done && R(run); i++) {
            uint32_t mark = sh->arena_used;
            bool g;
            char *w = expand(sh, i, &g, false);
            if (!w) {
                sh->arena_used = mark;
                acc = lpx_and(acc, refuse(sh, "for: word too long", NULL));
                continue;
            }
            /* each SPLIT-separated part of an unquoted $(...) is one item */
            bool split = strchr(w, SPLIT) != NULL;
            for (char *p = w; !done && R(run);) {
                char *e = p;
                while (*e && *e != SPLIT) e++;
                bool more = *e != 0;
                *e = 0;
                if (*p || !split) {
                    char val[ZXSH_VALUE_MAX];
                    size_t n = strlen(p);
                    if (n >= sizeof val) {
                        acc = lpx_and(acc, refuse(sh, "for: word too long", NULL));
                    } else {
                        memcpy(val, p, n + 1);
                        zxsh_set(sh, name, val);
                        uint32_t keep = sh->arena_used;
                        sh->pos = body;
                        acc = lpx_and(acc, eval_list(sh, true));
                        sh->arena_used = keep;
                        ran = true;
                        if (sh->cont) sh->cont = 0;
                        if (sh->brk) {
                            sh->brk--;
                            done = true;
                        }
                    }
                }
                if (!more) break;
                p = e + 1;
            }
            sh->arena_used = mark;
        }
        sh->loops--;
    }
    if (!ran) {
        sh->pos = body;
        eval_list(sh, false);
    } else {
        sh->pos = body;
        eval_list(sh, false); /* reposition after the body */
    }
    sh->in_clause = outer;
    if (!tok_is(sh, sh->pos, "done")) {
        syntax(sh, "expected done");
        return acc;
    }
    sh->pos++;
    if (R(run)) sh->last = acc;
    return acc;
}

int zxsh_word_kind(const char *name)
{
    static const char *const KW[] = {"if",   "then",    "else", "fi",    "for",
                                     "in",   "do",      "done", "while", "until",
                                     "both", "neither", "!",    "<?>",   NULL};
    if (zxsh_is_builtin(name)) return 1;
    if (is_host_builtin(name)) return 2;
    for (unsigned i = 0; KW[i]; i++)
        if (!strcmp(name, KW[i])) return 3;
    return 0;
}

/* The parse state a nested evaluation would overwrite. */
typedef struct {
    const char *src;
    uint32_t ntok, pos, arena_used;
    bool aborted, need_more, syntax, in_clause;
    char *cap_buf;
    uint32_t *cap_len;
} frame_t;

size_t zxsh_frame_size(const zxsh_t *sh)
{
    return sizeof(frame_t) + sh->ntok * sizeof(zxsh_tok_t) + sh->arena_used;
}

zxsh_status_t zxsh_eval_nested(zxsh_t *sh, const char *src, void *frame)
{
    frame_t f = {sh->src,       sh->ntok,   sh->pos,       sh->arena_used, sh->aborted,
                 sh->need_more, sh->syntax, sh->in_clause, sh->cap_buf,    sh->cap_len};
    char *p = (char *) frame;
    memcpy(p, &f, sizeof f);
    memcpy(p + sizeof f, sh->tok, f.ntok * sizeof(zxsh_tok_t));
    memcpy(p + sizeof f + f.ntok * sizeof(zxsh_tok_t), sh->arena, f.arena_used);
    zxsh_status_t st = zxsh_eval(sh, src);
    memcpy(sh->tok, p + sizeof f, f.ntok * sizeof(zxsh_tok_t));
    memcpy(sh->arena, p + sizeof f + f.ntok * sizeof(zxsh_tok_t), f.arena_used);
    sh->src = f.src;
    sh->ntok = f.ntok;
    sh->pos = f.pos;
    sh->arena_used = f.arena_used;
    sh->need_more = f.need_more;
    sh->syntax = f.syntax;
    sh->in_clause = f.in_clause;
    sh->cap_buf = f.cap_buf;
    sh->cap_len = f.cap_len;
    sh->aborted = f.aborted || st == ZXSH_ABORTED;
    return st;
}

bool zxsh_builtin(zxsh_t *sh, const zxsh_stage_t *st, lpx_t *v)
{
    return st->argc > 0 && zxsh_is_builtin(st->argv[0]) && builtin(sh, st, v);
}

/* Aliases: replace a command word that names an alias (typed lines only). */
static void expand_aliases(zxsh_t *sh)
{
    static const char *const CMDPOS[] = {"then", "else", "do", "if", "while", "until", "!", NULL};
    uint32_t o = 0, from = 0;
    bool cmd = true, any = false;
    for (uint32_t t = 0; t < sh->ntok; t++) {
        const zxsh_tok_t *k = &sh->tok[t];
        bool next = false;
        if (k->kind == K_WORD) {
            for (unsigned i = 0; CMDPOS[i]; i++)
                if (tok_is(sh, t, CMDPOS[i])) next = true;
            char nm[ZXSH_NAME_MAX];
            const char *val = NULL;
            if (cmd && !next && k->len < sizeof nm && name_ok(sh->src + k->off, k->len)) {
                memcpy(nm, sh->src + k->off, k->len);
                nm[k->len] = 0;
                val = zxsh_alias_get(sh, nm);
            }
            if (val) {
                size_t m = strlen(val), pre = k->off - from;
                if (o + pre + m + 1 >= ZXSH_ALIAS_SRC) return; /* too long: leave it as typed */
                memcpy(sh->asrc + o, sh->src + from, pre);
                o += (uint32_t) pre;
                memcpy(sh->asrc + o, val, m);
                o += (uint32_t) m;
                from = k->off + k->len;
                any = true;
            }
        } else {
            next = k->kind != K_EOF && !IS_REDIR(k->kind);
        }
        cmd = next;
    }
    if (!any) return;
    size_t rest = strlen(sh->src + from);
    if (o + rest + 1 >= ZXSH_ALIAS_SRC) return;
    memcpy(sh->asrc + o, sh->src + from, rest + 1);
    sh->src = sh->asrc;
    lex(sh);
}

/* ---------------------------------------------------------------- driver */

void zxsh_init(zxsh_t *sh, const zxsh_ops_t *ops, void *u, bool interactive)
{
    memset(sh, 0, sizeof *sh);
    sh->ops = ops;
    sh->u = u;
    sh->interactive = interactive;
    sh->caps = ZXSH_CAP_ALL & ~(interactive ? 0u : ZXSH_CAP_RESOLVE); /* S5 */
    sh->last = T_OK;
    sh->status = 0;
    lpx_qlog_init(&sh->qlog, sh->qrec, ZXSH_QLOG_CAP);
}

static zxsh_status_t eval_once(zxsh_t *sh, const char *src)
{
    sh->src = src;
    sh->syntax = sh->need_more = sh->aborted = sh->in_clause = false;
    if (sh->depth == 1) { /* a nested `source` keeps the outer loops and Ctrl-C */
        sh->interrupt = 0;
        sh->brk = sh->cont = sh->loops = 0;
    }
    sh->cap_buf = NULL;
    sh->why[0] = 0;
    lex(sh);
    if (sh->syntax) goto bad;
    if (sh->need_more) return ZXSH_NEED_MORE;
    if (sh->interactive && sh->depth == 1) expand_aliases(sh);
    if (sh->syntax) goto bad;
    if (sh->need_more) return ZXSH_NEED_MORE;
    /* pass 1: parse only, so a syntax error means nothing ran */
    sh->pos = 0;
    eval_list(sh, false);
    if (!sh->syntax && !sh->need_more && sh->tok[sh->pos].kind != K_EOF)
        syntax(sh, "unexpected keyword");
    if (sh->need_more) return ZXSH_NEED_MORE;
    if (sh->syntax) goto bad;
    /* pass 2: run */
    sh->pos = 0;
    sh->arena_used = 0;
    eval_list(sh, true);
    if (sh->interrupt && sh->depth == 1) { /* S11: an interrupted line has no answer */
        sh->last = lpx_make(LPX_B_NEITHER, LPX_P_ENTROPIC, LPX_PH_MANIFEST);
        sh->status = 130;
        set_why(sh, "interrupted (Ctrl-C)", NULL);
        memcpy(sh->last_why, sh->why, sizeof sh->why);
    }
    if (sh->exiting) return ZXSH_EXIT;
    return sh->aborted ? ZXSH_ABORTED : ZXSH_OK;
bad:
    if (!sh->quiet_syntax) {
        say(sh, 2, "zxsh: ");
        say(sh, 2, sh->why);
        say(sh, 2, "\n");
    }
    return ZXSH_SYNTAX;
}

zxsh_status_t zxsh_eval(zxsh_t *sh, const char *src)
{
    if (sh->locked) {
        say(sh, 2, "zxsh: locked (quarantine log full)\n");
        return ZXSH_ABORTED;
    }
    if (sh->depth >= ZXSH_MAX_DEPTH) return ZXSH_SYNTAX;
    sh->depth++;
    zxsh_status_t st = eval_once(sh, src);
    /* S5: a resolved hold runs after the line that resolved it */
    while (st == ZXSH_OK && sh->has_pending) {
        char buf[sizeof sh->pending];
        memcpy(buf, sh->pending, sizeof buf);
        sh->has_pending = false;
        st = eval_once(sh, buf);
        if (st == ZXSH_NEED_MORE) st = ZXSH_SYNTAX;
    }
    sh->has_pending = false;
    sh->depth--;
    return st;
}
