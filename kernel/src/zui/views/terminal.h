/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* terminal.h — the Terminal's native screen (zv.h): a scrollback of what
 * the shell printed, drawn through a VT100 / ANSI subset, and an input line
 * whose Enter hands the line to a shell the caller supplies.
 *
 *   T1 THE SHELL IS THE CALLER'S. The view runs nothing itself: Enter echoes
 *      the line (after a muted "$ ") and calls term_shell_fn, which answers
 *      by writing bytes back with term_write, now or later (a process's
 *      output as it arrives). No shell: the line is echoed and refused.
 *   T2 THE SUBSET. UTF-8 text; CR, LF, BS, TAB (every 8 columns), BEL
 *      (ignored); ESC c (reset); CSI m (SGR: 0 reset, 1 bold, 22, 4
 *      underline, 24, 7 inverse, 27, 30..37 and 90..97 the sixteen colours,
 *      39 default; backgrounds 40..49, 100..107 and 38/48 extended colours
 *      are read and ignored), K (erase in line 0, 1, 2), J (erase in screen
 *      0, 1, 2, 3), H and f (cursor position on the TERM_ROWS screen), A, B,
 *      C, D (cursor moves), G (column). Any other CSI, and OSC strings (to
 *      BEL or ESC \), are consumed and dropped, never printed.
 *   T3 THE PALETTE. The sixteen colours are colour roles (zui_palette.h C1),
 *      so output follows the scheme and keeps its contrast: red is ALERT,
 *      green OK, yellow GOLD, blue and cyan INFO, white TEXT, black FAINT;
 *      the bright ones a step lighter. Inverse draws a raised block. No
 *      background colour is ever painted: the ground stays the ground.
 *   T4 SCROLLBACK. TERM_LINES lines; the oldest fall off the top. The view
 *      follows the newest output unless the person has scrolled up.
 *   T5 Fixed size, no allocation, integer only. */
#ifndef ZXV_VIEW_TERMINAL_H
#define ZXV_VIEW_TERMINAL_H

#include "zv.h"

#define TERM_COLS      132u
#define TERM_ROWS      24u  /* the screen CSI H addresses */
#define TERM_LINES     256u /* the scrollback, with the screen */
#define TERM_INPUT_MAX 256u

typedef struct term_view term_view_t;
/* T1: run line; answer with term_write (now or later). */
typedef void (*term_shell_fn)(void *ctx, term_view_t *t, const char *line);

/* A cell: the code point (bits 0..20), the colour (21..25: 0 default,
 * 1..8 = SGR 30..37, 9..16 = SGR 90..97), bold (26), underline (27),
 * inverse (28). */
#define TERM_CP(c)   ((c) & 0x1FFFFFu)
#define TERM_FG(c)   (((c) >> 21) & 31u)
#define TERM_BOLD    (1u << 26)
#define TERM_UNDER   (1u << 27)
#define TERM_INVERSE (1u << 28)

struct term_view {
    zv_shell_t sh;
    zui_id_t scroll, screen, prompt, input, clear;
    char in_buf[TERM_INPUT_MAX];
    term_shell_fn shell;
    void *shell_ctx;
    /* the scrollback, a ring of lines */
    uint32_t cell[TERM_LINES][TERM_COLS];
    uint32_t head, n;  /* the oldest line; how many */
    uint32_t row, col; /* the cursor: a line (0 = oldest) and a column */
    uint32_t attr;     /* SGR state, in cell bits 21..28 */
    uint32_t dropped;  /* lines fallen off the top */
    /* the parser */
    uint8_t state; /* ground, ESC, CSI, OSC, OSC-ESC */
    uint32_t par[8], npar;
    bool par_any, priv;
    uint32_t u8, u8_left; /* a UTF-8 sequence in progress */
    bool follow;          /* T4: keep the newest line in view */
};

void term_view_build(term_view_t *t, zui_ui_t *ui, term_shell_fn shell, void *ctx);
void term_view_sync(term_view_t *t);
bool term_view_action(term_view_t *t, zui_id_t w, zui_action_t a, int32_t arg);
/* T2: bytes from the shell, any split (sequences may straddle calls). */
void term_write(term_view_t *t, const char *bytes, uint32_t n);
void term_puts(term_view_t *t, const char *s);
void term_reset(term_view_t *t); /* ESC c */
uint32_t term_lines(const term_view_t *t);
/* Line i (0 = oldest kept) as UTF-8 without trailing blanks; its length. */
uint32_t term_line_text(const term_view_t *t, uint32_t i, char *out, uint32_t cap);
uint32_t term_cell(const term_view_t *t, uint32_t line, uint32_t col);
/* T3: the colour role a cell is drawn in. */
zui_color_role_t term_role(uint32_t cell);

#endif /* ZXV_VIEW_TERMINAL_H */
