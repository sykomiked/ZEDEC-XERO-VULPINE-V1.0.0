/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* test_xedit.c — the X-EDIT gap buffer: text, lines and cursor motion stay
 * right when the gap sits in the middle of the text, and nothing is written
 * past a caller's buffer. */
#include <stdio.h>
#include <string.h>
#include "xedit.h"

static int failures = 0;
#define CHECK(c, m)                                                                                \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            printf("[FAIL] %s\n", m);                                                              \
            failures++;                                                                            \
        } else                                                                                     \
            printf("[PASS] %s\n", m);                                                              \
    } while (0)

static xedit_t g_e; /* ~600 KB */

int main(void)
{
    xedit_init(&g_e);
    char longname[400];
    memset(longname, 'n', sizeof longname - 1);
    longname[sizeof longname - 1] = 0;
    int32_t idx = xedit_buffer_create(&g_e, longname);
    CHECK(idx == 0, "a buffer with an over-long filename is created");
    xedit_buffer_t *b = &g_e.buffers[0];
    CHECK(strlen(b->filename) == XEDIT_MAX_FILENAME - 1, "...and the name is truncated in place");

    xedit_insert(b, "alpha\nbeta\ngamma\n", 17);
    /* put the gap in the middle: cursor to the start of "beta" and insert */
    xedit_cursor_goto(b, 6);
    xedit_insert(b, "X", 1);
    char t[64], line[64];
    xedit_get_text(b, t, sizeof t);
    CHECK(strcmp(t, "alpha\nXbeta\ngamma\n") == 0, "text reads back across the gap");
    CHECK(b->line_count == 4 && b->line_starts[1] == 6 && b->line_starts[2] == 12,
          "line starts are logical positions, not gap-relative indices");
    xedit_get_line(b, 2, line, sizeof line);
    CHECK(strcmp(line, "gamma") == 0, "a line AFTER the gap reads back correctly");
    xedit_get_line(b, 1, line, sizeof line);
    CHECK(strcmp(line, "Xbeta") == 0, "the line holding the gap reads back correctly");

    /* deleting a newline removes its line */
    xedit_cursor_goto(b, 6);
    xedit_delete_back(b);
    xedit_get_text(b, t, sizeof t);
    CHECK(strcmp(t, "alphaXbeta\ngamma\n") == 0 && b->line_count == 3,
          "delete_back of a newline joins the lines and updates the line table");
    xedit_cursor_goto(b, xedit_buffer_size(b));
    CHECK(xedit_delete_forward(b) == -1, "delete_forward at the end of the text is refused");
    xedit_cursor_goto(b, 10);
    CHECK(xedit_delete_forward(b) == 0 && b->line_count == 2,
          "delete_forward of a newline joins the lines");

    /* never write past the caller's buffer */
    char small[5];
    memset(small, 0x7E, sizeof small);
    uint32_t n = xedit_get_text(b, small, sizeof small);
    CHECK(n == 4 && small[4] == '\0', "get_text keeps the NUL inside a short buffer");
    n = xedit_get_line(b, 0, small, sizeof small);
    CHECK(n == 4 && small[4] == '\0', "get_line keeps the NUL inside a short buffer");

    /* the content hash follows the text, not the gap position */
    xedit_update_hash(b);
    uint8_t h1[XEDIT_HASH_SIZE];
    memcpy(h1, b->cid, sizeof h1);
    xedit_cursor_goto(b, 0);
    xedit_insert(b, "", 0); /* moves the gap, changes no text */
    xedit_update_hash(b);
    CHECK(memcmp(h1, b->cid, sizeof h1) == 0, "the hash depends on the text, not the gap");
    CHECK(xedit_verify_integrity(b), "verify_integrity agrees with the stored hash");

    printf("\n%s: %d failure(s)\n", failures ? "*** FAILED ***" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
