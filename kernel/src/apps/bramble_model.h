/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* bramble_model.h — Bramble, whiteboard and mind maps, as a model (zxm.h):
 * one board, a tree of labelled nodes laid out on integer rings about the
 * board's centre, kept as a text file in the Hoard. No drawing, no
 * allocation, integers only.
 *
 *   BR1 BOARDS ARE FILES. A board is a UTF-8 text file in the Hoard, read
 *       with files.read and written whole with files.write (begin/add/end
 *       when it does not fit one request; nothing appears until the end):
 *         bramble 1
 *         canvas <width> <height>
 *         step <ring step>
 *         node <parent> <label>       one line a node, the root first
 *       A parent is the place of an earlier node (-1 for the root, which
 *       is the only node without one). Places are not stored: BR2 lays
 *       them out again on every change and every open.
 *   BR2 RINGS. A node at depth d sits on the integer square of radius
 *       r = d * step about the centre, 8r units round; the node order of
 *       count on its ring sits p = floor(order * 8r / count) units
 *       clockwise from the top-left corner (-r, -r), y down
 *       (blocks/bramble/ring_x.cpp, ring_y.cpp and their KRON twins,
 *       witnessed). The nodes of one ring are ordered by their parents'
 *       order on the ring inside, then as they were added, so siblings sit
 *       together. Outside the blocks' domain the place is BRAMBLE_OFF.
 *   BR3 LIMITS. A map holds 1..BRAMBLE_NODES_MAX nodes, at most
 *       BRAMBLE_DEPTH_MAX deep, with a ring step of BRAMBLE_STEP_MIN..
 *       BRAMBLE_STEP_MAX units (blocks/bramble/limits.eve, Sutra
 *       witnessed). Its canvas is Gesso's (gesso_rules.h GR6, used through
 *       gesso.canvas_limits_ok), and the deepest ring must fit inside it:
 *       2 * depth * step + 1 at most the canvas's shorter side. An edit
 *       past a limit is refused (ZXM_E_ARG), never cut.
 *   BR4 RECORD. Every node fits its ^ZXBRAMBLE record
 *       (blocks/bramble/node.myra) before a board is saved.
 *   BR5 OWNER'S WORD. A save changes kept state, so it takes
 *       owner_confirmed: without it nothing is sent (ZXM_E_STATE). A
 *       save_as writes a new file only (exclusive); over the file a board
 *       came from, only save writes. */
#ifndef BRAMBLE_MODEL_H
#define BRAMBLE_MODEL_H

#include "zxm.h"

#define BRAMBLE_NODES_MAX 512
#define BRAMBLE_DEPTH_MAX 15
#define BRAMBLE_STEP_MIN  16
#define BRAMBLE_STEP_MAX  256
#define BRAMBLE_LABEL_MAX 64u
#define BRAMBLE_PATH_MAX  1024u
#define BRAMBLE_FILE_MAX  (64u * 1024u)

/* BR2: the ring blocks' domain */
#define BRAMBLE_RING_DEPTH_MAX 64
#define BRAMBLE_RING_STEP_MAX  4096
#define BRAMBLE_RING_COUNT_MAX 65536
#define BRAMBLE_OFF            INT64_C(-1000000) /* past every ring: 64 * 4096 = 262144 */

typedef struct {
    int32_t parent; /* place of the parent node, -1 for the root */
    int32_t depth;
    int32_t order; /* place on its ring, 0 .. the ring's count - 1 */
    int64_t x, y;  /* from the board's centre, y down */
    char label[BRAMBLE_LABEL_MAX + 1];
} bramble_node_t;

typedef struct {
    zxm_link_t *link;
    char space[ZXM_SPACE_MAX + 1];   /* where path is; "" = home */
    char path[BRAMBLE_PATH_MAX + 1]; /* "" = not saved yet */
    int64_t width, height, step;
    uint32_t n;
    int32_t depth; /* the deepest ring in use */
    bramble_node_t node[BRAMBLE_NODES_MAX];
    bool dirty;
} bramble_t;

/* BR2, as the blocks run them */
int64_t bramble_ring_x(int64_t depth, int64_t order, int64_t count, int64_t step);
int64_t bramble_ring_y(int64_t depth, int64_t order, int64_t count, int64_t step);
/* BR3 */
bool bramble_limits_ok(int64_t nodes, int64_t depth, int64_t step);
bool bramble_fits_canvas(int64_t width, int64_t height, int64_t depth, int64_t step);
/* BR4 */
bool bramble_node_ok(int64_t node, int64_t parent, int64_t depth, int64_t order, int64_t x,
                     int64_t y);

/* A new board with one root node; ZXM_E_ARG past BR3 or for a bad label
 * (1..BRAMBLE_LABEL_MAX bytes of UTF-8, no control characters). */
int bramble_new(bramble_t *b, zxm_link_t *link, int64_t width, int64_t height, int64_t step,
                const char *root_label);
/* A child of node parent; its place is written to *id. */
int bramble_add(bramble_t *b, uint32_t parent, const char *label, uint32_t *id);
int bramble_rename(bramble_t *b, uint32_t node, const char *label);
/* Removes node and every node under it (not the root). */
int bramble_remove(bramble_t *b, uint32_t node);

/* space: a space id from spaces.list, "" for home. A file that is not a
 * board, or a board past BR3, is refused (ZXM_E_ARG) and b is left as it
 * was. */
int bramble_open(bramble_t *b, const char *space, const char *path);
int bramble_save(bramble_t *b, bool owner_confirmed); /* ZXM_E_STATE: no path */
int bramble_save_as(bramble_t *b, const char *space, const char *path, bool owner_confirmed);

#endif
