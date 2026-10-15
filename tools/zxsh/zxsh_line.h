/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxsh_line.h — line editing for the interactive shell, with the keys every
 * terminal user already knows.
 *
 *   L1 KEYS. Left/Right, Home/End, Ctrl-A/E/B/F, Alt-B/F (word), Backspace,
 *      Delete, Ctrl-D (delete, or end of input on an empty line), Ctrl-K/U/W
 *      (kill to end / to start / word), Ctrl-Y (yank the last kill), Ctrl-T
 *      (transpose), Ctrl-L (clear screen), Ctrl-C (drop the line), Up/Down
 *      (history), Ctrl-R (search history backwards; Enter takes the match),
 *      Tab (complete; a second Tab lists the choices).
 *   L2 DISPLAY. One row; a line longer than the terminal scrolls sideways
 *      around the cursor. The prompt may hold colour escapes: only visible
 *      characters count. UTF-8 is kept whole; every character counts as one
 *      column (wide characters may misalign the cursor).
 *   L3 FALLBACK. When stdin is not a terminal, or TERM is "dumb", the line is
 *      read with no editing at all.
 */
#ifndef ZXSH_LINE_H
#define ZXSH_LINE_H

#include <stddef.h>

/* Completion: given the line and the cursor, return the byte offset where
 * the word being completed starts and fill up to max choices (each a
 * malloc'd string the editor frees). A choice ending in '/' is a directory
 * and gets no trailing space. */
typedef int (*zline_complete_fn)(void *ctx, const char *line, size_t cursor, char **choices,
                                 int max, size_t *word_start);

#define ZLINE_EOF  (-1)
#define ZLINE_INTR (-2)

/* Read one line (without its newline) into buf. Returns its length,
 * ZLINE_EOF at end of input, or ZLINE_INTR when Ctrl-C dropped it. */
int zline_read(const char *prompt, char *buf, size_t max, zline_complete_fn comp, void *ctx);

/* History, oldest first; zline_history_add skips empty and repeated lines. */
void zline_history_add(const char *line);
int zline_history_count(void);

#endif /* ZXSH_LINE_H */
