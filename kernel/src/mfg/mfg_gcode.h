/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* mfg_gcode.h — F.2 toolpath checker for a subset of G-code (RS-274 style).
 *
 * WHAT IT DOES
 *   Reads a G-code program from a caller buffer, line by line, and checks it
 *   against a machine profile BEFORE a person releases the job. It never
 *   sends anything to a machine. The machine's own interlocks and the
 *   person approving the job remain in charge; this is a second pair of
 *   eyes for arithmetic and programming errors.
 *
 *   Supported words: N (line number, first word only), G0 G1 G2 G3 (linear
 *   and XY arcs with I/J centre offsets, plane G17 only), G17, G20/G21
 *   (inch/mm), G90/G91 (absolute/incremental), X Y Z I J, F (feed per
 *   minute), S (spindle rpm, whole numbers), M3/M4/M5, M2/M30, comments in
 *   ( ) and after ';', and a line holding only '%'. Any other word (G18,
 *   G28, T, R, ...) is reported as UNKNOWN_WORD and the job is REJECTED.
 *
 *   Numbers are parsed exactly from the decimal text into integer
 *   micrometres (um). 1 mm = 1000 um, 1 inch = 25400 um exactly. A value
 *   that is not a whole number of um (e.g. 0.0001 mm, or 0.001 in = 25.4
 *   um) is REJECTED as too precise; it is never rounded. Feeds are in
 *   um/min, spindle speeds in rpm.
 *
 *   Checks (severity in brackets):
 *     - travel envelope: every end point inside the profile box [REJECT];
 *       arcs: an exact arc point outside [REJECT], only the conservative
 *       arc bounding box outside [HOLD]
 *     - feed: cutting moves (G1/G2/G3) need a feed set, <= feed_max and
 *       >= feed_min_cut [REJECT]
 *     - spindle: speed in [spindle_min, spindle_max] when turned on or
 *       changed while on [REJECT]; a cutting move with spindle off [REJECT]
 *     - rapids (G0): moving sideways (X or Y changes) with any point
 *       below safe_z, or descending to an end below safe_z [REJECT]; a
 *       straight vertical retract upward is allowed; segment touching a
 *       keep-out box [REJECT]
 *     - fixtures: any move segment touching a fixture box [REJECT] (exact
 *       integer/rational slab test, boxes are closed so touching a face or
 *       a corner counts); arcs: an exact arc point inside [REJECT], only
 *       the conservative bounding box overlapping [HOLD] (four-valued BOTH:
 *       the bound cannot decide, so a person looks)
 *     - arcs: centre offset not zero, start and end radius^2 equal within
 *       arc_r2_tol (um^2) [REJECT]
 *     - modal state: motion before G20/G21 and G90/G91 are declared [HOLD,
 *       mm/absolute assumed for the rest of the check]; axis words before
 *       any motion mode [REJECT]; no M2/M30 [HOLD]; content after M2/M30
 *       [HOLD]
 *     - malformed text, conflicting words on one line, line too long, too
 *       many lines, coordinates beyond +-MFG_GC_COORD_MAX [REJECT]
 *
 * HONEST LIMITS
 *   - The tool is a point (its tip path). Tool radius, tool length, holder
 *     and spindle geometry are NOT modelled: inflate fixture boxes by the
 *     tool's size yourself. No cutter compensation (G41/G42), no work
 *     offsets (G54..), no canned cycles, no R-form arcs, no other planes.
 *   - Arc extents use a conservative bounding box (radius from an exact
 *     integer square root, rounded outward). It can HOLD a safe arc; it
 *     cannot pass an arc whose bounding box touches a box.
 *   - Feed limits are checked on the programmed feed, not on the machine's
 *     real acceleration or overrides. Robot programs are not parsed yet.
 *   - A RELEASE_OK verdict means "no issue found by these checks", not
 *     "safe". The machine's interlocks and a person stay in charge.
 *
 * Freestanding, integer-only, bounded, no allocation, no libc.
 */
#ifndef ZXV_MFG_GCODE_H
#define ZXV_MFG_GCODE_H

#include "mfg_common.h"

#define MFG_GC_MAX_LINE   256u         /* bytes per line, excluding newline */
#define MFG_GC_MAX_LINES  100000u      /* lines per program */
#define MFG_GC_MAX_ISSUES 64u          /* stored issues; more are counted */
#define MFG_GC_MAX_BOXES  16u          /* fixture + keep-out boxes */
#define MFG_GC_COORD_MAX  500000000LL  /* +-500 m in um: keeps products in int64 */
#define MFG_GC_FEED_MAX   1000000000LL /* 1 km/min in um/min: sanity cap */
#define MFG_GC_RPM_MAX    1000000LL    /* sanity cap on S words */

typedef enum {
    MFG_BOX_FIXTURE = 0, /* no move of any kind may touch it */
    MFG_BOX_KEEPOUT = 1  /* no rapid (G0) may touch it */
} mfg_box_kind_t;

typedef struct {
    int64_t lo[3]; /* x, y, z in um, closed box: lo <= p <= hi */
    int64_t hi[3];
    uint8_t kind; /* mfg_box_kind_t */
} mfg_box_t;

typedef struct {
    int64_t env_lo[3]; /* travel envelope, um */
    int64_t env_hi[3];
    int64_t start[3];     /* tool position when the program starts, um */
    int64_t safe_z;       /* rapids may not travel or descend below it, um */
    int64_t feed_min_cut; /* um/min, >= 1 */
    int64_t feed_max;     /* um/min */
    int64_t spindle_min;  /* rpm, >= 1 */
    int64_t spindle_max;  /* rpm */
    int64_t arc_r2_tol;   /* allowed |r_start^2 - r_end^2|, um^2, >= 0 */
    uint32_t nbox;
    mfg_box_t box[MFG_GC_MAX_BOXES];
} mfg_gc_profile_t;

typedef enum {
    MFG_ISS_MALFORMED = 1,    /* bad number, bad character, comment */
    MFG_ISS_UNKNOWN_WORD = 2, /* word or code not supported */
    MFG_ISS_CONFLICT = 3,     /* two words of one group on a line */
    MFG_ISS_TOO_PRECISE = 4,  /* value is not a whole number of um */
    MFG_ISS_LINE_TOO_LONG = 5,
    MFG_ISS_TOO_MANY_LINES = 6,  /* checking stopped */
    MFG_ISS_COORD_RANGE = 7,     /* beyond +-MFG_GC_COORD_MAX; checking stopped */
    MFG_ISS_MODAL_UNSET = 8,     /* units or distance mode not declared */
    MFG_ISS_MOTION_UNSET = 9,    /* axis words with no motion mode */
    MFG_ISS_ENVELOPE = 10,       /* definitely outside the travel envelope */
    MFG_ISS_ENVELOPE_MAYBE = 11, /* arc bounding box leaves the envelope */
    MFG_ISS_FEED_UNSET = 12,
    MFG_ISS_FEED_HIGH = 13,
    MFG_ISS_FEED_LOW = 14,
    MFG_ISS_SPINDLE_RANGE = 15,
    MFG_ISS_CUT_SPINDLE_OFF = 16,
    MFG_ISS_RAPID_LOW = 17, /* rapid below safe_z */
    MFG_ISS_RAPID_KEEPOUT = 18,
    MFG_ISS_FIXTURE = 19,       /* definite collision with a fixture box */
    MFG_ISS_FIXTURE_MAYBE = 20, /* arc bounding box overlaps a fixture */
    MFG_ISS_ARC_CENTRE = 21,    /* I and J both missing or both zero */
    MFG_ISS_ARC_RADIUS = 22,    /* start/end radius mismatch */
    MFG_ISS_NO_END = 23,        /* no M2/M30 */
    MFG_ISS_AFTER_END = 24      /* words after M2/M30 */
} mfg_gc_issue_code_t;

typedef struct {
    uint32_t line;       /* 1-based physical line in the buffer, 0 = whole program */
    int64_t nword;       /* value of the N word on that line, -1 if none */
    uint8_t code;        /* mfg_gc_issue_code_t */
    uint8_t severity;    /* MFG_HOLD or MFG_REJECT */
    uint8_t box;         /* box index for box issues, else 0 */
    const char *message; /* static text */
} mfg_gc_issue_t;

typedef struct {
    uint8_t verdict;  /* mfg_verdict_t: worst severity seen (RELEASE_OK if none) */
    uint32_t nissue;  /* stored issues (<= MFG_GC_MAX_ISSUES) */
    uint32_t dropped; /* issues found but not stored (still counted in verdict) */
    uint32_t lines;   /* physical lines read */
    uint32_t moves;   /* motion blocks checked */
    int64_t end_pos[3];
    mfg_gc_issue_t issue[MFG_GC_MAX_ISSUES];
} mfg_gc_result_t;

/* Validate a profile: envelope ordered and within +-MFG_GC_COORD_MAX, start
 * inside the envelope, feed/spindle ranges ordered and >= 1, boxes ordered
 * and of a known kind, nbox <= MFG_GC_MAX_BOXES. */
mfg_status_t mfg_gc_profile_check(const mfg_gc_profile_t *p);

/* Check a program. text may contain len bytes (NUL inside is malformed);
 * len 0 is an empty program. Returns MFG_OK and fills *out (including the
 * verdict) on success; errors only for NULL arguments, len > the line/size
 * bound (MFG_GC_MAX_LINES * (MFG_GC_MAX_LINE + 2)) or an invalid profile. */
mfg_status_t mfg_gc_check(const char *text, size_t len, const mfg_gc_profile_t *p,
                          mfg_gc_result_t *out);

/* Exact decimal -> integer micrometres. mm_or_inch: 0 = millimetres,
 * 1 = inches. s/len is the number text only (optional sign, digits, at most
 * one '.', at least one digit, at most 9 fraction digits). Returns MFG_OK,
 * MFG_ERR_RANGE for malformed text, MFG_ERR_OVERFLOW if too large, or
 * MFG_ERR_STATE if the value is not a whole number of um. */
mfg_status_t mfg_gc_parse_um(const char *s, size_t len, int units_inch, int64_t *out_um);

/* Exact segment/closed-box intersection test (slab method on rationals).
 * Coordinates must be within +-MFG_GC_COORD_MAX. Returns 1 if the segment
 * a..b touches the box, 0 if not, MFG_ERR_RANGE / MFG_ERR_NULL on bad input. */
int mfg_gc_segment_hits_box(const int64_t a[3], const int64_t b[3], const mfg_box_t *box);

/* floor(sqrt(v)) exactly, for any uint64. */
uint64_t mfg_isqrt_u64(uint64_t v);

#endif /* ZXV_MFG_GCODE_H */
