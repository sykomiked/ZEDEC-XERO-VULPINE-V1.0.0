/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* gen_pascal.c — random Pascal programs for differential testing against
 * fpc -Miso (ref_pascal.sh runs fpc and zxlang on the same programs and
 * requires identical output).
 *
 * Each program has the same frame: global integers, Booleans and an array
 * with a random lower bound, a pure function of two integers, a recursive
 * function, a Boolean function, and a procedure with a value parameter, two
 * var parameters, a local array and a nested procedure that changes its
 * parent's local through the static link. The main body is random:
 * assignments, if/else, for to/downto, while and repeat loops on their own
 * counters, case over e mod k with every label present, calls with
 * variables, elements and aliased variables as var arguments, and write /
 * writeln of integers, Booleans and strings with and without widths.
 *
 * The generator runs what it writes. It builds the program as a tree,
 * executes the tree with Pascal's rules (div truncates, mod is never
 * negative, and and or stop early), and whenever an operation would leave
 * 32 bits, divide by zero, take mod of a number below 1 or index outside
 * an array, it replaces that operation by its left operand (an index by
 * the lower bound) and runs again, until a run is clean. So every program
 * is well defined in ISO 7185 and fpc and Lingua must print the same text.
 * Functions are pure, so the order fpc picks for operands cannot show.
 *
 * The same tree prints as Delphi (ref_delphi.sh, fpc -Mdelphi), Modula-2
 * (ref_modula2.sh, gm2 -fpim4) or Oberon (ref_oberon.sh, OBNC), executed
 * with that language's rules: in Delphi mod truncates (a divisor of zero
 * is the only unsafe one) and a case without a matching label does
 * nothing or runs its else; in Modula-2 and Oberon DIV rounds down, MOD is
 * never negative and a divisor below 1 is unsafe. Delphi adds bitwise and,
 * or and xor on integers, Inc and Dec, Result and Exit, Continue and Break,
 * case ranges and else, and // comments; Modula-2 adds INC and DEC, LOOP
 * and EXIT, ELSIF, CASE ranges and ELSE; Oberon adds INC and DEC, ELSIF
 * and CASE ranges. Modula-2 and Oberon have no write for Booleans, so a
 * Boolean item is printed through IF; succ and pred print as e + 1 and
 * e - 1, sqr becomes abs, and Oberon's v starts at 0 (its arrays do) and
 * its nested procedure takes pr's t and x as parameters (an Oberon
 * procedure cannot see the variables of the procedures around it). The
 * Pascal stream (no dialect argument) is unchanged by all of this. oberon-m2
 * prints the Oberon program (the same tree) as Modula-2, so gm2 can check
 * what zxlang prints for the Oberon text where no Oberon compiler is
 * installed; gm2 13 aborts a BY -1 loop that crosses from 0 to -1 (a
 * defect), so neither mode makes one.
 *
 * Usage: gen_pascal [delphi|m2|oberon|oberon-m2] <seed>   (writes the program to stdout) */
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t rng;
static unsigned rnd(unsigned n)
{
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return n ? (unsigned) (rng % n) : 0;
}

enum { D_ISO, D_DELPHI, D_M2, D_OBERON };
static int dia;   /* every new random choice is made only when dia != D_ISO */
static int as_m2; /* oberon-m2: an Oberon program printed as Modula-2 */
static int obp(void)
{
    return dia == D_OBERON && !as_m2;
}

/* ------------------------------------------------------------ names */

enum {
    G0,
    G1,
    G2,
    G3,
    G4,
    G5, /* global integers */
    B0,
    B1, /* global Booleans */
    LI,
    LJ,
    C0,
    C1,
    C2, /* main: loop variables and counters */
    PX,
    PY,
    PZ,
    PT,
    PI, /* pr(x; var y, z), locals t, i */
    IU, /* inner(var u) */
    FX,
    FY,
    FT, /* f1(x, y), local t */
    RN, /* fr(n) */
    BX, /* fb(x) */
    NVAR
};
static const char *const vname[NVAR] = {"g0", "g1", "g2", "g3", "g4", "g5", "b0", "b1",
                                        "i",  "j",  "c0", "c1", "c2", "x",  "y",  "z",
                                        "t",  "i",  "u",  "x",  "y",  "t",  "n",  "x"};
enum { A_V = 1, A_W = 2 }; /* the global array v and pr's local array w */

/* ------------------------------------------------------------ expressions */

enum {
    E_NUM,
    E_CONST,
    E_VAR,
    E_ELEM,
    E_BIN,
    E_NEG,
    E_FN,
    E_F1,
    E_FR,
    E_FRREC,
    E_FB,
    E_REL,
    E_AND,
    E_OR,
    E_NOT,
    E_ODD,
    E_BOOL,
    E_BVAR,
    E_MAXINT,
    E_ORD
};
enum { O_ADD, O_SUB, O_MUL, O_DIV, O_MOD, O_BAND, O_BOR, O_BXOR };
enum { F_ABS, F_SQR, F_SUCC, F_PRED };
enum { R_EQ, R_NE, R_LT, R_LE, R_GT, R_GE };

typedef struct {
    int kind, op, a, b, v;
} enode_t;

#define MAXE 60000
static enode_t en[MAXE];
static int nen;

static int enew(int kind, int op, int a, int b, int v)
{
    if (nen >= MAXE) {
        fprintf(stderr, "gen_pascal: expression pool full\n");
        exit(2);
    }
    en[nen].kind = kind;
    en[nen].op = op;
    en[nen].a = a;
    en[nen].b = b;
    en[nen].v = v;
    return nen++;
}

/* what an expression may use */
typedef struct {
    const int *ivars;
    int ni;
    const int *bvars;
    int nb;
    int arrays; /* A_V | A_W */
    int calls;  /* may call f1, fr, fb */
} scope_t;

static int64_t k0, k1; /* the constants k0 and k1 */
static int vlo, vlen;  /* v: array[vlo .. vlo + vlen - 1] */

static int gen_bool(const scope_t *s, int d);

static int gen_int(const scope_t *s, int d)
{
    unsigned k = d <= 0 ? rnd(4) : rnd(20);
    if (k == 0 || ((k == 2 || k == 3) && !s->ni))
        return enew(E_NUM, 0, -1, -1, (int) rnd(30) - (rnd(4) ? 0 : 10));
    if (k == 1) return enew(E_CONST, 0, -1, -1, (int) rnd(2));
    if (k == 2 || k == 3) return enew(E_VAR, 0, -1, -1, s->ivars[rnd((unsigned) s->ni)]);
    if (d <= 0) return enew(E_NUM, 0, -1, -1, (int) rnd(100));
    if (k <= 8) {
        int op = (int) rnd(5);
        if (dia == D_DELPHI && !rnd(4)) op = O_BAND + (int) rnd(3);
        int a = gen_int(s, d - 1), b = gen_int(s, d - 1);
        return enew(E_BIN, op, a, b, 0);
    }
    if (k == 9) return enew(E_NEG, 0, gen_int(s, d - 1), -1, 0);
    if (k == 10 || k == 11) {
        int a = gen_int(s, d - 1), op = (int) rnd(4); /* the order gcc gave the old call */
        if (dia >= D_M2 && op == F_SQR) op = F_ABS;
        return enew(E_FN, op, a, -1, 0);
    }
    if (k == 12 && s->arrays) {
        int arr = (s->arrays & A_W) && (!(s->arrays & A_V) || rnd(2)) ? A_W : A_V;
        return enew(E_ELEM, 0, gen_int(s, d - 1), -1, arr);
    }
    if (k == 13 && s->calls) return enew(E_F1, 0, gen_int(s, d - 1), gen_int(s, d - 1), 0);
    if (k == 14 && s->calls) /* fr((e) mod 6): depth 0 to 5 */
        return enew(E_FR, 0, enew(E_BIN, O_MOD, gen_int(s, d - 1), enew(E_NUM, 0, -1, -1, 6), 0),
                    -1, 0);
    if (k == 15 && !rnd(4)) return enew(E_MAXINT, 0, -1, -1, 0);
    if (k == 16 && dia < D_M2) /* ord(b) + 0: fpc gives ord of a Boolean a 0..1 type, so pred of it
                    is a range error under -Cr; ISO 7185 says ord is an integer */
        return enew(E_BIN, O_ADD, enew(E_ORD, 0, gen_bool(s, d - 1), -1, 0),
                    enew(E_NUM, 0, -1, -1, 0), 0);
    return enew(E_NUM, 0, -1, -1, (int) rnd(1000));
}

static int gen_bool(const scope_t *s, int d)
{
    unsigned k = d <= 0 ? rnd(3) : rnd(12);
    if (k == 0) return enew(E_BOOL, 0, -1, -1, (int) rnd(2));
    if (k == 1 && s->nb) return enew(E_BVAR, 0, -1, -1, s->bvars[rnd((unsigned) s->nb)]);
    if (k <= 5 || d <= 0) {
        int a = gen_int(s, d > 0 ? d - 1 : 0), b = gen_int(s, d > 0 ? d - 1 : 0);
        return enew(E_REL, (int) rnd(6), a, b, 0);
    }
    if (k == 6) return enew(E_AND, 0, gen_bool(s, d - 1), gen_bool(s, d - 1), 0);
    if (k == 7) return enew(E_OR, 0, gen_bool(s, d - 1), gen_bool(s, d - 1), 0);
    if (k == 8) return enew(E_NOT, 0, gen_bool(s, d - 1), -1, 0);
    if (k == 9) return enew(E_ODD, 0, gen_int(s, d - 1), -1, 0);
    if (k == 10 && s->calls) return enew(E_FB, 0, gen_int(s, d - 1), -1, 0);
    return enew(E_REL, R_LT, gen_int(s, d - 1), gen_int(s, d - 1), 0);
}

/* ------------------------------------------------------------ statements */

enum {
    S_ASSIGN,
    S_ELEM,
    S_BASSIGN,
    S_IF,
    S_FOR,
    S_WHILE,
    S_REPEAT,
    S_CASE,
    S_WRITE,
    S_CALL,
    S_INC
};
enum { W_INT, W_BOOL, W_STR };

typedef struct {
    int kind, var, e1, e2, e3, body, els, next, down, width, item, arm;
} snode_t;
typedef struct {
    int kind, e, width, str, next;
} item_t;
typedef struct {
    int labels[4], nl, body, next;
} arm_t;

#define MAXS 4000
static snode_t sn[MAXS];
static int nsn;
static item_t it[MAXS];
static int nit;
static arm_t am[MAXS];
static int nam;

static int snew(int kind)
{
    if (nsn >= MAXS) {
        fprintf(stderr, "gen_pascal: statement pool full\n");
        exit(2);
    }
    memset(&sn[nsn], 0, sizeof sn[nsn]);
    sn[nsn].kind = kind;
    sn[nsn].body = sn[nsn].els = sn[nsn].next = sn[nsn].item = sn[nsn].arm = -1;
    sn[nsn].e1 = sn[nsn].e2 = sn[nsn].e3 = -1;
    return nsn++;
}

static const char *const strs[] = {"a", "b c", "it''s", "--", "x=", "", "Pascal", "|"};

static const int main_ivars[] = {G0, G1, G2, G3, G4, G5};
static const int main_bvars[] = {B0, B1};
static int loopvars_used, counters_used;

static int gen_stmts(int depth, int n);

static int gen_write(const scope_t *s)
{
    int st = snew(S_WRITE), last = -1, n = 1 + (int) rnd(4);
    sn[st].var = (int) rnd(3) != 0; /* writeln */
    for (int k = 0; k < n; k++) {
        if (nit >= MAXS) {
            fprintf(stderr, "gen_pascal: item pool full\n");
            exit(2);
        }
        item_t *x = &it[nit];
        x->next = -1;
        x->width = rnd(2) ? -1 : (int) rnd(14);
        x->kind = (int) rnd(3);
        x->str = (int) rnd(sizeof strs / sizeof strs[0]);
        x->e = x->kind == W_INT    ? gen_int(s, 1 + (int) rnd(3))
               : x->kind == W_BOOL ? gen_bool(s, 2)
                                   : -1;
        if (last >= 0)
            it[last].next = nit;
        else
            sn[st].item = nit;
        last = nit++;
    }
    return st;
}

static int gen_stmt(int depth)
{
    scope_t s = {main_ivars, 6, main_bvars, 2, A_V, 1};
    unsigned k = depth >= 3 ? rnd(4) : rnd(11);
    int st;
    switch (k) {
    case 0:
        if (dia != D_ISO && !rnd(3)) { /* Inc(g[, e]) / Dec: g := g + e, repaired to g := g */
            st = snew(S_INC);
            sn[st].var = main_ivars[rnd(6)];
            int by = rnd(2) ? enew(E_NUM, 0, -1, -1, 1) : gen_int(&s, 1 + (int) rnd(2));
            sn[st].e1 =
                enew(E_BIN, rnd(2) ? O_ADD : O_SUB, enew(E_VAR, 0, -1, -1, sn[st].var), by, 0);
            return st;
        }
        st = snew(S_ASSIGN);
        sn[st].var = main_ivars[rnd(6)];
        sn[st].e1 = gen_int(&s, 1 + (int) rnd(4));
        return st;
    case 1:
        st = snew(S_ELEM);
        sn[st].e1 = gen_int(&s, 1);
        sn[st].e2 = gen_int(&s, 1 + (int) rnd(3));
        return st;
    case 2:
        st = snew(S_BASSIGN);
        sn[st].var = main_bvars[rnd(2)];
        sn[st].e1 = gen_bool(&s, 1 + (int) rnd(3));
        return st;
    case 3:
        return gen_write(&s);
    case 4:
    case 5:
        st = snew(S_IF);
        sn[st].e1 = gen_bool(&s, 1 + (int) rnd(3));
        sn[st].body = gen_stmts(depth + 1, 1 + (int) rnd(3));
        if (rnd(2)) sn[st].els = gen_stmts(depth + 1, 1 + (int) rnd(2));
        return st;
    case 6:
        if (loopvars_used >= 2) return gen_write(&s);
        st = snew(S_FOR);
        sn[st].var = LI + loopvars_used++;
        sn[st].down = (int) rnd(2);
        sn[st].e1 = enew(E_NUM, 0, -1, -1, (int) rnd(5) - 2);
        sn[st].e2 = enew(E_NUM, 0, -1, -1, (int) rnd(6) - 1);
        if (sn[st].down) {
            int t = sn[st].e1;
            sn[st].e1 = sn[st].e2;
            sn[st].e2 = t;
            /* gm2 13 -fsoft-check-all aborts a BY -1 loop whose variable would go
             * from 0 to -1 (a defect: PIM defines the loop), so none is made */
            if (dia >= D_M2 && en[sn[st].e1].v >= 0 && en[sn[st].e2].v < 0) en[sn[st].e2].v = 0;
        }
        sn[st].body = gen_stmts(depth + 1, 1 + (int) rnd(3));
        loopvars_used--;
        return st;
    case 7:
    case 8:
        if (counters_used >= 3) return gen_write(&s);
        st = snew(k == 7 ? S_WHILE : S_REPEAT);
        sn[st].var = C0 + counters_used++;
        sn[st].width = 1 + (int) rnd(4); /* the count */
        if (k == 7 && (dia == D_DELPHI || dia == D_M2) && rnd(2)) {
            /* count down first, then Continue when e2, Break after the body when e3
             * (Modula-2: a LOOP with EXIT) */
            sn[st].e2 = gen_bool(&s, 1 + (int) rnd(2));
            sn[st].e3 = gen_bool(&s, 1 + (int) rnd(2));
        }
        sn[st].body = gen_stmts(depth + 1, 1 + (int) rnd(3));
        counters_used--;
        return st;
    case 9: {
        st = snew(S_CASE);
        int kk = 2 + (int) rnd(4), last = -1;
        sn[st].width = kk;
        sn[st].e1 = gen_int(&s, 1 + (int) rnd(2));
        int perm[6];
        for (int j = 0; j < kk; j++) perm[j] = j;
        for (int j = kk - 1; j > 0; j--) {
            int r = (int) rnd((unsigned) j + 1), t = perm[j];
            perm[j] = perm[r];
            perm[r] = t;
        }
        int labelled = kk; /* with an else, the last label of perm is left to it */
        if ((dia == D_DELPHI || dia == D_M2) && !rnd(3)) labelled = kk - 1;
        for (int j = 0; j < labelled;) {
            if (nam >= MAXS) {
                fprintf(stderr, "gen_pascal: case pool full\n");
                exit(2);
            }
            int a = nam++; /* claimed before the body, which may hold another case */
            am[a].nl = 0;
            am[a].next = -1;
            int take = 1 + (int) rnd(2);
            while (take-- && j < labelled) am[a].labels[am[a].nl++] = perm[j++];
            int b = rnd(5) ? gen_stmts(depth + 1, 1 + (int) rnd(2)) : -1;
            am[a].body = b;
            if (last >= 0)
                am[last].next = a;
            else
                sn[st].arm = a;
            last = a;
        }
        if (labelled < kk) sn[st].els = gen_stmts(depth + 1, 1 + (int) rnd(2));
        return st;
    }
    default: {
        st = snew(S_CALL);
        sn[st].e1 = gen_int(&s, 1 + (int) rnd(3));
        sn[st].var = main_ivars[rnd(6)];
        /* the second var argument: a global (perhaps the same one) or an element */
        if (rnd(3) == 0) {
            sn[st].e2 = gen_int(&s, 1);
            sn[st].e3 = -1;
        } else {
            sn[st].e3 = rnd(4) == 0 ? sn[st].var : main_ivars[rnd(6)];
        }
        return st;
    }
    }
}

static int gen_stmts(int depth, int n)
{
    int first = -1, last = -1;
    for (int k = 0; k < n; k++) {
        int s = gen_stmt(depth);
        if (last >= 0)
            sn[last].next = s;
        else
            first = s;
        last = s;
    }
    return first;
}

/* the fixed parts' expressions */
static int f1a, f1b, fr0, frstep, fbe, pt, pw, iu, it_, pz;
static int body;

/* ------------------------------------------------------------ execution */

typedef struct frame {
    int64_t loc[NVAR];
    int64_t *ref[NVAR];
    unsigned char has[NVAR];
    int64_t w[4];
    int has_w;
    struct frame *parent;
} frame_t;

static int64_t glob[NVAR], varr[64];
static jmp_buf bad;
static int bad_node, bad_arr; /* bad_arr: 0 replace by the left operand, else the index's array */
static long steps;

static void unsafe(int node, int arr)
{
    bad_node = node;
    bad_arr = arr;
    longjmp(bad, 1);
}

static void step(void)
{
    if (++steps > 200000) {
        bad_node = -1;
        longjmp(bad, 1);
    }
}

static int64_t chk(int node, int64_t v)
{
    if (v < INT32_MIN || v > INT32_MAX) unsafe(node, 0);
    return v;
}

/* a divisor the dialect does not define (or the reference would refuse) */
static int divbad(int op, int64_t y)
{
    if (op != O_DIV && op != O_MOD) return 0;
    if (dia == D_ISO) return op == O_DIV ? y == 0 : y <= 0;
    if (dia == D_DELPHI) return y == 0;
    return y <= 0;
}

static int64_t arith(int op, int64_t x, int64_t y)
{
    switch (op) {
    case O_ADD:
        return x + y;
    case O_SUB:
        return x - y;
    case O_MUL:
        return x * y;
    case O_DIV: /* y > 0 in Modula-2 and Oberon, which round down */
        return x / y - (dia >= D_M2 && x % y < 0);
    case O_MOD:
        return dia == D_DELPHI ? x % y : ((x % y) + y) % y;
    case O_BAND:
        return x & y;
    case O_BOR:
        return x | y;
    default:
        return x ^ y;
    }
}

static int64_t *cell(frame_t *f, int var)
{
    for (; f; f = f->parent)
        if (f->has[var]) return f->ref[var] ? f->ref[var] : &f->loc[var];
    return &glob[var];
}

/* idx: the index expression, replaced by the lower bound when outside */
static int64_t *elem(frame_t *f, int idx, int arr, int64_t i)
{
    if (arr == A_W) {
        for (; f && !f->has_w; f = f->parent) {
        }
        if (!f || i < 0 || i > 3) unsafe(idx, A_W);
        return &f->w[i];
    }
    if (i < vlo || i >= vlo + vlen) unsafe(idx, A_V);
    return &varr[i - vlo];
}

static int64_t ev(frame_t *f, int id);

static int64_t call_f1(int64_t x, int64_t y)
{
    frame_t g;
    memset(&g, 0, sizeof g);
    g.has[FX] = g.has[FY] = g.has[FT] = 1;
    g.loc[FX] = x;
    g.loc[FY] = y;
    g.loc[FT] = ev(&g, f1a);
    return ev(&g, f1b);
}

static int64_t call_fr(int64_t n)
{
    frame_t g;
    memset(&g, 0, sizeof g);
    g.has[RN] = 1;
    g.loc[RN] = n;
    step();
    return n <= 0 ? ev(&g, fr0) : ev(&g, frstep);
}

static int64_t ev(frame_t *f, int id)
{
    enode_t *e = &en[id];
    int64_t x, y;
    step();
    switch (e->kind) {
    case E_NUM:
        return e->v;
    case E_CONST:
        return e->v ? k1 : k0;
    case E_MAXINT:
        return INT32_MAX;
    case E_VAR:
    case E_BVAR:
        return *cell(f, e->v);
    case E_BOOL:
        return e->v;
    case E_ELEM:
        x = ev(f, e->a);
        return *elem(f, e->a, e->v, x);
    case E_BIN:
        x = ev(f, e->a);
        y = ev(f, e->b);
        if (divbad(e->op, y)) unsafe(id, 0);
        return chk(id, arith(e->op, x, y));
    case E_NEG:
        return chk(id, -ev(f, e->a));
    case E_FN:
        x = ev(f, e->a);
        switch (e->op) {
        case F_ABS:
            return chk(id, x < 0 ? -x : x);
        case F_SQR:
            return chk(id, x * x);
        case F_SUCC:
            return chk(id, x + 1);
        default:
            return chk(id, x - 1);
        }
    case E_F1:
        x = ev(f, e->a);
        y = ev(f, e->b);
        return call_f1(x, y);
    case E_FR:
        return call_fr(ev(f, e->a));
    case E_FRREC:
        return call_fr(*cell(f, RN) - 1);
    case E_FB: {
        frame_t g;
        memset(&g, 0, sizeof g);
        g.has[BX] = 1;
        g.loc[BX] = ev(f, e->a);
        return ev(&g, fbe);
    }
    case E_REL:
        x = ev(f, e->a);
        y = ev(f, e->b);
        switch (e->op) {
        case R_EQ:
            return x == y;
        case R_NE:
            return x != y;
        case R_LT:
            return x < y;
        case R_LE:
            return x <= y;
        case R_GT:
            return x > y;
        default:
            return x >= y;
        }
    case E_AND:
        return ev(f, e->a) ? ev(f, e->b) : 0;
    case E_OR:
        return ev(f, e->a) ? 1 : ev(f, e->b);
    case E_NOT:
        return !ev(f, e->a);
    case E_ODD:
        return (ev(f, e->a) % 2) != 0;
    default: /* E_ORD */
        return ev(f, e->a);
    }
}

static void call_pr(int64_t x, int64_t *y, int64_t *z)
{
    frame_t p, q;
    memset(&p, 0, sizeof p);
    p.has[PX] = p.has[PY] = p.has[PZ] = p.has[PT] = p.has[PI] = 1;
    p.has_w = 1;
    p.loc[PX] = x;
    p.ref[PY] = y;
    p.ref[PZ] = z;
    step();
    p.loc[PT] = ev(&p, pt);
    for (p.loc[PI] = 0; p.loc[PI] <= 3; p.loc[PI]++) p.w[p.loc[PI]] = ev(&p, pw);
    for (int k = 0; k < 2; k++) { /* inner(y); inner(z) */
        memset(&q, 0, sizeof q);
        q.parent = &p;
        q.has[IU] = 1;
        q.ref[IU] = k ? z : y;
        int64_t v = ev(&q, iu);
        *q.ref[IU] = v;
        p.loc[PT] = ev(&q, it_);
    }
    *z = ev(&p, pz);
}

static void exec(frame_t *f, int st);

static void exec_list(frame_t *f, int st)
{
    for (; st >= 0; st = sn[st].next) exec(f, st);
}

static void exec(frame_t *f, int st)
{
    snode_t *s = &sn[st];
    step();
    switch (s->kind) {
    case S_ASSIGN:
    case S_BASSIGN:
    case S_INC:
        glob[s->var] = ev(f, s->e1);
        break;
    case S_ELEM: {
        int64_t *c = elem(f, s->e1, A_V, ev(f, s->e1));
        *c = ev(f, s->e2);
        break;
    }
    case S_IF:
        if (ev(f, s->e1))
            exec_list(f, s->body);
        else
            exec_list(f, s->els);
        break;
    case S_FOR: {
        int64_t a = ev(f, s->e1), b = ev(f, s->e2);
        if (s->down)
            for (glob[s->var] = a; glob[s->var] >= b; glob[s->var]--) exec_list(f, s->body);
        else
            for (glob[s->var] = a; glob[s->var] <= b; glob[s->var]++) exec_list(f, s->body);
        break;
    }
    case S_WHILE:
    case S_REPEAT:
        glob[s->var] = s->width;
        if (s->e2 >= 0) { /* count down, Continue when e2, Break when e3 */
            while (glob[s->var] > 0) {
                glob[s->var]--;
                if (ev(f, s->e2)) continue;
                exec_list(f, s->body);
                if (ev(f, s->e3)) break;
            }
            break;
        }
        do {
            exec_list(f, s->body);
            glob[s->var]--;
        } while (glob[s->var] > 0);
        break;
    case S_CASE: {
        int64_t v = ev(f, s->e1);
        int64_t sel = dia == D_DELPHI ? v % s->width : ((v % s->width) + s->width) % s->width;
        int hit = 0;
        for (int a = s->arm; a >= 0; a = am[a].next)
            for (int j = 0; j < am[a].nl; j++)
                if (am[a].labels[j] == sel) {
                    exec_list(f, am[a].body);
                    hit = 1;
                }
        if (!hit) exec_list(f, s->els); /* Delphi: no else, nothing */
        break;
    }
    case S_WRITE:
        for (int k = s->item; k >= 0; k = it[k].next)
            if (it[k].e >= 0) ev(f, it[k].e);
        break;
    default: {
        int64_t x = ev(f, s->e1), *y = &glob[s->var], *z;
        if (s->e3 >= 0)
            z = &glob[s->e3];
        else
            z = elem(f, s->e2, A_V, ev(f, s->e2));
        call_pr(x, y, z);
        break;
    }
    }
}

static int64_t init[6];

/* Run the program: 0 when clean; 1 after repairing the tree; -1 too long. */
static int run_once(void)
{
    memset(glob, 0, sizeof glob);
    memset(varr, 0, sizeof varr);
    steps = 0;
    if (setjmp(bad)) {
        if (bad_node < 0) return -1;
        if (bad_arr) {
            en[bad_node].kind = E_NUM;
            en[bad_node].v = bad_arr == A_W ? 0 : vlo;
        } else {
            en[bad_node] = en[en[bad_node].a];
        }
        return 1;
    }
    frame_t m;
    memset(&m, 0, sizeof m);
    for (int k = 0; k < 6; k++) glob[G0 + k] = init[k];
    glob[B0] = 1;
    for (int k = 0; k < vlen; k++) varr[k] = (int64_t) (vlo + k) * 3 - 1;
    exec_list(&m, body);
    return 0;
}

/* fpc folds constant expressions and refuses one that overflows, divides by
 * zero, takes mod of a constant below 1 or indexes outside an array, even in
 * code that never runs. Returns 1 and the value when id is constant. */
static int fold(int id, int64_t *out)
{
    enode_t *e = &en[id];
    int64_t x = 0, y = 0;
    int cx, cy;
    switch (e->kind) {
    case E_NUM:
    case E_BOOL:
        *out = e->v;
        return 1;
    case E_CONST:
        *out = e->v ? k1 : k0;
        return 1;
    case E_MAXINT:
        *out = INT32_MAX;
        return 1;
    case E_BIN:
    case E_REL:
    case E_AND:
    case E_OR:
        cx = fold(e->a, &x);
        cy = fold(e->b, &y);
        if (e->kind == E_BIN && cy && divbad(e->op, y)) return -1;
        /* fpc also folds x mod 1 and x * 0 to the constant 0 (folding them
         * where fpc does not only costs a needless repair) */
        if (e->kind == E_BIN && cx >= 0 && cy >= 0 &&
            ((e->op == O_MOD && cy == 1 && y == 1) ||
             (e->op == O_MUL && ((cx == 1 && x == 0) || (cy == 1 && y == 0))))) {
            *out = 0;
            return 1;
        }
        if ((e->kind == E_AND || e->kind == E_OR) && cx >= 0 && cy >= 0 &&
            ((cx == 1 && x == (e->kind == E_OR)) || (cy == 1 && y == (e->kind == E_OR)))) {
            *out = e->kind == E_OR; /* x and false, x or true */
            return 1;
        }
        if (!cx || !cy || cx < 0 || cy < 0) return cx < 0 || cy < 0 ? -1 : 0;
        if (e->kind == E_REL)
            *out = e->op == R_EQ   ? x == y
                   : e->op == R_NE ? x != y
                   : e->op == R_LT ? x < y
                   : e->op == R_LE ? x <= y
                   : e->op == R_GT ? x > y
                                   : x >= y;
        else if (e->kind == E_AND)
            *out = x && y;
        else if (e->kind == E_OR)
            *out = x || y;
        else
            *out = arith(e->op, x, y);
        return *out < INT32_MIN || *out > INT32_MAX ? -1 : 1;
    case E_NEG:
    case E_FN:
    case E_NOT:
    case E_ODD:
    case E_ORD:
        cx = fold(e->a, &x);
        if (cx <= 0) return cx;
        *out = e->kind == E_NEG   ? -x
               : e->kind == E_NOT ? !x
               : e->kind == E_ODD ? (x % 2) != 0
               : e->kind == E_ORD ? x
               : e->op == F_ABS   ? (x < 0 ? -x : x)
               : e->op == F_SQR   ? x * x
               : e->op == F_SUCC  ? x + 1
                                  : x - 1;
        return *out < INT32_MIN || *out > INT32_MAX ? -1 : 1;
    case E_ELEM:
        cx = fold(e->a, &x);
        if (cx == 1 && (e->v == A_W ? (x < 0 || x > 3) : (x < vlo || x >= vlo + vlen))) {
            en[e->a].kind = E_NUM;
            en[e->a].v = e->v == A_W ? 0 : vlo;
        }
        return cx < 0 ? -1 : 0;
    default:
        return 0;
    }
}

/* Repair every node that fpc would refuse while compiling; 1 if any. */
static int static_repair(void)
{
    int changed = 0;
    for (int id = 0; id < nen; id++) {
        int64_t v;
        int k = en[id].kind;
        if ((k == E_BIN || k == E_NEG || k == E_FN) && fold(id, &v) < 0) {
            int64_t va, vb;
            /* repair the innermost: only when the operands themselves fold */
            if (fold(en[id].a, &va) >= 0 && (k != E_BIN || fold(en[id].b, &vb) >= 0)) {
                en[id] = en[en[id].a];
                changed = 1;
            }
        }
        if (k == E_ELEM) fold(id, &v);
        /* OBNC 0.17 crashes folding a constant product whose right operand is -1 */
        int64_t va, vb;
        if (dia == D_OBERON && k == E_BIN && en[id].op == O_MUL && fold(en[id].a, &va) == 1 &&
            fold(en[id].b, &vb) == 1 && vb == -1) {
            en[id] = en[en[id].a];
            changed = 1;
        }
    }
    for (int st = 0; st < nsn; st++) { /* element targets: v[c] := ... and var arguments */
        int64_t v;
        int idx = sn[st].kind == S_ELEM ? sn[st].e1 : sn[st].kind == S_CALL ? sn[st].e2 : -1;
        if (idx >= 0 && fold(idx, &v) == 1 && (v < vlo || v >= vlo + vlen)) {
            en[idx].kind = E_NUM;
            en[idx].v = vlo;
            changed = 1;
        }
    }
    return changed;
}

/* ------------------------------------------------------------ printing */

static int style; /* keywords: 0 lower, 1 upper, 2 capitalized */

static void kw(const char *k)
{
    for (int i = 0; k[i]; i++) {
        int ch = (unsigned char) k[i];
        if (style == 1 || (style == 2 && i == 0)) ch = ch >= 'a' && ch <= 'z' ? ch - 32 : ch;
        putchar(ch);
    }
}

static void name(const char *n)
{
    int up = rnd(8) == 0 && dia < D_M2; /* Pascal names are not case-sensitive */
    for (int i = 0; n[i]; i++) putchar(up && n[i] >= 'a' && n[i] <= 'z' ? n[i] - 32 : n[i]);
}

static void ind(int level)
{
    for (int i = 0; i < level; i++) fputs(rnd(8) ? "  " : "\t", stdout);
}

static void pe(int id, int need)
{
    enode_t *e = &en[id];
    static const char *const bop[] = {" + ", " - ", " * ", " div ", " mod "};
    static const char *const rop[] = {" = ", " <> ", " < ", " <= ", " > ", " >= "};
    static const char *const fn[] = {"abs", "sqr", "succ", "pred"};
    int p, paren;
    switch (e->kind) {
    case E_NUM:
        if (e->v < 0 && need > 0)
            printf("(%d)", e->v);
        else
            printf("%d", e->v);
        return;
    case E_CONST:
        name(e->v ? "k1" : "k0");
        return;
    case E_MAXINT:
        name("maxint");
        return;
    case E_VAR:
    case E_BVAR:
        name(vname[e->v]);
        return;
    case E_BOOL:
        kw(e->v ? "true" : "false");
        return;
    case E_ELEM:
        name(e->v == A_W ? "w" : "v");
        putchar('[');
        pe(e->a, 0);
        putchar(']');
        return;
    case E_NEG:
        if (need == 0 && rnd(2)) {
            putchar('-');
            pe(e->a, dia == D_DELPHI ? 4 : 3); /* Delphi: the sign takes a factor */
        } else {
            fputs("(-", stdout);
            pe(e->a, dia == D_DELPHI ? 4 : 3);
            putchar(')');
        }
        return;
    case E_FN:
        name(fn[e->op]);
        putchar('(');
        pe(e->a, 0);
        putchar(')');
        return;
    case E_F1:
        name("f1");
        putchar('(');
        pe(e->a, 0);
        fputs(rnd(3) ? ", " : ",", stdout);
        pe(e->b, 0);
        putchar(')');
        return;
    case E_FR:
        name("fr");
        putchar('(');
        pe(e->a, 0);
        putchar(')');
        return;
    case E_FRREC:
        name("fr");
        fputs("(", stdout);
        name("n");
        fputs(" - 1)", stdout);
        return;
    case E_FB:
        name("fb");
        putchar('(');
        pe(e->a, 0);
        putchar(')');
        return;
    case E_ODD:
    case E_ORD:
        name(e->kind == E_ODD ? "odd" : "ord");
        putchar('(');
        pe(e->a, 0);
        putchar(')');
        return;
    case E_NOT:
        kw("not");
        putchar(' ');
        pe(e->a, 4);
        return;
    default:
        break;
    }
    /* binary: + - or xor at 2, * div mod and at 3, comparisons at 1 */
    p = e->kind == E_REL                                      ? 1
        : e->kind == E_OR                                     ? 2
        : e->kind == E_AND                                    ? 3
        : e->op <= O_SUB || e->op == O_BOR || e->op == O_BXOR ? 2
                                                              : 3;
    paren = p < need || rnd(6) == 0;
    if (paren) putchar('(');
    if (e->kind == E_REL) {
        pe(e->a, 2);
        fputs(rop[e->op], stdout);
        pe(e->b, 2);
    } else {
        pe(e->a, p);
        if (e->kind == E_AND || e->kind == E_OR) {
            putchar(' ');
            kw(e->kind == E_AND ? "and" : "or");
            putchar(' ');
        } else if (e->op >= O_DIV) {
            static const char *const wop[] = {"div", "mod", "and", "or", "xor"};
            putchar(' ');
            kw(wop[e->op - O_DIV]);
            putchar(' ');
        } else {
            fputs(bop[e->op], stdout);
        }
        pe(e->b, p + 1);
    }
    if (paren) putchar(')');
}

static void ps_list(int st, int level, int wrap);

/* a case arm's labels; two consecutive ones may be a range (not in ISO Pascal) */
static void print_labels(int a)
{
    int *l = am[a].labels;
    if (dia != D_ISO && am[a].nl == 2 && (l[1] == l[0] + 1 || l[0] == l[1] + 1) && rnd(2)) {
        printf("%d..%d", l[0] < l[1] ? l[0] : l[1], l[0] < l[1] ? l[1] : l[0]);
        return;
    }
    for (int j = 0; j < am[a].nl; j++) printf(j ? ", %d" : "%d", l[j]);
}

static void ps(int st, int level)
{
    snode_t *s = &sn[st];
    if (rnd(12) == 0) {
        ind(level);
        fputs(dia == D_DELPHI && !rnd(3) ? "// step\n"
              : rnd(2)                   ? "{ step }\n"
                                         : "(* step *)\n",
              stdout);
    }
    ind(level);
    switch (s->kind) {
    case S_INC: {
        enode_t *e = &en[s->e1];
        if (e->kind == E_BIN && en[e->a].kind == E_VAR && en[e->a].v == s->var) {
            name(e->op == O_ADD ? "inc" : "dec");
            putchar('(');
            name(vname[s->var]);
            if (en[e->b].kind != E_NUM || en[e->b].v != 1 || rnd(2)) {
                fputs(", ", stdout);
                pe(e->b, 0);
            }
            putchar(')');
            break;
        }
        name(vname[s->var]); /* repaired */
        fputs(" := ", stdout);
        pe(s->e1, 0);
        break;
    }
    case S_ASSIGN:
    case S_BASSIGN:
        name(vname[s->var]);
        fputs(" := ", stdout);
        pe(s->e1, 0);
        break;
    case S_ELEM:
        name("v");
        putchar('[');
        pe(s->e1, 0);
        fputs("] := ", stdout);
        pe(s->e2, 0);
        break;
    case S_IF:
        kw("if");
        putchar(' ');
        pe(s->e1, 0);
        putchar(' ');
        kw("then");
        putchar('\n');
        ps_list(s->body, level + 1, s->els >= 0);
        if (s->els >= 0) {
            putchar('\n');
            ind(level);
            kw("else");
            putchar('\n');
            ps_list(s->els, level + 1, 0);
        }
        return;
    case S_FOR:
        kw("for");
        putchar(' ');
        name(vname[s->var]);
        fputs(" := ", stdout);
        pe(s->e1, 0);
        putchar(' ');
        kw(s->down ? "downto" : "to");
        putchar(' ');
        pe(s->e2, 0);
        putchar(' ');
        kw("do");
        putchar('\n');
        ps_list(s->body, level + 1, 0);
        return;
    case S_WHILE:
    case S_REPEAT:
        name(vname[s->var]);
        printf(" := %d;\n", s->width);
        ind(level);
        if (s->e2 >= 0) { /* Delphi: Continue and Break */
            kw("while");
            putchar(' ');
            name(vname[s->var]);
            fputs(" > 0 ", stdout);
            kw("do");
            putchar('\n');
            ind(level);
            kw("begin");
            putchar('\n');
            ind(level + 1);
            name(vname[s->var]);
            fputs(" := ", stdout);
            name(vname[s->var]);
            fputs(" - 1;\n", stdout);
            ind(level + 1);
            kw("if");
            putchar(' ');
            pe(s->e2, 0);
            putchar(' ');
            kw("then");
            putchar(' ');
            name("continue");
            fputs(";\n", stdout);
            for (int b = s->body; b >= 0; b = sn[b].next) {
                ps(b, level + 1);
                fputs(";\n", stdout);
            }
            ind(level + 1);
            kw("if");
            putchar(' ');
            pe(s->e3, 0);
            putchar(' ');
            kw("then");
            putchar(' ');
            name("break");
            putchar('\n');
            ind(level);
            kw("end");
            return;
        }
        if (s->kind == S_WHILE) {
            kw("while");
            putchar(' ');
            name(vname[s->var]);
            fputs(" > 0 ", stdout);
            kw("do");
            putchar('\n');
            ind(level);
            kw("begin");
            putchar('\n');
        } else {
            kw("repeat");
            putchar('\n');
        }
        for (int b = s->body; b >= 0; b = sn[b].next) {
            ps(b, level + 1);
            fputs(";\n", stdout);
        }
        ind(level + 1);
        name(vname[s->var]);
        fputs(" := ", stdout);
        name(vname[s->var]);
        fputs(" - 1\n", stdout);
        ind(level);
        if (s->kind == S_WHILE) {
            kw("end");
        } else {
            kw("until");
            putchar(' ');
            name(vname[s->var]);
            fputs(" <= 0", stdout);
        }
        return;
    case S_CASE:
        kw("case");
        fputs(" (", stdout);
        pe(s->e1, 0);
        fputs(") ", stdout);
        kw("mod");
        printf(" %d ", s->width);
        kw("of");
        putchar('\n');
        for (int a = s->arm; a >= 0; a = am[a].next) {
            ind(level + 1);
            print_labels(a);
            fputs(":\n", stdout);
            if (am[a].body >= 0) ps_list(am[a].body, level + 2, 0);
            /* a ';' before else, which would otherwise join a trailing if */
            if (am[a].next >= 0 || s->els >= 0 || rnd(2)) putchar(';');
            putchar('\n');
        }
        if (s->els >= 0) { /* Delphi: else and a statement list */
            ind(level);
            kw("else");
            putchar('\n');
            for (int b = s->els; b >= 0; b = sn[b].next) {
                ps(b, level + 1);
                fputs(sn[b].next >= 0 || rnd(2) ? ";\n" : "\n", stdout);
            }
        }
        ind(level);
        kw("end");
        return;
    case S_WRITE:
        name(s->var ? "writeln" : "write");
        putchar('(');
        for (int k = s->item; k >= 0; k = it[k].next) {
            if (k != s->item) fputs(", ", stdout);
            if (it[k].kind == W_STR)
                printf("'%s'", strs[it[k].str]);
            else
                pe(it[k].e, 0);
            if (it[k].width >= 0) printf(":%d", it[k].width);
        }
        putchar(')');
        break;
    default:
        name("pr");
        putchar('(');
        pe(s->e1, 0);
        fputs(", ", stdout);
        name(vname[s->var]);
        fputs(", ", stdout);
        if (s->e3 >= 0) {
            name(vname[s->e3]);
        } else {
            name("v");
            putchar('[');
            pe(s->e2, 0);
            putchar(']');
        }
        putchar(')');
        break;
    }
}

/* a statement list as one statement: begin ... end unless it is one
 * statement and need not be wrapped */
static void ps_list(int st, int level, int wrap)
{
    if (st < 0) {
        ind(level);
        kw("begin");
        putchar(' ');
        kw("end");
        return;
    }
    if (sn[st].next < 0 && !wrap && sn[st].kind != S_WHILE && sn[st].kind != S_REPEAT && rnd(4)) {
        ps(st, level);
        return;
    }
    ind(level);
    kw("begin");
    putchar('\n');
    for (int b = st; b >= 0; b = sn[b].next) {
        ps(b, level + 1);
        fputs(sn[b].next >= 0 ? ";\n" : "\n", stdout);
    }
    ind(level);
    kw("end");
}

static void generate(void)
{
    nen = nsn = nit = nam = 0;
    loopvars_used = counters_used = 0;
    k0 = (int64_t) rnd(20) - 5;
    k1 = -(int64_t) rnd(9) - 1;
    vlo = (int) rnd(7) - 3;
    if (dia == D_OBERON) vlo = 0;
    vlen = 3 + (int) rnd(6);
    for (int k = 0; k < 6; k++) init[k] = (int64_t) rnd(50) - 10;
    static const int f1v[] = {FX, FY}, f1w[] = {FT, FX, FY}, rn[] = {RN}, bx[] = {BX};
    static const int ptv[] = {PX}, pwv[] = {PX, PT, PI}, iuv[] = {IU, PT, PX},
                     pzv[] = {PX, PY, PZ, PT};
    scope_t s1 = {f1v, 2, 0, 0, 0, 0}, s2 = {f1w, 3, 0, 0, 0, 0}, s0 = {0, 0, 0, 0, 0, 0},
            sr = {rn, 1, 0, 0, 0, 0}, sb = {bx, 1, 0, 0, 0, 0}, sp1 = {ptv, 1, 0, 0, 0, 1},
            sp2 = {pwv, 3, 0, 0, 0, 0}, sp3 = {iuv, 3, 0, 0, 0, 0},
            sp4 = {pzv, 4, main_bvars, 2, A_W, 1};
    f1a = gen_int(&s1, 3);
    f1b = gen_int(&s2, 3);
    fr0 = gen_int(&s0, 2);
    frstep = enew(E_BIN, (int) rnd(2), enew(E_FRREC, 0, -1, -1, 0), gen_int(&sr, 2), 0);
    fbe = gen_bool(&sb, 3);
    pt = gen_int(&sp1, 3);
    pw = gen_int(&sp2, 2);
    iu = gen_int(&sp3, 3);
    it_ = gen_int(&sp3, 2);
    pz = gen_int(&sp4, 3);
    body = gen_stmts(0, 6 + (int) rnd(14));
}

static void print_program(void)
{
    style = (int) rnd(3);
    kw("program");
    if (dia == D_DELPHI) {
        fputs(" gen;\n", stdout);
        if (rnd(2)) fputs("{$APPTYPE CONSOLE}\n", stdout);
    } else {
        fputs(" gen(", stdout);
        name("output");
        fputs(");\n", stdout);
    }
    kw("const");
    printf("\n  k0 = %lld;\n  k1 = %lld;\n", (long long) k0, (long long) k1);
    kw("var");
    fputs("\n  g0, g1, g2, g3, g4, g5, i, j, c0, c1, c2: ", stdout);
    kw("integer");
    fputs(";\n  b0, b1: ", stdout);
    kw("boolean");
    fputs(";\n  v: ", stdout);
    kw("array");
    printf("[%d..%d] ", vlo, vlo + vlen - 1);
    kw("of");
    putchar(' ');
    kw("integer");
    fputs(";\n\n", stdout);

    kw("function");
    fputs(" f1(x, y: integer): integer;\n", stdout);
    kw("var");
    fputs(" t: integer;\n", stdout);
    kw("begin");
    fputs("\n  t := ", stdout);
    pe(f1a, 0);
    fputs(";\n  ", stdout);
    if (dia == D_DELPHI)
        name(rnd(2) ? "result" : "f1");
    else
        fputs("f1", stdout);
    fputs(" := ", stdout);
    pe(f1b, 0);
    putchar('\n');
    kw("end");
    fputs(";\n\n", stdout);

    kw("function");
    fputs(" fr(n: integer): integer;\n", stdout);
    kw("begin");
    fputs("\n  ", stdout);
    kw("if");
    fputs(" n <= 0 ", stdout);
    kw("then");
    if (dia == D_DELPHI && rnd(2)) { /* Exit(value), then Result */
        putchar(' ');
        name("exit");
        putchar('(');
        pe(fr0, 0);
        fputs(");\n  ", stdout);
        name("result");
        fputs(" := ", stdout);
        pe(frstep, 0);
        putchar('\n');
    } else {
        fputs(" fr := ", stdout);
        pe(fr0, 0);
        fputs("\n  ", stdout);
        kw("else");
        fputs(" fr := ", stdout);
        pe(frstep, 0);
        putchar('\n');
    }
    kw("end");
    fputs(";\n\n", stdout);

    kw("function");
    fputs(" fb(x: integer): boolean;\n", stdout);
    kw("begin");
    fputs("\n  ", stdout);
    if (dia == D_DELPHI)
        name(rnd(2) ? "result" : "fb");
    else
        fputs("fb", stdout);
    fputs(" := ", stdout);
    pe(fbe, 0);
    putchar('\n');
    kw("end");
    fputs(";\n\n", stdout);

    kw("procedure");
    fputs(" pr(x: integer; ", stdout);
    kw("var");
    fputs(" y, z: integer);\n", stdout);
    kw("var");
    fputs(" t, i: integer; w: ", stdout);
    kw("array");
    fputs("[0..3] ", stdout);
    kw("of");
    fputs(" integer;\n  ", stdout);
    kw("procedure");
    fputs(" inner(", stdout);
    kw("var");
    fputs(" u: integer);\n  ", stdout);
    kw("begin");
    fputs("\n    u := ", stdout);
    pe(iu, 0);
    fputs(";\n    t := ", stdout);
    pe(it_, 0);
    fputs("\n  ", stdout);
    kw("end");
    fputs(";\n", stdout);
    kw("begin");
    fputs("\n  t := ", stdout);
    pe(pt, 0);
    fputs(";\n  ", stdout);
    kw("for");
    fputs(" i := 0 ", stdout);
    kw("to");
    fputs(" 3 ", stdout);
    kw("do");
    fputs(" w[i] := ", stdout);
    pe(pw, 0);
    fputs(";\n  inner(y);\n  inner(z);\n  z := ", stdout);
    pe(pz, 0);
    fputs(";\n  writeln('pr', x, y:6, z:6, t:6)\n", stdout);
    kw("end");
    fputs(";\n\n", stdout);

    kw("begin");
    putchar('\n');
    for (int k = 0; k < 6; k++) printf("  g%d := %lld;\n", k, (long long) init[k]);
    fputs("  b0 := ", stdout);
    kw("true");
    fputs("; b1 := ", stdout);
    kw("false");
    fputs(";\n  ", stdout);
    kw("for");
    printf(" i := %d ", vlo);
    kw("to");
    printf(" %d ", vlo + vlen - 1);
    kw("do");
    fputs(" v[i] := i * 3 - 1;\n", stdout);
    for (int b = body; b >= 0; b = sn[b].next) {
        ps(b, 1);
        fputs(";\n", stdout);
    }
    fputs("  writeln(g0, g1, g2, g3, g4, g5, b0, b1);\n  ", stdout);
    kw("for");
    printf(" i := %d ", vlo);
    kw("to");
    printf(" %d ", vlo + vlen - 1);
    kw("do");
    fputs(" write(v[i]:5);\n  writeln\n", stdout);
    kw("end");
    fputs(".\n", stdout);
}

/* ------------------------------------------------------------ Modula-2 and Oberon */

static void pm(int id, int need)
{
    enode_t *e = &en[id];
    int ob = obp(), p, paren;
    switch (e->kind) {
    case E_NUM:
        printf(e->v < 0 && need > 0 ? "(%d)" : "%d", e->v);
        return;
    case E_CONST:
        fputs(e->v ? "k1" : "k0", stdout);
        return;
    case E_MAXINT:
        fputs(ob ? "2147483647" : "MAX(INTEGER)", stdout);
        return;
    case E_VAR:
    case E_BVAR:
        fputs(vname[e->v], stdout);
        return;
    case E_BOOL:
        fputs(e->v ? "TRUE" : "FALSE", stdout);
        return;
    case E_ELEM:
        fputs(e->v == A_W ? "w[" : "v[", stdout);
        pm(e->a, 0);
        putchar(']');
        return;
    case E_NEG:
        if (need == 0 && rnd(2)) {
            putchar('-');
            pm(e->a, 3);
        } else {
            fputs("(-", stdout);
            pm(e->a, 3);
            putchar(')');
        }
        return;
    case E_FN: /* abs; succ and pred as e + 1 and e - 1 (sqr is not generated) */
        if (e->op == F_ABS || e->op == F_SQR) {
            fputs("ABS(", stdout);
            pm(e->a, 0);
            putchar(')');
        } else {
            putchar('(');
            pm(e->a, 2);
            fputs(e->op == F_SUCC ? " + 1)" : " - 1)", stdout);
        }
        return;
    case E_F1:
        fputs("f1(", stdout);
        pm(e->a, 0);
        fputs(rnd(3) ? ", " : ",", stdout);
        pm(e->b, 0);
        putchar(')');
        return;
    case E_FR:
    case E_FB:
        fputs(e->kind == E_FR ? "fr(" : "fb(", stdout);
        pm(e->a, 0);
        putchar(')');
        return;
    case E_FRREC:
        fputs("fr(n - 1)", stdout);
        return;
    case E_ODD:
        fputs("ODD(", stdout);
        pm(e->a, 0);
        putchar(')');
        return;
    case E_NOT:
        fputs(ob || rnd(3) == 0 ? "~" : "NOT ", stdout);
        pm(e->a, 4);
        return;
    default:
        break;
    }
    p = e->kind == E_REL ? 1 : e->kind == E_OR ? 2 : e->kind == E_AND ? 3 : e->op <= O_SUB ? 2 : 3;
    paren = p < need || rnd(6) == 0;
    if (paren) putchar('(');
    if (e->kind == E_REL) {
        static const char *const rop[] = {" = ", " # ", " < ", " <= ", " > ", " >= "};
        pm(e->a, 2);
        fputs(e->op == R_NE && !ob && rnd(2) ? " <> " : rop[e->op], stdout);
        pm(e->b, 2);
    } else {
        static const char *const bop[] = {" + ", " - ", " * ", " DIV ", " MOD "};
        pm(e->a, p);
        if (e->kind == E_AND)
            fputs(ob || rnd(3) == 0 ? " & " : " AND ", stdout);
        else if (e->kind == E_OR)
            fputs(" OR ", stdout);
        else
            fputs(bop[e->op], stdout);
        pm(e->b, p + 1);
    }
    if (paren) putchar(')');
}

static void psm(int st, int level);

static void pseq(int st, int level)
{
    for (int b = st; b >= 0; b = sn[b].next) {
        psm(b, level);
        fputs(sn[b].next >= 0 ? ";\n" : "\n", stdout);
    }
}

static void wint(int e, int width)
{
    fputs(obp() ? "Out.Int(" : "WriteInt(", stdout);
    pm(e, 0);
    printf(", %d)", width);
}

static void wstr(const char *s)
{
    fputs(obp() ? "Out.String(\"" : "WriteString(\"", stdout);
    for (; *s; s++) {
        putchar(*s);
        if (s[0] == '\'' && s[1] == '\'') s++; /* Pascal's '' is one quote */
    }
    fputs("\")", stdout);
}

static void wln(void)
{
    fputs(obp() ? "Out.Ln" : "WriteLn", stdout);
}

/* a Boolean as TRUE or FALSE: IF b THEN WriteString("TRUE") ELSE ... END */
static void wbool(int e)
{
    fputs("IF ", stdout);
    pm(e, 0);
    fputs(" THEN ", stdout);
    wstr("TRUE");
    fputs(" ELSE ", stdout);
    wstr("FALSE");
    fputs(" END", stdout);
}

static void psm(int st, int level)
{
    snode_t *s = &sn[st];
    const char *v = vname[s->var];
    if (rnd(12) == 0) {
        ind(level);
        fputs(rnd(2) ? "(* step *)\n" : "(* step (* nested *) *)\n", stdout);
    }
    ind(level);
    switch (s->kind) {
    case S_INC: {
        enode_t *e = &en[s->e1];
        if (e->kind == E_BIN && en[e->a].kind == E_VAR && en[e->a].v == s->var) {
            printf("%s(%s", e->op == O_ADD ? "INC" : "DEC", v);
            if (en[e->b].kind != E_NUM || en[e->b].v != 1 || rnd(2)) {
                fputs(", ", stdout);
                pm(e->b, 0);
            }
            putchar(')');
            return;
        }
        printf("%s := ", v);
        pm(s->e1, 0);
        return;
    }
    case S_ASSIGN:
    case S_BASSIGN:
        printf("%s := ", v);
        pm(s->e1, 0);
        return;
    case S_ELEM:
        fputs("v[", stdout);
        pm(s->e1, 0);
        fputs("] := ", stdout);
        pm(s->e2, 0);
        return;
    case S_IF:
        fputs("IF ", stdout);
        for (;;) {
            pm(s->e1, 0);
            fputs(" THEN\n", stdout);
            pseq(s->body, level + 1);
            /* ELSE IF ... END END as ELSIF when the else is one if */
            if (s->els >= 0 && sn[s->els].kind == S_IF && sn[s->els].next < 0 && rnd(2)) {
                s = &sn[s->els];
                ind(level);
                fputs("ELSIF ", stdout);
                continue;
            }
            break;
        }
        if (s->els >= 0) {
            ind(level);
            fputs("ELSE\n", stdout);
            pseq(s->els, level + 1);
        }
        ind(level);
        fputs("END", stdout);
        return;
    case S_FOR:
        printf("FOR %s := ", v);
        pm(s->e1, 0);
        fputs(" TO ", stdout);
        pm(s->e2, 0);
        fputs(s->down ? " BY -1 DO\n" : rnd(4) ? " DO\n" : " BY 1 DO\n", stdout);
        pseq(s->body, level + 1);
        ind(level);
        fputs("END", stdout);
        return;
    case S_WHILE:
    case S_REPEAT:
        printf("%s := %d;\n", v, s->width);
        ind(level);
        if (s->e2 >= 0) { /* Modula-2: LOOP and EXIT */
            fputs("LOOP\n", stdout);
            ind(level + 1);
            printf("IF %s <= 0 THEN EXIT END;\n", v);
            ind(level + 1);
            printf("%s := %s - 1;\n", v, v);
            ind(level + 1);
            fputs("IF NOT (", stdout);
            pm(s->e2, 0);
            fputs(") THEN\n", stdout);
            for (int b = s->body; b >= 0; b = sn[b].next) {
                psm(b, level + 2);
                fputs(";\n", stdout);
            }
            ind(level + 2);
            fputs("IF ", stdout);
            pm(s->e3, 0);
            fputs(" THEN EXIT END\n", stdout);
            ind(level + 1);
            fputs("END\n", stdout);
            ind(level);
            fputs("END", stdout);
            return;
        }
        if (s->kind == S_WHILE)
            printf("WHILE %s > 0 DO\n", v);
        else
            fputs("REPEAT\n", stdout);
        for (int b = s->body; b >= 0; b = sn[b].next) {
            psm(b, level + 1);
            fputs(";\n", stdout);
        }
        ind(level + 1);
        printf("%s := %s - 1\n", v, v);
        ind(level);
        if (s->kind == S_WHILE)
            fputs("END", stdout);
        else
            printf("UNTIL %s <= 0", v);
        return;
    case S_CASE:
        fputs("CASE (", stdout);
        pm(s->e1, 0);
        printf(") MOD %d OF\n", s->width);
        for (int a = s->arm; a >= 0; a = am[a].next) {
            ind(level);
            fputs(a == s->arm ? "  " : "| ", stdout);
            print_labels(a);
            fputs(":\n", stdout);
            pseq(am[a].body, level + 2);
        }
        if (s->els >= 0) {
            ind(level);
            fputs("ELSE\n", stdout);
            pseq(s->els, level + 1);
        }
        ind(level);
        fputs("END", stdout);
        return;
    case S_WRITE: {
        int first = 1;
        for (int k = s->item; k >= 0; k = it[k].next, first = 0) {
            if (!first) fputs("; ", stdout);
            if (it[k].kind == W_STR)
                wstr(strs[it[k].str]);
            else if (it[k].kind == W_BOOL)
                wbool(it[k].e);
            else
                wint(it[k].e, it[k].width < 0 ? 0 : it[k].width);
        }
        if (s->var) {
            fputs("; ", stdout);
            wln();
        }
        return;
    }
    default:
        fputs("pr(", stdout);
        pm(s->e1, 0);
        printf(", %s, ", v);
        if (s->e3 >= 0) {
            fputs(vname[s->e3], stdout);
        } else {
            fputs("v[", stdout);
            pm(s->e2, 0);
            putchar(']');
        }
        putchar(')');
        return;
    }
}

static void print_module(void)
{
    int ob = obp();
    fputs("MODULE gen;\n", stdout);
    fputs(ob ? "IMPORT Out;\n" : "FROM InOut IMPORT WriteInt, WriteString, WriteLn;\n", stdout);
    printf("CONST\n  k0 = %lld;\n  k1 = %lld;\n", (long long) k0, (long long) k1);
    fputs("VAR\n  g0, g1, g2, g3, g4, g5, i, j, c0, c1, c2: INTEGER;\n  b0, b1: BOOLEAN;\n",
          stdout);
    if (ob)
        printf("  v: ARRAY %d OF INTEGER;\n\n", vlen);
    else
        printf("  v: ARRAY [%d..%d] OF INTEGER;\n\n", vlo, vlo + vlen - 1);

    fputs("PROCEDURE f1(x, y: INTEGER): INTEGER;\nVAR t: INTEGER;\nBEGIN\n  t := ", stdout);
    pm(f1a, 0);
    fputs(";\n  RETURN ", stdout);
    pm(f1b, 0);
    fputs("\nEND f1;\n\n", stdout);

    fputs("PROCEDURE fr(n: INTEGER): INTEGER;\n", stdout);
    if (ob) { /* an Oberon function ends with its only RETURN */
        fputs("VAR r: INTEGER;\nBEGIN\n  IF n <= 0 THEN r := ", stdout);
        pm(fr0, 0);
        fputs(" ELSE r := ", stdout);
        pm(frstep, 0);
        fputs(" END;\n  RETURN r\n", stdout);
    } else if (rnd(2)) {
        fputs("BEGIN\n  IF n <= 0 THEN RETURN ", stdout);
        pm(fr0, 0);
        fputs(" END;\n  RETURN ", stdout);
        pm(frstep, 0);
        putchar('\n');
    } else {
        fputs("BEGIN\n  IF n <= 0 THEN RETURN ", stdout);
        pm(fr0, 0);
        fputs(" ELSE RETURN ", stdout);
        pm(frstep, 0);
        fputs(" END\n", stdout);
    }
    fputs("END fr;\n\n", stdout);

    fputs("PROCEDURE fb(x: INTEGER): BOOLEAN;\nBEGIN\n  RETURN ", stdout);
    pm(fbe, 0);
    fputs("\nEND fb;\n\n", stdout);

    fputs("PROCEDURE pr(x: INTEGER; VAR y, z: INTEGER);\nVAR t, i: INTEGER;\n", stdout);
    fputs(ob ? "  w: ARRAY 4 OF INTEGER;\n" : "  w: ARRAY [0..3] OF INTEGER;\n", stdout);
    /* Oberon: inner cannot see pr's t and x, so they are its parameters */
    fputs(ob ? "  PROCEDURE inner(VAR u, t: INTEGER; x: INTEGER);\n"
             : "  PROCEDURE inner(VAR u: INTEGER);\n",
          stdout);
    fputs("  BEGIN\n    u := ", stdout);
    pm(iu, 0);
    fputs(";\n    t := ", stdout);
    pm(it_, 0);
    fputs("\n  END inner;\nBEGIN\n  t := ", stdout);
    pm(pt, 0);
    fputs(";\n  FOR i := 0 TO 3 DO w[i] := ", stdout);
    pm(pw, 0);
    fputs(ob ? " END;\n  inner(y, t, x);\n  inner(z, t, x);\n  z := "
             : " END;\n  inner(y);\n  inner(z);\n  z := ",
          stdout);
    pm(pz, 0);
    fputs(";\n  ", stdout);
    wstr("pr");
    fputs("; ", stdout);
    for (int k = 0; k < 4; k++) {
        static const int pv[] = {PX, PY, PZ, PT};
        int id = enew(E_VAR, 0, -1, -1, pv[k]);
        wint(id, k ? 6 : 0);
        fputs("; ", stdout);
    }
    wln();
    fputs("\nEND pr;\n\nBEGIN\n", stdout);
    for (int k = 0; k < 6; k++) printf("  g%d := %lld;\n", k, (long long) init[k]);
    printf("  b0 := TRUE; b1 := FALSE;\n  FOR i := %d TO %d DO v[i] := i * 3 - 1 END;\n", vlo,
           vlo + vlen - 1);
    for (int b = body; b >= 0; b = sn[b].next) {
        psm(b, 1);
        fputs(";\n", stdout);
    }
    fputs("  ", stdout);
    for (int k = 0; k < 6; k++) {
        wint(enew(E_VAR, 0, -1, -1, G0 + k), 12);
        fputs("; ", stdout);
    }
    fputs("\n  ", stdout);
    for (int k = 0; k < 2; k++) {
        wbool(enew(E_BVAR, 0, -1, -1, B0 + k));
        fputs("; ", stdout);
    }
    wln();
    printf(";\n  FOR i := %d TO %d DO ", vlo, vlo + vlen - 1);
    fputs(ob ? "Out.Int(v[i], 5)" : "WriteInt(v[i], 5)", stdout);
    fputs(" END;\n  ", stdout);
    wln();
    fputs("\nEND gen.\n", stdout);
}

int main(int argc, char **argv)
{
    if (argc == 3) {
        as_m2 = !strcmp(argv[1], "oberon-m2");
        dia = !strcmp(argv[1], "delphi")                                    ? D_DELPHI
              : !strcmp(argv[1], "m2")                                      ? D_M2
              : !strcmp(argv[1], "oberon") || !strcmp(argv[1], "oberon-m2") ? D_OBERON
                                                                            : -1;
        argv++;
        argc--;
    }
    if (argc != 2 || dia < 0) {
        fprintf(stderr, "usage: gen_pascal [delphi|m2|oberon|oberon-m2] seed\n");
        return 2;
    }
    uint64_t seed = strtoull(argv[1], 0, 10);
    for (int attempt = 0;; attempt++) {
        rng = (seed + (uint64_t) attempt * 1000003u) * 0x9E3779B97F4A7C15ull + 11;
        generate();
        int r = 1;
        for (int fix = 0; fix < 5000 && r == 1; fix++) {
            r = static_repair() ? 1 : run_once();
        }
        if (r == 0) break;
        if (attempt > 100) {
            fprintf(stderr, "gen_pascal: no well-defined program for seed %llu\n",
                    (unsigned long long) seed);
            return 1;
        }
    }
    if (dia >= D_M2)
        print_module();
    else
        print_program();
    return 0;
}
