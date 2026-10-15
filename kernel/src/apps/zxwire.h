/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxwire.h — circuits: apps wired together like a circuit diagram.
 *
 * A circuit is an entry whose language is "wire". It does no work of its
 * own: it names parts (other entries: an app's, a block's, another app's
 * used as a block), and wires each part's inputs to the circuit's inputs,
 * to an earlier part's answer, or to a fixed setting. The Circuit board
 * (zxboard.h, views/circuit.c) draws and edits exactly this text, so a
 * person builds an app by wiring and configuring, and never writes code.
 *
 *   provides pay_ok truth amount rate
 *   logic    wire pay_ok <<
 *   part f   fee_check.fee    amount=amount rate=rate
 *   part ok  fee_check.fee_ok amount=amount fee=f
 *   out  ok
 *   at   f 160 80
 *   at   ok 400 80
 *   .
 *
 *   W1 AN ENTRY LIKE ANY OTHER. A circuit is bound with "logic wire"
 *      (alias "circuit"), always inline, under its own provides line,
 *      which names the circuit's inputs and what it answers. So it has a
 *      contract (B2), can be written alongside a second implementation in
 *      another language and witnessed (A5), exported and used as a block
 *      (C1), shown by "view auto", pinned by content id (C3) and credited
 *      (C2). zxwire_register hooks circuits into a host; without it they
 *      stay unloaded like any language with no runtime (A2).
 *   W2 PARTS. "part <name> <entry> [<input>=<source> ...]": every input of
 *      the part's contract is wired exactly once. A source is one of the
 *      circuit's inputs, an earlier part (its whole answer), or a setting:
 *      whole numbers joined by commas ("25" or "1,2,3"); a setting of one
 *      number is the same for every row the part's other inputs carry.
 *      Parts are named
 *      relative to the circuit's own manifest, so a circuit inside a used
 *      app still finds its parts after composing (B4). Up to ZXWIRE_PARTS.
 *   W3 ONE WAY. A part can only take earlier parts, so a circuit never
 *      loops. "out <source>" is the circuit's answer (exactly one).
 *   W4 TYPED WIRES, HELD NOT GUESSED. Inputs take whole numbers. A wire
 *      that carries a truth into an input, an answer that is not whole
 *      (1/3), or an "out" of the wrong kind is not converted: the circuit
 *      is held (BOTH, ZXAPP_E_CONFLICT) and says which wire. Each part is
 *      called witnessed: a part whose languages disagree holds the circuit
 *      there, and nothing downstream runs on a held answer. The circuit's
 *      join is TRUE when every part was witnessed, else NEITHER.
 *   W5 LAYOUT. "at <name> <x> <y>" places a part, an input or "out" on the
 *      board. Runs ignore it; it is part of the manifest's content, so a
 *      shared app looks the same to everyone.
 *   W6 BOUNDED. A circuit may use another circuit, ZXWIRE_DEPTH deep; the
 *      values on a wire are at most ZXWIRE_VALUES. No allocation: the
 *      working space is in the caller's zxwire_t.
 */
#ifndef ZXWIRE_H
#define ZXWIRE_H

#include "zxapp.h"

#define ZXWIRE_PARTS  16u
#define ZXWIRE_CONST  8u  /* whole numbers in one setting */
#define ZXWIRE_VALUES 64u /* values one wire carries */
#define ZXWIRE_DEPTH  4u

enum {
    ZXWIRE_SRC_NONE = 0, /* not wired yet (the board only) */
    ZXWIRE_SRC_INPUT,    /* the circuit's input number `index` */
    ZXWIRE_SRC_PART,     /* part number `index` */
    ZXWIRE_SRC_CONST     /* a setting */
};

typedef struct {
    uint8_t kind; /* ZXWIRE_SRC_* */
    uint8_t index;
    uint8_t nconst;
    int64_t konst[ZXWIRE_CONST];
} zxwire_src_t;

typedef struct {
    char name[ZXAPP_ID_MAX];
    char entry[ZXAPP_ENTRY_MAX]; /* as written: relative to the circuit's manifest */
    char port[ZXAPP_INPUTS][ZXAPP_ID_MAX];
    zxwire_src_t src[ZXAPP_INPUTS];
    uint32_t nports;
    int32_t x, y; /* W5 */
    bool placed;
} zxwire_part_t;

/* A circuit, parsed. The inputs are its contract's (W1). */
typedef struct {
    char input[ZXAPP_INPUTS][ZXAPP_ID_MAX];
    uint32_t ninputs;
    uint8_t kind; /* ZXAPP_KIND_*: what it answers */
    zxwire_part_t part[ZXWIRE_PARTS];
    uint32_t nparts;
    zxwire_src_t out;
    int32_t in_x[ZXAPP_INPUTS], in_y[ZXAPP_INPUTS], out_x, out_y;
    bool in_placed[ZXAPP_INPUTS], out_placed;
    char err[ZXAPP_ERR_MAX];
    uint32_t err_line; /* within the circuit's text, from 1 */
} zxwire_circuit_t;

/* Parse a circuit's text (W2, W3, W5) against its contract P. Names are
 * checked here; whether the parts exist is zxwire_check's. */
int zxwire_parse(zxwire_circuit_t *c, const zxapp_provide_t *P, const char *src, uint32_t n);

/* The full name a part's entry has in app `a`, for a circuit whose own
 * entry is `self` (W2: "dash.total" finds "swarm_shares.x" as
 * "dash.swarm_shares.x"). False when it does not fit. */
bool zxwire_resolve(const char *self, const char *entry, char out[ZXAPP_ENTRY_MAX]);

/* W4: what a wire into an input carries, given what its source answers. */
enum {
    ZXWIRE_OK = ZXAPP_TRUE,     /* whole numbers into an input */
    ZXWIRE_HELD = ZXAPP_BOTH,   /* the wrong kind: a truth into an input */
    ZXWIRE_OPEN = ZXAPP_NEITHER /* not wired, or its source is missing */
};
/* The kind a source answers in circuit c (ZXAPP_KIND_NUM for inputs and
 * settings), with `kinds[i]` what part i answers (0 = unknown). */
uint8_t zxwire_src_kind(const zxwire_circuit_t *c, const zxwire_src_t *s, const uint8_t *kinds);

/* The working space for running circuits (W6). */
typedef struct {
    zxwire_circuit_t c[ZXWIRE_DEPTH];
    zxapp_num_t val[ZXWIRE_DEPTH][ZXWIRE_PARTS][ZXWIRE_VALUES];
    uint32_t nval[ZXWIRE_DEPTH][ZXWIRE_PARTS];
    int64_t arg[ZXWIRE_DEPTH][ZXAPP_INPUTS][ZXWIRE_VALUES];
    uint32_t depth;
    zxapp_wiring_t hook;
} zxwire_t;

/* W1: circuits run on host h with w as their working space. */
void zxwire_register(zxapp_host_t *h, zxwire_t *w);

#endif /* ZXWIRE_H */
