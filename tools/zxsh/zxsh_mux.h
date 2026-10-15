/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxsh_mux.h — sub-terminals: several shells and commands side by side.
 *
 * From the main terminal, `split`, `tab` and `together` open sub-terminals
 * (panes) that run at the same time, each with its own pseudo-terminal, its
 * own title bar and its own verdict letter (T F B N, ! when forbidden). The
 * keys are the familiar multiplexer keys: Ctrl-B then % " arrows o c n p
 * 1-9 x q ? (zxsh_mux.c M5).
 *
 * zmux_request works in one of three modes:
 *   CLIENT    $ZXSH_MUX_SOCK is set (this shell runs inside a pane): send the
 *             request to the multiplexer that owns the socket and wait for
 *             its reply ("together" waits until its panes have finished).
 *   SERVER    stdin and stdout are terminals: this process becomes the
 *             multiplexer until every pane is closed (or Ctrl-B q), then
 *             restores the terminal exactly and returns.
 *   NO TERMINAL  "together" runs its commands as child processes at the same
 *             time, prefixing each output line with "[k] " (k = 1-based
 *             command index), then prints "[k] L  CMD" per command; "split"
 *             and "tab" return NEITHER ("sub-terminals need a terminal").
 *
 * RULE C (combined verdict). A finished command's tag is the tag its pane
 * reported with OSC 7770 when one arrived, else zmux_tag_from_status of its
 * exit: 0 TRUE, 1 FALSE, 2 NEITHER (syntax), 3 FALSE/FORBIDDEN, 4 BOTH,
 * 5 NEITHER, any other code FALSE (all neutral); killed by SIGSEGV SIGBUS
 * SIGILL SIGFPE SIGABRT SIGSYS SIGTRAP -> FALSE/FORBIDDEN; any other signal
 * NEITHER/entropic. A crash is never covered by a report. zmux_combine:
 * any forbidden tag -> FALSE/FORBIDDEN; else all bases equal -> that base,
 * NEGENTROPIC when n >= 2 (the witnesses agree), NEUTRAL when n == 1; else
 * (they disagree) BOTH/ENTROPIC. n == 0 is NEITHER/neutral. Every result
 * has phase MANIFEST.
 */
#ifndef ZXSH_MUX_H
#define ZXSH_MUX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "lpres_hex.h"

typedef struct {
    const char *self; /* absolute path of the zxsh binary, used to start panes */
    const char *home; /* $ZXSH_HOME directory, where the control socket lives */
} zmux_cfg_t;

#define ZMUX_MAX_CMDS 16

/* kind: "split" (dir 'h' = new pane to the right, 'v' = below), "tab", or
 * "together". cmds/n: commands for the new panes; n == 0 for split/tab means
 * a plain shell pane. Returns: split/tab -> TRUE/neutral once opened,
 * NEITHER with *why on failure; together -> the combined verdict (rule C).
 * why may be written (NUL-terminated, <= whymax bytes; may be NULL). */
lpx_t zmux_request(const zmux_cfg_t *cfg, const char *kind, char dir, const char *const *cmds,
                   int n, char *why, uint32_t whymax);

/* Rule C. */
lpx_t zmux_combine(const lpx_t *tags, int n);
lpx_t zmux_tag_from_status(int wait_status);

/* ---- the VT emulator of one pane (exposed for tests) ---------------- */

#define ZVT_BOLD      0x01u
#define ZVT_DIM       0x02u
#define ZVT_ITALIC    0x04u
#define ZVT_UNDERLINE 0x08u
#define ZVT_REVERSE   0x10u

/* A colour: 0 = default, ZVT_IDX | n (0..255), ZVT_RGB | 0xRRGGBB. */
#define ZVT_IDX 0x01000000u
#define ZVT_RGB 0x02000000u

/* One cell holds one UTF-8 character (1..4 bytes). Every character is
 * treated as one column wide: wide (CJK, emoji) and combining characters
 * are not laid out correctly. */
typedef struct {
    uint8_t ch[4];
    uint8_t len;
    uint8_t attr;
    uint32_t fg, bg;
} zvt_cell_t;

#define ZVT_NPAR 16

typedef struct {
    int rows, cols;
    zvt_cell_t *buf[2]; /* main and alternate screen */
    int scr;            /* which one is shown */
    int cx, cy;         /* cursor, 0-based */
    bool wrapnext, autowrap, cursor_on;
    int top, bot; /* scroll region, inclusive rows */
    zvt_cell_t pen;
    int sx, sy; /* saved cursor (ESC 7, CSI s, ?1049) */
    zvt_cell_t spen;
    /* parser */
    uint8_t st, priv, inter, u8n, u8need;
    uint8_t u8[4];
    int par[ZVT_NPAR];
    int npar;
    char osc[256];
    uint32_t oscn;
    /* results */
    char title[64]; /* OSC 0 / OSC 2 */
    int tag;        /* last OSC 7770 report (an lpx_t), -1 = none */
    char reply[64]; /* bytes to write back into the pane (DSR, DA) */
    uint32_t replen;
} zvt_t;

int zvt_init(zvt_t *v, int rows, int cols); /* 0, or -1 when out of memory */
void zvt_free(zvt_t *v);
void zvt_feed(zvt_t *v, const char *s, size_t n);
int zvt_resize(zvt_t *v, int rows, int cols);
/* The cell at (row, col) of the shown screen; NULL when out of range. */
const zvt_cell_t *zvt_cell(const zvt_t *v, int row, int col);

/* ---- the layout of one tab: a binary split tree (exposed for tests) -- */

#define ZMUX_MAXP  16 /* panes per tab */
#define ZMUX_NODES (2 * ZMUX_MAXP)

typedef struct {
    int x, y, w, h;
} zmux_rect_t;

typedef struct {
    bool used;
    char dir;         /* 0 for a leaf, 'h' (side by side) or 'v' (stacked) */
    int parent, a, b; /* node indices, -1 for none */
    int wa, wb;       /* split weights of a and b */
    int pane;         /* leaf: the pane id */
} zmux_node_t;

typedef struct {
    zmux_node_t n[ZMUX_NODES];
    int root; /* -1 when empty */
} zmux_tree_t;

void zmux_tree_init(zmux_tree_t *t);
/* Add `pane`: in an empty tree it becomes the root; otherwise the leaf of
 * `at` is halved ('h' new pane to the right, 'v' below). 0, or -1 when `at`
 * is missing or the tree is full. */
int zmux_tree_add(zmux_tree_t *t, int at, char dir, int pane);
/* Build an empty tree holding panes[0..n-1] tiled as evenly as possible. */
int zmux_tree_tile(zmux_tree_t *t, const int *panes, int n);
/* Remove a pane; its sibling takes the space. Leaves left, -1 if absent. */
int zmux_tree_remove(zmux_tree_t *t, int pane);
/* Leaf rectangles of `area` in leaf order (each includes its title row),
 * and the one-cell border lines between them when borders != NULL (room for
 * max entries). Panes, borders cover the area exactly once. Returns the
 * number of leaves. */
int zmux_tree_rects(const zmux_tree_t *t, zmux_rect_t area, int *panes, zmux_rect_t *rects, int max,
                    zmux_rect_t *borders, int *nborders);

#endif /* ZXSH_MUX_H */
