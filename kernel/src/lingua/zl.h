/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zl.h — Lingua: one shared intermediate form for every language ZXV runs.
 *
 * Each source language has a front-end that compiles a program into the
 * Lingua IR (zl_prog_t), a small stack machine over exact integers. One
 * checked runtime (zl_run) executes the IR for every language, so every
 * language gets the same guarantees at once: fuel-metered, bounded memory,
 * no floating point, overflow and division faults trapped, never wrapped.
 *
 * Each language also has an AI twin: its canonical spelling. The twin is
 * the language itself with exactly one way to write each construct
 * (upper-case keywords, single spaces, minimal parentheses, one statement
 * per line), so a model that writes the twin needs a far smaller grammar
 * mask, and a person who knows the language reads it without training.
 *
 *   zl_twin(lang, src)       canonical twin text of a program
 *   zl_compile(twin, ...)    accepted only if the text is already canonical
 *
 * The twin and the original compile to byte-identical IR (zl_prog_equal),
 * so a program written by a model and one written by a person in the 1960s
 * are checked to be the same program, not merely similar.
 *
 * GUARANTEES (each tested in test_zl.c and fuzzed in fuzz_zl.c)
 *   L1  Freestanding: no libc, no allocation, no floating point. The caller
 *       owns every byte (zl_prog_t, zl_work_t, the output buffer).
 *       Recursion is bounded by ZL_MAX_DEPTH (expression nesting).
 *   L2  Fail closed: an input a front-end does not admit is refused with a
 *       negative status, a line and column, and a reason. Constructs outside
 *       the subset (floating-point literals, string variables, ...) are
 *       refused by name, never guessed.
 *   L3  Every compiled program passes zl_verify, and zl_run re-checks every
 *       stack access, slot index, jump and call at run time.
 *   L4  Exact: integer overflow, division by zero, inexact division where a
 *       language's division is real-valued, and array bounds all trap with
 *       a status; nothing wraps or rounds.
 *   L5  Twin round trip: zl_twin(twin(x)) == twin(x) byte for byte, and
 *       twin(x) compiles to IR equal to x's.
 *   L6  Deterministic: one program, one input, one fuel budget give one
 *       output and one status on every machine.
 */
#ifndef ZL_H
#define ZL_H

#include <stddef.h>
#include <stdint.h>

/* ---- status codes: lingua owns -2601 .. -2699 ---- */
typedef enum {
    ZL_OK = 0,
    ZL_E_ARG = -2601,         /* NULL pointer or unknown language */
    ZL_E_SYNTAX = -2602,      /* the front-end's grammar does not admit it */
    ZL_E_UNSUPPORTED = -2603, /* valid in the language, outside the subset */
    ZL_E_NONCANON = -2604,    /* twin input that is not in canonical form */
    ZL_E_LIMIT = -2605,       /* a fixed capacity (code, slots, names) is full */
    ZL_E_UNDEFINED = -2606,   /* jump to a missing label, unknown word */
    ZL_E_VERIFY = -2607,      /* zl_verify rejected the IR */
    /* run-time traps */
    ZL_E_OVERFLOW = -2620, /* result outside the program's integer width */
    ZL_E_DIVZERO = -2621,
    ZL_E_INEXACT = -2622, /* real division or power with a remainder */
    ZL_E_BOUNDS = -2623,  /* array index or address out of range */
    ZL_E_STACK = -2624,   /* data stack underflow or overflow */
    ZL_E_RSTACK = -2625,  /* call depth exceeded or RETURN without GOSUB */
    ZL_E_FUEL = -2626,    /* instruction budget spent */
    ZL_E_OUTPUT = -2627,  /* output buffer full */
    ZL_E_INPUT = -2628,   /* input list exhausted */
    ZL_E_DOMAIN = -2629   /* e.g. a zero DO step, a negative power */
} zl_status_t;

/* ---- languages ---- */
typedef enum {
    ZL_LANG_FORTRAN = 1, /* FORTRAN 77 fixed form, INTEGER subset (1957 lineage) */
    ZL_LANG_BASIC = 2,   /* ECMA-55 Minimal BASIC, integer subset (1964 lineage) */
    ZL_LANG_FORTH = 3,   /* Forth-2012 core subset (1970 lineage) */
    ZL_LANG__COUNT = 4
} zl_lang_t;

/* A twin is selected by OR-ing this flag into the language. */
#define ZL_TWIN 0x100

const char *zl_lang_name(int lang); /* "FORTRAN", "BASIC", "Forth" */
const char *zl_twin_name(int lang); /* "Reckonwyrm", "Hatchling", "Cairndrake" */
const char *zl_status_name(int status);

/* ---- capacities (fixed; a program that needs more is refused) ---- */
#define ZL_MAX_CODE     8192 /* IR instructions */
#define ZL_MAX_SLOTS    4096 /* integer memory cells (variables + arrays) */
#define ZL_MAX_STR      256  /* string literals */
#define ZL_MAX_STRBYTES 8192
#define ZL_MAX_STACK    256 /* data stack depth */
#define ZL_MAX_CALLS    64  /* call (GOSUB / word) depth */
#define ZL_MAX_DEPTH    48  /* expression and control nesting */
#define ZL_MAX_LINE     512 /* longest source line or statement */

/* ---- the IR ---- */
typedef enum {
    ZL_HALT = 0,
    ZL_PUSH,   /* a: value */
    ZL_LOAD,   /* a: slot */
    ZL_STORE,  /* a: slot                       (pops value) */
    ZL_LOADX,  /* a: base, b: length            (pops index; checks 0<=i<b) */
    ZL_STOREX, /* a: base, b: length            (pops value, then index) */
    ZL_ADD,
    ZL_SUB,
    ZL_MUL,
    ZL_DIVT,
    ZL_MODT, /* truncating (FORTRAN, C) */
    ZL_DIVF,
    ZL_MODF, /* floored (Forth as gforth does it) */
    ZL_DIVX, /* exact: traps ZL_E_INEXACT on a remainder (BASIC) */
    ZL_POW,  /* a: 0 truncating negative exponents, 1 exact */
    ZL_NEG,
    ZL_ABS,
    ZL_SGN,
    ZL_MIN,
    ZL_MAX,
    ZL_EQ,
    ZL_NE,
    ZL_LT,
    ZL_LE,
    ZL_GT,
    ZL_GE, /* push 1 or 0 */
    ZL_AND,
    ZL_OR,
    ZL_XOR,
    ZL_INVERT, /* bitwise */
    ZL_NOT,    /* logical: 0 -> 1, otherwise 0 */
    ZL_DUP,
    ZL_DROP,
    ZL_SWAP,
    ZL_OVER,
    ZL_ROT,
    ZL_JMP,  /* a: target */
    ZL_JZ,   /* a: target                     (pops; jumps when zero) */
    ZL_JNZ,  /* a: target                     (pops; jumps when not zero) */
    ZL_CALL, /* a: target */
    ZL_RET,
    ZL_OUTI,   /* pops; a: format flags, b: minimum width */
    ZL_OUTS,   /* a: string index */
    ZL_OUTC,   /* pops a byte 0..255 */
    ZL_NL,     /* newline */
    ZL_TAB,    /* a: zone width: advance to the start of the next zone */
    ZL_IN,     /* pushes the next input value */
    ZL_LOADA,  /* pops an address (slot); checks it is < nslots */
    ZL_STOREA, /* pops an address, then a value */
    ZL_TRAP,   /* a: status to stop with (e.g. STOP with a code) */
    ZL_OP__COUNT
} zl_op_t;

/* ZL_OUTI flags */
#define ZL_FMT_SIGNSPACE 1u /* a space before a non-negative number */
#define ZL_FMT_TRAIL     2u /* a space after the number */

typedef struct {
    uint8_t op;
    uint8_t pad[3];
    int32_t b;
    int64_t a;
    uint32_t line; /* source line, for traps and listings */
    uint32_t pad2;
} zl_ins_t;

typedef struct {
    int lang;           /* ZL_LANG_* (never with ZL_TWIN) */
    uint32_t word_bits; /* 32 or 64: integer width checked on every result */
    uint32_t ncode, nslots, nstr, strbytes;
    zl_ins_t code[ZL_MAX_CODE];
    uint32_t str_off[ZL_MAX_STR], str_len[ZL_MAX_STR];
    char str[ZL_MAX_STRBYTES];
} zl_prog_t;

typedef struct {
    int status;      /* ZL_OK or a negative zl_status_t */
    uint32_t line;   /* 1-based source line, 0 if not tied to one */
    uint32_t col;    /* 1-based column, 0 if not known */
    const char *msg; /* static English text */
} zl_diag_t;

/* Scratch space for compiling. The caller provides it so nothing is
 * allocated (about 470 KB); the fields are private to the module. */
typedef struct {
    zl_prog_t tmp;      /* zl_twin compiles here */
    uint64_t fe[32768]; /* front-end tables */
} zl_work_t;

/* Compile source text in `lang` (or `lang | ZL_TWIN` to accept only the
 * canonical twin spelling). On failure `p` is left zeroed and `d` says why. */
int zl_compile(int lang, const char *src, size_t len, zl_prog_t *p, zl_work_t *w, zl_diag_t *d);

/* Write the canonical twin of `src` into out[0..cap). *out_len gets the
 * length (not NUL-terminated; a NUL is added when there is room). */
int zl_twin(int lang, const char *src, size_t len, char *out, size_t cap, size_t *out_len,
            zl_work_t *w, zl_diag_t *d);

/* Structural check of an IR program (opcodes, operands, targets). */
int zl_verify(const zl_prog_t *p, zl_diag_t *d);

/* Byte-exact comparison of two programs' IR, string table and widths
 * (the source line numbers are not part of the comparison). */
int zl_prog_equal(const zl_prog_t *a, const zl_prog_t *b);

/* 64-bit FNV-1a digest of the same bytes zl_prog_equal compares. */
uint64_t zl_prog_digest(const zl_prog_t *p);

/* Precompiled programs (zl_pack.c): write a verified program as "zlir"
 * text, or read one back without a front-end. zl_unpack refuses text that
 * is malformed, fails zl_verify or does not match its digest, leaving `p`
 * zeroed. zl_pack NUL-terminates, so cap must exceed the text's length;
 * otherwise it returns ZL_E_LIMIT with *out_len = 0 and out empty. */
int zl_pack(const zl_prog_t *p, char *out, size_t cap, size_t *out_len);
int zl_unpack(const char *src, size_t len, zl_prog_t *p, zl_diag_t *d);
const char *zl_op_name(int op); /* "PUSH"; "?" when out of range */

/* Optional record of every number the program printed, in order, so the
 * same algorithm written in different languages can be compared by value
 * even when their print formats differ. */
typedef struct {
    int64_t *v;
    size_t cap, n;
} zl_trace_t;

typedef struct {
    const int64_t *in; /* INPUT / READ values */
    size_t nin;
    int preload; /* nonzero: push in[0..nin) onto the data stack before the
                    first instruction instead (Forth takes its arguments
                    there); more than ZL_MAX_STACK traps ZL_E_STACK */
    char *out;   /* program output */
    size_t out_cap;
    size_t out_len;     /* set by zl_run */
    uint64_t fuel;      /* instructions allowed; 0 means 10^7 */
    uint64_t used;      /* set by zl_run */
    uint32_t trap_line; /* source line of the trapping instruction */
    uint32_t trap_pc;
    zl_trace_t *trace; /* may be NULL */
    /* machine state, owned by the caller so zl_run needs little stack */
    int64_t mem[ZL_MAX_SLOTS];
    int64_t stack[ZL_MAX_STACK];
    uint32_t rstack[ZL_MAX_CALLS];
} zl_run_t;

/* Run a program that passed zl_verify. Returns ZL_OK on HALT/END/STOP, or
 * the trap status; r->out holds what was printed up to that point. */
int zl_run(const zl_prog_t *p, zl_run_t *r);

#endif /* ZL_H */
