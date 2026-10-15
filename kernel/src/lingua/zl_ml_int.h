/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zl_ml_int.h — what the ML family front-ends share: the syntax tree, the
 * Hindley-Milner type checker and the code generator of zl_ml.c. Standard
 * ML and OCaml are parsed in zl_ml.c itself; Haskell (zl_haskell.c) has its
 * own lexer (the layout rule), parser and twin printer and builds the same
 * tree, then hands it to zl_ml_finish. Private to the module. */
#ifndef ZL_ML_INT_H
#define ZL_ML_INT_H

#include "zl_int.h"

#define M_NODES  2400
#define M_TYPES  6000
#define M_BINDS  1024
#define M_FNS    256
#define M_PTYS   1024
#define M_SCOPE  768
#define M_FIX    1536
#define M_COMS   256
#define M_TOPS   512
#define M_DEPTH  96 /* nesting of expressions and patterns */
#define M_GROUPS 16
#define M_PIECES 64
#define M_IMAP   128
#define M_PATH   48
#define M_STR    256

/* dialects: D_HS evaluates left to right as D_SML does */
enum { D_SML = 0, D_OCAML = 1, D_HS = 2 };

typedef struct {
    int k;
    size_t at;
    uint32_t len, line, col;
    int64_t v; /* T_INT: SML the value, OCaml the magnitude */
    int big;   /* T_INT: magnitude past 2^62 */
} mtok_t;

/* ------------------------------------------------------------ the tree */

enum {
    E_INT = 1,
    E_BOOL,
    E_UNIT,
    E_NIL,
    E_VAR,
    E_STR,
    E_LIST,
    E_TUPLE,
    E_BIN,
    E_NEG,
    E_APP,
    E_IF,
    E_CASE,
    E_LET, /* a: declarations; b: body. Haskell: op 0 let, 1 where, 2 do let;
              a is then in dependency order and c the source order (N_ITEM) */
    E_SEQ, /* Haskell: a do block (F_DO) */
    P_WILD,
    P_VAR,
    P_INT,
    P_BOOL,
    P_UNIT,
    P_NIL,
    P_CONS,
    P_LIST,
    P_TUPLE,
    D_VAL, /* Haskell: c is the type signature, if any */
    D_FUN,
    D_EXP,
    N_FB, /* Haskell: c is the type signature, if any */
    N_CLAUSE,
    N_RULE,
    N_COMMENT,
    /* Haskell */
    E_GUARDS, /* a: N_GALT list (a guard, b body); falls through when all fail */
    N_GALT,
    E_RANGE,  /* [a .. b] or, with c, [a, c .. b] */
    N_SIG,    /* a: P_VAR names; b: the type */
    N_TYPE,   /* op: TK_*; a, b: parts; names: v, len */
    N_ITEM    /* a: the item in source order */
};

/* N_TYPE kinds */
enum { TK_INT = 1, TK_BOOL, TK_UNIT, TK_IO, TK_LIST, TK_FUN, TK_TUPLE, TK_VAR };

enum {
    O_ADD = 1,
    O_SUB,
    O_MUL,
    O_DIV,
    O_MOD,
    O_EQ,
    O_NE,
    O_LT,
    O_LE,
    O_GT,
    O_GE,
    O_ANDALSO,
    O_ORELSE,
    O_CONS,
    O_APPEND,
    O_CAT,
    /* Haskell */
    O_QUOT,
    O_REM,
    O_POW,
    O_INDEX
};

/* node flags */
enum {
    F_FUNCTION = 1, /* N_FB: OCaml "let f = function ..." */
    F_REC = 2,      /* D_FUN: recursive (SML fun, OCaml let rec) */
    F_TOP = 4,      /* D_EXP / D_VAL from a top-level expression */
    F_PRINTED = 8,  /* E_STR: inside a print argument */
    F_SEQPAREN = 16,
    F_STMT = 32,  /* Haskell: where an IO action runs (statement position) */
    F_LAZY = 64,  /* Haskell D_VAL: evaluated when first used (call by need) */
    F_DO = 128    /* Haskell E_SEQ: a do block */
};

typedef struct {
    int64_t v;       /* literal value; names: source offset */
    int32_t a, b, c; /* children */
    int32_t next;    /* next sibling */
    int32_t ty;      /* type term */
    int32_t sym;     /* binding, function or builtin (-id) */
    uint32_t line;
    uint16_t col, len;
    uint8_t kind, op, flags, pad;
} mnode_t;

/* ------------------------------------------------------------ types */

enum { TY_VAR = 0, TY_INT, TY_BOOL, TY_UNIT, TY_LIST, TY_IO };
enum { TINT = 0, TBOOL = 1, TUNIT = 2, TIO = 3 };

typedef struct {
    uint8_t k, gen;
    uint16_t lvl;
    int32_t a; /* TY_VAR: link or -1; TY_LIST: element */
} mty_t;

enum { BK_VAR = 1, BK_FN, BK_LAZY /* Haskell: a thunk (fn) with value cell addr, flag addr + 1 */ };

typedef struct {
    uint32_t pos;
    uint16_t len;
    uint8_t kind, gen;
    int32_t ty, fn, addr;
    uint8_t level, pad[3];
} mbind_t;

typedef struct {
    int32_t fb, bind;
    int32_t p0, rty; /* parameter types: ptys[p0 .. p0+np) */
    uint16_t np, ng;
    uint8_t gar[M_GROUPS];
    uint8_t level, gen, defined, pad;
    int32_t entry, enter_at, start;
    uint32_t nloc;
} mfn_t;

/* builtins */
enum {
    B_PRINT = 1,
    B_ITOS,
    B_BTOS,
    B_ABS,
    B_NOT,
    B_MIN,
    B_MAX,
    B_QUOT,
    B_REM,
    B_HD,
    B_TL,
    B_NULL,
    B_LENGTH,
    B_REV,
    B_PRINT_INT,
    B_PRINT_STRING,
    B_PRINT_NEWLINE,
    B_PRINT_ENDLINE,
    B_PRINTF,
    B_SUCC,
    B_PRED,
    B_MAXINT,
    B_MININT,
    /* Haskell */
    B_HPRINT,
    B_SHOW,
    B_RETURN,
    B_SIGNUM,
    B_NEGATE,
    B_EVEN,
    B_ODD,
    B_GCD,
    B_LCM,
    B_SUM,
    B_PRODUCT,
    B_MAXIMUM,
    B_MINIMUM,
    B_TAKE,
    B_DROP,
    B_ELEM,
    B_NOTELEM,
    B_REPLICATE,
    B_LAST,
    B_INIT,
    B_CONCAT,
    B_AND,
    B_OR,
    B_OTHERWISE,
    B_DIVOP, /* div mod quot rem, when not given two arguments */
    B__COUNT
};

enum {
    H_SMLINT = 0,
    H_MUL63,
    H_EQ,
    H_CMP,
    H_LEN,
    H_REV,
    H_REVAPP,
    H_APPEND,
    /* Haskell */
    H_SUM,
    H_PRODUCT,
    H_MAXIMUM,
    H_MINIMUM,
    H_TAKE,
    H_DROP,
    H_ELEM,
    H_REPLICATE,
    H_LAST,
    H_INIT,
    H_CONCAT,
    H_AND,
    H_OR,
    H_RANGE,
    H_RANGE3,
    H_INDEX,
    H_SHOWLI,
    H_SHOWLB,
    H_GCD,
    H_LCM,
    H__COUNT
};

typedef struct {
    int32_t at, fn; /* fn < 0: helper -fn-1 */
} mfix_t;

typedef struct {
    size_t at; /* inner text, trimmed */
    uint32_t len, line;
} mcom_t;

typedef struct {
    size_t pos, lstart;
    uint32_t line;
} mlex_t;

typedef struct {
    zl_cc_t *c;
    zl_out_t *tw;
    const char *src;
    size_t len;
    int dial;
    mlex_t lx;
    mtok_t tok;
    size_t last_end; /* end of the last token taken */
    uint32_t depth;
    int lists; /* the program uses lists: 62-bit words */
    uint32_t nn, nty, nb, nf, npty, nsc, nfix, ncom, ntops, ncomuse;
    int32_t tl; /* type level */
    /* code generation */
    int32_t curfn;
    uint32_t lvl;
    int32_t helper[H__COUNT];
    uint8_t hused[H__COUNT];
    int32_t scratch; /* four slots for H_MUL63 */
    mnode_t nodes[M_NODES];
    mty_t tys[M_TYPES];
    mbind_t binds[M_BINDS];
    mfn_t fns[M_FNS];
    int32_t ptys[M_PTYS];
    int32_t scope[M_SCOPE];
    mfix_t fix[M_FIX];
    mcom_t coms[M_COMS];
    int32_t tops[M_TOPS];
    int32_t imap[M_IMAP][2];
    int32_t rig[M_IMAP], rign[M_IMAP]; /* Haskell signatures: rigid type variables and their names */
    uint32_t nrig;
    char sbuf[M_STR];
} mw_t;

ZL_STATIC_ASSERT(sizeof(mw_t) <= sizeof(((zl_work_t *) 0)->fe), mw_fits);

#define TRY(x)                                                                                     \
    do {                                                                                           \
        int s_ = (x);                                                                              \
        if (s_ < 0) return s_;                                                                     \
    } while (0)
#define N(w, i) (&(w)->nodes[i])

/* zl_ml.c, for the Haskell front-end */
/* Clear the shared state for a compile into c (the twin goes to tw). */
void zl_ml_begin(mw_t *w, zl_cc_t *c, zl_out_t *tw, const char *src, size_t len, int dial);
/* A new node at line and column, or a negative status (LIMIT recorded). */
int32_t zl_ml_node(mw_t *w, int kind, uint32_t line, uint32_t col);
/* Record the first failure at line and column; returns st. */
int zl_ml_fail(mw_t *w, int st, uint32_t line, uint32_t col, const char *msg);
/* Is the name at src[at..at+len) one this subset knows from the Haskell Prelude? */
int zl_ml_prelude(mw_t *w, size_t at, uint32_t len);
/* Type-check and compile the declarations in w->tops. */
int zl_ml_finish(mw_t *w);

#endif /* ZL_ML_INT_H */
