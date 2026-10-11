/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* tb_parse.c — the Trebago parser (README.md, "Trebago").
 *
 * A line-oriented lexer and a recursive-descent parser that builds the
 * program's tables: symbols, dimension names, expression nodes, loop
 * index slots and statements. Every refusal carries the line and column
 * of the token that caused it. Literals are read exactly by
 * zxv_rat_parse; constants are folded by the executor's evaluator.
 * Integer code only (R11). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tb_internal.h"

enum {
    T_EOF = 0, T_NL, T_NAME, T_NUM, T_LP, T_RP, T_COMMA, T_EQ, T_COLON,
    T_PLUS, T_MINUS, T_STAR, T_SLASH, T_POW
};

#define MAX_SCOPE 64

typedef struct {
    const char *t;
    size_t len, pos, line_start;
    uint32_t line;
    /* the current token */
    int tok;
    size_t tpos, tlen;
    uint32_t tline, tcol;
    char name[TB_MAX_NAME + 1]; /* T_NAME, lower case */
    int64_t num, den;           /* T_NUM */
    int is_int;                 /* T_NUM written without a '.' */
    /* state */
    tb_program *p;
    tb_diag *diag;
    int32_t st;
    int32_t scope[MAX_SCOPE];
    uint32_t nscope;
    uint32_t stmt_rank;
    int in_sum, in_scan, in_const;
} P;

static const char *const k_reserved[] = {"trebago", "end", "in",  "out", "inout", "var",
                                         "const",   "sum", "scan", "abs", "min",   "max"};

static int is_reserved(const char *s) {
    for (size_t i = 0; i < sizeof k_reserved / sizeof k_reserved[0]; i++)
        if (strcmp(s, k_reserved[i]) == 0) return 1;
    return 0;
}

static int32_t fail_at(P *x, int32_t status, uint32_t line, uint32_t col, size_t off,
                       const char *what) {
    if (x->st == ZXV_OK) x->st = tb_diag_at(x->diag, status, line, col, off, "%s", what);
    return x->st;
}

#define FAIL(x, status, ...)                                                                  \
    do {                                                                                      \
        char m_[320];                                                                         \
        snprintf(m_, sizeof m_, __VA_ARGS__);                                                 \
        fail_at((x), (status), (x)->tline, (x)->tcol, (x)->tpos, m_);                         \
    } while (0)

/* ===== lexer ===== */

static int is_alpha(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
static int is_digit(int c) { return c >= '0' && c <= '9'; }

static void lex(P *x) {
    const char *t = x->t;
    if (x->st != ZXV_OK) {
        x->tok = T_EOF;
        return;
    }
    for (;;) {
        while (x->pos < x->len && (t[x->pos] == ' ' || t[x->pos] == '\t' || t[x->pos] == '\r'))
            x->pos++;
        if (x->pos < x->len && t[x->pos] == '!')
            while (x->pos < x->len && t[x->pos] != '\n') x->pos++;
        if (x->pos < x->len && t[x->pos] == '&') {
            /* continuation: '&' then only blanks or a comment to the end of the line */
            size_t amp = x->pos++;
            while (x->pos < x->len && (t[x->pos] == ' ' || t[x->pos] == '\t' || t[x->pos] == '\r'))
                x->pos++;
            if (x->pos < x->len && t[x->pos] == '!')
                while (x->pos < x->len && t[x->pos] != '\n') x->pos++;
            if (x->pos >= x->len || t[x->pos] != '\n') {
                x->tpos = amp;
                x->tline = x->line;
                x->tcol = (uint32_t) (amp - x->line_start + 1u);
                FAIL(x, TB_E_SYNTAX, "'&' continues a statement on the next line, so it must end its line");
                x->tok = T_EOF;
                return;
            }
            x->pos++;
            x->line++;
            x->line_start = x->pos;
            continue;
        }
        break;
    }
    x->tpos = x->pos;
    x->tline = x->line;
    x->tcol = (uint32_t) (x->pos - x->line_start + 1u);
    x->tlen = 1;
    if (x->pos >= x->len) {
        x->tok = T_EOF;
        x->tlen = 0;
        return;
    }
    {
        int c = (unsigned char) t[x->pos];
        if (c == '\n') {
            x->pos++;
            x->line++;
            x->line_start = x->pos;
            x->tok = T_NL;
            return;
        }
        if (is_alpha(c)) {
            size_t s = x->pos, n;
            while (x->pos < x->len &&
                   (is_alpha((unsigned char) t[x->pos]) || is_digit((unsigned char) t[x->pos]) ||
                    t[x->pos] == '_'))
                x->pos++;
            n = x->pos - s;
            x->tlen = n;
            if (n > TB_MAX_NAME) {
                FAIL(x, TB_E_SYNTAX, "a name has at most %u characters", TB_MAX_NAME);
                x->tok = T_EOF;
                return;
            }
            for (size_t i = 0; i < n; i++) {
                char ch = t[s + i];
                x->name[i] = (ch >= 'A' && ch <= 'Z') ? (char) (ch - 'A' + 'a') : ch;
            }
            x->name[n] = '\0';
            x->tok = T_NAME;
            return;
        }
        if (is_digit(c)) {
            size_t s = x->pos, n;
            char buf[80];
            int32_t st;
            x->is_int = 1;
            while (x->pos < x->len && is_digit((unsigned char) t[x->pos])) x->pos++;
            if (x->pos < x->len && t[x->pos] == '.') {
                x->is_int = 0;
                x->pos++;
                if (x->pos >= x->len || !is_digit((unsigned char) t[x->pos])) {
                    FAIL(x, TB_E_SYNTAX, "a decimal point needs digits after it (write 2.0, not 2.)");
                    x->tok = T_EOF;
                    return;
                }
                while (x->pos < x->len && is_digit((unsigned char) t[x->pos])) x->pos++;
            }
            n = x->pos - s;
            x->tlen = n;
            if (x->pos < x->len && (is_alpha((unsigned char) t[x->pos]) || t[x->pos] == '_' ||
                                    t[x->pos] == '.')) {
                int ch = (unsigned char) t[x->pos];
                if (ch == 'e' || ch == 'E' || ch == 'd' || ch == 'D')
                    FAIL(x, TB_E_SYNTAX,
                         "exponent notation is not exact here: write the number out in full, e.g. 0.001 or 1/1000 for 1.0e-3, 1000000 for 1.0e6");
                else if (ch == '_')
                    FAIL(x, TB_E_SYNTAX, "kind suffixes (1.0_dp) are not Trebago: every number is exact");
                else
                    FAIL(x, TB_E_SYNTAX, "a number must not run into a name or another '.'");
                x->tok = T_EOF;
                return;
            }
            if (n >= sizeof buf) {
                FAIL(x, ZXV_E_RANGE, "this number does not fit an exact int64/int64 rational");
                x->tok = T_EOF;
                return;
            }
            memcpy(buf, t + s, n);
            buf[n] = '\0';
            st = zxv_rat_parse(buf, &x->num, &x->den);
            if (st != ZXV_OK) {
                FAIL(x, st == ZXV_E_RANGE ? ZXV_E_RANGE : TB_E_SYNTAX,
                     "%s does not fit an exact int64/int64 rational", buf);
                x->tok = T_EOF;
                return;
            }
            x->tok = T_NUM;
            return;
        }
        x->pos++;
        switch (c) {
        case '(': x->tok = T_LP; return;
        case ')': x->tok = T_RP; return;
        case ',': x->tok = T_COMMA; return;
        case '=': x->tok = T_EQ; return;
        case ':': x->tok = T_COLON; return;
        case '+': x->tok = T_PLUS; return;
        case '-': x->tok = T_MINUS; return;
        case '/': x->tok = T_SLASH; return;
        case '*':
            if (x->pos < x->len && t[x->pos] == '*') {
                x->pos++;
                x->tlen = 2;
                x->tok = T_POW;
            } else {
                x->tok = T_STAR;
            }
            return;
        default: break;
        }
        if (c >= 0x20 && c < 0x7F)
            FAIL(x, TB_E_SYNTAX, "unexpected character '%c'", c);
        else
            FAIL(x, TB_E_SYNTAX, "unexpected byte 0x%02x", (unsigned) c);
        x->tok = T_EOF;
    }
}

static const char *tok_text(int tok) {
    switch (tok) {
    case T_EOF: return "the end of the text";
    case T_NL: return "the end of the line";
    case T_NAME: return "a name";
    case T_NUM: return "a number";
    case T_LP: return "'('";
    case T_RP: return "')'";
    case T_COMMA: return "','";
    case T_EQ: return "'='";
    case T_COLON: return "':'";
    case T_PLUS: return "'+'";
    case T_MINUS: return "'-'";
    case T_STAR: return "'*'";
    case T_SLASH: return "'/'";
    case T_POW: return "'**'";
    default: return "?";
    }
}

static int expect(P *x, int tok, const char *context) {
    if (x->st != ZXV_OK) return 0;
    if (x->tok != tok) {
        if (x->tok == T_NAME)
            FAIL(x, TB_E_SYNTAX, "expected %s %s, found '%s'", tok_text(tok), context, x->name);
        else
            FAIL(x, TB_E_SYNTAX, "expected %s %s, found %s", tok_text(tok), context, tok_text(x->tok));
        return 0;
    }
    lex(x);
    return 1;
}

static void skip_nl(P *x) {
    while (x->st == ZXV_OK && x->tok == T_NL) lex(x);
}

static void expect_eol(P *x) {
    if (x->st != ZXV_OK) return;
    if (x->tok != T_NL && x->tok != T_EOF) {
        if (x->tok == T_NAME)
            FAIL(x, TB_E_SYNTAX, "expected the end of the line, found '%s' (one statement per line)", x->name);
        else
            FAIL(x, TB_E_SYNTAX, "expected the end of the line, found %s", tok_text(x->tok));
    }
}

/* ===== tables ===== */

static int32_t find_sym(const tb_program *p, const char *n) {
    for (uint32_t i = 0; i < p->nsyms; i++)
        if (strcmp(p->syms[i].name, n) == 0) return (int32_t) i;
    return -1;
}

static int32_t find_dim(const tb_program *p, const char *n) {
    for (uint32_t i = 0; i < p->ndims; i++)
        if (strcmp(p->dims[i].name, n) == 0) return (int32_t) i;
    return -1;
}

static int32_t find_slot(const P *x, const char *n) {
    for (uint32_t i = x->nscope; i-- > 0;)
        if (strcmp(x->p->slots[x->scope[i]].name, n) == 0) return x->scope[i];
    return -1;
}

static int grow(void **arr, uint32_t *cap, uint32_t need, size_t elem) {
    if (need <= *cap) return 1;
    {
        uint32_t nc = *cap ? *cap : 16u;
        void *n;
        while (nc < need) nc *= 2u;
        n = realloc(*arr, (size_t) nc * elem);
        if (!n) return 0;
        *arr = n;
        *cap = nc;
    }
    return 1;
}

static int32_t mk(P *x, int kind, int32_t a, int32_t b, int32_t c, int32_t d, uint32_t line,
                  uint32_t col) {
    tb_program *p = x->p;
    tb_node *n;
    uint32_t depth = 0;
    const int32_t kids[4] = {a, b, c, d};
    if (x->st != ZXV_OK) return -1;
    if (p->nnodes >= TB_MAX_NODES) {
        fail_at(x, TB_E_LIMIT, line, col, x->tpos, "too many expression nodes in one program");
        return -1;
    }
    if (!grow((void **) &p->nodes, &p->cap_nodes, p->nnodes + 1u, sizeof(tb_node))) {
        x->st = ZXV_E_NOMEM;
        return -1;
    }
    /* children are node ids for these kinds only */
    if (kind != N_NUM && kind != N_IDX && kind != N_DIM && kind != N_SYM) {
        for (int k = 0; k < 4; k++) {
            if (k == 0 && (kind == N_ELEM || kind == N_SUM)) continue; /* a is a sym / slot */
            if (kids[k] >= 0 && p->nodes[kids[k]].depth > depth) depth = p->nodes[kids[k]].depth;
        }
    }
    depth++;
    if (depth > TB_MAX_DEPTH) {
        fail_at(x, TB_E_LIMIT, line, col, x->tpos,
                "expression nested too deep; split it into several statements");
        return -1;
    }
    n = &p->nodes[p->nnodes];
    memset(n, 0, sizeof *n);
    n->kind = (uint8_t) kind;
    n->depth = (uint16_t) depth;
    n->a = a;
    n->b = b;
    n->c = c;
    n->d = d;
    n->num = 0;
    n->den = 1;
    n->line = line;
    n->col = col;
    return (int32_t) p->nnodes++;
}

static int32_t new_slot(P *x, const char *name, uint32_t line, uint32_t col) {
    tb_program *p = x->p;
    if (x->st != ZXV_OK) return -1;
    if (is_reserved(name)) {
        fail_at(x, TB_E_NAME, line, col, x->tpos, "a reserved word cannot be a loop index");
        return -1;
    }
    if (find_sym(p, name) >= 0 || find_dim(p, name) >= 0 || find_slot(x, name) >= 0) {
        char m[160];
        snprintf(m, sizeof m, "'%s' is already a name here; pick another loop index", name);
        fail_at(x, TB_E_NAME, line, col, x->tpos, m);
        return -1;
    }
    if (!grow((void **) &p->slots, &p->cap_slots, p->nslots + 1u, sizeof(tb_slot))) {
        x->st = ZXV_E_NOMEM;
        return -1;
    }
    memset(&p->slots[p->nslots], 0, sizeof(tb_slot));
    memcpy(p->slots[p->nslots].name, name, strlen(name) + 1u);
    p->slots[p->nslots].range_sym = -1;
    p->slots[p->nslots].range_axis = -1;
    return (int32_t) p->nslots++;
}

static void push(P *x, int32_t slot) {
    if (x->st != ZXV_OK) return;
    if (x->nscope >= MAX_SCOPE) {
        FAIL(x, TB_E_LIMIT, "more than %u nested loop indices", MAX_SCOPE);
        return;
    }
    x->scope[x->nscope++] = slot;
}

static void add_use(P *x, int32_t slot, int32_t sym, int32_t axis, uint32_t line, uint32_t col) {
    tb_program *p = x->p;
    if (x->st != ZXV_OK) return;
    if (!grow((void **) &p->uses, &p->cap_uses, p->nuses + 1u, sizeof(tb_use))) {
        x->st = ZXV_E_NOMEM;
        return;
    }
    p->uses[p->nuses].slot = slot;
    p->uses[p->nuses].sym = sym;
    p->uses[p->nuses].axis = axis;
    p->uses[p->nuses].line = line;
    p->uses[p->nuses].col = col;
    p->nuses++;
}

/* ===== expressions ===== */

static int32_t expr(P *x);
static int32_t factor(P *x);

static int32_t need_int(P *x, int32_t n, const char *what) {
    if (n < 0 || x->st != ZXV_OK) return -1;
    if (!x->p->nodes[n].is_int) {
        fail_at(x, TB_E_SYNTAX, x->p->nodes[n].line, x->p->nodes[n].col, x->tpos, what);
        return -1;
    }
    return n;
}

static int32_t call_sum(P *x, uint32_t line, uint32_t col) {
    int32_t slot, lo = -1, hi = -1, body, n;
    uint32_t il, ic, first_use;
    if (x->in_const) {
        FAIL(x, TB_E_SYNTAX, "a constant cannot use sum");
        return -1;
    }
    if (!expect(x, T_LP, "after sum")) return -1;
    if (x->tok != T_NAME) {
        FAIL(x, TB_E_SYNTAX, "expected a loop index after sum(, as in sum(i, x(i))");
        return -1;
    }
    il = x->tline;
    ic = x->tcol;
    {
        char nm[TB_MAX_NAME + 1];
        memcpy(nm, x->name, sizeof nm);
        lex(x);
        if (x->tok == T_EQ) {
            lex(x);
            lo = need_int(x, expr(x), "a sum's lower bound must be an integer expression");
            if (!expect(x, T_COLON, "between a sum's bounds (sum(i = 1:n, ...))")) return -1;
            hi = need_int(x, expr(x), "a sum's upper bound must be an integer expression");
        }
        if (!expect(x, T_COMMA, "after the sum's index")) return -1;
        slot = new_slot(x, nm, il, ic);
    }
    if (slot < 0) return -1;
    first_use = x->p->nuses;
    push(x, slot);
    x->in_sum++;
    body = expr(x);
    x->in_sum--;
    x->nscope--;
    if (!expect(x, T_RP, "to close sum(")) return -1;
    if (x->st != ZXV_OK) return -1;
    if (lo < 0) {
        for (uint32_t u = first_use; u < x->p->nuses; u++)
            if (x->p->uses[u].slot == slot) {
                x->p->slots[slot].range_sym = x->p->uses[u].sym;
                x->p->slots[slot].range_axis = x->p->uses[u].axis;
                break;
            }
        if (x->p->slots[slot].range_axis < 0) {
            char m[200];
            snprintf(m, sizeof m,
                     "cannot tell the range of '%s': use it alone as a subscript (x(%s)) or give "
                     "bounds (sum(%s = 1:n, ...))",
                     x->p->slots[slot].name, x->p->slots[slot].name, x->p->slots[slot].name);
            fail_at(x, TB_E_SHAPE, il, ic, x->tpos, m);
            return -1;
        }
    }
    n = mk(x, N_SUM, slot, body, lo, hi, line, col);
    return n;
}

static int32_t call_fn(P *x, const char *fn, uint32_t line, uint32_t col) {
    int32_t a, b;
    if (!expect(x, T_LP, "after the function name")) return -1;
    if (strcmp(fn, "scan") == 0) {
        if (x->in_const || x->stmt_rank != 1 || x->in_sum || x->in_scan) {
            fail_at(x, TB_E_SYNTAX, line, col, x->tpos,
                    "scan(...) is the running sum along a vector statement's index "
                    "(p(i) = scan(x(i))); it cannot appear here");
            return -1;
        }
        x->in_scan = 1;
        a = expr(x);
        x->in_scan = 0;
        if (!expect(x, T_RP, "to close scan(")) return -1;
        return mk(x, N_SCAN, a, -1, -1, -1, line, col);
    }
    a = expr(x);
    if (strcmp(fn, "abs") == 0) {
        if (!expect(x, T_RP, "to close abs(")) return -1;
        return mk(x, N_ABS, a, -1, -1, -1, line, col);
    }
    /* min / max: two or more arguments, folded from the left */
    if (!expect(x, T_COMMA, "(min and max take two or more arguments)"))
        return -1;
    b = expr(x);
    a = mk(x, fn[1] == 'i' ? N_MIN : N_MAX, a, b, -1, -1, line, col);
    while (x->st == ZXV_OK && x->tok == T_COMMA) {
        lex(x);
        b = expr(x);
        a = mk(x, fn[1] == 'i' ? N_MIN : N_MAX, a, b, -1, -1, line, col);
    }
    if (!expect(x, T_RP, "to close the argument list")) return -1;
    return a;
}

static int32_t primary(P *x) {
    uint32_t line = x->tline, col = x->tcol;
    tb_program *p = x->p;
    if (x->st != ZXV_OK) return -1;
    if (x->tok == T_NUM) {
        int32_t n = mk(x, N_NUM, -1, -1, -1, -1, line, col);
        if (n < 0) return -1;
        p->nodes[n].num = x->num;
        p->nodes[n].den = x->den;
        p->nodes[n].is_int = (uint8_t) x->is_int;
        lex(x);
        return n;
    }
    if (x->tok == T_LP) {
        int32_t n;
        lex(x);
        n = expr(x);
        if (!expect(x, T_RP, "to close '('")) return -1;
        return n;
    }
    if (x->tok != T_NAME) {
        FAIL(x, TB_E_SYNTAX, "expected a number, a name or '(', found %s", tok_text(x->tok));
        return -1;
    }
    {
        char nm[TB_MAX_NAME + 1];
        int32_t s, slot, dim;
        memcpy(nm, x->name, sizeof nm);
        lex(x);
        if (strcmp(nm, "sum") == 0) return call_sum(x, line, col);
        if (strcmp(nm, "scan") == 0 || strcmp(nm, "abs") == 0 || strcmp(nm, "min") == 0 ||
            strcmp(nm, "max") == 0)
            return call_fn(x, nm, line, col);
        if (is_reserved(nm)) {
            fail_at(x, TB_E_SYNTAX, line, col, x->tpos, "a keyword cannot appear in an expression");
            return -1;
        }
        s = find_sym(p, nm);
        slot = find_slot(x, nm);
        dim = find_dim(p, nm);
        if (x->tok == T_LP) {
            int32_t sub[TB_MAX_RANK] = {-1, -1}, n;
            uint32_t r;
            char m[200];
            if (s < 0) {
                snprintf(m, sizeof m, "'%s' is not an array%s", nm,
                         (slot >= 0 || dim >= 0) ? "" : " (no declaration names it)");
                fail_at(x, (slot >= 0 || dim >= 0) ? TB_E_SYNTAX : TB_E_NAME, line, col, x->tpos, m);
                return -1;
            }
            if (p->syms[s].rank == 0) {
                snprintf(m, sizeof m, "'%s' is a scalar; it takes no subscript", nm);
                fail_at(x, TB_E_SHAPE, line, col, x->tpos, m);
                return -1;
            }
            if (x->in_const) {
                fail_at(x, TB_E_SYNTAX, line, col, x->tpos, "a constant cannot use an array");
                return -1;
            }
            lex(x);
            for (r = 0; r < p->syms[s].rank; r++) {
                uint32_t sl = x->tline, sc = x->tcol;
                if (r && x->tok == T_RP) {
                    FAIL(x, TB_E_SHAPE, "'%s' has rank %u: too few subscripts", nm, (unsigned) p->syms[s].rank);
                    return -1;
                }
                if (r && !expect(x, T_COMMA, "between subscripts")) return -1;
                if (r) {
                    sl = x->tline;
                    sc = x->tcol;
                }
                sub[r] = need_int(x, expr(x),
                                  "a subscript must be an integer expression: loop indices, "
                                  "dimensions and integer literals with + - *");
                if (sub[r] < 0) return -1;
                if (p->nodes[sub[r]].kind == N_IDX)
                    add_use(x, p->nodes[sub[r]].a, s, (int32_t) r, sl, sc);
            }
            if (x->tok == T_COMMA) {
                snprintf(m, sizeof m, "'%s' has rank %u: too many subscripts", nm, (unsigned) p->syms[s].rank);
                FAIL(x, TB_E_SHAPE, "%s", m);
                return -1;
            }
            if (!expect(x, T_RP, "to close the subscripts")) return -1;
            n = mk(x, N_ELEM, s, sub[0], sub[1], -1, line, col);
            return n;
        }
        if (slot >= 0) {
            int32_t n = mk(x, N_IDX, slot, -1, -1, -1, line, col);
            if (n >= 0) p->nodes[n].is_int = 1;
            return n;
        }
        if (dim >= 0) {
            int32_t n;
            if (x->in_const) {
                fail_at(x, TB_E_SYNTAX, line, col, x->tpos, "a constant cannot use a dimension");
                return -1;
            }
            n = mk(x, N_DIM, dim, -1, -1, -1, line, col);
            if (n >= 0) p->nodes[n].is_int = 1;
            return n;
        }
        if (s >= 0) {
            char m[200];
            if (p->syms[s].rank > 0) {
                snprintf(m, sizeof m, "'%s' is an array; subscript it (%s(i))", nm, nm);
                fail_at(x, TB_E_SHAPE, line, col, x->tpos, m);
                return -1;
            }
            if (x->in_const && p->syms[s].role != TB_ROLE_CONST) {
                fail_at(x, TB_E_SYNTAX, line, col, x->tpos,
                        "a constant can use only numbers and earlier constants");
                return -1;
            }
            return mk(x, N_SYM, s, -1, -1, -1, line, col);
        }
        {
            char m[200];
            snprintf(m, sizeof m, "unknown name '%s'", nm);
            fail_at(x, TB_E_NAME, line, col, x->tpos, m);
        }
        return -1;
    }
}

static int32_t power(P *x) {
    int32_t b = primary(x);
    if (x->st == ZXV_OK && x->tok == T_POW) {
        uint32_t line = x->tline, col = x->tcol;
        int32_t e;
        lex(x);
        e = need_int(x, factor(x),
                     "an exponent must be an integer expression (x ** 2, x ** (n - i)); a "
                     "fractional power is not exact");
        return mk(x, N_POW, b, e, -1, -1, line, col);
    }
    return b;
}

static int32_t factor(P *x) {
    if (x->st != ZXV_OK) return -1;
    if (x->tok == T_MINUS || x->tok == T_PLUS) {
        int neg = x->tok == T_MINUS;
        uint32_t line = x->tline, col = x->tcol;
        int32_t f;
        lex(x);
        f = factor(x);
        if (!neg || f < 0) return f;
        {
            int32_t n = mk(x, N_NEG, f, -1, -1, -1, line, col);
            if (n >= 0) x->p->nodes[n].is_int = x->p->nodes[f].is_int;
            return n;
        }
    }
    return power(x);
}

static int32_t term(P *x) {
    int32_t a = factor(x);
    while (x->st == ZXV_OK && (x->tok == T_STAR || x->tok == T_SLASH)) {
        int mul = x->tok == T_STAR;
        uint32_t line = x->tline, col = x->tcol;
        int32_t b, n;
        lex(x);
        b = factor(x);
        n = mk(x, mul ? N_MUL : N_DIV, a, b, -1, -1, line, col);
        if (n >= 0 && mul) x->p->nodes[n].is_int = x->p->nodes[a].is_int && x->p->nodes[b].is_int;
        a = n;
    }
    return a;
}

static int32_t expr(P *x) {
    int32_t a = term(x);
    while (x->st == ZXV_OK && (x->tok == T_PLUS || x->tok == T_MINUS)) {
        int add = x->tok == T_PLUS;
        uint32_t line = x->tline, col = x->tcol;
        int32_t b, n;
        lex(x);
        b = term(x);
        n = mk(x, add ? N_ADD : N_SUB, a, b, -1, -1, line, col);
        if (n >= 0) x->p->nodes[n].is_int = x->p->nodes[a].is_int && x->p->nodes[b].is_int;
        a = n;
    }
    return a;
}

/* ===== declarations and statements ===== */

static int32_t new_sym(P *x, const char *nm, int32_t role, uint32_t line, uint32_t col) {
    tb_program *p = x->p;
    tb_sym *s;
    char m[160];
    if (x->st != ZXV_OK) return -1;
    if (is_reserved(nm)) {
        snprintf(m, sizeof m, "'%s' is a reserved word", nm);
        fail_at(x, TB_E_NAME, line, col, x->tpos, m);
        return -1;
    }
    if (find_sym(p, nm) >= 0 || find_dim(p, nm) >= 0) {
        snprintf(m, sizeof m, "'%s' is declared twice", nm);
        fail_at(x, TB_E_NAME, line, col, x->tpos, m);
        return -1;
    }
    if (p->nsyms >= TB_MAX_SYMBOLS) {
        fail_at(x, TB_E_LIMIT, line, col, x->tpos, "too many declarations");
        return -1;
    }
    s = &p->syms[p->nsyms];
    memset(s, 0, sizeof *s);
    memcpy(s->name, nm, strlen(nm) + 1u);
    s->role = role;
    s->dim[0] = s->dim[1] = -1;
    s->cden = 1;
    s->line = line;
    s->col = col;
    return (int32_t) p->nsyms++;
}

static void decl_item(P *x, int32_t role) {
    tb_program *p = x->p;
    int32_t s;
    if (x->tok != T_NAME) {
        FAIL(x, TB_E_SYNTAX, "expected a name to declare");
        return;
    }
    s = new_sym(x, x->name, role, x->tline, x->tcol);
    if (s < 0) return;
    lex(x);
    if (x->tok != T_LP) return;
    lex(x);
    for (;;) {
        uint32_t r = p->syms[s].rank;
        if (x->st != ZXV_OK) return;
        if (r >= TB_MAX_RANK) {
            FAIL(x, TB_E_SHAPE, "at most %u dimensions (scalars, vectors and matrices)", TB_MAX_RANK);
            return;
        }
        if (x->tok == T_NAME) {
            int32_t d;
            if (is_reserved(x->name)) {
                FAIL(x, TB_E_NAME, "'%s' is a reserved word", x->name);
                return;
            }
            if (find_sym(p, x->name) >= 0) {
                FAIL(x, TB_E_NAME, "'%s' is a variable, not a dimension", x->name);
                return;
            }
            d = find_dim(p, x->name);
            if (d < 0) {
                if (p->ndims >= 2u * TB_MAX_SYMBOLS) {
                    FAIL(x, TB_E_LIMIT, "too many dimension names");
                    return;
                }
                d = (int32_t) p->ndims++;
                memcpy(p->dims[d].name, x->name, strlen(x->name) + 1u);
            }
            p->syms[s].dim[r] = d;
        } else if (x->tok == T_NUM && x->is_int) {
            if (x->num > (int64_t) TB_MAX_ELEMENTS) {
                FAIL(x, TB_E_LIMIT, "an extent is at most %u", TB_MAX_ELEMENTS);
                return;
            }
            p->syms[s].fixed[r] = x->num;
        } else {
            FAIL(x, TB_E_SYNTAX, "an extent is a dimension name or a whole number");
            return;
        }
        p->syms[s].rank = r + 1u;
        lex(x);
        if (x->tok == T_COMMA) {
            lex(x);
            continue;
        }
        expect(x, T_RP, "to close the extents");
        return;
    }
}

static void decl(P *x, const char *kw) {
    tb_program *p = x->p;
    if (strcmp(kw, "const") == 0) {
        int32_t s, e;
        if (x->tok != T_NAME) {
            FAIL(x, TB_E_SYNTAX, "expected a name after const");
            return;
        }
        s = new_sym(x, x->name, TB_ROLE_CONST, x->tline, x->tcol);
        if (s < 0) return;
        lex(x);
        if (x->tok == T_LP) {
            FAIL(x, TB_E_SHAPE, "a constant is a scalar");
            return;
        }
        if (!expect(x, T_EQ, "after the constant's name")) return;
        x->in_const = 1;
        e = expr(x);
        x->in_const = 0;
        if (x->st != ZXV_OK) return;
        {
            int32_t st = tb_fold(p, e, &p->syms[s].cnum, &p->syms[s].cden, x->diag);
            if (st != ZXV_OK) x->st = st;
        }
        return;
    }
    {
        int32_t role = strcmp(kw, "in") == 0      ? TB_ROLE_IN
                       : strcmp(kw, "out") == 0   ? TB_ROLE_OUT
                       : strcmp(kw, "inout") == 0 ? TB_ROLE_INOUT
                                                  : TB_ROLE_VAR;
        decl_item(x, role);
        while (x->st == ZXV_OK && x->tok == T_COMMA) {
            lex(x);
            decl_item(x, role);
        }
    }
}

static void stmt(P *x) {
    tb_program *p = x->p;
    tb_stmt st;
    int32_t s;
    uint32_t line = x->tline, col = x->tcol;
    char m[200];
    memset(&st, 0, sizeof st);
    st.idx[0] = st.idx[1] = -1;
    if (x->tok != T_NAME) {
        FAIL(x, TB_E_SYNTAX, "expected a statement (name = expression) or end");
        return;
    }
    s = find_sym(p, x->name);
    if (s < 0) {
        if (find_dim(p, x->name) >= 0)
            FAIL(x, TB_E_NAME, "'%s' is a dimension; it is set by the arrays you bind", x->name);
        else if (is_reserved(x->name))
            FAIL(x, TB_E_SYNTAX, "'%s' cannot start a statement", x->name);
        else
            FAIL(x, TB_E_NAME, "unknown name '%s' (declare it with out, inout or var)", x->name);
        return;
    }
    if (p->syms[s].role == TB_ROLE_IN || p->syms[s].role == TB_ROLE_CONST) {
        FAIL(x, TB_E_NAME, "'%s' is %s; only out, inout and var can be assigned", x->name,
             p->syms[s].role == TB_ROLE_IN ? "an input (in)" : "a constant");
        return;
    }
    lex(x);
    x->nscope = 0;
    if (p->syms[s].rank > 0) {
        if (x->tok != T_LP) {
            FAIL(x, TB_E_SHAPE, "'%s' is an array: assign its elements, as in %s(i) = ...", p->syms[s].name,
                 p->syms[s].name);
            return;
        }
        lex(x);
        for (uint32_t r = 0; r < p->syms[s].rank; r++) {
            int32_t slot;
            if (r && !expect(x, T_COMMA, "between the target's indices")) return;
            if (x->tok != T_NAME) {
                FAIL(x, TB_E_SYNTAX,
                     "a target takes loop index names (y(i) = ...), not expressions: every element is assigned");
                return;
            }
            slot = new_slot(x, x->name, x->tline, x->tcol);
            if (slot < 0) return;
            p->slots[slot].range_sym = s;
            p->slots[slot].range_axis = (int32_t) r;
            st.idx[r] = slot;
            push(x, slot);
            lex(x);
        }
        if (x->tok == T_COMMA) {
            FAIL(x, TB_E_SHAPE, "'%s' has rank %u: too many indices", p->syms[s].name, (unsigned) p->syms[s].rank);
            return;
        }
        if (!expect(x, T_RP, "after the target's indices")) return;
    } else if (x->tok == T_LP) {
        snprintf(m, sizeof m, "'%s' is a scalar; it takes no subscript", p->syms[s].name);
        FAIL(x, TB_E_SHAPE, "%s", m);
        return;
    }
    if (!expect(x, T_EQ, "after the target")) return;
    x->stmt_rank = p->syms[s].rank;
    st.sym = s;
    st.rhs = expr(x);
    st.line = line;
    st.col = col;
    x->nscope = 0;
    if (x->st != ZXV_OK) return;
    if (!grow((void **) &p->stmts, &p->cap_stmts, p->nstmts + 1u, sizeof(tb_stmt))) {
        x->st = ZXV_E_NOMEM;
        return;
    }
    p->stmts[p->nstmts++] = st;
}

static int is_decl_kw(const char *n) {
    return strcmp(n, "in") == 0 || strcmp(n, "out") == 0 || strcmp(n, "inout") == 0 ||
           strcmp(n, "var") == 0 || strcmp(n, "const") == 0;
}

void tb_program_destroy(tb_program *p) {
    if (!p) return;
    free(p->syms);
    free(p->dims);
    free(p->nodes);
    free(p->slots);
    free(p->uses);
    free(p->stmts);
    free(p);
}

int32_t tb_parse(const char *text, size_t len, tb_program **out_program, tb_diag *diag) {
    P x;
    tb_program *p;
    if (diag) memset(diag, 0, sizeof *diag);
    if (!out_program || (!text && len)) return ZXV_E_ARG;
    if (len > TB_MAX_TEXT)
        return tb_diag_at(diag, TB_E_LIMIT, 0, 0, 0, "the text is longer than %u bytes", TB_MAX_TEXT);
    {
        size_t ok = tb_utf8_check((const uint8_t *) text, len);
        if (ok != len) {
            uint32_t line = 1;
            size_t ls = 0;
            for (size_t i = 0; i < ok; i++)
                if (text[i] == '\n') {
                    line++;
                    ls = i + 1u;
                }
            return tb_diag_at(diag, ZXV_E_UTF8, line, (uint32_t) (ok - ls + 1u), ok, "invalid UTF-8");
        }
    }
    p = (tb_program *) calloc(1, sizeof *p);
    if (!p) return ZXV_E_NOMEM;
    p->syms = (tb_sym *) calloc(TB_MAX_SYMBOLS, sizeof(tb_sym));
    p->dims = (tb_dim *) calloc(2u * TB_MAX_SYMBOLS, sizeof(tb_dim));
    if (!p->syms || !p->dims) {
        tb_program_destroy(p);
        return ZXV_E_NOMEM;
    }
    memset(&x, 0, sizeof x);
    x.t = text ? text : "";
    x.len = len;
    x.line = 1;
    x.p = p;
    x.diag = diag;
    x.st = ZXV_OK;

    lex(&x);
    skip_nl(&x);
    if (x.st == ZXV_OK && (x.tok != T_NAME || strcmp(x.name, "trebago") != 0))
        FAIL(&x, TB_E_SYNTAX, "a program starts with: trebago NAME");
    if (x.st == ZXV_OK) lex(&x);
    if (x.st == ZXV_OK && (x.tok != T_NAME || is_reserved(x.name)))
        FAIL(&x, TB_E_SYNTAX, "expected the program's name after trebago");
    if (x.st == ZXV_OK) {
        memcpy(p->name, x.name, strlen(x.name) + 1u);
        lex(&x);
    }
    expect_eol(&x);
    skip_nl(&x);
    while (x.st == ZXV_OK && x.tok == T_NAME && is_decl_kw(x.name)) {
        char kw[TB_MAX_NAME + 1];
        memcpy(kw, x.name, sizeof kw);
        lex(&x);
        decl(&x, kw);
        expect_eol(&x);
        skip_nl(&x);
    }
    while (x.st == ZXV_OK && x.tok == T_NAME && strcmp(x.name, "end") != 0) {
        if (is_decl_kw(x.name)) {
            FAIL(&x, TB_E_SYNTAX, "declarations come before the first statement");
            break;
        }
        stmt(&x);
        expect_eol(&x);
        skip_nl(&x);
    }
    if (x.st == ZXV_OK && (x.tok != T_NAME || strcmp(x.name, "end") != 0)) {
        if (x.tok == T_EOF)
            FAIL(&x, TB_E_SYNTAX, "expected end before the end of the text");
        else
            FAIL(&x, TB_E_SYNTAX, "expected a statement (name = expression) or end");
    }
    if (x.st == ZXV_OK) {
        lex(&x);
        skip_nl(&x);
        if (x.st == ZXV_OK && x.tok != T_EOF) FAIL(&x, TB_E_SYNTAX, "nothing may follow end");
    }
    /* every dimension a var uses must be bound by a parameter */
    for (uint32_t i = 0; x.st == ZXV_OK && i < p->nsyms; i++) {
        const tb_sym *s = &p->syms[i];
        if (s->role != TB_ROLE_VAR) continue;
        for (uint32_t r = 0; r < s->rank; r++) {
            int bound = 0;
            if (s->dim[r] < 0) continue;
            for (uint32_t j = 0; j < p->nsyms && !bound; j++) {
                const tb_sym *q = &p->syms[j];
                if (q->role < TB_ROLE_IN || q->role > TB_ROLE_INOUT) continue;
                for (uint32_t k = 0; k < q->rank; k++)
                    if (q->dim[k] == s->dim[r]) bound = 1;
            }
            if (!bound) {
                char m[200];
                snprintf(m, sizeof m,
                         "dimension '%s' of var '%s' is not an extent of any in, out or inout array, "
                         "so nothing sets it",
                         p->dims[s->dim[r]].name, s->name);
                fail_at(&x, TB_E_SHAPE, s->line, s->col, 0, m);
            }
        }
    }
    if (x.st != ZXV_OK) {
        int32_t st = x.st;
        if (st == ZXV_E_NOMEM) tb_set_error("out of memory");
        tb_program_destroy(p);
        return st;
    }
    *out_program = p;
    return ZXV_OK;
}

int32_t tb_program_name(const tb_program *p, char *buf, size_t cap, size_t *out_len) {
    if (!p) return ZXV_E_ARG;
    return tb_out_text(p->name, strlen(p->name), buf, cap, out_len);
}

int32_t tb_param_count(const tb_program *p, uint32_t *out_count) {
    uint32_t n = 0;
    if (!p || !out_count) return ZXV_E_ARG;
    for (uint32_t i = 0; i < p->nsyms; i++)
        if (p->syms[i].role >= TB_ROLE_IN && p->syms[i].role <= TB_ROLE_INOUT) n++;
    *out_count = n;
    return ZXV_OK;
}

int32_t tb_param_at(const tb_program *p, uint32_t index, tb_param *out) {
    uint32_t n = 0;
    if (!p || !out) return ZXV_E_ARG;
    for (uint32_t i = 0; i < p->nsyms; i++) {
        const tb_sym *s = &p->syms[i];
        if (s->role < TB_ROLE_IN || s->role > TB_ROLE_INOUT) continue;
        if (n++ != index) continue;
        memset(out, 0, sizeof *out);
        memcpy(out->name, s->name, strlen(s->name) + 1u);
        out->role = s->role;
        out->rank = s->rank;
        for (uint32_t r = 0; r < s->rank; r++) {
            if (s->dim[r] >= 0)
                memcpy(out->dim[r], p->dims[s->dim[r]].name, strlen(p->dims[s->dim[r]].name) + 1u);
            else
                out->fixed[r] = s->fixed[r];
        }
        return ZXV_OK;
    }
    return ZXV_E_NOT_FOUND;
}
