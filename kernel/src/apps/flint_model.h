/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* flint_model.h — Flint, the terminal, as a model (zxm.h): the session a
 * terminal window shows. It reads a command line with zxsh's word rules,
 * runs the read-only builtins over zxvd (files.* and sys.*), and keeps what
 * they print in a bounded scrollback and every command in a bounded
 * history. No drawing, no allocation, no floats.
 *
 *   FL1 GRID. The window's columns and rows are whole cells: floor(pixels /
 *       cell), for a span of 0..65535 pixels and a cell of 1..256 pixels
 *       (blocks/flint/cells.kron and cells.cpp). Outside that, or with no
 *       whole cell, the size is refused and the grid stays as it was.
 *   FL2 SCROLLBACK. A ring of lines inside two limits, 24..10000 lines and
 *       4096..1048576 bytes of text (blocks/flint/scroll.eve). A new line
 *       first drops the oldest lines until it fits, so the newest are kept;
 *       a line longer than FLINT_LINE_MAX bytes is cut on a UTF-8 boundary.
 *   FL3 HISTORY. Every command run is recorded: its number, when it ran
 *       (Unix seconds, from the view's clock), its exit status and its
 *       length (blocks/flint/history.myra). A command whose record would
 *       not fit is refused and runs nothing.
 *   FL4 STATUS. As sh counts it: 0 ran, 1 refused (zxvd said no), 2 misused
 *       (bad quoting, a pipe or redirection, wrong arguments), 127 no such
 *       command. zxvd has no method that runs a zxsh line or a process, so
 *       only the builtins below run here.
 *   FL5 PATHS. The Hoard is the terminal's "/". A path is joined to the
 *       working folder; "." and ".." are resolved here (".." at "/" stays
 *       at "/"), and zxvd judges what is left. */
#ifndef FLINT_MODEL_H
#define FLINT_MODEL_H

#include "zxm.h"

#define FLINT_REFUSED    (-1)
#define FLINT_PIXELS_MAX 65535
#define FLINT_CELL_MAX   256
#define FLINT_LINES_MIN  24u
#define FLINT_LINES_MAX  10000u
#define FLINT_BYTES_MIN  4096u
#define FLINT_BYTES_MAX  1048576u
#define FLINT_LINES_DEF  2000u
#define FLINT_BYTES_DEF  262144u
#define FLINT_LINE_MAX   1024u /* bytes of one scrollback line */
#define FLINT_CMD_MAX    1024u /* bytes of one command line */
#define FLINT_ARGS_MAX   16u
#define FLINT_PATH_MAX   1024u
#define FLINT_HISTORY    256u /* commands kept */
#define FLINT_SEQ_MAX    INT64_C(999999999)
#define FLINT_STAMP_MAX  INT64_C(999999999999)
#define FLINT_CAT_MAX    65536u /* bytes cat shows of one file */
#define FLINT_CHUNK      16384u /* bytes asked of files.read at once */

enum { FLINT_ST_OK = 0, FLINT_ST_REFUSED = 1, FLINT_ST_MISUSE = 2, FLINT_ST_NOCMD = 127 };

/* flint_parse's results besides a count of words */
enum {
    FLINT_P_QUOTE = -1, /* a quote not closed, or a backslash at the end */
    FLINT_P_MANY = -2,  /* more than FLINT_ARGS_MAX words */
    FLINT_P_LONG = -3,  /* longer than FLINT_CMD_MAX bytes */
    FLINT_P_CTRL = -4,  /* a control character other than a tab */
    FLINT_P_OP = -5,    /* an unquoted | ; & < > or ( ): zxsh's, not run here */
};

typedef struct {
    uint32_t argc;
    char *argv[FLINT_ARGS_MAX];
    char buf[FLINT_CMD_MAX + FLINT_ARGS_MAX]; /* the words, each ending in 0 */
} flint_words_t;

typedef struct {
    uint64_t at; /* where its bytes start in the ring */
    uint32_t len;
} flint_line_t;

typedef struct {
    int64_t seq, stamp;
    int32_t status;
    uint32_t len;
    char text[FLINT_CMD_MAX + 1];
} flint_hist_t;

typedef struct {
    zxm_link_t *link;
    char cwd[FLINT_PATH_MAX + 1]; /* inside the Hoard, no leading '/'; "" = "/" */
    int64_t cols, rows;           /* the grid (FL1); 0 until a size is set */
    /* FL2: the scrollback */
    uint32_t lines_limit, bytes_limit;
    char ring[FLINT_BYTES_MAX];
    uint64_t head, tail; /* bytes written and dropped; head - tail in use */
    flint_line_t line[FLINT_LINES_MAX];
    uint32_t first, nlines;
    /* FL3: the history */
    flint_hist_t hist[FLINT_HISTORY];
    uint32_t hfirst, nhist;
    int64_t next_seq;
    int32_t last_status;
    /* work space */
    flint_words_t words;
    char out[FLINT_LINE_MAX + 1];
    uint32_t outn;
    char text[FLINT_CAT_MAX + 1];
} flint_t;

/* FL1: whole cells across a span, or FLINT_REFUSED (cells.cpp's rule). */
int64_t flint_cells(int64_t pixels, int64_t cell);
/* FL2: scrollback limits inside their ranges (scroll.eve's rule). */
bool flint_scroll_ok(int64_t lines, int64_t bytes);
/* FL3: a history record that fits its digits (history.myra's rule). */
bool flint_history_ok(int64_t seq, int64_t stamp, int64_t status, int64_t bytes);
/* Share in use, x100 (used_share.used_x100's rule), or FLINT_REFUSED. */
int64_t flint_used_x100(uint64_t total, uint64_t left);

/* Split a command line into words with zxsh's rules (see flint.zxapp):
 * the number of words, or a FLINT_P_* code. */
int32_t flint_parse(const char *line, uint32_t n, flint_words_t *w);
/* FL5: join `arg` to the folder `cwd` into out; ZXM_E_ARG when too long. */
int flint_resolve(const char *cwd, const char *arg, char *out, uint32_t cap);

/* Default limits, the Hoard's top as the working folder, no grid yet. */
void flint_init(flint_t *f, zxm_link_t *link);
/* FL2: new limits; the oldest lines go until both hold. ZXM_E_ARG outside. */
int flint_set_limits(flint_t *f, int64_t lines, int64_t bytes);
/* FL1: the window's size in pixels and its cell's; ZXM_E_ARG when refused. */
int flint_resize(flint_t *f, int64_t width, int64_t height, int64_t cell_w, int64_t cell_h);

/* Run one command line at Unix time `now`: its prompt and output go into
 * the scrollback and it is recorded (FL3). Returns its status (FL4), or a
 * ZXM_E_* code when it was refused before it ran (nothing recorded). */
int flint_run(flint_t *f, const char *line, int64_t now);

/* Text into the scrollback, one line per '\n' (a view's own notices). */
void flint_print(flint_t *f, const char *s, uint32_t n);
void flint_clear(flint_t *f);
uint32_t flint_lines(const flint_t *f);
uint64_t flint_bytes(const flint_t *f);
/* Line i (0 = oldest) into out: its length, or -1 when there is none or it
 * does not fit cap (one byte is kept for the ending 0). */
int32_t flint_line(const flint_t *f, uint32_t i, char *out, uint32_t cap);
/* The first line the grid's rows show, so the newest is at the bottom. */
uint32_t flint_top(const flint_t *f);

uint32_t flint_history_count(const flint_t *f);
const flint_hist_t *flint_history_at(const flint_t *f, uint32_t i); /* 0 = oldest */

#endif
