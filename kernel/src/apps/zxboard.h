/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxboard.h — the Circuit board: build an app by wiring apps together.
 *
 * The board is the model behind the visual builder (views/circuit.c). A
 * person drags apps and blocks off the shelf, wires their inputs to the
 * app's inputs, to each other, or to a setting, and saves. What is saved
 * is an ordinary manifest (zxapp.h) with one circuit (zxwire.h), so the
 * new app runs, nests, is shared by content id and credits its parts
 * exactly like an app written by hand. Nobody writes code; everything the
 * board can do is in the manifest text, so an expert can also edit it.
 *
 *   CB1 THE SHELF is every entry the registry can lend: each block's and
 *       each exporting app's provides lines, as "<id>.<entry>", with its
 *       inputs and what it answers (zxboard_shelf).
 *   CB2 PARTS AND PORTS. A part is a shelf entry placed on the board; its
 *       ports are its contract's inputs, in order, each unwired at first.
 *       The board's own inputs and its answer kind are the new app's
 *       contract.
 *   CB3 EVERY WIRE HAS A STATE, in the logic's words, computed fresh by
 *       zxboard_check and drawn by the view: TRUE (whole numbers into an
 *       input), BOTH (held: a truth into an input, the answer of the wrong
 *       kind, or a loop), NEITHER (not wired, or its part is missing from
 *       the registry). Nothing is converted or dropped to make a wire fit.
 *   CB4 SAVE writes the manifest: the parts in an order where every
 *       source comes first, a use line per block (its registered
 *       major.minor), the circuit's provides line, the parts' contracts
 *       re-exported so the app works as a block too (C1), "view auto", and
 *       the layout (W5). It is refused while a wire is NEITHER or a loop,
 *       because that app could not load; a held wire saves, and holds when
 *       run, as the board said it would.
 *   CB5 ROUND TRIP. zxboard_open reads a saved manifest back onto the
 *       board, and saving it again gives the same text, so the content id
 *       is stable until the person changes something.
 *   CB6 TRY runs the board as it stands (save, compose, load, call
 *       witnessed) without registering anything, so a person sees the
 *       answer, or why it is held, before they keep it.
 */
#ifndef ZXBOARD_H
#define ZXBOARD_H

#include "zxwire.h"

#define ZXBOARD_TEXT 8192u

/* CB1: one shelf entry. */
typedef struct {
    const zxapp_t *from;         /* the block or app that provides it */
    const zxapp_provide_t *prov; /* its contract */
    char ref[ZXAPP_ENTRY_MAX];   /* "<id>.<entry>", as a part names it */
} zxboard_item_t;

/* CB3: one port's (or the answer's) state. */
typedef struct {
    int32_t port[ZXWIRE_PARTS][ZXAPP_INPUTS]; /* ZXWIRE_OK / _HELD / _OPEN */
    int32_t part[ZXWIRE_PARTS];               /* ZXWIRE_OPEN when its entry is missing */
    uint8_t kind[ZXWIRE_PARTS];               /* what each part answers (0 = unknown) */
    int32_t out;
    bool loop[ZXWIRE_PARTS]; /* the part sits on a loop */
    uint32_t ok, held, open; /* wires (ports and the answer) in each state */
    int32_t lp;              /* the board: TRUE all wired, BOTH any held, NEITHER any open */
    char why[ZXAPP_ERR_MAX]; /* the first problem, in words ("" when all TRUE) */
} zxboard_check_t;

/* A place on the board. */
enum { ZXBOARD_NODE_PART = 0, ZXBOARD_NODE_INPUT, ZXBOARD_NODE_OUT };

typedef struct {
    const zxapp_registry_t *reg;
    char id[ZXAPP_ID_MAX];
    char name[48];
    char author[ZXAPP_AUTHOR_MAX];
    char entry[ZXAPP_ID_MAX]; /* the circuit's entry name ("main" by default) */
    zxwire_circuit_t c;       /* inputs, answer kind, parts, wires, layout */
    char err[ZXAPP_ERR_MAX];  /* the last refusal, for the view */
    /* CB6 working space */
    char text[ZXBOARD_TEXT];
    zxapp_t app;
} zxboard_t;

/* An empty board building app `id` ("Name" shown), answering numbers. */
int zxboard_init(zxboard_t *b, const zxapp_registry_t *reg, const char *id, const char *name);

/* CB1: shelf entry i (false past the end); zxboard_shelf_count. */
uint32_t zxboard_shelf_count(const zxboard_t *b);
bool zxboard_shelf(const zxboard_t *b, uint32_t i, zxboard_item_t *out);
/* The registry's contract for a part's entry ("dash.shares.share"); NULL when missing. */
const zxapp_provide_t *zxboard_contract(const zxboard_t *b, const char *ref, const zxapp_t **from);

/* CB2: the new app's own inputs and answer. */
int zxboard_input_add(zxboard_t *b, const char *name);
int zxboard_input_remove(zxboard_t *b, uint32_t i); /* unwires what took it */
int zxboard_answer_kind(zxboard_t *b, uint8_t kind);

/* CB2: place shelf entry `ref` at (x, y); returns the part's index, or < 0.
 * Its name is the entry's last word, made unique ("fee", "fee2"). */
int zxboard_part_add(zxboard_t *b, const char *ref, int32_t x, int32_t y);
int zxboard_part_remove(zxboard_t *b, uint32_t part); /* unwires what took it */
int zxboard_part_rename(zxboard_t *b, uint32_t part, const char *name);

/* Wire port `port` of a part to a source: an input, a part, or a setting
 * (ZXWIRE_SRC_NONE unwires it). A part may not take itself. */
int zxboard_wire(zxboard_t *b, uint32_t part, uint32_t port, const zxwire_src_t *src);
int zxboard_out(zxboard_t *b, const zxwire_src_t *src);
/* Sources, for the view. */
zxwire_src_t zxboard_src_input(uint32_t i);
zxwire_src_t zxboard_src_part(uint32_t i);
/* "25" or "1,2,3": false when it is not up to 8 whole numbers. */
bool zxboard_src_setting(const char *text, zxwire_src_t *out);

/* Place a node (W5). */
int zxboard_move(zxboard_t *b, int node, uint32_t i, int32_t x, int32_t y);

/* CB3 */
void zxboard_check(const zxboard_t *b, zxboard_check_t *out);

/* CB4: write the manifest into out; returns its length, or < 0 (b->err
 * says why: ZXAPP_E_CONTRACT for an open wire or a loop, ZXAPP_E_BUFFER
 * when it does not fit). */
int32_t zxboard_save(zxboard_t *b, char *out, uint32_t cap);
/* CB5: read app `a`'s circuit `entry` onto the board. */
int zxboard_open(zxboard_t *b, const zxapp_registry_t *reg, const zxapp_t *a, const char *entry);
/* CB6: run the board on host h (which has its runtimes and zxwire hook). */
int zxboard_try(zxboard_t *b, zxapp_host_t *h, const zxapp_arg_t *args, uint32_t nargs,
                zxapp_num_t *out, uint32_t cap, uint32_t *n_out, int32_t *lp);

#endif /* ZXBOARD_H */
