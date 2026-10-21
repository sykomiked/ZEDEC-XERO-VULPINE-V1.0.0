/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* burin_model.h — Burin, vector drawing and engraving-style art, as a model
 * (zxm.h): one drawing of stroked paths (lines and cubic curves) on a
 * page, hatched the way an engraver shades, kept and printed through the
 * Hoard. No allocation, integers only; lengths are hundredths of a point
 * (burin_rules.h), y grows up the page as it does in PDF.
 *
 *   BM1 DRAWINGS ARE FILES. A drawing is a text file in the Hoard (one
 *       "burin 1 <width> <height>" line, then per path a "p <stroke> <x>
 *       <y>" line and its segments, "l <x> <y>" or "c <x1> <y1> <x2> <y2>
 *       <x> <y>"), written whole with files.write (begin/add/end when it
 *       does not fit one request) and read back with files.read. A file
 *       that does not parse, or breaks BM2, leaves the open drawing as it
 *       was.
 *   BM2 LIMITS. A page is 1 .. BURIN_PAGE_MAX on each side, a path holds
 *       BURIN_POINTS_MAX points at most (BURIN_SEGS segments) with a
 *       stroke 1 .. BURIN_STROKE_MAX, every point inside
 *       -BURIN_COORD_MAX .. BURIN_COORD_MAX (blocks/burin/path.eve, Eve).
 *       A change that would break them is refused (ZXM_E_ARG) or, past
 *       BURIN_PATHS paths, ZXM_E_TOOBIG; nothing is changed first.
 *   BM3 FLATTENING. A cubic segment is cut into the fewest equal steps n
 *       (1 .. BURIN_STEPS_MAX) whose chords stay within the tolerance:
 *       with d the largest second difference of its control points on
 *       either axis, the least n with 3 * d <= 4 * tol * n^2; each point is
 *       burin_bezier_at (blocks/burin/bezier.cpp and bezier.kron,
 *       witnessed). What an engraving tool and the printed page follow.
 *   BM4 HATCHING. A rectangle is shaded with level lines across its
 *       height: at a spacing (burin_hatch_lines, blocks/burin/
 *       hatch_lines.tb and .kron) or a count set evenly (burin_hatch_gap,
 *       hatch_gap.tb and .kron), line k at the floor of k * span /
 *       (lines + 1). Each line is a path of its own.
 *   BM5 PRINTING. The drawing is written as a one-page PDF (flattened by
 *       BM3, coordinates in points with two decimals written from
 *       integers) and printed with print.submit; a printer takes or
 *       refuses the format itself (zxvd does not convert). The job's id
 *       and state are kept. */
#ifndef BURIN_MODEL_H
#define BURIN_MODEL_H

#include "burin_rules.h"
#include "zxm.h"

#define BURIN_PATHS     64u
#define BURIN_PATH_MAX  1024u
#define BURIN_FILE_MAX  (1u << 20) /* a drawing's text, and a PDF */
#define BURIN_TOL       25         /* the default flatness, a quarter of a point */
#define BURIN_URI_MAX   511u
#define BURIN_STATE_MAX 16u

enum { BURIN_LINE = 0, BURIN_CUBIC = 1 };

typedef struct {
    int32_t kind; /* BURIN_LINE: only x, y */
    int32_t x1, y1, x2, y2, x, y;
} burin_seg_t;

typedef struct {
    int32_t x0, y0; /* where it starts */
    int32_t stroke;
    uint32_t nseg;
    burin_seg_t seg[BURIN_SEGS];
} burin_path_t;

typedef struct {
    zxm_link_t *link;
    char space[ZXM_SPACE_MAX + 1];
    char path[BURIN_PATH_MAX + 1]; /* "" = not saved yet */
    int32_t width, height;         /* 0 = no drawing yet */
    uint32_t npaths;
    burin_path_t p[BURIN_PATHS];
    bool dirty;
    int64_t job;                     /* the last print job's id; 0 = none */
    char job_state[BURIN_STATE_MAX]; /* as the printer said it */
} burin_t;

void burin_init(burin_t *b, zxm_link_t *link);
/* BM2: a new empty drawing on a page of width by height. */
int burin_new(burin_t *b, int64_t width, int64_t height);
/* Points a path holds: its start, 1 per line, 3 per cubic. */
int64_t burin_points(const burin_path_t *p);
/* BM2: start a path at x, y; it becomes the last path. */
int burin_move_to(burin_t *b, int64_t x, int64_t y, int64_t stroke);
int burin_line_to(burin_t *b, int64_t x, int64_t y);
int burin_curve_to(burin_t *b, int64_t x1, int64_t y1, int64_t x2, int64_t y2, int64_t x,
                   int64_t y);
/* BM3: the steps for one cubic from (x0, y0) at a tolerance (1 or more). */
int64_t burin_flat_steps(const burin_seg_t *s, int64_t x0, int64_t y0, int64_t tol);
/* BM3: path i as a polyline into x and y (cap points); *n its points. */
int burin_flatten(const burin_t *b, uint32_t i, int64_t tol, int32_t *x, int32_t *y, uint32_t cap,
                  uint32_t *n);
/* BM4: shade the rectangle x0..x1 by y0..y1 with lines at a spacing, or
 * with a count of lines set evenly. *added is how many paths it made. */
int burin_hatch_spaced(burin_t *b, int64_t x0, int64_t y0, int64_t x1, int64_t y1, int64_t gap,
                       int64_t stroke, uint32_t *added);
int burin_hatch_even(burin_t *b, int64_t x0, int64_t y0, int64_t x1, int64_t y1, int64_t lines,
                     int64_t stroke, uint32_t *added);
/* BR5: whether path i's record fits ^ZXBURIN at mtime. */
bool burin_path_record(const burin_t *b, uint32_t i, int64_t mtime);

/* BM1. space: a space id from spaces.list, "" for home. */
int burin_save(burin_t *b); /* to b->path; ZXM_E_STATE when there is none */
int burin_save_as(burin_t *b, const char *space, const char *path);
int burin_open(burin_t *b, const char *space, const char *path);
/* BM5: write the drawing as a PDF at pdf (in b->space), flattened at tol. */
int burin_export_pdf(burin_t *b, const char *pdf, int64_t tol);
/* BM5: print the PDF at pdf on the printer at uri; copies 0 = its default. */
int burin_print(burin_t *b, const char *uri, const char *pdf, uint32_t copies);

#endif
