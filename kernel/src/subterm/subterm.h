/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/*
 * subterm.h — SUBTERM: one root terminal, many portholes.
 *
 * "One prompt, many portholes — and when a command needs several, they all
 *  march to the same tick."
 *
 * A ROOT terminal under which commands open SUBTERMINALS. Some commands
 * spawn MULTIPLE subterminals SYNCHRONIZED BY PHASE TICKS: a synced group
 * shares ONE integer phase-tick clock, so every member reports the same
 * tick ordinal after each advance (LOCKSTEP). Output routes to a specific
 * subterminal's bounded ring, and built-in TUTORIAL modes drive scripted,
 * deterministic guided sequences.
 *
 * This module owns the subterminal TREE, the phase-tick LOCKSTEP, output
 * ROUTING and TUTORIALS. It does not render glyphs or read a keyboard —
 * the display/rendering layer and the input device are OPS BOUNDARIES
 * (font/graphics + input drivers). Phase ticks here are pure integer
 * ordinals (phase-tick ordering, never a wall clock); same inputs =>
 * same outputs.
 *
 * Reuses the concepts of src/pterm (the shell/console) and
 * src/pterm/pterm_mux (phase-tick multiplexing) without re-implementing
 * either: SUBTERM is the tree + lockstep + routing + tutorial slice.
 */
#ifndef ZXV_SUBTERM_H
#define ZXV_SUBTERM_H

#include <stdint.h>
#include <stdbool.h>

/* ===== Bounds (fixed-size, integer-only, freestanding) ===== */
#define SUBTERM_MAX          32   /* total subterminals incl. the root */
#define SUBTERM_ROOT          0   /* the root terminal's id is always 0 */
#define SUBTERM_RING        256   /* per-subterminal bounded output ring */
#define SUBTERM_TITLE_LEN    32
#define SUBTERM_MAX_GROUPS    8    /* concurrent synced groups           */
#define SUBTERM_LESSON_MAX   16    /* max scripted steps in one lesson    */

/* A subterminal is not synced unless it belongs to a group. */
#define SUBTERM_NO_GROUP     (-1)

/* ===== A single subterminal (a porthole) ===== */
typedef struct {
    bool     used;
    int32_t  parent;                 /* parent subterminal id (root = -1's owner) */
    int32_t  group;                  /* synced-group id, or SUBTERM_NO_GROUP */
    char     title[SUBTERM_TITLE_LEN];
    /* Bounded output ring — oldest bytes are dropped when full. */
    uint8_t  ring[SUBTERM_RING];
    uint32_t ring_head;              /* index of oldest byte           */
    uint32_t ring_len;              /* bytes currently held (<= RING)  */
    uint64_t tick;                   /* own tick for non-synced nodes  */
} subterm_node_t;

/* ===== A scripted tutorial lesson (caller-owned, const) ===== */
typedef struct {
    char        title[SUBTERM_TITLE_LEN];
    const char *steps[SUBTERM_LESSON_MAX]; /* prompt strings, in order */
    uint32_t    n_steps;
} subterm_lesson_t;

/* ===== The whole tree ===== */
typedef struct {
    subterm_node_t node[SUBTERM_MAX];
    uint32_t       num_nodes;                 /* live nodes incl. root  */
    /* One shared phase-tick clock per synced group. */
    uint64_t       group_tick[SUBTERM_MAX_GROUPS];
    uint32_t       group_size[SUBTERM_MAX_GROUPS];
    bool           group_used[SUBTERM_MAX_GROUPS];
    uint32_t       num_groups;
    /* Tutorial cursor. */
    const subterm_lesson_t *tut;
    uint32_t                tut_pos;
    bool                    tut_active;
    bool                    initialized;
} subterm_tree_t;

/* ===== API ===== */

/* Initialize the tree with a single ROOT terminal (id SUBTERM_ROOT). */
void subterm_init(subterm_tree_t *t);

/* Open a child subterminal under `parent`. Returns the child id, or -1
 * if the parent is invalid or the pool is full. */
int32_t subterm_open(subterm_tree_t *t, int32_t parent, const char *title);

/* Open `n` subterminals under `parent` sharing ONE phase-tick clock.
 * Returns the synced-GROUP id (>=0), or -1 if n==0, no group slot is
 * free, or the node pool cannot hold n more. */
int32_t subterm_spawn_synced(subterm_tree_t *t, int32_t parent, uint32_t n);

/* Advance every synced subterminal by exactly one phase tick. Because
 * each group shares a single clock, all members of a group report the
 * same tick ordinal afterwards (LOCKSTEP). */
void subterm_tick(subterm_tree_t *t);

/* The current tick ordinal of subterminal `id` (a synced member reports
 * its group's shared clock). Returns 0 for an out-of-range id. */
uint64_t subterm_tick_of(const subterm_tree_t *t, int32_t id);

/* Route `len` output bytes to subterminal `id`'s bounded ring. Returns
 * bytes accepted, or -1 if id is out of range / not live. */
int32_t subterm_write(subterm_tree_t *t, int32_t id,
                      const uint8_t *data, uint32_t len);

/* Copy up to `cap` bytes currently held in `id`'s ring into `out`, in
 * order (oldest first). Returns bytes copied, or -1 if id invalid. */
int32_t subterm_read(const subterm_tree_t *t, int32_t id,
                     uint8_t *out, uint32_t cap);

/* Tree introspection. */
int32_t  subterm_parent_of(const subterm_tree_t *t, int32_t id);
uint32_t subterm_group_count(const subterm_tree_t *t, int32_t group);
int32_t  subterm_group_member(const subterm_tree_t *t, int32_t group, uint32_t k);

/* ===== Tutorials ===== */

/* Begin a scripted lesson. Returns the first step's prompt (or NULL for
 * an empty lesson). Deterministic — no clock, no randomness. */
const char *subterm_tutorial_begin(subterm_tree_t *t,
                                   const subterm_lesson_t *lesson);

/* Advance to the next step and return its prompt, or NULL when the
 * lesson has ended (the tutorial then becomes inactive). */
const char *subterm_tutorial_step(subterm_tree_t *t);

#endif /* ZXV_SUBTERM_H */
