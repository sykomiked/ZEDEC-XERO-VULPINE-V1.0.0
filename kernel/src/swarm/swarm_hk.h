/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* swarm_hk.h — Hackronomicon shorthand as the language agents use to ask
 * each other for work inside the swarm.
 *
 * Every request between agents is one line of Hackronomicon notation
 * (Part One, Ch 2), parsed into a tree before it is sent so the receiver
 * reads exactly what the sender meant:
 *
 *   verb(object, key: value)   apply the verb, refined by modifiers
 *   A + B   also      A - B   without    A -> B   then (also → and −)
 *   A / B   relative to         A > B   outranks
 *   A && B  both      A || B  either     A^(p/q)  dial (depth, strictness)
 *
 *   K1  PRECEDENCE (book, Ch 2): parentheses, then dials, then "/", then
 *       "+ -", then "&& ||", then ">", then "->". Inside a call, "key:"
 *       binds loosest.
 *   K2  ONE READING.  swarm_hk_canonical prints the tree fully
 *       parenthesised, so sender and receiver can compare readings
 *       ("state your reading").
 *   K3  NO STACKING.  A request with more than 5 operators is refused (the
 *       book's notation-stacking warning); split it into steps.
 *   K4  NO SELF-REQUESTS, and a hand-off strand is at most 89 words (III.8).
 *   K5  SIX TRUTH STATES (V.1, V.9) travel with every reply:
 *       ⊤ true, ⊥ false, ⊥̸ glut, N neutral, P paradox, U unknown.
 * Freestanding: no libc, no allocation, no floating point.
 */
#ifndef SWARM_HK_H
#define SWARM_HK_H

#include <stdint.h>
#include <stdbool.h>

#define SWARM_HK_MAX_TEXT   256u
#define SWARM_HK_MAX_NODES  64u
#define SWARM_HK_MAX_OPS    5u  /* K3 */
#define SWARM_HK_STRAND_MAX 89u /* K4, F(11) words */

typedef enum {
    SWARM_HK_ATOM = 0, /* words: an object, a value, a number */
    SWARM_HK_CALL,     /* verb(args) */
    SWARM_HK_KV,       /* key: value (only inside a call) */
    SWARM_HK_BIN,      /* A op B */
    SWARM_HK_DIAL      /* A^(p/q) */
} swarm_hk_kind_t;

typedef enum {
    SWARM_HK_OP_THEN = 0, /* ->  */
    SWARM_HK_OP_OUTRANK,  /* >   */
    SWARM_HK_OP_AND,      /* &&  */
    SWARM_HK_OP_OR,       /* ||  */
    SWARM_HK_OP_ALSO,     /* +   */
    SWARM_HK_OP_WITHOUT,  /* -   */
    SWARM_HK_OP_PER       /* /   */
} swarm_hk_op_t;

typedef struct {
    swarm_hk_kind_t kind;
    swarm_hk_op_t op;    /* BIN */
    uint16_t start, len; /* ATOM text, CALL verb, KV key: span in the source */
    int16_t a, b;        /* BIN operands; DIAL/KV operand in a; -1 if none */
    int16_t first_arg;   /* CALL: first argument node, -1 if none */
    int16_t next;        /* next argument in a call, -1 if last */
    int32_t num, den;    /* DIAL p/q */
} swarm_hk_node_t;

typedef struct {
    char src[SWARM_HK_MAX_TEXT];
    uint16_t src_len;
    swarm_hk_node_t node[SWARM_HK_MAX_NODES];
    uint16_t num_nodes;
    int16_t root;
    uint16_t num_ops; /* binary operators and dials, for K3 */
} swarm_hk_ast_t;

typedef enum {
    SWARM_HK_OK = 0,
    SWARM_HK_ERR_SYNTAX = -1,
    SWARM_HK_ERR_TOO_LONG = -2,
    SWARM_HK_ERR_STACKED = -3, /* K3 */
    SWARM_HK_ERR_SELF = -4,    /* K4 */
    SWARM_HK_ERR_ARG = -5
} swarm_hk_status_t;

/* K5: the six truth states. */
typedef enum {
    SWARM_HK_TRUE = 0, /* ⊤ verified */
    SWARM_HK_FALSE,    /* ⊥ refuted */
    SWARM_HK_GLUT,     /* ⊥̸ contradiction held */
    SWARM_HK_NEUTRAL,  /* N evidence silent */
    SWARM_HK_PARADOX,  /* P unresolvable here: escalate */
    SWARM_HK_UNKNOWN   /* U unknown, preferred to a guess */
} swarm_hk_truth_t;

/* An agent-to-agent request. */
typedef struct {
    uint32_t from, to;
    uint32_t ordinal;       /* @n: its position in the plan */
    swarm_hk_truth_t truth; /* the sender's truth state for its premise */
    swarm_hk_ast_t ast;
} swarm_hk_msg_t;

/* Parse one line of shorthand (K1). */
swarm_hk_status_t swarm_hk_parse(const char *text, swarm_hk_ast_t *out);

/* K2: write the fully parenthesised reading into buf (NUL-terminated).
 * Returns its length, or -1 if buf is too small. */
int32_t swarm_hk_canonical(const swarm_hk_ast_t *ast, char *buf, uint32_t cap);

/* K1-K4: build a request from one agent to another. */
swarm_hk_status_t swarm_hk_request(swarm_hk_msg_t *msg, uint32_t from, uint32_t to,
                                   uint32_t ordinal, swarm_hk_truth_t truth, const char *text);

/* K4: number of words in a hand-off strand, and whether it fits 89. */
uint32_t swarm_hk_words(const char *text);
bool swarm_hk_strand_ok(const char *text);

/* K5 (V.9): combine truth states. ⊤∧⊥ = ⊥̸, ⊤∧U = U, ⊤∨U = ⊤, ⊥̸∧x = ⊥̸,
 * P dominates ⊥̸, N∨x = x. */
swarm_hk_truth_t swarm_hk_and(swarm_hk_truth_t a, swarm_hk_truth_t b);
swarm_hk_truth_t swarm_hk_or(swarm_hk_truth_t a, swarm_hk_truth_t b);

#endif /* SWARM_HK_H */
