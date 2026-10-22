/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zl_prolog.c — the Prolog front-end: Horn clauses over integers, atoms,
 * compound terms and lists, as SWI-Prolog 9 runs them, with the twin
 * "Riddlewyrm".
 *
 * Resolution is compiled onto the IR (62-bit words) as a small machine
 * kept entirely in IR data, so the IR call stack never grows with the
 * program's recursion:
 *   - an atom is a string literal (ZL_V_STRC), [] is ZL_V_NIL, a list cell
 *     is an IR pair, a compound f(A1..An) a vector [f, A1, ..., An], and a
 *     variable a vector [stamp, value] whose value is itself while unbound
 *     (the stamp, a number, tells it from a compound, whose cell 0 is an
 *     atom);
 *   - every clause body runs in an environment vector [continuation code,
 *     continuation environment, cut barrier, variables..., temporaries...];
 *   - a choicepoint is a vector [previous, alternative code, trail mark,
 *     environment, continuation, cut barrier, variable stamp, arguments...];
 *     backtracking resets the trailed variables (only those older than the
 *     newest choicepoint are trailed) and jumps to the alternative;
 *   - calls and returns are jumps (JMPI through code values); the last call
 *     of a clause reuses its continuation, so loops written as recursion run
 *     in constant space. A predicate's clauses are tried in order, skipping
 *     those whose first argument cannot match.
 * Unification, arithmetic, term printing (SWI's writer: operators, spacing,
 * quoting) and copying run as IR helpers emitted only when used.
 *
 * Admitted: clauses and the directive ":- initialization(G)." (goals run
 * after loading, in order; a goal that fails ends quietly, as SWI goes on
 * after its warning); integers (62-bit), atoms (quoted too), compound terms,
 * lists, operators of SWI's default table; control: , ; -> \+ ! true fail
 * false not once ignore forall findall; = \= == \== is =:= =\= < > =< >=
 * with + - * / // mod rem div min max abs sign ^ ** /\ \/ xor \ >> <<;
 * var nonvar atom number integer atomic compound callable is_list;
 * write print writeq writeln nl tab format/1,2 (~w ~p ~q ~a ~d ~c ~i ~n ~~),
 * halt; and, unless the program defines them, append/3 member/2
 * memberchk/2 reverse/2 nth0/3 nth1/3 last/2 sum_list/2 sumlist/2
 * max_list/2 min_list/2 numlist/3 (written in Prolog after SWI's
 * library, with SWI's errors); length/2 and between/3 are built in.
 *
 * Exactness: an integer past 62 bits traps OVERFLOW (SWI has bignums); /
 * whose result is not an integer traps INEXACT (SWI makes a float, as ^ and
 * ** do for negative exponents); every SWI error (instantiation, type,
 * evaluation, existence of a matching rule) traps DOMAIN or DIVZERO, so
 * output stops exactly where SWI's stops; printing an unbound variable
 * traps DOMAIN (SWI prints a name like _123 that depends on its memory).
 * There is no occurs check, as ISO and SWI: X = f(X) succeeds and makes a
 * cyclic term. Cyclic terms are outside the subset: SWI treats them as
 * rational trees, here writing, comparing, copying or evaluating one never
 * ends and the run stops on a trap (fuel, stack or memory), never on a
 * different answer; a program that only binds them behaves as in SWI.
 *
 * Refused by name (ZL_E_UNSUPPORTED): floats, strings as values (a
 * "string" is admitted only as a format control), back quotes, character
 * escapes other than \n \t \\ \' \", assert/retract and every other
 * built-in outside the subset (SWI's whole system predicate table is
 * listed), directives other than initialization/1, op/3, call/N and
 * variables as goals, catch/throw, DCG rules, the soft cut *->, the bar
 * as an infix operator, defining a system predicate or a hook (portray/1
 * ...), format directives with column or numeric arguments, atoms and
 * functions other than the integer ones in arithmetic. A call to a
 * predicate the program does not define is refused (ZL_E_UNDEFINED).
 *
 * The twin: one clause per line or block, "Head :-" and each goal of the
 * top-level conjunction on its own line indented four spaces, ending in ","
 * or "."; terms in canonical form (", " between arguments and list
 * elements, infix operators between single spaces except the comma, "- X"
 * for a prefix operator, parentheses only where priorities need them, atoms
 * quoted only when they must be, 0'c written as its code); a "%" comment
 * kept only between clauses, as "% text"; a blank line before each group
 * (a directive, a comment run, or the first clause of another predicate). */
#include "zl_int.h"

#define P_NODES   5000
#define P_ATOMS   600
#define P_NAMES   12000
#define P_CLAUSES 1500
#define P_PREDS   400
#define P_ITEMS   1600
#define P_VARS    250
#define P_LABELS  8000
#define P_NEST    200 /* reader nesting */
#define P_SDEPTH  200 /* IR data stack while building a term */
#define P_TEMPS   24
#define P_ARGM    2000 /* parse() maxp + P_ARGM: inside an argument, where a comma ends the term */
#define P_ARG     (1200 + P_ARGM)

enum { K_INT = 1, K_ATOM, K_VAR, K_STR, K_COMP, K_CONS, K_COMMENT };
enum { I_CLAUSE = 1, I_INIT, I_COMMENT };

typedef struct {
    int64_t v;         /* INT: value; ATOM, COMP: atom; VAR: (index+1) | length << 32 (index -1: _);
                          STR, COMMENT: length */
    int32_t kid, next; /* COMP, CONS: first child; VAR, STR, COMMENT: source offset */
    uint32_t line;
    uint16_t col;
    uint8_t kind, flags;
} pnode_t;

/* SWI's atomType: how an atom is written by writeq/print */
enum { AT_LOWER = 1, AT_SYMBOL, AT_SOLO, AT_SPECIAL, AT_QUOTE };

typedef struct {
    uint32_t off, len;  /* the name in names[] */
    int32_t str;        /* string index once code uses it, else -1 */
    uint16_t pre, in;   /* prefix / infix operator priority, 0 if none */
    uint8_t pret, int_; /* prefix: 0 fx 1 fy; infix: 0 xfx 1 xfy 2 yfx */
    uint8_t type;
    uint8_t pad;
} patom_t;

typedef struct {
    int32_t head, body; /* nodes; body -1 for a fact */
    int32_t pred, next; /* its predicate (-1: an initialization goal), next clause of it */
    int32_t lab;        /* an initialization goal's code */
    uint16_t nvars;
    uint8_t prelude;
    uint8_t pad;
} pclause_t;

typedef struct {
    int32_t atom, first, last, label;
    uint16_t arity, nclauses;
    uint8_t user, prelude;
    uint8_t pad[2];
} ppred_t;

typedef struct {
    int32_t ref;
    uint8_t kind;
    uint8_t pad[3];
} pitem_t;

typedef struct {
    int kind; /* T_* */
    size_t start, end;
    uint32_t line, col;
    int64_t ival;
    int32_t atom;
    int fn;     /* a name immediately followed by "(" */
    int layout; /* layout before the token */
    char ch;    /* T_PUNCT */
} ptok_t;

enum { T_EOF = 0, T_NAME, T_VAR, T_INT, T_STR, T_PUNCT, T_END };

/* helpers emitted on demand */
enum {
    H_DEREF,
    H_TCLASS,
    H_NEWVAR,
    H_BIND,
    H_UNIFY,
    H_IDENT,
    H_UC,
    H_ALLOC,
    H_PUSHCP,
    H_FAIL,
    H_KEYOF,
    H_EVAL,
    H_EV,
    H_WRITE,
    H_WT,
    H_WATOM,
    H_WNUM,
    H_PUTC,
    H_OPENTOK,
    H_CLS,
    H_COPY,
    H_CP,
    H_REVERSE,
    H_ISLIST,
    H_SKIP,
    H_TAB,
    H_FMTA,
    H_FMTD,
    H_FMTC,
    H_PROCEED,
    H_PROCF,
    H_CUTE,
    H_SETKEY,
    H_TRYNEXT,
    H_POPCP,
    H_KEYIS,
    H_SETC,
    H_SETT,
    H_BETWEEN,
    H_LENGTH,
    H_MKLIST,
    H_NEWVARS,
    H_SAVEB,
    H_CUTTO,
    H_ATOMINFO,
    H__COUNT,
};

typedef struct {
    /* zeroed for every compile */
    zl_cc_t *c;
    zl_out_t *tw;
    const char *src; /* the text being read (the program or a prelude snippet) */
    size_t len, pos, lstart;
    uint32_t line;
    int prelude;      /* reading a prelude snippet: '$' names allowed, lines are 0 */
    int at_top;       /* between clauses: comments are kept */
    const char *usrc; /* the program */
    size_t ulen;
    ptok_t tok, peek;
    int has_peek;
    uint32_t nnodes, natoms, nnames, nclauses, npreds, nitems, nvars, nlabels;
    uint32_t depth;
    int32_t hlab[H__COUNT]; /* label + 1 */
    uint8_t hdone[H__COUNT];
    /* slots */
    int32_t sA, nA, sE, sCPC, sCPE, sB, sB0, sTR, sVC, sFAILK, sLASTC, sQ, sKEY, sMAP, sT, sCT;
    /* the clause being compiled */
    uint32_t ntemp, cvars;
    int noenv, cl_prelude;
    int32_t a_nil, a_comma, a_semi, a_arrow, a_neck, a_not, a_cut, a_curly, a_minus, a_bar, a_dcg,
        a_init;
    /* tables: always written before they are read */
    struct {
        uint32_t off, len;
    } vars[P_VARS];
    int32_t dir[P_VARS];   /* the head argument a variable arrives in, or -1 */
    int16_t vslot[P_VARS]; /* its environment cell */
    pnode_t nodes[P_NODES];
    patom_t atoms[P_ATOMS];
    char names[P_NAMES];
    pclause_t clauses[P_CLAUSES];
    ppred_t preds[P_PREDS];
    pitem_t items[P_ITEMS];
    int32_t labels[P_LABELS];
    char sbuf[1024]; /* a decoded string or a quoted spelling */
} pw_t;

ZL_STATIC_ASSERT(sizeof(pw_t) <= sizeof(((zl_work_t *) 0)->fe), pw_fits);

#define TRY(x)                                                                                     \
    do {                                                                                           \
        int s_ = (x);                                                                              \
        if (s_) return s_;                                                                         \
    } while (0)

/* ------------------------------------------------------------ tables */

/* SWI-Prolog 9's default operators (current_op/3). */
static const struct {
    const char *name;
    uint16_t pri;
    uint8_t kind; /* 0 prefix, 1 infix */
    uint8_t type; /* prefix: 0 fx 1 fy; infix: 0 xfx 1 xfy 2 yfx */
} ops[] = {
    {"$", 1, 0, 0},
    {",", 1000, 1, 1},
    {".", 100, 1, 2},
    {"|", 1105, 1, 1},
    {"*", 400, 1, 2},
    {"**", 200, 1, 0},
    {"*->", 1050, 1, 1},
    {"+", 200, 0, 1},
    {"+", 500, 1, 2},
    {"-", 200, 0, 1},
    {"-", 500, 1, 2},
    {"-->", 1200, 1, 0},
    {"->", 1050, 1, 1},
    {"/", 400, 1, 2},
    {"//", 400, 1, 2},
    {"/\\", 500, 1, 2},
    {":", 600, 1, 1},
    {":-", 1200, 0, 0},
    {":-", 1200, 1, 0},
    {":<", 700, 1, 0},
    {":=", 800, 1, 0},
    {";", 1100, 1, 1},
    {"<", 700, 1, 0},
    {"<<", 400, 1, 2},
    {"=", 700, 1, 0},
    {"=..", 700, 1, 0},
    {"=:=", 700, 1, 0},
    {"=<", 700, 1, 0},
    {"==", 700, 1, 0},
    {"=>", 1200, 1, 0},
    {"=@=", 700, 1, 0},
    {"=\\=", 700, 1, 0},
    {">", 700, 1, 0},
    {">:<", 700, 1, 0},
    {">=", 700, 1, 0},
    {">>", 400, 1, 2},
    {"?-", 1200, 0, 0},
    {"@<", 700, 1, 0},
    {"@=<", 700, 1, 0},
    {"@>", 700, 1, 0},
    {"@>=", 700, 1, 0},
    {"\\", 200, 0, 1},
    {"\\+", 900, 0, 1},
    {"\\/", 500, 1, 2},
    {"\\=", 700, 1, 0},
    {"\\==", 700, 1, 0},
    {"\\=@=", 700, 1, 0},
    {"^", 200, 1, 1},
    {"as", 700, 1, 0},
    {"discontiguous", 1150, 0, 0},
    {"div", 400, 1, 2},
    {"dynamic", 1150, 0, 0},
    {"initialization", 1150, 0, 0},
    {"is", 700, 1, 0},
    {"meta_predicate", 1150, 0, 0},
    {"mod", 400, 1, 2},
    {"module_transparent", 1150, 0, 0},
    {"multifile", 1150, 0, 0},
    {"public", 1150, 0, 0},
    {"rdiv", 400, 1, 2},
    {"rem", 400, 1, 2},
    {"table", 1150, 0, 0},
    {"thread_initialization", 1150, 0, 0},
    {"thread_local", 1150, 0, 0},
    {"volatile", 1150, 0, 0},
    {"xor", 400, 1, 2},
};

/* Goals compiled in line. */
enum {
    B_NONE = 0,
    B_CONJ,
    B_DISJ,
    B_ITE,
    B_NOT,
    B_NOTW, /* not/1 */
    B_CUT,
    B_TRUE,
    B_FAIL,
    B_FALSE,
    B_ONCE,
    B_IGNORE,
    B_FORALL,
    B_FINDALL,
    B_UNIFY,
    B_NUNIFY,
    B_IDENT,
    B_NIDENT,
    B_IS,
    B_AEQ,
    B_ANE,
    B_ALT,
    B_AGT,
    B_ALE,
    B_AGE,
    B_VAR,
    B_NONVAR,
    B_ATOM,
    B_NUMBER,
    B_INTEGER,
    B_ATOMIC,
    B_COMPOUND,
    B_CALLABLE,
    B_ISLIST,
    B_WRITE,
    B_PRINT,
    B_WRITEQ,
    B_WRITELN,
    B_NL,
    B_TAB,
    B_FORMAT1,
    B_FORMAT2,
    B_HALT,
    B_BETWEEN,
    B_LENGTH,
    B_ERROR, /* '$error': prelude only */
};

static const struct {
    const char *name;
    uint8_t arity, id, internal;
} bis[] = {
    {",", 2, B_CONJ, 0},
    {";", 2, B_DISJ, 0},
    {"->", 2, B_ITE, 0},
    {"\\+", 1, B_NOT, 0},
    {"not", 1, B_NOTW, 0},
    {"!", 0, B_CUT, 0},
    {"true", 0, B_TRUE, 0},
    {"fail", 0, B_FAIL, 0},
    {"false", 0, B_FALSE, 0},
    {"once", 1, B_ONCE, 0},
    {"ignore", 1, B_IGNORE, 0},
    {"forall", 2, B_FORALL, 0},
    {"findall", 3, B_FINDALL, 0},
    {"=", 2, B_UNIFY, 0},
    {"\\=", 2, B_NUNIFY, 0},
    {"==", 2, B_IDENT, 0},
    {"\\==", 2, B_NIDENT, 0},
    {"is", 2, B_IS, 0},
    {"=:=", 2, B_AEQ, 0},
    {"=\\=", 2, B_ANE, 0},
    {"<", 2, B_ALT, 0},
    {">", 2, B_AGT, 0},
    {"=<", 2, B_ALE, 0},
    {">=", 2, B_AGE, 0},
    {"var", 1, B_VAR, 0},
    {"nonvar", 1, B_NONVAR, 0},
    {"atom", 1, B_ATOM, 0},
    {"number", 1, B_NUMBER, 0},
    {"integer", 1, B_INTEGER, 0},
    {"atomic", 1, B_ATOMIC, 0},
    {"compound", 1, B_COMPOUND, 0},
    {"callable", 1, B_CALLABLE, 0},
    {"is_list", 1, B_ISLIST, 0},
    {"write", 1, B_WRITE, 0},
    {"print", 1, B_PRINT, 0},
    {"writeq", 1, B_WRITEQ, 0},
    {"writeln", 1, B_WRITELN, 0},
    {"nl", 0, B_NL, 0},
    {"tab", 1, B_TAB, 0},
    {"format", 1, B_FORMAT1, 0},
    {"format", 2, B_FORMAT2, 0},
    {"halt", 0, B_HALT, 0},
    {"between", 3, B_BETWEEN, 0},
    {"length", 2, B_LENGTH, 0},
    {"$error", 0, B_ERROR, 1},
};

/* Library predicates written in Prolog, after SWI's library(lists),
 * loaded when a program calls one it does not define. Errors call
 * '$error', which traps. */
static const struct {
    const char *name;
    uint8_t arity;
    const char *text;
} prelude[] = {
    {"append", 3, "append([], L, L).\nappend([H|T], L, [H|R]) :- append(T, L, R).\n"},
    {"member", 2, "member(X, [H|T]) :- '$member_'(T, X, H).\n"},
    {"$member_", 3, "'$member_'(_, X, X).\n'$member_'([H|T], X, _) :- '$member_'(T, X, H).\n"},
    {"memberchk", 2, "memberchk(X, [H|T]) :- '$member_'(T, X, H), !.\n"},
    {"reverse", 2,
     "reverse(Xs, Ys) :- '$reverse'(Xs, Ys, [], Ys).\n'$reverse'([], [], Ys, Ys).\n"
     "'$reverse'([X|Xs], [_|Bound], Rs, Ys) :- '$reverse'(Xs, Bound, [X|Rs], Ys).\n"},
    {"nth0", 3,
     "nth0(I, L, E) :- integer(I), !, '$nth0_det'(I, L, E).\n"
     "nth0(I, L, E) :- var(I), !, L = [H|T], '$nth_gen'(T, E, H, 0, I).\n"
     "nth0(_, _, _) :- '$error'.\n"},
    {"nth1", 3,
     "nth1(I, L, E) :- integer(I), !, I0 is I - 1, '$nth0_det'(I0, L, E).\n"
     "nth1(I, L, E) :- var(I), !, L = [H|T], '$nth_gen'(T, E, H, 1, I).\n"
     "nth1(_, _, _) :- '$error'.\n"},
    {"$nth0_det", 3,
     "'$nth0_det'(0, [E|_], E) :- !.\n"
     "'$nth0_det'(N, [_|T], E) :- M is N - 1, M >= 0, '$nth0_det'(M, T, E).\n"},
    {"$nth_gen", 5,
     "'$nth_gen'(_, E, E, B, B).\n"
     "'$nth_gen'([H|T], E, _, N, B) :- M is N + 1, '$nth_gen'(T, E, H, M, B).\n"},
    {"last", 2,
     "last([X|Xs], L) :- '$last_'(Xs, X, L).\n'$last_'([], L, L).\n"
     "'$last_'([X|Xs], _, L) :- '$last_'(Xs, X, L).\n"},
    {"sum_list", 2,
     "sum_list(Xs, S) :- '$sum_list'(Xs, 0, S).\n'$sum_list'(L, _, _) :- var(L), !, '$error'.\n"
     "'$sum_list'([], S0, S) :- !, S = S0.\n"
     "'$sum_list'([X|Xs], S0, S) :- !, S1 is S0 + X, '$sum_list'(Xs, S1, S).\n"
     "'$sum_list'(_, _, _) :- '$error'.\n"},
    {"sumlist", 2, "sumlist(Xs, S) :- sum_list(Xs, S).\n"},
    {"max_list", 2,
     "max_list(L, _) :- var(L), !, '$error'.\nmax_list([H|T], M) :- !, '$max_list'(T, H, M).\n"
     "max_list([], _) :- !, fail.\nmax_list(_, _) :- '$error'.\n"
     "'$max_list'(L, _, _) :- var(L), !, '$error'.\n'$max_list'([], M0, M) :- !, M = M0.\n"
     "'$max_list'([H|T], M0, M) :- !, M1 is max(H, M0), '$max_list'(T, M1, M).\n"
     "'$max_list'(_, _, _) :- '$error'.\n"},
    {"min_list", 2,
     "min_list(L, _) :- var(L), !, '$error'.\nmin_list([H|T], M) :- !, '$min_list'(T, H, M).\n"
     "min_list([], _) :- !, fail.\nmin_list(_, _) :- '$error'.\n"
     "'$min_list'(L, _, _) :- var(L), !, '$error'.\n'$min_list'([], M0, M) :- !, M = M0.\n"
     "'$min_list'([H|T], M0, M) :- !, M1 is min(H, M0), '$min_list'(T, M1, M).\n"
     "'$min_list'(_, _, _) :- '$error'.\n"},
    {"numlist", 3,
     "numlist(L, U, Ns) :- '$must_int'(L), '$must_int'(U), L =< U, '$numlist_'(L, U, Ns).\n"
     "'$numlist_'(U, U, List) :- !, List = [U].\n"
     "'$numlist_'(L, U, [L|Ns]) :- L2 is L + 1, '$numlist_'(L2, U, Ns).\n"},
    {"$must_int", 1, "'$must_int'(X) :- integer(X), !.\n'$must_int'(_) :- '$error'.\n"},
};

/* Hooks and directives a program may not define: they change what the
 * built-ins do. */
static const struct {
    const char *name;
    uint8_t arity;
} hooks[] = {
    {"portray", 1},
    {"term_expansion", 2},
    {"term_expansion", 4},
    {"goal_expansion", 2},
    {"goal_expansion", 4},
    {"message_hook", 3},
    {"exception", 3},
    {"prolog_load_file", 2},
    {"message_property", 2},
    {"prolog_exception_hook", 4},
    {"prolog_trace_interception", 4},
};

/* SWI-Prolog 9.0.4's system predicates (module system, '$' names left
 * out): defining one is an error there, calling one outside the subset is
 * refused here by name. */
static const char sys_preds[] =
    "!/0 *->/2 ,/2 ->/2 ./3 :</2 ;/2 </2 <meta-call>/1 =/2 =../2 =:=/2 =</2 ==/2 =@=/2 "
    "=\\=/2 >/2 >:</2 >=/2 ?=/2 @/2 @</2 @=</2 @>/2 @>=/2 [|]/2 \\+/1 \\=/2 \\==/2 \\=@=/2 "
    "abolish/1 abolish/2 abolish_all_tables/0 abolish_module_tables/1 "
    "abolish_monotonic_tables/0 abolish_nonincremental_tables/0 "
    "abolish_nonincremental_tables/1 abolish_private_tables/0 abolish_shared_tables/0 "
    "abolish_table_subgoals/1 abort/0 absolute_file_name/2 absolute_file_name/3 "
    "access_file/2 acyclic_term/1 add_import_module/3 answer_count_restraint/0 append/1 "
    "apply/2 arg/3 assert/1 assert/2 asserta/1 asserta/2 assertz/1 assertz/2 "
    "at_end_of_stream/0 at_end_of_stream/1 at_halt/1 atom/1 atom_chars/2 atom_codes/2 "
    "atom_concat/3 atom_length/2 atom_number/2 atom_prefix/2 atom_string/2 atom_to_term/3 "
    "atomic/1 atomic_concat/3 atomic_list_concat/2 atomic_list_concat/3 atomics_to_string/2 "
    "atomics_to_string/3 attach_packs/0 attach_packs/1 attach_packs/2 attvar/1 autoload/1 "
    "autoload/2 autoload_path/1 b_getval/2 b_set_dict/3 b_setval/2 bagof/3 between/3 blob/2 "
    "bounded_number/3 break/0 byte_count/2 call/1 call/2 call/3 call/4 call/5 call/6 call/7 "
    "call/8 call_cleanup/2 call_cleanup/3 call_continuation/1 call_dcg/3 call_residue_vars/2 "
    "call_shared_object_function/2 call_with_depth_limit/3 call_with_inference_limit/3 "
    "callable/1 cancel_halt/1 catch/3 catch_with_backtrace/3 char_code/2 char_conversion/2 "
    "char_type/2 character_count/2 clause/2 clause/3 clause_property/2 close/1 close/2 "
    "close_shared_object/1 code_type/2 collation_key/2 compare/3 compile_aux_clauses/1 "
    "compile_predicates/1 compiling/0 compound/1 compound_name_arguments/3 "
    "compound_name_arity/3 consult/1 context_module/1 copy_predicate_clauses/2 "
    "copy_stream_data/2 copy_stream_data/3 copy_term/2 copy_term/3 copy_term/4 "
    "copy_term_nat/2 copy_term_nat/4 create_prolog_flag/3 current_arithmetic_function/1 "
    "current_atom/1 current_blob/2 current_char_conversion/2 current_engine/1 current_flag/1 "
    "current_format_predicate/2 current_functor/2 current_input/1 current_key/1 "
    "current_locale/1 current_module/1 current_op/3 current_output/1 current_predicate/1 "
    "current_predicate/2 current_prolog_flag/2 current_resource/2 current_signal/3 "
    "current_table/2 current_transaction/1 current_trie/1 cyclic_term/1 date_time_stamp/2 "
    "dcg_translate_rule/2 dcg_translate_rule/4 default_module/2 del_attr/2 del_attrs/1 "
    "del_dict/4 delete_directory/1 delete_file/1 delete_import_module/2 det/1 "
    "deterministic/1 dict_create/3 dict_pairs/3 directory_files/2 discontiguous/1 divmod/4 "
    "downcase_atom/2 duplicate_term/2 dwim_match/2 dwim_match/3 dwim_predicate/2 dynamic/1 "
    "dynamic/2 engine_create/3 engine_create/4 engine_destroy/1 engine_fetch/1 engine_next/2 "
    "engine_next_reified/2 engine_post/2 engine_post/3 engine_self/1 engine_yield/1 "
    "ensure_loaded/1 erase/1 exists_directory/1 exists_file/1 exists_source/1 "
    "exists_source/2 expand_file_name/2 expand_file_search_path/2 expand_goal/2 "
    "expand_goal/4 expand_term/2 expand_term/4 export/1 fail/0 false/0 fast_read/2 "
    "fast_term_serialized/2 fast_write/2 file_base_name/2 file_directory_name/2 "
    "file_name_extension/3 fill_buffer/1 findall/3 findall/4 findnsols/4 findnsols/5 flag/3 "
    "float/1 float_class/2 float_parts/4 flush_output/0 flush_output/1 forall/2 format/1 "
    "format/2 format/3 format_predicate/2 format_time/3 format_time/4 freeze/2 frozen/2 "
    "functor/3 functor/4 garbage_collect/0 garbage_collect_atoms/0 garbage_collect_clauses/0 "
    "gc_file_search_cache/1 get/1 get/2 get0/1 get0/2 get_attr/3 get_attrs/2 get_byte/1 "
    "get_byte/2 get_char/1 get_char/2 get_code/1 get_code/2 get_dict/3 get_dict/5 get_flag/2 "
    "get_single_char/1 get_string_code/3 get_time/1 getenv/2 goal_expansion/2 "
    "goal_expansion/4 ground/1 halt/0 halt/1 ignore/1 import/1 import_module/2 "
    "initialization/1 initialization/2 initialize/0 instance/2 integer/1 is/2 "
    "is_absolute_file_name/1 is_dict/1 is_dict/2 is_engine/1 is_list/1 "
    "is_most_general_term/1 is_stream/1 is_thread/1 is_trie/1 keysort/2 known_licenses/0 "
    "leash/1 length/2 license/0 license/1 license/2 line_count/2 line_position/2 "
    "load_files/1 load_files/2 locale_create/3 locale_destroy/1 locale_property/2 "
    "make_directory/1 make_library_index/1 make_library_index/2 memberchk/2 "
    "message_queue_create/1 message_queue_create/2 message_queue_destroy/1 "
    "message_queue_property/2 message_queue_set/2 message_to_string/2 meta_predicate/1 "
    "module/1 module_property/2 module_transparent/1 msort/2 multifile/1 mutex_create/1 "
    "mutex_create/2 mutex_destroy/1 mutex_lock/1 mutex_property/2 mutex_statistics/0 "
    "mutex_trylock/1 mutex_unlock/1 mutex_unlock_all/0 name/2 nb_current/2 nb_delete/1 "
    "nb_getval/2 nb_link_dict/3 nb_linkarg/3 nb_linkval/2 nb_set_dict/3 nb_setarg/3 "
    "nb_setval/2 nl/0 nl/1 non_terminal/1 nonground/2 nonvar/1 noprofile/1 noprotocol/0 "
    "normalize_space/2 not/1 not_exists/1 notrace/0 notrace/1 nth_clause/3 "
    "nth_integer_root_and_remainder/4 number/1 number_chars/2 number_codes/2 number_string/2 "
    "numbervars/3 numbervars/4 on_signal/3 once/1 op/3 open/3 open/4 open_null_stream/1 "
    "open_resource/2 open_resource/3 open_shared_object/2 open_shared_object/3 open_string/2 "
    "open_xterm/5 peek_byte/1 peek_byte/2 peek_char/1 peek_char/2 peek_code/1 peek_code/2 "
    "peek_string/3 phrase/2 phrase/3 plus/3 predicate_option_mode/2 predicate_option_type/2 "
    "predicate_property/2 print/1 print/2 print_message/2 print_message_lines/3 "
    "print_toplevel_variables/0 profiler/2 prolog/0 prolog_alert_signal/2 "
    "prolog_choice_attribute/3 prolog_current_choice/1 prolog_current_frame/1 "
    "prolog_cut_to/1 prolog_debug/1 prolog_frame_attribute/3 prolog_interrupt/0 "
    "prolog_listen/2 prolog_listen/3 prolog_load_context/2 prolog_nodebug/1 "
    "prolog_skip_frame/1 prolog_skip_level/2 prolog_stack_property/2 prolog_to_os_filename/2 "
    "prolog_unlisten/2 prompt/2 prompt1/1 protocol/1 protocola/1 protocolling/1 public/1 "
    "put/1 put/2 put_attr/3 put_attrs/2 put_byte/1 put_byte/2 put_char/1 put_char/2 "
    "put_code/1 put_code/2 put_dict/3 put_dict/4 qcompile/1 qcompile/2 radial_restraint/0 "
    "random_property/1 rational/1 rational/3 read/1 read/2 read_clause/3 read_link/3 "
    "read_pending_chars/3 read_pending_codes/3 read_string/3 read_string/5 read_term/2 "
    "read_term/3 read_term_from_atom/3 read_term_with_history/2 recorda/2 recorda/3 "
    "recorded/2 recorded/3 recordz/2 recordz/3 redefine_system_predicate/1 reexport/1 "
    "reexport/2 register_iri_scheme/3 reload_library_index/0 rename_file/2 repeat/0 "
    "require/1 reset/3 reset_profiler/0 residual_goals/1 retract/1 retractall/1 rule/2 "
    "rule/3 same_file/2 same_term/2 see/1 seeing/1 seek/4 seen/0 select_dict/3 "
    "set_end_of_stream/1 set_flag/2 set_input/1 set_locale/1 set_module/1 set_output/1 "
    "set_prolog_IO/3 set_prolog_flag/2 set_prolog_gc_thread/1 set_prolog_stack/2 "
    "set_random/1 set_stream/2 set_stream_position/2 set_system_IO/3 setarg/3 setenv/2 "
    "setlocale/3 setof/3 setup_call_catcher_cleanup/4 setup_call_cleanup/3 shell/1 shell/2 "
    "shift/1 shift_for_copy/1 sig_atomic/1 sig_block/1 sig_pending/1 sig_remove/2 "
    "sig_unblock/1 size_abstract_term/3 size_file/2 skip/1 skip/2 sleep/1 snapshot/1 sort/2 "
    "sort/4 source_file/1 source_file/2 source_file_property/2 source_location/2 "
    "split_string/4 stamp_date_time/3 start_abstract_tabling/3 start_moded_tabling/5 "
    "start_subsumptive_tabling/3 start_tabling/3 statistics/2 stream_pair/3 "
    "stream_position_data/3 stream_property/2 string/1 string_bytes/3 string_chars/2 "
    "string_code/3 string_codes/2 string_concat/3 string_length/2 string_lower/2 "
    "string_upper/2 strip_module/3 style_check/1 sub_atom/5 sub_atom_icasechk/3 sub_string/5 "
    "subsumes_term/2 succ/2 tab/1 tab/2 table/1 tabled_call/1 tell/1 telling/1 "
    "term_attvars/2 term_expansion/2 term_expansion/4 term_hash/2 term_hash/4 "
    "term_singletons/2 term_string/2 term_string/3 term_to_atom/2 term_variables/2 "
    "term_variables/3 text_to_string/2 thread_affinity/3 thread_alias/1 thread_create/2 "
    "thread_create/3 thread_detach/1 thread_exit/1 thread_get_message/1 thread_get_message/2 "
    "thread_get_message/3 thread_idle/2 thread_initialization/1 thread_join/1 thread_join/2 "
    "thread_local/1 thread_peek_message/1 thread_peek_message/2 thread_property/2 "
    "thread_self/1 thread_send_message/2 thread_send_message/3 thread_setconcurrency/2 "
    "thread_signal/2 thread_statistics/3 thread_update/2 thread_wait/2 throw/1 time_file/2 "
    "tmp_file/2 tmp_file_stream/3 tnot/1 told/0 trace/0 tracing/0 transaction/1 "
    "transaction/2 transaction/3 transaction_updates/1 trie_delete/3 trie_destroy/1 "
    "trie_gen/2 trie_gen/3 trie_gen_compiled/2 trie_gen_compiled/3 trie_insert/2 "
    "trie_insert/3 trie_insert/4 trie_lookup/3 trie_new/1 trie_property/2 trie_term/2 "
    "trie_update/3 trim_heap/0 trim_stacks/0 true/0 tty_get_capability/3 tty_goto/2 "
    "tty_put/2 tty_size/2 ttyflush/0 undefined/0 undo/1 unifiable/3 "
    "unify_with_occurs_check/2 unload_file/1 unsetenv/1 untable/1 unwrap_predicate/2 "
    "upcase_atom/2 use_foreign_library/1 use_foreign_library/2 use_module/1 use_module/2 "
    "var/1 var_number/2 var_property/2 variant_hash/2 variant_sha1/2 verbose_expansion/1 "
    "version/0 version/1 visible/1 volatile/1 wait_for_input/3 wildcard_match/2 "
    "wildcard_match/3 with_mutex/2 with_output_to/2 with_tty_raw/1 working_directory/2 "
    "write/1 write/2 write_canonical/1 write_canonical/2 write_length/3 write_term/2 "
    "write_term/3 writeln/1 writeln/2 writeq/1 writeq/2 zip_clone/2 zip_close_/2 "
    "zip_file_info_/3 zip_lock/1 zip_open_stream/3 zip_unlock/1 zipper_goto/2 "
    "zipper_open_current/3 zipper_open_new_file_in_zip/4 ";

/* ------------------------------------------------------------ basics */

static pnode_t *N(pw_t *w, int32_t n)
{
    return &w->nodes[n];
}

static int ok(pw_t *w)
{
    return w->c->failed ? w->c->d->status : ZL_OK;
}

static int nfail(pw_t *w, int32_t n, int st, const char *msg)
{
    if (n >= 0) {
        w->c->line = N(w, n)->line;
        w->c->col = N(w, n)->col;
    }
    return zl_fail(w->c, st, msg);
}

static int tfail(pw_t *w, const ptok_t *t, int st, const char *msg)
{
    w->c->line = t->line;
    w->c->col = t->col;
    return zl_fail(w->c, st, msg);
}

static int pfail(pw_t *w, int st, const char *msg)
{
    w->c->line = w->line;
    w->c->col = (uint32_t) (w->pos - w->lstart) + 1;
    return zl_fail(w->c, st, msg);
}

/* SWI's character classes (pl-ctype.c), ASCII only. */
enum { C_CT = 0, C_SP, C_SO, C_SY, C_PU, C_DQ, C_SQ, C_BQ, C_UC, C_LC, C_DI };

static int cls(int c)
{
    if (c >= '0' && c <= '9') return C_DI;
    if (c >= 'a' && c <= 'z') return C_LC;
    if ((c >= 'A' && c <= 'Z') || c == '_') return C_UC;
    switch (c) {
    case ' ':
    case '\t':
    case '\n':
    case '\r':
    case '\v':
    case '\f':
        return C_SP;
    case '#':
    case '$':
    case '&':
    case '*':
    case '+':
    case '-':
    case '.':
    case '/':
    case ':':
    case '<':
    case '=':
    case '>':
    case '?':
    case '@':
    case '\\':
    case '^':
    case '~':
        return C_SY;
    case '(':
    case ')':
    case '[':
    case ']':
    case '{':
    case '}':
    case ',':
    case '|':
        return C_PU;
    case '!':
    case '%':
    case ';':
        return C_SO;
    case '"':
        return C_DQ;
    case '\'':
        return C_SQ;
    case '`':
        return C_BQ;
    default:
        return C_CT;
    }
}

static int same(const char *a, size_t n, const char *s)
{
    size_t i = 0;
    for (; i < n; i++)
        if (!s[i] || s[i] != a[i]) return 0;
    return s[i] == 0;
}

/* ------------------------------------------------------------ atoms */

static int atom_type(const char *s, size_t n)
{
    if (n == 0) return AT_QUOTE;
    if (cls((unsigned char) s[0]) == C_LC) {
        for (size_t i = 1; i < n; i++)
            if (cls((unsigned char) s[i]) < C_UC) return AT_QUOTE;
        return AT_LOWER;
    }
    if (cls((unsigned char) s[0]) == C_SY) {
        if (n == 1 && s[0] == '.') return AT_QUOTE;
        if (n >= 2 && s[0] == '/' && s[1] == '*') return AT_QUOTE;
        for (size_t i = 0; i < n; i++)
            if (cls((unsigned char) s[i]) != C_SY) return AT_QUOTE;
        return AT_SYMBOL;
    }
    if (n == 1 && s[0] != '%' && cls((unsigned char) s[0]) == C_SO) return AT_SOLO;
    if (same(s, n, "[]") || same(s, n, "{}")) return AT_SPECIAL;
    return AT_QUOTE;
}

static int32_t atom_n(pw_t *w, const char *s, size_t n)
{
    for (uint32_t i = 0; i < w->natoms; i++) {
        patom_t *a = &w->atoms[i];
        if (a->len != n) continue;
        size_t k = 0;
        while (k < n && w->names[a->off + k] == s[k]) k++;
        if (k == n) return (int32_t) i;
    }
    if (w->natoms >= P_ATOMS || n > P_NAMES - w->nnames) {
        zl_fail(w->c, ZL_E_LIMIT, "too many atoms for the Prolog reader");
        return -1;
    }
    patom_t *a = &w->atoms[w->natoms];
    zl_bzero(a, sizeof *a);
    a->off = w->nnames;
    a->len = (uint32_t) n;
    a->str = -1;
    zl_bcopy(w->names + w->nnames, s, n);
    w->nnames += (uint32_t) n;
    a->type = (uint8_t) atom_type(s, n);
    for (size_t k = 0; k < sizeof ops / sizeof ops[0]; k++) {
        if (!same(s, n, ops[k].name)) continue;
        if (ops[k].kind == 0) {
            a->pre = ops[k].pri;
            a->pret = ops[k].type;
        } else {
            a->in = ops[k].pri;
            a->int_ = ops[k].type;
        }
    }
    return (int32_t) w->natoms++;
}

static int32_t atom_s(pw_t *w, const char *s)
{
    size_t n = 0;
    while (s[n]) n++;
    return atom_n(w, s, n);
}

static const char *aname(pw_t *w, int32_t a)
{
    return w->names + w->atoms[a].off;
}

/* ------------------------------------------------------------ tokens */

static void adv(pw_t *w)
{
    if (w->src[w->pos] == '\n' && !w->prelude) {
        w->line++;
        w->lstart = w->pos + 1;
    }
    w->pos++;
}

static int at(pw_t *w, size_t k)
{
    return w->pos + k < w->len ? (unsigned char) w->src[w->pos + k] : -1;
}

static int32_t newnode(pw_t *w, int kind, uint32_t line, uint32_t col)
{
    if (w->nnodes >= P_NODES) {
        zl_fail(w->c, ZL_E_LIMIT, "program too large for the Prolog reader");
        return -1;
    }
    pnode_t *n = &w->nodes[w->nnodes];
    zl_bzero(n, sizeof *n);
    n->kind = (uint8_t) kind;
    n->kid = n->next = -1;
    n->line = line;
    n->col = (uint16_t) (col > 65535 ? 65535 : col);
    return (int32_t) w->nnodes++;
}

static int add_item(pw_t *w, int kind, int32_t ref)
{
    if (w->nitems >= P_ITEMS) return pfail(w, ZL_E_LIMIT, "too many clauses");
    w->items[w->nitems].kind = (uint8_t) kind;
    w->items[w->nitems++].ref = ref;
    return ZL_OK;
}

/* Skip layout and comments; between clauses a % comment is kept. */
static int skip_layout(pw_t *w)
{
    while (w->pos < w->len) {
        int c = at(w, 0);
        if (cls(c) == C_SP) {
            adv(w);
        } else if (c == '%') {
            uint32_t line = w->line, col = (uint32_t) (w->pos - w->lstart) + 1;
            while (w->pos < w->len && (at(w, 0) == '%' || at(w, 0) == ' ' || at(w, 0) == '\t'))
                adv(w);
            size_t a = w->pos;
            while (w->pos < w->len && at(w, 0) != '\n') adv(w);
            size_t b = w->pos;
            while (b > a && cls((unsigned char) w->src[b - 1]) == C_SP) b--;
            if (w->at_top && !w->prelude) {
                if (b - a > ZL_MAX_LINE) return pfail(w, ZL_E_LIMIT, "comment too long");
                for (size_t i = a; i < b; i++)
                    if ((unsigned char) w->src[i] < 32 && w->src[i] != '\t')
                        return pfail(w, ZL_E_UNSUPPORTED, "a control character in a comment");
                int32_t n = newnode(w, K_COMMENT, line, col);
                if (n < 0) return ok(w);
                N(w, n)->kid = (int32_t) a;
                N(w, n)->v = (int64_t) (b - a);
                TRY(add_item(w, I_COMMENT, n));
            }
        } else if (c == '/' && at(w, 1) == '*') {
            adv(w);
            adv(w);
            while (w->pos < w->len && !(at(w, 0) == '*' && at(w, 1) == '/')) adv(w);
            if (w->pos >= w->len) return pfail(w, ZL_E_SYNTAX, "a /* comment without its */");
            adv(w);
            adv(w);
        } else {
            break;
        }
    }
    return ZL_OK;
}

/* Decode a quoted atom or string body starting after its quote q into
 * w->sbuf, its length to *n; pos ends after the closing quote. */
static int quoted(pw_t *w, int q, size_t *n)
{
    size_t k = 0;
    for (;;) {
        int c = at(w, 0);
        if (c < 0)
            return pfail(w, ZL_E_SYNTAX, "a quoted name or string without its closing quote");
        if (c == q) {
            if (at(w, 1) != q) {
                adv(w);
                break;
            }
            adv(w);
            adv(w);
        } else if (c == '\\') {
            int e = at(w, 1);
            if (e == 'n')
                c = '\n';
            else if (e == 't')
                c = '\t';
            else if (e == '\\' || e == '\'' || e == '"' || e == '`')
                c = e;
            else
                return pfail(w, ZL_E_UNSUPPORTED,
                             "escapes other than \\n \\t \\\\ \\' \\\" \\` in quotes");
            adv(w);
            adv(w);
        } else if (c < 32 || c > 126) {
            return pfail(w, ZL_E_UNSUPPORTED,
                         "a quoted name or string holds only printable ASCII (or \\n, \\t)");
        } else {
            adv(w);
        }
        if (k >= sizeof w->sbuf / 2) return pfail(w, ZL_E_LIMIT, "quoted name or string too long");
        w->sbuf[k++] = (char) c;
    }
    *n = k;
    return ZL_OK;
}

static int lex(pw_t *w, ptok_t *t)
{
    TRY(skip_layout(w));
    zl_bzero(t, sizeof *t);
    t->start = w->pos;
    t->line = w->prelude ? 0 : w->line;
    t->col = (uint32_t) (w->pos - w->lstart) + 1;
    t->atom = -1;
    if (w->pos >= w->len) {
        t->kind = T_EOF;
        t->end = w->pos;
        return ZL_OK;
    }
    int c = at(w, 0), k = cls(c);
    if (k == C_DI) {
        int64_t v = 0;
        if (c == '0' && at(w, 1) == '\'') {
            int ch = at(w, 2);
            if (ch < 32 || ch > 126 || ch == '\\' || ch == '\'')
                return pfail(w, ZL_E_UNSUPPORTED,
                             "0' with a quote, backslash or control character");
            adv(w);
            adv(w);
            adv(w);
            v = ch;
        } else {
            if (c == '0' && (at(w, 1) == 'x' || at(w, 1) == 'o' || at(w, 1) == 'b'))
                return pfail(w, ZL_E_UNSUPPORTED, "hexadecimal, octal and binary numbers");
            while (cls(at(w, 0)) == C_DI) {
                if (v > (ZL_V_REF - 1 - (at(w, 0) - '0')) / 10)
                    return pfail(w, ZL_E_UNSUPPORTED,
                                 "an integer beyond 62 bits (SWI has bignums)");
                v = v * 10 + (at(w, 0) - '0');
                if (v >= ZL_V_REF)
                    return pfail(w, ZL_E_UNSUPPORTED,
                                 "an integer beyond 62 bits (SWI has bignums)");
                adv(w);
            }
            if (at(w, 0) == '.' && cls(at(w, 1)) == C_DI)
                return pfail(w, ZL_E_UNSUPPORTED, "floating-point numbers");
        }
        if (at(w, 0) >= 0 && (cls(at(w, 0)) >= C_UC || at(w, 0) == '\''))
            return pfail(w, ZL_E_UNSUPPORTED,
                         "a number followed by a letter (floats, digit groups, radix)");
        t->kind = T_INT;
        t->ival = v;
    } else if (k == C_LC || k == C_UC) {
        size_t a = w->pos;
        while (w->pos < w->len && cls(at(w, 0)) >= C_UC) adv(w);
        if (k == C_UC) {
            t->kind = T_VAR;
        } else {
            t->kind = T_NAME;
            t->atom = atom_n(w, w->src + a, w->pos - a);
            if (t->atom < 0) return ok(w);
        }
    } else if (k == C_SY) {
        if (c == '.' && (at(w, 1) < 0 || cls(at(w, 1)) == C_SP || at(w, 1) == '%')) {
            adv(w);
            t->kind = T_END;
            t->end = w->pos;
            return ZL_OK;
        }
        size_t a = w->pos;
        while (w->pos < w->len && cls(at(w, 0)) == C_SY) adv(w);
        if (w->pos - a >= 2 && w->src[a] == '/' && w->src[a + 1] == '*')
            return pfail(w, ZL_E_SYNTAX, "a comment inside a symbol name");
        if (!w->prelude && w->src[a] == '$')
            return pfail(w, ZL_E_UNSUPPORTED, "names beginning with $");
        t->kind = T_NAME;
        t->atom = atom_n(w, w->src + a, w->pos - a);
        if (t->atom < 0) return ok(w);
    } else if (c == '!' || c == ';') {
        adv(w);
        t->kind = T_NAME;
        t->atom = c == '!' ? w->a_cut : w->a_semi;
    } else if (k == C_PU) {
        adv(w);
        t->kind = T_PUNCT;
        t->ch = (char) c;
    } else if (c == '\'') {
        size_t n;
        adv(w);
        TRY(quoted(w, '\'', &n));
        if (!w->prelude && n && w->sbuf[0] == '$')
            return tfail(w, t, ZL_E_UNSUPPORTED, "names beginning with $");
        if (same(w->sbuf, n, "[]") || same(w->sbuf, n, "[|]"))
            return tfail(w, t, ZL_E_UNSUPPORTED,
                         "the quoted atoms '[]' and '[|]' (SWI 7 tells them from [] and lists)");
        if (same(w->sbuf, n, ".") && at(w, 0) == '(')
            return tfail(w, t, ZL_E_UNSUPPORTED, "a '.'(...) term (SWI 7 reads it as a dict call)");
        t->kind = T_NAME;
        t->atom = atom_n(w, w->sbuf, n);
        if (t->atom < 0) return ok(w);
    } else if (c == '"') {
        size_t n;
        adv(w);
        t->kind = T_STR;
        TRY(quoted(w, '"', &n));
    } else if (c == '`') {
        return pfail(w, ZL_E_UNSUPPORTED, "back-quoted text");
    } else {
        return pfail(w, ZL_E_UNSUPPORTED, "a character outside the Prolog subset");
    }
    t->end = w->pos;
    t->fn = t->kind == T_NAME && at(w, 0) == '(';
    return ZL_OK;
}

static int next_tok(pw_t *w, ptok_t *t)
{
    if (w->has_peek) {
        *t = w->peek;
        w->has_peek = 0;
        return ZL_OK;
    }
    return lex(w, t);
}

static int peek_tok(pw_t *w, ptok_t **t)
{
    if (!w->has_peek) {
        TRY(lex(w, &w->peek));
        w->has_peek = 1;
    }
    *t = &w->peek;
    return ZL_OK;
}

static int expect(pw_t *w, char ch, const char *msg)
{
    ptok_t t;
    TRY(next_tok(w, &t));
    if (t.kind != T_PUNCT || t.ch != ch) return tfail(w, &t, ZL_E_SYNTAX, msg);
    return ZL_OK;
}

/* ------------------------------------------------------------ reader */

static int32_t tnode(pw_t *w, int kind, const ptok_t *t)
{
    return newnode(w, kind, t->line, t->col);
}

static int32_t mkcomp(pw_t *w, int32_t atom, int32_t first, const ptok_t *t)
{
    int32_t n = tnode(w, K_COMP, t);
    if (n < 0) return -1;
    N(w, n)->v = atom;
    N(w, n)->kid = first;
    return n;
}

static int32_t mkatom(pw_t *w, int32_t atom, const ptok_t *t)
{
    int32_t n = tnode(w, K_ATOM, t);
    if (n >= 0) N(w, n)->v = atom;
    return n;
}

static uint32_t arity(pw_t *w, int32_t n)
{
    uint32_t k = 0;
    if (N(w, n)->kind != K_COMP) return 0;
    for (int32_t a = N(w, n)->kid; a >= 0; a = N(w, a)->next) k++;
    return k;
}

static int32_t arg(pw_t *w, int32_t n, uint32_t i)
{
    int32_t a = N(w, n)->kid;
    while (i-- && a >= 0) a = N(w, a)->next;
    return a;
}

static int terminator(const ptok_t *t)
{
    return t->kind == T_END || t->kind == T_EOF ||
           (t->kind == T_PUNCT &&
            (t->ch == ')' || t->ch == ',' || t->ch == '|' || t->ch == ']' || t->ch == '}'));
}

static int32_t parse(pw_t *w, int maxp, int *outp);

static int32_t var_node(pw_t *w, const ptok_t *t)
{
    int32_t n = tnode(w, K_VAR, t);
    if (n < 0) return -1;
    size_t len = t->end - t->start;
    int64_t idx = -1;
    if (len > 255) {
        tfail(w, t, ZL_E_LIMIT, "variable name too long");
        return -1;
    }
    if (!(len == 1 && w->src[t->start] == '_')) {
        for (uint32_t i = 0; i < w->nvars && idx < 0; i++)
            if (w->vars[i].len == len) {
                size_t k = 0;
                while (k < len && w->src[w->vars[i].off + k] == w->src[t->start + k]) k++;
                if (k == len) idx = i;
            }
        if (idx < 0) {
            if (w->nvars >= P_VARS) {
                tfail(w, t, ZL_E_LIMIT, "too many variables in one clause");
                return -1;
            }
            w->vars[w->nvars].off = (uint32_t) t->start;
            w->vars[w->nvars].len = (uint32_t) len;
            idx = w->nvars++;
        }
    }
    N(w, n)->v = (idx + 1) | ((int64_t) len << 32);
    N(w, n)->kid = (int32_t) t->start;
    return n;
}

static int var_index(pw_t *w, int32_t n)
{
    return (int) (N(w, n)->v & 0xffffffff) - 1;
}

/* The arguments of f( ... ) up to the closing parenthesis. */
static int32_t args(pw_t *w, int32_t *first)
{
    int32_t last = -1, n = 0;
    *first = -1;
    for (;;) {
        int p;
        int32_t a = parse(w, P_ARG, &p);
        if (a < 0) return -1;
        if (last < 0)
            *first = a;
        else
            N(w, last)->next = a;
        last = a;
        n++;
        ptok_t t;
        if (next_tok(w, &t)) return -1;
        if (t.kind == T_PUNCT && t.ch == ')') break;
        if (!(t.kind == T_PUNCT && t.ch == ',')) {
            tfail(w, &t, ZL_E_SYNTAX, "expected , or ) after an argument");
            return -1;
        }
        if (n >= 250) {
            tfail(w, &t, ZL_E_LIMIT, "too many arguments");
            return -1;
        }
    }
    return n;
}

static int32_t list(pw_t *w, const ptok_t *open)
{
    int32_t elems[256], tail = -1;
    uint32_t n = 0;
    for (;;) {
        int p;
        int32_t e = parse(w, P_ARG, &p);
        if (e < 0) return -1;
        if (n >= 256) {
            nfail(w, e, ZL_E_LIMIT, "a list literal longer than 256 elements");
            return -1;
        }
        elems[n++] = e;
        ptok_t *pk;
        if (peek_tok(w, &pk)) return -1;
        if (pk->kind == T_PUNCT && pk->ch == ',') {
            w->has_peek = 0;
            continue;
        }
        if (pk->kind == T_PUNCT && pk->ch == '|') {
            w->has_peek = 0;
            tail = parse(w, P_ARG, &p);
            if (tail < 0) return -1;
        }
        break;
    }
    if (expect(w, ']', "expected , | or ] in the list")) return -1;
    if (tail < 0) tail = mkatom(w, w->a_nil, open);
    for (uint32_t i = n; i-- > 0;) {
        int32_t c = tnode(w, K_CONS, open);
        if (c < 0 || tail < 0) return -1;
        N(w, c)->kid = elems[i];
        N(w, elems[i])->next = tail;
        tail = c;
    }
    return tail;
}

static int32_t primary(pw_t *w, int maxp, int *outp)
{
    ptok_t t, *pk;
    *outp = 0;
    if (next_tok(w, &t)) return -1;
    switch (t.kind) {
    case T_INT: {
        int32_t n = tnode(w, K_INT, &t);
        if (n >= 0) N(w, n)->v = t.ival;
        return n;
    }
    case T_VAR:
        return var_node(w, &t);
    case T_STR: {
        int32_t n = tnode(w, K_STR, &t);
        if (n < 0) return -1;
        N(w, n)->kid = (int32_t) t.start + 1;
        N(w, n)->v = (int64_t) (t.end - t.start - 2);
        return n;
    }
    case T_PUNCT:
        if (t.ch == '(') {
            int p;
            int32_t x = parse(w, 1200, &p);
            if (x < 0 || expect(w, ')', "expected )")) return -1;
            return x;
        }
        if (t.ch == '[' || t.ch == '{') {
            char close = t.ch == '[' ? ']' : '}';
            if (peek_tok(w, &pk)) return -1;
            if (pk->kind == T_PUNCT && pk->ch == close) {
                w->has_peek = 0;
                if (at(w, 0) == '(') {
                    tfail(w, &t, ZL_E_UNSUPPORTED, "[] or {} as the name of a compound term");
                    return -1;
                }
                return mkatom(w, t.ch == '[' ? w->a_nil : w->a_curly, &t);
            }
            if (t.ch == '[') return list(w, &t);
            int p;
            int32_t x = parse(w, 1200, &p);
            if (x < 0 || expect(w, '}', "expected }")) return -1;
            return mkcomp(w, w->a_curly, x, &t);
        }
        tfail(w, &t, ZL_E_SYNTAX, "a term was expected");
        return -1;
    case T_NAME: {
        patom_t *A = &w->atoms[t.atom];
        if (t.atom == w->a_minus && !t.fn) {
            if (peek_tok(w, &pk)) return -1;
            if (pk->kind == T_INT && pk->start == t.end) {
                int32_t n = tnode(w, K_INT, &t);
                if (n >= 0) N(w, n)->v = -pk->ival;
                w->has_peek = 0;
                return n;
            }
        }
        if (t.fn) {
            ptok_t open;
            int32_t first;
            if (next_tok(w, &open) || args(w, &first) < 0) return -1;
            return mkcomp(w, t.atom, first, &t);
        }
        if (A->pre) {
            if (peek_tok(w, &pk)) return -1;
            if (terminator(pk)) return mkatom(w, t.atom, &t);
            if (pk->kind == T_NAME && w->atoms[pk->atom].in && !w->atoms[pk->atom].pre && !pk->fn) {
                tfail(w, &t, ZL_E_UNSUPPORTED,
                      "a prefix operator before an infix operator: put the atom in parentheses");
                return -1;
            }
            int pri = A->pre, am = A->pret ? pri : pri - 1, p;
            if (pri > (maxp > 1200 ? maxp - P_ARGM : maxp)) {
                tfail(w, &t, ZL_E_SYNTAX, "operator priority clash");
                return -1;
            }
            int32_t x = parse(w, maxp > 1200 ? am + P_ARGM : am, &p);
            if (x < 0) return -1;
            *outp = pri;
            return mkcomp(w, t.atom, x, &t);
        }
        return mkatom(w, t.atom, &t);
    }
    case T_END:
        tfail(w, &t, ZL_E_SYNTAX, "a term was expected before the full stop");
        return -1;
    default:
        tfail(w, &t, ZL_E_SYNTAX, "a term was expected");
        return -1;
    }
}

static int32_t parse(pw_t *w, int maxp, int *outp)
{
    if (++w->depth > P_NEST) {
        pfail(w, ZL_E_LIMIT, "terms nested too deeply");
        return -1;
    }
    int lp, argm = maxp > 1200;
    if (argm) maxp -= P_ARGM; /* SWI reads arguments up to 1200, ending at a comma */
    int32_t left = primary(w, argm ? maxp + P_ARGM : maxp, &lp);
    while (left >= 0) {
        ptok_t *pk;
        if (peek_tok(w, &pk)) return -1;
        int32_t op = -1;
        if (pk->kind == T_NAME)
            op = pk->atom;
        else if (pk->kind == T_PUNCT && pk->ch == ',')
            op = w->a_comma;
        if (op < 0 || !w->atoms[op].in || op == w->a_bar || (argm && op == w->a_comma)) break;
        int pri = w->atoms[op].in, ty = w->atoms[op].int_;
        int lmax = ty == 2 ? pri : pri - 1, rmax = ty == 1 ? pri : pri - 1, rp;
        if (pri > maxp || lp > lmax) break;
        if (w->atoms[op].len == 1 && aname(w, op)[0] == '.') {
            tfail(w, pk, ZL_E_UNSUPPORTED, "the infix '.' (SWI 7 reads it as a dict call)");
            return -1;
        }
        ptok_t t = *pk;
        w->has_peek = 0;
        int32_t right = parse(w, argm ? rmax + P_ARGM : rmax, &rp);
        if (right < 0) return -1;
        N(w, left)->next = right;
        left = mkcomp(w, op, left, &t);
        lp = pri;
    }
    w->depth--;
    *outp = lp;
    return left;
}

/* ------------------------------------------------------------ program */

static uint32_t alen(pw_t *w, int32_t a)
{
    return w->atoms[a].len;
}

/* Is name/ar in SWI's system predicate table? */
static int is_sys(pw_t *w, int32_t atom, uint32_t ar)
{
    const char *s = aname(w, atom);
    uint32_t n = alen(w, atom);
    for (const char *p = sys_preds; *p;) {
        const char *e = p;
        while (*e && *e != ' ') e++;
        const char *sl = e;
        while (sl > p && sl[-1] != '/') sl--;
        if (sl > p && (uint32_t) (sl - 1 - p) == n) {
            uint32_t k = 0, num = 0;
            while (k < n && p[k] == s[k]) k++;
            for (const char *d = sl; d < e; d++) num = num * 10 + (uint32_t) (*d - '0');
            if (k == n && num == ar) return 1;
        }
        p = *e ? e + 1 : e;
    }
    return 0;
}

static int find_bi(pw_t *w, int32_t atom, uint32_t ar, int internal)
{
    for (size_t i = 0; i < sizeof bis / sizeof bis[0]; i++)
        if (bis[i].arity == ar && same(aname(w, atom), alen(w, atom), bis[i].name) &&
            (!bis[i].internal || internal))
            return bis[i].id;
    return B_NONE;
}

static int is_hook(pw_t *w, int32_t atom, uint32_t ar)
{
    for (size_t i = 0; i < sizeof hooks / sizeof hooks[0]; i++)
        if (hooks[i].arity == ar && same(aname(w, atom), alen(w, atom), hooks[i].name)) return 1;
    return 0;
}

static int32_t find_pred(pw_t *w, int32_t atom, uint32_t ar)
{
    for (uint32_t i = 0; i < w->npreds; i++)
        if (w->preds[i].atom == atom && w->preds[i].arity == ar) return (int32_t) i;
    return -1;
}

/* name and arity of a goal or head; -1 if it is neither an atom nor a compound */
static int32_t functor(pw_t *w, int32_t n, uint32_t *ar)
{
    *ar = 0;
    if (N(w, n)->kind == K_ATOM) return (int32_t) N(w, n)->v;
    if (N(w, n)->kind == K_COMP) {
        *ar = arity(w, n);
        return (int32_t) N(w, n)->v;
    }
    return -1;
}

static int add_clause(pw_t *w, int32_t head, int32_t body, int32_t pred)
{
    if (w->nclauses >= P_CLAUSES)
        return nfail(w, head >= 0 ? head : body, ZL_E_LIMIT, "too many clauses");
    pclause_t *cl = &w->clauses[w->nclauses];
    zl_bzero(cl, sizeof *cl);
    cl->head = head;
    cl->body = body;
    cl->pred = pred;
    cl->next = -1;
    cl->nvars = (uint16_t) w->nvars;
    cl->prelude = (uint8_t) w->prelude;
    if (pred >= 0) {
        ppred_t *p = &w->preds[pred];
        if (p->last >= 0)
            w->clauses[p->last].next = (int32_t) w->nclauses;
        else
            p->first = (int32_t) w->nclauses;
        p->last = (int32_t) w->nclauses;
        p->nclauses++;
    }
    w->nclauses++;
    return ZL_OK;
}

/* Read one clause; *done is set at the end of the text. */
static int read_clause(pw_t *w, int *done)
{
    ptok_t *pk, t;
    int p;
    w->at_top = 1;
    w->nvars = 0;
    w->depth = 0;
    TRY(peek_tok(w, &pk));
    w->at_top = 0;
    if (pk->kind == T_EOF) {
        *done = 1;
        return ZL_OK;
    }
    *done = 0;
    int32_t x = parse(w, 1200, &p);
    if (x < 0) return ok(w);
    TRY(next_tok(w, &t));
    if (t.kind != T_END) return tfail(w, &t, ZL_E_SYNTAX, "operator expected, or a full stop");
    uint32_t ar;
    int32_t f = functor(w, x, &ar), head = x, body = -1;
    if (f == w->a_neck && ar == 1) {
        int32_t g = N(w, x)->kid;
        uint32_t gar;
        if (functor(w, g, &gar) == w->a_init && gar == 1) {
            TRY(add_clause(w, -1, N(w, g)->kid, -1));
            return w->prelude ? ZL_OK : add_item(w, I_INIT, (int32_t) w->nclauses - 1);
        }
        return nfail(w, x, ZL_E_UNSUPPORTED, "directives other than initialization/1");
    }
    if (f == w->a_neck && ar == 2) {
        head = N(w, x)->kid;
        body = N(w, head)->next;
    } else if (f == w->a_dcg && ar == 2) {
        return nfail(w, x, ZL_E_UNSUPPORTED, "DCG rules (-->)");
    } else if (f >= 0 && same(aname(w, f), alen(w, f), "?-") && ar == 1) {
        return nfail(w, x, ZL_E_UNSUPPORTED, "?- directives");
    }
    f = functor(w, head, &ar);
    if (f < 0 || f == w->a_nil)
        return nfail(w, head, ZL_E_UNSUPPORTED,
                     "a clause head that is not an atom or compound term");
    if (!w->prelude && (find_bi(w, f, ar, 1) || is_sys(w, f, ar)))
        return nfail(w, head, ZL_E_UNSUPPORTED,
                     "defining a built-in predicate (SWI: no permission to modify it)");
    if (is_hook(w, f, ar))
        return nfail(w, head, ZL_E_UNSUPPORTED, "defining a hook such as portray/1");
    int32_t pr = find_pred(w, f, ar);
    if (pr >= 0 && w->preds[pr].prelude != w->prelude)
        return nfail(w, head, ZL_E_UNSUPPORTED, "a predicate the prelude defines");
    if (pr < 0) {
        if (w->npreds >= P_PREDS) return nfail(w, head, ZL_E_LIMIT, "too many predicates");
        pr = (int32_t) w->npreds++;
        ppred_t *pp = &w->preds[pr];
        zl_bzero(pp, sizeof *pp);
        pp->atom = f;
        pp->arity = (uint16_t) ar;
        pp->first = pp->last = -1;
        pp->label = -1;
        pp->user = (uint8_t) !w->prelude;
        pp->prelude = (uint8_t) w->prelude;
    }
    TRY(add_clause(w, head, body, pr));
    return w->prelude ? ZL_OK : add_item(w, I_CLAUSE, (int32_t) w->nclauses - 1);
}

static int read_text(pw_t *w, const char *src, size_t len, int prelude)
{
    w->src = src;
    w->len = len;
    w->pos = 0;
    w->lstart = 0;
    w->line = prelude ? 0 : 1;
    w->prelude = prelude;
    w->has_peek = 0;
    for (int done = 0; !done;) TRY(read_clause(w, &done));
    return ZL_OK;
}

static int load_prelude(pw_t *w, size_t i)
{
    const char *t = prelude[i].text;
    size_t n = 0;
    while (t[n]) n++;
    const char *s0 = w->src;
    size_t l0 = w->len;
    TRY(read_text(w, t, n, 1));
    w->src = s0;
    w->len = l0;
    w->prelude = 0;
    return ZL_OK;
}

/* Check a goal and load the prelude predicates it calls. */
static int scan_goal(pw_t *w, int32_t g, int internal, uint32_t depth, uint8_t *loaded)
{
    uint32_t ar;
    if (depth > P_NEST) return nfail(w, g, ZL_E_LIMIT, "goals nested too deeply");
    int k = N(w, g)->kind;
    if (k == K_VAR)
        return nfail(w, g, ZL_E_UNSUPPORTED, "a variable as a goal (call/1) is outside the subset");
    int32_t f = functor(w, g, &ar);
    if (f < 0 || f == w->a_nil)
        return nfail(w, g, ZL_E_UNSUPPORTED, "a number, string or list is not a goal");
    int b = find_bi(w, f, ar, internal);
    switch (b) {
    case B_CONJ:
    case B_DISJ:
    case B_ITE:
    case B_FORALL:
        TRY(scan_goal(w, arg(w, g, 0), internal, depth + 1, loaded));
        return scan_goal(w, arg(w, g, 1), internal, depth + 1, loaded);
    case B_NOT:
    case B_NOTW:
    case B_ONCE:
    case B_IGNORE:
        return scan_goal(w, arg(w, g, 0), internal, depth + 1, loaded);
    case B_FINDALL:
        return scan_goal(w, arg(w, g, 1), internal, depth + 1, loaded);
    case B_NONE:
        break;
    default:
        return ZL_OK;
    }
    if (find_pred(w, f, ar) >= 0) return ZL_OK;
    for (size_t i = 0; i < sizeof prelude / sizeof prelude[0]; i++)
        if (prelude[i].arity == ar && same(aname(w, f), alen(w, f), prelude[i].name)) {
            if (loaded[i]) break;
            loaded[i] = 1;
            return load_prelude(w, i);
        }
    if (is_sys(w, f, ar))
        return nfail(w, g, ZL_E_UNSUPPORTED,
                     "a built-in predicate outside the Prolog subset (assert, call/N, catch, ...)");
    return nfail(w, g, ZL_E_UNDEFINED, "a call to a predicate the program does not define");
}

static int collect(pw_t *w)
{
    uint8_t loaded[sizeof prelude / sizeof prelude[0]];
    zl_bzero(loaded, sizeof loaded);
    TRY(read_text(w, w->usrc, w->ulen, 0));
    for (uint32_t i = 0; i < w->nclauses; i++) {
        pclause_t *cl = &w->clauses[i];
        if (cl->body >= 0) TRY(scan_goal(w, cl->body, cl->prelude, 0, loaded));
    }
    return ok(w);
}

/* ------------------------------------------------------------ emitting */

static int32_t E(pw_t *w, int op, int64_t a, int32_t b)
{
    return zl_emit(w->c, op, a, b);
}

static void o(pw_t *w, int op)
{
    E(w, op, 0, 0);
}

static void pu(pw_t *w, int64_t v)
{
    E(w, ZL_PUSH, v, 0);
}

static void ld(pw_t *w, int32_t s)
{
    E(w, ZL_LOAD, s, 0);
}

static void st(pw_t *w, int32_t s)
{
    E(w, ZL_STORE, s, 0);
}

static void ldf(pw_t *w, int k)
{
    E(w, ZL_LOADF, k, 0);
}

static void stf(pw_t *w, int k)
{
    E(w, ZL_STOREF, k, 0);
}

static void isa(pw_t *w, int k)
{
    E(w, ZL_ISA, k, 0);
}

static void link0(pw_t *w)
{
    E(w, ZL_LINK, 0, 0);
}

/* top := top[i] */
static void vref(pw_t *w, int64_t i)
{
    pu(w, i);
    o(w, ZL_VREF);
}

/* Jumps, calls and code values carry label numbers until gen() ends. */
static int32_t newlab(pw_t *w)
{
    if (w->nlabels >= P_LABELS) {
        zl_fail(w->c, ZL_E_LIMIT, "program too large for the Prolog compiler");
        return 0;
    }
    w->labels[w->nlabels] = -1;
    return (int32_t) w->nlabels++;
}

static void setlab(pw_t *w, int32_t l)
{
    w->labels[l] = (int32_t) w->c->p->ncode;
}

static void jl(pw_t *w, int op, int32_t l)
{
    E(w, op, l, 0);
}

static void pucode(pw_t *w, int32_t l)
{
    pu(w, ZL_V_CODE + l);
}

static int32_t hl(pw_t *w, int h)
{
    if (!w->hlab[h]) w->hlab[h] = newlab(w) + 1;
    return w->hlab[h] - 1;
}

static void callh(pw_t *w, int h)
{
    jl(w, ZL_CALL, hl(w, h));
}

static void jh(pw_t *w, int op, int h)
{
    jl(w, op, hl(w, h));
}

static int32_t atom_find(pw_t *w, const char *s)
{
    size_t n = 0;
    while (s[n]) n++;
    for (uint32_t i = 0; i < w->natoms; i++)
        if (same(aname(w, (int32_t) i), w->atoms[i].len, s)) return (int32_t) i;
    return -1;
}

/* The run-time value of an atom: a string literal ([] is the empty list). */
static int64_t aval(pw_t *w, int32_t a)
{
    if (a == w->a_nil) return ZL_V_NIL;
    patom_t *A = &w->atoms[a];
    if (A->str < 0) {
        A->str = zl_addstr(w->c, aname(w, a), A->len);
        if (A->str < 0) return ZL_V_NIL;
    }
    return ZL_V_STRC + A->str;
}

/* What the writer needs to know about an atom (ATOMINFO). */
enum { AI_QUOTE = 1, AI_MINUS = 2, AI_COMMA = 4, AI_BAR = 8, AI_CURLY = 16 };
#define AI_PRE 8 /* prefix priority at bit 8, fy at bit 20 */
#define AI_FY  ((int64_t) 1 << 20)
#define AI_IN  24 /* infix priority at bit 24, type at bit 36 */
#define AI_INT 36
#define AI_OPS (((int64_t) 4095 << AI_PRE) | ((int64_t) 4095 << AI_IN))
enum { C_PREFIX_SIGN = 0x200000, C_PREFIX_OP = 0x400000, C_INFIX_OP = 0x800000 };

static int64_t ainfo(pw_t *w, int32_t a)
{
    patom_t *A = &w->atoms[a];
    const char *s = aname(w, a);
    int64_t v = 0;
    if (A->type == AT_QUOTE) v |= AI_QUOTE;
    if (same(s, A->len, "-")) v |= AI_MINUS;
    if (same(s, A->len, ",")) v |= AI_COMMA;
    if (same(s, A->len, "|")) v |= AI_BAR;
    if (same(s, A->len, "{}")) v |= AI_CURLY;
    v |= (int64_t) A->pre << AI_PRE;
    if (A->pret) v |= AI_FY;
    v |= (int64_t) A->in << AI_IN;
    v |= (int64_t) A->int_ << AI_INT;
    return v;
}

/* SWI's character classes, 4 bits each, 15 printable characters a word. */
static int64_t ct_word(int k)
{
    int64_t v = 0;
    for (int i = 0; i < 15; i++) {
        int ch = 32 + k * 15 + i;
        if (ch > 126) break;
        v |= (int64_t) cls(ch) << (4 * i);
    }
    return v;
}

/* ------------------------------------------------------------ helpers */

/* DEREF (t -- t): follow bound variables. */
static void h_deref(pw_t *w)
{
    int32_t l = newlab(w), r = newlab(w);
    setlab(w, l);
    o(w, ZL_DUP);
    isa(w, ZL_ISA_VECTOR);
    jl(w, ZL_JZ, r);
    o(w, ZL_DUP);
    vref(w, 0);
    isa(w, ZL_ISA_NUMBER);
    jl(w, ZL_JZ, r);
    o(w, ZL_DUP);
    vref(w, 1);
    o(w, ZL_SWAP);
    o(w, ZL_OVER);
    o(w, ZL_EQ);
    jl(w, ZL_JZ, l);
    setlab(w, r);
    o(w, ZL_RET);
}

/* TCLASS (t -- k): 0 variable, 1 compound, 2 list cell, 3 atom, 4 integer,
 * 5 the empty list (SWI 7+: [] is a reserved symbol, not an atom). */
static void h_tclass(pw_t *w)
{
    int32_t i = newlab(w), p = newlab(w), a = newlab(w);
    callh(w, H_DEREF);
    o(w, ZL_DUP);
    isa(w, ZL_ISA_NUMBER);
    jl(w, ZL_JNZ, i);
    o(w, ZL_DUP);
    isa(w, ZL_ISA_PAIR);
    jl(w, ZL_JNZ, p);
    o(w, ZL_DUP);
    isa(w, ZL_ISA_VECTOR);
    jl(w, ZL_JZ, a);
    vref(w, 0);
    isa(w, ZL_ISA_NUMBER);
    pu(w, 1);
    o(w, ZL_SWAP);
    o(w, ZL_SUB);
    o(w, ZL_RET);
    setlab(w, i);
    o(w, ZL_DROP);
    pu(w, 4);
    o(w, ZL_RET);
    setlab(w, p);
    o(w, ZL_DROP);
    pu(w, 2);
    o(w, ZL_RET);
    setlab(w, a);
    pu(w, ZL_V_NIL);
    o(w, ZL_EQ);
    pu(w, 2);
    o(w, ZL_MUL);
    pu(w, 3);
    o(w, ZL_ADD);
    o(w, ZL_RET);
}

/* NEWVAR ( -- v): a fresh variable [stamp, itself]. */
static void h_newvar(pw_t *w)
{
    pu(w, 2);
    o(w, ZL_VNEW);
    o(w, ZL_DUP);
    pu(w, 1);
    o(w, ZL_OVER);
    o(w, ZL_VSET);
    o(w, ZL_DUP);
    pu(w, 0);
    ld(w, w->sVC);
    pu(w, 1);
    o(w, ZL_ADD);
    o(w, ZL_DUP);
    st(w, w->sVC);
    o(w, ZL_VSET);
    o(w, ZL_RET);
}

/* ALLOC (size --): a new environment [CPC, CPE, B0, 0...]. */
static void h_alloc(pw_t *w)
{
    o(w, ZL_VNEW);
    o(w, ZL_DUP);
    pu(w, 0);
    ld(w, w->sCPC);
    o(w, ZL_VSET);
    o(w, ZL_DUP);
    pu(w, 1);
    ld(w, w->sCPE);
    o(w, ZL_VSET);
    o(w, ZL_DUP);
    pu(w, 2);
    ld(w, w->sB0);
    o(w, ZL_VSET);
    st(w, w->sE);
    o(w, ZL_RET);
}

/* PUSHCP (alt n --): a choicepoint saving the machine and n arguments. */
static void h_pushcp(pw_t *w)
{
    static const uint8_t cells[] = {0, 2, 3, 4, 5, 6, 7};
    int32_t t7 = w->sT + 7, t8 = w->sT + 8, l = newlab(w), d = newlab(w);
    int32_t regs[7];
    regs[0] = w->sB;
    regs[1] = w->sTR;
    regs[2] = w->sE;
    regs[3] = w->sCPC;
    regs[4] = w->sCPE;
    regs[5] = w->sB0;
    regs[6] = w->sVC;
    st(w, t7);
    ld(w, t7);
    pu(w, 8);
    o(w, ZL_ADD);
    o(w, ZL_VNEW);
    o(w, ZL_DUP);
    st(w, t8);
    o(w, ZL_SWAP);
    pu(w, 1);
    o(w, ZL_SWAP);
    o(w, ZL_VSET);
    for (int k = 0; k < 7; k++) {
        ld(w, t8);
        pu(w, cells[k]);
        ld(w, regs[k]);
        o(w, ZL_VSET);
    }
    setlab(w, l);
    ld(w, t7);
    jl(w, ZL_JZ, d);
    ld(w, t7);
    pu(w, 1);
    o(w, ZL_SUB);
    st(w, t7);
    ld(w, t8);
    ld(w, t7);
    pu(w, 8);
    o(w, ZL_ADD);
    ld(w, t7);
    E(w, ZL_LOADX, w->sA, (int32_t) w->nA);
    o(w, ZL_VSET);
    jl(w, ZL_JMP, l);
    setlab(w, d);
    ld(w, t8);
    st(w, w->sB);
    o(w, ZL_RET);
}

/* FAIL: backtrack to the newest choicepoint (or to FAILK when none). */
static void h_fail(pw_t *w)
{
    int32_t t7 = w->sT + 7, f1 = newlab(w), f2 = newlab(w), f3 = newlab(w), f4 = newlab(w),
            f5 = newlab(w);
    ld(w, w->sB);
    pu(w, ZL_V_NIL);
    o(w, ZL_EQ);
    jl(w, ZL_JZ, f1);
    ld(w, w->sFAILK);
    o(w, ZL_JMPI);
    setlab(w, f1);
    ld(w, w->sB);
    vref(w, 2);
    st(w, t7);
    setlab(w, f2);
    ld(w, w->sTR);
    ld(w, t7);
    o(w, ZL_EQ);
    jl(w, ZL_JNZ, f3);
    ld(w, w->sTR);
    o(w, ZL_CAR);
    pu(w, 1);
    o(w, ZL_OVER);
    o(w, ZL_VSET);
    ld(w, w->sTR);
    o(w, ZL_CDR);
    st(w, w->sTR);
    jl(w, ZL_JMP, f2);
    setlab(w, f3);
    ld(w, w->sB);
    vref(w, 3);
    st(w, w->sE);
    ld(w, w->sB);
    vref(w, 4);
    st(w, w->sCPC);
    ld(w, w->sB);
    vref(w, 5);
    st(w, w->sCPE);
    ld(w, w->sB);
    vref(w, 6);
    st(w, w->sB0);
    ld(w, w->sB);
    o(w, ZL_VLEN);
    pu(w, 8);
    o(w, ZL_SUB);
    st(w, t7);
    setlab(w, f4);
    ld(w, t7);
    jl(w, ZL_JZ, f5);
    ld(w, t7);
    pu(w, 1);
    o(w, ZL_SUB);
    st(w, t7);
    ld(w, t7);
    ld(w, w->sB);
    ld(w, t7);
    pu(w, 8);
    o(w, ZL_ADD);
    o(w, ZL_VREF);
    E(w, ZL_STOREX, w->sA, (int32_t) w->nA);
    jl(w, ZL_JMP, f4);
    setlab(w, f5);
    ld(w, w->sB);
    vref(w, 1);
    o(w, ZL_JMPI);
}

/* KEYOF (t -- key): what first-argument indexing compares. */
static void h_keyof(pw_t *w)
{
    int32_t v = newlab(w), c = newlab(w), p = newlab(w);
    callh(w, H_DEREF);
    o(w, ZL_DUP);
    callh(w, H_TCLASS);
    o(w, ZL_DUP);
    jl(w, ZL_JZ, v);
    o(w, ZL_DUP);
    pu(w, 1);
    o(w, ZL_EQ);
    jl(w, ZL_JNZ, c);
    pu(w, 2);
    o(w, ZL_EQ);
    jl(w, ZL_JNZ, p);
    o(w, ZL_RET);
    setlab(w, v);
    o(w, ZL_DROP);
    o(w, ZL_DROP);
    pu(w, ZL_V_TRUE);
    o(w, ZL_RET);
    setlab(w, c);
    o(w, ZL_DROP);
    vref(w, 0);
    o(w, ZL_RET);
    setlab(w, p);
    o(w, ZL_DROP);
    pu(w, ZL_V_FALSE);
    o(w, ZL_RET);
}

/* UNIFY / IDENT (a b -- flag): stubs into UC (a b mode). */
static void h_ustub(pw_t *w, int mode)
{
    link0(w);
    o(w, ZL_ROT);
    o(w, ZL_ROT);
    pu(w, mode);
    jh(w, ZL_JMP, H_UC);
}

/* UC: frame [a, b, mode, i, n/class(b), class(a)]. */
static void h_uc(pw_t *w)
{
    int32_t L = newlab(w), anv = newlab(w), bnv = newlab(w), ncons = newlab(w), C = newlab(w),
            last = newlab(w), yes = newlab(w), no = newlab(w), uc = hl(w, H_UC);
    E(w, ZL_ENTER, 6, 3);
    setlab(w, L);
    ldf(w, 0);
    callh(w, H_DEREF);
    stf(w, 0);
    ldf(w, 1);
    callh(w, H_DEREF);
    stf(w, 1);
    ldf(w, 0);
    ldf(w, 1);
    o(w, ZL_EQ);
    jl(w, ZL_JNZ, yes);
    ldf(w, 0);
    callh(w, H_TCLASS);
    o(w, ZL_DUP);
    stf(w, 5);
    jl(w, ZL_JNZ, anv);
    ldf(w, 2);
    jl(w, ZL_JNZ, no);
    link0(w);
    ldf(w, 0);
    ldf(w, 1);
    callh(w, H_BIND);
    jl(w, ZL_JMP, yes);
    setlab(w, anv);
    ldf(w, 1);
    callh(w, H_TCLASS);
    o(w, ZL_DUP);
    stf(w, 4);
    jl(w, ZL_JNZ, bnv);
    ldf(w, 2);
    jl(w, ZL_JNZ, no);
    link0(w);
    ldf(w, 1);
    ldf(w, 0);
    callh(w, H_BIND);
    jl(w, ZL_JMP, yes);
    setlab(w, bnv);
    ldf(w, 5);
    ldf(w, 4);
    o(w, ZL_NE);
    jl(w, ZL_JNZ, no);
    ldf(w, 5);
    pu(w, 2);
    o(w, ZL_EQ);
    jl(w, ZL_JZ, ncons);
    link0(w);
    ldf(w, 0);
    o(w, ZL_CAR);
    ldf(w, 1);
    o(w, ZL_CAR);
    ldf(w, 2);
    jl(w, ZL_CALL, uc);
    jl(w, ZL_JZ, no);
    ldf(w, 0);
    o(w, ZL_CDR);
    stf(w, 0);
    ldf(w, 1);
    o(w, ZL_CDR);
    stf(w, 1);
    jl(w, ZL_JMP, L);
    setlab(w, ncons);
    ldf(w, 5);
    pu(w, 1);
    o(w, ZL_NE);
    jl(w, ZL_JNZ, no);
    ldf(w, 0);
    o(w, ZL_VLEN);
    o(w, ZL_DUP);
    stf(w, 4);
    ldf(w, 1);
    o(w, ZL_VLEN);
    o(w, ZL_NE);
    jl(w, ZL_JNZ, no);
    ldf(w, 0);
    vref(w, 0);
    ldf(w, 1);
    vref(w, 0);
    o(w, ZL_NE);
    jl(w, ZL_JNZ, no);
    pu(w, 1);
    stf(w, 3);
    setlab(w, C);
    ldf(w, 3);
    ldf(w, 4);
    pu(w, 1);
    o(w, ZL_SUB);
    o(w, ZL_EQ);
    jl(w, ZL_JNZ, last);
    link0(w);
    ldf(w, 0);
    ldf(w, 3);
    o(w, ZL_VREF);
    ldf(w, 1);
    ldf(w, 3);
    o(w, ZL_VREF);
    ldf(w, 2);
    jl(w, ZL_CALL, uc);
    jl(w, ZL_JZ, no);
    ldf(w, 3);
    pu(w, 1);
    o(w, ZL_ADD);
    stf(w, 3);
    jl(w, ZL_JMP, C);
    setlab(w, last);
    ldf(w, 0);
    ldf(w, 3);
    o(w, ZL_VREF);
    stf(w, 0);
    ldf(w, 1);
    ldf(w, 3);
    o(w, ZL_VREF);
    stf(w, 1);
    jl(w, ZL_JMP, L);
    setlab(w, yes);
    o(w, ZL_LEAVE);
    pu(w, 1);
    o(w, ZL_RET);
    setlab(w, no);
    o(w, ZL_LEAVE);
    pu(w, 0);
    o(w, ZL_RET);
}

/* BIND (link v t): v := t, trailing when older than the newest
 * choicepoint. No occurs check, as ISO and SWI. */
static void h_bind(pw_t *w)
{
    int32_t done = newlab(w);
    E(w, ZL_ENTER, 2, 2);
    ldf(w, 0);
    pu(w, 1);
    ldf(w, 1);
    o(w, ZL_VSET);
    ld(w, w->sB);
    pu(w, ZL_V_NIL);
    o(w, ZL_EQ);
    jl(w, ZL_JNZ, done);
    ldf(w, 0);
    vref(w, 0);
    ld(w, w->sB);
    vref(w, 7);
    o(w, ZL_GT);
    jl(w, ZL_JNZ, done);
    ldf(w, 0);
    ld(w, w->sTR);
    o(w, ZL_CONS);
    st(w, w->sTR);
    setlab(w, done);
    o(w, ZL_LEAVE);
    o(w, ZL_RET);
}

/* Evaluable functions: name, arity, IR op (0: none, unary plus). */
static const struct {
    const char *name;
    uint8_t arity, op, a;
} evs[] = {
    {"+", 2, ZL_ADD, 0},    {"-", 2, ZL_SUB, 0},     {"*", 2, ZL_MUL, 0},    {"//", 2, ZL_DIVT, 0},
    {"mod", 2, ZL_MODF, 0}, {"rem", 2, ZL_MODT, 0},  {"div", 2, ZL_DIVF, 0}, {"/", 2, ZL_DIVX, 0},
    {"min", 2, ZL_MIN, 0},  {"max", 2, ZL_MAX, 0},   {"^", 2, ZL_POW, 1},    {"**", 2, ZL_POW, 1},
    {"/\\", 2, ZL_AND, 0},  {"\\/", 2, ZL_OR, 0},    {"xor", 2, ZL_XOR, 0},  {">>", 2, ZL_SHR, 0},
    {"<<", 2, ZL_SHL, 0},   {"-", 1, ZL_NEG, 0},     {"+", 1, ZL_HALT, 0},   {"abs", 1, ZL_ABS, 0},
    {"sign", 1, ZL_SGN, 0}, {"\\", 1, ZL_INVERT, 0},
};

static void ev_op(pw_t *w, size_t i)
{
    if (evs[i].op != ZL_HALT) E(w, evs[i].op, evs[i].a, 0);
}

/* EVAL (t -- n) */
static void h_eval(pw_t *w)
{
    link0(w);
    o(w, ZL_SWAP);
    jh(w, ZL_JMP, H_EV);
}

/* Compare the functor on top with the evaluable functions of one arity
 * whose atoms the program uses (no other functor can occur at run time). */
static void ev_chain(pw_t *w, uint8_t ar)
{
    int32_t labs[sizeof evs / sizeof evs[0]];
    for (size_t i = 0; i < sizeof evs / sizeof evs[0]; i++) {
        int32_t a = atom_find(w, evs[i].name);
        labs[i] = -1;
        if (evs[i].arity != ar || a < 0 || w->atoms[a].str < 0) continue;
        labs[i] = newlab(w);
        o(w, ZL_DUP);
        pu(w, ZL_V_STRC + w->atoms[a].str);
        o(w, ZL_EQ);
        jl(w, ZL_JNZ, labs[i]);
    }
    E(w, ZL_TRAP, ZL_E_DOMAIN, 0);
    for (size_t i = 0; i < sizeof evs / sizeof evs[0]; i++) {
        if (labs[i] < 0) continue;
        setlab(w, labs[i]);
        o(w, ZL_DROP);
        ev_op(w, i);
        o(w, ZL_LEAVE);
        o(w, ZL_RET);
    }
}

/* EV: frame [t, len]. */
static void h_ev(pw_t *w)
{
    int32_t e1 = newlab(w), e2 = newlab(w), e3 = newlab(w), bad = newlab(w), self = hl(w, H_EV);
    E(w, ZL_ENTER, 2, 1);
    ldf(w, 0);
    callh(w, H_DEREF);
    o(w, ZL_DUP);
    stf(w, 0);
    o(w, ZL_DUP);
    isa(w, ZL_ISA_NUMBER);
    jl(w, ZL_JZ, e1);
    o(w, ZL_LEAVE);
    o(w, ZL_RET);
    setlab(w, e1);
    o(w, ZL_DUP);
    isa(w, ZL_ISA_PAIR);
    jl(w, ZL_JZ, e2);
    o(w, ZL_DUP);
    o(w, ZL_CDR);
    callh(w, H_DEREF);
    pu(w, ZL_V_NIL);
    o(w, ZL_NE);
    jl(w, ZL_JNZ, bad);
    o(w, ZL_CAR);
    link0(w);
    o(w, ZL_SWAP);
    jl(w, ZL_CALL, self);
    o(w, ZL_LEAVE);
    o(w, ZL_RET);
    setlab(w, e2);
    o(w, ZL_DUP);
    isa(w, ZL_ISA_VECTOR);
    jl(w, ZL_JZ, bad);
    o(w, ZL_DUP);
    vref(w, 0);
    isa(w, ZL_ISA_NUMBER);
    jl(w, ZL_JNZ, bad);
    o(w, ZL_DUP);
    o(w, ZL_VLEN);
    o(w, ZL_DUP);
    stf(w, 1);
    pu(w, 2);
    o(w, ZL_EQ);
    jl(w, ZL_JZ, e3);
    vref(w, 1);
    link0(w);
    o(w, ZL_SWAP);
    jl(w, ZL_CALL, self);
    ldf(w, 0);
    vref(w, 0);
    ev_chain(w, 1);
    setlab(w, e3);
    ldf(w, 1);
    pu(w, 3);
    o(w, ZL_NE);
    jl(w, ZL_JNZ, bad);
    vref(w, 1);
    link0(w);
    o(w, ZL_SWAP);
    jl(w, ZL_CALL, self);
    ldf(w, 0);
    vref(w, 2);
    link0(w);
    o(w, ZL_SWAP);
    jl(w, ZL_CALL, self);
    ldf(w, 0);
    vref(w, 0);
    ev_chain(w, 2);
    setlab(w, bad);
    E(w, ZL_TRAP, ZL_E_DOMAIN, 0);
}

/* ISLIST (t -- flag) */
static void h_islist(pw_t *w)
{
    int32_t L = newlab(w), e = newlab(w);
    setlab(w, L);
    callh(w, H_DEREF);
    o(w, ZL_DUP);
    isa(w, ZL_ISA_PAIR);
    jl(w, ZL_JZ, e);
    o(w, ZL_CDR);
    jl(w, ZL_JMP, L);
    setlab(w, e);
    pu(w, ZL_V_NIL);
    o(w, ZL_EQ);
    o(w, ZL_RET);
}

/* SKIP (l -- tail count) */
static void h_skip(pw_t *w)
{
    int32_t t6 = w->sT + 6, L = newlab(w), d = newlab(w);
    pu(w, 0);
    st(w, t6);
    setlab(w, L);
    callh(w, H_DEREF);
    o(w, ZL_DUP);
    isa(w, ZL_ISA_PAIR);
    jl(w, ZL_JZ, d);
    o(w, ZL_CDR);
    ld(w, t6);
    pu(w, 1);
    o(w, ZL_ADD);
    st(w, t6);
    jl(w, ZL_JMP, L);
    setlab(w, d);
    ld(w, t6);
    o(w, ZL_RET);
}

/* REVERSE (l -- r) of a list findall built. */
static void h_reverse(pw_t *w)
{
    int32_t L = newlab(w), d = newlab(w);
    pu(w, ZL_V_NIL);
    o(w, ZL_SWAP);
    setlab(w, L);
    o(w, ZL_DUP);
    isa(w, ZL_ISA_PAIR);
    jl(w, ZL_JZ, d);
    o(w, ZL_DUP);
    o(w, ZL_CAR);
    o(w, ZL_ROT);
    o(w, ZL_CONS);
    o(w, ZL_SWAP);
    o(w, ZL_CDR);
    jl(w, ZL_JMP, L);
    setlab(w, d);
    o(w, ZL_DROP);
    o(w, ZL_RET);
}

/* COPY (t -- t'): a copy with fresh variables (findall). */
static void h_copy(pw_t *w)
{
    pu(w, ZL_V_NIL);
    st(w, w->sMAP);
    link0(w);
    o(w, ZL_SWAP);
    callh(w, H_CP);
    o(w, ZL_RET);
}

/* CP: frame [t, new, i]; MAP is an association list old . new. */
static void h_cp(pw_t *w)
{
    int32_t pair = newlab(w), ret = newlab(w), var = newlab(w), cl = newlab(w), cd = newlab(w),
            pl = newlab(w), pe = newlab(w), ml = newlab(w), mf = newlab(w), mn = newlab(w),
            self = hl(w, H_CP);
    E(w, ZL_ENTER, 3, 1);
    ldf(w, 0);
    callh(w, H_DEREF);
    o(w, ZL_DUP);
    stf(w, 0);
    o(w, ZL_DUP);
    isa(w, ZL_ISA_PAIR);
    jl(w, ZL_JNZ, pair);
    o(w, ZL_DUP);
    isa(w, ZL_ISA_VECTOR);
    jl(w, ZL_JZ, ret);
    o(w, ZL_DUP);
    vref(w, 0);
    isa(w, ZL_ISA_NUMBER);
    jl(w, ZL_JNZ, var);
    o(w, ZL_VLEN);
    o(w, ZL_VNEW);
    stf(w, 1);
    ldf(w, 1);
    pu(w, 0);
    ldf(w, 0);
    vref(w, 0);
    o(w, ZL_VSET);
    pu(w, 1);
    stf(w, 2);
    setlab(w, cl);
    ldf(w, 2);
    ldf(w, 0);
    o(w, ZL_VLEN);
    o(w, ZL_LT);
    jl(w, ZL_JZ, cd);
    ldf(w, 1);
    ldf(w, 2);
    link0(w);
    ldf(w, 0);
    ldf(w, 2);
    o(w, ZL_VREF);
    jl(w, ZL_CALL, self);
    o(w, ZL_VSET);
    ldf(w, 2);
    pu(w, 1);
    o(w, ZL_ADD);
    stf(w, 2);
    jl(w, ZL_JMP, cl);
    setlab(w, cd);
    ldf(w, 1);
    o(w, ZL_LEAVE);
    o(w, ZL_RET);
    setlab(w, pair);
    o(w, ZL_DROP);
    link0(w);
    ldf(w, 0);
    o(w, ZL_CAR);
    jl(w, ZL_CALL, self);
    pu(w, ZL_V_NIL);
    o(w, ZL_CONS);
    o(w, ZL_DUP);
    stf(w, 1);
    stf(w, 2);
    setlab(w, pl);
    ldf(w, 0);
    o(w, ZL_CDR);
    callh(w, H_DEREF);
    o(w, ZL_DUP);
    stf(w, 0);
    isa(w, ZL_ISA_PAIR);
    jl(w, ZL_JZ, pe);
    link0(w);
    ldf(w, 0);
    o(w, ZL_CAR);
    jl(w, ZL_CALL, self);
    pu(w, ZL_V_NIL);
    o(w, ZL_CONS);
    o(w, ZL_DUP);
    ldf(w, 2);
    o(w, ZL_SWAP);
    o(w, ZL_SETCDR);
    stf(w, 2);
    jl(w, ZL_JMP, pl);
    setlab(w, pe);
    ldf(w, 2);
    link0(w);
    ldf(w, 0);
    jl(w, ZL_CALL, self);
    o(w, ZL_SETCDR);
    ldf(w, 1);
    o(w, ZL_LEAVE);
    o(w, ZL_RET);
    setlab(w, var);
    o(w, ZL_DROP);
    ld(w, w->sMAP);
    stf(w, 1);
    setlab(w, ml);
    ldf(w, 1);
    pu(w, ZL_V_NIL);
    o(w, ZL_EQ);
    jl(w, ZL_JNZ, mn);
    ldf(w, 1);
    o(w, ZL_CAR);
    o(w, ZL_CAR);
    ldf(w, 0);
    o(w, ZL_EQ);
    jl(w, ZL_JNZ, mf);
    ldf(w, 1);
    o(w, ZL_CDR);
    stf(w, 1);
    jl(w, ZL_JMP, ml);
    setlab(w, mf);
    ldf(w, 1);
    o(w, ZL_CAR);
    o(w, ZL_CDR);
    o(w, ZL_LEAVE);
    o(w, ZL_RET);
    setlab(w, mn);
    callh(w, H_NEWVAR);
    o(w, ZL_DUP);
    ldf(w, 0);
    o(w, ZL_SWAP);
    o(w, ZL_CONS);
    ld(w, w->sMAP);
    o(w, ZL_CONS);
    st(w, w->sMAP);
    setlab(w, ret);
    o(w, ZL_LEAVE);
    o(w, ZL_RET);
}

/* TAB (n --): n spaces (none when n <= 0). */
static void h_tab(pw_t *w)
{
    int32_t L = newlab(w), d = newlab(w);
    setlab(w, L);
    o(w, ZL_DUP);
    pu(w, 0);
    o(w, ZL_GT);
    jl(w, ZL_JZ, d);
    pu(w, ' ');
    o(w, ZL_OUTC);
    pu(w, 1);
    o(w, ZL_SUB);
    jl(w, ZL_JMP, L);
    setlab(w, d);
    o(w, ZL_DROP);
    o(w, ZL_RET);
}

static void outs(pw_t *w, const char *s)
{
    size_t n = 0;
    while (s[n]) n++;
    E(w, ZL_OUTS, zl_addstr(w->c, s, n), 0);
}

/* FMTA (t --): ~a takes an atom other than [] (or a number, as SWI does). */
static void h_fmta(pw_t *w)
{
    int32_t num = newlab(w), bad = newlab(w);
    callh(w, H_DEREF);
    o(w, ZL_DUP);
    isa(w, ZL_ISA_NUMBER);
    jl(w, ZL_JNZ, num);
    o(w, ZL_DUP);
    isa(w, ZL_ISA_STRING);
    jl(w, ZL_JZ, bad);
    E(w, ZL_OUTV, 0, 0);
    o(w, ZL_RET);
    setlab(w, num);
    E(w, ZL_OUTI, 0, 0);
    o(w, ZL_RET);
    setlab(w, bad);
    E(w, ZL_TRAP, ZL_E_DOMAIN, 0);
}

/* FMTD (t --): ~d takes an integer. */
static void h_fmtd(pw_t *w)
{
    int32_t bad = newlab(w);
    callh(w, H_DEREF);
    o(w, ZL_DUP);
    isa(w, ZL_ISA_NUMBER);
    jl(w, ZL_JZ, bad);
    E(w, ZL_OUTI, 0, 0);
    o(w, ZL_RET);
    setlab(w, bad);
    E(w, ZL_TRAP, ZL_E_DOMAIN, 0);
}

/* FMTC (t --): ~c takes an ASCII code. */
static void h_fmtc(pw_t *w)
{
    int32_t bad = newlab(w);
    callh(w, H_DEREF);
    o(w, ZL_DUP);
    isa(w, ZL_ISA_NUMBER);
    jl(w, ZL_JZ, bad);
    o(w, ZL_DUP);
    pu(w, 0);
    o(w, ZL_LT);
    jl(w, ZL_JNZ, bad);
    o(w, ZL_DUP);
    pu(w, 127);
    o(w, ZL_GT);
    jl(w, ZL_JNZ, bad);
    o(w, ZL_OUTC);
    o(w, ZL_RET);
    setlab(w, bad);
    E(w, ZL_TRAP, ZL_E_DOMAIN, 0);
}

/* WRITE (t quoted --) */
static void h_write(pw_t *w)
{
    st(w, w->sQ);
    pu(w, -1);
    st(w, w->sLASTC);
    link0(w);
    o(w, ZL_SWAP);
    pu(w, 1200);
    pu(w, 0);
    callh(w, H_WT);
    o(w, ZL_RET);
}

/* PUTC (c --): one character, remembered for token spacing. */
static void h_putc(pw_t *w)
{
    o(w, ZL_DUP);
    o(w, ZL_OUTC);
    st(w, w->sLASTC);
    o(w, ZL_RET);
}

/* CLS (c -- class) */
static void h_cls(pw_t *w)
{
    int32_t low = newlab(w), hi = newlab(w);
    o(w, ZL_DUP);
    pu(w, 32);
    o(w, ZL_LT);
    jl(w, ZL_JNZ, low);
    o(w, ZL_DUP);
    pu(w, 126);
    o(w, ZL_GT);
    jl(w, ZL_JNZ, hi);
    pu(w, 32);
    o(w, ZL_SUB);
    o(w, ZL_DUP);
    pu(w, 15);
    o(w, ZL_DIVT);
    E(w, ZL_LOADX, w->sCT, 7);
    o(w, ZL_SWAP);
    pu(w, 15);
    o(w, ZL_MODT);
    pu(w, 4);
    o(w, ZL_MUL);
    o(w, ZL_SHR);
    pu(w, 15);
    o(w, ZL_AND);
    o(w, ZL_RET);
    setlab(w, low);
    o(w, ZL_DUP);
    pu(w, 9);
    o(w, ZL_GE);
    o(w, ZL_SWAP);
    pu(w, 13);
    o(w, ZL_LE);
    o(w, ZL_AND);
    o(w, ZL_RET);
    setlab(w, hi);
    o(w, ZL_DROP);
    pu(w, 0);
    o(w, ZL_RET);
}

/* OPENTOK (c -- rc): SWI's PutOpenToken: a space when the last character
 * written and c would otherwise join into one token; rc 2 if it wrote one. */
static void h_opentok(pw_t *w)
{
    int32_t t0 = w->sT, t1 = w->sT + 1, t2 = w->sT + 2, o2 = newlab(w), o3 = newlab(w),
            o4 = newlab(w), o5 = newlab(w), no = newlab(w), yes = newlab(w);
    st(w, t0);
    ld(w, w->sLASTC);
    pu(w, -1);
    o(w, ZL_EQ);
    jl(w, ZL_JNZ, no);
    ld(w, w->sLASTC);
    pu(w, C_PREFIX_SIGN);
    o(w, ZL_AND);
    jl(w, ZL_JZ, o2);
    ld(w, t0);
    callh(w, H_CLS);
    o(w, ZL_DUP);
    pu(w, C_DI);
    o(w, ZL_EQ);
    o(w, ZL_SWAP);
    pu(w, C_SY);
    o(w, ZL_EQ);
    o(w, ZL_OR);
    jl(w, ZL_JNZ, yes);
    setlab(w, o2);
    ld(w, w->sLASTC);
    pu(w, C_PREFIX_OP);
    o(w, ZL_AND);
    jl(w, ZL_JZ, o3);
    ld(w, t0);
    pu(w, '(');
    o(w, ZL_EQ);
    ld(w, t0);
    pu(w, '{');
    o(w, ZL_EQ);
    o(w, ZL_OR);
    jl(w, ZL_JNZ, yes);
    setlab(w, o3);
    ld(w, w->sLASTC);
    pu(w, C_INFIX_OP);
    o(w, ZL_AND);
    jl(w, ZL_JZ, o4);
    ld(w, t0);
    pu(w, '(');
    o(w, ZL_EQ);
    jl(w, ZL_JNZ, no);
    setlab(w, o4);
    ld(w, w->sLASTC);
    pu(w, 0x1fffff);
    o(w, ZL_AND);
    o(w, ZL_DUP);
    st(w, w->sLASTC);
    callh(w, H_CLS);
    st(w, t1);
    ld(w, t0);
    callh(w, H_CLS);
    st(w, t2);
    ld(w, t1);
    pu(w, C_UC);
    o(w, ZL_GE);
    ld(w, t2);
    pu(w, C_UC);
    o(w, ZL_GE);
    o(w, ZL_AND);
    jl(w, ZL_JNZ, yes);
    ld(w, t1);
    pu(w, C_SY);
    o(w, ZL_EQ);
    ld(w, t2);
    pu(w, C_SY);
    o(w, ZL_EQ);
    o(w, ZL_AND);
    jl(w, ZL_JNZ, yes);
    ld(w, t0);
    pu(w, '(');
    o(w, ZL_EQ);
    jl(w, ZL_JZ, o5);
    ld(w, t1);
    pu(w, C_PU);
    o(w, ZL_EQ);
    ld(w, t1);
    pu(w, C_SP);
    o(w, ZL_EQ);
    o(w, ZL_OR);
    jl(w, ZL_JZ, yes);
    setlab(w, o5);
    ld(w, t0);
    pu(w, '\'');
    o(w, ZL_EQ);
    ld(w, t1);
    pu(w, C_DI);
    o(w, ZL_EQ);
    ld(w, w->sLASTC);
    pu(w, '\'');
    o(w, ZL_EQ);
    o(w, ZL_OR);
    o(w, ZL_AND);
    jl(w, ZL_JNZ, yes);
    setlab(w, no);
    pu(w, 1);
    o(w, ZL_RET);
    setlab(w, yes);
    pu(w, ' ');
    o(w, ZL_OUTC);
    pu(w, ' ');
    st(w, w->sLASTC);
    pu(w, 2);
    o(w, ZL_RET);
}

/* WNUM (n --) */
static void h_wnum(pw_t *w)
{
    int32_t pos = newlab(w), out = newlab(w);
    o(w, ZL_DUP);
    pu(w, 0);
    o(w, ZL_LT);
    jl(w, ZL_JZ, pos);
    pu(w, '-');
    jl(w, ZL_JMP, out);
    setlab(w, pos);
    pu(w, '0');
    setlab(w, out);
    callh(w, H_OPENTOK);
    o(w, ZL_DROP);
    E(w, ZL_OUTI, 0, 0);
    pu(w, '0');
    st(w, w->sLASTC);
    o(w, ZL_RET);
}

/* WATOM (a -- rc): SWI's writeAtom, quoting when QUOTED asks for it. */
static void h_watom(pw_t *w)
{
    int32_t t3 = w->sT + 3, t4 = w->sT + 4, t5 = w->sT + 5, s = newlab(w), ne = newlab(w),
            raw = newlab(w), q = newlab(w), ql = newlab(w), qe = newlab(w), eq = newlab(w),
            eb = newlab(w), en = newlab(w), et = newlab(w), qn = newlab(w);
    o(w, ZL_DUP);
    pu(w, ZL_V_NIL);
    o(w, ZL_NE);
    jl(w, ZL_JNZ, s);
    o(w, ZL_DROP);
    pu(w, '[');
    callh(w, H_OPENTOK);
    outs(w, "[]");
    pu(w, ']');
    st(w, w->sLASTC);
    o(w, ZL_RET);
    setlab(w, s);
    st(w, t3);
    ld(w, t3);
    o(w, ZL_VLEN);
    o(w, ZL_DUP);
    st(w, t4);
    jl(w, ZL_JNZ, ne);
    ld(w, w->sQ);
    jl(w, ZL_JNZ, q);
    pu(w, 1);
    o(w, ZL_RET);
    setlab(w, ne);
    ld(w, w->sQ);
    jl(w, ZL_JZ, raw);
    ld(w, t3);
    callh(w, H_ATOMINFO);
    pu(w, AI_QUOTE);
    o(w, ZL_AND);
    jl(w, ZL_JNZ, q);
    setlab(w, raw);
    ld(w, t3);
    vref(w, 0);
    callh(w, H_OPENTOK);
    ld(w, t3);
    E(w, ZL_OUTV, 0, 0);
    ld(w, t3);
    ld(w, t4);
    pu(w, 1);
    o(w, ZL_SUB);
    o(w, ZL_VREF);
    st(w, w->sLASTC);
    o(w, ZL_RET);
    setlab(w, q);
    pu(w, '\'');
    callh(w, H_OPENTOK);
    pu(w, '\'');
    o(w, ZL_OUTC);
    pu(w, 0);
    st(w, t5);
    setlab(w, ql);
    ld(w, t5);
    ld(w, t4);
    o(w, ZL_LT);
    jl(w, ZL_JZ, qe);
    ld(w, t3);
    ld(w, t5);
    o(w, ZL_VREF);
    o(w, ZL_DUP);
    pu(w, '\'');
    o(w, ZL_EQ);
    jl(w, ZL_JNZ, eq);
    o(w, ZL_DUP);
    pu(w, '\\');
    o(w, ZL_EQ);
    jl(w, ZL_JNZ, eb);
    o(w, ZL_DUP);
    pu(w, '\n');
    o(w, ZL_EQ);
    jl(w, ZL_JNZ, en);
    o(w, ZL_DUP);
    pu(w, '\t');
    o(w, ZL_EQ);
    jl(w, ZL_JNZ, et);
    o(w, ZL_OUTC);
    jl(w, ZL_JMP, qn);
    setlab(w, eq);
    o(w, ZL_DROP);
    outs(w, "\\'");
    jl(w, ZL_JMP, qn);
    setlab(w, eb);
    o(w, ZL_DROP);
    outs(w, "\\\\");
    jl(w, ZL_JMP, qn);
    setlab(w, en);
    o(w, ZL_DROP);
    outs(w, "\\n");
    jl(w, ZL_JMP, qn);
    setlab(w, et);
    o(w, ZL_DROP);
    outs(w, "\\t");
    setlab(w, qn);
    ld(w, t5);
    pu(w, 1);
    o(w, ZL_ADD);
    st(w, t5);
    jl(w, ZL_JMP, ql);
    setlab(w, qe);
    pu(w, '\'');
    o(w, ZL_OUTC);
    pu(w, '\'');
    st(w, w->sLASTC);
    o(w, ZL_RET);
}

static void putc_(pw_t *w, int ch)
{
    pu(w, ch);
    callh(w, H_PUTC);
}

/* "(" as SWI's PutOpenBrace writes it */
static void open_brace(pw_t *w)
{
    pu(w, '(');
    callh(w, H_OPENTOK);
    o(w, ZL_DROP);
    putc_(w, '(');
}

/* WT (link t prec flags): SWI's writeTerm2 for integers, atoms, lists,
 * {}/1, operators and canonical compounds. Frame: [t, prec, flags,
 * functor, arity, info / index, embrace, priority, type]. */
static void h_wt(pw_t *w)
{
    int32_t self = hl(w, H_WT), notnum = newlab(w), atom = newlab(w), plain = newlab(w),
            list = newlab(w), lloop = newlab(w), ltail = newlab(w), lend = newlab(w),
            lclose = newlab(w), comp = newlab(w), notcurly = newlab(w), notpre = newlab(w),
            pre1 = newlab(w), pre2 = newlab(w), in1 = newlab(w), in2 = newlab(w), in3 = newlab(w),
            bar = newlab(w), close = newlab(w), canon = newlab(w), cl = newlab(w), cend = newlab(w),
            ret = newlab(w);
    E(w, ZL_ENTER, 9, 3);
    ldf(w, 0);
    callh(w, H_DEREF);
    stf(w, 0);
    ldf(w, 0);
    isa(w, ZL_ISA_NUMBER);
    jl(w, ZL_JZ, notnum);
    ldf(w, 0);
    callh(w, H_WNUM);
    jl(w, ZL_JMP, ret);
    setlab(w, notnum);
    ldf(w, 0);
    isa(w, ZL_ISA_PAIR);
    jl(w, ZL_JNZ, list);
    ldf(w, 0);
    isa(w, ZL_ISA_VECTOR);
    jl(w, ZL_JZ, atom);
    ldf(w, 0);
    vref(w, 0);
    o(w, ZL_DUP);
    stf(w, 3);
    isa(w, ZL_ISA_NUMBER);
    jl(w, ZL_JZ, comp);
    E(w, ZL_TRAP, ZL_E_DOMAIN, 0); /* an unbound variable */
    /* atoms */
    setlab(w, atom);
    ldf(w, 2);
    jl(w, ZL_JZ, plain);
    ldf(w, 0);
    callh(w, H_ATOMINFO);
    pu(w, AI_OPS);
    o(w, ZL_AND);
    jl(w, ZL_JZ, plain);
    open_brace(w);
    ldf(w, 0);
    callh(w, H_WATOM);
    o(w, ZL_DROP);
    putc_(w, ')');
    jl(w, ZL_JMP, ret);
    setlab(w, plain);
    ldf(w, 0);
    callh(w, H_WATOM);
    o(w, ZL_DROP);
    jl(w, ZL_JMP, ret);
    /* lists */
    setlab(w, list);
    putc_(w, '[');
    setlab(w, lloop);
    link0(w);
    ldf(w, 0);
    o(w, ZL_CAR);
    pu(w, 999);
    pu(w, 0);
    jl(w, ZL_CALL, self);
    ldf(w, 0);
    o(w, ZL_CDR);
    callh(w, H_DEREF);
    o(w, ZL_DUP);
    stf(w, 0);
    o(w, ZL_DUP);
    pu(w, ZL_V_NIL);
    o(w, ZL_EQ);
    jl(w, ZL_JNZ, lend);
    isa(w, ZL_ISA_PAIR);
    jl(w, ZL_JZ, ltail);
    putc_(w, ',');
    jl(w, ZL_JMP, lloop);
    setlab(w, ltail);
    putc_(w, '|');
    link0(w);
    ldf(w, 0);
    pu(w, 999);
    pu(w, 0);
    jl(w, ZL_CALL, self);
    jl(w, ZL_JMP, lclose);
    setlab(w, lend);
    o(w, ZL_DROP);
    setlab(w, lclose);
    putc_(w, ']');
    jl(w, ZL_JMP, ret);
    /* compounds */
    setlab(w, comp);
    ldf(w, 0);
    o(w, ZL_VLEN);
    pu(w, 1);
    o(w, ZL_SUB);
    stf(w, 4);
    ldf(w, 3);
    callh(w, H_ATOMINFO);
    stf(w, 5);
    pu(w, 0);
    stf(w, 6);
    ldf(w, 4);
    pu(w, 1);
    o(w, ZL_NE);
    jl(w, ZL_JNZ, notcurly);
    ldf(w, 5);
    pu(w, AI_CURLY);
    o(w, ZL_AND);
    jl(w, ZL_JZ, notcurly);
    pu(w, '{');
    callh(w, H_OPENTOK);
    o(w, ZL_DROP);
    putc_(w, '{');
    link0(w);
    ldf(w, 0);
    vref(w, 1);
    pu(w, 1200);
    pu(w, 0);
    jl(w, ZL_CALL, self);
    putc_(w, '}');
    jl(w, ZL_JMP, ret);
    setlab(w, notcurly);
    ldf(w, 4);
    pu(w, 1);
    o(w, ZL_NE);
    jl(w, ZL_JNZ, notpre);
    ldf(w, 5);
    pu(w, AI_PRE);
    o(w, ZL_SHR);
    pu(w, 4095);
    o(w, ZL_AND);
    o(w, ZL_DUP);
    stf(w, 7);
    jl(w, ZL_JZ, canon);
    ldf(w, 7);
    ldf(w, 1);
    o(w, ZL_GT);
    o(w, ZL_DUP);
    stf(w, 6);
    jl(w, ZL_JZ, pre1);
    open_brace(w);
    setlab(w, pre1);
    ldf(w, 3);
    callh(w, H_WATOM);
    o(w, ZL_DROP);
    ld(w, w->sLASTC);
    pu(w, C_PREFIX_OP);
    o(w, ZL_OR);
    st(w, w->sLASTC);
    ldf(w, 5);
    pu(w, AI_MINUS);
    o(w, ZL_AND);
    jl(w, ZL_JZ, pre2);
    ld(w, w->sLASTC);
    pu(w, C_PREFIX_SIGN);
    o(w, ZL_OR);
    st(w, w->sLASTC);
    setlab(w, pre2);
    link0(w);
    ldf(w, 0);
    vref(w, 1);
    ldf(w, 7);
    ldf(w, 5);
    pu(w, 20);
    o(w, ZL_SHR);
    pu(w, 1);
    o(w, ZL_AND);
    o(w, ZL_ADD);
    pu(w, 1);
    o(w, ZL_SUB);
    pu(w, 1);
    jl(w, ZL_CALL, self);
    jl(w, ZL_JMP, close);
    setlab(w, notpre);
    ldf(w, 4);
    pu(w, 2);
    o(w, ZL_NE);
    jl(w, ZL_JNZ, canon);
    ldf(w, 5);
    pu(w, AI_IN);
    o(w, ZL_SHR);
    pu(w, 4095);
    o(w, ZL_AND);
    o(w, ZL_DUP);
    stf(w, 7);
    jl(w, ZL_JZ, canon);
    ldf(w, 7);
    ldf(w, 1);
    o(w, ZL_GT);
    o(w, ZL_DUP);
    stf(w, 6);
    jl(w, ZL_JZ, in1);
    open_brace(w);
    setlab(w, in1);
    ldf(w, 5);
    pu(w, AI_INT);
    o(w, ZL_SHR);
    pu(w, 3);
    o(w, ZL_AND);
    stf(w, 8);
    link0(w);
    ldf(w, 0);
    vref(w, 1);
    ldf(w, 7);
    ldf(w, 8);
    pu(w, 2);
    o(w, ZL_EQ);
    o(w, ZL_ADD);
    pu(w, 1);
    o(w, ZL_SUB);
    pu(w, 1);
    jl(w, ZL_CALL, self);
    ldf(w, 5);
    pu(w, AI_COMMA | AI_BAR);
    o(w, ZL_AND);
    jl(w, ZL_JZ, in2);
    ldf(w, 5);
    pu(w, AI_COMMA);
    o(w, ZL_AND);
    jl(w, ZL_JZ, bar);
    putc_(w, ',');
    jl(w, ZL_JMP, in3);
    setlab(w, bar);
    putc_(w, '|');
    jl(w, ZL_JMP, in3);
    setlab(w, in2);
    ldf(w, 3);
    callh(w, H_WATOM);
    pu(w, 2);
    o(w, ZL_NE);
    jl(w, ZL_JNZ, in3);
    putc_(w, ' ');
    setlab(w, in3);
    ld(w, w->sLASTC);
    pu(w, C_INFIX_OP);
    o(w, ZL_OR);
    st(w, w->sLASTC);
    link0(w);
    ldf(w, 0);
    vref(w, 2);
    ldf(w, 7);
    ldf(w, 8);
    pu(w, 1);
    o(w, ZL_EQ);
    o(w, ZL_ADD);
    pu(w, 1);
    o(w, ZL_SUB);
    pu(w, 1);
    jl(w, ZL_CALL, self);
    setlab(w, close);
    ldf(w, 6);
    jl(w, ZL_JZ, ret);
    putc_(w, ')');
    jl(w, ZL_JMP, ret);
    setlab(w, canon);
    ldf(w, 3);
    callh(w, H_WATOM);
    o(w, ZL_DROP);
    putc_(w, '(');
    pu(w, 1);
    stf(w, 5);
    setlab(w, cl);
    link0(w);
    ldf(w, 0);
    ldf(w, 5);
    o(w, ZL_VREF);
    pu(w, 999);
    pu(w, 0);
    jl(w, ZL_CALL, self);
    ldf(w, 5);
    ldf(w, 4);
    o(w, ZL_EQ);
    jl(w, ZL_JNZ, cend);
    putc_(w, ',');
    ldf(w, 5);
    pu(w, 1);
    o(w, ZL_ADD);
    stf(w, 5);
    jl(w, ZL_JMP, cl);
    setlab(w, cend);
    putc_(w, ')');
    setlab(w, ret);
    o(w, ZL_LEAVE);
    o(w, ZL_RET);
}

/* ATOMINFO (a -- info): emitted last, over every atom the code uses, in
 * the order of their strings (which follows the code, not the order the
 * reader met the atoms in, so a program and its twin agree). */
static void h_atominfo(pw_t *w)
{
    int32_t labs[P_ATOMS];
    int64_t infos[P_ATOMS];
    uint32_t ord[P_ATOMS], n = 0;
    for (uint32_t i = 0; i < w->natoms; i++) {
        uint32_t j = n++;
        for (; j > 0 && w->atoms[ord[j - 1]].str > w->atoms[i].str; j--) ord[j] = ord[j - 1];
        ord[j] = i;
    }
    for (uint32_t k = 0; k < n; k++) {
        uint32_t i = ord[k];
        labs[i] = -1;
        if ((int32_t) i == w->a_nil || w->atoms[i].str < 0) continue;
        infos[i] = ainfo(w, (int32_t) i);
        if (!infos[i]) continue;
        labs[i] = newlab(w);
        o(w, ZL_DUP);
        pu(w, ZL_V_STRC + w->atoms[i].str);
        o(w, ZL_EQ);
        jl(w, ZL_JNZ, labs[i]);
    }
    o(w, ZL_DROP);
    pu(w, 0);
    o(w, ZL_RET);
    for (uint32_t k = 0; k < n; k++) {
        uint32_t i = ord[k];
        if (labs[i] < 0) continue;
        setlab(w, labs[i]);
        o(w, ZL_DROP);
        pu(w, infos[i]);
        o(w, ZL_RET);
    }
}

/* PROCEED: return through the environment's continuation. */
static void h_proceed(pw_t *w)
{
    ld(w, w->sE);
    vref(w, 0);
    ld(w, w->sE);
    vref(w, 1);
    st(w, w->sE);
    o(w, ZL_JMPI);
}

/* PROCF: a fact (or a built-in predicate) returns to CPC in CPE. */
static void h_procf(pw_t *w)
{
    ld(w, w->sCPE);
    st(w, w->sE);
    ld(w, w->sCPC);
    o(w, ZL_JMPI);
}

/* CUTE: ! in a clause body. */
static void h_cute(pw_t *w)
{
    ld(w, w->sE);
    vref(w, 2);
    st(w, w->sB);
    o(w, ZL_RET);
}

static void h_setkey(pw_t *w)
{
    ld(w, w->sA);
    callh(w, H_KEYOF);
    st(w, w->sKEY);
    o(w, ZL_RET);
}

/* TRYNEXT (code --): the next candidate clause, or none: pop. */
static void h_trynext(pw_t *w)
{
    int32_t p = newlab(w);
    o(w, ZL_DUP);
    jl(w, ZL_JZ, p);
    ld(w, w->sB);
    pu(w, 1);
    o(w, ZL_ROT);
    o(w, ZL_VSET);
    o(w, ZL_RET);
    setlab(w, p);
    o(w, ZL_DROP);
    jh(w, ZL_JMP, H_POPCP);
}

static void h_popcp(pw_t *w)
{
    ld(w, w->sB);
    vref(w, 0);
    st(w, w->sB);
    o(w, ZL_RET);
}

/* KEYIS (c -- flag): can a clause with first-argument key c match? */
static void h_keyis(pw_t *w)
{
    ld(w, w->sKEY);
    o(w, ZL_EQ);
    ld(w, w->sKEY);
    pu(w, ZL_V_TRUE);
    o(w, ZL_EQ);
    o(w, ZL_OR);
    o(w, ZL_RET);
}

/* SETC (code --): continuation for a call that returns here. */
static void h_setc(pw_t *w)
{
    st(w, w->sCPC);
    ld(w, w->sE);
    st(w, w->sCPE);
    ld(w, w->sB);
    st(w, w->sB0);
    o(w, ZL_RET);
}

/* SETT: the last call of a body inherits the clause's continuation. */
static void h_sett(pw_t *w)
{
    ld(w, w->sE);
    vref(w, 0);
    st(w, w->sCPC);
    ld(w, w->sE);
    vref(w, 1);
    st(w, w->sCPE);
    ld(w, w->sB);
    st(w, w->sB0);
    o(w, ZL_RET);
}

/* MKLIST (n -- list): n fresh variables. */
static void h_mklist(pw_t *w)
{
    int32_t L = newlab(w), d = newlab(w);
    pu(w, ZL_V_NIL);
    o(w, ZL_SWAP);
    setlab(w, L);
    o(w, ZL_DUP);
    jl(w, ZL_JZ, d);
    pu(w, 1);
    o(w, ZL_SUB);
    o(w, ZL_SWAP);
    callh(w, H_NEWVAR);
    o(w, ZL_SWAP);
    o(w, ZL_CONS);
    o(w, ZL_SWAP);
    jl(w, ZL_JMP, L);
    setlab(w, d);
    o(w, ZL_DROP);
    o(w, ZL_RET);
}

/* between(L, H, X) as SWI runs it: L and H integers (H may be inf or
 * infinite), X an integer or unbound; any other argument is an error. */
static void h_between(pw_t *w)
{
    int32_t a0 = w->sA, a1 = w->sA + 1, a2 = w->sA + 2, hok = newlab(w), hinf = newlab(w),
            xint = newlab(w), retry = newlab(w), last = newlab(w), more = newlab(w),
            fail1 = newlab(w), err = newlab(w);
    static const char *const infs[] = {"inf", "infinite"};
    ld(w, a0);
    callh(w, H_DEREF);
    o(w, ZL_DUP);
    isa(w, ZL_ISA_NUMBER);
    jl(w, ZL_JZ, err);
    st(w, a0);
    ld(w, a1);
    callh(w, H_DEREF);
    o(w, ZL_DUP);
    isa(w, ZL_ISA_NUMBER);
    jl(w, ZL_JNZ, hok);
    for (int k = 0; k < 2; k++) {
        int32_t a = atom_find(w, infs[k]);
        if (a < 0 || w->atoms[a].str < 0) continue;
        o(w, ZL_DUP);
        pu(w, ZL_V_STRC + w->atoms[a].str);
        o(w, ZL_EQ);
        jl(w, ZL_JNZ, hinf);
    }
    jl(w, ZL_JMP, err);
    setlab(w, hinf);
    o(w, ZL_DROP);
    pu(w, ZL_V_REF - 1);
    setlab(w, hok);
    st(w, a1);
    ld(w, a2);
    callh(w, H_DEREF);
    o(w, ZL_DUP);
    isa(w, ZL_ISA_NUMBER);
    jl(w, ZL_JNZ, xint);
    o(w, ZL_DUP);
    callh(w, H_TCLASS);
    jl(w, ZL_JNZ, err);
    st(w, a2);
    ld(w, a0);
    ld(w, a1);
    o(w, ZL_GT);
    jh(w, ZL_JNZ, H_FAIL);
    ld(w, a0);
    ld(w, a1);
    o(w, ZL_LT);
    jl(w, ZL_JZ, last);
    pucode(w, retry);
    pu(w, 3);
    callh(w, H_PUSHCP);
    jl(w, ZL_JMP, last);
    setlab(w, retry);
    ld(w, a0);
    pu(w, 1);
    o(w, ZL_ADD);
    st(w, a0);
    ld(w, w->sB);
    pu(w, 8);
    ld(w, a0);
    o(w, ZL_VSET);
    ld(w, a0);
    ld(w, a1);
    o(w, ZL_LT);
    jl(w, ZL_JNZ, more);
    callh(w, H_POPCP);
    setlab(w, more);
    setlab(w, last);
    ld(w, a2);
    ld(w, a0);
    callh(w, H_UNIFY);
    jh(w, ZL_JZ, H_FAIL);
    jh(w, ZL_JMP, H_PROCF);
    setlab(w, xint);
    o(w, ZL_DUP);
    ld(w, a0);
    o(w, ZL_LT);
    jl(w, ZL_JNZ, fail1);
    ld(w, a1);
    o(w, ZL_GT);
    jh(w, ZL_JNZ, H_FAIL);
    jh(w, ZL_JMP, H_PROCF);
    setlab(w, fail1);
    o(w, ZL_DROP);
    jh(w, ZL_JMP, H_FAIL);
    setlab(w, err);
    E(w, ZL_TRAP, ZL_E_DOMAIN, 0);
}

/* length(L, N) as SWI runs it, enumerating longer lists when both are
 * open. A0 list or its tail, A1 N, A2 cells counted, A3 cells added. */
static void h_length(pw_t *w)
{
    int32_t a0 = w->sA, a1 = w->sA + 1, a2 = w->sA + 2, a3 = w->sA + 3, nvar = newlab(w),
            i2 = newlab(w), v2 = newlab(w), retry = newlab(w), emit = newlab(w), err = newlab(w);
    ld(w, a1);
    callh(w, H_DEREF);
    o(w, ZL_DUP);
    st(w, a1);
    callh(w, H_TCLASS);
    o(w, ZL_DUP);
    jl(w, ZL_JZ, nvar);
    pu(w, 4);
    o(w, ZL_NE);
    jl(w, ZL_JNZ, err);
    ld(w, a1);
    pu(w, 0);
    o(w, ZL_LT);
    jl(w, ZL_JNZ, err);
    ld(w, a0);
    callh(w, H_SKIP);
    st(w, a2);
    st(w, a0);
    ld(w, a0);
    pu(w, ZL_V_NIL);
    o(w, ZL_EQ);
    jl(w, ZL_JZ, i2);
    ld(w, a1);
    ld(w, a2);
    o(w, ZL_EQ);
    jh(w, ZL_JZ, H_FAIL);
    jh(w, ZL_JMP, H_PROCF);
    setlab(w, i2);
    ld(w, a0);
    callh(w, H_TCLASS);
    jl(w, ZL_JNZ, err);
    ld(w, a1);
    ld(w, a2);
    o(w, ZL_LT);
    jh(w, ZL_JNZ, H_FAIL);
    ld(w, a0);
    ld(w, a1);
    ld(w, a2);
    o(w, ZL_SUB);
    callh(w, H_MKLIST);
    callh(w, H_UNIFY);
    jh(w, ZL_JZ, H_FAIL);
    jh(w, ZL_JMP, H_PROCF);
    setlab(w, nvar);
    o(w, ZL_DROP);
    ld(w, a0);
    callh(w, H_SKIP);
    st(w, a2);
    st(w, a0);
    ld(w, a0);
    pu(w, ZL_V_NIL);
    o(w, ZL_EQ);
    jl(w, ZL_JZ, v2);
    ld(w, a1);
    ld(w, a2);
    callh(w, H_UNIFY);
    jh(w, ZL_JZ, H_FAIL);
    jh(w, ZL_JMP, H_PROCF);
    setlab(w, v2);
    ld(w, a0);
    callh(w, H_TCLASS);
    jl(w, ZL_JNZ, err);
    pu(w, 0);
    st(w, a3);
    pucode(w, retry);
    pu(w, 4);
    callh(w, H_PUSHCP);
    jl(w, ZL_JMP, emit);
    setlab(w, retry);
    ld(w, a3);
    pu(w, 1);
    o(w, ZL_ADD);
    st(w, a3);
    ld(w, w->sB);
    pu(w, 11);
    ld(w, a3);
    o(w, ZL_VSET);
    setlab(w, emit);
    ld(w, a0);
    ld(w, a3);
    callh(w, H_MKLIST);
    callh(w, H_UNIFY);
    jh(w, ZL_JZ, H_FAIL);
    ld(w, a1);
    ld(w, a2);
    ld(w, a3);
    o(w, ZL_ADD);
    callh(w, H_UNIFY);
    jh(w, ZL_JZ, H_FAIL);
    jh(w, ZL_JMP, H_PROCF);
    setlab(w, err);
    E(w, ZL_TRAP, ZL_E_DOMAIN, 0);
}

/* NEWVARS (i n --): env[i .. i+n) := fresh variables */
static void h_newvars(pw_t *w)
{
    int32_t L = newlab(w), d = newlab(w);
    setlab(w, L);
    o(w, ZL_DUP);
    jl(w, ZL_JZ, d);
    o(w, ZL_OVER);
    ld(w, w->sE);
    o(w, ZL_SWAP);
    callh(w, H_NEWVAR);
    o(w, ZL_VSET);
    pu(w, 1);
    o(w, ZL_SUB);
    o(w, ZL_SWAP);
    pu(w, 1);
    o(w, ZL_ADD);
    o(w, ZL_SWAP);
    jl(w, ZL_JMP, L);
    setlab(w, d);
    o(w, ZL_DROP);
    o(w, ZL_DROP);
    o(w, ZL_RET);
}

/* SAVEB (i --): env[i] := B */
static void h_saveb(pw_t *w)
{
    ld(w, w->sE);
    o(w, ZL_SWAP);
    ld(w, w->sB);
    o(w, ZL_VSET);
    o(w, ZL_RET);
}

/* CUTTO (i --): B := env[i] */
static void h_cutto(pw_t *w)
{
    ld(w, w->sE);
    o(w, ZL_SWAP);
    o(w, ZL_VREF);
    st(w, w->sB);
    o(w, ZL_RET);
}

static void emit_helpers(pw_t *w)
{
    for (int again = 1; again && !w->c->failed;) {
        again = 0;
        for (int h = 0; h < H__COUNT; h++) {
            if (h == H_ATOMINFO || !w->hlab[h] || w->hdone[h]) continue;
            w->hdone[h] = 1;
            again = 1;
            setlab(w, w->hlab[h] - 1);
            switch (h) {
            case H_DEREF:
                h_deref(w);
                break;
            case H_TCLASS:
                h_tclass(w);
                break;
            case H_NEWVAR:
                h_newvar(w);
                break;
            case H_BIND:
                h_bind(w);
                break;
            case H_UNIFY:
                h_ustub(w, 0);
                break;
            case H_IDENT:
                h_ustub(w, 1);
                break;
            case H_UC:
                h_uc(w);
                break;
            case H_ALLOC:
                h_alloc(w);
                break;
            case H_PUSHCP:
                h_pushcp(w);
                break;
            case H_FAIL:
                h_fail(w);
                break;
            case H_KEYOF:
                h_keyof(w);
                break;
            case H_EVAL:
                h_eval(w);
                break;
            case H_EV:
                h_ev(w);
                break;
            case H_WRITE:
                h_write(w);
                break;
            case H_WT:
                h_wt(w);
                break;
            case H_WATOM:
                h_watom(w);
                break;
            case H_WNUM:
                h_wnum(w);
                break;
            case H_PUTC:
                h_putc(w);
                break;
            case H_OPENTOK:
                h_opentok(w);
                break;
            case H_CLS:
                h_cls(w);
                break;
            case H_COPY:
                h_copy(w);
                break;
            case H_CP:
                h_cp(w);
                break;
            case H_REVERSE:
                h_reverse(w);
                break;
            case H_ISLIST:
                h_islist(w);
                break;
            case H_SKIP:
                h_skip(w);
                break;
            case H_TAB:
                h_tab(w);
                break;
            case H_FMTA:
                h_fmta(w);
                break;
            case H_FMTD:
                h_fmtd(w);
                break;
            case H_FMTC:
                h_fmtc(w);
                break;
            case H_PROCEED:
                h_proceed(w);
                break;
            case H_PROCF:
                h_procf(w);
                break;
            case H_CUTE:
                h_cute(w);
                break;
            case H_SETKEY:
                h_setkey(w);
                break;
            case H_TRYNEXT:
                h_trynext(w);
                break;
            case H_POPCP:
                h_popcp(w);
                break;
            case H_KEYIS:
                h_keyis(w);
                break;
            case H_SETC:
                h_setc(w);
                break;
            case H_SETT:
                h_sett(w);
                break;
            case H_BETWEEN:
                h_between(w);
                break;
            case H_LENGTH:
                h_length(w);
                break;
            case H_MKLIST:
                h_mklist(w);
                break;
            case H_NEWVARS:
                h_newvars(w);
                break;
            case H_SAVEB:
                h_saveb(w);
                break;
            case H_CUTTO:
                h_cutto(w);
                break;
            default:
                break;
            }
        }
    }
    if (w->hlab[H_ATOMINFO]) {
        w->hdone[H_ATOMINFO] = 1;
        setlab(w, w->hlab[H_ATOMINFO] - 1);
        h_atominfo(w);
    }
}

/* ------------------------------------------------------------ clauses */

static void ldvar(pw_t *w, int k)
{
    if (w->noenv) {
        ld(w, w->sA + w->dir[k]);
    } else {
        ld(w, w->sE);
        vref(w, w->vslot[k]);
    }
}

/* Push the value of a term written in the clause. */
static int build(pw_t *w, int32_t n, uint32_t d)
{
    pnode_t *x = N(w, n);
    if (d > 60) return nfail(w, n, ZL_E_LIMIT, "a term nested too deeply for the IR data stack");
    switch (x->kind) {
    case K_INT:
        pu(w, x->v);
        return ok(w);
    case K_ATOM:
        pu(w, aval(w, (int32_t) x->v));
        return ok(w);
    case K_VAR: {
        int k = var_index(w, n);
        if (k < 0)
            callh(w, H_NEWVAR);
        else
            ldvar(w, k);
        return ok(w);
    }
    case K_CONS: {
        uint32_t ne = 0;
        int32_t t = n;
        while (N(w, t)->kind == K_CONS) {
            t = N(w, N(w, t)->kid)->next;
            ne++;
        }
        TRY(build(w, t, d + 1));
        for (uint32_t i = ne; i-- > 0;) {
            int32_t c = n;
            for (uint32_t j = 0; j < i; j++) c = N(w, N(w, c)->kid)->next;
            TRY(build(w, N(w, c)->kid, d + 1));
            o(w, ZL_SWAP);
            o(w, ZL_CONS);
        }
        return ok(w);
    }
    case K_COMP: {
        int64_t i = 1;
        pu(w, (int64_t) arity(w, n) + 1);
        o(w, ZL_VNEW);
        o(w, ZL_DUP);
        pu(w, 0);
        pu(w, aval(w, (int32_t) x->v));
        o(w, ZL_VSET);
        for (int32_t a = x->kid; a >= 0; a = N(w, a)->next, i++) {
            o(w, ZL_DUP);
            pu(w, i);
            TRY(build(w, a, d + 1));
            o(w, ZL_VSET);
        }
        return ok(w);
    }
    default:
        return nfail(w, n, ZL_E_UNSUPPORTED,
                     "strings as values (a \"string\" is admitted only as a format control)");
    }
}

/* Compile an arithmetic expression written in the clause. */
static int cexpr(pw_t *w, int32_t n, uint32_t d)
{
    pnode_t *x = N(w, n);
    if (d > 60) return nfail(w, n, ZL_E_LIMIT, "an expression nested too deeply");
    switch (x->kind) {
    case K_INT:
        pu(w, x->v);
        return ok(w);
    case K_VAR:
        TRY(build(w, n, d));
        callh(w, H_EVAL);
        return ok(w);
    case K_CONS: {
        int32_t e = x->kid, t = N(w, e)->next;
        if (N(w, t)->kind != K_ATOM || N(w, t)->v != w->a_nil)
            return nfail(w, n, ZL_E_UNSUPPORTED, "a list of more than one element in arithmetic");
        return cexpr(w, e, d + 1);
    }
    case K_COMP: {
        uint32_t ar = arity(w, n);
        for (size_t i = 0; i < sizeof evs / sizeof evs[0]; i++) {
            if (evs[i].arity != ar ||
                !same(aname(w, (int32_t) x->v), alen(w, (int32_t) x->v), evs[i].name))
                continue;
            for (int32_t a = x->kid; a >= 0; a = N(w, a)->next) TRY(cexpr(w, a, d + 1));
            ev_op(w, i);
            return ok(w);
        }
        return nfail(w, n, ZL_E_UNSUPPORTED,
                     "an evaluable function outside + - * / // mod rem div min max abs sign "
                     "^ ** /\\ \\/ xor \\ >> << (floats, sqrt, ...)");
    }
    case K_ATOM:
        return nfail(w, n, ZL_E_UNSUPPORTED,
                     "an atom in arithmetic (pi, e, inf, random, ... are floats or impure)");
    default:
        return nfail(w, n, ZL_E_UNSUPPORTED, "a string in arithmetic");
    }
}

static int has_cut(pw_t *w, int32_t g)
{
    uint32_t ar;
    int32_t f = functor(w, g, &ar);
    if (f == w->a_cut && ar == 0) return 1;
    if ((f == w->a_comma || f == w->a_semi || f == w->a_arrow) && ar == 2)
        return has_cut(w, arg(w, g, 0)) || has_cut(w, arg(w, g, 1));
    return 0;
}

static int64_t newtemp(pw_t *w)
{
    return 3 + (int64_t) w->cvars + w->ntemp++;
}

/* env[i] := B */
static void save_b(pw_t *w, int64_t i)
{
    pu(w, i);
    callh(w, H_SAVEB);
}

/* B := env[i] */
static void cut_to(pw_t *w, int64_t i)
{
    pu(w, i);
    callh(w, H_CUTTO);
}

/* B := B.previous */
static void pop_cp(pw_t *w)
{
    callh(w, H_POPCP);
}

static void push_cp(pw_t *w, int32_t alt)
{
    pucode(w, alt);
    pu(w, 0);
    callh(w, H_PUSHCP);
}

static void proceed_env(pw_t *w)
{
    jh(w, ZL_JMP, H_PROCEED);
}

enum { SUB_TRUE = -1, SUB_FAIL = -2 };

static int cgoal(pw_t *w, int32_t g, int64_t cut, int tail);

static int c_sub(pw_t *w, int32_t g, int64_t cut, int tail)
{
    if (g == SUB_TRUE) {
        if (tail) proceed_env(w);
        return ok(w);
    }
    if (g == SUB_FAIL) {
        jh(w, ZL_JMP, H_FAIL);
        return ok(w);
    }
    return cgoal(w, g, cut, tail);
}

/* (C -> T ; El): the condition's cut is local to it. */
static int c_ite(pw_t *w, int32_t C, int32_t T, int32_t El, int64_t cut, int tail)
{
    int64_t t1 = newtemp(w), t2 = t1;
    int32_t lelse = newlab(w), lend = newlab(w);
    save_b(w, t1);
    push_cp(w, lelse);
    if (has_cut(w, C)) {
        t2 = newtemp(w);
        save_b(w, t2);
    }
    TRY(cgoal(w, C, t2, 0));
    cut_to(w, t1);
    TRY(c_sub(w, T, cut, tail));
    if (!tail) jl(w, ZL_JMP, lend);
    setlab(w, lelse);
    pop_cp(w);
    TRY(c_sub(w, El, cut, tail));
    setlab(w, lend);
    return ok(w);
}

static int c_disj(pw_t *w, int32_t a, int32_t b, int64_t cut, int tail)
{
    int32_t lb = newlab(w), lend = newlab(w);
    push_cp(w, lb);
    TRY(cgoal(w, a, cut, tail));
    if (!tail) jl(w, ZL_JMP, lend);
    setlab(w, lb);
    pop_cp(w);
    TRY(cgoal(w, b, cut, tail));
    setlab(w, lend);
    return ok(w);
}

/* \+ a (mode 0), a = \= goal (mode 1), forall(a, b) (mode 2). */
static int c_neg(pw_t *w, int32_t a, int32_t b, int mode, int tail)
{
    int64_t t1 = newtemp(w), t2 = t1;
    int32_t lok = newlab(w);
    save_b(w, t1);
    push_cp(w, lok);
    if (mode != 1 && (has_cut(w, a) || (mode == 2 && has_cut(w, b)))) {
        t2 = newtemp(w);
        save_b(w, t2);
    }
    if (mode == 1) {
        TRY(build(w, arg(w, a, 0), 0));
        TRY(build(w, arg(w, a, 1), 0));
        callh(w, H_UNIFY);
        jh(w, ZL_JZ, H_FAIL);
    } else {
        TRY(cgoal(w, a, t2, 0));
        if (mode == 2) TRY(c_neg(w, b, -1, 0, 0));
    }
    cut_to(w, t1);
    jh(w, ZL_JMP, H_FAIL);
    setlab(w, lok);
    pop_cp(w);
    if (tail) proceed_env(w);
    return ok(w);
}

static int c_findall(pw_t *w, int32_t g)
{
    int32_t T = arg(w, g, 0), G = arg(w, g, 1), Lst = arg(w, g, 2), done = newlab(w);
    int64_t ta = newtemp(w), t2 = 2;
    ld(w, w->sE);
    pu(w, ta);
    pu(w, ZL_V_NIL);
    o(w, ZL_VSET);
    push_cp(w, done);
    if (has_cut(w, G)) {
        t2 = newtemp(w);
        save_b(w, t2);
    }
    TRY(cgoal(w, G, t2, 0));
    ld(w, w->sE);
    pu(w, ta);
    TRY(build(w, T, 0));
    callh(w, H_COPY);
    ld(w, w->sE);
    vref(w, ta);
    o(w, ZL_CONS);
    o(w, ZL_VSET);
    jh(w, ZL_JMP, H_FAIL);
    setlab(w, done);
    pop_cp(w);
    TRY(build(w, Lst, 0));
    ld(w, w->sE);
    vref(w, ta);
    callh(w, H_REVERSE);
    callh(w, H_UNIFY);
    jh(w, ZL_JZ, H_FAIL);
    return ok(w);
}

/* Decode a "string" node into w->sbuf; returns its length. */
static size_t decode(pw_t *w, int32_t n)
{
    const char *s = w->usrc + N(w, n)->kid;
    size_t len = (size_t) N(w, n)->v, k = 0;
    for (size_t i = 0; i < len;) {
        char c = s[i];
        if (c == '\\') {
            char e = s[i + 1];
            c = e == 'n' ? '\n' : e == 't' ? '\t' : e;
            i += 2;
        } else if (c == '"') {
            i += 2;
        } else {
            i++;
        }
        w->sbuf[k++] = c;
    }
    return k;
}

/* Literal format text: printable runs with OUTS, newlines with NL. */
static void out_text(pw_t *w, const char *s, size_t n)
{
    size_t a = 0;
    for (size_t i = 0; i <= n; i++) {
        if (i < n && s[i] != '\n') continue;
        if (i > a) E(w, ZL_OUTS, zl_addstr(w->c, s + a, i - a), 0);
        if (i < n) o(w, ZL_NL);
        a = i + 1;
    }
}

static int c_format(pw_t *w, int32_t g, int32_t f, int32_t args)
{
    const char *s;
    size_t n;
    char *seg = w->sbuf + sizeof w->sbuf / 2;
    size_t sn = 0;
    int32_t al[64];
    uint32_t na = 0, ai = 0;
    if (N(w, f)->kind == K_STR) {
        n = decode(w, f);
        s = w->sbuf;
    } else if (N(w, f)->kind == K_ATOM && N(w, f)->v != w->a_nil) {
        s = aname(w, (int32_t) N(w, f)->v);
        n = alen(w, (int32_t) N(w, f)->v);
    } else {
        return nfail(w, f, ZL_E_UNSUPPORTED,
                     "a format control that is not a \"string\" or an atom");
    }
    if (args >= 0) {
        int32_t t = args;
        if (N(w, t)->kind == K_VAR)
            return nfail(w, t, ZL_E_UNSUPPORTED,
                         "format arguments in a variable (write the list in the call)");
        if (N(w, t)->kind == K_CONS || (N(w, t)->kind == K_ATOM && N(w, t)->v == w->a_nil)) {
            while (N(w, t)->kind == K_CONS) {
                if (na >= 64) return nfail(w, args, ZL_E_LIMIT, "more than 64 format arguments");
                al[na++] = N(w, t)->kid;
                t = N(w, N(w, t)->kid)->next;
            }
            if (N(w, t)->kind != K_ATOM || N(w, t)->v != w->a_nil)
                return nfail(w, t, ZL_E_UNSUPPORTED, "format arguments in a partial list");
        } else {
            al[na++] = args;
        }
    }
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (c != '~') {
            seg[sn++] = c;
            continue;
        }
        if (i + 1 >= n) return nfail(w, f, ZL_E_UNSUPPORTED, "a format control ending in ~");
        c = s[++i];
        if (c == '~' || c == 'n') {
            seg[sn++] = c == 'n' ? '\n' : '~';
            continue;
        }
        if (c == 'w' || c == 'p' || c == 'q' || c == 'a' || c == 'd' || c == 'c' || c == 'i') {
            out_text(w, seg, sn);
            sn = 0;
            if (ai >= na) { /* SWI: format("not enough arguments") */
                E(w, ZL_TRAP, ZL_E_DOMAIN, 0);
                return ok(w);
            }
            int32_t x = al[ai++];
            if (c == 'i') continue;
            TRY(build(w, x, 0));
            if (c == 'w' || c == 'p' || c == 'q') {
                pu(w, c != 'w');
                callh(w, H_WRITE);
            } else {
                callh(w, c == 'a' ? H_FMTA : c == 'd' ? H_FMTD : H_FMTC);
            }
            continue;
        }
        if (zl_isdigit(c) || c == '*' || c == '`')
            return nfail(w, f, ZL_E_UNSUPPORTED, "format directives with numeric arguments");
        if (c == 'e' || c == 'f' || c == 'g')
            return nfail(w, f, ZL_E_UNSUPPORTED, "floating-point format directives");
        if (c == 't' || c == '|' || c == '+')
            return nfail(w, f, ZL_E_UNSUPPORTED, "format column stops (~t ~| ~+)");
        return nfail(w, f, ZL_E_UNSUPPORTED,
                     "a format directive other than ~w ~p ~q ~a ~d ~c ~i ~n ~~");
    }
    out_text(w, seg, sn);
    if (ai < na) E(w, ZL_TRAP, ZL_E_DOMAIN, 0); /* SWI: too many arguments */
    (void) g;
    return ok(w);
}

static int c_call(pw_t *w, int32_t g, int32_t f, uint32_t ar, int tail)
{
    int32_t p = find_pred(w, f, ar), ret = -1, j = 0;
    if (p < 0)
        return nfail(w, g, ZL_E_UNDEFINED, "a call to a predicate the program does not define");
    for (int32_t a = N(w, g)->kind == K_COMP ? N(w, g)->kid : -1; a >= 0; a = N(w, a)->next, j++) {
        TRY(build(w, a, 0));
        st(w, w->sA + j);
    }
    if (!tail) {
        ret = newlab(w);
        pucode(w, ret);
        callh(w, H_SETC);
    } else {
        callh(w, H_SETT);
    }
    jl(w, ZL_JMP, w->preds[p].label);
    if (!tail) setlab(w, ret);
    return ok(w);
}

/* between/3 and length/2: built-in predicates entered like a user one */
static int c_native(pw_t *w, int32_t g, uint32_t ar, int h, int tail)
{
    int32_t ret = -1;
    for (uint32_t j = 0; j < ar; j++) {
        TRY(build(w, arg(w, g, j), 0));
        st(w, w->sA + (int32_t) j);
    }
    if (!tail) {
        ret = newlab(w);
        pucode(w, ret);
        callh(w, H_SETC);
    } else {
        callh(w, H_SETT);
    }
    jh(w, ZL_JMP, h);
    if (!tail) setlab(w, ret);
    return ok(w);
}

static int type_test(pw_t *w, int b)
{
    callh(w, H_TCLASS);
    switch (b) {
    case B_VAR:
        pu(w, 0);
        o(w, ZL_EQ);
        break;
    case B_NONVAR:
        pu(w, 0);
        o(w, ZL_NE);
        break;
    case B_ATOM:
        pu(w, 3);
        o(w, ZL_EQ);
        break;
    case B_NUMBER:
    case B_INTEGER:
        pu(w, 4);
        o(w, ZL_EQ);
        break;
    case B_ATOMIC:
        pu(w, 3);
        o(w, ZL_GE);
        break;
    case B_COMPOUND:
        o(w, ZL_DUP);
        pu(w, 1);
        o(w, ZL_EQ);
        o(w, ZL_SWAP);
        pu(w, 2);
        o(w, ZL_EQ);
        o(w, ZL_OR);
        break;
    default: /* callable */
        o(w, ZL_DUP);
        pu(w, 1);
        o(w, ZL_GE);
        o(w, ZL_SWAP);
        pu(w, 3);
        o(w, ZL_LE);
        o(w, ZL_AND);
        break;
    }
    jh(w, ZL_JZ, H_FAIL);
    return ok(w);
}

static int cgoal(pw_t *w, int32_t g, int64_t cut, int tail)
{
    uint32_t ar;
    int32_t f = functor(w, g, &ar);
    w->c->line = N(w, g)->line;
    int b = find_bi(w, f, ar, w->cl_prelude);
    int32_t a0 = ar > 0 ? arg(w, g, 0) : -1, a1 = ar > 1 ? arg(w, g, 1) : -1;
    switch (b) {
    case B_CONJ:
        TRY(cgoal(w, a0, cut, 0));
        return cgoal(w, a1, cut, tail);
    case B_DISJ: {
        uint32_t lar;
        if (functor(w, a0, &lar) == w->a_arrow && lar == 2)
            return c_ite(w, arg(w, a0, 0), arg(w, a0, 1), a1, cut, tail);
        return c_disj(w, a0, a1, cut, tail);
    }
    case B_ITE:
        return c_ite(w, a0, a1, SUB_FAIL, cut, tail);
    case B_NOT:
    case B_NOTW:
        return c_neg(w, a0, -1, 0, tail);
    case B_NUNIFY:
        return c_neg(w, g, -1, 1, tail);
    case B_FORALL:
        return c_neg(w, a0, a1, 2, tail);
    case B_ONCE:
        return c_ite(w, a0, SUB_TRUE, SUB_FAIL, cut, tail);
    case B_IGNORE:
        return c_ite(w, a0, SUB_TRUE, SUB_TRUE, cut, tail);
    case B_FAIL:
    case B_FALSE:
        jh(w, ZL_JMP, H_FAIL);
        return ok(w);
    case B_HALT:
        o(w, ZL_HALT);
        return ok(w);
    case B_ERROR:
        E(w, ZL_TRAP, ZL_E_DOMAIN, 0);
        return ok(w);
    case B_NONE:
        return c_call(w, g, f, ar, tail);
    case B_CUT:
        if (cut == 2)
            callh(w, H_CUTE);
        else
            cut_to(w, cut);
        break;
    case B_TRUE:
        break;
    case B_FINDALL:
        TRY(c_findall(w, g));
        break;
    case B_UNIFY:
    case B_IDENT:
    case B_NIDENT:
        TRY(build(w, a0, 0));
        TRY(build(w, a1, 0));
        callh(w, b == B_UNIFY ? H_UNIFY : H_IDENT);
        jh(w, b == B_NIDENT ? ZL_JNZ : ZL_JZ, H_FAIL);
        break;
    case B_IS:
        TRY(build(w, a0, 0));
        TRY(cexpr(w, a1, 0));
        callh(w, H_UNIFY);
        jh(w, ZL_JZ, H_FAIL);
        break;
    case B_AEQ:
    case B_ANE:
    case B_ALT:
    case B_AGT:
    case B_ALE:
    case B_AGE: {
        static const uint8_t cmp[] = {ZL_EQ, ZL_NE, ZL_LT, ZL_GT, ZL_LE, ZL_GE};
        TRY(cexpr(w, a0, 0));
        TRY(cexpr(w, a1, 0));
        o(w, cmp[b - B_AEQ]);
        jh(w, ZL_JZ, H_FAIL);
        break;
    }
    case B_VAR:
    case B_NONVAR:
    case B_ATOM:
    case B_NUMBER:
    case B_INTEGER:
    case B_ATOMIC:
    case B_COMPOUND:
    case B_CALLABLE:
        TRY(build(w, a0, 0));
        TRY(type_test(w, b));
        break;
    case B_ISLIST:
        TRY(build(w, a0, 0));
        callh(w, H_ISLIST);
        jh(w, ZL_JZ, H_FAIL);
        break;
    case B_WRITE:
    case B_PRINT:
    case B_WRITEQ:
    case B_WRITELN:
        TRY(build(w, a0, 0));
        pu(w, b == B_PRINT || b == B_WRITEQ);
        callh(w, H_WRITE);
        if (b == B_WRITELN) o(w, ZL_NL);
        break;
    case B_NL:
        o(w, ZL_NL);
        break;
    case B_TAB:
        TRY(cexpr(w, a0, 0));
        callh(w, H_TAB);
        break;
    case B_FORMAT1:
        TRY(c_format(w, g, a0, -1));
        break;
    case B_FORMAT2:
        TRY(c_format(w, g, a0, a1));
        break;
    case B_BETWEEN:
    case B_LENGTH:
        return c_native(w, g, ar, b == B_BETWEEN ? H_BETWEEN : H_LENGTH, tail);
    default:
        return nfail(w, g, ZL_E_UNSUPPORTED, "a goal outside the Prolog subset");
    }
    if (tail) proceed_env(w);
    return ok(w);
}

static int c_clause(pw_t *w, pclause_t *cl, int32_t lab)
{
    int32_t h = cl->head, size_at = -1, j = 0;
    uint32_t nv = cl->nvars;
    int need = cl->body >= 0;
    w->c->line = N(w, h >= 0 ? h : cl->body)->line;
    w->cl_prelude = cl->prelude;
    for (uint32_t k = 0; k < nv; k++) w->dir[k] = -1;
    if (h >= 0 && N(w, h)->kind == K_COMP)
        for (int32_t a = N(w, h)->kid; a >= 0; a = N(w, a)->next, j++) {
            int k = N(w, a)->kind == K_VAR ? var_index(w, a) : -1;
            if (k >= 0 && w->dir[k] < 0) w->dir[k] = j;
        }
    for (uint32_t k = 0; k < nv; k++)
        if (w->dir[k] < 0) need = 1;
    w->noenv = !need;
    w->cvars = nv;
    w->ntemp = 0;
    setlab(w, lab);
    if (need) {
        /* the cells: variables that arrive in an argument, then fresh ones */
        int16_t nd = 0, nf = 0;
        for (uint32_t k = 0; k < nv; k++)
            if (w->dir[k] >= 0) w->vslot[k] = (int16_t) (3 + nd++);
        for (uint32_t k = 0; k < nv; k++)
            if (w->dir[k] < 0) w->vslot[k] = (int16_t) (3 + nd + nf++);
        size_at = E(w, ZL_PUSH, 0, 0);
        callh(w, H_ALLOC);
        for (uint32_t k = 0; k < nv; k++) {
            if (w->dir[k] < 0) continue;
            ld(w, w->sE);
            pu(w, w->vslot[k]);
            ld(w, w->sA + w->dir[k]);
            o(w, ZL_VSET);
        }
        if (nf == 1) {
            ld(w, w->sE);
            pu(w, 3 + nd);
            callh(w, H_NEWVAR);
            o(w, ZL_VSET);
        } else if (nf > 1) {
            pu(w, 3 + nd);
            pu(w, nf);
            callh(w, H_NEWVARS);
        }
    }
    j = 0;
    if (h >= 0 && N(w, h)->kind == K_COMP)
        for (int32_t a = N(w, h)->kid; a >= 0; a = N(w, a)->next, j++) {
            if (N(w, a)->kind == K_VAR) {
                int k = var_index(w, a);
                if (k < 0 || w->dir[k] == j) continue;
            }
            ld(w, w->sA + j);
            TRY(build(w, a, 0));
            callh(w, H_UNIFY);
            jh(w, ZL_JZ, H_FAIL);
        }
    if (cl->body >= 0) {
        TRY(cgoal(w, cl->body, 2, 1));
    } else {
        jh(w, ZL_JMP, H_PROCF);
    }
    if (size_at >= 0) zl_patch(w->c, size_at, 3 + (int64_t) nv + w->ntemp);
    return ok(w);
}

/* First-argument key of a clause; 0 when it matches anything. */
static int ckey(pw_t *w, int32_t ci, int64_t *key)
{
    int32_t a = N(w, w->clauses[ci].head)->kid;
    switch (N(w, a)->kind) {
    case K_INT:
        *key = N(w, a)->v;
        return 1;
    case K_ATOM:
    case K_COMP:
        *key = aval(w, (int32_t) N(w, a)->v);
        return 1;
    case K_CONS:
        *key = ZL_V_FALSE;
        return 1;
    default:
        return 0;
    }
}

/* A predicate: one choicepoint while clauses remain whose first argument
 * can match; K_k tries clause k, N_k finds the first candidate from k. */
static int c_pred(pw_t *w, int32_t pi)
{
    ppred_t *p = &w->preds[pi];
    uint32_t n = p->nclauses, ar = p->arity;
    int keyed = 0;
    int64_t key;
    int32_t ci, k0, n0, c0, pf;
    w->c->line = N(w, w->clauses[p->first].head)->line;
    if (n == 1) return c_clause(w, &w->clauses[p->first], p->label);
    for (ci = p->first; ci >= 0 && ar; ci = w->clauses[ci].next)
        if (ckey(w, ci, &key)) keyed = 1;
    k0 = (int32_t) w->nlabels;
    for (uint32_t i = 0; i < 3 * n + 1; i++) newlab(w);
    if (w->c->failed) return ok(w);
    n0 = k0 + (int32_t) n;
    c0 = n0 + (int32_t) n + 1;
    pf = newlab(w);
    setlab(w, p->label);
    pu(w, 0);
    pu(w, ar);
    callh(w, H_PUSHCP);
    if (keyed) callh(w, H_SETKEY);
    jl(w, ZL_CALL, n0);
    o(w, ZL_DUP);
    jl(w, ZL_JZ, pf);
    o(w, ZL_JMPI);
    setlab(w, pf);
    o(w, ZL_DROP);
    pop_cp(w);
    jh(w, ZL_JMP, H_FAIL);
    for (uint32_t k = 0; k < n; k++) {
        setlab(w, k0 + (int32_t) k);
        if (k + 1 < n) {
            if (keyed) callh(w, H_SETKEY);
            jl(w, ZL_CALL, n0 + (int32_t) k + 1);
            callh(w, H_TRYNEXT);
        } else {
            pop_cp(w);
        }
        jl(w, ZL_JMP, c0 + (int32_t) k);
    }
    ci = p->first;
    for (uint32_t k = 0; k < n; k++, ci = w->clauses[ci].next) {
        setlab(w, n0 + (int32_t) k);
        if (keyed && ckey(w, ci, &key)) {
            pu(w, key);
            callh(w, H_KEYIS);
            jl(w, ZL_JZ, n0 + (int32_t) k + 1);
        }
        pucode(w, k0 + (int32_t) k);
        o(w, ZL_RET);
    }
    setlab(w, n0 + (int32_t) n);
    pu(w, 0);
    o(w, ZL_RET);
    ci = p->first;
    for (uint32_t k = 0; k < n; k++, ci = w->clauses[ci].next)
        TRY(c_clause(w, &w->clauses[ci], c0 + (int32_t) k));
    return ok(w);
}

static int gen(pw_t *w)
{
    zl_cc_t *c = w->c;
    zl_prog_t *p = c->p;
    uint32_t na = 4; /* between/3 and length/2 use four */
    int32_t lstart;
    for (uint32_t i = 0; i < w->npreds; i++)
        if (w->preds[i].arity > na) na = w->preds[i].arity;
    w->nA = (int32_t) na;
    w->sA = zl_slots(c, na);
    w->sE = zl_slots(c, 1);
    w->sCPC = zl_slots(c, 1);
    w->sCPE = zl_slots(c, 1);
    w->sB = zl_slots(c, 1);
    w->sB0 = zl_slots(c, 1);
    w->sTR = zl_slots(c, 1);
    w->sVC = zl_slots(c, 1);
    w->sFAILK = zl_slots(c, 1);
    w->sLASTC = zl_slots(c, 1);
    w->sQ = zl_slots(c, 1);
    w->sKEY = zl_slots(c, 1);
    w->sMAP = zl_slots(c, 1);
    w->sT = zl_slots(c, 10);
    w->sCT = zl_slots(c, 7);
    if (c->failed) return ok(w);
    c->line = 0;
    lstart = newlab(w);
    jl(w, ZL_JMP, lstart);
    for (uint32_t i = 0; i < w->npreds; i++) w->preds[i].label = newlab(w);
    for (uint32_t i = 0; i < w->nclauses; i++)
        if (w->clauses[i].pred < 0) w->clauses[i].lab = newlab(w);
    for (uint32_t i = 0; i < w->npreds; i++) TRY(c_pred(w, (int32_t) i));
    for (uint32_t i = 0; i < w->nclauses; i++)
        if (w->clauses[i].pred < 0) TRY(c_clause(w, &w->clauses[i], w->clauses[i].lab));
    c->line = 0;
    emit_helpers(w);
    setlab(w, lstart);
    {
        static const int32_t nil_regs = 6;
        int32_t regs[6];
        regs[0] = w->sB;
        regs[1] = w->sB0;
        regs[2] = w->sTR;
        regs[3] = w->sE;
        regs[4] = w->sCPE;
        regs[5] = w->sMAP;
        for (int32_t k = 0; k < nil_regs; k++) {
            pu(w, ZL_V_NIL);
            st(w, regs[k]);
        }
    }
    pu(w, 0);
    st(w, w->sVC);
    pu(w, -1);
    st(w, w->sLASTC);
    if (w->hdone[H_CLS])
        for (int k = 0; k < 7; k++) {
            pu(w, ct_word(k));
            st(w, w->sCT + k);
        }
    for (uint32_t i = 0; i < w->nitems; i++) {
        if (w->items[i].kind != I_INIT) continue;
        pclause_t *cl = &w->clauses[w->items[i].ref];
        int32_t next = newlab(w);
        c->line = N(w, cl->body)->line;
        pu(w, ZL_V_NIL);
        st(w, w->sB);
        pu(w, ZL_V_NIL);
        st(w, w->sTR);
        pu(w, ZL_V_NIL);
        st(w, w->sB0);
        pu(w, ZL_V_NIL);
        st(w, w->sCPE);
        pucode(w, next);
        o(w, ZL_DUP);
        st(w, w->sFAILK);
        st(w, w->sCPC);
        jl(w, ZL_JMP, cl->lab);
        setlab(w, next);
    }
    c->line = 0;
    o(w, ZL_HALT);
    if (c->failed) return ok(w);
    for (uint32_t i = 0; i < p->ncode; i++) {
        zl_ins_t *in = &p->code[i];
        int64_t l = -1;
        if (in->op == ZL_JMP || in->op == ZL_JZ || in->op == ZL_JNZ || in->op == ZL_CALL)
            l = in->a;
        else if (in->op == ZL_PUSH && in->a >= ZL_V_CODE &&
                 in->a < ZL_V_CODE + (int64_t) w->nlabels)
            l = in->a - ZL_V_CODE;
        else
            continue;
        if (l < 0 || l >= (int64_t) w->nlabels || w->labels[l] < 0)
            return zl_fail(c, ZL_E_VERIFY, "internal: a label was never placed");
        in->a = in->op == ZL_PUSH ? ZL_V_CODE + w->labels[l] : w->labels[l];
    }
    return ok(w);
}

/* ------------------------------------------------------------ twin */

static void tw_quoted(pw_t *w, const char *s, size_t n, char q)
{
    zl_putc(w->tw, q);
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (c == q || c == '\\') {
            zl_putc(w->tw, '\\');
            zl_putc(w->tw, c);
        } else if (c == '\n') {
            zl_puts(w->tw, "\\n");
        } else if (c == '\t') {
            zl_puts(w->tw, "\\t");
        } else {
            zl_putc(w->tw, c);
        }
    }
    zl_putc(w->tw, q);
}

static void tw_atom(pw_t *w, int32_t a)
{
    if (w->atoms[a].type == AT_QUOTE)
        tw_quoted(w, aname(w, a), alen(w, a), '\'');
    else
        zl_put(w->tw, aname(w, a), alen(w, a));
}

static void tw_term(pw_t *w, int32_t n, int prec, int oparg)
{
    pnode_t *x = N(w, n);
    int32_t f = (int32_t) x->v;
    switch (x->kind) {
    case K_INT:
        zl_puti(w->tw, x->v);
        return;
    case K_VAR:
        zl_put(w->tw, w->usrc + x->kid, (size_t) (x->v >> 32));
        return;
    case K_STR:
        tw_quoted(w, w->sbuf, decode(w, n), '"');
        return;
    case K_ATOM:
        if (oparg && (w->atoms[f].pre || w->atoms[f].in)) {
            zl_putc(w->tw, '(');
            tw_atom(w, f);
            zl_putc(w->tw, ')');
        } else {
            tw_atom(w, f);
        }
        return;
    case K_CONS: {
        int32_t t = n;
        zl_putc(w->tw, '[');
        for (;;) {
            tw_term(w, N(w, t)->kid, 999, 0);
            t = N(w, N(w, t)->kid)->next;
            if (N(w, t)->kind == K_CONS) {
                zl_puts(w->tw, ", ");
                continue;
            }
            if (N(w, t)->kind != K_ATOM || N(w, t)->v != w->a_nil) {
                zl_putc(w->tw, '|');
                tw_term(w, t, 999, 0);
            }
            break;
        }
        zl_putc(w->tw, ']');
        return;
    }
    default:
        break;
    }
    /* compounds */
    patom_t *A = &w->atoms[f];
    uint32_t ar = arity(w, n);
    if (f == w->a_curly && ar == 1) {
        zl_putc(w->tw, '{');
        tw_term(w, x->kid, 1200, 0);
        zl_putc(w->tw, '}');
        return;
    }
    if (ar == 1 && A->pre) {
        int pri = A->pre;
        if (pri > prec) zl_putc(w->tw, '(');
        tw_atom(w, f);
        zl_putc(w->tw, ' ');
        tw_term(w, x->kid, A->pret ? pri : pri - 1, 1);
        if (pri > prec) zl_putc(w->tw, ')');
        return;
    }
    if (ar == 2 && A->in && f != w->a_bar) {
        int pri = A->in;
        if (pri > prec) zl_putc(w->tw, '(');
        tw_term(w, x->kid, A->int_ == 2 ? pri : pri - 1, 1);
        if (f == w->a_comma) {
            zl_puts(w->tw, ", ");
        } else {
            zl_putc(w->tw, ' ');
            tw_atom(w, f);
            zl_putc(w->tw, ' ');
        }
        tw_term(w, N(w, x->kid)->next, A->int_ == 1 ? pri : pri - 1, 1);
        if (pri > prec) zl_putc(w->tw, ')');
        return;
    }
    tw_atom(w, f);
    zl_putc(w->tw, '(');
    for (int32_t a = x->kid; a >= 0; a = N(w, a)->next) {
        tw_term(w, a, 999, 0);
        if (N(w, a)->next >= 0) zl_puts(w->tw, ", ");
    }
    zl_putc(w->tw, ')');
}

/* The goals of a body, one a line; conjunctions flattened. */
static void tw_goals(pw_t *w, int32_t g, int last)
{
    uint32_t ar;
    if (functor(w, g, &ar) == w->a_comma && ar == 2) {
        tw_goals(w, N(w, g)->kid, 0);
        tw_goals(w, N(w, N(w, g)->kid)->next, last);
        return;
    }
    zl_puts(w->tw, "    ");
    tw_term(w, g, 999, 0);
    zl_puts(w->tw, last ? ".\n" : ",\n");
}

static void print_twin(pw_t *w)
{
    int prev = 0;
    int32_t prev_pred = -1;
    for (uint32_t i = 0; i < w->nitems; i++) {
        pitem_t *it = &w->items[i];
        if (it->kind == I_COMMENT) {
            pnode_t *x = N(w, it->ref);
            if (prev && prev != I_COMMENT) zl_putc(w->tw, '\n');
            zl_putc(w->tw, '%');
            if (x->v) {
                zl_putc(w->tw, ' ');
                zl_put(w->tw, w->usrc + x->kid, (size_t) x->v);
            }
            zl_putc(w->tw, '\n');
            prev = I_COMMENT;
            continue;
        }
        pclause_t *cl = &w->clauses[it->ref];
        if (prev && prev != I_COMMENT &&
            (it->kind == I_INIT || prev == I_INIT || cl->pred != prev_pred))
            zl_putc(w->tw, '\n');
        if (it->kind == I_INIT) {
            zl_puts(w->tw, ":- initialization(");
            tw_term(w, cl->body, 999, 0);
            zl_puts(w->tw, ").\n");
        } else {
            tw_term(w, cl->head, 1199, 0);
            if (cl->body >= 0) {
                zl_puts(w->tw, " :-\n");
                tw_goals(w, cl->body, 1);
            } else {
                zl_puts(w->tw, ".\n");
            }
        }
        prev = it->kind;
        prev_pred = cl->pred;
    }
}

/* ------------------------------------------------------------ entry */

int zl_prolog_compile(const char *src, size_t len, zl_cc_t *c, zl_out_t *tw, void *work)
{
    pw_t *w = (pw_t *) work;
    zl_bzero(w, offsetof(pw_t, vars));
    w->c = c;
    w->tw = tw;
    w->usrc = src;
    w->ulen = len;
    c->p->word_bits = 62;
    w->a_nil = atom_s(w, "[]");
    w->a_comma = atom_s(w, ",");
    w->a_semi = atom_s(w, ";");
    w->a_arrow = atom_s(w, "->");
    w->a_neck = atom_s(w, ":-");
    w->a_not = atom_s(w, "\\+");
    w->a_cut = atom_s(w, "!");
    w->a_curly = atom_s(w, "{}");
    w->a_minus = atom_s(w, "-");
    w->a_bar = atom_s(w, "|");
    w->a_dcg = atom_s(w, "-->");
    w->a_init = atom_s(w, "initialization");
    TRY(collect(w));
    TRY(gen(w));
    if (tw) print_twin(w);
    return ok(w);
}
