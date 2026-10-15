/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zl_forth.c — Forth front-end: a Forth-2012 core subset (the 1970
 * language), with gforth's choices where the standard leaves one open
 * (floored division, 64-bit cells), and its twin "Cairndrake".
 *
 * Admitted: decimal numbers; + - * / MOD /MOD NEGATE ABS MIN MAX 1+ 1- 2*
 * 2/; = <> < > 0= 0< 0> 0<> TRUE FALSE; AND OR XOR INVERT; DUP DROP SWAP
 * OVER ROT NIP TUCK 2DUP 2DROP ?DUP; . CR EMIT SPACE SPACES ." .( ;
 * : ; RECURSE EXIT; IF ELSE THEN, BEGIN UNTIL, BEGIN AGAIN, BEGIN WHILE
 * REPEAT, DO LOOP +LOOP I J LEAVE UNLOOP; VARIABLE, <n> CONSTANT,
 * CREATE ... <n> CELLS ALLOT, @ ! +! ? CELLS CELL+; BYE; comments with
 * ( ) and \.
 *
 * The program is compiled, not interpreted, so the few words whose
 * standard meaning reads the run-time stack at definition time
 * (CONSTANT, ALLOT) take a literal number written just before them.
 * Control words outside a definition are refused, as the standard says.
 * Cells are 64 bits; a result outside them traps instead of wrapping. A
 * word that uses RECURSE may not contain DO loops (loop indices live in
 * fixed cells). Refused by name: floating point, strings in memory, BASE,
 * HEX, EVALUATE, POSTPONE, IMMEDIATE, DOES>, KEY, ACCEPT.
 *
 * Cairndrake (the twin): words and names upper case, one space between
 * tokens, each colon definition on its own line, a top-level line ends
 * after CR, '\' comments on their own line, '( )' comments with single
 * spaces, ." and .( text kept exactly. Addresses count cells, so CELLS
 * changes nothing: a program that writes "n CELLS +" runs as in gforth. */
#include "zl_int.h"

#define Q_WORDS 512
#define Q_TEXT  64

enum { W_COLON = 1, W_VAR, W_CONST };
enum { C_IF = 1, C_ELSE, C_BEGIN, C_WHILE, C_DO };

typedef struct {
    char name[32];
    uint8_t len, kind;
    int64_t v; /* colon: code address; var: slot; const: value */
} qword_t;

typedef struct {
    uint8_t kind;
    int32_t at;    /* IF/ELSE/WHILE: jump to patch; BEGIN/DO: loop top */
    int32_t begin; /* WHILE: its BEGIN */
    int32_t idx, lim;
    int32_t leaves[16];
    uint32_t nleaves;
} qctl_t;

typedef struct {
    qword_t words[Q_WORDS];
    uint32_t nwords;
    qctl_t ctl[ZL_MAX_DEPTH];
    uint32_t nctl;
    int in_def, def_recurse, def_do;
    int32_t def_skip, def_addr;
    char def_name[32];
    uint8_t def_len;
    int32_t create_word; /* the latest CREATE, for ALLOT */
    int32_t last_push;   /* index of a PUSH emitted for the previous token */
    int64_t last_value;
    int line_open; /* twin: the current line has tokens */
    zl_cc_t *c;
    zl_out_t *tw;
} qw_t;

ZL_STATIC_ASSERT(sizeof(qw_t) <= sizeof(((zl_work_t *) 0)->fe), qw_fits);

static int qfail(qw_t *w, int st, const char *msg)
{
    return zl_fail(w->c, st, msg);
}

/* ------------------------------------------------------------ twin */

static void tok_begin(qw_t *w)
{
    if (!w->tw) return;
    if (w->line_open) zl_putc(w->tw, ' ');
    w->line_open = 1;
}

static void tok(qw_t *w, const char *s, size_t n)
{
    if (!w->tw) return;
    tok_begin(w);
    for (size_t i = 0; i < n; i++) zl_putc(w->tw, (char) zl_upper((unsigned char) s[i]));
}

static void tok_raw(qw_t *w, const char *s, size_t n)
{
    if (w->tw) zl_put(w->tw, s, n);
}

static void line_end(qw_t *w)
{
    if (!w->tw) return;
    if (w->line_open) zl_putc(w->tw, '\n');
    w->line_open = 0;
}

/* ------------------------------------------------------------ helpers */

static int32_t E(qw_t *w, int op, int64_t a, int32_t b)
{
    return zl_emit(w->c, op, a, b);
}

static int32_t find(qw_t *w, const char *s, size_t n)
{
    for (uint32_t i = w->nwords; i-- > 0;) { /* newest definition wins */
        if (w->words[i].len != n) continue;
        size_t k = 0;
        while (k < n && w->words[i].name[k] == zl_upper((unsigned char) s[k])) k++;
        if (k == n) return (int32_t) i;
    }
    return -1;
}

static int32_t addword(qw_t *w, const char *s, size_t n, int kind, int64_t v)
{
    if (n == 0 || n > 31) {
        qfail(w, ZL_E_SYNTAX, "a name is 1 to 31 characters");
        return -1;
    }
    if (w->nwords >= Q_WORDS) {
        qfail(w, ZL_E_LIMIT, "too many definitions");
        return -1;
    }
    qword_t *q = &w->words[w->nwords];
    for (size_t k = 0; k < n; k++) q->name[k] = (char) zl_upper((unsigned char) s[k]);
    q->len = (uint8_t) n;
    q->kind = (uint8_t) kind;
    q->v = v;
    return (int32_t) w->nwords++;
}

static int is_num(const char *s, size_t n, int64_t *v)
{
    size_t k = 0;
    int neg = 0;
    int64_t x = 0;
    if (n && s[0] == '-') {
        neg = 1;
        k = 1;
    }
    if (k >= n) return 0;
    for (; k < n; k++) {
        if (!zl_isdigit((unsigned char) s[k])) return 0;
        if (__builtin_mul_overflow(x, 10, &x) || __builtin_sub_overflow(x, s[k] - '0', &x))
            return -1;
    }
    if (!neg) {
        if (x == INT64_MIN) return -1;
        x = -x;
    }
    *v = x;
    return 1;
}

typedef struct {
    const char *n;
    uint8_t ops[8];
} prim_t;

/* Primitives that are a fixed IR sequence (0 ends the list). */
static const prim_t prims[] = {{"+", {ZL_ADD}},
                               {"-", {ZL_SUB}},
                               {"*", {ZL_MUL}},
                               {"/", {ZL_DIVF}},
                               {"MOD", {ZL_MODF}},
                               {"/MOD", {ZL_OVER, ZL_OVER, ZL_MODF, ZL_ROT, ZL_ROT, ZL_DIVF}},
                               {"NEGATE", {ZL_NEG}},
                               {"ABS", {ZL_ABS}},
                               {"MIN", {ZL_MIN}},
                               {"MAX", {ZL_MAX}},
                               {"=", {ZL_EQ, ZL_NEG}},
                               {"<>", {ZL_NE, ZL_NEG}},
                               {"<", {ZL_LT, ZL_NEG}},
                               {">", {ZL_GT, ZL_NEG}},
                               {"AND", {ZL_AND}},
                               {"OR", {ZL_OR}},
                               {"XOR", {ZL_XOR}},
                               {"INVERT", {ZL_INVERT}},
                               {"DUP", {ZL_DUP}},
                               {"DROP", {ZL_DROP}},
                               {"SWAP", {ZL_SWAP}},
                               {"OVER", {ZL_OVER}},
                               {"ROT", {ZL_ROT}},
                               {"NIP", {ZL_SWAP, ZL_DROP}},
                               {"TUCK", {ZL_SWAP, ZL_OVER}},
                               {"2DUP", {ZL_OVER, ZL_OVER}},
                               {"2DROP", {ZL_DROP, ZL_DROP}},
                               {".", {0xFE}},
                               {"CR", {ZL_NL}},
                               {"EMIT", {ZL_OUTC}},
                               {"@", {ZL_LOADA}},
                               {"!", {ZL_STOREA}},
                               {"+!", {ZL_DUP, ZL_LOADA, ZL_ROT, ZL_ADD, ZL_SWAP, ZL_STOREA}},
                               {"?", {ZL_LOADA, 0xFE}},
                               {"CELLS", {0xFD}},
                               {0, {0}}};

/* Primitives with an immediate operand: op, operand, op ... */
static int prim_imm(qw_t *w, const char *s, size_t n)
{
    struct {
        const char *n;
        int op1;
        int64_t a;
        int op2;
        int op3;
    } t[] = {{"1+", ZL_PUSH, 1, ZL_ADD, 0},      {"1-", ZL_PUSH, 1, ZL_SUB, 0},
             {"2*", ZL_PUSH, 2, ZL_MUL, 0},      {"2/", ZL_PUSH, 2, ZL_DIVF, 0},
             {"CELL+", ZL_PUSH, 1, ZL_ADD, 0},   {"0=", ZL_PUSH, 0, ZL_EQ, ZL_NEG},
             {"0<", ZL_PUSH, 0, ZL_LT, ZL_NEG},  {"0>", ZL_PUSH, 0, ZL_GT, ZL_NEG},
             {"0<>", ZL_PUSH, 0, ZL_NE, ZL_NEG}, {"TRUE", ZL_PUSH, -1, 0, 0},
             {"FALSE", ZL_PUSH, 0, 0, 0},        {0, 0, 0, 0, 0}};
    for (int k = 0; t[k].n; k++) {
        if (!zl_wordeq(s, n, t[k].n)) continue;
        E(w, t[k].op1, t[k].a, 0);
        if (t[k].op2) E(w, t[k].op2, 0, 0);
        if (t[k].op3) E(w, t[k].op3, 0, 0);
        return 1;
    }
    return 0;
}

static qctl_t *push_ctl(qw_t *w, int kind)
{
    if (w->nctl >= ZL_MAX_DEPTH) {
        qfail(w, ZL_E_LIMIT, "control structures nested too deeply");
        return 0;
    }
    qctl_t *q = &w->ctl[w->nctl++];
    zl_bzero(q, sizeof *q);
    q->kind = (uint8_t) kind;
    return q;
}

static qctl_t *top_ctl(qw_t *w, int kind, const char *msg)
{
    if (w->nctl == 0 || w->ctl[w->nctl - 1].kind != kind) {
        qfail(w, ZL_E_SYNTAX, msg);
        return 0;
    }
    return &w->ctl[w->nctl - 1];
}

static qctl_t *nth_do(qw_t *w, int nth)
{
    for (uint32_t i = w->nctl; i-- > 0;)
        if (w->ctl[i].kind == C_DO && nth-- == 0) return &w->ctl[i];
    return 0;
}

/* ------------------------------------------------------------ words */

static int control(qw_t *w, const char *s, size_t n, int *done)
{
    zl_cc_t *c = w->c;
    qctl_t *q;
    *done = 1;
    if (zl_wordeq(s, n, "IF")) {
        if (!(q = push_ctl(w, C_IF))) return c->d->status;
        q->at = E(w, ZL_JZ, 0, 0);
    } else if (zl_wordeq(s, n, "ELSE")) {
        if (!(q = top_ctl(w, C_IF, "ELSE without IF"))) return c->d->status;
        int32_t j = E(w, ZL_JMP, 0, 0);
        zl_patch(c, q->at, (int64_t) c->p->ncode);
        q->kind = C_ELSE;
        q->at = j;
    } else if (zl_wordeq(s, n, "THEN")) {
        if (w->nctl == 0 ||
            (w->ctl[w->nctl - 1].kind != C_IF && w->ctl[w->nctl - 1].kind != C_ELSE))
            return qfail(w, ZL_E_SYNTAX, "THEN without IF");
        zl_patch(c, w->ctl[--w->nctl].at, (int64_t) c->p->ncode);
    } else if (zl_wordeq(s, n, "BEGIN")) {
        if (!(q = push_ctl(w, C_BEGIN))) return c->d->status;
        q->at = (int32_t) c->p->ncode;
    } else if (zl_wordeq(s, n, "UNTIL") || zl_wordeq(s, n, "AGAIN")) {
        if (!(q = top_ctl(w, C_BEGIN, "UNTIL or AGAIN without BEGIN"))) return c->d->status;
        E(w, zl_wordeq(s, n, "UNTIL") ? ZL_JZ : ZL_JMP, q->at, 0);
        w->nctl--;
    } else if (zl_wordeq(s, n, "WHILE")) {
        if (!(q = top_ctl(w, C_BEGIN, "WHILE without BEGIN"))) return c->d->status;
        q->kind = C_WHILE;
        q->begin = q->at;
        q->at = E(w, ZL_JZ, 0, 0);
    } else if (zl_wordeq(s, n, "REPEAT")) {
        if (!(q = top_ctl(w, C_WHILE, "REPEAT without WHILE"))) return c->d->status;
        E(w, ZL_JMP, q->begin, 0);
        zl_patch(c, q->at, (int64_t) c->p->ncode);
        w->nctl--;
    } else if (zl_wordeq(s, n, "DO")) {
        if (!(q = push_ctl(w, C_DO))) return c->d->status;
        q->idx = zl_slots(c, 1);
        q->lim = zl_slots(c, 1);
        if (q->idx < 0 || q->lim < 0) return c->d->status;
        E(w, ZL_STORE, q->idx, 0); /* ( limit start -- ) */
        E(w, ZL_STORE, q->lim, 0);
        q->at = (int32_t) c->p->ncode;
        w->def_do = 1;
    } else if (zl_wordeq(s, n, "LOOP") || zl_wordeq(s, n, "+LOOP")) {
        if (!(q = top_ctl(w, C_DO, "LOOP without DO"))) return c->d->status;
        if (zl_wordeq(s, n, "LOOP")) {
            E(w, ZL_LOAD, q->idx, 0);
            E(w, ZL_PUSH, 1, 0);
            E(w, ZL_ADD, 0, 0);
            E(w, ZL_DUP, 0, 0);
            E(w, ZL_STORE, q->idx, 0);
            E(w, ZL_LOAD, q->lim, 0);
            E(w, ZL_EQ, 0, 0);
            E(w, ZL_JZ, q->at, 0);
        } else {
            /* exit when index - limit changes sign (crosses limit-1 | limit) */
            E(w, ZL_LOAD, q->idx, 0);
            E(w, ZL_LOAD, q->lim, 0);
            E(w, ZL_SUB, 0, 0); /* n d */
            E(w, ZL_SWAP, 0, 0);
            E(w, ZL_OVER, 0, 0);
            E(w, ZL_ADD, 0, 0); /* d d' */
            E(w, ZL_DUP, 0, 0);
            E(w, ZL_LOAD, q->lim, 0);
            E(w, ZL_ADD, 0, 0);
            E(w, ZL_STORE, q->idx, 0);
            E(w, ZL_XOR, 0, 0);
            E(w, ZL_PUSH, 0, 0);
            E(w, ZL_LT, 0, 0);
            E(w, ZL_JZ, q->at, 0);
        }
        for (uint32_t k = 0; k < q->nleaves; k++) zl_patch(c, q->leaves[k], (int64_t) c->p->ncode);
        w->nctl--;
    } else if (zl_wordeq(s, n, "I") || zl_wordeq(s, n, "J")) {
        qctl_t *d = nth_do(w, zl_wordeq(s, n, "J"));
        if (!d) return qfail(w, ZL_E_SYNTAX, "I or J outside a DO loop");
        E(w, ZL_LOAD, d->idx, 0);
    } else if (zl_wordeq(s, n, "LEAVE")) {
        qctl_t *d = nth_do(w, 0);
        if (!d) return qfail(w, ZL_E_SYNTAX, "LEAVE outside a DO loop");
        if (d->nleaves >= 16) return qfail(w, ZL_E_LIMIT, "too many LEAVEs in one loop");
        d->leaves[d->nleaves++] = E(w, ZL_JMP, 0, 0);
    } else if (zl_wordeq(s, n, "UNLOOP")) {
        if (!nth_do(w, 0)) return qfail(w, ZL_E_SYNTAX, "UNLOOP outside a DO loop");
    } else if (zl_wordeq(s, n, "BYE")) {
        E(w, ZL_HALT, 0, 0);
    } else if (zl_wordeq(s, n, "EXIT")) {
        E(w, ZL_RET, 0, 0);
    } else if (zl_wordeq(s, n, "RECURSE")) {
        w->def_recurse = 1;
        E(w, ZL_CALL, w->def_addr, 0);
    } else if (zl_wordeq(s, n, "SPACES")) {
        int32_t top = E(w, ZL_DUP, 0, 0);
        E(w, ZL_PUSH, 0, 0);
        E(w, ZL_GT, 0, 0);
        int32_t out = E(w, ZL_JZ, 0, 0);
        E(w, ZL_PUSH, 32, 0);
        E(w, ZL_OUTC, 0, 0);
        E(w, ZL_PUSH, 1, 0);
        E(w, ZL_SUB, 0, 0);
        E(w, ZL_JMP, top, 0);
        zl_patch(c, out, (int64_t) c->p->ncode);
        E(w, ZL_DROP, 0, 0);
    } else if (zl_wordeq(s, n, "?DUP")) {
        E(w, ZL_DUP, 0, 0);
        int32_t j = E(w, ZL_JZ, 0, 0);
        E(w, ZL_DUP, 0, 0);
        zl_patch(c, j, (int64_t) c->p->ncode);
    } else {
        *done = 0;
        return ZL_OK;
    }
    return c->failed ? c->d->status : ZL_OK;
}

static int compile_only(const char *s, size_t n)
{
    static const char *const k[] = {"IF",     "ELSE", "THEN",    "BEGIN", "UNTIL", "AGAIN", "WHILE",
                                    "REPEAT", "DO",   "LOOP",    "+LOOP", "I",     "J",     "LEAVE",
                                    "UNLOOP", "EXIT", "RECURSE", ";",     0};
    for (int i = 0; k[i]; i++)
        if (zl_wordeq(s, n, k[i])) return 1;
    return 0;
}

int zl_forth_compile(const char *src, size_t len, zl_cc_t *c, zl_out_t *tw, void *work)
{
    qw_t *w = (qw_t *) work;
    zl_bzero(w, sizeof *w);
    w->c = c;
    w->tw = tw;
    w->create_word = -1;
    w->last_push = -1;
    c->p->word_bits = 64;
    size_t i = 0;
    uint32_t line = 1;
    c->line = 1;
    while (i < len) {
        char ch = src[i];
        if (ch == '\n') {
            line++;
            c->line = line;
            i++;
            continue;
        }
        if (zl_isspace((unsigned char) ch)) {
            i++;
            continue;
        }
        size_t a = i;
        while (i < len && !zl_isspace((unsigned char) src[i]) && src[i] != '\n') i++;
        const char *s = src + a;
        size_t n = i - a;
        int32_t this_push = -1;
        int s_;
        int64_t v;
        c->col = 0;

        if (n == 1 && s[0] == '\\') { /* comment to end of line */
            size_t b = i;
            while (b < len && src[b] != '\n') b++;
            size_t x = i, y = b;
            while (x < y && zl_isspace((unsigned char) src[x])) x++;
            while (y > x && zl_isspace((unsigned char) src[y - 1])) y--;
            if (tw) {
                line_end(w);
                zl_putc(tw, '\\');
                if (y > x) {
                    zl_putc(tw, ' ');
                    zl_put(tw, src + x, y - x);
                }
                zl_putc(tw, '\n');
            }
            i = b;
            continue;
        }
        if (n == 1 && s[0] == '(') { /* comment to ')' */
            size_t b = i;
            while (b < len && src[b] != ')') {
                if (src[b] == '\n') {
                    line++;
                    c->line = line;
                }
                b++;
            }
            if (b >= len) return qfail(w, ZL_E_SYNTAX, "unterminated ( comment");
            if (tw) {
                tok(w, "(", 1);
                int sp = 1;
                for (size_t k = i; k < b; k++) {
                    char x = src[k];
                    if (zl_isspace((unsigned char) x) || x == '\n') {
                        sp = 1;
                        continue;
                    }
                    if (sp) zl_putc(tw, ' ');
                    sp = 0;
                    zl_putc(tw, x);
                }
                zl_puts(tw, " )");
            }
            i = b + 1;
            continue;
        }
        if (zl_wordeq(s, n, ".\"") || zl_wordeq(s, n, ".(")) {
            char close = s[1] == '"' ? '"' : ')';
            if (close == ')' && w->in_def)
                return qfail(w, ZL_E_UNSUPPORTED,
                             ".( inside a definition prints at compile time; use .\"");
            if (i >= len || (src[i] != ' ' && src[i] != '\t'))
                return qfail(w, ZL_E_SYNTAX, "a space must follow .\" and .(");
            size_t b = i + 1; /* the text starts after one space */
            size_t e = b;
            while (e < len && src[e] != close && src[e] != '\n') e++;
            if (e >= len || src[e] != close) return qfail(w, ZL_E_SYNTAX, "unterminated string");
            int32_t k = zl_addstr(c, src + b, e - b);
            if (k < 0) return c->d->status;
            E(w, ZL_OUTS, k, 0);
            tok(w, s, n);
            tok_raw(w, " ", 1);
            tok_raw(w, src + b, e - b);
            if (tw) zl_putc(tw, close);
            i = e + 1;
            w->last_push = -1;
            continue;
        }
        if (zl_wordeq(s, n, ":")) {
            if (w->in_def) return qfail(w, ZL_E_SYNTAX, ": inside a definition");
            size_t b = i;
            while (b < len && zl_isspace((unsigned char) src[b])) b++;
            size_t e = b;
            while (e < len && !zl_isspace((unsigned char) src[e]) && src[e] != '\n') e++;
            if (e == b || e - b > 31)
                return qfail(w, ZL_E_SYNTAX, "a name (1 to 31 characters) must follow :");
            line_end(w);
            tok(w, ":", 1);
            tok(w, src + b, e - b);
            w->in_def = 1;
            w->def_recurse = 0;
            w->def_do = 0;
            w->def_len = (uint8_t) (e - b);
            for (size_t k = 0; k < e - b; k++)
                w->def_name[k] = (char) zl_upper((unsigned char) src[b + k]);
            w->def_skip = E(w, ZL_JMP, 0, 0);
            w->def_addr = (int32_t) c->p->ncode;
            i = e;
            w->last_push = -1;
            continue;
        }
        if (zl_wordeq(s, n, ";")) {
            if (!w->in_def) return qfail(w, ZL_E_SYNTAX, "; outside a definition");
            if (w->nctl) return qfail(w, ZL_E_SYNTAX, "unfinished control structure at ;");
            if (w->def_recurse && w->def_do)
                return qfail(w, ZL_E_UNSUPPORTED,
                             "RECURSE in a word with DO loops is not in this subset");
            E(w, ZL_RET, 0, 0);
            zl_patch(c, w->def_skip, (int64_t) c->p->ncode);
            if (addword(w, w->def_name, w->def_len, W_COLON, w->def_addr) < 0) return c->d->status;
            w->in_def = 0;
            tok(w, ";", 1);
            line_end(w);
            w->last_push = -1;
            continue;
        }
        if (!w->in_def && compile_only(s, n))
            return qfail(w, ZL_E_SYNTAX, "compile-only word outside a definition");
        if (zl_wordeq(s, n, "VARIABLE") || zl_wordeq(s, n, "CONSTANT") ||
            zl_wordeq(s, n, "CREATE")) {
            if (w->in_def)
                return qfail(w, ZL_E_UNSUPPORTED,
                             "defining words inside a definition are not in this subset");
            size_t b = i;
            while (b < len && zl_isspace((unsigned char) src[b])) b++;
            size_t e = b;
            while (e < len && !zl_isspace((unsigned char) src[e]) && src[e] != '\n') e++;
            if (e == b) return qfail(w, ZL_E_SYNTAX, "a name must follow");
            if (zl_wordeq(s, n, "CONSTANT")) {
                if (w->last_push < 0 || w->last_push != (int32_t) c->p->ncode - 1)
                    return qfail(w, ZL_E_UNSUPPORTED,
                                 "CONSTANT needs a literal number just before it");
                c->p->ncode--; /* the literal becomes the constant's value */
                if (addword(w, src + b, e - b, W_CONST, w->last_value) < 0) return c->d->status;
            } else {
                int32_t slot = zl_slots(c, 1);
                if (slot < 0) return c->d->status;
                int32_t id = addword(w, src + b, e - b, W_VAR, slot);
                if (id < 0) return c->d->status;
                w->create_word = zl_wordeq(s, n, "CREATE") ? id : -1;
                if (zl_wordeq(s, n, "CREATE"))
                    c->p->nslots--; /* CREATE reserves nothing until ALLOT */
            }
            tok(w, s, n);
            tok(w, src + b, e - b);
            i = e;
            w->last_push = -1;
            continue;
        }
        if (zl_wordeq(s, n, "ALLOT")) {
            if (w->in_def || w->create_word < 0)
                return qfail(w, ZL_E_UNSUPPORTED, "ALLOT only right after CREATE name <n> [CELLS]");
            if (w->last_push < 0 || w->last_push != (int32_t) c->p->ncode - 1)
                return qfail(w, ZL_E_UNSUPPORTED, "ALLOT needs a literal number just before it");
            if (w->last_value < 1 || w->last_value > ZL_MAX_SLOTS)
                return qfail(w, ZL_E_LIMIT, "ALLOT size out of range");
            c->p->ncode--;
            if (zl_slots(c, (uint32_t) w->last_value) < 0) return c->d->status;
            w->create_word = -1;
            tok(w, s, n);
            w->last_push = -1;
            continue;
        }
        s_ = is_num(s, n, &v);
        if (s_ < 0) return qfail(w, ZL_E_UNSUPPORTED, "number wider than 64 bits");
        if (s_ > 0) {
            this_push = E(w, ZL_PUSH, v, 0);
            if (tw) {
                tok_begin(w);
                zl_puti(tw, v);
            }
            w->last_push = this_push;
            w->last_value = v;
            continue;
        }
        {
            int32_t id = find(w, s, n);
            if (id >= 0) {
                qword_t *q = &w->words[id];
                if (q->kind == W_COLON)
                    E(w, ZL_CALL, q->v, 0);
                else
                    E(w, ZL_PUSH, q->v, 0);
                tok(w, s, n);
                if (q->kind == W_CONST) {
                    w->last_push = (int32_t) c->p->ncode - 1;
                    w->last_value = q->v;
                } else
                    w->last_push = -1;
                continue;
            }
        }
        int done = 0;
        if (prim_imm(w, s, n)) {
            done = 1;
        } else {
            for (int k = 0; prims[k].n; k++) {
                if (!zl_wordeq(s, n, prims[k].n)) continue;
                for (int j = 0; j < 8 && prims[k].ops[j]; j++) {
                    if (prims[k].ops[j] == 0xFE)
                        E(w, ZL_OUTI, ZL_FMT_TRAIL, 0);
                    else if (prims[k].ops[j] == 0xFD) { /* CELLS: one address unit per cell */
                        if (w->last_push == (int32_t) c->p->ncode - 1) this_push = w->last_push;
                    } else
                        E(w, prims[k].ops[j], 0, 0);
                }
                done = 1;
                break;
            }
        }
        if (!done) {
            if ((s_ = control(w, s, n, &done))) return s_;
        }
        if (!done) {
            if (zl_wordeq(s, n, "SPACE")) {
                int32_t k = zl_addstr(c, " ", 1);
                E(w, ZL_OUTS, k, 0);
                done = 1;
            }
        }
        if (!done) {
            static const char *const unsup[] = {
                "BASE",   "HEX", "DECIMAL", "EVALUATE", "POSTPONE", "IMMEDIATE", "DOES>", "KEY",
                "ACCEPT", "F+",  "F.",      "FLOAT",    "S\"",      "TYPE",      "COUNT", "MOVE",
                "FILL",   "C@",  "C!",      "2@",       "2!",       ">R",        "R>",    "R@",
                "DEPTH",  ".S",  "WORDS",   "SEE",      "INCLUDE",  "INCLUDED",  0};
            for (int k = 0; unsup[k]; k++)
                if (zl_wordeq(s, n, unsup[k]))
                    return qfail(w, ZL_E_UNSUPPORTED, "word not in this subset");
            if (n && s[0] == '-' && n > 1 && zl_isdigit((unsigned char) s[1]))
                return qfail(w, ZL_E_UNSUPPORTED, "only decimal integers are in this subset");
            for (size_t k = 0; k < n; k++)
                if (zl_isdigit((unsigned char) s[0]) && (s[k] == '.' || s[k] == 'E' || s[k] == 'e'))
                    return qfail(w, ZL_E_UNSUPPORTED,
                                 "floating-point and double numbers are not in this subset");
            return qfail(w, ZL_E_UNDEFINED, "undefined word");
        }
        tok(w, s, n);
        if (!w->in_def && zl_wordeq(s, n, "CR")) line_end(w); /* top-level lines end at CR */
        w->last_push = this_push;
        if (c->failed) return c->d->status;
    }
    if (w->in_def) return qfail(w, ZL_E_SYNTAX, "definition not ended with ;");
    line_end(w);
    E(w, ZL_HALT, 0, 0);
    return c->failed ? c->d->status : ZL_OK;
}
