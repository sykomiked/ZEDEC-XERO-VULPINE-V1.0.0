/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* gnomon_model.h — Gnomon, the geometry and maths explorer, as a model
 * (zxm.h): one figure in the plane, drawn point by point or walked by a
 * turtle, measured exactly (areas, squared sides, slopes as reduced
 * fractions, turns) and kept as a file in the Banker. No allocation,
 * integers only (gnomon_rules.h); y up.
 *
 *   GM1 FIGURES ARE FILES. A figure is a text file in the Hoard (one
 *       "gnomon 1 <kind> <vertices> <closed>" line, then a "p <x> <y>"
 *       line per vertex in order), written whole with files.write
 *       (begin/add/end when it does not fit one request) and read back
 *       with files.read. Only a turtle path is closed; one read back
 *       stands at its last vertex (its first when closed), facing east.
 *       A file that does not parse, or breaks GM2, leaves the open figure
 *       as it was.
 *   GM2 LIMITS. Every coordinate is inside -GNOMON_COORD_MAX ..
 *       GNOMON_COORD_MAX and a figure holds at most GNOMON_VERTS_MAX
 *       vertices. A change that would break them is refused (ZXM_E_ARG)
 *       or, past the count, ZXM_E_TOOBIG; nothing is changed first.
 *   GM3 THE TURTLE. A turtle path (kind GNOMON_KIND_TURTLE) starts at the
 *       origin facing east (heading 0; 1 north, 2 west, 3 south) with the
 *       origin as its first vertex. gnomon_turtle_turn turns it whole
 *       quarter turns (positive to the left), gnomon_turtle_forward moves
 *       it steps along its heading and adds the point it reaches; a move
 *       that brings it back to its first vertex closes the path instead.
 *       Quarter turns keep every point on the integer grid, so the path is
 *       exact. It is C, since Lingua's Logo (twin Kiteling) reads no input
 *       (zl_logo.c refuses readword and readlist) and its turtle graphics
 *       are outside its subset.
 *   GM4 MEASURES. Twice the area and the area (gnomon_polygon_area2 and
 *       gnomon_polygon_area, blocks/gnomon/area2 and area, KRON and
 *       Trebago witnessed), each side's squared length (gnomon_side_len_sq,
 *       side_sq, KRON and C++ witnessed), the turn at each vertex
 *       (gnomon_triangle_area2, triangle, KRON and C++ witnessed), whether
 *       the figure is convex, and each side's slope as a reduced fraction
 *       (kernel/src/rational). A measure of fewer than 3 vertices, or of a
 *       vertical side's slope, is ZXM_E_STATE.
 *   GM5 RECORD. A saved figure is listed by gnomon_record
 *       (blocks/gnomon/figure.myra, ^ZXGNOMON). */
#ifndef GNOMON_MODEL_H
#define GNOMON_MODEL_H

#include "gnomon_rules.h"
#include "zxm.h"

#define GNOMON_PATH_MAX    1024u
#define GNOMON_FILE_MAX    (1u << 15) /* a figure's text */
#define GNOMON_KIND_POLY   0
#define GNOMON_KIND_TURTLE 1

typedef struct {
    zxm_link_t *link;
    char space[ZXM_SPACE_MAX + 1];
    char path[GNOMON_PATH_MAX + 1]; /* "" = not saved yet */
    uint32_t n;
    int64_t x[GNOMON_VERTS_MAX], y[GNOMON_VERTS_MAX];
    int32_t kind;    /* GNOMON_KIND_* */
    int32_t heading; /* GM3: quarter turns from east, 0 .. 3 */
    int64_t tx, ty;  /* GM3: where the turtle stands */
    bool closed;     /* GM3: the turtle came back to its first vertex */
    bool dirty;
} gnomon_t;

/* An empty polygon. */
void gnomon_init(gnomon_t *g, zxm_link_t *link);
/* GM2: an empty polygon; the link and the path are kept. */
void gnomon_clear(gnomon_t *g);
/* GM2: a vertex at the end of a polygon (ZXM_E_STATE on a turtle path). */
int gnomon_add_point(gnomon_t *g, int64_t x, int64_t y);

/* GM3: a turtle path at the origin, facing east. */
void gnomon_turtle_start(gnomon_t *g);
int gnomon_turtle_turn(gnomon_t *g, int64_t quarters);
int gnomon_turtle_forward(gnomon_t *g, int64_t steps);

/* GM4 */
int gnomon_area2(const gnomon_t *g, int64_t *twice);
int gnomon_area(const gnomon_t *g, int64_t *num, int64_t *den);
/* side i runs from vertex i to vertex i + 1 (the last to the first) */
int gnomon_side_sq(const gnomon_t *g, uint32_t i, int64_t *sq);
int gnomon_slope(const gnomon_t *g, uint32_t i, int64_t *num, int64_t *den);
/* the turn at vertex i, twice the area of its corner triangle: > 0 left */
int gnomon_turn_at(const gnomon_t *g, uint32_t i, int64_t *twice);
/* *convex: every turn the same way (or straight), and at least one not */
int gnomon_convex(const gnomon_t *g, bool *convex);

/* GM5: whether this figure's record, as figure number `figure`, fits. */
bool gnomon_record(const gnomon_t *g, int64_t figure, int64_t mtime);

/* GM1. space: a space id from spaces.list, "" for home. */
int gnomon_save(gnomon_t *g); /* to g->path; ZXM_E_STATE when there is none */
int gnomon_save_as(gnomon_t *g, const char *space, const char *path);
int gnomon_open(gnomon_t *g, const char *space, const char *path);

#endif
