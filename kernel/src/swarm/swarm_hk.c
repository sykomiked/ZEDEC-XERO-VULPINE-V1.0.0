/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* swarm_hk.c — Hackronomicon shorthand parser. See swarm_hk.h. */
#include "swarm_hk.h"

typedef enum { T_END = 0, T_LP, T_RP, T_COMMA, T_COLON, T_CARET, T_OP, T_WORDS, T_BAD } tok_kind_t;

typedef struct {
    tok_kind_t kind;
    swarm_hk_op_t op;
    uint16_t start, len, next; /* next = position after the token */
} tok_t;

typedef struct {
    swarm_hk_ast_t *ast;
    uint16_t pos;
    bool failed;
} parser_t;

static bool is_space(char c)
{
    return c == ' ' || c == '\t';
}

/* Is there an operator at position i? Sets *op and *w (its byte width). */
static bool op_at(const char *s, uint16_t n, uint16_t i, swarm_hk_op_t *op, uint16_t *w)
{
    unsigned char c = (unsigned char) s[i];
    if (c == '-' && i + 1 < n && s[i + 1] == '>') {
        *op = SWARM_HK_OP_THEN;
        *w = 2;
        return true;
    }
    if (c == 0xE2 && i + 2 < n && (unsigned char) s[i + 1] == 0x86 &&
        (unsigned char) s[i + 2] == 0x92) {
        *op = SWARM_HK_OP_THEN;
        *w = 3;
        return true; /* → */
    }
    if (c == 0xE2 && i + 2 < n && (unsigned char) s[i + 1] == 0x88 &&
        (unsigned char) s[i + 2] == 0x92) {
        *op = SWARM_HK_OP_WITHOUT;
        *w = 3;
        return true; /* − */
    }
    if (c == '&' && i + 1 < n && s[i + 1] == '&') {
        *op = SWARM_HK_OP_AND;
        *w = 2;
        return true;
    }
    if (c == '|' && i + 1 < n && s[i + 1] == '|') {
        *op = SWARM_HK_OP_OR;
        *w = 2;
        return true;
    }
    if (c == '+') {
        *op = SWARM_HK_OP_ALSO;
        *w = 1;
        return true;
    }
    if (c == '/') {
        *op = SWARM_HK_OP_PER;
        *w = 1;
        return true;
    }
    if (c == '>') {
        *op = SWARM_HK_OP_OUTRANK;
        *w = 1;
        return true;
    }
    /* ASCII '-' is "without" only when it stands apart: hyphenated words stay words. */
    if (c == '-' && (i == 0 || is_space(s[i - 1])) && (i + 1 >= n || is_space(s[i + 1]))) {
        *op = SWARM_HK_OP_WITHOUT;
        *w = 1;
        return true;
    }
    return false;
}

static bool is_delim(const char *s, uint16_t n, uint16_t i)
{
    char c = s[i];
    swarm_hk_op_t op;
    uint16_t w;
    return c == '(' || c == ')' || c == ',' || c == ':' || c == '^' || op_at(s, n, i, &op, &w);
}

static tok_t lex(const swarm_hk_ast_t *a, uint16_t pos)
{
    const char *s = a->src;
    uint16_t n = a->src_len;
    tok_t t = {T_END, SWARM_HK_OP_THEN, pos, 0, pos};
    while (pos < n && is_space(s[pos])) pos++;
    t.start = pos;
    if (pos >= n) {
        t.next = pos;
        return t;
    }
    char c = s[pos];
    swarm_hk_op_t op;
    uint16_t w;
    if (c == '(') {
        t.kind = T_LP;
        t.len = 1;
        t.next = pos + 1;
        return t;
    }
    if (c == ')') {
        t.kind = T_RP;
        t.len = 1;
        t.next = pos + 1;
        return t;
    }
    if (c == ',') {
        t.kind = T_COMMA;
        t.len = 1;
        t.next = pos + 1;
        return t;
    }
    if (c == ':') {
        t.kind = T_COLON;
        t.len = 1;
        t.next = pos + 1;
        return t;
    }
    if (c == '^') {
        t.kind = T_CARET;
        t.len = 1;
        t.next = pos + 1;
        return t;
    }
    if (op_at(s, n, pos, &op, &w)) {
        t.kind = T_OP;
        t.op = op;
        t.len = w;
        t.next = pos + w;
        return t;
    }
    uint16_t end = pos;
    while (end < n && !is_delim(s, n, end)) end++;
    uint16_t trim = end;
    while (trim > pos && is_space(s[trim - 1])) trim--;
    t.kind = T_WORDS;
    t.len = trim - pos;
    t.next = end;
    return t;
}

static int16_t new_node(parser_t *p, swarm_hk_kind_t k)
{
    swarm_hk_ast_t *a = p->ast;
    if (a->num_nodes >= SWARM_HK_MAX_NODES) {
        p->failed = true;
        return -1;
    }
    swarm_hk_node_t *nd = &a->node[a->num_nodes];
    nd->kind = k;
    nd->op = SWARM_HK_OP_THEN;
    nd->start = nd->len = 0;
    nd->a = nd->b = nd->first_arg = nd->next = -1;
    nd->num = nd->den = 0;
    return (int16_t) a->num_nodes++;
}

static int16_t parse_expr(parser_t *p, int min_level);

/* ^( [-] p [ / q ] ) */
static bool parse_dial(parser_t *p, int32_t *num, int32_t *den)
{
    const char *s = p->ast->src;
    uint16_t n = p->ast->src_len, i = p->pos;
    while (i < n && is_space(s[i])) i++;
    if (i >= n || s[i] != '(') return false;
    i++;
    int32_t sign = 1, v = 0, q = 1;
    bool digits = false;
    while (i < n && is_space(s[i])) i++;
    if (i < n && s[i] == '-') {
        sign = -1;
        i++;
    } else if (i + 2 < n && (unsigned char) s[i] == 0xE2 && (unsigned char) s[i + 1] == 0x88 &&
               (unsigned char) s[i + 2] == 0x92) {
        sign = -1;
        i += 3;
    }
    while (i < n && s[i] >= '0' && s[i] <= '9' && v < 100000) {
        v = v * 10 + (s[i] - '0');
        i++;
        digits = true;
    }
    if (!digits) return false;
    while (i < n && is_space(s[i])) i++;
    if (i < n && s[i] == '/') {
        i++;
        q = 0;
        digits = false;
        while (i < n && is_space(s[i])) i++;
        while (i < n && s[i] >= '0' && s[i] <= '9' && q < 100000) {
            q = q * 10 + (s[i] - '0');
            i++;
            digits = true;
        }
        if (!digits || q == 0) return false;
    }
    while (i < n && is_space(s[i])) i++;
    if (i >= n || s[i] != ')') return false;
    p->pos = i + 1;
    *num = sign * v;
    *den = q;
    return true;
}

static int16_t parse_primary(parser_t *p)
{
    tok_t t = lex(p->ast, p->pos);
    int16_t node = -1;
    if (t.kind == T_LP) {
        p->pos = t.next;
        node = parse_expr(p, 1);
        tok_t r = lex(p->ast, p->pos);
        if (r.kind != T_RP) {
            p->failed = true;
            return -1;
        }
        p->pos = r.next;
    } else if (t.kind == T_WORDS) {
        p->pos = t.next;
        tok_t lp = lex(p->ast, p->pos);
        if (lp.kind == T_LP) { /* verb(args) */
            node = new_node(p, SWARM_HK_CALL);
            if (node < 0) return -1;
            p->ast->node[node].start = t.start;
            p->ast->node[node].len = t.len;
            p->pos = lp.next;
            tok_t close = lex(p->ast, p->pos);
            if (close.kind == T_RP) {
                p->pos = close.next;
            } else {
                int16_t last = -1;
                for (;;) {
                    int16_t arg = parse_expr(p, 1);
                    if (p->failed) return -1;
                    tok_t c = lex(p->ast, p->pos);
                    if (c.kind == T_COLON) { /* key: value */
                        if (p->ast->node[arg].kind != SWARM_HK_ATOM) {
                            p->failed = true;
                            return -1;
                        }
                        int16_t kv = new_node(p, SWARM_HK_KV);
                        if (kv < 0) return -1;
                        p->ast->node[kv].start = p->ast->node[arg].start;
                        p->ast->node[kv].len = p->ast->node[arg].len;
                        p->pos = c.next;
                        p->ast->node[kv].a = parse_expr(p, 1);
                        if (p->failed) return -1;
                        arg = kv;
                        c = lex(p->ast, p->pos);
                    }
                    if (last < 0)
                        p->ast->node[node].first_arg = arg;
                    else
                        p->ast->node[last].next = arg;
                    last = arg;
                    p->pos = c.next;
                    if (c.kind == T_RP) break;
                    if (c.kind != T_COMMA) {
                        p->failed = true;
                        return -1;
                    }
                }
            }
        } else {
            node = new_node(p, SWARM_HK_ATOM);
            if (node < 0) return -1;
            p->ast->node[node].start = t.start;
            p->ast->node[node].len = t.len;
        }
    } else {
        p->failed = true;
        return -1;
    }
    for (;;) { /* postfix dials */
        tok_t c = lex(p->ast, p->pos);
        if (c.kind != T_CARET) break;
        p->pos = c.next;
        int32_t num, den;
        if (!parse_dial(p, &num, &den)) {
            p->failed = true;
            return -1;
        }
        int16_t d = new_node(p, SWARM_HK_DIAL);
        if (d < 0) return -1;
        p->ast->node[d].a = node;
        p->ast->node[d].num = num;
        p->ast->node[d].den = den;
        p->ast->num_ops++;
        node = d;
    }
    return node;
}

static int level_of(swarm_hk_op_t op)
{
    switch (op) {
    case SWARM_HK_OP_THEN:
        return 1;
    case SWARM_HK_OP_OUTRANK:
        return 2;
    case SWARM_HK_OP_AND:
    case SWARM_HK_OP_OR:
        return 3;
    case SWARM_HK_OP_ALSO:
    case SWARM_HK_OP_WITHOUT:
        return 4;
    case SWARM_HK_OP_PER:
        return 5;
    }
    return 0;
}

static int16_t parse_expr(parser_t *p, int min_level)
{ /* precedence climbing */
    int16_t left = parse_primary(p);
    while (!p->failed) {
        tok_t t = lex(p->ast, p->pos);
        if (t.kind != T_OP || level_of(t.op) < min_level) break;
        int lv = level_of(t.op);
        p->pos = t.next;
        int16_t right = parse_expr(p, lv + 1); /* left-associative */
        if (p->failed) return -1;
        int16_t b = new_node(p, SWARM_HK_BIN);
        if (b < 0) return -1;
        p->ast->node[b].op = t.op;
        p->ast->node[b].a = left;
        p->ast->node[b].b = right;
        p->ast->num_ops++;
        left = b;
    }
    return left;
}

swarm_hk_status_t swarm_hk_parse(const char *text, swarm_hk_ast_t *out)
{
    if (!text || !out) return SWARM_HK_ERR_ARG;
    uint16_t n = 0;
    while (text[n]) {
        if (n + 1u >= SWARM_HK_MAX_TEXT) return SWARM_HK_ERR_TOO_LONG;
        out->src[n] = text[n];
        n++;
    }
    out->src[n] = 0;
    out->src_len = n;
    out->num_nodes = 0;
    out->num_ops = 0;
    out->root = -1;
    parser_t p = {out, 0, false};
    int16_t root = parse_expr(&p, 1);
    if (p.failed || root < 0 || lex(out, p.pos).kind != T_END) return SWARM_HK_ERR_SYNTAX;
    out->root = root;
    return SWARM_HK_OK;
}

/* ===== canonical reading (K2) ===== */

typedef struct {
    char *buf;
    uint32_t cap, len;
    bool overflow;
} out_t;

static void put_c(out_t *o, char c)
{
    if (o->len + 1u >= o->cap) {
        o->overflow = true;
        return;
    }
    o->buf[o->len++] = c;
}
static void put_s(out_t *o, const char *s)
{
    while (*s) put_c(o, *s++);
}
static void put_span(out_t *o, const swarm_hk_ast_t *a, uint16_t st, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++) put_c(o, a->src[st + i]);
}
static void put_int(out_t *o, int32_t v)
{
    char d[12];
    int k = 0;
    uint32_t u = v < 0 ? (uint32_t) (-(int64_t) v) : (uint32_t) v;
    if (v < 0) put_c(o, '-');
    do {
        d[k++] = (char) ('0' + u % 10u);
        u /= 10u;
    } while (u && k < 11);
    while (k) put_c(o, d[--k]);
}

static const char *op_text(swarm_hk_op_t op)
{
    switch (op) {
    case SWARM_HK_OP_THEN:
        return " -> ";
    case SWARM_HK_OP_OUTRANK:
        return " > ";
    case SWARM_HK_OP_AND:
        return " && ";
    case SWARM_HK_OP_OR:
        return " || ";
    case SWARM_HK_OP_ALSO:
        return " + ";
    case SWARM_HK_OP_WITHOUT:
        return " - ";
    case SWARM_HK_OP_PER:
        return " / ";
    }
    return " ? ";
}

static void print_node(out_t *o, const swarm_hk_ast_t *a, int16_t i, uint32_t depth)
{
    if (i < 0 || depth > SWARM_HK_MAX_NODES) {
        o->overflow = true;
        return;
    }
    const swarm_hk_node_t *nd = &a->node[i];
    switch (nd->kind) {
    case SWARM_HK_ATOM:
        put_span(o, a, nd->start, nd->len);
        break;
    case SWARM_HK_CALL:
        put_span(o, a, nd->start, nd->len);
        put_c(o, '(');
        for (int16_t g = nd->first_arg; g >= 0; g = a->node[g].next) {
            print_node(o, a, g, depth + 1);
            if (a->node[g].next >= 0) put_s(o, ", ");
        }
        put_c(o, ')');
        break;
    case SWARM_HK_KV:
        put_span(o, a, nd->start, nd->len);
        put_s(o, ": ");
        print_node(o, a, nd->a, depth + 1);
        break;
    case SWARM_HK_BIN:
        put_c(o, '(');
        print_node(o, a, nd->a, depth + 1);
        put_s(o, op_text(nd->op));
        print_node(o, a, nd->b, depth + 1);
        put_c(o, ')');
        break;
    case SWARM_HK_DIAL:
        put_c(o, '(');
        print_node(o, a, nd->a, depth + 1);
        put_s(o, ")^(");
        put_int(o, nd->num);
        if (nd->den != 1) {
            put_c(o, '/');
            put_int(o, nd->den);
        }
        put_c(o, ')');
        break;
    }
}

int32_t swarm_hk_canonical(const swarm_hk_ast_t *ast, char *buf, uint32_t cap)
{
    if (!ast || !buf || cap == 0 || ast->root < 0) return -1;
    out_t o = {buf, cap, 0, false};
    print_node(&o, ast, ast->root, 0);
    buf[o.len] = 0;
    return o.overflow ? -1 : (int32_t) o.len;
}

/* ===== requests between agents (K3, K4) ===== */

swarm_hk_status_t swarm_hk_request(swarm_hk_msg_t *msg, uint32_t from, uint32_t to,
                                   uint32_t ordinal, swarm_hk_truth_t truth, const char *text)
{
    if (!msg || (uint32_t) truth > (uint32_t) SWARM_HK_UNKNOWN) return SWARM_HK_ERR_ARG;
    if (from == to) return SWARM_HK_ERR_SELF;
    swarm_hk_status_t st = swarm_hk_parse(text, &msg->ast);
    if (st != SWARM_HK_OK) return st;
    if (msg->ast.num_ops > SWARM_HK_MAX_OPS) return SWARM_HK_ERR_STACKED;
    msg->from = from;
    msg->to = to;
    msg->ordinal = ordinal;
    msg->truth = truth;
    return SWARM_HK_OK;
}

uint32_t swarm_hk_words(const char *text)
{
    if (!text) return 0;
    uint32_t words = 0;
    bool in = false;
    for (const char *c = text; *c; c++) {
        bool sp = *c == ' ' || *c == '\t' || *c == '\n' || *c == '\r';
        if (!sp && !in) words++;
        in = !sp;
    }
    return words;
}

bool swarm_hk_strand_ok(const char *text)
{
    return swarm_hk_words(text) <= SWARM_HK_STRAND_MAX;
}

/* ===== six-state truth (K5, V.9) ===== */

swarm_hk_truth_t swarm_hk_and(swarm_hk_truth_t a, swarm_hk_truth_t b)
{
    if (a == SWARM_HK_PARADOX || b == SWARM_HK_PARADOX) return SWARM_HK_PARADOX; /* P dominates */
    if (a == SWARM_HK_GLUT || b == SWARM_HK_GLUT) return SWARM_HK_GLUT; /* ⊥̸∧x = ⊥̸ */
    if ((a == SWARM_HK_TRUE && b == SWARM_HK_FALSE) || (a == SWARM_HK_FALSE && b == SWARM_HK_TRUE))
        return SWARM_HK_GLUT; /* ⊤∧⊥ = ⊥̸ */
    if (a == SWARM_HK_UNKNOWN || b == SWARM_HK_UNKNOWN) return SWARM_HK_UNKNOWN; /* ⊤∧U = U */
    if (a == SWARM_HK_NEUTRAL) return b; /* silence adds nothing */
    if (b == SWARM_HK_NEUTRAL) return a;
    return a; /* ⊤∧⊤, ⊥∧⊥ */
}

swarm_hk_truth_t swarm_hk_or(swarm_hk_truth_t a, swarm_hk_truth_t b)
{
    if (a == SWARM_HK_PARADOX || b == SWARM_HK_PARADOX) return SWARM_HK_PARADOX;
    if (a == SWARM_HK_GLUT || b == SWARM_HK_GLUT) return SWARM_HK_GLUT;
    if (a == SWARM_HK_NEUTRAL) return b; /* N∨x = x */
    if (b == SWARM_HK_NEUTRAL) return a;
    if (a == SWARM_HK_TRUE || b == SWARM_HK_TRUE) return SWARM_HK_TRUE; /* ⊤∨U = ⊤ */
    if (a == SWARM_HK_UNKNOWN || b == SWARM_HK_UNKNOWN) return SWARM_HK_UNKNOWN;
    return SWARM_HK_FALSE; /* ⊥∨⊥ */
}
